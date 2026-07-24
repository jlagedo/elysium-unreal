#pragma once

#include "CoreMinimal.h"

// One projected decal recovered from a map's `.decals` sidecar (roadmap 7.2). All vectors are
// Unreal space (cm, Z-up, left-handed) — the exporter (UE_bsp_to_scene.py) already converted them,
// so the runtime reads them verbatim. The runtime builds one deferred UDecalComponent per def:
// Normal is the room-facing projection axis, SDir/TDir the surface tangent frame, HalfW/HalfH the
// on-surface half-extents. Mat keys into the shared <map>.mtl for the albedo/emissive textures.
struct FElysiumDecalDef
{
	FString Mat;
	FVector Loc = FVector::ZeroVector;      // projected centre on the wall, cm
	FVector Normal = FVector::UpVector;     // unit, room-facing outward normal
	FVector SDir = FVector::ForwardVector;  // unit, surface U axis (maps to HalfW)
	FVector TDir = FVector::RightVector;    // unit, surface V axis (maps to HalfH)
	float HalfW = 0.f;                       // cm
	float HalfH = 0.f;                       // cm
};

// Parses a `.decals` sidecar: one decal per line,
//   <material> lx ly lz  nx ny nz  sx sy sz  tx ty tz  hw hh
// (15 whitespace-separated tokens). Malformed lines are skipped.
struct FElysiumDecals
{
	// Read the sidecar file and parse it. Returns true when the file was read (even if it held zero
	// valid decals); false when the file is absent/unreadable.
	static bool Parse(const FString& Path, TArray<FElysiumDecalDef>& Out);

	// Parse already-loaded lines (the file-free core, so it is unit-testable without disk I/O).
	static void ParseLines(const TArray<FString>& Lines, TArray<FElysiumDecalDef>& Out);
};
