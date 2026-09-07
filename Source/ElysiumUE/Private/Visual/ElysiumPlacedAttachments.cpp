#include "Visual/ElysiumPlacedAttachments.h"

#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "ReferenceSkeleton.h"

bool ElysiumPlacedAttachments::RefPoseTransform(const USkeletalMesh* Mesh, FName Socket,
	FTransform& Out)
{
	if (!Mesh || Socket.IsNone())
	{
		return false;
	}
	const USkeletalMeshSocket* Found = Mesh->FindSocket(Socket);
	if (!Found)
	{
		return false;
	}

	const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
	int32 BoneIndex = RefSkeleton.FindBoneIndex(Found->BoneName);
	if (BoneIndex == INDEX_NONE)
	{
		return false;
	}

	// The socket's own bone-local transform first, then every parent's ref-pose local transform up
	// to the root. `USkeletalMeshSocket::GetSocketLocalTransform` is not available off a component,
	// so the pair is composed here.
	FTransform Result(Found->RelativeRotation, Found->RelativeLocation, Found->RelativeScale);
	const TArray<FTransform>& RefPose = RefSkeleton.GetRefBonePose();
	while (BoneIndex != INDEX_NONE)
	{
		if (!RefPose.IsValidIndex(BoneIndex))
		{
			return false;
		}
		Result *= RefPose[BoneIndex];
		BoneIndex = RefSkeleton.GetParentIndex(BoneIndex);
	}

	Out = Result;
	return true;
}
