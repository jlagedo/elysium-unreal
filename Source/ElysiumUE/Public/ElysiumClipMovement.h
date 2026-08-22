#pragma once

#include "CoreMinimal.h"

// VtMB's **animation-driven movement**: the rule that a playing sequence, not the player's command,
// decides where the body goes (`docs/vtmb/source_movement.md`, `docs/vtmb/combat-and-damage.md`).
//
// One mechanism answers two things a player sees as separate — a melee swing stops a walking
// character dead, and the swing's own authored lunge carries them forward. `CPlayerMove::SetupMove`
// discards `forwardmove`/`sidemove`/`upmove` and refills them from the playing sequence's authored
// `mstudiomovement_t` path; `CGameMovement::WalkMove` then ASSIGNS the resulting wish to the
// velocity instead of accelerating toward it. The move is still swept and still collides — nothing
// teleports — so what changes is where the wish comes from, not who integrates it.
//
// This header owns the pure half: the record, the piecewise sampler over it, and the predicate that
// says whether the body is animation-driven this frame. It reads nothing but its own arguments, so
// it is asserted with no world, no catalog and no UObject (`Elysium.Substrate.MeleeMovementLock`) —
// the same pure-rules/engine-half split as `ElysiumMoveSolve.h` and `ElysiumCameraSolve.h`.
//
// It lives in `Public/` for the reason `ElysiumSwingRecord.h` and `ElysiumComboChain.h` do: the
// record crosses from the offline sidecar reader in the visual layer to the player's own mover.

// One `mstudiomovement_t` row, exactly as the blend sidecar states it
// (`pipeline/src/elysium_pipeline/formats/mdl_skel.py` -> `movement_table`).
//
// **Every value here is already Unreal-native and nothing converts it again.** The Source->Unreal
// reflection is spent at export, which is the whole reason retail's `x -> forwardmove`,
// `-y -> sidemove` reads with a negation and this does not: that negation IS the reflection, spent
// at the point of use instead. Take X as forward, Y as right and Z as up. Negating Y a second time
// mirrors every sideways attack.
struct FElysiumMovementRecord
{
	// The last frame of the block this record covers. The array is ascending, and the final entry's
	// value is the clip's own `numframes - 1`.
	int32 EndFrame = 0;
	// `motionflags` — which channels the block authors. Provenance: the sampler reads the vector.
	int32 Flags = 0;
	// **Lengths, not rates.** They are the ease coefficients of `v0 * f + 0.5 * (v1 - v0) * f * f`
	// over the block's own fraction, so the whole block's travel is `0.5 * (v0 + v1)` — exact on
	// every shipped record.
	float V0Cm = 0.0f;
	float V1Cm = 0.0f;
	// The block's authored yaw about Z, degrees, in Unreal's sense. Every shipped record states
	// exactly 0.0 (`docs/vtmb/animation_and_movers.md` carries the shipped census), so the rotation the
	// sampler applies with it is the identity in practice; it is implemented rather than dropped
	// because that zero is a fact about the corpus, not about the format.
	float YawDegrees = 0.0f;
	// The block's unit travel direction.
	FVector Direction = FVector::ZeroVector;
	// The **cumulative** position at `EndFrame`, from the clip's origin. Never "the displacement":
	// `baseballbat_attack_med` lunges 46.7 cm mid-clip and ends at exactly zero, so a reader taking
	// the last record's position makes that clip stand still.
	FVector PositionCm = FVector::ZeroVector;
};

// One clip's authored displacement path — the whole `mstudiomovement_t` array, in file order.
//
// **An empty path is a value, not a hole.** A label the sidecar's `movement` block does not carry
// authors no record at all, which is what makes retail's `Studio_AnimMovement` return false: no
// substitution happens, the discarded command is not refilled, and the body is held where it stands
// by ordinary friction. A sidecar carrying no `movement_fields` at all is the different absence —
// that file was written by a pipeline that never looked, and its reader reports rather than
// behaving as though the clip authored zero records.
struct FElysiumClipMovementPath
{
	TArray<FElysiumMovementRecord> Records;

