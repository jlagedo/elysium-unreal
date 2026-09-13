#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 29c-1, family **BaseHelpers** — `CAI_BaseNPC`'s own unnamed layer 0–9 bodies.
//
// Every assertion below comes from the decompiled C: the two melee ladders' thresholds and the
// three ways they differ, `SelectRandomExpressionForState`'s state-to-word pairing (which is NOT
// SDK 2013's field order), `SetExpression`'s three arms, `RememberUnreachable`'s backward scan,
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
	FElysiumNpc::FHintWords MakeHint()
	{
		FElysiumNpc::FHintWords Hint;
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
// Slots 555 / 556 — `MeleeAttack1Conditions` (`0x1026d9a0`) and `MeleeAttack2Conditions`
// (`0x1026da90`).
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBaseHelpersMeleeConditionsTest,
	"Elysium.Substrate.NpcKernelBaseHelpers.MeleeAttackConditions",
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
	const int32 TooFarForMelee = static_cast<int32>(EElysiumNpcCond::TooFarForMelee);   // 0x09
	const int32 CanMelee1 = static_cast<int32>(EElysiumNpcCond::CanMeleeAttack1);       // 0x51
	const int32 CanMelee2 = static_cast<int32>(EElysiumNpcCond::CanMeleeAttack2);       // 0x52

	// Rung 2 of both ladders: past the shared 64-unit attack band (`_DAT_10451acc`) the answer is
	// `COND_TOO_FAR_TO_ATTACK`, whatever the dot is.
	TestEqual(TEXT("1: past 64 units answers COND_TOO_FAR_TO_ATTACK"),
		F.Npc->MeleeAttack1Conditions(1.0f, 64.1f), TooFarToAttack);
	TestEqual(TEXT("2: past 64 units answers COND_TOO_FAR_TO_ATTACK"),
		F.Npc->MeleeAttack2Conditions(1.0f, 64.1f), TooFarToAttack);
	TestEqual(TEXT("1: exactly 64 units is INSIDE the band (retail's compare is strictly greater)"),
		F.Npc->MeleeAttack1Conditions(0.0f, 64.0f), None);

	// The outer rung is where the two ladders differ first. `0x1026da90` reads
	// `_DAT_1044c3a8 = 180.0f`; `0x1026d9a0` reads `_DAT_1044ddb0`, which is UNRECOVERED and stands
	// at +inf, so its `COND_TOO_FAR_FOR_MELEE` arm is deliberately unreachable and says so.
	TestEqual(TEXT("2: past 180 units answers COND_TOO_FAR_FOR_MELEE"),
		F.Npc->MeleeAttack2Conditions(1.0f, 180.1f), TooFarForMelee);
	TestEqual(TEXT("2: exactly 180 units falls through to the 64-unit band"),
		F.Npc->MeleeAttack2Conditions(1.0f, 180.0f), TooFarToAttack);
	TestEqual(TEXT("1: the unrecovered outer band leaves the 0x09 arm unreachable"),
		F.Npc->MeleeAttack1Conditions(1.0f, 1.0e9f), TooFarToAttack);

	// The dot gate, `flDot < 0.7` (`_DAT_104492d0`, read as a double; SDK 2013's twin states the
	// literal). Shared by both bodies.
	TestEqual(TEXT("1: a dot under 0.7 refuses"), F.Npc->MeleeAttack1Conditions(0.69f, 10.f), None);
	TestEqual(TEXT("2: a dot under 0.7 refuses"), F.Npc->MeleeAttack2Conditions(0.69f, 10.f), None);

	// THE SECOND DIFFERENCE: `0x1026d9a0` re-dispatches `GetEnemy()` as a null gate after the dot
	// and `0x1026da90` does not — so with no committed enemy the two answer differently.
	F.Npc->Senses.Memory.Enemy = FElysiumEntityHandle();
	TestEqual(TEXT("1: no enemy refuses after the dot gate"),
		F.Npc->MeleeAttack1Conditions(0.9f, 10.f), None);
	TestEqual(TEXT("2: no enemy still answers COND_CAN_MELEE_ATTACK2 — there is no null gate"),
		F.Npc->MeleeAttack2Conditions(0.9f, 10.f), CanMelee2);

	// THE THIRD DIFFERENCE: the `FL_ONGROUND` read. Only `0x1026d9a0` has it.
	F.Npc->Senses.Memory.Enemy = F.Other->Handle;
	F.Other->Flags &= ~1;
	TestEqual(TEXT("1: an airborne enemy answers nothing"),
		F.Npc->MeleeAttack1Conditions(0.9f, 10.f), None);
	F.Other->Flags |= 1;
	TestEqual(TEXT("1: a grounded enemy answers COND_CAN_MELEE_ATTACK1"),
		F.Npc->MeleeAttack1Conditions(0.9f, 10.f), CanMelee1);
	TestEqual(TEXT("2: the same enemy answers COND_CAN_MELEE_ATTACK2 either way"),
		F.Npc->MeleeAttack2Conditions(0.9f, 10.f), CanMelee2);
	F.Other->Flags &= ~1;
	TestEqual(TEXT("2: and an airborne one too — no ground read in this body"),
		F.Npc->MeleeAttack2Conditions(0.9f, 10.f), CanMelee2);

	return true;
}

