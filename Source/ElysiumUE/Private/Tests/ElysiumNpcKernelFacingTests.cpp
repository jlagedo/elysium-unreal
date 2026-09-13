#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Tests/ElysiumNpcTestFixture.h"

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
	using FPick = FElysiumNpc::FTurnActivityPick;

	auto Base = [](float Delta) { return FElysiumNpc::TurnActivityBaseLadder(Delta, AllSequences); };
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
	const FPick Fallen = FElysiumNpc::TurnActivityBaseLadder(-90.f, No90Right);
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

	// The gate: `cvar(0x109247ec) || m_bAllowTurningAnims`. The cvar is a seam answering false and
	// its name and default are unrecovered, so the authored key is the live half.
	TestFalse(TEXT("the turning-anims cvar seam answers nothing"), Guard->TurningAnimsEnabled());

	Guard->ScheduleHost.MemoryBits = 0;
	Guard->IdealActivityNumber = 0;
	Guard->bAllowTurningAnims = false;
	Guard->MotorIdealYawDelta = -100.f;
	Guard->SetTurnActivity();
	TestEqual(TEXT("with the gate closed the body still lands on ACT_IDLE"),
		Guard->IdealActivityNumber, 1);
	TestEqual(TEXT("and tags nothing"), static_cast<int32>(Guard->ScheduleHost.MemoryBits & 0x2000),
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
		static_cast<int32>(Guard->ScheduleHost.MemoryBits & 0x2000), 0);

	// The motor seam: `CAI_Motor::DeltaIdealYaw` has no source, so it stands at retail's
	// already-aligned answer.
	Guard->MotorIdealYawDelta = 0.f;
	TestEqual(TEXT("the motor's yaw-delta seam answers retail's aligned value"),
		Guard->MotorDeltaIdealYaw(), 0.f);

	// `FacingIdeal` `0x10278c80` — `|delta| <= 0.006`, and the tolerance is a DOUBLE in `.rdata`.
	TestTrue(TEXT("aligned is facing ideal"), Guard->FacingIdeal());
	Guard->MotorIdealYawDelta = 0.006f;
	TestTrue(TEXT("the tolerance is inclusive — the listing's compare is <=, not <"),
		Guard->FacingIdeal());
	Guard->MotorIdealYawDelta = 0.0061f;
	TestFalse(TEXT("just past it is not"), Guard->FacingIdeal());
	Guard->MotorIdealYawDelta = -0.005f;
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

	// All three overloads are the same shape: a cvar gate, then a tail jump into the motor. The
	// cvar at `0x10924f74` is a seam whose name and default are unrecovered, and it answers false,
	// so all three add nothing — which is the refusal, stated.
	TestFalse(TEXT("the facing-target cvar seam answers nothing"), Guard->FacingTargetsEnabled());
	Guard->AddFacingTarget(Subject, 1.f, 2.f, 3.f);
	Guard->AddFacingTarget(FVector(1.0, 2.0, 3.0), 1.f, 2.f, 3.f);
	Guard->AddFacingTarget(Subject, FVector(1.0, 2.0, 3.0), 1.f, 2.f, 3.f);
	TestEqual(TEXT("so slots 517, 518 and 519 all queue nothing"),
		Guard->FacingTargetRequests.Num(), 0);

	// The seam below them, exercised directly: which of the motor's three overloads each slot
	// reaches is a recovered fact (519 -> motor slot 14, 518 -> 13, 517 -> 12) and is what the
	// queue will carry the day one exists.
	FElysiumNpc::FFacingTargetRequest Request;
	Request.MotorSlot = 14;
	Request.Target = Subject->Handle;
	Request.Duration = 1.5f;
	Guard->MotorAddFacingTarget(Request);
	TestEqual(TEXT("the motor seam records the request"), Guard->FacingTargetRequests.Num(), 1);
	TestEqual(TEXT("on the overload slot 519 tail-jumps to"),
		Guard->FacingTargetRequests[0].MotorSlot, 14);
	TestTrue(TEXT("carrying the entity it was asked to face"),
		Guard->FacingTargetRequests[0].Target == Subject->Handle);

	// `ClearFacingTarget` `0x102e11f0` cancels the queue's current entry.
	Guard->ClearFacingTarget();
	TestEqual(TEXT("ClearFacingTarget empties it"), Guard->FacingTargetRequests.Num(), 0);

	// Slot 520 `0x10278e00` forwards to the motor's slot 15, which has nothing to answer with, so
	// the out parameter is left where the caller put it.
	FVector Direction(7.0, 8.0, 9.0);
	TestEqual(TEXT("GetFacingDirection weighs nothing"), Guard->GetFacingDirection(Direction), 0.f);
	TestEqual(TEXT("and leaves the caller's vector alone"), Direction, FVector(7.0, 8.0, 9.0));

	// Slot 526 `0x1027d9f0` — the base declines, unconditionally, and that is the rule every
	// species override is measured against.
	TestFalse(TEXT("OverrideMoveFacing declines"), Guard->OverrideMoveFacing(nullptr, 0.1f));
	TestFalse(TEXT("and declines again with a live interval"),
		Guard->OverrideMoveFacing(nullptr, 10.f));

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
			FMath::Atan(static_cast<float>(Delta.Z) / static_cast<float>(Delta.Size()))) * 0.2f;
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

	// `CAI_BaseHumanoid`'s branch override `0x1025eaf0` takes the same capability gate and, past
	// it, drops the two head-cache bits — the one effect this substrate can reproduce, because the
	// pose-parameter indices at +0x5fb8..+0x6010 have no port member.
	Guard->HumanoidHeadCacheBits = 0x3;
	Guard->CapabilityWord = 0;
	Guard->SetHeadDirectionHumanoid(Target, 1.f);
	TestEqual(TEXT("the humanoid override takes the same capability gate"),
		static_cast<int32>(Guard->HumanoidHeadCacheBits), 0x3);
	Guard->CapabilityWord = 0x1000;
	Guard->SetHeadDirectionHumanoid(Target, 1.f);
	TestEqual(TEXT("and past it drops both cache bits"),
		static_cast<int32>(Guard->HumanoidHeadCacheBits), 0);

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
		{ TEXT("CPayphone"),           TEXT("0x101aa7f0"), TEXT("0x101aa820") },
		{ TEXT("CNPCMaker"),           TEXT("0x1034adf0"), TEXT("0x1034ae20") },
		{ TEXT("CNPCMaker_Fleshpile"), TEXT("0x1034be90"), TEXT("0x1034bec0") },
		{ TEXT("CNPCMaker_Zombie"),    TEXT("0x1034cad0"), TEXT("0x1034cb00") },
		{ TEXT("CNPC_VRat"),           TEXT("0x103ad7f0"), TEXT("0x103ad820") },
	};
	for (const FRow& Row : Rows)
	{
		const FElysiumNpcClass* Cls = ElysiumNpcKernelClass::Find(Row.Class);
		TestNotNull(*FString::Printf(TEXT("%s is a census class"), Row.Class), Cls);
		if (Cls == nullptr)
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s fills slot 370 with %s"), Row.Class, Row.Body370),
			FString(ElysiumNpcKernelClass::BodyOf(Cls, 370)), FString(Row.Body370));
		TestEqual(*FString::Printf(TEXT("%s fills slot 371 with %s"), Row.Class, Row.Body371),
			FString(ElysiumNpcKernelClass::BodyOf(Cls, 371)), FString(Row.Body371));
	}

	// `CAI_BaseHumanoid` is the one class in this family's rows that keeps a head of its own
	// (`0x1025f0f0` / `0x1025f160`, the attachment-cached pair) rather than forwarding to its body.
	{
		const FElysiumNpcClass* Humanoid = ElysiumNpcKernelClass::Find(TEXT("CAI_BaseHumanoid"));
		TestNotNull(TEXT("CAI_BaseHumanoid is a census class"), Humanoid);
		if (Humanoid != nullptr)
		{
			TestEqual(TEXT("and fills slot 370 with its own cached reader"),
				FString(ElysiumNpcKernelClass::BodyOf(Humanoid, 370)), FString(TEXT("0x1025f0f0")));
			TestEqual(TEXT("and slot 371 with its 3-D twin"),
				FString(ElysiumNpcKernelClass::BodyOf(Humanoid, 371)), FString(TEXT("0x1025f160")));
		}
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

	// The species half of 370/371: the rat forwards to its body direction, the humanoid combatant
	// does not appear in the table at all.
	TestTrue(TEXT("CNPC_VRat answers its head aim with its body direction"),
		Rat->HeadDirectionIsBodyDirection());
	TestFalse(TEXT("CNPC_VHumanCombatant does not"), Guard->HeadDirectionIsBodyDirection());

	Rat->Angles = FVector(0.0, 90.0, 0.0);
	FVector RatHead2D = FVector::ZeroVector;
	TestTrue(TEXT("so the rat has a recovered answer for slot 370"),
		Rat->RetailHeadDirection(/*b2D*/ true, RatHead2D));
	TestEqual(TEXT("and it is exactly BodyDirection2D"), RatHead2D, Rat->BodyDirection2D());
	FVector GuardHead2D = FVector::ZeroVector;
	TestFalse(TEXT("the combatant's own class has no recovered species answer"),
		Guard->RetailHeadDirection(/*b2D*/ true, GuardHead2D));

	// `CAI_BaseHumanoid`'s cached pair, `0x1025f160` / `0x1025f0b0` behind the refresh at
	// `0x1025e7b0`. **SEAM**: the "head" attachment is the animating tier's, so the refresh takes
	// retail's own no-attachment arm — the head direction is the body's forward and the eye
	// direction, whose two endpoints then coincide, is the zero vector.
	Guard->Angles = FVector(0.0, 90.0, 0.0);
	Guard->HumanoidHeadCacheBits = 0;
	const FVector Head3D = Guard->HeadDirection3DHumanoid();
	TestEqual(TEXT("the no-attachment arm answers the body's own forward"), Head3D,
		FVector(0.0, -1.0, 0.0), 0.001f);
	TestEqual(TEXT("and the cache latches both bits"),
		static_cast<int32>(Guard->HumanoidHeadCacheBits), 0x3);
	Guard->Angles = FVector(0.0, 0.0, 0.0);
	TestEqual(TEXT("a second read is the LATCHED vector, not a re-derivation"),
		Guard->HeadDirection3DHumanoid(), Head3D);
	TestEqual(TEXT("the eye direction's endpoints coincide on that arm, so it is zero"),
		Guard->EyeDirection3DHumanoid(), FVector::ZeroVector);
	TestEqual(TEXT("and its 2-D form normalizes the zero vector to itself"),
		Guard->EyeDirection2DHumanoid(), FVector::ZeroVector);

	Guard->HumanoidHeadCacheBits = 0;
	Guard->Angles = FVector(0.0, 90.0, 0.0);
	TestEqual(TEXT("the 2-D head reader is the 3-D one flattened and renormalized"),
		Guard->HeadDirection2DHumanoid(), FVector(0.0, -1.0, 0.0), 0.001f);

	return true;
}

