// Content-free Substrate automation for cycle 10c — NPC law witnessing: the four authored
// thresholds, the direct-player and global-event lanes, the three ignore-window setters at their
// real callers, the flee and attack consumers, and the schedule-selection submission that reaches
// cycle 10b's incident admission.
//
// Every number asserted here is either a fact from `docs/vtmb/npc-ai-reverse-engineering.md` §
// "Player-law observation transaction" (the four conditions 31-34, the authored-6 disable, the
// closed-window advance, the closest-player + sight gate, the cone/`m_flSeekDistInspection`/trace
// acceptance and the strongest-retained rule), a fact from `docs/vtmb/player-entity.md` § "Law,
// Masquerade and world response" (the submission order, the offender-still-the-player gate, the
// act-count copy, the flee-only scare record), a fact from `docs/vtmb/feeding.md` (the victim's
// three-second windows), or a CHOSEN constant this cycle named in `Substrate/ElysiumNpcWitness.h`
// and read back through that name rather than by literal.
//
// The engine contributes one world term — is the segment between two points clear — which the
// recording services script. Nothing loads `vdata`.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumNpcMindTypes.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "ElysiumSheetSlots.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumTestServices.h"

#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace ElysiumNpcWitnessTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	namespace EW = ElysiumNpcWitness;
	namespace EL = ElysiumLaw;
	using ECond = EElysiumNpcCond;
	using EChannel = ElysiumNpcWitness::EChannel;

	float Cm(float Units) { return Units * ElysiumMove::U; }

	// One bystander facing +X at the origin, the player in front of it, a `worldspawn` that is a
	// safe area (the only value that lets an incident through the terminal guard) and an
	// `events_world` for the law outputs to have a bus.
	struct FWitnessFixture
	{
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World;
		FElysiumNpc* Guard = nullptr;
		FElysiumNpc* Victim = nullptr;
		FElysiumPlayer* Player = nullptr;

		explicit FWitnessFixture(int32 SafeArea = 1)
			: World(nullptr, nullptr, Services.Bundle())
		{
			ElysiumRng::SeedAll(0x57495453);

			FElysiumEntityDefs Defs;
			Defs.MapName = TEXT("__npcwitness_test__");

			FElysiumEntityDef WorldSpawn;
			WorldSpawn.Classname = TEXT("worldspawn");
			WorldSpawn.Keys.Add(TEXT("safearea"), FString::FromInt(SafeArea));
			Defs.Defs.Add(MoveTemp(WorldSpawn));

			FElysiumEntityDef WorldEvents;
			WorldEvents.Classname = TEXT("events_world");
			WorldEvents.TargetName = TEXT("world");
			Defs.Defs.Add(MoveTemp(WorldEvents));

			// `vision`/`hearing` are authored so the derive path (and the rulebook it would want)
			// stays out of every case that is not about perception. 4000 Source units of sight.
			for (const TCHAR* Name : { TEXT("guard"), TEXT("victim") })
			{
				FElysiumEntityDef Npc;
				Npc.Classname = TEXT("npc_VHumanCombatant");
				Npc.TargetName = Name;
				Npc.Origin = FVector::ZeroVector;
				Npc.Keys.Add(TEXT("vision"), TEXT("4000"));
				Npc.Keys.Add(TEXT("hearing"), TEXT("1.0"));
				Defs.Defs.Add(MoveTemp(Npc));
			}

			World.Load(MoveTemp(Defs));
			World.SpawnPlayer();
			World.Activate(0.0);
			World.Tick(0.0);

			Guard = static_cast<FElysiumNpc*>(World.FindByName(TEXT("guard")));
			Victim = static_cast<FElysiumNpc*>(World.FindByName(TEXT("victim")));
			Player = World.FindPlayer();
			if (Player)
			{
				// Inside the recovered 512-unit near bypass, so the sight cache is committed with no
				// trace at all and a case that blocks the trace still has a seen player.
				Player->Origin = FVector(Cm(100.f), 0.0, 0.0);
			}
			if (Guard)
			{
				Guard->MaxHealth = 100;
				Guard->Schedule.Clear();
			}
			Quiet();
		}

		// Nothing here wants an NPC's own think competing with the pass a case is driving.
		void Quiet()
		{
			for (FElysiumNpc* Npc : { Guard, Victim })
			{
				if (Npc)
				{
					Npc->NextThink = ELYSIUM_NEVER_THINK;
				}
			}
		}

		// Commit the sight cache, then run the whole recovered decision-pass input side — which is
		// where the law lanes join.
		void Look(double Now)
		{
			if (Guard)
			{
				Guard->Senses.TickSight(*Guard, Now);
			}
		}
		void Gather(double Now)
		{
			if (Guard)
			{
				ElysiumNpcEnemy::GatherConditions(*Guard, Now);
			}
		}
		void LookAndGather(double Now)
		{
			Look(Now);
			Gather(Now);
		}

		// A player activity write also publishes a world law record (`ElysiumLaw::ApplyTimedWrite`),
		// so a case that is about the DIRECT lane drops the store afterwards and asserts one
		// producer. The global-lane cases publish explicitly instead.
		void IsolateDirectLane()
		{
			World.LawEvents().Reset();
			for (FElysiumNpc* Npc : { Guard, Victim })
			{
				if (Npc)
				{
					Npc->Witness.GlobalCursor = 0;
				}
			}
		}

		// Open a channel's observation window by hand, for a case that is about the comparison and
		// not about which of the three setters opened it.
		void OpenWindow(EChannel Channel, double Now, double Seconds = 10.0)
		{
			if (Guard)
			{
				Guard->Witness.Channel(Channel).IgnoreUntil = Now + Seconds;
			}
		}

		void Thresholds(int32 CrimFlee, int32 CrimAttack, int32 SuperFlee, int32 SuperAttack)
		{
			if (Guard)
			{
				Guard->PlCriminalFlee = CrimFlee;
				Guard->PlCriminalAttack = CrimAttack;
				Guard->PlSupernaturalFlee = SuperFlee;
				Guard->PlSupernaturalAttack = SuperAttack;
			}
		}

		bool Has(ECond Cond) const
		{
			return Guard != nullptr && Guard->Cognition.Conditions.Has(Cond);
		}
	};
}

