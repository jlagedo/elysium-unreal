#include "Visual/ElysiumNpcClips.h"

#include "ElysiumContentPaths.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumClips, Log, All);

namespace ElysiumActivity
{
	const TCHAR* Idle = TEXT("ACT_IDLE");
	const TCHAR* Disposition = TEXT("ACT_DISPOSITION");
}

namespace
{
	void ReadStringArray(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field,
		TArray<FString>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Object->TryGetArrayField(Field, Values) || Values == nullptr)
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			FString Text;
			if (Value.IsValid() && Value->TryGetString(Text) && !Text.IsEmpty())
			{
				Out.Add(Text);
			}
		}
	}

	bool ReadJsonFile(const FString& Path, TSharedPtr<FJsonObject>& OutRoot, FString& OutError)
	{
		FString Raw;
		if (!FFileHelper::LoadFileToString(Raw, *Path))
		{
			OutError = FString::Printf(TEXT("not found: %s"), *Path);
			return false;
		}
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
		if (!FJsonSerializer::Deserialize(Reader, OutRoot) || !OutRoot.IsValid())
		{
			OutError = FString::Printf(TEXT("malformed JSON: %s"), *Path);
			return false;
		}
		return true;
	}

	void ReadIndexGroup(const TSharedPtr<FJsonObject>& Root, const TCHAR* Field,
		TMap<FString, FElysiumNpcIndexEntry>& Out)
	{
		const TSharedPtr<FJsonObject>* Group = nullptr;
		if (!Root->TryGetObjectField(Field, Group) || Group == nullptr)
		{
			return;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Group)->Values)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(Obj) || Obj == nullptr)
			{
				continue;
			}
			FElysiumNpcIndexEntry E;
			(*Obj)->TryGetStringField(TEXT("glb"), E.Glb);
			(*Obj)->TryGetStringField(TEXT("model"), E.Model);
			(*Obj)->TryGetNumberField(TEXT("bones"), E.Bones);
			(*Obj)->TryGetNumberField(TEXT("clips"), E.ClipCount);
			(*Obj)->TryGetStringField(TEXT("facial"), E.Facial);
			(*Obj)->TryGetNumberField(TEXT("morphs"), E.MorphCount);
			(*Obj)->TryGetStringField(TEXT("eyes"), E.Eyes);
			(*Obj)->TryGetNumberField(TEXT("eyeballs"), E.EyeballCount);
			ReadStringArray(*Obj, TEXT("split_bones"), E.SplitRotationBones);
			(*Obj)->TryGetStringField(TEXT("procedural"), E.Procedural);
			(*Obj)->TryGetNumberField(TEXT("procedural_bones"), E.ProceduralBones);
			(*Obj)->TryGetStringField(TEXT("blends"), E.Blends);
			(*Obj)->TryGetNumberField(TEXT("blend_grids"), E.BlendGrids);
			Out.Add(Pair.Key, MoveTemp(E));
		}
	}
}

// --- FElysiumNpcClipSet ----------------------------------------------------------------

