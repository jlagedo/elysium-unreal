#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcAnim10Shared.h"

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
// The three melee selectors and the zombie idle gate moved to their species classes (story 5
// step 4). The walked prose is `docs/vtmb/npc-ai/shape.md`.
//
// Every body below is retail's, arm by arm, in retail's order, with the `0x10……` address on the line
// that carries it. The standing fact of this family: **an activity id is state, not a picture.** The
// only thing here that is "visual" is which clip an id eventually resolves to, and this family does
// not touch that at all — it lands the ids, the commit order, the two edges that fire listeners and
// the four words the pose arm writes.

namespace
{
	// Unit-prefixed: the module builds adaptive-unity and anonymous namespaces are merged.

	// --- The retail `Activity` numbers these bodies name ------------------------------------------
	//
	// Spelled once, as every other family in this band spells them: the port stands no retail
	// activity table (family Hints' `RestartIdealActivityId` records why).
	constexpr int32 GAnim10ActReset = 0;            // ACT_RESET
	constexpr int32 GAnim10ActTransition = 2;       // ACT_TRANSITION — `SetActivity`'s second refusal
	constexpr int32 GAnim10ActCover = 6;            // ACT_COVER
	constexpr int32 GAnim10ActReloadFast = 0x55;    // ACT_RELOAD_FAST
	constexpr int32 GAnim10ActRunFrenzy = 0xf17;    // ACT_RUN_FRENZY
	constexpr int32 GAnim10ActHuntWalk = 0x1115;    // ACT_HUNT_WALK
	constexpr int32 GAnim10ActCombatMove = 0x1121;  // ACT_COMBATMOVE

	// The Troika `SetActivity` families. `0x1093`..`0x1096` is the idle-fidget set and
	// `0x1115`..`0x1117` the hunt-walk set; the request that ENTERS each arm is the family's first
	// member, which is why the two bounds tests read "my current activity is already in the family".
	constexpr int32 GAnim10ActFidgetSetFirst = 0x1093;
	constexpr int32 GAnim10ActFidgetSetLast = 0x1096;
	constexpr int32 GAnim10ActHuntSetFirst = 0x1115;
	constexpr int32 GAnim10ActHuntSetLast = 0x1117;

	// --- The bit masks --------------------------------------------------------------------------
	//
	// `m_bfNPCFrenziedFlags` (+0x5b84). `0x40` is the frenzy gait and `0x20` the hurried one;
	// `0x200` is the bit the human body treats as "aggressive, whatever the state says".
	constexpr uint32 GAnim10FrenziedRunFrenzy = 0x40;
	constexpr uint32 GAnim10FrenziedRunGait = 0x20;

	// `CapabilitiesGet()` (slot 513). `0x8000000` gates BOTH Troika delegates and `0x40` is the
	// human body's no-aim-gait bit.
	constexpr int32 GAnim10CapCoverAndReload = 0x8000000;

	// `m_afMemory` (+0x5d8c). Bit 1 (`0x2`) turns the Troika body's `ACT_IDLE` into a cover
	// delegate; bit 27 (`0x8000000`) is the human body's own aggressive override.
	constexpr uint32 GAnim10MemoryCoverIdle = 0x2;

	// `m_bfAINPCFlags` (+0x14b8) `0x20 CARRYING_BODY` — the form bit BOTH `0x10381c80` and
	// `0x103be130` probe, and `0x10000 FORCE_RELAXED_ANIMS`.
	// `m_bfAINPCFlags2` (+0x14bc) `0x400 MOVE_FACE_ENEMY` and `0x80000 D_MILDLY_CRAZY`.

	// --- The ConVars, rows of the tunables table (0019/4), every one read at its `+0x2c` int --------
	constexpr NpcKernelAnim10Shared::EAnim10ConVar GAnim10CvGait = NpcKernelAnim10Shared::EAnim10ConVar::DebugForceAnim;             // 0x10295590's gait override
	constexpr NpcKernelAnim10Shared::EAnim10ConVar GAnim10CvHitBuildup = NpcKernelAnim10Shared::EAnim10ConVar::NpcHitBuildupAmount;  // slot 326's buildup ceiling

