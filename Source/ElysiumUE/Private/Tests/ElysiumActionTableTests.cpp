#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "Visual/ElysiumActionTables.h"
#include "Visual/ElysiumNpcClips.h"

// LIFE2 — the recovered activity-translation tables and player action rules as project source.
//
// Two levels, and they prove different things. A Substrate suite is content-free: it walks the
// committed model and requires the recovered shape back, which is what catches a bad regeneration or
// a hand-edit. A Content suite walks the same model against the exported clip vocabulary and
// requires the *behaviour* the decode was read for — the weapon fallback order carries real weight,
// no rewrite kind is dead, every activity the player surface asks for is one a shipped model can
// answer — which is what catches a table transcribed perfectly and interpreted wrongly. A byte diff
// against the binary would pass that second case; these do not.

static constexpr EAutomationTestFlags GElysiumActionTableFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	bool SequenceContains(const ElysiumActionTables::FActionBlock& Block, const FString& Base)
	{
		for (int32 Index = 0; Index < Block.BaseCount; ++Index)
		{
			if (Base.Equals(Block.Bases[Index], ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	bool AnySequenceContains(const FString& Base)
	{
		for (const ElysiumActionTables::FWeaponLadder& Ladder : ElysiumActionTables::WeaponLadders())
		{
			for (int32 Index = 0; Index < Ladder.BlockCount; ++Index)
			{
				if (SequenceContains(Ladder.Blocks[Index], Base))
				{
					return true;
				}
			}
		}
		return false;
	}

	const TCHAR* KindName(ElysiumActionTables::ERewriteKind Kind)
	{
		using ElysiumActionTables::ERewriteKind;
		switch (Kind)
		{
		case ERewriteKind::Identity:   return TEXT("identity");
		case ERewriteKind::Append:     return TEXT("append");
		case ERewriteKind::Substitute: return TEXT("substitute");
		case ERewriteKind::Rename:     return TEXT("rename");
		case ERewriteKind::Exception:  return TEXT("exception");
		}
		return TEXT("?");
	}

	// One body's worth of vocabulary, in the shape `Translate` asks for.
	auto Carrying(const TSet<FString>& Activities)
	{
		return [&Activities](const FString& Activity) { return Activities.Contains(Activity); };
	}

	enum class ECorpus : uint8
	{
		Loaded,
		Incomplete,
		NoIndex,
		NoBodies,
	};

	// The union of every exported body's activity vocabulary. Both Content suites here ask the same
	// question of it — whether *some* shipped model can play what a table names — so they load it the
	// same way.
	//
	// **Matched case-insensitively, deliberately.** The tables carry the DLL's registered literals,
	// which are upper case throughout, while the shipped models author 91 of their activities in
	// mixed case (`ACT_ALERT_180_INTO_Katana`, `ACT_MELEE_ATTACK_sledgehammer`). A case-sensitive
	// read leaves that content unreachable and costs the weapon walk 228 resolutions. This is the
	// rule the resolver already uses — `FElysiumNpcClipSet::ByActivity` compares `IgnoreCase`, and so
	// does `TSet<FString>` — so the walk has to use it too, or conformance would be measured against
	// a resolver nobody runs.
	ECorpus LoadCorpusVocabulary(FAutomationTestBase& Test, TSet<FString>& OutVocabulary,
		int32& OutBodies)
	{
		OutVocabulary.Reset();
		OutBodies = 0;

		if (FElysiumContentPaths::IsIncomplete(TEXT("npc")))
		{
			return ECorpus::Incomplete;
		}

		FElysiumNpcIndex Index;
		FString Error;
		if (!Index.Load(Error) || !Index.IsValid())
		{
			return ECorpus::NoIndex;
		}

		for (const TPair<FString, FElysiumNpcIndexEntry>& Pair : Index.Npcs)
		{
			FString StemError;
			if (FElysiumNpcClipSet::LoadActivities(Pair.Key, OutVocabulary, StemError))
			{
				++OutBodies;
			}
			else
			{
				Test.AddWarning(FString::Printf(TEXT("no clip vocabulary for '%s': %s"), *Pair.Key,
					*StemError));
			}
		}
		return OutBodies > 0 ? ECorpus::Loaded : ECorpus::NoBodies;
	}

	// Reports the abstention and answers whether the caller should stop.
	bool AbstainedOnCorpus(FAutomationTestBase& Test, ECorpus Status)
	{
		switch (Status)
		{
		case ECorpus::Loaded:
			return false;
		case ECorpus::Incomplete:
			Test.AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the npc export domain is marked incomplete"));
			return true;
		case ECorpus::NoIndex:
			Test.AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no exported npc index "
				"(run: uv run elysium export grid)"));
			return true;
		case ECorpus::NoBodies:
			break;
		}
		Test.AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the export carries no clip vocabularies"));
		return true;
	}

	// A predicate answer set, so a rule walk in a test reads as the player state it stands for.
	// Everything except the two operand predicates is a plain membership test.
	struct FPlayerState
	{
		TArray<ElysiumActionTables::EPlayerPredicate> Holds;
		int32 Phase = 0;
		int32 Ideal = 0;

		bool operator()(ElysiumActionTables::EPlayerPredicate Predicate, int32 Operand) const
		{
			using ElysiumActionTables::EPlayerPredicate;
			if (Predicate == EPlayerPredicate::JumpPhase)
			{
				return Operand == Phase;
			}
			if (Predicate == EPlayerPredicate::IdealActivityIs)
			{
				return Operand == Ideal;
			}
			return Holds.Contains(Predicate);
		}
	};

	// What an ordered list answers for one state: the row's activity, or empty when nothing
	// overrides what the selector already had.
	FString Answer(TArrayView<const ElysiumActionTables::FPlayerRule> Rules,
		const FPlayerState& State)
	{
		const ElysiumActionTables::FPlayerRule* Rule = ElysiumActionTables::SelectRule(Rules,
			[&State](ElysiumActionTables::EPlayerPredicate Predicate, int32 Operand)
			{ return State(Predicate, Operand); });
		if (Rule == nullptr || Rule->Activity == nullptr)
		{
			return FString();
		}
		return FString(Rule->Activity);
	}
}

// =====================================================================================
// Round trip and well-formedness. No game files: the committed model is the whole input.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWeaponActivityTablesTest,
	"Elysium.Substrate.WeaponActivityTables", GElysiumActionTableFlags)