// =====================================================================================
// The authored threshold matrix: the `0..5` clamp, the 6 disable and the negative sentinel.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcWitnessThresholdTest,
	"Elysium.Substrate.NpcWitness.Thresholds", GElysiumTestFlags)
bool FElysiumNpcWitnessThresholdTest::RunTest(const FString&)
{
	// "The player clamps activity to `0..5`; an authored threshold of 6 is therefore an effective
	// disable." Every reachable level is tested against it.
	for (int32 Level = 0; Level <= EL::MaxActivityLevel; ++Level)
	{
		TestFalse(*FString::Printf(TEXT("level %d cannot reach the authored disable"), Level),
			EW::PassesThreshold(Level, EW::ResolveThreshold(EW::DisabledThreshold)));
	}
	// The comparison is `>=`, which is what makes 5 the highest ATTAINABLE threshold and 6 the
	// disable rather than making 5 a second disable.
	TestTrue(TEXT("level 5 meets an authored 5"), EW::PassesThreshold(5, EW::ResolveThreshold(5)));
	TestFalse(TEXT("level 4 does not meet an authored 5"),
		EW::PassesThreshold(4, EW::ResolveThreshold(5)));
	TestTrue(TEXT("level 1 meets the dominant authored attack threshold 1"),
		EW::PassesThreshold(1, EW::ResolveThreshold(1)));
	TestFalse(TEXT("level 0 does not meet an authored 1"),
		EW::PassesThreshold(0, EW::ResolveThreshold(1)));

	// The negative authored value is the unset sentinel and resolves to the default, which is the
	// disable — the marked reading in `ElysiumNpcWitness::ResolveThreshold`.
	TestEqual(TEXT("an authored -1 resolves to the default"), EW::ResolveThreshold(-1),
		EW::DefaultThreshold);
	TestEqual(TEXT("the default IS the authored disable"), EW::DefaultThreshold,
		EW::DisabledThreshold);
	for (int32 Level = 0; Level <= EL::MaxActivityLevel; ++Level)
	{
		TestFalse(*FString::Printf(TEXT("an authored -1 reacts to nothing at level %d"), Level),
			EW::PassesThreshold(Level, EW::ResolveThreshold(-1)));
	}

	// And an NPC that authors nothing carries the disable rather than a zero that would fire on an
	// activity level of nothing.
	FWitnessFixture F;
	if (F.Guard == nullptr)
	{
		return false;
	}
	TestEqual(TEXT("an unauthored pl_criminal_flee defaults to the disable"),
		F.Guard->PlCriminalFlee, EW::DefaultThreshold);
	TestEqual(TEXT("an unauthored pl_supernatural_attack defaults to the disable"),
		F.Guard->PlSupernaturalAttack, EW::DefaultThreshold);
	TestEqual(TEXT("an unauthored pl_investigate defaults to the disable"),
		F.Guard->PlInvestigate, EW::DefaultThreshold);
	return true;
}

// =====================================================================================
// The keyfields reach the leaf through the ordinary class-chain walk.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcWitnessKeyfieldTest,
	"Elysium.Substrate.NpcWitness.Keyfields", GElysiumTestFlags)
bool FElysiumNpcWitnessKeyfieldTest::RunTest(const FString&)
{
	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__npcwitness_keys__");
	FElysiumEntityDef Npc;
	Npc.Classname = TEXT("npc_VHumanCombatant");
	Npc.TargetName = TEXT("cop");
	// The dominant authored cop-shaped row: flee disabled, attack at 1.
	Npc.Keys.Add(TEXT("pl_criminal_flee"), TEXT("6"));
	Npc.Keys.Add(TEXT("pl_criminal_attack"), TEXT("1"));
	Npc.Keys.Add(TEXT("pl_supernatural_flee"), TEXT("3"));
	Npc.Keys.Add(TEXT("pl_supernatural_attack"), TEXT("1"));
	Npc.Keys.Add(TEXT("pl_investigate"), TEXT("5"));
	Npc.Keys.Add(TEXT("investigate_mode"), TEXT("4"));
	Npc.Keys.Add(TEXT("investigate_mode_combat"), TEXT("4"));
	Npc.Keys.Add(TEXT("full_investigate"), TEXT("1"));
	Defs.Defs.Add(MoveTemp(Npc));

	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	FElysiumNpc* Cop = static_cast<FElysiumNpc*>(World.FindByName(TEXT("cop")));
	if (!TestNotNull(TEXT("the authored NPC exists"), Cop))
	{
		return false;
	}
	TestEqual(TEXT("pl_criminal_flee"), Cop->PlCriminalFlee, 6);
	TestEqual(TEXT("pl_criminal_attack"), Cop->PlCriminalAttack, 1);
	TestEqual(TEXT("pl_supernatural_flee"), Cop->PlSupernaturalFlee, 3);
	TestEqual(TEXT("pl_supernatural_attack"), Cop->PlSupernaturalAttack, 1);
	TestEqual(TEXT("pl_investigate"), Cop->PlInvestigate, 5);
	// Parsed and carried with their meanings unrecovered; nothing reads them.
	TestEqual(TEXT("investigate_mode is carried"), Cop->InvestigateMode, 4);
	TestEqual(TEXT("investigate_mode_combat is carried"), Cop->InvestigateModeCombat, 4);
	TestEqual(TEXT("full_investigate is carried"), Cop->FullInvestigate, 1);
	return true;
}

// =====================================================================================
// The direct lane: the sight gate, the closed-window advance, and the no-replay rule.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcWitnessDirectLaneTest,
	"Elysium.Substrate.NpcWitness.DirectLane", GElysiumTestFlags)
