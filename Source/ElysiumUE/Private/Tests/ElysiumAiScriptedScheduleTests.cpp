// Content-free Substrate automation: `aiscripted_schedule` — the authored AI director.
//
// Every number and every ordering asserted here is a fact from
// `docs/vtmb/npc-ai-reverse-engineering.md` -> "`aiscripted_schedule`" and "Direct schedule
// changes", or a key spelling read off the exported `.ents` of the five maps that carry the
// entity (`sm_apartment_1`, `sm_diner_1`, `sm_hub_1`, `sm_medical_1`, `sm_warehouse_1`). Nothing
// here reads the export: the fixture states the authored record in code, which is what makes these
// the runtime's statement of the contract rather than a reading of one map.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/ScopeExit.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumNpcMindTypes.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSheetSlots.h"
#include "ElysiumStub.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumAiScriptedSchedule.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumAiScriptedScheduleTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

using ECond = EElysiumNpcCond;
using EId = EElysiumScheduleId;

namespace
{
	double Cm(double SourceUnits) { return SourceUnits * ElysiumMove::U; }

	// The one model key every fixture NPC needs: without it the leaf builds no body, and without a
	// body the recording services build no motor.
	const TCHAR* const GModel =
		TEXT("models/character/npc/common/blueblood/male/Blueblood_Male.mdl");

	// Where `goal_a` stands. Far enough that no acquisition or attack condition is in play.
	const FVector GGoalOrigin(500.0, 0.0, 0.0);

	// The world.
	// One directed NPC at the origin, one other NPC it can be pointed at, one `point_target` goal
	// (which is what four of the six named corpus goals are), the `aiscripted_schedule` itself, and
	// two patrol points for the executor hand-over cases.
	struct FAiScheduleFixture
	{
		struct FSetup
		{
			int32 Mode = 2;
			int32 ForceState = 0;
			const TCHAR* GoalName = TEXT("goal_a");
			int32 SpawnFlags = 0;
			bool bPatrol = false;
			// Leave the mind un-admitted, so a case can fire the director at an NPC that has never
			// thought — which is what `npc_maker.OnSpawnNPC` does in `sm_medical_1`.
			bool bSkipAdmission = false;
		};

		FElysiumRecordingServices Services;
		FElysiumEntityWorld World;
		FElysiumNpc* Guard = nullptr;
		FElysiumNpc* Victim = nullptr;
		FElysiumEntity* Director = nullptr;
		FElysiumEntity* Goal = nullptr;
		FElysiumPlayer* Player = nullptr;
		FElysiumEntityHandle DirectorHandle;

		// The row set every case shares: the directed guard, the victim it can be pointed at, the
		// `point_target` goal, the `aiscripted_schedule` director itself, and two patrol points.
		// `World.Load`/`SpawnPlayer`/`Activate` still happen in the constructor body below rather
		// than through `FElysiumNpcWorldFixture`, because the first `Tick` — the admission barrier —
		// is conditional here (`bSkipAdmission`), unlike every other fixture's fixed sequence.
		static FElysiumNpcWorldBuilder BuildWorld(const FSetup& Setup)
		{
			FElysiumNpcWorldBuilder Builder(TEXT("__aischedule_test__"), 0x41534348);

			FElysiumEntityDef& GuardDef = Builder.AddNpc(TEXT("guard"));
			GuardDef.Keys.Add(TEXT("model"), GModel);
			GuardDef.Keys.Add(TEXT("vision"), TEXT("4000"));
			GuardDef.Keys.Add(TEXT("hearing"), TEXT("1.0"));

			FElysiumEntityDef& VictimDef = Builder.AddNpc(TEXT("victim"), FVector(100.0, 0.0, 0.0));
			VictimDef.Keys.Add(TEXT("model"), GModel);

			// `point_target` is the corpus's own goal classname on four of the six named rows. It has
			// no registered leaf, which is exactly the point: a goal is a POSITION, not a behaviour.
			Builder.AddEntity(TEXT("point_target"), TEXT("goal_a"), GGoalOrigin);

			FElysiumEntityDef& DirectorDef = Builder.AddEntity(TEXT("aiscripted_schedule"),
				TEXT("director"), FVector(0.0, 400.0, 0.0));
			DirectorDef.Keys.Add(TEXT("m_iszEntity"), TEXT("guard"));
			DirectorDef.Keys.Add(TEXT("goalent"), Setup.GoalName);
			DirectorDef.Keys.Add(TEXT("schedule"), FString::FromInt(Setup.Mode));
			DirectorDef.Keys.Add(TEXT("forcestate"), FString::FromInt(Setup.ForceState));
			DirectorDef.Keys.Add(TEXT("m_flRadius"), TEXT("1024"));
			if (Setup.SpawnFlags != 0)
			{
				DirectorDef.Keys.Add(TEXT("spawnflags"), FString::FromInt(Setup.SpawnFlags));
			}

			for (int32 PointIndex = 1; PointIndex <= 2; ++PointIndex)
			{
				Builder.AddEntity(TEXT("info_node_patrol_point"),
					*FString::Printf(TEXT("route_%d"), PointIndex),
					FVector(0.0, static_cast<double>(PointIndex) * -200.0, 0.0));
			}
			return Builder;
		}