bool FElysiumWeaponActivityTablesTest::RunTest(const FString&)
{
	using namespace ElysiumActionTables;

	const FActionTableCensus& Stored = Census();

	// --- the round trip: the compressed model is the recovered stream ---------------------------
	{
		TArray<FExpandedRow> Rows;
		ExpandAll(Rows);

		TestEqual(TEXT("the model expands to the recovered row count"), Rows.Num(),
			Stored.RetailRows);
		TestEqual(TEXT("and to 9,214 rows"), Rows.Num(), 9214);

		// The digest was taken from the retail decode at generation time and is recomputed here
		// from the committed model, so the two agree only if the compression is lossless.
		const uint64 Digest = DigestOf(Rows);
		TestTrue(FString::Printf(
			TEXT("the expansion digests to the recovered row stream (0x%016llx against 0x%016llx)"),
			Digest, Stored.RetailRowDigest), Digest == Stored.RetailRowDigest);

		int32 RequiredRows = 0;
		for (const FExpandedRow& Row : Rows)
		{
			RequiredRows += Row.bRequired ? 1 : 0;
		}
		TestEqual(TEXT("the `required` column survives the round trip"), RequiredRows,
			Stored.RetailRequired);
		TestEqual(TEXT("which is 201 flagged rows"), RequiredRows, 201);

		// "In the recovered order" has two halves: each class's rows are one contiguous run, and
		// the runs are in the order the RTTI decode reports. A shuffled emission passes a count.
		TArray<FString> Order;
		for (const FExpandedRow& Row : Rows)
		{
			if (Order.Num() == 0 || Order.Last() != Row.CppClass)
			{
				TestFalse(FString::Printf(TEXT("%s's rows are one contiguous run"), *Row.CppClass),
					Order.Contains(Row.CppClass));
				Order.Add(Row.CppClass);
			}
		}
		TestEqual(TEXT("over 61 weapon classes"), Order.Num(), Stored.WeaponClasses);
		// Ordinal, not `FString::operator<`: the generator emits the RTTI decode's own ascending
		// class order, and that is a byte compare rather than a case-folded one.
		for (int32 Index = 1; Index < Order.Num(); ++Index)
		{
			TestTrue(FString::Printf(TEXT("class order is the decode's: %s before %s"),
				*Order[Index - 1], *Order[Index]),
				FCString::Strcmp(*Order[Index - 1], *Order[Index]) < 0);
		}
	}

	// --- well-formedness: what a malformed regeneration would break -----------------------------
	{
		TSet<const TCHAR* const*> Sequences;
		int32 Blocks = 0;
		int32 Exceptions = 0;

		for (const FWeaponLadder& Ladder : WeaponLadders())
		{
			const FString ClassName(Ladder.CppClass);
			TestTrue(FString::Printf(TEXT("%s's ladder is non-empty"), *ClassName),
				Ladder.BlockCount > 0 && Ladder.Blocks != nullptr);
			Blocks += Ladder.BlockCount;

			for (int32 Index = 0; Index < Ladder.BlockCount; ++Index)
			{
				const FActionBlock& Block = Ladder.Blocks[Index];
				TestTrue(FString::Printf(TEXT("%s block %d walks a base sequence"), *ClassName,
					Index), Block.Bases != nullptr && Block.BaseCount > 0);
				TestTrue(FString::Printf(TEXT("%s block %d names a family (empty is a value)"),
					*ClassName, Index), Block.Family != nullptr);
				Sequences.Add(Block.Bases);

				for (int32 Flag = 0; Flag < Block.RequiredCount; ++Flag)
				{
					TestTrue(FString::Printf(TEXT("%s block %d `required` position %d is in range"),
						*ClassName, Index, Flag),
						Block.RequiredPositions[Flag] >= 0 &&
						Block.RequiredPositions[Flag] < Block.BaseCount);
					TestTrue(FString::Printf(
						TEXT("%s block %d `required` positions ascend"), *ClassName, Index),
						Flag == 0 || Block.RequiredPositions[Flag - 1] < Block.RequiredPositions[Flag]);
				}
			}

			Exceptions += Ladder.ExceptionCount;
			for (int32 Index = 0; Index < Ladder.ExceptionCount; ++Index)
			{
				const FActionException& Row = Ladder.Exceptions[Index];
				const FString Base(Row.Base);
				if (!TestTrue(FString::Printf(TEXT("%s exception %d addresses a real block"),
					*ClassName, Index), Row.Block >= 0 && Row.Block < Ladder.BlockCount))
				{
					continue;
				}
				TestTrue(FString::Printf(TEXT("%s exception %d's base `%s` is in block %d"),
					*ClassName, Index, *Base, Row.Block),
					SequenceContains(Ladder.Blocks[Row.Block], Base));
				// The lookup binary-searches, so the emitted order is load-bearing rather than
				// cosmetic: an unsorted array silently answers null for a row that is present.
				if (Index > 0)
				{
					const FActionException& Prev = Ladder.Exceptions[Index - 1];
					const bool bSorted = Prev.Block < Row.Block ||
						(Prev.Block == Row.Block && FCString::Strcmp(Prev.Base, Row.Base) < 0);
					TestTrue(FString::Printf(TEXT("%s's exceptions are sorted by (block, base)"),
						*ClassName), bSorted);
				}
				TestNotNull(FString::Printf(TEXT("%s exception %d is reachable by lookup"),
					*ClassName, Index), FindException(Ladder, Row.Block, Base));
			}
		}

		TestEqual(TEXT("the ladders carry the stored block count"), Blocks, Stored.Blocks);
		TestEqual(TEXT("which is 110 blocks"), Blocks, 110);
		TestEqual(TEXT("the ladders carry the stored exception count"), Exceptions,
			Stored.Exceptions);
		TestEqual(TEXT("110 blocks draw on 18 shared base sequences"), Sequences.Num(),
			Stored.BaseSequences);

		int32 Entries = 0;
		int32 RequiredFlags = 0;
		for (const FWeaponLadder& Ladder : WeaponLadders())
		{
			for (int32 Index = 0; Index < Ladder.BlockCount; ++Index)
			{
				const FActionBlock& Block = Ladder.Blocks[Index];
				if (Sequences.Remove(Block.Bases) > 0)
				{
					Entries += Block.BaseCount;
					RequiredFlags += Block.RequiredCount;
				}
			}
		}
		TestEqual(TEXT("the sequences hold the stored entry count"), Entries, Stored.BaseEntries);
		TestEqual(TEXT("and the stored `required` flags"), RequiredFlags, Stored.RequiredFlags);
	}

	// --- no dead rewrite rule --------------------------------------------------------------------
	{
		TestEqual(TEXT("three rename rules"), RenameRules().Num(), 3);
		for (const FActionRename& Rule : RenameRules())
		{
			TestTrue(FString::Printf(TEXT("rename base `%s` is a base some ladder walks"),
				Rule.Base), AnySequenceContains(FString(Rule.Base)));
		}
		TestEqual(TEXT("eight substitute bases"), SubstituteBases().Num(), 8);
		for (const TCHAR* const& Base : SubstituteBases())
		{
			TestTrue(FString::Printf(TEXT("substitute base `%s` is a base some ladder walks"),
				Base), AnySequenceContains(FString(Base)));
		}
	}

	// --- the rewrite itself -----------------------------------------------------------------------
	{
		TestEqual(TEXT("the ordinary case appends the family"),
			Rewrite(TEXT("ACT_RUN"), TEXT("KATANA")), FString(TEXT("ACT_RUN_KATANA")));
		TestEqual(TEXT("an empty family translates a base to itself"),
			Rewrite(TEXT("ACT_RUN"), TEXT("")), FString(TEXT("ACT_RUN")));
		TestEqual(TEXT("`ACT_AIM` is renamed rather than decorated"),
			Rewrite(TEXT("ACT_AIM"), TEXT("KATANA")), FString(TEXT("ACT_READY_KATANA")));
		TestEqual(TEXT("and so is the ranged attack pair"),
			Rewrite(TEXT("ACT_RANGE_ATTACK1_LAYER"), TEXT("GLOCK")),
			FString(TEXT("ACT_RANGE_ATTACK_LAYER_GLOCK")));

		// The `_BACK` pair, which is the whole reason the rewrite has two kinds rather than one:
		// the same trailing token is a family slot in one family and a literal direction in the
		// other, and both readings have to live in the same table.
		TestEqual(TEXT("a sneak attack's `_BACK` is a family slot the family replaces"),
			Rewrite(TEXT("ACT_SNEAKATTACK_SUCCESS_ATTACKER_SHORTVICTIM_BACK"), TEXT("KATANA")),
			FString(TEXT("ACT_SNEAKATTACK_SUCCESS_ATTACKER_SHORTVICTIM_KATANA")));
		TestEqual(TEXT("a knockback's `_BACK` is a direction the family follows"),
			Rewrite(TEXT("ACT_KNOCKBACK_SMALL_LOW_BACK"), TEXT("KATANA")),
			FString(TEXT("ACT_KNOCKBACK_SMALL_LOW_BACK_KATANA")));
		TestEqual(TEXT("and the two are different kinds"),
			static_cast<int32>(KindOf(TEXT("ACT_KNOCKBACK_SMALL_LOW_BACK"), TEXT("KATANA"))),
			static_cast<int32>(ERewriteKind::Append));
		TestEqual(TEXT("not one rule guessed from the suffix"),
			static_cast<int32>(KindOf(TEXT("ACT_SNEAKATTACK_SUCCESS_ATTACKER_SHORTVICTIM_BACK"),
				TEXT("KATANA"))), static_cast<int32>(ERewriteKind::Substitute));
	}

	// --- the ladder, and the walk over it ---------------------------------------------------------
	{
		const FWeaponLadder* Katana = FindLadderByEntityClass(TEXT("item_w_katana"));
		if (!TestNotNull(TEXT("`item_w_katana` reaches a ladder"), Katana))
		{
			return false;
		}
		TestEqual(TEXT("which is the katana's own class"), FString(Katana->CppClass),
			FString(TEXT("CWeaponMelee_Katana")));
		TestEqual(TEXT("the katana ladder is three rungs"), Katana->BlockCount, 3);
		TestEqual(TEXT("rung 1 is the weapon's own animation set"),
			FString(Katana->Blocks[0].Family), FString(TEXT("KATANA")));
		TestEqual(TEXT("rung 2 the shared class set"), FString(Katana->Blocks[1].Family),
			FString(TEXT("MELEESHARED_ONEHAND")));
		TestEqual(TEXT("rung 3 a cousin weapon"), FString(Katana->Blocks[2].Family),
			FString(TEXT("BASEBALLBAT")));

		// A body that carries only the shared set falls through rung 1 into rung 2, which is the
		// whole point of retaining the order.
		const TSet<FString> Shared = { FString(TEXT("ACT_RUN_MELEESHARED_ONEHAND")) };
		const FTranslation Fallback = Translate(*Katana, TEXT("ACT_RUN"), Carrying(Shared));
		TestTrue(TEXT("the shared set answers the katana's run"), Fallback.bTranslated);
		TestEqual(TEXT("at rung 2"), Fallback.Rung, 2);
		TestEqual(TEXT("with the shared activity"), Fallback.Activity,
			FString(TEXT("ACT_RUN_MELEESHARED_ONEHAND")));
		TestEqual(TEXT("over three applicable rungs"), Fallback.ApplicableRungs, 3);

		const TSet<FString> Own = { FString(TEXT("ACT_RUN_KATANA")),
			FString(TEXT("ACT_RUN_MELEESHARED_ONEHAND")) };
		const FTranslation First = Translate(*Katana, TEXT("ACT_RUN"), Carrying(Own));
		TestEqual(TEXT("a body carrying both takes the weapon's own rung"), First.Rung, 1);
		TestEqual(TEXT("and its activity"), First.Activity, FString(TEXT("ACT_RUN_KATANA")));

		// A miss is the untranslated base, which is the same answer retail's own empty table gives.
		const TSet<FString> Nothing;
		const FTranslation Miss = Translate(*Katana, TEXT("ACT_RUN"), Carrying(Nothing));
		TestFalse(TEXT("a body carrying neither translates nothing"), Miss.bTranslated);
		TestEqual(TEXT("and keeps the base it was asked for"), Miss.Activity,
			FString(TEXT("ACT_RUN")));
		TestEqual(TEXT("with no answering rung"), Miss.Rung, 0);

		// An exception is a literal the family does not produce; the unarmed table is where the
		// relaxed gaits collapse onto the plain ones.
		const FWeaponLadder* Unarmed = FindLadderByEntityClass(TEXT("item_w_unarmed"));
		if (TestNotNull(TEXT("`item_w_unarmed` reaches a ladder"), Unarmed))
		{
			const TCHAR* Literal = FindException(*Unarmed, 0, TEXT("ACT_RUN_RELAXED"));
			if (TestNotNull(TEXT("the unarmed table states a literal for ACT_RUN_RELAXED"),
				Literal))
			{
				TestEqual(TEXT("and it is the plain run"), FString(Literal),
					FString(TEXT("ACT_RUN")));
			}
			const TSet<FString> Plain = { FString(TEXT("ACT_RUN")) };
			const FTranslation Exception = Translate(*Unarmed, TEXT("ACT_RUN_RELAXED"),
				Carrying(Plain));
			TestTrue(TEXT("so an unarmed relaxed run resolves"), Exception.bTranslated);
			TestEqual(TEXT("through the exception rather than the family"),
				static_cast<int32>(Exception.Kind), static_cast<int32>(ERewriteKind::Exception));
		}

		// A class with no table is not an error: 108 of the 169 subclasses carry an empty one and
		// translate nothing, exactly as an unarmed body does.
		TestNull(TEXT("a class carrying no table has no ladder"),
			FindLadder(TEXT("CWeaponMelee_NotAWeapon")));
	}

	return true;
}

