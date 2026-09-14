#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "ElysiumAnimationIntent.h"
#include "Player/ElysiumCameraShots.h"
#include "Substrate/ElysiumReactions.h"
#include "Visual/ElysiumActionTables.h"

// Story 29d, families **Anim10** and **SpeciesAnim10** — activity, sequence, pose and model.
// `ElysiumNpcKernelAnim10_2.cpp` carries `MaintainEyeDirection`, the three melee selectors and the
// zombie idle gate. The walked prose is `docs/vtmb/npc-ai/shape.md`.
//
// Every body below is retail's, arm by arm, in retail's order, with the `0x10……` address on the line
// that carries it. The standing fact of this family: **an activity id is state, not a picture.** The
// only thing here that is "visual" is which clip an id eventually resolves to, and this family does
// not touch that at all — it lands the ids, the commit order, the two edges that fire listeners and
// the four words the pose arm writes.

namespace
{
	// Unit-prefixed: the module builds adaptive-unity and anonymous namespaces are merged.

	// --- The vtable slots this family fills and dispatches on -------------------------------------
	constexpr int32 GAnim10SlotSetModel = 105;
	constexpr int32 GAnim10SlotSetActivity = 310;
	constexpr int32 GAnim10SlotUpdatePose = 314;
	constexpr int32 GAnim10SlotEarlyTranslate = 375;
	constexpr int32 GAnim10SlotExpresserSpeak = 584;
	constexpr int32 GAnim10SlotMeleeCombat = 604;
	constexpr int32 GAnim10SlotShouldPlayIdleSound = 509;

	// --- The retail `Activity` numbers these bodies name ------------------------------------------
	//
	// Spelled once, as every other family in this band spells them: the port stands no retail
	// activity table (family Hints' `RestartIdealActivityId` records why).
	constexpr int32 GAnim10ActReset = 0;            // ACT_RESET
	constexpr int32 GAnim10ActIdle = 1;             // ACT_IDLE
	constexpr int32 GAnim10ActTransition = 2;       // ACT_TRANSITION — `SetActivity`'s second refusal
	constexpr int32 GAnim10ActFidget = 3;           // ACT_FIDGET
	constexpr int32 GAnim10ActAim = 5;              // the armed-idle the human body rewrites 1 into
	constexpr int32 GAnim10ActCover = 6;            // ACT_COVER
	constexpr int32 GAnim10ActWalk = 9;             // ACT_WALK
	constexpr int32 GAnim10ActRun = 0x13;           // ACT_RUN
	constexpr int32 GAnim10ActWalkAim = 0x11;       // ACT_WALK_AIM
	constexpr int32 GAnim10ActRunAim = 0x15;        // ACT_RUN_AIM
	constexpr int32 GAnim10ActWalkRelaxed = 0x16;   // the aggressive-clear rewrite of ACT_WALK
	constexpr int32 GAnim10ActRunRelaxed = 0x17;    // ... and of ACT_RUN
	constexpr int32 GAnim10ActReloadFast = 0x55;    // ACT_RELOAD_FAST
	constexpr int32 GAnim10ActDisposition = 0xf1;   // ACT_DISPOSITION
	constexpr int32 GAnim10ActRunFrenzy = 0xf17;    // ACT_RUN_FRENZY
	constexpr int32 GAnim10ActHuntWalk = 0x1115;    // ACT_HUNT_WALK
	constexpr int32 GAnim10ActCombatMove = 0x1121;  // ACT_COMBATMOVE

	// The six aggressive-set rewrites of `0x103854f0`, as pairs.
	constexpr int32 GAnim10AggressivePairs[][2] = {
		{ 0x3b, 0x3d }, { 0x3c, 0x3e }, { 0x9d, 0x9f },
		{ 0x9e, 0xa0 }, { 0xa1, 0xa3 }, { 0xa2, 0xa4 },
	};

	// The Troika `SetActivity` families. `0x1093`..`0x1096` is the idle-fidget set and
	// `0x1115`..`0x1117` the hunt-walk set; the request that ENTERS each arm is the family's first
	// member, which is why the two bounds tests read "my current activity is already in the family".
	constexpr int32 GAnim10ActFidgetSetFirst = 0x1093;
	constexpr int32 GAnim10ActFidgetSetLast = 0x1096;
	constexpr int32 GAnim10ActHuntSetFirst = 0x1115;
	constexpr int32 GAnim10ActHuntSetLast = 0x1117;

	// The Hengeyokai's two carry activities and the Tzimisce's four body-carry variants.
	constexpr int32 GAnim10ActPickupLightIdle = 0x128;   // 296
	constexpr int32 GAnim10ActPickupLightCarry = 0x129;  // 297
	constexpr int32 GAnim10ActIdleBody = 0xfc;           // 252, `m_bHeavyBodyTarget` NON-zero
	constexpr int32 GAnim10ActIdleBodyL = 0xfd;          // 253, `m_bHeavyBodyTarget` ZERO
	constexpr int32 GAnim10ActWalkBody = 0xfe;           // 254
	constexpr int32 GAnim10ActWalkBodyL = 0xff;          // 255

	// The Tzimisce runner's four variants, and the head claw's one.
	constexpr int32 GAnim10ActTzIdle2 = 0x1134;    // 4404
	constexpr int32 GAnim10ActTzFidget2 = 0x1135;  // 4405
	constexpr int32 GAnim10ActTzWalk2 = 0x1136;    // 4406
	constexpr int32 GAnim10ActTzRun2 = 0x1137;     // 4407

	// --- The bit masks --------------------------------------------------------------------------
	//
	// `m_bfNPCFrenziedFlags` (+0x5b84). `0x40` is the frenzy gait and `0x20` the hurried one;
	// `0x200` is the bit the human body treats as "aggressive, whatever the state says".
	constexpr uint32 GAnim10FrenziedRunFrenzy = 0x40;
	constexpr uint32 GAnim10FrenziedRunGait = 0x20;
	constexpr uint32 GAnim10FrenziedForceAggressive = 0x200;

	// `CapabilitiesGet()` (slot 513). `0x8000000` gates BOTH Troika delegates and `0x40` is the
	// human body's no-aim-gait bit.
	constexpr int32 GAnim10CapCoverAndReload = 0x8000000;
	constexpr int32 GAnim10CapNoAimGait = 0x40;

	// `m_afMemory` (+0x5d8c). Bit 1 (`0x2`) turns the Troika body's `ACT_IDLE` into a cover
	// delegate; bit 27 (`0x8000000`) is the human body's own aggressive override.
	constexpr uint32 GAnim10MemoryCoverIdle = 0x2;
	constexpr uint32 GAnim10MemoryAggressive = 0x8000000;

	// The active weapon's `+0x19c` NODRAW bit, which makes the human body treat an armed NPC as
	// unarmed, and its slot-360 (`+0x5a0`) ranged-aim bits.
	constexpr uint32 GAnim10WeaponNoDraw = 0x40;
	constexpr uint32 GAnim10WeaponRangedAim = 0x6000;

	// `m_bfAINPCFlags` (+0x14b8) `0x20 CARRYING_BODY` — the form bit BOTH `0x10381c80` and
	// `0x103be130` probe, and `0x10000 FORCE_RELAXED_ANIMS`.
	// `m_bfAINPCFlags2` (+0x14bc) `0x400 MOVE_FACE_ENEMY` and `0x80000 D_MILDLY_CRAZY`.

	// --- The ConVar globals, by address -----------------------------------------------------------
	//
	// Read through family **Debug10**'s `DebugConVar`, which is the kernel's ONE ConVar seam: it keys
	// on the retail global's SPELLING and answers the `+0x2c` int, which is exactly the word all five
	// of these arms read. Reused rather than stood in parallel, per the story's seam rule; the seam's
	// name says "debug" because that family stood it, not because the set is debug-only.
	constexpr TCHAR GAnim10CvGait[] = TEXT("DAT_10924d6c");        // 0x10295590's gait override
	constexpr TCHAR GAnim10CvAggressive[] = TEXT("DAT_10924f74");  // 0x103854f0's combat-aggression
	constexpr TCHAR GAnim10CvAlert[] = TEXT("DAT_10923f5c");       // ... for m_NPCState 3
	constexpr TCHAR GAnim10CvHunt[] = TEXT("DAT_10924034");        // ... for m_NPCState 0xb
	constexpr TCHAR GAnim10CvHitBuildup[] = TEXT("DAT_109245e4");  // slot 326's buildup ceiling

	// The two FLOAT ConVars `MaintainEyeDirection` draws its cycler-actor duration between. They read
	// `+0x28`, not `+0x2c`, so they take this family's own float seam.
	TMap<FString, float> GAnim10FloatConVars;

	// The gait override's two live values.
	constexpr int32 GAnim10GaitForceRun = 1;
	constexpr int32 GAnim10GaitForceWalk = 2;

	// --- The census addresses the slot methods dispatch on ----------------------------------------
	constexpr TCHAR GAnim10Body_TroikaSetModel[] = TEXT("0x10298ce0");
	constexpr TCHAR GAnim10Body_BaseHumanoidSetModel[] = TEXT("0x1025e510");
	constexpr TCHAR GAnim10Body_GhoulCroucherSetModel[] = TEXT("0x1037b1f0");
	constexpr TCHAR GAnim10Body_ZombieSetModel[] = TEXT("0x103e0540");
	constexpr TCHAR GAnim10Body_HeadClawSetActivity[] = TEXT("0x103c1cd0");
	constexpr TCHAR GAnim10Body_RunnerSetActivity[] = TEXT("0x103c3d80");
	constexpr TCHAR GAnim10Body_DogTranslate[] = TEXT("0x10374ad0");
	constexpr TCHAR GAnim10Body_HengeyokaiTranslate[] = TEXT("0x10381b50");
	constexpr TCHAR GAnim10Body_HumanTranslate[] = TEXT("0x103854f0");
	constexpr TCHAR GAnim10Body_TzimisceTranslate[] = TEXT("0x103bde40");
	constexpr TCHAR GAnim10Body_RunnerTranslate[] = TEXT("0x103c3e10");
	constexpr TCHAR GAnim10Body_ExpresserSpeak[] = TEXT("0x10260dc0");

	// --- The two vocalization-group literals `0x1037b1f0` and `0x103e0540` write ------------------
	//
	// `-(uint)(s[0] != '\0') & <addr>` is retail's "a null `string_t` for an empty literal" idiom;
	// neither literal is empty, so both always store the pointer.
	constexpr TCHAR GAnim10ZombieMale[] = TEXT("Zombie_Male");      // 0x1063b150
	constexpr TCHAR GAnim10ZombieFemale[] = TEXT("Zombie_Female");  // 0x1063b140
	constexpr int32 GAnim10ZombieVSoundTableIndex = 2;              // the literal written to +0x00bc

