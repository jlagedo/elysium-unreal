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

	// A bone-local point off the sidecar's `[x, y, z]` centimetre triple, read verbatim: the `UE_`
	// exporter already stated it Unreal-native, so nothing is converted here.
	bool ReadPointCm(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, FVector& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Components = nullptr;
		if (!Object->TryGetArrayField(Field, Components) || Components == nullptr
			|| Components->Num() != 3)
		{
			return false;
		}
		Out = FVector((*Components)[0]->AsNumber(), (*Components)[1]->AsNumber(),
			(*Components)[2]->AsNumber());
		return true;
	}

	// The `swings` column: one object per authored swing-contact record. A row that does not carry
	// the shape is dropped rather than half-read — a record with no window or no segment is not a
	// contact the walk could test, and inventing either would put a swing where the file states none.
	//
	// Answers how many rows it dropped, which the caller folds into the slice's malformed count: the
	// column is this repository's own exporter product, so a row it cannot read is a pipeline defect
	// and never an authored absence, and the absence has its own (silent) shape — no column at all.
	int32 ReadSwingRecords(const TSharedPtr<FJsonValue>& Column, TArray<FElysiumSwingRecord>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (!Column.IsValid() || !Column->TryGetArray(Rows) || Rows == nullptr)
		{
			return 1;   // a stated column that is not an array of records
		}
		int32 Dropped = 0;
		Out.Reserve(Rows->Num());
		for (const TSharedPtr<FJsonValue>& RowValue : *Rows)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!RowValue.IsValid() || !RowValue->TryGetObject(Object) || Object == nullptr)
			{
				++Dropped;
				continue;
			}
			const TSharedPtr<FJsonObject>& Row = *Object;

			FElysiumSwingRecord Record;
			double Window = 0.0;
			if (!Row->TryGetNumberField(TEXT("start"), Window))
			{
				++Dropped;
				continue;
			}
			Record.Start = static_cast<float>(Window);
			if (!Row->TryGetNumberField(TEXT("end"), Window))
			{
				++Dropped;
				continue;
			}
			Record.End = static_cast<float>(Window);
			Row->TryGetStringField(TEXT("bone"), Record.Bone);
			if (!ReadPointCm(Row, TEXT("a_cm"), Record.ACm)
				|| !ReadPointCm(Row, TEXT("b_cm"), Record.BCm))
			{
				++Dropped;
				continue;
			}
			// Four direction buckets of candidate activity names, kept in file order: bucket `k`
			// answers direction `(b8 + k) mod 4`, so the array position is load-bearing and a
			// bucket that names nothing has to stay in place as an empty one.
			const TArray<TSharedPtr<FJsonValue>>* Buckets = nullptr;
			if (Row->TryGetArrayField(TEXT("kb_names"), Buckets) && Buckets != nullptr)
			{
				Record.KnockbackNames.Reserve(Buckets->Num());
				for (const TSharedPtr<FJsonValue>& BucketValue : *Buckets)
				{
					TArray<FString>& Names = Record.KnockbackNames.AddDefaulted_GetRef();
					const TArray<TSharedPtr<FJsonValue>>* Candidates = nullptr;
					if (BucketValue.IsValid() && BucketValue->TryGetArray(Candidates)
						&& Candidates != nullptr)
					{
						for (const TSharedPtr<FJsonValue>& Name : *Candidates)
						{
							FString Text;
							if (Name.IsValid() && Name->TryGetString(Text) && !Text.IsEmpty())
							{
								Names.Add(Text);
							}
						}
					}
				}
			}
			int32 Byte = 0;
			Record.B8 = Row->TryGetNumberField(TEXT("b8"), Byte) ? Byte : 0;
			Record.Ba = Row->TryGetNumberField(TEXT("ba"), Byte) ? Byte : 0;
			// Stated on every row rather than inferred: a consumer forbidden to repair authored data
			// reads the flag rather than re-testing the window it must reproduce.
			Row->TryGetBoolField(TEXT("degenerate"), Record.bDegenerate);
			Out.Add(MoveTemp(Record));
		}
		return Dropped;
	}

	// The `combo` column: one object carrying the whole authored chain block. The exporter writes it
	// WHOLE or not at all — `read_combo_chain` already answered "is any of this authored" — so a
	// column that is stated and cannot be read is a pipeline defect and never an authored absence,
	// exactly like a swing record that will not parse.
	//
	// Answers 1 when it dropped the column, so the caller can fold it into the slice's report. A
	// half-read block is refused rather than filled with markers: a missing window would put a
	// hand-off where the file states none, and a missing mask would enter direction-keyed selection
	// as the neutral attack.
	int32 ReadComboChain(const TSharedPtr<FJsonValue>& Column, FElysiumComboChain& Out)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (!Column.IsValid() || !Column->TryGetObject(Object) || Object == nullptr)
		{
			return 1;   // a stated column that is not a record
		}
		const TSharedPtr<FJsonObject>& Row = *Object;

		FElysiumComboChain Chain;
		if (!Row->TryGetNumberField(TEXT("mask"), Chain.Mask))
		{
			return 1;
		}
		double Cycle = 0.0;
		if (!Row->TryGetNumberField(TEXT("w_open"), Cycle))
		{
			return 1;
		}
		Chain.WindowOpen = static_cast<float>(Cycle);
		if (!Row->TryGetNumberField(TEXT("w_close"), Cycle))
		{
			return 1;
		}
		Chain.WindowClose = static_cast<float>(Cycle);
		if (!Row->TryGetNumberField(TEXT("w_hold"), Cycle))
		{
			return 1;
		}
		Chain.HoldCycle = static_cast<float>(Cycle);
		// The three names are optional WITHIN a stated record: the exporter writes each as an empty
		// string where the descriptor resolves none, and 12 of the 208 carriers state a dodge activity
		// with no successor at all.
		Row->TryGetStringField(TEXT("dodge"), Chain.Dodge);
		Row->TryGetStringField(TEXT("chain"), Chain.Chain);
		Row->TryGetStringField(TEXT("chain_alt"), Chain.ChainAlt);
		Chain.bStated = true;
		Out = MoveTemp(Chain);
		return 0;
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
			(*Obj)->TryGetNumberField(TEXT("event_sequences"), E.EventSequences);
			Out.Add(Pair.Key, MoveTemp(E));
		}
	}
}

