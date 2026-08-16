// Content-free Substrate automation for the player law channels (cycle 10b): the three activity
// channels and their deadlines, the two witnessed-incident consumers, the delayed police response,
// the Masquerade rate limiter, the pursuit/alert state machine, the world-area gate and its
// transition teardown, the feed producers, and `trigger_player_activity_level`.
//
// Every number asserted here is either a fact from `docs/vtmb/player-entity.md` § "Law, Masquerade
// and world response" (raise-never-lower, the refresh-and-count rule, the `-1` clear sentinel, the
// `max(previous level, pl_min_act_timer)` duration, `desired cops = max(1, severity - 1)`, the
// area-type-0 suppression), a fact from `docs/vtmb/feeding.md` (supernatural 2 + criminal 3 for two
// seconds; interrupted feed criminal 1 for two seconds), a fact from `docs/vtmb/entity_io.md` § the
// activity trigger (the `-1` unset default, exact-match release, the `0x20` bit), or a CHOSEN
// constant this cycle named in `Substrate/ElysiumLaw.h` and reads back through that name rather
// than by literal.
//
// Nothing here loads `vdata`: the rulebook is fabricated in memory and bound through
// `ElysiumSheetRules::BindTables` (K10). The player think is reached ONLY through
// `FElysiumEntityWorld::RunPlayerThink` — never `Tick` — so every cadence case drives that call.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "ElysiumSheetSlots.h"
#include "ElysiumVariant.h"
#include "Substrate/ElysiumActivityTrigger.h"
#include "Substrate/ElysiumDisciplines.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumSheetMath.h"
#include "Tests/ElysiumTestServices.h"

#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace ElysiumLawTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	using EC = EElysiumTraitContainer;
	namespace EL = ElysiumLaw;
	namespace ED = ElysiumDisciplines;

	bool NearlyEqual(double A, double B) { return FMath::Abs(A - B) < 1e-3; }

	// --- The fabricated rulebook ----------------------------------------------------------------

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

	// Four containers, sized to the compiled tables. `Masquerade` gets its real 0..5 bounds because
	// the rate-limiter case counts increments against them.
	FElysiumStatTable MakeStats()
	{
		FElysiumStatTable Table;

		FElysiumStatContainer& Attribs = Table.Containers[(uint8)EC::Attributes];
		Attribs.InternalName = TEXT("Attributes");
		for (const FElysiumSheetSlot& Slot : ElysiumSheetSlots(EC::Attributes))
		{
			Attribs.Stats.Add(MakeStat(Slot.Index, Slot.Internal, 0, 10000, 0));
		}
		Attribs.Stats[ElysiumSlot::Masquerade] =
			MakeStat(ElysiumSlot::Masquerade, TEXT("Masquerade"), 0, 5, 0);
		Attribs.Stats[ElysiumSlot::BloodPool] =
			MakeStat(ElysiumSlot::BloodPool, TEXT("BloodPool"), 0, 15, 10);
		Attribs.Stats[ElysiumSlot::MaxHealth] =
			MakeStat(ElysiumSlot::MaxHealth, TEXT("Max_Health"), 0, 1000, 100);
		Attribs.Stats[ElysiumSlot::Health] =
			MakeStat(ElysiumSlot::Health, TEXT("Health"), 0, 1000, 0);

		FElysiumStatContainer& Abilities = Table.Containers[(uint8)EC::Abilities];
		Abilities.InternalName = TEXT("Abilities");
		for (const FElysiumSheetSlot& Slot : ElysiumSheetSlots(EC::Abilities))
		{
			Abilities.Stats.Add(MakeStat(Slot.Index, Slot.Internal, 0, 5, 1));
		}

		// The learned container. `Is_Instant` is the shipped split: the four targeted Disciplines
		// are 0, the nine native ones 1.
		FElysiumStatContainer& Learned = Table.Containers[(uint8)EC::Disciplines];
		Learned.InternalName = TEXT("Disciplines");
		for (const FElysiumSheetSlot& Slot : ElysiumSheetSlots(EC::Disciplines))
		{
			FElysiumStat Stat = MakeStat(Slot.Index, Slot.Internal, -1, 5, -1);
			Stat.bIsInstant = !(Slot.Index == ED::Animalism || Slot.Index == ED::Dementation
				|| Slot.Index == ED::Dominate || Slot.Index == ED::Thaumaturgy);
			Learned.Stats.Add(MoveTemp(Stat));
		}

		FElysiumStatContainer& Active = Table.Containers[(uint8)EC::ActiveDisciplines];
		Active.InternalName = TEXT("Active_Disciplines");
		for (const FElysiumSheetSlot& Slot : ElysiumSheetSlots(EC::ActiveDisciplines))
		{
			FElysiumStat Stat = MakeStat(Slot.Index, Slot.Internal, 0, 5, 0);
			Stat.IncPredependency.Add(TEXT("BloodPool > 0"));
			Stat.Durations.bAuthored = true;
			for (int32 Level = 1; Level <= 5; ++Level)
			{
				Stat.Durations.Initial[Level] = 60;
				Stat.Durations.Add[Level] = 60;
			}
			Active.Stats.Add(MoveTemp(Stat));
		}
		return Table;
	}

	FElysiumDiscAffectsTable MakeCatchAll(const TCHAR* HitTable)
	{
		FElysiumDiscAffectsTable Table;
		FElysiumDiscMapping Mapping;
		Mapping.HitTable = HitTable;
		Table.DefaultMappings.Add(MoveTemp(Mapping));
		return Table;
	}

	const TCHAR* const GCovertRecord = TEXT("Law_Test_Covert");

	// One self-shaped targeted record: covert, `SupernaturalLvl 2`, so a committed cast is the
	// smallest real law producer this suite can drive.
	FElysiumDisciplineTargets MakeTargets()
	{
		FElysiumDisciplineTargets Table;
		FElysiumDisciplineTgt Record;
		Record.Name = TEXT("Covert");
		Record.InternalName = GCovertRecord;
		Record.Discipline = TEXT("Dominate");
		Record.Level = 1;
		Record.BloodCost = 0;
		Record.bOvert = false;
		Record.SupernaturalLvl = 2;
		Record.RecoveryTime = 0.f;
		Record.AoE.Shape = EElysiumDiscShape::Self;
		Record.AoE.Tables.Add(MakeCatchAll(TEXT("Hit_Human")));
		FElysiumDiscHit Hit;
		Hit.Name = TEXT("Hit_Human");
		Hit.Duration.Parse(TEXT("-1"));
		Hit.TraitEffects.Add(TEXT("Discipline (Law-Test)"));
		Record.Hits.Add(MoveTemp(Hit));
		Table.Add(MoveTemp(Record));
		return Table;
	}

	// The one group the covert record installs, so the hit has something to commit and the law
	// production behind it actually runs.
	FElysiumTraitEffects MakeTraitEffects()
	{
		FElysiumTraitEffects Table;
		FElysiumTraitEffectGroup Group;
		Group.InternalName = TEXT("Discipline (Law-Test)");
		Group.Category = TEXT("Discipline");
		Table.Add(MoveTemp(Group));
		return Table;
	}

	struct FRulesFixture
	{
		FElysiumStatTable Stats = MakeStats();
		FElysiumTraitEffects Effects = MakeTraitEffects();
		FElysiumFeatTable Feats;
		FElysiumClanTable Clans;
		FElysiumDisciplineTargets Targets = MakeTargets();

		FRulesFixture() { Rebind(); }
		~FRulesFixture() { ElysiumSheetRules::BindTables(ElysiumSheetRules::FBoundTables()); }
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

	// --- The world -------------------------------------------------------------------------------

	// Every `events_world` output this domain fires, wired to a distinct marker input so a pending
	// queue record identifies which edge produced it. The target does not exist: the record is
	// queued by `FireOutput` regardless, and the suite never drains the queue, so counting pending
	// records by input name is an exact count of "how many times this output fired".
	void AddLawOutputs(FElysiumEntityDef& Def)
	{
		struct FRow { const TCHAR* Output; const TCHAR* Marker; };
		static const FRow Rows[] =
		{
			{ TEXT("OnStartCopPursuitMode"),    TEXT("MarkCopPursuitStart") },
			{ TEXT("OnEndCopPursuitMode"),      TEXT("MarkCopPursuitEnd") },
			{ TEXT("OnStartCopAlertMode"),      TEXT("MarkCopAlertStart") },
			{ TEXT("OnEndCopAlertMode"),        TEXT("MarkCopAlertEnd") },
			{ TEXT("OnStartHunterPursuitMode"), TEXT("MarkHunterStart") },
			{ TEXT("OnEndHunterPursuitMode"),   TEXT("MarkHunterEnd") },
			{ TEXT("OnCopsComing"),             TEXT("MarkCopsComing") },
			{ TEXT("OnCopsOutside"),            TEXT("MarkCopsOutside") },
		};
		for (const FRow& Row : Rows)
		{
			FElysiumOutputDef Out;
			Out.Name = Row.Output;
			Out.Target = TEXT("law_sink");
			Out.Input = Row.Marker;
			Out.Delay = 0.f;
			Out.Times = -1;
			Def.Outputs.Add(MoveTemp(Out));
		}
	}

	struct FLawFixture
	{
		FRulesFixture Rules;
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World;
		FElysiumPlayer* Player = nullptr;

		// `SafeArea` seeds the authored `worldspawn` baseline; 1 (safe) is what most maps carry and
		// is the only value that lets an incident through the terminal guard.
		explicit FLawFixture(int32 SafeArea = 1, bool bCopWaitArea = false)
			: World(nullptr, nullptr, Services.Bundle())
		{
			ElysiumRng::SeedAll(0x4C41573B);

			FElysiumEntityDefs Defs;
			Defs.MapName = TEXT("__law_test__");

			FElysiumEntityDef WorldSpawn;
			WorldSpawn.Classname = TEXT("worldspawn");
			WorldSpawn.Keys.Add(TEXT("safearea"), FString::FromInt(SafeArea));
			WorldSpawn.Keys.Add(TEXT("copwaitarea"), bCopWaitArea ? TEXT("1") : TEXT("0"));
			Defs.Defs.Add(MoveTemp(WorldSpawn));

			FElysiumEntityDef WorldEvents;
			WorldEvents.Classname = TEXT("events_world");
			WorldEvents.TargetName = TEXT("world");
			AddLawOutputs(WorldEvents);
			Defs.Defs.Add(MoveTemp(WorldEvents));

			// A witness the response admission can name, and which a test can kill.
			FElysiumEntityDef Witness;
			Witness.Classname = TEXT("npc_VPedestrian");
			Witness.TargetName = TEXT("witness");
			Witness.Origin = FVector(200.f, 0.f, 0.f);
			Defs.Defs.Add(MoveTemp(Witness));

			World.Load(MoveTemp(Defs));
			World.SpawnPlayer();
			World.Activate(0.0);

			Player = World.FindPlayer();
			if (Player)
			{
				Player->Sheet.SeedFrom(Rules.Stats);
				Player->Sheet.SetBase(EC::Attributes, ElysiumSlot::Clan, 2 /*Brujah*/);
				Player->RecomputeSheet();
			}
		}

		void PlayerThink(double Now) { World.RunPlayerThink(Now); }

		FElysiumEntityHandle Witness() const
		{
			const FElysiumEntity* Ent = const_cast<FElysiumEntityWorld&>(World).FindByName(TEXT("witness"));
			return Ent ? Ent->Handle : FElysiumEntityHandle::Invalid();
		}

		// How many pending queue records carry this marker input — one per output fire.
		int32 Fired(const TCHAR* Marker) const
		{
			const FName Input(Marker);
			int32 N = 0;
			for (const FElysiumIOEvent& Event : World.Queue().Pending())
			{
				if (Event.Input == Input) { ++N; }
			}
			return N;
		}

		void SendWorldInput(const TCHAR* Input, const FElysiumVariant& Param)
		{
			World.AcceptInput(TEXT("world"), FName(Input), Param,
				World.PlayerHandle(), World.PlayerHandle());
		}
	};
}