bool FElysiumNpcClipSet::Load(const FString& InStem, FString& OutError)
{
	Stem = InStem;
	Clips.Reset();

	TSharedPtr<FJsonObject> Root;
	if (!ReadJsonFile(FElysiumContentPaths::NpcClips(InStem), Root, OutError))
	{
		return false;
	}

	// The slice interns its owner stems and activity literals into two arrays and stores each
	// clip as [owner_i, activity_i, weight, flags, frames, fps] — the strings repeat across
	// ~1,540 rows, and the owner column especially (31 distinct stems) pays for the indirection.
	TArray<FString> Owners, Activities;
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Root->TryGetArrayField(TEXT("owners"), Arr))
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr) { Owners.Add(V->AsString()); }
	}
	if (Root->TryGetArrayField(TEXT("activities"), Arr))
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr) { Activities.Add(V->AsString()); }
	}
	if (Owners.IsEmpty())
	{
		OutError = TEXT("slice carries no `owners` intern table");
		return false;
	}

	const TSharedPtr<FJsonObject>* ClipObj = nullptr;
	if (!Root->TryGetObjectField(TEXT("clips"), ClipObj) || ClipObj == nullptr)
	{
		OutError = TEXT("slice carries no `clips` map");
		return false;
	}

	Clips.Reserve((*ClipObj)->Values.Num());
	int32 Malformed = 0;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*ClipObj)->Values)
	{
		const TArray<TSharedPtr<FJsonValue>>* Row = nullptr;
		if (!Pair.Value.IsValid() || !Pair.Value->TryGetArray(Row) || Row->Num() < 6)
		{
			++Malformed;
			continue;
		}
		const int32 OwnerIdx = static_cast<int32>((*Row)[0]->AsNumber());
		const int32 ActIdx = static_cast<int32>((*Row)[1]->AsNumber());
		if (!Owners.IsValidIndex(OwnerIdx))
		{
			++Malformed;
			continue;
		}
		FElysiumNpcClip Clip;
		Clip.Owner = Owners[OwnerIdx];
		Clip.Activity = Activities.IsValidIndex(ActIdx) ? Activities[ActIdx] : FString();
		Clip.Weight = static_cast<int32>((*Row)[2]->AsNumber());
		Clip.Flags = static_cast<int32>((*Row)[3]->AsNumber());
		Clip.Frames = static_cast<int32>((*Row)[4]->AsNumber());
		Clip.Fps = static_cast<float>((*Row)[5]->AsNumber());
		// Trailing and optional: a slice written before the field existed keeps the struct's own
		// 0.2, which is the value 5,762 of the 5,836 shipped sequences carry anyway.
		if (Row->Num() > 6)
		{
			Clip.Fade = static_cast<float>((*Row)[6]->AsNumber());
		}
		Clips.Add(Pair.Key, MoveTemp(Clip));
	}
	if (Malformed > 0)
	{
		UE_LOG(LogElysiumClips, Warning, TEXT("npc clips '%s': %d malformed row(s) skipped"),
			*InStem, Malformed);
	}
	return !Clips.IsEmpty();
}

bool FElysiumNpcClipSet::LoadActivities(const FString& InStem, TSet<FString>& Out, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	if (!ReadJsonFile(FElysiumContentPaths::NpcClips(InStem), Root, OutError))
	{
		return false;
	}
	TArray<FString> Activities;
	ReadStringArray(Root, TEXT("activities"), Activities);
	if (Activities.IsEmpty())
	{
		OutError = FString::Printf(TEXT("slice carries no `activities` intern table: %s"), *InStem);
		return false;
	}
	Out.Append(Activities);
	return true;
}

TArray<FString> FElysiumNpcClipSet::ByActivity(const FString& Activity) const
{
	TArray<FString> Out;
	for (const TPair<FString, FElysiumNpcClip>& Pair : Clips)
	{
		if (Pair.Value.Activity.Equals(Activity, ESearchCase::IgnoreCase))
		{
			Out.Add(Pair.Key);
		}
	}
	return Out;
}

TArray<FString> FElysiumNpcClipSet::StanceClips(const FString& AnimName, bool bWantTransitions) const
{
	// `stances.mdl` names an idle `Stance_<Name>_Idle_<N>` and the authored blend between two of
	// them `Stance_<Name>_Trans_<A>_<B>`; both carry ACT_DISPOSITION, so the discriminator is the
	// middle token. Matching it is what keeps `Stance_Neutral_Trans_1_2` (31 frames) out of the
	// idle set — and what makes a stance change able to play a real transition instead of snapping.
	const FString Prefix = FString::Printf(TEXT("Stance_%s_%s"), *AnimName,
		bWantTransitions ? TEXT("Trans") : TEXT("Idle"));
	TArray<FString> Out;
	for (const TPair<FString, FElysiumNpcClip>& Pair : Clips)
	{
		if (Pair.Value.Activity.Equals(ElysiumActivity::Disposition, ESearchCase::IgnoreCase)
			&& Pair.Key.StartsWith(Prefix, ESearchCase::IgnoreCase))
		{
			Out.Add(Pair.Key);
		}
	}
	return Out;
}

void FElysiumNpcClipSet::SortByWeight(TArray<FString>& Labels) const
{
	Labels.Sort([this](const FString& A, const FString& B)
	{
		const FElysiumNpcClip* CA = Clips.Find(A);
		const FElysiumNpcClip* CB = Clips.Find(B);
		const int32 WA = CA ? CA->Weight : 0;
		const int32 WB = CB ? CB->Weight : 0;
		return WA != WB ? WA > WB : A < B;
	});
}

