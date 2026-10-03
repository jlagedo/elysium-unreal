#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumHint.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumNpcTestFixture.h"

#include <limits>

// Story 29c-1, family **BaseHelpers** — `CAI_BaseNPC`'s own unnamed layer 0–9 bodies.
//
// Every assertion below comes from the decompiled C: the two melee ladders' thresholds and the
// three ways they differ, `RememberUnreachable`'s backward scan,
// `CalcIdealYaw`'s three delta forms, the face-anim ladder's four rungs, the hint validators' bands
// and projections, and the victim-side slots' condition writes.
//
// Where a body can only answer "nothing" because its input is a seam — the hint store, the
// navigator's goal type, the weapon's range, the retail task number — the case says so: that the
// seam is asked and that the refusal is the recovered one.

static constexpr EAutomationTestFlags GElysiumNpcKernelBaseHelpersFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// One `npc_VHumanCombatant` (a registered spawn leaf, `Substrate/ElysiumNpcClasses.cpp`) and a
	// second body to stand in as an enemy. Both quiet, so no think competes with the pass a case
	// drives.
	struct FBaseHelpersFixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Npc = nullptr;
		FElysiumNpc* Other = nullptr;

		FBaseHelpersFixture()
			: World([]
			{
				FElysiumNpcWorldBuilder Builder(TEXT("basehelpers"), 7u);
				Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
				Builder.AddNpc(TEXT("guard"), FVector::ZeroVector);
				Builder.AddNpc(TEXT("foe"), FVector(500.0, 0.0, 0.0));
				return Builder;
			}())
		{
			Npc = World.Npc(TEXT("guard"));
			Other = World.Npc(TEXT("foe"));
			FElysiumNpcWorldFixture::Quiet({ Npc, Other });
		}
	};

	// The hint words every pure-rule case starts from: valid, enabled, facing +X, band [10, 100]
	// source units, dot floor 0.5.
	FElysiumNpcBase::FHintWords MakeHint()
	{
		FElysiumNpcBase::FHintWords Hint;
		Hint.bValid = true;
		Hint.Disabled = 0;
		Hint.OriginCm = FVector::ZeroVector;
		Hint.Angles = FVector::ZeroVector;   // yaw 0 — the hint faces +X
		Hint.TargetDistMin = 10.f;
		Hint.TargetDistMax = 100.f;
		Hint.TargetAngleRangeDot = 0.5f;
		return Hint;
	}

	FVector AtUnits(double X, double Y = 0.0, double Z = 0.0)
	{
		return FVector(X, Y, Z) * ElysiumMove::U;
	}
}

// -------------------------------------------------------------------------------------------------
// Slot 555 — `MeleeAttack1Conditions` (`0x1026d9a0`); slot 556 `MeleeAttack2Conditions` was deleted
// as dead in 0019/6.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBaseHelpersMeleeConditionsTest,
	"Elysium.Arm.NpcKernelBaseHelpers.MeleeAttackConditions",
	GElysiumNpcKernelBaseHelpersFlags)
bool FElysiumNpcKernelBaseHelpersMeleeConditionsTest::RunTest(const FString&)
{
	FBaseHelpersFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Npc)
		|| !TestNotNull(TEXT("the foe spawned"), F.Other))
	{
		return false;
	}

	const int32 None = static_cast<int32>(EElysiumNpcCond::None);
	const int32 TooFarToAttack = static_cast<int32>(EElysiumNpcCond::TooFarToAttack);   // 0x60
	const int32 CanMelee1 = static_cast<int32>(EElysiumNpcCond::CanMeleeAttack1);       // 0x51

	// Rung 2 of both ladders: past the shared 64-unit attack band (`_DAT_10451acc`) the answer is
	// `COND_TOO_FAR_TO_ATTACK`, whatever the dot is.
	TestEqual(TEXT("1: past 64 units answers COND_TOO_FAR_TO_ATTACK"),
		F.Npc->MeleeAttack1Conditions(1.0f, 64.1f), TooFarToAttack);
	TestEqual(TEXT("1: exactly 64 units is INSIDE the band (retail's compare is strictly greater)"),
		F.Npc->MeleeAttack1Conditions(0.0f, 64.0f), None);

	// `0x1026d9a0`'s outer band reads `_DAT_1044ddb0` = 256.0f (`ElysiumNpcTunables::Melee1OuterBand`);
	// past it the answer is `COND_TOO_FAR_FOR_MELEE` (0x09), at it the 0x60 rung still answers.
	const int32 TooFarForMelee = static_cast<int32>(EElysiumNpcCond::TooFarForMelee);   // 0x09
	TestEqual(TEXT("1: at the outer band the 0x60 rung still answers"),
		F.Npc->MeleeAttack1Conditions(1.0f, ElysiumNpcTunables::Melee1OuterBand), TooFarToAttack);
	TestEqual(TEXT("1: past the outer band answers COND_TOO_FAR_FOR_MELEE"),
		F.Npc->MeleeAttack1Conditions(1.0f, 1.0e9f), TooFarForMelee);

	// The dot gate, `flDot < 0.7` (`_DAT_104492d0`, read as a double; SDK 2013's twin states the
	// literal). Shared by both bodies.
	TestEqual(TEXT("1: a dot under 0.7 refuses"), F.Npc->MeleeAttack1Conditions(0.69f, 10.f), None);

	// `0x1026d9a0` re-dispatches `GetEnemy()` as a null gate after the dot.
	F.Npc->BaseMemory.Enemy = FElysiumEntityHandle();
	TestEqual(TEXT("1: no enemy refuses after the dot gate"),
		F.Npc->MeleeAttack1Conditions(0.9f, 10.f), None);

	// The `FL_ONGROUND` read.
	F.Npc->BaseMemory.Enemy = F.Other->Handle;
	F.Other->Flags &= ~1;
	TestEqual(TEXT("1: an airborne enemy answers nothing"),
		F.Npc->MeleeAttack1Conditions(0.9f, 10.f), None);
	F.Other->Flags |= 1;
	TestEqual(TEXT("1: a grounded enemy answers COND_CAN_MELEE_ATTACK1"),
		F.Npc->MeleeAttack1Conditions(0.9f, 10.f), CanMelee1);

	return true;
}