// =====================================================================================
// The pure channel rule: raise / never-lower / refresh / count / clear / duration
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumLawChannelTest,
	"Elysium.Substrate.Law.Channels", GElysiumTestFlags)
bool FElysiumLawChannelTest::RunTest(const FString&)
{
	// --- The variant-type policy ---------------------------------------------------------------
	TestEqual(TEXT("an integer variant passes through"),
		EL::SanitizeLevel(FElysiumVariant::Int(3)), 3);
	TestEqual(TEXT("...clamped at 5"), EL::SanitizeLevel(FElysiumVariant::Int(9)), 5);
	TestEqual(TEXT("a negative reads as zero"), EL::SanitizeLevel(FElysiumVariant::Int(-1)), 0);
	TestEqual(TEXT("a bool is an integer variant"),
		EL::SanitizeLevel(FElysiumVariant::Bool(true)), 1);
	TestEqual(TEXT("an authored wire's integer string is accepted (the marshalling divergence)"),
		EL::SanitizeLevel(FElysiumVariant::String(TEXT("4"))), 4);
	TestEqual(TEXT("...and its `-1` still reads as zero"),
		EL::SanitizeLevel(FElysiumVariant::String(TEXT("-1"))), 0);
	TestEqual(TEXT("a non-numeric string is the wrong type"),
		EL::SanitizeLevel(FElysiumVariant::String(TEXT("high"))), 0);
	TestEqual(TEXT("a float is the wrong type"),
		EL::SanitizeLevel(FElysiumVariant::Float(3.f)), 0);
	TestEqual(TEXT("a vector is the wrong type"),
		EL::SanitizeLevel(FElysiumVariant::Vector(FVector(3, 3, 3))), 0);
	TestEqual(TEXT("void is the wrong type"), EL::SanitizeLevel(FElysiumVariant::Void()), 0);

	// --- The first write on a clean channel ----------------------------------------------------
	EL::FChannel Channel;
	{
		const EL::FWriteResult R = EL::WriteTimedChannel(Channel, 3, EL::DeriveDuration, 100.0);
		TestTrue(TEXT("the first non-zero write raises"), R.bRaised);
		TestTrue(TEXT("...refreshes"), R.bRefreshed);
		TestTrue(TEXT("...and counts"), R.bCounted);
		TestEqual(TEXT("the level is the requested one"), Channel.Level, 3);
		TestEqual(TEXT("the act count is one"), Channel.Count, 1);
		// The channel held 0, so `max(0, pl_min_act_timer)` is the floor.
		TestTrue(TEXT("the derived duration is pl_min_act_timer over a zero level"),
			NearlyEqual(R.Duration, EL::MinActTimerSeconds));
		TestTrue(TEXT("the deadline is now + that duration"),
			NearlyEqual(Channel.Expiry, 100.0 + EL::MinActTimerSeconds));
	}

	// --- A LOWER non-zero write: never lowers, but still refreshes and still counts -------------
	{
		const EL::FWriteResult R = EL::WriteTimedChannel(Channel, 1, EL::DeriveDuration, 110.0);
		TestFalse(TEXT("a lower request does not raise"), R.bRaised);
		TestEqual(TEXT("...and does not lower the retained level"), Channel.Level, 3);
		TestTrue(TEXT("...but it does refresh the deadline"), R.bRefreshed);
		TestEqual(TEXT("...and does increment the act count"), Channel.Count, 2);
		// The previously retained level is 3, which is above `pl_min_act_timer` (2.0), so the `max`
		// picks the level — this is the assertion that keeps the recovered `max` from being dead.
		TestTrue(TEXT("the derived duration is the previously retained LEVEL when it is larger"),
			NearlyEqual(R.Duration, 3.0));
		TestTrue(TEXT("...and the deadline moved with it"), NearlyEqual(Channel.Expiry, 113.0));
	}

	// --- A higher write raises ------------------------------------------------------------------
	{
		const EL::FWriteResult R = EL::WriteTimedChannel(Channel, 5, EL::DeriveDuration, 120.0);
		TestTrue(TEXT("a higher request raises"), R.bRaised);
		TestEqual(TEXT("...to the requested level"), Channel.Level, 5);
		TestEqual(TEXT("...counting again"), Channel.Count, 3);
		TestTrue(TEXT("the duration is derived from the level BEFORE the raise"),
			NearlyEqual(R.Duration, 3.0));
	}

	// --- An explicit positive duration wins ------------------------------------------------------
	{
		const EL::FWriteResult R = EL::WriteTimedChannel(Channel, 2, 2.0f, 130.0);
		TestTrue(TEXT("a positive explicit duration wins over the derived one"),
			NearlyEqual(R.Duration, 2.0));
		TestTrue(TEXT("...and sets the deadline"), NearlyEqual(Channel.Expiry, 132.0));
		TestEqual(TEXT("the level is still the retained maximum"), Channel.Level, 5);
	}

	// --- Zero clears -----------------------------------------------------------------------------
	{
		const EL::FWriteResult R = EL::WriteTimedChannel(Channel, 0, EL::DeriveDuration, 140.0);
		TestTrue(TEXT("an explicit zero clears"), R.bCleared);
		TestEqual(TEXT("...the level"), Channel.Level, 0);
		TestTrue(TEXT("...and installs the -1 sentinel, not a due deadline"),
			NearlyEqual(Channel.Expiry, EL::NoDeadline));
		TestFalse(TEXT("...without counting an incident"), R.bCounted);
		TestEqual(TEXT("...leaving the monotonic act count alone"), Channel.Count, 4);
	}

	// --- The expiry rule -------------------------------------------------------------------------
	{
		EL::FChannel Timed;
		EL::WriteTimedChannel(Timed, 2, 2.0f, 200.0);
		TestFalse(TEXT("a live deadline does not expire early"), EL::ExpireTimedChannel(Timed, 201.9));
		TestEqual(TEXT("...and the level stands"), Timed.Level, 2);
		TestTrue(TEXT("the deadline expires at its exact time"), EL::ExpireTimedChannel(Timed, 202.0));
		TestEqual(TEXT("...clearing the level"), Timed.Level, 0);
		TestFalse(TEXT("a second pass reports nothing — the transition fires once"),
			EL::ExpireTimedChannel(Timed, 300.0));

		EL::FChannel Cleared;
		Cleared.Expiry = EL::NoDeadline;
		TestFalse(TEXT("the -1 sentinel never expires"), EL::ExpireTimedChannel(Cleared, 1e9));
	}

	// --- Investigate: direct replacement, both directions -----------------------------------------
	TestEqual(TEXT("investigate replaces upward"), EL::WriteDirectChannel(4), 4);
	TestEqual(TEXT("...and downward"), EL::WriteDirectChannel(1), 1);
	TestEqual(TEXT("...clamped at 5"), EL::WriteDirectChannel(11), 5);
	return true;
}

