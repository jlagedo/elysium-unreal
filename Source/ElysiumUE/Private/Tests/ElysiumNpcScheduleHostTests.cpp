#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumPhysProp.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Tests/ElysiumTestServices.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcTaskFailureTest,
	"Elysium.Substrate.NpcScheduleHost.Failure", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumNpcTaskFailureTest::RunTest(const FString&)
{
	FElysiumRecordingServices Services;
	Services.bProvideNpcMotor = true;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__npc_failure__");
	FElysiumEntityDef Def;
	Def.Classname = TEXT("npc_VHuman");
	Def.TargetName = TEXT("victim");
	Def.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/Jack.mdl"));
	Def.Keys.Add(TEXT("vision"), TEXT("540"));
	Def.Keys.Add(TEXT("hearing"), TEXT("1"));
	Defs.Defs.Add(MoveTemp(Def));
	FElysiumEntityDef PropDef;
	PropDef.Classname = TEXT("prop_physics");
	PropDef.TargetName = TEXT("kick_barrel");
	PropDef.Keys.Add(TEXT("npc_kickable"), TEXT("1"));
	Defs.Defs.Add(MoveTemp(PropDef));
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);
	FElysiumEntity* Entity = World.FindByName(TEXT("victim"));
	FElysiumNpc* Npc = Entity ? Entity->AsNpc() : nullptr;
	if (!TestNotNull(TEXT("NPC constructed"), Npc)
		|| !TestFalse(TEXT("modeled NPC receives a navigation motor"), Services.NpcMotors.IsEmpty())) return false;
	FElysiumRecordingNpcMotor& Motor = *Services.NpcMotors[0];
	FElysiumPhysProp* Prop = static_cast<FElysiumPhysProp*>(World.FindByName(TEXT("kick_barrel")));
	if (!TestNotNull(TEXT("authored kick prop exists"), Prop)) return false;
	TestTrue(TEXT("npc_kickable is loaded from the authored key"), Prop->bNpcKickable);
	Npc->ScheduleHost.KickProp = Prop->Handle;
	Motor.Navigation.bActiveGoal = true;
	Motor.Navigation.Type = EElysiumNpcNavType::Jump;
	Motor.Navigation.bGrounded = false;
	Motor.Navigation.VelocityCmPerSecond = FVector(1,0,0);
	TestTrue(TEXT("a moving jump remains in StopMoving"), Npc->StopMovingTask() == EElysiumTaskResult::Running);
	Motor.Navigation.VelocityCmPerSecond = FVector(0.01 * ElysiumMove::U,0,0);
	TestTrue(TEXT("stuck threshold equality fails"), Npc->StopMovingTask() == EElysiumTaskResult::Failed);
	TestEqual(TEXT("the task supplies FAIL_STUCK_ONTOP"), Npc->TaskFailureReason(), 0x1c);
	TestTrue(TEXT("stuck StopMoving switches to ground before failure"), Motor.Navigation.Type == EElysiumNpcNavType::Ground);
	// A direct navigator failure still in Jump has a different preservation result.
	Motor.Navigation.Type = EElysiumNpcNavType::Jump;
	Npc->NpcFlags.Set(EElysiumNpcFlag::PRESERVE_PATH);
	Npc->NpcFlags.AddOblivious();
	Npc->NpcFlags.Set(EElysiumNpcFlag::NO_DIALOG);
	Npc->NpcFlags.Set(EElysiumNpcFlag2::SLEEP_BOUNDING_BOX);
	Npc->ScheduleHost.SavedSleepExtents = FVector(10,10,30);
	Npc->ScheduleHost.MemoryBits = 0xffffffffu;
	Npc->ScheduleHost.GoalToleranceCm = 25.f;
	Npc->ScheduleHost.InsideInterruptDistanceSqr = 64.f;
	Npc->ScheduleHost.OutsideInterruptDistanceSqr = 256.f;
	Npc->ScheduleHost.HintNode = 5;
	Npc->ScheduleHost.bOwnsHint = true;
	Npc->ScheduleHost.FailedCoverLosChecks = 3;
	Npc->NpcFlags.Set(EElysiumNpcFlag::AT_COVER_HINT);
	Npc->ScheduleHost.NextAI = Npc->ScheduleHost.NextNormal = Npc->ScheduleHost.NextMove = Npc->ScheduleHost.NextUpdate = 50.0;
	Npc->TaskFail(0x1c);
	TestTrue(TEXT("jump failure preserves the path bit"), Npc->NpcFlags.Has(EElysiumNpcFlag::PRESERVE_PATH));
	TestTrue(TEXT("failure raises TASK_FAILED"), Npc->Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed));
	TestFalse(TEXT("NO_DIALOG is released by the failure mask"), Npc->HasDialogSuppressFlag());
	TestTrue(TEXT("the bounded retail refcount leak is retained"), Npc->IsOblivious());
	TestFalse(TEXT("the leak loses its bookkeeping bit"), Npc->NpcFlags.Has(EElysiumNpcFlag2::MADE_OBLIVIOUS));
	TestEqual(TEXT("both native memory masks apply"), Npc->ScheduleHost.MemoryBits, 0x0fffdfffu);
	TestEqual(TEXT("all next clocks reset"), Npc->ScheduleHost.NextAI + Npc->ScheduleHost.NextNormal
		+ Npc->ScheduleHost.NextMove + Npc->ScheduleHost.NextUpdate, 0.0);
	TestEqual(TEXT("goal tolerance reset"), Npc->ScheduleHost.GoalToleranceCm, 0.f);
	TestEqual(TEXT("inside interrupt reset"), Npc->ScheduleHost.InsideInterruptDistanceSqr, 0.f);
	TestEqual(TEXT("outside interrupt reset"), Npc->ScheduleHost.OutsideInterruptDistanceSqr, 0.f);
	TestEqual(TEXT("hint released for five seconds"), Npc->ScheduleHost.HintReusableAt, 5.0);
	TestTrue(TEXT("sleep attack margin restored"), Npc->ScheduleHost.AttackExtentsCm.Equals(FVector(10,10,30)));
	TestFalse(TEXT("TaskFail consumes the prop's kickable permission"), Prop->bNpcKickable);
	TestFalse(TEXT("TaskFail drops the resolved kick prop handle"), Npc->ScheduleHost.KickProp.IsSet());
	TestFalse(TEXT("ClearHintNode clears AT_COVER_HINT"), Npc->NpcFlags.Has(EElysiumNpcFlag::AT_COVER_HINT));
	TestEqual(TEXT("ClearHintNode clears failed cover LOS count"), Npc->ScheduleHost.FailedCoverLosChecks, 0);
	const FBox Collision(FVector(-16,-16,0), FVector(16,16,72));
	const FBox Attack = Npc->AttackBounds(Collision);
	TestTrue(TEXT("attack min subtracts the restored margin"), Attack.Min.Equals(FVector(-26,-26,-30)));
	TestTrue(TEXT("attack max adds the restored margin"), Attack.Max.Equals(FVector(26,26,102)));
	Motor.Navigation.Type = EElysiumNpcNavType::Ground;
	Npc->TaskFail(0x29);
	TestFalse(TEXT("ground failure clears preserve-path"), Npc->NpcFlags.Has(EElysiumNpcFlag::PRESERVE_PATH));
	TestEqual(TEXT("reason retained for diagnostics"), Npc->ScheduleHost.FailureReason, 0x29);
	Npc->TaskStarting();
	TestEqual(TEXT("starting a task clears the previous failure reason"), Npc->ScheduleHost.FailureReason, 0);
	Motor.Navigation.Type = EElysiumNpcNavType::Jump;
	Motor.Navigation.bActiveGoal = false;
	TestTrue(TEXT("StopMoving with no goal completes without probing jump failure"), Npc->BeginStopMovingTask() == EElysiumTaskResult::Complete);
	Motor.Navigation.bActiveGoal = true;
	Motor.Navigation.VelocityCmPerSecond = FVector(10,0,0);
	TestTrue(TEXT("StartTask clears the goal and keeps the moving jump running"), Npc->BeginStopMovingTask() == EElysiumTaskResult::Running);
	TestFalse(TEXT("the goal was cleared independently from nav type"), Motor.Navigation.bActiveGoal);
	Motor.Navigation.VelocityCmPerSecond = FVector::ZeroVector;
	TestTrue(TEXT("RunTask still detects a stuck jump after the goal clear"), Npc->StopMovingTask() == EElysiumTaskResult::Failed);
	Npc->NpcFlags.Set(EElysiumNpcFlag::PRESERVE_PATH);
	Npc->TaskFail(Npc->TaskFailureReason());
	TestFalse(TEXT("StopMoving failure observes the ground transition"), Npc->NpcFlags.Has(EElysiumNpcFlag::PRESERVE_PATH));
	Npc->ReconnectToSquad();
	TestEqual(TEXT("reconnect clamps a missing disconnect at zero"), Npc->ScheduleHost.SquadDisconnected, 0);
	Npc->ScheduleHost.HintReusableAt = 77.0;
	Npc->ClearScheduleHint(5.f);
	TestEqual(TEXT("a missing hint does not change a cooldown"), Npc->ScheduleHost.HintReusableAt, 77.0);
	Npc->ScheduleHost.HintNode = 8;
	Npc->ScheduleHost.bOwnsHint = false;
	Npc->ClearScheduleHint(5.f);
	TestEqual(TEXT("another owner's hint is not put on cooldown"), Npc->ScheduleHost.HintReusableAt, 77.0);
	Npc->NpcFlags.Set(EElysiumNpcFlag2::SLEEP_BOUNDING_BOX);
	Npc->ScheduleHost.SavedSleepExtents = FVector::ZeroVector;
	Npc->TaskFail(0x0c);
	TestTrue(TEXT("zero attack margin restores successfully"), Npc->ScheduleHost.AttackExtentsCm.IsZero());
	Npc->NpcFlags.Clear(EElysiumNpcFlag::PRESERVE_PATH);
	Npc->NpcFlags.Set(EElysiumNpcFlag2::SLEEP_BOUNDING_BOX);
	Npc->NpcFlags.Set(EElysiumNpcFlag2::ACTIVITY_COPY_PROP_CLEAN);
	Npc->ScheduleHost.SavedSleepExtents = FVector(4,5,6);
	Npc->ScheduleHost.SquadDisconnected = 1;
	Npc->NpcFlags.AddOblivious();
	Npc->bInvincible = true;
	Npc->OnScheduleChange();
	TestTrue(TEXT("schedule replacement also restores attack margins"), Npc->ScheduleHost.AttackExtentsCm.Equals(FVector(4,5,6)));
	TestFalse(TEXT("activity-copy cleanup clears invincibility"), Npc->bInvincible);
	TestFalse(TEXT("activity-copy bit is consumed by unconditional tail"), Npc->NpcFlags.Has(EElysiumNpcFlag2::ACTIVITY_COPY_PROP_CLEAN));
	TestEqual(TEXT("UnOblivious reconnects even without disconnect bookkeeping bit"), Npc->ScheduleHost.SquadDisconnected, 0);
	Npc->ScheduleDone();
	TestTrue(TEXT("host publishes the native schedule-done condition"), Npc->Cognition.Conditions.Has(EElysiumNpcCond::ScheduleDone));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcFailureMasksTest,
	"Elysium.Substrate.NpcScheduleHost.MasksAndSave", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumNpcFailureMasksTest::RunTest(const FString&)
{
	FElysiumNpcFlags Flags;
	for (uint32 Bit = 1; Bit != 0x80000000u; Bit <<= 1)
	{
		Flags.Set(static_cast<EElysiumNpcFlag>(Bit));
		Flags.Set(static_cast<EElysiumNpcFlag2>(Bit));
	}
	Flags.AddOblivious();
	TestTrue(TEXT("mask transaction reports the retail leak"), Flags.OnTaskFail());
	for (uint32 Bit = 1; Bit != 0x80000000u; Bit <<= 1)
	{
		TestEqual(FString::Printf(TEXT("flags1 bit %08x matches mask"), Bit),
			Flags.Has(static_cast<EElysiumNpcFlag>(Bit)), (Bit & 0xa3f40178u) != 0);
		TestEqual(FString::Printf(TEXT("flags2 bit %08x matches mask"), Bit),
			Flags.Has(static_cast<EElysiumNpcFlag2>(Bit)), (Bit & 0x7fffe24fu) != 0);
	}
	TArray<uint8> Bytes;
	{
		FMemoryWriter Writer(Bytes, true);
		FElysiumSaveArchive Ar(Writer, FElysiumSaveVersion::Latest);
		Flags.Serialize(Ar);
	}
	FElysiumNpcFlags Loaded;
	{
		FMemoryReader Reader(Bytes, true);
		FElysiumSaveArchive Ar(Reader, FElysiumSaveVersion::Latest);
		Loaded.Serialize(Ar);
	}
	TestTrue(TEXT("leaked obliviousness survives save"), Loaded.IsOblivious());
	TestFalse(TEXT("save does not reconstruct the lost bookkeeping bit"), Loaded.Has(EElysiumNpcFlag2::MADE_OBLIVIOUS));
	Loaded.Clear(EElysiumNpcFlag::PRESERVE_PATH);
	TestFalse(TEXT("later schedule change cannot recover the lost decrement"), Loaded.OnScheduleChange());
	TestTrue(TEXT("later schedule still retains leaked obliviousness"), Loaded.IsOblivious());
	FElysiumNpcFlags Stages;
	Stages.Set(EElysiumNpcFlag::D_IS_BUSY);
	Stages.Set(EElysiumNpcFlag2::ACTIVITY_COPY_PROP_CLEAN);
	Stages.BeginScheduleChange();
	Stages.ApplyScheduleChangeMasks();
	TestFalse(TEXT("busy flag is already released at the callback stage"), Stages.Has(EElysiumNpcFlag::D_IS_BUSY));
	TestTrue(TEXT("activity-copy predicate survives until the callback stage"), Stages.Has(EElysiumNpcFlag2::ACTIVITY_COPY_PROP_CLEAN));
	Stages.FinishScheduleChange();
	TestFalse(TEXT("tail clears activity-copy after the callback stage"), Stages.Has(EElysiumNpcFlag2::ACTIVITY_COPY_PROP_CLEAN));
	return true;
}
#endif