	// --- The 26 pose-parameter names `0x1025e510` caches, in retail's push order ------------------
	//
	// 0..12 go through `CBaseAnimating::LookupPoseParameter` (family Anim's seam, answering -1) and
	// 13..25 through `thunk_FUN_100b5d10`, which is `LookupFlexController` — family Anim ported that
	// body and recorded that it answers **0** on a miss, not -1.
	const TCHAR* const GAnim10PoseParamNames[] = {
		TEXT("body_trans_Y"),      // +0x5fb8, 0x105c8ec8
		TEXT("body_trans_X"),      // +0x5fbc, 0x105c8eb8
		TEXT("body_lift"),         // +0x5fc0, 0x105c8eac
		TEXT("body_yaw"),          // +0x5fc4, 0x105c8ea0
		TEXT("body_pitch"),        // +0x5fc8, 0x10589114
		TEXT("body_roll"),         // +0x5fcc, 0x105c8e94
		TEXT("spine_yaw"),         // +0x5fd0, 0x105c8e88
		TEXT("spine_pitch"),       // +0x5fd4, 0x105c8e78
		TEXT("spine_roll"),        // +0x5fd8, 0x105c8e68
		TEXT("neck_trans"),        // +0x5fdc, 0x105c8e58
		TEXT("head_yaw"),          // +0x5fe0, 0x105c8e4c — index 10, the first of 0x1025efc0's three
		TEXT("head_pitch"),        // +0x5fe4, 0x105c8e3c
		TEXT("head_roll"),         // +0x5fe8, 0x105c8e30 — index 12, the last
		TEXT("move_rightleft"),    // +0x5fec, 0x105c8e1c — the LookupFlexController half begins
		TEXT("move_forwardback"),  // +0x5ff0, 0x105c8e08
		TEXT("move_updown"),       // +0x5ff4, 0x105c8df8
		TEXT("body_rightleft"),    // +0x5ff8, 0x105c8de4
		TEXT("body_updown"),       // +0x5ffc, 0x105c8dd4
		TEXT("body_tilt"),         // +0x6000, 0x105c8dc8
		TEXT("chest_rightleft"),   // +0x6004, 0x105c8db4
		TEXT("chest_updown"),      // +0x6008, 0x105c8da4
		TEXT("chest_tilt"),        // +0x600c, 0x105c8d94
		TEXT("head_forwardback"),  // +0x6010, 0x105c8d80
		TEXT("head_rightleft"),    // +0x6014, 0x105c8d6c
		TEXT("head_updown"),       // +0x6018, 0x105c8d5c
		TEXT("head_tilt"),         // +0x601c, 0x105c8d50
	};
	static_assert(UE_ARRAY_COUNT(GAnim10PoseParamNames) == 26,
		"0x1025e510 caches exactly 26 pose parameters into +0x5fb8..+0x601c");
	// The split point: below it `LookupPoseParameter`, at or above it `LookupFlexController`.
	constexpr int32 GAnim10PoseParamFlexFirst = 13;

	// `_DAT_104629b8` — the scale `SetDefaultEyeOffset`'s fallback applies to `mins + maxs`.
	// **UNRECOVERED**: no reader outside `0x10274ca0` and the corpus does not hold the cell. 0.5 is
	// the SDK's own midpoint of a bounding box and is what makes the fallback an eye at the centre;
	// the arm is the recovered part, the number is not.
	constexpr double GAnim10EyeOffsetFallbackScale = 0.5;

	// `_DAT_1049ae9c` — the pitch bias `UpdatePoseParameters` adds before the clamp. **RECOVERED
	// 2026-09-14** out of the pinned image's `.rdata` as a float: **7.5**. Story 29d's checklist left
	// it unnamed.
	constexpr float GAnim10AimPitchBias = 7.5f;
	// `_DAT_1049a180` = -45.0f and `_DAT_1049949c` = 45.0f — the clamp pair, recovered by family
	// Facing for the turn ladder and read at the same two addresses here.
	constexpr float GAnim10AimClampLow = -45.0f;
	constexpr float GAnim10AimClampHigh = 45.0f;
	// `_DAT_104454c4` = 0.0f, the value the no-aim arm writes into `+0x1064`.
	constexpr float GAnim10Zero = 0.0f;

	// The Auspex aura indices `0x1029e750` answers, with the retail `m_NPCState` that reaches each.
	constexpr int32 GAnim10AuraNone = -1;
	constexpr int32 GAnim10AuraCalm = 0;
	constexpr int32 GAnim10AuraCombat = 1;
	constexpr int32 GAnim10AuraHostile = 2;
	constexpr int32 GAnim10AuraMildlyCrazy = 3;
	constexpr int32 GAnim10AuraFrenzied = 4;
	constexpr int32 GAnim10AuraNeutral = 5;
	constexpr int32 GAnim10AuraAlert = 7;
	constexpr int32 GAnim10DispositionHate = 1;   // D_HT, the slot-404 answer the default arm wants
	constexpr uint32 GAnim10Flags2MildlyCrazy = 0x80000;   // EElysiumNpcFlag2::D_MILDLY_CRAZY

	// `+0xba == 2`, the swing record's unconditional-knockback marker. `ElysiumReactions` already
	// names it; read from there so the two cannot drift.

	// This family's random draws all come off the NPC schedule stream — the same one story 29c-1's
	// melee height-diff timer and the disposition stance rolls use. `SetActivity`'s pick and
	// `MaintainEyeDirection`'s blink re-arm are both per-NPC selection draws of exactly that kind.
	FRandomStream& Anim10Rng()
	{
		return ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
	}

	// `VectorAngles` `0x10139970`: Source `[pitch yaw roll]` for a direction in THIS world's axes,
	// whose Y is the negation of Source's. Family Facing spells the same conversion for the turn
	// ladder (`ElysiumNpcKernelFacing.cpp`); it is a file-static there and cannot be called, so the
	// fifteen pure lines are repeated with the same citation rather than a second reading being made.
	FVector Anim10RetailVectorAngles(const FVector& PortDir)
	{
		const double Y = -PortDir.Y;
		if (PortDir.X == 0.0 && Y == 0.0)
		{
			return FVector(PortDir.Z > 0.0 ? 270.0 : 90.0, 0.0, 0.0);
		}
		float Yaw = FMath::RadiansToDegrees(static_cast<float>(FMath::Atan2(Y, PortDir.X)));
		if (Yaw < 0.0f)
		{
			Yaw += 360.0f;
		}
		const double Flat = FMath::Sqrt(PortDir.X * PortDir.X + Y * Y);
		float Pitch = FMath::RadiansToDegrees(static_cast<float>(FMath::Atan2(-PortDir.Z, Flat)));
		if (Pitch < 0.0f)
		{
			Pitch += 360.0f;
		}
		return FVector(Pitch, Yaw, 0.0);
	}

	// `*(char *)(param_1 + 0xba) == '\x02'` — slot 326's unconditional-knockback marker, read off the
	// SWING record the slot's first argument points at. `FElysiumSwingRecord::Ba` is that byte and
	// `ElysiumReactions::KnockbackUnconditionalMarker` is the 2.
	bool Anim10SwingRecordIsUnconditional(const void* SwingRecord)
	{
		const FElysiumSwingRecord* Record = static_cast<const FElysiumSwingRecord*>(SwingRecord);
		return Record != nullptr && Record->Ba == ElysiumReactions::KnockbackUnconditionalMarker;
	}

	// Retail's `m_NPCState` ordinals, the same table families Anim, Conditions and Sounds carry.
	// **The retail enum is wider than this runtime's vocabulary** — 5, 6, 8, 9, 0xa, 0xb, 0xc, 0xd
	// and 0xe have no `EElysiumNpcState` member — so the arms of `0x1029e750` that name them are
	// ported and UNREACHABLE. Named at the body.
	int32 Anim10RetailNpcState(EElysiumNpcState State)
	{
		switch (State)
		{
		case EElysiumNpcState::Idle:     return 1;
		case EElysiumNpcState::Combat:   return 2;
		case EElysiumNpcState::Alert:    return 3;
		case EElysiumNpcState::Scripted: return 4;
		case EElysiumNpcState::Prone:    return 6;
		case EElysiumNpcState::Dead:     return 7;
		default:                         return 0;
		}
	}
}

// =================================================================================================
// The seams.
// =================================================================================================

float FElysiumNpc::Anim10FloatConVar(const TCHAR* RetailGlobal)
{
	// Retail: `cv->vtable[4]()` must answer 0 (the object is a ConVar, not a ConCommand) and the
	// FLOAT at `cv + 0x28` is the value. Neither name nor shipped default of `DAT_1090fc0c` or
	// `DAT_1090fc9c` is anywhere in the corpus, so both answer 0.0 — which is retail's own
	// `IsCommand()` arm — and a test sets what it wants.
	if (RetailGlobal == nullptr)
	{
		return 0.f;
	}
	const float* Value = GAnim10FloatConVars.Find(FString(RetailGlobal));
	return Value != nullptr ? *Value : 0.f;
}

void FElysiumNpc::SetAnim10FloatConVar(const TCHAR* RetailGlobal, float Value)
{
	if (RetailGlobal == nullptr)
	{
		GAnim10FloatConVars.Reset();
		return;
	}
	GAnim10FloatConVars.Add(FString(RetailGlobal), Value);
}

const TCHAR* FElysiumNpc::HumanoidPoseParamName(int32 Index)
{
	if (Index < 0 || Index >= NumHumanoidPoseParams)
	{
		return TEXT("");
	}
	return GAnim10PoseParamNames[Index];
}

void* FElysiumNpc::CurrentHintPointer()
{
	// `m_pHintNode` (`+0x5ddc`). Story 29c-1's slots 569/570 read the node off
	// `ScheduleHost.HintNode` and use their `void*` only as a non-null marker, so this hands them
	// exactly the null/non-null retail hands them.
	return ScheduleHost.HintNode != INDEX_NONE ? static_cast<void*>(&ScheduleHost) : nullptr;
}

void FElysiumNpc::ResolveStanceTableRow()
{
	// `thunk_FUN_100ec640(this)` — the disposition stance-table ROW INDEX. Family Precache10 already
	// reaches the same resolver from slot 104 through `EnsureStanceResolved()`; calling it here is
	// what keeps slot 104 and slot 105 writing `+0x64e8` the same way.
	EnsureStanceResolved();
}

int32 FElysiumNpc::FindTransitionSequence(int32 From, int32 To) const
{
	// `CBaseAnimating::FindTransitionSequence(from, to, &dir)`. SEAM: family Anim recorded that this
	// substrate stands no studio header and no sequence index at all, so there is no transition
	// graph to walk.
	//
	// **It answers `To`, the DESTINATION, and that is retail's own no-transition answer**, not a
	// convenience: the SDK body walks the studio transition table and returns the destination
	// sequence unchanged when no transition clip stands between the two. That is the arm
	// `AdvanceToIdealActivity` reads as `transition == m_nIdealSequence`, which commits the ideal
	// through `SetActivityAndSequence` DIRECTLY.
	//
	// Answering the `-1` sentinel instead would have been wrong twice over. It is retail's
	// "invalid sequence" value, not its "no transition" one; and it takes the arm that dispatches
	// slot 310 with the ideal activity — which `CAI_BaseNPC::SetActivity` then REFUSES, because
	// `m_Activity` is 2 `ACT_TRANSITION` on every path that reaches this body. A body would be
	// stranded in the transition activity for good. The `-1` arm is ported above and is what a
	// studio header with a genuinely invalid sequence pair would reach.
	(void)From;
	return To;
}