// =====================================================================================
// The setters and the expiry pass, driven on the real player heartbeat
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumLawExpiryTest,
	"Elysium.Substrate.Law.Expiry", GElysiumTestFlags)
bool FElysiumLawExpiryTest::RunTest(const FString&)
{
	FLawFixture F;
	if (!TestNotNull(TEXT("the player exists"), F.Player))
	{
		return false;
	}

	// The three inputs, as the class chain delivers them.
	auto Send = [&F](const TCHAR* Input, const FElysiumVariant& Param)
	{
		F.World.AcceptInput(F.World.PlayerHandle(), FName(Input), Param,
			F.World.PlayerHandle(), F.World.PlayerHandle());
	};
	Send(TEXT("SetCriminalLevel"), FElysiumVariant::Int(3));
	Send(TEXT("SetSupernaturalLevel"), FElysiumVariant::Int(2));
	Send(TEXT("SetInvestigateLevel"), FElysiumVariant::Int(4));
	TestEqual(TEXT("the criminal input wrote its level"), F.Player->Law.Criminal, 3);
	TestEqual(TEXT("the supernatural input wrote its level"), F.Player->Law.Supernatural, 2);
	TestEqual(TEXT("the investigate input wrote its level"), F.Player->Law.Investigate, 4);
	TestEqual(TEXT("the criminal act count moved"), F.Player->CriminalActCount(), 1);
	TestEqual(TEXT("the supernatural act count moved"), F.Player->SupernaturalActCount(), 1);

	// An input with a wrong-type parameter reads as zero, which is the CLEAR arm.
	Send(TEXT("SetInvestigateLevel"), FElysiumVariant::String(TEXT("lots")));
	TestEqual(TEXT("a wrong-type investigate write clears it"), F.Player->Law.Investigate, 0);

	// Investigate carries neither a deadline nor a count, so nothing about it ages out.
	TestTrue(TEXT("investigate has no deadline of its own to expire"),
		F.Player->Law.CriminalExpiry > 0.0 && F.Player->Law.SupernaturalExpiry > 0.0);

	// The deadlines were derived from a zero starting level, so both are `pl_min_act_timer` out.
	// Every think below is driven a hair past the deadline it is about: the entity's `NextThink` is
	// a float and the deadlines are doubles, so an exactly-equal drive is a rounding coin toss
	// rather than an assertion about the rule.
	const double Deadline = static_cast<double>(EL::MinActTimerSeconds);
	F.PlayerThink(Deadline - 0.5);
	TestEqual(TEXT("neither channel expires before its deadline"),
		F.Player->Law.Criminal + F.Player->Law.Supernatural, 5);

	F.PlayerThink(Deadline + 0.05);
	TestEqual(TEXT("the criminal level expires on the heartbeat that passes its deadline"),
		F.Player->Law.Criminal, 0);
	TestEqual(TEXT("...and so does the supernatural level, in the same pass"),
		F.Player->Law.Supernatural, 0);
	TestTrue(TEXT("both deadlines fall back to the -1 sentinel"),
		NearlyEqual(F.Player->Law.CriminalExpiry, EL::NoDeadline)
		&& NearlyEqual(F.Player->Law.SupernaturalExpiry, EL::NoDeadline));
	TestEqual(TEXT("the act counts are monotonic — expiry does not decrement them"),
		F.Player->CriminalActCount() + F.Player->SupernaturalActCount(), 2);

	// A lower non-zero write refreshes the deadline without lowering the level, which is the one
	// combination that would be invisible without both a level and a clock to check.
	ElysiumLaw::SetCriminalLevel(*F.Player, 2, 5.0f);
	const double FirstExpiry = F.Player->Law.CriminalExpiry;
	F.PlayerThink(FirstExpiry - 1.0);
	TestEqual(TEXT("a level inside its window survives the heartbeat"), F.Player->Law.Criminal, 2);

	ElysiumLaw::SetCriminalLevel(*F.Player, 1, 5.0f);
	const double Refreshed = F.Player->Law.CriminalExpiry;
	TestTrue(TEXT("the lower write pushed the deadline out"), Refreshed > FirstExpiry);
	F.PlayerThink(FirstExpiry + 0.5);
	TestEqual(TEXT("...so the level outlives the deadline it originally had"),
		F.Player->Law.Criminal, 2);
	F.PlayerThink(Refreshed + 0.05);
	TestEqual(TEXT("...and expires at the refreshed one"), F.Player->Law.Criminal, 0);
	return true;
}

// =====================================================================================
// The Masquerade rate limiter, and the area-type-0 suppression end to end
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumLawMasqueradeTest,
	"Elysium.Substrate.Law.Masquerade", GElysiumTestFlags)
