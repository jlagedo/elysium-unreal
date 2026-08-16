// Content-free Substrate automation for the Discipline runtime (13.2), plus one Content-tier case
// that parses the real `disciplinetgt_*` corpus.
//
// Every number asserted here is either a fact from `docs/vtmb/disciplines.md` (the two execution
// families, the eight-step targeted transaction, the interruption flags, the law production) or a
// value the test itself authors into a fabricated rulebook. Nothing loads `vdata` in the Substrate
// tier: the tables are built in memory and bound through `ElysiumSheetRules::BindTables`, which is
// the seam that makes this domain runnable headless (K10).
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "ElysiumSheetSlots.h"
#include "ElysiumStub.h"
#include "ElysiumVariant.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumDisciplines.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpc.h"        // Cycle 11b — the AI_Schedule channel's receiver
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumSheetMath.h"
#include "Substrate/ElysiumWeaponClasses.h"
#include "Tests/ElysiumTestServices.h"

#include "Misc/ScopeExit.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace ElysiumDisciplineTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	using EC = EElysiumTraitContainer;
	namespace ED = ElysiumDisciplines;

	// ------------------------------------------------------------------------------------------
	// The fabricated rulebook
	// ------------------------------------------------------------------------------------------

	// One stat, addressed by INDEX because file position is the engine's trait id.
	FElysiumStat MakeStat(int32 Index, const TCHAR* Internal, int32 Min, int32 Max, int32 Default)
	{
		FElysiumStat Stat;
		Stat.Index = Index;
		Stat.InternalName = Internal;
		Stat.Name = Internal;
		Stat.Min = Min;
		Stat.Max = Max;
		Stat.MinExpr = FString::FromInt(Min);
		Stat.MaxExpr = FString::FromInt(Max);
		Stat.Default = Default;
		return Stat;
	}

	FElysiumStat::FAction MakeAction(const TCHAR* ValueExpr, bool bOnIncrement)
	{
		FElysiumStat::FAction Action;
		Action.ValueExpr = ValueExpr;
		Action.bOnIncrement = bOnIncrement;
		return Action;
	}

	// The four containers, sized to the compiled tables so `At(Index)` answers for every slot the
	// runtime addresses. Bounds are deliberately generous except where a test asserts a clamp.
	FElysiumStatTable MakeStats()
	{
		FElysiumStatTable Table;

		FElysiumStatContainer& Attribs = Table.Containers[(uint8)EC::Attributes];
		Attribs.InternalName = TEXT("Attributes");
		for (const FElysiumSheetSlot& Slot : ElysiumSheetSlots(EC::Attributes))
		{
			Attribs.Stats.Add(MakeStat(Slot.Index, Slot.Internal, 0, 10000, 0));
		}
		Attribs.Stats[ElysiumSlot::BloodPool] =
			MakeStat(ElysiumSlot::BloodPool, TEXT("BloodPool"), 0, 15, 10);
		Attribs.Stats[ElysiumSlot::MaxHealth] =
			MakeStat(ElysiumSlot::MaxHealth, TEXT("Max_Health"), 0, 1000, 100);
		Attribs.Stats[ElysiumSlot::Health] =
			MakeStat(ElysiumSlot::Health, TEXT("Health"), 0, 1000, 0);
		Attribs.Stats[ElysiumSlot::Strength] =
			MakeStat(ElysiumSlot::Strength, TEXT("Strength"), 0, 5, 2);
		Attribs.Stats[ElysiumSlot::Stamina] =
			MakeStat(ElysiumSlot::Stamina, TEXT("Stamina"), 0, 5, 2);

		FElysiumStatContainer& Abilities = Table.Containers[(uint8)EC::Abilities];
		Abilities.InternalName = TEXT("Abilities");
		for (const FElysiumSheetSlot& Slot : ElysiumSheetSlots(EC::Abilities))
		{
			Abilities.Stats.Add(MakeStat(Slot.Index, Slot.Internal, 0, 5, 1));
		}

		// The learned container. `Is_Instant` is the one authoritative split: 0 on the four targeted
		// Disciplines, 1 on the nine native ones — the shipped classification.
		FElysiumStatContainer& Learned = Table.Containers[(uint8)EC::Disciplines];
		Learned.InternalName = TEXT("Disciplines");
		for (const FElysiumSheetSlot& Slot : ElysiumSheetSlots(EC::Disciplines))
		{
			FElysiumStat Stat = MakeStat(Slot.Index, Slot.Internal, -1, 5, -1);
			Stat.bIsInstant = !(Slot.Index == ED::Animalism || Slot.Index == ED::Dementation
				|| Slot.Index == ED::Dominate || Slot.Index == ED::Thaumaturgy);
			Learned.Stats.Add(MoveTemp(Stat));
		}

		// The active container. Every slot takes the two shipped gates; Fortitude and Bloodbuff get
		// the durations and Actions the real file authors for them.
		FElysiumStatContainer& Active = Table.Containers[(uint8)EC::ActiveDisciplines];
		Active.InternalName = TEXT("Active_Disciplines");
		for (const FElysiumSheetSlot& Slot : ElysiumSheetSlots(EC::ActiveDisciplines))
		{
			FElysiumStat Stat = MakeStat(Slot.Index, Slot.Internal, 0, 5, 0);
			Stat.IncPredependency.Add(TEXT("BloodPool > 0"));
			Stat.IncPredependency.Add(TEXT("Health < Max_Health"));
			Stat.Durations.bAuthored = true;
			for (int32 Level = 1; Level <= 5; ++Level)
			{
				Stat.Durations.Initial[Level] = 25;
				Stat.Durations.Add[Level] = 25;
			}
			// The blood payment: an increment-triggered `Stat` mutation, exactly as authored.
			FElysiumStat::FAction Pay = MakeAction(TEXT(">0"), /*bOnIncrement*/ true);
			Pay.StatMutation = TEXT("BloodPool -1");
			Stat.Actions.Add(MoveTemp(Pay));
			Active.Stats.Add(MoveTemp(Stat));
		}

		// Fortitude: `+rank` automatic soak successes, one authored group per level.
		for (int32 Level = 1; Level <= 5; ++Level)
		{
			FElysiumStat::FAction State = MakeAction(*FString::FromInt(Level), /*bOnIncrement*/ false);
			State.Effect = FString::Printf(TEXT("Discipline (Fortitude%d)"), Level);
			Active.Stats[ED::Fortitude].Actions.Add(MoveTemp(State));
		}
		// Bloodbuff: three blood, `BloodPool > 2`, one group at value 1 — the shipped shape.
		Active.Stats[ED::CorpusVampirus].IncPredependency.Reset();
		Active.Stats[ED::CorpusVampirus].IncPredependency.Add(TEXT("BloodPool > 2"));
		Active.Stats[ED::CorpusVampirus].Actions.Reset();
		{
			FElysiumStat::FAction Pay = MakeAction(TEXT("1"), /*bOnIncrement*/ true);
			Pay.StatMutation = TEXT("BloodPool -3");
			Active.Stats[ED::CorpusVampirus].Actions.Add(MoveTemp(Pay));
			FElysiumStat::FAction State = MakeAction(TEXT("1"), /*bOnIncrement*/ false);
			State.Effect = TEXT("Discipline (Corpus_Vampirus)");
			Active.Stats[ED::CorpusVampirus].Actions.Add(MoveTemp(State));
			// The Toreador-gated pair, so the `Clan != Toreador` predependency has both arms.
			FElysiumStat::FAction NonTor = MakeAction(TEXT("1"), /*bOnIncrement*/ false);
			NonTor.Predependency = TEXT("Clan != Toreador");
			NonTor.Effect = TEXT("Discipline (Corpus_Vampirus-NonToreador)");
			Active.Stats[ED::CorpusVampirus].Actions.Add(MoveTemp(NonTor));
		}
		// Auspex: a level-dependent duration, so the renewal test can assert `Add_N` != `Initial_N`.
		for (int32 Level = 1; Level <= 5; ++Level)
		{
			Active.Stats[ED::Auspex].Durations.Initial[Level] = 20;
			Active.Stats[ED::Auspex].Durations.Add[Level] = 10;
		}
		return Table;
	}

	FElysiumTraitEffect MakeEffect(const TCHAR* Trait, EElysiumTraitOp Op, int32 Amount,
		bool bPercent = false)
	{
		FElysiumTraitEffect Fx;
		Fx.Trait = Trait;
		Fx.Op = Op;
		Fx.Amount = Amount;
		Fx.bPercent = bPercent;
		return Fx;
	}

	void AddGroup(FElysiumTraitEffects& Table, const TCHAR* Name,
		std::initializer_list<FElysiumTraitEffect> Effects)
	{
		FElysiumTraitEffectGroup Group;
		Group.InternalName = Name;
		Group.Category = TEXT("Discipline");
		for (const FElysiumTraitEffect& Fx : Effects)
		{
			Group.Effects.Add(Fx);
		}
		Table.Add(MoveTemp(Group));
	}

	FElysiumTraitEffects MakeTraitEffects()
	{
		FElysiumTraitEffects Table;
		// Fortitude's authored contribution: `+rank` automatic soak successes.
		for (int32 Level = 1; Level <= 5; ++Level)
		{
			AddGroup(Table, *FString::Printf(TEXT("Discipline (Fortitude%d)"), Level),
				{ MakeEffect(TEXT("Automatic_Soak_Successes"), EElysiumTraitOp::Add, Level) });
		}
		// Bloodbuff: +2 Strength/Stamina with each maximum raised to 10.
		AddGroup(Table, TEXT("Discipline (Corpus_Vampirus)"),
			{ MakeEffect(TEXT("Strength"), EElysiumTraitOp::Max, 10),
			  MakeEffect(TEXT("Strength"), EElysiumTraitOp::Add, 2),
			  MakeEffect(TEXT("Stamina"), EElysiumTraitOp::Max, 10),
			  MakeEffect(TEXT("Stamina"), EElysiumTraitOp::Add, 2) });
		AddGroup(Table, TEXT("Discipline (Corpus_Vampirus-NonToreador)"), {});
		// The payload operators, on the LEARNED Discipline trait — where every shipped History
		// authors them.
		AddGroup(Table, TEXT("History (TEST Discipline Mods)"),
			{ MakeEffect(TEXT("Fortitude"), EElysiumTraitOp::Duration, 200, /*bPercent*/ true),
			  MakeEffect(TEXT("Thaumaturgy"), EElysiumTraitOp::BloodCost, 2) });
		// The two groups the targeted tests install on their victims.
		AddGroup(Table, TEXT("Discipline (Test-Daze)"),
			{ MakeEffect(TEXT("Perception"), EElysiumTraitOp::Add, -2) });
		AddGroup(Table, TEXT("Discipline (Thaumaturgy-Bloodshield)"), {});
		return Table;
	}

	// --- Fabricated `DisciplineTgt` records ----------------------------------------------------

	FElysiumDiscHit MakeHit(const TCHAR* Name)
	{
		FElysiumDiscHit Hit;
		Hit.Name = Name;
		return Hit;
	}

	// One `Affects_Table` that admits everything and names one hit table.
	FElysiumDiscAffectsTable MakeCatchAll(const TCHAR* HitTable)
	{
		FElysiumDiscAffectsTable Table;
		FElysiumDiscMapping Mapping;
		Mapping.HitTable = HitTable;
		Table.DefaultMappings.Add(MoveTemp(Mapping));
		return Table;
	}

	FElysiumDiscFilterRow MakeFilter(EElysiumDiscFilter Kind, int32 Number = 1,
		const TCHAR* Text = nullptr)
	{
		FElysiumDiscFilterRow Row;
		Row.Kind = Kind;
		Row.RawKey = ElysiumDiscFilterName(Kind);
		Row.Number = Number;
		Row.bEnabled = Number != 0;
		if (Text) { Row.Text = Text; }
		return Row;
	}

	const TCHAR* const GDazeRecord = TEXT("Test_Dominate_Daze");
	const TCHAR* const GShieldRecord = TEXT("Test_Thaumaturgy_Shield");
	const TCHAR* const GBoltRecord = TEXT("Test_Thaumaturgy_Bolt");
	const TCHAR* const GHelperRecord = TEXT("Test_Helper_Return");
	// Cycle 11b — the two `HitInfo.AI_Schedule` records. One names a program this runtime
	// registers, one names a program it does not, which is the whole shape of the channel: a
	// schedule name resolves against the kernel's registry or fails by name.
	const TCHAR* const GCommandRecord = TEXT("Test_Dominate_Command");
	const TCHAR* const GBerserkRecord = TEXT("Test_Thaumaturgy_Berserk");
	// A registered program (`Substrate/ElysiumNpcCombatSchedules.cpp`), named exactly as
	// `ElysiumScheduleIdFromName` spells it.
	const TCHAR* const GRegisteredSchedule = TEXT("SCHED_TROIKA_MELEE_IDLE");
	// The Berserk family the shipped records name, which this runtime registers no program for.
	const TCHAR* const GUnregisteredSchedule = TEXT("SCHED_BERSERK");

	FElysiumDisciplineTargets MakeTargets()
	{
		FElysiumDisciplineTargets Table;

		// Dominate 1 — a self-centred radius, one group, a finite duration, damage-interruptible.
		{
			FElysiumDisciplineTgt Record;
			Record.Name = TEXT("Daze");
			Record.InternalName = GDazeRecord;
			Record.Discipline = TEXT("Dominate");
			Record.Level = 1;
			Record.BloodCost = 2;
			Record.bOvert = false;
			Record.SupernaturalLvl = 2;
			Record.RecoveryTime = 5.f;
			Record.bRemoveOnTakeDamage = true;
			Record.AoE.Shape = EElysiumDiscShape::Radius;
			Record.AoE.Range = 600.f;
			Record.AoE.Filters.Rows.Add(MakeFilter(EElysiumDiscFilter::NoSelf));
			Record.AoE.Tables.Add(MakeCatchAll(TEXT("Hit_Human")));
			FElysiumDiscHit Hit = MakeHit(TEXT("Hit_Human"));
			Hit.TraitEffects.Add(TEXT("Discipline (Test-Daze)"));
			Hit.Duration.Parse(TEXT("10"));
			Record.Hits.Add(MoveTemp(Hit));
			Table.Add(MoveTemp(Record));
		}

		// Thaumaturgy 3 — the Bloodshield shape: self, a health buffer, an infinite duration, overt.
		{
			FElysiumDisciplineTgt Record;
			Record.Name = TEXT("Blood Shield");
			Record.InternalName = GShieldRecord;
			Record.Discipline = TEXT("Thaumaturgy");
			Record.Level = 3;
			Record.BloodCost = 3;
			Record.bOvert = true;
			Record.bTriggerAISound = true;
			Record.SupernaturalLvl = 4;
			Record.RecoveryTime = 12.f;
			Record.AoE.Shape = EElysiumDiscShape::Self;
			Record.AoE.Filters.Rows.Add(MakeFilter(EElysiumDiscFilter::Self));
			Record.AoE.Tables.Add(MakeCatchAll(TEXT("Hit_Player_Human")));
			FElysiumDiscHit Hit = MakeHit(TEXT("Hit_Player_Human"));
			Hit.HealthBuffer.Parse(TEXT("80"));
			Hit.Duration.Parse(TEXT("-1"));
			Hit.TraitEffects.Add(TEXT("Discipline (Thaumaturgy-Bloodshield)"));
			Record.Hits.Add(MoveTemp(Hit));
			Table.Add(MoveTemp(Record));
		}

		// Thaumaturgy 1 — a single-target damage record that nests a zero-cost helper.
		{
			FElysiumDisciplineTgt Record;
			Record.Name = TEXT("Bolt");
			Record.InternalName = GBoltRecord;
			Record.Discipline = TEXT("Thaumaturgy");
			Record.Level = 1;
			Record.BloodCost = 1;
			Record.bOvert = true;
			Record.SupernaturalLvl = 2;
			Record.AoE.Shape = EElysiumDiscShape::Target;
			Record.AoE.Range = 600.f;
			Record.AoE.Filters.Rows.Add(MakeFilter(EElysiumDiscFilter::NoSelf));
			Record.AoE.Tables.Add(MakeCatchAll(TEXT("Hit_Human")));
			FElysiumDiscHit Hit = MakeHit(TEXT("Hit_Human"));
			Hit.DmgHealth.Parse(TEXT("30%"));
			FElysiumDiscTriggerCast Cast;
			Cast.DisciplineFx = GHelperRecord;
			Cast.Source = TEXT("Target");
			Cast.Affects = TEXT("Self");
			Hit.TriggerCasting.Add(MoveTemp(Cast));
			Record.Hits.Add(MoveTemp(Hit));
			Table.Add(MoveTemp(Record));
		}

		// The zero-cost helper the bolt nests into: it returns two blood to the caster.
		{
			FElysiumDisciplineTgt Record;
			Record.Name = TEXT("Return");
			Record.InternalName = GHelperRecord;
			Record.Discipline = TEXT("Thaumaturgy");
			Record.Level = 1;
			Record.BloodCost = 0;
			Record.RecoveryTime = 0.f;
			Record.AoE.Shape = EElysiumDiscShape::Self;
			Record.AoE.Tables.Add(MakeCatchAll(TEXT("Hit_Human")));
			FElysiumDiscHit Hit = MakeHit(TEXT("Hit_Human"));
			Hit.HealBlood.Parse(TEXT("2"));
			Record.Hits.Add(MoveTemp(Hit));
			Table.Add(MoveTemp(Record));
		}

		// --- Cycle 11b: the two AI-schedule records ------------------------------------------
		// Both are radius-shaped over the same three victims and cost nothing, so the only thing
		// either one does to a target is name a schedule. That is deliberate: the assertion "the
		// victim is running that program" then has exactly one possible producer.
		struct FScheduleRow { const TCHAR* Record; const TCHAR* Discipline; int32 Level;
			const TCHAR* Schedule; };
		static const FScheduleRow ScheduleRows[] =
		{
			{ GCommandRecord, TEXT("Dominate"),    5, GRegisteredSchedule },
			{ GBerserkRecord, TEXT("Thaumaturgy"), 5, GUnregisteredSchedule },
		};
		for (const FScheduleRow& Row : ScheduleRows)
		{
			FElysiumDisciplineTgt Record;
			Record.Name = Row.Record;
			Record.InternalName = Row.Record;
			Record.Discipline = Row.Discipline;
			Record.Level = Row.Level;
			Record.BloodCost = 0;
			Record.AoE.Shape = EElysiumDiscShape::Radius;
			Record.AoE.Range = 600.f;
			Record.AoE.Filters.Rows.Add(MakeFilter(EElysiumDiscFilter::NoSelf));
			Record.AoE.Tables.Add(MakeCatchAll(TEXT("Hit_Human")));
			FElysiumDiscHit Hit = MakeHit(TEXT("Hit_Human"));
			Hit.AiSchedule = Row.Schedule;
			Record.Hits.Add(MoveTemp(Hit));
			Table.Add(MoveTemp(Record));
		}
		return Table;
	}

	// One record per interruption flag, all self-shaped so the target set is never empty.
	void AddInterruptRecords(FElysiumDisciplineTargets& Table)
	{
		struct FRow { const TCHAR* Name; int32 Level; bool bDamage; bool bHear; bool bBump; };
		static const FRow Rows[] =
		{
			{ TEXT("Test_Interrupt_Damage"), 2, true,  false, false },
			{ TEXT("Test_Interrupt_Hear"),   3, false, true,  false },
			{ TEXT("Test_Interrupt_Bump"),   4, false, false, true  },
		};
		for (const FRow& Row : Rows)
		{
			FElysiumDisciplineTgt Record;
			Record.Name = Row.Name;
			Record.InternalName = Row.Name;
			Record.Discipline = TEXT("Dominate");
			Record.Level = Row.Level;
			Record.BloodCost = 0;
			Record.bRemoveOnTakeDamage = Row.bDamage;
			Record.bRemoveOnHearCombat = Row.bHear;
			Record.bRemoveOnWasBumped = Row.bBump;
			Record.AoE.Shape = EElysiumDiscShape::Self;
			Record.AoE.Tables.Add(MakeCatchAll(TEXT("Hit_Human")));
			FElysiumDiscHit Hit = MakeHit(TEXT("Hit_Human"));
			Hit.TraitEffects.Add(TEXT("Discipline (Test-Daze)"));
			Hit.Duration.Parse(TEXT("-1"));
			Record.Hits.Add(MoveTemp(Hit));
			Table.Add(MoveTemp(Record));
		}
	}

	// The whole fabricated rulebook, bound for the lifetime of the fixture. The subsystem always
	// wins over this, so binding it can never change what a real run reads.
	struct FRulesFixture
	{
		FElysiumStatTable Stats = MakeStats();
		FElysiumTraitEffects Effects = MakeTraitEffects();
		FElysiumFeatTable Feats;
		FElysiumClanTable Clans;
		FElysiumDisciplineTargets Targets = MakeTargets();

		FRulesFixture()
		{
			AddInterruptRecords(Targets);
			Rebind();
		}
		~FRulesFixture()
		{
			ElysiumSheetRules::BindTables(ElysiumSheetRules::FBoundTables());
		}
		void Rebind()
		{
			ElysiumSheetRules::FBoundTables Bound;
			Bound.Stats = &Stats;
			Bound.TraitEffects = &Effects;
			Bound.Feats = &Feats;
			Bound.Clans = &Clans;
			Bound.DisciplineTargets = &Targets;
			ElysiumSheetRules::BindTables(Bound);
		}
	};

	// ------------------------------------------------------------------------------------------
	// World fixtures
	// ------------------------------------------------------------------------------------------

	FElysiumEntityDefs MakeDisciplineTestDefs()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__discipline_test__");

		// Three victims inside the radius records' reach, in stable def order.
		for (int32 i = 0; i < 3; ++i)
		{
			FElysiumEntityDef Victim;
			Victim.Classname = TEXT("npc_VPedestrian");
			Victim.TargetName = FString::Printf(TEXT("victim%d"), i);
			Victim.Origin = FVector(100.0f * static_cast<float>(i + 1), 0.0f, 0.0f);
			Defs.Defs.Add(MoveTemp(Victim));
		}
		// One far outside every authored range.
		FElysiumEntityDef Far;
		Far.Classname = TEXT("npc_VPedestrian");
		Far.TargetName = TEXT("faraway");
		Far.Origin = FVector(500000.0f, 0.0f, 0.0f);
		Defs.Defs.Add(MoveTemp(Far));

		// The `events_world` policy entity, so the world-area gate has something to look for.
		FElysiumEntityDef WorldEnt;
		WorldEnt.Classname = TEXT("events_world");
		WorldEnt.TargetName = TEXT("world");
		Defs.Defs.Add(MoveTemp(WorldEnt));
		return Defs;
	}

	FElysiumCombatCharacter* FindCharacter(FElysiumEntityWorld& World, const TCHAR* Name)
	{
		FElysiumEntity* Ent = World.FindByName(Name);
		return Ent ? Ent->AsCombatCharacter() : nullptr;
	}

	int32 Trait(const FElysiumCombatCharacter& Char, EC Container, int32 Slot)
	{
		return Char.Sheet.GetCurrent(Container, Slot);
	}

	int32 Blood(const FElysiumCombatCharacter& Char) { return Char.BloodPoolValue(); }

	// Seed a character the way a live spawn would: the sheet's authored defaults, a clan (so the
	// Kindred classification and the `Clan != Toreador` gate both have a value), and a health track.
	void SeedCharacter(FElysiumCombatCharacter& Char, const FElysiumStatTable& Stats,
		int32 ClanIndex = 2 /*Brujah*/)
	{
		Char.Sheet.SeedFrom(Stats);
		Char.Sheet.SetBase(EC::Attributes, ElysiumSlot::Clan, ClanIndex);
		Char.RecomputeSheet();
	}

	void Learn(FElysiumCombatCharacter& Char, int32 Index, int32 Rank)
	{
		Char.Sheet.SetBase(EC::Disciplines, Index, Rank);
		Char.RecomputeSheet();
	}

	bool NearlyEqual(double A, double B) { return FMath::Abs(A - B) < 1e-3; }

	// How many pending queue records carry the discipline expiry input.
	int32 PendingExpiries(const FElysiumEntityWorld& World)
	{
		int32 N = 0;
		for (const FElysiumIOEvent& Event : World.Queue().Pending())
		{
			if (Event.Input == ED::ExpiryInput()) { ++N; }
		}
		return N;
	}

	bool HasEffect(const FElysiumCombatCharacter& Char, const TCHAR* Group)
	{
		return Char.Effects.ContainsByPredicate([Group](const FString& Entry)
			{ return Entry.Equals(Group, ESearchCase::IgnoreCase); });
	}
}

