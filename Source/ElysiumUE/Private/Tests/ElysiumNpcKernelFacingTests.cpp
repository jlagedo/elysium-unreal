#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcChangBros.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcAndreiBlood.h"
#include "Substrate/ElysiumNpcSabbatGunman.h"
#include "Substrate/ElysiumNpcMingXiao.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumNpcTestCensus.h"

// Story 29c-1, family **Facing** — the facing-target queue and the turn-activity ladder.
//
// Every threshold asserted here was read out of the pinned retail `vampire.dll`'s `.rdata` at the
// address the body cites, so a case that fails is a divergence from retail and not from an opinion.
// Where an input is a SEAM the case says so: it asserts that the seam is asked and that the refusal
// is the recovered one, which is the only honest assertion available until the seam has a source.

static constexpr EAutomationTestFlags GElysiumNpcKernelFacingFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// `SelectWeightedSequence(act) != -1` for a body that authors everything, so the ladder's
	// ORDER is what is under test rather than a body's clip set.
	bool AllSequences(int32)
	{
		return true;
	}
}

// --- The two turn ladders -----------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelFacingTurnLadderTest,
	"Elysium.Substrate.NpcKernelFacing.TurnLadder", GElysiumNpcKernelFacingFlags)
bool FElysiumNpcKernelFacingTurnLadderTest::RunTest(const FString&)
{
	using FPick = FElysiumNpcBase::FTurnActivityPick;

	auto Base = [](float Delta) { return FElysiumNpcBase::TurnActivityBaseLadder(Delta, AllSequences); };
	auto Troika = [](float Delta)
	{
		return FElysiumNpc::TurnActivityTroikaLadder(Delta, AllSequences);
	};

	// `CAI_BaseNPC::SetTurnActivity` `0x10289d10`: a `[-100, -80)` band for ACT_90_RIGHT, an
	// `[80, 100)` band for ACT_90_LEFT, `|d| >= 160` for ACT_180_LEFT, then the loose `< -45` /
	// `>= 45` pair. Negative turns right, positive turns left.
	TestEqual(TEXT("base: -90 is ACT_90_RIGHT"), Base(-90.f).Activity, 0xa2);
	TestTrue(TEXT("base: and tags the turn memory bit"), Base(-90.f).bTagsTurnMemory);
	TestEqual(TEXT("base: -80 exactly falls out of the 90 band and turns right"),
		Base(-80.f).Activity, 0x3c);
	TestEqual(TEXT("base: -101 is past the band's far edge and turns right"),
		Base(-101.f).Activity, 0x3c);
	TestEqual(TEXT("base: 80 exactly is ACT_90_LEFT"), Base(80.f).Activity, 0xa1);
	TestEqual(TEXT("base: 100 exactly is past it and turns left"), Base(100.f).Activity, 0x3b);
	TestEqual(TEXT("base: 160 exactly is ACT_180_LEFT"), Base(160.f).Activity, 0x9d);
	TestEqual(TEXT("base: -200 is ACT_180_LEFT too — the rung is on |d|"), Base(-200.f).Activity,
		0x9d);
	TestEqual(TEXT("base: -46 turns right"), Base(-46.f).Activity, 0x3c);
	TestEqual(TEXT("base: -45 exactly does not"), Base(-45.f).Activity, 1);
	TestEqual(TEXT("base: 45 exactly turns left"), Base(45.f).Activity, 0x3b);
	TestEqual(TEXT("base: 44 does not"), Base(44.f).Activity, 1);
	TestFalse(TEXT("base: the loose turn rungs do NOT tag the memory bit"),
		Base(45.f).bTagsTurnMemory);
	TestFalse(TEXT("base: nor does the ACT_IDLE tail"), Base(0.f).bTagsTurnMemory);

	// `CAI_BaseNPCTroika::SetTurnActivity` `0x10297640` — the body slot 572 carries for every
	// spawnable species. Wider bands, a `-15/15` loose pair, and every pick tags the memory bit.
	TestEqual(TEXT("troika: -100 is ACT_90_RIGHT"), Troika(-100.f).Activity, 0xa2);
	TestEqual(TEXT("troika: -140 exactly is still inside the band"), Troika(-140.f).Activity, 0xa2);
	TestEqual(TEXT("troika: 100 is ACT_90_LEFT"), Troika(100.f).Activity, 0xa1);
	TestEqual(TEXT("troika: -200 prefers ACT_180_RIGHT"), Troika(-200.f).Activity, 0x9e);
	TestEqual(TEXT("troika: 200 prefers ACT_180_LEFT"), Troika(200.f).Activity, 0x9d);
	TestEqual(TEXT("troika: 140 exactly reaches neither 180 rung and turns left"),
		Troika(140.f).Activity, 0x3b);
	TestEqual(TEXT("troika: -20 turns right"), Troika(-20.f).Activity, 0x3c);
	TestEqual(TEXT("troika: -15 exactly does not"), Troika(-15.f).Activity, 1);
	TestEqual(TEXT("troika: 15 exactly turns left"), Troika(15.f).Activity, 0x3b);
	TestTrue(TEXT("troika: the loose turn rungs DO tag the memory bit, unlike the base line"),
		Troika(15.f).bTagsTurnMemory);
	TestFalse(TEXT("troika: the ACT_IDLE tail still does not"), Troika(0.f).bTagsTurnMemory);

	// The 180 rungs are ORDERED PAIRS: the sign picks which is asked first, and the other is the
	// fallback when the body authors no clip for it.
	auto No180Right = [](int32 Activity) { return Activity != 0x9e; };
	auto No180Left = [](int32 Activity) { return Activity != 0x9d; };
	TestEqual(TEXT("troika: -200 falls back to ACT_180_LEFT when the right one is unauthored"),
		FElysiumNpc::TurnActivityTroikaLadder(-200.f, No180Right).Activity, 0x9d);
	TestEqual(TEXT("troika: 200 falls back to ACT_180_RIGHT when the left one is unauthored"),
		FElysiumNpc::TurnActivityTroikaLadder(200.f, No180Left).Activity, 0x9e);

	// An unauthored rung is skipped entirely and the ladder keeps walking, which is what the
	// `SelectWeightedSequence != -1` guard is for.
	auto No90Right = [](int32 Activity) { return Activity != 0xa2; };
	const FPick Fallen = FElysiumNpcBase::TurnActivityBaseLadder(-90.f, No90Right);
	TestEqual(TEXT("base: a body with no ACT_90_RIGHT walks on to the loose right turn"),
		Fallen.Activity, 0x3c);
	TestFalse(TEXT("and picks up the loose rung's untagged memory"), Fallen.bTagsTurnMemory);

	// A body that authors nothing lands on retail's tail.
	TestEqual(TEXT("a body that authors nothing lands on ACT_IDLE"),
		FElysiumNpc::TurnActivityTroikaLadder(-200.f, [](int32) { return false; }).Activity, 1);

	return true;
}

