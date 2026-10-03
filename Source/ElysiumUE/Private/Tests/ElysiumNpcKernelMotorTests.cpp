#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcTestHull.h"
#include "Substrate/ElysiumNpcChangBros.h"
#include "Substrate/ElysiumNpcAsianVampire.h"
#include "Substrate/ElysiumNpcHengeyokai.h"
#include "Substrate/ElysiumNpcSheriffMan.h"
#include "Substrate/ElysiumNpcGargoyle.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcWerewolf.h"
#include "Substrate/ElysiumNpcTzimisce.h"
#include "Substrate/ElysiumNpcMingXiao.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcRat.h"
#include "Substrate/ElysiumNpcDog.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumNpcTestCensus.h"
#include "Tests/ElysiumTestServices.h"

// Story 29c-1, family **Motor** — `CAI_Motor`, `CAI_Navigator` and everything the NPC asks of its
// motor.
//
// Every threshold asserted here was read out of the pinned retail `vampire.dll`'s `.rdata` at the
// address the body cites, so a case that fails is a divergence from retail and not from an opinion.
// Where an input is a SEAM the case says so: it asserts that the seam is ASKED and that the refusal
// is the recovered one, which is the only honest assertion available until the seam has a source.
// This family has more of those than any other in the story — there is no navigator, no node graph,
// no move probe, no hull table and no collision-extent surface in this substrate.

static constexpr EAutomationTestFlags GElysiumNpcKernelMotorFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// Source units into this world's centimetres, Y negated — the inverse of the bodies' own
	// conversion, so a case can place a body at a retail distance.
	FVector PortUnits(double X, double Y, double Z)
	{
		return FVector(X * ElysiumMove::U, -Y * ElysiumMove::U, Z * ElysiumMove::U);
	}

	// `CNPC_VMingXiao`'s tuning record, as the SEAM answers it (every field 0) and as a case can
	// stand it to prove the two field offsets apart.
	float ZeroTuning(int32)
	{
		return 0.f;
	}
}

// --- The movement tunables, slots 521 / 522 / 523 / 524 -------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotorTunablesTest,
	"Elysium.Arm.NpcKernelMotor.Tunables", GElysiumNpcKernelMotorFlags)
bool FElysiumNpcKernelMotorTunablesTest::RunTest(const FString&)
{
	// `CAI_TestHull`'s answers are its own class's overrides (`FElysiumNpcTestHull`, story 5 fold A1;
	// `NpcKernelTestHull.Bodies`); commit B collapsed the class-keyed row table into the bodies below.
	// The base line (a base-only NPC answers the `CAI_BaseNPC` bodies): `0x101a6b60` returns the SAME
	// `_DAT_10453b94` as `0x101a6b40`, so its jump speed and its step height are one constant, 18.
	FElysiumNpcTestHull HullOnly;
	FElysiumNpcBase& BaseLine = HullOnly;
	TestEqual(TEXT("the base line's step height is 18"), BaseLine.FElysiumNpcBase::StepHeight(), 18.0f);
	TestEqual(TEXT("and its jump speed is the same 18"), BaseLine.FElysiumNpcBase::GetStepDownHeight(), 18.0f);

	// The Troika line: `0x101a6b40`'s 18 for the step height slot 522 carries, and Troika's own
	// override of 523 (`0x101aa670`, `_DAT_1044faa8`) = 36.0 -- a DIFFERENT constant.
	// The live slots on a spawnable species.
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_motor_tunables"), 4301);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	if (!TestNotNull(TEXT("guard"), Guard))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });

	TestEqual(TEXT("slot 522 answers 18"), Guard->StepHeight(), 18.0f);
	TestEqual(TEXT("slot 523 answers 36"), Guard->GetStepDownHeight(), 36.0f);
	return true;
}

// --- Slot 523 on the species, and the stand test's drop -------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotorStandDropTest,
	"Elysium.Arm.NpcKernelMotor.StandDrop", GElysiumNpcKernelMotorFlags)
bool FElysiumNpcKernelMotorStandDropTest::RunTest(const FString&)
{
	// Slot 523 is the STEP-DOWN height (R1 §5), whatever the SDK slot table calls it: the drop
	// `CheckStandPosition 0x102e7270` traces below the feet. Three species fill it on their own class:
	// `CNPC_VMingXiao` `0x10391050` 50.0 (`0x1044ffe8`), `CNPC_VMingXiaoTentacle` `0x1039b070` 30.0
	// (`0x104492a8`), `CNPC_VTzimisce` `0x103b6df0` 56.0 (`0x104cc500`).
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_motor_stand_drop"), 4311);
	Builder.AddNpcOfClass(TEXT("tzimisce"), FVector::ZeroVector, TEXT("CNPC_VTzimisce"));
	Builder.AddNpcOfClass(TEXT("ming"), FVector(2000.0, 0.0, 0.0), TEXT("CNPC_VMingXiao"));
	Builder.AddNpcOfClass(TEXT("tentacle"), FVector(4000.0, 0.0, 0.0), TEXT("CNPC_VMingXiaoTentacle"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpcTzimisce* Tzimisce = Fixture.NpcAs<FElysiumNpcTzimisce>(TEXT("tzimisce"));
	FElysiumNpcMingXiao* Ming = Fixture.NpcAs<FElysiumNpcMingXiao>(TEXT("ming"));
	FElysiumNpcMingXiaoTentacle* Tentacle = Fixture.NpcAs<FElysiumNpcMingXiaoTentacle>(TEXT("tentacle"));
	if (!TestNotNull(TEXT("tzimisce"), Tzimisce) || !TestNotNull(TEXT("ming"), Ming)
		|| !TestNotNull(TEXT("tentacle"), Tentacle))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Tzimisce, Ming, Tentacle });
	TestEqual(TEXT("0x103b6df0: the Tzimisce drops 56"), Tzimisce->GetStepDownHeight(), 56.0f);
	TestEqual(TEXT("0x10391050: Ming Xiao drops 50"), Ming->GetStepDownHeight(), 50.0f);
	TestEqual(TEXT("0x1039b070: the tentacle drops 30"), Tentacle->GetStepDownHeight(), 30.0f);

	// The drop is the one the stand trace takes: the end is `pos.z - slot 523`, dispatched on the
	// species, and the foot box comes off the species' own `m_Collision` (TZIMISCE1, hull 10).
	FElysiumRetailTrace Asked;
	Fixture.Services.TraceRetailQuery = [&Asked](const FElysiumRetailTrace& Trace, FElysiumRetailTraceResult& Out)
	{
		Asked = Trace;
		return true;   // a clear world: fraction 1.0
	};
	const double U = ElysiumMove::U;
	TestFalse(TEXT("a clear drop is no ground"), Tzimisce->CanStandAt(FVector(0.0, 0.0, 10.0), 0x202400b));
	TestTrue(TEXT("the Tzimisce's stand trace ends 56 below the spot"),
		Asked.EndCm.Equals(FVector(0.0, 0.0, 10.0 - 56.0) * U, 1e-3));
	FVector Mins = FVector::ZeroVector;
	FVector Maxs = FVector::ZeroVector;
	Tzimisce->RetailHullExtents(Tzimisce->HullKind, FElysiumNpcBase::EElysiumHullExtents::Full, Mins, Maxs);
	TestTrue(TEXT("and its foot box is 0.75 / 0.25 of its own hull's x"),
		FMath::IsNearlyEqual(Asked.MinsCm.X, (0.75 * Mins.X + 0.25 * Maxs.X) * U, 1e-3));
	TestTrue(TEXT("at its own mins.z, zero height"),
		FMath::IsNearlyEqual(Asked.MinsCm.Z, Mins.Z * U, 1e-3) && FMath::IsNearlyEqual(Asked.MaxsCm.Z, Mins.Z * U, 1e-3));
	Asked = FElysiumRetailTrace();
	Ming->CanStandAt(FVector(0.0, 0.0, 0.0), 0x202400b);
	TestTrue(TEXT("Ming Xiao's ends 50 below"), Asked.EndCm.Equals(FVector(0.0, 0.0, -50.0) * U, 1e-3));
	Tentacle->CanStandAt(FVector(0.0, 0.0, 0.0), 0x202400b);
	TestTrue(TEXT("the tentacle's 30 below"), Asked.EndCm.Equals(FVector(0.0, 0.0, -30.0) * U, 1e-3));
	Fixture.Services.TraceRetailQuery = nullptr;
	return true;
}

// --- `IsJumpLegal`, slot 521 ----------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotorJumpLegalTest,
	"Elysium.Arm.NpcKernelMotor.IsJumpLegal", GElysiumNpcKernelMotorFlags)
bool FElysiumNpcKernelMotorJumpLegalTest::RunTest(const FString&)
{
	// `FUN_10280790` `0x10280790`, four arms in order. `_DAT_104493d0 = 0.1` is added to three of
	// the four thresholds; the apex uses `_DAT_10460020 = 1.25` as a MULTIPLIER on the rise instead.
	auto Legal = [](double StartZ, double ApexZ, double EndZ, double EndX)
	{
		return FElysiumNpcBase::IsJumpLegalGeometry(FVector(0.0, 0.0, StartZ), FVector(0.0, 0.0, ApexZ),
			FVector(EndX, 0.0, EndZ), 80.0f, 250.0f, 160.0f);
	};

	TestTrue(TEXT("a flat 10-unit hop is legal"), Legal(0.0, 0.0, 0.0, 10.0));
	// Arm 1: `maxRise + 0.1 < end.z - start.z`.
	TestTrue(TEXT("a rise of exactly 80.1 is still legal"), Legal(0.0, 0.0, 80.1, 0.0));
	TestFalse(TEXT("a rise of 80.2 is not"), Legal(0.0, 0.0, 80.2, 0.0));
	// Arm 2: `maxDrop + 0.1 < start.z - end.z`, with the apex measured from the START so a drop
	// needs an apex at the start height to clear arm 3.
	//
	// **On the base thresholds this arm is unreachable**, and that is a recovered fact rather than a
	// gap in the case: a drop big enough to trip the 250 is also a 3-D distance big enough to trip
	// the 160, and arm 4 runs on the same numbers. The boundary is therefore asserted on
	// `CAI_TestHull`'s 1024/1024/1024, where the two agree.
	TestTrue(TEXT("a 150-unit drop is legal on the base thresholds"),
		Legal(150.0, 150.0, 0.0, 0.0));
	TestFalse(TEXT("a 250-unit drop is not — the 160 distance arm forecloses the 250 drop arm"),
		Legal(250.0, 250.0, 0.0, 0.0));
	auto Hull = [](double StartZ, double ApexZ, double EndZ, double EndX)
	{
		return FElysiumNpcBase::IsJumpLegalGeometry(FVector(0.0, 0.0, StartZ), FVector(0.0, 0.0, ApexZ),
			FVector(EndX, 0.0, EndZ), 1024.0f, 1024.0f, 1024.0f);
	};
	TestTrue(TEXT("the test hull admits a drop of exactly 1024.1"),
		Hull(1024.1, 1024.1, 0.0, 0.0));
	TestFalse(TEXT("and refuses 1024.2"), Hull(1024.2, 1024.2, 0.0, 0.0));
	// Arm 3: `maxRise * 1.25 < apex.z - start.z` — 100.0 for the 80.0 rise, and NOT slackened by 0.1.
	TestTrue(TEXT("an apex exactly 100 above the start is legal"), Legal(0.0, 100.0, 0.0, 0.0));
	TestFalse(TEXT("an apex 100.1 above it is not"), Legal(0.0, 100.1, 0.0, 0.0));
	// Arm 4: the 3-D distance, with the SAME 0.1 slack.
	TestTrue(TEXT("a flat 160.1-unit jump is legal"), Legal(0.0, 0.0, 0.0, 160.1));
	TestFalse(TEXT("a flat 160.2-unit jump is not"), Legal(0.0, 0.0, 0.0, 160.2));

	// `CAI_TestHull::IsJumpLegal` — the same body with 1024 everywhere.
	TestTrue(TEXT("the test hull's 1024 rise admits what the base refuses"),
		FElysiumNpcBase::IsJumpLegalGeometry(FVector::ZeroVector, FVector::ZeroVector,
			FVector(0.0, 0.0, 300.0), 1024.0f, 1024.0f, 1024.0f));
	TestFalse(TEXT("and its apex scale is still 1.25 of the rise"),
		FElysiumNpcBase::IsJumpLegalGeometry(FVector::ZeroVector, FVector(0.0, 0.0, 1280.1),
			FVector::ZeroVector, 1024.0f, 1024.0f, 1024.0f));
	return true;
}