		explicit FAiScheduleFixture(const FSetup& Setup)
			: World(nullptr, nullptr, Services.Bundle())
		{
			Services.bProvideNpcMotor = true;

			FElysiumNpcWorldBuilder Builder = BuildWorld(Setup);
			World.Load(MoveTemp(Builder.Defs));
			World.SpawnPlayer();
			World.Activate(0.0);
			if (!Setup.bSkipAdmission)
			{
				// `Activate` arms admission and schedules the first think at t=0, so THIS tick is
				// already the admission barrier. A case that wants an un-admitted mind must skip it.
				World.Tick(0.0);
			}

			FElysiumEntity* GuardEnt = World.FindByName(TEXT("guard"));
			FElysiumEntity* VictimEnt = World.FindByName(TEXT("victim"));
			Guard = GuardEnt ? GuardEnt->AsNpc() : nullptr;
			Victim = VictimEnt ? VictimEnt->AsNpc() : nullptr;
			Director = World.FindByName(TEXT("director"));
			Goal = World.FindByName(TEXT("goal_a"));
			Player = World.FindPlayer();
			if (Director != nullptr)
			{
				DirectorHandle = Director->Handle;
			}
			if (Player != nullptr)
			{
				// Out of every reach and cone unless a case deliberately puts it back in play.
				Player->Origin = FVector(0.0, Cm(9000.0), 0.0);
			}
			for (FElysiumNpc* Npc : { Guard, Victim })
			{
				if (Npc != nullptr)
				{
					Npc->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::MaxHealth, 100);
					Npc->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Health, 0);
					Npc->RecomputeSheet();
					Npc->MaxHealth = 100;
				}
			}

			// Two ordinary thinks cross the admission barrier and settle the loadout, exactly as the
			// production path does.
			if (!Setup.bSkipAdmission)
			{
				for (int32 i = 0; i < 2; ++i)
				{
					Wake();
					World.Tick(0.0);
				}
			}
			if (Setup.bPatrol)
			{
				World.AcceptInput(TEXT("!self"), FName(TEXT("FollowPatrolPath")),
					FElysiumVariant::String(TEXT("route_1 route_2")),
					Guard ? Guard->Handle : FElysiumEntityHandle::Invalid(),
					Guard ? Guard->Handle : FElysiumEntityHandle::Invalid());
				Step(0.0);   // the patrol executor takes its token and issues the first leg
			}
			Quiet();
		}

		FAiScheduleFixture(const FAiScheduleFixture&) = delete;
		FAiScheduleFixture& operator=(const FAiScheduleFixture&) = delete;

		void Wake()
		{
			for (FElysiumNpc* Npc : { Guard, Victim })
			{
				if (Npc != nullptr)
				{
					Npc->NextThink = 0.0f;
				}
			}
		}

		void Quiet()
		{
			FElysiumNpcWorldFixture::Quiet({ Guard, Victim });
		}


		void Step(double Now)
		{
			if (Guard != nullptr)
			{
				// Unconditionally due, which is what every other NPC fixture writes. `NextThink` is a
				// float and the tick clock a double, so `float(Now)` rounds ABOVE `Now` for most
				// values and `RunThinks`' `NextThink > Now` test would skip the very think this call
				// exists to run.
				Guard->NextThink = 0.0f;
			}
			World.Tick(Now);
			Quiet();
		}

		void FireStartSchedule()
		{
			World.AcceptInput(TEXT("!self"), FName(TEXT("StartSchedule")), FElysiumVariant::Void(),
				DirectorHandle, DirectorHandle);
		}

		FElysiumRecordingNpcMotor* MotorFor(const FElysiumNpc* Npc) const
		{
			for (const TUniquePtr<FElysiumRecordingNpcMotor>& Motor : Services.NpcMotors)
			{
				if (Npc != nullptr && Motor && Motor->Owner == Npc->Handle)
				{
					return Motor.Get();
				}
			}
			return nullptr;
		}

		FString Debug(const FElysiumEntity* Entity, const TCHAR* Key) const
		{
			return FElysiumNpcWorldFixture::Debug(Entity, Key);
		}
	};
}

// The two recovered tables: `forcestate`'s asymmetry and the mode set.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAiScriptedScheduleTablesTest,
	"Elysium.Substrate.AiScriptedSchedule.Tables", GElysiumTestFlags)