// --- Slot 572's own body, gate and all ----------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelFacingSetTurnActivityTest,
	"Elysium.Substrate.NpcKernelFacing.SetTurnActivity", GElysiumNpcKernelFacingFlags)
bool FElysiumNpcKernelFacingSetTurnActivityTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_facing_turn"), 4211);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	TestNotNull(TEXT("the guard spawned"), Guard);
	if (Guard == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });

	// The gate: `cvar(0x109247ec) || m_bAllowTurningAnims`. The cvar is `debug_turning`, shipped
	// "0", so the authored key is the live half.
	TestFalse(TEXT("debug_turning ships off"), Guard->TurningAnimsEnabled());

	Guard->BaseScheduleHost.MemoryBits = 0;
	Guard->IdealActivityNumber = 0;
	Guard->bAllowTurningAnims = false;
	Guard->Angles.Y = 0.0; Guard->MotorIdealYaw = -100.f;   // `DeltaIdealYaw` 0x102e1f90: AngleDiff(ideal, AngleMod(0))
	Guard->SetTurnActivity();
	TestEqual(TEXT("with the gate closed the body still lands on ACT_IDLE"),
		Guard->IdealActivityNumber, 1);
	TestEqual(TEXT("and tags nothing"), static_cast<int32>(Guard->BaseScheduleHost.MemoryBits & 0x2000),
		0);

	// With the gate open the ladder runs — and stops at its tail, because the sequence seam answers
	// -1 for every activity. That refusal is the recovered one: retail's own answer for a body that
	// authors no turn clips is ACT_IDLE.
	TestEqual(TEXT("the sequence seam answers -1 for every activity"),
		Guard->SelectWeightedSequenceForActivity(0xa2), -1);
	Guard->bAllowTurningAnims = true;
	Guard->IdealActivityNumber = 0;
	Guard->SetTurnActivity();
	TestEqual(TEXT("so the open gate lands on ACT_IDLE too"), Guard->IdealActivityNumber, 1);
	TestEqual(TEXT("and still tags nothing"),
		static_cast<int32>(Guard->BaseScheduleHost.MemoryBits & 0x2000), 0);

	// `CAI_Motor::DeltaIdealYaw` (`0x102e1f90`) over its two live inputs (story 8 wave 2): the body
	// standing on its ideal yaw is aligned.
	Guard->Angles.Y = 0.0; Guard->MotorIdealYaw = 0.f;   // `DeltaIdealYaw` 0x102e1f90: AngleDiff(ideal, AngleMod(0))
	TestEqual(TEXT("a body on its ideal yaw answers the aligned 0"),
		Guard->MotorDeltaIdealYaw(), 0.f);

	// `FacingIdeal` `0x10278c80` — `|delta| <= 0.006`, and the tolerance is a DOUBLE in `.rdata`.
	TestTrue(TEXT("aligned is facing ideal"), Guard->FacingIdeal());
	Guard->Angles.Y = 0.0; Guard->MotorIdealYaw = 0.006f;   // `DeltaIdealYaw` 0x102e1f90: AngleDiff(ideal, AngleMod(0))
	TestTrue(TEXT("the tolerance is inclusive — the listing's compare is <=, not <"),
		Guard->FacingIdeal());
	Guard->Angles.Y = 0.0; Guard->MotorIdealYaw = 0.0061f;   // `DeltaIdealYaw` 0x102e1f90: AngleDiff(ideal, AngleMod(0))
	TestFalse(TEXT("just past it is not"), Guard->FacingIdeal());
	Guard->Angles.Y = 0.0; Guard->MotorIdealYaw = -0.005f;   // `DeltaIdealYaw` 0x102e1f90: AngleDiff(ideal, AngleMod(0))
	TestTrue(TEXT("and the test is on the absolute value"), Guard->FacingIdeal());

	return true;
}

// --- The facing-target queue, slots 517/518/519/520/526 -----------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelFacingTargetsTest,
	"Elysium.Substrate.NpcKernelFacing.FacingTargets", GElysiumNpcKernelFacingFlags)
bool FElysiumNpcKernelFacingTargetsTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_facing_queue"), 4212);
	Builder.AddNpc(TEXT("guard"));
	Builder.AddNpc(TEXT("subject"), FVector(300.0, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	FElysiumNpc* Subject = Fixture.Npc(TEXT("subject"));
	TestNotNull(TEXT("the guard spawned"), Guard);
	TestNotNull(TEXT("the subject spawned"), Subject);
	if (Guard == nullptr || Subject == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard, Subject });

	// Both live overloads are the same shape: a cvar gate, then a tail jump into the motor. The
	// cvar at `0x10924f74` is `debug_allow_move_facing`, shipped "1", so both reach the motor on
	// their own overload (518 -> motor slot 13, 517 -> 12). Slot 519 (no dispatch site in
	// the DLL) is gone (story 6).
	TestTrue(TEXT("debug_allow_move_facing ships on"), Guard->FacingTargetsEnabled());
	Guard->FacingTargetRequests.Reset();
	Guard->AddFacingTarget(FVector(1.0, 2.0, 3.0), 1.f, 2.f, 3.f);
	Guard->AddFacingTarget(Subject, FVector(1.0, 2.0, 3.0), 1.f, 2.f, 3.f);
	TestEqual(TEXT("so slots 518 and 517 each queue one"), Guard->FacingTargetRequests.Num(), 2);
	if (Guard->FacingTargetRequests.Num() == 2)
	{
		TestEqual(TEXT("518 reaches motor slot 13"), Guard->FacingTargetRequests[0].MotorSlot, 13);
		TestEqual(TEXT("517 reaches motor slot 12"), Guard->FacingTargetRequests[1].MotorSlot, 12);
	}
	// Cleared, both add nothing — retail's refusal arm.
	ElysiumNpcTunables::SetConVar(ElysiumNpcTunables::EConVar::DebugAllowMoveFacing, 0.f);
	Guard->FacingTargetRequests.Reset();
	Guard->AddFacingTarget(FVector(1.0, 2.0, 3.0), 1.f, 2.f, 3.f);
	Guard->AddFacingTarget(Subject, FVector(1.0, 2.0, 3.0), 1.f, 2.f, 3.f);
	TestEqual(TEXT("with the cvar cleared slots 517 and 518 queue nothing"),
		Guard->FacingTargetRequests.Num(), 0);
	ElysiumNpcTunables::ResetConVars();

	// The seam below them, exercised directly: slot 519 (gone) tail-jumped to motor slot 14, the
	// ENTITY form; the queue still takes that form.
	FElysiumNpcBase::FFacingTargetRequest Request;
	Request.MotorSlot = 14;
	Request.Target = Subject->Handle;
	Request.Importance = 1.0f;
	Request.Duration = 1.5f;
	Guard->MotorAddFacingTarget(Request);
	TestEqual(TEXT("the motor seam records the request"), Guard->FacingTargetRequests.Num(), 1);
	TestEqual(TEXT("on the overload slot 519 tail-jumps to"),
		Guard->FacingTargetRequests[0].MotorSlot, 14);
	TestTrue(TEXT("carrying the entity it was asked to face"),
		Guard->FacingTargetRequests[0].Target == Subject->Handle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelFacingQueueTest,
	"Elysium.Substrate.NpcKernelFacing.FacingQueue", GElysiumNpcKernelFacingFlags)
bool FElysiumNpcKernelFacingQueueTest::RunTest(const FString&)
{
	// `m_facingQueue` (motor +0x54) as slot 15 `0x102e2180` blends it (0019/6 fix 3).
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_facing_queue_blend"), 4213);
	Builder.AddNpc(TEXT("guard"));
	Builder.AddNpc(TEXT("subject"), FVector(300.0, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	FElysiumNpc* Subject = Fixture.Npc(TEXT("subject"));
	if (!TestNotNull(TEXT("the guard spawned"), Guard) || !TestNotNull(TEXT("the subject spawned"), Subject))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard, Subject });
	const FVector G = Guard->Origin;
	const double Tol = 1e-3;

	// Two targets blended. A (importance 1) then B (importance 0.5), no ramp: `w` is the importance.
	// `acc = (B - self) * 0.5 + acc * 0.5` with the delta RAW in Source units (its normalise is
	// discarded), so the far-reaching B out-pulls A's unit accumulator.
	Guard->FacingQueue.Reset();
	const FVector A = G + FVector(1000.0, 0.0, 0.0);
	const FVector B = G + FVector(0.0, 1000.0, 0.0);
	Guard->AddFacingTarget(A, 1.0f, 10.0f, 0.0f);
	Guard->AddFacingTarget(B, 0.5f, 1.0f, 0.0f);
	TestEqual(TEXT("two entries queued"), Guard->FacingQueue.Num(), 2);
	double RangeCm = 0.0;
	const FVector Blend = Guard->MotorFacingQueueBlend(RangeCm);
	const FVector Expected = FVector(0.5, 0.5 * 1000.0 / ElysiumMove::U, 0.0).GetSafeNormal();
	TestTrue(TEXT("the blend is retail's raw-delta average"), Blend.Equals(Expected, Tol));
	TestTrue(TEXT("not the normalised-delta average"), Blend.Y > Blend.X * 2.0);
	TestEqual(TEXT("at the farthest contributing range"), RangeCm, 1000.0, Tol);
	Guard->MotorHandFacingTarget();
	TestTrue(TEXT("the body is handed the blended point"), Guard->FacingTargetHanded.IsSet()
		&& Guard->FacingTargetHanded.GetValue().Equals(G + Expected * 1000.0, 0.01));

	// A re-add of the same position replaces its record (the humanoid look queue's shape, a dead row).
	Guard->AddFacingTarget(A, 1.0f, 10.0f, 0.0f);
	TestEqual(TEXT("a re-added position replaces its record"), Guard->FacingQueue.Num(), 2);
	TestTrue(TEXT("and moves it to the tail"), Guard->FacingQueue.Last().PositionCm == A);

	// Slot 7 `0x102e11f0` never reads +0x54: every entry stands.
	Guard->ClearFacingTarget();
	TestEqual(TEXT("slot 7 leaves the queue whole"), Guard->FacingQueue.Num(), 2);

	// Expiry: past B's end stamp slot 15's compaction drops it and A alone is faced.
	Fixture.Advance(Guard->World->NowSeconds() + 2.0);
	const FVector AfterExpiry = Guard->MotorFacingQueueBlend(RangeCm);
	TestEqual(TEXT("the expired entry is compacted away"), Guard->FacingQueue.Num(), 1);
	TestTrue(TEXT("and the survivor is faced alone"), AfterExpiry.Equals(FVector(1.0, 0.0, 0.0), Tol));

	// One moving entity target: the ENTITY form (motor slot 14) follows its entity every blend.
	Guard->FacingQueue.Reset();
	FElysiumNpcBase::FFacingTargetRequest Follow;
	Follow.MotorSlot = 14;
	Follow.Target = Subject->Handle;
	Follow.Importance = 1.0f;
	Follow.Duration = 10.0f;
	Guard->MotorAddFacingTarget(Follow);
	const FVector Before = Guard->MotorFacingQueueBlend(RangeCm);
	TestTrue(TEXT("faces the entity where it stands"),
		Before.Equals((Subject->EyePosition() - G).GetSafeNormal(), Tol));
	Subject->Origin = G + FVector(0.0, 800.0, 0.0);
	const FVector After = Guard->MotorFacingQueueBlend(RangeCm);
	TestTrue(TEXT("and where it moved to, on the next blend"),
		After.Equals((Subject->EyePosition() - G).GetSafeNormal(), Tol));
	TestTrue(TEXT("the bearing changed"), !After.Equals(Before, Tol));

	// Empty queue: nothing is handed.
	Guard->FacingQueue.Reset();
	Guard->MotorHandFacingTarget();
	TestFalse(TEXT("an empty queue hands no point"), Guard->FacingTargetHanded.IsSet());
	return true;
}

// --- Slot 277 `SetViewtarget` and slot 539 `SetAim` ---------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelFacingAimTest,
	"Elysium.Substrate.NpcKernelFacing.Aim", GElysiumNpcKernelFacingFlags)
bool FElysiumNpcKernelFacingAimTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_facing_aim"), 4213);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	TestNotNull(TEXT("the guard spawned"), Guard);
	if (Guard == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });

	// `0x100b5b00` — three floats into `m_viewtarget` (+0x0848), and nothing else.
	Guard->SetViewtarget(FVector(11.0, 22.0, 33.0));
	TestEqual(TEXT("SetViewtarget copies the point"), Guard->Viewtarget, FVector(11.0, 22.0, 33.0));

	// `0x1026b480` — `VectorAngles(aim)`, then the pitch pose and a HARD ZERO yaw pose. The zero is
	// in the listing as a pushed integer, not the computed yaw, and reproducing it is the point of
	// this case.
	Guard->PoseParameterWrites.Reset();
	Guard->SetAim(FVector(0.0, -1.0, 0.0));   // Source +Y, i.e. yaw 90
	TestEqual(TEXT("SetAim writes exactly two pose parameters"),
		Guard->PoseParameterWrites.Num(), 2);
	if (Guard->PoseParameterWrites.Num() == 2)
	{
		TestEqual(TEXT("the first is aim_pitch"), Guard->PoseParameterWrites[0].Name,
			FString(TEXT("aim_pitch")));
		TestEqual(TEXT("level aim is pitch 0"), Guard->PoseParameterWrites[0].Value, 0.f,
			KINDA_SMALL_NUMBER);
		TestEqual(TEXT("the second is aim_yaw"), Guard->PoseParameterWrites[1].Name,
			FString(TEXT("aim_yaw")));
		TestEqual(TEXT("and its value is retail's hard zero, not the aim's own yaw"),
			Guard->PoseParameterWrites[1].Value, 0.f);
	}

	// `VectorAngles` on a vertical aim takes its degenerate arm: straight up is pitch 270, which is
	// the opposite of what the sign of Z suggests.
	Guard->PoseParameterWrites.Reset();
	Guard->SetAim(FVector(0.0, 0.0, 1.0));
	if (Guard->PoseParameterWrites.Num() == 2)
	{
		TestEqual(TEXT("straight up is pitch 270"), Guard->PoseParameterWrites[0].Value, 270.f);
	}
	Guard->PoseParameterWrites.Reset();
	Guard->SetAim(FVector(0.0, 0.0, -1.0));
	if (Guard->PoseParameterWrites.Num() == 2)
	{
		TestEqual(TEXT("and straight down is pitch 90"), Guard->PoseParameterWrites[0].Value, 90.f);
	}

	return true;
}

// --- Slot 537 `SetHeadDirection` ----------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelFacingHeadDirectionTest,
	"Elysium.Substrate.NpcKernelFacing.SetHeadDirection", GElysiumNpcKernelFacingFlags)