// =====================================================================================
// The rulebook: the record parser's own shapes, with no file behind them
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDisciplineRulebookTest,
	"Elysium.Substrate.Discipline.Rulebook", GElysiumTestFlags)
bool FElysiumDisciplineRulebookTest::RunTest(const FString&)
{
	// --- `FElysiumDiscAmount` — the authored payload grammar ---------------------------------
	{
		FElysiumDiscAmount Flat;
		TestTrue(TEXT("a bare number parses"), Flat.Parse(TEXT("3")));
		TestFalse(TEXT("...and is not a range"), Flat.IsRange());
		TestEqual(TEXT("...with the number as its value"), Flat.Low(), 3);
		TestFalse(TEXT("...and no percent"), Flat.bPercent);

		FElysiumDiscAmount Percent;
		TestTrue(TEXT("a percentage parses"), Percent.Parse(TEXT("30%")));
		TestTrue(TEXT("...as a percentage"), Percent.bPercent);
		TestEqual(TEXT("...of 30"), Percent.Low(), 30);

		FElysiumDiscAmount Range;
		TestTrue(TEXT("a range parses"), Range.Parse(TEXT("6-8%")));
		TestTrue(TEXT("...as a range"), Range.IsRange());
		TestTrue(TEXT("...with both ends"), Range.Min == 6 && Range.Max == 8);
		TestTrue(TEXT("...and its percentage"), Range.bPercent);

		FElysiumDiscAmount Infinite;
		TestTrue(TEXT("the infinite sentinel parses"), Infinite.Parse(TEXT("-1")));
		TestFalse(TEXT("...as a single negative number, never as a range"), Infinite.IsRange());
		TestEqual(TEXT("...of -1"), Infinite.Low(), -1);

		FElysiumDiscAmount Absent;
		TestFalse(TEXT("an unauthored payload does not parse"), Absent.Parse(FString()));
		TestFalse(TEXT("...and is not authored"), Absent.bAuthored);
	}

	// --- `InheritFrom` folds a base row under a derived one ------------------------------------
	{
		FElysiumDisciplineTgt Record;
		Record.InternalName = TEXT("Test_Inherit");
		FElysiumDiscHit Base = MakeHit(TEXT("Hit_Player_Human"));
		Base.HealthBuffer.Parse(TEXT("80"));
		Base.Duration.Parse(TEXT("-1"));
		Base.TraitEffects.Add(TEXT("Discipline (Thaumaturgy-Bloodshield)"));
		Record.Hits.Add(MoveTemp(Base));

		FElysiumDiscHit Derived = MakeHit(TEXT("Hit_Supernatural_BloodGuardian"));
		Derived.InheritFrom = TEXT("Hit_Player_Human");
		Derived.HealthBuffer.Parse(TEXT("300"));   // the derived row overrides
		Record.Hits.Add(MoveTemp(Derived));

		FElysiumDiscHit Resolved;
		TestTrue(TEXT("the derived table resolves"),
			Record.ResolveHit(TEXT("Hit_Supernatural_BloodGuardian"), Resolved));
		TestEqual(TEXT("a key the derived row authors wins"), Resolved.HealthBuffer.Low(), 300);
		TestEqual(TEXT("a key only the base authors is inherited"), Resolved.Duration.Low(), -1);
		TestEqual(TEXT("...and so is its trait-effect list"), Resolved.TraitEffects.Num(), 1);

		TestFalse(TEXT("a table the record does not hold is a miss, not an empty hit"),
			Record.ResolveHit(TEXT("Hit_Nothing"), Resolved));
	}

	// --- `FindFor` picks the main record, not a helper sharing its (Discipline, Level) ---------
	{
		FRulesFixture Rules;
		const FElysiumDisciplineTgt* Main = Rules.Targets.FindFor(TEXT("Thaumaturgy"), 1);
		if (TestNotNull(TEXT("Thaumaturgy 1 resolves"), Main))
		{
			TestEqual(TEXT("...to the main record, which precedes its helper in file order"),
				Main->InternalName, FString(GBoltRecord));
		}
		const FElysiumDisciplineTgt* Helper = Rules.Targets.Find(GHelperRecord);
		if (TestNotNull(TEXT("the helper is still reachable by name"), Helper))
		{
			TestTrue(TEXT("...and is classified as a helper (zero blood, zero recovery)"),
				Helper->IsHelper());
			TestFalse(TEXT("...while the main record is not"), Main && Main->IsHelper());
		}
	}

	// --- The `Action` `Value` predicate ---------------------------------------------------------
	{
		FElysiumStat::FAction Exact = MakeAction(TEXT("3"), false);
		TestTrue(TEXT("`3` admits 3"), Exact.Admits(3));
		TestFalse(TEXT("...and refuses 2"), Exact.Admits(2));

		FElysiumStat::FAction Above = MakeAction(TEXT(">0"), false);
		TestTrue(TEXT("`>0` admits 1"), Above.Admits(1));
		TestFalse(TEXT("...and refuses 0"), Above.Admits(0));

		FElysiumStat::FAction Unauthored = MakeAction(TEXT(""), false);
		TestFalse(TEXT("an unauthored predicate admits nothing rather than everything"),
			Unauthored.Admits(1));
	}
	return true;
}

