#include "ElysiumBankRemapNode.h"

#include "Visual/ElysiumBankRemap.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimStats.h"
#include "Animation/AnimTrace.h"

void FAnimNode_ElysiumBankRemap::SetTable(TSharedPtr<const FElysiumBankRemap> InTable,
	const FBoneContainer*													  RequiredBones)
{
	Table = MoveTemp(InTable);
	// The stale resolution from whatever source tuple this closure held before must not survive a
	// table swap. A table installed after the initial CacheBones pass is resolved immediately by the
	// game-thread caller; an early install with no valid container resolves on CacheBones itself.
	ResolvedTranslate.Reset();
	ResolvedSimilarity.Reset();
	if (RequiredBones != nullptr && RequiredBones->IsValid())
	{
		ResolveBones(*RequiredBones);
	}
}

bool FAnimNode_ElysiumBankRemap::HasWork() const
{
	return !ResolvedTranslate.IsEmpty() || !ResolvedSimilarity.IsEmpty();
}

void FAnimNode_ElysiumBankRemap::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Initialize_AnyThread)
	FAnimNode_Base::Initialize_AnyThread(Context);
	Source.Initialize(Context);
}

void FAnimNode_ElysiumBankRemap::ResolveBones(const FBoneContainer& RequiredBones)
{
	ResolvedTranslate.Reset();
	ResolvedSimilarity.Reset();

	if (!Table.IsValid())
	{
		// No retarget source on the playing asset, or a body whose bind pose tracks that bank
		// closely enough that every bone copies -- the ordinary "nothing to correct" case. A
		// resolvable-but-unbuildable pair already warned at the door that tried it
		// (`UElysiumAnimSubsystem::GetBankRemap`), so this is not a second chance to say the same
		// thing.
		return;
	}

	ResolvedTranslate.Reserve(Table->Translate.Num());
	for (const FElysiumBankRemapTranslateEntry& Entry : Table->Translate)
	{
		FBoneReference Ref(Entry.Bone);
		Ref.Initialize(RequiredBones);
		// A bone the table names that this LOD's compact pose does not carry is an ordinary
		// absence -- the bone the correction would touch is not being posed either -- not a failure.
		if (Ref.IsValidToEvaluate(RequiredBones))
		{
			ResolvedTranslate.Add(FResolvedTranslate{ MoveTemp(Ref), Entry.Offset });
		}
	}
	ResolvedSimilarity.Reserve(Table->Similarity.Num());
	for (const FElysiumBankRemapSimilarityEntry& Entry : Table->Similarity)
	{
		FBoneReference Ref(Entry.Bone);
		Ref.Initialize(RequiredBones);
		if (Ref.IsValidToEvaluate(RequiredBones))
		{
			ResolvedSimilarity.Add(FResolvedSimilarity{ MoveTemp(Ref), Entry.Rotation, Entry.Scale });
		}
	}
}

void FAnimNode_ElysiumBankRemap::Apply(FPoseContext& Output)
{
	if (ResolvedTranslate.IsEmpty() && ResolvedSimilarity.IsEmpty())
	{
		return;
	}

	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();

	// TRANSLATE: `p += (b - a)`, baked as `Offset`. O(entries), never a walk of the skeleton.
	for (const FResolvedTranslate& Entry : ResolvedTranslate)
	{
		const FCompactPoseBoneIndex Index = Entry.Bone.GetCompactPoseIndex(Bones);
		Output.Pose[Index].AddToTranslation(Entry.Offset);
	}
	// SIMILARITY: `p = shortestArc(a -> b).RotateVector(p) * (|b| / |a|)`. Rotation and scale are
	// untouched on every bone this node ever writes -- only `SetTranslation` is called below.
	for (const FResolvedSimilarity& Entry : ResolvedSimilarity)
	{
		const FCompactPoseBoneIndex Index = Entry.Bone.GetCompactPoseIndex(Bones);
		FTransform&					Bone = Output.Pose[Index];
		Bone.SetTranslation(Entry.Rotation.RotateVector(Bone.GetTranslation()) * Entry.Scale);
	}
}

void FAnimNode_ElysiumBankRemap::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(CacheBones_AnyThread)
	Source.CacheBones(Context);
	ResolveBones(Context.AnimInstanceProxy->GetRequiredBones());
}

void FAnimNode_ElysiumBankRemap::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Update_AnyThread)
	Source.Update(Context);
}

void FAnimNode_ElysiumBankRemap::Evaluate_AnyThread(FPoseContext& Output)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Evaluate_AnyThread)
	Source.Evaluate(Output);

	if (Output.AnimInstanceProxy != nullptr && !IsLODEnabled(Output.AnimInstanceProxy))
	{
		return;
	}
	Apply(Output);
}

void FAnimNode_ElysiumBankRemap::GatherDebugData(FNodeDebugData& DebugData)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(GatherDebugData)
	FString DebugLine = DebugData.GetNodeName(this);
	DebugLine += FString::Printf(TEXT("(Table: %s, Translate: %d, Similarity: %d)"),
		Table.IsValid() ? TEXT("yes") : TEXT("none"), ResolvedTranslate.Num(),
		ResolvedSimilarity.Num());

	DebugData.AddDebugItem(DebugLine);
	Source.GatherDebugData(DebugData);
}
