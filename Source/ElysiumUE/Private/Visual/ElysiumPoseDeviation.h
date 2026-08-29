#pragma once

#include "CoreMinimal.h"
#include "ReferenceSkeleton.h"

// How far an evaluated pose is from another one.
//
// **This measures the one failure an animation graph has that logs nothing.** A blend-list pin left
// dead, an asset pin left null, a request that resolved nothing projected anyway: each of them
// evaluates a player node with nothing to play, and a player node with nothing to play answers the
// skeleton's bind pose. The package still compiles, the generator still exports, the tiers still
// pass, and the Content Browser preview runs no anim graph at all — so the bone transforms are the
// only observable there is.
//
// Two callers share the comparator and not the reference. `Elysium.Content.PlayerGraphInstance`
// takes its reference by evaluating the graph *before* publishing anything, which is the bind pose
// by construction because a body that has held nothing cannot hold. A live panel has no such moment
// and must build the reference from the skeleton, which is what `FillRefPoseComponentSpace` is for.
namespace ElysiumPose
{
	struct FDeviation
	{
		// Bones whose rotation differs by more than the threshold.
		int32 MovedBones = 0;
		// The largest single difference, in degrees.
		float MaxDegrees = 0.0f;
	};

	// Compare two component-space pose arrays.
	//
	// **Bone 0 is skipped**: in component space the root carries the actor transform, so a body that
	// merely walked somewhere would read as a changed pose. The 0.5 degree threshold is what
	// separates a bone that is posed from one that is arithmetic noise away from where it started.
	inline FDeviation Measure(const TArray<FTransform>& A, const TArray<FTransform>& B,
		float ThresholdDegrees = 0.5f)
	{
		FDeviation Out;
		const int32 Num = FMath::Min(A.Num(), B.Num());
		for (int32 Bone = 1; Bone < Num; ++Bone)
		{
			const float Degrees = FMath::RadiansToDegrees(
				A[Bone].GetRotation().AngularDistance(B[Bone].GetRotation()));
			if (Degrees > ThresholdDegrees)
			{
				++Out.MovedBones;
			}
			Out.MaxDegrees = FMath::Max(Out.MaxDegrees, Degrees);
		}
		return Out;
	}

	// The skeleton's own bind pose, in component space, so it can be compared against whatever
	// `USkeletalMeshComponent::GetComponentSpaceTransforms` returns.
	//
	// The reference skeleton stores parent-relative transforms and guarantees a parent precedes its
	// child, so one forward pass composes the whole rig.
	inline void FillRefPoseComponentSpace(const FReferenceSkeleton& RefSkeleton,
		TArray<FTransform>& Out)
	{
		const TArray<FTransform>& Local = RefSkeleton.GetRefBonePose();
		Out.Reset();
		Out.SetNum(Local.Num());
		for (int32 Bone = 0; Bone < Local.Num(); ++Bone)
		{
			const int32 Parent = RefSkeleton.GetParentIndex(Bone);
			Out[Bone] = Out.IsValidIndex(Parent) ? Local[Bone] * Out[Parent] : Local[Bone];
		}
	}
}