// --- The yaw-speed ladders, slot 516 (0019/6: restored as data) ----------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotorYawLaddersTest,
	"Elysium.Arm.NpcKernelMotor.MaxYawSpeedLadders", GElysiumNpcKernelMotorFlags)
bool FElysiumNpcKernelMotorYawLaddersTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_motor_yaw"), 4302);
	Builder.AddNpc(TEXT("guard"));
	Builder.AddNpcOfClass(TEXT("dog"), FVector(300.0, 0.0, 0.0), TEXT("CNPC_VDog"));
	Builder.AddNpcOfClass(TEXT("tzimisce"), FVector(600.0, 0.0, 0.0), TEXT("CNPC_VTzimisce"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	FElysiumNpcDog* Dog = Fixture.NpcAs<FElysiumNpcDog>(TEXT("dog"));
	FElysiumNpcTzimisce* Tzim = Fixture.NpcAs<FElysiumNpcTzimisce>(TEXT("tzimisce"));
	if (!TestNotNull(TEXT("guard"), Guard) || !TestNotNull(TEXT("dog"), Dog) || !TestNotNull(TEXT("tzimisce"), Tzim))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard, Dog, Tzim });

	// `CAI_BaseNPC::MaxYawSpeed` `0x10280bb0`: `_DAT_1049949c`.
	TestEqual(TEXT("the base line is 45"), Guard->FElysiumNpcBase::MaxYawSpeed(), 45.0f);

	// `CAI_BaseNPCTroika::MaxYawSpeed` `0x10297ce0`, on the activity word (+0x0fec).
	const struct { int32 Activity; float Yaw; } Troika[] = {
		{ 0x3b, 30.f }, { 0x3c, 30.f }, { 0x13, 160.f }, { 0x1093, 160.f }, { 0x1096, 160.f },
		{ 0x1121, 30.f }, { 0x1092, 45.f },
		// Out of combat (state byte bit 7 clear) the `debug_slow_*` cvars: "20" idle, "25" walk.
		{ 1, 20.f }, { 5, 20.f }, { 9, 25.f },
	};
	for (const auto& Row : Troika)
	{
		Guard->ActivityNumber = Row.Activity;
		TestEqual(*FString::Printf(TEXT("troika 0x%x"), Row.Activity), Guard->MaxYawSpeed(), Row.Yaw);
	}
	TestEqual(TEXT("an idle guard's state byte has bit 7 clear"),
		static_cast<int32>(Guard->NpcStateFlags() & 0x80), 0);
	// `PLAYING_FACE_ANIM` suppresses the whole ladder.
	Guard->ActivityNumber = 0x13;
	Guard->NpcFlags.Set(EElysiumNpcFlag::PLAYING_FACE_ANIM);
	TestEqual(TEXT("a face anim answers 45 even on ACT_RUN"), Guard->MaxYawSpeed(), 45.0f);
	Guard->NpcFlags.Clear(EElysiumNpcFlag::PLAYING_FACE_ANIM);
	// The turning arm beats every activity; `GetIdealYawSpeed()` answers 0, so it lands on 1.0.
	Guard->BaseScheduleHost.MemoryBits |= 0x2000;
	TestEqual(TEXT("the turning arm's floor, 1.0"), Guard->MaxYawSpeed(), 1.0f);
	Guard->BaseScheduleHost.MemoryBits &= ~0x2000u;

	// `CNPC_VDog` `0x10374130`.
	const struct { int32 Activity; float Yaw; } DogRows[] = {
		{ 0x13, 40.f }, { 0x3b, 30.f }, { 9, 45.f }, { 1, 90.f }, { 5, 90.f },
	};
	for (const auto& Row : DogRows)
	{
		Dog->ActivityNumber = Row.Activity;
		TestEqual(*FString::Printf(TEXT("dog 0x%x"), Row.Activity), Dog->MaxYawSpeed(), Row.Yaw);
	}
	// `CNPC_VTzimisce` `0x103ba020`.
	const struct { int32 Activity; float Yaw; } TzimRows[] = {
		{ 1, 5.f }, { 0xfc, 5.f }, { 0x13, 30.f }, { 9, 11.f },
	};
	for (const auto& Row : TzimRows)
	{
		Tzim->ActivityNumber = Row.Activity;
		TestEqual(*FString::Printf(TEXT("tzimisce 0x%x"), Row.Activity), Tzim->MaxYawSpeed(), Row.Yaw);
	}

	// `CNPC_VMingXiao` `0x10394930`: +0x48 inside (0x1129, 0x112e), +0x44 outside.
	auto Field = [](int32 Offset) { return Offset == 0x48 ? 7.0f : 3.0f; };
	TestEqual(TEXT("mingxiao 0x112a reads +0x48"), FElysiumNpcMingXiao::MaxYawSpeedMingXiao(0x112a, Field), 7.0f);
	TestEqual(TEXT("mingxiao 0x112d reads +0x48"), FElysiumNpcMingXiao::MaxYawSpeedMingXiao(0x112d, Field), 7.0f);
	TestEqual(TEXT("mingxiao 0x1129 reads +0x44"), FElysiumNpcMingXiao::MaxYawSpeedMingXiao(0x1129, Field), 3.0f);
	TestEqual(TEXT("mingxiao 0x112e reads +0x44"), FElysiumNpcMingXiao::MaxYawSpeedMingXiao(0x112e, Field), 3.0f);

	// The rate the mover is handed: `UpdateYaw(int)` x 10 (degrees per tenth of a second).
	TestEqual(TEXT("45 turns at 450 deg/s"), FElysiumNpcBase::MotorYawRateDegPerS(45.f), 450.0f);
	TestEqual(TEXT("the int truncates 1.9 to 1"), FElysiumNpcBase::MotorYawRateDegPerS(1.9f), 10.0f);
	return true;
}

// --- What the kernel states to the motor (0019/6 lane R3) -----------------------------------------

namespace
{
	// The recording motor the services built for one NPC (its `Owner` is the NPC's handle).
	FElysiumRecordingNpcMotor* KernelMotorR3RecordingMotorOf(const FElysiumRecordingServices& Services,
		const FElysiumEntityHandle& Owner)
	{
		for (const TUniquePtr<FElysiumRecordingNpcMotor>& Motor : Services.NpcMotors)
		{
			if (Motor.IsValid() && Motor->Owner == Owner)
			{
				return Motor.Get();
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotorRequestSeamsTest,
	"Elysium.Arm.NpcKernelMotor.RequestSeams", GElysiumNpcKernelMotorFlags)
bool FElysiumNpcKernelMotorRequestSeamsTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_motor_seams"), 4303);
	FElysiumEntityDef& GuardDef = Builder.AddNpc(TEXT("guard"));
	GuardDef.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/Jack.mdl"));
	Builder.AddNpc(TEXT("other"), FVector(200.0, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder), [](FElysiumRecordingServices& Services)
		{
			Services.bProvideNpcMotor = true;
			Services.bNpcActivitiesResolve = true;
		});
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	FElysiumNpc* Other = Fixture.Npc(TEXT("other"));
	FElysiumRecordingNpcMotor* GuardMotor =
		Guard != nullptr ? KernelMotorR3RecordingMotorOf(Fixture.Services, Guard->Handle) : nullptr;
	if (!TestNotNull(TEXT("guard"), Guard) || !TestNotNull(TEXT("other"), Other)
		|| !TestNotNull(TEXT("the guard wears a recording motor"), GuardMotor))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard, Other });
	FElysiumRecordingNpcMotor& Motor = *GuardMotor;

	// The turn carries retail's rate: `0x102e1c10(yaw, -1)` stores `MaxYawSpeed()` (`0x102e1cf0`) and
	// `UpdateYaw(-1)` turns by it, x10.
	Guard->ActivityNumber = 0x13;
	Guard->MotorSetIdealYawAndUpdate(90.f, -1.f);
	TestEqual(TEXT("0x102e1cf0 stored ACT_RUN's 160"), Guard->MotorYawSpeedWord, 160.0f);
	TestEqual(TEXT("and Face was handed 1600 deg/s"), Motor.RequestedYawSpeedDegPerS, 1600.0f);
	Guard->MotorSetIdealYawAndUpdate(0.f, 10.f);
	TestEqual(TEXT("a stated speed is stored and turned by"), Motor.RequestedYawSpeedDegPerS, 100.0f);

	// A kernel move request carries the STORED word `+0x38` (the task's 10 above), not a fresh slot
	// 516, and registers the slot 69 answers first.
	Guard->NpcFlags.Set(EElysiumNpcFlag::NAV_IGNORE_NPC);
	FElysiumNpcMoveRequest Leg;
	Leg.DestinationCm = FVector(100.0, 0.0, 0.0);
	TestTrue(TEXT("the leg is issued"), Guard->NavIssueLeg(Leg));
	TestEqual(TEXT("the request carries the stored word x 10"), Motor.LastMoveRequest.YawSpeedDegPerS, 100.0f);
	TestTrue(TEXT("NAV_IGNORE_NPC: the other NPC is registered with SetMoveIgnore"),
		Motor.MoveIgnored.Contains(Other->Handle));
	TestFalse(TEXT("never the NPC itself"), Motor.MoveIgnored.Contains(Guard->Handle));

