#pragma once

#include "CoreMinimal.h"
#include "Animation/BoneSocketReference.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "BoneControllers/AnimNode_AnimDynamics.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
// By value: the cloth node holds a live tuning as a member, so the rig's own header rather than a
// forward declaration. The rig itself is still only ever held by shared pointer.
#include "Visual/ElysiumClothRig.h"

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

// The simulated-garment spike — one `FAnimNode_AnimDynamics` chain per lattice column, hosted
// together so the proxy installs, updates and evaluates them as a unit (`Visual/ElysiumClothRig.h`).
//
// This one reproduces neither retail secondary-motion path. VtMB's garment solver consumes an
// authored particle/constraint payload after ordinary skinning, independently from its custom
// hair/body bone-chain solver (`docs/vtmb/secondary_motion.md`). This approximation synthesises a
// bone lattice instead. It runs LAST, after the composition stage, so the
// simulation sees the finished skeleton rather than one still missing its corrections. The bone
// sets are disjoint in any case: the lattice is synthesised and no clip or procedural rule can
// name it.
//
// It is a container rather than another `FAnimNode_SkeletalControlBase` because AnimDynamics is
// per-chain, and a garment is ten of them. Three things the engine does for a graph-hosted node and
// cannot do for this one, all of which the proxy must therefore do by hand:
//
//  - The simulation timestep arrives ONLY through `UpdateInternal`, and the field it writes is
//    private — so `Update` has to run even though the stage above needs no update at all.
//  - `PreUpdate` is a game-thread pass the engine drives from the node list
//    `FAnimInstanceProxy::GetCustomNodes` reports. That list is gathered during
//    `InitializeAnimation`, which is strictly before any rig is installed, so reporting these
//    chains through it registers an empty array — and would hand the proxy pointers into a
//    `TArray` that `SetRig` later reallocates. The anim instance calls `PreUpdate` directly for
//    that reason.
//  - `PhysicsBodyDefinitions` must run root-first from `BoundBone` to `ChainEnd` or `InitPhysics`
//    rebuilds it from the reference skeleton and clones one prototype over every row, silently
//    flattening the per-row cone ramp. `Build` therefore lets the engine size the array through
//    `UpdateChainPhysicsBodyDefinitions` and only then writes the per-row values into it.
//
// Not wired: `ResetDynamics`, which reaches a node through the same registration `PreUpdate` does.
// A teleported body's garment lurches once and settles rather than being reset with it.

// One chain. Derived solely to reach `EvaluateSkeletalControl_AnyThread`, because the public
// `EvaluateComponentSpace_AnyThread` cannot be used on a node that is not in a graph:
//
//   - `FPoseLinkBase::Update` treats an unlinked link as a no-op, which is why `Update_AnyThread`
//     is safe to call directly.
//   - `FComponentSpacePoseLink::EvaluateComponentSpace` does the OPPOSITE — an unlinked link hits
//     `else { Output.ResetToRefPose(); }`. Evaluating through the public entry point therefore
//     discards the blended, composed pose and simulates the garment over a bind-pose body.
//
// So this evaluates the control directly and blends the result in, exactly as
// `FAnimNode_ElysiumAxisInterp::Apply` does for the same reason.
struct FElysiumClothChainNode : public FAnimNode_AnimDynamics
{
	void ApplyTo(FComponentSpacePoseContext& Output);
};
USTRUCT()
struct FAnimNode_ElysiumCloth
{
	GENERATED_BODY()

	FAnimNode_ElysiumCloth() = default;

	// Install the body's garment rig; null clears it and tears the chains down. Call from the game
	// thread through the anim instance, which blocks on any in-flight evaluation first.
	void SetRig(TSharedPtr<const FElysiumClothRig> InRig);
	bool HasWork() const;

	// Re-tune the live chains without rebuilding them. Same game-thread contract as SetRig.
	//
	// The tuning survives a rebuild — a body that re-caches its bones keeps whatever the last write
	// asked for rather than snapping back to the file. Every knob the engine re-reads per frame
	// lands on the next tick with the simulation's state intact; the two it bakes at `InitPhysics`
	// re-seat the chain instead, which is visible as the garment dropping from the pose again.
	void SetTuning(const FElysiumClothTuning& InTuning);
	const FElysiumClothTuning& GetTuning() const { return Tuning; }

	// Build the chains against this bone container and resolve every bone reference. Driven from
	// the proxy's CacheBones — the reference skeleton the body definitions need is only reachable
	// from here, so construction and resolution are one step.
	void CacheBones(const FAnimationCacheBonesContext& Context);
	// Advance the simulation clock. A chain never updated simulates at dt = 0 forever.
	void Update(const FAnimationUpdateContext& Context);
	// Simulate and write back. No-op while the rig is absent or the chains have not been built.
	void Apply(FComponentSpacePoseContext& Output);

	// The chains' game-thread pass: it reads the component's own AnimDynamics disable switch and
	// samples wind. Game thread only — it dereferences the anim instance's component and world.
	void PreUpdate(const UAnimInstance* Instance);

	// How many chains were built and resolved — what the debug surface reports, and what a "the
	// garment is not simulating" report is checked against.
	int32 NumChains() const { return Chains.Num(); }

private:
	void Build(const FBoneContainer& RequiredBones);
	// Write the tuned values over the built chains. Split from Build so the two have one owner
	// each: Build settles what a chain *is* (its bones, its body array, its locked axes), this
	// settles every number a slider can move.
	void ApplyTuning();

	TSharedPtr<const FElysiumClothRig> Rig;
	FElysiumClothTuning Tuning;
	// Rebuilt per bone container: a chain owns live simulation state bound to one skeleton, so it
	// cannot outlive the container it resolved against. Not a UPROPERTY — `FAnimNode_AnimDynamics`
	// holds no object references to keep alive, and the array is plain C++ state.
	TArray<FElysiumClothChainNode> Chains;
	bool bBuilt = false;
};
