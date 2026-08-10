#include "Visual/ElysiumAnimNodes.h"

#include "Visual/ElysiumCompositionRig.h"

#include "Animation/AnimInstanceProxy.h"

namespace
{
	// The component-space transform a bone has at this point in the pass, preferring a correction
	// this pass has already produced over what the incoming pose holds. Retail composes bone by bone
	// into one live array; this is that array, restricted to the handful of bones being rewritten.
	// By value: the search list grows as the pass runs, and the pose computes component space
	// lazily, so neither source is safe to hold a reference into.
	FTransform CurrentComponentSpace(FComponentSpacePoseContext& Output,
		const TArray<FBoneTransform>& Produced, const FCompactPoseBoneIndex BoneIndex)
	{
		for (const FBoneTransform& Transform : Produced)
		{
			if (Transform.BoneIndex == BoneIndex)
			{
				return Transform.Transform;
			}
		}
		return Output.Pose.GetComponentSpaceTransform(BoneIndex);
	}
}

// --- axis interpolation ------------------------------------------------------------------------

void FAnimNode_ElysiumAxisInterp::SetRig(TSharedPtr<const FElysiumCompositionRig> InRig)
{
	Rig = MoveTemp(InRig);
	Resolved.Reset();
	bResolved = false;
}

bool FAnimNode_ElysiumAxisInterp::HasWork() const
{
	return Rig.IsValid() && !Rig->AxisRules.IsEmpty();
}

void FAnimNode_ElysiumAxisInterp::ResolveBones(const FBoneContainer& RequiredBones)
{
	InitializeBoneReferences(RequiredBones);
}

void FAnimNode_ElysiumAxisInterp::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	Resolved.Reset();
	bResolved = true;
	if (!HasWork())
	{
		return;
	}

	Resolved.Reserve(Rig->AxisRules.Num());
	for (int32 RuleIndex = 0; RuleIndex < Rig->AxisRules.Num(); ++RuleIndex)
	{
		const FElysiumAxisInterpRule& Rule = Rig->AxisRules[RuleIndex];
		FResolvedRule Entry;
		Entry.RuleIndex = RuleIndex;
		Entry.Bone = FBoneReference(Rule.Bone);
		Entry.Control = FBoneReference(Rule.Control);
		Entry.Bone.Initialize(RequiredBones);
		Entry.Control.Initialize(RequiredBones);
		// A rule whose driven bone or control is not in this LOD's bone container simply does not
		// run — the bone it would correct is not being posed either.
		if (Entry.Bone.IsValidToEvaluate(RequiredBones)
			&& Entry.Control.IsValidToEvaluate(RequiredBones))
		{
			Resolved.Add(MoveTemp(Entry));
		}
	}
	Resolved.Sort([&RequiredBones](const FResolvedRule& A, const FResolvedRule& B)
	{
		return A.Bone.GetCompactPoseIndex(RequiredBones) < B.Bone.GetCompactPoseIndex(RequiredBones);
	});
}

bool FAnimNode_ElysiumAxisInterp::IsValidToEvaluate(const USkeleton*, const FBoneContainer&)
{
	return !Resolved.IsEmpty();
}

void FAnimNode_ElysiumAxisInterp::Apply(FComponentSpacePoseContext& Output)
{
	if (!bResolved)
	{
		InitializeBoneReferences(Output.Pose.GetPose().GetBoneContainer());
	}
	if (Resolved.IsEmpty()
		|| (Output.AnimInstanceProxy != nullptr && !IsLODEnabled(Output.AnimInstanceProxy)))
	{
		return;
	}

	// Named for what it is, and not `BoneTransforms`: the base class keeps a scratch array of
	// that name for the graph path this node does not take.
	TArray<FBoneTransform> Corrections;
	EvaluateSkeletalControl_AnyThread(Output, Corrections);
	if (!Corrections.IsEmpty())
	{
		Output.Pose.LocalBlendCSBoneTransforms(Corrections, 1.f);
	}
}

void FAnimNode_ElysiumAxisInterp::EvaluateSkeletalControl_AnyThread(
	FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	const FBoneContainer& RequiredBones = Output.Pose.GetPose().GetBoneContainer();

	for (const FResolvedRule& Entry : Resolved)
	{
		const FElysiumAxisInterpRule& Rule = Rig->AxisRules[Entry.RuleIndex];

		const FCompactPoseBoneIndex ControlIndex = Entry.Control.GetCompactPoseIndex(RequiredBones);
		const FCompactPoseBoneIndex ControlParent =
			Output.Pose.GetPose().GetParentBoneIndex(ControlIndex);

		const FQuat ControlComponent =
			CurrentComponentSpace(Output, OutBoneTransforms, ControlIndex).GetRotation();
		const FQuat ControlLocal = ControlParent.IsValid()
			? CurrentComponentSpace(Output, OutBoneTransforms, ControlParent)
				.GetRotation().Inverse() * ControlComponent
			: ControlComponent;

		// The rule produces the driven bone's local transform outright — the animated one is
		// discarded, not adjusted.
		const FTransform NewLocal = Rig->EvaluateRule(Rule, ControlLocal);

		const FCompactPoseBoneIndex BoneIndex = Entry.Bone.GetCompactPoseIndex(RequiredBones);
		const FCompactPoseBoneIndex ParentIndex = Output.Pose.GetPose().GetParentBoneIndex(BoneIndex);
		const FTransform NewComponent = ParentIndex.IsValid()
			? NewLocal * CurrentComponentSpace(Output, OutBoneTransforms, ParentIndex)
			: NewLocal;

		OutBoneTransforms.Add(FBoneTransform(BoneIndex, NewComponent));
	}
}