	// Mid-leg, a writer of `+0x38` (`0x102e1cf0`, as `SetActivityAndSequence`'s tail runs it) reaches
	// the body's turn on the next think; an unchanged word states nothing.
	const int32 YawCallsBefore = Motor.YawSpeedCalls;
	Guard->MotorStoreMaxYawSpeed();
	Guard->MotorThinkUpkeep();
	TestEqual(TEXT("the re-stored word is handed mid-leg"), Motor.MidMoveYawSpeedDegPerS, 1600.0f);
	TestEqual(TEXT("once"), Motor.YawSpeedCalls, YawCallsBefore + 1);
	Guard->MotorThinkUpkeep();
	TestEqual(TEXT("an unchanged word is not re-stated"), Motor.YawSpeedCalls, YawCallsBefore + 1);

	// Slot 69 is asked every think while the move is live: an answer that changes mid-leg is seen.
	Guard->NpcFlags.Clear(EElysiumNpcFlag::NAV_IGNORE_NPC);
	Guard->MotorThinkUpkeep();
	TestFalse(TEXT("a withdrawn answer is cleared within a think"), Motor.MoveIgnored.Contains(Other->Handle));
	Guard->NpcFlags.Set(EElysiumNpcFlag::NAV_IGNORE_NPC);
	Guard->MotorThinkUpkeep();
	TestTrue(TEXT("a new answer is registered within a think"), Motor.MoveIgnored.Contains(Other->Handle));

	// Every `Motor->Stop()` site clears the set and ends the upkeep.
	Guard->StopMoving();
	TestEqual(TEXT("a stop clears exactly what was registered"), Motor.MoveIgnored.Num(), 0);
	Guard->MotorThinkUpkeep();
	TestEqual(TEXT("and no think re-registers after it"), Motor.MoveIgnored.Num(), 0);
	TestTrue(TEXT("a second leg registers again"), Guard->NavIssueLeg(Leg));
	TestTrue(TEXT("with the answer standing"), Motor.MoveIgnored.Contains(Other->Handle));
	Guard->ClearMoveIgnores();
	TestEqual(TEXT("the move's end clears exactly what was registered"), Motor.MoveIgnored.Num(), 0);
	Guard->NpcFlags.Clear(EElysiumNpcFlag::NAV_IGNORE_NPC);

	// The hull resize reaches the capsule seam, in centimetres.
	const int32 HullsBefore = Motor.HullSizes.Num();
	Guard->SetHullSizeSmall(/*bForce=*/true);
	if (TestEqual(TEXT("SetHullSizeSmall resizes through SetHullSize"), Motor.HullSizes.Num(), HullsBefore + 1))
	{
		TestTrue(TEXT("the box is the kernel's, in cm"), Motor.HullSizes.Last().Max.Z
			== Guard->LastSetSizeMaxsUnits.Z * ElysiumMove::U);
	}

	// The facing-while-moving target: the queue's blend, handed on the think; slot 7 leaves it; the
	// entry's end stamp drops it.
	FElysiumNpcBase::FFacingTargetRequest Face;
	Face.MotorSlot = 13;
	Face.Position = FVector(0.0, 500.0, 0.0);
	Face.Importance = 1.0f;
	Face.Duration = 1.0f;
	Guard->MotorAddFacingTarget(Face);
	Guard->MotorThinkUpkeep();
	TestTrue(TEXT("the queue's blend is the mover's facing target"),
		Motor.FacingTarget.IsSet() && Motor.FacingTarget.GetValue().Equals(Face.Position, 0.01));
	Guard->ClearFacingTarget();
	Guard->MotorThinkUpkeep();
	TestTrue(TEXT("0x102e11f0 does not touch the queue"), Motor.FacingTarget.IsSet());
	Fixture.Advance(Guard->World->NowSeconds() + 2.0);
	Guard->MotorThinkUpkeep();
	TestFalse(TEXT("past its duration the entry expires and the body faces the path"),
		Motor.FacingTarget.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotorWerewolfRouteTest,
	"Elysium.Arm.NpcKernelMotor.WerewolfHasPathRoute", GElysiumNpcKernelMotorFlags)
bool FElysiumNpcKernelMotorWerewolfRouteTest::RunTest(const FString&)
{
	// `CNPC_VWerewolf::HasPath` `0x103d0db0` -> `0x102ee380(start, end)` -> `0x102fdcc0`: the motor's
	// two-point route query, the start stated explicitly.
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_motor_wolf_route"), 4304);
	FElysiumEntityDef& WolfDef = Builder.AddNpcOfClass(TEXT("wolf"), FVector::ZeroVector, TEXT("CNPC_VWerewolf"));
	WolfDef.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/Jack.mdl"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder), [](FElysiumRecordingServices& Services)
		{
			Services.bProvideNpcMotor = true;
			Services.bNpcActivitiesResolve = true;
		});
	FElysiumNpcWerewolf* Wolf = Fixture.NpcAs<FElysiumNpcWerewolf>(TEXT("wolf"));
	FElysiumRecordingNpcMotor* WolfMotor =
		Wolf != nullptr ? KernelMotorR3RecordingMotorOf(Fixture.Services, Wolf->Handle) : nullptr;
	if (!TestNotNull(TEXT("wolf"), Wolf) || !TestNotNull(TEXT("the wolf wears a motor"), WolfMotor))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Wolf });
	FElysiumRecordingNpcMotor& Motor = *WolfMotor;
	const FVector StartUnits(10.0, 20.0, 0.0);
	const FVector EndUnits(40.0, 20.0, 0.0);
	TestFalse(TEXT("no mesh behind the motor: retail's no-path false"), Wolf->WerewolfHasPath(StartUnits, EndUnits));
	Motor.RouteQuery = [](const FVector&, float& Length) { Length = 76.2f; return true; };
	TestTrue(TEXT("a route between the two points: true"), Wolf->WerewolfHasPath(StartUnits, EndUnits));
	if (TestTrue(TEXT("the start was stated"), Motor.RouteStarts.Num() > 0 && Motor.RouteStarts.Last().IsSet()))
	{
		TestEqual(TEXT("in centimetres"), Motor.RouteStarts.Last().GetValue(), StartUnits * ElysiumMove::U);
	}
	Motor.RouteQuery = [](const FVector&, float&) { return false; };
	TestFalse(TEXT("the mesh answered no route: false"), Wolf->WerewolfHasPath(StartUnits, EndUnits));
	return true;
}

// --- Slots 68 and 69 — the collision-ignore chains -----------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotorIgnoreCollisionTest,
	"Elysium.Arm.NpcKernelMotor.IgnoreCollision", GElysiumNpcKernelMotorFlags)
bool FElysiumNpcKernelMotorIgnoreCollisionTest::RunTest(const FString&)
{
	// The seven classes that add an arm in front of the Troika bodies of slot 68 or 69, each its
	// class's own override (story 5 step 4), by name with its retail addresses.
	const TCHAR* Expected[][3] = {
		{ TEXT("CNPC_VGargoyle"), nullptr, TEXT("0x10379490") },
		{ TEXT("CNPC_VHengeyokai"), nullptr, TEXT("0x10380f90") },
		{ TEXT("CNPC_VMingXiao"), nullptr, TEXT("0x10396fd0") },
		{ TEXT("CNPC_VMingXiaoTentacle"), TEXT("0x1039eb50"), TEXT("0x1039eb90") },
		{ TEXT("CNPC_VRat"), TEXT("0x103ad6d0"), nullptr },
		{ TEXT("CNPC_VTzimisce"), nullptr, TEXT("0x103bfa00") },
		{ TEXT("CNPC_VWerewolf"), TEXT("0x103d9ab0"), TEXT("0x103d9ba0") },
	};
	for (const TCHAR* const (&Row)[3] : Expected)
	{
		const FElysiumNpcClass* Cls = ElysiumNpcTestCensus::Find(Row[0]);
		for (int32 Arm = 0; Arm < 2; ++Arm)
		{
			const int32 Slot = 68 + Arm;
			if (Row[1 + Arm] != nullptr)
			{
				TestEqual(*FString::Printf(TEXT("%s's slot-%d body"), Row[0], Slot),
					FString(ElysiumNpcTestCensus::BodyOf(Cls, Slot)), FString(Row[1 + Arm]));
			}
			else
			{
				TestNull(*FString::Printf(TEXT("%s does not replace slot %d"), Row[0], Slot),
					ElysiumNpcTestCensus::OverrideOf(Cls, Slot));
			}
		}
	}

	// `CNPC_VGargoyle::NavIgnoreCollision` `0x10379490`'s three `FClassnameIs` compares, which use
	// `__strcmpi` and so are case-insensitive.
	TestTrue(TEXT("prop_dynamic"), FElysiumNpcGargoyle::GargoyleIgnoresClassname(TEXT("prop_dynamic")));
	TestTrue(TEXT("PROP_DYNAMIC — the compare is case-insensitive"),
		FElysiumNpcGargoyle::GargoyleIgnoresClassname(TEXT("PROP_DYNAMIC")));
	TestTrue(TEXT("func_brush"), FElysiumNpcGargoyle::GargoyleIgnoresClassname(TEXT("func_brush")));
	TestTrue(TEXT("func_door_rotating"),
		FElysiumNpcGargoyle::GargoyleIgnoresClassname(TEXT("func_door_rotating")));
	TestFalse(TEXT("func_door is NOT one of the three"),
		FElysiumNpcGargoyle::GargoyleIgnoresClassname(TEXT("func_door")));
	TestFalse(TEXT("prop_physics is not either"),
		FElysiumNpcGargoyle::GargoyleIgnoresClassname(TEXT("prop_physics")));

	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_motor_collide"), 4303);
	Builder.AddNpc(TEXT("guard"));
	Builder.AddNpc(TEXT("other"), FVector(200.0, 0.0, 0.0));
	Builder.AddNpc(TEXT("rat"), FVector(400.0, 0.0, 0.0), TEXT("npc_VRat"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	FElysiumNpc* Other = Fixture.Npc(TEXT("other"));
	FElysiumNpcRat* Rat = Fixture.NpcAs<FElysiumNpcRat>(TEXT("rat"));
	if (!TestNotNull(TEXT("guard"), Guard) || !TestNotNull(TEXT("other"), Other)
		|| !TestNotNull(TEXT("rat"), Rat))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard, Other, Rat });

	// The shared head, arm 1. Without `NAV_IGNORE_NPC` the only NPC arm is the sleeping one.
	TestFalse(TEXT("an ordinary NPC is not ignored"), Guard->IgnoreCollisionSharedHead(Other));
	Other->NpcFlags.Set(EElysiumNpcFlag::SLEEPING);
	TestTrue(TEXT("a SLEEPING NPC is"), Guard->IgnoreCollisionSharedHead(Other));
	Other->NpcFlags.Clear(EElysiumNpcFlag::SLEEPING);

	// With `NAV_IGNORE_NPC` set, any NPC and the player are ignored and the sleeping arm is not
	// reached at all.
	Guard->NpcFlags.Set(EElysiumNpcFlag::NAV_IGNORE_NPC);
	TestTrue(TEXT("NAV_IGNORE_NPC ignores every NPC"), Guard->IgnoreCollisionSharedHead(Other));
	TestTrue(TEXT("and the player"), Guard->IgnoreCollisionSharedHead(Fixture.Player()));
	// `m_bForceNPCCheck` suppresses the whole first arm, which is exactly what `CanStandAt` brackets
	// its probe with.
	Guard->bForceNpcCheck = true;
	TestFalse(TEXT("m_bForceNPCCheck suppresses arm 1 entirely"),
		Guard->IgnoreCollisionSharedHead(Other));
	Guard->bForceNpcCheck = false;
	Guard->NpcFlags.Clear(EElysiumNpcFlag::NAV_IGNORE_NPC);

	// Arm 2: the kicked physics prop.
	Guard->ScheduleHost.KickProp = Other->Handle;
	TestTrue(TEXT("the kick prop is ignored"), Guard->IgnoreCollisionSharedHead(Other));
	Guard->ScheduleHost.KickProp = FElysiumEntityHandle::Invalid();

	// A null candidate reaches none of the arms.
	TestFalse(TEXT("a null candidate is not ignored"),
		Guard->IgnoreCollisionSharedHead(nullptr));

	// The two slots on an ordinary Troika species. `m_edtDerivedType` is a SEAM answering 0, so both
	// derived-type gates fall through and the chains reach their `IsIgnoreCollisionEntity` tail,
	// which has no handle to compare against.
	TestFalse(TEXT("slot 68 declines for an ordinary NPC"), Guard->ShouldIgnoreCollision(Other));
	TestFalse(TEXT("slot 69 declines too"), Guard->NavIgnoreCollision(Other));
	// `m_bNavIgnorePhysicsProps` swaps the NAV chain's mask from 0x12 to 0x16 — unobservable while
	// the derived-type seam answers 0, and the case says so.
	Guard->bNavIgnorePhysicsProps = true;
	TestFalse(TEXT("and still declines with m_bNavIgnorePhysicsProps set, because the "
		"m_edtDerivedType seam answers 0"), Guard->NavIgnoreCollision(Other));
	Guard->bNavIgnorePhysicsProps = false;

	// The tail: `m_hIgnoreCollisionEntity` (+0x055c), which nothing writes in this runtime yet.
	TestFalse(TEXT("the tail refuses with no handle"), Guard->IsIgnoreCollisionEntityTail(Other));
	Guard->IgnoreCollisionEntity = Other->Handle;
	TestTrue(TEXT("and answers true once the handle names the candidate"),
		Guard->IsIgnoreCollisionEntityTail(Other));
	TestFalse(TEXT("but not for a different candidate"), Guard->IsIgnoreCollisionEntityTail(Rat));
	Guard->IgnoreCollisionEntity = FElysiumEntityHandle::Invalid();

	// `CNPC_VRat::ShouldIgnoreCollision` `0x103ad6d0`: the fixed global entity, else the base. The
	// global is a SEAM answering null, so the rat declines like anybody else.
	TestTrue(TEXT("the rat resolves to CNPC_VRat"), Rat->AsSpecies<FElysiumNpcRat>() != nullptr);
	TestNull(TEXT("which it does"), Rat->RatIgnoredGlobalEntity());
	return true;
}