bool FElysiumLawMasqueradeTest::RunTest(const FString&)
{
	// --- The pure decision -----------------------------------------------------------------------
	{
		FElysiumPoliceState Police;
		Police.MasqueradeTimerNext = 0.0;
		const EL::FMasqueradeVerdict First = EL::DecideMasquerade(Police, 10.0);
		TestTrue(TEXT("an open window admits the increment"), First.bIncrement);
		TestTrue(TEXT("...and reschedules by debug_masquerade_timer"),
			NearlyEqual(First.NextDeadline, 10.0 + EL::MasqueradeTimerSeconds));
		Police.MasqueradeTimerNext = First.NextDeadline;
		TestFalse(TEXT("a second incident inside the window is refused"),
			EL::DecideMasquerade(Police, 10.0 + EL::MasqueradeTimerSeconds - 0.1).bIncrement);
		TestTrue(TEXT("...and admitted again at the deadline"),
			EL::DecideMasquerade(Police, 10.0 + EL::MasqueradeTimerSeconds).bIncrement);
	}

	// --- Through the real consumer, in a SAFE area ------------------------------------------------
	{
		FLawFixture F(/*SafeArea*/ 1);
		if (!TestNotNull(TEXT("the player exists"), F.Player)) { return false; }
		const int32 Before = F.Player->GetMasqueradeLevel();

		TestEqual(TEXT("the first witnessed supernatural incident is admitted"),
			(int32)EL::PlayerSupernaturalIncident(*F.Player, 3, F.Witness(), FVector::ZeroVector),
			(int32)EL::EAdmission::Accepted);
		TestEqual(TEXT("...and raises Masquerade by exactly one"),
			F.Player->GetMasqueradeLevel(), Before + 1);

		TestEqual(TEXT("a second incident inside the window is rate-limited"),
			(int32)EL::PlayerSupernaturalIncident(*F.Player, 3, F.Witness(), FVector::ZeroVector),
			(int32)EL::EAdmission::RefusedRateLimited);
		TestEqual(TEXT("...and does not move the counter"),
			F.Player->GetMasqueradeLevel(), Before + 1);

		// A criminal incident is the other consumer, and it never touches Masquerade.
		EL::PlayerCriminalIncident(*F.Player, 4, F.Witness(), FVector::ZeroVector);
		TestEqual(TEXT("a criminal incident does not touch Masquerade"),
			F.Player->GetMasqueradeLevel(), Before + 1);
		TestTrue(TEXT("...but it does enter the police-response admission"),
			F.Player->Police.bResponsePending);
	}

	// --- The area guard, end to end through a real Discipline cast in COMBAT area -----------------
	{
		FLawFixture F(/*SafeArea*/ 0);
		if (!TestNotNull(TEXT("the player exists"), F.Player)) { return false; }
		TestEqual(TEXT("the fabricated map is a combat area"),
			EL::WorldAreaType(F.World), (int32)EL::EArea::Combat);

		// The cast still PRODUCES activity — the guard is at incident admission, not at the producer.
		F.Player->Sheet.SetBase(EC::Disciplines, ED::Dominate, 1);
		F.Player->RecomputeSheet();
		TestEqual(TEXT("the covert cast commits"),
			(int32)ED::Use(*F.Player, ED::Dominate, 1), (int32)ED::EResult::Accepted);
		TestEqual(TEXT("...raising supernatural activity to the record's SupernaturalLvl"),
			F.Player->Law.Supernatural, 2);
		TestEqual(TEXT("...and incrementing the act count the witness lane reads"),
			F.Player->SupernaturalActCount(), 1);
		TestTrue(TEXT("...on a finite deadline the expiry pass owns"),
			F.Player->Law.SupernaturalExpiry > 0.0);

		// But the incident the witness would submit is suppressed at the terminal thunk.
		const int32 Before = F.Player->GetMasqueradeLevel();
		TestEqual(TEXT("combat area suppresses the supernatural incident"),
			(int32)EL::PlayerSupernaturalIncident(*F.Player, 2, F.Witness(), FVector::ZeroVector),
			(int32)EL::EAdmission::RefusedCombatArea);
		TestEqual(TEXT("...so Masquerade never moves"), F.Player->GetMasqueradeLevel(), Before);
		TestEqual(TEXT("combat area suppresses the criminal incident too"),
			(int32)EL::PlayerCriminalIncident(*F.Player, 4, F.Witness(), FVector::ZeroVector),
			(int32)EL::EAdmission::RefusedCombatArea);
		TestFalse(TEXT("...so no police response is queued"), F.Player->Police.bResponsePending);
	}
	return true;
}

// =====================================================================================
// The delayed police response: admission, replacement, the witness check, the delta
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumLawResponseTest,
	"Elysium.Substrate.Law.Response", GElysiumTestFlags)
bool FElysiumLawResponseTest::RunTest(const FString&)
{
	// --- The desired-cop arithmetic ---------------------------------------------------------------
	TestEqual(TEXT("severity 1 still wants one cop"), EL::DesiredCops(1), 1);
	TestEqual(TEXT("severity 2 wants one"), EL::DesiredCops(2), 1);
	TestEqual(TEXT("severity 5 wants four"), EL::DesiredCops(5), 4);
	TestEqual(TEXT("a nonsense severity still wants one"), EL::DesiredCops(0), 1);

	// --- The queue rules --------------------------------------------------------------------------
	{
		FElysiumPoliceState Police;
		const FElysiumEntityHandle W(3, 1);
		TestEqual(TEXT("the first incident is queued"),
			(int32)EL::QueueResponse(Police, 3, W, FVector::ZeroVector, 100.0, 8.0),
			(int32)EL::EQueueVerdict::Queued);
		TestTrue(TEXT("...with its random deadline"), NearlyEqual(Police.ResponseDeadline, 108.0));

		TestEqual(TEXT("an equal severity does not replace"),
			(int32)EL::QueueResponse(Police, 3, W, FVector::ZeroVector, 101.0, 4.0),
			(int32)EL::EQueueVerdict::RejectedLower);
		TestTrue(TEXT("...and does not reschedule"), NearlyEqual(Police.ResponseDeadline, 108.0));

		TestEqual(TEXT("a higher severity replaces"),
			(int32)EL::QueueResponse(Police, 5, W, FVector::ZeroVector, 102.0, 4.0),
			(int32)EL::EQueueVerdict::ReplacedHigher);
		TestEqual(TEXT("...taking the new severity"), Police.ResponseSeverity, 5);
		TestTrue(TEXT("...but explicitly NOT rescheduling"),
			NearlyEqual(Police.ResponseDeadline, 108.0));

		// Hunters refuse new admission outright.
		FElysiumPoliceState Hunted;
		Hunted.HuntersInPursuit = 1;
		TestEqual(TEXT("hunters in pursuit refuse a new response"),
			(int32)EL::QueueResponse(Hunted, 5, W, FVector::ZeroVector, 100.0, 8.0),
			(int32)EL::EQueueVerdict::RejectedHunters);
		TestFalse(TEXT("...leaving nothing pending"), Hunted.bResponsePending);
	}

	// --- Consume: timing, the witness check and the grace delta -----------------------------------
	{
		FElysiumPoliceState Police;
		EL::QueueResponse(Police, 5, FElysiumEntityHandle(3, 1), FVector::ZeroVector, 100.0, 8.0);
		TestFalse(TEXT("nothing is consumed before the deadline"),
			EL::ConsumeResponse(Police, /*bWitnessLive*/ true, 107.9).bConsumed);

		const EL::FConsumeResult First = EL::ConsumeResponse(Police, /*bWitnessLive*/ true, 108.0);
		TestTrue(TEXT("the due record is consumed"), First.bConsumed);
		TestEqual(TEXT("severity 5 resolves to four desired cops"), First.Desired, 4);
		TestEqual(TEXT("...none of them already on the street, so the delta is four"), First.Delta, 4);
		TestFalse(TEXT("nothing is left pending"), Police.bResponsePending);

		// A duplicate inside the grace window adds nothing.
		EL::QueueResponse(Police, 5, FElysiumEntityHandle(3, 1), FVector::ZeroVector, 110.0, 1.0);
		const EL::FConsumeResult Duplicate = EL::ConsumeResponse(Police, true, 111.0);
		TestTrue(TEXT("the duplicate is consumed"), Duplicate.bConsumed);
		TestEqual(TEXT("...and adds nothing inside debug_cop_grace_time"), Duplicate.Delta, 0);

		// A higher severity inside the window adds only the difference. Severity 5 already put four
		// on the street, so there is nothing above it — drop the baseline and re-run at a lower one.
		Police.GraceSpawned = 2;
		Police.GraceUntil = 200.0;
		EL::QueueResponse(Police, 5, FElysiumEntityHandle(3, 1), FVector::ZeroVector, 120.0, 1.0);
		const EL::FConsumeResult Higher = EL::ConsumeResponse(Police, true, 121.0);
		TestEqual(TEXT("a higher severity inside the window adds only the difference"),
			Higher.Delta, 2);
		TestEqual(TEXT("...and the baseline rises to the new desired count"), Police.GraceSpawned, 4);

		// A dead witness costs the response outright.
		FElysiumPoliceState Lost;
		EL::QueueResponse(Lost, 4, FElysiumEntityHandle(3, 1), FVector::ZeroVector, 100.0, 1.0);
		const EL::FConsumeResult Dead = EL::ConsumeResponse(Lost, /*bWitnessLive*/ false, 101.0);
		TestTrue(TEXT("a record with a dead witness is still consumed"), Dead.bConsumed);
		TestTrue(TEXT("...and reports the loss"), Dead.bWitnessLost);
		TestEqual(TEXT("...spawning nothing"), Dead.Delta, 0);
	}

	// --- Through the real think, up to the warned spawn seam ---------------------------------------
	{
		FLawFixture F(/*SafeArea*/ 1);
		if (!TestNotNull(TEXT("the player exists"), F.Player)) { return false; }
		EL::PlayerCriminalIncident(*F.Player, 5, F.Witness(), FVector(10.f, 0.f, 0.f));
		TestTrue(TEXT("the incident queued a response"), F.Player->Police.bResponsePending);
		const double Due = F.Player->Police.ResponseDeadline;
		TestTrue(TEXT("...on a deadline inside the recovered random band"),
			Due >= EL::ResponseTimerMinSeconds - 1e-3 && Due <= EL::ResponseTimerMaxSeconds + 1e-3);

		F.PlayerThink(Due - 1.0);
		TestTrue(TEXT("the think leaves an undue record alone"), F.Player->Police.bResponsePending);

		// The spawn itself is the one warned SEAM. It reports once per module load, so the
		// expectation is registered in ignore mode (-1): a suite that already tripped it must not
		// fail here for the warning not repeating.
		AddExpectedError(TEXT("the police-response spawn is unbuilt"),
			EAutomationExpectedErrorFlags::Contains, -1);
		F.PlayerThink(Due + 0.05);
		TestFalse(TEXT("the due record is consumed by the think"),
			F.Player->Police.bResponsePending);
		TestEqual(TEXT("...recording what it would have put on the street"),
			F.Player->Police.GraceSpawned, EL::DesiredCops(5));
	}

	// --- The cop-wait-area path fires the authored world outputs instead of spawning ---------------
	{
		FLawFixture F(/*SafeArea*/ 1, /*bCopWaitArea*/ true);
		if (!TestNotNull(TEXT("the player exists"), F.Player)) { return false; }
		EL::PlayerCriminalIncident(*F.Player, 4, F.Witness(), FVector::ZeroVector);
		F.PlayerThink(F.Player->Police.ResponseDeadline + 0.05);
		TestEqual(TEXT("a cop wait area fires OnCopsComing"), F.Fired(TEXT("MarkCopsComing")), 1);
		TestEqual(TEXT("...and OnCopsOutside"), F.Fired(TEXT("MarkCopsOutside")), 1);
	}
	return true;
}

