#include "ElysiumPlacedModelActor.h"

#include "Animation/AnimSequence.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"

AElysiumPlacedModelActor::AElysiumPlacedModelActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);

	CollisionProxy = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CollisionProxy"));
	CollisionProxy->SetupAttachment(SceneRoot);
	CollisionProxy->SetVisibility(false, true);
	CollisionProxy->SetHiddenInGame(true, true);
	CollisionProxy->SetMobility(EComponentMobility::Static);

	SkeletalVisual = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("SkeletalVisual"));
	SkeletalVisual->SetupAttachment(SceneRoot);
	SkeletalVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkeletalVisual->SetMobility(EComponentMobility::Static);
	SkeletalVisual->SetVisibility(false, true);
}

bool AElysiumPlacedModelActor::ConfigureRest(USkeletalMesh* SkeletalMesh,
	UAnimSequence* RestSequence, UStaticMesh* StaticMesh, bool bSolid)
{
	if (!SkeletalMesh || !RestSequence || !StaticMesh)
	{
		return false;
	}
	CollisionProxy->SetStaticMesh(StaticMesh);
	CollisionProxy->SetCollisionProfileName(bSolid ? TEXT("BlockAll") : TEXT("ElysiumPickOnly"));
	SkeletalVisual->SetSkeletalMeshAsset(SkeletalMesh);

	for (const FStaticMaterial& Material : StaticMesh->GetStaticMaterials())
	{
		const int32 Slot = SkeletalVisual->GetMaterialIndex(Material.MaterialSlotName);
		if (Slot != INDEX_NONE && Material.MaterialInterface)
		{
			SkeletalVisual->SetMaterial(Slot, Material.MaterialInterface);
		}
	}

	SkeletalVisual->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	SkeletalVisual->SetAnimation(RestSequence);
	SkeletalVisual->SetPosition(0.0f, false);
	SkeletalVisual->SetPlayRate(0.0f);
	SkeletalVisual->TickAnimation(0.0f, false);
	SkeletalVisual->RefreshBoneTransforms();
	SkeletalVisual->SetComponentTickEnabled(false);
	SkeletalVisual->SetVisibility(true, true);
	return true;
}