void FElysiumNpc::WeaponSetActivity(int32 Activity, float Duration)
{
	// `CBaseCombatCharacter::Weapon_SetActivity(activity, duration)`. SEAM: the weapon's activity
	// lives on the item's own clip set here and no per-weapon activity word stands on the NPC.
	FWeaponActivityRequest Request;
	Request.Activity = Activity;
	Request.Duration = Duration;
	WeaponActivityRequests.Add(Request);
}

void FElysiumNpc::SetViewOffset(const FVector& OffsetUnits)
{
	// `thunk_FUN_1009f380(this, &offset)` — `CBaseEntity::SetViewOffset`. SEAM: no writable
	// `m_vecViewOffset` word; the write is recorded so the per-commit dispatch of slot 533 is
	// assertable.
	ViewOffsetWrites.Add(OffsetUnits);
}

bool FElysiumNpc::AimPointFor(const FElysiumEntity* AimTarget, FVector& OutPointUnits) const
{
	// `thunk_FUN_10278650(this, &out, &eye, 0.0, <one>)` — the aim POINT slot 314 takes its angles
	// to. SEAM: the body-target arithmetic behind it is not stood here; retail's own answer for a
	// target with no authored body-target list is the target's centre, which is slot 192
	// `WorldSpaceCenter`, so that is what this answers.
	if (AimTarget == nullptr)
	{
		return false;
	}
	// slot 192 `WorldSpaceCenter` on a GENERIC entity: `ElysiumCameraShots::SurroundingBounds` is
	// the port's one bounds accessor, the same one `FElysiumNpc::WorldSpaceCenter` reads.
	OutPointUnits = ElysiumCameraShots::SurroundingBounds(*AimTarget).GetCenter();
	return true;
}

int32 FElysiumNpc::HitBuildupConVarValue()
{
	// `(**(code **)(*DAT_109245e4 + 4))()` then `DAT_109245e4[0xb]` (`+0x2c`), slot 326's buildup
	// ceiling. `FElysiumCombatCharacter::HitBuildupAdmitAtOrBelow` is the port's recovered value for
	// the SAME ConVar (`docs/vtmb/combat-and-damage.md` → "Who may be knocked back"); a test can
	// still drive the ConVar seam, and the seam wins when it carries a value.
	const int32 Seam = DebugConVar(GAnim10CvHitBuildup);
	return Seam != 0 ? Seam : FElysiumCombatCharacter::HitBuildupAdmitAtOrBelow;
}

uint32 FElysiumNpc::ActiveWeaponDrawFlags() const
{
	// The active weapon's `+0x19c`, whose bit `0x40 NODRAW` makes `0x103854f0` treat an armed body as
	// unarmed. SEAM: no port member carries the weapon's draw flags — the port's weapon state is the
	// item catalogue row, which has no such word — so this answers 0, the arm in which the weapon
	// DOES draw and the aggressive decision tree runs. Answering `0x40` would clear
	// `m_bAggressiveAnims` for every armed body and make the whole tree unreachable.
	return 0u;
}

bool FElysiumNpc::EntityUnselectable() const
{
	// SEAM for `CBaseEntity::EntityUnselectable(this)`, slot 359's first refusal. UNRECOVERED as a
	// port concept: nothing here marks an entity unselectable. FALSE is the arm that lets the rest of
	// the aura ladder run, so no aura is silently refused.
	return false;
}

void FElysiumNpc::PlayReaction(const FVector& DirectionUnits, int32 Kind)
{
	// SEAM for `thunk_FUN_10344f80(this, &direction, kind, 0)`. Its three REFUSAL gates are exactly
	// `ElysiumReactions::IsKnockbackAllowed`'s and are asked through that one rule rather than
	// restated: slot 400 `AllowsKnockbackBypass` first, then the template byte `+0x9e
	// Disallow_Knockbacks`, then `CVStatList_t::IsEqual(0x0f, 0x11)`, the dead test the port spells
	// as `!IsInert() && !HasReportedDeath()`.
	//
	// What has no callee here is everything past the gate — slot 323's direction bucket,
	// `LookupActivity`, `TranslateFlyingKnockbackActivity` (player-only) and slot 320 — so the
	// admitted request is recorded with its KIND, which is the one thing that separates slot 330's
	// two distance bands.
	const bool bAlive = !IsInert() && !HasReportedDeath();
	if (!ElysiumReactions::IsKnockbackAllowed(bAlive, DisallowsKnockbacks(), /*bBuildupAdmits*/ true,
			AllowsKnockbackBypass()))
	{
		return;
	}
	FNearMissReaction Reaction;
	Reaction.DirectionUnits = DirectionUnits;
	Reaction.Kind = Kind;
	NearMissReactions.Add(Reaction);
}

bool FElysiumNpc::NearMissBands(const FElysiumEntity* Weapon, float& OutNearUnits,
	float& OutFarUnits) const
{
	// SEAM for `thunk_FUN_102517e0(weapon)` — the `CVDmg_t` row (three rows of stride `0xec84`,
	// searched on the weapon's `+0x848`) whose `+0x404` / `+0x408` are slot 330's two bands.
	//
	// There is no admitting value to borrow: retail's own miss arm BUILDS a default row rather than
	// answering none, so "no row" is a state retail never reaches. This answers false and slot 330
	// takes its beyond-the-far-band arm, which does nothing at all. Named, not hidden.
	(void)Weapon;
	OutNearUnits = 0.f;
	OutFarUnits = 0.f;
	return false;
}

void FElysiumNpc::ExpressiveNpcSpeak(int32 ConceptId, const TCHAR* Modifier)
{
	// `FUN_10260dc0` (`CAI_BaseHumanoid#584`, `CAI_ExpressiveNPC#584`): `MOV ECX,[ECX+0x5f48]` then
	// `JMP 0x100116d0` — a tail jump through the Expresser pointer into `0x10311c10` with the concept
	// id and the modifier text. There is no pre- or post-work at all; the Troika body `0x1028d910`
	// (`ResetAllThinkStamps`, family Closure) has nothing to do with it.
	//
	// SEAM: family Lifecycle's `ExpressiveNpcExpresser()` answers null — this runtime stands no
	// expression substrate — and retail would fault on a null expresser. NAMED CRASH GUARD: the
	// request is recorded instead.
	FExpresserSpeak Speak;
	Speak.ConceptId = ConceptId;
	Speak.Modifier = Modifier != nullptr ? FString(Modifier) : FString();
	ExpresserSpeaks.Add(Speak);
	(void)ExpressiveNpcExpresser();
}

// =================================================================================================
// Slot 105 `SetModel` — one slot, four retail bodies.
// =================================================================================================

void FElysiumNpc::SetModel(TCHAR* ModelName)
{
	// The vtable, spelled as a table lookup: an override of slot 105 replaces this body outright,
	// and an arm that wants the Troika body calls `SetModel()` back under `FSpeciesDispatchScope`,
	// which is retail's non-virtual thunk.
	if (SetModelSpecies(ModelName))
	{
		return;
	}
	TroikaSetModel(ModelName);
}

namespace
{
	struct FAnim10SetModelArm
	{
		const TCHAR* Address = nullptr;
		const TCHAR* RetailClass = nullptr;
		void (FElysiumNpc::*Body)(TCHAR*) = nullptr;
	};
}

bool FElysiumNpc::SetModelSpecies(TCHAR* ModelName)
{
	static const FAnim10SetModelArm Arms[] =
	{
		// `CAI_BaseHumanoid#105`. UNREACHABLE: the class carries no entity classname in the census.
		{ GAnim10Body_BaseHumanoidSetModel, TEXT("CAI_BaseHumanoid"),
			&FElysiumNpc::BaseHumanoidSetModel },
		// The two vocalization-group arms share one 119-byte body, so they share one port method and
		// are told apart by the address handed to it.
		{ GAnim10Body_GhoulCroucherSetModel, TEXT("CNPC_VGhoulCroucher"), nullptr },
		{ GAnim10Body_ZombieSetModel, TEXT("CNPC_VZombie"), nullptr },
	};

	// Retail's non-virtual thunk: while slot 105's species body runs, slot 105's dispatcher answers
	// "no species body", so the arm's own `SetModel()` reaches the Troika body directly.
	if (SpeciesDispatchingSlot == GAnim10SlotSetModel)
	{
		return false;
	}
	const FElysiumNpcClassSlot* Override =
		ElysiumNpcKernelClass::OverrideOf(RetailClass(), GAnim10SlotSetModel);
	if (Override == nullptr)
	{
		// `RetailClass()` is null for a classname the census claims nothing for — a spawned
		// `npc_VCop`'s own recovered answer — and a class with no slot-105 row inherits the Troika
		// body, which is exactly what returning false runs.
		return false;
	}
	for (const FAnim10SetModelArm& Arm : Arms)
	{
		if (FCString::Strcmp(Arm.Address, Override->Address) != 0)
		{
			continue;
		}
		const FSpeciesDispatchScope Scope(*this, GAnim10SlotSetModel);
		if (Arm.Body != nullptr)
		{
			(this->*Arm.Body)(ModelName);
		}
		else
		{
			ZombieLineSetModel(ModelName, Arm.Address);
		}
		return true;
	}
	// Unreachable: `Elysium.Substrate.NpcKernelAnim10.SetModelArmCoverage` asserts the table carries
	// every slot-105 override row the census holds.
	return false;
}

void FElysiumNpc::TroikaSetModel(TCHAR* ModelName)
{
	// `CAI_BaseNPCTroika::SetModel` `0x10298ce0`, 50 bytes. FOUR calls, and the ORDER is the body:
	// each reads what the one before it wrote. The model must be set before the hull and the hull
	// before the eye.

	// 1. `CBaseCombatCharacter::SetModel(name)`. `FElysiumEntity::SetRuntimeModel` is that write —
	//    it mutates the authoritative field AND runs the body-follow hook, which is what retail's
	//    own `SetModel` does through `SetModelIndex`.
	SetRuntimeModel(ModelName != nullptr ? FString(ModelName) : FString());

	// 2. `thunk_FUN_10273070(this, '\x01')` — `SetHullSizeNormal(force = true)`.
	SetHullSizeNormal(/*bForce*/ true);

	// 3. `thunk_FUN_10274ca0(this)` — `SetDefaultEyeOffset`, which reads the hull's mins/maxs.
	SetDefaultEyeOffset();

	// 4. `*(undefined4 *)&this->field_0x64e8 = thunk_FUN_100ec640(this)`.
	ResolveStanceTableRow();
}

