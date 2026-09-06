#pragma once

#include "CoreMinimal.h"

// One `surfacedata_t`'s AUDIO half, as a plain value the substrate can resolve a footstep against
// without touching a `UObject` (`docs/architecture/footstep-architecture.md` §4.2).
//
// Retail reads exactly these fields off the `surfacedata_t*` the NPC caches at `+0x5b90`
// (`vampire.dll 1026d460`) and the player caches as `m_pSurfaceData`:
//
//   * `stepleft` (`+0x2c`) / `stepright` (`+0x2e`) — the two footstep keys. Each is a VARIATION
//     POOL, not a single sound: `scripts/surfaceproperties.txt` repeats the key inside one entry
//     block and the engine picks between the repeats (`docs/vtmb/surface_properties.md` →
//     "Footsteps"). Duplicates are therefore alternates and are NEVER collapsed here; a pool's
//     size is part of the authored data.
//   * `gamematerial` — VtMB's single-letter material class, which is what
//     `CBasePlayer::UpdateStepSound` (`1011e940`) switches its dry step volume on: `D` 0.25/0.55,
//     `V` 0.40/0.70, anything else 0.20/0.50 (walk/run). Kept as the authored string rather than a
//     `TCHAR` because 37 of the 63 shipped entries declare none anywhere in their `base` chain, and
//     an empty string is the honest spelling of that.
//   * `impact` / `scrape` — the sound-SCRIPT names in
//     `scripts/game_sounds_surfaceproperties.txt`. They ride along because the surface table is
//     resolved once per surface and AUD2's later impact work reads the same record; nothing in the
//     footstep lane consumes them yet.
//
// Engine-neutral by construction: the strings are the engine-relative paths
// `IElysiumAudio::PlayVoice`/`PlayBodySound` take (`surfaces/concrete/stepleft1.wav`), already
// stripped of the `vtmb:sound:` id prefix by whoever filled the record. The map actor answers
// `IElysiumEmbodiment::ResolveSurfaceSounds` from the baked `UElysiumPhysicalMaterial`; a recording
// double answers from a table a test wrote — the `ResolveDisposition` precedent for authored data
// crossing the seam as data.
struct FElysiumSurfaceSounds
{
	// `stepleft`, in source order. Duplicates preserved: they are the alternates.
	TArray<FString> StepLeft;
	// `stepright`, in source order.
	TArray<FString> StepRight;
	// `gamematerial`, resolved through the `base` chain. Empty when no unit in the chain declares
	// one, which is retail's "no letter" and takes `UpdateStepSound`'s default volume pair.
	FString GameMaterial;
	// `impact` / `scrape` — the first sound-script name each key carries, or empty. Single strings
	// rather than pools because the script name is the addressing unit; the wavs behind it are the
	// script's own variation set.
	FString Impact;
	FString Scrape;

	// A surface a step can actually be drawn from. Retail's silent arm is a null `surfacedata_t`,
	// not an empty pool, but a baked entry whose chain never declares a step key answers the same
	// way and the producer has to be able to tell.
	bool HasSteps() const { return !StepLeft.IsEmpty() || !StepRight.IsEmpty(); }
};
