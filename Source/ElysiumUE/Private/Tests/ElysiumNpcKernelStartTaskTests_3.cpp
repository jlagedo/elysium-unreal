// Story 0019/8 (29e under the strict verdict), family **StartTask19** -- the family's tests, third
// part: lane L03, `CAI_BaseNPC::StartTask` `0x102827f0` (slot 442 on the base).
//
// Every case drives the BASE body directly (`FElysiumNpcBase::StartTaskSlot442`, qualified, so the
// Troika override and the species above it do not intercept) on a headless guard: no motor, no node
// graph, no hint store. So the arms whose route or search has no source here are asserted on what
// they write and ask for before that source (the goal record, the search radii, the failure code and
// line), which is what the listing fixes. Test names carry the retail arm address.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumLocalIdSpace.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcBase.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumNpcTestCensus.h"

static constexpr EAutomationTestFlags GStartTask19BaseFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	struct FStartTask19BaseFixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Guard = nullptr;
		FElysiumNpc* Other = nullptr;
		FElysiumPlayer* Player = nullptr;

		FStartTask19BaseFixture()
			: World([]
				{
					FElysiumNpcWorldBuilder Builder(TEXT("starttask19_base"), 1903);
					Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
					Builder.AddNpcOfClass(TEXT("guard"), FVector::ZeroVector, TEXT("CNPC_VHumanCombatant"));
					Builder.AddNpc(TEXT("other"), FVector(400.f, 0.f, 0.f), TEXT("npc_VHumanCombatant"));
					return Builder;
				}())
		{
			Guard = World.Npc(TEXT("guard"));
			Other = World.Npc(TEXT("other"));
			Player = World.Player();
			FElysiumNpcWorldFixture::Quiet({ Guard, Other });
		}

		// The class-LOCAL task (the registrar's number the switch compares) as the GLOBAL id a schedule
		// step carries (`ElysiumScheduleText.h`); the body translates it back (slot 450's body).
		int32 GlobalTask(int32 LocalTask) const
		{
			const FElysiumLocalIdSpace* Space = Guard->IdSpace(EElysiumIdCategory::Task);
			return Space != nullptr ? Space->LocalToGlobal(LocalTask) : LocalTask;
		}

		// One StartTask call on the BASE body, from a clean task status.
		void Run(int32 TaskId, float Data = 0.f)
		{
			Guard->Cognition.Conditions.Clear(EElysiumNpcCond::TaskFailed);
			Guard->Schedule.TaskStatus = EElysiumTaskStatus::New;
			Guard->BaseScheduleHost.FailureReason = 0;
			FElysiumScheduleStep Step;
			Step.TaskId = GlobalTask(TaskId);
			Step.Data = Data;
			Guard->FElysiumNpcBase::StartTaskSlot442(&Step);
		}

		bool Completed() const { return Guard->Schedule.TaskStatus == EElysiumTaskStatus::Complete; }
		bool Failed() const { return Guard->Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed); }
		int32 Reason() const { return Guard->BaseScheduleHost.FailureReason; }
		double Now() const { return World.World.NowSeconds(); }
		void SetEnemy(FElysiumEntity* Enemy)
		{
			Guard->BaseMemory.Enemy = Enemy != nullptr ? Enemy->Handle : FElysiumEntityHandle::Invalid();
		}
	};
}

// -------------------------------------------------------------------------------------------------
// The dispatch: `0x10282809` range guard, byte table `0x10287138`, default arm `0x10286f63`, and the
// epilogue arm `0x10286f7d` (tasks 5, 0x68, 0x74).
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19BaseDispatchTest,
	"Elysium.Substrate.NpcKernelStartTask19.Base.Dispatch_0x10286f63", GStartTask19BaseFlags)
bool FElysiumNpcKernelStartTask19BaseDispatchTest::RunTest(const FString&)
{
	FStartTask19BaseFixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard))
	{
		return false;
	}
	// Ids with no arm: SOUND_DEATH 0x45, SET_TOLERANCE_DISTANCE_ABS 0x4f, the VtMB block, and both
	// sides of the (iTask - 1) <= 0x11f guard. Each DevMsgs and leaves the task running.
	for (const int32 TaskId : { 0x45, 0x4f, 0x78, 0x11f, 0x121, 0 })
	{
		const int32 Before = F.Guard->StartTaskNav.DevMessages;
		F.Run(TaskId);
		TestFalse(FString::Printf(TEXT("0x%x does not complete"), TaskId), F.Completed());
		TestFalse(FString::Printf(TEXT("0x%x does not fail"), TaskId), F.Failed());
		TestEqual(FString::Printf(TEXT("0x%x DevMsgs once"), TaskId), F.Guard->StartTaskNav.DevMessages, Before + 1);
		TestTrue(FString::Printf(TEXT("0x%x names the no-entry message"), TaskId),
			F.Guard->StartTaskNav.LastDevMessage.StartsWith(TEXT("No StartTask entry for ")));
	}
	// The epilogue arm: no start work at all.
	for (const int32 TaskId : { 0x05, 0x68, 0x74 })
	{
		const int32 Before = F.Guard->StartTaskNav.DevMessages;
		F.Run(TaskId);
		TestFalse(FString::Printf(TEXT("0x%x does nothing"), TaskId), F.Completed() || F.Failed());
		TestEqual(FString::Printf(TEXT("0x%x prints nothing"), TaskId), F.Guard->StartTaskNav.DevMessages, Before);
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// The bookkeeping arms: 0x00, 0x02, 0x04, 0x3a..0x3f, 0x42, 0x44, 0x45, 0x5b, 0x5c, 0x62.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19BaseBookkeepingTest,
	"Elysium.Substrate.NpcKernelStartTask19.Base.Bookkeeping_0x10282828", GStartTask19BaseFlags)