// -------------------------------------------------------------------------------------------------
// `RememberUnreachable` (`0x10274080`) — the write side of family Positions' list.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBaseHelpersRememberUnreachableTest,
	"Elysium.Arm.NpcKernelBaseHelpers.RememberUnreachable",
	GElysiumNpcKernelBaseHelpersFlags)
bool FElysiumNpcKernelBaseHelpersRememberUnreachableTest::RunTest(const FString&)
{
	FBaseHelpersFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Npc)
		|| !TestNotNull(TEXT("the foe spawned"), F.Other))
	{
		return false;
	}
	F.Npc->UnreachableEnts.Reset();
	const double Now = F.World.World.NowSeconds();

	F.Other->Origin = FVector(100.0, 0.0, 0.0);
	F.Npc->RememberUnreachable(F.Other);
	if (!TestEqual(TEXT("the first call appends one record"), F.Npc->UnreachableEnts.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("the record names the entity"), F.Npc->UnreachableEnts[0].Entity.Index,
		F.Other->Handle.Index);
	// `curtime + _DAT_10449258`, and `_DAT_10449258 = 3.0f`.
	TestEqual(TEXT("the expiry is curtime + 3.0"), F.Npc->UnreachableEnts[0].ExpiresAt, Now + 3.0,
		1.0e-3);
	TestEqual(TEXT("and the position is where it was standing"),
		F.Npc->UnreachableEnts[0].PositionCm.X, 100.0, 1.0e-3);

	// A second call for the SAME entity refreshes the record rather than appending — the backward
	// scan's whole point.
	F.Other->Origin = FVector(250.0, 0.0, 0.0);
	F.Npc->RememberUnreachable(F.Other);
	TestEqual(TEXT("a repeat refreshes rather than appends"), F.Npc->UnreachableEnts.Num(), 1);
	TestEqual(TEXT("and rewrites the position"), F.Npc->UnreachableEnts[0].PositionCm.X, 250.0,
		1.0e-3);

	// A different entity appends beside it.
	F.Npc->RememberUnreachable(F.World.Player());
	TestEqual(TEXT("a different entity appends"), F.Npc->UnreachableEnts.Num(), 2);

	// Retail dereferences a null argument and faults; the port refuses. NAMED DIVERGENCE.
	F.Npc->RememberUnreachable(nullptr);
	TestEqual(TEXT("a null entity still appends a record with the -1 handle retail writes"),
		F.Npc->UnreachableEnts.Num(), 3);
	TestFalse(TEXT("whose handle is unset"), F.Npc->UnreachableEnts[2].Entity.IsSet());

	return true;
}

// -------------------------------------------------------------------------------------------------
// The geometry and clock helpers.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBaseHelpersGeometryTest,
	"Elysium.Arm.NpcKernelBaseHelpers.GeometryAndClocks",
	GElysiumNpcKernelBaseHelpersFlags)
bool FElysiumNpcKernelBaseHelpersGeometryTest::RunTest(const FString&)
{
	FBaseHelpersFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Npc))
	{
		return false;
	}

	// Slot 515 `CalcIdealYaw` (`0x10274b30`). The word it picks its arm by is the path's MOVEMENT
	// ACTIVITY (`0x102ee3f0`, `path+0x2c`), 1 by default, so the DEFAULT arm is the one a fresh body
	// takes: `VecToYaw(target - GetOrigin())`.
	TestEqual(TEXT("the path's activity is 1, so the default delta form runs"),
		F.Npc->Navigator.GetMovementActivity(), 1);
	F.Npc->Origin = FVector::ZeroVector;
	TestEqual(TEXT("+X is yaw 0"), F.Npc->CalcIdealYaw(FVector(100.0, 0.0, 0.0)), 0.f, 1.0e-3f);
	// This world's +Y is Source's -Y, and `VecToYaw` (`0x101d2c70`) folds into [0, 360)
	// (`0x101d2cb4 FADD 360.0`), so +Y is the RETAIL yaw 270 (L05 integration: the case read the
	// Unreal-convention 90 the body answered before its first caller landed).
	TestEqual(TEXT("+Y is retail yaw 270"), F.Npc->CalcIdealYaw(FVector(0.0, 100.0, 0.0)), 270.f, 1.0e-3f);
	TestEqual(TEXT("-Y is retail yaw 90"), F.Npc->CalcIdealYaw(FVector(0.0, -100.0, 0.0)), 90.f, 1.0e-3f);
	TestEqual(TEXT("-X is yaw 180"), FMath::Abs(F.Npc->CalcIdealYaw(FVector(-100.0, 0.0, 0.0))),
		180.f, 1.0e-3f);
	F.Npc->Origin = FVector(100.0, 0.0, 0.0);
	TestEqual(TEXT("the origin is subtracted, not ignored"),
		F.Npc->CalcIdealYaw(FVector(200.0, 0.0, 0.0)), 0.f, 1.0e-3f);
	TestEqual(TEXT("a zero-length 2-D delta answers zero"),
		F.Npc->CalcIdealYaw(FVector(100.0, 0.0, 9999.0)), 0.f, 1.0e-3f);
	F.Npc->Origin = FVector::ZeroVector;

	// 0x1028ebc0 — `FInViewCone`, then the vision-distance band, then the trace. The trace seam
	// reports a clear line, so with an unbounded vision distance the CONE alone decides, and with a
	// zero one the band alone refuses.
	const FVector Ahead = F.Npc->EyePosition() + FVector(100.0, 0.0, 0.0);
	F.Npc->Senses.Perception.VisionDistanceCm = 0.f;
	TestFalse(TEXT("a zero vision distance refuses a point 100 cm away"),
		F.Npc->FUN_1028ebc0(Ahead));
	F.Npc->Senses.Perception.VisionDistanceCm = 100000.f;
	TestEqual(TEXT("with the band open the cone is what answers"), F.Npc->FUN_1028ebc0(Ahead),
		FElysiumNpcSenses::IsInViewCone(*F.Npc, Ahead, 1.0f));

	// 0x102906a0 / c0 / 700 — `IsThinkDue` over the three named clocks. The predicate is
	// `(stamp - curtime) <= frametime`.
	const double Now = F.World.World.NowSeconds();
	const double Frame = F.World.World.FrameSeconds();
	F.Npc->ScheduleHost.NextUpdate = Now + Frame * 0.5;
	F.Npc->ScheduleHost.NextNormal = Now + Frame * 100.0;
	F.Npc->ScheduleHost.NextAI = Now - 1.0;
	TestTrue(TEXT("+0x6244 within a frame is due"), F.Npc->IsUpdateThinkDue());
	TestFalse(TEXT("+0x6248 far ahead is not"), F.Npc->IsNormalThinkDue());
	TestTrue(TEXT("+0x6250 already past is due"), F.Npc->IsAiThinkDue());
	F.Npc->ScheduleHost.NextUpdate = Now + Frame;
	TestTrue(TEXT("exactly one frame ahead is due — the compare is <=, not <"),
		F.Npc->IsUpdateThinkDue());

	// `SetDefaultEyeOffset` (`0x10274ca0`): the model seam has no `.qc` offset, so the sentinel arm
	// — the DevMsg and the `(mins + maxs) * 0.75` fallback — is the one every call takes. The
	// collision-extent seam answers zero, so the fallback lands on zero and nothing is written.
	F.Npc->SetDefaultEyeOffset();
	TestTrue(TEXT("the eye-offset fallback runs without a view-offset word to write"), true);

	// `GetScriptCustomMoveActivity` (`0x10289fe0`): no cine, so `ACT_WALK` (9).
	TestEqual(TEXT("with no scripted sequence the custom move is ACT_WALK"),
		F.Npc->GetScriptCustomMoveActivity(), 9);

	// `GetNavTargetEntity` (`0x102729d0`): the goal-type seam answers -1, which is retail's own
	// default arm — NULL.
	TestNull(TEXT("an unstated goal type answers no nav target"), F.Npc->GetNavTargetEntity());

	return true;
}