void FElysiumNpc::BaseHumanoidSetModel(TCHAR* ModelName)
{
	// `CAI_BaseHumanoid::SetModel` `0x1025e510`, 485 bytes, `CAI_BaseHumanoid#105`.
	//
	// ARGUED, and this is why the arm exists at all: `CAI_BaseHumanoid` sits under `CAI_BaseActor`
	// on a sibling SDK branch with 590 vtable slots and NO entity classname anywhere in the 77-class
	// census, while the spawnable chain is `CBaseCombatCharacter -> CAI_BaseNPC ->
	// CAI_BaseNPCTroika -> species`. So this is a registry-style species arm of slot 105 that no
	// spawned `npc_*` can reach today, and not a base beneath the Troika body. It is ported because
	// it is the ONLY recovered reading of what the 26 words at `+0x5fb8`..`+0x601c` are.
	SetRuntimeModel(ModelName != nullptr ? FString(ModelName) : FString());
	for (int32 Index = 0; Index < NumHumanoidPoseParams; ++Index)
	{
		// The two halves take different misses, and that asymmetry is retail's: the first thirteen go
		// through `CBaseAnimating::LookupPoseParameter` (family Anim's seam, -1) and the last thirteen
		// through `thunk_FUN_100b5d10`, which is `LookupFlexController` — whose recovered body answers
		// **0**, not -1, when nothing matches. A misspelt flex name therefore caches controller zero.
		HumanoidPoseParams[Index] = Index < GAnim10PoseParamFlexFirst
			? LookupPoseParameter(GAnim10PoseParamNames[Index])
			: LookupFlexController(GAnim10PoseParamNames[Index]);
	}
}

void FElysiumNpc::ZombieLineSetModel(TCHAR* ModelName, const TCHAR* RetailBody)
{
	// `CNPC_VGhoulCroucher::SetModel` `0x1037b1f0` and `CNPC_VZombie::SetModel` `0x103e0540` — the
	// same 119 bytes on two classes. One port body; `RetailBody` names which arm is running.
	(void)RetailBody;

	// The Troika base runs FIRST, so the model write, the hull and the eye all happen BEFORE
	// `IsMale` is ever asked. The call is direct in retail (`thunk_FUN_10298ce0`), which is what the
	// dispatch scope around this arm reproduces.
	SetModel(ModelName);

	// `CBaseCombatCharacter::IsMale` — the character's own stat slot 11.
	VSoundGroupName = Sheet.IsMale() ? FString(GAnim10ZombieMale) : FString(GAnim10ZombieFemale);

	// `*(undefined4 *)&this->field_0xbc = 2` — unconditional, and AFTER the gender read.
	VSoundTableIndex = GAnim10ZombieVSoundTableIndex;

	// `thunk_FUN_101f55a0(&DAT_1073dc28, this, group ? group : "", 0)` — the vocalization registry,
	// with the create flag CLEAR. Family Precache10's `VSoundGroupRowFor` is that seam and answers
	// retail's own count-zero miss; it is called rather than restated.
	VSoundGroupRow = VSoundGroupRowFor(*VSoundGroupName);
}

// =================================================================================================
// The activity triple.
// =================================================================================================

void FElysiumNpc::SetActivityAndSequence(int32 Activity, int32 Sequence, int32 TranslatedActivityIn,
	int32 WeaponActivity)
{
	// `CAI_BaseNPC::SetActivityAndSequence` `0x10272490`, 243 bytes. The commit, and every one of its
	// six effects is ordered against the others.

	// 1. `*(int *)((int)this + 0xff4) = param_3` — the TRANSLATED activity is written FIRST, before
	//    anything below can read it. Both the `EyeOffset` dispatch and the listener test below read
	//    the NEW value.
	TranslatedActivity = TranslatedActivityIn;

	if (Sequence < 0)
	{
		// 2. A negative sequence takes `ResetSequence(0)` and SKIPS the cycle, the duration and the
		//    weapon activity entirely. The rest of the body still runs.
		CommitForcedSequence(0);
	}
	else
	{
		// 3. The cycle word is zeroed UNLESS the sequence equals `m_nSequence` with `+0x65d` set, OR
		//    the old activity and the new one are BOTH in {ACT_WALK, ACT_RUN} — retail's walk/run
		//    cycle carry, which is what keeps a footfall in phase across a gait change.
		const bool bSameSequenceLooped =
			static_cast<float>(Sequence) == static_cast<float>(SequenceNumber) && bSequenceLoopedOnce;
		const bool bOldIsGait = ActivityNumber == GAnim10ActWalk || ActivityNumber == GAnim10ActRun;
		const bool bNewIsGait = Activity == GAnim10ActWalk || Activity == GAnim10ActRun;
		if (!bSameSequenceLooped && !(bOldIsGait && bNewIsGait))
		{
			SequenceCycle = 0.f;   // +0x06f8 m_flCycle
		}
		// 4. `ResetSequence(seq)`, `SequenceDuration(seq)`, then `Weapon_SetActivity(act, duration)`.
		CommitForcedSequence(Sequence);
		const float Duration = SequenceDurationOf(Sequence);
		WeaponSetActivity(WeaponActivity, Duration);
	}

	// 5. slot 533 `EyeOffset(activity, m_TranslatedActivity)` feeds `SetViewOffset` — for EVERY
	//    request, the negative-sequence one included.
	SetViewOffset(EyeOffset(Activity, TranslatedActivity));

	// 6a. slot 465 `OnChangeActivity(activity)` fires only when `m_Activity` DIFFERS from the new
	//     activity. Read BEFORE step 7 overwrites it.
	if (ActivityNumber != Activity)
	{
		OnChangeActivity(Activity);
	}
	// 6b. The activity-change listener `thunk_FUN_101f6010(&DAT_1073dd58, this, m_TranslatedActivity,
	//     m_Activity)` fires on a DIFFERENT comparison — `m_Activity` against the TRANSLATED
	//     activity, not against the requested one — and only while `m_bKeepSound` is clear. That is
	//     the whole reason `m_bKeepSound` exists: the Troika random pick raises it so the listener is
	//     not told about the intermediate activity.
	if (ActivityNumber != TranslatedActivity && !bKeepSound)
	{
		FActivityChangeNotice Notice;
		Notice.NewTranslatedActivity = TranslatedActivity;
		Notice.OldActivity = ActivityNumber;
		ActivityChangeNotices.Add(Notice);
	}

	// 7. `*(int *)((int)this + 0xfec) = param_1`, then the navigator.
	ActivityNumber = Activity;
	// `thunk_FUN_102e1cf0(m_pNavigator)` — SEAM, counted; family Motor found no navigator object.
	++NavigatorActivityNotices;
}

void FElysiumNpc::BaseSetActivity(int32 Activity)
{
	// `CAI_BaseNPC::SetActivity` `0x102725d0`, 95 bytes — slot 310's BASE body, a distinct retail
	// function beside the Troika override `0x10295750` that owns the slot. Ported under its own name,
	// the convention wave 1's `BasePrecache` set.
	//
	//     if ((m_Activity != act) && (act == 0 || m_Activity != 2)) { ... }
	//
	// Two refusals: a request that equals what is already playing is a NO-OP (which is why
	// `RestartIdealActivity` has to clear `m_Activity` first), and a body in `ACT_TRANSITION` (2)
	// refuses everything except `ACT_RESET` (0).
	if (ActivityNumber == Activity)
	{
		return;
	}
	if (Activity != GAnim10ActReset && ActivityNumber == GAnim10ActTransition)
	{
		return;
	}
	IdealActivityNumber = Activity;   // +0x0ff0 m_IdealActivity
	// `thunk_FUN_10272130(this, act, &m_nIdealSequence, &m_IdealTranslatedActivity,
	// &m_IdealWeaponActivity)` — family Anim's `ResolveActivityToSequence`.
	ResolveActivityToSequence(Activity, IdealSequence, IdealTranslatedActivity, IdealWeaponActivity);
	// `thunk_FUN_10272490(this, m_IdealActivity, m_nIdealSequence, m_IdealTranslatedActivity,
	// m_IdealWeaponActivity)` — note it re-reads `m_IdealActivity` rather than using the argument.
	SetActivityAndSequence(IdealActivityNumber, IdealSequence, IdealTranslatedActivity,
		IdealWeaponActivity);
}

void FElysiumNpc::AdvanceToIdealActivity()
{
	// `AdvanceToIdealActivity` `0x102726a0`. `param_1[0x1bc]` is `+0x6f0 m_nSequence`,
	// `param_1[0x1733]` is `+0x5ccc m_nIdealSequence`, `param_1[0x3fc]` is `+0x0ff0
	// m_IdealActivity`, `param_1[0x1734]`/`[0x1735]` the ideal translated/weapon pair, and
	// `+0x4d8` is slot 310.
	const int32 Transition = FindTransitionSequence(SequenceNumber, IdealSequence);
	if (Transition == INDEX_NONE)
	{
		// `if (fVar1 == -NAN)` — retail's own "no transition" sentinel. Dispatch slot 310 with the
		// ideal activity and stop. This is the arm the port always takes: `FindTransitionSequence` is
		// a seam over a studio graph this substrate does not stand.
		SetActivity(IdealActivityNumber);
		return;
	}
	if (Transition != IdealSequence)
	{
		// A real transition clip: commit ACTIVITY 2 (`ACT_TRANSITION`) with it, re-resolving the
		// translated/weapon pair from the transition sequence's OWN activity when it has one. Retail
		// seeds both locals with 2 before the resolve, so a transition whose sequence carries no
		// activity commits `(2, seq, 2, 2)`.
		int32 TranslatedForTransition = GAnim10ActTransition;
		int32 WeaponForTransition = GAnim10ActTransition;
		int32 SequenceForTransition = 0;
		const int32 TransitionActivity = SequenceActivityOf(Transition);
		if (TransitionActivity != INDEX_NONE)
		{
			ResolveActivityToSequence(TransitionActivity, SequenceForTransition,
				TranslatedForTransition, WeaponForTransition);
		}
		SetActivityAndSequence(GAnim10ActTransition, Transition, TranslatedForTransition,
			WeaponForTransition);
		return;
	}
	// The transition IS the ideal sequence: commit the ideal outright.
	SetActivityAndSequence(IdealActivityNumber, IdealSequence, IdealTranslatedActivity,
		IdealWeaponActivity);
}

void FElysiumNpc::BaseMaintainActivity()
{
	// `CAI_BaseNPC::MaintainActivity` `0x102727d0`, 230 bytes. The scope-trace push and pop around it
	// are retail's debug stack and are not reproduced; what is left is the gate and the two arms.
	//
	// NOT a vtable slot (`vtmb_func 0x102727d0`: `__thiscall`, no dispatch site), so it takes its own
	// name. Family SaveRestore10 named it as the one call `m_bForceMaintainActivity` spans.
	if (!ShouldMaintainActivity())   // slot 466, vtable +0x748
	{
		return;
	}
	if (ActivityNumber == IdealActivityNumber && SequenceNumber == IdealSequence)
	{
		// Nothing to maintain. Both terms are ORed in retail, so either mismatch is enough.
		return;
	}
	if (ActivityNumber == GAnim10ActTransition)
	{
		// The special arm: a body in `ACT_TRANSITION` WAITS for `m_bSequenceFinished` and then
		// advances, and does NOT re-resolve the ideal. That is what lets a transition clip play out.
		if (bSequenceFinished)
		{
			AdvanceToIdealActivity();
		}
		return;
	}
	// Every other activity re-resolves the ideal FIRST and then advances.
	ResolveActivityToSequence(IdealActivityNumber, IdealSequence, IdealTranslatedActivity,
		IdealWeaponActivity);
	AdvanceToIdealActivity();
}