bool FElysiumNpcKernelFacingHeadDirectionTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_facing_head"), 4214);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	TestNotNull(TEXT("the guard spawned"), Guard);
	if (Guard == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });
	Guard->Origin = FVector::ZeroVector;
	Guard->Angles = FVector::ZeroVector;

	// `0x1026af70`'s first line: `CapabilitiesGet() & bits_CAP_TURN_HEAD (0x1000)`.
	Guard->CapabilityWord = 0;
	Guard->HeadYaw = 100.f;
	FVector Target(100.0, 0.0, 0.0);
	Guard->SetHeadDirection(Target, 1.f);
	TestEqual(TEXT("without the turn-head capability the body does nothing"), Guard->HeadYaw,
		100.f);

	// The filter is `yaw = yaw*0.8 + target*0.2` per fixed 0.1 s step, and the loop is a DO/WHILE:
	// an interval shorter than one step still integrates once.
	Guard->CapabilityWord = 0x1000;
	Guard->HeadYaw = 100.f;
	Guard->HeadPitch = 0.f;
	Guard->SetHeadDirection(Target, 0.05f);
	TestEqual(TEXT("a sub-step interval integrates exactly once"), Guard->HeadYaw, 80.f,
		0.01f);

	Guard->HeadYaw = 100.f;
	Guard->SetHeadDirection(Target, 0.25f);
	TestEqual(TEXT("0.25 s is three 0.1 s steps: 100 -> 80 -> 64 -> 51.2"), Guard->HeadYaw, 51.2f,
		0.01f);

	// An interval of zero skips the integration entirely; the guard below it still runs.
	Guard->HeadYaw = 400.f;
	Guard->SetHeadDirection(Target, 0.f);
	TestEqual(TEXT("retail's lone guard zeroes a yaw past 360"), Guard->HeadYaw, 0.f);
	Guard->HeadYaw = -400.f;
	Guard->SetHeadDirection(Target, 0.f);
	TestEqual(TEXT("and it is ONE-SIDED — a runaway negative is left alone"), Guard->HeadYaw,
		-400.f);

	// The pitch half is `-RAD2DEG(atan(dz / |delta|))` against the EYE point (slot 193, which is the
	// origin plus the standing view offset), and the FULL 3-D distance — not the flat one — so a
	// target above the eye drives the pitch NEGATIVE.
	Guard->HeadPitch = 0.f;
	FVector Above(100.0, 0.0, 400.0);
	{
		const FVector Delta = Above - Guard->EyePosition();
		const float Expected = -FMath::RadiansToDegrees(
			FMath::Atan(static_cast<float>(Delta.Z) / static_cast<float>(Delta.Size()))) * ElysiumNpcTunables::HeadFilterBlend;
		Guard->SetHeadDirection(Above, 0.05f);
		TestEqual(TEXT("one step is 0.2 of the pitch to the target, measured from the eye"),
			Guard->HeadPitch, Expected, 0.01f);
		TestTrue(TEXT("and a target above the eye drives it negative"), Guard->HeadPitch < 0.f);
	}

	// A target dead ahead of a body that is already facing it leaves the yaw filter pulling toward
	// zero, because `AngleDiff(VecToYaw(delta), GetAngles().y)` is zero.
	Guard->HeadYaw = 50.f;
	Guard->SetHeadDirection(Target, 0.05f);
	TestEqual(TEXT("a body already facing its target decays its head yaw toward zero"),
		Guard->HeadYaw, 40.f, 0.01f);

	return true;
}

// --- Slots 370..373, the head/eye direction readers ----------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelFacingDirectionsTest,
	"Elysium.Substrate.NpcKernelFacing.Directions", GElysiumNpcKernelFacingFlags)
bool FElysiumNpcKernelFacingDirectionsTest::RunTest(const FString&)
{
	// The species table, by name, against the census — every row checked against
	// `docs/vtmb/npc-kernel/slots.md` through the dispatcher rather than against itself.
	struct FRow
	{
		const TCHAR* Class;
		const TCHAR* Body370;
		const TCHAR* Body371;
	};
	const FRow Rows[] =
	{
		{ TEXT("CCineNPC"),            TEXT("0x101a6d40"), TEXT("0x101a6d70") },
		{ TEXT("CCineAI"),             TEXT("0x101a6d40"), TEXT("0x101a6d70") },
		{ TEXT("CCineAISchedule"),     TEXT("0x101a6d40"), TEXT("0x101a6d70") },
		// The three `CNPCMaker*` rows closed at C++ dispatch in 0019/6 and are no longer asserted.
		{ TEXT("CNPC_VRat"),           TEXT("0x103ad7f0"), TEXT("0x103ad820") },
	};
	for (const FRow& Row : Rows)
	{
		const FElysiumNpcClass* Cls = ElysiumNpcTestCensus::Find(Row.Class);
		TestNotNull(*FString::Printf(TEXT("%s is a census class"), Row.Class), Cls);
		if (Cls == nullptr)
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s fills slot 370 with %s"), Row.Class, Row.Body370),
			FString(ElysiumNpcTestCensus::BodyOf(Cls, 370)), FString(Row.Body370));
		TestEqual(*FString::Printf(TEXT("%s fills slot 371 with %s"), Row.Class, Row.Body371),
			FString(ElysiumNpcTestCensus::BodyOf(Cls, 371)), FString(Row.Body371));
	}

	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_facing_dirs"), 4215);
	Builder.AddNpc(TEXT("guard"));
	Builder.AddNpc(TEXT("rat"), FVector(200.0, 0.0, 0.0), TEXT("npc_VRat"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	FElysiumNpc* Rat = Fixture.Npc(TEXT("rat"));
	TestNotNull(TEXT("the guard spawned"), Guard);
	TestNotNull(TEXT("the rat spawned"), Rat);
	if (Guard == nullptr || Rat == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard, Rat });

	// Slots 372/373 `0x1026b210` / `0x1026b240`: 20 bytes each, a tail jump through this object's
	// own vtable at +0x5c8 and +0x5cc — slots 370 and 371. The base tier has no independent eye
	// aim, and the forward IS the behaviour, which is what this asserts: the two answers are the
	// same object's head answers, whatever those become.
	TestEqual(TEXT("EyeDirection2D is HeadDirection2D"), Guard->EyeDirection2D(),
		Guard->HeadDirection2D());
	TestEqual(TEXT("EyeDirection3D is HeadDirection3D"), Guard->EyeDirection3D(),
		Guard->HeadDirection3D());

	// The species half of 370/371 (story 5 step 3: overrides on `FElysiumNpcRat`): the rat answers
	// its head aim with its body direction, through the vtable, on both slots.
	Rat->Angles = FVector(0.0, 90.0, 0.0);
	TestEqual(TEXT("CNPC_VRat's slot 370 is exactly BodyDirection2D"), Rat->HeadDirection2D(),
		Rat->BodyDirection2D());
	TestEqual(TEXT("and its slot 371 is exactly BodyDirection3D"), Rat->HeadDirection3D(),
		Rat->BodyDirection3D());
	TestEqual(TEXT("so its eyes aim along its body"), Rat->EyeDirection2D(), Rat->BodyDirection2D());
	// The humanoid combatant keeps the Troika-line body, which is not the body direction.
	Guard->Angles = FVector(0.0, 90.0, 0.0);
	TestNotEqual(TEXT("CNPC_VHumanCombatant does not forward to its body direction"),
		Guard->HeadDirection2D(), Guard->BodyDirection2D());

	return true;
}