// =====================================================================================
// Behavioural conformance, against the exported clip vocabulary.
//
// This is the level a byte diff cannot reach. A table transcribed perfectly but read wrongly —
// the wrong rewrite kind on a base, a ladder walked back to front — reproduces the binary and
// still resolves the wrong pose, or nothing. What separates the two is whether the *content*
// agrees: whether the fallback order carries weight, and whether every rewrite kind names
// activities that shipped models actually carry.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumActionTableConformanceTest,
	"Elysium.Content.ActionTableConformance", GElysiumActionTableFlags)
bool FElysiumActionTableConformanceTest::RunTest(const FString&)
{
	using namespace ElysiumActionTables;

	// The corpus vocabulary is the union of every exported body's, because the claim under test is
	// about the tables rather than about one character: a weapon's ladder is asked whether any
	// shipped model can play what it names.
	TSet<FString> Vocabulary;
	int32 Bodies = 0;
	if (AbstainedOnCorpus(*this, LoadCorpusVocabulary(*this, Vocabulary, Bodies)))
	{
		return true;
	}

	// Ordinal, because `FString`'s own comparison is the case-insensitive one this line exists to
	// measure the effect of.
	int32 MixedCase = 0;
	for (const FString& Activity : Vocabulary)
	{
		MixedCase += FCString::Strcmp(*Activity, *Activity.ToUpper()) != 0 ? 1 : 0;
	}
	AddInfo(FString::Printf(
		TEXT("corpus: %d bodies, %d distinct activities, %d of them not upper case"), Bodies,
		Vocabulary.Num(), MixedCase));

	// --- walk every ladder ------------------------------------------------------------------------
	TMap<int32, int32> ByRung;
	TMap<FString, int32> ResolvedByKind;
	TMap<FString, int32> CandidatesByKind;
	TMap<FString, int32> UnresolvedByGroup;
	int32 Requests = 0;
	int32 Resolved = 0;
	int32 DeeperThanFirst = 0;
	int32 UnresolvedWithBareBase = 0;
	int32 VmRequests = 0;
	int32 VmResolved = 0;
	int32 DirectionalRequests = 0;
	int32 DirectionalResolved = 0;

	auto GroupOf = [](const FString& Base) -> FString
	{
		if (Base.StartsWith(TEXT("ACT_VM_")))
		{
			return TEXT("viewmodel ACT_VM_*");
		}
		FString Tail;
		if ((Base.StartsWith(TEXT("ACT_WALK_")) || Base.StartsWith(TEXT("ACT_RUN_"))) &&
			Base.Split(TEXT("_"), nullptr, &Tail, ESearchCase::CaseSensitive,
				ESearchDir::FromEnd) &&
			Tail.IsNumeric())
		{
			return TEXT("directional walk/run");
		}
		FString Family = Base;
		Family.RemoveFromStart(TEXT("ACT_"));
		int32 Underscore = INDEX_NONE;
		if (Family.FindChar(TEXT('_'), Underscore))
		{
			Family.LeftInline(Underscore);
		}
		return Family;
	};

	TArray<FString> Bases;
	for (const FWeaponLadder& Ladder : WeaponLadders())
	{
		CollectBases(Ladder, Bases);
		for (const FString& Base : Bases)
		{
			++Requests;
			const FString Group = GroupOf(Base);
			const bool bViewmodel = Group == TEXT("viewmodel ACT_VM_*");
			const bool bDirectional = Group == TEXT("directional walk/run");
			VmRequests += bViewmodel ? 1 : 0;
			DirectionalRequests += bDirectional ? 1 : 0;

			// Every candidate the walk would test, so a kind that never names a shipped activity
			// is visible as a kind rather than as a hole in the totals.
			for (int32 BlockIndex = 0; BlockIndex < Ladder.BlockCount; ++BlockIndex)
			{
				const FActionBlock& Block = Ladder.Blocks[BlockIndex];
				if (!SequenceContains(Block, Base))
				{
					continue;
				}
				const FString Family(Block.Family);
				const bool bException = FindException(Ladder, BlockIndex, Base) != nullptr;
				const FString Kind(KindName(bException ? ERewriteKind::Exception
					: KindOf(Base, Family)));
				CandidatesByKind.FindOrAdd(Kind) += 1;
			}

			const FTranslation Result = Translate(Ladder, Base, Carrying(Vocabulary));
			if (Result.bTranslated)
			{
				++Resolved;
				ByRung.FindOrAdd(Result.Rung) += 1;
				ResolvedByKind.FindOrAdd(FString(KindName(Result.Kind))) += 1;
				DeeperThanFirst += Result.Rung > 1 ? 1 : 0;
				VmResolved += bViewmodel ? 1 : 0;
				DirectionalResolved += bDirectional ? 1 : 0;
			}
			else
			{
				UnresolvedByGroup.FindOrAdd(Group) += 1;
				UnresolvedWithBareBase += Vocabulary.Contains(Base) ? 1 : 0;
			}
		}
	}

	// --- report -----------------------------------------------------------------------------------
	AddInfo(FString::Printf(TEXT("requests: %d over %d ladders; %d resolved, %d never"), Requests,
		WeaponLadders().Num(), Resolved, Requests - Resolved));
	TArray<int32> Rungs;
	ByRung.GetKeys(Rungs);
	Rungs.Sort();
	for (const int32 Rung : Rungs)
	{
		AddInfo(FString::Printf(TEXT("  rung %d: %d"), Rung, ByRung[Rung]));
	}
	for (const TPair<FString, int32>& Pair : CandidatesByKind)
	{
		AddInfo(FString::Printf(TEXT("  kind %-11s %5d candidates, %4d answered"), *Pair.Key,
			Pair.Value, ResolvedByKind.FindRef(Pair.Key)));
	}

	// Risk 2 in the LIFE2 plan: a never-resolving request is either content the corpus does not
	// carry or a misdecoded rule, and the two are told apart by whether the misses cluster on a
	// rewrite kind. They are named here rather than counted, so the residual is a list.
	UnresolvedByGroup.ValueSort([](int32 Left, int32 Right) { return Left > Right; });
	for (const TPair<FString, int32>& Pair : UnresolvedByGroup)
	{
		AddInfo(FString::Printf(TEXT("  never resolves: %-22s %5d"), *Pair.Key, Pair.Value));
	}
	AddInfo(FString::Printf(
		TEXT("  of the %d never-resolving requests, %d have their bare base in the corpus and no "
			"weapon-decorated form"), Requests - Resolved, UnresolvedWithBareBase));

	// --- the acceptance clauses -------------------------------------------------------------------
	if (!TestTrue(TEXT("the corpus answers some part of the tables"), Resolved > 0))
	{
		return false;
	}

	// The fallback order is load-bearing. If it were decoration, a front-to-back walk and a
	// first-row-wins read would agree, and the tables could be flattened to one row per base.
	const float DeepShare = static_cast<float>(DeeperThanFirst) / static_cast<float>(Resolved);
	AddInfo(FString::Printf(TEXT("  %d of %d resolutions come from rung 2 or later (%.1f%%)"),
		DeeperThanFirst, Resolved, 100.0f * DeepShare));
	TestTrue(TEXT("more than a third of resolutions come from rung 2 or later"),
		DeepShare >= 0.33f);
	TestTrue(TEXT("and the ladder reaches past rung 2"), ByRung.FindRef(3) > 0);

	// No rewrite kind is dead. A misdecoded rule produces candidates that no shipped model carries,
	// so it shows up here as a kind at 0% rather than as a wrong pose in play.
	for (const TPair<FString, int32>& Pair : CandidatesByKind)
	{
		TestTrue(FString::Printf(TEXT("rewrite kind `%s` answers at least one request (%d "
			"candidates)"), *Pair.Key, Pair.Value), ResolvedByKind.FindRef(Pair.Key) > 0);
	}

	// The two families that sit at exactly 0%, named rather than silent. Both are content facts:
	// the NPC corpus carries no viewmodel bodies, and `ACT_WALK_45` exists in no shipped model even
	// unsuffixed. The viewmodel clause is expected to change when LIFE6 exports viewmodel bodies —
	// a failure here then is the corpus growing, not the tables breaking.
	TestTrue(TEXT("the tables do ask for viewmodel activities"), VmRequests > 0);
	TestEqual(FString::Printf(
		TEXT("and none of the %d viewmodel requests resolve: the NPC corpus has no viewmodel "
			"bodies"), VmRequests), VmResolved, 0);
	TestTrue(TEXT("the tables do ask for directional walk/run"), DirectionalRequests > 0);
	TestEqual(FString::Printf(
		TEXT("and none of the %d directional requests resolve: `ACT_WALK_45` exists in no shipped "
			"model even unsuffixed"), DirectionalRequests), DirectionalResolved, 0);

	return true;
}