// --- FElysiumNpcIndex ------------------------------------------------------------------

bool FElysiumNpcIndex::Load(FString& OutError)
{
	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *FElysiumContentPaths::NpcIndex()))
	{
		OutError = FString::Printf(TEXT("not found: %s"), *FElysiumContentPaths::NpcIndex());
		return false;
	}
	return LoadJsonText(JsonText, OutError);
}

bool FElysiumNpcIndex::LoadJsonText(const FString& JsonText, FString& OutError)
{
	ManifestVersion = 0;
	Npcs.Reset();
	Banks.Reset();
	Cinematics.Reset();
	AnimatedProps.Reset();
	PlacedModels.Reset();

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("malformed npc_index JSON");
		return false;
	}
	Root->TryGetNumberField(TEXT("manifest_version"), ManifestVersion);
	if (ManifestVersion < 3 || ManifestVersion > 7)
	{
		OutError = FString::Printf(TEXT("unsupported npc_index manifest version %d (expected 3 to 7)"),
			ManifestVersion);
		return false;
	}
	ReadIndexGroup(Root, TEXT("npcs"), Npcs);
	ReadIndexGroup(Root, TEXT("banks"), Banks);

	const TSharedPtr<FJsonObject>* CinObj = nullptr;
	if (Root->TryGetObjectField(TEXT("cinematics"), CinObj) && CinObj != nullptr)
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*CinObj)->Values)
		{
			const TSharedPtr<FJsonObject>* Entry = nullptr;
			if (!Pair.Value->TryGetObject(Entry) || Entry == nullptr)
			{
				continue;
			}
			FElysiumCinematicSet Set;
			(*Entry)->TryGetStringField(TEXT("stem"), Set.Stem);
			const TArray<TSharedPtr<FJsonValue>>* Roots = nullptr;
			if ((*Entry)->TryGetArrayField(TEXT("roots"), Roots) && Roots != nullptr)
			{
				for (const TSharedPtr<FJsonValue>& RootVal : *Roots)
				{
					const TSharedPtr<FJsonObject>* R = nullptr;
					if (!RootVal->TryGetObject(R) || R == nullptr)
					{
						continue;
					}
					FString RootName, Bank;
					if ((*R)->TryGetStringField(TEXT("root"), RootName)
						&& (*R)->TryGetStringField(TEXT("bank"), Bank))
					{
						Set.Roots.Add(RootName.ToLower(), Bank);
					}
				}
			}
			Cinematics.Add(Pair.Key.ToLower(), MoveTemp(Set));
		}
	}

	if (ManifestVersion >= 4)
	{
		auto ReadPropGroup = [&Root](const TCHAR* Field,
			TMap<FString, FElysiumAnimatedPropEntry>& Out)
		{
			const TSharedPtr<FJsonObject>* PropObj = nullptr;
			if (!Root->TryGetObjectField(Field, PropObj) || PropObj == nullptr)
			{
				return;
			}
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*PropObj)->Values)
			{
				const TSharedPtr<FJsonObject>* Obj = nullptr;
				if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(Obj) || Obj == nullptr)
				{
					continue;
				}
				FElysiumAnimatedPropEntry Entry;
				Entry.Stem = Pair.Key;
				(*Obj)->TryGetStringField(TEXT("glb"), Entry.Glb);
				(*Obj)->TryGetStringField(TEXT("eskm"), Entry.Eskm);
				(*Obj)->TryGetStringField(TEXT("model"), Entry.Model);
				(*Obj)->TryGetStringField(TEXT("static_stem"), Entry.StaticStem);
				(*Obj)->TryGetStringField(TEXT("clip_mode"), Entry.ClipMode);
				(*Obj)->TryGetBoolField(TEXT("static_equivalent"), Entry.bStaticEquivalent);
				ReadStringArray(*Obj, TEXT("rest_candidates"), Entry.RestCandidates);
				(*Obj)->TryGetNumberField(TEXT("bones"), Entry.Bones);
				ReadStringArray(*Obj, TEXT("split_bones"), Entry.SplitRotationBones);
				(*Obj)->TryGetStringField(TEXT("procedural"), Entry.Procedural);
				(*Obj)->TryGetNumberField(TEXT("procedural_bones"), Entry.ProceduralBones);
				(*Obj)->TryGetStringField(TEXT("blends"), Entry.Blends);
				(*Obj)->TryGetNumberField(TEXT("blend_grids"), Entry.BlendGrids);
				Entry.Model.ReplaceInline(TEXT("\\"), TEXT("/"));
				Entry.Model.ToLowerInline();
				// Two shapes. v6 writes one object per clip carrying the selection keys; v4/v5
				// wrote bare, alphabetically sorted names. Both are accepted so an index that
				// predates the re-export still stands its props — it just has no rest pose and
				// no loop flags, which is the behaviour those versions already had.
				const TArray<TSharedPtr<FJsonValue>>* Clips = nullptr;
				if ((*Obj)->TryGetArrayField(TEXT("clips"), Clips) && Clips != nullptr)
				{
					for (const TSharedPtr<FJsonValue>& Clip : *Clips)
					{
						if (!Clip.IsValid())
						{
							continue;
						}
						FElysiumPropClip Row;
						Row.Index = Entry.Clips.Num();
						const TSharedPtr<FJsonObject>* ClipObj = nullptr;
						if (Clip->TryGetObject(ClipObj) && ClipObj != nullptr)
						{
							(*ClipObj)->TryGetStringField(TEXT("name"), Row.Name);
							(*ClipObj)->TryGetStringField(TEXT("activity"), Row.Activity);
							(*ClipObj)->TryGetNumberField(TEXT("weight"), Row.Weight);
							(*ClipObj)->TryGetNumberField(TEXT("flags"), Row.Flags);
							(*ClipObj)->TryGetNumberField(TEXT("index"), Row.Index);
							(*ClipObj)->TryGetNumberField(TEXT("frames"), Row.Frames);
							(*ClipObj)->TryGetNumberField(TEXT("fps"), Row.Fps);
							(*ClipObj)->TryGetNumberField(TEXT("bounds_radius_m"), Row.BoundsRadiusMeters);
						}
						else
						{
							Row.Name = Clip->AsString();
						}
						if (!Row.Name.IsEmpty())
						{
							Entry.Clips.Add(MoveTemp(Row));
						}
					}
				}
				if (!Entry.Model.IsEmpty() && (!Entry.Eskm.IsEmpty() || !Entry.Glb.IsEmpty()))
				{
					Out.Add(Entry.Stem, MoveTemp(Entry));
				}
			}
		};
		ReadPropGroup(TEXT("animated_props"), AnimatedProps);
		if (ManifestVersion >= 7)
		{
			ReadPropGroup(TEXT("placed_models"), PlacedModels);
		}
	}

	if (Npcs.IsEmpty())
	{
		OutError = TEXT("index carries no NPCs (re-run: uv run elysium export bundle npc)");
		return false;
	}
	return true;
}