// =====================================================================================
// `FElysiumSheetEffects::FRow` — the payload operators
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDisciplinePayloadTest,
	"Elysium.Substrate.Discipline.Payload", GElysiumTestFlags)
bool FElysiumDisciplinePayloadTest::RunTest(const FString&)
{
	FRulesFixture Rules;
	FElysiumSheetEffects Layer;
	const FString Groups[] = { TEXT("History (TEST Discipline Mods)"),
		TEXT("Discipline (Fortitude3)") };
	Layer.Build(Rules.Effects, Groups, &Rules.Feats);

	// The payload operators never move a trait's value — the engine's own switch breaks on them.
	TestEqual(TEXT("a Duration payload does not move the trait it names"),
		Layer.ApplyToTrait(EC::Disciplines, ED::Fortitude, 3), 3);
	TestEqual(TEXT("a BloodCost payload does not move the trait it names"),
		Layer.ApplyToTrait(EC::Disciplines, ED::Thaumaturgy, 2), 2);

	// They are readable by the system that owns them.
	TestTrue(TEXT("the Duration payload is present on Fortitude"),
		Layer.HasPayload(EElysiumTraitOp::Duration, EC::Disciplines, ED::Fortitude));
	TestEqual(TEXT("`Duration 200%` doubles a 25 s window"),
		Layer.ApplyPayload(EElysiumTraitOp::Duration, EC::Disciplines, ED::Fortitude, 25), 50);
	TestEqual(TEXT("`BloodCost 2` adds two to a record's cost"),
		Layer.ApplyPayload(EElysiumTraitOp::BloodCost, EC::Disciplines, ED::Thaumaturgy, 1), 3);
	TestEqual(TEXT("a trait with no payload row is returned unchanged"),
		Layer.ApplyPayload(EElysiumTraitOp::Duration, EC::Disciplines, ED::Auspex, 20), 20);
	TestFalse(TEXT("...and reports no payload"),
		Layer.HasPayload(EElysiumTraitOp::BloodCost, EC::Disciplines, ED::Auspex));

	// The arithmetic rows in the same group still land where they always did.
	TestEqual(TEXT("the Fortitude group still adds its automatic soak successes"),
		Layer.ApplyToTrait(EC::Attributes, ElysiumSlot::AutomaticSoakSuccesses, 0), 3);
	return true;
}

// =====================================================================================
// Activation: the learned gate, the single payment, tier memory and renewal
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDisciplineActivationTest,
	"Elysium.Substrate.Discipline.Activation", GElysiumTestFlags)
