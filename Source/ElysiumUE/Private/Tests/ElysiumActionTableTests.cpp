#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Visual/ElysiumActionTables.h"

// The recovered activity-translation tables and player action rules as project source.
//
// Content-free: the suite walks the committed model and requires the recovered shape back, which is
// what catches a bad regeneration or a hand-edit. Whether a shipped model can actually answer what a
// table names is a property of what the bake wrote, and the character verifier owns it — a byte diff
// against the binary would pass this file's cases, and these do not.

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

	// The NPC side.

	int32 NpcBodyIndex(const TCHAR* Name)
	{
		TArrayView<const ElysiumActionTables::FNpcTranslationBody> Bodies =
			ElysiumActionTables::NpcTranslationBodies();
		for (int32 Index = 0; Index < Bodies.Num(); ++Index)
		{
			if (FCString::Stricmp(Bodies[Index].Name, Name) == 0)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	// The predicates a walk is standing in for. `RunnerVariantIs` is the one operand predicate the
	// caller answers; `CoverContextIs` belongs to the walk, because a `ForceCoverContext` row can
	// change the context out from under it.
	struct FNpcState
	{
		TArray<ElysiumActionTables::ENpcPredicate> Holds;
		int32 Variant = INDEX_NONE;

		bool operator()(ElysiumActionTables::ENpcPredicate Predicate, int32 Operand) const
		{
			if (Predicate == ElysiumActionTables::ENpcPredicate::RunnerVariantIs)
			{
				return Operand == Variant;
			}
			return Holds.Contains(Predicate);
		}
	};

	ElysiumActionTables::FNpcTranslation WalkNpc(const TCHAR* BodyName, const TCHAR* Base,
		const FNpcState& State, const TSet<FString>& Carries, int32 CoverContext = 0)
	{
		return ElysiumActionTables::NpcTranslate(NpcBodyIndex(BodyName), FString(Base), CoverContext,
			[&State](ElysiumActionTables::ENpcPredicate Predicate, int32 Operand)
			{ return State(Predicate, Operand); },
			[&Carries](const FString& Activity) { return Carries.Contains(Activity); });
	}

	const TCHAR* SlotName(ElysiumActionTables::ENpcSlot Slot)
	{
		using ElysiumActionTables::ENpcSlot;
		switch (Slot)
		{
		case ENpcSlot::PreTranslate:   return TEXT("pre-translate");
		case ENpcSlot::ClassTranslate: return TEXT("class-translate");
		case ENpcSlot::Cover:          return TEXT("cover");
		case ENpcSlot::Reload:         return TEXT("reload");
		case ENpcSlot::EarlyTranslate: return TEXT("early-translate");
		}
		return TEXT("?");
	}

	// The body index one class column names, so a class row can be checked column by column.
	int32 ClassColumn(const ElysiumActionTables::FNpcClass& Class, ElysiumActionTables::ENpcSlot Slot)
	{
		using ElysiumActionTables::ENpcSlot;
		switch (Slot)
		{
		case ENpcSlot::PreTranslate:   return Class.PreTranslate;
		case ENpcSlot::ClassTranslate: return Class.ClassTranslate;
		case ENpcSlot::Cover:          return Class.Cover;
		case ENpcSlot::Reload:         return Class.Reload;
		case ENpcSlot::EarlyTranslate: break;
		}
		return INDEX_NONE;
	}

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

// Round trip and well-formedness. No game files: the committed model is the whole input.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWeaponActivityTablesTest,
	"Elysium.Substrate.WeaponActivityTables", GElysiumActionTableFlags)
bool FElysiumWeaponActivityTablesTest::RunTest(const FString&)
{
	using namespace ElysiumActionTables;

	const FActionTableCensus& Stored = Census();

	// The round trip: the compressed model is the recovered stream.
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

	// Well-formedness: what a malformed regeneration would break.
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

	// No dead rewrite rule.
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

	// The rewrite itself.
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

	// The ladder, and the walk over it.
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


// The player action rules. Well-formedness and the walk over them, with no game files: the
// committed rules are the whole input.
//
// The selector this stands for is code rather than a table, so what a bad regeneration breaks is
// not a row count but an *order* — a ladder whose unconditional row moved up answers the same
// activity for every state, and a jump arm whose landing rows lost their gait deferral drops a
// running body into a land. The walks below are what catch that.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlayerActionRulesTest,
	"Elysium.Substrate.PlayerActionRules", GElysiumActionTableFlags)
bool FElysiumPlayerActionRulesTest::RunTest(const FString&)
{
	using namespace ElysiumActionTables;

	const FPlayerActionCensus& Stored = PlayerCensus();

	// The compiled vocabulary.
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

	// Well-formedness of every row.
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

	// The gait ladder.
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

	// The arms.
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

	// The pose writes.
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

	// The translations, the latch and the discipline fields.
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


// The NPC translation surface, content-free.
//
// The census catches a bad regeneration. What it cannot catch is the thing this suite exists for:
// **the order**. A `RewriteAndReturn` row that slid behind its chain would let the common Troika
// body swallow the dog's fidget; a `ForceCoverContext` row that moved after the context rows would
// hand a forced-low body its medium cover; a chain that flipped from `BeforeRules` to `AfterRules`
// would let the Tzimisce runner's variant selection run against an untranslated request. Every one
// of those keeps the same row count.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcActivityTablesTest,
	"Elysium.Substrate.NpcActivityTables", GElysiumActionTableFlags)
bool FElysiumNpcActivityTablesTest::RunTest(const FString&)
{
	using namespace ElysiumActionTables;

	const FNpcTableCensus& Stored = NpcCensus();
	TArrayView<const FNpcTranslationBody> Bodies = NpcTranslationBodies();
	TArrayView<const FNpcClass> Classes = NpcClasses();

	// The census is the table.
	{
		int32 PerSlot[5] = { 0, 0, 0, 0, 0 };
		int32 Rules = 0;
		int32 FamilyRules = 0;
		for (const FNpcTranslationBody& Body : Bodies)
		{
			++PerSlot[static_cast<int32>(Body.Slot)];
			Rules += Body.RuleCount;
			for (int32 Index = 0; Index < Body.RuleCount; ++Index)
			{
				FamilyRules += Body.Rules[Index].FromFamily != nullptr ? 1 : 0;
			}
		}
		TestEqual(TEXT("pre-translation bodies"), PerSlot[0], Stored.PreTranslateBodies);
		TestEqual(TEXT("class-translation bodies"), PerSlot[1], Stored.ClassTranslateBodies);
		TestEqual(TEXT("cover delegates"), PerSlot[2], Stored.CoverBodies);
		TestEqual(TEXT("reload delegates"), PerSlot[3], Stored.ReloadBodies);
		TestEqual(TEXT("the paired-action tail"), PerSlot[4], Stored.TailBodies);
		TestEqual(TEXT("translation rules"), Rules, Stored.TranslationRules);
		TestEqual(TEXT("rules over an unenumerated request family"), FamilyRules,
			Stored.UnrecoveredFamilyRules);
		TestEqual(TEXT("subclasses"), Classes.Num(), Stored.Subclasses);
		TestEqual(TEXT("entity aliases"), NpcEntityAliases().Num(), Stored.EntityAliases);
		TestEqual(TEXT("grapple bases"), NpcGrappleFamilies().Num(), Stored.GrappleFamilies);
		TestEqual(TEXT("the predicate vocabulary the emission was generated against"),
			static_cast<int32>(ENpcPredicate::Count), Stored.Predicates);

		// The acceptance sentence, as an assertion: 77 descendants collapse to 10 + 5 + 2 + 2.
		TestEqual(TEXT("77 subclasses"), Stored.Subclasses, 77);
		TestEqual(TEXT("collapsing to 10 pre-translation bodies"), Stored.PreTranslateBodies, 10);
		TestEqual(TEXT("5 class-translation bodies"), Stored.ClassTranslateBodies, 5);
		TestEqual(TEXT("2 cover delegates"), Stored.CoverBodies, 2);
		TestEqual(TEXT("and 2 reload delegates"), Stored.ReloadBodies, 2);
	}

	// Every body is well formed, and the chain graph terminates.
	for (int32 Index = 0; Index < Bodies.Num(); ++Index)
	{
		const FNpcTranslationBody& Body = Bodies[Index];
		const FString Where = FString::Printf(TEXT("body %d (%s)"), Index,
			Body.Name != nullptr ? Body.Name : TEXT("<unnamed>"));

		TestNotNull(*FString::Printf(TEXT("%s names itself"), *Where), Body.Name);
		TestNotNull(*FString::Printf(TEXT("%s carries its retail address"), *Where), Body.Address);
		TestNotNull(*FString::Printf(TEXT("%s states its policy"), *Where), Body.Policy);
		TestTrue(*FString::Printf(TEXT("%s has rules exactly when it says so"), *Where),
			(Body.Rules != nullptr) == (Body.RuleCount > 0));
		TestTrue(*FString::Printf(TEXT("%s chains exactly when it names a body"), *Where),
			(Body.Chain != ENpcChain::None) == (Body.ChainTo != INDEX_NONE));

		if (Body.ChainTo != INDEX_NONE)
		{
			if (TestTrue(*FString::Printf(TEXT("%s chains to a real body"), *Where),
				Bodies.IsValidIndex(Body.ChainTo)))
			{
				const FNpcTranslationBody& Next = Bodies[Body.ChainTo];
				// A body only ever inherits within its own slot; the one exception is the Troika
				// pre-translation finishing through the non-virtual paired-action tail.
				TestTrue(*FString::Printf(TEXT("%s chains within its slot (%s -> %s)"), *Where,
					SlotName(Body.Slot), SlotName(Next.Slot)),
					Next.Slot == Body.Slot || Next.Slot == ENpcSlot::EarlyTranslate);
			}
			// Follow the chain to its end. A cycle would hang the runtime walk on its own tripwire.
			int32 Hops = 0;
			int32 Cursor = Body.ChainTo;
			while (Cursor != INDEX_NONE && Bodies.IsValidIndex(Cursor) && Hops <= Bodies.Num())
			{
				TestTrue(*FString::Printf(TEXT("%s does not chain back onto itself"), *Where),
					Cursor != Index);
				Cursor = Bodies[Cursor].ChainTo;
				++Hops;
			}
			TestTrue(*FString::Printf(TEXT("%s's chain terminates"), *Where), Hops <= Bodies.Num());
		}

		for (int32 RuleIndex = 0; RuleIndex < Body.RuleCount; ++RuleIndex)
		{
			const FNpcRule& Rule = Body.Rules[RuleIndex];
			const FString Row = FString::Printf(TEXT("%s row %d"), *Where, RuleIndex);
			for (const ENpcPredicate Predicate : Rule.Predicates)
			{
				TestTrue(*FString::Printf(TEXT("%s names a live predicate"), *Row),
					static_cast<int32>(Predicate) < static_cast<int32>(ENpcPredicate::Count));
			}
			TestFalse(*FString::Printf(TEXT("%s matches a literal or a family, never both"), *Row),
				Rule.From != nullptr && Rule.FromFamily != nullptr);

			const bool bRoutesOnly = Rule.Route == ENpcRoute::Delegate
				|| Rule.Route == ENpcRoute::ForceCoverContext
				|| Rule.Route == ENpcRoute::Grapple;
			TestTrue(*FString::Printf(TEXT("%s names a target unless it only routes"), *Row),
				(Rule.To != nullptr) == !bRoutesOnly);
			if (Rule.To != nullptr)
			{
				TestTrue(*FString::Printf(TEXT("%s's target carries its registered ID"), *Row),
					Rule.ToId > 0);
			}
			if (Rule.From != nullptr)
			{
				TestTrue(*FString::Printf(TEXT("%s's request carries its registered ID"), *Row),
					Rule.FromId > 0);
			}
			if (Rule.Route == ENpcRoute::Delegate)
			{
				TestTrue(*FString::Printf(TEXT("%s delegates to a delegate slot"), *Row),
					Rule.Delegate == ENpcSlot::Cover || Rule.Delegate == ENpcSlot::Reload);
			}
			if (Rule.Route == ENpcRoute::ForceCoverContext)
			{
				TestTrue(*FString::Printf(TEXT("%s forces a real cover context"), *Row),
					Rule.Operand != 0);
			}
		}
	}

	// The class ledger points at the bodies it says it does.
	{
		TArray<int32> Inheritors;
		Inheritors.SetNumZeroed(Bodies.Num());
		const ENpcSlot Columns[] = { ENpcSlot::PreTranslate, ENpcSlot::ClassTranslate,
			ENpcSlot::Cover, ENpcSlot::Reload };

		for (const FNpcClass& Class : Classes)
		{
			for (const ENpcSlot Slot : Columns)
			{
				const int32 Index = ClassColumn(Class, Slot);
				if (!TestTrue(*FString::Printf(TEXT("%s names a real %s body"), Class.CppClass,
					SlotName(Slot)), Bodies.IsValidIndex(Index)))
				{
					continue;
				}
				TestEqual(*FString::Printf(TEXT("%s's %s column holds a %s body"), Class.CppClass,
					SlotName(Slot), SlotName(Slot)), static_cast<int32>(Bodies[Index].Slot),
					static_cast<int32>(Slot));
				++Inheritors[Index];
				TestTrue(*FString::Printf(TEXT("%s resolves the same body twice"), Class.CppClass),
					NpcBodyFor(Class, Slot) == &Bodies[Index]);
			}
			TestTrue(*FString::Printf(TEXT("%s names a real StartTask body"), Class.CppClass),
				NpcTaskHandlers().IsValidIndex(Class.StartTask));
			TestTrue(*FString::Printf(TEXT("%s names a real RunTask body"), Class.CppClass),
				NpcTaskHandlers().IsValidIndex(Class.RunTask));
		}

		for (int32 Index = 0; Index < Bodies.Num(); ++Index)
		{
			TestEqual(*FString::Printf(TEXT("%s's inheritor count is what the ledger holds"),
				Bodies[Index].Name), Inheritors[Index], Bodies[Index].InheritorCount);
		}
		// The tail is reached by chain, never by a vtable column, which is why it is counted apart
		// from the 10 + 5 + 2 + 2.
		const int32 Tail = NpcBodyIndex(TEXT("EarlyTranslate_Grapple"));
		if (TestTrue(TEXT("the paired-action tail is in the ledger"), Bodies.IsValidIndex(Tail)))
		{
			TestEqual(TEXT("and no class names it directly"), Inheritors[Tail], 0);
		}
	}

	// The entity aliases resolve.
	{
		TSet<FString> Seen;
		for (const FNpcEntityAlias& Alias : NpcEntityAliases())
		{
			bool bDuplicate = false;
			Seen.Add(FString(Alias.Classname).ToLower(), &bDuplicate);
			TestFalse(*FString::Printf(TEXT("%s is claimed once"), Alias.Classname), bDuplicate);
			if (!TestTrue(*FString::Printf(TEXT("%s resolves to a real class"), Alias.Classname),
				Classes.IsValidIndex(Alias.ClassIndex)))
			{
				continue;
			}
			const FNpcClass& Owner = Classes[Alias.ClassIndex];
			TestTrue(*FString::Printf(TEXT("%s resolves through the lookup too"), Alias.Classname),
				FindNpcClassByEntityClass(FString(Alias.Classname)) == &Owner);

			bool bClaimed = false;
			for (int32 Index = 0; Index < Owner.EntityClassnameCount; ++Index)
			{
				bClaimed |= FCString::Stricmp(Owner.EntityClassnames[Index], Alias.Classname) == 0;
			}
			TestTrue(*FString::Printf(TEXT("%s is a name %s's own constructor claims"),
				Alias.Classname, Owner.CppClass), bClaimed);
		}

		// The one documented miss. `sm_junkyard_1`'s Night Watchman names a class the retail DLL does
		// not carry, and it is not silently answered with a cousin.
		TestNull(TEXT("npc_BaseVampAI resolves to nothing rather than to a neighbour"),
			FindNpcClassByEntityClass(TEXT("npc_BaseVampAI")));
	}

	// The task handlers and their policies.
	{
		TArrayView<const FNpcTaskHandler> Handlers = NpcTaskHandlers();
		TArrayView<const FNpcTaskPolicy> Policies = NpcTaskPolicies();

		int32 Start = 0;
		int32 Custom = 0;
		int32 Animating = 0;
		for (const FNpcTaskHandler& Handler : Handlers)
		{
			Start += Handler.Phase == ENpcTaskPhase::Start ? 1 : 0;
			Custom += Handler.bSharedDispatcher ? 0 : 1;
			Animating += (!Handler.bSharedDispatcher && Handler.PolicyCount > 0) ? 1 : 0;
		}
		TestEqual(TEXT("StartTask bodies"), Start, Stored.StartTaskHandlers);
		TestEqual(TEXT("RunTask bodies"), Handlers.Num() - Start, Stored.RunTaskHandlers);
		TestEqual(TEXT("custom task bodies"), Custom, Stored.CustomTaskHandlers);
		TestEqual(TEXT("animation-bearing custom bodies"), Animating,
			Stored.AnimationBearingHandlers);

		TArray<int32> PolicyCounts;
		PolicyCounts.SetNumZeroed(Handlers.Num());
		int32 Routes = 0;
		for (const FNpcTaskPolicy& Policy : Policies)
		{
			if (TestTrue(TEXT("every policy names a real handler"),
				Handlers.IsValidIndex(Policy.Handler)))
			{
				++PolicyCounts[Policy.Handler];
			}
			TestTrue(TEXT("every policy routes at least one task"), Policy.TaskCount > 0);
			Routes += Policy.TaskCount;
			TestTrue(TEXT("a policy lists activities exactly when it counts them"),
				(Policy.Activities != nullptr) == (Policy.ActivityCount > 0));
			// The argument route is the one that names no literal: the task's own float argument is
			// cast to an activity ID. Every other route has to name what it asks for.
			if (Policy.Route == ENpcTaskRoute::SetIdealArgument)
			{
				TestEqual(TEXT("an argument route names no literal activity"), Policy.ActivityCount,
					0);
			}
			else
			{
				TestTrue(TEXT("every other route names an activity"), Policy.ActivityCount > 0);
			}
		}
		TestEqual(TEXT("100 policy rows"), Policies.Num(), Stored.TaskPolicies);
		TestEqual(TEXT("over 111 task routes"), Routes, Stored.TaskRoutes);
		// The finding, as an assertion: every recovered route asks for an activity. None looks a
		// sequence label up and none allocates an overlay layer.
		TestEqual(TEXT("no custom body looks an exact sequence label up"), Stored.ExactLabelRoutes,
			0);
		TestEqual(TEXT("and none allocates an overlay layer"), Stored.LayerRoutes, 0);

		for (int32 Index = 0; Index < Handlers.Num(); ++Index)
		{
			TestEqual(*FString::Printf(TEXT("handler %s carries the policies it counts"),
				Handlers[Index].Address), PolicyCounts[Index], Handlers[Index].PolicyCount);
		}

		// A task a shared melee family names is routed by more than one body, which is the whole
		// reason the ledger is keyed by (handler, task) rather than by task.
		TArray<const FNpcTaskPolicy*> Blocking;
		CollectNpcTaskPolicies(TEXT("TASK_MELEE_BLOCK"), Blocking);
		TestTrue(TEXT("TASK_MELEE_BLOCK is routed by more than one body"), Blocking.Num() > 1);

		// The restart rule, as a policy over these rows.
		//
		// `RestartIdealActivity` (`0x10289ee0`) clears the current activity before `SetIdealActivity`,
		// so a request equal to what is already playing is not swallowed as an unchanged ideal:
		// repeated attacks, reloads, pre-jumps and lands restart their sequence
		// (`docs/vtmb/animation_and_movers.md`). **It belongs to the route, not to requests in
		// general** — the same document's controlled crouch trace proves that a held request through
		// the ordinary ideal route runs 1.791 s uninterrupted without restarting.
		TestTrue(TEXT("the restart helper restarts"),
			RouteRestartsIdenticalRequest(ENpcTaskRoute::RestartIdeal));
		TestTrue(TEXT("and so does its choice form"),
			RouteRestartsIdenticalRequest(ENpcTaskRoute::RestartIdealChoice));
		TestFalse(TEXT("the ordinary ideal route does not"),
			RouteRestartsIdenticalRequest(ENpcTaskRoute::SetIdeal));
		TestFalse(TEXT("nor the immediate commit"),
			RouteRestartsIdenticalRequest(ENpcTaskRoute::SetActivity));
		TestFalse(TEXT("nor the argument route"),
			RouteRestartsIdenticalRequest(ENpcTaskRoute::SetIdealArgument));
		TestFalse(TEXT("nor the navigator route"),
			RouteRestartsIdenticalRequest(ENpcTaskRoute::SetIdealNavigator));
		TestFalse(TEXT("nor a task remap"),
			RouteRestartsIdenticalRequest(ENpcTaskRoute::RemapSharedTask));

		// By task, over the rows this catalog carries — the 100 CUSTOM policies. The shared
		// `CAI_BaseNPC` dispatcher's own restart rows (`TASK_RANGE_ATTACK1`, `TASK_RELOAD`,
		// `TASK_PRE_JUMP`, `TASK_LAND`) are recorded in the document and are not in this table, so a
		// caller keyed on a task gets an answer only for a custom body.
		TestTrue(TEXT("a repeated melee attack restarts"),
			TaskRestartsIdenticalRequest(TEXT("TASK_MELEE_ATTACK1")));
		TestTrue(TEXT("a repeated block restarts"),
			TaskRestartsIdenticalRequest(TEXT("TASK_MELEE_BLOCK")));
		TestTrue(TEXT("a repeated pounce restarts"),
			TaskRestartsIdenticalRequest(TEXT("TASK_POUNCE_ATTACK1")));
		// The counter-cases, one per non-restart route that names a task here.
		TestFalse(TEXT("an ordinary ideal request does not restart"),
			TaskRestartsIdenticalRequest(TEXT("TASK_CROW_HOP")));
		TestFalse(TEXT("nor an immediate commit"),
			TaskRestartsIdenticalRequest(TEXT("TASK_SPECIAL_IDLE_ACTIVITY")));
		TestFalse(TEXT("nor an argument route"),
			TaskRestartsIdenticalRequest(TEXT("TASK_PLAY_CLAW_SEQUENCE")));
		// A task the catalog does not carry states no route, and inventing a restart for it would
		// re-fire a clip retail holds.
		TestFalse(TEXT("an unknown task states no route"),
			TaskRestartsIdenticalRequest(TEXT("TASK_NO_SUCH_THING")));
		TestFalse(TEXT("and neither does an empty one"), TaskRestartsIdenticalRequest(FString()));

		// The same answer keyed by the LOGICAL activity, which is what a weapon or a reaction can
		// state — retail chooses the route at the task, ahead of every translation, so the key is the
		// untranslated request.
		TestTrue(TEXT("ACT_MELEE_ATTACK is a restart-route activity"),
			ActivityRestartsIdenticalRequest(TEXT("ACT_MELEE_ATTACK")));
		// Case-folded, because a producer spells the request and the table spells the row.
		TestTrue(TEXT("and is matched case-insensitively"),
			ActivityRestartsIdenticalRequest(TEXT("act_melee_attack")));
		TestTrue(TEXT("so is ACT_BLOCK"), ActivityRestartsIdenticalRequest(TEXT("ACT_BLOCK")));
		// `ACT_MELEE_ATTACK` is also named by a Manbat `SetIdeal` row, and the restart route still
		// wins: a route that has to be honoured somewhere is honoured wherever the activity appears.
		// `ACT_FIDGET` and `ACT_CAULDRON_DEATH` are named by non-restart rows ALONE, which is what
		// makes them the real counter-cases rather than absent names.
		TestFalse(TEXT("an activity only ever committed immediately does not restart"),
			ActivityRestartsIdenticalRequest(TEXT("ACT_FIDGET")));
		TestFalse(TEXT("nor one only ever set as an ideal"),
			ActivityRestartsIdenticalRequest(TEXT("ACT_CAULDRON_DEATH")));
		TestFalse(TEXT("an activity no policy names answers no route"),
			ActivityRestartsIdenticalRequest(TEXT("ACT_NO_SUCH_ACTIVITY")));
		TestFalse(TEXT("and so does an empty one"), ActivityRestartsIdenticalRequest(FString()));
	}

	// The order, which no count catches.
	{
		const TSet<FString> Nothing;
		const FNpcState Plain;

		// The dog's leaf keeps the fidget the common Troika body would have turned into an idle.
		// Slide that `RewriteAndReturn` row behind the chain and the two answers swap.
		TestEqual(TEXT("the dog keeps its fidget"),
			WalkNpc(TEXT("PreTranslate_Dog"), TEXT("ACT_FIDGET"), Plain, Nothing).Activity,
			FString(TEXT("ACT_FIDGET")));
		TestEqual(TEXT("and the common Troika body turns the same request into an idle"),
			WalkNpc(TEXT("PreTranslate_Troika"), TEXT("ACT_FIDGET"), Plain, Nothing).Activity,
			FString(TEXT("ACT_IDLE")));

		// The human body's own rows run before the Troika chain.
		FNpcState Relaxed;
		Relaxed.Holds.Add(ENpcPredicate::NotArmedAlert);
		TestEqual(TEXT("an unalerted human walks relaxed"),
			WalkNpc(TEXT("PreTranslate_Human"), TEXT("ACT_WALK"), Relaxed, Nothing).Activity,
			FString(TEXT("ACT_WALK_RELAXED")));

		FNpcState Alerted;
		Alerted.Holds.Add(ENpcPredicate::ArmedAlert);
		TestEqual(TEXT("an alerted human turns alert"),
			WalkNpc(TEXT("PreTranslate_Human"), TEXT("ACT_TURN_LEFT"), Alerted, Nothing).Activity,
			FString(TEXT("ACT_TURN_LEFT_ALERT")));

		FNpcState AlertedAiming = Alerted;
		AlertedAiming.Holds.Add(ENpcPredicate::RangedAimCapable);
		TestEqual(TEXT("... and aims instead of idling with a ranged weapon up"),
			WalkNpc(TEXT("PreTranslate_Human"), TEXT("ACT_IDLE"), AlertedAiming, Nothing).Activity,
			FString(TEXT("ACT_AIM")));

		// The chain runs after: a gait override the human body does not carry still reaches the
		// request through the common Troika body.
		FNpcState Hurrying;
		Hurrying.Holds.Add(ENpcPredicate::GaitOverrideRun);
		TestEqual(TEXT("the human body's chain still applies the global gait override"),
			WalkNpc(TEXT("PreTranslate_Human"), TEXT("ACT_WALK"), Hurrying, Nothing).Activity,
			FString(TEXT("ACT_RUN")));

		// ... and the Tzimisce runner's chain runs *before* its rules, so a request the chain
		// delegates never reaches the variant selection at all.
		FNpcState Variant;
		Variant.Variant = 1;
		const FNpcTranslation Delegated = WalkNpc(TEXT("PreTranslate_TzimisceRunner"),
			TEXT("ACT_COVER"), Variant, Nothing);
		TestTrue(TEXT("the runner's chain delegates a cover request before its own rules run"),
			Delegated.bDelegated);
		TestEqual(TEXT("and it delegates to the cover slot"), static_cast<int32>(Delegated.Delegate),
			static_cast<int32>(ENpcSlot::Cover));
		TestEqual(TEXT("an ordinary request reaches the runner's variant selection"),
			WalkNpc(TEXT("PreTranslate_TzimisceRunner"), TEXT("ACT_FIDGET"), Variant,
				Nothing).Activity, FString(TEXT("ACT_TZ_FIDGET2")));

		// The cover delegate is an ordered candidate list against the body's own vocabulary.
		const TSet<FString> Crouching = { TEXT("ACT_MIDCRUNCH_IDLE"), TEXT("ACT_CRUNCH_IDLE"),
			TEXT("ACT_COVER") };
		TestEqual(TEXT("Troika cover prefers the mid-crunch idle for context 100"),
			WalkNpc(TEXT("Cover_Troika"), TEXT("ACT_COVER"), Plain, Crouching, 100).Activity,
			FString(TEXT("ACT_MIDCRUNCH_IDLE")));

		// Forced low cover rewrites the context *before* the context rows read it. Move that row
		// after them and a forced-low body answers the medium crunch instead.
		FNpcState ForcedLow;
		ForcedLow.Holds.Add(ENpcPredicate::ForcedLowCover);
		TestEqual(TEXT("forced low cover reaches the low crunch from context 100"),
			WalkNpc(TEXT("Cover_Troika"), TEXT("ACT_COVER"), ForcedLow, Crouching, 100).Activity,
			FString(TEXT("ACT_CRUNCH_IDLE")));

		// A body carrying none of the crunch idles falls through to the base delegate.
		const TSet<FString> BareCover = { TEXT("ACT_COVER") };
		TestEqual(TEXT("a body with only plain cover falls through to the base delegate"),
			WalkNpc(TEXT("Cover_Troika"), TEXT("ACT_COVER"), Plain, BareCover, 100).Activity,
			FString(TEXT("ACT_COVER")));
		TestEqual(TEXT("and a body with nothing at all lands on the base delegate's idle"),
			WalkNpc(TEXT("Cover_Troika"), TEXT("ACT_COVER"), Plain, Nothing, 100).Activity,
			FString(TEXT("ACT_IDLE")));

		// The Troika fast reload's crunch answer re-enters the whole translator rather than being
		// answered directly, which is the one route that does.
		const FNpcTranslation Reload = WalkNpc(TEXT("Reload_Troika"), TEXT("ACT_RELOAD_FAST"), Plain,
			Nothing, 101);
		TestEqual(TEXT("Troika fast reload reaches the low crunch idle"), Reload.Activity,
			FString(TEXT("ACT_CRUNCH_IDLE")));
		TestTrue(TEXT("... through the whole translator"), Reload.bThroughTranslator);
		TestEqual(TEXT("and without a cover context it answers the fast reload"),
			WalkNpc(TEXT("Reload_Troika"), TEXT("ACT_RELOAD_FAST"), Plain, Nothing).Activity,
			FString(TEXT("ACT_RELOAD_FAST")));

		// The delegating rows on the common Troika pre-translation.
		const FNpcTranslation Cover = WalkNpc(TEXT("PreTranslate_Troika"), TEXT("ACT_COVER"), Plain,
			Nothing);
		TestTrue(TEXT("a cover request leaves the pre-translation for the cover delegate"),
			Cover.bDelegated);
		FNpcState Reloading;
		Reloading.Holds.Add(ENpcPredicate::ReloadFastCapable);
		const FNpcTranslation Fast = WalkNpc(TEXT("PreTranslate_Troika"), TEXT("ACT_RELOAD_FAST"),
			Reloading, Nothing);
		TestTrue(TEXT("a capable fast reload leaves for the reload delegate"), Fast.bDelegated);
		TestEqual(TEXT("... which is the reload slot"), static_cast<int32>(Fast.Delegate),
			static_cast<int32>(ENpcSlot::Reload));

		// An ordinary request finishes through the paired-action tail.
		TestTrue(TEXT("an ordinary request reaches the paired-action tail"),
			WalkNpc(TEXT("PreTranslate_Troika"), TEXT("ACT_IDLE"), Plain, Nothing).bGrappleTail);

		// The two rows whose request family the recovered reading did not enumerate say so rather
		// than guessing a membership.
		FNpcState Frenzied;
		Frenzied.Holds.Add(ENpcPredicate::MovementPolicyFrenzy);
		const FNpcTranslation Unresolved = WalkNpc(TEXT("PreTranslate_Troika"), TEXT("ACT_WALK"),
			Frenzied, Nothing);
		TestTrue(TEXT("a frenzy movement policy reports what it cannot decide"),
			Unresolved.bUnresolved);
		TestEqual(TEXT("and leaves the request alone rather than guessing"), Unresolved.Activity,
			FString(TEXT("ACT_WALK")));

		// The remaining leaves.
		TestEqual(TEXT("the wolf morph answers every request with its one activity"),
			WalkNpc(TEXT("PreTranslate_WolfMorph"), TEXT("ACT_RANGE_ATTACK1"), Plain,
				Nothing).Activity, FString(TEXT("ACT_WOLF_MORPH")));
		TestEqual(TEXT("the stalker turns a gait into a combat move"),
			WalkNpc(TEXT("PreTranslate_Stalker"), TEXT("ACT_RUN"), Plain, Nothing).Activity,
			FString(TEXT("ACT_COMBATMOVE")));
		const FNpcTranslation StalkerIdle = WalkNpc(TEXT("PreTranslate_Stalker"), TEXT("ACT_IDLE"),
			Plain, Nothing);
		TestFalse(TEXT("and leaves everything else alone"), StalkerIdle.bTranslated);

		// The class-translation side, and the Ming Xiao leaf that repeats the human normalizations.
		FNpcState Laughing;
		Laughing.Holds.Add(ENpcPredicate::LaughIdleFlagged);
		TestEqual(TEXT("the Troika class translation reaches the laugh idle"),
			WalkNpc(TEXT("ClassTranslate_Troika"), TEXT("ACT_IDLE"), Laughing, Nothing).Activity,
			FString(TEXT("ACT_LAUGH_IDLE")));
		TestEqual(TEXT("the human class translation returns an alert turn to its ordinary form"),
			WalkNpc(TEXT("ClassTranslate_Human"), TEXT("ACT_180_RIGHT_ALERT"), Plain,
				Nothing).Activity, FString(TEXT("ACT_180_RIGHT")));
		TestEqual(TEXT("... and Ming Xiao's own body does the same, then the Troika rule"),
			WalkNpc(TEXT("ClassTranslate_MingXiao"), TEXT("ACT_IDLE"), Laughing, Nothing).Activity,
			FString(TEXT("ACT_LAUGH_IDLE")));
	}

	// The grapple arithmetic.
	{
		// The recovered table: `+1`/`+2` attacker with a short/tall victim in front, `+3`/`+4` the
		// victim's side of the same, `+5`…`+8` the four from behind.
		TestEqual(TEXT("attacker, short victim, front"), GrappleOffset(false, false, false), 1);
		TestEqual(TEXT("attacker, tall victim, front"), GrappleOffset(true, false, false), 2);
		TestEqual(TEXT("victim, short attacker, front"), GrappleOffset(false, true, false), 3);
		TestEqual(TEXT("victim, tall attacker, front"), GrappleOffset(true, true, false), 4);
		TestEqual(TEXT("attacker, short victim, back"), GrappleOffset(false, false, true), 5);
		TestEqual(TEXT("attacker, tall victim, back"), GrappleOffset(true, false, true), 6);
		TestEqual(TEXT("victim, short attacker, back"), GrappleOffset(false, true, true), 7);
		TestEqual(TEXT("victim, tall attacker, back"), GrappleOffset(true, true, true), 8);

		TSet<FString> Variants;
		int32 Swapped = 0;
		for (const FNpcGrappleFamily& Family : NpcGrappleFamilies())
		{
			TestNotNull(*FString::Printf(TEXT("%s resolves through the lookup"), Family.Base),
				FindGrappleFamily(FString(Family.Base)));
			TestTrue(*FString::Printf(TEXT("%s carries its registered ID"), Family.Base),
				Family.BaseId > 0);
			Swapped += Family.RoleOrder == ENpcGrappleRoleOrder::Canonical ? 0 : 1;

			for (int32 Mask = 0; Mask < 8; ++Mask)
			{
				const bool bTall = (Mask & 1) != 0;
				const bool bVictim = (Mask & 2) != 0;
				const bool bBack = (Mask & 4) != 0;
				const FNpcGrappleVariant Variant = GrappleVariant(Family, bTall, bVictim, bBack);
				TestEqual(*FString::Printf(TEXT("%s's variant %d is contiguous with its base"),
					Family.Base, Variant.Offset), Variant.ActivityId,
					Family.BaseId + Variant.Offset);
				TestTrue(*FString::Printf(TEXT("%s's variant names the base it came from"),
					Family.Base), Variant.Activity.StartsWith(FString(Family.Base) + TEXT("_"),
						ESearchCase::CaseSensitive));
				TestTrue(*FString::Printf(TEXT("%s's role agreement follows its order"),
					Family.Base), Variant.bNameAgreesWithRole ==
					(Family.RoleOrder == ENpcGrappleRoleOrder::Canonical));

				bool bDuplicate = false;
				Variants.Add(Variant.Activity, &bDuplicate);
				TestFalse(*FString::Printf(TEXT("%s is generated once"), *Variant.Activity),
					bDuplicate);
			}
		}
		TestEqual(TEXT("232 variants come out of 29 bases"), Variants.Num(),
			NpcCensus().GrappleVariants);
		TestEqual(TEXT("and the generated set is what the collector walks"), Variants.Num(), 232);

		TArray<FString> Collected;
		CollectGrappleVariants(Collected);
		TestEqual(TEXT("the collector agrees with the arithmetic"), Collected.Num(), 232);

		// Four of the eight zombie-feeding families register the attacker and victim halves the
		// other way round, so the literal at the slot the role arithmetic picks names the opposite
		// role. The arithmetic never reads the name, so this is a naming fact, not a routing one.
		TestEqual(TEXT("four families register attacker and victim the other way round"), Swapped,
			4);
		const TCHAR* const SwappedBases[] = { TEXT("ACT_ZOMBIE_FEEDING_ENGAGE"),
			TEXT("ACT_ZOMBIE_FEEDING_IDLE"), TEXT("ACT_ZOMBIE_FEEDING_BITE"),
			TEXT("ACT_ZOMBIE_FEEDING_FEED_LOOP") };
		for (const TCHAR* Base : SwappedBases)
		{
			const FNpcGrappleFamily* Family = FindGrappleFamily(FString(Base));
			if (TestNotNull(*FString::Printf(TEXT("%s is a registered base"), Base), Family))
			{
				TestEqual(*FString::Printf(TEXT("%s is one of the four"), Base),
					static_cast<int32>(Family->RoleOrder),
					static_cast<int32>(ENpcGrappleRoleOrder::AttackerVictimSwapped));
			}
		}

		TestNull(TEXT("an ordinary activity is not a paired-action base"),
			FindGrappleFamily(TEXT("ACT_IDLE")));
	}

	return true;
}


#endif  // WITH_DEV_AUTOMATION_TESTS
