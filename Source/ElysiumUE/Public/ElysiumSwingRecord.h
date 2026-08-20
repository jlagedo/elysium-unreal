#pragma once

#include "CoreMinimal.h"

// One authored swing-contact record on a melee attack sequence — where on the swinging limb the
// attack sweeps, over which slice of the clip cycle, and which knockback the victim answers it with
// (`docs/vtmb/combat-and-damage.md`).
//
// The sequence descriptor declares `numswingcentres`/`swingcentreindex`, and the export writes the
// decoded array onto the clip sidecar's `swings` column
// (`pipeline/src/elysium_pipeline/formats/mdl_skel.py` -> `read_swing_records`, projected
// Unreal-native by `UE_mdl_skeletal.unreal_swings`). 574 of the install's 14,012 descriptors carry
// one; every other sequence carries none, which is an authored absence and not a gap.
//
// It lives in `Public/` rather than beside the parser for the same reason `Public/ElysiumAnimEvent.h`
// does: it crosses the outbound service seam, where `IElysiumEmbodiment::NpcClipSwings` hands one
// sequence's records down to the substrate.
struct FElysiumSwingRecord
{
	// The contact window as a fraction of the clip cycle, unitless. `End < Start` is authored on
	// four shipped records and is carried verbatim under `bDegenerate` — the overlap test reads the
	// pair as written, so such a record fires only on a sub-interval that straddles it.
	float Start = 0.0f;
	float End = 0.0f;

	// The bone the segment is stated in, by NAME: an NPC and the bank it fights from are separate
	// images with separate bone tables, so the name is the durable key. Empty when the descriptor's
	// bone index fell outside its own model's table, which is a defect the export already reported.
	FString Bone;

	// The segment's endpoints, BONE-LOCAL, in Unreal centimetres. The exporter put them through the
	// same `source_to_unreal` projection as every bind translation, so the frame they are stated in
	// is the frame the runtime's bone transform is in and no conversion happens here.
	FVector ACm = FVector::ZeroVector;
	FVector BCm = FVector::ZeroVector;

	// Four direction buckets of up to four candidate `ACT_*` knockback activities each. The buckets
	// are a ROTATION rather than a fixed direction order: bucket `k` answers direction
	// `(B8 + k) mod 4` over the cycle BACK, LEFT, FORWARD, RIGHT.
	TArray<TArray<FString>> KnockbackNames;

	// The two raw bytes the range test reads. `B8` is the direction bucket 0 answers; `Ba == 2` is
	// the marker that makes the knockback unconditional. Both are `0xFF` on the 639 records that
	// fill fewer than four buckets.
	int32 B8 = 0;
	int32 Ba = 0;

	// The file states this window backwards. Carried rather than repaired: authored data is
	// reproduced, and a consumer has to be told which records are so rather than infer it.
	bool bDegenerate = false;

	// The record states a limb to sweep at all. A row whose bone name did not resolve, or whose two
	// endpoints coincide, has no segment; it is skipped rather than swept as a point.
	bool HasSegment() const { return !Bone.IsEmpty() && !ACm.Equals(BCm, UE_KINDA_SMALL_NUMBER); }
};
