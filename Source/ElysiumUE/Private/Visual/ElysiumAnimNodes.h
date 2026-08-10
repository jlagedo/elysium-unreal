#pragma once

#include "CoreMinimal.h"
#include "Animation/BoneSocketReference.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"

#include "ElysiumAnimNodes.generated.h"

struct FElysiumCompositionRig;

// VtMB's one composition stage as an Unreal skeletal control (roadmap CAP7.2).
//
// It derives `FAnimNode_SkeletalControlBase` and runs in the slot a post-process Anim Blueprint
// occupies — after the graph has blended locals, before skinning — which is retail's own order.
// This runtime has no Anim Blueprint asset (`UElysiumNpcAnimInstance` is a native anim instance
// with no graph), so that slot is the tail of `FElysiumNpcAnimProxy::Evaluate`, and the node is
// driven through `ResolveBones` + `Apply` rather than through pose links.
//
// **It is the only VtMB rule left in the frame path**, and it earns that on one property: it reads
// a LIVE control-bone orientation, so its input is the blended pose rather than anything a file
// states. Every other rule names a value the file carries somewhere and is resolved at bake
// instead (repo-root `CLAUDE.md`, "Poses are baked native").
//
// It is safe to evaluate on an animation worker thread: it reads a shared immutable rig held by
// `TSharedPtr`, resolves every bone index once in `InitializeBoneReferences`, and touches no
// UObject and no game-thread state while evaluating.
//
// The engine's own `FAnimNode_PoseDriver` is the wrong tool even though its shape matches. It
// interpolates target poses with a radial basis function rather than the sign-selected three-way
// slerp the rule uses, so it would approximate a stage that reproduces retail to 1e-4.

// The one genuine stage — `ProcType == 1`: read the control bone's LOCAL rotation, evaluate the six-entry
// three-way blend, and REPLACE the driven bone's local transform outright
// (`docs/vtmb/procedural_bones.md`). The clip's channels for a driven bone are decoded, carried
// through, and discarded; this does not adjust the animated value.
//
// Skeletal controls evaluate in component space while the rule reads a local rotation, so the
// control's local is derived as `parentComponent⁻¹ · controlComponent` and the result is put back
// as `newLocal · parentComponent`.
USTRUCT()
struct FAnimNode_ElysiumAxisInterp : public FAnimNode_SkeletalControlBase
{
	GENERATED_BODY()

	FAnimNode_ElysiumAxisInterp() = default;

	void SetRig(TSharedPtr<const FElysiumCompositionRig> InRig);
	bool HasWork() const;

	void ResolveBones(const FBoneContainer& RequiredBones);
	void Apply(FComponentSpacePoseContext& Output);

	// How many rules resolved both their driven bone and their control against the current bone
	// container — what the debug surface reports, and what a "the correction is not running" report
	// is checked against.
	int32 NumResolvedRules() const { return Resolved.Num(); }

protected:
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,
		TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;

private:
	// One rule that resolved against this skeleton. A bank-retargeted body and a compact pose both
	// renumber, so the sidecar's own bone indices are never used to address the pose.
	struct FResolvedRule
	{
		FBoneReference Bone;
		FBoneReference Control;
		int32 RuleIndex = INDEX_NONE;
	};

	TSharedPtr<const FElysiumCompositionRig> Rig;
	// Ascending driven-bone order. Retail evaluates bone by bone into a live array, so a driven bone
	// whose parent is itself driven must see the corrected parent; walking in this order and reading
	// back the corrections already produced this pass reproduces that without touching the pose
	// mid-flight.
	TArray<FResolvedRule> Resolved;
	bool bResolved = false;
};