bool FElysiumAiScriptedScheduleTablesTest::RunTest(const FString&)
{
	// The load-bearing asymmetry. "Treating the keyvalue as the native enum would swap combat and
	// alert", which is why authored 2 and 3 are asserted in BOTH directions here.
	EElysiumNpcState State = EElysiumNpcState::Prone;
	TestFalse(TEXT("authored 0 forces no state at all"),
		ElysiumAiScriptedSchedule::ForcedState(0, State));

	TestTrue(TEXT("authored 1 forces a state"), ElysiumAiScriptedSchedule::ForcedState(1, State));
	TestEqual(TEXT("...and it is idle"), State, EElysiumNpcState::Idle);

	TestTrue(TEXT("authored 2 forces a state"), ElysiumAiScriptedSchedule::ForcedState(2, State));
	TestEqual(TEXT("...and it is ALERT, not combat"), State, EElysiumNpcState::Alert);

	TestTrue(TEXT("authored 3 forces a state"), ElysiumAiScriptedSchedule::ForcedState(3, State));
	TestEqual(TEXT("...and it is COMBAT, not alert"), State, EElysiumNpcState::Combat);

	TestFalse(TEXT("a value outside the recovered table forces nothing"),
		ElysiumAiScriptedSchedule::ForcedState(4, State));
	TestFalse(TEXT("...and is not a known force state"),
		ElysiumAiScriptedSchedule::IsKnownForceState(4));
	TestTrue(TEXT("0..3 are the known force states"),
		ElysiumAiScriptedSchedule::IsKnownForceState(0)
		&& ElysiumAiScriptedSchedule::IsKnownForceState(3));

	// The mode table: 1..5 and nothing else.
	TestFalse(TEXT("mode 0 is not a movement mode"), ElysiumAiScriptedSchedule::IsKnownMode(0));
	for (int32 Mode = 1; Mode <= 5; ++Mode)
	{
		TestTrue(FString::Printf(TEXT("mode %d is recovered"), Mode),
			ElysiumAiScriptedSchedule::IsKnownMode(Mode));
	}
	TestFalse(TEXT("mode 6 is outside the recovered table"),
		ElysiumAiScriptedSchedule::IsKnownMode(6));

	TestTrue(TEXT("modes 1 and 2 are the move-to-goal variants"),
		ElysiumAiScriptedSchedule::IsMoveToGoal(1) && ElysiumAiScriptedSchedule::IsMoveToGoal(2));
	TestTrue(TEXT("modes 4 and 5 are the follow-path variants"),
		ElysiumAiScriptedSchedule::IsFollowPath(4) && ElysiumAiScriptedSchedule::IsFollowPath(5));
	TestFalse(TEXT("mode 3 runs no program at all"),
		ElysiumAiScriptedSchedule::IsMoveToGoal(3) || ElysiumAiScriptedSchedule::IsFollowPath(3));

	TestEqual(TEXT("the move variants share one registered program"),
		ElysiumAiScriptedSchedule::ProgramFor(1), EId::ScriptedMoveToGoal);
	TestEqual(TEXT("...both of them"),
		ElysiumAiScriptedSchedule::ProgramFor(2), EId::ScriptedMoveToGoal);
	TestEqual(TEXT("the follow variants share the other"),
		ElysiumAiScriptedSchedule::ProgramFor(5), EId::ScriptedFollowPath);
	TestEqual(TEXT("mode 3 names no program"),
		ElysiumAiScriptedSchedule::ProgramFor(3), EId::None);

	// CHOSEN, NOT RECOVERED — asserted so the choice is visible in one place and a decoded
	// discriminator changes exactly one function and this block.
	TestFalse(TEXT("CHOSEN: mode 1 walks"), ElysiumAiScriptedSchedule::IsRunVariant(1));
	TestTrue(TEXT("CHOSEN: mode 2 runs"), ElysiumAiScriptedSchedule::IsRunVariant(2));
	TestFalse(TEXT("CHOSEN: mode 4 walks"), ElysiumAiScriptedSchedule::IsRunVariant(4));
	TestTrue(TEXT("CHOSEN: mode 5 runs"), ElysiumAiScriptedSchedule::IsRunVariant(5));

	// Both programs are registered into the SHARED kernel registry, and both are reachable by name.
	for (const EId Program : { EId::ScriptedMoveToGoal, EId::ScriptedFollowPath })
	{
		TestNotNull(FString::Printf(TEXT("%s is registered"), ElysiumScheduleName(Program)),
			ElysiumScheduleFor(Program));
		TestTrue(TEXT("...and is recognised as a scripted program"),
			ElysiumAiScriptedSchedule::IsScriptedProgram(Program));
		EId ByName = EId::None;
		TestTrue(TEXT("...and resolves from its own name"),
			ElysiumScheduleIdFromName(ElysiumScheduleName(Program), ByName));
		TestEqual(TEXT("...to itself"), ByName, Program);
	}
	TestFalse(TEXT("an ordinary combat program is not a scripted one"),
		ElysiumAiScriptedSchedule::IsScriptedProgram(EId::ChaseEnemy));

	// The scripted programs declare NO interrupts, and that is the family's decision rather than an
	// unfilled default: an authored director is not re-decided by ordinary stimulus.
	for (const EId Program : { EId::ScriptedMoveToGoal, EId::ScriptedFollowPath })
	{
		if (const FElysiumSchedule* Schedule = ElysiumScheduleFor(Program))
		{
			TestTrue(TEXT("a scripted director's program admits no interrupts"),
				Schedule->Interrupts.IsEmpty());
		}
	}
	return true;
}

// Mode 1/2 — the move to the goal, over the recording motor.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAiScriptedScheduleMoveTest,
	"Elysium.Substrate.AiScriptedSchedule.MoveToGoal", GElysiumTestFlags)