	bool IsEmpty() const { return Records.IsEmpty(); }
	// The clip's last authored frame — the final record's own `EndFrame`.
	int32 LastFrame() const { return Records.IsEmpty() ? 0 : Records.Last().EndFrame; }
};

// The player's ideal activity and where its clip stands, which is everything the animation-driven
// predicate reads (`m_IdealActivity`, `m_nSequence`, `m_flCycle`, `seqdesc->w_hold`).
//
// It is a record rather than four parameters because the predicate is re-evaluated every frame from
// all four together — see `IsAnimationDriven`.
struct FElysiumIdealActivityState
{
	// `m_IdealActivity` — the LOGICAL activity forced onto the body, un-translated. Empty is a body
	// nothing has forced an activity onto, which matches no row and is the answer for every frame
	// outside an action.
	FString Activity;
	// `m_nSequence >= 0` — a clip really is standing on the base channel. False on a body with no
	// pose layer at all, which is a headless run and the gym.
	bool bHasSequence = false;
	// `m_flCycle` — the playing clip's own normalized position.
	float Cycle = 0.0f;
	// `seqdesc->w_hold` (`mstudioseqdesc_t`+0x2F8), the cycle the lock is released at. It does double
	// duty in the file: the same field is the combo hand-off cycle. **A sequence that authors no
	// combo block authors no `w_hold` and takes 1.0**, which is why a heavy finisher locks for its
	// whole clip.
	float HoldCycle = 1.0f;
};

// Where the base channel's own clip stands this frame, as the pose layer reports it.
//
// Pushed onto the animation driver by whoever owns the body, before the driver ticks: the driver
// also serves bodies that have no animation instance at all, so it never reaches for one. A
// default-constructed record is "nothing is standing there", which is a headless run and the gym.
struct FElysiumBaseClipCycle
{
	// `m_nSequence >= 0` — the channel really is standing on a clip.
	bool bPlaying = false;
	// `m_flCycle`.
	float Cycle = 0.0f;
	// The playing sequence's own `w_hold`, or 1.0 where it authors no combo block.
	float HoldCycle = 1.0f;
	// The clip's authored length in seconds and frame count, which the mover's window needs. Zero on
	// a body whose pose layer answered nothing.
	float LengthSeconds = 0.0f;
	int32 FrameCount = 0;
	// `m_flPlaybackRate` — the speed the pose layer is running that clip at. Retail's cycle rate is
	// `GetSequenceCycleRate(seq) * m_flPlaybackRate`, so the two halves of that product are carried
	// separately and multiplied where the lock is built.
	float PlayRate = 1.0f;
};

// Which arm of `vt+0x670` an ideal activity takes.
//
// **Only the melee rows are implemented.** The recovered predicate also covers four families this
// rung does not own, and each one is a row in the same table rather than a different mechanism:
//
//  - the block family (`ACT_BLOCKED_REACTION_LEFT`/`_RIGHT`, `ACT_BLOCK`, `ACT_BLOCK_HEAVY`), which
//    is driven while `curtime < m_flNextAttack` and therefore reads a clock rather than a cycle;
//  - the flying-knockback family, driven for its whole clip;
//  - the grounded knockback family, driven while `m_flCycle <= 0.8`;
//  - `ACT_VOMIT_INTO`/`_IDLE`/`_GETOUT` and `ACT_FEEDING_ENGAGE_FAILURE`, driven for the whole clip.
//
// They are named here and answer `None`, because a family with no row reads as "not driven" either
// way and an unlisted family would look like an oversight rather than a scope line.
//
// `vt+0x674` is the same predicate OR `ideal == ACT_LAND_HARD` — see `IsMovementLocked`.
enum class EElysiumAnimDrivenArm : uint8
{
	// Not an animation-driven family: the player's own command decides the move.
	None = 0,
	// The ordinary melee attack — driven while the cycle is below the sequence's authored `w_hold`.
	UntilHoldCycle,
	// The air, `2COMBO` and heavy melee families — driven for the whole clip.
	WholeClip,
};

