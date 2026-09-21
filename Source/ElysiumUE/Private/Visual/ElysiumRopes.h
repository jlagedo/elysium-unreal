#pragma once

#include "CoreMinimal.h"

#include "ElysiumRopes.generated.h"

// One cable segment, carried by the `AElysiumRopeActor` the map bake stands in the level. VtMB
// strings its overhead wires as chains of move_rope/keyframe_rope nodes linked by NextKey; both
// classnames construct the same `CRopeKeyframe`, and the producer (UE_map_sidecars.rope_rows)
// resolves each chain into per-segment rows, already in Unreal space (cm, Z-up, left-handed), so
// the runtime reads them verbatim and builds one UCableComponent per def.
//
// 0018 story 21-3: these rows reach the game as baked actors. Until then the runtime parsed
// `$ELYSIUM_EXPORT_ROOT/<map>/<map>.ropes` at map load, the last per-map file it opened from
// outside the project. The sidecar survives as an offline intermediate and nothing reads it here.
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
USTRUCT()
struct FElysiumRopeDef
{
	GENERATED_BODY()

	// Flag bits, as `CRopeKeyframe::KeyValue` sets them. Not a UENUM: `Flags` is the whole word and
	// the bake hands it over as one integer, exactly as the producer computed it.
	enum EFlags : uint8
	{
		Dangling  = 1 << 0,   // clears ROPE_LOCK_END_POINT — the far end hangs free
		Collide   = 1 << 1,
		Barbed    = 1 << 2,
		Breakable = 1 << 3,
	};

	UPROPERTY(VisibleAnywhere, Category = "Elysium|Rope")
	FString MaterialId;                // "vtmb:material:cable/cable"

	UPROPERTY(VisibleAnywhere, Category = "Elysium|Rope")
	FVector A = FVector::ZeroVector;   // start endpoint, cm

	UPROPERTY(VisibleAnywhere, Category = "Elysium|Rope")
	FVector B = FVector::ZeroVector;   // end endpoint, cm

	UPROPERTY(VisibleAnywhere, Category = "Elysium|Rope")
	float WidthCm = 5.f;

	UPROPERTY(VisibleAnywhere, Category = "Elysium|Rope")
	float RestCm = 0.f;                // simulated rest length; < |B-A| means taut

	UPROPERTY(VisibleAnywhere, Category = "Elysium|Rope")
	int32 Nodes = 10;                  // m_nSegments, [2, 10]; 2 == rigid straight segment

	UPROPERTY(VisibleAnywhere, Category = "Elysium|Rope")
	float TexScale = 1.f;

	UPROPERTY(VisibleAnywhere, Category = "Elysium|Rope")
	uint8 Flags = 0;
};