// --- The species rows: slot 465 and the two boss facing bodies ----------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelFacingActivityTest,
	"Elysium.Substrate.NpcKernelFacing.OnChangeActivity", GElysiumNpcKernelFacingFlags)
bool FElysiumNpcKernelFacingActivityTest::RunTest(const FString&)
{
	// Slot 465's ported species rows, by name, against the census. `CNPC_Crow#465` (`0x10357b30`)
	// is on a class no map stands and carries no port arm.
	struct FRow
	{
		const TCHAR* Class;
		const TCHAR* Body;
	};
	const FRow Rows[] =
	{
		{ TEXT("CNPC_VMingXiao"),     TEXT("0x103947b0") },
		{ TEXT("CNPC_VSabbatGunman"), TEXT("0x103a56f0") },
		{ TEXT("CNPC_VWerewolf"),     TEXT("0x103d5f60") },
	};
	for (const FRow& Row : Rows)
	{
		const FElysiumNpcClass* Cls = ElysiumNpcTestCensus::Find(Row.Class);
		TestNotNull(*FString::Printf(TEXT("%s is a census class"), Row.Class), Cls);
		if (Cls != nullptr)
		{
			TestEqual(*FString::Printf(TEXT("%s fills slot 465 with %s"), Row.Class, Row.Body),
				FString(ElysiumNpcTestCensus::BodyOf(Cls, 465)), FString(Row.Body));
		}
	}
	// And the base they all chain to.
	{
		const FElysiumNpcClass* Troika = ElysiumNpcTestCensus::Find(TEXT("CAI_BaseNPCTroika"));
		TestEqual(TEXT("the Troika line's own slot 465 is the empty body they all chain to"),
			FString(ElysiumNpcTestCensus::BodyOf(Troika, 465)), FString(TEXT("0x10295a60")));
	}

	// `CNPC_VSabbatGunman::OnChangeActivity` `0x103a56f0`'s pick. At or BELOW the threshold the
	// trail is cleared and the scalar is retail's -1 literal; above it both come from the convars.
	{
		using FPick = FElysiumNpc::FMotionTrailPick;
		const FPick Stopped = FElysiumNpcSabbatGunman::SabbatGunmanMotionTrail(0.f, 50.f, 7, 2.f);
		TestEqual(TEXT("a stopped gunman clears the motion trail"), Stopped.MotionTrail, 0);
		TestEqual(TEXT("and takes retail's -1 scalar"), Stopped.PlaybackScalar, -1.f);
		const FPick AtThreshold = FElysiumNpcSabbatGunman::SabbatGunmanMotionTrail(50.f, 50.f, 7, 2.f);
		TestEqual(TEXT("the threshold itself is on the stopped side of the compare"),
			AtThreshold.MotionTrail, 0);
		const FPick Moving = FElysiumNpcSabbatGunman::SabbatGunmanMotionTrail(51.f, 50.f, 7, 2.f);
		TestEqual(TEXT("a moving one takes the convar's trail id"), Moving.MotionTrail, 7);
		TestEqual(TEXT("and the convar's playback scalar"), Moving.PlaybackScalar, 2.f);
	}

	// `CNPC_VMingXiao::OnChangeActivity` `0x103947b0`. The tuning stub answers its own field offset
	// so the case can name WHICH field each arm reads — the whole point of the two three-row arms.
	{
		using FPlayback = FElysiumNpc::FMingXiaoPlayback;
		auto Field = [](int32 Offset) { return static_cast<float>(Offset); };
		const FPlayback WalkFast =
			FElysiumNpcMingXiao::MingXiaoPlaybackScalar(9, /*bDisciplineArm*/ true, 0, Field);
		TestEqual(TEXT("the discipline arm reads +0x1c for ACT_WALK"), WalkFast.Scalar, 28.f);
		TestTrue(TEXT("and knows it was a walk/run"), WalkFast.bWalkOrRun);
		TestEqual(TEXT("ACT_RUN reads the same field"),
			FElysiumNpcMingXiao::MingXiaoPlaybackScalar(0x13, true, 0, Field).Scalar, 28.f);
		const FPlayback Special = FElysiumNpcMingXiao::MingXiaoPlaybackScalar(0x4b, true, 0, Field);
		TestEqual(TEXT("activity 0x4b reads +0x18"), Special.Scalar, 24.f);
		TestTrue(TEXT("and leaves before the second scalar write"), Special.bSecondWriteSkipped);
		TestEqual(TEXT("anything else reads +0x14"),
			FElysiumNpcMingXiao::MingXiaoPlaybackScalar(5, true, 0, Field).Scalar, 20.f);

		// The tentacle arm: `per * (6 - count) + base`, floored at 0.1.
		TestEqual(TEXT("the tentacle arm blends +0x60 over (6 - count) onto +0x5c"),
			FElysiumNpcMingXiao::MingXiaoPlaybackScalar(9, false, 2, Field).Scalar, 96.f * 4.f + 92.f);
		TestEqual(TEXT("a full six tentacles leave the base alone"),
			FElysiumNpcMingXiao::MingXiaoPlaybackScalar(0x4b, false, 6, Field).Scalar, 84.f);
		TestEqual(TEXT("and an all-zero record lands on retail's 0.1 floor"),
			FElysiumNpcMingXiao::MingXiaoPlaybackScalar(5, false, 0, [](int32) { return 0.f; }).Scalar,
			0.1f);
	}

	// Story 5 step 3 correction: the species bodies are the classes' own slot-465 overrides, so the
	// activity change reaches them through the vtable. A spawned `npc_VSabbatGunman` standing still
	// (the ground-speed seam answers 0) takes `0x103a56f0`'s stopped arm and clears its trail.
	{
		FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_facing_gunman"), 4218);
		Builder.AddNpcOfClass(TEXT("gunman"), FVector::ZeroVector, TEXT("CNPC_VSabbatGunman"));
		FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
		FElysiumNpc* Gunman = Fixture.Npc(TEXT("gunman"));
		TestNotNull(TEXT("the gunman spawned"), Gunman);
		if (Gunman != nullptr)
		{
			FElysiumNpcWorldFixture::Quiet({ Gunman });
			Gunman->MotionTrail = 3;
			Gunman->OnChangeActivity(9);
			TestEqual(TEXT("CNPC_VSabbatGunman's own slot 465 clears the trail of a stopped gunman"),
				Gunman->MotionTrail, 0);
		}
	}

	// A spawned NPC whose class has no slot-465 override chains straight to the empty base, which
	// is retail's whole answer for it.
	{
		FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_facing_act"), 4217);
		Builder.AddNpc(TEXT("guard"));
		FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
		FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
		TestNotNull(TEXT("the guard spawned"), Guard);
		if (Guard != nullptr)
		{
			FElysiumNpcWorldFixture::Quiet({ Guard });
			TestNull(TEXT("CNPC_VHumanCombatant overrides no slot 465"),
				ElysiumNpcTestCensus::OverrideOf(Guard->RetailClass(), 465));
			Guard->MotionTrail = 3;
			Guard->OnChangeActivity(9);
			TestEqual(TEXT("so nothing species-specific runs"), Guard->MotionTrail, 3);
		}
	}

	return true;
}

