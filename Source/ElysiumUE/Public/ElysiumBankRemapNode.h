#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Animation/AnimNodeBase.h"
#include "BoneContainer.h"

#include "ElysiumBankRemapNode.generated.h"

struct FElysiumBankRemap;

/**
 * Retail's per-body bank bone-remap (`vampire.dll FUN_100c67b0`), applied ONCE over the pose one
 * include closure composed -- the base channel's own resolved host plus its autolayer trio, or one
 * overlay slot's own closure -- reproduced here because Unreal's own
 * `EBoneTranslationRetargetingMode::OrientAndScale` covers only the similarity category and
 * applies it PER SEQUENCE, which double-counts the affine pure-translation branch
 * across a base+additive pair.
 *
 * **Translation only.** Every outcome (`FElysiumBankRemap`'s own header carries the arithmetic)
 * leaves rotation and scale untouched; this node writes nothing but a bone's local translation,
 * on exactly the bones the resolved table names.
 *
 * Bones are resolved BY NAME against the evaluating `FBoneContainer` in `CacheBones_AnyThread` --
 * never by an index a table might carry -- because a bank-retargeted skeleton and a compact
 * pose both renumber (the same rule `FElysiumCompositionRig` states for its own rules). The
 * resolution is cached there and only re-walked when the engine calls `CacheBones_AnyThread` again
 * (an LOD change, most commonly), so an ordinary frame pays one array walk sized to the entries the
 * table names, never the whole skeleton. A bone the table names that this LOD's compact pose does
 * not carry is dropped from the resolved list, silently -- an ordinary absence.
 *
 * The node owns no lookup of its own: `SetTable` is handed the already-resolved table for whatever
 * (mesh, source skeleton, retarget source) tuple this closure is currently playing, exactly as
 * `FAnimNode_ElysiumAxisInterp::SetRig` is handed the whole composition rig. There is no sidecar and
 * no owner map to search here -- `UElysiumBipedAnimInstance` reads the playing asset's own
 * `RetargetSource`, asks `UElysiumAnimSubsystem::GetBankRemap` for the table that (mesh, source)
 * pair builds to, and re-calls `SetTable` only when that pair changes (a new bank starts posing the
 * base channel, or a different slot claim starts one) -- never every frame.
 */
USTRUCT(BlueprintInternalUseOnly)
struct ELYSIUMUE_API FAnimNode_ElysiumBankRemap : public FAnimNode_Base
{
	GENERATED_USTRUCT_BODY()

	/** The pose the closure composed -- the base channel's own trio, or one overlay slot's. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Links)
	FPoseLink Source;

	/** Max LOD this node runs at, the same contract every stock node in this graph carries. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Performance, meta = (DisplayName = "LOD Threshold"))
	int32 LODThreshold = INDEX_NONE;

	// The resolved table for this closure's current (mesh, source skeleton, retarget source) tuple,
	// kept alive as long as this node needs it -- `UElysiumAnimSubsystem::GetBankRemap` caches it
	// for the life of the game instance. Null is the ordinary "nothing to correct" case (no retarget
	// source on the
	// playing asset, or a body whose bind pose tracks that bank closely enough that every bone
	// copies) and leaves the incoming pose exactly as `Source` produced it. `RequiredBones` is passed
	// when a table changes after the graph's initial CacheBones pass; without that immediate resolve,
	// the node would keep empty resolved arrays until an unrelated LOD change cached bones again.
	void SetTable(TSharedPtr<const FElysiumBankRemap> InTable,
		const FBoneContainer*						  RequiredBones = nullptr);
	// Whether the resolved table has any bone to correct on this skeleton -- the debug surface's
	// question, and what a "the correction is not running" report is checked against.
	bool HasWork() const;
	// How many named bones resolved against the current bone container, by branch. Mirrors
	// `FAnimNode_ElysiumAxisInterp::NumResolvedRules` -- the debug surface's question, and what a
	// "a bone the table names but the pose lacks was skipped, not substituted" check reads.
	int32 NumResolvedTranslate() const { return ResolvedTranslate.Num(); }
	int32 NumResolvedSimilarity() const { return ResolvedSimilarity.Num(); }

	// Resolve every named bone against `RequiredBones` directly -- the door `CacheBones_AnyThread`
	// calls internally, mirroring `FAnimNode_ElysiumAxisInterp::ResolveBones`. Exposed publicly so a
	// caller (or a test) that already holds a `FBoneContainer` states it here without a compiled
	// graph, a `FAnimationCacheBonesContext` or an `FAnimInstanceProxy` in the way.
	void ResolveBones(const FBoneContainer& RequiredBones);
	// Apply the resolved correction to `Output.Pose` IN PLACE, without touching `Source` at all --
	// the door `Evaluate_AnyThread` calls after `Source.Evaluate(Output)`, and what a test that hands
	// in an already-composed pose calls directly.
	void Apply(FPoseContext& Output);

	virtual void  Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void  CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void  Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void  Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void  GatherDebugData(FNodeDebugData& DebugData) override;
	virtual int32 GetLODThreshold() const override { return LODThreshold; }

private:
	struct FResolvedTranslate
	{
		FBoneReference Bone;
		FVector		   Offset = FVector::ZeroVector;
	};
	struct FResolvedSimilarity
	{
		FBoneReference Bone;
		FQuat		   Rotation = FQuat::Identity;
		float		   Scale = 1.f;
	};

	TSharedPtr<const FElysiumBankRemap> Table;

	// Resolved against the CURRENT bone container by `CacheBones_AnyThread`. Order does not matter:
	// unlike a hierarchy composition (axis interpolation, split inheritance), a translation-only
	// correction on one named bone has no dependency on any other bone's result.
	TArray<FResolvedTranslate>	ResolvedTranslate;
	TArray<FResolvedSimilarity> ResolvedSimilarity;
};