// -------------------------------------------------------------------------------------------------
// `CAI_BaseHumanoid`'s branch — five bodies on a class no map stands.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBaseHelpersBaseActorTest,
	"Elysium.Substrate.NpcKernelBaseHelpers.BaseActorBranch", GElysiumNpcKernelBaseHelpersFlags)
bool FElysiumNpcKernelBaseHelpersBaseActorTest::RunTest(const FString&)
{
	FBaseHelpersFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Npc))
	{
		return false;
	}

	// The census confirms the standing fact: no classname resolves to `CAI_BaseHumanoid`, so
	// nothing a map stands ever dispatches these.
	TestFalse(TEXT("no spawnable leaf resolves to CAI_BaseHumanoid — it stands no entity classname"),
		F.Npc->IsRetailClass(TEXT("CAI_BaseHumanoid")));

	// 0x1025f1a0 `HasActiveLookTargets` — the look-queue COUNT at +0x5f94, which family Facing's
	// `LookTargets` carries.
	F.Npc->LookTargets.Reset();
	TestFalse(TEXT("an empty look queue has no active targets"), F.Npc->HasActiveLookTargets());
	F.Npc->LookTargets.AddDefaulted();
	TestTrue(TEXT("one record is enough"), F.Npc->HasActiveLookTargets());

	// 0x1025e780 — the slot-277 override clears ONLY bit 0 of `m_fLatchedPositions`.
	F.Npc->LatchedPositions = 0xff;
	F.Npc->FUN_1025e780(FVector(1.0, 2.0, 3.0));
	TestEqual(TEXT("SetViewtarget's override clears bit 0 and nothing else"),
		F.Npc->LatchedPositions, 0xfe);

	// 0x10260540 `SelectRandomExpressionForState`. The pairing is the BODY's, not SDK 2013's: state
	// 2 reads +0x5fb0 (alert) and state 3 reads +0x5fac (combat), which is the reverse of the SDK's
	// field order, and the port follows the body.
	F.Npc->IdleExpression = TEXT("idle");
	F.Npc->AlertExpression = TEXT("alert");
	F.Npc->CombatExpression = TEXT("combat");
	F.Npc->DeathExpression = TEXT("death");
	F.Npc->ExpressionOverride.Reset();
	const FString* Answer = F.Npc->SelectRandomExpressionForState(1);
	TestEqual(TEXT("state 1 answers the idle expression"),
		Answer != nullptr ? *Answer : FString(), FString(TEXT("idle")));
	Answer = F.Npc->SelectRandomExpressionForState(2);
	TestEqual(TEXT("state 2 answers the alert expression"),
		Answer != nullptr ? *Answer : FString(), FString(TEXT("alert")));
	Answer = F.Npc->SelectRandomExpressionForState(3);
	TestEqual(TEXT("state 3 answers the combat expression"),
		Answer != nullptr ? *Answer : FString(), FString(TEXT("combat")));
	Answer = F.Npc->SelectRandomExpressionForState(5);
	TestEqual(TEXT("state 5 (PLAYDEAD) answers the death expression"),
		Answer != nullptr ? *Answer : FString(), FString(TEXT("death")));
	Answer = F.Npc->SelectRandomExpressionForState(7);
	TestEqual(TEXT("state 7 (DEAD) answers it too"),
		Answer != nullptr ? *Answer : FString(), FString(TEXT("death")));
	TestNull(TEXT("a state with no case answers nothing"),
		F.Npc->SelectRandomExpressionForState(4));
	F.Npc->IdleExpression.Reset();
	TestNull(TEXT("a state whose word is the string_t null answers nothing"),
		F.Npc->SelectRandomExpressionForState(1));

	// The override wins for every state BUT 7 — a dead body takes the per-state table even with an
	// override set, which is the one arm of this body that is not obvious.
	F.Npc->ExpressionOverride = TEXT("override");
	Answer = F.Npc->SelectRandomExpressionForState(3);
	TestEqual(TEXT("the override replaces the combat expression"),
		Answer != nullptr ? *Answer : FString(), FString(TEXT("override")));
	Answer = F.Npc->SelectRandomExpressionForState(7);
	TestEqual(TEXT("but NOT the death one — state 7 skips the override"),
		Answer != nullptr ? *Answer : FString(), FString(TEXT("death")));

	// 0x10260670 / 0x10260750 — `SetExpression` / `ClearExpression`.
	F.Npc->ExpressionScene = TEXT("Scenes/Talk.vcd");
	F.Npc->SetExpression(FString());
	TestTrue(TEXT("an empty name clears the scene word"), F.Npc->ExpressionScene.IsEmpty());

	F.Npc->ExpressionScene = TEXT("Scenes/Talk.vcd");
	F.Npc->SetExpression(TEXT("scenes/TALK.vcd"));
	TestEqual(TEXT("the same name under __strcmpi is a no-op, cache intact"),
		F.Npc->ExpressionScene, FString(TEXT("Scenes/Talk.vcd")));

	F.Npc->SetExpression(TEXT("Scenes/Other.vcd"));
	TestTrue(TEXT("a different name clears the cache and the scene seam refuses, so it stays clear"),
		F.Npc->ExpressionScene.IsEmpty());
	TestFalse(TEXT("and no expression scene entity was spawned — the seam is the refusal"),
		F.Npc->ExpressionSceneEnt.IsSet());

	F.Npc->ExpressionScene = TEXT("Scenes/Talk.vcd");
	F.Npc->ExpressionSceneEnt = F.Other->Handle;
	F.Npc->ClearExpression();
	TestTrue(TEXT("ClearExpression writes the one word"), F.Npc->ExpressionScene.IsEmpty());
	TestTrue(TEXT("and deliberately leaves the scene entity standing — retail's 11 bytes"),
		F.Npc->ExpressionSceneEnt.IsSet());

	// 0x1025ea00 `ValidHeadTarget`, the facing/height cone. The dot gate is STRICT.
	F.Npc->Origin = FVector::ZeroVector;
	F.Npc->Angles = FVector::ZeroVector;
	TestTrue(TEXT("a point straight ahead at eye height passes both gates"),
		F.Npc->ValidHeadTargetBaseActor(F.Npc->EyePosition() + FVector(100.0, 0.0, 0.0)));
	TestFalse(TEXT("a point directly behind fails the dot gate"),
		F.Npc->ValidHeadTargetBaseActor(F.Npc->EyePosition() + FVector(-100.0, 0.0, 0.0)));
	TestFalse(TEXT("a point ahead but far above fails the height gate"),
		F.Npc->ValidHeadTargetBaseActor(F.Npc->EyePosition() + FVector(100.0, 0.0, 10000.0)));

	return true;
}