bool FElysiumNpcKernelStartTask19BaseBookkeepingTest::RunTest(const FString&)
{
	FStartTask19BaseFixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard))
	{
		return false;
	}
	F.Guard->ActivityNumber = 7;
	F.Run(0x01);                                                         // 0x10282828
	TestEqual(TEXT("RESET_ACTIVITY zeroes m_Activity"), F.Guard->ActivityNumber, 0);
	TestTrue(TEXT("RESET_ACTIVITY completes"), F.Completed());

	F.Run(0x03);                                                         // 0x10286cd9
	TestTrue(TEXT("ANNOUNCE_ATTACK completes bare"), F.Completed());

	F.Run(0x06, 2.f);                                                    // 0x10286c0d
	TestEqual(TEXT("SUGGEST_STATE writes m_IdealNPCState"), F.Guard->IdealStateRetail(), 2);
	TestTrue(TEXT("SUGGEST_STATE completes"), F.Completed());

	F.Guard->BaseScheduleHost.MemoryBits = 0x1;
	F.Run(0x6c, 6.f);                                                    // 0x102829bd
	TestEqual(TEXT("REMEMBER ORs"), F.Guard->BaseScheduleHost.MemoryBits, 0x7u);
	TestTrue(TEXT("REMEMBER completes"), F.Completed());
	F.Run(0x6d, 2.f);                                                    // 0x102829e9
	TestEqual(TEXT("FORGET clears"), F.Guard->BaseScheduleHost.MemoryBits, 0x5u);

	F.Run(0x4d, 9.f);                                                    // 0x10286c45
	TestEqual(TEXT("SET_FAIL_SCHEDULE"), F.Guard->Schedule.FailScheduleOverride, 9);
	TestTrue(TEXT("SET_FAIL_SCHEDULE completes"), F.Completed());
	F.Run(0x51);                                                         // 0x10286d58
	TestEqual(TEXT("CLEAR_FAIL_SCHEDULE zeroes"), F.Guard->Schedule.FailScheduleOverride, 0);

	F.Run(0x50, 2.7f);                                                   // 0x10286d1a
	TestEqual(TEXT("SET_ROUTE_SEARCH_TIME truncates to whole seconds"), F.Guard->Navigator.RouteSearchTime, 2.f);
	TestTrue(TEXT("SET_ROUTE_SEARCH_TIME completes"), F.Completed());

	const int32 DevBefore = F.Guard->StartTaskNav.DevMessages;
	F.Run(0x44);                                                         // 0x102868a3
	TestEqual(TEXT("SOUND_ANGRY is only its DevMsg"), F.Guard->StartTaskNav.LastDevMessage, FString(TEXT("SOUND\n")));
	TestEqual(TEXT("SOUND_ANGRY DevMsgs once"), F.Guard->StartTaskNav.DevMessages, DevBefore + 1);
	TestTrue(TEXT("SOUND_ANGRY completes"), F.Completed());
	for (const int32 TaskId : { 0x46, 0x47, 0x48, 0x49, 0x4a })         // 0x10286863 .. 0x102868c9
	{
		F.Run(TaskId);
		TestTrue(FString::Printf(TEXT("sound task 0x%x completes"), TaskId), F.Completed());
	}

	F.Guard->bIsUsingSmallHull = false;
	F.Run(0x73);                                                         // 0x10286e0b
	TestTrue(TEXT("USE_SMALL_HULL takes the small hull"), F.Guard->bIsUsingSmallHull);
	TestTrue(TEXT("USE_SMALL_HULL completes"), F.Completed());
	return true;
}

// -------------------------------------------------------------------------------------------------
// The timer arms: 0x01, 0x20..0x23, 0x25, 0x29, 0x57, 0x61.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19BaseTimersTest,
	"Elysium.Substrate.NpcKernelStartTask19.Base.Timers_0x10286505", GStartTask19BaseFlags)
bool FElysiumNpcKernelStartTask19BaseTimersTest::RunTest(const FString&)
{
	FStartTask19BaseFixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard))
	{
		return false;
	}
	const double Now = F.Now();
	for (const int32 TaskId : { 0x02, 0x04, 0x2d })                      // 0x10286505, 0x10286537
	{
		F.Run(TaskId, 3.5f);
		TestEqual(FString::Printf(TEXT("0x%x stamps curtime + data"), TaskId), F.Guard->BaseScheduleHost.WaitFinished, Now + 3.5);
		TestFalse(FString::Printf(TEXT("0x%x does not complete"), TaskId), F.Completed());
	}
	// No floor: a zero operand stamps curtime itself.
	F.Run(0x02, 0.f);
	TestEqual(TEXT("WAIT has no floor"), F.Guard->BaseScheduleHost.WaitFinished, Now);

	F.Guard->BaseScheduleHost.bShouldMove = false;
	F.Run(0x24, 2.f);                                                    // 0x102864f1
	TestTrue(TEXT("WALK_PATH_TIMED sets m_bShouldMove"), F.Guard->BaseScheduleHost.bShouldMove);
	TestEqual(TEXT("WALK_PATH_TIMED walks"), F.Guard->Navigator.MovementActivity, 0x09);
	TestEqual(TEXT("WALK_PATH_TIMED stamps the wait"), F.Guard->BaseScheduleHost.WaitFinished, Now + 2.0);
	F.Run(0x27, 1.f);                                                    // 0x10286523
	TestEqual(TEXT("RUN_PATH_TIMED runs"), F.Guard->Navigator.MovementActivity, 0x13);
	TestEqual(TEXT("RUN_PATH_TIMED stamps the wait"), F.Guard->BaseScheduleHost.WaitFinished, Now + 1.0);
	F.Run(0x25);                                                         // 0x102864af
	TestEqual(TEXT("WALK_PATH_WITHIN_DIST walks"), F.Guard->Navigator.MovementActivity, 0x09);
	F.Run(0x26);                                                         // 0x102864d0
	TestEqual(TEXT("RUN_PATH_WITHIN_DIST runs"), F.Guard->Navigator.MovementActivity, 0x13);
	F.Guard->Navigator.MovementActivity = 0;
	F.Run(0x72);                                                         // 0x102864d7
	TestEqual(TEXT("WEAPON_RUN_PATH runs"), F.Guard->Navigator.MovementActivity, 0x13);
	TestFalse(TEXT("none of the gait arms completes"), F.Completed());

	F.Guard->BaseScheduleHost.MoveWaitFinished = 99.0;
	F.Run(0x29);                                                         // 0x10284218
	TestEqual(TEXT("CLEAR_MOVE_WAIT stamps curtime"), F.Guard->BaseScheduleHost.MoveWaitFinished, Now);
	TestTrue(TEXT("CLEAR_MOVE_WAIT completes"), F.Completed());

	F.Run(0x67, 2.f);                                                    // 0x10283dae
	TestTrue(TEXT("WAIT_RANDOM draws in [0.1, data]"),
		F.Guard->BaseScheduleHost.WaitFinished >= Now + 0.1 - 1e-4 && F.Guard->BaseScheduleHost.WaitFinished <= Now + 2.0 + 1e-4);
	TestFalse(TEXT("WAIT_RANDOM does not complete"), F.Completed());
	return true;
}

// -------------------------------------------------------------------------------------------------
// The gait and strafe arms: 0x1e, 0x1f, 0x24.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19BasePathGaitTest,
	"Elysium.Substrate.NpcKernelStartTask19.Base.PathGait_0x102863f1", GStartTask19BaseFlags)