// =====================================================================================
// Pursuit and alert: one output per edge, and the alert's own expiry
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumLawPursuitTest,
	"Elysium.Substrate.Law.Pursuit", GElysiumTestFlags)
bool FElysiumLawPursuitTest::RunTest(const FString&)
{
	// --- The pure edges ----------------------------------------------------------------------------
	{
		FElysiumPoliceState Police;
		Police.bHeightenedAlert = true;
		Police.HeightenedAlertExpiry = 500.0;
		TestEqual(TEXT("zero to one is the start edge"),
			(int32)EL::SetCopsInPursuit(Police, 1, 100.0), (int32)EL::EEdge::Start);
		TestTrue(TEXT("...and it ZEROES the alert deadline rather than clearing the byte"),
			Police.bHeightenedAlert && NearlyEqual(Police.HeightenedAlertExpiry, 0.0));
		TestEqual(TEXT("one to two is no edge"),
			(int32)EL::SetCopsInPursuit(Police, 2, 101.0), (int32)EL::EEdge::None);
		TestEqual(TEXT("two to one is no edge either"),
			(int32)EL::SetCopsInPursuit(Police, 1, 102.0), (int32)EL::EEdge::None);
		TestEqual(TEXT("one to zero is the end edge"),
			(int32)EL::SetCopsInPursuit(Police, 0, 103.0), (int32)EL::EEdge::End);
		TestTrue(TEXT("...raising heightened alert"), Police.bHeightenedAlert);
		TestTrue(TEXT("...on the debug_heightened_alert_expire_time deadline"),
			NearlyEqual(Police.HeightenedAlertExpiry, 103.0 + EL::HeightenedAlertExpireSeconds));

		TestFalse(TEXT("the alert does not expire early"),
			EL::ExpireHeightenedAlert(Police, Police.HeightenedAlertExpiry - 0.1));
		TestTrue(TEXT("...and expires at its deadline"),
			EL::ExpireHeightenedAlert(Police, Police.HeightenedAlertExpiry));
		TestFalse(TEXT("...once, not on every later pass"),
			EL::ExpireHeightenedAlert(Police, 1e9));
	}

	// --- Hunters are the same shape with their own outputs and no alert -----------------------------
	{
		FElysiumPoliceState Police;
		TestEqual(TEXT("hunter zero to one is a start edge"),
			(int32)EL::SetHuntersInPursuit(Police, 2), (int32)EL::EEdge::Start);
		TestFalse(TEXT("...with no heightened alert of its own"), Police.bHeightenedAlert);
		TestEqual(TEXT("hunter one to zero is an end edge"),
			(int32)EL::SetHuntersInPursuit(Police, 0), (int32)EL::EEdge::End);
		TestFalse(TEXT("...still with no alert"), Police.bHeightenedAlert);
	}

	// --- The authored `events_world` outputs, once per edge ------------------------------------------
	{
		FLawFixture F;
		if (!TestNotNull(TEXT("the player exists"), F.Player)) { return false; }

		EL::SetCopPursuitCount(*F.Player, 1);
		TestEqual(TEXT("the zero-to-one edge fires OnStartCopPursuitMode once"),
			F.Fired(TEXT("MarkCopPursuitStart")), 1);
		EL::SetCopPursuitCount(*F.Player, 3);
		EL::SetCopPursuitCount(*F.Player, 2);
		TestEqual(TEXT("...and not again while the count merely changes"),
			F.Fired(TEXT("MarkCopPursuitStart")), 1);

		EL::SetCopPursuitCount(*F.Player, 0);
		TestEqual(TEXT("the one-to-zero edge fires OnEndCopPursuitMode once"),
			F.Fired(TEXT("MarkCopPursuitEnd")), 1);
		TestEqual(TEXT("...and OnStartCopAlertMode in the same transaction"),
			F.Fired(TEXT("MarkCopAlertStart")), 1);
		TestEqual(TEXT("no alert END has fired yet"), F.Fired(TEXT("MarkCopAlertEnd")), 0);

		const double AlertDue = F.Player->Police.HeightenedAlertExpiry;
		F.PlayerThink(AlertDue - 1.0);
		TestEqual(TEXT("the alert does not end early"), F.Fired(TEXT("MarkCopAlertEnd")), 0);
		F.PlayerThink(AlertDue + 0.05);
		TestEqual(TEXT("the think fires OnEndCopAlertMode at its deadline"),
			F.Fired(TEXT("MarkCopAlertEnd")), 1);
		F.PlayerThink(AlertDue + 10.0);
		TestEqual(TEXT("...exactly once"), F.Fired(TEXT("MarkCopAlertEnd")), 1);

		EL::SetHunterPursuitCount(*F.Player, 1);
		TestEqual(TEXT("the hunter start edge fires its own output"),
			F.Fired(TEXT("MarkHunterStart")), 1);
		EL::SetHunterPursuitCount(*F.Player, 0);
		TestEqual(TEXT("...and so does its end edge"), F.Fired(TEXT("MarkHunterEnd")), 1);
	}
	return true;
}

// =====================================================================================
// The world area: the Discipline refusal and the two transition teardowns
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumLawWorldAreaTest,
	"Elysium.Substrate.Law.WorldArea", GElysiumTestFlags)
