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
			ReadStringArray(*Obj, TEXT("split_bones"), E.SplitRotationBones);
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
		Clips.Add(Pair.Key, MoveTemp(Clip));
	}
	if (Malformed > 0)
	{
		UE_LOG(LogElysiumClips, Warning, TEXT("npc clips '%s': %d malformed row(s) skipped"),
			*InStem, Malformed);
	}
	return !Clips.IsEmpty();
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

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("malformed npc_index JSON");
		return false;
	}
	Root->TryGetNumberField(TEXT("manifest_version"), ManifestVersion);
	if (ManifestVersion != 3 && ManifestVersion != 4)
	{
		OutError = FString::Printf(TEXT("unsupported npc_index manifest version %d (expected 3 or 4)"),
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
		const TSharedPtr<FJsonObject>* PropObj = nullptr;
		if (Root->TryGetObjectField(TEXT("animated_props"), PropObj) && PropObj != nullptr)
		{
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
				(*Obj)->TryGetStringField(TEXT("model"), Entry.Model);
				(*Obj)->TryGetNumberField(TEXT("bones"), Entry.Bones);
				ReadStringArray(*Obj, TEXT("split_bones"), Entry.SplitRotationBones);
				Entry.Model.ReplaceInline(TEXT("\\"), TEXT("/"));
				Entry.Model.ToLowerInline();
				const TArray<TSharedPtr<FJsonValue>>* Clips = nullptr;
				if ((*Obj)->TryGetArrayField(TEXT("clips"), Clips) && Clips != nullptr)
				{
					for (const TSharedPtr<FJsonValue>& Clip : *Clips)
					{
						if (Clip.IsValid())
						{
							Entry.Clips.Add(Clip->AsString().ToLower());
						}
					}
				}
				if (!Entry.Glb.IsEmpty() && !Entry.Model.IsEmpty())
				{
					AnimatedProps.Add(Entry.Stem, MoveTemp(Entry));
				}
			}
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

const FElysiumAnimatedPropEntry* FElysiumNpcIndex::FindAnimatedProp(const FString& ModelPath) const
{
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