// =====================================================================================
// The player action rules. Well-formedness and the walk over them, with no game files: the
// committed rules are the whole input.
//
// The selector this stands for is code rather than a table, so what a bad regeneration breaks is
// not a row count but an *order* — a ladder whose unconditional row moved up answers the same
// activity for every state, and a jump arm whose landing rows lost their gait deferral drops a
// running body into a land. The walks below are what catch that.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlayerActionRulesTest,
	"Elysium.Substrate.PlayerActionRules", GElysiumActionTableFlags)
bool FElysiumPlayerActionRulesTest::RunTest(const FString&)
{
	using namespace ElysiumActionTables;

	const FPlayerActionCensus& Stored = PlayerCensus();

	// --- the compiled vocabulary ------------------------------------------------------------------
	{
		TestEqual(TEXT("the emission carries the stored code count"), PlayerActions().Num(),
			Stored.Actions);
		TestEqual(TEXT("which is the seventeen compiled `PLAYER_*` codes"), PlayerActions().Num(),
			17);
		TestEqual(TEXT("and the predicate vocabulary it was generated against"), Stored.Predicates,
			static_cast<int32>(EPlayerPredicate::Count));

		TArray<FString> Names;
		int32 Dormant = 0;
		int32 ArmRules = 0;
		for (int32 Index = 0; Index < PlayerActions().Num(); ++Index)
		{
			const FPlayerAction& Action = PlayerActions()[Index];
			const FString Name(Action.Name);
			TestEqual(TEXT("the codes are emitted in table order"), Action.Code, Index);
			TestTrue(FString::Printf(TEXT("`%s` is spelled as the binary registers it"), *Name),
				Name.StartsWith(TEXT("PLAYER_"), ESearchCase::CaseSensitive));
			TestFalse(FString::Printf(TEXT("`%s` is named once"), *Name), Names.Contains(Name));
			Names.Add(Name);
			TestTrue(FString::Printf(TEXT("`%s` names its producer"), *Name),
				Action.Producer != nullptr && FCString::Strlen(Action.Producer) > 0);

			if (Action.Reach == EPlayerReach::Dormant)
			{
				++Dormant;
				// Dormant is a measured absence over the whole pinned surface, so it has to be
				// visible as one: a dormant code with an arm would be a reachable code mislabelled.
				TestEqual(FString::Printf(TEXT("dormant `%s` carries no arm"), *Name),
					Action.RuleCount, 0);
			}
			ArmRules += Action.RuleCount;
			TestTrue(FString::Printf(TEXT("`%s`'s arm and its count agree"), *Name),
				(Action.Rules != nullptr) == (Action.RuleCount > 0));
		}
		TestEqual(TEXT("the emission carries the stored dormant count"), Dormant, Stored.Dormant);
		TestEqual(TEXT("which is exactly four"), Dormant, 4);
		TestEqual(TEXT("and the stored arm-rule count"), ArmRules, Stored.ArmRules);

		// The four, by name: superjump and feed are vocabulary the shipped paths route around, and
		// the aiming pair has no producer at all.
		const TCHAR* const DormantNames[] = { TEXT("PLAYER_SUPERJUMP"), TEXT("PLAYER_FEED"),
			TEXT("PLAYER_START_AIMING"), TEXT("PLAYER_LEAVE_AIMING") };
		for (const TCHAR* Name : DormantNames)
		{
			const FPlayerAction* Action = FindPlayerAction(FString(Name));
			if (TestNotNull(FString::Printf(TEXT("`%s` is a compiled code"), Name), Action))
			{
				TestEqual(FString::Printf(TEXT("`%s` is dormant"), Name),
					static_cast<int32>(Action->Reach), static_cast<int32>(EPlayerReach::Dormant));
			}
		}

		// Codes `0` and `1` have no arm by design: their whole selection is the gait ladder, which
		// runs ahead of the dispatch. Code `4` has none because protected ownership precedes the
		// ordinary selector entirely. All three are reachable regardless, so "no arm" and "dormant"
		// stay different facts.
		const int32 ArmlessCodes[] = { 0, 1, 4 };
		for (const int32 Code : ArmlessCodes)
		{
			const FPlayerAction* Action = FindPlayerAction(Code);
			if (TestNotNull(FString::Printf(TEXT("code %d resolves"), Code), Action))
			{
				TestEqual(FString::Printf(TEXT("`%s` has no distinct arm"), Action->Name),
					Action->RuleCount, 0);
				TestEqual(FString::Printf(TEXT("`%s` is reachable all the same"), Action->Name),
					static_cast<int32>(Action->Reach),
					static_cast<int32>(EPlayerReach::Reachable));
			}
		}
	}

	// --- well-formedness of every row ------------------------------------------------------------
	{
		TMap<FString, int32> IdByName;
		auto CheckActivity = [this, &IdByName](const TCHAR* Activity, int32 Id, const TCHAR* Where)
		{
			// An ID is what joins a row to VtMB's own vocabulary rather than to a spelling, so the
			// pair has to be present or absent together, and one name never carries two IDs.
			if (Activity == nullptr)
			{
				TestEqual(FString::Printf(TEXT("%s: an unnamed activity carries no ID"), Where), Id,
					0);
				return;
			}
			const FString Name(Activity);
			TestTrue(FString::Printf(TEXT("%s: `%s` carries a registered ID"), Where, *Name),
				Id > 0);
			const int32* Seen = IdByName.Find(Name);
			if (Seen != nullptr)
			{
				TestEqual(FString::Printf(TEXT("%s: `%s` keeps one ID"), Where, *Name), Id, *Seen);
			}
			else
			{
				IdByName.Add(Name, Id);
			}
		};

		auto CheckRules = [this, &CheckActivity](TArrayView<const FPlayerRule> Rules,
			const FString& Where)
		{
			for (int32 Index = 0; Index < Rules.Num(); ++Index)
			{
				const FPlayerRule& Rule = Rules[Index];
				const FString At = FString::Printf(TEXT("%s row %d"), *Where, Index);
				CheckActivity(Rule.Activity, Rule.ActivityId, *At);
				CheckActivity(Rule.Layer, Rule.LayerId, *At);

				// `Always` pads the tail. A real predicate after one would be silently skipped by
				// the evaluator, which is the one shape that reads as a stricter row than it is.
				bool bPadding = false;
				bool bReadsOperand = false;
				for (const EPlayerPredicate Predicate : Rule.Predicates)
				{
					if (Predicate == EPlayerPredicate::Always)
					{
						bPadding = true;
						continue;
					}
					TestFalse(FString::Printf(TEXT("%s: `Always` only pads the tail"), *At),
						bPadding);
					TestTrue(FString::Printf(TEXT("%s: predicate is in the vocabulary"), *At),
						static_cast<int32>(Predicate) < static_cast<int32>(EPlayerPredicate::Count));
					bReadsOperand |= Predicate == EPlayerPredicate::JumpPhase ||
						Predicate == EPlayerPredicate::IdealActivityIs;
				}
				TestTrue(FString::Printf(
					TEXT("%s: an operand is set exactly when a predicate reads one"), *At),
					bReadsOperand == (Rule.Operand != 0));
			}
		};

		CheckRules(PlayerGaitLadder(), TEXT("the gait ladder"));
		for (const FPlayerAction& Action : PlayerActions())
		{
			CheckRules(MakeArrayView(Action.Rules, Action.RuleCount), FString(Action.Name));
		}

		TArray<FString> Activities;
		CollectPlayerActivities(Activities);
		TestEqual(TEXT("the surface names the stored activity count"), Activities.Num(),
			Stored.Activities);
		// The two translation targets and the melee-hold ideal are collected as well, and all three
		// are already named by a rule row — so the collected set is exactly the set of activities
		// carrying a registered ID, with nothing named only in prose.
		TestEqual(TEXT("and every one of them is a row's own, ID-carrying activity"),
			Activities.Num(), IdByName.Num());

		// An `IdealActivityIs` row is a step in a retained chain, so its operand has to be an
		// activity the same arm can have put there. A stale operand would leave the chain stuck on
		// its first step with nothing logged.
		for (const FPlayerAction& Action : PlayerActions())
		{
			for (int32 Index = 0; Index < Action.RuleCount; ++Index)
			{
				const FPlayerRule& Rule = Action.Rules[Index];
				bool bReadsIdeal = false;
				for (const EPlayerPredicate Predicate : Rule.Predicates)
				{
					bReadsIdeal |= Predicate == EPlayerPredicate::IdealActivityIs;
				}
				if (!bReadsIdeal)
				{
					continue;
				}
				bool bProduced = false;
				for (int32 Other = 0; Other < Action.RuleCount; ++Other)
				{
					bProduced |= Action.Rules[Other].ActivityId == Rule.Operand;
				}
				TestTrue(FString::Printf(
					TEXT("%s row %d waits on an activity its own arm produces (%d)"), Action.Name,
					Index, Rule.Operand), bProduced);
			}
		}
	}

	// --- the gait ladder --------------------------------------------------------------------------
	{
		TestEqual(TEXT("the ladder carries the stored row count"), PlayerGaitLadder().Num(),
			Stored.GaitRules);

		// The ladder always answers, and only its last row is unconditional. An unconditional row
		// anywhere earlier makes every row after it dead while the table still looks complete.
		for (int32 Index = 0; Index < PlayerGaitLadder().Num(); ++Index)
		{
			bool bUnconditional = true;
			for (const EPlayerPredicate Predicate : PlayerGaitLadder()[Index].Predicates)
			{
				bUnconditional &= Predicate == EPlayerPredicate::Always;
			}
			TestTrue(FString::Printf(TEXT("gait row %d is conditional unless it is the last"),
				Index), bUnconditional == (Index == PlayerGaitLadder().Num() - 1));
		}

		FPlayerState State;
		// Stationary and ducked is a crouch whatever the weapon says — the duck test is inside the
		// stationary arm, ahead of the combat stance.
		State.Holds = { EPlayerPredicate::BelowMoveThreshold, EPlayerPredicate::Ducking,
			EPlayerPredicate::CombatReady };
		TestEqual(TEXT("stationary and ducked crouches"), Answer(PlayerGaitLadder(), State),
			FString(TEXT("ACT_CROUCH")));

		State.Holds = { EPlayerPredicate::BelowMoveThreshold, EPlayerPredicate::CombatReady };
		TestEqual(TEXT("stationary in combat stance aims"), Answer(PlayerGaitLadder(), State),
			FString(TEXT("ACT_AIM")));

		State.Holds = { EPlayerPredicate::BelowMoveThreshold };
		TestEqual(TEXT("stationary otherwise idles"), Answer(PlayerGaitLadder(), State),
			FString(TEXT("ACT_IDLE")));

		// Ducked and moving is a flat two-state pair: sneak at any speed, with no walk/run split and
		// no relaxed variant.
		State.Holds = { EPlayerPredicate::Ducking, EPlayerPredicate::AboveGaitThreshold,
			EPlayerPredicate::Relaxed };
		TestEqual(TEXT("ducked and moving sneaks at any speed"), Answer(PlayerGaitLadder(), State),
			FString(TEXT("ACT_SNEAK")));

		State.Holds = { EPlayerPredicate::AboveGaitThreshold, EPlayerPredicate::Relaxed };
		TestEqual(TEXT("out of combat above the threshold runs relaxed"),
			Answer(PlayerGaitLadder(), State), FString(TEXT("ACT_RUN_RELAXED")));

		State.Holds = { EPlayerPredicate::AboveGaitThreshold, EPlayerPredicate::CombatReady };
		TestEqual(TEXT("in combat it runs plainly"), Answer(PlayerGaitLadder(), State),
			FString(TEXT("ACT_RUN")));

		// `CombatReady` and `Relaxed` are not complements: a morphed body in stance satisfies
		// neither, and keeps the plain gait rather than falling into the relaxed one.
		State.Holds = { EPlayerPredicate::AboveGaitThreshold };
		TestEqual(TEXT("and a body that is neither keeps the plain run"),
			Answer(PlayerGaitLadder(), State), FString(TEXT("ACT_RUN")));

		State.Holds = { EPlayerPredicate::Relaxed };
		TestEqual(TEXT("below the threshold it walks relaxed"), Answer(PlayerGaitLadder(), State),
			FString(TEXT("ACT_WALK_RELAXED")));

		State.Holds = {};
		TestEqual(TEXT("and the ladder always answers"), Answer(PlayerGaitLadder(), State),
			FString(TEXT("ACT_WALK")));
	}

	// --- the arms ---------------------------------------------------------------------------------
	{
		const FPlayerAction* Jump = FindPlayerAction(2);
		if (TestNotNull(TEXT("`PLAYER_JUMP` carries an arm"), Jump))
		{
			const TArrayView<const FPlayerRule> Rules(Jump->Rules, Jump->RuleCount);
			FPlayerState State;

			State.Phase = 1;
			TestEqual(TEXT("phase 1 leaps"), Answer(Rules, State), FString(TEXT("ACT_LEAP")));
			State.Phase = 7;
			TestEqual(TEXT("phase 7 falls"), Answer(Rules, State), FString(TEXT("ACT_FALLING")));

			// The landing phases defer to a moving gait, which is what keeps a running body running
			// through its own landing instead of stopping to play a land.
			State.Phase = 8;
			State.Holds = { EPlayerPredicate::GaitIsWalkOrRun };
			TestEqual(TEXT("a moving body keeps its gait through the landing"), Answer(Rules, State),
				FString());
			State.Holds = { EPlayerPredicate::Ducking };
			TestEqual(TEXT("a ducked one lands crouched"), Answer(Rules, State),
				FString(TEXT("ACT_LAND_CROUCH")));
			State.Holds = {};
			TestEqual(TEXT("and a still one lands"), Answer(Rules, State),
				FString(TEXT("ACT_LAND")));

			State.Phase = 11;
			TestEqual(TEXT("the last phase lands hard"), Answer(Rules, State),
				FString(TEXT("ACT_LAND_HARD")));

			// A phase outside the switch leaves the ladder's answer alone rather than forcing one.
			State.Phase = 12;
			TestNull(TEXT("a phase the switch does not cover overrides nothing"),
				SelectRule(Rules, [&State](EPlayerPredicate Predicate, int32 Operand)
					{ return State(Predicate, Operand); }));
		}

		// Melee replaces the base; ranged only adds a layer, so an armed player keeps walking while
		// the upper body fires. That difference is the whole reason a row carries both fields.
		const FPlayerAction* Attack = FindPlayerAction(5);
		if (TestNotNull(TEXT("`PLAYER_ATTACK1` carries an arm"), Attack))
		{
			const TArrayView<const FPlayerRule> Rules(Attack->Rules, Attack->RuleCount);
			FPlayerState State;
			State.Holds = { EPlayerPredicate::HasActiveWeapon, EPlayerPredicate::MeleeCapable };
			TestEqual(TEXT("a grounded melee swing replaces the base"), Answer(Rules, State),
				FString(TEXT("ACT_MELEE_ATTACK")));
			State.Holds.Add(EPlayerPredicate::Airborne);
			TestEqual(TEXT("and an airborne one takes the air variant"), Answer(Rules, State),
				FString(TEXT("ACT_MELEE_AIR_ATTACK")));

			State.Holds = { EPlayerPredicate::HasActiveWeapon, EPlayerPredicate::RangedCapable };
			const FPlayerRule* Ranged = SelectRule(Rules,
				[&State](EPlayerPredicate Predicate, int32 Operand)
				{ return State(Predicate, Operand); });
			if (TestNotNull(TEXT("a ranged attack answers"), Ranged))
			{
				TestNull(TEXT("and names no base, so the gait stands"), Ranged->Activity);
				TestNotNull(TEXT("adding a layer instead"), Ranged->Layer);
				TestEqual(TEXT("which is the ranged attack layer"), FString(Ranged->Layer),
					FString(TEXT("ACT_RANGE_ATTACK1_LAYER")));
			}

			State.Holds = { EPlayerPredicate::HasActiveWeapon };
			TestNull(TEXT("a weapon that is neither capability selects nothing"),
				SelectRule(Rules, [&State](EPlayerPredicate Predicate, int32 Operand)
					{ return State(Predicate, Operand); }));
		}

		// The two retained chains have the same shape, and it is the shape that matters: the
		// unfinished-clip rows come first, so every later row is already the finished case.
		struct FChain
		{
			int32 Code;
			EPlayerPredicate NotEntered;
			EPlayerPredicate Release;
			const TCHAR* Enter;
			const TCHAR* Loop;
			const TCHAR* Leave;
		};
		const FChain Chains[] =
		{
			{ 7, EPlayerPredicate::NotInPrayer, EPlayerPredicate::PrayerReleased,
				TEXT("ACT_PRAYING_BEGIN"), TEXT("ACT_PRAYING_IDLE"), TEXT("ACT_PRAYING_END") },
			{ 12, EPlayerPredicate::VomitNotEntered, EPlayerPredicate::PurgeComplete,
				TEXT("ACT_VOMIT_INTO"), TEXT("ACT_VOMIT_IDLE"), TEXT("ACT_VOMIT_GETOUT") },
		};
		for (const FChain& Chain : Chains)
		{
			const FPlayerAction* Action = FindPlayerAction(Chain.Code);
			if (!TestNotNull(FString::Printf(TEXT("code %d carries a chain"), Chain.Code), Action))
			{
				continue;
			}
			const TArrayView<const FPlayerRule> Rules(Action->Rules, Action->RuleCount);
			const FString Name(Action->Name);

			FPlayerState State;
			State.Holds = { EPlayerPredicate::SequenceUnfinished, Chain.NotEntered };
			TestEqual(FString::Printf(TEXT("%s enters its chain"), *Name), Answer(Rules, State),
				FString(Chain.Enter));

			State.Holds = { EPlayerPredicate::SequenceUnfinished };
			TestEqual(FString::Printf(TEXT("%s holds while its clip plays"), *Name),
				Answer(Rules, State), FString());

			// The steps are keyed on the stored ideal, so walking them is walking the chain.
			auto IdOf = [&Rules](const TCHAR* Activity) -> int32
			{
				for (const FPlayerRule& Rule : Rules)
				{
					if (Rule.Activity != nullptr && FCString::Stricmp(Rule.Activity, Activity) == 0)
					{
						return Rule.ActivityId;
					}
				}
				return 0;
			};
			State.Holds = {};
			State.Ideal = IdOf(Chain.Enter);
			TestEqual(FString::Printf(TEXT("%s advances into its loop"), *Name),
				Answer(Rules, State), FString(Chain.Loop));

			State.Ideal = IdOf(Chain.Loop);
			TestEqual(FString::Printf(TEXT("%s holds the loop until it is released"), *Name),
				Answer(Rules, State), FString(Chain.Loop));

			State.Holds = { Chain.Release };
			TestEqual(FString::Printf(TEXT("%s leaves on release"), *Name), Answer(Rules, State),
				FString(Chain.Leave));

			State.Holds = {};
			State.Ideal = IdOf(Chain.Leave);
			TestEqual(FString::Printf(TEXT("%s returns to idle"), *Name), Answer(Rules, State),
				FString(TEXT("ACT_IDLE")));
		}
	}

	// --- the pose writes ---------------------------------------------------------------------------
	{
		TestEqual(TEXT("three pose parameters are written"), PlayerPoseWrites().Num(),
			Stored.PoseWrites);

		const TCHAR* Expected[] = { TEXT("move_yaw"), TEXT("aim_yaw"), TEXT("aim_pitch") };
		for (int32 Index = 0; Index < PlayerPoseWrites().Num(); ++Index)
		{
			TestEqual(TEXT("in the selector's own write order"),
				FString(PlayerPoseWrites()[Index].Parameter), FString(Expected[Index]));
		}

		const FPlayerPoseWrite& MoveYaw = PlayerPoseWrites()[0];
		// The one gated write: a stationary body holds its last `move_yaw` rather than returning it
		// to zero, which is why the gate is a value here and not an omission.
		TestEqual(TEXT("`move_yaw` is written only while moving"),
			static_cast<int32>(MoveYaw.Gate), static_cast<int32>(EPlayerPredicate::Moving));
		TestEqual(TEXT("from velocity against facing, so it is right-positive"),
			static_cast<int32>(MoveYaw.Source),
			static_cast<int32>(EPlayerPoseSource::VelocityAgainstFacing));
		TestEqual(TEXT("and it slews rather than snapping"), MoveYaw.SlewDegreesPerSecond,
			PlayerTuning().PoseSlewDegreesPerSecond);
		TestEqual(TEXT("at 720 degrees a second"), MoveYaw.SlewDegreesPerSecond, 720.0f);
		TestEqual(TEXT("inside a 0.3 second window"), PlayerTuning().PoseSlewWindowSeconds, 0.3f);

		// `aim_yaw` is a literal zero on every call in every state. A weapon aim grid is therefore
		// pitch-only, and a remake that drives this parameter is adding a mechanism VtMB never had.
		const FPlayerPoseWrite& AimYaw = PlayerPoseWrites()[1];
		TestEqual(TEXT("`aim_yaw` is a literal"), static_cast<int32>(AimYaw.Source),
			static_cast<int32>(EPlayerPoseSource::Literal));
		TestEqual(TEXT("and the literal is zero"), AimYaw.Value, 0.0f);
		TestEqual(TEXT("written unconditionally"), static_cast<int32>(AimYaw.Gate),
			static_cast<int32>(EPlayerPredicate::Always));
		TestEqual(TEXT("with no slew"), AimYaw.SlewDegreesPerSecond, 0.0f);

		const FPlayerPoseWrite& AimPitch = PlayerPoseWrites()[2];
		TestEqual(TEXT("`aim_pitch` carries an absolute angle"),
			static_cast<int32>(AimPitch.Source),
			static_cast<int32>(EPlayerPoseSource::AimPitchField));
		TestEqual(TEXT("written unconditionally too"), static_cast<int32>(AimPitch.Gate),
			static_cast<int32>(EPlayerPredicate::Always));
	}

	// --- the translations, the latch and the discipline fields --------------------------------------
	{
		TestEqual(TEXT("two player-side translations"), PlayerTranslations().Num(),
			Stored.Translations);
		TestEqual(TEXT("a relaxed walk resolves as a plain one"),
			TranslatePlayerActivity(TEXT("ACT_WALK_RELAXED")), FString(TEXT("ACT_WALK")));
		TestEqual(TEXT("and a relaxed run as a plain run"),
			TranslatePlayerActivity(TEXT("ACT_RUN_RELAXED")), FString(TEXT("ACT_RUN")));
		TestEqual(TEXT("everything else passes through"),
			TranslatePlayerActivity(TEXT("ACT_SNEAK")), FString(TEXT("ACT_SNEAK")));

		// The ladder produces both relaxed forms, so dropping either row would leave an unarmed gait
		// selecting nothing: a player body carries no relaxed sequence, only weapon-suffixed ones.
		for (const FPlayerTranslation& Row : PlayerTranslations())
		{
			bool bProduced = false;
			for (const FPlayerRule& Rule : PlayerGaitLadder())
			{
				bProduced |= Rule.Activity != nullptr &&
					FCString::Stricmp(Rule.Activity, Row.From) == 0;
			}
			TestTrue(FString::Printf(TEXT("the ladder can ask for `%s`"), Row.From), bProduced);
		}

		TestEqual(TEXT("the action latch retains one code"), PlayerTuning().LatchedCode, 12);
		const FPlayerAction* Latched = FindPlayerAction(PlayerTuning().LatchedCode);
		if (TestNotNull(TEXT("which is a compiled code"), Latched))
		{
			TestEqual(TEXT("and it is `PLAYER_VOMIT`"), FString(Latched->Name),
				FString(TEXT("PLAYER_VOMIT")));
		}

		// The effective vdata surface, carried whole: two fields, and the only reason code `12` is
		// reachable from data at all.
		TestEqual(TEXT("both effective `Player_Anim` rows are carried"), PlayerAnimFields().Num(),
			Stored.AnimFields);
		TestEqual(TEXT("which is two"), PlayerAnimFields().Num(), 2);
		for (const FPlayerAnimField& Field : PlayerAnimFields())
		{
			const FPlayerAction* Action = FindPlayerAction(Field.Code);
			if (TestNotNull(FString::Printf(TEXT("`%s` names a compiled code"), Field.Value),
				Action))
			{
				TestEqual(TEXT("and the row agrees with the table"), FString(Action->Name),
					FString(Field.Value));
			}
			TestEqual(TEXT("both rows are `PLAYER_VOMIT`"), FString(Field.Value),
				FString(TEXT("PLAYER_VOMIT")));
			TestTrue(TEXT("read from the vdata search path"),
				FString(Field.Path).StartsWith(TEXT("vdata/")));
		}

		// An unfinished swing refuses an idle or an aim, so a melee attack is not cut short by the
		// ladder's own default answer.
		TestEqual(TEXT("the melee hold refuses two activities"),
			PlayerTuning().MeleeHoldRefuseCount, 2);
		for (int32 Index = 0; Index < PlayerTuning().MeleeHoldRefuseCount; ++Index)
		{
			const FString Refused(PlayerTuning().MeleeHoldRefuses[Index]);
			bool bProduced = false;
			for (const FPlayerRule& Rule : PlayerGaitLadder())
			{
				bProduced |= Rule.Activity != nullptr &&
					Refused.Equals(Rule.Activity, ESearchCase::IgnoreCase);
			}
			TestTrue(FString::Printf(TEXT("the ladder can produce the refused `%s`"), *Refused),
				bProduced);
		}
	}

	return true;
}