// --- Slots 133, 153, 166, 210, 525, 575 and `AutoMovement` ---------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotorSlotsTest,
	"Elysium.Arm.NpcKernelMotor.Slots", GElysiumNpcKernelMotorFlags)
bool FElysiumNpcKernelMotorSlotsTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_motor_slots"), 4304);
	Builder.AddNpc(TEXT("guard"));
	Builder.AddNpc(TEXT("other"), FVector(200.0, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	FElysiumNpc* Other = Fixture.Npc(TEXT("other"));
	if (!TestNotNull(TEXT("guard"), Guard) || !TestNotNull(TEXT("other"), Other))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard, Other });

	// (Slot 210's body is the character movement component's since 0019/6: its case is gone.)

	// Slot 166 `0x10026f80`: a null candidate is standable, a non-null one defers to its
	// `IsStandable()` — a SEAM answering false.
	// The cast disambiguates slot 166 `CanStandOn(CBaseEntity*)` from slot 165's
	// `CanStandOn(edict_t*)`, which the generator spells `CanStandOn(void*)`.
	TestTrue(TEXT("slot 166 admits a null candidate"),
		Guard->CanStandOn(static_cast<FElysiumEntity*>(nullptr)));

	// Slot 525 `0x1027da90` — the base declines unconditionally.
	TestFalse(TEXT("slot 525 declines"), Guard->OverrideMove(0.1f));

	// Slot 153 `0x10280300` forwards to the navigator's `IsGoalActive`. With no motor in a headless
	// fixture there is no goal.
	TestFalse(TEXT("slot 153 answers false with no motor"), Guard->IsMoving());

	// Slot 575 `0x102bf4a0`. The gate needs an enemy, `MOVE_FACE_ENEMY`, an active weapon AND the
	// weapon's `0x6000` capability bits — the last of which is a SEAM answering 0, so the Troika
	// gate closes before the base rung is ever reached.
	TestFalse(TEXT("slot 575 declines with no enemy"), Guard->ShouldMoveAndShoot());
	Guard->BaseMemory.Enemy = Other->Handle;
	Guard->NpcFlags.Set(EElysiumNpcFlag2::MOVE_FACE_ENEMY);
	Guard->CapabilityWord |= (1 << 6);
	TestFalse(TEXT("so slot 575 still declines even with bits_CAP_MOVE_SHOOT set"),
		Guard->ShouldMoveAndShoot());
	Guard->NpcFlags.Clear(EElysiumNpcFlag2::MOVE_FACE_ENEMY);
	Guard->BaseMemory.Enemy = FElysiumEntityHandle::Invalid();

	// `AutoMovement` `0x10280a50`. The recovered GATE is `GetMoveType() == 4` with `FL_FROZEN 0x400`
	// clear; both fail here, so the apply is never reached — which is the whole point of porting the
	// gate rather than the extraction.
	TestFalse(TEXT("AutoMovement refuses at move type != 4"), Guard->AutoMovement());
	return true;
}

// --- The navigator seam ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotorNavigatorTest,
	"Elysium.Arm.NpcKernelMotor.Navigator", GElysiumNpcKernelMotorFlags)
bool FElysiumNpcKernelMotorNavigatorTest::RunTest(const FString&)
{
	// The navigator bodies are the Troika line's and run on a Troika NPC; only `0x10382d20` is a
	// Hengeyokai body, and it runs on a Hengeyokai (the step-4 review: the whole case had moved onto
	// the species, so `OnNavFailed`'s `TaskFail` went through the Hengeyokai's own override).
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_motor_nav"), 4305);
	Builder.AddTroikaNpc(TEXT("guard"), FVector::ZeroVector);
	Builder.AddNpcOfClass(TEXT("heng"), FVector(400.0, 0.0, 0.0), TEXT("CNPC_VHengeyokai"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	FElysiumNpcHengeyokai* Heng = Fixture.NpcAs<FElysiumNpcHengeyokai>(TEXT("heng"));
	if (!TestNotNull(TEXT("guard"), Guard) || !TestNotNull(TEXT("heng"), Heng))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard, Heng });
	TestNull(TEXT("the guard is the bare Troika line"), Guard->RetailClass());

	// `FUN_1027d990` / `FUN_1027d9b0` — the one navigator word this family reads and writes.
	TestEqual(TEXT("the nav type starts at Ground 0"), Guard->NavGetType(), 0);
	Guard->NavSetType(2);
	TestEqual(TEXT("and takes the value it is given"), Guard->NavGetType(), 2);
	Guard->NavSetType(0);

	// `CAI_Navigator::vfunc3` `0x102ecb50` — the owner-pointer snapshot, of which only the fact and
	// the argument survive the port.
	TestFalse(TEXT("no snapshot taken yet"), Guard->Navigator.bSnapshotTaken);
	Guard->NavSnapshotOwnerPointers(7);
	TestTrue(TEXT("the snapshot is recorded"), Guard->Navigator.bSnapshotTaken);
	TestEqual(TEXT("with its argument"), Guard->Navigator.SnapshotArgument, 7);

	// `FUN_1027a6c0` `0x1027a6c0` — the navigator goal's movement activity, or `ACT_IDLE` 1. The pair
	// is `0x102ee6a0` (`IsGoalActive`) then `0x102ee510` (the path's movement activity); this guard
	// has no active goal, so `0x102ee6a0` refuses and the answer is the fallback.
	int32 Activity = 0;
	TestFalse(TEXT("0x102ee6a0: no active goal refuses"), Guard->NavLinkActivity(Activity));
	TestEqual(TEXT("so the resolved activity is ACT_IDLE"), Guard->ResolveLinkActivity(), 1);

	// `FUN_10382d20` `0x10382d20` — `UpdateYaw(-1)`, a one-line forward since 0019/6.
	const int32 YawUpdatesBefore = Heng->MotorUpdateYawCalls;
	Heng->ClearLinkActivity();
	TestEqual(TEXT("one yaw update"), Heng->MotorUpdateYawCalls, YawUpdatesBefore + 1);

	// `CAI_Navigator::OnNavFailed` `0x102eeae0` — `TaskFail`, then the ideal activity from the link,
	// then the failed latch. `CAI_Navigator#9` `0x102eeb50` is a tail-jump into the same body.
	TestFalse(TEXT("the navigator has not failed"), Guard->Navigator.bNavFailed);
	Guard->NavOnNavFailed(0x1b);
	TestTrue(TEXT("OnNavFailed sets the latch"), Guard->Navigator.bNavFailed);
	TestEqual(TEXT("and re-plays the resolved link activity, which is ACT_IDLE"),
		Guard->IdealActivityNumber, 1);
	TestEqual(TEXT("and routes the caller's reason through TaskFail"),
		Guard->BaseScheduleHost.FailureReason, 0x1b);

	// `FUN_102bf7e0` `0x102bf7e0` — `m_bShouldMove` is set UNCONDITIONALLY, outside the stop.
	Guard->BaseScheduleHost.bShouldMove = false;
	Guard->ResumeScheduledMove();
	TestTrue(TEXT("ResumeScheduledMove sets bShouldMove even with no active goal"),
		Guard->BaseScheduleHost.bShouldMove);

	// `PatrolNodeInterestRecord` — the node-graph read. This world has no network, so every index
	// is out of range, which is retail's own counted-refusal arm (the networked arms are
	// `Elysium.Arm.PlaceSeams.Patrol`).
	TestEqual(TEXT("an empty network answers 0 for any route step"), Guard->NavNodeWordAt(0), 0);
	TestEqual(TEXT("including a negative one"), Guard->NavNodeWordAt(-1), 0);

	// Slot 528 `0x10280360`. The gate is the navigator's goal TYPE, 0 with no goal, so the body
	// answers true — retail's own answer for every goal type but 6 — and the cover trace is never
	// reached.
	TestEqual(TEXT("with no goal the goal type is 0"), Guard->NavGoalState(), 0);
	const int32 TracesBefore = Guard->MotorSeams.HullTraces;
	TestTrue(TEXT("slot 528 validates"), Guard->ValidateNavGoal());
	TestEqual(TEXT("and ran no trace"), Guard->MotorSeams.HullTraces, TracesBefore);
	FVector Goal(1.0, 2.0, 3.0);
	TestTrue(TEXT("the goal position always answers"), Guard->NavGoalPosition(Goal));
	TestEqual(TEXT("and with no goal it is retail's (0,0,0), never 'none'"), Goal, FVector::ZeroVector);
	return true;
}