// -------------------------------------------------------------------------------------------------
// `RememberUnreachable` (`0x10274080`) — the write side of family Positions' list.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBaseHelpersRememberUnreachableTest,
	"Elysium.Substrate.NpcKernelBaseHelpers.RememberUnreachable",
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
	"Elysium.Substrate.NpcKernelBaseHelpers.GeometryAndClocks",
	GElysiumNpcKernelBaseHelpersFlags)
bool FElysiumNpcKernelBaseHelpersGeometryTest::RunTest(const FString&)
{
	FBaseHelpersFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Npc))
	{
		return false;
	}

	// Slot 515 `CalcIdealYaw` (`0x10274b30`). The navigator's state word is family Motor's seam and
	// answers -1, so the DEFAULT arm is the one every call takes: `VecToYaw(target - GetOrigin())`.
	TestEqual(TEXT("the navigator seam answers -1, so the default delta form runs"),
		F.Npc->NavGoalState(), INDEX_NONE);
	F.Npc->Origin = FVector::ZeroVector;
	TestEqual(TEXT("+X is yaw 0"), F.Npc->CalcIdealYaw(FVector(100.0, 0.0, 0.0)), 0.f, 1.0e-3f);
	TestEqual(TEXT("+Y is yaw 90"), F.Npc->CalcIdealYaw(FVector(0.0, 100.0, 0.0)), 90.f, 1.0e-3f);
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
	"Elysium.Substrate.NpcKernelBaseHelpers.FaceAnimLadder", GElysiumNpcKernelBaseHelpersFlags)
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

	// Rung 2 reads `_DAT_104704b4`, which is UNRECOVERED and stands at 0.0 — so the rung fires for
	// every delta at or below zero. The rung's SHAPE (a `<=` against a negative band edge) is
	// exact; the literal is the one thing waiting.
	Pick = FElysiumNpc::FaceAnimLadder(-10.f, Any);
	TestEqual(TEXT("a negative delta inside 140 picks 0x10fd"), Pick.Activity, 0x10fd);
	TestEqual(TEXT("and records face anim 6"), Pick.FaceAnim, 6);

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
		F.Npc->MotorIdealYawDelta = 20.f;
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
// The hint validators — `0x10295ed0`, `0x102961a0`, `0x10296c40`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBaseHelpersHintValidatorsTest,
	"Elysium.Substrate.NpcKernelBaseHelpers.HintValidators", GElysiumNpcKernelBaseHelpersFlags)