bool FElysiumNpcKernelStartTask19BasePathGaitTest::RunTest(const FString&)
{
	FStartTask19BaseFixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard))
	{
		return false;
	}
	// The kernel tier authors no sequence (`SelectWeightedSequenceForActivity` answers -1), so each
	// ladder takes its fallback rung.
	F.Guard->BaseScheduleHost.MemoryBits = 0x3;
	F.Run(0x22);                                                         // 0x102863f1
	TestEqual(TEXT("RUN_PATH falls back to ACT_WALK"), F.Guard->Navigator.MovementActivity, 0x09);
	TestEqual(TEXT("RUN_PATH forgets INCOVER"), F.Guard->BaseScheduleHost.MemoryBits, 0x1u);
	TestTrue(TEXT("RUN_PATH completes"), F.Completed());

	F.Guard->BaseScheduleHost.MemoryBits = 0x2;
	F.Run(0x23);                                                         // 0x10286438
	TestEqual(TEXT("WALK_PATH falls to ACT_RUN when ACT_WALK is missing"), F.Guard->Navigator.MovementActivity, 0x13);
	TestEqual(TEXT("WALK_PATH forgets INCOVER"), F.Guard->BaseScheduleHost.MemoryBits, 0x0u);
	TestTrue(TEXT("WALK_PATH completes"), F.Completed());

	F.Guard->BaseScheduleHost.bShouldMove = false;
	F.Run(0x28);                                                         // 0x10286556
	TestTrue(TEXT("STRAFE_PATH sets m_bShouldMove"), F.Guard->BaseScheduleHost.bShouldMove);
	const int32 Strafe = F.Guard->Navigator.MovementActivity;
	TestTrue(TEXT("STRAFE_PATH picks a strafe"), Strafe == 0x37 || Strafe == 0x38);
	TestTrue(TEXT("STRAFE_PATH completes"), F.Completed());
	return true;
}

// -------------------------------------------------------------------------------------------------
// The position arms: 0x13..0x17.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19BasePositionsTest,
	"Elysium.Substrate.NpcKernelStartTask19.Base.Positions_0x10282afd", GStartTask19BaseFlags)
bool FElysiumNpcKernelStartTask19BasePositionsTest::RunTest(const FString&)
{
	FStartTask19BaseFixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("other"), F.Other))
	{
		return false;
	}
	F.Guard->Angles = FVector(0.0, 45.0, 0.0);
	F.Run(0x17);                                                         // 0x10282afd
	TestEqual(TEXT("STORE_LASTPOSITION origin"), F.Guard->LastPosition, F.Guard->Origin);
	TestEqual(TEXT("STORE_LASTPOSITION facing"), F.Guard->LastFacing, F.Guard->Angles);
	TestTrue(TEXT("STORE_LASTPOSITION completes"), F.Completed());
	F.Run(0x18);                                                         // 0x10282b5b
	TestEqual(TEXT("CLEAR_LASTPOSITION origin"), F.Guard->LastPosition, FVector::ZeroVector);
	TestEqual(TEXT("CLEAR_LASTPOSITION facing"), F.Guard->LastFacing, FVector::ZeroVector);

	F.Guard->SavePosition = FVector(1.0, 2.0, 3.0);
	F.Run(0x19);                                                         // 0x10282bb7
	TestEqual(TEXT("STORE_POSITION_IN_SAVEPOSITION"), F.Guard->SavePosition, F.Guard->Origin);

	F.SetEnemy(nullptr);
	F.Run(0x1b);                                                         // 0x10282cc7
	TestTrue(TEXT("STORE_ENEMY_POSITION with no enemy fails"), F.Failed());
	TestEqual(TEXT("... with FAIL_NO_ENEMY"), F.Reason(), 0x06);
	F.SetEnemy(F.Other);
	F.Run(0x1b);
	TestEqual(TEXT("STORE_ENEMY_POSITION stores the enemy origin"), F.Guard->SavePosition, F.Other->Origin);
	TestTrue(TEXT("STORE_ENEMY_POSITION completes"), F.Completed());

	// The Troika line's slot 474 answers its own best-sound record, never null.
	F.Guard->Senses.Memory.BestSound.Position = FVector(10.0, 20.0, 30.0);
	F.Guard->Senses.Memory.BestSound.Source = FElysiumEntityHandle::Invalid();
	F.Run(0x1a);                                                         // 0x10282bf1
	TestEqual(TEXT("STORE_BESTSOUND stores the sound origin"), F.Guard->SavePosition, FVector(10.0, 20.0, 30.0));
	TestTrue(TEXT("STORE_BESTSOUND completes"), F.Completed());
	return true;
}

// -------------------------------------------------------------------------------------------------
// The target arms: 0x05, 0x06, 0x07, 0x11, 0x2d, 0x55, 0x56.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19BaseTargetsTest,
	"Elysium.Substrate.NpcKernelStartTask19.Base.Targets_0x10283f36", GStartTask19BaseFlags)