bool FElysiumAiScriptedScheduleMoveTest::RunTest(const FString&)
{
	// Mode 2 with `forcestate 2`: the three warehouse thug rows, in miniature.
	{
		FAiScheduleFixture::FSetup Setup;
		Setup.Mode = 2;
		Setup.ForceState = 2;
		FAiScheduleFixture F(Setup);
		if (!TestNotNull(TEXT("the directed NPC constructs"), F.Guard)
			|| !TestNotNull(TEXT("the director constructs"), F.Director))
		{
			return false;
		}
		TestEqual(TEXT("the authored mode parses under the corpus's own key"),
			F.Debug(F.Director, TEXT("Mode")), FString(TEXT("2 (move-to-goal B) run")));
		TestTrue(TEXT("the authored force state parses through the recovered mapping"),
			F.Debug(F.Director, TEXT("Force state")).Contains(TEXT("2 -> Alert")));
		TestTrue(TEXT("nothing is pushed before the wire fires"),
			!F.Guard->ScriptedScheduleOrder.IsSet());

		F.FireStartSchedule();

		TestTrue(TEXT("the push takes the body under the ScriptedSchedule owner"),
			F.Guard->GetMind().Owner() == EElysiumBodyOwner::ScriptedSchedule);
		TestTrue(TEXT("the pushed order is visible in one row"),
			F.Guard->ScriptedScheduleOrder.IsSet() && F.Guard->ScriptedScheduleOrder.Mode == 2);
		// The state push is a STATE, not a hold: the arbiter must not overwrite it with `Scripted`.
		TestTrue(TEXT("forcestate 2 put the mind in ALERT, not in a scripted hold"),
			F.Guard->GetMind().State() == EElysiumNpcState::Alert);

		F.Step(0.1);
		FElysiumRecordingNpcMotor* Motor = F.MotorFor(F.Guard);
		if (!TestNotNull(TEXT("the directed NPC owns a motor"), Motor))
		{
			return false;
		}
		TestTrue(TEXT("the program issued the move"), Motor->bMoving);
		TestTrue(TEXT("...to the goal entity's own origin"),
			Motor->RequestedFeet.Equals(GGoalOrigin));
		TestEqual(TEXT("...at the running gait mode 2 chose"),
			Motor->RequestedSpeedCmPerSecond, ElysiumNpcGait::RunSpeed);

		// Arrival ends the program, which is what releases the claim.
		Motor->SampleStatus = EElysiumNpcMoveStatus::Reached;
		F.Step(0.2);
		F.Step(0.3);
		TestTrue(TEXT("the order is dropped once its program ends"),
			!F.Guard->ScriptedScheduleOrder.IsSet());
		TestTrue(TEXT("...and the body goes back"),
			F.Guard->GetMind().Owner() == EElysiumBodyOwner::None);
		// `forcestate` was a state push and not a temporary hold, so it OUTLIVES the program.
		TestTrue(TEXT("the pushed state survives the program that came with it"),
			F.Guard->GetMind().State() == EElysiumNpcState::Alert);
	}

	// Mode 1 walks, which is the CHOSEN half of the pair.
	{
		FAiScheduleFixture::FSetup Setup;
		Setup.Mode = 1;
		FAiScheduleFixture F(Setup);
		if (F.Guard == nullptr)
		{
			return false;
		}
		F.FireStartSchedule();
		F.Step(0.1);
		if (FElysiumRecordingNpcMotor* Motor = F.MotorFor(F.Guard))
		{
			TestEqual(TEXT("mode 1 takes the walking gait"),
				Motor->RequestedSpeedCmPerSecond, ElysiumNpcGait::WalkSpeed);
		}
		TestTrue(TEXT("mode 1 with forcestate 0 pushes no state"),
			F.Guard->GetMind().State() == EElysiumNpcState::Idle);
	}

	// Mode 4/5: the follow-path family runs its own program.
	{
		FAiScheduleFixture::FSetup Setup;
		Setup.Mode = 4;
		FAiScheduleFixture F(Setup);
		if (F.Guard == nullptr)
		{
			return false;
		}
		F.FireStartSchedule();
		TestEqual(TEXT("a follow-path mode starts the follow-path program"),
			F.Guard->Schedule.Current, EId::ScriptedFollowPath);
		F.Step(0.1);
		if (FElysiumRecordingNpcMotor* Motor = F.MotorFor(F.Guard))
		{
			TestTrue(TEXT("a one-node route walks straight to the goal"),
				Motor->RequestedFeet.Equals(GGoalOrigin));
			// The exhausted-route leg ends the program rather than looping forever.
			Motor->SampleStatus = EElysiumNpcMoveStatus::Reached;
		}
		F.Step(0.2);
		F.Step(0.3);
		TestTrue(TEXT("an exhausted route ends the follow-path program"),
			!F.Guard->ScriptedScheduleOrder.IsSet());
	}
	return true;
}

// Mode 3 — the goal becomes the enemy, through the ordinary acquisition transaction.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAiScriptedScheduleAssignEnemyTest,
	"Elysium.Substrate.AiScriptedSchedule.AssignEnemy", GElysiumTestFlags)