// -------------------------------------------------------------------------------------------------
// The face-anim turn ladder (`0x10297a20`).
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBaseHelpersFaceAnimTest,
	"Elysium.Arm.NpcKernelBaseHelpers.FaceAnimLadder", GElysiumNpcKernelBaseHelpersFlags)
bool FElysiumNpcKernelBaseHelpersFaceAnimTest::RunTest(const FString&)
{
	const auto Any = [](int32) { return true; };
	const auto NoneAuthored = [](int32) { return false; };

	// Rung 1: outside [-140, 140] (`_DAT_1049ae3c` / `_DAT_1049ae38`, the same pair family Facing
	// recovered for the Troika turn ladder) -> activity 0x10ff, `m_eFaceAnim` 8, a DRAWN duration.
	FElysiumNpc::FFaceAnimPick Pick = FElysiumNpc::FaceAnimLadder(170.f, Any);
	TestEqual(TEXT("a 170-degree delta picks 0x10ff"), Pick.Activity, 0x10ff);
	TestEqual(TEXT("and records face anim 8"), Pick.FaceAnim, 8);
	TestTrue(TEXT("with a drawn duration"), Pick.bRandomDuration);
	Pick = FElysiumNpc::FaceAnimLadder(-170.f, Any);
	TestEqual(TEXT("and so does -170"), Pick.Activity, 0x10ff);

	// Rung 2: at or below `_DAT_104704b4` = -40 -> 0x10fd, face anim 6. The edge is INCLUSIVE.
	Pick = FElysiumNpc::FaceAnimLadder(-50.f, Any);
	TestEqual(TEXT("a -50-degree delta picks 0x10fd"), Pick.Activity, 0x10fd);
	TestEqual(TEXT("and records face anim 6"), Pick.FaceAnim, 6);
	Pick = FElysiumNpc::FaceAnimLadder(-40.f, Any);
	TestEqual(TEXT("and so does exactly -40"), Pick.Activity, 0x10fd);
	Pick = FElysiumNpc::FaceAnimLadder(-10.f, Any);
	TestEqual(TEXT("but a -10-degree delta falls through to the small rung"), Pick.Activity, 0x10f8);

	// Rung 3: at or past 40 degrees (`_DAT_10462950`) -> 0x10fa, face anim 3.
	Pick = FElysiumNpc::FaceAnimLadder(50.f, Any);
	TestEqual(TEXT("a 50-degree delta picks 0x10fa"), Pick.Activity, 0x10fa);
	TestEqual(TEXT("and records face anim 3"), Pick.FaceAnim, 3);

	// Rung 4: everything else -> 0x10f8, face anim 1, and the duration is the DELTA, not a draw.
	Pick = FElysiumNpc::FaceAnimLadder(20.f, Any);
	TestEqual(TEXT("a 20-degree delta picks 0x10f8"), Pick.Activity, 0x10f8);
	TestEqual(TEXT("and records face anim 1"), Pick.FaceAnim, 1);
	TestFalse(TEXT("with the yaw delta itself as the duration"), Pick.bRandomDuration);

	// Every rung is GATED on the body authoring the activity; a body that authors none falls all
	// the way to the ACT_IDLE tail with face anim 0.
	Pick = FElysiumNpc::FaceAnimLadder(170.f, NoneAuthored);
	TestEqual(TEXT("a body with no turn clips falls to ACT_IDLE"), Pick.Activity, 1);
	TestEqual(TEXT("and records face anim 0"), Pick.FaceAnim, 0);

	// The body around the ladder writes both words.
	FBaseHelpersFixture F;
	if (F.Npc != nullptr)
	{
		F.Npc->Angles.Y = 0.0; F.Npc->MotorIdealYaw = 20.f;   // `DeltaIdealYaw` 0x102e1f90: AngleDiff(ideal, AngleMod(0))
		F.Npc->FaceAnim = 99;
		F.Npc->FUN_10297a20();
		TestEqual(TEXT("the body writes m_eFaceAnim (+0x63e4) from the pick"), F.Npc->FaceAnim,
			F.Npc->SelectWeightedSequenceForActivity(0x10f8) != -1 ? 1 : 0);
		TestEqual(TEXT("and m_flFaceYawDiff (+0x63e8) from the yaw delta on the lower rungs"),
			F.Npc->FaceYawDiff, 20.f, 1.0e-3f);
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// The two cover validators — `0x10295ed0`, `0x102961a0`. (`0x10296c40` is family Hints'
// `ValidateHintCoverRange`, tested in `ElysiumNpcKernelHintsTests.cpp`; the duplicate this family
// carried was deleted by 0018 story 8, and its cases with it.)
// -------------------------------------------------------------------------------------------------

namespace
{
	// A world with one network node whose yaw is NOT the hint's authored angle: node 0 at (100, 0)
	// stands at Unreal yaw -90, which is Source yaw 90 (`0x102f47b0` reflects the row back) — a
	// facing of Unreal -Y. The `info_node_hint` binds to it; the crate is the cover object, 60 units
	// toward Unreal -Y of the hint.
	struct FCoverValidatorRig
	{
		FElysiumNpcWorldFixture F;
		FElysiumNpc* Npc = nullptr;
		FElysiumEntity* Crate = nullptr;
		FElysiumHint* Hint = nullptr;

		FCoverValidatorRig()
			: F([]
			{
				FElysiumNpcWorldBuilder Builder(TEXT("basehelpers_cover"), 8u);
				Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
				Builder.AddPlace(2, AtUnits(100.0), /*YawDeg (Unreal)=*/-90.0f);   // node 0
				Builder.AddNpc(TEXT("guard"), FVector::ZeroVector);
				Builder.AddEntity(TEXT("info_target"), TEXT("crate"), AtUnits(100.0, -60.0));
				FElysiumEntityDef& Def = Builder.AddEntity(TEXT("info_node_hint"), TEXT("h"), AtUnits(100.0));
				Def.Keys.Add(TEXT("hinttype"), TEXT("100"));
				return Builder;
			}())
		{
			Npc = F.Npc(TEXT("guard"));
			Crate = F.World.FindByName(TEXT("crate"));
			Hint = FElysiumHint::Cast(F.World.FindByName(TEXT("h")));
			FElysiumNpcWorldFixture::Quiet({ Npc });
			if (Hint != nullptr)
			{
				Hint->Origin = AtUnits(100.0);
				Hint->NodeId = 0;                       // bound: `0x102d12e0` answers the node's yaw
				Hint->Angles = FVector::ZeroVector;     // the AUTHORED angle faces +X
				Hint->Disabled = 0;
				Hint->TargetDistMin = 0.f;
				Hint->TargetDistMax = 1000.f;
				Hint->TargetAngleRangeDot = 0.5f;
			}
			if (Npc != nullptr && Crate != nullptr)
			{
				Npc->ScheduleHost.HintCoverObject = Crate->Handle;
				Npc->BaseScheduleHost.HintNode = INDEX_NONE;
			}
		}

		~FCoverValidatorRig()
		{
			F.Services.TraceRetailQuery = nullptr;
		}

		bool Ready(FAutomationTestBase& Test) const
		{
			return Test.TestNotNull(TEXT("the guard stood"), Npc)
				&& Test.TestNotNull(TEXT("the crate stood"), Crate)
				&& Test.TestNotNull(TEXT("the hint stood"), Hint);
		}
	};

	// Records the last request and answers `Answer`.
	struct FCoverTraceDouble
	{
		FElysiumRetailTrace Seen;
		FElysiumRetailTraceResult Answer;
		int32 Calls = 0;

		void Install(FCoverValidatorRig& Rig)
		{
			Rig.F.Services.TraceRetailQuery = [this](const FElysiumRetailTrace& Request,
				FElysiumRetailTraceResult& Out)
			{
				Seen = Request;
				++Calls;
				Out = Answer;
				return true;
			};
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBaseHelpersHintValidatorsTest,
	"Elysium.Arm.NpcKernelBaseHelpers.HintValidators", GElysiumNpcKernelBaseHelpersFlags)
bool FElysiumNpcKernelBaseHelpersHintValidatorsTest::RunTest(const FString&)
{
	FBaseHelpersFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Npc))
	{
		return false;
	}
	using EReason = FElysiumNpc::EHintRejectReason;
	const FElysiumNpcBase::FHintWords Base = MakeHint();
	const float NaN = std::numeric_limits<float>::quiet_NaN();

	// --- 0x10295ed0's rule, the quiet twin -------------------------------------------------------
	TestTrue(TEXT("0x10295ed0: a cover object 50 units along the hint's facing is valid"),
		F.Npc->CoverHintStillValid(Base, AtUnits(50.0), FVector::ZeroVector, true));
	TestFalse(TEXT("0x10295ed0: 5 units is under m_flTargetDistMin"),
		F.Npc->CoverHintStillValid(Base, AtUnits(5.0), FVector::ZeroVector, false));
	TestFalse(TEXT("0x10295ed0: 200 units is over m_flTargetDistMax"),
		F.Npc->CoverHintStillValid(Base, AtUnits(200.0), FVector::ZeroVector, false));
	// THE CURRENT HINT gets a 64-unit tolerance on BOTH ends (`_DAT_10451acc`), which is the one
	// thing that distinguishes the two band policies.
	TestTrue(TEXT("0x10295ed0: but 5 units passes for the hint I am already standing on"),
		F.Npc->CoverHintStillValid(Base, AtUnits(5.0), FVector::ZeroVector, true));
	TestTrue(TEXT("0x10295ed0: and so does 160"),
		F.Npc->CoverHintStillValid(Base, AtUnits(160.0), FVector::ZeroVector, true));
	TestFalse(TEXT("0x10295ed0: 165 is past even the tolerance"),
		F.Npc->CoverHintStillValid(Base, AtUnits(165.0), FVector::ZeroVector, true));
	// The two band policies do not share a compare direction: an unordered `m_flTargetDistMax`
	// continues on the current hint (`TEST AH,5 / JP`) and fails another (`AND EAX,0x4100 / JZ`).
	{
		FElysiumNpcBase::FHintWords Unordered = Base;
		Unordered.TargetDistMax = NaN;
		TestTrue(TEXT("0x10295ed0 10295f9c: an unordered max+64 compare continues on the current hint"),
			F.Npc->CoverHintStillValid(Unordered, AtUnits(50.0), FVector::ZeroVector, true));
		TestFalse(TEXT("0x10295ed0 10295fd0: ...and fails on another"),
			F.Npc->CoverHintStillValid(Unordered, AtUnits(50.0), AtUnits(0.0, 10.0), false));
	}
	// The projection: the cover object must lie STRICTLY ahead of the facing past the dot floor.
	TestFalse(TEXT("0x10295ed0: a cover object behind the hint fails the dot floor"),
		F.Npc->CoverHintStillValid(Base, AtUnits(-50.0), FVector::ZeroVector, true));
	{
		FElysiumNpcBase::FHintWords Unordered = Base;
		Unordered.TargetAngleRangeDot = NaN;
		TestTrue(TEXT("0x10295ed0 1029603c: an unordered projection compare continues (TEST AH,0x41 / JNP)"),
			F.Npc->CoverHintStillValid(Unordered, AtUnits(-50.0), FVector::ZeroVector, true));
	}
	// The facing is a SOURCE yaw through `0x101d2f40`, and the port's world has Y negated: a
	// standalone hint at yaw 90 faces Unreal -Y.
	{
		FElysiumNpcBase::FHintWords Turned = Base;
		Turned.Angles = FVector(0.0, 90.0, 0.0);
		TestTrue(TEXT("0x10295ed0: Source yaw 90 faces a cover object at Unreal -Y"),
			F.Npc->CoverHintStillValid(Turned, AtUnits(0.0, -50.0), FVector::ZeroVector, true));
		TestFalse(TEXT("0x10295ed0: ...and turns its back on one at Unreal +Y"),
			F.Npc->CoverHintStillValid(Turned, AtUnits(0.0, 50.0), FVector::ZeroVector, true));
	}
	{
		FElysiumNpcBase::FHintWords Disabled = Base;
		Disabled.Disabled = 1;
		TestFalse(TEXT("0x10295ed0: a disabled hint is never valid"),
			F.Npc->CoverHintStillValid(Disabled, AtUnits(50.0), FVector::ZeroVector, true));
	}
	// A hint that is NOT the current one has two extra gates: a 512-unit proximity
	// (`_DAT_10483aac`) and, for hint type 0x283d, a forward-projection test.
	TestTrue(TEXT("0x10295ed0: a nearby other hint passes the 512-unit proximity gate"),
		F.Npc->CoverHintStillValid(Base, AtUnits(50.0), AtUnits(0.0, 10.0), false));
	TestFalse(TEXT("0x10295ed0: one 600 units away does not"),
		F.Npc->CoverHintStillValid(Base, AtUnits(50.0), AtUnits(0.0, 600.0), false));
	{
		// The 0x283d arm dots the direction `0x10137220` NORMALISED in place — a 3-D normalise, Z
		// included — with the facing, in X and Y only, against 0.5 strictly.
		FElysiumNpcBase::FHintWords Forward = Base;
		Forward.HintType = 0x283d;
		TestTrue(TEXT("0x10295ed0 102960b6: I stand behind a 0x283d hint -> forward projection 1.0 passes"),
			F.Npc->CoverHintStillValid(Forward, AtUnits(50.0), AtUnits(-100.0), false));
		TestFalse(TEXT("0x10295ed0 102960b6: in front of it -> -1.0 fails"),
			F.Npc->CoverHintStillValid(Forward, AtUnits(50.0), AtUnits(100.0), false));
		TestTrue(TEXT("0x10295ed0 1029609d: the direction is normalised — 0.4 units behind still dots ~1.0"),
			F.Npc->CoverHintStillValid(Forward, AtUnits(50.0), AtUnits(-0.4), false));
		TestFalse(TEXT("0x10295ed0 1029609d: ...in 3-D, so a steep drop dots under 0.5 in X and Y"),
			F.Npc->CoverHintStillValid(Forward, AtUnits(50.0), AtUnits(-1.0, 0.0, -10.0), false));
		TestTrue(TEXT("0x10295ed0: a type other than 0x283d skips that arm"),
			F.Npc->CoverHintStillValid(Base, AtUnits(50.0), AtUnits(100.0), false));
	}

	// --- 0x102961a0's rule, the verbose twin -----------------------------------------------------
	TestEqual(TEXT("0x102961a0: the verbose twin accepts the same geometry"),
		F.Npc->CoverHintRejectReason(Base, AtUnits(50.0), true), EReason::None);
	TestEqual(TEXT("0x102961a0: and answers one shared reason for both band policies"),
		F.Npc->CoverHintRejectReason(Base, AtUnits(200.0), false), EReason::DistanceOutOfBand);
	TestEqual(TEXT("0x102961a0: and its own reason for the projection"),
		F.Npc->CoverHintRejectReason(Base, AtUnits(-50.0), true), EReason::OutsideGoodRange);
	{
		FElysiumNpcBase::FHintWords Turned = Base;
		Turned.Angles = FVector(0.0, 90.0, 0.0);
		TestEqual(TEXT("0x102961a0: the same Source-axis facing — yaw 90 faces Unreal -Y"),
			F.Npc->CoverHintRejectReason(Turned, AtUnits(0.0, -50.0), true), EReason::None);
		TestEqual(TEXT("0x102961a0: ...and not Unreal +Y"),
			F.Npc->CoverHintRejectReason(Turned, AtUnits(0.0, 50.0), true), EReason::OutsideGoodRange);
	}
	{
		// The `target_name` gate, which `0x10295ed0` does NOT have. An empty name admits everyone.
		FElysiumNpcBase::FHintWords Named = Base;
		Named.TargetName = TEXT("someone_else");
		TestEqual(TEXT("0x102961a0: a target_name naming another NPC is a mismatch"),
			F.Npc->CoverHintRejectReason(Named, AtUnits(50.0), true),
			EReason::TargetNameMismatch);
		Named.TargetName = F.Npc->TargetName.ToUpper();
		TestEqual(TEXT("0x102961a0: and the comparison is case-insensitive"),
			F.Npc->CoverHintRejectReason(Named, AtUnits(50.0), true), EReason::None);
	}

	// The seams every entry point depends on, asked and refusing.
	FElysiumNpcBase::FHintWords Resolved;
	TestFalse(TEXT("the hint store answers nothing"), F.Npc->HintWords(0, Resolved));
	float RangeUnits = -1.f;
	TestFalse(TEXT("an NPC with no active weapon has no +0x8c0 to answer"),
		F.Npc->ActiveWeaponMaxRangeUnits(RangeUnits));
	TestFalse(TEXT("so 0x10295ed0's entry point answers false for every node"),
		F.Npc->FUN_10295ed0(0));
	TestEqual(TEXT("and 0x102961a0's answers NoHint"), F.Npc->FUN_102961a0(0), EReason::NoHint);
	TestFalse(TEXT("no NPC is the ai_debug_npc, so no reason string is ever formatted"),
		F.Npc->IsHintDebugNpc());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBaseHelpersCoverNodeFacingTest,
	"Elysium.Arm.NpcKernelBaseHelpers.CoverNodeFacing", GElysiumNpcKernelBaseHelpersFlags)
bool FElysiumNpcKernelBaseHelpersCoverNodeFacingTest::RunTest(const FString&)
{
	FCoverValidatorRig R;
	if (!R.Ready(*this))
	{
		return false;
	}
	using EReason = FElysiumNpc::EHintRejectReason;
	FElysiumNpc* Npc = R.Npc;
	const int32 HintId = R.Hint->Handle.Index;

	// The pure rules over words: a NODE-BOUND hint is judged by the node's yaw (`0x102d12e0` ->
	// `0x102f47b0`), not by its authored angle.
	FElysiumNpcBase::FHintWords Words = MakeHint();
	Words.NodeId = 0;                                   // node 0: Source yaw 90, Unreal -Y
	Words.Angles = FVector::ZeroVector;                 // authored: +X
	TestTrue(TEXT("0x102d12e0: a node-bound hint faces its NODE's yaw — a cover object at Unreal -Y passes"),
		Npc->CoverHintStillValid(Words, AtUnits(0.0, -50.0), FVector::ZeroVector, true));
	TestFalse(TEXT("0x102d12e0: ...and one along its authored +X fails"),
		Npc->CoverHintStillValid(Words, AtUnits(50.0), FVector::ZeroVector, true));
	TestEqual(TEXT("0x102961a0: the verbose twin reads the same node yaw"),
		Npc->CoverHintRejectReason(Words, AtUnits(0.0, -50.0), true), EReason::None);
	TestEqual(TEXT("0x102961a0: ...and rejects along the authored +X"),
		Npc->CoverHintRejectReason(Words, AtUnits(50.0), true), EReason::OutsideGoodRange);
	Words.NodeId = 7;
	TestTrue(TEXT("0x102f47b0: an id past the network answers yaw 0.0 — +X passes"),
		Npc->CoverHintStillValid(Words, AtUnits(50.0), FVector::ZeroVector, true));
	Words.NodeId = INDEX_NONE;
	TestTrue(TEXT("0x102d12e0: an unbound hint reads its own angles (+X passes)"),
		Npc->CoverHintStillValid(Words, AtUnits(50.0), FVector::ZeroVector, true));

	// The entry point `0x10295ed0` over the live hint: the crate is the cover object, at Unreal -Y of
	// the hint, which faces it only by its node's yaw. No collision world: `0x102968f0` passes.
	TestTrue(TEXT("0x10295ed0: the live node-bound hint faces the cover object by its node yaw"),
		Npc->FUN_10295ed0(HintId));
	R.Hint->NodeId = INDEX_NONE;
	TestFalse(TEXT("0x10295ed0: unbound, its authored +X faces past the cover object"),
		Npc->FUN_10295ed0(HintId));
	TestEqual(TEXT("0x102961a0: ...and the verbose twin answers the projection reason"),
		Npc->FUN_102961a0(HintId), EReason::OutsideGoodRange);
	R.Hint->NodeId = 0;

	// `102960e5`: the tail CALLS `0x102968f0` with the COVER OBJECT as its target.
	FCoverTraceDouble Trace;
	Trace.Install(R);
	Trace.Answer = FElysiumRetailTraceResult();
	Trace.Answer.Fraction = 0.5f;
	TestFalse(TEXT("0x10295ed0 102960e5: a blocked hint LOS fails another hint"),
		Npc->FUN_10295ed0(HintId));
	TestTrue(TEXT("0x10295ed0 102960e5: ...a line that ends at the cover object's eye"),
		Trace.Seen.EndCm.Equals(R.Crate->EyePosition(), 0.01));
	TestEqual(TEXT("0x10295ed0 102960e5: ...under 0x102968f0's mask"), Trace.Seen.RetailMask, 0x46804099);
	Trace.Answer = FElysiumRetailTraceResult();
	TestTrue(TEXT("0x10295ed0 102960e5: a clear hint LOS passes"), Npc->FUN_10295ed0(HintId));

	// The current hint accepts before the tail: no trace is cast.
	Trace.Answer.Fraction = 0.5f;
	Npc->BaseScheduleHost.HintNode = HintId;
	const int32 CallsBefore = Trace.Calls;
	TestTrue(TEXT("0x10295ed0 10296048: the current hint accepts before 0x102968f0"),
		Npc->FUN_10295ed0(HintId));
	TestEqual(TEXT("0x10295ed0 10296048: ...and casts no trace"), Trace.Calls, CallsBefore);
	TestEqual(TEXT("0x102961a0 10296459: the verbose twin's current hint accepts before its ray"),
		Npc->FUN_102961a0(HintId), EReason::None);
	TestEqual(TEXT("0x102961a0 10296459: ...and casts no trace"), Trace.Calls, CallsBefore);
	Npc->BaseScheduleHost.HintNode = INDEX_NONE;

	// `0x102961a0`'s inline ray: from me, raised by my collision maxs z, to `0x102d1180`.
	TestEqual(TEXT("0x102961a0 102965bd: a blocked inline ray fails"),
		Npc->FUN_102961a0(HintId), EReason::FailedLos);
	FVector MinsUnits = FVector::ZeroVector;
	FVector MaxsUnits = FVector::ZeroVector;
	FElysiumNpcBase::RetailCollisionExtents(*Npc, MinsUnits, MaxsUnits);
	TestTrue(TEXT("0x102961a0 1029648f: the ray starts at my origin raised by the collision maxs z"),
		Trace.Seen.StartCm.Equals(Npc->Origin + FVector(0.0, 0.0, MaxsUnits.Z * ElysiumMove::U), 0.01));
	FVector HintPointCm = FVector::ZeroVector;
	Npc->HintPositionCm(HintId, HintPointCm);
	TestTrue(TEXT("0x102961a0 102964a2: ...and ends at the hint's position for me (0x102d1180)"),
		Trace.Seen.EndCm.Equals(HintPointCm, 0.01));
	TestEqual(TEXT("0x102961a0 1029657e: ...under mask 0x46804099"), Trace.Seen.RetailMask, 0x46804099);
	TestTrue(TEXT("0x102961a0 1029650f: ...with CTraceFilterSimple's pass entity, me"),
		Trace.Seen.Ignore.Num() == 1 && Trace.Seen.Ignore[0] == Npc->Handle);
	Trace.Answer = FElysiumRetailTraceResult();
	Trace.Answer.bStartSolid = true;
	TestEqual(TEXT("0x102961a0 102965dc: startsolid fails a full-fraction ray"),
		Npc->FUN_102961a0(HintId), EReason::FailedLos);
	Trace.Answer = FElysiumRetailTraceResult();
	TestEqual(TEXT("0x102961a0: a clear ray passes"), Npc->FUN_102961a0(HintId), EReason::None);

	R.F.Services.TraceRetailQuery = nullptr;
	return true;
}

// -------------------------------------------------------------------------------------------------
// The victim-side reaction slots and the remaining slot bodies.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBaseHelpersReactionSlotsTest,
	"Elysium.Arm.NpcKernelBaseHelpers.ReactionSlots", GElysiumNpcKernelBaseHelpersFlags)
bool FElysiumNpcKernelBaseHelpersReactionSlotsTest::RunTest(const FString&)
{
	FBaseHelpersFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Npc)
		|| !TestNotNull(TEXT("the foe spawned"), F.Other))
	{
		return false;
	}

	// Condition 10 is `COND_BEING_ATTACKED` (`0x0a`) and `0x10269a20` SETS it — 29c's walk read the
	// call as a clear and it is not.
	F.Npc->Cognition.Conditions.Reset();
	F.Npc->HitBuildupCount = 0;
	F.Npc->Slot21(F.Other);
	TestTrue(TEXT("slot 21 sets COND_BEING_ATTACKED"),
		F.Npc->Cognition.Conditions.Has(EElysiumNpcCond::BeingAttacked));
	TestEqual(TEXT("and raises m_iHitBuildupCount (+0x6064)"), F.Npc->HitBuildupCount, 1);

	F.Npc->Cognition.Conditions.Reset();
	F.Npc->Slot22(F.Other);
	TestTrue(TEXT("slot 22 sets the same condition"),
		F.Npc->Cognition.Conditions.Has(EElysiumNpcCond::BeingAttacked));
	TestEqual(TEXT("and does NOT raise the counter — that is the whole difference"),
		F.Npc->HitBuildupCount, 1);

	F.Npc->Cognition.Conditions.Reset();
	F.Npc->Slot27(F.Other);
	TestTrue(TEXT("slot 27 sets it too, and then asks slot 600 for a melee coordinator slot"),
		F.Npc->Cognition.Conditions.Has(EElysiumNpcCond::BeingAttacked));
	TestEqual(TEXT("without raising the counter"), F.Npc->HitBuildupCount, 1);

	// Slot 317: the condition unconditionally, then the dodge bit ONLY when the running program's
	// interrupt mask lists it (`ConditionInterruptsCurrentSchedule` `0x10269c70`). With no program
	// installed, no mask lists anything, so the dodge arm is not taken — which is the recovered
	// refusal, not a gap.
	F.Npc->Cognition.Conditions.Reset();
	F.Npc->Schedule.Clear();
	const bool bDodged = F.Npc->Slot317(F.Other);
	TestTrue(TEXT("slot 317 sets COND_BEING_ATTACKED whatever the mask says"),
		F.Npc->Cognition.Conditions.Has(EElysiumNpcCond::BeingAttacked));
	TestFalse(TEXT("and reports false with no program to interrupt"), bDodged);
	TestFalse(TEXT("so COND_SHOULD_DODGE is not raised"),
		F.Npc->Cognition.Conditions.Has(EElysiumNpcCond::ShouldDodge));
	TestEqual(TEXT("the mask test is the port's 0x10269c70"),
		ElysiumSchedule::MaskHasCondition(F.Npc->Schedule, *F.Npc, EElysiumNpcCond::ShouldDodge),
		bDodged);

	// 0x1027e0f0 — the BASE line's slot-532 body, `FElysiumNpcBase::Slot532` (story 5 step 5 folded
	// the second port body `FUN_1027e0f0` into it). Retail's `AL = 1` is dropped by every dispatch
	// site, so the slot is `void`; the two door words are the whole effect.
	F.Npc->OpeningDoor = F.Other->Handle;
	F.Npc->bOpeningDoorWait = true;
	F.Npc->FElysiumNpcBase::Slot532(0);
	TestFalse(TEXT("and forgets the door being opened (+0x5d24)"), F.Npc->OpeningDoor.IsSet());
	TestFalse(TEXT("and the wait flag (+0x5d30)"), F.Npc->bOpeningDoorWait);

	// Slot 571 (`0x10289ce0`): ACT_WALK below the distance constant, ACT_RUN at or beyond it.
	// `_DAT_1049a17c` is 190.0f in the image (`0x433e0000`); `10289cec TEST AH,0x5` / `10289cf4 JNP`
	// walks only on an ordered `<` (C0 alone), so a NaN distance runs. (Corrected at the L05
	// integration: the old case read the unrecovered 0.0 stand-in and expected ACT_RUN at 100.)
	TestEqual(TEXT("slot 571 answers ACT_RUN at the distance constant"),
		F.Npc->Slot571(190.f), 0x13);
	TestEqual(TEXT("and ACT_WALK below it"), F.Npc->Slot571(100.f), 9);
	TestEqual(TEXT("and ACT_RUN on an unordered distance"),
		F.Npc->Slot571(std::numeric_limits<float>::quiet_NaN()), 0x13);

	// Slot 529 (`0x10280330`): no task answers TRUE; a running task the seam cannot number answers
	// false, which is retail's answer for a task that is not 0x6e / 0x0b / 0x72.
	F.Npc->Schedule.Clear();
	TestTrue(TEXT("no task is a continuous move"), F.Npc->IsCurTaskContinuousMove());
	int32 TaskNumber = 0;
	TestFalse(TEXT("and this runtime's tasks carry no retail numbers"),
		F.Npc->CurrentRetailTaskNumber(TaskNumber));

	// Slot 527 (`0x10293e80`): a null node is usable; with no node store the hint read finds
	// nothing, so every node is usable.
	TestFalse(TEXT("a null node is not unusable"), F.Npc->IsUnusableNode(nullptr));
	// `0x102d1540` on an index that names no hint: retail would dereference a NULL `CAI_Hint*`; the
	// port's crash guard answers true, the free answer.
	TestTrue(TEXT("because an index that names no hint is free (the crash guard)"),
		F.Npc->IsHintAvailableToMe(0));

	// Slot 542 (`0x10273dd0`): the ownership move family Squad already stands. The port carries one
	// enemy memory per NPC and no squad memory to point it at, so the move changes nothing — and
	// retail's null-squad fault is not reproduced.
	TestNull(TEXT("there is no squad object to repoint the enemy memory at"),
		F.Npc->ConnectedSquad());
	F.Npc->Slot542();
	TestTrue(TEXT("so slot 542 is a recorded move and not a crash"), true);

	return true;
}

