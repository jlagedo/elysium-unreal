#pragma once

#include "CoreMinimal.h"
#include "Animation/BoneSocketReference.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"

#include "ElysiumAnimNodes.generated.h"

struct FElysiumCompositionRig;

// VtMB's two composition stages as Unreal skeletal controls (roadmap CAP7.2).
//
// Both derive `FAnimNode_SkeletalControlBase` and both run in the slot a post-process Anim
// Blueprint occupies — after the graph has blended locals, before skinning — which is retail's own
// order. This runtime has no Anim Blueprint asset (`UElysiumNpcAnimInstance` is a native anim
// instance with no graph), so that slot is the tail of `FElysiumNpcAnimProxy::Evaluate`, and the
// nodes are driven through `ResolveBones` + `Apply` rather than through pose links.
//
// **Their order is load-bearing**: split inheritance first, then axis interpolation. A graph that
// runs them the other way produces a different skeleton, because the split bone is `Bip01 Spine1`
// and every driven arm bone hangs off it.
//
// Both are safe to evaluate on an animation worker thread: they read a shared immutable rig held by
// `TSharedPtr`, resolve every bone index once in `InitializeBoneReferences`, and touch no UObject
// and no game-thread state while evaluating.
//
// The engine's own `FAnimNode_PoseDriver` is the wrong tool even though its shape matches. It
// interpolates target poses with a radial basis function rather than the sign-selected three-way
// slerp the rule uses, so it would approximate a stage that reproduces retail to 1e-4.

// Stage 1 — `Flags & 0x2`: rotation from the component root rather than the parent, translation
// from the parent (`docs/vtmb/animation_and_movers.md` A.4a).
//
// In component space the entity transform divides out, so retail's
//
//     rotation(boneToWorld[i])    = rotation(rootToWorld) * rotation(L)
//     translation(boneToWorld[i]) = TransformPoint(boneToWorld[parent[i]], p[i])
//
// is exactly "replace the composed rotation with the bone's own local rotation and leave the
// translation alone" — the ordinary hierarchy already puts the translation where the second line
// wants it. That is the whole of this node.
USTRUCT()
struct FAnimNode_ElysiumSplitInheritance : public FAnimNode_SkeletalControlBase
{
	GENERATED_BODY()

	FAnimNode_ElysiumSplitInheritance() = default;

	// Install the body's rig; null clears it. Call from the game thread through the anim instance,
	// which blocks on any in-flight evaluation first.
	void SetRig(TSharedPtr<const FElysiumCompositionRig> InRig);
	// Whether this stage has any bone to correct on this body.
	bool HasWork() const;

	// Resolve every bone index against a bone container, once. Driven from the proxy's CacheBones,
	// which is the callback the container change actually arrives on.
	void ResolveBones(const FBoneContainer& RequiredBones);
	// Evaluate into Output and write the result back. No-op when the stage has nothing to do or the
	// component's LOD is past LODThreshold.
	void Apply(FComponentSpacePoseContext& Output);

protected:
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,
		TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;

private:
	TSharedPtr<const FElysiumCompositionRig> Rig;
	// Ascending compact-pose order, which is what LocalBlendCSBoneTransforms requires and what
	// keeps a parent resolved before a child if a rig ever nests two split bones.
	TArray<FBoneReference> Bones;
	bool bResolved = false;
};

// Stage 2 — `ProcType == 1`: read the control bone's LOCAL rotation, evaluate the six-entry
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
