// Story 0019/8 (29e under the strict verdict), family **StartTask19** -- the family's tests, second
// part.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Test names carry `Elysium.Substrate.NpcKernelStartTask19.` and the retail address.
//
// Owns (StartTask19's `rule` rows): 0x102a1910 (the arms past the cut).
//
// Lane L02: one case per arm group of `CAI_BaseNPCTroika::StartTask 0x102a1910` in
// `[0x102a5046, 0x102a77f7]`. Every case drives `FElysiumNpc::StartTaskTroikaTail` directly, so no
// species override and no first-part dispatch stands between the step and the arm. Every expected
// value is read off the listing address named beside it.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumLocalIdSpace.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumScheduleText.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumNpcTestCensus.h"

static constexpr EAutomationTestFlags GStartTask19TailFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// One world: the subject (a plain Troika-line human) and a second body a case can point a
	// handle at.
	struct FStartTask19TailFixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Npc = nullptr;
		FElysiumNpc* Other = nullptr;

		FStartTask19TailFixture()
			: World([]
				{
					FElysiumNpcWorldBuilder Builder(TEXT("starttask19_tail"), 1919);
					Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
					Builder.AddNpcOfClass(TEXT("subject"), FVector::ZeroVector, TEXT("CNPC_VHumanCombatant"));
					Builder.AddNpc(TEXT("other"), FVector(300.f, 0.f, 0.f), TEXT("npc_VHumanCombatant"));
					return Builder;
				}())
		{
			Npc = World.Npc(TEXT("subject"));
			Other = World.Npc(TEXT("other"));
			FElysiumNpcWorldFixture::Quiet({ Npc, Other });
		}

		double Now() const { return World.World.NowSeconds(); }

		// The class-LOCAL task (the registrar's number the switch compares) as the GLOBAL id a schedule
		// step carries (`ElysiumScheduleText.h`); the body translates it back (slot 450's body).
		int32 GlobalTask(int32 LocalTask) const
		{
			const FElysiumLocalIdSpace* Space = Npc->IdSpace(EElysiumIdCategory::Task);
			return Space != nullptr ? Space->LocalToGlobal(LocalTask) : LocalTask;
		}

		// A fresh task: RUNNING, no raised failure.
		int32 Run(int32 TaskId, float Data = 0.f)
		{
			FElysiumScheduleStep Step;
			Step.TaskId = GlobalTask(TaskId);
			Step.Data = Data;
			return RunStep(Step);
		}
		int32 RunRaw(int32 TaskId, uint32 Raw)
		{
			FElysiumScheduleStep Step;
			Step.TaskId = GlobalTask(TaskId);
			Step.SetRawWord(Raw);
			return RunStep(Step);
		}
		int32 RunStep(const FElysiumScheduleStep& Step)
		{
			Npc->Schedule.TaskStatus = EElysiumTaskStatus::Running;
			Npc->Cognition.Conditions.Clear(EElysiumNpcCond::TaskFailed);
			Npc->BaseScheduleHost.FailureReason = 0;
			return Npc->StartTaskTroikaTail(const_cast<FElysiumScheduleStep*>(&Step));
		}

		bool Completed() const { return Npc->Schedule.TaskStatus == EElysiumTaskStatus::Complete; }
		int32 Failure() const { return Npc->BaseScheduleHost.FailureReason; }
	};
}

// -------------------------------------------------------------------------------------------------
// The shared tails: `0x102a77ea` (index 0x00) and `0x102a66d7` (index 0x1d).
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19TailSharedTailsTest,
	"Elysium.Substrate.NpcKernelStartTask19.TroikaTail.SharedTails_102a77ea_102a66d7", GStartTask19TailFlags)