// =================================================================================================
// Slot 310 `SetActivity` — one slot, three retail bodies.
// =================================================================================================

void FElysiumNpc::SetActivity(int32 Activity)
{
	if (SetActivitySpecies(Activity))
	{
		return;
	}
	TroikaSetActivity(Activity);
}

namespace
{
	struct FAnim10SetActivityArm
	{
		const TCHAR* Address = nullptr;
		const TCHAR* RetailClass = nullptr;
		void (FElysiumNpc::*Body)(int32) = nullptr;
	};
}

bool FElysiumNpc::SetActivitySpecies(int32 Activity)
{
	static const FAnim10SetActivityArm Arms[] =
	{
		{ GAnim10Body_HeadClawSetActivity, TEXT("CNPC_VTzimisceHeadClaw"),
			&FElysiumNpc::TzimisceHeadClawSetActivity },
		{ GAnim10Body_RunnerSetActivity, TEXT("CNPC_VTzimisceRunner"),
			&FElysiumNpc::TzimisceRunnerSetActivity },
	};

	if (SpeciesDispatchingSlot == GAnim10SlotSetActivity)
	{
		return false;
	}
	const FElysiumNpcClassSlot* Override =
		ElysiumNpcKernelClass::OverrideOf(RetailClass(), GAnim10SlotSetActivity);
	if (Override == nullptr)
	{
		return false;
	}
	for (const FAnim10SetActivityArm& Arm : Arms)
	{
		if (FCString::Strcmp(Arm.Address, Override->Address) != 0)
		{
			continue;
		}
		const FSpeciesDispatchScope Scope(*this, GAnim10SlotSetActivity);
		(this->*Arm.Body)(Activity);
		return true;
	}
	return false;
}

void FElysiumNpc::TroikaSetActivity(int32 Activity)
{
	// `CAI_BaseNPCTroika::SetActivity` `0x10295750`, 612 bytes. Three top arms, in this order.
	if (Activity == GAnim10ActFidgetSetFirst)
	{
		// --- Request 0x1093, the idle-fidget family -----------------------------------------------
		if (ActivityNumber < GAnim10ActFidgetSetFirst || ActivityNumber > GAnim10ActFidgetSetLast)
		{
			// Not already in the family: hand `0x1093` to the base and STOP.
			BaseSetActivity(GAnim10ActFidgetSetFirst);
			return;
		}
		if (IsActivityFinished())   // slot 251, vtable +0x3ec
		{
			// `m_bKeepSound` is raised around the pick so the activity-change listener is not told
			// about the intermediate activity, then cleared on every one of the four exits.
			bKeepSound = true;
			const int32 Roll = Anim10Rng().RandRange(0, 99);
			int32 Picked = GAnim10ActFidgetSetFirst;
			if (Roll < 15)
			{
				Picked = 0x1095;
			}
			else if (Roll < 30)
			{
				Picked = 0x1096;
			}
			else if (Roll < 40)
			{
				Picked = 0x1094;
			}
			BaseSetActivity(Picked);
			bKeepSound = false;
			return;
		}
		// Already in the family and the activity has NOT finished: retail falls out of the whole
		// body and does nothing. No base call.
		return;
	}
	if (Activity == GAnim10ActHuntSetFirst)
	{
		// --- Request 0x1115, the hunt-walk family --------------------------------------------------
		if (NpcFlags.HasFrenzied(GAnim10FrenziedRunFrenzy))
		{
			// The frenzy gait forces `ACT_RUN_FRENZY` — unless it is ALREADY playing, in which case
			// the body does nothing at all rather than restarting it.
			if (ActivityNumber != GAnim10ActRunFrenzy)
			{
				BaseSetActivity(GAnim10ActRunFrenzy);
			}
			return;
		}
		if (NpcFlags.HasFrenzied(GAnim10FrenziedRunGait))
		{
			if (ActivityNumber != GAnim10ActRun)
			{
				BaseSetActivity(GAnim10ActRun);
			}
			return;
		}
		if (ActivityNumber < GAnim10ActHuntSetFirst || ActivityNumber > GAnim10ActHuntSetLast)
		{
			BaseSetActivity(GAnim10ActHuntSetFirst);
			return;
		}
		if (!IsActivityFinished())
		{
			return;
		}
		// The two roll bounds start at 15 and 15 and WIDEN to 8 and 0x18 when the enemy is one this
		// NPC's memory knows — which is retail asking "have I actually seen where he is".
		int32 FirstBound = 0xf;
		int32 SecondBound = 0xf;
		FElysiumEntity* Enemy = GetEnemy();   // slot 168, vtable +0x2a0
		if (Enemy != nullptr)
		{
			// `thunk_FUN_102dfa20(GetEnemies(), enemy)` — does slot 541's memory carry a record for
			// him? `FElysiumNpcEnemyMemory::Find` is that question.
			const FElysiumNpcEnemyMemoryRecord* Record = EnemyMemory.Find(Enemy->Handle);
			if (Record != nullptr)
			{
				// `thunk_FUN_102dfed0(GetEnemies(), &out, enemy)` then slot 217 `GetAbsOrigin()`:
				// retail reads the last known position and its own origin into stack vectors and
				// DISCARDS both. The reads are reproduced because they are dispatches a program can
				// observe; the values go nowhere in retail either.
				const FVector LastKnown = Record->LastPosition;
				(void)LastKnown;
				(void)Origin;   // slot 217 GetAbsOrigin, read and discarded
				SecondBound = 0x18;
				FirstBound = 8;
			}
		}
		bKeepSound = true;
		const int32 Roll = Anim10Rng().RandRange(0, 99);
		int32 Picked = GAnim10ActHuntSetFirst;
		if (Roll < FirstBound)
		{
			Picked = 0x1116;
		}
		else if (Roll < SecondBound + FirstBound)
		{
			Picked = 0x1117;
		}
		BaseSetActivity(Picked);
		bKeepSound = false;
		return;
	}
	// --- Any other request forwards to the base unchanged ------------------------------------------
	BaseSetActivity(Activity);
}

void FElysiumNpc::TzimisceHeadClawSetActivity(int32 Activity)
{
	// `CNPC_VTzimisceHeadClaw::SetActivity` `0x103c1cd0`, 55 bytes. ONE rewrite in front of the
	// Troika body: request 9 `ACT_WALK` with a non-null slot 167 `GetEnemy` becomes `0x1136
	// ACT_TZ_WALK2`. Request 9 WITHOUT an enemy, and every other request, forwards unchanged.
	if (Activity == GAnim10ActWalk && GetEnemy() != nullptr)   // vtable +0x29c
	{
		SetActivity(GAnim10ActTzWalk2);   // the direct `thunk_FUN_10295750`, via the dispatch scope
		return;
	}
	SetActivity(Activity);
}

void FElysiumNpc::TzimisceRunnerSetActivity(int32 Activity)
{
	// `CNPC_VTzimisceRunner::SetActivity` `0x103c3d80`, 110 bytes. A five-entry request remap in
	// front of the Troika body, gated on the form byte `+0x6672` being non-zero, tested in retail's
	// own order — 1, 9, 0x13, 3, 0xf1 — and NOT in numeric order.
	int32 Request = Activity;
	if (bTzimisceRunnerForm)
	{
		if (Activity == GAnim10ActIdle)
		{
			Request = GAnim10ActTzIdle2;
		}
		else if (Activity == GAnim10ActWalk)
		{
			Request = GAnim10ActTzWalk2;
		}
		else if (Activity == GAnim10ActRun)
		{
			Request = GAnim10ActTzRun2;
		}
		else if (Activity == GAnim10ActFidget)
		{
			Request = GAnim10ActTzFidget2;
		}
		else if (Activity == GAnim10ActDisposition)
		{
			Request = GAnim10ActTzIdle2;
		}
	}
	SetActivity(Request);
}

// =================================================================================================
// Slot 375 `NPC_EarlyTranslateActivity` — one slot, six retail bodies.
// =================================================================================================

int32 FElysiumNpc::NPC_EarlyTranslateActivity(int32 Activity)
{
	int32 Answer = Activity;
	if (NpcEarlyTranslateActivitySpecies(Activity, Answer))
	{
		return Answer;
	}
	return TroikaNpcEarlyTranslateActivity(Activity);
}

namespace
{
	struct FAnim10TranslateArm
	{
		const TCHAR* Address = nullptr;
		const TCHAR* RetailClass = nullptr;
		int32 (FElysiumNpc::*Body)(int32) = nullptr;
	};
}

bool FElysiumNpc::NpcEarlyTranslateActivitySpecies(int32 Activity, int32& OutActivity)
{
	static const FAnim10TranslateArm Arms[] =
	{
		{ GAnim10Body_DogTranslate, TEXT("CNPC_VDog"),
			&FElysiumNpc::DogNpcEarlyTranslateActivity },
		{ GAnim10Body_HengeyokaiTranslate, TEXT("CNPC_VHengeyokai"),
			&FElysiumNpc::HengeyokaiNpcEarlyTranslateActivity },
		// 39 census classes share `0x103854f0`, `CNPC_VCop` among them — and `CNPC_VCop`'s classname
		// list is deliberately null, so a SPAWNED cop's `RetailClass()` is null and correctly falls
		// through to the Troika body instead.
		{ GAnim10Body_HumanTranslate, TEXT("CNPC_VHuman"),
			&FElysiumNpc::HumanNpcEarlyTranslateActivity },
		{ GAnim10Body_TzimisceTranslate, TEXT("CNPC_VTzimisce"),
			&FElysiumNpc::TzimisceNpcEarlyTranslateActivity },
		{ GAnim10Body_RunnerTranslate, TEXT("CNPC_VTzimisceRunner"),
			&FElysiumNpc::TzimisceRunnerNpcEarlyTranslateActivity },
	};

	if (SpeciesDispatchingSlot == GAnim10SlotEarlyTranslate)
	{
		return false;
	}
	const FElysiumNpcClassSlot* Override =
		ElysiumNpcKernelClass::OverrideOf(RetailClass(), GAnim10SlotEarlyTranslate);
	if (Override == nullptr)
	{
		return false;
	}
	for (const FAnim10TranslateArm& Arm : Arms)
	{
		if (FCString::Strcmp(Arm.Address, Override->Address) != 0)
		{
			continue;
		}
		const FSpeciesDispatchScope Scope(*this, GAnim10SlotEarlyTranslate);
		OutActivity = (this->*Arm.Body)(Activity);
		return true;
	}
	return false;
}