bool FElysiumDisciplineActivationTest::RunTest(const FString&)
{
	FRulesFixture Rules;
	ElysiumRng::SeedAll(9109);
	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MakeDisciplineTestDefs());
	World.SpawnPlayer();
	World.Activate(0.0);
	World.Tick(0.0);

	FElysiumPlayer* Player = World.FindPlayer();
	if (!TestNotNull(TEXT("the player exists"), Player))
	{
		return false;
	}
	SeedCharacter(*Player, Rules.Stats);
	TestEqual(TEXT("the seeded pool is the authored default"), Blood(*Player), 10);

	// --- The learned gate, before anything is spent -------------------------------------------
	{
		const ED::EResult Result = ED::Use(*Player, ED::Fortitude, 3);
		TestEqual(TEXT("an unlearned discipline is refused"), (int32)Result,
			(int32)ED::EResult::RefusedNotLearned);
		TestEqual(TEXT("...and nothing is spent"), Blood(*Player), 10);
		TestEqual(TEXT("...and no active slot moves"),
			Trait(*Player, EC::ActiveDisciplines, ED::Fortitude), 0);
	}

	// --- The activation transaction -------------------------------------------------------------
	Learn(*Player, ED::Fortitude, 3);
	{
		const ED::EResult Result = ED::Use(*Player, ED::Fortitude, 3);
		TestEqual(TEXT("a learned discipline activates"), (int32)Result,
			(int32)ED::EResult::Accepted);
		TestEqual(TEXT("the blood payment lands exactly once"), Blood(*Player), 9);
		TestEqual(TEXT("the active slot carries the tier"),
			Trait(*Player, EC::ActiveDisciplines, ED::Fortitude), 3);
		TestTrue(TEXT("the authored trait-effect group is installed"),
			HasEffect(*Player, TEXT("Discipline (Fortitude3)")));
		TestTrue(TEXT("the expiry is scheduled at the authored Initial_N"),
			NearlyEqual(Player->Disciplines.EndTime[ED::Fortitude], 25.0));
		TestEqual(TEXT("...as ONE owned event on the one queue"), PendingExpiries(World), 1);
	}

	// --- Renewal extends the owned event rather than stacking a second owner --------------------
	{
		World.Tick(10.0);
		const int32 FirstSerial = Player->Disciplines.ExpirySerial[ED::Fortitude];
		const ED::EResult Result = ED::Use(*Player, ED::Fortitude, 3);
		TestEqual(TEXT("a renewal is accepted"), (int32)Result, (int32)ED::EResult::Accepted);
		TestEqual(TEXT("...and pays the increment actions again"), Blood(*Player), 8);
		TestTrue(TEXT("...extending the deadline by the authored Add_N"),
			NearlyEqual(Player->Disciplines.EndTime[ED::Fortitude], 50.0));
		TestNotEqual(TEXT("...under a new serial"),
			Player->Disciplines.ExpirySerial[ED::Fortitude], FirstSerial);
		TestEqual(TEXT("...with exactly one live active slot, never two"),
			Trait(*Player, EC::ActiveDisciplines, ED::Fortitude), 3);
		TestEqual(TEXT("...and exactly one installed copy of the group"),
			Player->Effects.FilterByPredicate([](const FString& E)
				{ return E.Equals(TEXT("Discipline (Fortitude3)")); }).Num(), 1);
	}

	// --- `Add_N` differs from `Initial_N` where the file says so -------------------------------
	{
		Learn(*Player, ED::Auspex, 2);
		ED::Use(*Player, ED::Auspex, 2);
		const double FirstEnd = Player->Disciplines.EndTime[ED::Auspex];
		TestTrue(TEXT("Auspex opens on its authored Initial_2"), NearlyEqual(FirstEnd, 10.0 + 20.0));
		ED::Use(*Player, ED::Auspex, 2);
		TestTrue(TEXT("...and renews by its authored Add_2, which is a different number"),
			NearlyEqual(Player->Disciplines.EndTime[ED::Auspex], FirstEnd + 10.0));
	}

	// --- The predependency gate ------------------------------------------------------------------
	{
		Learn(*Player, ED::CorpusVampirus, 1);
		Player->Sheet.SetBase(EC::Attributes, ElysiumSlot::BloodPool, 2);
		Player->RecomputeSheet();
		const ED::EResult Result = ED::Use(*Player, ED::CorpusVampirus, 1);
		TestEqual(TEXT("`BloodPool > 2` refuses Bloodbuff at two blood"), (int32)Result,
			(int32)ED::EResult::RefusedPredependency);
		TestEqual(TEXT("...spending nothing"), Blood(*Player), 2);

		Player->Sheet.SetBase(EC::Attributes, ElysiumSlot::BloodPool, 6);
		Player->RecomputeSheet();
		TestEqual(TEXT("...and admits it at six"), (int32)ED::Use(*Player, ED::CorpusVampirus, 1),
			(int32)ED::EResult::Accepted);
		TestEqual(TEXT("Bloodbuff's authored payment is three"), Blood(*Player), 3);
		TestEqual(TEXT("...and its group raises Strength past its ordinary max of 5"),
			Trait(*Player, EC::Attributes, ElysiumSlot::Strength), 4);
		TestTrue(TEXT("the `Clan != Toreador` arm applies to a Brujah"),
			HasEffect(*Player, TEXT("Discipline (Corpus_Vampirus-NonToreador)")));
	}

	// --- Selection: tier memory ------------------------------------------------------------------
	{
		Player->Sheet.SetBase(EC::Attributes, ElysiumSlot::BloodPool, 10);
		Player->RecomputeSheet();
		TestEqual(TEXT("selecting an unlearned index is rejected"),
			(int32)ED::Select(*Player, ED::Protean), (int32)ED::EResult::RefusedNotLearned);
		TestEqual(TEXT("...and the selection does not move"), Player->SelectedDiscipline,
			(int32)INDEX_NONE);

		ED::Select(*Player, ED::Fortitude);
		TestEqual(TEXT("a valid selection is stored"), Player->SelectedDiscipline,
			(int32)ED::Fortitude);
		TestEqual(TEXT("...with the remembered tier reset to zero"), Player->SelectedTier, 0);

		Player->SelectedTier = 2;
		ED::Select(*Player, ED::Fortitude);
		TestEqual(TEXT("reselecting the same index preserves the remembered tier"),
			Player->SelectedTier, 2);

		ED::Select(*Player, ED::Auspex);
		TestEqual(TEXT("a newly selected index resets it"), Player->SelectedTier, 0);
		TestEqual(TEXT("...and stores the new index"), Player->SelectedDiscipline,
			(int32)ED::Auspex);

		// `vdiscipline_last` performs only the shared authority's last step with the remembered pair.
		const int32 BloodBefore = Blood(*Player);
		TestEqual(TEXT("vdiscipline_last casts the remembered pair"),
			(int32)ED::UseLast(*Player), (int32)ED::EResult::Accepted);
		TestEqual(TEXT("...paying once"), Blood(*Player), BloodBefore - 1);
	}
	return true;
}

// =====================================================================================
// Expiry: the owned event, the stale-renewal guard and the recompute
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDisciplineExpiryTest,
	"Elysium.Substrate.Discipline.Expiry", GElysiumTestFlags)
bool FElysiumDisciplineExpiryTest::RunTest(const FString&)
{
	FRulesFixture Rules;
	ElysiumRng::SeedAll(9109);
	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MakeDisciplineTestDefs());
	World.SpawnPlayer();
	World.Activate(0.0);
	World.Tick(0.0);

	FElysiumPlayer* Player = World.FindPlayer();
	if (!TestNotNull(TEXT("the player exists"), Player))
	{
		return false;
	}
	SeedCharacter(*Player, Rules.Stats);
	Learn(*Player, ED::Fortitude, 3);

	// --- The teardown recomputes the sheet ------------------------------------------------------
	ED::Use(*Player, ED::Fortitude, 3);
	TestEqual(TEXT("the Fortitude group feeds the automatic-soak slot while it is up"),
		Trait(*Player, EC::Attributes, ElysiumSlot::AutomaticSoakSuccesses), 3);

	World.Tick(24.0);
	TestEqual(TEXT("the state stands until its deadline"),
		Trait(*Player, EC::ActiveDisciplines, ED::Fortitude), 3);

	World.Tick(26.0);
	TestEqual(TEXT("the owned event clears the active slot"),
		Trait(*Player, EC::ActiveDisciplines, ED::Fortitude), 0);
	TestFalse(TEXT("...removes the group it installed"),
		HasEffect(*Player, TEXT("Discipline (Fortitude3)")));
	TestEqual(TEXT("...and RECOMPUTES: the automatic-soak slot goes back to zero"),
		Trait(*Player, EC::Attributes, ElysiumSlot::AutomaticSoakSuccesses), 0);
	TestEqual(TEXT("...leaving no pending expiry behind"), PendingExpiries(World), 0);

	// --- A renewal's superseded event is dropped, not honoured ----------------------------------
	{
		ED::Use(*Player, ED::Fortitude, 3);            // ends at 26 + 25 = 51
		World.Tick(40.0);
		ED::Use(*Player, ED::Fortitude, 3);            // renewal: extends to 76
		TestEqual(TEXT("both records are on the queue"), PendingExpiries(World), 2);

		World.Tick(52.0);                              // past the FIRST deadline only
		TestEqual(TEXT("the superseded event is dropped as stale"),
			Trait(*Player, EC::ActiveDisciplines, ED::Fortitude), 3);
		TestTrue(TEXT("...leaving the extended state up"),
			HasEffect(*Player, TEXT("Discipline (Fortitude3)")));

		World.Tick(77.0);                              // past the extended deadline
		TestEqual(TEXT("the live event ends it"),
			Trait(*Player, EC::ActiveDisciplines, ED::Fortitude), 0);
	}
	return true;
}

// =====================================================================================
// The one teardown: `vdiscipline_endall` and the `ClearActiveDisciplines` input
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDisciplineTeardownTest,
	"Elysium.Substrate.Discipline.Teardown", GElysiumTestFlags)
