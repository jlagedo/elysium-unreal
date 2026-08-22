#include "Visual/ElysiumRenderedBone.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "SkeletalRenderPublic.h"

bool ElysiumRenderedBone::RenderedSkinningMatrices(const USkeletalMeshComponent& Mesh, FName Bone,
	FMatrix& OutRefToLocal, FMatrix& OutDrawnCS)
{
	const USkeletalMesh* const Asset = Mesh.GetSkeletalMeshAsset();
	const FSkeletalMeshObject* const MeshObject = Mesh.GetMeshObject();
	const int32 Index = Asset != nullptr && MeshObject != nullptr
			&& MeshObject->HaveValidDynamicData()
		? Asset->GetRefSkeleton().FindBoneIndex(Bone) : INDEX_NONE;
	if (Index == INDEX_NONE)
	{
		return false;
	}
	const TConstArrayView<FMatrix44f> RefToLocals = MeshObject->GetReferenceToLocalMatrices();
	const TArray<FMatrix44f>& InvBind = Asset->GetRefBasesInvMatrix();
	if (!RefToLocals.IsValidIndex(Index) || !InvBind.IsValidIndex(Index))
	{
		return false;
	}
	// RefToLocal = InvBind * ComponentSpace, so the bind matrix on the left recovers the rendered
	// component-space bone.
	OutRefToLocal = FMatrix(RefToLocals[Index]);
	OutDrawnCS = FMatrix(InvBind[Index].Inverse()) * OutRefToLocal;
	return true;
}

bool ElysiumRenderedBone::RenderedWorldTransform(const USkeletalMeshComponent& Mesh, FName Bone,
	FTransform& OutWorld)
{
	FMatrix RefToLocal, DrawnCS;
	if (!RenderedSkinningMatrices(Mesh, Bone, RefToLocal, DrawnCS))
	{
		return false;
	}
	OutWorld = FTransform(DrawnCS * Mesh.GetComponentTransform().ToMatrixWithScale());
	return true;
}