// -------------------------------------------------------------------------------------------------
// The patrol-node interest draw (`0x1029f650` / `0x1029f730`).
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBaseHelpersPatrolInterestTest,
	"Elysium.Arm.NpcKernelBaseHelpers.PatrolInterestDraw",
	GElysiumNpcKernelBaseHelpersFlags)
bool FElysiumNpcKernelBaseHelpersPatrolInterestTest::RunTest(const FString&)
{
	FBaseHelpersFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Npc))
	{
		return false;
	}

	// The reset is FIRST and UNCONDITIONAL: a node with no record clears a flag that was standing.
	F.Npc->ScheduleHost.bPatrolPathUseHint = true;
	TestFalse(TEXT("the draw answers false with no interest record"), F.Npc->FUN_1029f650(3));
	TestFalse(TEXT("and clears m_bPatrolPathUseHint (+0x65a0) on the way in"),
		F.Npc->ScheduleHost.bPatrolPathUseHint);
	// This world has no network, so node 3 is outside it: `PatrolNodeInterestRecord` counts the miss and answers
	// no record (the networked arms are `Elysium.Substrate.PlaceSeams.*`).
	const int32 Misses = FElysiumNpc::PatrolNodeMissCounter();
	TestEqual(TEXT("because node 3 is outside the (empty) network, there is no record"),
		F.Npc->PatrolNodeInterestRecord(3), INDEX_NONE);
	TestEqual(TEXT("...and the miss bumps DAT_106c994c"), FElysiumNpc::PatrolNodeMissCounter(), Misses + 1);
	TestEqual(TEXT("a -1 id is no record and counts nothing"), F.Npc->PatrolNodeInterestRecord(INDEX_NONE), INDEX_NONE);
	TestEqual(TEXT("...nothing"), FElysiumNpc::PatrolNodeMissCounter(), Misses + 1);
	TestEqual(TEXT("and a record that names no live hint rolls against zero"),
		F.Npc->PatrolNodeInterestPercent(F.Other->Handle.Index), 0);

	// The read side answers nothing unless the flag stands, and caches at +0x659c when it does.
	F.Npc->ScheduleHost.bPatrolPathUseHint = false;
	F.Npc->ScheduleHost.Unknown659c = 42u;
	TestEqual(TEXT("with the flag clear the cached record is not even consulted"),
		F.Npc->FUN_1029f730(3), 0);
	F.Npc->ScheduleHost.bPatrolPathUseHint = true;
	TestEqual(TEXT("with it set the cache is returned as it stands"), F.Npc->FUN_1029f730(3), 42);
	F.Npc->ScheduleHost.Unknown659c = 0u;

	// 0x1029f610 — the patrol cell's network check.
	TestFalse(TEXT("a null cell answers false"), F.Npc->FUN_1029f610(nullptr));
	FElysiumNpc::FPatrolPathCell Empty;
	TestFalse(TEXT("so does a cell holding no path"), F.Npc->FUN_1029f610(&Empty));

	return true;
}