bool FElysiumNpcWitnessDirectLaneTest::RunTest(const FString&)
{
	// --- The lane needs the closest player AND sight ---------------------------------------------
	{
		FWitnessFixture F;
		if (F.Guard == nullptr || F.Player == nullptr)
		{
			return false;
		}
		F.Thresholds(/*CrimFlee*/ 2, /*CrimAttack*/ 6, /*SuperFlee*/ 6, /*SuperAttack*/ 6);
		F.OpenWindow(EChannel::Criminal, 0.0);
		EL::SetCriminalLevel(*F.Player, 3);
		F.IsolateDirectLane();
		TestEqual(TEXT("the write counted one act"), F.Player->CriminalActCount(), 1);

		// The player is behind the guard: out of cone, so the sight cache commits "not visible" and
		// the whole lane is refused.
		F.Player->Origin = FVector(-Cm(100.f), 0.0, 0.0);
		F.LookAndGather(0.0);
		TestFalse(TEXT("an unseen player raises no criminal flee condition"),
			F.Has(ECond::CriminalFleeLevel));
		// The recovered gate is on the lane, not on the comparison: nothing is compared, so nothing
		// is accounted for either. This is what distinguishes it from the closed-window arm below.
		TestEqual(TEXT("...and the processed count is NOT advanced"),
			F.Guard->Witness.Channel(EChannel::Criminal).Processed, 0);

		// Step back in front of it and the same act is witnessed — it was never consumed.
		F.Player->Origin = FVector(Cm(100.f), 0.0, 0.0);
		F.Guard->Senses.Memory.PlayerLosNextUpdateTime = -1.0;   // the 2 s sight cadence, not the rule
		F.LookAndGather(0.1);
		TestTrue(TEXT("the same act is witnessed once the player is seen"),
			F.Has(ECond::CriminalFleeLevel));
		TestEqual(TEXT("the retained record names the player as offender"),
			F.Guard->Witness.Channel(EChannel::Criminal).Offender.Index, F.Player->Handle.Index);
		TestEqual(TEXT("...at the level the player is carrying"),
			F.Guard->Witness.Channel(EChannel::Criminal).Level, 3);
		// Gathering does not account for the act either: the copy is schedule selection's half.
		TestEqual(TEXT("condition gathering does not advance the processed count"),
			F.Guard->Witness.Channel(EChannel::Criminal).Processed, 0);
	}

	// --- A closed window advances the count WITHOUT producing the condition ----------------------
	{
		FWitnessFixture F;
		if (F.Guard == nullptr || F.Player == nullptr)
		{
			return false;
		}
		F.Thresholds(/*CrimFlee*/ 1, 6, 6, 6);
		// No window is opened: the spawn-zero default is closed.
		TestFalse(TEXT("a spawned NPC's criminal window is closed"),
			EW::IsChannelOpen(*F.Guard, EChannel::Criminal, 0.0));
		EL::SetCriminalLevel(*F.Player, 5);
		F.IsolateDirectLane();
		F.LookAndGather(0.0);
		TestFalse(TEXT("a closed window produces no condition"), F.Has(ECond::CriminalFleeLevel));
		TestEqual(TEXT("...but the processed count advances past the act"),
			F.Guard->Witness.Channel(EChannel::Criminal).Processed, F.Player->CriminalActCount());

		// "so the same act is not replayed when observation resumes": open the window and gather
		// again against the very same act.
		F.OpenWindow(EChannel::Criminal, 1.0);
		F.LookAndGather(1.0);
		TestFalse(TEXT("the act is not replayed once the window opens"),
			F.Has(ECond::CriminalFleeLevel));

		// A NEW act is witnessed normally, which proves the NPC was not disabled by the advance.
		EL::SetCriminalLevel(*F.Player, 5);
		F.IsolateDirectLane();
		TestEqual(TEXT("the second write counted a second act"), F.Player->CriminalActCount(), 2);
		F.LookAndGather(1.1);
		TestTrue(TEXT("a new act inside an open window is witnessed"),
			F.Has(ECond::CriminalFleeLevel));
	}

	// --- Processed-count catch-up over several acts ----------------------------------------------
	{
		FWitnessFixture F;
		if (F.Guard == nullptr || F.Player == nullptr)
		{
			return false;
		}
		F.Thresholds(1, 6, 6, 6);
		for (int32 i = 0; i < 4; ++i)
		{
			EL::SetCriminalLevel(*F.Player, 2);
		}
		F.IsolateDirectLane();
		TestEqual(TEXT("four writes counted four acts"), F.Player->CriminalActCount(), 4);
		F.LookAndGather(0.0);
		TestEqual(TEXT("one closed-window pass catches the count up to all four"),
			F.Guard->Witness.Channel(EChannel::Criminal).Processed, 4);
		TestFalse(TEXT("and still produced nothing"), F.Has(ECond::CriminalFleeLevel));
	}

	// --- The two channels are independent ---------------------------------------------------------
	{
		FWitnessFixture F;
		if (F.Guard == nullptr || F.Player == nullptr)
		{
			return false;
		}
		F.Thresholds(/*CrimFlee*/ 6, /*CrimAttack*/ 6, /*SuperFlee*/ 2, /*SuperAttack*/ 6);
		F.OpenWindow(EChannel::Supernatural, 0.0);
		EL::SetCriminalLevel(*F.Player, 5);
		EL::SetSupernaturalLevel(*F.Player, 3);
		F.IsolateDirectLane();
		F.LookAndGather(0.0);
		TestTrue(TEXT("the supernatural channel fires on its own threshold"),
			F.Has(ECond::SupernaturalFleeLevel));
		TestFalse(TEXT("the criminal channel is disabled at 6 and does not"),
			F.Has(ECond::CriminalFleeLevel));
		TestEqual(TEXT("the criminal channel's closed window still advanced its own count"),
			F.Guard->Witness.Channel(EChannel::Criminal).Processed, F.Player->CriminalActCount());
		TestEqual(TEXT("...while the supernatural count is left for schedule selection"),
			F.Guard->Witness.Channel(EChannel::Supernatural).Processed, 0);
	}

	// --- The investigate condition ----------------------------------------------------------------
	{
		FWitnessFixture F;
		if (F.Guard == nullptr || F.Player == nullptr)
		{
			return false;
		}
		F.Guard->PlInvestigate = 1;
		EL::SetInvestigateLevel(*F.Player, 3);
		F.LookAndGather(0.0);
		TestTrue(TEXT("COND_INVESTIGATE_LEVEL is raised with no act count involved"),
			F.Has(ECond::InvestigateLevel));
		// It is a direct replacement with no timer: lowering the player's level clears it again.
		EL::SetInvestigateLevel(*F.Player, 0);
		F.Gather(0.1);
		TestFalse(TEXT("...and is recomputed rather than latched"), F.Has(ECond::InvestigateLevel));
	}
	return true;
}