int32 FElysiumNpc::TroikaNpcEarlyTranslateActivity(int32 Activity)
{
	// `CAI_BaseNPCTroika::NPC_EarlyTranslateActivity` `0x10295590`, 295 bytes.
	int32 Request = Activity;

	// 1. The gait override ConVar `DAT_10924d6c`: 1 forces running, 2 forces walking.
	const int32 Gait = DebugConVar(GAnim10CvGait);
	if (Gait == GAnim10GaitForceRun)
	{
		if (Request == GAnim10ActWalk || Request == GAnim10ActHuntWalk)
		{
			Request = GAnim10ActRun;
		}
	}
	else if (Gait == GAnim10GaitForceWalk && Request == GAnim10ActRun)
	{
		Request = GAnim10ActWalk;
	}

	// 2. The frenzy word. Bit 0x40 wins outright over bit 0x20 — they are an `else if` in retail, not
	//    two tests — and each rewrite `goto`es past step 3.
	bool bRewroteGait = false;
	if (NpcFlags.HasFrenzied(GAnim10FrenziedRunFrenzy))
	{
		if (Request == GAnim10ActRunRelaxed || Request == GAnim10ActWalk || Request == GAnim10ActRun
			|| Request == GAnim10ActWalkRelaxed || Request == GAnim10ActHuntWalk
			|| Request == GAnim10ActCombatMove)
		{
			Request = GAnim10ActRunFrenzy;
			bRewroteGait = true;
		}
	}
	else if (NpcFlags.HasFrenzied(GAnim10FrenziedRunGait))
	{
		if (Request == GAnim10ActHuntWalk || Request == GAnim10ActWalk
			|| Request == GAnim10ActWalkRelaxed || Request == GAnim10ActCombatMove)
		{
			Request = GAnim10ActRun;
			bRewroteGait = true;
		}
	}

	// 3. `if (param_1 == 3) param_1 = 1;` — and ONLY when step 2 took neither rewrite, because both
	//    of those arms `goto LAB_10295655` past this test. UNOBSERVABLE, because 3 is in neither
	//    rewrite set, so a request that reaches step 2 as 3 leaves it as 3 either way. Recorded and
	//    reproduced because the listing says so.
	if (!bRewroteGait && Request == GAnim10ActFidget)
	{
		Request = GAnim10ActIdle;
	}

	// 4. BOTH delegates sit under capability bit `0x8000000`, and each RETURNS the delegate's answer
	//    rather than falling through. The checklist's walk put the reload delegate under the gate and
	//    left the cover one outside it; the listing puts one `TEST` in front of both
	//    (`10295655 CALL [+0x804]`, `1029565f TEST 0x8000000`), and so does this.
	if ((CapabilitiesGet() & GAnim10CapCoverAndReload) != 0)   // slot 513, vtable +0x804
	{
		if (Request == GAnim10ActReloadFast)
		{
			return GetReloadActivity(CurrentHintPointer());   // slot 570, vtable +0x8e8
		}
		if (Request == GAnim10ActCover
			|| (Request == GAnim10ActIdle
				&& (ScheduleHost.MemoryBits & GAnim10MemoryCoverIdle) != 0))
		{
			return GetCoverActivity(CurrentHintPointer());    // slot 569, vtable +0x8e4
		}
	}

	// 5. Otherwise the chain tail.
	return BaseCombatCharacterNpcEarlyTranslateActivity(Request);
}

int32 FElysiumNpc::BaseCombatCharacterNpcEarlyTranslateActivity(int32 Activity) const
{
	// `CBaseCombatCharacter::NPC_EarlyTranslateActivity(activity)` — the tail every arm ends in.
	// SEAM: it is not an NPC-kernel row and no census class below `CAI_BaseNPCTroika` overrides it;
	// it answers the activity UNCHANGED, which is the identity every recovered caller relies on.
	return Activity;
}

int32 FElysiumNpc::DogNpcEarlyTranslateActivity(int32 Activity)
{
	// `CNPC_VDog::NPC_EarlyTranslateActivity` `0x10374ad0`, 21 bytes. ONE early return the Troika
	// base never sees: retail preserves `ACT_FIDGET` by returning with `EAX` still holding the
	// request. Everything else forwards to `0x10295590`.
	if (Activity == GAnim10ActFidget)
	{
		return Activity;
	}
	return NPC_EarlyTranslateActivity(Activity);   // the direct thunk, via the dispatch scope
}

int32 FElysiumNpc::HengeyokaiNpcEarlyTranslateActivity(int32 Activity)
{
	// `CNPC_VHengeyokai::NPC_EarlyTranslateActivity` `0x10381b50`, 61 bytes.
	if (HengeyokaiCarryFormBit())
	{
		if (Activity == GAnim10ActIdle)
		{
			return GAnim10ActPickupLightIdle;
		}
		if (Activity == GAnim10ActWalk || Activity == GAnim10ActRun)
		{
			return GAnim10ActPickupLightCarry;
		}
	}
	// Every other request, and the whole bit-clear case, tail-calls the HUMAN body — not the Troika
	// one — which itself chains the Troika pre-translate.
	return HumanNpcEarlyTranslateActivity(Activity);
}

bool FElysiumNpc::HengeyokaiCarryFormBit() const
{
	// `thunk_FUN_10381c80(this)` — `m_bfAINPCFlags` (`+0x14b8`) bit 5, `0x20 CARRYING_BODY`.
	return NpcFlags.Has(EElysiumNpcFlag::CARRYING_BODY);
}

bool FElysiumNpc::TzimisceCarryFormBit() const
{
	// `thunk_FUN_103be130(this)` — the SAME `+0x14b8` bit 5 on `CNPC_VTzimisce`.
	return NpcFlags.Has(EElysiumNpcFlag::CARRYING_BODY);
}

int32 FElysiumNpc::HumanNpcEarlyTranslateActivity(int32 Activity)
{
	// `CNPC_VHuman::NPC_EarlyTranslateActivity` `0x103854f0`, 565 bytes, slot 375 for 39 census
	// classes. The armed/alert decision tree, then the rewrites.
	int32 Request = Activity;

	// 1. Capability bit 0x40 removes aim from a gait request.
	if ((CapabilitiesGet() & GAnim10CapNoAimGait) != 0)
	{
		if (Request == GAnim10ActWalkAim)
		{
			Request = GAnim10ActWalk;
		}
		else if (Request == GAnim10ActRunAim)
		{
			Request = GAnim10ActRun;
		}
	}

	// 2. No active weapon, or one whose `+0x19c` carries `0x40 NODRAW`: `m_bAggressiveAnims` is
	//    CLEARED and the request goes straight to the Troika pre-translate, skipping BOTH rewrite
	//    blocks. Retail calls `GetActiveWeapon()` twice here; the second call is what reads `+0x19c`.
	const FElysiumEntity* Weapon = ActiveWeaponEntity();
	if (Weapon == nullptr || (ActiveWeaponDrawFlags() & GAnim10WeaponNoDraw) == GAnim10WeaponNoDraw)
	{
		bAggressiveAnims = false;
		return TroikaNpcEarlyTranslateActivity(Request);
	}

	// 3. The decision tree, in retail's own nesting. The OUTER test is a four-term OR: the aggressive
	//    arm is taken only when capability `0x40` is set AND the ConVar `DAT_10924f74` is LIVE with a
	//    non-zero `+0x2c` AND `m_bfAINPCFlags2` carries `0x400 MOVE_FACE_ENEMY`.
	const bool bCombatAggressive = (CapabilitiesGet() & GAnim10CapNoAimGait) != 0
		&& DebugConVar(GAnim10CvAggressive) != 0
		&& NpcFlags.Has(EElysiumNpcFlag2::MOVE_FACE_ENEMY);
	if (bCombatAggressive)
	{
		bAggressiveAnims = true;
	}
	else if (NpcFlags.Has(EElysiumNpcFlag::FORCE_RELAXED_ANIMS))
	{
		// `m_bfAINPCFlags & 0x10000` wins over everything below it.
		bAggressiveAnims = false;
	}
	else if (NpcFlags.HasFrenzied(GAnim10FrenziedForceAggressive))
	{
		// `m_bfNPCFrenziedFlags & 0x200` skips the state ladder entirely and sets the flag.
		bAggressiveAnims = true;
	}
	else
	{
		// The state ladder. Retail CLEARS the flag first and then decides, which is why a state
		// outside {2, 3, 0xb} leaves it clear.
		bAggressiveAnims = false;
		const int32 State = Anim10RetailNpcState(Mind.State());
		if (State == 2)
		{
			bAggressiveAnims = true;
		}
		else if (State == 3 || State == 0xb)
		{
			// **THE POLARITY, read at the listing.** `goto LAB_10385611` — which leaves the flag
			// CLEAR — requires `(probe != 0 || cv[0xb] == 0) && (m_afMemory & 0x8000000) == 0`, i.e.
			// the ConVar is dead or zero AND the memory bit is clear. So the flag is SET when the
			// ConVar is live and non-zero, OR `m_afMemory` carries `0x8000000`. The pack-07 walk had
			// this arm inverted; the body wins.
			const int32 StateConVar = State == 3
				? DebugConVar(GAnim10CvAlert)
				: DebugConVar(GAnim10CvHunt);
			const bool bMemory =
				(ScheduleHost.MemoryBits & GAnim10MemoryAggressive) != 0;
			if (StateConVar != 0 || bMemory)
			{
				bAggressiveAnims = true;
			}
		}
	}

	// 4. Aggressive CLEAR rewrites the two gaits into their relaxed forms — through the Troika body,
	//    not by returning directly.
	if (!bAggressiveAnims)
	{
		if (Request == GAnim10ActWalk)
		{
			return TroikaNpcEarlyTranslateActivity(GAnim10ActWalkRelaxed);
		}
		if (Request == GAnim10ActRun)
		{
			return TroikaNpcEarlyTranslateActivity(GAnim10ActRunRelaxed);
		}
		return TroikaNpcEarlyTranslateActivity(Request);
	}

	// 5. Aggressive SET. `ACT_IDLE` becomes the armed idle ONLY when there is an active weapon whose
	//    slot 360 answer carries `0x6000`; retail re-fetches the weapon twice here too, and a null
	//    one falls out of the switch rather than taking the rewrite.
	if (Request == GAnim10ActIdle)
	{
		if (ActiveWeaponEntity() != nullptr
			&& (ActiveWeaponCapabilityWord() & GAnim10WeaponRangedAim) != 0)
		{
			return TroikaNpcEarlyTranslateActivity(GAnim10ActAim);
		}
		return TroikaNpcEarlyTranslateActivity(Request);
	}
	for (const int32(&Pair)[2] : GAnim10AggressivePairs)
	{
		if (Request == Pair[0])
		{
			return TroikaNpcEarlyTranslateActivity(Pair[1]);
		}
	}
	// 6. Everything else falls to the Troika pre-translate unchanged.
	return TroikaNpcEarlyTranslateActivity(Request);
}