namespace ElysiumClipMovement
{
	// The activity's arm, by name. Exact matches, because `ACT_MELEE_ATTACK` and
	// `ACT_MELEE_ATTACK_2COMBO` take different arms and a prefix test would collapse them.
	//
	// It deliberately coincides with `ElysiumCombo::BusyArmFor` on the melee family and is not shared
	// with it: that one is `CWeaponMelee`'s press-refusal, this one is `CBasePlayer`'s movement and
	// reselection lock, and they are two writes in two objects that happen to agree about melee.
	EElysiumAnimDrivenArm AnimDrivenArmFor(const FString& Activity);

	// **The predicate, and there is no latch behind it.** This is `vt+0x670`, published as bit `0x08`
	// at `player + 0x1ed8`; it is rebuilt every `PreThink` from the state below. A remembered "swing
	// active" flag gives the wrong answer the moment a reaction overwrites the ideal activity
	// mid-swing; retail's follows instantly because it is recomputed.
	//
	// Every melee row additionally requires a live sequence and `m_flCycle < 1.0`, which is the
	// predicate's own cycle guard and the third of the swing's three phases.
	//
	// **This is the arm the JUMP refusal reads**, and it is the narrower of the two.
	bool IsAnimationDriven(const FElysiumIdealActivityState& State);

	// `vt+0x674`, published as bit `0x80` — the arm the MOVEMENT substitution reads. The two virtuals
	// differ by exactly one line: `+0x674` is `+0x670` plus `if (m_IdealActivity == ACT_LAND_HARD)
	// return true`. `ACT_LAND_HARD` matches none of `AnimDrivenArmFor`'s rows, so the whole
	// divergence is that a hard landing locks the body to its animation while still permitting a
	// jump.
	//
	// **On the melee rows the two answers are identical today**, because those rows are all this
	// rung implements; the split is structural so the landing, knockback and vomit families can be
	// added without inheriting a jump refusal retail does not have.
	bool IsMovementLocked(const FElysiumIdealActivityState& State);

	// Whether an unfinished melee swing REFUSES this reselection — the second half of the same
	// mechanism, and the one that matters once the lock has released.
	//
	// Between `w_hold` and 1.0 the player has movement back and the ordinary selector runs again, so
	// what the swing's tail survives on is `ElysiumActionTables::FPlayerActionTuning`'s recovered
	// refuse set: `ACT_IDLE` and `ACT_AIM` are refused — the selector returns without applying — and
	// anything else is applied. A body standing still therefore plays its swing out, and a body that
	// is moving answers a gait, which IS applied and cuts the last ~9% of the clip. That is a
	// deliberate recovery cancel, not a dropped frame.
	//
	// Keyed on the recovered `MeleeHoldIdeal` (`ACT_MELEE_ATTACK`) alone, which is the only activity
	// the retail check names — and the only one that HAS this window, since the other three melee
	// rows are driven for their whole clip and never reach it.
	bool RefusesReselection(const FElysiumIdealActivityState& State, const FString& Candidate);