bool FElysiumDisciplineTeardownTest::RunTest(const FString&)
{
	FRulesFixture Rules;
	ElysiumRng::SeedAll(9109);
	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MakeDisciplineTestDefs());
	World.SpawnPlayer();
	World.Activate(0.0);
	World.Tick(0.0);

	FElysiumPlayer* Player = World.FindPlayer();
	if (!TestNotNull(TEXT("the player exists"), Player))
	{
		return false;
	}
	SeedCharacter(*Player, Rules.Stats);
	Learn(*Player, ED::Fortitude, 3);
	Learn(*Player, ED::Auspex, 2);
	Learn(*Player, ED::Thaumaturgy, 3);

	ED::Use(*Player, ED::Fortitude, 3);
	ED::Use(*Player, ED::Auspex, 2);
	ED::Use(*Player, ED::Thaumaturgy, 3);   // the targeted self-shaped shield
	TestEqual(TEXT("two native states are up"),
		Trait(*Player, EC::ActiveDisciplines, ED::Fortitude)
		+ Trait(*Player, EC::ActiveDisciplines, ED::Auspex), 5);
	TestEqual(TEXT("...and one targeted effect is tracked"),
		Player->Disciplines.TargetEffects.Num(), 1);
	TestEqual(TEXT("the shield filled the health buffer"),
		Trait(*Player, EC::Attributes, ElysiumSlot::HealthBuffer), 80);

	// --- The input, delivered through the one queue --------------------------------------------
	World.EnqueueInput(TEXT("!player"), FName(TEXT("ClearActiveDisciplines")),
		FElysiumVariant::Void(), 0.0, FElysiumEntityHandle::Invalid(),
		FElysiumEntityHandle::Invalid());
	World.Tick(1.0);

	TestEqual(TEXT("ClearActiveDisciplines zeroes every active slot"),
		Trait(*Player, EC::ActiveDisciplines, ED::Fortitude)
		+ Trait(*Player, EC::ActiveDisciplines, ED::Auspex), 0);
	TestEqual(TEXT("...drops every tracked targeted effect"),
		Player->Disciplines.TargetEffects.Num(), 0);
	TestFalse(TEXT("...removes the groups both families installed"),
		HasEffect(*Player, TEXT("Discipline (Fortitude3)"))
		|| HasEffect(*Player, TEXT("Discipline (Thaumaturgy-Bloodshield)")));
	TestEqual(TEXT("...and recomputes the slots those groups fed"),
		Trait(*Player, EC::Attributes, ElysiumSlot::AutomaticSoakSuccesses), 0);

	// The pending events are retired: delivering them changes nothing.
	World.Tick(100.0);
	TestEqual(TEXT("a retired expiry is a no-op, not a second teardown"),
		Trait(*Player, EC::ActiveDisciplines, ED::Fortitude), 0);

	// --- `vdiscipline_endall` is the same path, and is idempotent -------------------------------
	ED::Use(*Player, ED::Fortitude, 3);
	TestEqual(TEXT("a discipline can be re-cast after a teardown"),
		Trait(*Player, EC::ActiveDisciplines, ED::Fortitude), 3);
	ED::ExecuteEndAll(World);
	TestEqual(TEXT("vdiscipline_endall converges on the same teardown"),
		Trait(*Player, EC::ActiveDisciplines, ED::Fortitude), 0);
	ED::ExecuteEndAll(World);
	TestEqual(TEXT("...and running it again is a no-op"),
		Player->Disciplines.TargetEffects.Num(), 0);
	return true;
}

// =====================================================================================
// The per-power joins: Fortitude -> soak, Potence -> the melee floor, Bloodshield -> buffer
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDisciplineFortitudeTest,
	"Elysium.Substrate.Discipline.Fortitude", GElysiumTestFlags)
bool FElysiumDisciplineFortitudeTest::RunTest(const FString&)
{
	FRulesFixture Rules;
	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MakeDisciplineTestDefs());
	World.SpawnPlayer();
	World.Activate(0.0);
	World.Tick(0.0);

	FElysiumPlayer* Player = World.FindPlayer();
	if (!TestNotNull(TEXT("the player exists"), Player))
	{
		return false;
	}
	SeedCharacter(*Player, Rules.Stats);
	Learn(*Player, ED::Fortitude, 4);

	// The join is one slot: the discipline's trait-effect group writes
	// `Automatic_Soak_Successes`, and the damage resolver reads that slot. Neither half knows
	// about the other, which is why no damage-file edit was needed.
	auto RollSoak = [Player]() -> int32
	{
		// Same seed on both sides, so the only difference between the two runs is the slot.
		ElysiumRng::SeedAll(4242);
		FElysiumDmg Dmg;
		Dmg.Family = EElysiumDmgFamily::Bashing;
		Dmg.Flags = ElysiumDamage::FlagDirectInput;
		Dmg.ExtraInput = 40;
		FElysiumDamageContext Context = FElysiumDamageContext::FromCharacter(*Player);
		ElysiumDamage::Apply(Dmg, nullptr, *Player, Context, /*bDisallowFirearmsToBashing*/ false);
		return Dmg.SoakSuccesses;
	};

	const int32 WithoutFortitude = RollSoak();
	TestEqual(TEXT("the automatic-soak slot starts at zero"),
		Trait(*Player, EC::Attributes, ElysiumSlot::AutomaticSoakSuccesses), 0);

	ED::Use(*Player, ED::Fortitude, 4);
	TestEqual(TEXT("Fortitude 4 writes +4 automatic soak successes through its effect group"),
		Trait(*Player, EC::Attributes, ElysiumSlot::AutomaticSoakSuccesses), 4);

	const int32 WithFortitude = RollSoak();
	TestEqual(TEXT("the soak resolver reads exactly those automatic successes"),
		WithFortitude - WithoutFortitude, 4);

	ED::ClearAll(*Player);
	TestEqual(TEXT("teardown takes them back off"),
		Trait(*Player, EC::Attributes, ElysiumSlot::AutomaticSoakSuccesses), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDisciplinePotenceTest,
	"Elysium.Substrate.Discipline.Potence", GElysiumTestFlags)
bool FElysiumDisciplinePotenceTest::RunTest(const FString&)
{
	FRulesFixture Rules;

	// A minimal melee catalogue. The classname is test-local because `ElysiumItems::Install`
	// registers a class once per process and never unregisters.
	const TCHAR* const Fists = TEXT("item_w_test_potence_fists");
	FElysiumItemTable Items;
	{
		FElysiumItemDef Def;
		Def.Classname = Fists;
		Def.PrintName = Fists;
		Def.Type = EElysiumItemType::WeaponMelee;
		Def.bWieldable = true;
		FElysiumWeaponMode Mode;
		Mode.Tag = TEXT("Primary");
		Mode.TypeName = TEXT("Attack");
		Mode.Type = EElysiumWeaponModeType::Attack;
		// `DMG_FAITH` is in the recovered no-soak mask, so the committed number is exactly the
		// melee commit's own `DamageInflicted x BaseDamage` with no soak roll between them — which
		// is what makes the floor assertion below exact rather than dice-dependent. The victim is
		// mortal, so the mask's aggravated-tracking arm never runs either.
		Mode.Dmg = TEXT("1 Lethal Close_Combat_Brawl DMG_FAITH");
		Mode.BaseLethality = 2;   // a deliberately tiny margin, so the Potence floor is what decides
		Mode.SkillRequirement = 1;
		Mode.AttackRate = 0.5f;
		Def.Modes.Add(MoveTemp(Mode));
		Items.Items.Add(MoveTemp(Def));
	}
	// The lookup IS the index: a hand-built table has to build it, or `GiveNamedItem` finds nothing.
	Items.Reindex();
	ElysiumItems::Install(Items);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Items); };

	auto RunMelee = [&Rules, Fists](int32 PotenceRank) -> int32
	{
		ElysiumRng::SeedAll(7100);
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeDisciplineTestDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumPlayer* Player = World.FindPlayer();
		FElysiumCombatCharacter* Victim = FindCharacter(World, TEXT("victim0"));
		if (!Player || !Victim)
		{
			return INDEX_NONE;
		}
		SeedCharacter(*Player, Rules.Stats);
		SeedCharacter(*Victim, Rules.Stats);
		Player->Origin = FVector::ZeroVector;
		Player->Angles = FVector::ZeroVector;   // yaw 0: forward is +X, where victim0 stands

		if (PotenceRank > 0)
		{
			Learn(*Player, ED::Potence, PotenceRank);
			ED::Use(*Player, ED::Potence, PotenceRank);
		}
		if (ElysiumWeapons::ActivePotenceRank(*Player) != PotenceRank)
		{
			return INDEX_NONE;   // the join itself failed; the caller reports it
		}

		const FElysiumEntityHandle Handle = Player->Inventory.GiveNamedItem(*Player, Fists);
		FElysiumEntity* Ent = World.Resolve(Handle);
		FElysiumItem* Item = Ent ? Ent->AsItem() : nullptr;
		FElysiumWeapon* Weapon = Item ? Item->AsWeapon() : nullptr;
		if (!Weapon)
		{
			return INDEX_NONE;
		}
		// Melee acquires its own opponent: the player stands at the origin facing +X, where
		// `victim0` is.
		Weapon->AttackIntent(FElysiumWeapon::EIntent::Primary);
		World.Tick(2.0);   // the commit enters on the animation event, which the schedule supplies
		return Victim->Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Health);
	};

	// Both runs draw from the same seeded stream and differ in exactly one thing — the active
	// Potence rank — so the committed number is the floor applied to the same margin.
	const int32 Unbuffed = RunMelee(0);
	const int32 Buffed = RunMelee(5);
	if (!TestTrue(TEXT("`ActivePotenceRank` reads the active slot in both runs"),
		Unbuffed != INDEX_NONE && Buffed != INDEX_NONE))
	{
		return false;
	}
	if (!TestTrue(TEXT("the unbuffed melee commit landed a positive margin"), Unbuffed > 0))
	{
		return false;   // nothing to floor: the assertion below would be vacuous
	}
	TestEqual(TEXT("the active Potence rank floors the melee commit's DamageInflicted"),
		Buffed, FMath::Max(Unbuffed, 5));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDisciplineBloodshieldTest,
	"Elysium.Substrate.Discipline.Bloodshield", GElysiumTestFlags)
bool FElysiumDisciplineBloodshieldTest::RunTest(const FString&)
{
	FRulesFixture Rules;
	ElysiumRng::SeedAll(9109);
	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MakeDisciplineTestDefs());
	World.SpawnPlayer();
	World.Activate(0.0);
	World.Tick(0.0);

	FElysiumPlayer* Player = World.FindPlayer();
	if (!TestNotNull(TEXT("the player exists"), Player))
	{
		return false;
	}
	SeedCharacter(*Player, Rules.Stats);
	Learn(*Player, ED::Thaumaturgy, 3);

	TestEqual(TEXT("the shield casts"), (int32)ED::Use(*Player, ED::Thaumaturgy, 3),
		(int32)ED::EResult::Accepted);
	TestEqual(TEXT("the authored blood cost is paid once"), Blood(*Player), 7);
	TestEqual(TEXT("the hit fills HealthBuffer"),
		Trait(*Player, EC::Attributes, ElysiumSlot::HealthBuffer), 80);
	TestTrue(TEXT("...and installs the Bloodshield trait group"),
		HasEffect(*Player, TEXT("Discipline (Thaumaturgy-Bloodshield)")));
	TestEqual(TEXT("...tracked as one owned targeted effect"),
		Player->Disciplines.TargetEffects.Num(), 1);
	TestTrue(TEXT("...with the authored infinite duration"),
		Player->Disciplines.TargetEffects[0].IsInfinite());

	// A partial absorption only reduces the buffer.
	Player->TakeDamage(30.0f);
	TestEqual(TEXT("the buffer absorbs first"),
		Trait(*Player, EC::Attributes, ElysiumSlot::HealthBuffer), 50);
	TestEqual(TEXT("...and no damage reaches the health counter"),
		Trait(*Player, EC::Attributes, ElysiumSlot::Health), 0);
	TestEqual(TEXT("...leaving the effect tracked"), Player->Disciplines.TargetEffects.Num(), 1);

	// Exhausting it ends the power and reconciles the tracking.
	Player->TakeDamage(70.0f);
	TestEqual(TEXT("the exhausted buffer clears"),
		Trait(*Player, EC::Attributes, ElysiumSlot::HealthBuffer), 0);
	TestEqual(TEXT("...and the remainder lands on the health counter"),
		Trait(*Player, EC::Attributes, ElysiumSlot::Health), 20);
	TestFalse(TEXT("...ending the Bloodshield trait group"),
		HasEffect(*Player, TEXT("Discipline (Thaumaturgy-Bloodshield)")));
	TestEqual(TEXT("...and retiring its tracked effect with it"),
		Player->Disciplines.TargetEffects.Num(), 0);
	return true;
}

