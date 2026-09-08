#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ElysiumSpriteActor.generated.h"

class UElysiumSpriteComponent;

/**
 * One `env_sprite` in the baked level: a plain actor whose root is the billboard component. The bake (`bake_map_v2.
 * _place_sprites`) writes the component's every value, the `CSprite::Spawn` hidden state, the
 * `elysium.sprite` / `elysium.ent=<index>` tags and `EntityIndex`; the runtime buckets it once
 * (`UElysiumMapVisuals::AdoptBakedLevel`) and the entity's inputs reach it through
 * `UElysiumMapVisuals::SetSpriteVisible`. It has no behaviour of its own.
 */
UCLASS()
class ELYSIUMUE_API AElysiumSpriteActor : public AActor
{
	GENERATED_BODY()

public:
	AElysiumSpriteActor();

	/** The root: the one billboard. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium")
	TObjectPtr<UElysiumSpriteComponent> Sprite;

	/** The entity's lump ordinal (`FElysiumEntityHandle::Index`), the same number the `elysium.ent` tag carries. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elysium")
	int32 EntityIndex = INDEX_NONE;
};
