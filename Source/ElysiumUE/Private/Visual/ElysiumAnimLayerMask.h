#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimMetaData.h"

#include "ElysiumAnimLayerMask.generated.h"

/**
 * Which bones a VtMB layer sequence owns, carried on the sequence itself.
 *
 * VtMB's partial-body overlays -- `<weapon>_aim_layer` and the rest of the 209 shipped `*_layer`
 * sequences -- are composed over a base pose through a per-bone gate, the animation record's
 * `weight`@0 (`docs/vtmb/animation_and_movers.md` A.4). A bone outside the gate keeps whatever the
 * base pose put there; a bone inside it takes the overlay, holding its bind pose where the overlay
 * animates nothing. The two are the same bytes in the file and opposite results on screen, which is
 * why the gate ships rather than being inferred.
 *
 * The gate itself is a `UBlendProfile` in `EBlendProfileMode::BlendMask` on the sequence's own
 * skeleton, because that is the asset an Animation Blueprint's layered blend already consumes; this
 * names which one. Metadata rather than a runtime side table so the pairing survives with the asset
 * -- a sequence that says which bones it owns can be composed correctly by anything that opens it,
 * including the animation editor.
 *
 * Absent on every ordinary clip, which owns the whole rig and needs no gate.
 */
UCLASS(EditInlineNew)
class UElysiumAnimLayerMask : public UAnimMetaData
{
	GENERATED_BODY()

public:
	/** The blend mask on this sequence's skeleton (`USkeleton::GetBlendProfile`). */
	UPROPERTY(VisibleAnywhere, Category = "Elysium")
	FName Profile;

	/** How many bones it owns, for a readout that does not have to open the profile. */
	UPROPERTY(VisibleAnywhere, Category = "Elysium")
	int32 OwnedBones = 0;
};