// --- The three player-relative facing bodies ----------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelFacingPlayerTest,
	"Elysium.Substrate.NpcKernelFacing.PlayerFacing", GElysiumNpcKernelFacingFlags)
bool FElysiumNpcKernelFacingPlayerTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_facing_player"), 4218);
	Builder.AddNpc(TEXT("leader"), FVector::ZeroVector, TEXT("npc_VSabbatLeader"));
	Builder.AddNpcOfClass(TEXT("chang"), FVector::ZeroVector, TEXT("CNPC_VChangBros"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpcSabbatLeader* Leader = Fixture.NpcAs<FElysiumNpcSabbatLeader>(TEXT("leader"));
	FElysiumNpcChangBros* Chang = Fixture.NpcAs<FElysiumNpcChangBros>(TEXT("chang"));
	FElysiumPlayer* Player = Fixture.Player();
	TestNotNull(TEXT("the leader spawned"), Leader);
	TestNotNull(TEXT("the player spawned"), Player);
	if (Leader == nullptr || Chang == nullptr || Player == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Leader, Chang });

	// `CNPC_VSabbatLeader::PlayerIsFacingMe` `0x103aaf50`: no closest player is false outright.
	Leader->Senses.Memory.ClosestPlayer = FElysiumEntityHandle();
	TestFalse(TEXT("no closest player is not facing me"), Leader->PlayerIsFacingMe());

	Leader->Senses.Memory.ClosestPlayer = Player->Handle;
	Player->Origin = FVector::ZeroVector;
	Player->Angles = FVector::ZeroVector;   // Source yaw 0 — forward is +X in this world's axes

	// The threshold is `dot < 0.34202` -> false, and 0.34202 is cos(70 degrees): a 140-degree cone.
	Leader->Origin = FVector(100.0, 0.0, 0.0);
	TestTrue(TEXT("dead ahead is facing me"), Leader->PlayerIsFacingMe());
	Leader->Origin = FVector(FMath::Cos(FMath::DegreesToRadians(60.0)) * 100.0,
		FMath::Sin(FMath::DegreesToRadians(60.0)) * 100.0, 0.0);
	TestTrue(TEXT("60 degrees off is still inside the cone"), Leader->PlayerIsFacingMe());
	Leader->Origin = FVector(FMath::Cos(FMath::DegreesToRadians(80.0)) * 100.0,
		FMath::Sin(FMath::DegreesToRadians(80.0)) * 100.0, 0.0);
	TestFalse(TEXT("80 degrees off is outside it"), Leader->PlayerIsFacingMe());
	Leader->Origin = FVector(-100.0, 0.0, 0.0);
	TestFalse(TEXT("directly behind is not facing me"), Leader->PlayerIsFacingMe());

	// The length epsilon comes FIRST: a player standing exactly on the leader counts as facing him,
	// because the dot is never reached.
	Leader->Origin = FVector::ZeroVector;
	TestTrue(TEXT("a player standing on the leader counts as facing him"),
		Leader->PlayerIsFacingMe());

	// `CNPC_VChangBros::UpdateFacingTimer` `0x1036d600`: passing all three gates LEAVES the stamp,
	// anything else resets it to curtime. The retail distances are Source units, so 50 u is 127 cm
	// and 150 u is 381 cm.
	Chang->FacingTime = -1.0;
	Chang->Origin = FVector(100.0, 0.0, 0.0);
	Chang->UpdateFacingTimer();
	TestEqual(TEXT("close, level and faced leaves the stamp alone"), Chang->FacingTime, -1.0);

	Chang->Origin = FVector(100.0, 0.0, 200.0);   // 200 cm of height, past the 50 u band
	Chang->UpdateFacingTimer();
	TestEqual(TEXT("too much height difference resets it to curtime"), Chang->FacingTime,
		Fixture.World.NowSeconds(), 0.001);

	Chang->FacingTime = -1.0;
	Chang->Origin = FVector(500.0, 0.0, 0.0);     // 500 cm, past the 150 u band
	Chang->UpdateFacingTimer();
	TestEqual(TEXT("too far resets it"), Chang->FacingTime, Fixture.World.NowSeconds(), 0.001);

	Chang->FacingTime = -1.0;
	Chang->Origin = FVector(0.0, 100.0, 0.0);     // beside the player, 90 degrees off his yaw
	Chang->UpdateFacingTimer();
	TestEqual(TEXT("a yaw delta past 70 degrees resets it"), Chang->FacingTime,
		Fixture.World.NowSeconds(), 0.001);

	Chang->FacingTime = -1.0;
	Chang->Senses.Memory.ClosestPlayer = FElysiumEntityHandle();
	Chang->UpdateFacingTimer();
	TestEqual(TEXT("and no closest player at all resets it"), Chang->FacingTime,
		Fixture.World.NowSeconds(), 0.001);

	// `CNPC_VChangBros::GetFacingTimeToTeleport` `0x1036dc60`: 21 s while the squad still has a
	// second member, 7 s otherwise. **SEAM**: `ConnectedSquad()` answers nothing because this
	// substrate stands no squad object, so the long arm is unreachable and 7 is the answer.
	TestTrue(TEXT("the squad seam answers nothing"), Chang->ConnectedSquad() == nullptr);
	TestEqual(TEXT("so the facing-to-teleport wait is the lone-brother 7 seconds"),
		Chang->GetFacingTimeToTeleport(), 7.f);

	return true;
}