// =====================================================================================
// The player rules against the exported clip vocabulary.
//
// The claim is narrow and it is the one that matters: every activity the player surface can ask
// for is an activity a shipped model can answer — directly, or through the weapon table that sits
// between the selector and the body. What does not resolve stays a named list rather than a
// count, because that is the difference between "the corpus does not carry it" and "the rule is
// wrong".
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlayerActionConformanceTest,
	"Elysium.Content.PlayerActionConformance", GElysiumActionTableFlags)
bool FElysiumPlayerActionConformanceTest::RunTest(const FString&)
{
	using namespace ElysiumActionTables;

	TSet<FString> Vocabulary;
	int32 Bodies = 0;
	if (AbstainedOnCorpus(*this, LoadCorpusVocabulary(*this, Vocabulary, Bodies)))
	{
		return true;
	}

	TArray<FString> Activities;
	CollectPlayerActivities(Activities);
	TestEqual(TEXT("the surface names the emitted activity count"), Activities.Num(),
		PlayerCensus().Activities);

	AddInfo(FString::Printf(TEXT("corpus: %d bodies, %d distinct activities; the player rules ask "
		"for %d"), Bodies, Vocabulary.Num(), Activities.Num()));

	int32 Direct = 0;
	int32 Translated = 0;
	TArray<FString> Residual;
	for (const FString& Activity : Activities)
	{
		if (Vocabulary.Contains(Activity))
		{
			++Direct;
			continue;
		}

		// A player base is not always asked of the body as spelled: the weapon table stands between
		// the selector and the model, so `ACT_AIM` is answered as `ACT_READY_<F>` and the two layers
		// as their `_<F>` forms. A base some ladder can translate into something a shipped model
		// carries is resolved, not missing — and the joint is the point, since these are the two
		// tables LIFE2 commits.
		FString Through;
		for (const FWeaponLadder& Ladder : WeaponLadders())
		{
			const FTranslation Result = Translate(Ladder, Activity, Carrying(Vocabulary));
			if (Result.bTranslated)
			{
				Through = FString::Printf(TEXT("%s -> %s"), Ladder.CppClass, *Result.Activity);
				break;
			}
		}
		if (!Through.IsEmpty())
		{
			++Translated;
			AddInfo(FString::Printf(TEXT("  %-38s resolves through the weapon table (%s)"),
				*Activity, *Through));
			continue;
		}
		Residual.Add(Activity);
	}

	AddInfo(FString::Printf(TEXT("  %d resolve directly, %d through the weapon table, %d never"),
		Direct, Translated, Residual.Num()));
	for (const FString& Activity : Residual)
	{
		AddInfo(FString::Printf(TEXT("  never resolves: %s"), *Activity));
	}

	TestTrue(TEXT("the corpus answers the player surface"), Direct > 0);
	// Both joints carry weight: a base the body answers as spelled, and a base the weapon table has
	// to reach. If the second went to zero the player rules would have stopped naming translated
	// bases, which is a decode change rather than a corpus one.
	TestTrue(TEXT("and the weapon table answers the bases the body cannot"), Translated > 0);

	// The residual is one row, and it is content rather than a rule. `ACT_LAND_CROUCH` is what the
	// landing arm asks for while ducked; the retail capture shows the player body's own selection
	// returning nothing for it, so the corpus carrying no clip is the same absence retail has.
	TestEqual(TEXT("exactly one player base resolves nowhere in the corpus"), Residual.Num(), 1);
	if (Residual.Num() == 1)
	{
		TestEqual(TEXT("and it is `ACT_LAND_CROUCH`, which no shipped body carries"), Residual[0],
			FString(TEXT("ACT_LAND_CROUCH")));
	}

	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