// --- Slots 535/536, `CAI_BaseHumanoid`'s look-target list ---------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelFacingLookTargetsTest,
	"Elysium.Substrate.NpcKernelFacing.LookTargets", GElysiumNpcKernelFacingFlags)
bool FElysiumNpcKernelFacingLookTargetsTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_facing_looks"), 4216);
	Builder.AddNpc(TEXT("guard"));
	Builder.AddNpc(TEXT("a"), FVector(100.0, 0.0, 0.0));
	Builder.AddNpc(TEXT("b"), FVector(200.0, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	FElysiumNpc* A = Fixture.Npc(TEXT("a"));
	FElysiumNpc* B = Fixture.Npc(TEXT("b"));
	TestNotNull(TEXT("the guard spawned"), Guard);
	TestNotNull(TEXT("a spawned"), A);
	TestNotNull(TEXT("b spawned"), B);
	if (Guard == nullptr || A == nullptr || B == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard, A, B });

	// `0x1025f760` — the entity overload. The record is 0x24 bytes: kind, handle, position, start,
	// end and rate, with the priority last. Duration 2 s and influence 1 gives rate 0.5, and the end
	// stamp is `curtime + duration`.
	const double Now = Fixture.World.NowSeconds();
	Guard->AddLookTargetHumanoid(A, /*Priority*/ 5, /*Duration*/ 2.f, /*Influence*/ 1.f);
	TestEqual(TEXT("one record is added"), Guard->LookTargets.Num(), 1);
	if (Guard->LookTargets.Num() == 1)
	{
		const FElysiumNpc::FLookTargetRecord& R = Guard->LookTargets[0];
		TestEqual(TEXT("kind 0 is the entity form"), R.Kind, 0);
		TestTrue(TEXT("holding the entity's handle"), R.Target == A->Handle);
		TestEqual(TEXT("the priority is the caller's"), R.Priority, 5);
		TestEqual(TEXT("the start stamp is curtime"), R.StartTime, Now, 0.001);
		TestEqual(TEXT("the end stamp is curtime plus the duration"), R.EndTime, Now + 2.0, 0.001);
		TestEqual(TEXT("and the rate is influence / duration"), R.Rate, 0.5f, 0.0001f);
	}

	// Re-adding the same entity REPLACES: retail removes the existing record first, then appends,
	// so the new one is at the tail and the count does not grow.
	Guard->AddLookTargetHumanoid(B, 1, 4.f, 4.f);
	Guard->AddLookTargetHumanoid(A, 9, 1.f, 3.f);
	TestEqual(TEXT("re-adding an entity does not grow the list"), Guard->LookTargets.Num(), 2);
	if (Guard->LookTargets.Num() == 2)
	{
		TestTrue(TEXT("and the re-added entity moves to the tail"),
			Guard->LookTargets[1].Target == A->Handle);
		TestEqual(TEXT("with the new priority"), Guard->LookTargets[1].Priority, 9);
		TestEqual(TEXT("and the new rate"), Guard->LookTargets[1].Rate, 3.0f, 0.0001f);
		TestTrue(TEXT("the untouched record keeps its place"),
			Guard->LookTargets[0].Target == B->Handle);
	}

	// `0x1025f8e0` — the position overload matches an existing record by its stored POSITION rather
	// than by entity, and writes kind 1.
	Guard->LookTargets.Reset();
	Guard->AddLookTargetHumanoid(FVector(1.0, 2.0, 3.0), 2, 2.f, 2.f);
	Guard->AddLookTargetHumanoid(FVector(4.0, 5.0, 6.0), 3, 2.f, 2.f);
	TestEqual(TEXT("two distinct positions are two records"), Guard->LookTargets.Num(), 2);
	TestEqual(TEXT("and the kind is 1"), Guard->LookTargets[0].Kind, 1);
	Guard->AddLookTargetHumanoid(FVector(1.0, 2.0, 3.0), 8, 2.f, 2.f);
	TestEqual(TEXT("re-adding the same position replaces rather than appends"),
		Guard->LookTargets.Num(), 2);
	TestEqual(TEXT("with the replacement at the tail"), Guard->LookTargets[1].Priority, 8);
	TestEqual(TEXT("and the other position untouched"), Guard->LookTargets[0].Priority, 3);

	return true;
}