bool FElysiumNpcKernelStartTask19BaseTargetsTest::RunTest(const FString&)
{
	FStartTask19BaseFixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("other"), F.Other)
		|| !TestNotNull(TEXT("player"), F.Player))
	{
		return false;
	}
	F.Run(0x07);                                                         // 0x10283ed5
	TestTrue(TEXT("TARGET_PLAYER targets the player"), F.Guard->GetTarget() == F.Player->Handle);
	TestTrue(TEXT("TARGET_PLAYER completes"), F.Completed());

	F.Guard->SetTarget(FElysiumEntityHandle::Invalid());
	for (const int32 TaskId : { 0x0b, 0x15, 0x31 })                      // 0x10283dde, 0x10285949, 0x10283b9e
	{
		F.Run(TaskId);
		TestTrue(FString::Printf(TEXT("0x%x with no target fails"), TaskId), F.Failed());
		TestEqual(FString::Printf(TEXT("0x%x ... FAIL_NO_TARGET"), TaskId), F.Reason(), 0x01);
	}
	// The move-to-target arm fails with 1 and STILL runs its shared tail (`0x102841f2`).
	F.Guard->ScriptArrivalActivity = 5;
	F.Guard->ScriptArrivalSequence = TEXT("seq");
	F.Run(0x08);
	TestEqual(TEXT("WALK_TO_TARGET with no target fails 1"), F.Reason(), 0x01);
	TestEqual(TEXT("... and clears the arrival activity"), F.Guard->ScriptArrivalActivity, INDEX_NONE);
	TestTrue(TEXT("... and the arrival sequence"), F.Guard->ScriptArrivalSequence.IsEmpty());
	TestFalse(TEXT("... and its completion is blocked by the failure"), F.Completed());

	// A far target: `MOVE_TO_TARGET_RANGE` leaves the task running with no goal.
	F.Guard->SetTarget(F.Other->Handle);
	const int32 GoalsBefore = F.Guard->StartTaskNav.SetGoalCalls;
	F.Run(0x0b);
	TestFalse(TEXT("MOVE_TO_TARGET_RANGE far: running"), F.Completed() || F.Failed());
	TestEqual(TEXT("MOVE_TO_TARGET_RANGE sets no goal"), F.Guard->StartTaskNav.SetGoalCalls, GoalsBefore);
	// No sequence for ACT_WALK at the kernel tier: WALK_TO_TARGET completes before any goal.
	F.Run(0x08);
	TestTrue(TEXT("WALK_TO_TARGET without the sequence completes"), F.Completed());
	TestEqual(TEXT("... with no goal set"), F.Guard->StartTaskNav.SetGoalCalls, GoalsBefore);

	// The target standing on the NPC: under 1.0 unit completes.
	F.Other->Origin = F.Guard->Origin;
	F.Run(0x0b);
	TestTrue(TEXT("MOVE_TO_TARGET_RANGE at the target completes"), F.Completed());

	// GET_PATH_TO_TARGET: a type-4 goal at the target, pTarget the target, tolerance -1.0, flag 0.
	F.Other->Origin = FVector(500.f, 0.f, 0.f);
	F.Run(0x15);                                                         // 0x10285949
	TestEqual(TEXT("GET_PATH_TO_TARGET goal type"), F.Guard->StartTaskNav.LastGoal.Type, 4);
	TestEqual(TEXT("... at the target"), F.Guard->StartTaskNav.LastGoal.DestCm, F.Other->Origin);
	TestTrue(TEXT("... pTarget is the target"), F.Guard->StartTaskNav.LastGoal.Target == F.Other->Handle);
	TestEqual(TEXT("... tolerance -1.0"), F.Guard->StartTaskNav.LastGoal.ToleranceUnits, -1.f);
	TestEqual(TEXT("... SetGoal flag 0"), F.Guard->StartTaskNav.LastSetGoalFlags, 0);

	// FACE_TARGET: the yaw hold, the ideal yaw toward the target, no completion.
	const int32 Holds = F.Guard->StartTaskNav.MotorYawHolds;
	F.Guard->BaseScheduleHost.bMotorAnimationMovement = false;
	F.Run(0x31);                                                         // 0x10283b9e
	TestEqual(TEXT("FACE_TARGET holds the motor yaw"), F.Guard->StartTaskNav.MotorYawHolds, Holds + 1);
	TestEqual(TEXT("FACE_TARGET ideal yaw"), F.Guard->MotorIdealYaw, F.Guard->CalcIdealYaw(F.Other->Origin));
	TestFalse(TEXT("FACE_TARGET does not complete"), F.Completed());

	// PLANT_ON_SCRIPT: slot 62 SetOrigin onto the target, complete.
	F.Run(0x65);                                                         // 0x10286b54
	TestEqual(TEXT("PLANT_ON_SCRIPT plants on the target"), F.Guard->Origin, F.Other->Origin);
	TestTrue(TEXT("PLANT_ON_SCRIPT completes"), F.Completed());

	// FACE_SCRIPT: the target's angle-modded yaw, then ClearGoal; no completion.
	F.Other->Angles = FVector(0.0, 91.0, 0.0);
	const int32 Clears = F.Guard->StartTaskNav.ClearGoalCalls;
	F.Run(0x66);                                                         // 0x10286b96
	TestEqual(TEXT("FACE_SCRIPT ideal yaw"), F.Guard->MotorIdealYaw, FElysiumNpcBase::StartTaskAngleMod(91.f));
	TestEqual(TEXT("FACE_SCRIPT clears the goal"), F.Guard->StartTaskNav.ClearGoalCalls, Clears + 1);
	TestFalse(TEXT("FACE_SCRIPT does not complete"), F.Completed());
	return true;
}

// -------------------------------------------------------------------------------------------------
// The enemy path arms: 0x0b, 0x0c, 0x0d, 0x0e, 0x10.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19BaseEnemyPathsTest,
	"Elysium.Substrate.NpcKernelStartTask19.Base.EnemyPaths_0x1028509b", GStartTask19BaseFlags)
bool FElysiumNpcKernelStartTask19BaseEnemyPathsTest::RunTest(const FString&)
{
	FStartTask19BaseFixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("other"), F.Other))
	{
		return false;
	}
	F.SetEnemy(nullptr);
	for (const int32 TaskId : { 0x0f, 0x11, 0x14 })                      // 0x1028509b, 0x102844d1, 0x1028545a
	{
		F.Run(TaskId);
		TestEqual(FString::Printf(TEXT("0x%x with no enemy: FAIL_NO_ENEMY"), TaskId), F.Reason(), 0x06);
	}
	F.SetEnemy(F.Other);

	// GET_PATH_TO_ENEMY: a type-2 goal, flag 0; the headless navigator refuses it, so the
	// DevWarning, the unreachable mark and 0xc follow.
	F.Run(0x0f);
	TestEqual(TEXT("GET_PATH_TO_ENEMY goal type 2"), F.Guard->StartTaskNav.LastGoal.Type, 2);
	TestEqual(TEXT("... tolerance -1.0"), F.Guard->StartTaskNav.LastGoal.ToleranceUnits, -1.f);
	TestEqual(TEXT("... a refused route fails 0xc"), F.Reason(), 0x0c);
	TestEqual(TEXT("... after its DevWarning"), F.Guard->StartTaskNav.LastDevMessage, FString(TEXT("GetPathToEnemy failed!!\n")));
	bool bMarked = false;
	for (const FElysiumNpcBase::FUnreachableEntity& Row : F.Guard->UnreachableEnts)
	{
		bMarked |= Row.Entity == F.Other->Handle;
	}
	TestTrue(TEXT("... and the enemy is remembered unreachable"), bMarked);

	// The mark makes the NEXT call refuse before the null test (`IsUnreachable` first).
	F.Run(0x0f);
	TestEqual(TEXT("an unreachable enemy fails 0xc before any goal"), F.Reason(), 0x0c);

	F.Guard->UnreachableEnts.Reset();
	F.Run(0x10);                                                         // 0x10284349
	TestEqual(TEXT("GET_PATH_TO_ENEMY_LKP SetGoal flag 2"), F.Guard->StartTaskNav.LastSetGoalFlags, 2);
	TestEqual(TEXT("... goal type 4"), F.Guard->StartTaskNav.LastGoal.Type, 4);
	TestEqual(TEXT("... fails 0xc after its DevWarning"), F.Guard->StartTaskNav.LastDevMessage, FString(TEXT("GetPathToEnemyLKP failed!!\n")));

	// LKP_LOS / ENEMY_LOS: the weapon clamp (no weapon: 0 .. min(2000, m_flDistTooFar)) into
	// FindLosPos, which has no source here -> 0xb.
	F.Guard->UnreachableEnts.Reset();
	F.Guard->DistTooFar = 1500.f;
	F.Run(0x11);                                                         // 0x102844d1
	TestEqual(TEXT("LKP_LOS fails FAIL_NO_SHOOT"), F.Reason(), 0x0b);
	TestEqual(TEXT("... min range 0"), F.Guard->StartTaskNav.LastSearchMinUnits, 0.f);
	TestEqual(TEXT("... max clamped to m_flDistTooFar"), F.Guard->StartTaskNav.LastSearchMaxUnits, 1500.f);
	F.Guard->DistTooFar = 3000.f;
	F.Run(0x14);                                                         // 0x1028545a
	TestEqual(TEXT("ENEMY_LOS fails FAIL_NO_SHOOT"), F.Reason(), 0x0b);
	TestEqual(TEXT("... unarmed max 2000"), F.Guard->StartTaskNav.LastSearchMaxUnits, 2000.f);
	TestEqual(TEXT("... the threat is the enemy's origin"), F.Guard->StartTaskNav.LastSearchThreatCm, F.Other->Origin);

	// ENEMY_CORPSE: LKP minus forward * 64 units, SetGoal flag 2, no completion by the arm.
	F.Guard->Angles = FVector::ZeroVector;
	F.Run(0x12);                                                         // 0x1028523a
	TestEqual(TEXT("ENEMY_CORPSE SetGoal flag 2"), F.Guard->StartTaskNav.LastSetGoalFlags, 2);
	FVector Forward = FVector::ZeroVector;
	FElysiumNpcBase::StartTaskAngleVectors(F.Guard->Angles, &Forward, nullptr);
	const FVector Lkp = F.Guard->StartTaskNav.LastGoal.DestCm + Forward * (64.f * ElysiumMove::U);
	TestTrue(TEXT("... the dest is 64 units short of the LKP"),
		Lkp.Equals(F.Guard->EnemyMemory.Find(F.Other->Handle) != nullptr
			? F.Guard->EnemyMemory.Find(F.Other->Handle)->LastPosition : FVector::ZeroVector, 0.01));
	return true;
}

