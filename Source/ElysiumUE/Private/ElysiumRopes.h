#pragma once

#include "CoreMinimal.h"

// One cable segment recovered from a map's `.ropes` sidecar (roadmap 8.7). VtMB strings its
// overhead wires as chains of move_rope/keyframe_rope nodes linked by NextKey; the exporter
// (UE_bsp_to_scene.py) resolves each chain into per-segment lines, already in Unreal space (cm,
// Z-up, left-handed), so the runtime reads them verbatim and builds one UCableComponent per def:
// A/B are the two fixed endpoints, WidthCm the cable thickness, SlackCm the extra rest length
// beyond the straight A→B distance (what makes it hang), Subdiv the node subdivision count, and
// TexScale the along-length texture tiling factor. Tex is a "tex/rope_*.png" path (the decoded
// RopeMaterial), or "-" when the exporter could not decode it (runtime falls back to a plain MID).
struct FElysiumRopeDef
{
	FString Tex;                       // "tex/..." albedo, or "-" for none
	FVector A = FVector::ZeroVector;   // start endpoint, cm
	FVector B = FVector::ZeroVector;   // end endpoint, cm
	float WidthCm = 5.f;
	float SlackCm = 0.f;
	int32 Subdiv = 2;
	float TexScale = 1.f;
};

// Parses a `.ropes` sidecar: one segment per line,
//   <tex> ax ay az  bx by bz  width_cm slack_cm subdiv texscale
// (11 whitespace-separated tokens). Malformed lines are skipped.
struct FElysiumRopes
{
	// Read the sidecar file and parse it. Returns true when the file was read (even if it held zero
	// valid segments); false when the file is absent/unreadable.
	static bool Parse(const FString& Path, TArray<FElysiumRopeDef>& Out);

	// Parse already-loaded lines (the file-free core, so it is unit-testable without disk I/O).
	static void ParseLines(const TArray<FString>& Lines, TArray<FElysiumRopeDef>& Out);
};