bool FElysiumAiScriptedScheduleAssignEnemyTest::RunTest(const FString&)
{
	// The four diner assassins: mode 3, forcestate 3, goal `!player`.
	FAiScheduleFixture::FSetup Setup;
	Setup.Mode = 3;
	Setup.ForceState = 3;
	Setup.GoalName = TEXT("!player");
	FAiScheduleFixture F(Setup);
	if (F.Guard == nullptr || F.Victim == nullptr || F.Player == nullptr)
	{
		return false;
	}

	// A previous acquisition episode, so the last-enemy transfer and the latch reset are observable
	// rather than vacuous.
	F.Guard->Senses.Memory.Enemy = F.Victim->Handle;
	F.Guard->Senses.Memory.bEnemyLosLatched = true;
	F.Guard->Senses.Memory.EnemyLosFailures = 7;
	F.Guard->Senses.Memory.bEnemyOccluded = true;
	F.Guard->Cognition.Conditions.Reset();

	F.FireStartSchedule();

	TestTrue(TEXT("the goal is committed as the enemy"),
		F.Guard->Senses.Memory.Enemy == F.Player->Handle);
	TestTrue(TEXT("...through SetEnemy, so the old handle went down the last-enemy path"),
		F.Guard->Senses.Memory.LastEnemy == F.Victim->Handle);
	TestFalse(TEXT("...and the previous LOS episode's latch was forgotten"),
		F.Guard->Senses.Memory.bEnemyLosLatched);
	TestEqual(TEXT("...along with its failure debounce"),
		F.Guard->Senses.Memory.EnemyLosFailures, 0);
	TestFalse(TEXT("...and its occlusion flag"), F.Guard->Senses.Memory.bEnemyOccluded);

	// "injects native condition 0x54".
	TestTrue(TEXT("NEW_ENEMY (0x54) is injected"),
		F.Guard->Cognition.Conditions.Has(ECond::NewEnemy));
	TestEqual(TEXT("NEW_ENEMY is retail's own 0x54"), static_cast<int32>(ECond::NewEnemy), 0x54);

	// "copies its target position".
	TestTrue(TEXT("the goal's position is copied onto the NPC"),
		F.Guard->SavePosition.Equals(F.Player->Origin));

	// `forcestate 3` is COMBAT, and mode 3 supplied the enemy that state needs.
	TestTrue(TEXT("forcestate 3 put the mind in combat"),
		F.Guard->GetMind().State() == EElysiumNpcState::Combat);
	// Mode 3 pushes policy only: it claims no body.
	TestTrue(TEXT("mode 3 claims no body — it is a policy push, not a movement order"),
		F.Guard->GetMind().Owner() == EElysiumBodyOwner::None);
	TestTrue(TEXT("...and leaves no order behind"),
		!F.Guard->ScriptedScheduleOrder.IsSet());

	// Ordinary combat selection follows from the state the director pushed.
	const EId Selected = F.Guard->SelectSchedule();
	TestFalse(TEXT("combat selection follows the push rather than the idle stance"),
		Selected == EId::IdleDisposition);
	TestEqual(TEXT("...into a registered combat program"), Selected, EId::MeleeIdle);
	return true;
}

// The recovered refusals: a missing goal, a bare row, and the route-failure switch.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAiScriptedScheduleRefusalTest,
	"Elysium.Substrate.AiScriptedSchedule.Refusals", GElysiumTestFlags)