// --- The ground and stand probes ------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotorProbesTest,
	"Elysium.Arm.NpcKernelMotor.Probes", GElysiumNpcKernelMotorFlags)
bool FElysiumNpcKernelMotorProbesTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_motor_probes"), 4306);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	if (!TestNotNull(TEXT("guard"), Guard))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });

	// `CAI_BaseNPCTroika::CanStandAt` `0x102a0ed0` — the `m_bForceNPCCheck` bracket around the probe.
	// With no collision world the stand trace answers the clear default, which refuses.
	TestFalse(TEXT("bForceNpcCheck starts clear"), Guard->bForceNpcCheck);
	const int32 ProbesBefore = Guard->MotorSeams.MoveProbeChecks;
	TestFalse(TEXT("CanStandAt refuses with no collision world"),
		Guard->CanStandAt(FVector(10.0, 0.0, 0.0), 0x202400b));
	TestEqual(TEXT("and the probe was asked exactly once"), Guard->MotorSeams.MoveProbeChecks,
		ProbesBefore + 1);
	TestFalse(TEXT("and the bracket closed"), Guard->bForceNpcCheck);

	// `CheckStandPosition 0x102e7270` (R1 §1) over a stated world: the knob answers the one hull
	// trace the probe makes and records what it was asked.
	FElysiumPlayer* Player = Fixture.Player();
	if (!TestNotNull(TEXT("player"), Player))
	{
		return false;
	}
	FElysiumRetailTrace Asked;
	FElysiumRetailTraceResult Stated;
	bool bForcedDuringTrace = false;
	Fixture.Services.TraceRetailQuery = [&Asked, &Stated, &bForcedDuringTrace, Guard](
		const FElysiumRetailTrace& Trace, FElysiumRetailTraceResult& Out)
	{
		Asked = Trace;
		bForcedDuringTrace = Guard->bForceNpcCheck;
		Out = Stated;
		return true;
	};
	const double U = ElysiumMove::U;
	const FVector Spot(10.0, 0.0, 0.0);    // Source units, the port's axes
	TestFalse(TEXT("102e741a: a clear drop (fraction exactly 1.0) is no ground"),
		Guard->CanStandAt(Spot, 0x202400b));
	TestTrue(TEXT("0x102a0ed0 holds m_bForceNPCCheck up during the trace"), bForcedDuringTrace);
	TestTrue(TEXT("102e72af: the start is the spot lifted 0.1"),
		Asked.StartCm.Equals(FVector(10.0, 0.0, 0.1) * U, 1e-3));
	TestTrue(TEXT("102e72b9: the end drops slot 523, 36 on the Troika line"),
		Asked.EndCm.Equals(FVector(10.0, 0.0, -36.0) * U, 1e-3));
	TestTrue(TEXT("the foot box: 0.75 mins + 0.25 maxs of HUMAN_HULL's (-13..13) OBB"),
		Asked.MinsCm.Equals(FVector(-6.5, -6.5, 0.0) * U, 1e-3));
	TestTrue(TEXT("...to 0.25 mins + 0.75 maxs, zero height at mins.z"),
		Asked.MaxsCm.Equals(FVector(6.5, 6.5, 0.0) * U, 1e-3));
	TestEqual(TEXT("under the caller's mask"), Asked.RetailMask, 0x202400b);
	TestTrue(TEXT("with the tester as the pass entity"), Asked.Ignore.Contains(Guard->Handle));

	Stated.Fraction = 0.9999f;
	TestTrue(TEXT("a world hit just below 1.0 is ground: slot 166 on a null entity stands"),
		Guard->CanStandAt(Spot, 0x202400b));
	Stated.Fraction = 0.f;
	Stated.bStartSolid = true;
	Stated.bAllSolid = true;
	TestTrue(TEXT("start-solid is NOT tested: a trace starting in the floor is a hit and stands"),
		Guard->CanStandAt(Spot, 0x202400b));
	Stated = FElysiumRetailTraceResult();
	Stated.Fraction = 0.5f;
	Stated.HitEntity = Player->Handle;
	TestFalse(TEXT("slot 166 refuses an entity IsStandable does not allow"),
		Guard->CanStandAt(Spot, 0x202400b));
	Stated = FElysiumRetailTraceResult();
	FVector CallerMins(-2.0, -4.0, 5.0);
	FVector CallerMaxs(2.0, 4.0, 9.0);
	Guard->MoveProbeCheckStandPosition(Spot, 0x2400b, &CallerMins, &CallerMaxs);
	TestTrue(TEXT("a caller's box replaces the OBB, the foot box built from it"),
		Asked.MinsCm.Equals(FVector(-1.0, -2.0, 5.0) * U, 1e-3)
			&& Asked.MaxsCm.Equals(FVector(1.0, 2.0, 5.0) * U, 1e-3));
	TestEqual(TEXT("and the mask is the caller's"), Asked.RetailMask, 0x2400b);
	Fixture.Services.TraceRetailQuery = nullptr;

	// `CAI_BaseNPC::CheckOnGround` `0x1026e5e0`. Without condition 0x73 and at nav type 0 the body
	// stamps its deadline (`curtime + 0.5`, `_DAT_104454d0`) and then measures the floor through the
	// floor facts seam (`IElysiumNpcMotor::SampleFloor`, story 6), which replaced its hull trace.
	const EElysiumNpcCond OnGround = static_cast<EElysiumNpcCond>(0x73);
	// This world stands no movement component: the deadline is stamped, the seam answers nothing and
	// neither ground write runs. The floor arms are `NpcKernelMotor.CheckOnGroundFloor`'s.
	TestEqual(TEXT("the deadline starts at 0"), Guard->CheckOnGroundTime, 0.0);
	Guard->CheckOnGround();
	TestTrue(TEXT("CheckOnGround stamped its 0.5-second deadline"),
		FMath::IsNearlyEqual(Guard->CheckOnGroundTime, Fixture.World.NowSeconds() + 0.5, 1e-6));
	TestFalse(TEXT("a world with no movement component writes nothing"),
		Guard->Cognition.Conditions.Has(OnGround));

	// The `HasCondition(0x73)` arm: with the condition set and FL_ONGROUND clear and nav type 0 the
	// body returns without clearing it; with either of those two false it clears it.
	Guard->Cognition.Conditions.Set(OnGround);
	Guard->CheckOnGround();
	TestTrue(TEXT("airborne on nav type 0 keeps the condition"),
		Guard->Cognition.Conditions.Has(OnGround));
	Guard->Flags |= 1;   // FL_ONGROUND
	Guard->CheckOnGround();
	TestFalse(TEXT("FL_ONGROUND clears it"), Guard->Cognition.Conditions.Has(OnGround));
	Guard->Flags &= ~1;
	Guard->Cognition.Conditions.Set(OnGround);
	Guard->NavSetType(1);
	Guard->CheckOnGround();
	TestFalse(TEXT("so does a non-ground nav type"), Guard->Cognition.Conditions.Has(OnGround));
	Guard->NavSetType(0);

	// `CNPC_VWerewolf::GetGroundpoint` `0x103d6a40`. With no collision world the trace answers no hit
	// and the body lands on retail's own no-hit arm, which answers `DAT_10713de0/de4/de8` — and `staticinit_101371a0`
	// fills all three with `0x7f7fffff`, so that fallback is **`vec3_invalid`, not `vec3_origin`**.
	FElysiumNpcWorldBuilder WolfBuilder(TEXT("npc_kernel_motor_groundpoint"), 4306);
	WolfBuilder.AddNpcOfClass(TEXT("wolf"), FVector::ZeroVector, TEXT("CNPC_VWerewolf"));
	FElysiumNpcWorldFixture WolfFixture(MoveTemp(WolfBuilder));
	FElysiumNpcWerewolf* Wolf = WolfFixture.NpcAs<FElysiumNpcWerewolf>(TEXT("wolf"));
	if (!TestNotNull(TEXT("the werewolf spawned"), Wolf))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Wolf });
	const FVector Ground = Wolf->GetGroundpoint(FVector(100.0, 100.0, 100.0));
	TestEqual(TEXT("GetGroundpoint answers retail's no-hit fallback, vec3_invalid"),
		static_cast<float>(Ground.X), 3.4028234663852886e+38f);
	TestEqual(TEXT("in all three terms"), static_cast<float>(Ground.Z),
		3.4028234663852886e+38f);
	// The hull table, replayed from the image. Hull 0 is HUMAN_HULL, and its two extent pairs
	// differ: the full box is 13 wide and the small one 8, at the same 72 height.
	FVector Mins(1.0, 1.0, 1.0);
	FVector Maxs(2.0, 2.0, 2.0);
	TestTrue(TEXT("the hull table answers for hull 0"),
		Guard->RetailHullExtents(0, FElysiumNpcBase::EElysiumHullExtents::Full, Mins, Maxs));
	TestEqual(TEXT("HUMAN_HULL's full mins"), Mins, FVector(-13.0, -13.0, 0.0));
	TestEqual(TEXT("...and its full maxs"), Maxs, FVector(13.0, 13.0, 72.0));
	TestTrue(TEXT("the small pair is the same row's other box"),
		Guard->RetailHullExtents(0, FElysiumNpcBase::EElysiumHullExtents::Small, Mins, Maxs));
	TestEqual(TEXT("HUMAN_HULL's small mins"), Mins, FVector(-8.0, -8.0, 0.0));
	TestEqual(TEXT("...and its small maxs"), Maxs, FVector(8.0, 8.0, 72.0));
	// Retail's table has 22 rows; a hull id outside it keeps the refusal every caller's failure
	// arm is written against.
	TestFalse(TEXT("a hull id the table does not carry still refuses"),
		Guard->RetailHullExtents(22, FElysiumNpcBase::EElysiumHullExtents::Full, Mins, Maxs));
	TestEqual(TEXT("and zeroes both extents"), Mins, FVector::ZeroVector);
	TestEqual(TEXT("both"), Maxs, FVector::ZeroVector);
	// Not every small box is smaller: TZIMISCE1 (bit 10) widens from 35 to 45.
	TestTrue(TEXT("TZIMISCE1's full box"),
		Guard->RetailHullExtents(10, FElysiumNpcBase::EElysiumHullExtents::Full, Mins, Maxs));
	TestEqual(TEXT("reaches 35"), Maxs.X, 35.0);
	TestTrue(TEXT("and its SMALL box is wider"),
		Guard->RetailHullExtents(10, FElysiumNpcBase::EElysiumHullExtents::Small, Mins, Maxs));
	TestEqual(TEXT("at 45"), Maxs.X, 45.0);

	// `PerformMovement` `0x1026c120` and `PostRun` `0x1026c7c0` — the delegate and the ordered pair.
	Guard->PerformMovement(0.25f, 3);
	TestEqual(TEXT("PerformMovement forwarded its interval"),
		Guard->MotorSeams.PerformMovementInterval, 0.25f);
	TestEqual(TEXT("once"), Guard->MotorSeams.PerformMovement, 1);
	Guard->PostRun();
	TestEqual(TEXT("PostRun ran its pair once"), Guard->MotorSeams.PostRunWeaponUpdates, 1);
	// The interval is `RunAnimation`'s answer, i.e. `CBaseAnimating::StudioFrameAdvance(0)`
	// `0x1008f120`: on this fresh clock (`m_flPrevAnimTime == curtime`'s re-seed, `0x1008f192..
	// 0x1008f1b3`) a zero argument becomes `0x3dcccccd` (`0x1008f1ca`) and the interval is
	// `(0.1 + curtime) - m_flAnimTime` = 0.1. (The earlier 0 was the stubbed seam's answer.)
	return true;
}