// -------------------------------------------------------------------------------------------------
// The cover arms: 0x08, 0x47..0x4e.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19BaseCoverTest,
	"Elysium.Substrate.NpcKernelStartTask19.Base.Cover_0x10283558", GStartTask19BaseFlags)
bool FElysiumNpcKernelStartTask19BaseCoverTest::RunTest(const FString&)
{
	FStartTask19BaseFixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("other"), F.Other))
	{
		return false;
	}
	const float Cover = F.Guard->CoverRadius();

	F.Run(0x5e, 2.f);                                                    // 0x102837c9
	TestEqual(TEXT("COVER_FROM_ORIGIN with no cover: 8"), F.Reason(), 0x08);
	TestEqual(TEXT("... hides from its own origin"), F.Guard->StartTaskNav.LastSearchThreatCm, F.Guard->Origin);
	TestEqual(TEXT("... radius 0 .. CoverRadius"), F.Guard->StartTaskNav.LastSearchMaxUnits, Cover);

	F.SetEnemy(nullptr);
	F.Run(0x58, 2.f);                                                    // 0x10283558
	TestEqual(TEXT("COVER_FROM_ENEMY with no enemy hides from itself, and fails 8"), F.Reason(), 0x08);
	TestEqual(TEXT("... the threat is the NPC"), F.Guard->StartTaskNav.LastSearchThreatCm, F.Guard->Origin);
	F.Run(0x59, 2.f);                                                    // 0x102836fa
	TestEqual(TEXT("LATERAL_COVER does no search, and fails 8"), F.Reason(), 0x08);

	for (const int32 TaskId : { 0x5a, 0x5b, 0x5c, 0x5d })                // 0x10282eab, 0x102833ac, 0x10283035, 0x102831ea
	{
		F.Run(TaskId, 100.f);
		TestEqual(FString::Printf(TEXT("0x%x with no enemy: FAIL_NO_ENEMY"), TaskId), F.Reason(), 0x06);
	}
	F.SetEnemy(F.Other);
	const float Resolved = F.Guard->ResolveTaskDistance(100.f);
	F.Run(0x5b, 100.f);
	TestEqual(TEXT("NODE_COVER fails 8"), F.Reason(), 0x08);
	TestEqual(TEXT("... min 0"), F.Guard->StartTaskNav.LastSearchMinUnits, 0.f);
	TestEqual(TEXT("... max CoverRadius"), F.Guard->StartTaskNav.LastSearchMaxUnits, Cover);
	F.Run(0x5c, 100.f);
	TestEqual(TEXT("NEAR_NODE_COVER min 0"), F.Guard->StartTaskNav.LastSearchMinUnits, 0.f);
	TestEqual(TEXT("... max is the operand"), F.Guard->StartTaskNav.LastSearchMaxUnits, Resolved);
	F.Run(0x5d, 100.f);
	TestEqual(TEXT("FAR_NODE_COVER min is the operand"), F.Guard->StartTaskNav.LastSearchMinUnits, Resolved);
	TestEqual(TEXT("... max CoverRadius"), F.Guard->StartTaskNav.LastSearchMaxUnits, Cover);
	F.Run(0x5a);                                                         // 0x10282eab
	TestEqual(TEXT("BACKAWAY with no node: FAIL_NO_BACKAWAY_NODE"), F.Reason(), 0x07);

	F.Guard->Senses.Memory.BestSound.Position = FVector(100.0, 0.0, 0.0);
	F.Guard->Senses.Memory.BestSound.RadiusCm = 10.5f * ElysiumMove::U;
	F.Run(0x57, 1.f);                                                    // 0x102838e7
	TestEqual(TEXT("COVER_FROM_BEST_SOUND fails 8"), F.Reason(), 0x08);
	TestEqual(TEXT("... the sound's volume is the minimum"), F.Guard->StartTaskNav.LastSearchMinUnits, 10.f);
	TestEqual(TEXT("... the sound is the threat"), F.Guard->StartTaskNav.LastSearchThreatEyeCm, FVector(100.0, 0.0, 0.0));

	// MOVE_AWAY_PATH: the first goal walks away along the motor yaw + 180; refused, the cover
	// fallback finds nothing and fails 8.
	const int32 Goals = F.Guard->StartTaskNav.SetGoalCalls;
	F.Run(0x0c, 50.f);                                                   // 0x1028611a
	TestEqual(TEXT("MOVE_AWAY_PATH set one goal"), F.Guard->StartTaskNav.SetGoalCalls, Goals + 1);
	TestEqual(TEXT("... walking"), F.Guard->StartTaskNav.LastGoal.MovementActivity, 0x09);
	TestEqual(TEXT("... then failed FAIL_NO_COVER"), F.Reason(), 0x08);
	return true;
}

// -------------------------------------------------------------------------------------------------
// TASK_SET_GOAL 0x102847a3 and TASK_GET_PATH_TO_GOAL 0x10284ae8.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19BaseStoredGoalTest,
	"Elysium.Substrate.NpcKernelStartTask19.Base.StoredGoal_0x102847a3", GStartTask19BaseFlags)