// =====================================================================================
// The Nosferatu closest-player case, its flee-only policy, and the attack suppression.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcWitnessNosferatuTest,
	"Elysium.Substrate.NpcWitness.Nosferatu", GElysiumTestFlags)
bool FElysiumNpcWitnessNosferatuTest::RunTest(const FString&)
{
	FWitnessFixture F;
	if (F.Guard == nullptr || F.Player == nullptr)
	{
		return false;
	}
	// Severity 2 against a flee threshold of 2 and an attack threshold of 1: without the flee-only
	// policy both would pass, which is what makes the suppression assertable.
	F.Thresholds(6, 6, /*SuperFlee*/ 2, /*SuperAttack*/ 1);

	// A non-Nosferatu closest player opens nothing.
	F.Player->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Clan,
		FElysiumSheet::ClanFromName(TEXT("Brujah")));
	F.Look(0.0);
	TestFalse(TEXT("a Brujah closest player opens no Nosferatu window"),
		EW::IsWindowOpen(F.Guard->Witness.NosferatuIgnoreUntil, 0.0));

	// The real caller: `SetClosestPlayer`'s own body, which is `TickSight`.
	F.Player->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Clan,
		FElysiumSheet::ClanFromName(TEXT("Nosferatu")));
	F.Guard->Senses.Memory.PlayerLosNextUpdateTime = -1.0;
	F.Look(0.1);
	TestTrue(TEXT("a Nosferatu closest player opens the Nosferatu window"),
		EW::IsWindowOpen(F.Guard->Witness.NosferatuIgnoreUntil, 0.1));
	TestFalse(TEXT("...for five seconds and no longer"),
		EW::IsWindowOpen(F.Guard->Witness.NosferatuIgnoreUntil,
			0.1 + EW::NosferatuWindowSeconds + 0.01));

	// The record it produces needs no supernatural act at all.
	TestEqual(TEXT("the player has committed no supernatural act"),
		F.Player->SupernaturalActCount(), 0);
	F.Gather(0.1);
	TestTrue(TEXT("the severity-2 flee-only record raises the flee condition"),
		F.Has(ECond::SupernaturalFleeLevel));
	TestFalse(TEXT("flee-only suppresses the attack arm even past its threshold"),
		F.Has(ECond::SupernaturalAttackLevel));
	TestTrue(TEXT("the flee-only policy is retained on the NPC"),
		F.Guard->Witness.bSupernaturalFleeOnly);
	TestEqual(TEXT("the retained severity is the recovered 2"),
		F.Guard->Witness.Channel(EChannel::Supernatural).Level, EW::NosferatuSeverity);

	// The flee-only branch queues a player-owned scare record instead of submitting, and the copy
	// still happens so the same sighting is not re-queued every pass.
	const EElysiumScheduleId Selected = F.Guard->SelectSchedule();
	TestEqual(TEXT("the flee arm selects the run-away program"),
		static_cast<int32>(Selected), static_cast<int32>(EElysiumScheduleId::RunAway));
	TestEqual(TEXT("one scare record was queued on the player"), F.Player->ScareQueue.Num(), 1);
	if (F.Player->ScareQueue.Num() == 1)
	{
		TestEqual(TEXT("...keyed by the NPC that was scared"),
			F.Player->ScareQueue[0].Npc.Index, F.Guard->Handle.Index);
		TestEqual(TEXT("...at the retained severity"), F.Player->ScareQueue[0].Severity, 2);
	}
	TestFalse(TEXT("no supernatural incident was submitted directly"),
		F.Player->Police.MasqueradeTimerNext > 0.0);

	// A repeat keeps the greater severity and refreshes its timestamp rather than growing the queue.
	EL::QueueScareRecord(*F.Player, F.Guard->Handle, 1);
	TestEqual(TEXT("a repeat does not grow the queue"), F.Player->ScareQueue.Num(), 1);
	TestEqual(TEXT("...and keeps the greater severity"), F.Player->ScareQueue[0].Severity, 2);
	EL::QueueScareRecord(*F.Player, F.Guard->Handle, 4);
	TestEqual(TEXT("...and takes a greater one"), F.Player->ScareQueue[0].Severity, 4);

	// `PlayerRuleUpdate` selects from the queue and submits the supernatural incident. The player
	// think is deadline-driven, so the case arms it rather than assuming a cadence.
	F.Player->NextThink = 0.f;
	F.World.RunPlayerThink(0.2);
	TestEqual(TEXT("the queue is drained by the player think"), F.Player->ScareQueue.Num(), 0);
	TestTrue(TEXT("...and the submission reached the Masquerade consumer"),
		F.Player->Police.MasqueradeTimerNext > 0.0);
	return true;
}

// =====================================================================================
// The global-event lane: cone, `m_flSeekDistInspection`, trace, strongest-retained, expiry.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcWitnessGlobalLaneTest,
	"Elysium.Substrate.NpcWitness.GlobalLane", GElysiumTestFlags)
