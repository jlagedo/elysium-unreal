#include "ElysiumSpriteActor.h"

#include "ElysiumSpriteComponent.h"

AElysiumSpriteActor::AElysiumSpriteActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Sprite = CreateDefaultSubobject<UElysiumSpriteComponent>(TEXT("Sprite"));
	SetRootComponent(Sprite);
}