int32 FElysiumNpc::TzimisceNpcEarlyTranslateActivity(int32 Activity)
{
	// `CNPC_VTzimisce::NPC_EarlyTranslateActivity` `0x103bde40`, 102 bytes.
	if (TzimisceCarryFormBit())
	{
		// **The polarity is the ZERO test**: `m_bHeavyBodyTarget` CLEAR takes the `_L` variants
		// (0xfd / 0xff) and SET takes the plain ones (0xfc / 0xfe). The generated table's
		// `BodySideLeft` comment read it the other way round; the listing's `CMP byte, 0` is what
		// this follows, and the generator's comment is corrected with it.
		if (!bHeavyBodyTarget)
		{
			if (Activity == GAnim10ActIdle)
			{
				return GAnim10ActIdleBodyL;
			}
			if (Activity == GAnim10ActWalk || Activity == GAnim10ActRun)
			{
				return GAnim10ActWalkBodyL;
			}
		}
		else
		{
			if (Activity == GAnim10ActIdle)
			{
				return GAnim10ActIdleBody;
			}
			if (Activity == GAnim10ActWalk || Activity == GAnim10ActRun)
			{
				return GAnim10ActWalkBody;
			}
		}
	}
	return NPC_EarlyTranslateActivity(Activity);   // the direct `thunk_FUN_10295590`
}

int32 FElysiumNpc::TzimisceRunnerNpcEarlyTranslateActivity(int32 Activity)
{
	// `CNPC_VTzimisceRunner::NPC_EarlyTranslateActivity` `0x103c3e10`, 82 bytes. It chains the Troika
	// body FIRST and only then remaps the TRANSLATED activity, so it is a POST-PASS on the base's
	// answer and not a replacement — which is exactly why its slot-310 twin `0x103c3d80` remaps the
	// REQUEST instead and the two look alike but are not.
	int32 Translated = NPC_EarlyTranslateActivity(Activity);
	if (bTzimisceRunnerForm)
	{
		switch (Translated)
		{
		case GAnim10ActIdle:
		case GAnim10ActDisposition:
			Translated = GAnim10ActTzIdle2;
			break;
		case GAnim10ActFidget:
			return GAnim10ActTzFidget2;
		case GAnim10ActWalk:
			return GAnim10ActTzWalk2;
		case GAnim10ActRun:
			return GAnim10ActTzRun2;
		default:
			break;
		}
	}
	return Translated;
}

// =================================================================================================
// Slot 314 `UpdatePoseParameters`.
// =================================================================================================

void FElysiumNpc::UpdatePoseParameters(float Interval)
{
	// `CAI_BaseNPCTroika::UpdatePoseParameters` `0x102bf070`, 528 bytes.
	//
	// The aim target is `m_hShootTargetOverride` (+0x5ba8) resolved and, failing that, slot 167
	// `GetEnemy`. Retail's control flow is a `goto` pair: a LIVE override enters the aim arm even
	// when `GetEnemy()` is null, and a dead override falls back to the enemy.
	FElysiumEntity* AimTarget = (World != nullptr && ShootTargetOverride.IsSet())
		? World->Resolve(ShootTargetOverride)
		: nullptr;
	bool bHaveTarget = AimTarget != nullptr;
	if (!bHaveTarget)
	{
		AimTarget = GetEnemy();   // vtable +0x29c
		bHaveTarget = AimTarget != nullptr;
	}

	// `m_iIsOblivious` (+0x5bb4) at 1 or more takes the no-aim arm even WITH a target: the test is
	// inside the aim arm, not in front of it.
	if (!bHaveTarget || IsOblivious())
	{
		bAimWeaponAtTarget = false;                 // +0x0e6c
		SetPoseParameterYaw = GAnim10Zero;          // +0x1064 = _DAT_104454c4
		SetPoseParameterPitch = 0.f;                // +0x1068
		++BaseUpdatePoseParameterCalls;             // the tail runs on BOTH arms
		return;
	}

	// slot 193 `EyePosition()`, then `0x10278650` for the aim point, then the delta.
	const FVector Eye = EyePosition();
	FVector AimPoint = FVector::ZeroVector;
	const bool bHaveAimPoint = AimPointFor(AimTarget, AimPoint);
	FVector Delta = AimPoint - Eye;
	// `PTR_thunk_FUN_10137220` is `VectorNormalize`, which leaves a zero-length vector alone.
	Delta.Normalize();

	bAimWeaponAtTarget = true;                                       // +0x0e6c
	WeaponAimTarget = AimTarget->Handle;                             // +0x0e70, else 0xffffffff
	// `m_vWeaponAimOffset` = aim point minus the TARGET's slot-217 origin. Retail dispatches slot 217
	// on the TARGET here, not on itself.
	WeaponAimOffset = bHaveAimPoint ? AimPoint - AimTarget->Origin : FVector::ZeroVector;

	// `VectorAngles(delta)`, the pitch biased by `_DAT_1049ae9c` (**7.5**, recovered 2026-09-14 out
	// of the pinned image; the checklist left it unnamed), the yaw taken RELATIVE to slot 219
	// `GetAbsAngles`, and both clamped to [-45, 45].
	const FVector AimAngles = Anim10RetailVectorAngles(Delta);
	const float Pitch = static_cast<float>(AimAngles.X) + GAnim10AimPitchBias;
	// slot 219 `GetAbsAngles()` — `FElysiumEntity::Angles` is Source `[pitch yaw roll]`, so `.Y`.
	const float Yaw = static_cast<float>(AimAngles.Y) - static_cast<float>(Angles.Y);

	auto Clamp = [](float Value)
	{
		// Retail's own two-sided clamp, low bound first: `if (low <= v) { if (high < v) v = high; }
		// else v = low;`.
		if (GAnim10AimClampLow <= Value)
		{
			return GAnim10AimClampHigh < Value ? GAnim10AimClampHigh : Value;
		}
		return GAnim10AimClampLow;
	};
	SetPoseParameterYaw = Clamp(Yaw);       // +0x1064, written FIRST in the listing's store pair
	SetPoseParameterPitch = Clamp(Pitch);   // +0x1068

	// The tail is ALWAYS `CBaseCombatCharacter::UpdatePoseParameters(param_1)`, whichever arm ran.
	(void)Interval;
	++BaseUpdatePoseParameterCalls;
}

// =================================================================================================
// Slot 326 — the knockback-eligibility gate.
// =================================================================================================

bool FElysiumNpc::BaseKnockbackAllowed(void* SwingRecord, FElysiumEntity* Attacker)
{
	// `CAI_BaseNPC::FUN_103482e0`, 170 bytes, slot 326's BASE body. Three arms in order: the NPC
	// template record's byte `+0x9e Disallow_Knockbacks`, then the `CVStatList_t::IsEqual(0x0f,
	// 0x11)` dead test, then slot 158 `m_lifeState == 0`.
	//
	// The checklist verdicts this row `present` against `ElysiumReactions::IsKnockbackAllowed`
	// (`ElysiumReactions.cpp:127`), which carries all three; it is CALLED rather than re-read, so the
	// two cannot drift. No arm has a side effect, so retail's term order is unobservable.
	(void)SwingRecord;
	(void)Attacker;
	const bool bAlive = !IsInert() && !HasReportedDeath();
	return ElysiumReactions::IsKnockbackAllowed(bAlive, DisallowsKnockbacks(),
		/*bBuildupAdmits*/ true, /*bClassBypass*/ false);
}

bool FElysiumNpc::Slot326(void* SwingRecord, FElysiumEntity* Attacker)
{
	// `CAI_BaseNPCTroika::FUN_1029fec0` `0x1029fec0`, 81 bytes.
	//
	//     bVar1 = CAI_BaseNPC::FUN_103482e0(this, record, attacker);
	//     if (!bVar1) return bVar1 & 0xffffff00;                 // the base's false passes through
	//     cv = IsCommand(DAT_109245e4) ? 0 : DAT_109245e4[0xb];
	//     if (m_iHitBuildupCount <= cv || attacker->+0xba == 2) return 1;
	//     return 0;                                              // the base's TRUE becomes false
	//
	// So the Troika arm can only ever NARROW the base's answer: it turns a true into a false when the
	// victim has already taken more hits than the ConVar admits and the attack is not marked
	// unconditional. `+0xba` is the SWING RECORD's marker, which `ElysiumReactions::
	// KnockbackUnconditionalMarker` names; retail reads it off `param_1`, the first argument.
	if (!BaseKnockbackAllowed(SwingRecord, Attacker))
	{
		return false;
	}
	const int32 Ceiling = HitBuildupConVarValue();
	const bool bUnconditional = Anim10SwingRecordIsUnconditional(SwingRecord);
	return HitBuildupCount <= Ceiling || bUnconditional;
}

// =================================================================================================
// Slot 330 — the near-miss flinch.
// =================================================================================================

void FElysiumNpc::Slot330(float Unused, void* BulletInfo)
{
	// `CAI_BaseNPCTroika::FUN_1029fbe0` `0x1029fbe0`, 205 bytes, `void(float, FireBulletsInfo_t*)`.
	//
	// `delta = GetAbsOrigin() - info->m_vecSrc` (+0x8/+0xc/+0x10), and its LENGTH comes out of
	// `0x10137220` — which family Senses10 recovered as `VectorNormalize`, a body that ANSWERS the
	// length while normalising in place. Retail keeps the un-normalised delta on the stack and hands
	// THAT to the reaction, not the normalised one: the normalise writes through `ECX`, which points
	// at a second copy.
	(void)Unused;

	const FFireBulletsInfo* Info = static_cast<const FFireBulletsInfo*>(BulletInfo);
	if (Info == nullptr)
	{
		// NAMED CRASH GUARD: retail dereferences `param_2` before any null test (`1029fbf7 FSUB
		// [param_2 + 8]`), so a null info faults. Nothing in this runtime calls slot 330 with one.
		return;
	}
	const FVector Delta = Origin - Info->SourceUnits;   // slot 217 GetAbsOrigin
	const float Length = static_cast<float>(Delta.Size());

	// The whole body is gated on THREE non-null terms, in retail's order: the firing weapon
	// (`info + 0x98`), the attacker (`info + 0x94`) and the attacker's own combat-character pointer
	// (`attacker + 0x9c`).
	const FElysiumEntity* Weapon = Info->Weapon;
	const FElysiumEntity* Attacker = Info->Attacker;
	if (Weapon == nullptr || Attacker == nullptr || Attacker->AsCombatCharacter() == nullptr)
	{
		return;
	}

	// The weapon's `CVDmg_t` row, and TWO distance bands off it. SEAM: `NearMissBands` answers false
	// (no `CVDmg_t` table here) and the body then takes retail's beyond-the-far-band arm, which does
	// nothing at all.
	float NearBand = 0.f;
	float FarBand = 0.f;
	if (!NearMissBands(Weapon, NearBand, FarBand))
	{
		return;
	}
	if (Length < NearBand)
	{
		PlayReaction(Delta, /*Kind*/ 2);
		return;
	}
	if (Length < FarBand)
	{
		PlayReaction(Delta, /*Kind*/ 0);
	}
	// Beyond the far band nothing happens.
}