bool FElysiumNpcWitnessGlobalLaneTest::RunTest(const FString&)
{
	// The player commits nothing in this case, so every condition raised here came off a record.
	// --- Acceptance: in cone, inside the seek distance, unobstructed ------------------------------
	{
		FWitnessFixture F;
		if (F.Guard == nullptr)
		{
			return false;
		}
		F.Thresholds(/*CrimFlee*/ 2, 6, 6, 6);
		EW::PublishLawEvent(&F.World, EChannel::Criminal, 3, FVector(Cm(200.f), 0.0, 0.0),
			F.Player->Handle);
		F.Gather(0.0);
		TestTrue(TEXT("an accepted record raises the flee condition"),
			F.Has(ECond::CriminalFleeLevel));
		TestEqual(TEXT("...and is retained at its own severity"),
			F.Guard->Witness.Channel(EChannel::Criminal).Level, 3);

		// The same record is not scanned twice: an accepted record is consumed.
		F.Gather(0.1);
		TestFalse(TEXT("a consumed record does not re-raise its condition"),
			F.Has(ECond::CriminalFleeLevel));
	}

	// --- Out of the view cone ---------------------------------------------------------------------
	{
		FWitnessFixture F;
		if (F.Guard == nullptr)
		{
			return false;
		}
		F.Thresholds(2, 6, 6, 6);
		EW::PublishLawEvent(&F.World, EChannel::Criminal, 3, FVector(-Cm(200.f), 0.0, 0.0),
			F.Player->Handle);
		F.Gather(0.0);
		TestFalse(TEXT("a record behind the observer is refused"), F.Has(ECond::CriminalFleeLevel));

		// It was refused rather than consumed, so turning toward it still witnesses it.
		F.Guard->Angles = FVector(0.0, 180.0, 0.0);
		F.Gather(0.1);
		TestTrue(TEXT("a refused record is still available once the observer turns"),
			F.Has(ECond::CriminalFleeLevel));
	}

	// --- Beyond `m_flSeekDistInspection` ---------------------------------------------------------
	{
		FWitnessFixture F;
		if (F.Guard == nullptr)
		{
			return false;
		}
		F.Thresholds(2, 6, 6, 6);
		// The seek distance IS the resolved visual radius: 4000 authored units.
		TestTrue(TEXT("the guard's seek distance resolved from its authored vision"),
			F.Guard->Senses.Perception.VisionDistanceCm > Cm(3999.f));
		EW::PublishLawEvent(&F.World, EChannel::Criminal, 3, FVector(Cm(5000.f), 0.0, 0.0),
			F.Player->Handle);
		F.Gather(0.0);
		TestFalse(TEXT("a record past the seek distance is refused"),
			F.Has(ECond::CriminalFleeLevel));
	}

	// --- Obstructed ------------------------------------------------------------------------------
	{
		FWitnessFixture F;
		if (F.Guard == nullptr)
		{
			return false;
		}
		F.Thresholds(2, 6, 6, 6);
		F.Services.bLineOfSightClear = false;
		EW::PublishLawEvent(&F.World, EChannel::Criminal, 3, FVector(Cm(200.f), 0.0, 0.0),
			F.Player->Handle);
		F.Gather(0.0);
		TestFalse(TEXT("a record behind geometry is refused"), F.Has(ECond::CriminalFleeLevel));
		F.Services.bLineOfSightClear = true;
		F.Gather(0.1);
		TestTrue(TEXT("...and is witnessed once the wall is gone"), F.Has(ECond::CriminalFleeLevel));
	}

	// --- The strongest per channel is retained, independently -------------------------------------
	{
		FWitnessFixture F;
		if (F.Guard == nullptr)
		{
			return false;
		}
		F.Thresholds(/*CrimFlee*/ 2, 6, /*SuperFlee*/ 1, 6);
		EW::PublishLawEvent(&F.World, EChannel::Criminal, 2, FVector(Cm(200.f), 0.0, 0.0),
			F.Player->Handle);
		EW::PublishLawEvent(&F.World, EChannel::Criminal, 4, FVector(Cm(300.f), 0.0, 0.0),
			F.Player->Handle);
		EW::PublishLawEvent(&F.World, EChannel::Criminal, 1, FVector(Cm(400.f), 0.0, 0.0),
			F.Player->Handle);
		EW::PublishLawEvent(&F.World, EChannel::Supernatural, 1, FVector(Cm(250.f), 0.0, 0.0),
			F.Player->Handle);
		F.Gather(0.0);
		TestEqual(TEXT("the strongest criminal record is retained"),
			F.Guard->Witness.Channel(EChannel::Criminal).Level, 4);
		TestTrue(TEXT("...with its own origin"),
			F.Guard->Witness.Channel(EChannel::Criminal).Location.X > Cm(299.f)
			&& F.Guard->Witness.Channel(EChannel::Criminal).Location.X < Cm(301.f));
		TestEqual(TEXT("the supernatural channel keeps its own, independently"),
			F.Guard->Witness.Channel(EChannel::Supernatural).Level, 1);
		TestTrue(TEXT("both channels raised their flee conditions"),
			F.Has(ECond::CriminalFleeLevel) && F.Has(ECond::SupernaturalFleeLevel));
	}

	// --- Expiry -----------------------------------------------------------------------------------
	{
		FWitnessFixture F;
		if (F.Guard == nullptr)
		{
			return false;
		}
		F.Thresholds(2, 6, 6, 6);
		EW::PublishLawEvent(&F.World, EChannel::Criminal, 3, FVector(Cm(200.f), 0.0, 0.0),
			F.Player->Handle);
		TestEqual(TEXT("the store retains the record"), F.World.LawEvents().NumRetained(), 1);
		F.Gather(EW::RecordLifetimeSeconds + 0.1);
		TestFalse(TEXT("an expired record is never accepted"), F.Has(ECond::CriminalFleeLevel));
	}
	return true;
}