// --- The species rows: slot 465 and the two boss facing bodies ----------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelFacingActivityTest,
	"Elysium.Substrate.NpcKernelFacing.OnChangeActivity", GElysiumNpcKernelFacingFlags)
bool FElysiumNpcKernelFacingActivityTest::RunTest(const FString&)
{
	// Slot 465's four species rows, by name, against the census.
	struct FRow
	{
		const TCHAR* Class;
		const TCHAR* Body;
	};
	const FRow Rows[] =
	{
		{ TEXT("CNPC_Crow"),          TEXT("0x10357b30") },
		{ TEXT("CNPC_VMingXiao"),     TEXT("0x103947b0") },
		{ TEXT("CNPC_VSabbatGunman"), TEXT("0x103a56f0") },
		{ TEXT("CNPC_VWerewolf"),     TEXT("0x103d5f60") },
	};
	for (const FRow& Row : Rows)
	{
		const FElysiumNpcClass* Cls = ElysiumNpcKernelClass::Find(Row.Class);
		TestNotNull(*FString::Printf(TEXT("%s is a census class"), Row.Class), Cls);
		if (Cls != nullptr)
		{
			TestEqual(*FString::Printf(TEXT("%s fills slot 465 with %s"), Row.Class, Row.Body),
				FString(ElysiumNpcKernelClass::BodyOf(Cls, 465)), FString(Row.Body));
		}
	}
	// And the base the four of them chain to.
	{
		const FElysiumNpcClass* Troika = ElysiumNpcKernelClass::Find(TEXT("CAI_BaseNPCTroika"));
		TestEqual(TEXT("the Troika line's own slot 465 is the empty body they all chain to"),
			FString(ElysiumNpcKernelClass::BodyOf(Troika, 465)), FString(TEXT("0x10295a60")));
	}

	// `CNPC_VSabbatGunman::OnChangeActivity` `0x103a56f0`'s pick. At or BELOW the threshold the
	// trail is cleared and the scalar is retail's -1 literal; above it both come from the convars.
	{
		using FPick = FElysiumNpc::FMotionTrailPick;
		const FPick Stopped = FElysiumNpc::SabbatGunmanMotionTrail(0.f, 50.f, 7, 2.f);
		TestEqual(TEXT("a stopped gunman clears the motion trail"), Stopped.MotionTrail, 0);
		TestEqual(TEXT("and takes retail's -1 scalar"), Stopped.PlaybackScalar, -1.f);
		const FPick AtThreshold = FElysiumNpc::SabbatGunmanMotionTrail(50.f, 50.f, 7, 2.f);
		TestEqual(TEXT("the threshold itself is on the stopped side of the compare"),
			AtThreshold.MotionTrail, 0);
		const FPick Moving = FElysiumNpc::SabbatGunmanMotionTrail(51.f, 50.f, 7, 2.f);
		TestEqual(TEXT("a moving one takes the convar's trail id"), Moving.MotionTrail, 7);
		TestEqual(TEXT("and the convar's playback scalar"), Moving.PlaybackScalar, 2.f);
	}

	// `CNPC_VMingXiao::OnChangeActivity` `0x103947b0`. The tuning stub answers its own field offset
	// so the case can name WHICH field each arm reads — the whole point of the two three-row arms.
	{
		using FPlayback = FElysiumNpc::FMingXiaoPlayback;
		auto Field = [](int32 Offset) { return static_cast<float>(Offset); };
		const FPlayback WalkFast =
			FElysiumNpc::MingXiaoPlaybackScalar(9, /*bDisciplineArm*/ true, 0, Field);
		TestEqual(TEXT("the discipline arm reads +0x1c for ACT_WALK"), WalkFast.Scalar, 28.f);
		TestTrue(TEXT("and knows it was a walk/run"), WalkFast.bWalkOrRun);
		TestEqual(TEXT("ACT_RUN reads the same field"),
			FElysiumNpc::MingXiaoPlaybackScalar(0x13, true, 0, Field).Scalar, 28.f);
		const FPlayback Special = FElysiumNpc::MingXiaoPlaybackScalar(0x4b, true, 0, Field);
		TestEqual(TEXT("activity 0x4b reads +0x18"), Special.Scalar, 24.f);
		TestTrue(TEXT("and leaves before the second scalar write"), Special.bSecondWriteSkipped);
		TestEqual(TEXT("anything else reads +0x14"),
			FElysiumNpc::MingXiaoPlaybackScalar(5, true, 0, Field).Scalar, 20.f);

		// The tentacle arm: `per * (6 - count) + base`, floored at 0.1.
		TestEqual(TEXT("the tentacle arm blends +0x60 over (6 - count) onto +0x5c"),
			FElysiumNpc::MingXiaoPlaybackScalar(9, false, 2, Field).Scalar, 96.f * 4.f + 92.f);
		TestEqual(TEXT("a full six tentacles leave the base alone"),
			FElysiumNpc::MingXiaoPlaybackScalar(0x4b, false, 6, Field).Scalar, 84.f);
		TestEqual(TEXT("and an all-zero record lands on retail's 0.1 floor"),
			FElysiumNpc::MingXiaoPlaybackScalar(5, false, 0, [](int32) { return 0.f; }).Scalar,
			0.1f);
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
				ElysiumNpcKernelClass::OverrideOf(Guard->RetailClass(), 465));
			Guard->MotionTrail = 3;
			Guard->OnChangeActivitySpecies(9);
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
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Leader = Fixture.Npc(TEXT("leader"));
	FElysiumPlayer* Player = Fixture.Player();
	TestNotNull(TEXT("the leader spawned"), Leader);
	TestNotNull(TEXT("the player spawned"), Player);
	if (Leader == nullptr || Player == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Leader });

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
	Leader->FacingTime = -1.0;
	Leader->Origin = FVector(100.0, 0.0, 0.0);
	Leader->UpdateFacingTimer();
	TestEqual(TEXT("close, level and faced leaves the stamp alone"), Leader->FacingTime, -1.0);

	Leader->Origin = FVector(100.0, 0.0, 200.0);   // 200 cm of height, past the 50 u band
	Leader->UpdateFacingTimer();
	TestEqual(TEXT("too much height difference resets it to curtime"), Leader->FacingTime,
		Fixture.World.NowSeconds(), 0.001);

	Leader->FacingTime = -1.0;
	Leader->Origin = FVector(500.0, 0.0, 0.0);     // 500 cm, past the 150 u band
	Leader->UpdateFacingTimer();
	TestEqual(TEXT("too far resets it"), Leader->FacingTime, Fixture.World.NowSeconds(), 0.001);

	Leader->FacingTime = -1.0;
	Leader->Origin = FVector(0.0, 100.0, 0.0);     // beside the player, 90 degrees off his yaw
	Leader->UpdateFacingTimer();
	TestEqual(TEXT("a yaw delta past 70 degrees resets it"), Leader->FacingTime,
		Fixture.World.NowSeconds(), 0.001);

	Leader->FacingTime = -1.0;
	Leader->Senses.Memory.ClosestPlayer = FElysiumEntityHandle();
	Leader->UpdateFacingTimer();
	TestEqual(TEXT("and no closest player at all resets it"), Leader->FacingTime,
		Fixture.World.NowSeconds(), 0.001);

	// `CNPC_VChangBros::GetFacingTimeToTeleport` `0x1036dc60`: 21 s while the squad still has a
	// second member, 7 s otherwise. **SEAM**: `ConnectedSquad()` answers nothing because this
	// substrate stands no squad object, so the long arm is unreachable and 7 is the answer.
	TestTrue(TEXT("the squad seam answers nothing"), Leader->ConnectedSquad() == nullptr);
	TestEqual(TEXT("so the facing-to-teleport wait is the lone-brother 7 seconds"),
		Leader->GetFacingTimeToTeleport(), 7.f);

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
	FElysiumNpc* Andrei = Fixture.Npc(TEXT("andrei"));
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