// =====================================================================================
// The targeted transaction: gate order, the single payment, per-target application
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDisciplineTargetedTest,
	"Elysium.Substrate.Discipline.Targeted", GElysiumTestFlags)
bool FElysiumDisciplineTargetedTest::RunTest(const FString&)
{
	FRulesFixture Rules;
	ElysiumRng::SeedAll(9109);
	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MakeDisciplineTestDefs());
	World.SpawnPlayer();
	World.Activate(0.0);
	World.Tick(0.0);

	FElysiumPlayer* Player = World.FindPlayer();
	if (!TestNotNull(TEXT("the player exists"), Player))
	{
		return false;
	}
	SeedCharacter(*Player, Rules.Stats);
	Player->Origin = FVector::ZeroVector;
	Player->Angles = FVector::ZeroVector;
	TArray<FElysiumCombatCharacter*> Victims;
	for (int32 i = 0; i < 3; ++i)
	{
		FElysiumCombatCharacter* Victim =
			FindCharacter(World, *FString::Printf(TEXT("victim%d"), i));
		if (!TestNotNull(TEXT("a victim exists"), Victim))
		{
			return false;
		}
		SeedCharacter(*Victim, Rules.Stats, /*ClanIndex*/ 0);   // mortal: the Human filter arm
		Victims.Add(Victim);
	}

	// --- Step 2 comes before step 5: an unlearned rank refuses without paying ------------------
	{
		const ED::EResult Result = ED::Use(*Player, ED::Dominate, 1);
		TestEqual(TEXT("an unlearned targeted discipline is refused"), (int32)Result,
			(int32)ED::EResult::RefusedNotLearned);
		TestEqual(TEXT("...before any blood is spent"), Blood(*Player), 10);
	}

	Learn(*Player, ED::Dominate, 1);

	// --- Step 2: the blood verify, still before payment ----------------------------------------
	{
		Player->Sheet.SetBase(EC::Attributes, ElysiumSlot::BloodPool, 1);
		Player->RecomputeSheet();
		const ED::EResult Result = ED::Use(*Player, ED::Dominate, 1);
		TestEqual(TEXT("a cost above the pool is refused"), (int32)Result,
			(int32)ED::EResult::RefusedBlood);
		TestEqual(TEXT("...spending nothing"), Blood(*Player), 1);
		Player->Sheet.SetBase(EC::Attributes, ElysiumSlot::BloodPool, 10);
		Player->RecomputeSheet();
	}

	// --- Step 4 comes before step 5: an empty target set aborts before payment -----------------
	{
		// Move every candidate out of the record's authored 600-unit radius.
		TArray<FVector> Saved;
		for (FElysiumCombatCharacter* Victim : Victims)
		{
			Saved.Add(Victim->Origin);
			Victim->Origin = FVector(900000.0f, 0.0f, 0.0f);
		}
		const ED::EResult Result = ED::Use(*Player, ED::Dominate, 1);
		TestEqual(TEXT("an empty target set aborts"), (int32)Result,
			(int32)ED::EResult::RefusedNoTargets);
		TestEqual(TEXT("...BEFORE the single payment"), Blood(*Player), 10);
		TestEqual(TEXT("...and increments no cast counter"), Player->DisciplineCastCount, 0);
		for (int32 i = 0; i < Victims.Num(); ++i)
		{
			Victims[i]->Origin = Saved[i];
		}
	}

	// --- Steps 5-7: one payment, every resolved target hit -------------------------------------
	{
		const ED::EResult Result = ED::Use(*Player, ED::Dominate, 1);
		TestEqual(TEXT("the cast commits"), (int32)Result, (int32)ED::EResult::Accepted);
		TestEqual(TEXT("the adjusted cost is deducted ONCE, not once per target"),
			Blood(*Player), 8);
		TestEqual(TEXT("...and the player's cast counter advances once"),
			Player->DisciplineCastCount, 1);
		int32 Affected = 0;
		for (FElysiumCombatCharacter* Victim : Victims)
		{
			Affected += (Victim->Disciplines.TargetEffects.Num() == 1) ? 1 : 0;
			TestTrue(TEXT("every resolved target takes the mapped hit's trait group"),
				HasEffect(*Victim, TEXT("Discipline (Test-Daze)")));
		}
		TestEqual(TEXT("all three in-range targets are affected"), Affected, 3);
		TestTrue(TEXT("the caster is excluded by the record's own No_Self filter"),
			Player->Disciplines.TargetEffects.IsEmpty());
		TestTrue(TEXT("a candidate outside the authored range is not in the set"),
			FindCharacter(World, TEXT("faraway"))->Disciplines.TargetEffects.IsEmpty());
	}

	// --- Step 8: the record's own recovery refuses a second cast --------------------------------
	{
		const ED::EResult Result = ED::Use(*Player, ED::Dominate, 1);
		TestEqual(TEXT("the authored RecoveryTime refuses an immediate recast"), (int32)Result,
			(int32)ED::EResult::RefusedRecovering);
		TestEqual(TEXT("...spending nothing"), Blood(*Player), 8);
	}

	// --- The duration is an owned event and its expiry removes the effect ----------------------
	{
		World.Tick(9.0);
		TestTrue(TEXT("the effect stands until its authored duration"),
			HasEffect(*Victims[0], TEXT("Discipline (Test-Daze)")));
		World.Tick(11.0);
		TestFalse(TEXT("the owned expiry removes the group it installed"),
			HasEffect(*Victims[0], TEXT("Discipline (Test-Daze)")));
		TestEqual(TEXT("...and stops tracking it"),
			Victims[0]->Disciplines.TargetEffects.Num(), 0);
	}

	// --- `Trigger_Casting` runs a zero-cost helper's implementable channels ---------------------
	{
		Learn(*Player, ED::Thaumaturgy, 1);
		Player->Sheet.SetBase(EC::Attributes, ElysiumSlot::BloodPool, 5);
		Player->RecomputeSheet();
		const int32 VictimHealthBefore =
			Victims[0]->Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Health);
		const ED::EResult Result = ED::Use(*Player, ED::Thaumaturgy, 1);
		TestEqual(TEXT("the bolt commits"), (int32)Result, (int32)ED::EResult::Accepted);
		TestEqual(TEXT("`Dmg_Health 30%` lands as a fraction of the victim's Max_Health"),
			Victims[0]->Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Health) - VictimHealthBefore,
			30);
		// 5 blood - 1 cost + 2 the nested zero-cost helper returned.
		TestEqual(TEXT("the nested helper's blood channel runs, and it is not a second purchase"),
			Blood(*Player), 6);
	}
	return true;
}

// =====================================================================================
// The three interruption flags, each through its own subscription
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDisciplineInterruptionTest,
	"Elysium.Substrate.Discipline.Interruption", GElysiumTestFlags)
bool FElysiumDisciplineInterruptionTest::RunTest(const FString&)
{
	FRulesFixture Rules;
	ElysiumRng::SeedAll(9109);
	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MakeDisciplineTestDefs());
	World.SpawnPlayer();
	World.Activate(0.0);
	World.Tick(0.0);

	FElysiumPlayer* Player = World.FindPlayer();
	if (!TestNotNull(TEXT("the player exists"), Player))
	{
		return false;
	}
	SeedCharacter(*Player, Rules.Stats);
	Player->Origin = FVector::ZeroVector;   // inside `victim0`'s NPC_TAKE_DAMAGE reach
	Learn(*Player, ED::Dominate, 5);

	// All three records are zero-cost, self-shaped and infinite, so nothing but the flag ends them.
	auto CastAll = [Player]()
	{
		ED::ClearAll(*Player);
		ED::Use(*Player, ED::Dominate, 2);   // damage-interruptible
		ED::Use(*Player, ED::Dominate, 3);   // hear-combat-interruptible
		ED::Use(*Player, ED::Dominate, 4);   // bump-interruptible
	};
	auto Tracked = [Player](const TCHAR* Record) -> bool
	{
		return Player->Disciplines.TargetEffects.ContainsByPredicate(
			[Record](const FElysiumActiveDisciplineEffect& Effect)
				{ return Effect.Record.Equals(Record); });
	};

	// --- Damage, through the one typed health commit ---------------------------------------------
	CastAll();
	TestEqual(TEXT("three interruptible effects are tracked"),
		Player->Disciplines.TargetEffects.Num(), 3);
	Player->TakeDamage(5.0f);
	TestFalse(TEXT("ShouldRemove_OnTakeDamage fires from the real commit"),
		Tracked(TEXT("Test_Interrupt_Damage")));
	TestTrue(TEXT("...and leaves the other two alone"),
		Tracked(TEXT("Test_Interrupt_Hear")) && Tracked(TEXT("Test_Interrupt_Bump")));

	// --- Heard combat, through the substrate sound-event bus -------------------------------------
	CastAll();
	{
		// A real producer: another character taking damage emits `NPC_TAKE_DAMAGE` on the bus.
		FElysiumCombatCharacter* Victim = FindCharacter(World, TEXT("victim0"));
		if (!TestNotNull(TEXT("a victim exists"), Victim))
		{
			return false;
		}
		SeedCharacter(*Victim, Rules.Stats, /*ClanIndex*/ 0);
		Victim->TakeDamage(5.0f);
		// The bus delivers nothing; the poll happens on the owner's think. `Tick` runs every OTHER
		// entity's think — `RunThinks` skips the player, whose think the map actor drives from the
		// pre-move pass — so the player's own poll is reached through that same entry.
		World.RunPlayerThink(1.0);
		TestFalse(TEXT("ShouldRemove_OnHearCombat fires from the sound bus poll"),
			Tracked(TEXT("Test_Interrupt_Hear")));
		TestTrue(TEXT("...and leaves the bump effect alone"), Tracked(TEXT("Test_Interrupt_Bump")));
	}

	// --- The bump entry -------------------------------------------------------------------------
	CastAll();
	ED::NotifyBumped(*Player);
	TestFalse(TEXT("ShouldRemove_OnWasBumped fires from the bump entry"),
		Tracked(TEXT("Test_Interrupt_Bump")));
	TestTrue(TEXT("...and leaves the damage effect alone"), Tracked(TEXT("Test_Interrupt_Damage")));
	return true;
}

// =====================================================================================
// Law production: the supernatural level, and the independent Overt criminal write
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDisciplineLawTest,
	"Elysium.Substrate.Discipline.Law", GElysiumTestFlags)