// =================================================================================================
// Slot 359 — the Auspex aura index.
// =================================================================================================

int32 FElysiumNpc::Slot359(FElysiumEntity* Observer)
{
	// `CAI_BaseNPCTroika::FUN_1029e750` `0x1029e750`, 156 bytes, vtable `+0x59c`.
	//
	// `CBaseCombatCharacter::UpdateAuspex` (`0x100532b0`) calls it with the observer and uses the
	// answer as `DAT_10937160[idx + IsKindred * 8]` to pick the aura colour, treating a NEGATIVE as
	// no aura at all. So -1 means "draw nothing", not "error".
	if (EntityUnselectable())
	{
		return GAnim10AuraNone;
	}
	if (CurFrenzyCount > 0)   // +0x146c m_iCurFrenzyCount
	{
		return GAnim10AuraFrenzied;
	}
	if (NpcFlags.Has(EElysiumNpcFlag2::D_MILDLY_CRAZY))
	{
		// `(m_bfAINPCFlags2 & 0x80000) == 0x80000` — `D_MILDLY_CRAZY`. Note the arm order: retail
		// tests this BEFORE the state switch, so a mildly-crazy body never reads its state.
		return GAnim10AuraMildlyCrazy;
	}
	// The `m_NPCState` switch. **Six of its cases name retail states this runtime's
	// `EElysiumNpcState` has no member for** — 5, 8, 9, 0xa, 0xb, 0xd and 0xe — so those arms are
	// ported and UNREACHABLE until the state vocabulary widens. Named, not dropped.
	switch (Anim10RetailNpcState(Mind.State()))
	{
	case 2:
	case 0xb:
		return GAnim10AuraCombat;
	case 3:
	case 0xe:
		return GAnim10AuraAlert;
	case 5:
	case 6:
	case 8:
	case 9:
	case 10:
	case 0xd:
		return GAnim10AuraCalm;
	case 7:
		return GAnim10AuraNone;
	default:
		// The default arm is the one that reads the OBSERVER: a non-null observer whose
		// `IRelationType` (slot 404, vtable +0x650) answers `1 D_HT` gets the hostile colour.
		if (Observer != nullptr && IRelationType(Observer) == GAnim10DispositionHate)
		{
			return GAnim10AuraHostile;
		}
		return GAnim10AuraNeutral;
	}
}

// =================================================================================================
// Slot 584 — `CAI_ExpressiveNPC`'s expresser forward.
// =================================================================================================

bool FElysiumNpc::Slot584Species(int32 ConceptId)
{
	// The species prologue of slot 584. `CAI_BaseHumanoid#584` and `CAI_ExpressiveNPC#584` both carry
	// `FUN_10260dc0`; the Troika line's own body is `0x1028d910` (`ResetAllThinkStamps`, family
	// Closure) and is what runs for everything else.
	//
	// **Half of it is reachable.** `CAI_BaseHumanoid` carries no entity classname in the 77-class
	// census, so that half never runs; `CAI_ExpressiveNPC` claims `npc_TestBaseHumanoid`, so a body
	// spawned under that classname DOES take this arm. The checklist's walk called both unreachable.
	if (SpeciesDispatchingSlot == GAnim10SlotExpresserSpeak)
	{
		return false;
	}
	const FElysiumNpcClassSlot* Override =
		ElysiumNpcKernelClass::OverrideOf(RetailClass(), GAnim10SlotExpresserSpeak);
	if (Override == nullptr
		|| FCString::Strcmp(Override->Address, GAnim10Body_ExpresserSpeak) != 0)
	{
		return false;
	}
	const FSpeciesDispatchScope Scope(*this, GAnim10SlotExpresserSpeak);
	// The generated slot signature carries the TROIKA body's arity (one int), so the modifier string
	// has no way through and this arm speaks with an empty one. Stated rather than invented.
	ExpressiveNpcSpeak(ConceptId, nullptr);
	return true;
}

// =================================================================================================
// The one live-state evaluator both surfaces read.
// =================================================================================================

void FElysiumNpc::BindPreTranslateState(FElysiumAnimationIntent& Intent) const
{
	// The one wire between slot 375 and the generated tables. `ENpcPredicate` rides as `int32`
	// because the intent is public and the enum is a `Visual/` private header's; the cast is here
	// and nowhere else.
	Intent.NpcLiveState = [this](int32 Predicate, int32 Operand)
	{
		return PreTranslatePredicate(Predicate, Operand);
	};
}

bool FElysiumNpc::PreTranslatePredicate(int32 Predicate, int32 Operand) const
{
	using ElysiumActionTables::ENpcPredicate;
	static_assert(static_cast<int32>(ENpcPredicate::Count) == 18,
		"the generated predicate vocabulary changed; this evaluator has to answer all of it");

	// **THIS IS WHERE SLOT 375'S TWO SURFACES MEET.** The kernel body above reads the live state
	// directly; the generated table walk (`Visual/ElysiumNpcActivityTables.cpp` through
	// `ElysiumActionTables::NpcTranslate`) reads it through an `ENpcPredicate`. Before this story the
	// table walker answered FALSE to every predicate but two, so its frenzy, gait, cover, reload and
	// form rows could not fire at all. Every answer below is the SAME read the slot-375 body makes,
	// cited at the same address.
	switch (static_cast<ENpcPredicate>(Predicate))
	{
	case ENpcPredicate::Always:
		return true;
	case ENpcPredicate::GaitOverrideRun:
		// `0x10295590` step 1: the ConVar `DAT_10924d6c` reads 1.
		return DebugConVar(GAnim10CvGait) == GAnim10GaitForceRun;
	case ENpcPredicate::GaitOverrideWalk:
		return DebugConVar(GAnim10CvGait) == GAnim10GaitForceWalk;
	case ENpcPredicate::MovementPolicyFrenzy:
		// `0x10295590` step 2: `m_bfNPCFrenziedFlags & 0x40`.
		return NpcFlags.HasFrenzied(GAnim10FrenziedRunFrenzy);
	case ENpcPredicate::MovementPolicyRun:
		// ... and `& 0x20`, which retail reaches only when `0x40` is CLEAR — the two are an
		// `else if`. The table's row order reproduces that, so the predicate itself is the bare bit.
		return NpcFlags.HasFrenzied(GAnim10FrenziedRunGait);
	case ENpcPredicate::ReloadFastCapable:
	case ENpcPredicate::CoverCapable:
		// `0x10295590` step 4: ONE capability test, `0x8000000`, in front of BOTH delegates.
		return (const_cast<FElysiumNpc*>(this)->CapabilitiesGet() & GAnim10CapCoverAndReload) != 0;
	case ENpcPredicate::CoverIdleFlagged:
		// `m_afMemory & 2`, the bit that turns `ACT_IDLE` into a cover request.
		return (ScheduleHost.MemoryBits & GAnim10MemoryCoverIdle) != 0;
	case ENpcPredicate::NoAimGait:
		// `0x103854f0` step 1: capability bit `0x40`.
		return (const_cast<FElysiumNpc*>(this)->CapabilitiesGet() & GAnim10CapNoAimGait) != 0;
	case ENpcPredicate::ArmedAlert:
		// `0x103854f0`'s whole decision tree, answered as the ONE flag it writes. The flag is the
		// branch: every rewrite below it reads `m_bAggressiveAnims` and nothing else.
		return ActiveWeaponEntity() != nullptr && bAggressiveAnims;
	case ENpcPredicate::NotArmedAlert:
		// The OTHER arm of the same branch — and **both arms require an active weapon**. An UNARMED
		// body takes step 2's early-out at `10385e90`, which clears the flag and jumps straight to
		// `switchD_10385635_caseD_2` — `thunk_FUN_10295590(this, param_1)` with the request
		// UNCHANGED. Neither rewrite block runs for it, so an unarmed `ACT_WALK` stays `ACT_WALK`
		// and never becomes `ACT_WALK_RELAXED`. The generated table's comment read the early-out as
		// TAKING the relaxed arm; the listing says it skips it.
		return ActiveWeaponEntity() != nullptr && !bAggressiveAnims;
	case ENpcPredicate::RangedAimCapable:
		// `0x103854f0` step 5: the active weapon's slot-360 answer carries `0x6000`.
		return ActiveWeaponEntity() != nullptr
			&& (ActiveWeaponCapabilityWord() & GAnim10WeaponRangedAim) != 0;
	case ENpcPredicate::LaughIdleFlagged:
		// `m_bfAINPCFlags2 & 0x80000` — the Troika CLASS-translation's laugh-idle gate, the same bit
		// slot 359 reads as `D_MILDLY_CRAZY`.
		return NpcFlags.Has(EElysiumNpcFlag2::D_MILDLY_CRAZY);
	case ENpcPredicate::FormBit:
		// The class's OWN form bit. `CNPC_VHengeyokai` and `CNPC_VTzimisce` read `m_bfAINPCFlags`
		// bit 5 (`0x10381c80`, `0x103be130`); `CNPC_VTzimisceRunner` reads its own byte `+0x6672`.
		// Which one is a property of the CLASS, which is why one predicate serves all three.
		if (ElysiumNpcKernelClass::DerivesFrom(RetailClass(), TEXT("CNPC_VTzimisceRunner")))
		{
			return bTzimisceRunnerForm;
		}
		return NpcFlags.Has(EElysiumNpcFlag::CARRYING_BODY);
	case ENpcPredicate::BodySideLeft:
		// `CNPC_VTzimisce`'s `+0x6688`. **The `_L` variants are the ZERO arm** (`0x103bde40`:
		// `if (m_bHeavyBodyTarget == '\0')` takes `0xfd` / `0xff`, which are `ACT_IDLE_BODY_L` and
		// `ACT_WALK_BODY_L`). The generated header's comment read it the other way round and is
		// corrected with this body.
		return !bHeavyBodyTarget;
	case ENpcPredicate::RunnerVariantIs:
		// The generated rows that used this key were the wrong reading of `0x103c3e10` — retail
		// tests the byte for NON-ZERO and switches on the incoming activity, it does not compare a
		// variant value — and the generator no longer emits any. Answered here for completeness as
		// "the form byte equals the operand", which is what the name claims.
		return (bTzimisceRunnerForm ? 1 : 0) == Operand;
	case ENpcPredicate::CoverContextIs:
		// The cover context is the walker's own seed, not NPC state; the walker answers it itself.
		return false;
	case ENpcPredicate::ForcedLowCover:
		// `m_bfAINPCFlags & 0x200` — `COWER_PATH`, the same bit story 29c-1's slot 569 opens with.
		return NpcFlags.Has(EElysiumNpcFlag::COWER_PATH);
	default:
		return false;
	}
}
