#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "ElysiumSpriteComponent.generated.h"

class UMaterialInterface;

/**
 * One `env_sprite` billboard.
 *
 * The bake writes every field: the imported `MI_`'s per-blend child, the size in Source units
 * (`scale x texture`), `rendercolor`/`renderamt`, the entity's `rendermode` and `renderfx`, and
 * the VMT's `parallel_upright`. The proxy (`FElysiumSpriteSceneProxy`, the task's one piece of
 * rendering code) draws one camera-facing quad per view, applies Source's glow rule for
 * rendermode 3/9 off the Sprites settings page, and gates the blend by a per-sprite GPU occlusion
 * query -- the visible fraction of a small box grid at the origin, one frame late, smoothed at
 * VtMB's own `r_glowfadein`/`r_glowfadeout`. Nothing here ticks: visibility is the actor's
 * hidden state, written by the `env_sprite` leaf through the map actor.
 */
UCLASS(ClassGroup = Elysium, meta = (BlueprintSpawnableComponent))
class ELYSIUMUE_API UElysiumSpriteComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UElysiumSpriteComponent();

	/** The `MI_Sprite_<material>_<blend>` child of the imported sprite `MI_` (vertex colour and alpha on, the mode's blend). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elysium")
	TObjectPtr<UMaterialInterface> Material;

	/** `scale x texture` in Source units; a fixed-size card is this x 2.54 cm, a rendermode-3 corona this x dist / 200. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elysium")
	FVector2D SizeInches = FVector2D(64.0, 64.0);

	/** `rendercolor` (RGB) and `renderamt` (A). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elysium")
	FColor Color = FColor::White;

	/** The entity's `rendermode`: 3 / 9 take the glow rule, everything else draws plain. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elysium")
	int32 RenderMode = 0;

	/** The entity's `renderfx`; 14 (NoDissipation) skips the glow's distance term. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elysium")
	int32 RenderFx = 0;

	/** `$spriteorientation parallel_upright`: a yaw-only billboard about world Z. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elysium")
	bool bUpright = false;

	/**
	 * The sprite model's frame count: the base texture's TTH frames, what `VEngineServer014` slot 26
	 * answers `CSprite::Spawn` 0x1042e550 for a sprite model (`*(model+0xb4)`), so the leaf writes
	 * `m_flMaxFrame = Frames - 1` (L0-r013). A bake older than the field reads 1, a one-frame sprite.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elysium")
	int32 Frames = 1;

	bool IsGlow() const;

	//~ UPrimitiveComponent
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const override;
};