// --- `FacePlayerAdvance`, the one body that reaches the mover -----------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelFacingAdvanceTest,
	"Elysium.Substrate.NpcKernelFacing.FacePlayerAdvance", GElysiumNpcKernelFacingFlags)
bool FElysiumNpcKernelFacingAdvanceTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_facing_advance"), 4219);
	FElysiumEntityDef& AndreiDef =
		Builder.AddNpc(TEXT("andrei"), FVector::ZeroVector, TEXT("npc_VAndreiBlood"));
	// Without a model key the leaf builds no body, and without a body the recording services build
	// no mover — and this is the one case in the family that has to reach one.
	AndreiDef.Keys.Add(TEXT("model"),
		TEXT("models/character/npc/common/blueblood/male/Blueblood_Male.mdl"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder),
		[](FElysiumRecordingServices& Services) { Services.bProvideNpcMotor = true; });
	FElysiumNpcAndreiBlood* Andrei = Fixture.NpcAs<FElysiumNpcAndreiBlood>(TEXT("andrei"));
	FElysiumPlayer* Player = Fixture.Player();
	TestNotNull(TEXT("Andrei spawned"), Andrei);
	TestNotNull(TEXT("the player spawned"), Player);
	if (Andrei == nullptr || Player == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Andrei });

	// `CNPC_VAndreiBlood::FacePlayerAdvance` `0x1035e5f0` is a gate plus one motor command. With no
	// closest player the gate refuses and nothing is commanded.
	Andrei->Senses.Memory.ClosestPlayer = FElysiumEntityHandle();
	Andrei->FacePlayerAdvance();

	Andrei->Origin = FVector::ZeroVector;
	Andrei->Angles = FVector::ZeroVector;
	Andrei->Senses.Memory.ClosestPlayer = Player->Handle;
	Player->Origin = FVector(0.0, 100.0, 0.0);
	Andrei->FacePlayerAdvance();

	// Source's +Y is this world's -Y, so a player at port +Y is at Source yaw -90, and `Face` takes
	// this world's yaw, which is the negated Source one: +90.
	TestEqual(TEXT("a recording mover stood"), Fixture.Services.NpcMotors.Num(), 1);
	if (Fixture.Services.NpcMotors.Num() > 0)
	{
		TestEqual(TEXT("the mover is commanded to the yaw from Andrei to the player"),
			Fixture.Services.NpcMotors[0]->RequestedYaw, 90.f, 0.01f);

		// The other half of the gate: with the closest-player handle cleared the body returns
		// before the command, so the mover keeps the yaw it was last given.
		Andrei->Senses.Memory.ClosestPlayer = FElysiumEntityHandle();
		Player->Origin = FVector(0.0, -100.0, 0.0);
		Andrei->FacePlayerAdvance();
		TestEqual(TEXT("and no closest player commands nothing at all"),
			Fixture.Services.NpcMotors[0]->RequestedYaw, 90.f, 0.01f);
	}

	// **SEAM**, stated: retail hands the motor a TARGET and the fixed turn rate `DAT_104a6f80 =
	// 10.0f` (`thunk_FUN_102e20b0`). `IElysiumNpcMotor::Face` takes a yaw and carries no rate, so
	// the recovered 10.0 has nowhere to land yet.

	return true;
}

#endif
