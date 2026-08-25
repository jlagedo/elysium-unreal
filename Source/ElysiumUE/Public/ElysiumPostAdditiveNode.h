#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Animation/AnimNodeBase.h"

#include "ElysiumPostAdditiveNode.generated.h"

/**
 * VtMB's additive combine, as an Unreal blend node.
 *
 * Retail accumulates one evaluated `_delta` layer onto the running local pose with the delta on
 * the **right** -- `q = normalize(q ⊗ scale(D, s))`, `pos += D.pos · s`
 * (`vampire.dll 0x100c12b0`, twin `client.dll 0x10088d60`, selected by the sequence descriptor's
 * `flags & 0x10`, which all 118 shipped `_delta` sequences carry). Every
 * `EAdditiveAnimationType` Unreal ships puts the delta on the **left** instead, so no
 * `AdditiveAnimType`, `RefPoseType` or reference-frame choice reaches retail's answer: the
 * difference is a conjugation by the base rotation, which is not a property of the clip and
 * changes with whatever pose the delta lands on.
 *
 * That is why this is a runtime rule rather than a bake input, and it is one of the four the
 * animation programme names (`docs/architecture/animation-architecture.md`). A `_delta` ships as
 * an ordinary sequence holding the raw decoded delta -- every track parent-relative, every bone
 * the clip does not animate at identity rotation and zero translation -- and this node states
 * what composing it means. The clip needs no VtMB knowledge to be evaluated; the composition
 * order lives here, once, where the two poses meet.
 *
 * `scale(D, s)` is retail's `QuaternionScale` (`0x1013add0`), an exact shortest-arc slerp from
 * identity, not an nlerp -- so it is `FQuat::Slerp(FQuat::Identity, D, s)`. Unreal's quaternion
 * `*` is the Hamilton product in the same order retail's `QuaternionMult` (`0x1013af60`) computes
 * it, so `Base * Scaled` **is** the post-multiply; it is not reversed here.
 *
 * The node takes no per-bone mask asset. A `_delta`'s masked-out bones are baked at the additive
 * identity, where `q ⊗ I = q` and `pos += 0`, so gating them would be the same no-op. The one
 * gate it does apply is retail's own shape from the other side -- a bone whose per-bone weight
 * makes `s` zero is skipped (`0x100c150e`) -- read off the engine's own signal: a bone the
 * sequence carries no track for evaluates to the compact reference pose bit-for-bit, and the
 * node leaves it alone. That is what keeps a body's own bones -- the ones a shared bank never
 * declares and no bake against the bank can write -- from being turned by their own bind.
 */
USTRUCT(BlueprintInternalUseOnly)
struct ELYSIUMUE_API FAnimNode_ElysiumPostAdditive : public FAnimNode_Base
{
	GENERATED_USTRUCT_BODY()

	/** The pose the delta lands on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Links)
	FPoseLink Base;

	/** The raw `_delta`, evaluated as the ordinary sequence it now ships as. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Links)
	FPoseLink Additive;

	/**
	 * Retail's `s`. `1.0` for an autolayer -- the dispatcher pushes a literal there
	 * (`0x1008a0ce`) -- and the slot's weight for an overlay, which this graph applies once on
	 * the outer layered blend rather than twice.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Alpha, meta = (PinShownByDefault))
	float Alpha = 1.0f;

	/** Max LOD this node runs at, the same contract every stock blend node carries. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Performance, meta = (DisplayName = "LOD Threshold"))
	int32 LODThreshold = INDEX_NONE;

	// FAnimNode_Base interface
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	virtual int32 GetLODThreshold() const override { return LODThreshold; }
	// End of FAnimNode_Base interface

private:
	/** The clamped weight `Update_AnyThread` resolved, which is what `Evaluate` composes with. */
	float ActualAlpha = 0.0f;
};