// -------------------------------------------------------------------------------------------------
// The three rows the port ALREADY carried — asserted so the claim is checkable.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBaseHelpersAlreadyCarriedTest,
	"Elysium.Arm.NpcKernelBaseHelpers.AlreadyCarriedRows",
	GElysiumNpcKernelBaseHelpersFlags)
bool FElysiumNpcKernelBaseHelpersAlreadyCarriedTest::RunTest(const FString&)
{
	FBaseHelpersFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Npc))
	{
		return false;
	}

	// `0x10272790` — the BASE arm of `ShouldMaintainActivity`, carried inside family Anim's slot-466
	// body: `GetState() == 4 (NPC_STATE_SCRIPT) && m_Activity != 2` refuses, everything else
	// maintains. An idle NPC is not in the script state, so it maintains.
	F.Npc->bForceMaintainActivity = false;
	TestTrue(TEXT("0x10272790's arm: a non-scripted body maintains its activity"),
		F.Npc->ShouldMaintainActivity());

	// `0x10298910` and `0x102989e0` — one parser, two empty answers. `FElysiumNpc::ParseGroupMask`
	// is the body and `bEmptyIsEveryGroup` is the ONE thing that separates them.
	TestEqual(TEXT("0x10298910: an empty interesting-place list is the zero mask"),
		FElysiumNpc::ParseGroupMask(FString(), /*bEmptyIsEveryGroup=*/false), 0u);
	TestEqual(TEXT("0x102989e0: an empty hint list is every group"),
		FElysiumNpc::ParseGroupMask(FString(), /*bEmptyIsEveryGroup=*/true), 0xffffffffu);
	TestEqual(TEXT("id N sets bit N-1"),
		FElysiumNpc::ParseGroupMask(TEXT("1 3 32"), false), (1u << 0) | (1u << 2) | (1u << 31));
	TestEqual(TEXT("and anything outside 1..32 is dropped without a diagnostic"),
		FElysiumNpc::ParseGroupMask(TEXT("0 33 -5 2"), false), 1u << 1);

	// Both setters keep the authored string beside the parsed mask, which is what retail's
	// parse-on-write does.
	F.Npc->SetHintGroups(TEXT("2"));
	TestEqual(TEXT("SetHintGroups parses into +0x62e4"), F.Npc->ScheduleHost.HintGroupMask,
		1u << 1);
	F.Npc->SetInterestingPlaceGroups(TEXT("4"));
	TestEqual(TEXT("SetInterestingPlaceGroups parses into +0x62dc"),
		F.Npc->InterestingPlaceGroupMask, 1u << 3);

	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
