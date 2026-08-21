#pragma once

#include "CoreMinimal.h"

// The animation's own speed, per direction (CCC7, `docs/vtmb/source_movement.md` → "Player speed is
// animation-driven").
//
// VtMB has no scalar gait speed. `CHL2_Player::PreThink` reads each locomotion fan's per-cell root
// motion into per-direction tables and the client writes one cell's **absolute** speed into the move
// command; `m_flMaxspeed` is only the peak over those cells, a clamp ceiling. So a body's forward
// speed and its strafe speed are different authored numbers, and asking "how fast is a walk" has no
// answer without a direction.
//
// This is that table, and only the table: cells in, a speed at an angle out. It carries no UObject
// and no engine gameplay type, so it is asserted with no world (`Elysium.Substrate.GaitSpeeds`) —
// the same pure-rules/engine-half split `ElysiumMoveSolve.h` and `ElysiumCameraSolve.h` use. The
// engine halves are `ElysiumBlendGrids::SpeedFan` (which fills one from a baked grid) and
// `UElysiumMovementComponent::WishSpeed` (which asks it a question once per substep).
//
// It lives in its own header rather than inside `ElysiumMoveSolve.h` because it is *produced* by the
// Visual layer and *consumed* by the Player layer, while `ElysiumMoveSolve.h` stays the mover's own
// `CGameMovement` math.

// One gait's fan, in the body's facing frame.
//
// Cell *k* sits at `AxisMin + k * (AxisMax - AxisMin) / (Count - 1)`, so on the shipped 9-cell
// `move_yaw` fan cell 0 IS -180 and cell 8 IS +180 — the two share one clip across the wrap seam,
// and 0 degrees lands exactly on cell 4 (`docs/vtmb/animation_and_movers.md` → "A `move_yaw` fan's
// cells are angles").
struct FElysiumGaitSpeedTable
{
	// The most cells a shipped locomotion fan carries. Every one of the 107 fans in a player body's
	// resolved vocabulary is 9x1.
	static constexpr int32 MaxCells = 9;

	// Authored ground speed per cell, cm/s, **before** `Scale`.
	float Cells[MaxCells] = {};
	int32 Count = 0;

	// The pose-parameter range the cells span, degrees.
	float AxisMin = 0.0f;
	float AxisMax = 0.0f;

	// The gait's own multiplier — `sv_walkscale` 1.0, `sv_runscale` 1.0, `sv_sneakscale` **2.3**,
	// times the character's rate multiplier where it applies. Kept as a field rather than folded
	// into the cells so a readout can still show what the animation authored against what the scale
	// did to it.
	float Scale = 1.0f;

	bool IsValid() const;

	// The commanded speed at a facing-relative direction, cm/s, scaled. The two cells the angle
	// falls between are blended.
	//
	// **A divergence, and a deliberate one.** Retail snaps to the nearest cell — its speed source is
	// a 3x3 digital key table, so it can never land between two. A stick can, and on the walk fan
	// the cells differ by more than 2x, so snapping reads as the stride popping.
	float SpeedAt(float YawDegrees) const;

	// The 0-degree cell — the forward gait. Retail's walk/run threshold is this plus 1 u/s.
	float Forward() const;

	// The largest cell, scaled. This is what retail publishes as `m_flMaxspeed`, and it is a ceiling
	// by construction: no direction can command more.
	float Peak() const;

	// Average each mirrored pair, so strafing left and strafing right command the same speed.
	//
	// **A divergence, and a deliberate one.** The authored walk fan is asymmetric — 38.2 u/s left
	// against 23.9 u/s right on the male body — which is faithful and reads as a limp. Cells 0 and
	// `Count - 1` are the same clip, so averaging them changes nothing.
	void Symmetrize();
};

// The three gaits a body publishes together. They are resolved from the **un-relaxed**
// `ACT_WALK`/`ACT_RUN`/`ACT_SNEAK`, exactly as retail's extractor does, so no table depends on the
// gait currently selected and the set can be built once per body rather than per frame.
struct FElysiumGaitSpeeds
{
	FElysiumGaitSpeedTable Walk;
	FElysiumGaitSpeedTable Run;
	FElysiumGaitSpeedTable Sneak;

	// Valid when at least one gait resolved. A body missing one fan still steers by the others: the
	// tables are per gait, exactly as retail's six are, and an unresolved one answers zero on its own
	// without touching the two that did resolve.
	bool IsValid() const;

	// The ceiling across every gait — `m_flMaxspeed`.
	float Peak() const;
};

// Everything the wish-speed decision reads, as data rather than as mover state — so the decision
// table is asserted with no pawn, no world and no console.
struct FElysiumWishSpeedInput
{
	// Body state. `bDucked` is the settled crouch *or* either ramp: retail's ladder branches on
	// `FL_DUCKING`, which is one bit.
	bool bNoclip = false;
	bool bOnGround = true;
	bool bDucked = false;
	// `+speed`, which selects the **slow** gait — the run is the default.
	bool bWalkKey = false;

	// The **commanded** direction, facing-relative, and the command's own deflection.
	float WishYawDegrees = 0.0f;
	float Scale = 1.0f;

	// What the last grounded substep commanded — what an airborne body keeps commanding, because
	// retail's tables stop refreshing for the duration of a jump.
	float LastGroundedWishSpeed = 0.0f;

	// The two speeds that are not the animation's: `sv_jump_maxspeed` and the dev fly speed.
	float JumpMaxSpeed = 0.0f;
	float NoclipSpeed = 0.0f;
};

namespace ElysiumGait
{
	// Which gait a command selects — retail's ladder minus the speed tests, which belong to the
	// animation classifier and read the speed this produces rather than deciding it.
	const FElysiumGaitSpeedTable& TableFor(const FElysiumGaitSpeeds& Speeds, bool bDucked,
		bool bWalkKey);

	// The whole seam. A selected gait that resolved no fan commands **zero** — retail's gait fill is
	// conditional on a 9-blend grid, writes nothing when there is not one, and `Spawn` is the only
	// site that ever clears the slot, so an unwritten cell on a fresh body reads zero and no constant
	// stands in for it. The decision is per gait, not wholesale: a body with a walk fan and no sneak
	// fan still walks at its authored speed and only its crouch is silent.
	float WishSpeedFrom(const FElysiumWishSpeedInput& In, const FElysiumGaitSpeeds& Speeds);

	// `m_flMaxspeed` — the **ceiling** over the cells, which is a different question from
	// `WishSpeedFrom`'s "which cell". Grounded it is the peak over every gait; while the jump phase
	// is live retail's `PreThink` pins it to `sv_jump_maxspeed` instead, so an airborne body is
	// bounded higher than a standing one.
	//
	// It reads only the body half of the input — `bNoclip`, `bOnGround`, `JumpMaxSpeed`,
	// `NoclipSpeed`. A ceiling is a property of the body, so neither the commanded direction nor the
	// command's deflection enters it.
	float MaxSpeedFrom(const FElysiumWishSpeedInput& In, const FElysiumGaitSpeeds& Speeds);
}