bool FElysiumNpcKernelBaseHelpersHintValidatorsTest::RunTest(const FString&)
{
	FBaseHelpersFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Npc))
	{
		return false;
	}
	using EReason = FElysiumNpc::EHintRejectReason;
	const FElysiumNpc::FHintWords Base = MakeHint();

	// --- 0x10295ed0's rule, the quiet twin -------------------------------------------------------
	TestTrue(TEXT("a cover object 50 units along the hint's facing is valid"),
		F.Npc->CoverHintStillValid(Base, AtUnits(50.0), FVector::ZeroVector, true));
	TestFalse(TEXT("5 units is under m_flTargetDistMin"),
		F.Npc->CoverHintStillValid(Base, AtUnits(5.0), FVector::ZeroVector, false));
	TestFalse(TEXT("200 units is over m_flTargetDistMax"),
		F.Npc->CoverHintStillValid(Base, AtUnits(200.0), FVector::ZeroVector, false));
	// THE CURRENT HINT gets a 64-unit tolerance on BOTH ends (`_DAT_10451acc`), which is the one
	// thing that distinguishes the two band policies.
	TestTrue(TEXT("but 5 units passes for the hint I am already standing on"),
		F.Npc->CoverHintStillValid(Base, AtUnits(5.0), FVector::ZeroVector, true));
	TestTrue(TEXT("and so does 160"),
		F.Npc->CoverHintStillValid(Base, AtUnits(160.0), FVector::ZeroVector, true));
	TestFalse(TEXT("165 is past even the tolerance"),
		F.Npc->CoverHintStillValid(Base, AtUnits(165.0), FVector::ZeroVector, true));
	// The projection: the cover object must lie ahead of the hint's own facing.
	TestFalse(TEXT("a cover object behind the hint fails the dot floor"),
		F.Npc->CoverHintStillValid(Base, AtUnits(-50.0), FVector::ZeroVector, true));
	{
		FElysiumNpc::FHintWords Disabled = Base;
		Disabled.Disabled = 1;
		TestFalse(TEXT("a disabled hint is never valid"),
			F.Npc->CoverHintStillValid(Disabled, AtUnits(50.0), FVector::ZeroVector, true));
	}
	// A hint that is NOT the current one has two extra gates: a 512-unit proximity
	// (`_DAT_10483aac`) and, for hint type 0x283d, a forward-projection test.
	TestTrue(TEXT("a nearby other hint passes the 512-unit proximity gate"),
		F.Npc->CoverHintStillValid(Base, AtUnits(50.0), AtUnits(0.0, 10.0), false));
	TestFalse(TEXT("one 600 units away does not"),
		F.Npc->CoverHintStillValid(Base, AtUnits(50.0), AtUnits(0.0, 600.0), false));

	// --- 0x102961a0's rule, the verbose twin -----------------------------------------------------
	TestEqual(TEXT("the verbose twin accepts the same geometry"),
		F.Npc->CoverHintRejectReason(Base, AtUnits(50.0), true), EReason::None);
	TestEqual(TEXT("and answers one shared reason for both band policies"),
		F.Npc->CoverHintRejectReason(Base, AtUnits(200.0), false), EReason::DistanceOutOfBand);
	TestEqual(TEXT("and its own reason for the projection"),
		F.Npc->CoverHintRejectReason(Base, AtUnits(-50.0), true), EReason::OutsideGoodRange);
	{
		// The `target_name` gate, which `0x10295ed0` does NOT have. An empty name admits everyone.
		FElysiumNpc::FHintWords Named = Base;
		Named.TargetName = TEXT("someone_else");
		TestEqual(TEXT("a target_name naming another NPC is a mismatch"),
			F.Npc->CoverHintRejectReason(Named, AtUnits(50.0), true),
			EReason::TargetNameMismatch);
		Named.TargetName = F.Npc->TargetName.ToUpper();
		TestEqual(TEXT("and the comparison is case-insensitive"),
			F.Npc->CoverHintRejectReason(Named, AtUnits(50.0), true), EReason::None);
	}

	// --- 0x10296c40's rule, the attack-position validator ----------------------------------------
	//
	// THE GEOMETRY EVERY CASE BELOW STANDS IN. The hint is at the origin facing +X and the enemy is
	// somewhere along +X; `MeCm` puts the NPC BEHIND the hint, on the far side from the enemy.
	// That position is not decoration — `0x10296c40`'s projection gate is
	// `dot( normalize2D(me - enemy), normalize2D(hint - enemy) ) >= _DAT_10451ab4`, and retail's
	// literal is in its own message, `"Projection (%.2f) < 0.2"`. An NPC standing ON its enemy
	// makes `me - enemy` the zero vector, whose normalise leaves it zero in retail's
	// `VectorNormalize` exactly as it does in the port's, so the dot is 0 and the gate rejects
	// before any band is ever reached. Behind the hint the dot is 1 and the bands decide, which is
	// what these cases are about.
	const float Good = 0.5f;
	const float Bad = 2.0f;
	const FVector MeCm = AtUnits(-10.0);
	F.Npc->bStayEntrenched = false;
	TestEqual(TEXT("an enemy 50 units along the hint's facing is a valid attack position"),
		F.Npc->AttackHintRejectReason(Base, AtUnits(50.0), MeCm, false, true, Good, Bad),
		EReason::None);
	TestEqual(TEXT("no active weapon refuses first"),
		F.Npc->AttackHintRejectReason(Base, AtUnits(50.0), MeCm, false, false, Good, Bad),
		EReason::NoActiveWeapon);
	TestEqual(TEXT("under m_flTargetDistMin answers its own reason"),
		F.Npc->AttackHintRejectReason(Base, AtUnits(5.0), MeCm, false, true, Good, Bad),
		EReason::DistanceBelowMin);
	TestEqual(TEXT("over m_flTargetDistMax answers the other"),
		F.Npc->AttackHintRejectReason(Base, AtUnits(200.0), MeCm, false, true, Good, Bad),
		EReason::DistanceAboveMax);
	// `m_bStayEntrenched` SKIPS the upper bound entirely — and, on my own hint, the whole body.
	F.Npc->bStayEntrenched = true;
	TestEqual(TEXT("m_bStayEntrenched drops the upper bound"),
		F.Npc->AttackHintRejectReason(Base, AtUnits(200.0), MeCm, false, true, Good, Bad),
		EReason::None);
	TestEqual(TEXT("and on my own hint it accepts before any test at all"),
		F.Npc->AttackHintRejectReason(Base, AtUnits(5.0), MeCm, true, false, Good, Bad),
		EReason::None);
	F.Npc->bStayEntrenched = false;
	// The projection gate itself, both ways round, and only for a hint that is not already mine.
	TestEqual(TEXT("standing on the far side of the enemy from the hint fails the projection"),
		F.Npc->AttackHintRejectReason(Base, AtUnits(50.0), AtUnits(150.0), false, true, Good, Bad),
		EReason::Projection);
	TestEqual(TEXT("and so does standing exactly on it — a zero direction dots to zero"),
		F.Npc->AttackHintRejectReason(Base, AtUnits(50.0), AtUnits(50.0), false, true, Good, Bad),
		EReason::Projection);
	TestEqual(TEXT("but the current hint skips the projection entirely"),
		F.Npc->AttackHintRejectReason(Base, AtUnits(50.0), AtUnits(150.0), true, true, Good, Bad),
		EReason::None);
	// The two band gates are NOT symmetric: good range is `<=` and bad range is `>=`. At 50 units
	// the facing projection is exactly 1.0, so each bound set to 1.0 lands on its own boundary.
	TestEqual(TEXT("a facing dot at exactly the good-range floor is OUTSIDE it"),
		F.Npc->AttackHintRejectReason(Base, AtUnits(50.0), MeCm, false, true, 1.0f, Bad),
		EReason::OutsideGoodRange);
	TestEqual(TEXT("and one at exactly the bad-range ceiling is INSIDE it"),
		F.Npc->AttackHintRejectReason(Base, AtUnits(50.0), MeCm, false, true, Good, 1.0f),
		EReason::InsideBadRange);

	// The seams every entry point depends on, asked and refusing.
	FElysiumNpc::FHintWords Resolved;
	TestFalse(TEXT("the hint store answers nothing"), F.Npc->HintWords(0, Resolved));
	float RangeUnits = -1.f;
	TestFalse(TEXT("and no weapon carries a maximum range"),
		F.Npc->ActiveWeaponMaxRangeUnits(RangeUnits));
	TestFalse(TEXT("so 0x10295ed0's entry point answers false for every node"),
		F.Npc->FUN_10295ed0(0));
	TestEqual(TEXT("and 0x102961a0's answers NoHint"), F.Npc->FUN_102961a0(0), EReason::NoHint);
	TestFalse(TEXT("no NPC is the ai_debug_npc, so no reason string is ever formatted"),
		F.Npc->IsHintDebugNpc());

	return true;
}

