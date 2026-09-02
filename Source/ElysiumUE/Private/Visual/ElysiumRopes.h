#pragma once

#include "CoreMinimal.h"

// One cable segment recovered from a map's `.ropes` sidecar. VtMB strings its
// overhead wires as chains of move_rope/keyframe_rope nodes linked by NextKey; both classnames
// construct the same `CRopeKeyframe`, and the producer (UE_map_sidecars.write_ropes) resolves
// each chain into per-segment lines, already in Unreal space (cm, Z-up, left-handed), so the
// runtime reads them verbatim and builds one UCableComponent per def.
//
// The numbers are the RE'd `CRopeKeyframe` state (vampire.dll + client.dll), not the raw Hammer
// keyvalues: A/B are the two endpoints, WidthCm the strand thickness, Nodes the simulated node
// count `m_nSegments` — which VtMB takes from the `Type` keyvalue (0 -> 10, 1 -> 4, else -> 2)
// clamped to [2, 10], **not** from `Subdiv` — and TexScale the along-length tiling factor.
//
// MaterialId (R6.5) is the rope material's `vtmb:material:<key>` unit id — `cable/cable`,
// `cable/rope`, `cable/chain` or an authored RopeMaterial — and the runtime binds the `MI_` the
// material lane imported for it (`FElysiumContentPaths::BakedMaterial`). The instance carries the
// texture, the normal map and the shader mode; nothing about the look rides in this line.
//
// RestCm is the strand's simulated rest length, and it is the only thing the sag depends on: the
// surplus over |B-A| *is* the sag. The producer derives it from the engine's own two-stage
// integer computation (`RopeThink` folds Slack into `m_RopeLength`, then `RecomputeSprings` adds
// Slack a second time and subtracts a flat 100 units) — see `write_ropes` for the derivation. It
// is emitted pre-resolved because the truncation is integral and belongs in Source units. RestCm
// below |B-A| is normal and means the cable hangs taut.
//
// Nodes == 2 is the important case: one segment between two locked points, which cannot sag. A
// quarter of the game's rope nodes are `Type 2` and are meant to render as dead-straight taut
// cable (the observatory lift cables, hanging-lamp chains).
struct FElysiumRopeDef
{
	// Flag bits, as `CRopeKeyframe::KeyValue` sets them.
	enum EFlags : uint8
	{
		Dangling  = 1 << 0,   // clears ROPE_LOCK_END_POINT — the far end hangs free
		Collide   = 1 << 1,
		Barbed    = 1 << 2,
		Breakable = 1 << 3,
	};

	FString MaterialId;                // "vtmb:material:cable/cable"
	FVector A = FVector::ZeroVector;   // start endpoint, cm
	FVector B = FVector::ZeroVector;   // end endpoint, cm
	float WidthCm = 5.f;
	float RestCm = 0.f;                // simulated rest length; < |B-A| means taut
	int32 Nodes = 10;                  // m_nSegments, [2, 10]; 2 == rigid straight segment
	float TexScale = 1.f;
	uint8 Flags = 0;
};

// Parses a `.ropes` sidecar: one segment per line,
//   vtmb:material:<key> ax ay az  bx by bz  width_cm rest_cm nodes texscale flags
// (12 whitespace-separated tokens). Malformed lines are skipped.
struct FElysiumRopes
{
	// Read the sidecar file and parse it. Returns true when the file was read (even if it held zero
	// valid segments); false when the file is absent/unreadable.
	static bool Parse(const FString& Path, TArray<FElysiumRopeDef>& Out);

	// Parse already-loaded lines (the file-free core, so it is unit-testable without disk I/O).
	static void ParseLines(const TArray<FString>& Lines, TArray<FElysiumRopeDef>& Out);
};