	// The gait override's two live values.
	constexpr int32 GAnim10GaitForceRun = 1;
	constexpr int32 GAnim10GaitForceWalk = 2;

	// --- The two vocalization-group literals `0x1037b1f0` and `0x103e0540` write ------------------
	//
	// `-(uint)(s[0] != '\0') & <addr>` is retail's "a null `string_t` for an empty literal" idiom;
	// neither literal is empty, so both always store the pointer.
	constexpr TCHAR GAnim10ZombieMale[] = TEXT("Zombie_Male");      // 0x1063b150
	constexpr TCHAR GAnim10ZombieFemale[] = TEXT("Zombie_Female");  // 0x1063b140
	constexpr int32 GAnim10ZombieVSoundTableIndex = 2;              // the literal written to +0x00bc

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
	// ladder (`ElysiumNpcFacing.cpp`); it is a file-static there and cannot be called, so the
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

}

// =================================================================================================
// The seams.
// =================================================================================================

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
	// ceiling: `npc_hit_buildup_amount`, "2". `FElysiumCombatCharacter::HitBuildupAdmitAtOrBelow` is
	// the player-side reading of the same ConVar (`docs/vtmb/combat-and-damage.md`).
	return ElysiumNpcTunables::ConVarInt(GAnim10CvHitBuildup);
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

// =================================================================================================
// Slot 105 `SetModel` — one slot, three retail bodies.
// =================================================================================================