bool FElysiumNpcKernelStartTask19BaseStoredGoalTest::RunTest(const FString&)
{
	FStartTask19BaseFixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("other"), F.Other))
	{
		return false;
	}
	F.SetEnemy(nullptr);
	F.Run(0x0d, 0.f);
	TestEqual(TEXT("SET_GOAL 0 with no enemy: 6"), F.Reason(), 0x06);
	F.Run(0x0d, 2.f);
	TestEqual(TEXT("SET_GOAL 2 with no enemy: 6"), F.Reason(), 0x06);
	F.Guard->SetTarget(FElysiumEntityHandle::Invalid());
	F.Run(0x0d, 1.f);
	TestEqual(TEXT("SET_GOAL 1 with no target: 1"), F.Reason(), 0x01);
	F.Run(0x0d, 3.f);
	TestEqual(TEXT("SET_GOAL 3 with no target: 1"), F.Reason(), 0x01);

	F.SetEnemy(F.Other);
	F.Run(0x0d, 0.f);                                                    // 0x102847bf
	TestEqual(TEXT("SET_GOAL 0 stores the enemy origin"), F.Guard->BaseScheduleHost.StoredPathGoal, F.Other->Origin);
	TestEqual(TEXT("... type 2"), F.Guard->BaseScheduleHost.StoredPathType, 2);
	TestTrue(TEXT("... the enemy as the target"), F.Guard->BaseScheduleHost.StoredPathTarget == F.Other->Handle);
	TestEqual(TEXT("... then runs"), F.Guard->Navigator.MovementActivity, 0x13);
	TestTrue(TEXT("... and completes"), F.Completed());

	F.Guard->SetTarget(F.Other->Handle);
	F.Run(0x0d, 1.f);                                                    // 0x102848bf
	TestEqual(TEXT("SET_GOAL 1 type 1"), F.Guard->BaseScheduleHost.StoredPathType, 1);
	TestTrue(TEXT("... the target as the target"), F.Guard->BaseScheduleHost.StoredPathTarget == F.Other->Handle);

	F.Guard->SavePosition = FVector(7.0, 8.0, 9.0);
	F.Run(0x0d, 4.f);                                                    // 0x10284a8d
	TestEqual(TEXT("SET_GOAL 4 stores the save position"), F.Guard->BaseScheduleHost.StoredPathGoal, FVector(7.0, 8.0, 9.0));
	TestEqual(TEXT("... type 4"), F.Guard->BaseScheduleHost.StoredPathType, 4);
	TestFalse(TEXT("... no target"), F.Guard->BaseScheduleHost.StoredPathTarget.IsSet());

	F.Guard->BaseScheduleHost.StoredPathType = 9;
	F.Run(0x0d, 7.f);                                                    // 0x102847b2 JA
	TestEqual(TEXT("SET_GOAL out of table leaves the store"), F.Guard->BaseScheduleHost.StoredPathType, 9);
	TestTrue(TEXT("... and completes"), F.Completed());

	// GET_PATH_TO_GOAL: the goal built from the store at hull tolerance.
	F.Run(0x0d, 4.f);
	F.Run(0x0e, 9.f);                                                    // 0x10284c5a
	TestEqual(TEXT("GET_PATH_TO_GOAL out of range: 0xc"), F.Reason(), 0x0c);
	F.Run(0x0e, 0.f);                                                    // 0x10284dfd
	TestEqual(TEXT("mode 0: the stored point"), F.Guard->StartTaskNav.LastGoal.DestCm, FVector(7.0, 8.0, 9.0));
	TestEqual(TEXT("... type from the store"), F.Guard->StartTaskNav.LastGoal.Type, 4);
	TestEqual(TEXT("... hull tolerance"), F.Guard->StartTaskNav.LastGoal.ToleranceUnits, -2.f);
	TestEqual(TEXT("... a refused route: 0xc"), F.Reason(), 0x0c);
	F.Run(0x0e, 1.f);                                                    // 0x10284c87
	TestEqual(TEXT("mode 1: FindLosPos has no source -> 0xb"), F.Reason(), 0x0b);
	F.Run(0x0e, 2.f);                                                    // 0x10284b7f
	TestEqual(TEXT("mode 2: the cover miss fails 8 THEN 0xc, the second reason standing"), F.Reason(), 0x0c);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The facing arms: 0x27, 0x28, 0x2e, 0x2f, 0x59, 0x5a.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19BaseFacingTest,
	"Elysium.Substrate.NpcKernelStartTask19.Base.Facing_0x10283ae3", GStartTask19BaseFlags)
bool FElysiumNpcKernelStartTask19BaseFacingTest::RunTest(const FString&)
{
	FStartTask19BaseFixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard))
	{
		return false;
	}
	F.Guard->BaseScheduleHost.bMotorAnimationMovement = false;
	F.Guard->Angles = FVector(0.0, 30.0, 0.0);
	const float Current = FElysiumNpcBase::StartTaskAngleMod(30.f);

	const int32 Holds = F.Guard->StartTaskNav.MotorYawHolds;
	F.Run(0x2b);                                                         // 0x10283cd5
	TestEqual(TEXT("FACE_IDEAL holds the yaw"), F.Guard->StartTaskNav.MotorYawHolds, Holds + 1);
	TestFalse(TEXT("FACE_IDEAL does not complete"), F.Completed());

	F.Run(0x33);                                                         // 0x10283ae3
	TestEqual(TEXT("SET_IDEAL_YAW_TO_CURRENT"), F.Guard->MotorIdealYaw, Current);
	TestTrue(TEXT("... completes"), F.Completed());

	F.Run(0x6a, 20.f);                                                   // 0x10282926
	TestEqual(TEXT("TURN_LEFT adds"), F.Guard->MotorIdealYaw, FElysiumNpcBase::StartTaskAngleMod(Current + 20.f));
	F.Run(0x6b, 20.f);                                                   // 0x10282848
	TestEqual(TEXT("TURN_RIGHT subtracts"), F.Guard->MotorIdealYaw, FElysiumNpcBase::StartTaskAngleMod(Current - 20.f));
	TestFalse(TEXT("the turns do not complete"), F.Completed());

	// The +0x28 flip: +180 under 180.
	F.Guard->BaseScheduleHost.bMotorAnimationMovement = true;
	F.Run(0x33);
	TestEqual(TEXT("the animation-movement latch flips the stored yaw"), F.Guard->MotorIdealYaw, Current + 180.f);
	F.Guard->BaseScheduleHost.bMotorAnimationMovement = false;

	// FACE_PATH with no route: DevWarning and 0xc.
	F.Run(0x2c);                                                         // 0x10283cf7
	TestEqual(TEXT("FACE_PATH with no route: 0xc"), F.Reason(), 0x0c);
	TestEqual(TEXT("... after its DevWarning"), F.Guard->StartTaskNav.LastDevMessage, FString(TEXT("No route to face!\n")));

	F.Guard->LastPosition = FVector(0.0, 300.0, 0.0);
	F.Run(0x32);                                                         // 0x10283aad
	TestEqual(TEXT("FACE_LASTPOSITION ideal yaw"), F.Guard->MotorIdealYaw, F.Guard->CalcIdealYaw(FVector(0.0, 300.0, 0.0)));
	TestFalse(TEXT("... no completion"), F.Completed());
	return true;
}

// -------------------------------------------------------------------------------------------------
// The hint arms: 0x2b, 0x2c, 0x37, 0x38, 0x39, 0x12.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19BaseHintsTest,
	"Elysium.Substrate.NpcKernelStartTask19.Base.Hints_0x10282a17", GStartTask19BaseFlags)
