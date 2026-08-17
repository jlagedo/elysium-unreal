#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "Visual/ElysiumActionTables.h"
#include "Visual/ElysiumNpcClips.h"

// LIFE2 — the recovered weapon activity-translation tables as project source.
//
// Two levels, and they prove different things. The Substrate suite is content-free: it expands the
// committed model and requires the recovered row stream back, byte for byte, which is what catches a
// bad regeneration or a hand-edit. The Content suite walks every ladder against the exported clip
// vocabulary and requires the *behaviour* the decode was read for — the fallback order carries real
// weight, no rewrite kind is dead — which is what catches a table transcribed perfectly and
// interpreted wrongly. A byte diff against the binary would pass that second case; this does not.

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

	if (FElysiumContentPaths::IsIncomplete(TEXT("npc")))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the npc export domain is marked incomplete"));
		return true;
	}

	FElysiumNpcIndex Index;
	FString Error;
	if (!Index.Load(Error) || !Index.IsValid())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no exported npc index (run: uv run elysium export grid)"));
		return true;
	}

	// The corpus vocabulary is the union of every exported body's, because the claim under test is
	// about the tables rather than about one character: a weapon's ladder is asked whether any
	// shipped model can play what it names.
	//
	// **Matched case-insensitively, deliberately.** The tables carry the DLL's registered literals,
	// which are upper case throughout, while the shipped models author 91 of their activities in
	// mixed case (`ACT_ALERT_180_INTO_Katana`, `ACT_MELEE_ATTACK_sledgehammer`). A case-sensitive
	// read leaves that content unreachable and costs the walk 228 resolutions. This is the rule the
	// resolver already uses — `FElysiumNpcClipSet::ByActivity` compares `IgnoreCase` — so the walk
	// has to use it too, or conformance would be measured against a resolver nobody runs.
	TSet<FString> Vocabulary;
	int32 Bodies = 0;
	for (const TPair<FString, FElysiumNpcIndexEntry>& Pair : Index.Npcs)
	{
		FString StemError;
		if (FElysiumNpcClipSet::LoadActivities(Pair.Key, Vocabulary, StemError))
		{
			++Bodies;
		}
		else
		{
			AddWarning(FString::Printf(TEXT("no clip vocabulary for '%s': %s"), *Pair.Key,
				*StemError));
		}
	}
	if (Bodies == 0)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the export carries no clip vocabularies"));
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

#endif  // WITH_DEV_AUTOMATION_TESTS