FString FElysiumNpcIndex::BankGlbPath(const FString& BankStem) const
{
	const FElysiumNpcIndexEntry* E = Banks.Find(BankStem);
	return E ? FElysiumContentPaths::NpcBankGlb(E->Glb) : FString();
}

FString FElysiumCinematicSet::BankForRoot(const FString& Root) const
{
	if (const FString* Exact = Roots.Find(Root.ToLower()))
	{
		return *Exact;
	}
	// A single-actor cinematic can omit bonerename. Multiple roots are never interchangeable:
	// iteration order is not semantic, and choosing another root animates the wrong cast member.
	if (Root.IsEmpty() && Roots.Num() == 1)
	{
		return Roots.CreateConstIterator().Value();
	}
	return FString();
}

const FElysiumCinematicSet* FElysiumNpcIndex::FindCinematic(const FString& ModelPath) const
{
	FString Key = ModelPath;
	Key.ReplaceInline(TEXT("\\"), TEXT("/"));
	Key.ToLowerInline();
	if (!Key.StartsWith(TEXT("models/")))
	{
		Key = TEXT("models/") + Key;
	}
	return Cinematics.Find(Key);
}

FString FElysiumNpcIndex::CinematicBank(const FString& ModelPath, const FString& BoneRoot) const
{
	const FElysiumCinematicSet* Set = FindCinematic(ModelPath);
	return Set ? Set->BankForRoot(BoneRoot) : FString();
}