bool FElysiumLawWorldAreaTest::RunTest(const FString&)
{
	FLawFixture F(/*SafeArea*/ 0);
	if (!TestNotNull(TEXT("the player exists"), F.Player))
	{
		return false;
	}

	// The authored `worldspawn` baseline reaches the `events_world` leaf and the registered field.
	TestEqual(TEXT("the authored worldspawn baseline seeds the world area"),
		EL::WorldAreaType(F.World), 0);

	// Learn the three powers this case drives: two native (Celerity, Protean) and one targeted.
	F.Player->Sheet.SetBase(EC::Disciplines, ED::Celerity, 2);
	F.Player->Sheet.SetBase(EC::Disciplines, ED::Protean, 2);
	F.Player->Sheet.SetBase(EC::Disciplines, ED::Fortitude, 2);
	F.Player->Sheet.SetBase(EC::Disciplines, ED::Dominate, 1);
	F.Player->RecomputeSheet();

	TestEqual(TEXT("Celerity activates in a combat area"),
		(int32)ED::Use(*F.Player, ED::Celerity, 2), (int32)ED::EResult::Accepted);
	TestEqual(TEXT("Protean activates"),
		(int32)ED::Use(*F.Player, ED::Protean, 2), (int32)ED::EResult::Accepted);
	TestEqual(TEXT("Fortitude activates"),
		(int32)ED::Use(*F.Player, ED::Fortitude, 2), (int32)ED::EResult::Accepted);
	TestTrue(TEXT("all three are running"),
		ED::ActiveRank(*F.Player, ED::Celerity) > 0 && ED::ActiveRank(*F.Player, ED::Protean) > 0
		&& ED::ActiveRank(*F.Player, ED::Fortitude) > 0);

	// --- Safe-area entry ends ONLY Celerity and Protean -------------------------------------------
	F.SendWorldInput(TEXT("SetSafeArea"), FElysiumVariant::Int(1));
	TestEqual(TEXT("the world area is now safe"), EL::WorldAreaType(F.World), 1);
	TestEqual(TEXT("safe-area entry ends Celerity"), ED::ActiveRank(*F.Player, ED::Celerity), 0);
	TestEqual(TEXT("...and Protean"), ED::ActiveRank(*F.Player, ED::Protean), 0);
	TestTrue(TEXT("...and nothing else"), ED::ActiveRank(*F.Player, ED::Fortitude) > 0);

	// A Discipline is still admitted in a safe area.
	TestEqual(TEXT("a Discipline is admitted in a safe area"),
		(int32)ED::Use(*F.Player, ED::Dominate, 1), (int32)ED::EResult::Accepted);

	// --- Elysium entry runs the whole teardown and refuses every Discipline ------------------------
	// The Bloodbuff/LockPick exception reports its own unbuilt state on the way past.
	AddExpectedError(TEXT("the Elysium unarmed enforcement is unbuilt"),
		EAutomationExpectedErrorFlags::Contains, -1);
	F.SendWorldInput(TEXT("SetSafeArea"), FElysiumVariant::Int(2));
	TestEqual(TEXT("the world area is now Elysium"), EL::WorldAreaType(F.World), 2);
	TestEqual(TEXT("Elysium entry runs the full teardown"),
		ED::ActiveRank(*F.Player, ED::Fortitude), 0);

	TestEqual(TEXT("a Discipline is refused in Elysium"),
		(int32)ED::Use(*F.Player, ED::Dominate, 1), (int32)ED::EResult::RefusedWorldArea);
	AddExpectedError(TEXT("the Bloodbuff-while-LockPick exception is unbuilt"),
		EAutomationExpectedErrorFlags::Contains, -1);
	TestEqual(TEXT("...including Bloodbuff, whose exception is the reported seam"),
		(int32)ED::Use(*F.Player, ED::CorpusVampirus, 1), (int32)ED::EResult::RefusedWorldArea);

	// Re-entering combat has no teardown of its own and stops suppressing nothing else.
	F.SendWorldInput(TEXT("SetSafeArea"), FElysiumVariant::Int(0));
	TestEqual(TEXT("the world area is back to combat"), EL::WorldAreaType(F.World), 0);
	TestEqual(TEXT("a Discipline is admitted again"),
		(int32)ED::Use(*F.Player, ED::Dominate, 1), (int32)ED::EResult::Accepted);
	return true;
}

// =====================================================================================
// The feed producers
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumLawFeedProducerTest,
	"Elysium.Substrate.Law.FeedProducer", GElysiumTestFlags)
bool FElysiumLawFeedProducerTest::RunTest(const FString&)
{
	FLawFixture F;
	if (!TestNotNull(TEXT("the player exists"), F.Player))
	{
		return false;
	}
	FElysiumEntity* WitnessEnt = F.World.FindByName(TEXT("witness"));
	FElysiumCombatCharacter* Victim = WitnessEnt ? WitnessEnt->AsCombatCharacter() : nullptr;
	if (!TestNotNull(TEXT("the victim exists"), Victim))
	{
		return false;
	}
	Victim->Sheet.SeedFrom(F.Rules.Stats);
	Victim->Sheet.SetBase(EC::Attributes, ElysiumSlot::BloodPool, 5);
	Victim->RecomputeSheet();
	F.Player->Sheet.SetBase(EC::Attributes, ElysiumSlot::BloodPool, 0);
	F.Player->RecomputeSheet();
	// The narrow unit: this case is about what an ACCEPTED PULSE writes, not about how a pair is
	// established — the acceptance policy, the grapple and the phase machine are the feeding suite's
	// and are not re-tested here. So the pairing link is set directly and the transaction is opened
	// through the same `FeedBegin` the bite event calls, which is the smallest unit that can produce
	// one pulse deterministically.
	F.Player->FeedState.Peer = Victim->Handle;
	if (!TestTrue(TEXT("the feed transaction opens"), F.Player->FeedBegin(*Victim)))
	{
		return false;
	}
	// The pulse emits `PLAYER_AGGRESSIVE_FEED`, and no volume table is bound in this suite.
	AddExpectedError(TEXT("sound_volume_table.txt"), EAutomationExpectedErrorFlags::Contains, -1);
	F.Player->FeedState.NextPulse = 0.f;   // make the first pulse due at the fixture's clock
	if (!TestTrue(TEXT("one pulse is performed"), F.Player->Feed(0.0)))
	{
		return false;
	}

	TestEqual(TEXT("the pulse raises supernatural activity to 2"), F.Player->Law.Supernatural, 2);
	TestEqual(TEXT("...and criminal activity to 3"), F.Player->Law.Criminal, 3);
	TestTrue(TEXT("...each on an explicit two-second deadline, not a level's worth of seconds"),
		NearlyEqual(F.Player->Law.SupernaturalExpiry, EL::FeedActivitySeconds)
		&& NearlyEqual(F.Player->Law.CriminalExpiry, EL::FeedActivitySeconds));
	TestEqual(TEXT("...counting one incident on each channel"),
		F.Player->SupernaturalActCount() + F.Player->CriminalActCount(), 2);
	TestEqual(TEXT("the pulse itself does not touch Masquerade"),
		F.Player->GetMasqueradeLevel(), 0);
	TestFalse(TEXT("...and does not call the police"), F.Player->Police.bResponsePending);

	// The interrupted path raises ONLY criminal 1 — and since criminal already stands at 3, the
	// raise-never-lower rule keeps 3 while the deadline and the count still move.
	const int32 CriminalActs = F.Player->CriminalActCount();
	const int32 SupernaturalActs = F.Player->SupernaturalActCount();
	F.Player->FeedInterrupt();
	TestEqual(TEXT("the interrupted feed writes criminal, so its act count moves"),
		F.Player->CriminalActCount(), CriminalActs + 1);
	TestEqual(TEXT("...and does not write supernatural at all"),
		F.Player->SupernaturalActCount(), SupernaturalActs);
	TestEqual(TEXT("...without lowering the level the pulse had raised"), F.Player->Law.Criminal, 3);

	// The teardown is idempotent, and so is its law write: a second call must not count again.
	F.Player->FeedInterrupt();
	TestEqual(TEXT("a repeated teardown counts nothing"),
		F.Player->CriminalActCount(), CriminalActs + 1);
	return true;
}

// =====================================================================================
// `trigger_player_activity_level`
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumLawActivityTriggerTest,
	"Elysium.Substrate.Law.ActivityTrigger", GElysiumTestFlags)
