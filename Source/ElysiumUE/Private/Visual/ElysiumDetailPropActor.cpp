#include "ElysiumDetailPropActor.h"

#include "Components/InstancedStaticMeshComponent.h"

AElysiumDetailPropActor::AElysiumDetailPropActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Instances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Instances"));
	SetRootComponent(Instances);
	Instances->SetMobility(EComponentMobility::Static);
	// VtMB's detail objects live in client.dll alone: nothing collides with one, nothing traces
	// against one, and VRAD never lit the world by one, so they cast nothing here either.
	Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Instances->SetCollisionProfileName(TEXT("NoCollision"));
	Instances->SetCastShadow(false);
	Instances->SetCanEverAffectNavigation(false);
}