bool FElysiumNpcKernelStartTask19BaseHintsTest::RunTest(const FString&)
{
	FStartTask19BaseFixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard))
	{
		return false;
	}
	F.Guard->BaseScheduleHost.HintNode = INDEX_NONE;
	F.Run(0x40, 3.f);                                                    // 0x10282a17
	TestEqual(TEXT("FIND_HINTNODE with no hint: 4"), F.Reason(), 0x04);
	const int32 Locks = F.Guard->StartTaskNav.HintLockAttempts;
	F.Run(0x41, 3.f);
	TestEqual(TEXT("FIND_LOCK_HINTNODE fails 4 and falls into the lock"), F.Reason(), 0x04);
	TestEqual(TEXT("... whose own null test fails before the claim"), F.Guard->StartTaskNav.HintLockAttempts, Locks);
	F.Run(0x43);                                                         // 0x10282a79
	TestEqual(TEXT("LOCK_HINTNODE with no hint: 4"), F.Reason(), 0x04);
	F.Run(0x16);                                                         // 0x10285a9e
	TestEqual(TEXT("GET_PATH_TO_HINTNODE with no hint: 4"), F.Reason(), 0x04);

	// A held (unresolvable) hint: the claim seam refuses, so 0x11 and the hint is dropped.
	F.Guard->BaseScheduleHost.HintNode = 12345;
	F.Run(0x43);
	TestEqual(TEXT("LOCK_HINTNODE refused: 0x11"), F.Reason(), 0x11);
	TestEqual(TEXT("... and the failure drops the hint"), F.Guard->BaseScheduleHost.HintNode, INDEX_NONE);

	F.Guard->BaseScheduleHost.HintNode = 12345;
	F.Run(0x40, 3.f);
	TestTrue(TEXT("FIND_HINTNODE with a hint held completes at once"), F.Completed());
	F.Run(0x42);                                                         // 0x10282d44
	TestEqual(TEXT("CLEAR_HINTNODE drops the hint"), F.Guard->BaseScheduleHost.HintNode, INDEX_NONE);
	TestTrue(TEXT("... and completes"), F.Completed());

	const int32 Guards = F.Guard->StartTaskNav.CrashGuards;
	F.Run(0x30);                                                         // 0x10282dfb
	F.Run(0x2f);                                                         // 0x10283a36
	TestEqual(TEXT("the unguarded hint reads stop at the crash guard"), F.Guard->StartTaskNav.CrashGuards, Guards + 2);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The activity and attack arms: 0x26, 0x30..0x36, 0x40, 0x46, 0x60.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19BaseActivitiesTest,
	"Elysium.Substrate.NpcKernelStartTask19.Base.Activities_0x10284286", GStartTask19BaseFlags)
bool FElysiumNpcKernelStartTask19BaseActivitiesTest::RunTest(const FString&)
{
	FStartTask19BaseFixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard))
	{
		return false;
	}
	const double Now = F.Now();
	for (const int32 TaskId : { 0x34, 0x35, 0x36, 0x37, 0x39, 0x3a, 0x3b, 0x3c })
	{
		F.Guard->LastAttackTime = -1.0;
		F.Run(TaskId);
		TestEqual(FString::Printf(TEXT("0x%x stamps m_flLastAttackTime"), TaskId), F.Guard->LastAttackTime, Now);
		TestFalse(FString::Printf(TEXT("0x%x does not complete"), TaskId), F.Completed());
	}
	for (const int32 TaskId : { 0x38, 0x3d, 0x3e, 0x3f })
	{
		F.Guard->LastAttackTime = -1.0;
		F.Run(TaskId);
		TestEqual(FString::Printf(TEXT("0x%x does not stamp"), TaskId), F.Guard->LastAttackTime, -1.0);
	}
	F.Guard->LastHitGroup = 1;
	F.Run(0x2a);                                                         // 0x102867e5
	TestEqual(TEXT("SMALL_FLINCH falls back to 0x49 with no sequence"), F.Guard->IdealActivityNumber, 0x49);
	TestFalse(TEXT("... no completion"), F.Completed());
	F.Run(0x71);                                                         // 0x10286df5
	TestEqual(TEXT("WEAPON_PICKUP asks for 0x5c"), F.Guard->IdealActivityNumber, 0x5c);
	F.Run(0x52);                                                         // 0x10282dde
	TestFalse(TEXT("PLAY_SEQUENCE does not complete"), F.Completed() || F.Failed());
	F.Run(0x4b);                                                         // 0x10284311
	TestFalse(TEXT("SET_ACTIVITY does not complete"), F.Completed() || F.Failed());
	return true;
}

// -------------------------------------------------------------------------------------------------
// The movement-state arms: 0x4f, 0x58, 0x5d, 0x5e, 0x65.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19BaseMovementTest,
	"Elysium.Substrate.NpcKernelStartTask19.Base.Movement_0x10286749", GStartTask19BaseFlags)
bool FElysiumNpcKernelStartTask19BaseMovementTest::RunTest(const FString&)
{
	FStartTask19BaseFixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard))
	{
		return false;
	}
	F.Guard->BaseScheduleHost.bShouldMove = true;
	F.Run(0x69);                                                         // 0x10282d71
	TestFalse(TEXT("STOP_MOVING with no goal clears m_bShouldMove"), F.Guard->BaseScheduleHost.bShouldMove);
	TestTrue(TEXT("... and completes"), F.Completed());

	F.Guard->BaseScheduleHost.bShouldMove = true;
	const int32 Clears = F.Guard->StartTaskNav.ClearGoalCalls;
	F.Run(0x6e);                                                         // 0x10286749
	TestFalse(TEXT("WAIT_FOR_MOVEMENT with no waypoint stops"), F.Guard->BaseScheduleHost.bShouldMove);
	TestTrue(TEXT("... completes"), F.Completed());
	TestEqual(TEXT("... and clears the goal"), F.Guard->StartTaskNav.ClearGoalCalls, Clears + 1);

	F.Guard->BaseScheduleHost.bShouldMove = true;
	F.Run(0x6f);                                                         // 0x102866bf
	TestFalse(TEXT("WAIT_FOR_MOVEMENT_STEP with no goal stops"), F.Guard->BaseScheduleHost.bShouldMove);
	TestTrue(TEXT("... completes"), F.Completed());

	for (const int32 TaskId : { 0x5f, 0xdf })                            // 0x10286801
	{
		F.Guard->LifeState = 0;
		F.Run(TaskId);
		TestEqual(FString::Printf(TEXT("0x%x writes LIFE_DYING"), TaskId), F.Guard->LifeState, 1);
		TestFalse(FString::Printf(TEXT("0x%x parks the program"), TaskId), F.Completed() || F.Failed());
	}

	F.Guard->SequencePlaybackRate = 1.f;
	F.Run(0x77);                                                         // 0x102869a6
	TestEqual(TEXT("FREEZE stops the playback rate"), F.Guard->SequencePlaybackRate, 0.f);
	TestFalse(TEXT("... and never completes"), F.Completed());
	return true;
}