// =====================================================================================
// The three deadline setters at their real callers.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcWitnessDeadlineSetterTest,
	"Elysium.Substrate.NpcWitness.DeadlineSetters", GElysiumTestFlags)
bool FElysiumNpcWitnessDeadlineSetterTest::RunTest(const FString&)
{
	// --- The feed pulse, end to end on a real victim ---------------------------------------------
	{
		FWitnessFixture F;
		if (F.Guard == nullptr || F.Victim == nullptr || F.Player == nullptr)
		{
			return false;
		}
		TestFalse(TEXT("the victim's criminal window starts closed"),
			EW::IsChannelOpen(*F.Victim, EChannel::Criminal, 0.0));

		// `cower` is the recovered auto-accept disposition, so the grapple opens with no opposed roll.
		F.Victim->Disposition = TEXT("cower");
		F.Player->AttemptFeed(*F.Victim);
		if (!TestTrue(TEXT("the feed pair opened"), F.Player->IsFeedPaired()))
		{
			return false;
		}
		if (!TestTrue(TEXT("the transaction began"), F.Player->FeedBegin(*F.Victim)))
		{
			return false;
		}
		const int32 CriminalBefore = F.Player->CriminalActCount();
		// One pulse, at its own deadline. It is the recovered producer of BOTH the player's activity
		// writes and the victim's observation windows.
		TestTrue(TEXT("the pulse ran"), F.Player->Feed(10.0));
		TestEqual(TEXT("the pulse raised the player's criminal activity"),
			F.Player->CriminalActCount(), CriminalBefore + 1);
		TestTrue(TEXT("the feed opened the victim's criminal window"),
			EW::IsChannelOpen(*F.Victim, EChannel::Criminal, 10.0));
		TestTrue(TEXT("...and its supernatural window"),
			EW::IsChannelOpen(*F.Victim, EChannel::Supernatural, 10.0));
		TestFalse(TEXT("...for three seconds and no longer"),
			EW::IsChannelOpen(*F.Victim, EChannel::Criminal, 10.0 + EW::FeedWindowSeconds + 0.01));
		// The windows are the VICTIM's, not every NPC's.
		TestFalse(TEXT("a bystander's window is untouched by someone else's feed"),
			EW::IsChannelOpen(*F.Guard, EChannel::Criminal, 10.0));

		// The interrupted path opens the same victim windows.
		F.Victim->Witness.Channel(EChannel::Criminal).IgnoreUntil = 0.0;
		F.Victim->Witness.Channel(EChannel::Supernatural).IgnoreUntil = 0.0;
		// The pair's own thinks are silenced first: this case advances the clock to time the windows,
		// not to run the transaction forward.
		F.Quiet();
		F.Player->NextThink = ELYSIUM_NEVER_THINK;
		F.World.Tick(11.0);
		F.Player->FeedInterrupt();
		TestTrue(TEXT("an interrupted feed opens the victim's criminal window"),
			EW::IsChannelOpen(*F.Victim, EChannel::Criminal, 11.0));
		TestTrue(TEXT("...and the supernatural one with it"),
			EW::IsChannelOpen(*F.Victim, EChannel::Supernatural, 11.0));
	}

	// --- Entering the alert state (the CHOSEN mapping of retail state 14) -------------------------
	{
		FWitnessFixture F;
		if (F.Guard == nullptr)
		{
			return false;
		}
		TestFalse(TEXT("an idle NPC's criminal window is closed"),
			EW::IsChannelOpen(*F.Guard, EChannel::Criminal, 0.0));
		// The recovered idle -> alert promotion: the hear family, with no `no_alert_state` test.
		F.Guard->Cognition.Conditions.Reset();
		F.Guard->Cognition.Conditions.Set(ECond::HearCombat);
		F.Guard->UpdateIdealState(5.0);
		TestTrue(TEXT("entering alert opens the criminal window"),
			EW::IsChannelOpen(*F.Guard, EChannel::Criminal, 5.0));
		TestFalse(TEXT("...for two seconds and no longer"),
			EW::IsChannelOpen(*F.Guard, EChannel::Criminal, 5.0 + EW::StateChangeWindowSeconds + 0.01));
		TestFalse(TEXT("the supernatural window is NOT opened by a state change"),
			EW::IsChannelOpen(*F.Guard, EChannel::Supernatural, 5.0));

		// Staying alert is not a second edge: the window is not refreshed on every pass.
		F.Guard->UpdateIdealState(8.0);
		TestFalse(TEXT("remaining in alert does not reopen the window"),
			EW::IsChannelOpen(*F.Guard, EChannel::Criminal, 8.0));
	}
	return true;
}

// =====================================================================================
// The consumers: flee selects the retreat program, attack installs the hate row and lets
// the ordinary enemy transaction take it into combat.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcWitnessConsumerTest,
	"Elysium.Substrate.NpcWitness.Consumers", GElysiumTestFlags)