bool FElysiumAiScriptedScheduleRefusalTest::RunTest(const FString&)
{
	// A missing goal logs and stops, and pushes NOTHING.
	// This fires in shipped content: `sm_medical_1`'s `guard_to_cs` names `cs_target`, which that
	// map does not contain.
	{
		AddExpectedError(TEXT("resolves to nothing"), EAutomationExpectedErrorFlags::Contains, 1);
		FAiScheduleFixture::FSetup Setup;
		Setup.Mode = 2;
		Setup.ForceState = 3;
		Setup.GoalName = TEXT("cs_target");   // authored, absent — the corpus's own case
		FAiScheduleFixture F(Setup);
		if (F.Guard == nullptr)
		{
			return false;
		}
		F.FireStartSchedule();
		TestTrue(TEXT("a missing goal pushes no order"),
			!F.Guard->ScriptedScheduleOrder.IsSet());
		TestTrue(TEXT("...and claims no body"),
			F.Guard->GetMind().Owner() == EElysiumBodyOwner::None);
		// "logs and stops" — including the state, which the director never got far enough to push.
		TestTrue(TEXT("...and does not push the forced state either"),
			F.Guard->GetMind().State() == EElysiumNpcState::Idle);
		// The second fire is latched, not repeated.
		F.FireStartSchedule();
	}

	// The spawn validator: neither a schedule nor a forced state.
	{
		AddExpectedError(TEXT("authors neither a schedule mode nor a forced state"),
			EAutomationExpectedErrorFlags::Contains, 1);
		FAiScheduleFixture::FSetup Setup;
		Setup.Mode = 0;
		Setup.ForceState = 0;
		FAiScheduleFixture F(Setup);
		TestNotNull(TEXT("the bare row still constructs as an entity"), F.Director);
	}

	// A forced state with no movement mode is an ordinary row.
	{
		FAiScheduleFixture::FSetup Setup;
		Setup.Mode = 0;
		Setup.ForceState = 1;
		FAiScheduleFixture F(Setup);
		if (F.Guard == nullptr)
		{
			return false;
		}
		F.FireStartSchedule();
		TestTrue(TEXT("a state-only row pushes the state"),
			F.Guard->GetMind().State() == EElysiumNpcState::Idle);
		TestTrue(TEXT("...and no order"),
			!F.Guard->ScriptedScheduleOrder.IsSet());
	}

	// A body that will not take the route reports it once.
	{
		AddExpectedError(TEXT("could not take the route an aiscripted_schedule pushed"),
			EAutomationExpectedErrorFlags::Contains, 1);
		FAiScheduleFixture::FSetup Setup;
		Setup.Mode = 2;
		FAiScheduleFixture F(Setup);
		if (F.Guard == nullptr)
		{
			return false;
		}
		if (FElysiumRecordingNpcMotor* Motor = F.MotorFor(F.Guard))
		{
			Motor->bAcceptMoves = false;   // "this goal has no path"
		}
		F.FireStartSchedule();
		F.Step(0.1);
		TestTrue(TEXT("a refused route ends the order rather than holding the body"),
			!F.Guard->ScriptedScheduleOrder.IsSet());
		TestTrue(TEXT("...and gives the body back"),
			F.Guard->GetMind().Owner() == EElysiumBodyOwner::None);
	}

	// A director that fires before the NPC's first think is DEFERRED, not lost.
	// `sm_medical_1` wires `guard_to_nurse` off an `npc_maker`'s `OnSpawnNPC`, so this is the
	// shipped case rather than a synthetic one.
	{
		FAiScheduleFixture::FSetup Setup;
		Setup.Mode = 2;
		Setup.ForceState = 2;
		Setup.bSkipAdmission = true;
		FAiScheduleFixture F(Setup);
		if (F.Guard == nullptr)
		{
			return false;
		}
		F.FireStartSchedule();
		TestTrue(TEXT("an un-admitted mind keeps the push waiting"),
			F.Guard->GetMind().Admission() == FElysiumNpcMind::EAdmission::Armed);
		TestTrue(TEXT("...and claims no body while it waits"),
			F.Guard->GetMind().Owner() == EElysiumBodyOwner::None);

		F.Step(0.1);   // the admission think, which establishes Idle and returns
		F.Step(0.2);   // the replay
		TestTrue(TEXT("the deferred order lands once admission has run"),
			F.Guard->ScriptedScheduleOrder.IsSet() && F.Guard->ScriptedScheduleOrder.Mode == 2);
		// The point of deferring the WHOLE push: admission establishes idle, so a forced state
		// applied ahead of it would have been wiped by the barrier it was racing.
		TestTrue(TEXT("...carrying the forced state the admission barrier would have wiped"),
			F.Guard->GetMind().State() == EElysiumNpcState::Alert);
	}

	// Spawn flag 0x800 suppresses exactly that warning.
	{
		// No AddExpectedError here on purpose: the point of the case is that nothing is emitted.
		FAiScheduleFixture::FSetup Setup;
		Setup.Mode = 2;
		Setup.SpawnFlags = ElysiumAiScriptedSchedule::SpawnFlagSuppressRouteWarning;
		FAiScheduleFixture F(Setup);
		if (F.Guard == nullptr)
		{
			return false;
		}
		TestTrue(TEXT("the suppression flag is visible on the director"),
			F.Debug(F.Director, TEXT("Spawnflags")).Contains(TEXT("0x800")));
		if (FElysiumRecordingNpcMotor* Motor = F.MotorFor(F.Guard))
		{
			Motor->bAcceptMoves = false;
		}
		F.FireStartSchedule();
		F.Step(0.1);
		TestTrue(TEXT("the refusal still ends the order silently"),
			!F.Guard->ScriptedScheduleOrder.IsSet());
	}
	return true;
}

// `ChangeSchedule` / `StartSchedule` — a native schedule named by a script.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAiScriptedScheduleNamedTest,
	"Elysium.Substrate.AiScriptedSchedule.NamedSchedule", GElysiumTestFlags)
bool FElysiumAiScriptedScheduleNamedTest::RunTest(const FString&)
{
	ElysiumStub::ClearTally();
	ON_SCOPE_EXIT { ElysiumStub::ClearTally(); };

	FAiScheduleFixture::FSetup Setup;
	FAiScheduleFixture F(Setup);
	if (F.Guard == nullptr)
	{
		return false;
	}

	// A registered name starts through the ordinary kernel.
	F.World.AcceptInput(TEXT("!self"), FName(TEXT("ChangeSchedule")),
		FElysiumVariant::String(TEXT("SCHED_TROIKA_MELEE_IDLE")), F.Guard->Handle, F.Guard->Handle);
	TestEqual(TEXT("a registered name starts its own program"),
		F.Guard->Schedule.Current, EId::MeleeIdle);

	// Case folding is retail's own posture for every named surface in this runtime.
	F.World.AcceptInput(TEXT("!self"), FName(TEXT("StartSchedule")),
		FElysiumVariant::String(TEXT("sched_troika_chase_enemy")), F.Guard->Handle,
		F.Guard->Handle);
	TestEqual(TEXT("the lookup is case-insensitive, and StartSchedule takes the same door"),
		F.Guard->Schedule.Current, EId::ChaseEnemy);

	// The unknown names, which is every name the shipped scripts actually use.
	AddExpectedError(TEXT("SCHED_VDOG_SNARL"), EAutomationExpectedErrorFlags::Contains, 1);
	F.World.AcceptInput(TEXT("!self"), FName(TEXT("ChangeSchedule")),
		FElysiumVariant::String(TEXT("SCHED_VDOG_SNARL")), F.Guard->Handle, F.Guard->Handle);
	TestEqual(TEXT("an unregistered name changes nothing"),
		F.Guard->Schedule.Current, EId::ChaseEnemy);

	TArray<ElysiumStub::FTally> Tally;
	ElysiumStub::CollectTally(Tally);
	const ElysiumStub::FTally* SnarlRow = Tally.FindByPredicate(
		[](const ElysiumStub::FTally& Row)
		{ return Row.Surface.Contains(TEXT("SCHED_VDOG_SNARL")); });
	if (TestNotNull(TEXT("the unknown name reports through the stub funnel"), SnarlRow))
	{
		TestEqual(TEXT("...keyed on the NAME, so the tally is a per-schedule work list"),
			SnarlRow->Kind, FString(TEXT("schedule")));
	}

	// A different unknown name is a different row rather than the same one twice.
	AddExpectedError(TEXT("SCHED_VDOG_MADEFRIEND"), EAutomationExpectedErrorFlags::Contains, 1);
	F.World.AcceptInput(TEXT("!self"), FName(TEXT("ChangeSchedule")),
		FElysiumVariant::String(TEXT("SCHED_VDOG_MADEFRIEND")), F.Guard->Handle, F.Guard->Handle);
	ElysiumStub::CollectTally(Tally);
	const int32 ScheduleRows = Tally.FilterByPredicate(
		[](const ElysiumStub::FTally& Row) { return Row.Kind == TEXT("schedule"); }).Num();
	TestEqual(TEXT("two unregistered names are two rows"), ScheduleRows, 2);

	// An empty parameter is refused by name rather than starting something arbitrary.
	AddExpectedError(TEXT("was fired with no schedule name"),
		EAutomationExpectedErrorFlags::Contains, 1);
	F.World.AcceptInput(TEXT("!self"), FName(TEXT("ChangeSchedule")), FElysiumVariant::Void(),
		F.Guard->Handle, F.Guard->Handle);
	return true;
}