// -------------------------------------------------------------------------------------------------
// The victim-side reaction slots and the remaining slot bodies.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBaseHelpersReactionSlotsTest,
	"Elysium.Substrate.NpcKernelBaseHelpers.ReactionSlots", GElysiumNpcKernelBaseHelpersFlags)
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
	F.Npc->Slot23(F.Other);
	TestTrue(TEXT("slot 23 is byte-identical to slot 22"),
		F.Npc->Cognition.Conditions.Has(EElysiumNpcCond::BeingAttacked));
	TestEqual(TEXT("counter still untouched"), F.Npc->HitBuildupCount, 1);

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

	// 0x1027e0f0 — the BASE line's slot-532 body. `Slot532(int32)` is the TROIKA override and is
	// story 29d's, so this is its own method and it reports handled.
	F.Npc->OpeningDoor = F.Other->Handle;
	F.Npc->bOpeningDoorWait = true;
	TestTrue(TEXT("the base slot-532 body reports handled"), F.Npc->FUN_1027e0f0());
	TestFalse(TEXT("and forgets the door being opened (+0x5d24)"), F.Npc->OpeningDoor.IsSet());
	TestFalse(TEXT("and the wait flag (+0x5d30)"), F.Npc->bOpeningDoorWait);

	// Slot 571 (`0x10289ce0`): ACT_WALK below the distance constant, ACT_RUN at or beyond it.
	// `_DAT_1049a17c` is UNRECOVERED and stands at zero, so every non-negative distance runs.
	TestEqual(TEXT("slot 571 answers ACT_RUN at or beyond the distance constant"),
		F.Npc->Slot571(100.f), 0x13);
	TestEqual(TEXT("and ACT_WALK below it"), F.Npc->Slot571(-1.f), 9);

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
	TestFalse(TEXT("and neither is one whose hint the seam cannot find"),
		F.Npc->IsUnusableNode(F.Npc));
	TestTrue(TEXT("because an absent hint is free"), F.Npc->IsHintAvailableToMe(0));

	// Slot 497 (`0x102947e0`): the once-only latch is the observable half. A second call does
	// nothing, which is `DAT_109249c4 & 1`.
	F.Npc->Slot497();
	F.Npc->Slot497();
	TestTrue(TEXT("slot 497's once-latch tolerates a second call"), true);

	// Slot 19 (`0x1028dfb0`): a null message does nothing at all; a message goes to the log,
	// because the verbose-trace toggle `DAT_10920534` reads clear here.
	F.Npc->TraceMessageBare(nullptr);
	F.Npc->TraceMessageBare(TEXT("trace"));
	TestTrue(TEXT("TraceMessageBare writes no member either way"), true);

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
	"Elysium.Substrate.NpcKernelBaseHelpers.PatrolInterestDraw",
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
	TestEqual(TEXT("because the record seam answers nothing"),
		F.Npc->PatrolNodeInterestRecord(3), INDEX_NONE);
	TestEqual(TEXT("and the ip_percent it would have rolled against is zero"),
		F.Npc->PatrolNodeInterestPercent(0), 0);

	// The read side answers nothing unless the flag stands, and caches at +0x659c when it does.
	F.Npc->ScheduleHost.bPatrolPathUseHint = false;
	F.Npc->ScheduleHost.Unknown659c = 42u;
	TestEqual(TEXT("with the flag clear the cached record is not even consulted"),
		F.Npc->FUN_1029f730(3), 0);
	F.Npc->ScheduleHost.bPatrolPathUseHint = true;
	TestEqual(TEXT("with it set the cache is returned as it stands"), F.Npc->FUN_1029f730(3), 42);
	F.Npc->ScheduleHost.Unknown659c = 0u;
	TestEqual(TEXT("an empty cache re-resolves, and the seam still answers nothing"),
		F.Npc->FUN_1029f730(3), 0);

	// 0x1029f610 — the navigator path probe, which has no path to ask.
	TestFalse(TEXT("a null goal entity answers false"), F.Npc->FUN_1029f610(nullptr));
	TestFalse(TEXT("and so does a live one, because the navigator keeps no readable path"),
		F.Npc->FUN_1029f610(F.Other));

	return true;
}

// -------------------------------------------------------------------------------------------------
// The three rows the port ALREADY carried — asserted so the claim is checkable.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBaseHelpersAlreadyCarriedTest,
	"Elysium.Substrate.NpcKernelBaseHelpers.AlreadyCarriedRows",
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