bool FElysiumNpcWitnessConsumerTest::RunTest(const FString&)
{
	// --- The flee consumer ------------------------------------------------------------------------
	{
		FWitnessFixture F;
		if (F.Guard == nullptr || F.Player == nullptr)
		{
			return false;
		}
		F.Thresholds(/*CrimFlee*/ 2, /*CrimAttack*/ 6, 6, 6);
		F.OpenWindow(EChannel::Criminal, 0.0);
		EL::SetCriminalLevel(*F.Player, 4);
		F.IsolateDirectLane();
		F.LookAndGather(0.0);
		if (!TestTrue(TEXT("the criminal flee condition is raised"),
			F.Has(ECond::CriminalFleeLevel)))
		{
			return false;
		}
		TestFalse(TEXT("the disabled attack threshold raises nothing"),
			F.Has(ECond::CriminalAttackLevel));

		const EElysiumScheduleId Selected = F.Guard->SelectSchedule();
		TestEqual(TEXT("31 routes selection into the run-away family"),
			static_cast<int32>(Selected), static_cast<int32>(EElysiumScheduleId::RunAway));
		TestTrue(TEXT("the retreat is stamped away from where the crime was witnessed"),
			FVector::Dist(F.Guard->SavePosition, F.Player->Origin) < 1.0);
		// The retreat program is registered, so selection can actually start it.
		TestNotNull(TEXT("the run-away program exists"),
			ElysiumScheduleFor(EElysiumScheduleId::RunAway));
		// An NPC that witnessed nothing takes exactly the selection it took before.
		FWitnessFixture Q;
		if (Q.Guard != nullptr)
		{
			TestEqual(TEXT("a quiet NPC still selects its disposition idle"),
				static_cast<int32>(Q.Guard->SelectSchedule()),
				static_cast<int32>(EElysiumScheduleId::IdleDisposition));
		}
	}

	// --- The attack consumer ----------------------------------------------------------------------
	{
		FWitnessFixture F;
		if (F.Guard == nullptr || F.Player == nullptr)
		{
			return false;
		}
		// The dominant authored cop row: flee disabled, attack at 1.
		F.Thresholds(/*CrimFlee*/ 6, /*CrimAttack*/ 1, 6, 6);
		F.OpenWindow(EChannel::Criminal, 0.0);
		EL::SetCriminalLevel(*F.Player, 3);
		F.IsolateDirectLane();
		F.LookAndGather(0.0);
		if (!TestTrue(TEXT("the criminal attack condition is raised"),
			F.Has(ECond::CriminalAttackLevel)))
		{
			return false;
		}
		TestEqual(TEXT("the guard is neutral toward the player before selection runs"),
			static_cast<int32>(F.Guard->Relationships.Resolve(F.Player->Handle, TEXT("player"))),
			static_cast<int32>(EElysiumRelationship::Neutral));

		const EElysiumScheduleId Selected = F.Guard->SelectSchedule();
		// The attack arm returns no program of its own: the ordinary selection below it runs.
		TestNotEqual(TEXT("the attack arm does not select the retreat"),
			static_cast<int32>(Selected), static_cast<int32>(EElysiumScheduleId::RunAway));
		TestEqual(TEXT("32 installs a D_HT row toward the player"),
			static_cast<int32>(F.Guard->Relationships.Resolve(F.Player->Handle, TEXT("player"))),
			static_cast<int32>(EElysiumRelationship::Hate));
		TestEqual(TEXT("...at IRelationPriority's own default for a live actor"),
			F.Guard->Relationships.ResolvePriority(F.Player->Handle, TEXT("player")),
			EW::AttackRelationPriority);

		// The ordinary enemy transaction does the rest, on the next pass and through its own gate.
		F.LookAndGather(0.1);
		TestTrue(TEXT("the enemy transaction commits the player as the enemy"),
			F.Guard->Senses.Memory.Enemy == F.Player->Handle);
		F.Guard->UpdateIdealState(0.1);
		// A committed enemy is what promotes the NPC to combat, and the combat branch is what
		// selects a fighting program rather than the disposition idle.
		TestNotEqual(TEXT("the promoted NPC no longer selects its disposition idle"),
			static_cast<int32>(F.Guard->SelectSchedule()),
			static_cast<int32>(EElysiumScheduleId::IdleDisposition));
	}
	return true;
}

// =====================================================================================
// The recovered split: gathering never reaches the incident consumers, and schedule
// selection is what submits and copies the act count.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcWitnessSubmissionTest,
	"Elysium.Substrate.NpcWitness.Submission", GElysiumTestFlags)
bool FElysiumNpcWitnessSubmissionTest::RunTest(const FString&)
{
	FWitnessFixture F;
	if (F.Guard == nullptr || F.Player == nullptr)
	{
		return false;
	}
	F.Thresholds(/*CrimFlee*/ 2, 6, 6, 6);
	F.OpenWindow(EChannel::Criminal, 0.0);
	EL::SetSupernaturalLevel(*F.Player, 3);
	EL::SetCriminalLevel(*F.Player, 3);
	// This case is the end-to-end one, so the world records the two writes published are left in
	// place: both lanes run, and the merge is what schedule selection submits.
	TestEqual(TEXT("each counted act published one world law record"),
		F.World.LawEvents().NumRetained(), 2);

	// --- Condition gathering is side-effect free --------------------------------------------------
	F.LookAndGather(0.0);
	TestTrue(TEXT("gathering raised the condition"), F.Has(ECond::CriminalFleeLevel));
	TestFalse(TEXT("gathering did not enter the police admission"),
		F.Player->Police.bResponsePending);
	TestTrue(TEXT("gathering did not touch the Masquerade timer"),
		F.Player->Police.MasqueradeTimerNext == 0.0);
	TestEqual(TEXT("gathering did not copy the act count"),
		F.Guard->Witness.Channel(EChannel::Criminal).Processed, 0);

	// --- Schedule selection submits ---------------------------------------------------------------
	F.Guard->SelectSchedule();
	TestTrue(TEXT("selection submitted the incident into the police admission"),
		F.Player->Police.bResponsePending);
	TestEqual(TEXT("...at the retained severity"), F.Player->Police.ResponseSeverity, 3);
	TestEqual(TEXT("...naming the witnessing NPC"), F.Player->Police.ResponseWitness.Index,
		F.Guard->Handle.Index);
	TestEqual(TEXT("selection copied the player's act count into the processed count"),
		F.Guard->Witness.Channel(EChannel::Criminal).Processed, F.Player->CriminalActCount());
	// A criminal incident never touches Masquerade.
	TestTrue(TEXT("the criminal submission left Masquerade alone"),
		F.Player->Police.MasqueradeTimerNext == 0.0);

	// The same act cannot be submitted twice: the copy is what stops the replay.
	F.Player->Police.bResponsePending = false;
	F.LookAndGather(0.1);
	TestFalse(TEXT("the witnessed act is not re-raised after the copy"),
		F.Has(ECond::CriminalFleeLevel));
	F.Guard->SelectSchedule();
	TestFalse(TEXT("...and nothing is submitted a second time"), F.Player->Police.bResponsePending);

	// --- The offender gate ------------------------------------------------------------------------
	{
		FWitnessFixture G;
		if (G.Guard == nullptr || G.Player == nullptr)
		{
			return false;
		}
		G.Thresholds(2, 6, 6, 6);
		G.OpenWindow(EChannel::Criminal, 0.0);
		EL::SetCriminalLevel(*G.Player, 3);
		G.IsolateDirectLane();
		G.LookAndGather(0.0);
		// A retained record whose offender no longer resolves to the player is dropped rather than
		// submitted against whoever occupies that slot now.
		G.Guard->Witness.Channel(EChannel::Criminal).Offender = G.Guard->Handle;
		G.Guard->SelectSchedule();
		TestFalse(TEXT("an incident whose offender is not the player is not submitted"),
			G.Player->Police.bResponsePending);
	}
	return true;
}