bool FElysiumDisciplineLawTest::RunTest(const FString&)
{
	FRulesFixture Rules;
	ElysiumRng::SeedAll(9109);
	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MakeDisciplineTestDefs());
	World.SpawnPlayer();
	World.Activate(0.0);
	World.Tick(0.0);

	FElysiumPlayer* Player = World.FindPlayer();
	if (!TestNotNull(TEXT("the player exists"), Player))
	{
		return false;
	}
	SeedCharacter(*Player, Rules.Stats);
	for (int32 i = 0; i < 3; ++i)
	{
		if (FElysiumCombatCharacter* Victim =
			FindCharacter(World, *FString::Printf(TEXT("victim%d"), i)))
		{
			SeedCharacter(*Victim, Rules.Stats, /*ClanIndex*/ 0);
		}
	}
	TestEqual(TEXT("the law counters start clear"),
		Player->Law.Supernatural + Player->Law.Criminal, 0);

	// A covert record: `Overt` is 0, so only the supernatural level moves.
	Learn(*Player, ED::Dominate, 1);
	TestEqual(TEXT("the covert cast commits"), (int32)ED::Use(*Player, ED::Dominate, 1),
		(int32)ED::EResult::Accepted);
	TestEqual(TEXT("a committed cast raises supernatural activity to the record's SupernaturalLvl"),
		Player->Law.Supernatural, 2);
	TestEqual(TEXT("...and a covert record makes no criminal call"), Player->Law.Criminal, 0);

	// An overt record: the independent `Overt` byte raises criminal activity to 3.
	Learn(*Player, ED::Thaumaturgy, 3);
	TestEqual(TEXT("the overt cast commits"), (int32)ED::Use(*Player, ED::Thaumaturgy, 3),
		(int32)ED::EResult::Accepted);
	TestEqual(TEXT("...raising supernatural activity to its own SupernaturalLvl"),
		Player->Law.Supernatural, 4);
	TestEqual(TEXT("...and criminal activity to the literal 3"), Player->Law.Criminal, 3);

	// A refused cast produces no law at all.
	Player->Law = FElysiumLawState();
	Player->Sheet.SetBase(EC::Attributes, ElysiumSlot::BloodPool, 0);
	Player->RecomputeSheet();
	ED::Use(*Player, ED::Thaumaturgy, 3);
	TestEqual(TEXT("a refused cast produces no law"),
		Player->Law.Supernatural + Player->Law.Criminal, 0);
	return true;
}

// =====================================================================================
// Cycle 11b — the `HitInfo.AI_Schedule` channel into the NPC kernel
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDisciplineAiScheduleTest,
	"Elysium.Substrate.Discipline.AiSchedule", GElysiumTestFlags)
bool FElysiumDisciplineAiScheduleTest::RunTest(const FString&)
{
	ElysiumStub::ClearTally();
	ON_SCOPE_EXIT { ElysiumStub::ClearTally(); };

	FRulesFixture Rules;
	ElysiumRng::SeedAll(9109);
	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MakeDisciplineTestDefs());
	World.SpawnPlayer();
	World.Activate(0.0);
	World.Tick(0.0);

	FElysiumPlayer* Player = World.FindPlayer();
	if (!TestNotNull(TEXT("the player exists"), Player))
	{
		return false;
	}
	SeedCharacter(*Player, Rules.Stats);
	Player->Origin = FVector::ZeroVector;

	TArray<FElysiumNpc*> Victims;
	TArray<EElysiumScheduleId> Before;
	for (int32 i = 0; i < 3; ++i)
	{
		FElysiumCombatCharacter* Victim =
			FindCharacter(World, *FString::Printf(TEXT("victim%d"), i));
		if (!TestNotNull(TEXT("a victim exists"), Victim))
		{
			return false;
		}
		SeedCharacter(*Victim, Rules.Stats, /*ClanIndex*/ 0);
		FElysiumNpc* Npc = Victim->AsNpc();
		if (!TestNotNull(TEXT("...and it is an NPC, so it has a schedule kernel"), Npc))
		{
			return false;
		}
		// Whatever ordinary selection has already given it — the baseline the refused cast below
		// must not disturb. Captured rather than asserted to be `None`, because what the first
		// admission think selects is the kernel's business and not this channel's.
		Before.Add(Npc->Schedule.Current);
		Victims.Add(Npc);
	}

	// --- An unregistered name fails BY NAME, and starts nothing --------------------------------
	// This runs first, so "the victim is still running nothing" is a statement about this cast
	// rather than about the order of the two.
	// Occurrences 0: the stub funnel's volume is a runtime setting (`elysium.StubWarn`), so the
	// exact number of warning lines is not what this case is asserting — the tally count below is.
	AddExpectedError(GUnregisteredSchedule, EAutomationExpectedErrorFlags::Contains, 0);
	Learn(*Player, ED::Thaumaturgy, 5);
	TestEqual(TEXT("the record itself still commits nothing and is refused no targets"),
		(int32)ED::Use(*Player, ED::Thaumaturgy, 5), (int32)ED::EResult::Accepted);
	for (int32 i = 0; i < Victims.Num(); ++i)
	{
		TestEqual(TEXT("an unregistered schedule name starts no program"),
			Victims[i]->Schedule.Current, Before[i]);
	}
	{
		TArray<ElysiumStub::FTally> Tally;
		ElysiumStub::CollectTally(Tally);
		const ElysiumStub::FTally* Row = Tally.FindByPredicate(
			[](const ElysiumStub::FTally& Entry)
			{ return Entry.Surface.Contains(GUnregisteredSchedule); });
		if (TestNotNull(TEXT("...and reports through the stub funnel instead"), Row))
		{
			TestEqual(TEXT("...on the schedule surface"), Row->Kind, FString(TEXT("schedule")));
			TestTrue(TEXT("...keyed on the record that named it, not on the input surface"),
				Row->Surface.Contains(TEXT("DisciplineTgt")));
			TestEqual(TEXT("...once per target the cast reached"), Row->Count, 3);
		}
	}

	// --- A registered name runs end-to-end through the ordinary kernel ---------------------------
	Learn(*Player, ED::Dominate, 5);
	TestEqual(TEXT("the cast commits"), (int32)ED::Use(*Player, ED::Dominate, 5),
		(int32)ED::EResult::Accepted);
	for (FElysiumNpc* Victim : Victims)
	{
		TestEqual(TEXT("every committed target is running the named program"),
			Victim->Schedule.Current, EElysiumScheduleId::MeleeIdle);
		TestTrue(TEXT("...through the ordinary kernel, from its first task"),
			Victim->Schedule.IsRunning() && Victim->Schedule.TaskIndex == 0);
		TestTrue(TEXT("...and is due a think, so the program advances"),
			static_cast<double>(Victim->NextThink) <= World.NowSeconds() + 1e-3);
	}
	return true;
}

// =====================================================================================
// Cycle 11b — the overt cast's AI sound, from the shared catalogue into an NPC's hearing
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDisciplineAlertSoundTest,
	"Elysium.Substrate.Discipline.AlertSound", GElysiumTestFlags)
bool FElysiumDisciplineAlertSoundTest::RunTest(const FString&)
{
	FRulesFixture Rules;
	ElysiumRng::SeedAll(9109);
	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MakeDisciplineTestDefs());
	World.SpawnPlayer();
	World.Activate(0.0);
	World.Tick(0.0);

	FElysiumPlayer* Player = World.FindPlayer();
	if (!TestNotNull(TEXT("the player exists"), Player))
	{
		return false;
	}
	SeedCharacter(*Player, Rules.Stats);
	Player->Origin = FVector::ZeroVector;

	FElysiumCombatCharacter* Listener = FindCharacter(World, TEXT("victim0"));
	FElysiumNpc* ListenerNpc = Listener ? Listener->AsNpc() : nullptr;
	if (!TestNotNull(TEXT("a listening NPC exists"), ListenerNpc))
	{
		return false;
	}
	SeedCharacter(*Listener, Rules.Stats, /*ClanIndex*/ 0);

	const uint64 SerialBefore = World.GameSounds().LastSerial();

	// The Bloodshield record carries `TriggerAISound`. It is self-shaped, so the committed target
	// is the caster, and the stimulus is emitted THERE — "an NPC was hit by a discipline that
	// should alert others" places the sound at whoever took the hit.
	Learn(*Player, ED::Thaumaturgy, 3);
	TestEqual(TEXT("the overt cast commits"), (int32)ED::Use(*Player, ED::Thaumaturgy, 3),
		(int32)ED::EResult::Accepted);

	TArrayView<const FElysiumGameSoundEvent> Emitted = World.GameSounds().EventsSince(SerialBefore);
	if (!TestEqual(TEXT("a TriggerAISound record emits exactly one stimulus per committed target"),
		Emitted.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("...under the shared catalogue's authored category name"),
		Emitted[0].Category, ElysiumGameSounds::DisciplineAlert());
	TestEqual(TEXT("...attributed to the caster"), Emitted[0].Source.Index, Player->Handle.Index);
	TestTrue(TEXT("...with the table-resolved reach, not an invented one"),
		Emitted[0].RadiusCm > 0.f);

	// The senses' ordinary hear path then reacts for free: no discipline-specific listener exists,
	// and none should — the category is one row on the same bus every other producer writes to.
	ListenerNpc->Senses.TickHearing(*ListenerNpc, 1.0);
	TestEqual(TEXT("the NPC hear path admits it like any other stimulus"),
		ListenerNpc->Senses.Memory.LastHeardCategory,
		ElysiumGameSounds::DisciplineAlert().ToString());
	TestEqual(TEXT("...remembering the caster as its source"),
		ListenerNpc->Senses.Memory.LastHeardSource.Index, Player->Handle.Index);

	// A record without the flag emits nothing: the classification is authored, never inferred from
	// the cast having happened.
	const uint64 SerialAfterOvert = World.GameSounds().LastSerial();
	Learn(*Player, ED::Dominate, 1);
	ED::Use(*Player, ED::Dominate, 1);   // the Daze record: TriggerAISound is unset
	TestEqual(TEXT("a record without TriggerAISound emits nothing"),
		World.GameSounds().EventsSince(SerialAfterOvert).Num(), 0);
	return true;
}

// =====================================================================================
// Cycle 11b — the NPC leaf carries its own tracked targeted effects across a save
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDisciplineNpcPersistenceTest,
	"Elysium.Substrate.Discipline.NpcPersistence", GElysiumTestFlags)
