#pragma once

#include "CoreMinimal.h"

// R7.4 (Phase 0 verdict D3): the sounds VtMB plays for water, resolved to the engine-relative
// filenames `AElysiumMapActor::PlayVoice` takes.
//
// VtMB's rule, from `vampire.dll`: `water.Impact` / `water.Scrape` off surfaceprop `water` on an
// impact or a scrape, `player/pl_wade2.wav` when the body leaves the water (`1003f4d0`), and a
// footstep from `Surfaces/Water/Step*` at water level 1 or `Surfaces/Wade/Step*` at level >= 2,
// on the step clock `CBasePlayer::UpdateStepSound` runs (`1011e940`). The footstep pool is keyed
// off the CLASSIFIED LEVEL, never off
// the surface material under the foot: the pier's foam cards bind `PM_default` (D4), so a
// material-keyed rule would give the waterline dry footsteps.
//
// THREE THINGS ARE NAMED DIVERGENCES, all recorded rather than guessed:
//
// 0. `player/pl_wade2.wav` IS NOT IN THE GAME. The literal is real -- `vampire.dll 1053c020`,
//    pushed by `CBaseEntity::PhysicsCheckWaterTransition` at `1003f4d0`, and again in
//    `client.dll 102c171c`/`1011cf40` -- but it is stock Source code naming a Half-Life 2 asset
//    Troika never packed: an index of every VPK plus every loose override in the install (67,469
//    packed entries, 78,538 files) carries no `pl_wade*` at all, and only eight `wade` files in
//    total, all of them `sound/surfaces/wade/`. The audio export mirrors the WHOLE supported
//    `sound/` tree, so this is not an export-selection gap that widening could close. Leaving the
//    water is therefore SILENT here because it was silent in VtMB -- the engine asked for a wav
//    that was never shipped. `Resolve` returns an empty string for `Exit` on any install that
//    does not carry the file, which is every retail and Unofficial Patch install measured; the
//    name is kept so an install that does carry it plays what the code names.
//
// 1. `water.Impact` and `water.Scrape` are sound SCRIPT names in
//    `scripts/game_sounds_surfaceproperties.txt` (`UElysiumPhysicalMaterial::SoundScriptImpact`
//    carries them verbatim as `vtmb:sound-script:` ids), and this runtime has no script table to
//    resolve one with. The wavs those scripts name are exported into the surfaceprop's own folder
//    beside its footstep pool -- `surfaces/water/impact1-3.wav` next to
//    `surfaces/water/stepleft1.wav` -- so the pool is taken from that folder by the script's own
//    verb. Replace this with the table when the sound-script lane lands; nothing else here moves.
// 2. VtMB draws the variation randomly. The draw arrives as a parameter from a named
//    `ElysiumRng::Stream`, because the substrate never calls `FMath::Rand*`.
namespace ElysiumWaterAudio
{
	enum class ECue : uint8
	{
		// A body hitting the water: `water.Impact`.
		Impact,
		// A body dragging along inside it: `water.Scrape`.
		Scrape,
		// Leaving the water: the one fixed filename VtMB names.
		Exit,
		// Water level 1 -- ankle deep.
		StepWater,
		// Water level >= 2 -- wading.
		StepWade,
	};

	// `player/pl_wade2.wav`, the literal `vampire.dll 1003f4d0` pushes. Divergence 0 in the file
	// header: no shipped VtMB install carries the file, so this resolves to nothing and leaving
	// the water is silent -- as it was in 2004. Kept by name, not by guessed substitute.
	inline const TCHAR* ExitSound = TEXT("player/pl_wade2.wav");

	// The wade pool's cycle. `UpdateStepSound`'s level >= 2 branch runs a four-phase counter
	// (`DAT_1070b898`) whose phase 0 returns BEFORE playing and the other three play, so three
	// wading steps in four sound. Level 1 has no such counter -- every step on the clock sounds.
	inline constexpr int32 StepsPerSound = 4;

	// The water half of `UpdateStepSound`'s speed pair, in Source units per second. A body with
	// any water level takes {60, 80} where a dry one takes {120, 220} (`1011ea3c`): below the
	// minimum no step is taken at all, and below the run speed the body is walking.
	inline constexpr float StepMinSpeedIn = 60.f;
	inline constexpr float StepRunSpeedIn = 80.f;
	// The interval each water pool sets on the step timer, in milliseconds, before the pair's own
	// minimum is added back to it (`1011ec5e`, `m_flStepSoundTime`).
	inline constexpr float StepIntervalWalkMs = 400.f;
	inline constexpr float StepIntervalRunMs  = 300.f;
	inline constexpr float StepIntervalWadeMs = 600.f;
	inline constexpr float StepIntervalBiasMs = 60.f;

	// Seconds to the next water step for a body moving at `Speed3dIn` Source units per second at
	// this classified level -- the interval `UpdateStepSound` writes back onto the timer.
	float StepIntervalSeconds(int32 WaterLevel, float Speed3dIn);

	// Which pool a classified water level draws its footstep from, and whether this step is one of
	// the ones that sounds. `StepIndex` counts the body's water steps, 0-based.
	bool IsSoundingStep(int32 StepIndex, int32 WaterLevel);
	ECue StepCue(int32 WaterLevel);

	// The engine-relative filename for a cue, or empty when the pool has no member (a checkout
	// whose sound export has not run, or a surfaceprop asset that is not baked). `Variation` is the
	// caller's own draw; `bRightFoot` selects the `stepright` pool over `stepleft` and is ignored
	// by the non-footstep cues.
	FString Resolve(ECue Cue, int32 Variation, bool bRightFoot = false);
}