// -------------------------------------------------------------------------------------------------
// The weapon and navigator-search arms: 0x1b, 0x43, 0x5f, 0x64, 0x66..0x68.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19BaseSearchesTest,
	"Elysium.Substrate.NpcKernelStartTask19.Base.Searches_0x10286ec2", GStartTask19BaseFlags)
bool FElysiumNpcKernelStartTask19BaseSearchesTest::RunTest(const FString&)
{
	FStartTask19BaseFixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("other"), F.Other))
	{
		return false;
	}
	F.Guard->SetTarget(F.Other->Handle);
	F.Run(0x70);                                                         // 0x10286d78
	TestEqual(TEXT("WEAPON_FIND with nothing found: 3"), F.Reason(), 0x03);
	TestFalse(TEXT("... after SetTarget(NULL) cleared the target"), F.Guard->GetTarget().IsSet());

	F.Run(0x9f, 0.5f);                                                   // 0x10286c9f
	TestEqual(TEXT("SET_MELEE_TOLERANCE with no weapon: 3"), F.Reason(), 0x03);
	F.Run(0xac);                                                         // 0x10286e2a
	TestEqual(TEXT("CHOOSE_BEST_MELEE_WEAPON: 0x1f"), F.Reason(), 0x1f);
	F.Run(0xad);                                                         // 0x10286e76
	TestEqual(TEXT("CHOOSE_BEST_RANGED_WEAPON: 0x1f"), F.Reason(), 0x1f);

	F.Run(0x76, 20050.f);                                                // 0x10286ec2
	TestEqual(TEXT("WANDER splits the operand: min = n / 10000"), F.Guard->StartTaskNav.LastSearchMinUnits, 2.f);
	TestEqual(TEXT("... max = n % 10000"), F.Guard->StartTaskNav.LastSearchMaxUnits, 50.f);
	// The radial probe `0x102ed610` is story 5's seam, so it falls back to `SetRandomGoal(1.0,
	// vec3_origin)` -- the capped point pick, which no place answers in this world (it has no
	// network): 0x18.
	TestEqual(TEXT("... the fallback pick is asked with an order of 1.0"), F.Guard->StartTaskNav.LastRandomGoalOrderUnits, 1.f);
	TestEqual(TEXT("... and finds no place: 0x18"), F.Reason(), 0x18);
	F.Run(0x1f, 200.f);                                                  // 0x10285d7f
	TestEqual(TEXT("RANDOM_NODE asks the pick with the resolved order"), F.Guard->StartTaskNav.LastRandomGoalOrderUnits, 200.f);
	TestEqual(TEXT("RANDOM_NODE with no network: 0x18 (the pick itself: Elysium.Substrate.PlaceSeams.Wander.*)"),
		F.Reason(), 0x18);

	F.Run(0x4e, 120.f);                                                  // 0x10286c69
	// `0x10286c84` -> `0x102ee1c0`: `MOV EAX,[ECX+0x30]; MOV [EAX+0x28],ECX` -- the PATH's tolerance,
	// not `m_flGoalTolerance` (`+0x6320`), which this arm never writes.
	TestEqual(TEXT("SET_TOLERANCE_DISTANCE writes the path tolerance"),
		F.Guard->Navigator.GoalToleranceCm, F.Guard->ResolveTaskDistance(120.f) * ElysiumMove::U);
	TestTrue(TEXT("... and completes"), F.Completed());
	return true;
}

// -------------------------------------------------------------------------------------------------
// The remaining path arms: 0x0f, 0x18, 0x19, 0x1c, 0x1d, 0x63, 0x69, and TASK_SET_SCHEDULE 0x41.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19BaseRoutesTest,
	"Elysium.Substrate.NpcKernelStartTask19.Base.Routes_0x10285ff8", GStartTask19BaseFlags)
bool FElysiumNpcKernelStartTask19BaseRoutesTest::RunTest(const FString&)
{
	FStartTask19BaseFixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("player"), F.Player))
	{
		return false;
	}
	F.Run(0x13);                                                         // 0x10285387
	TestTrue(TEXT("GET_PATH_TO_PLAYER aims at the player"), F.Guard->StartTaskNav.LastGoal.Target == F.Player->Handle);
	TestEqual(TEXT("... type 4"), F.Guard->StartTaskNav.LastGoal.Type, 4);

	F.Guard->SavePosition = FVector(11.0, 12.0, 13.0);
	F.Run(0x1d);                                                         // 0x10285cb9
	TestEqual(TEXT("GET_PATH_TO_SAVEPOSITION dest"), F.Guard->StartTaskNav.LastGoal.DestCm, FVector(11.0, 12.0, 13.0));

	F.Guard->LastPosition = FVector(21.0, 22.0, 23.0);
	F.Run(0x1c);                                                         // 0x10285bb0
	TestEqual(TEXT("GET_PATH_TO_LASTPOSITION dest"), F.Guard->StartTaskNav.LastGoal.DestCm, FVector(21.0, 22.0, 23.0));
	TestEqual(TEXT("... a refused route: 0xc"), F.Reason(), 0x0c);

	F.Run(0x21);                                                         // 0x10285ef8
	TestEqual(TEXT("GET_PATH_TO_BESTSCENT with no scent: 0x13"), F.Reason(), 0x13);
	F.Guard->Senses.Memory.BestSound.Position = FVector(31.0, 32.0, 33.0);
	F.Run(0x20);                                                         // 0x10285df8
	TestEqual(TEXT("GET_PATH_TO_BESTSOUND dest"), F.Guard->StartTaskNav.LastGoal.DestCm, FVector(31.0, 32.0, 33.0));

	F.Guard->Target.Empty();
	F.Run(0x120);                                                        // 0x10285ff8
	TestEqual(TEXT("PATHCORNER with no m_target fails with the SCENT code"), F.Reason(), 0x13);
	F.Guard->Target = TEXT("corner");
	const int32 Guards = F.Guard->StartTaskNav.CrashGuards;
	F.Run(0x120);
	TestEqual(TEXT("PATHCORNER with no m_pGoalEnt stops at the crash guard"), F.Guard->StartTaskNav.CrashGuards, Guards + 1);

	F.Run(0x75);                                                         // 0x10284e4a
	TestEqual(TEXT("DROPSHIP refused: the \"gah\" string is the reason"), F.Reason(), 0x105cdfe4);
	TestFalse(TEXT("... and its TaskComplete is blocked by the failure"), F.Completed());

	F.Guard->Schedule.Clear();
	F.Run(0x4c, 1.f);                                                    // 0x10282e27
	TestEqual(TEXT("SET_SCHEDULE stamps the UNtranslated id"), F.Guard->BaseScheduleHost.IdealScheduleRetail, 1);
	TestTrue(TEXT("... installs a program"), F.Guard->Schedule.IsRunning());
	TestFalse(TEXT("... and does not complete"), F.Completed());
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