void FElysiumNpc::SetModel(TCHAR* ModelName)
{
	// `CNPC_VGhoulCroucher` and `CNPC_VZombie` override slot 105 on their C++ classes (story 5
	// step 3); their shared body calls `TroikaSetModel` directly, as retail's `0x10298ce0` thunk does.
	TroikaSetModel(ModelName);
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

void FElysiumNpc::ZombieLineSetModel(TCHAR* ModelName, const TCHAR* RetailBody)
{
	// `CNPC_VGhoulCroucher::SetModel` `0x1037b1f0` and `CNPC_VZombie::SetModel` `0x103e0540` — the
	// same 119 bytes on two classes. One port body; `RetailBody` names which arm is running.
	(void)RetailBody;

	// The Troika base runs FIRST, so the model write, the hull and the eye all happen BEFORE
	// `IsMale` is ever asked. The call is direct in retail (`thunk_FUN_10298ce0`), and so it is here.
	TroikaSetModel(ModelName);

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
		const bool bOldIsGait = ActivityNumber == NpcKernelAnim10Shared::GAnim10ActWalk || ActivityNumber == NpcKernelAnim10Shared::GAnim10ActRun;
		const bool bNewIsGait = Activity == NpcKernelAnim10Shared::GAnim10ActWalk || Activity == NpcKernelAnim10Shared::GAnim10ActRun;
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
	// `CNPC_VTzimisceHeadClaw` and `CNPC_VTzimisceRunner` override slot 310 (story 5 step 3).
	TroikaSetActivity(Activity);
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
			if (ActivityNumber != NpcKernelAnim10Shared::GAnim10ActRun)
			{
				BaseSetActivity(NpcKernelAnim10Shared::GAnim10ActRun);
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

// =================================================================================================
// Slot 375 `NPC_EarlyTranslateActivity` — one slot, six retail bodies.
// =================================================================================================

int32 FElysiumNpc::NPC_EarlyTranslateActivity(int32 Activity)
{
	// Five classes override slot 375 on their C++ classes (story 5 step 3): Dog, Hengeyokai, the
	// human line (`0x103854f0`), Tzimisce and the Tzimisce runner.
	return TroikaNpcEarlyTranslateActivity(Activity);
}

int32 FElysiumNpc::TroikaNpcEarlyTranslateActivity(int32 Activity)
{
	// `CAI_BaseNPCTroika::NPC_EarlyTranslateActivity` `0x10295590`, 295 bytes.
	int32 Request = Activity;

	// 1. The gait override ConVar `debug_force_anim` (`DAT_10924d6c`, ships 0): 1 forces running, 2
	//    forces walking.
	const int32 Gait = ElysiumNpcTunables::ConVarInt(GAnim10CvGait);
	if (Gait == GAnim10GaitForceRun)
	{
		if (Request == NpcKernelAnim10Shared::GAnim10ActWalk || Request == GAnim10ActHuntWalk)
		{
			Request = NpcKernelAnim10Shared::GAnim10ActRun;
		}
	}
	else if (Gait == GAnim10GaitForceWalk && Request == NpcKernelAnim10Shared::GAnim10ActRun)
	{
		Request = NpcKernelAnim10Shared::GAnim10ActWalk;
	}

	// 2. The frenzy word. Bit 0x40 wins outright over bit 0x20 — they are an `else if` in retail, not
	//    two tests — and each rewrite `goto`es past step 3.
	bool bRewroteGait = false;
	if (NpcFlags.HasFrenzied(GAnim10FrenziedRunFrenzy))
	{
		if (Request == NpcKernelAnim10Shared::GAnim10ActRunRelaxed || Request == NpcKernelAnim10Shared::GAnim10ActWalk || Request == NpcKernelAnim10Shared::GAnim10ActRun
			|| Request == NpcKernelAnim10Shared::GAnim10ActWalkRelaxed || Request == GAnim10ActHuntWalk
			|| Request == GAnim10ActCombatMove)
		{
			Request = GAnim10ActRunFrenzy;
			bRewroteGait = true;
		}
	}
	else if (NpcFlags.HasFrenzied(GAnim10FrenziedRunGait))
	{
		if (Request == GAnim10ActHuntWalk || Request == NpcKernelAnim10Shared::GAnim10ActWalk
			|| Request == NpcKernelAnim10Shared::GAnim10ActWalkRelaxed || Request == GAnim10ActCombatMove)
		{
			Request = NpcKernelAnim10Shared::GAnim10ActRun;
			bRewroteGait = true;
		}
	}

	// 3. `if (param_1 == 3) param_1 = 1;` — and ONLY when step 2 took neither rewrite, because both
	//    of those arms `goto LAB_10295655` past this test. UNOBSERVABLE, because 3 is in neither
	//    rewrite set, so a request that reaches step 2 as 3 leaves it as 3 either way. Recorded and
	//    reproduced because the listing says so.
	if (!bRewroteGait && Request == NpcKernelAnim10Shared::GAnim10ActFidget)
	{
		Request = NpcKernelAnim10Shared::GAnim10ActIdle;
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
			|| (Request == NpcKernelAnim10Shared::GAnim10ActIdle
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
	switch (NpcKernelAnim10Shared::Anim10RetailNpcState(Mind.State()))
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

bool FElysiumNpc::AnimFormBit() const
{
	// `CNPC_VHengeyokai` and `CNPC_VTzimisce` read `m_bfAINPCFlags` bit 5 in their slot-375 bodies
	// (`0x10381c80`, `0x103be130`). `CNPC_VTzimisceRunner` reads its own byte `+0x6672` instead, and
	// overrides this on its C++ class.
	return NpcFlags.Has(EElysiumNpcFlag::CARRYING_BODY);
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
		return ElysiumNpcTunables::ConVarInt(GAnim10CvGait) == GAnim10GaitForceRun;
	case ENpcPredicate::GaitOverrideWalk:
		return ElysiumNpcTunables::ConVarInt(GAnim10CvGait) == GAnim10GaitForceWalk;
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
		return (const_cast<FElysiumNpc*>(this)->CapabilitiesGet() & NpcKernelAnim10Shared::GAnim10CapNoAimGait) != 0;
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
			&& (ActiveWeaponCapabilityWord() & NpcKernelAnim10Shared::GAnim10WeaponRangedAim) != 0;
	case ENpcPredicate::LaughIdleFlagged:
		// `m_bfAINPCFlags2 & 0x80000` — the Troika CLASS-translation's laugh-idle gate, the same bit
		// slot 359 reads as `D_MILDLY_CRAZY`.
		return NpcFlags.Has(EElysiumNpcFlag2::D_MILDLY_CRAZY);
	case ENpcPredicate::FormBit:
		// The class's OWN form bit, asked of the class (story 5 step 3): see `AnimFormBit`.
		return AnimFormBit();
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