bool FElysiumNpcKernelStartTask19TailSharedTailsTest::RunTest(const FString&)
{
	FStartTask19TailFixture F;
	if (!TestNotNull(TEXT("subject"), F.Npc))
	{
		return false;
	}
	// `0x102a77ea`: TASK_WAIT_PVS 0x05, TASK_DIE_EXPLODE_GIB 0xea, TASK_DIE_DUE_TO_PLAYER 0xeb -- a
	// bare return, the task left RUNNING.
	for (const int32 Id : { 0x05, 0xea, 0xeb })
	{
		F.Run(Id);
		TestFalse(FString::Printf(TEXT("0x%x stays running (0x102a77ea)"), Id), F.Completed());
		TestEqual(FString::Printf(TEXT("0x%x raises no failure"), Id), F.Failure(), 0);
	}
	// `0x102a66d7`: TASK_MELEE_DODGE 0x8b and TASK_SET_SHOOT_TARGET_OVERRIDE 0x125 complete.
	for (const int32 Id : { 0x8b, 0x125 })
	{
		F.Run(Id);
		TestTrue(FString::Printf(TEXT("0x%x completes (0x102a66d7)"), Id), F.Completed());
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// The knockback, finishing-move and on-fire arms (indices 0x1f..0x2a).
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19TailKnockbackTest,
	"Elysium.Substrate.NpcKernelStartTask19.TroikaTail.Knockback_102a6de4", GStartTask19TailFlags)
bool FElysiumNpcKernelStartTask19TailKnockbackTest::RunTest(const FString&)
{
	FStartTask19TailFixture F;
	if (!TestNotNull(TEXT("subject"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& Npc = *F.Npc;

	// 0x92 `0x102a6de4`: `RestartIdealActivity(m_knockbackType)` (`0x102a6df7`, `0x10289ee0`), no
	// completion.
	Npc.KnockbackType = 0x57;
	F.Run(0x92);
	TestEqual(TEXT("0x102a6df7 the ideal activity is m_knockbackType"), Npc.IdealActivityNumber, 0x57);
	TestFalse(TEXT("0x92 leaves the task running"), F.Completed());

	// 0x93 `0x102a6e09`: gravity from `m_fJumpGravity`, the jump nav type and `m_bJumping`.
	Npc.JumpGravity = 0.75f;
	Npc.bJumping = false;
	F.Run(0x93);
	TestEqual(TEXT("0x102a6e1c m_flGravity = m_fJumpGravity"), Npc.Gravity, 0.75f);
	TestEqual(TEXT("0x102a6e47 nav type 1"), Npc.Navigator.NavType, 1);
	TestTrue(TEXT("0x102a6e50 m_bJumping"), Npc.bJumping);
	TestFalse(TEXT("0x93 does not complete"), F.Completed());

	// 0x95 / 0x98 `0x102a6ef7`: the impact dust dispatch.
	const int32 DustBefore = Npc.TaskTailParticleDispatches.Num();
	F.Run(0x95);
	F.Run(0x98);
	TestEqual(TEXT("0x102a6f04 two dust dispatches"), Npc.TaskTailParticleDispatches.Num(), DustBefore + 2);
	TestEqual(TEXT("0x105da1e8 the literal"), Npc.TaskTailParticleDispatches.Last(), FString(TEXT("impact_dust_emitter")));

	// 0x96 `0x102a6eb4`: `m_fKnockbackWallHitFallTime = curtime + 0.01`.
	F.Run(0x96);
	TestEqual(TEXT("0x102a6ec2 +0x6014"), Npc.KnockbackWallHitFallTime, F.Now() + 0.01, 1e-4);

	// 0x99 `0x102a6f16`: the bone-track stamp.
	Npc.FinishingMoveBoneTrackLastTime = -1.0;
	F.Run(0x99);
	TestEqual(TEXT("0x102a6f58 +0x6018 = curtime"), Npc.FinishingMoveBoneTrackLastTime, F.Now(), 1e-6);

	// 0x9b `0x102a6fc5`: `m_flLastMeleeStepbackTime = curtime`, then the break tail.
	Npc.LastMeleeStepbackTime = -1.0;
	F.Run(0x9b);
	TestEqual(TEXT("0x102a6fcd +0x606c"), Npc.LastMeleeStepbackTime, F.Now(), 1e-6);
	TestTrue(TEXT("0x9b completes"), F.Completed());

	// 0x9c / 0x9d / 0x9e: `SetIdealActivity(0x1082 / 0x1083 / 0x1084)`.
	F.Run(0x9c);
	TestEqual(TEXT("0x102a6f81 ACT 0x1082"), Npc.IdealActivityNumber, 0x1082);
	F.Run(0x9d);
	TestEqual(TEXT("0x102a6f9a ACT 0x1083"), Npc.IdealActivityNumber, 0x1083);
	F.Run(0x9e);
	TestEqual(TEXT("0x102a6fb3 ACT 0x1084"), Npc.IdealActivityNumber, 0x1084);
	TestFalse(TEXT("the on-fire arms do not complete"), F.Completed());
	return true;
}

// -------------------------------------------------------------------------------------------------
// The interest arms (indices 0x38..0x3e).
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19TailInterestTest,
	"Elysium.Substrate.NpcKernelStartTask19.TroikaTail.Interest_102a634a", GStartTask19TailFlags)
bool FElysiumNpcKernelStartTask19TailInterestTest::RunTest(const FString&)
{
	FStartTask19TailFixture F;
	if (!TestNotNull(TEXT("subject"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& Npc = *F.Npc;
	AddExpectedError(TEXT("TaskFail"), EAutomationExpectedErrorFlags::Contains, 0);

	// 0xb2 `0x102a634a`: no current place -> line 0x3848, `TaskFail(0x22)`.
	F.Run(0xb2);
	TestEqual(TEXT("0x102a6396 fail 0x22"), F.Failure(), 0x22);

	// 0xb4 `0x102a644e`: `m_iInterestingDeathActivity = -1` FIRST, then the same failure.
	Npc.InterestingDeathActivity = 7;
	F.Run(0xb4);
	TestEqual(TEXT("0x102a6454 +0x6308 = -1"), Npc.InterestingDeathActivity, INDEX_NONE);
	TestEqual(TEXT("0x102a647f fail 0x22"), F.Failure(), 0x22);

	// 0xb3 / 0xb5: no patrol place -> complete.
	F.Run(0xb3);
	TestTrue(TEXT("0x102a643c 0xb3 no place completes"), F.Completed());
	F.Run(0xb5);
	TestTrue(TEXT("0x102a64c4 0xb5 no place completes"), F.Completed());

	// 0xb6 / 0xb7: the translated activity into the navigator (`0x102ee250`); no completion.
	F.Run(0xb6);
	TestEqual(TEXT("0x102a64f9 ACT_IDLE"), Npc.ScheduleHost.NavigationActivity, 1);
	F.Run(0xb7);
	TestEqual(TEXT("0x102a64f9 ACT_WALK"), Npc.ScheduleHost.NavigationActivity, 9);
	TestFalse(TEXT("0xb7 does not complete"), F.Completed());

	// 0xb8 `0x102a650b`: -1 clears INTERESTING_INTO and completes; a stored activity restarts.
	Npc.NpcFlags.Set(EElysiumNpcFlag::INTERESTING_INTO);
	Npc.InterestingDeathActivity = INDEX_NONE;
	F.Run(0xb8);
	TestFalse(TEXT("0x102a6530 INTERESTING_INTO cleared"), Npc.NpcFlags.Has(EElysiumNpcFlag::INTERESTING_INTO));
	TestTrue(TEXT("0x102a6539 completes"), F.Completed());
	Npc.InterestingDeathActivity = 0x1d;
	F.Run(0xb8);
	TestEqual(TEXT("0x102a6519 RestartIdealActivity(+0x6308)"), Npc.IdealActivityNumber, 0x1d);
	TestFalse(TEXT("0x102a6519 a stored activity leaves the task running"), F.Completed());
	return true;
}

// -------------------------------------------------------------------------------------------------
// The activity arms of chunk 4 (indices 0x59..0x61).
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19TailActivitiesTest,
	"Elysium.Substrate.NpcKernelStartTask19.TroikaTail.Activities_102a5046", GStartTask19TailFlags)
bool FElysiumNpcKernelStartTask19TailActivitiesTest::RunTest(const FString&)
{
	FStartTask19TailFixture F;
	if (!TestNotNull(TEXT("subject"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& Npc = *F.Npc;

	// 0xe4 `0x102a5046`: with no sequence (`SelectWeightedSequence < 0`, `0x102a505c JL`) the break
	// tail completes and NO gesture is added.
	const int32 GesturesBefore = Npc.TaskTailGestureCalls;
	F.Run(0xe4, 5.f);
	TestTrue(TEXT("0x102a505c completes"), F.Completed());
	TestEqual(TEXT("no gesture on the miss arm"), Npc.TaskTailGestureCalls, GesturesBefore);

	// 0xe6 `0x102a5125`: ACT_COWER_INTO rerolls the offset in {0, 3, 6}; SetIdealActivity(act + off).
	F.Run(0xe6, static_cast<float>(0x1097));
	const int32 Offset = Npc.CowerAnimOffset;
	TestTrue(TEXT("0x102a5149 offset is 0, 3 or 6"), Offset == 0 || Offset == 3 || Offset == 6);
	TestEqual(TEXT("0x102a515a act + offset"), Npc.IdealActivityNumber, 0x1097 + Offset);
	// Another cower activity reuses the stored offset.
	F.Run(0xe6, static_cast<float>(0x1098));
	TestEqual(TEXT("0x102a514f reuse of +0x6414"), Npc.IdealActivityNumber, 0x1098 + Offset);
	TestFalse(TEXT("0xe6 does not complete"), F.Completed());

	// 0xe7 `0x102a516c`: the same draw rule, no completion.
	Npc.CowerAnimOffset = 3;
	F.Run(0xe7, static_cast<float>(0x1098));
	TestEqual(TEXT("0x102a5196 the stored offset stands"), Npc.CowerAnimOffset, 3);
	TestFalse(TEXT("0xe7 does not complete"), F.Completed());

	// 0xec `0x102a51b3`: ACT_COMFORT_INTO or ACT_COMFORT2_INTO.
	F.Run(0xec);
	TestTrue(TEXT("0x102a51c2 0x106a or 0x106d"),
		Npc.IdealActivityNumber == 0x106a || Npc.IdealActivityNumber == 0x106d);

	// 0xed / 0xee `0x102a51e8`: -1 -> m_Activity = 0; else SetIdealActivity(m_Activity + 1).
	Npc.ActivityNumber = -1;
	F.Run(0xed);
	TestEqual(TEXT("0x102a5206 m_Activity = 0"), Npc.ActivityNumber, 0);
	Npc.ActivityNumber = 0x106a;
	F.Run(0xee);
	TestEqual(TEXT("0x102a51f4 m_Activity + 1"), Npc.IdealActivityNumber, 0x106b);

	// 0xef / 0xf0: the resist table on `m_iLastDisciplineHitBy` (+0xfd4).
	const int32 HitBy[] = { 5, 6, 12, 0 };
	const int32 Partial[] = { 0x108d, 0x108e, 0x108f, 0x108d };
	const int32 Full[] = { 0x1090, 0x1091, 0x1092, 0x1090 };
	for (int32 Row = 0; Row < 4; ++Row)
	{
		Npc.LastDisciplineHitBy = HitBy[Row];
		F.Run(0xef);
		TestEqual(FString::Printf(TEXT("0x102a521d hit by %d"), HitBy[Row]), Npc.IdealActivityNumber, Partial[Row]);
		F.Run(0xf0);
		TestEqual(FString::Printf(TEXT("0x102a5283 hit by %d"), HitBy[Row]), Npc.IdealActivityNumber, Full[Row]);
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// The word and service arms of chunk 4 (indices 0x5a, 0x5d, 0x62..0x67).
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19TailWordsTest,
	"Elysium.Substrate.NpcKernelStartTask19.TroikaTail.Words_102a52e9", GStartTask19TailFlags)
bool FElysiumNpcKernelStartTask19TailWordsTest::RunTest(const FString&)
{
	FStartTask19TailFixture F;
	if (!TestNotNull(TEXT("subject"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& Npc = *F.Npc;

	// 0xf1 `0x102a52e9`: `m_knockbackType = (int)operand`, complete.
	F.Run(0xf1, 3.9f);
	TestEqual(TEXT("0x102a52f5 __ftol truncates"), Npc.KnockbackType, 3);
	TestTrue(TEXT("0x102a52fb completes"), F.Completed());

	// 0xe8 `0x102a778e`: `m_lifeState = 1`, then the break tail.
	F.Run(0xe8);
	TestEqual(TEXT("0x102a778e +0x200 = LIFE_DYING"), Npc.LifeState, 1);
	TestTrue(TEXT("0x102a7798 completes"), F.Completed());

	// 0xe5 `0x102a5087`: the self-damage completes.
	F.Run(0xe5, 1.f);
	TestTrue(TEXT("0x102a5113 completes"), F.Completed());

	// 0xf2 / 0xf3 complete.
	F.Run(0xf2);
	TestTrue(TEXT("0x102a531b completes"), F.Completed());
	F.Run(0xf3);
	TestTrue(TEXT("0x102a533b completes"), F.Completed());

	// 0xf4 `0x102a534d`: `0x102b52a0(this, 1, 1)`, complete.
	// `0x102b52a0` (family Boss19's `ResetAiState`): `m_afMemory &= 0xf7fc7fff` (`0x102b52c5`) and,
	// with its second argument 1, `m_IdealNPCState = IDLE` (`0x102b5305`).
	Npc.BaseScheduleHost.MemoryBits = 0xffffffffu;
	F.Run(0xf4);
	TestEqual(TEXT("0x102a5353 the reset strips m_afMemory"), Npc.BaseScheduleHost.MemoryBits, 0xf7fc7fffu);
	TestEqual(TEXT("0x102b5305 the ideal state goes IDLE"), Npc.IdealStateRetail(), 1);
	TestTrue(TEXT("0x102a535c completes"), F.Completed());

	// 0xf5 `0x102a536e`: the disconnect and the RAW word-two OR, routing bit included.
	const int32 DisconnectsBefore = Npc.BaseScheduleHost.SquadDisconnected;
	F.Run(0xf5);
	TestEqual(TEXT("0x102a5370 ++m_iSquadDisconnected"), Npc.BaseScheduleHost.SquadDisconnected, DisconnectsBefore + 1);
	TestTrue(TEXT("0x102a537c D_DISCONNECT_SQUAD"), Npc.NpcFlags.Has(EElysiumNpcFlag2::D_DISCONNECT_SQUAD));
	TestTrue(TEXT("0x102a537c word-two bit 31 written"), Npc.NpcFlags.HasRawWord2Bits(0x80000000u));
	TestTrue(TEXT("0x102a5385 completes"), F.Completed());

	// 0xf6 `0x102a5397`: the act and the danger sound, complete.
	const int32 ActsBefore = Npc.TaskTailSupernaturalActs;
	F.Run(0xf6, 4.f);
	TestEqual(TEXT("0x102a53cf the act"), Npc.TaskTailSupernaturalActs, ActsBefore + 1);
	TestEqual(TEXT("its duration is the operand"), Npc.TaskTailLastSupernaturalActDuration, 4.f);
	TestTrue(TEXT("0x102a5414 completes"), F.Completed());
	return true;
}

// -------------------------------------------------------------------------------------------------
// The look arms (indices 0x68..0x6d, 0xad) and the turn-outs (0x6e, 0x6f).
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19TailLooksTest,
	"Elysium.Substrate.NpcKernelStartTask19.TroikaTail.Looks_102a5426", GStartTask19TailFlags)
bool FElysiumNpcKernelStartTask19TailLooksTest::RunTest(const FString&)
{
	FStartTask19TailFixture F;
	if (!TestNotNull(TEXT("subject"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& Npc = *F.Npc;
	AddExpectedError(TEXT("TaskFail"), EAutomationExpectedErrorFlags::Contains, 0);

	// 0xf7 `0x102a5426`: no route -> line 0x3599, `TaskFail(0xc)`.
	F.Run(0xf7);
	TestEqual(TEXT("0x102a5462 fail 0xc"), F.Failure(), 0xc);

	// 0xf8 `0x102a5515`: slot 474 always answers `m_BestSound`, so the look runs: the face flag and
	// `m_flWaitFinished = curtime + operand` (`0x102a18a0`).
	Npc.NpcFlags.Clear(EElysiumNpcFlag::PLAYING_FACE_ANIM);
	F.Run(0xf8, 2.f);
	TestTrue(TEXT("0x10297987 PLAYING_FACE_ANIM"), Npc.NpcFlags.Has(EElysiumNpcFlag::PLAYING_FACE_ANIM));
	TestEqual(TEXT("0x102a18c1 +0x5db4"), Npc.BaseScheduleHost.WaitFinished, F.Now() + 2.0, 1e-4);
	TestFalse(TEXT("the look leaves the task running"), F.Completed());

	// 0xfa `0x102a555c`: no fail path.
	F.Run(0xfa, 1.f);
	TestEqual(TEXT("0xfa raises no failure"), F.Failure(), 0);

	// 0xfb / 0xfc / 0xfd / 0x148: no player, no unknown, no detected attacker, no damage attacker.
	Npc.Senses.Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();
	F.Run(0xfb);
	TestEqual(TEXT("0x102a55d0 fail 0x17"), F.Failure(), 0x17);
	Npc.Senses.Memory.BestSeeUnknown = FElysiumEntityHandle::Invalid();
	Npc.Senses.Memory.LastSeeUnknown = FElysiumEntityHandle::Invalid();
	F.Run(0xfc);
	TestEqual(TEXT("0x102a564f fail 0x21"), F.Failure(), 0x21);
	Npc.Senses.Memory.DetectedAttackAttacker = FElysiumEntityHandle::Invalid();
	F.Run(0xfd);
	TestEqual(TEXT("0x102a568f fail 0x21"), F.Failure(), 0x21);
	Npc.BaseMemory.LastDamageAttacker = FElysiumEntityHandle::Invalid();
	F.Run(0x148);
	TestEqual(TEXT("0x102a777b fail 0x21"), F.Failure(), 0x21);

	// 0xfc with a live LAST unknown: looks at the stored position instead.
	if (TestNotNull(TEXT("other"), F.Other))
	{
		Npc.Senses.Memory.LastSeeUnknown = F.Other->Handle;
		F.Run(0xfc, 1.f);
		TestEqual(TEXT("0x102a5614 the last-unknown arm raises no failure"), F.Failure(), 0);
		Npc.BaseMemory.LastDamageAttacker = F.Other->Handle;
		F.Run(0x148, 1.f);
		TestEqual(TEXT("0x102a7733 a live attacker is looked at"), F.Failure(), 0);
	}

	// 0xfe `0x102a56a2`: m_eFaceAnim 3 -> 0x1102; ideal yaw = abs yaw - m_flFaceYawDiff.
	Npc.BaseScheduleHost.bMotorAnimationMovement = false;
	Npc.Angles = FVector(0.0, 30.0, 0.0);
	Npc.FaceAnim = 3;
	Npc.FaceYawDiff = 10.f;
	F.Run(0xfe, 1.f);
	TestEqual(TEXT("0x102a56c2 ACT 0x1102"), Npc.IdealActivityNumber, 0x1102);
	TestEqual(TEXT("0x102a5727 yaw - diff"), Npc.MotorIdealYaw, 20.f, 1e-4f);
	// 0xff `0x102a5754`: m_eFaceAnim 2 -> 0x1101 and +45.
	Npc.FaceAnim = 2;
	F.Run(0xff, 1.f);
	TestEqual(TEXT("0x102a5774 ACT 0x1101"), Npc.IdealActivityNumber, 0x1101);
	TestEqual(TEXT("0x102a5780 yaw + 45"), Npc.MotorIdealYaw, 75.f, 1e-4f);
	// Out of range -> ACT_IDLE, +0.
	Npc.FaceAnim = 9;
	F.Run(0xff, 1.f);
	TestEqual(TEXT("0x102a5811 ACT_IDLE"), Npc.IdealActivityNumber, 1);
	TestEqual(TEXT("0x102a581a +0"), Npc.MotorIdealYaw, 30.f, 1e-4f);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The flag arms (indices 0x70..0x72) and the movement-activity picks (0x73..0x76, 0x92).
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19TailFlagsTest,
	"Elysium.Substrate.NpcKernelStartTask19.TroikaTail.FlagsAndMovement_102a585d", GStartTask19TailFlags)
bool FElysiumNpcKernelStartTask19TailFlagsTest::RunTest(const FString&)
{
	FStartTask19TailFixture F;
	if (!TestNotNull(TEXT("subject"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& Npc = *F.Npc;

	// 0x100: a positive mask ORs word one; a negative one ORs word two RAW (bit 31 included).
	F.RunRaw(0x100, static_cast<uint32>(EElysiumNpcFlag::NO_DIALOG));
	TestTrue(TEXT("0x102a586b word one"), Npc.NpcFlags.Has(EElysiumNpcFlag::NO_DIALOG));
	TestTrue(TEXT("0x102a5874 completes"), F.Completed());
	F.RunRaw(0x100, 0x80000400u);
	TestTrue(TEXT("0x102a537c word two"), Npc.NpcFlags.Has(EElysiumNpcFlag2::MOVE_FACE_ENEMY));
	TestTrue(TEXT("0x102a537c bit 31 written"), Npc.NpcFlags.HasRawWord2Bits(0x80000000u));
	// 0x101 mirrors it.
	F.RunRaw(0x101, static_cast<uint32>(EElysiumNpcFlag::NO_DIALOG));
	TestFalse(TEXT("0x102a58bc word one cleared"), Npc.NpcFlags.Has(EElysiumNpcFlag::NO_DIALOG));
	TestTrue(TEXT("0x102a58c5 completes"), F.Completed());
	F.RunRaw(0x101, 0x80000400u);
	TestFalse(TEXT("0x102a717b word two cleared"), Npc.NpcFlags.Has(EElysiumNpcFlag2::MOVE_FACE_ENEMY));
	TestFalse(TEXT("0x102a717b bit 31 cleared"), Npc.NpcFlags.HasRawWord2Bits(0x80000000u));
	TestTrue(TEXT("0x102a7180 completes"), F.Completed());
	// 0x102: `AddMiscFlag(1 << (raw & 31))`.
	F.RunRaw(0x102, 5u);
	TestTrue(TEXT("0x102a5893 misc bit 5"), ElysiumMiscFlags::Has(Npc.MiscFlags, 1u << 5));
	TestTrue(TEXT("0x102a589c completes"), F.Completed());

	// 0x103 / 0x104 / 0x105 / 0x126: no sequences here, so every pick lands on its fallback, the
	// navigator's activity takes it, INCOVER is forgotten and the task completes (`0x102a5904`).
	struct FRow { int32 Task; int32 Activity; };
	const FRow Rows[] = { { 0x103, 0x13 }, { 0x104, 9 }, { 0x105, 9 }, { 0x126, 9 } };
	for (const FRow& Row : Rows)
	{
		Npc.BaseScheduleHost.MemoryBits = 0x2u | 0x40u;
		F.Run(Row.Task);
		TestEqual(FString::Printf(TEXT("0x%x navigator activity"), Row.Task), Npc.ScheduleHost.NavigationActivity, Row.Activity);
		TestTrue(FString::Printf(TEXT("0x%x forgets INCOVER only"), Row.Task), Npc.BaseScheduleHost.MemoryBits == 0x40u);
		TestTrue(FString::Printf(TEXT("0x%x completes"), Row.Task), F.Completed());
	}

	// 0x106 `0x102a59eb`: `motor+0x28 = (operand != 0)`.
	F.Run(0x106, 1.f);
	TestTrue(TEXT("0x102a5a2a motor+0x28 set"), Npc.BaseScheduleHost.bMotorAnimationMovement);
	F.Run(0x106, 0.f);
	TestFalse(TEXT("0x102a5a06 motor+0x28 clear"), Npc.BaseScheduleHost.bMotorAnimationMovement);
	TestTrue(TEXT("0x102a5a0f completes"), F.Completed());
	return true;
}

// -------------------------------------------------------------------------------------------------
// The dives (indices 0x77..0x79) and the cover animations (0x7a..0x7d).
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19TailDivesTest,
	"Elysium.Substrate.NpcKernelStartTask19.TroikaTail.DivesAndCover_102a5a45", GStartTask19TailFlags)
bool FElysiumNpcKernelStartTask19TailDivesTest::RunTest(const FString&)
{
	FStartTask19TailFixture F;
	if (!TestNotNull(TEXT("subject"), F.Npc))
	{
		return false;
	}
	AddExpectedError(TEXT("TaskFail"), EAutomationExpectedErrorFlags::Contains, 0);

	// 0x107 `0x102a5a45`: the navigator's goal is unreadable (the zero vector) and the body stands at
	// the origin, so the 2-D range is under 64 and the dive completes.
	F.Run(0x107);
	TestTrue(TEXT("0x102a5ab7 too close completes"), F.Completed());

	// 0x108 / 0x109: neither dive activity has a sequence -> `TaskFail(0xe)`.
	F.Run(0x108);
	TestEqual(TEXT("0x102a5cc1 fail 0xe"), F.Failure(), 0xe);
	F.Run(0x109);
	TestEqual(TEXT("0x102a5dc7 fail 0xe"), F.Failure(), 0xe);

	// 0x10a / 0x10b / 0x10c: no hint -> no cover activity -> complete.
	for (const int32 Id : { 0x10a, 0x10b, 0x10c })
	{
		F.Run(Id);
		TestTrue(FString::Printf(TEXT("0x%x completes with no hint"), Id), F.Completed());
	}
	// 0x10d: no override, no enemy, no cover object -> the null == null arm; raises no failure.
	F.Run(0x10d);
	TestEqual(TEXT("0x102a5e41 no failure"), F.Failure(), 0);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The hint and kick-prop arms (indices 0x7e..0x83).
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19TailKickTest,
	"Elysium.Substrate.NpcKernelStartTask19.TroikaTail.HintAndKick_102a5f16", GStartTask19TailFlags)
bool FElysiumNpcKernelStartTask19TailKickTest::RunTest(const FString&)
{
	FStartTask19TailFixture F;
	if (!TestNotNull(TEXT("subject"), F.Npc) || !TestNotNull(TEXT("other"), F.Other))
	{
		return false;
	}
	FElysiumNpc& Npc = *F.Npc;
	AddExpectedError(TEXT("TaskFail"), EAutomationExpectedErrorFlags::Contains, 0);

	// 0x10e / 0x10f / 0x110: no hint node -> `TaskFail(4)`.
	Npc.BaseScheduleHost.HintNode = INDEX_NONE;
	for (const int32 Id : { 0x10e, 0x10f, 0x110 })
	{
		F.Run(Id);
		TestEqual(FString::Printf(TEXT("0x%x fail 4"), Id), F.Failure(), 4);
	}

	// 0x111 / 0x112 / 0x113: no kick prop -> `TaskFail(0x25)`.
	Npc.ScheduleHost.KickProp = FElysiumEntityHandle::Invalid();
	for (const int32 Id : { 0x111, 0x112, 0x113 })
	{
		F.Run(Id);
		TestEqual(FString::Printf(TEXT("0x%x fail 0x25"), Id), F.Failure(), 0x25);
	}

	// With a live prop: 0x111 with no enemy fails 6; 0x112 completes; 0x113 kicks and clears.
	Npc.ScheduleHost.KickProp = F.Other->Handle;
	Npc.BaseMemory.Enemy = FElysiumEntityHandle::Invalid();
	F.Run(0x111);
	TestEqual(TEXT("0x102a624d no enemy fails 6"), F.Failure(), 6);
	// That failure released the prop (Troika `TaskFail 0x1029adb0` clears `m_hKickPhysicsProp`
	// `+0x643c`); the next cases stand it again.
	TestFalse(TEXT("0x1029adb0 the failure releases the kick prop"), Npc.ScheduleHost.KickProp.IsSet());
	Npc.ScheduleHost.KickProp = F.Other->Handle;
	F.Run(0x112);
	TestTrue(TEXT("0x102a629e a live prop completes"), F.Completed());
	const int32 KicksBefore = Npc.TaskTailKicks;
	F.Run(0x113);
	TestEqual(TEXT("0x102a62f5 RestartIdealActivity(ACT_KICK)"), Npc.IdealActivityNumber, 0xc84);
	TestEqual(TEXT("0x102a6304 the kick"), Npc.TaskTailKicks, KicksBefore + 1);
	TestTrue(TEXT("at the prop"), Npc.TaskTailLastKicked == F.Other->Handle);
	TestFalse(TEXT("0x102a630d the handle cleared"), Npc.ScheduleHost.KickProp.IsSet());
	TestFalse(TEXT("0x113 leaves the task running"), F.Completed());

	// 0x111 with a prop (this body, at the origin) and an enemy (other, on +X): the goal is the prop
	// minus 64 units along the prop->enemy direction -- `0x102a61f1` is `0x1001395d` = `0x10146190`,
	// `FSUB`: the kick spot is on the far side of the prop.
	Npc.ScheduleHost.KickProp = Npc.Handle;
	Npc.BaseMemory.Enemy = F.Other->Handle;
	F.Run(0x111);
	TestEqual(TEXT("0x102a61b4 goal type 4"), Npc.TaskTailLastNavGoal.Type, 4);
	TestEqual(TEXT("0x102a61f1 dest x = prop - 64"), Npc.TaskTailLastNavGoal.DestUnits.X, -64.0, 1e-3);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The enemy-LKP / cover arms (0x84, 0x85, 0xac) and the botch resolutions (0x86..0x88).
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19TailCoverTest,
	"Elysium.Substrate.NpcKernelStartTask19.TroikaTail.CoverAndBotch_102a654b", GStartTask19TailFlags)
bool FElysiumNpcKernelStartTask19TailCoverTest::RunTest(const FString&)
{
	FStartTask19TailFixture F;
	if (!TestNotNull(TEXT("subject"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& Npc = *F.Npc;
	AddExpectedError(TEXT("TaskFail"), EAutomationExpectedErrorFlags::Contains, 0);

	// 0x114: no (last) enemy -> line 0x38c3, `TaskFail(0xc)`.
	Npc.BaseMemory.Enemy = FElysiumEntityHandle::Invalid();
	F.Run(0x114);
	TestEqual(TEXT("0x102a6681 fail 0xc"), F.Failure(), 0xc);

	// 0x115: the threat is this body; nothing finds cover -> `TaskFail(8)`.
	F.Run(0x115, 2.f);
	TestEqual(TEXT("0x102a67ee fail 8"), F.Failure(), 8);

	// 0x147: no damage attacker -> `TaskFail(0x21)`.
	Npc.BaseMemory.LastDamageAttacker = FElysiumEntityHandle::Invalid();
	F.Run(0x147, 2.f);
	TestEqual(TEXT("0x102a770f fail 0x21"), F.Failure(), 0x21);

	// 0x116..0x118: `0x102a9770` answers 1 when BOTCHED_ATTACK is CLEAR, and that arm completes.
	Npc.NpcFlags.Clear(EElysiumNpcFlag::BOTCHED_ATTACK);
	for (const int32 Id : { 0x116, 0x117, 0x118 })
	{
		F.Run(Id);
		TestTrue(FString::Printf(TEXT("0x%x bit clear completes"), Id), F.Completed());
	}
	// Set: 0x116 restarts ACT 0x55; 0x117 / 0x118 play the cover-anim helpers; none completes.
	Npc.NpcFlags.Set(EElysiumNpcFlag::BOTCHED_ATTACK);
	for (const int32 Id : { 0x116, 0x117, 0x118 })
	{
		F.Run(Id);
		TestFalse(FString::Printf(TEXT("0x%x bit set does not complete"), Id), F.Completed());
		if (Id == 0x116)
		{
			TestEqual(TEXT("0x102a6829 RestartIdealActivity(0x55)"), Npc.IdealActivityNumber, 0x55);
		}
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// The wait deltas, step back, face-last-angle, sound, LOS goal and sleep box (0x89..0x8f).
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19TailWaitsTest,
	"Elysium.Substrate.NpcKernelStartTask19.TroikaTail.WaitsAndSteps_102a68a8", GStartTask19TailFlags)
bool FElysiumNpcKernelStartTask19TailWaitsTest::RunTest(const FString&)
{
	FStartTask19TailFixture F;
	if (!TestNotNull(TEXT("subject"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& Npc = *F.Npc;
	AddExpectedError(TEXT("TaskFail"), EAutomationExpectedErrorFlags::Contains, 0);

	// 0x119: `m_bWaitFinishedSet = 1`, `m_flWaitFinishedDelta = operand`.
	F.Run(0x119, 1.5f);
	TestTrue(TEXT("0x102a68ab +0x6334"), Npc.ScheduleHost.bWaitFinishedSet);
	TestEqual(TEXT("0x102a68b2 +0x6330"), Npc.ScheduleHost.WaitFinishedDelta, 1.5f);
	TestTrue(TEXT("0x102a68b8 completes"), F.Completed());
	// 0x11a: `+= RandomFloat(0, operand)`.
	F.Run(0x11a, 2.f);
	TestTrue(TEXT("0x102a68ce the delta grew by at most the operand"),
		Npc.ScheduleHost.WaitFinishedDelta >= 1.5f && Npc.ScheduleHost.WaitFinishedDelta <= 3.5f);

	// 0x11b: no step-back sequence -> the 180-degree clearance sweep (`0x102a1650`, clear here) ->
	// `m_flDesiredMoveYaw = 180`, `m_flWaitFinished = curtime + delta`.
	Npc.ScheduleHost.WaitFinishedDelta = 1.f;
	F.Run(0x11b);
	TestEqual(TEXT("0x102a6934 +0x63ec = 180"), Npc.ScheduleHost.DesiredMoveYaw, 180.f);
	TestEqual(TEXT("0x102a694b +0x5db4"), Npc.BaseScheduleHost.WaitFinished, F.Now() + 1.0, 1e-4);
	TestFalse(TEXT("0x11b does not complete"), F.Completed());

	// 0x11d: the ideal yaw from `m_qaLastFacing.y` through `AngleMod`.
	Npc.BaseScheduleHost.bMotorAnimationMovement = false;
	Npc.LastFacing = FVector(0.0, 90.0, 0.0);
	F.Run(0x11d);
	TestEqual(TEXT("0x102a6ff6 AngleMod(90)"), Npc.MotorIdealYaw, 90.f, 0.01f);

	// 0x11e: the VSound play with the operand as its id, channel 2, 1.0, 1.25.
	const int32 SpeaksBefore = Npc.VSoundSpeakCalls.Num();
	F.Run(0x11e, 12.f);
	if (TestEqual(TEXT("0x102a701b one play"), Npc.VSoundSpeakCalls.Num(), SpeaksBefore + 1))
	{
		TestEqual(TEXT("the id is the operand"), Npc.VSoundSpeakCalls.Last().ConceptId, 12);
		TestEqual(TEXT("channel 2"), Npc.VSoundSpeakCalls.Last().Channel, 2);
		TestEqual(TEXT("attenuation 1.25"), Npc.VSoundSpeakCalls.Last().Attenuation, 1.25f);
	}
	TestTrue(TEXT("0x102a7020 completes"), F.Completed());

	// 0x11f: no LOS position -> `TaskFail(0xb)`.
	F.Run(0x11f);
	TestEqual(TEXT("0x102a70d3 fail 0xb"), F.Failure(), 0xb);

	// 0x121: on -> SLEEP_BOUNDING_BOX (raw), the extents saved and set to (60, 60, 80); off ->
	// restored, the save reset to -1 and the bit cleared.
	Npc.SetAttackExtents(FVector(1.0, 2.0, 3.0));
	F.Run(0x121, 1.f);
	TestTrue(TEXT("0x102a70fd the sleep bit"), Npc.NpcFlags.Has(EElysiumNpcFlag2::SLEEP_BOUNDING_BOX));
	TestEqual(TEXT("0x102a7118 the save"), Npc.ScheduleHost.SavedSleepExtents, FVector(1.0, 2.0, 3.0));
	TestEqual(TEXT("0x102a713d the sleep box"), Npc.AttackExtentsCm, FVector(60.0, 60.0, 80.0) * ElysiumMove::U);
	TestTrue(TEXT("0x102a7140 completes"), F.Completed());
	F.Run(0x121, 0.f);
	TestEqual(TEXT("0x102a714e restored"), Npc.AttackExtentsCm, FVector(1.0, 2.0, 3.0));
	TestEqual(TEXT("0x102a716f reset"), Npc.ScheduleHost.SavedSleepExtents, FVector(-1.0));
	TestFalse(TEXT("0x102a717b cleared"), Npc.NpcFlags.Has(EElysiumNpcFlag2::SLEEP_BOUNDING_BOX));
	TestTrue(TEXT("0x102a7180 completes"), F.Completed());
	return true;
}

// -------------------------------------------------------------------------------------------------
// The circles (0x90, 0x91), the combat-move tolerance (0x93) and the cheer (0x94).
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19TailCirclesTest,
	"Elysium.Substrate.NpcKernelStartTask19.TroikaTail.Circles_102a6ab7", GStartTask19TailFlags)
bool FElysiumNpcKernelStartTask19TailCirclesTest::RunTest(const FString&)
{
	FStartTask19TailFixture F;
	if (!TestNotNull(TEXT("subject"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& Npc = *F.Npc;
	AddExpectedError(TEXT("TaskFail"), EAutomationExpectedErrorFlags::Contains, 0);

	// 0x122: no 0x1121 sequence -> `TaskFail(0x15)` AND the 1000 sentinel, which skips the tries --
	// so neither the yaw nor the wait moves, and no second failure is raised.
	// The yaw word is no witness: the failure itself zeroes it (Troika `TaskFail 0x1029adb0`,
	// `ElysiumNpc.cpp`, `DesiredMoveYaw = 0`). The sweep's other write, the wait, is.
	Npc.ScheduleHost.DesiredMoveYaw = 7.f;
	Npc.BaseScheduleHost.WaitFinished = 123.0;
	F.Run(0x122);
	TestEqual(TEXT("0x102a6ae5 fail 0x15"), F.Failure(), 0x15);
	TestEqual(TEXT("0x102a6aeb the sentinel skips the sweep: no wait written"), Npc.BaseScheduleHost.WaitFinished, 123.0);
	TestEqual(TEXT("0x1029adb0 the failure zeroes the yaw word"), Npc.ScheduleHost.DesiredMoveYaw, 0.f);

	// 0x123: no enemy -> fail 6; then neither 0x1121 nor ACT_WALK has a sequence -> fail 0x15.
	Npc.BaseMemory.Enemy = FElysiumEntityHandle::Invalid();
	F.Run(0x123, 50.f);
	TestEqual(TEXT("0x102a6d23 the activity refusal is the last word"), F.Failure(), 0x15);

	// 0x127: `m_flGoalTolerance = debug_melee_advance_combatmove_dist + RandomFloat(0, 50)`, then the
	// path's two words: `0x102a59d1` `0x102ee1c0` (path +0x28) and `0x102a42df` `0x102f2fe0` (path
	// +0x20) -- not the runner's `Schedule.ToleranceUnits`.
	F.Run(0x127, 50.f);
	const float Tolerance = Npc.ScheduleHost.GoalToleranceCm / ElysiumMove::U;
	TestTrue(TEXT("0x102a59b3 100 <= tolerance <= 150"), Tolerance >= 100.f && Tolerance <= 150.f);
	TestEqual(TEXT("0x102a59d1 path +0x28 in cm"), Npc.NavPathToleranceCm, Npc.ScheduleHost.GoalToleranceCm, 1e-3f);
	TestEqual(TEXT("0x102a42df path +0x20"), Npc.NavPathScalar20, Tolerance, 1e-3f);
	TestTrue(TEXT("0x102a42e8 completes"), F.Completed());

	// 0x128: `m_flWaitFinished = delta + curtime`, no completion.
	Npc.ScheduleHost.WaitFinishedDelta = 2.f;
	F.Run(0x128);
	TestEqual(TEXT("0x102a7197 +0x5db4"), Npc.BaseScheduleHost.WaitFinished, F.Now() + 2.0, 1e-4);
	TestFalse(TEXT("0x128 does not complete"), F.Completed());
	return true;
}

// -------------------------------------------------------------------------------------------------
// The accumulator (0x95..0x99), face-save-position (0x9a) and make-oblivious (0x9b).
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19TailAccumTest,
	"Elysium.Substrate.NpcKernelStartTask19.TroikaTail.AccumAndOblivious_102a71e4", GStartTask19TailFlags)
bool FElysiumNpcKernelStartTask19TailAccumTest::RunTest(const FString&)
{
	FStartTask19TailFixture F;
	if (!TestNotNull(TEXT("subject"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& Npc = *F.Npc;

	F.Run(0x129, 50.f);
	TestEqual(TEXT("0x102a71f2 set"), Npc.SpecialDistanceAccum, 50.f);
	TestTrue(TEXT("0x129 completes"), F.Completed());
	F.Run(0x12a, 20.f);
	TestEqual(TEXT("0x102a7211 add"), Npc.SpecialDistanceAccum, 70.f);
	F.Run(0x12c, 30.f);
	TestEqual(TEXT("0x102a7266 subtract"), Npc.SpecialDistanceAccum, 40.f);
	F.Run(0x12b, 10.f);
	TestTrue(TEXT("0x102a729c add random"), Npc.SpecialDistanceAccum >= 40.f && Npc.SpecialDistanceAccum <= 50.f);
	const float Before = Npc.SpecialDistanceAccum;
	F.Run(0x12d, 10.f);
	TestTrue(TEXT("0x102a7247 subtract random"),
		Npc.SpecialDistanceAccum <= Before && Npc.SpecialDistanceAccum >= Before - 10.f);
	TestTrue(TEXT("0x12d completes"), F.Completed());

	// 0x12e: no completion.
	F.Run(0x12e);
	TestFalse(TEXT("0x102a72d6 the turn leaves the task running"), F.Completed());

	// 0x131 on: the raw bit, the refcount, complete. Off: the refcount back, the bit cleared.
	const int32 CountBefore = Npc.ObliviousCount;
	F.Run(0x131, 1.f);
	TestTrue(TEXT("0x102a72fa MADE_OBLIVIOUS"), Npc.NpcFlags.Has(EElysiumNpcFlag2::MADE_OBLIVIOUS));
	TestEqual(TEXT("0x1026d147 ++m_iIsOblivious"), Npc.ObliviousCount, CountBefore + 1);
	TestTrue(TEXT("0x102a7315 completes"), F.Completed());
	F.Run(0x131, 0.f);
	TestFalse(TEXT("0x102a7326 cleared"), Npc.NpcFlags.Has(EElysiumNpcFlag2::MADE_OBLIVIOUS));
	TestEqual(TEXT("0x1026d160 --m_iIsOblivious"), Npc.ObliviousCount, CountBefore);
	TestTrue(TEXT("0x102a733a completes"), F.Completed());
	return true;
}

// -------------------------------------------------------------------------------------------------
// The late set (0x8c, 0x137..0x149).
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19TailLateTest,
	"Elysium.Substrate.NpcKernelStartTask19.TroikaTail.LateSet_102a7358", GStartTask19TailFlags)
bool FElysiumNpcKernelStartTask19TailLateTest::RunTest(const FString&)
{
	FStartTask19TailFixture F;
	if (!TestNotNull(TEXT("subject"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& Npc = *F.Npc;

	// 0x8c: `RestartIdealActivity(0x1155)`, no completion.
	F.Run(0x8c);
	TestEqual(TEXT("0x102a7346 ACT 0x1155"), Npc.IdealActivityNumber, 0x1155);
	TestFalse(TEXT("0x102a7346 running"), F.Completed());

	// 0x137: the retail `== 0` test -- a model with no sequence (-1) does NOT fail.
	F.Run(0x137);
	TestEqual(TEXT("0x102a7368 no-sequence is not refused (retail defect)"), F.Failure(), 0);
	TestFalse(TEXT("0x102a7375 running"), F.Completed());

	// 0x138: no squad -> the break tail.
	F.Run(0x138);
	TestTrue(TEXT("0x102a73ab completes"), F.Completed());

	// 0x139 / 0x13b / 0x13c: `RestartIdealActivity(0x2b / 0x30 / 0x32)`, running.
	struct FJumpRow { int32 Task; int32 Activity; };
	for (const FJumpRow& Row : { FJumpRow{ 0x139, 0x2b }, FJumpRow{ 0x13b, 0x30 }, FJumpRow{ 0x13c, 0x32 } })
	{
		F.Run(Row.Task);
		TestEqual(FString::Printf(TEXT("0x%x RestartIdealActivity(0x%x)"), Row.Task, Row.Activity),
			Npc.IdealActivityNumber, Row.Activity);                  // 0x102a73ec / 0x102a747a / 0x102a749e
		TestFalse(FString::Printf(TEXT("0x%x running"), Row.Task), F.Completed());
	}
	// 0x13a: gravity, nav type, `m_bJumping`.
	Npc.JumpGravity = 0.5f;
	Npc.bJumping = false;
	F.Run(0x13a);
	TestEqual(TEXT("0x102a7410 gravity"), Npc.Gravity, 0.5f);
	TestTrue(TEXT("0x102a7456 m_bJumping"), Npc.bJumping);
	TestEqual(TEXT("0x102a744d nav type 1"), Npc.Navigator.NavType, 1);

	// 0x13d: `m_bInvincible` and NO completion.
	F.Run(0x13d, 1.f);
	TestTrue(TEXT("0x102a74c5 +0x63d8"), Npc.bInvincible);
	TestFalse(TEXT("0x102a74cb running"), F.Completed());
	F.Run(0x13d, 0.f);
	TestFalse(TEXT("0x102a74da cleared"), Npc.bInvincible);

	// The copy-prop set: all complete except the fadeout, whose zero answer keeps the task running.
	for (const int32 Id : { 0x13e, 0x13f, 0x140, 0x141, 0x143, 0x144 })
	{
		F.Run(Id, 1.f);
		TestTrue(FString::Printf(TEXT("0x%x completes"), Id), F.Completed());
	}
	F.Run(0x142, 1.f);
	TestFalse(TEXT("0x102a7569 a zero fadeout answer keeps running"), F.Completed());

	// 0x145: the burn, complete.
	F.Run(0x145);
	TestTrue(TEXT("0x102a75ad completes"), F.Completed());

	// 0x146: `m_vecLastPosition = m_vecInitialPosition`, `m_qaLastFacing = m_qaInitialAngles`.
	Npc.InitialPosition = FVector(1.0, 2.0, 3.0);
	Npc.InitialAngles = FVector(0.0, 45.0, 0.0);
	F.Run(0x146);
	TestEqual(TEXT("0x102a75bf +0x5db8"), Npc.LastPosition, FVector(1.0, 2.0, 3.0));
	TestEqual(TEXT("0x102a75d1 +0x5dc4"), Npc.LastFacing, FVector(0.0, 45.0, 0.0));
	TestTrue(TEXT("0x102a75d6 completes"), F.Completed());

	// 0x149: no sequence for the operand nor for ACT_DIESIMPLE -> ACT_IDLE, which completes.
	F.Run(0x149, 5.f);
	TestEqual(TEXT("0x102a77cb ACT_IDLE"), Npc.IdealActivityNumber, 1);
	TestTrue(TEXT("0x102a77dd ACT_IDLE completes"), F.Completed());
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