// Precedence: the ScriptedSchedule owner's place in the arbiter.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAiScriptedSchedulePrecedenceTest,
	"Elysium.Substrate.AiScriptedSchedule.Precedence", GElysiumTestFlags)
bool FElysiumAiScriptedSchedulePrecedenceTest::RunTest(const FString&)
{
	// A director displaces a patrol route, and the route resumes.
	{
		FAiScheduleFixture::FSetup Setup;
		Setup.Mode = 2;
		Setup.bPatrol = true;
		FAiScheduleFixture F(Setup);
		if (F.Guard == nullptr)
		{
			return false;
		}
		TestTrue(TEXT("the patrol executor owns the body first"),
			F.Guard->GetMind().Owner() == EElysiumBodyOwner::Patrol);

		F.FireStartSchedule();
		TestTrue(TEXT("the director displaces it"),
			F.Guard->GetMind().Owner() == EElysiumBodyOwner::ScriptedSchedule);
		TestTrue(TEXT("...and the route is PARKED rather than lost"),
			F.Guard->GetMind().SuspendedOwner() == EElysiumBodyOwner::Patrol);

		F.Step(0.1);
		FElysiumRecordingNpcMotor* Motor = F.MotorFor(F.Guard);
		if (!TestNotNull(TEXT("the directed NPC owns a motor"), Motor))
		{
			return false;
		}
		TestTrue(TEXT("the goal, not the patrol point, is what the body was sent to"),
			Motor->RequestedFeet.Equals(GGoalOrigin));

		Motor->SampleStatus = EElysiumNpcMoveStatus::Reached;
		F.Step(0.2);
		F.Step(0.3);
		TestTrue(TEXT("the route comes back when the program ends"),
			F.Guard->GetMind().Owner() == EElysiumBodyOwner::Patrol);
		Motor->SampleStatus = EElysiumNpcMoveStatus::Moving;
		F.Step(0.4);
		TestTrue(TEXT("...and the resumed route re-issues its own leg"),
			Motor->RequestedFeet.Equals(FVector(0.0, -200.0, 0.0))
			|| Motor->RequestedFeet.Equals(FVector(0.0, -400.0, 0.0)));
	}

	// A scripted sequence displaces the director.
	{
		FAiScheduleFixture::FSetup Setup;
		Setup.Mode = 2;
		FAiScheduleFixture F(Setup);
		if (F.Guard == nullptr)
		{
			return false;
		}
		F.FireStartSchedule();
		TestTrue(TEXT("the director holds the body"),
			F.Guard->GetMind().Owner() == EElysiumBodyOwner::ScriptedSchedule);

		TestTrue(TEXT("a beat takes it outright"),
			F.Guard->ClaimScriptBody(TEXT("test beat")));
		TestTrue(TEXT("...and the arbiter says so"),
			F.Guard->GetMind().Owner() == EElysiumBodyOwner::Sequence);
		// A director's order is DROPPED rather than parked: `aiscripted_schedule` has no resume.
		TestTrue(TEXT("the pushed order leaves with the body"),
			!F.Guard->ScriptedScheduleOrder.IsSet());
		TestEqual(TEXT("...and so does the program it was running"),
			F.Guard->Schedule.Current, EId::None);

		F.Guard->ReleaseScriptBody(TEXT("test beat ended"));
		TestTrue(TEXT("the beat's release restores an unowned body"),
			F.Guard->GetMind().Owner() == EElysiumBodyOwner::None);
	}

	// Three deep: a beat over a combat claim over a patrol route.
	// The arbiter has ONE parked slot. A combat schedule that took the body off a route is holding
	// that route in it, so a beat parking the SCHEDULE on top would discard the route and strand the
	// mind owning a claim whose token the leaf has already retired.
	{
		FAiScheduleFixture::FSetup Setup;
		Setup.bPatrol = true;
		FAiScheduleFixture F(Setup);
		if (F.Guard == nullptr || F.Victim == nullptr)
		{
			return false;
		}
		F.Guard->Relationships.SetEntity(F.Victim->Handle, EElysiumRelationship::Hate, 5);
		F.Guard->Senses.Memory.Enemy = F.Victim->Handle;
		F.Guard->UpdateIdealState(0.5);
		F.Step(0.6);
		TestTrue(TEXT("the combat claim took the route"),
			F.Guard->GetMind().Owner() == EElysiumBodyOwner::Schedule
			&& F.Guard->GetMind().SuspendedOwner() == EElysiumBodyOwner::Patrol);

		TestTrue(TEXT("a beat takes it from the combat claim"),
			F.Guard->ClaimScriptBody(TEXT("test beat")));
		TestTrue(TEXT("...and the ROUTE is what stays parked, not the schedule"),
			F.Guard->GetMind().SuspendedOwner() == EElysiumBodyOwner::Patrol);

		F.Guard->ReleaseScriptBody(TEXT("test beat ended"));
		TestTrue(TEXT("the route survives the whole stack and comes back"),
			F.Guard->GetMind().Owner() == EElysiumBodyOwner::Patrol);
	}
	return true;
}