// --- `OnObstructingDoor`'s base branch and the cine no-op -----------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotorDoorTest,
	"Elysium.Arm.NpcKernelMotor.ObstructingDoor", GElysiumNpcKernelMotorFlags)
bool FElysiumNpcKernelMotorDoorTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_motor_door"), 4307);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	if (!TestNotNull(TEXT("guard"), Guard))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });

	using EResult = FElysiumNpcBase::EObstructingDoorResult;
	EResult Result = EResult::Ok;

	// `0x1027dc80` arm 1: the move goal's own max distance is already shorter than the clearance.
	float MaxDistance = 5.0f;
	TestFalse(TEXT("a nearer goal is not obstructed"),
		Guard->OnObstructingDoorBase(MaxDistance, 1, 10.0f, Result));
	TestEqual(TEXT("and the goal is untouched"), MaxDistance, 5.0f);

	// Arm 2: only door states 1 and 3 obstruct.
	MaxDistance = 100.0f;
	TestFalse(TEXT("state 0 does not obstruct"),
		Guard->OnObstructingDoorBase(MaxDistance, 0, 10.0f, Result));
	TestFalse(TEXT("state 2 does not either"),
		Guard->OnObstructingDoorBase(MaxDistance, 2, 10.0f, Result));
	TestEqual(TEXT("and neither wrote the goal"), MaxDistance, 100.0f);

	// Arm 3: a clearance under `_DAT_104493d0` = 0.1 answers -1 and leaves the goal alone.
	Result = EResult::Ok;
	TestTrue(TEXT("state 1 with no room obstructs"),
		Guard->OnObstructingDoorBase(MaxDistance, 1, 0.05f, Result));
	TestEqual(TEXT("with the illegal result"), static_cast<int32>(Result), -1);
	TestEqual(TEXT("and the goal untouched"), MaxDistance, 100.0f);

	// Arm 4: the goal is shortened to the clearance and the result is 0.
	Result = EResult::Illegal;
	TestTrue(TEXT("state 3 with room obstructs"),
		Guard->OnObstructingDoorBase(MaxDistance, 3, 12.5f, Result));
	TestEqual(TEXT("with the ok result"), static_cast<int32>(Result), 0);
	TestEqual(TEXT("and the goal shortened to the clearance"), MaxDistance, 12.5f);
	// Exactly 0.1 is NOT under the threshold.
	MaxDistance = 100.0f;
	Result = EResult::Illegal;
	TestTrue(TEXT("exactly 0.1 of clearance takes the shorten arm"),
		Guard->OnObstructingDoorBase(MaxDistance, 1, 0.1f, Result));
	TestEqual(TEXT("with the ok result"), static_cast<int32>(Result), 0);

	// `0x1039aaf0` / `0x1039ab10` — `CNPC_VMingXiao`'s `m_bBlockedByFriend` setter and getter.
	FElysiumNpcWorldBuilder MingBuilder(TEXT("npc_kernel_motor_blocked"), 4307);
	MingBuilder.AddNpcOfClass(TEXT("ming"), FVector::ZeroVector, TEXT("CNPC_VMingXiao"));
	FElysiumNpcWorldFixture MingFixture(MoveTemp(MingBuilder));
	FElysiumNpcMingXiao* Ming = MingFixture.NpcAs<FElysiumNpcMingXiao>(TEXT("ming"));
	if (!TestNotNull(TEXT("the Ming Xiao spawned"), Ming))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Ming });
	TestFalse(TEXT("blocked-by-friend starts clear"), Ming->BlockedByFriend());
	Ming->SetBlockedByFriend(true);
	TestTrue(TEXT("and takes what it is given"), Ming->BlockedByFriend());
	Ming->SetBlockedByFriend(false);
	TestFalse(TEXT("and back"), Ming->BlockedByFriend());
	return true;
}

// --- The jump chain -------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotorJumpChainTest,
	"Elysium.Arm.NpcKernelMotor.JumpChain", GElysiumNpcKernelMotorFlags)
bool FElysiumNpcKernelMotorJumpChainTest::RunTest(const FString&)
{
	// The two `SetupJump` species, each its class's own body (story 5 step 4): `0x10361a70` rises
	// 100 (`_DAT_104a9310`) and `0x103b1300` rises 400 (`_DAT_104c614c`). Neither rise is observable
	// while the hint-origin seam refuses every commit, which is what the gates below pin.
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_motor_jump"), 4308);
	// The AsianVampire's jump chain, with the SheriffMan and the ChangBros whose setup bodies sit
	// beside it.
	Builder.AddNpcOfClass(TEXT("guard"), FVector::ZeroVector, TEXT("CNPC_VAsianVampire"));
	Builder.AddNpcOfClass(TEXT("sheriff"), FVector(400.0, 0.0, 0.0), TEXT("CNPC_VSheriffMan"));
	Builder.AddNpcOfClass(TEXT("chang"), FVector(800.0, 0.0, 0.0), TEXT("CNPC_VChangBros"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpcAsianVampire* Guard = Fixture.NpcAs<FElysiumNpcAsianVampire>(TEXT("guard"));
	FElysiumNpcSheriffMan* SheriffNpc = Fixture.NpcAs<FElysiumNpcSheriffMan>(TEXT("sheriff"));
	FElysiumNpcChangBros* ChangNpc = Fixture.NpcAs<FElysiumNpcChangBros>(TEXT("chang"));
	FElysiumPlayer* Player = Fixture.Player();
	if (!TestNotNull(TEXT("guard"), Guard) || !TestNotNull(TEXT("sheriff"), SheriffNpc)
		|| !TestNotNull(TEXT("chang"), ChangNpc) || !TestNotNull(TEXT("player"), Player))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard, SheriffNpc, ChangNpc });

	// `SetupJump` and `SetupSuperJump` both take a float gate and do NOTHING at 0.0.
	const int32 CommitsBefore = Guard->MotorSeams.SetupJumpCommits
		+ SheriffNpc->MotorSeams.SetupJumpCommits + ChangNpc->MotorSeams.SetupJumpCommits;
	Guard->AsianVampireSetupJump(0.0f);
	SheriffNpc->SheriffManSetupJump(0.0f);
	ChangNpc->SetupSuperJump(0.0f);
	TestEqual(TEXT("neither setup ran at a zero gate"), Guard->MotorSeams.SetupJumpCommits
		+ SheriffNpc->MotorSeams.SetupJumpCommits + ChangNpc->MotorSeams.SetupJumpCommits, CommitsBefore);
	// With the gate open they still refuse: the hint store carries no origins, and retail would
	// dereference a null `m_pHintNode` here rather than check it.
	Guard->AsianVampireSetupJump(1.0f);
	SheriffNpc->SheriffManSetupJump(1.0f);
	ChangNpc->SetupSuperJump(1.0f);
	TestEqual(TEXT("so the three jump words are untouched"),
		Guard->JumpHeight + SheriffNpc->JumpHeight + ChangNpc->JumpHeight, 0.f);

	// `CNPC_VAsianVampire::GetJumpSchedule` `0x10362430`: schedule 0x15b when the closest player is
	// more than `_DAT_104a9314` = 40 SOURCE units BELOW this NPC, else 0x15a.
	Guard->Origin = PortUnits(0.0, 0.0, 100.0);
	Player->Origin = PortUnits(0.0, 0.0, 100.0);
	Guard->Senses.Memory.ClosestPlayer = Player->Handle;
	TestEqual(TEXT("level with the player is the across schedule"), Guard->GetJumpSchedule(),
		0x15a);
	Player->Origin = PortUnits(0.0, 0.0, 59.0);   // 41 units below
	TestEqual(TEXT("41 units below is the down schedule"), Guard->GetJumpSchedule(), 0x15b);
	Player->Origin = PortUnits(0.0, 0.0, 60.0);   // exactly 40 below — NOT strictly more
	TestEqual(TEXT("exactly 40 below is not"), Guard->GetJumpSchedule(), 0x15a);
	Guard->Senses.Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();
	TestEqual(TEXT("and with no closest player it is the across schedule"),
		Guard->GetJumpSchedule(), 0x15a);

	// `IsPosNearStoredJumpPositions` `0x103618a0` — a 2-D distance under `_DAT_104a9308` = 30, over
	// the two-entry ring, and the Z term is ignored.
	Guard->LastJumpPosition[0] = FVector(0.0, 0.0, 0.0);
	Guard->LastJumpPosition[1] = FVector(500.0, 0.0, 0.0);
	TestTrue(TEXT("29.9 units away in 2-D is near"),
		Guard->IsPosNearStoredJumpPositions(FVector(29.9, 0.0, 0.0)));
	TestFalse(TEXT("30 exactly is not — the compare is strict"),
		Guard->IsPosNearStoredJumpPositions(FVector(30.0, 0.0, 0.0)));
	TestTrue(TEXT("the second ring entry counts too"),
		Guard->IsPosNearStoredJumpPositions(FVector(500.0, 10.0, 0.0)));
	TestTrue(TEXT("and Z is ignored entirely"),
		Guard->IsPosNearStoredJumpPositions(FVector(0.0, 0.0, 9000.0)));

	// The ring write pairs with it, and refuses an index that names no hint (entity 3 is not one).
	Guard->LastJumpPositionIdx = 0;
	Guard->AddHintToStoredJumpPositions(3);
	TestEqual(TEXT("the ring index did not move — no hint origin to store"),
		Guard->LastJumpPositionIdx, 0);

	// `SelectJumpbaseNode` `0x10361730` — the global hint list is this world's, which authors no
	// hint, so the nearest type-18000 node is nobody.
	TestEqual(TEXT("no jumpbase node is found"), Guard->SelectJumpbaseNode(), INDEX_NONE);
	TArray<int32> Hints;
	TestTrue(TEXT("the hint list answers the world's"), Guard->NavAllHintNodes(Hints));
	TestEqual(TEXT("which is empty"), Hints.Num(), 0);

	// `StationaryForTooLong` `0x10362670` and `UpdateMovedTimeStamp` `0x10362540`.
	const double Now = Fixture.World.NowSeconds();
	Guard->MovedTimeStamp = Now;
	TestFalse(TEXT("just moved is not stationary"), Guard->StationaryForTooLong());
	Guard->MovedTimeStamp = Now - 3.0;
	TestTrue(TEXT("exactly 3 seconds IS — the compare is inclusive"),
		Guard->StationaryForTooLong());
	Guard->MovedTimeStamp = Now - 2.99;
	TestFalse(TEXT("2.99 is not"), Guard->StationaryForTooLong());

	Guard->Origin = PortUnits(0.0, 0.0, 0.0);
	Guard->MovedPosition = FVector(0.0, 0.0, 0.0);
	Guard->MovedTimeStamp = -5.0;
	Guard->UpdateMovedTimeStamp();
	TestEqual(TEXT("a body that has not moved does not re-stamp"), Guard->MovedTimeStamp, -5.0);
	Guard->Origin = PortUnits(40.0, 0.0, 0.0);   // exactly _DAT_104a931c = 40, which is NOT past it
	Guard->UpdateMovedTimeStamp();
	TestEqual(TEXT("exactly 40 units of travel does not either — the compare is strict"),
		Guard->MovedTimeStamp, -5.0);
	Guard->Origin = PortUnits(41.0, 0.0, 0.0);   // past 40
	Guard->UpdateMovedTimeStamp();
	TestTrue(TEXT("41 units of travel re-stamps"),
		FMath::IsNearlyEqual(Guard->MovedTimeStamp, Fixture.World.NowSeconds(), 1e-6));
	TestTrue(TEXT("and refreshes the remembered position"),
		Guard->MovedPosition.Equals(FVector(41.0, 0.0, 0.0), 1e-3));
	return true;
}