bool FElysiumLawActivityTriggerTest::RunTest(const FString&)
{
	FRulesFixture Rules;
	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__activity_trigger_test__");

	FElysiumEntityDef WorldSpawn;
	WorldSpawn.Classname = TEXT("worldspawn");
	WorldSpawn.Keys.Add(TEXT("safearea"), TEXT("1"));
	Defs.Defs.Add(MoveTemp(WorldSpawn));

	// `sm_medical_1`'s `restricted_section` shape, verbatim: criminal 3, the other two channels
	// authored `-1` (unset), spawnflags 33 = ALLOW_CLIENTS | the 0x20 release bit.
	FElysiumEntityDef Restricted;
	Restricted.Classname = TEXT("trigger_player_activity_level");
	Restricted.TargetName = TEXT("restricted_section");
	Restricted.Keys.Add(TEXT("criminal_level"), TEXT("3"));
	Restricted.Keys.Add(TEXT("supernatural_level"), TEXT("-1"));
	Restricted.Keys.Add(TEXT("investigate_level"), TEXT("-1"));
	Restricted.Keys.Add(TEXT("spawnflags"), TEXT("33"));
	Defs.Defs.Add(MoveTemp(Restricted));

	// The same volume without the release bit, to prove `0x20` is what gates the timed release.
	FElysiumEntityDef NoRelease;
	NoRelease.Classname = TEXT("trigger_player_activity_level");
	NoRelease.TargetName = TEXT("no_release");
	NoRelease.Keys.Add(TEXT("criminal_level"), TEXT("2"));
	NoRelease.Keys.Add(TEXT("investigate_level"), TEXT("4"));
	NoRelease.Keys.Add(TEXT("spawnflags"), TEXT("1"));
	Defs.Defs.Add(MoveTemp(NoRelease));

	World.Load(MoveTemp(Defs));
	World.SpawnPlayer();
	World.Activate(0.0);

	FElysiumPlayer* Player = World.FindPlayer();
	if (!TestNotNull(TEXT("the player exists"), Player))
	{
		return false;
	}
	Player->Sheet.SeedFrom(Rules.Stats);
	Player->RecomputeSheet();

	FElysiumEntity* Ent = World.FindByName(TEXT("restricted_section"));
	if (!TestNotNull(TEXT("the volume resolved to the registered leaf, not a stub record"), Ent))
	{
		return false;
	}
	TestFalse(TEXT("...and it is not an inert record"), Ent->IsRecordOnly());
	FElysiumActivityTrigger* Volume = static_cast<FElysiumActivityTrigger*>(Ent);
	TestEqual(TEXT("the authored criminal level parsed"), Volume->CriminalLevel, 3);
	TestEqual(TEXT("an authored -1 stays unset"), Volume->SupernaturalLevel, -1);

	// Give the player a supernatural level from elsewhere: an unset channel must never clear it.
	ElysiumLaw::SetSupernaturalLevel(*Player, 4, 100.f);

	// --- Touch: the first assertion happens on the link, not one interval later --------------------
	World.RouteEntityTouch(Volume->Handle, World.PlayerHandle(), /*bBegin*/ true);
	TestEqual(TEXT("entering the volume asserts its criminal level"), Player->Law.Criminal, 3);
	TestEqual(TEXT("...once"), Volume->RefreshCount(), 1);
	TestEqual(TEXT("...and leaves the unset channels alone"), Player->Law.Supernatural, 4);
	TestEqual(TEXT("...including investigate"), Player->Law.Investigate, 0);
	TestTrue(TEXT("the volume is occupied"), Volume->IsOccupied());

	// --- The occupancy refresh keeps the deadline alive past its own expiry ------------------------
	const int32 ActsAfterEntry = Player->CriminalActCount();
	for (double T = 0.1; T <= 6.0; T += 0.1)
	{
		World.Tick(T);            // the volume's refresh think
		World.RunPlayerThink(T);  // the expiry pass that would otherwise age it out
	}
	TestEqual(TEXT("an occupied volume re-asserts its level past the deadline it installs"),
		Player->Law.Criminal, 3);
	TestTrue(TEXT("...through repeated refreshes"), Volume->RefreshCount() > 1);
	TestTrue(TEXT("...each of which counts an incident, exactly as the recovered setter does"),
		Player->CriminalActCount() > ActsAfterEntry);

	// --- EndTouch: exact-match release, gated on the 0x20 bit ---------------------------------------
	World.RouteEntityTouch(Volume->Handle, World.PlayerHandle(), /*bBegin*/ false);
	TestEqual(TEXT("leaving a 0x20 volume clears the level it still owns"), Player->Law.Criminal, 0);
	TestFalse(TEXT("...and disarms the refresh"), Volume->IsOccupied());
	TestEqual(TEXT("...without touching a channel it never authored"), Player->Law.Supernatural, 4);

	// A value another source replaced is NOT released.
	World.RouteEntityTouch(Volume->Handle, World.PlayerHandle(), true);
	ElysiumLaw::SetCriminalLevel(*Player, 5, 100.f);   // something stronger overwrote it
	World.RouteEntityTouch(Volume->Handle, World.PlayerHandle(), false);
	TestEqual(TEXT("exact-match release keeps a value this volume no longer owns"),
		Player->Law.Criminal, 5);

	// --- The volume without the 0x20 bit -----------------------------------------------------------
	FElysiumEntity* PlainEnt = World.FindByName(TEXT("no_release"));
	if (!TestNotNull(TEXT("the second volume exists"), PlainEnt))
	{
		return false;
	}
	ElysiumLaw::SetCriminalLevel(*Player, 0);   // clear so the volume can own the value
	World.RouteEntityTouch(PlainEnt->Handle, World.PlayerHandle(), true);
	TestEqual(TEXT("it asserts its criminal level"), Player->Law.Criminal, 2);
	TestEqual(TEXT("...and its investigate level"), Player->Law.Investigate, 4);
	World.RouteEntityTouch(PlainEnt->Handle, World.PlayerHandle(), false);
	TestEqual(TEXT("without 0x20 the timed channel is NOT released"), Player->Law.Criminal, 2);
	TestEqual(TEXT("...but investigate is released regardless of the bit"),
		Player->Law.Investigate, 0);
	return true;
}

// =====================================================================================
// The save block
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumLawSaveTest,
	"Elysium.Substrate.Law.Save", GElysiumTestFlags)