// The pre-emption fix: a committed enemy outranks an autonomous executor.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAiScriptedSchedulePreemptionTest,
	"Elysium.Substrate.AiScriptedSchedule.CombatPreemption", GElysiumTestFlags)
bool FElysiumAiScriptedSchedulePreemptionTest::RunTest(const FString&)
{
	// The recovered emission this case runs into on the way back out of combat: `SelectIdealState`
	// case 2 emits `Combat state with no enemy` and falls back to alert. It is latched once per NPC.
	AddExpectedError(TEXT("Combat state with no enemy"), EAutomationExpectedErrorFlags::Contains, 1);

	FAiScheduleFixture::FSetup Setup;
	Setup.bPatrol = true;
	FAiScheduleFixture F(Setup);
	if (F.Guard == nullptr || F.Victim == nullptr)
	{
		return false;
	}
	TestTrue(TEXT("the guard starts on its route"),
		F.Guard->GetMind().Owner() == EElysiumBodyOwner::Patrol);

	// Commit an enemy through the real ideal-state pass, exactly as `FCombatFixture` does.
	F.Guard->Relationships.SetEntity(F.Victim->Handle, EElysiumRelationship::Hate, 5);
	F.Guard->Senses.Memory.Enemy = F.Victim->Handle;
	F.Guard->UpdateIdealState(0.5);
	TestTrue(TEXT("the acquisition promotes the mind to combat"),
		F.Guard->GetMind().State() == EElysiumNpcState::Combat);

	// A committed enemy must reach combat selection: the melee selector answers `CAN_MELEE_ATTACK1`
	// with `SCHED_TROIKA_MELEE_ATTACK1`, whose `TASK_FACE_ENEMY` claims the body and turns it. A
	// patrol executor issues `MoveTo` and never `Face`, so the turn is the discriminator. Routing
	// a patrolling NPC to `ThinkPatrol` skips combat selection entirely.
	F.Step(0.6);
	// Asked of the PRODUCER directly rather than read off the NPC's condition set after the think,
	// because the condition does not outlive the selection it drove: `ElysiumSchedule::Start` clears
	// every gathered condition, which is retail's own `CAI_BaseNPC::SetSchedule` (`0x10280e50`)
	// zeroing the six dwords at `+0x5c5c` that `SetCondition` (`0x10269a20`) writes. A post-install
	// snapshot of the set is empty in retail too, so the old spelling of this assertion was reading a
	// slot the install had legitimately wiped. The claim under test is unchanged.
	FElysiumNpcConditions Attack;
	ElysiumNpcCond::GatherAttackConditions(*F.Guard, 0.6, Attack);
	TestTrue(TEXT("the committed enemy raises CAN_MELEE_ATTACK1"),
		Attack.Has(EElysiumNpcCond::CanMeleeAttack1));
	TestTrue(TEXT("a patrolling NPC in combat reaches its combat program"),
		F.Services.Saw(TEXT("NpcMotor Face")));
	TestTrue(TEXT("...and the program's movement claim displaced the route"),
		F.Guard->GetMind().Owner() == EElysiumBodyOwner::Schedule);
	TestTrue(TEXT("...with the route PARKED rather than lost"),
		F.Guard->GetMind().SuspendedOwner() == EElysiumBodyOwner::Patrol);

	// The enemy dies. The transaction clears it, the ideal state falls back, and the route resumes.
	F.Victim->Kill();
	for (int32 i = 0; i < 4; ++i)
	{
		F.Step(0.7 + 0.1 * i);
	}
	TestFalse(TEXT("the dead enemy is no longer committed"),
		F.Guard->Senses.Memory.Enemy.IsSet() && F.Guard->Senses.Memory.Enemy == F.Victim->Handle);
	TestFalse(TEXT("the mind has left combat"),
		F.Guard->GetMind().State() == EElysiumNpcState::Combat);
	TestTrue(TEXT("the parked patrol route owns the body again"),
		F.Guard->GetMind().Owner() == EElysiumBodyOwner::Patrol);
	return true;
}

}   // namespace ElysiumAiScriptedScheduleTests

#endif // WITH_DEV_AUTOMATION_TESTS
