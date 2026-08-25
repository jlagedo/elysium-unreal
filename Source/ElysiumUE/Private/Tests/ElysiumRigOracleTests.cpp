// The animation resolver against a retail rig-resolution capture.
//
// `uv run elysium research rig_parity` turns a `life_rig_resolution` Frida session into
// `rig_oracle.json`: every (body, activity, clip label, owning bank) retail committed, rewritten
// in the export's own stem namespace. This test replays those rows through the same pure resolver
// the runtime uses (`ElysiumAnimResolve::Resolve`) over the real exported clip maps and asserts
// three things per row: the clip retail played is in this body's vocabulary under the same owner
// and activity; asking for that activity resolves to a clip of that activity from the bank retail
// resolved through; and an exact-label lookup lands on the same bank. The oracle is game-derived
// and lives under ELYSIUM_WORK_ROOT; the test abstains when no session has been captured.

#include "Misc/AutomationTest.h"

#include "ElysiumAnimationIntent.h"
#include "ElysiumContentPaths.h"
#include "Visual/ElysiumAnimationResolve.h"
#include "Visual/ElysiumBlendGrids.h"
#include "Visual/ElysiumNpcClips.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

static constexpr EAutomationTestFlags GElysiumRigOracleFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// `ELYSIUM_RIG_ORACLE` names one report; otherwise the newest single-body session under the
	// work root that `rig_parity` has been run on. The chaos recipe samples its ownership hooks,
	// which is why only the single-body recipe is the oracle of record here.
	FString FindOracle()
	{
		const FString Named = FPlatformMisc::GetEnvironmentVariable(TEXT("ELYSIUM_RIG_ORACLE"));
		if (!Named.IsEmpty())
		{
			return Named;
		}
		const FString WorkRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("ELYSIUM_WORK_ROOT"));
		if (WorkRoot.IsEmpty())
		{
			return FString();
		}
		const FString Frida = WorkRoot / TEXT("research") / TEXT("frida");
		TArray<FString> Sessions;
		IFileManager::Get().FindFiles(Sessions, *(Frida / TEXT("*-life_rig_resolution")), false, true);
		Sessions.Sort();
		for (int32 Index = Sessions.Num() - 1; Index >= 0; --Index)
		{
			const FString Candidate = Frida / Sessions[Index] / TEXT("rig_oracle.json");
			if (IFileManager::Get().FileExists(*Candidate))
			{
				return Candidate;
			}
		}
		return FString();
	}

	struct FOracleRow
	{
		FString Label;
		FString OwnerStem;
		TArray<FString> Activities;
		TArray<FString> Targets;
		int32 Hits = 0;

		bool Has(const TCHAR* Target) const { return Targets.Contains(Target); }
	};

	struct FOracleBody
	{
		FString Stem;
		FString Model;
		TArray<FOracleRow> Rows;
	};

	bool ReadOracle(const FString& Path, TArray<FOracleBody>& OutBodies, FString& OutError)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			OutError = FString::Printf(TEXT("cannot read %s"), *Path);
			return false;
		}
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = FString::Printf(TEXT("%s is not a JSON object"), *Path);
			return false;
		}
		const TSharedPtr<FJsonObject>* Oracle = nullptr;
		if (!Root->TryGetObjectField(TEXT("oracle"), Oracle) || Oracle == nullptr)
		{
			OutError = FString::Printf(TEXT("%s carries no `oracle` section"), *Path);
			return false;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Oracle)->Values)
		{
			const TSharedPtr<FJsonObject>* BodyObject = nullptr;
			if (!Entry.Value.IsValid() || !Entry.Value->TryGetObject(BodyObject))
			{
				continue;
			}
			FOracleBody Body;
			Body.Stem = Entry.Key;
			(*BodyObject)->TryGetStringField(TEXT("model"), Body.Model);
			const TArray<TSharedPtr<FJsonValue>>* Selections = nullptr;
			if ((*BodyObject)->TryGetArrayField(TEXT("selections"), Selections))
			{
				for (const TSharedPtr<FJsonValue>& Value : *Selections)
				{
					const TSharedPtr<FJsonObject>* RowObject = nullptr;
					if (!Value.IsValid() || !Value->TryGetObject(RowObject))
					{
						continue;
					}
					FOracleRow Row;
					(*RowObject)->TryGetStringField(TEXT("label"), Row.Label);
					// `null` when retail's owner has no export stem (a weapon or a viewmodel).
					if (!(*RowObject)->TryGetStringField(TEXT("owner_stem"), Row.OwnerStem))
					{
						continue;
					}
					(*RowObject)->TryGetStringArrayField(TEXT("activity_names"), Row.Activities);
					(*RowObject)->TryGetStringArrayField(TEXT("targets"), Row.Targets);
					(*RowObject)->TryGetNumberField(TEXT("hits"), Row.Hits);
					if (!Row.Label.IsEmpty() && !Row.OwnerStem.IsEmpty())
					{
						Body.Rows.Add(MoveTemp(Row));
					}
				}
			}
			OutBodies.Add(MoveTemp(Body));
		}
		OutBodies.Sort([](const FOracleBody& A, const FOracleBody& B) { return A.Stem < B.Stem; });
		return true;
	}

	// The clip map keys labels by their authored spelling; retail matches by `stricmp`. An
	// empty `Owner` asks for the include tree's first row, which is what a label-only lookup
	// resolves to; a named one asks for that bank's own copy.
	const FElysiumNpcClip* FindClip(const FElysiumNpcClipSet& Set, const FString& Label,
		const FString& Owner, FString& OutSpelling)
	{
		if (const FElysiumNpcClip* Exact = Set.Find(Label, Owner))
		{
			OutSpelling = Label;
			return Exact;
		}
		for (const TPair<FString, TArray<FElysiumNpcClip>>& Entry : Set.Clips)
		{
			if (!Entry.Key.Equals(Label, ESearchCase::IgnoreCase))
			{
				continue;
			}
			OutSpelling = Entry.Key;
			for (const FElysiumNpcClip& Clip : Entry.Value)
			{
				if (Owner.IsEmpty() || Clip.Owner.Equals(Owner, ESearchCase::IgnoreCase))
				{
					return &Clip;
				}
			}
			// The label exists under some other owner, which the caller reports as the
			// divergence it is rather than reading as an absence.
			return nullptr;
		}
		return nullptr;
	}

	// Whether the body carries this label at all, under any owner.
	bool CarriesLabel(const FElysiumNpcClipSet& Set, const FString& Label, FString& OutOwners)
	{
		for (const TPair<FString, TArray<FElysiumNpcClip>>& Entry : Set.Clips)
		{
			if (!Entry.Key.Equals(Label, ESearchCase::IgnoreCase))
			{
				continue;
			}
			TArray<FString> Owners;
			for (const FElysiumNpcClip& Clip : Entry.Value)
			{
				Owners.Add(FString::Printf(TEXT("%s (%s)"), *Clip.Owner, *Clip.Activity));
			}
			OutOwners = FString::Join(Owners, TEXT(", "));
			return true;
		}
		return false;
	}

	struct FRealTables
	{
		const FElysiumNpcIndex* Index = nullptr;
		TMap<FString, TSharedPtr<FElysiumBlendTable>> Cache;

		const FElysiumBlendTable* operator()(const FString& Owner)
		{
			if (const TSharedPtr<FElysiumBlendTable>* Found = Cache.Find(Owner))
			{
				return Found->Get();
			}
			const FElysiumNpcIndexEntry* Entry = Index->Npcs.Find(Owner);
			if (Entry == nullptr) { Entry = Index->Banks.Find(Owner); }
			TSharedPtr<FElysiumBlendTable> Table;
			if (Entry != nullptr && !Entry->Blends.IsEmpty())
			{
				Table = MakeShared<FElysiumBlendTable>();
				FString Error;
				if (!Table->Load(Entry->Blends, Error))
				{
					Table.Reset();
				}
			}
			Cache.Add(Owner, Table);
			return Table.Get();
		}
	};

	FElysiumAnimationSelection ResolveOn(const FElysiumNpcClipSet& Set, FRealTables& Tables,
		EElysiumAnimBodyKind BodyKind, EElysiumAnimRoute Route, const FString& Key,
		EElysiumAnimSelect Select = EElysiumAnimSelect::Weighted)
	{
		FElysiumAnimationCatalog Catalog;
		Catalog.Clips = &Set;
		Catalog.BlendTableFor = [&Tables](const FString& Owner) { return Tables(Owner); };

		FElysiumAnimationIntent Intent;
		Intent.Stem = Set.Stem;
		Intent.Route = Route;
		if (Route == EElysiumAnimRoute::ExactLabel)
		{
			Intent.SequenceLabel = Key;
		}
		else
		{
			Intent.Activity = Key;
		}
		Intent.Source = BodyKind == EElysiumAnimBodyKind::Player
			? EElysiumAnimSource::Player : EElysiumAnimSource::Npc;
		Intent.BodyKind = BodyKind;
		Intent.bAllowFallbackLadder = false;
		Intent.Select = Select;

		FElysiumAnimationSelection Out;
		ElysiumAnimResolve::Resolve(Intent, Catalog, Out);
		return Out;
	}

	// A failure list that reports its count in full and its first few rows verbatim.
	struct FDivergences
	{
		FString Kind;
		TArray<FString> Rows;

		void Add(const FString& Row) { Rows.Add(Row); }

		void Report(FAutomationTestBase& Test, int32 Examples = 12) const
		{
			if (Rows.IsEmpty())
			{
				return;
			}
			FString Message = FString::Printf(TEXT("%s: %d divergence(s)"), *Kind, Rows.Num());
			for (int32 Index = 0; Index < FMath::Min(Examples, Rows.Num()); ++Index)
			{
				Message += TEXT("\n    ") + Rows[Index];
			}
			if (Rows.Num() > Examples)
			{
				Message += FString::Printf(TEXT("\n    ... and %d more"), Rows.Num() - Examples);
			}
			Test.AddError(Message);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumRigOracleTest,
	"Elysium.Content.RigOracle", GElysiumRigOracleFlags)
bool FElysiumRigOracleTest::RunTest(const FString&)
{
	if (FElysiumContentPaths::IsIncomplete(TEXT("npc")))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the npc export domain is marked incomplete"));
		return true;
	}
	const FString OraclePath = FindOracle();
	if (OraclePath.IsEmpty())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no rig_oracle.json under $ELYSIUM_WORK_ROOT/research/frida "
			"(capture with frida_probe --recipe life_rig_resolution, then run rig_parity)"));
		return true;
	}
	TArray<FOracleBody> Bodies;
	FString Error;
	if (!ReadOracle(OraclePath, Bodies, Error))
	{
		AddError(Error);   // present but unreadable is a failure, not an abstention
		return false;
	}
	FElysiumNpcIndex Index;
	if (!Index.Load(Error) || !Index.IsValid())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no exported npc index (run: uv run elysium export bundle npc)"));
		return true;
	}
	FRealTables Tables;
	Tables.Index = &Index;

	FDivergences Vocabulary{ TEXT("clip retail played is not in the body's vocabulary under that owner") };
	FDivergences Activity{ TEXT("clip retail played carries a different activity in the export") };
	FDivergences Selection{ TEXT("resolving the activity does not reach a clip of that activity from retail's bank") };
	FDivergences Exact{ TEXT("an exact-label lookup lands on a different owner than retail's") };
	int32 BodiesChecked = 0;
	int32 RowsChecked = 0;
	TArray<FString> BodiesNotExported;

	for (const FOracleBody& Body : Bodies)
	{
		FElysiumNpcClipSet Set;
		FString LoadError;
		if (!Index.Npcs.Contains(Body.Stem) || !Set.Load(Body.Stem, LoadError))
		{
			BodiesNotExported.Add(Body.Stem);
			continue;
		}
		++BodiesChecked;
		const EElysiumAnimBodyKind BodyKind = Body.Model.Contains(TEXT("/character/pc/"))
			? EElysiumAnimBodyKind::Player : EElysiumAnimBodyKind::Cast;

		// Every bank retail resolved a given activity through on this body: the set a resolved
		// selection's owner has to fall in.
		TMap<FString, TSet<FString>> OwnersByActivity;
		// And the (label, owner) pairs behind them. Where an activity witnessed exactly one, a
		// deterministic selector has exactly one right answer and the test says so; where it
		// witnessed several, membership is all the capture supports.
		TMap<FString, TSet<FString>> PairsByActivity;
		for (const FOracleRow& Row : Body.Rows)
		{
			for (const FString& Name : Row.Activities)
			{
				OwnersByActivity.FindOrAdd(Name).Add(Row.OwnerStem);
				PairsByActivity.FindOrAdd(Name).Add(
					FString::Printf(TEXT("%s|%s"), *Row.Label.ToLower(), *Row.OwnerStem));
			}
		}

		for (const FOracleRow& Row : Body.Rows)
		{
			++RowsChecked;
			const FString Where = FString::Printf(TEXT("%s: %s"), *Body.Stem, *Row.Label);

			// (A) the vocabulary: the clip retail played exists under the same owner and activity.
			FString Spelling;
			const FElysiumNpcClip* Clip = FindClip(Set, Row.Label, Row.OwnerStem, Spelling);
			if (Clip == nullptr)
			{
				FString Owners;
				Vocabulary.Add(CarriesLabel(Set, Row.Label, Owners)
					? FString::Printf(TEXT("%s -> retail %s; export has it only under %s, hits %d"),
						*Where, *Row.OwnerStem, *Owners, Row.Hits)
					: FString::Printf(TEXT("%s -> retail %s; export has no such label"),
						*Where, *Row.OwnerStem));
			}
			else
			{
				for (const FString& Name : Row.Activities)
				{
					if (!Clip->Activity.Equals(Name, ESearchCase::IgnoreCase))
					{
						Activity.Add(FString::Printf(TEXT("%s -> retail %s; export %s"),
							*Where, *Name, *Clip->Activity));
					}
				}
			}

			// (B) the resolver, run under the selector the capture says produced this row.
			//
			// Retail has two pickers over one candidate array, and which one answered is recorded:
			// `select_heaviest_sequence` is deterministic — largest `actweight`, ties to the lowest
			// global sequence number — so its rows can be pinned to the exact clip retail
			// committed. `select_weighted_sequence` rolled, and what it rolled is not a property of
			// the corpus, so those rows are held only to the owner and the activity. Asserting the
			// draw's label would be asserting retail's RNG.
			//
			// The player melee arm keys on a button mask the capture does not carry, so its rows
			// are asserted on (A) and (C) only.
			const bool bHeaviest = Row.Has(TEXT("vampire.select_heaviest_sequence"))
				&& !Row.Has(TEXT("vampire.select_weighted_sequence"));
			const bool bMeleeArmOnly = Row.Has(TEXT("vampire.player_select_melee_sequence"))
				&& !Row.Has(TEXT("vampire.select_weighted_sequence"))
				&& !Row.Has(TEXT("vampire.select_heaviest_sequence"));
			if (!bMeleeArmOnly)
			{
				const EElysiumAnimSelect Select = bHeaviest
					? EElysiumAnimSelect::Heaviest : EElysiumAnimSelect::Weighted;
				for (const FString& Name : Row.Activities)
				{
					const FElysiumAnimationSelection Out = ResolveOn(Set, Tables, BodyKind,
						EElysiumAnimRoute::Activity, Name, Select);
					if (Out.SequenceLabel.IsEmpty() || Out.OwnerStem.IsEmpty())
					{
						Selection.Add(FString::Printf(TEXT("%s [%s] -> retail %s; export resolves nothing (outcome %d)"),
							*Where, *Name, *Row.OwnerStem, static_cast<int32>(Out.Outcome)));
						continue;
					}
					FString ResolvedSpelling;
					const FElysiumNpcClip* Resolved = FindClip(Set, Out.SequenceLabel, Out.OwnerStem,
						ResolvedSpelling);
					const FString ResolvedActivity = Resolved ? Resolved->Activity : FString();
					const TSet<FString>* Owners = OwnersByActivity.Find(Name);
					const TSet<FString>* Pairs = PairsByActivity.Find(Name);
					const bool bActivityAgrees = ResolvedActivity.Equals(Name, ESearchCase::IgnoreCase);
					// One witnessed pair under a deterministic selector is a committed answer, not a
					// sample: the resolver has to land on that clip and no other.
					const bool bExact = bHeaviest && Pairs != nullptr && Pairs->Num() == 1;
					const FString Landed = FString::Printf(TEXT("%s|%s"),
						*Out.SequenceLabel.ToLower(), *Out.OwnerStem);
					const bool bPlaceAgrees = bExact
						? Pairs->Contains(Landed)
						: (Owners != nullptr && Owners->Contains(Out.OwnerStem));
					if (!bPlaceAgrees || !bActivityAgrees)
					{
						Selection.Add(FString::Printf(
							TEXT("%s [%s, %s] -> retail %s/%s; export %s/%s (%s)"),
							*Where, *Name, bHeaviest ? TEXT("heaviest") : TEXT("weighted"),
							*Row.OwnerStem, *Row.Label, *Out.OwnerStem, *Out.SequenceLabel,
							*ResolvedActivity));
					}
				}
			}

			// (C) the exact-label route, for the labels retail looked up by name.
			// A label several banks declare resolves to the include tree's first through this
			// route, which is retail's own answer only when retail's owner IS that first; a
			// later owner's copy is reachable by activity, not by bare name.
			if (Row.Has(TEXT("vampire.lookup_sequence")) && Set.Find(Row.Label) != nullptr
				&& Set.Find(Row.Label)->Owner.Equals(Row.OwnerStem, ESearchCase::IgnoreCase))
			{
				const FElysiumAnimationSelection Out = ResolveOn(Set, Tables, BodyKind,
					EElysiumAnimRoute::ExactLabel, Spelling.IsEmpty() ? Row.Label : Spelling);
				if (!Out.OwnerStem.Equals(Row.OwnerStem, ESearchCase::IgnoreCase))
				{
					Exact.Add(FString::Printf(TEXT("%s -> retail %s; export %s"),
						*Where, *Row.OwnerStem, *Out.OwnerStem));
				}
			}
		}
	}

	if (BodiesChecked == 0)
	{
		AddInfo(FString::Printf(TEXT("ELYSIUM_TEST_ABSTAIN: none of the %d oracle bodies is exported (%s)"),
			Bodies.Num(), *FString::Join(BodiesNotExported, TEXT(", "))));
		return true;
	}
	AddInfo(FString::Printf(TEXT("oracle %s: %d bodies, %d rows replayed%s"), *OraclePath,
		BodiesChecked, RowsChecked,
		BodiesNotExported.IsEmpty()
			? TEXT("")
			: *FString::Printf(TEXT("; not exported: %s"), *FString::Join(BodiesNotExported, TEXT(", ")))));
	Vocabulary.Report(*this);
	Activity.Report(*this);
	Selection.Report(*this);
	Exact.Report(*this);
	return true;
}