// --- `CNPC_VSabbatLeader` and `CNPC_VChangBros` ---------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotorSpeciesProbesTest,
	"Elysium.Arm.NpcKernelMotor.SpeciesProbes", GElysiumNpcKernelMotorFlags)
bool FElysiumNpcKernelMotorSpeciesProbesTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_motor_species"), 4309);
	Builder.AddNpc(TEXT("leader"), FVector::ZeroVector, TEXT("npc_VSabbatLeader"));
	Builder.AddNpcOfClass(TEXT("chang"), FVector(0.0, 400.0, 0.0), TEXT("CNPC_VChangBros"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpcSabbatLeader* Leader = Fixture.NpcAs<FElysiumNpcSabbatLeader>(TEXT("leader"));
	FElysiumNpcChangBros* Chang = Fixture.NpcAs<FElysiumNpcChangBros>(TEXT("chang"));
	if (!TestNotNull(TEXT("chang"), Chang))
	{
		return false;
	}
	FElysiumPlayer* Player = Fixture.Player();
	if (!TestNotNull(TEXT("leader"), Leader) || !TestNotNull(TEXT("player"), Player))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Leader });
	TestTrue(TEXT("the leader resolves to CNPC_VSabbatLeader"),
		Leader->AsSpecies<FElysiumNpcSabbatLeader>() != nullptr);

	Leader->Origin = PortUnits(0.0, 0.0, 0.0);
	Player->Origin = PortUnits(20.0, 0.0, 0.0);
	Leader->Senses.Memory.ClosestPlayer = Player->Handle;

	// `CheckStuck` `0x103ab580` — both boxes come from `m_Collision`'s OBB slots: the leader's is its
	// standing hull's row (HUMAN_HULL, `(-13,-13,0)..(13,13,72)`), the player's the `CGameMovement`
	// standing hull (`0x1011e0d0`, `(-16,-16,0)..(16,16,72)`).
	FVector Mins(1.0, 1.0, 1.0);
	FVector Maxs(2.0, 2.0, 2.0);
	FVector RowMins = FVector::ZeroVector;
	FVector RowMaxs = FVector::ZeroVector;
	Leader->RetailHullExtents(Leader->HullKind, FElysiumNpcBase::EElysiumHullExtents::Full, RowMins, RowMaxs);
	TestTrue(TEXT("an NPC's m_Collision answers"), FElysiumNpcBase::RetailCollisionExtents(*Leader, Mins, Maxs));
	TestTrue(TEXT("with its standing hull's FULL row (0x10273070)"), Mins == RowMins && Maxs == RowMaxs);
	Leader->bIsUsingSmallHull = true;
	Leader->RetailHullExtents(Leader->HullKind, FElysiumNpcBase::EElysiumHullExtents::Small, RowMins, RowMaxs);
	FElysiumNpcBase::RetailCollisionExtents(*Leader, Mins, Maxs);
	TestTrue(TEXT("and its SMALL row while m_fIsUsingSmallHull stands (0x10273180)"),
		Mins == RowMins && Maxs == RowMaxs);
	Leader->bIsUsingSmallHull = false;
	TestTrue(TEXT("the player's box answers"), FElysiumNpcBase::RetailCollisionExtents(*Player, Mins, Maxs));
	TestEqual(TEXT("0x1011e0d0: standing mins"), Mins, FVector(-16.0, -16.0, 0.0));
	TestEqual(TEXT("0x1011e0d0: standing maxs"), Maxs, FVector(16.0, 16.0, 72.0));
	Fixture.Services.bPlayerDucking = true;
	FElysiumNpcBase::RetailCollisionExtents(*Player, Mins, Maxs);
	TestEqual(TEXT("ducked: the same footprint at half the height"), Maxs, FVector(16.0, 16.0, 36.0));
	Fixture.Services.bPlayerDucking = false;

	// The body, 20 units apart on X, both on z 0: the Z spans overlap (the leader's span is built from
	// the PLAYER's extents -- the reproduced retail bug), the 2-D centre distance 20 is under the radii
	// sum `|(32,32)|/2 + |(26,26)|/2`, and the push is `d * (sum + 1)`, NOT normalised, from the
	// player's centre, lifted 5.0 (`_DAT_10454110`).
	Leader->CheckStuck();
	const double SumRadius = FMath::Sqrt(2.0 * 32.0 * 32.0) * 0.5 + FMath::Sqrt(2.0 * 26.0 * 26.0) * 0.5;
	const FVector Expected(20.0 + -20.0 * (SumRadius + 1.0), 0.0, 36.0 + 5.0);
	TestTrue(TEXT("0x103ab580: the leader is pushed off the player's centre by d * (sum + 1)"),
		Leader->Origin.Equals(PortUnits(Expected.X, Expected.Y, Expected.Z), 0.05));

	// `PlayerInNoJumpZone` `0x103a9e70` — the hint list is empty, so nobody is ever inside one.
	TestFalse(TEXT("no player is in a no-jump zone"), Leader->PlayerInNoJumpZone());

	// `SetJumpVelocityTowardPlayer` `0x103aad40` — the arc solver is a SEAM, and retail's own
	// out-parameter is the body's current velocity, which it then assigns back unchanged.
	Leader->Velocity = FVector(7.0, 8.0, 9.0);
	const int32 SolvesBefore = Leader->MotorSeams.JumpArcSolves;
	Leader->SetJumpVelocityTowardPlayer();
	TestEqual(TEXT("the arc solver was asked"), Leader->MotorSeams.JumpArcSolves,
		SolvesBefore + 1);
	TestEqual(TEXT("and the velocity is unchanged, which is retail's own answer for an untouched "
		"out-parameter"), Leader->Velocity, FVector(7.0, 8.0, 9.0));
	// With no closest player the body does not even reach the solver.
	Leader->Senses.Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();
	const int32 SolvesAfter = Leader->MotorSeams.JumpArcSolves;
	Leader->SetJumpVelocityTowardPlayer();
	TestEqual(TEXT("no closest player, no solve"), Leader->MotorSeams.JumpArcSolves, SolvesAfter);

	// `CNPC_VChangBros::CheckForJumpAttack` `0x1036c8d0`. On a ChangBros: `m_ChangType != 0` refuses outright, and the sector seam answers
	// 4 — the value that CLOSES the gate.
	Chang->ChangType = 1;
	TestFalse(TEXT("a non-zero ChangType refuses"), Chang->CheckForJumpAttack());
	Chang->ChangType = 0;
	Chang->Senses.Memory.ClosestPlayer = Player->Handle;
	TestEqual(TEXT("the sector seam answers 4"), Chang->ChangBrosSector(FVector::ZeroVector), 4);
	TestFalse(TEXT("so the jump attack is refused rather than allowed on a guess"),
		Chang->CheckForJumpAttack());
	Chang->Senses.Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();
	TestFalse(TEXT("and with no closest player it refuses too"), Chang->CheckForJumpAttack());
	return true;
}

// --- `CNPC_VTzimisce`'s slot 410 branch -----------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotorTranslateGoalTest,
	"Elysium.Arm.NpcKernelMotor.TranslateNavGoal", GElysiumNpcKernelMotorFlags)
