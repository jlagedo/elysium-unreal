#include "Debug/ElysiumGymBuilder.h"

#if !UE_BUILD_SHIPPING

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumGym, Log, All);

namespace ElysiumGym
{

AActor* Spawn(UWorld* World, const FSpec& Spec, const FVector& Origin, bool bWithMeshes)
{
	if (!World || Spec.Placements.Num() == 0)
	{
		return nullptr;
	}

	AActor* Actor = World->SpawnActor<AActor>();
	if (!Actor)
	{
		return nullptr;
	}
#if WITH_EDITOR
	Actor->SetActorLabel(TEXT("ElysiumGym"));
#endif

	USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("GymRoot"));
	Actor->AddInstanceComponent(Root);
	Actor->SetRootComponent(Root);
	Root->RegisterComponent();
	Actor->SetActorLocation(Origin);

	UStaticMesh* Cube = nullptr;
	if (bWithMeshes)
	{
		Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	}

	for (const FPlacement& P : Spec.Placements)
	{
		const FName Name(*FString::Printf(TEXT("%s_%s"), *P.Lane.ToString(), *P.Tag.ToString()));

		UBoxComponent* Box = NewObject<UBoxComponent>(Actor, Name);
		Actor->AddInstanceComponent(Box);
		Box->SetupAttachment(Root);
		Box->SetBoxExtent(P.Extent, /*bUpdateOverlaps*/ false);
		Box->SetRelativeLocationAndRotation(P.Center, P.Rot);
		// The same profile the map's own brush bodies use, so the mover — which sweeps against the
		// hull's own object type — cannot tell a gym rung from a `.hulls` collider. That is the
		// point: the gym is where a threshold is measured, and it has to be measured against the
		// collision the game actually walks on.
		Box->SetCollisionProfileName(TEXT("BlockAll"));
		Box->SetGenerateOverlapEvents(false);
		// A synthetic room has no navmesh and wants none; a shape that affects navigation dirties
		// Recast tiles for nothing.
		Box->SetCanEverAffectNavigation(false);
		Box->SetMobility(EComponentMobility::Static);
		Box->SetHiddenInGame(!bWithMeshes);
		Box->RegisterComponent();

		if (Cube)
		{
			UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(
				Actor, FName(*FString::Printf(TEXT("%s_mesh"), *Name.ToString())));
			Actor->AddInstanceComponent(Mesh);
			Mesh->SetupAttachment(Box);
			Mesh->SetStaticMesh(Cube);
			Mesh->SetMobility(EComponentMobility::Static);
			// The engine cube is 100 cm on a side and centred, so an extent maps to a scale by 50.
			Mesh->SetRelativeScale3D(P.Extent / 50.0f);
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mesh->RegisterComponent();
		}
	}

	UE_LOG(LogElysiumGym, Log, TEXT("gym: %d solid(s) across %d lane(s) at %s"),
		Spec.Placements.Num(), Spec.Lanes.Num(), *Origin.ToCompactString());
	return Actor;
}

} // namespace ElysiumGym

#endif // !UE_BUILD_SHIPPING
