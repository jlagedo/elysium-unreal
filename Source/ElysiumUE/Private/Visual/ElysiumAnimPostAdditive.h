#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimMetaData.h"

#include "ElysiumAnimPostAdditive.generated.h"

/**
 * That this sequence is a VtMB `_delta` -- a difference composed with the post-multiply rule --
 * carried on the sequence itself.
 *
 * The 118 shipped `*_delta` sequences carry the descriptor flags `0x14`, and retail accumulates
 * them onto the running pose with the delta on the right (`docs/vtmb/animation_and_movers.md`
 * A.4). Unreal's own additive types all put it on the left, so the family ships as ordinary
 * sequences holding the raw decoded delta and composes through
 * `FAnimNode_ElysiumPostAdditive`. `UAnimSequence::IsValidAdditive()` is therefore **false** on
 * every one of them, and this is what says otherwise.
 *
 * Metadata rather than a table beside the mount, for the reason `UElysiumAnimLayerMask` is: a
 * clip that states how it composes can be composed correctly by anything that opens it, and the
 * pairing survives with the asset. Every consumer -- the slot resolver, the lab, the composition
 * test -- reads this one channel, because a consumer keyed on something else would drop the
 * delta while the rest of the frame still looked right.
 *
 * Absent on every ordinary clip and on every masked overlay, which compose by opposite rules:
 * an overlay replaces the bones its mask owns, an additive accumulates onto them.
 */
UCLASS(EditInlineNew)
class UElysiumAnimPostAdditive : public UAnimMetaData
{
	GENERATED_BODY()

public:
	/** How many bones the delta's own weight mask owns, for a readout that opens nothing. */
	UPROPERTY(VisibleAnywhere, Category = "Elysium")
	int32 OwnedBones = 0;
};
