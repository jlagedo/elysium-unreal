#include "ElysiumPlacedModelActor.h"

#include "Animation/AnimSequence.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "ElysiumFog.h"                     // ElysiumLightStyle::StampUnstyledDefault -- CPD slot 6 neutral
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "ElysiumCharacterProvenance.h"
#include "ElysiumSkeletalMesh.h"
#include "Visual/ElysiumNpcVisual.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumPlacedModelActor, Log, All);

AElysiumPlacedModelActor::AElysiumPlacedModelActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);
	// The root carries its children's mobility: a Static component under a Movable parent never
	// takes the parent's transform, which strands the whole placement at the world origin.
	SceneRoot->SetMobility(EComponentMobility::Static);

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
	// R7.4 (G6): the bake stamps the visual it is handed through `set_fog`; stamped here as well
	// so the rest body reads full brightness on every path that configures it.
	ElysiumLightStyle::StampUnstyledDefault(SkeletalVisual);

	const bool bNativeModel = SkeletalMesh->IsA<UElysiumSkeletalMesh>();
	SkeletalVisual->EmptyOverrideMaterials();
	// Legacy map assets keep their existing material adapter until the map cutover. Native
	// meshes already bind their own skinned material routes and complete skin table.
	if (!bNativeModel)
	{
		for (const FStaticMaterial& Material : StaticMesh->GetStaticMaterials())
		{
			const int32 Slot = SkeletalVisual->GetMaterialIndex(Material.MaterialSlotName);
			if (Slot != INDEX_NONE && Material.MaterialInterface)
			{
				SkeletalVisual->SetMaterial(Slot, Material.MaterialInterface);
			}
		}
	}

	SkeletalVisual->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	SkeletalVisual->SetAnimation(RestSequence);
	SkeletalVisual->SetPosition(0.0f, false);
	SkeletalVisual->SetPlayRate(0.0f);
	SkeletalVisual->TickAnimation(0.0f, false);
	SkeletalVisual->RefreshBoneTransforms();
	if (bNativeModel)
	{
		const auto* Data = UElysiumCharacterProvenance::Find(SkeletalMesh);
		if (!Data || Data->SourceGarmentCount < 0 || Data->ClothAssets.Num() != Data->SourceGarmentCount)
		{
			UE_LOG(LogElysiumPlacedModelActor, Warning, TEXT("%s: native placed model lacks complete garment provenance"), *SkeletalMesh->GetPathName());
			return false;
		}
		const auto* Garment = ElysiumNpcVisual::InstallGarment(SkeletalVisual, Data->AssetId);
		if (Data->SourceGarmentCount > 0 && !Garment)
		{
			UE_LOG(LogElysiumPlacedModelActor, Warning, TEXT("%s: native placed model garments could not be installed"), *Data->AssetId);
			return false;
		}
	}
	SkeletalVisual->SetComponentTickEnabled(false);
	SkeletalVisual->SetVisibility(true, true);
	return true;
}

FString AElysiumPlacedModelActor::RefreshGarmentMaterials()
{
	FString Error;
	if (!ElysiumNpcVisual::SyncGarmentMaterials(SkeletalVisual, Error))
		UE_LOG(LogElysiumPlacedModelActor, Warning, TEXT("%s: %s"), *GetPathName(), *Error);
	return Error;
}