// --- FElysiumNpcClipSet ----------------------------------------------------------------

bool FElysiumNpcClipSet::Load(const FString& InStem, FString& OutError)
{
	const FString Path = FElysiumContentPaths::NpcClips(InStem);
	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *Path))
	{
		Stem = InStem;
		Clips.Reset();
		OutError = FString::Printf(TEXT("not found: %s"), *Path);
		return false;
	}
	return LoadJsonText(InStem, JsonText, OutError);
}

bool FElysiumNpcClipSet::LoadJsonText(const FString& InStem, const FString& JsonText, FString& OutError)
{
	Stem = InStem;
	Clips.Reset();

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> SliceReader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(SliceReader, Root) || !Root.IsValid())
	{
		OutError = FString::Printf(TEXT("malformed JSON: %s"), *InStem);
		return false;
	}

	// The slice interns its owner stems and activity literals into two arrays and stores each clip as
	// [owner_i, activity_i, weight, flags, frames, fps, fade, reach_cm, blocked_reaction, swings,
	// combo] — the
	// strings repeat across ~1,540 rows, and the owner column especially (31 distinct stems) pays for
	// the indirection. The blocked-reaction literal is NOT interned: it names an activity the stem
	// may not be able to play, and the `activities` table is the playable vocabulary.
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
	int32 DroppedSwings = 0;
	int32 DroppedCombos = 0;
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
		// The two melee columns, on the same trailing-and-optional contract: the exporter truncates a
		// row at its last stated column, so a row that reaches neither is an ordinary sequence stating
		// neither. `reach_cm` holds the slot `blocked_reaction` sits behind and is written as a legal
		// null when a sequence names a reaction without a reach, which is why the read is guarded
		// rather than assumed numeric.
		if (Row->Num() > 7 && !(*Row)[7]->IsNull())
		{
			Clip.ReachCm = static_cast<float>((*Row)[7]->AsNumber());
		}
		if (Row->Num() > 8)
		{
			Clip.BlockedReaction = (*Row)[8]->AsString();
		}
		// The third melee column, on the same trailing-and-optional contract as the two above: a
		// slice written before the column existed simply ends at 8, and every such row reads as a
		// sequence declaring no swing records — which is what all but 574 shipped descriptors are
		// anyway. That is the whole compatibility rule, and it is why an unre-exported corpus loses
		// contact rather than mis-reading a column.
		if (Row->Num() > 9 && !(*Row)[9]->IsNull())
		{
			DroppedSwings += ReadSwingRecords((*Row)[9], Clip.Swings);
		}
		// The fourth melee column, on the same trailing-and-optional contract as the three above: a
		// slice written before it existed ends at 9, and every such row reads as a sequence declaring
		// no combo block — which is what all but 208 shipped descriptors are anyway. An attack with no
		// block is a terminal one that no press can continue and that direction-keyed selection never
		// considers, which is exactly the behaviour an unre-exported corpus should show.
		if (Row->Num() > 10 && !(*Row)[10]->IsNull())
		{
			DroppedCombos += ReadComboChain((*Row)[10], Clip.Combo);
		}
		Clips.Add(Pair.Key, MoveTemp(Clip));
	}
	if (Malformed > 0)
	{
		UE_LOG(LogElysiumClips, Warning, TEXT("npc clips '%s': %d malformed row(s) skipped"),
			*InStem, Malformed);
	}
	if (DroppedSwings > 0)
	{
		// Separate from the row count above because the failure is a different one: the row parsed
		// and its clip is usable, but a swing-contact record inside it could not be read and that
		// clip's swing now opens fewer windows than the descriptor authored.
		UE_LOG(LogElysiumClips, Warning,
			TEXT("npc clips '%s': %d swing-contact record(s) dropped — those windows will not open"),
			*InStem, DroppedSwings);
	}
	if (DroppedCombos > 0)
	{
		// Separate again, and a different consequence: the clip is usable but its authored chain is
		// gone, so a press inside that attack's hand-off window will start a new swing instead of
		// continuing the combo, and the attack is invisible to direction-keyed selection.
		UE_LOG(LogElysiumClips, Warning,
			TEXT("npc clips '%s': %d combo-chain block(s) dropped — those attacks chain nothing and "
				"are not direction-selectable"),
			*InStem, DroppedCombos);
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

bool FElysiumNpcClipSet::HasActivity(const FString& Activity) const
{
	if (Activity.IsEmpty())
	{
		return false;
	}
	for (const TPair<FString, FElysiumNpcClip>& Pair : Clips)
	{
		if (Pair.Value.Activity.Equals(Activity, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

float FElysiumNpcClipSet::MaxReachCmForActivity(const FString& Activity) const
{
	if (Activity.IsEmpty())
	{
		return 0.0f;
	}
	// The maximum over the answering sequences, not the pick's own: retail reads the reach off every
	// sequence the translated activity returns and queries at the largest, so a two-variant swing
	// whose long variant was not selected still acquires at the long distance. A clip stating no
	// reach contributes nothing rather than pinning the answer to zero.
	float Max = 0.0f;
	for (const TPair<FString, FElysiumNpcClip>& Pair : Clips)
	{
		if (Pair.Value.HasReach() && Pair.Value.Activity.Equals(Activity, ESearchCase::IgnoreCase))
		{
			Max = FMath::Max(Max, Pair.Value.ReachCm);
		}
	}
	return Max;
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
	if (ManifestVersion < 3 || ManifestVersion > 8)
	{
		OutError = FString::Printf(TEXT("unsupported npc_index manifest version %d (expected 3 to 8)"),
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
				(*Obj)->TryGetNumberField(TEXT("event_sequences"), Entry.EventSequences);
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
				if (!Entry.Model.IsEmpty() && !Entry.Eskm.IsEmpty())
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