const FElysiumPropClip* FElysiumAnimatedPropEntry::FindClip(const FString& Label) const
{
	// Linear: the largest exported prop vocabulary is 50 clips (`wolf_form`) and the theatre's
	// largest is 7, so a map would cost more than it saves and would lose declaration order.
	for (const FElysiumPropClip& Clip : Clips)
	{
		if (Clip.Name.Equals(Label, ESearchCase::IgnoreCase))
		{
			return &Clip;
		}
	}
	return nullptr;
}

FString FElysiumAnimatedPropEntry::RestSequence(int32 PlacementToken) const
{
	if (Clips.IsEmpty())
	{
		return FString();
	}
	// CBaseProp::Spawn: SelectWeightedSequence(ACT_IDLE), then sequence 0. The retail draw is
	// represented deterministically by a cross-language FNV-1a seed over model + placement token,
	// so save/load and rebuilds retain the same authored alternative.
	static const FString ActIdle(TEXT("ACT_IDLE"));
	TArray<const FElysiumPropClip*> Candidates;
	for (const FElysiumPropClip& Clip : Clips)
	{
		if (Clip.Activity.Equals(ActIdle, ESearchCase::IgnoreCase))
		{
			Candidates.Add(&Clip);
		}
	}
	Candidates.Sort([](const FElysiumPropClip& A, const FElysiumPropClip& B)
	{
		return A.Index < B.Index;
	});
	if (!Candidates.IsEmpty())
	{
		uint32 Hash = 2166136261u;
		FTCHARToUTF8 Utf8(*Model.ToLower());
		for (int32 Index = 0; Index < Utf8.Length(); ++Index)
		{
			Hash = (Hash ^ static_cast<uint8>(Utf8.Get()[Index])) * 16777619u;
		}
		const uint32 Token = static_cast<uint32>(FMath::Max(0, PlacementToken));
		for (int32 Shift = 0; Shift < 32; Shift += 8)
		{
			Hash = (Hash ^ static_cast<uint8>((Token >> Shift) & 0xffu)) * 16777619u;
		}
		int32 Total = 0;
		for (const FElysiumPropClip* Clip : Candidates)
		{
			Total += FMath::Max(1, Clip->Weight);
		}
		int32 Pick = static_cast<int32>(Hash % static_cast<uint32>(Total));
		for (const FElysiumPropClip* Clip : Candidates)
		{
			const int32 Weight = FMath::Max(1, Clip->Weight);
			if (Pick < Weight)
			{
				return Clip->Name;
			}
			Pick -= Weight;
		}
	}

	// The fallback, and the branch that actually fires: 16 of the 19 exported prop models tag no
	// ACT_IDLE at all. Sequence 0 is the lowest declared ordinal, not the array's first element —
	// a v4/v5 index carries alphabetically sorted names whose ordinals are the sort positions.
	const FElysiumPropClip* First = &Clips[0];
	for (const FElysiumPropClip& Clip : Clips)
	{
		if (Clip.Index < First->Index)
		{
			First = &Clip;
		}
	}
	return First->Name;
}

const FElysiumAnimatedPropEntry* FElysiumNpcIndex::FindAnimatedProp(const FString& ModelPath) const
{
	if (const FElysiumAnimatedPropEntry* Placed = FindPlacedModel(ModelPath))
	{
		return Placed;
	}
	FString Key = ModelPath;
	Key.ReplaceInline(TEXT("\\"), TEXT("/"));
	Key.ToLowerInline();
	if (!Key.StartsWith(TEXT("models/")))
	{
		Key = TEXT("models/") + Key;
	}
	for (const TPair<FString, FElysiumAnimatedPropEntry>& Pair : AnimatedProps)
	{
		if (Pair.Value.Model == Key)
		{
			return &Pair.Value;
		}
	}
	return nullptr;
}

const FElysiumAnimatedPropEntry* FElysiumNpcIndex::FindPlacedModel(const FString& ModelPath) const
{
	FString Key = ModelPath;
	Key.ReplaceInline(TEXT("\\"), TEXT("/"));
	Key.ToLowerInline();
	if (!Key.StartsWith(TEXT("models/")))
	{
		Key = TEXT("models/") + Key;
	}
	for (const TPair<FString, FElysiumAnimatedPropEntry>& Pair : PlacedModels)
	{
		if (Pair.Value.Model == Key)
		{
			return &Pair.Value;
		}
	}
	return nullptr;
}