// =====================================================================================
// The witness block round-trips, rebases, and is additive against an older payload.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcWitnessSaveTest,
	"Elysium.Substrate.NpcWitness.Save", GElysiumTestFlags)
bool FElysiumNpcWitnessSaveTest::RunTest(const FString&)
{
	TArray<uint8> Payload;
	{
		FWitnessFixture F;
		if (F.Guard == nullptr || F.Player == nullptr)
		{
			return false;
		}
		FElysiumNpcWitnessChannel& Criminal = F.Guard->Witness.Channel(EChannel::Criminal);
		Criminal.Processed = 3;
		Criminal.Level = 4;
		Criminal.Location = FVector(Cm(200.f), Cm(10.f), 0.0);
		Criminal.Offender = F.Player->Handle;
		Criminal.IgnoreUntil = 42.5;
		FElysiumNpcWitnessChannel& Super = F.Guard->Witness.Channel(EChannel::Supernatural);
		Super.Processed = 1;
		Super.Level = 2;
		Super.Offender = F.Player->Handle;
		F.Guard->Witness.bSupernaturalFleeOnly = true;
		F.Guard->Witness.NosferatuIgnoreUntil = 17.25;

		FMemoryWriter Writer(Payload, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Writer, FElysiumSaveVersion::Latest);
		F.Guard->Serialize(Ar);
	}

	FWitnessFixture G;
	if (G.Guard == nullptr || G.Player == nullptr)
	{
		return false;
	}
	{
		FMemoryReader Reader(Payload, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Reader, FElysiumSaveVersion::Latest);
		G.Guard->Serialize(Ar);
	}
	const FElysiumNpcWitness& R = G.Guard->Witness;
	TestEqual(TEXT("the criminal processed count survives"),
		R.Channel(EChannel::Criminal).Processed, 3);
	TestEqual(TEXT("the witnessed level survives"), R.Channel(EChannel::Criminal).Level, 4);
	TestTrue(TEXT("the witnessed location survives"),
		FVector::Dist(R.Channel(EChannel::Criminal).Location, FVector(Cm(200.f), Cm(10.f), 0.0)) < 1.0);
	TestTrue(TEXT("the ignore deadline survives"),
		FMath::IsNearlyEqual(R.Channel(EChannel::Criminal).IgnoreUntil, 42.5, 0.001));
	TestTrue(TEXT("the Nosferatu deadline survives"),
		FMath::IsNearlyEqual(R.NosferatuIgnoreUntil, 17.25, 0.001));
	TestTrue(TEXT("the flee-only policy survives"), R.bSupernaturalFleeOnly);
	// The offender handle is rebased onto the restored world rather than carrying a dead epoch.
	TestTrue(TEXT("the offender handle rebases onto the live player"),
		R.Channel(EChannel::Criminal).Offender == G.Player->Handle);
	TestNotNull(TEXT("...and resolves"),
		G.World.Resolve(R.Channel(EChannel::Criminal).Offender));
	// The session cursor is stamped at the live head rather than restored from a dead serial.
	TestEqual(TEXT("the global-lane cursor restores at the store's head"),
		static_cast<int32>(R.GlobalCursor),
		static_cast<int32>(G.World.LawEvents().LastSerial()));

	// --- Additive against the previous schema -----------------------------------------------------
	// The NPC leaf gates this block on the archive version in BOTH directions, so writing at `Law`
	// genuinely omits it — the same shape the senses and loadout blocks use, and the reason this
	// does not need a hand-written legacy byte stream.
	{
		TArray<uint8> Legacy;
		{
			FWitnessFixture F;
			if (F.Guard == nullptr)
			{
				return false;
			}
			F.Guard->Witness.Channel(EChannel::Criminal).Processed = 9;
			FMemoryWriter Writer(Legacy, /*bIsPersistent*/ true);
			FElysiumSaveArchive Ar(Writer, FElysiumSaveVersion::Law);
			F.Guard->Serialize(Ar);
		}
		FWitnessFixture H;
		if (H.Guard == nullptr)
		{
			return false;
		}
		H.Guard->Witness.Channel(EChannel::Criminal).Processed = 5;
		FMemoryReader Reader(Legacy, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Reader, FElysiumSaveVersion::Law);
		H.Guard->Serialize(Ar);
		TestEqual(TEXT("a pre-witness payload restores an NPC that has witnessed nothing"),
			H.Guard->Witness.Channel(EChannel::Criminal).Processed, 0);
		TestFalse(TEXT("...with its windows at the spawn-zero default"),
			EW::IsChannelOpen(*H.Guard, EChannel::Criminal, 0.0));
	}
	return true;
}

}   // namespace ElysiumNpcWitnessTests

#endif // WITH_DEV_AUTOMATION_TESTS
