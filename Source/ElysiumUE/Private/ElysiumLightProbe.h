#pragma once

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

class AElysiumMapActor;
class UWorld;

// Light attribution probe (`elysium.lightprobe`). For every source in the map's UElysiumLightRig
// it casts a ray fan against the real baked scene and records what that light is actually near,
// writing one JSON per map next to the Lights window's hand survey.
//
// The question it answers is "is this light standing in for a source, or standing in for bounce".
// VtMB has no global illumination, so a map's WORLDLIGHTS mix real fixtures with soft wide lights
// sprayed to keep rooms readable — the second kind is what a Lumen rebuild replaces. A fixture
// light sits ON its fixture (a few centimetres from a lamp model or a self-lit texture) and is the
// dominant contributor to the surfaces it reaches; a fill light floats in a volume and is one of
// many contributors everywhere it lands.
//
// It runs in-engine rather than over the exported sidecars because the engine holds the answers
// directly: Chaos traces against the real placed geometry, GetMaterialFromCollisionFaceIndex names
// the surface hit, and the bound MID's EmissiveScale says whether that surface actually glows —
// which is ground truth, not an inference from the .mtl.
namespace ElysiumLightProbe
{
	// Probe every light on the map actor's rig and write
	// <ContentRoot>/_lights/<map>.probe.json. Returns the number of lights probed, or -1 if
	// there was no rig / no world to trace against.
	int32 Run(UWorld* World, AElysiumMapActor* Map, int32 NumRays = 64);
}

#endif // !UE_BUILD_SHIPPING
