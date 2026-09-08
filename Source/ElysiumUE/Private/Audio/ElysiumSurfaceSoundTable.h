#pragma once

#include "CoreMinimal.h"

struct FElysiumSurfaceSounds;
class UElysiumPhysicalMaterial;

// The engine half of the surface sound table: the baked `UElysiumPhysicalMaterial` behind a
// surfaceprop name, and its audio fields as the engine-neutral record the substrate resolves a
// footstep against.
//
// One surface entry is one asset at `/ElysiumBaked/SurfaceProperties/PM_<name>`,
// and the importer has already flattened the
// `base` chain into it — so `brick`, which declares no `stepleft` of its own, carries `concrete`'s
// four alternates by the time it is baked. Resolution is therefore one load, never a chain walk.
//
// The cache is SESSION-scoped rather than per-map, unlike every body cache on the map actor: the
// 63 surface entries are map-independent authored data, and the alternative is a synchronous
// package load inside a footstep. It holds weak pointers, so a GC that reclaims an unreferenced
// material is answered with a reload rather than a dangling read — which is exactly what the
// water-audio lane's own loader did before this lifted it.
namespace ElysiumSurfaceSoundTable
{
	// The material at an explicit object path (`/ElysiumBaked/SurfaceProperties/PM_water.PM_water`),
	// loaded on first use and kept. Null when the asset is absent — an unbaked checkout, which is a
	// silent surface and not an error at this seam.
	const UElysiumPhysicalMaterial* Load(const TCHAR* ObjectPath);

	// The same, addressed by the surfaceprop NAME the locomotion sample publishes (`concrete`,
	// `default`). `NAME_None` answers null, which is retail's null `surfacedata_t`.
	const UElysiumPhysicalMaterial* Find(FName Surface);

	// `vtmb:sound:surfaces/water/stepleft1.wav` -> `surfaces/water/stepleft1.wav`, which is the
	// engine-relative path under `sound/` that `PlayVoice` and `PlayBodySound` take. An id that
	// does not carry the prefix is passed through unchanged.
	FString SoundRel(const FString& AssetId);

	// Fill `Out` from the baked material for `Surface`. False (and `Out` untouched) when the name
	// is `NAME_None` or no asset is baked for it; true otherwise, INCLUDING a surface whose chain
	// declares no step pool at all — 17 of the 63 entries declare one, 46 carry one after the
	// chain is flattened, and the remaining 17 resolve to a record with empty pools. The caller
	// tells "no surface" from "a surface with nothing to play" because retail does: the first is
	// `+0x5b90 == 0` and the second is an empty `stepleft` key.
	bool Resolve(FName Surface, FElysiumSurfaceSounds& Out);
}