bool FElysiumLawSaveTest::RunTest(const FString&)
{
	FElysiumPlayerRecord Record;
	Record.Law.Criminal = 3;
	Record.Law.Supernatural = 2;
	Record.Law.Investigate = 1;
	Record.Law.CriminalExpiry = 412.5;
	Record.Law.SupernaturalExpiry = 419.25;
	Record.Law.CriminalCount = 7;
	Record.Law.SupernaturalCount = 11;
	Record.Police.MasqueradeTimerNext = 500.0;
	Record.Police.bResponsePending = true;
	Record.Police.ResponseSeverity = 4;
	Record.Police.ResponseWitness = FElysiumEntityHandle(9, 3);
	Record.Police.ResponsePosition = FVector(10.f, 20.f, 30.f);
	Record.Police.ResponseDeadline = 430.0;
	Record.Police.GraceUntil = 460.0;
	Record.Police.GraceSpawned = 3;
	Record.Police.CopsInPursuit = 2;
	Record.Police.HuntersInPursuit = 1;
	Record.Police.bHeightenedAlert = true;
	Record.Police.HeightenedAlertExpiry = 480.0;

	TArray<uint8> Bytes;
	{
		FMemoryWriter Writer(Bytes, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Writer, FElysiumSaveVersion::Latest);
		Ar << Record;
	}
	FElysiumPlayerRecord Back;
	{
		FMemoryReader Reader(Bytes, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Reader, FElysiumSaveVersion::Latest);
		Ar << Back;
	}
	TestEqual(TEXT("the levels round-trip"), Back.Law.Criminal, 3);
	TestTrue(TEXT("the deadlines round-trip"),
		NearlyEqual(Back.Law.CriminalExpiry, 412.5)
		&& NearlyEqual(Back.Law.SupernaturalExpiry, 419.25));
	TestEqual(TEXT("the act counts round-trip"), Back.Law.CriminalCount, 7);
	TestEqual(TEXT("...both of them"), Back.Law.SupernaturalCount, 11);
	// The archive drops a handle's epoch on the way out (re-stamping is the applier's job, per
	// `operator<<(FArchive&, FElysiumEntityHandle&)`), so only the index survives the round trip.
	TestTrue(TEXT("the pending response round-trips whole"),
		Back.Police.bResponsePending && Back.Police.ResponseSeverity == 4
		&& Back.Police.ResponseWitness.Index == 9 && Back.Police.ResponseWitness.Epoch == 0
		&& NearlyEqual(Back.Police.ResponseDeadline, 430.0));
	TestEqual(TEXT("the grace baseline round-trips"), Back.Police.GraceSpawned, 3);
	TestEqual(TEXT("the pursuit counts round-trip"), Back.Police.CopsInPursuit, 2);
	TestEqual(TEXT("...including hunters"), Back.Police.HuntersInPursuit, 1);
	TestTrue(TEXT("the heightened alert round-trips"),
		Back.Police.bHeightenedAlert && NearlyEqual(Back.Police.HeightenedAlertExpiry, 480.0));
	TestTrue(TEXT("the Masquerade window round-trips"),
		NearlyEqual(Back.Police.MasqueradeTimerNext, 500.0));

	// A pre-law payload: the levels survive, the deadlines and counts default, and the police block
	// restores clean rather than being refused.
	//
	// Built manually rather than through `Ar << Old` with a Stealth-tagged writer: every gated block
	// in this file reads `Ar.IsSaving() || Version >= X`, which is correct for a REAL save (always
	// written at `Latest`) but means a writer cannot be made to emit an older wire shape just by
	// declaring an older version — saving always writes the block regardless. Reproducing the actual
	// Stealth-version byte stream means writing exactly what that build wrote: `FElysiumLawState`'s
	// bare three levels with no deadlines/counts (bypassing its own operator, which unconditionally
	// writes them while saving), the ArmorSlot/Feed/Disciplines/Stealth blocks Stealth's version
	// already carries, and nothing for the police block, which is what version 23 first appended. The
	// read side stays the ordinary `Ar << Migrated`, whose version gates ARE meaningful on load.
	{
		TArray<uint8> Legacy;
		{
			FMemoryWriter Writer(Legacy, /*bIsPersistent*/ true);
			FElysiumSaveArchive Ar(Writer, FElysiumSaveVersion::Stealth);
			FElysiumPlayerRecord Old = Record;
			Ar << Old.Name;
			Ar << Old.Sheet;
			Ar << Old.Money;
			Ar << Old.ArmorSlot;
			Ar << Old.Health << Old.MaxHealth;
			Ar << Old.ExperienceLog << Old.Effects << Old.EmailFlags;
			Ar << Old.ExperienceRemainder << Old.LifetimeExperience;
			// The pre-Law wire shape: three bare levels, no deadlines or act counts.
			Ar << Old.Law.Criminal << Old.Law.Supernatural << Old.Law.Investigate;
			Ar << Old.bUnkillable;
			Ar << Old.Journal;
			Ar << Old.QuestLogArea;
			Ar << Old.HistoryId;
			// Feeding (version 10).
			Ar << Old.FeedMap;
			Ar << Old.Feed.NextPulse << Old.Feed.Interval << Old.Feed.StartTime;
			Ar << Old.Feed.Target;
			Ar << Old.Feed.BloodStolen;
			Ar << Old.Feed.bContinuation;
			Ar << Old.Feed.Peer;
			Ar << Old.Feed.bVictim << Old.Feed.bFrozenByFeed;
			uint8 FeedPhase = static_cast<uint8>(Old.Feed.Phase);
			Ar << FeedPhase;
			Ar << Old.Feed.PhaseDeadline;
			// Disciplines (version 21).
			Ar << Old.DisciplineMap;
			Ar << Old.SelectedDiscipline << Old.SelectedTier << Old.DisciplineCastCount;
			for (int32 i = 0; i < FElysiumDisciplineState::SlotCount; ++i)
			{
				Ar << Old.Disciplines.EndTime[i];
				Ar << Old.Disciplines.ExpirySerial[i];
				Ar << Old.Disciplines.Groups[i];
			}
			Ar << Old.Disciplines.Recovery;
			Ar << Old.Disciplines.SerialCounter;
			int32 NumEffects = Old.Disciplines.TargetEffects.Num();
			Ar << NumEffects;
			for (FElysiumActiveDisciplineEffect& Effect : Old.Disciplines.TargetEffects)
			{
				Ar << Effect.Record << Effect.HitTable << Effect.Effects;
				Ar << Effect.EndTime << Effect.Serial;
				Ar << Effect.bRemoveOnTakeDamage << Effect.bRemoveOnHearCombat << Effect.bRemoveOnWasBumped;
				Ar << Effect.Source;
			}
			// Stealth (version 22).
			Ar << Old.StealthMap;
			Ar << Old.StealthModRaw;
			Ar << Old.Stealth.NextUpdateTime;
			Ar << Old.Stealth.VisionScalar << Old.Stealth.ConeScalar << Old.Stealth.HearingReductionCm;
			Ar << Old.Stealth.NextSampleIndex;
			for (int32 i = 0; i < FElysiumStealthSurface::NumSamples; ++i)
			{
				Ar << Old.Stealth.Samples[i];
			}
			Ar << Old.Stealth.LightOnMe;
			Ar << Old.Stealth.LightRow << Old.Stealth.StealthRow;
			uint8 StealthEligible = Old.Stealth.bEligible ? 1 : 0;
			Ar << StealthEligible;
			Ar << Old.Stealth.Generation;
			// No police block — that is version 23's own addition, the thing under test.
		}
		FElysiumPlayerRecord Migrated;
		Migrated.Police.CopsInPursuit = 9;   // must be overwritten, not merged
		FMemoryReader Reader(Legacy, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Reader, FElysiumSaveVersion::Stealth);
		Ar << Migrated;
		TestEqual(TEXT("a Stealth payload keeps its bare levels"), Migrated.Law.Criminal, 3);
		TestTrue(TEXT("...with the no-deadline sentinel rather than a due deadline"),
			NearlyEqual(Migrated.Law.CriminalExpiry, EL::NoDeadline)
			&& NearlyEqual(Migrated.Law.SupernaturalExpiry, EL::NoDeadline));
		TestEqual(TEXT("...and zero act counts"),
			Migrated.Law.CriminalCount + Migrated.Law.SupernaturalCount, 0);
		TestEqual(TEXT("...and a clean street"), Migrated.Police.CopsInPursuit, 0);
		TestFalse(TEXT("...with no pending response"), Migrated.Police.bResponsePending);
	}
	return true;
}

// =====================================================================================
// Hydrate/Dehydrate: the block is unscoped, but its witness handle is not
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumLawRecordTest,
	"Elysium.Substrate.Law.Record", GElysiumTestFlags)
bool FElysiumLawRecordTest::RunTest(const FString&)
{
	FLawFixture F;
	if (!TestNotNull(TEXT("the player exists"), F.Player))
	{
		return false;
	}
	ElysiumLaw::SetCriminalLevel(*F.Player, 4, 30.f);
	EL::SetCopPursuitCount(*F.Player, 1);
	EL::PlayerCriminalIncident(*F.Player, 5, F.Witness(), FVector(1.f, 2.f, 3.f));

	FElysiumPlayerRecord Record;
	F.Player->Dehydrate(Record);
	TestEqual(TEXT("the levels reach the record"), Record.Law.Criminal, 4);
	TestEqual(TEXT("the act counts reach the record"), Record.Law.CriminalCount, 1);
	TestEqual(TEXT("the pursuit count reaches the record"), Record.Police.CopsInPursuit, 1);
	TestTrue(TEXT("the pending response reaches the record"), Record.Police.bResponsePending);
	TestTrue(TEXT("...with a map handle for its witness"), Record.Police.ResponseWitness.IsSet());

	// Hydrating into a world where the witness still exists keeps the response.
	F.Player->Hydrate(Record);
	TestTrue(TEXT("a live witness keeps the pending response"), F.Player->Police.bResponsePending);
	TestEqual(TEXT("the deadlines and counts crossed unscoped"), F.Player->Law.CriminalCount, 1);
	TestEqual(TEXT("...and so did the pursuit count"), F.Player->Police.CopsInPursuit, 1);

	// A witness that does not survive the boundary costs only the response.
	Record.Police.ResponseWitness = FElysiumEntityHandle(9999, 1);
	F.Player->Hydrate(Record);
	TestFalse(TEXT("a witness the new world cannot resolve drops the response"),
		F.Player->Police.bResponsePending);
	TestEqual(TEXT("...but the wanted level still crosses"), F.Player->Law.Criminal, 4);
	TestEqual(TEXT("...and so does the pursuit state"), F.Player->Police.CopsInPursuit, 1);
	return true;
}

}   // namespace ElysiumLawTests

#endif   // WITH_DEV_AUTOMATION_TESTS