bool FElysiumDisciplineNpcPersistenceTest::RunTest(const FString&)
{
	FRulesFixture Rules;
	const TCHAR* const DazeGroup = TEXT("Discipline (Test-Daze)");

	// One built world with a cast already landed on victim0, plus the payload its NPC leaf writes.
	auto CastAndFreeze = [&Rules](TArray<uint8>& OutPayload, int32 Version,
		FElysiumActiveDisciplineEffect& OutExpected) -> bool
	{
		ElysiumRng::SeedAll(9109);
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeDisciplineTestDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumPlayer* Player = World.FindPlayer();
		FElysiumCombatCharacter* Victim = FindCharacter(World, TEXT("victim0"));
		if (Player == nullptr || Victim == nullptr)
		{
			return false;
		}
		SeedCharacter(*Player, Rules.Stats);
		SeedCharacter(*Victim, Rules.Stats, /*ClanIndex*/ 0);
		Player->Origin = FVector::ZeroVector;
		Learn(*Player, ED::Dominate, 1);
		if (!ED::Accepted(ED::Use(*Player, ED::Dominate, 1)))
		{
			return false;
		}
		if (Victim->Disciplines.TargetEffects.Num() != 1)
		{
			return false;
		}
		OutExpected = Victim->Disciplines.TargetEffects[0];

		FMemoryWriter Writer(OutPayload, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Writer, Version);
		Victim->Serialize(Ar);
		return true;
	};

	// --- The round trip -------------------------------------------------------------------------
	TArray<uint8> Payload;
	FElysiumActiveDisciplineEffect Expected;
	if (!TestTrue(TEXT("a cast landed a tracked effect on the NPC"),
		CastAndFreeze(Payload, FElysiumSaveVersion::Latest, Expected)))
	{
		return false;
	}
	TestEqual(TEXT("the tracked row names the record that cast it"),
		Expected.Record, FString(GDazeRecord));
	TestTrue(TEXT("...and carries a live expiry serial"), Expected.Serial != 0);

	{
		ElysiumRng::SeedAll(9109);
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeDisciplineTestDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumCombatCharacter* Victim = FindCharacter(World, TEXT("victim0"));
		if (!TestNotNull(TEXT("the restored victim exists"), Victim))
		{
			return false;
		}
		SeedCharacter(*Victim, Rules.Stats, /*ClanIndex*/ 0);
		const int32 HealthBefore = Victim->Health;
		TestEqual(TEXT("a freshly built NPC tracks nothing"),
			Victim->Disciplines.TargetEffects.Num(), 0);
		TestFalse(TEXT("...and carries none of the cast's groups"), HasEffect(*Victim, DazeGroup));

		{
			FMemoryReader Reader(Payload, /*bIsPersistent*/ true);
			FElysiumSaveArchive Ar(Reader, FElysiumSaveVersion::Latest);
			Victim->Serialize(Ar);
		}

		if (!TestEqual(TEXT("the tracked effect survives the leaf round trip"),
			Victim->Disciplines.TargetEffects.Num(), 1))
		{
			return false;
		}
		const FElysiumActiveDisciplineEffect& R = Victim->Disciplines.TargetEffects[0];
		TestEqual(TEXT("...with its record"), R.Record, Expected.Record);
		TestEqual(TEXT("...its hit table"), R.HitTable, Expected.HitTable);
		TestEqual(TEXT("...its group names"), R.Effects.Num(), Expected.Effects.Num());
		TestTrue(TEXT("...its deadline"), NearlyEqual(R.EndTime, Expected.EndTime));
		TestEqual(TEXT("...and the expiry serial the queued event is keyed on"),
			R.Serial, Expected.Serial);
		TestEqual(TEXT("the serial counter comes back with it, so a renewal cannot reuse a serial"),
			Victim->Disciplines.SerialCounter, Expected.Serial);
		TestTrue(TEXT("the authored group is re-installed into the effect list"),
			HasEffect(*Victim, DazeGroup));
		TestEqual(TEXT("...exactly once per tracked row, never twice"),
			Victim->Effects.FilterByPredicate([DazeGroup](const FString& E)
				{ return E.Equals(DazeGroup, ESearchCase::IgnoreCase); }).Num(), 1);
		TestEqual(TEXT("...and the restored health keyfield is not re-derived over"),
			Victim->Health, HealthBefore);

		// --- The stale serial drops harmlessly ---------------------------------------------------
		// The owned expiry events ride the map snapshot's queue block, not this leaf, so a restored
		// NPC can be handed a serial that no longer matches anything — a renewal minted a newer one
		// before the save, or teardown ran. The domain's own guard drops it.
		ED::CommitExpiry(*Victim, Expected.Serial + 500);
		TestEqual(TEXT("a serial matching nothing removes nothing"),
			Victim->Disciplines.TargetEffects.Num(), 1);
		TestTrue(TEXT("...and leaves the group installed"), HasEffect(*Victim, DazeGroup));

		// The MATCHING serial is what ends it, which is what makes the restore coherent.
		ED::CommitExpiry(*Victim, R.Serial);
		TestEqual(TEXT("the restored serial is the one the expiry event answers to"),
			Victim->Disciplines.TargetEffects.Num(), 0);
		TestFalse(TEXT("...and the teardown takes the group back off"),
			HasEffect(*Victim, DazeGroup));
	}

	// --- Additive against the previous schema ----------------------------------------------------
	// The NPC leaf gates every appended block on the archive version in BOTH directions, so writing
	// at `NpcWitness` genuinely omits this one — the same shape the senses, loadout and witness
	// blocks use, and the reason this needs no hand-written legacy byte stream.
	{
		TArray<uint8> Legacy;
		FElysiumActiveDisciplineEffect Ignored;
		if (!TestTrue(TEXT("the legacy payload is written"),
			CastAndFreeze(Legacy, FElysiumSaveVersion::NpcWitness, Ignored)))
		{
			return false;
		}
		ElysiumRng::SeedAll(9109);
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeDisciplineTestDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumCombatCharacter* Victim = FindCharacter(World, TEXT("victim0"));
		if (!TestNotNull(TEXT("the legacy victim exists"), Victim))
		{
			return false;
		}
		FMemoryReader Reader(Legacy, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Reader, FElysiumSaveVersion::NpcWitness);
		Victim->Serialize(Ar);
		TestEqual(TEXT("a pre-discipline payload restores an NPC carrying no tracked effects"),
			Victim->Disciplines.TargetEffects.Num(), 0);
		TestFalse(TEXT("...and none of the cast's groups"), HasEffect(*Victim, DazeGroup));
	}
	return true;
}

// =====================================================================================
// The Content tier: the real `disciplinetgt_*` corpus, and the `Active_*` blocks beside it
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDisciplineCorpusTest,
	"Elysium.Content.Disciplines", GElysiumTestFlags)
bool FElysiumDisciplineCorpusTest::RunTest(const FString&)
{
	if (FElysiumContentPaths::Root().IsEmpty())
	{
		AddInfo(TEXT("no export root — skipping"));
		return true;
	}

	FString Error;
	FElysiumDisciplineTargets Targets;
	if (!Targets.Load(Error))
	{
		AddInfo(FString::Printf(TEXT("disciplinetgt_* did not load (%s) — skipping"), *Error));
		return true;
	}

	// The five files hold the four targeted Disciplines plus Presence's own AoE records.
	TestTrue(TEXT("the corpus parses more than the twenty main records"), Targets.Num() >= 20);

	int32 WithAoE = 0;
	int32 WithHits = 0;
	for (const FElysiumDisciplineTgt& Record : Targets.Records)
	{
		TestFalse(TEXT("every record names a Discipline"), Record.Discipline.IsEmpty());
		TestFalse(TEXT("every record names an InternalName"), Record.InternalName.IsEmpty());
		WithAoE += Record.AoE.Tables.IsEmpty() ? 0 : 1;
		WithHits += Record.Hits.IsEmpty() ? 0 : 1;

		// Every `Affects_Table` mapping has to name a hit table the record actually holds. A miss
		// is an authored defect the runtime counts rather than repairs, so this is reported as
		// information rather than failed.
		for (const FElysiumDiscAffectsTable& Table : Record.AoE.Tables)
		{
			for (const TArray<FElysiumDiscMapping>* Rows : { &Table.Mappings, &Table.DefaultMappings })
			{
				for (const FElysiumDiscMapping& Mapping : *Rows)
				{
					FElysiumDiscHit Hit;
					if (!Mapping.HitTable.IsEmpty() && !Record.ResolveHit(Mapping.HitTable, Hit))
					{
						AddInfo(FString::Printf(TEXT("`%s` maps to `%s`, which it does not define"),
							*Record.InternalName, *Mapping.HitTable));
					}
				}
			}
		}
	}
	TestTrue(TEXT("most records carry an AoE definition"), WithAoE >= Targets.Num() - 2);
	TestTrue(TEXT("most records carry hit tables"), WithHits >= Targets.Num() - 2);

	// The five main Thaumaturgy levels resolve to the powers, not to their helper records.
	for (int32 Level = 1; Level <= 5; ++Level)
	{
		const FElysiumDisciplineTgt* Record = Targets.FindFor(TEXT("Thaumaturgy"), Level);
		if (TestNotNull(TEXT("a Thaumaturgy level resolves"), Record))
		{
			TestFalse(TEXT("...to a power the player pays for, not to a zero-cost helper"),
				Record->IsHelper());
		}
	}

	// The `stats.txt` half: `Is_Instant` splits the two families, and every `Active_*` block
	// authors the durations the native path reads.
	FElysiumStatTable Stats;
	FString StatsError;
	if (!Stats.Load(StatsError))
	{
		AddInfo(FString::Printf(TEXT("stats.txt did not load (%s)"), *StatsError));
		return true;
	}
	const FElysiumStatContainer& Learned = Stats.Container(EC::Disciplines);
	const FElysiumStatContainer& Active = Stats.Container(EC::ActiveDisciplines);
	TestTrue(TEXT("the compiled learned container holds at least thirteen"), Learned.Num() >= 13);
	TestTrue(TEXT("the compiled active container holds at least thirteen"), Active.Num() >= 13);

	for (int32 Index = 0; Index < ED::Count; ++Index)
	{
		const FElysiumStat* Row = Learned.At(Index);
		if (!Row)
		{
			continue;
		}
		const bool bTargeted = Index == ED::Animalism || Index == ED::Dementation
			|| Index == ED::Dominate || Index == ED::Thaumaturgy;
		TestEqual(*FString::Printf(TEXT("`Is_Instant` classifies %s"), *Row->InternalName),
			Row->bIsInstant, !bTargeted);

		if (const FElysiumStat* ActiveRow = Active.At(Index))
		{
			TestTrue(*FString::Printf(TEXT("%s authors its Durations block"), *ActiveRow->InternalName),
				ActiveRow->Durations.bAuthored);
			TestTrue(*FString::Printf(TEXT("%s authors at least one Action"), *ActiveRow->InternalName),
				!ActiveRow->Actions.IsEmpty());
		}
	}

	// The recovered per-power numbers, read back off the real file.
	if (const FElysiumStat* Fortitude = Active.At(ED::Fortitude))
	{
		TestEqual(TEXT("Fortitude runs 25 s at every level"), Fortitude->Durations.Initial[1], 25);
		TestEqual(TEXT("...and renews by 25"), Fortitude->Durations.Add[5], 25);
	}
	if (const FElysiumStat* Presence = Active.At(ED::Presence))
	{
		TestEqual(TEXT("Presence runs 16 s"), Presence->Durations.Initial[1], 16);
	}
	if (const FElysiumStat* Auspex = Active.At(ED::Auspex))
	{
		TestEqual(TEXT("Auspex's window grows with its rank"), Auspex->Durations.Initial[1], 20);
		TestEqual(TEXT("...to 36 s at five"), Auspex->Durations.Initial[5], 36);
	}
	return true;
}

}   // namespace ElysiumDisciplineTests

#endif   // WITH_DEV_AUTOMATION_TESTS
