#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ElysiumDetailPropActor.generated.h"

class UInstancedStaticMeshComponent;

/**
 * One map's placements of one detail model (R6.3, `docs/architecture/seam_map_map.md` ->
 * "Detail props (R6.3)"): the `dprp` game lump's records for that model, one instance each in
 * lump order, on a single instanced static mesh component. The bake (`bake_map_v2._place_details`)
 * writes everything -- the corpus mesh, every instance transform, the cull range off the Models
 * page, the per-instance `swayAmount / 255` as custom data float 0, the scene-fog stamp and the
 * `MI_DetailSway_*` slot overrides -- and the runtime only counts it (`UElysiumMapVisuals`) and
 * hides it with the static props. A detail object has no entity, no collision and no input in
 * VtMB (the lump is client-only), so this actor has no behaviour of its own.
 */
UCLASS()
class ELYSIUMUE_API AElysiumDetailPropActor : public AActor
{
	GENERATED_BODY()

public:
	AElysiumDetailPropActor();

	/** The root: every record of `ModelId` on this map, instance `k` being the model's `k`-th record. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium")
	TObjectPtr<UInstancedStaticMeshComponent> Instances;

	/** Canonical model unit identity; the native mesh is already bound on Instances. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elysium")
	FName ModelId;
};