bool FElysiumNpcKernelMotorTranslateGoalTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_motor_goal"), 4310);
	Builder.AddNpcOfClass(TEXT("guard"), FVector::ZeroVector, TEXT("CNPC_VTzimisce"));
	Builder.AddNpc(TEXT("enemy"), FVector(300.0, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpcTzimisce* Guard = Fixture.NpcAs<FElysiumNpcTzimisce>(TEXT("guard"));
	FElysiumNpc* Enemy = Fixture.Npc(TEXT("enemy"));
	if (!TestNotNull(TEXT("guard"), Guard) || !TestNotNull(TEXT("enemy"), Enemy))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard, Enemy });

	const FVector Goal(11.0, 22.0, 33.0);
	FVector Out = FVector::ZeroVector;

	// `CNPC_VTzimisce::vfunc410` `0x103bf580`, path mode 0 — neither 1 nor 2, so the caller's goal
	// comes back untouched.
	Guard->PathMode = 0;
	TestFalse(TEXT("path mode 0 does not translate"),
		Guard->TranslateNavGoalPositionTzimisce(Goal, Out));
	TestEqual(TEXT("and returns the caller's goal"), Out, Goal);

	// Path mode 1 with an enemy: the enemy's position replaces the goal.
	Guard->PathMode = 1;
	Enemy->Origin = PortUnits(50.0, 60.0, 70.0);
	Guard->BaseMemory.Enemy = Enemy->Handle;
	TestTrue(TEXT("path mode 1 with an enemy translates"),
		Guard->TranslateNavGoalPositionTzimisce(Goal, Out));
	TestTrue(TEXT("to the enemy's position"), Out.Equals(FVector(50.0, 60.0, 70.0), 1e-3));

	// Path mode 1 with NO enemy falls THROUGH to the pickup arm rather than returning the goal —
	// retail's own control flow, and the reason the pickup test is not nested under mode 2 alone.
	Guard->BaseMemory.Enemy = FElysiumEntityHandle::Invalid();
	Guard->PickupTarget = Enemy->Handle;
	Guard->PickupTargetPos = FVector(1.0, 2.0, 3.0);
	TestTrue(TEXT("path mode 1 with no enemy falls through to the pickup arm"),
		Guard->TranslateNavGoalPositionTzimisce(Goal, Out));
	TestEqual(TEXT("and answers the pickup position"), Out, FVector(1.0, 2.0, 3.0));

	// Path mode 2 goes straight to the pickup arm, and refuses when the handle is dead.
	Guard->PathMode = 2;
	TestTrue(TEXT("path mode 2 answers the pickup position"),
		Guard->TranslateNavGoalPositionTzimisce(Goal, Out));
	TestEqual(TEXT("the pickup position"), Out, FVector(1.0, 2.0, 3.0));
	Guard->PickupTarget = FElysiumEntityHandle::Invalid();
	TestFalse(TEXT("with no pickup target it does not translate"),
		Guard->TranslateNavGoalPositionTzimisce(Goal, Out));
	TestEqual(TEXT("and the caller's goal comes back"), Out, Goal);
	return true;
}

// --- `IElysiumNpcMotor::MinStoppingDistanceUnits` -------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotorStoppingDistanceTest,
	"Elysium.Arm.NpcKernelMotor.MinStoppingDistance", GElysiumNpcKernelMotorFlags)
bool FElysiumNpcKernelMotorStoppingDistanceTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_motor_stop"), 4311);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	if (!TestNotNull(TEXT("guard"), Guard))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });

	// `CAI_Motor#16`: `max(0.5 * v^2 / decel, 10.0)` when the deceleration source
	// answers a positive number, and `_DAT_1044e664` = 10.0 when it does not. With no motor at all
	// the floor is the answer, and that floor is retail's own, not a port constant.
	TestEqual(TEXT("no motor answers retail's floor, 10.0"),
		Guard->MotorMinStoppingDistanceUnits(), 10.0f);
	return true;
}

// `CAI_BaseNPC::CheckOnGround` `0x1026e5e0`'s measurement over the floor facts seam
// (`IElysiumNpcMotor::SampleFloor`, story 6). The rule's gates are `NpcKernelMotor.Probes`'.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotorCheckOnGroundFloorTest,
	"Elysium.Arm.NpcKernelMotor.CheckOnGroundFloor", GElysiumNpcKernelMotorFlags)
bool FElysiumNpcKernelMotorCheckOnGroundFloorTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_motor_check_on_ground"), 4307);
	// Without a model key the leaf builds no body, and without a body no motor.
	FElysiumEntityDef& GuardDef = Builder.AddNpc(TEXT("guard"));
	GuardDef.Keys.Add(TEXT("model"), TEXT("models/character/npc/common/blueblood/male/Blueblood_Male.mdl"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder),
		[](FElysiumRecordingServices& Services) { Services.bProvideNpcMotor = true; });
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	FElysiumPlayer* Player = Fixture.Player();
	if (!TestNotNull(TEXT("guard"), Guard) || !TestNotNull(TEXT("player"), Player))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });
	Guard->CheckOnGroundTime = 0.0;
	const EElysiumNpcCond OnGround = static_cast<EElysiumNpcCond>(0x73);
	FElysiumRecordingNpcMotor* GuardMotor = nullptr;
	for (const TUniquePtr<FElysiumRecordingNpcMotor>& Each : Fixture.Services.NpcMotors)
	{
		if (Each->Owner == Guard->Handle)
		{
			GuardMotor = Each.Get();
		}
	}
	if (!TestNotNull(TEXT("the guard moves through a recording motor"), GuardMotor))
	{
		return false;
	}
	GuardMotor->bReportsFloor = true;
	GuardMotor->Floor = FElysiumRecordingNpcMotor::RetailStandingFloor();
	const int32 SamplesBefore = GuardMotor->FloorSamples;
	Guard->CheckOnGround();
	TestTrue(TEXT("CheckOnGround stamped its 0.5-second deadline"),
		FMath::IsNearlyEqual(Guard->CheckOnGroundTime, Fixture.World.NowSeconds() + 0.5, 1e-6));
	TestEqual(TEXT("and sampled the floor once"), GuardMotor->FloorSamples, SamplesBefore + 1);
	TestFalse(TEXT("standing on the static world leaves the ground condition untouched"),
		Guard->Cognition.Conditions.Has(OnGround));

	// The deadline gate: a second call inside the window does nothing at all.
	Guard->CheckOnGround();
	TestEqual(TEXT("a second call inside the 0.5-second window samples nothing"),
		GuardMotor->FloorSamples, SamplesBefore + 1);

	// A named floor entity is ground too (`tr.m_pEnt != GetGroundEntity()` -> slot 208; slots 208 /
	// 209 are still generated stubs, so the write is not read back here).
	Guard->CheckOnGroundTime = Fixture.World.NowSeconds();
	GuardMotor->Floor.GroundEntityHandle = Player->Handle;
	Guard->CheckOnGround();
	TestFalse(TEXT("standing on an entity is ground"), Guard->Cognition.Conditions.Has(OnGround));

	// Retail's trace reaches 4.0 units below the feet (`_DAT_10449148`): a floor further than that
	// is "fraction 1.0" -- condition 0x73 set (and slot 208 handed null).
	Guard->CheckOnGroundTime = Fixture.World.NowSeconds();
	GuardMotor->Floor = FElysiumRecordingNpcMotor::RetailStandingFloor();
	GuardMotor->Floor.FloorDistanceCm = 4.5f * ElysiumMove::U;
	Guard->CheckOnGround();
	TestTrue(TEXT("no floor within 4.0 units sets condition 0x73"), Guard->Cognition.Conditions.Has(OnGround));
	Guard->Cognition.Conditions.Clear(OnGround);
	Guard->CheckOnGroundTime = Fixture.World.NowSeconds();
	GuardMotor->Floor.FloorDistanceCm = 3.9f * ElysiumMove::U;
	Guard->CheckOnGround();
	TestFalse(TEXT("a floor inside the reach is ground"), Guard->Cognition.Conditions.Has(OnGround));

	// No movement component: the seam answers nothing and neither write runs.
	Guard->CheckOnGroundTime = Fixture.World.NowSeconds();
	GuardMotor->bReportsFloor = false;
	Guard->CheckOnGround();
	TestFalse(TEXT("a headless motor writes nothing"), Guard->Cognition.Conditions.Has(OnGround));
	GuardMotor->bReportsFloor = true;
	GuardMotor->Floor = FElysiumRecordingNpcMotor::RetailStandingFloor();
	return true;
}

// The character half of the kernel's trace filters (`KernelTraceKeepsCharacter`): the candidate's
// slot 91 `ShouldCollide` (`0x100b4de0`) runs ahead of the slot-68 vetoes, as `CTraceFilterNav`
// (`0x102e3110` / `0x102e32d0`) and `CTraceFilterSimple` run it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotorTraceFilterShouldCollideTest,
	"Elysium.Arm.NpcKernelMotor.TraceFilterShouldCollide", GElysiumNpcKernelMotorFlags)
bool FElysiumNpcKernelMotorTraceFilterShouldCollideTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_motor_trace_filter"), 4308);
	Builder.AddNpc(TEXT("guard"));
	Builder.AddNpc(TEXT("other"), FVector(200.0, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	FElysiumNpc* Other = Fixture.Npc(TEXT("other"));
	if (!TestNotNull(TEXT("guard"), Guard) || !TestNotNull(TEXT("other"), Other))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard, Other });

	FElysiumRetailTraceCharacter OtherHit;
	OtherHit.Entity = Other->Handle;
	OtherHit.Fraction = 0.4f;
	Fixture.Services.TraceRetailQuery = [&OtherHit](const FElysiumRetailTrace& Asking,
		FElysiumRetailTraceResult& Out)
	{
		Out.Fraction = 1.0f;
		Out.EndPosCm = Asking.EndCm;
		Out.Characters.Add(OtherHit);
		return true;
	};
	auto Run = [Guard](int32 Mask)
	{
		FElysiumNpcBase::FKernelHullTrace Out;
		Guard->KernelHullTrace(FVector(0.0, 0.0, 10.0), FVector(100.0, 0.0, 10.0), FVector::ZeroVector,
			FVector::ZeroVector, Mask, Out);
		return Out.Fraction;
	};
	constexpr int32 MaskNpcSolid = 0x202400b;       // MONSTER set, 0x4000000 clear
	constexpr int32 DebrisOverride = 0x4000000;

	Other->CollisionGroup = 0;
	TestEqual(TEXT("a group-0 NPC is solid"), Run(MaskNpcSolid), 0.4f);
	Other->CollisionGroup = 1;   // COLLISION_GROUP_DEBRIS
	TestEqual(TEXT("slot 91 drops a debris-group NPC under a mask without 0x4000000"),
		Run(MaskNpcSolid), 1.0f);
	TestEqual(TEXT("and keeps it when the mask carries 0x4000000"), Run(MaskNpcSolid | DebrisOverride), 0.4f);
	Other->CollisionGroup = 0;

	Fixture.Services.TraceRetailQuery = nullptr;
	return true;
}
#endif  // WITH_DEV_AUTOMATION_TESTS