	// **The melee stop** — `CBasePlayer::PostThink`'s third half of the same mechanism.
	//
	// On the frame the movement lock RELEASES — the ideal activity is still `ACT_MELEE_ATTACK` and
	// the predicate above has just gone false — retail discards the body's carried motion outright
	// unless a direction key is held: `SetAbsVelocity(vec3_origin)` followed by
	// `SetLocalVelocity(vec3_origin)`, immediately before the classifier and `SetAnimation` run.
	//
	// It is what makes the reselection guard above WORK. Without it the swing's own authored lunge
	// is still on the body when the selector runs, the gait ladder reads a moving body, and
	// `RefusesReselection` lets that gait through — which cuts the swing's tail off. With the body
	// stopped the ladder answers an idle instead, the guard refuses it, and the clip plays out. The
	// two are one rule read from opposite ends: the guard says what a MOVING body loses, the stop
	// says which bodies count as moving.
	//
	// `HeldSelectionMask` is the button field in the FILE's own `IN_*` numbering
	// (`ElysiumCombo::SelectionMask`, retail's `+0x2088 & 0x79a`) — what
	// `FElysiumEntityWorld::PlayerSelectionStateMask` answers. It is the HELD KEYS and never the
	// realized velocity or the movement wish: the defect this exists to fix is precisely that
	// clip-driven motion must not be read back as input.
	//
	// **It is a per-frame WINDOW, not an edge.** The recovered block carries no latch: its three
	// tests all jump to the same convergence point, nothing reads or writes an "already stopped"
	// byte, and the busy predicate behind it is a pure function of the ideal activity, sequence and
	// cycle. So it re-runs on every frame from the sequence's `w_hold` to the end of the clip, and
	// the window sustains itself — a stopped body classifies as idle, the reselection guard refuses
	// that idle, the ideal activity therefore stays put, and the next frame stops the body again.
	// Anything that imparts motion during the tail without changing the ideal activity is killed
	// again on the frame after it lands, which is the behaviour a one-shot edge would lose.
	bool StopsMeleeTailMotion(const FElysiumIdealActivityState& State, int32 HeldSelectionMask);

	// `Studio_AnimPosition` — the cumulative position and yaw at a fractional frame.
	//
	// **Piecewise, and it must be.** Walk the records in order; the last one ending before the wanted
	// frame contributes its whole cumulative position, and the one containing it is eased into along
	// its own direction. Taking the final record's position instead answers a clip's endpoint, which
	// for an out-and-back lunge is zero.
	void PositionAtFrame(const FElysiumClipMovementPath& Path, float Frame, FVector& OutPositionCm,
		float& OutYawDegrees);

	// `Studio_AnimMovement` — the displacement between two cycles, in the frame the window opened in.
	//
	// False when there is nothing to sample: no records, a clip with fewer than two frames, or a
	// window that does not advance. False is the answer that leaves the discarded command unrefilled,
	// which is exactly what a clip authoring no movement does.
	//
	// `FrameCount` is the clip's authored frame count; the cycle maps onto `FrameCount - 1`, which is
	// the final record's own `EndFrame` on every shipped clip.
	bool SampleDelta(const FElysiumClipMovementPath& Path, int32 FrameCount, float CycleFrom,
		float CycleTo, FVector& OutDeltaCm);
}

// What the player's mover is handed about the swing that owns its command this frame.
//
// Pushed rather than pulled, for the same reason the gait tables are (`SetGaitSpeeds`): the mover
// runs in the pre-physics pass and the animation driver that knows the answer runs after the move,
// so the mover cannot ask — it has to be told, one frame later, exactly as retail's `SetupMove`
// reads the cycle the previous frame's animation left behind.
struct FElysiumAnimMovementLock
{
	// `vt+0x674` — whether the MOVEMENT lock holds. When it does the command's three movement axes
	// are discarded whatever else is true here: a lock with no path still stops the body.
	bool bActive = false;
	// `vt+0x670` — whether the same frame also refuses a jump press. It is the narrower arm: retail
	// permits a jump out of `ACT_LAND_HARD` while still driving the body from that clip. The melee
	// rows this rung implements set both together, so the two agree on every frame today.
	bool bRefusesJump = false;
	// `m_flCycle` as of the push.
	float Cycle = 0.0f;
	// `GetSequenceCycleRate(seq) * m_flPlaybackRate`, in cycles per second: how far the window the
	// mover samples reaches past `Cycle`.
	float CycleRate = 0.0f;
	// The playing clip's authored frame count, which is what the cycle is mapped onto.
	int32 FrameCount = 0;
	// The clip's authored path, shared rather than copied per frame. Null is a clip that authors no
	// movement, which is a value: the body is locked in place.
	TSharedPtr<const FElysiumClipMovementPath> Path;

	bool HasPath() const { return Path.IsValid() && !Path->IsEmpty() && FrameCount > 1; }
};
