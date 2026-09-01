#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ElysiumPropSkins.generated.h"

class UMaterialInterface;

// VtMB's alternate **skin families** as baked content.
//
// A `.mdl` carries a skin table (`NumSkinRefs`@308 / `NumSkinFamilies`@312 / `SkinIndex`@316):
// `skinTable[family][skinref]` names the texture each mesh draws with, so a skin is a material
// remap over one model's existing slots -- a traffic light's red/walk/flashing/yellow states, a
// doorknob's locked/unlocked, a laser emitter's armed/idle. `StudioMesh.Material` is a *skinref*,
// not a texture index; family 0 is the identity row on every readable model in the install, which
// is why decoding it as a texture index produced the right skin-0 materials and nothing else.
//
// The offline half writes `props/<stem>.skins` (one line per family, only the slots that differ);
// the bake resolves those names against the material instances it authored and stores the result
// here, so the runtime binds real baked MICs -- a swapped skin renders at exactly the quality the
// base skin does. Only families that change something are stored, and only the slots they change.
USTRUCT()
struct FElysiumSkinOverride
{
	GENERATED_BODY()

	// The mesh material slot this family repaints (`UStaticMesh::GetMaterialIndex`).
	UPROPERTY(EditAnywhere) FName SlotName;

	// What it repaints to -- a material instance from the map's own prop material package.
	UPROPERTY(EditAnywhere) TObjectPtr<UMaterialInterface> Material = nullptr;
};

// One skin family: the slots it repaints relative to the model's authored (family 0) set. A family
// that changes nothing is stored empty, so the array index stays the skin number VtMB uses.
USTRUCT()
struct FElysiumSkinFamily
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere) TArray<FElysiumSkinOverride> Overrides;
};

// Every family of one prop model, keyed by the OBJ stem the `.props` sidecar and the `model_mesh`
// annotation already name (so a caller that can build the body can find its skins).
USTRUCT()
struct FElysiumPropSkinModel
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere) FName Stem;

	// Index = VtMB's skin number. Element 0 is the authored set and is always empty. A family
	// identical to family 0 produces no row either, so this array may be shorter than FamilyCount
	// -- trimmed after the last family that actually repaints something
	// (docs/architecture/seam_map_model.md -> "Import" -> "Skins table").
	UPROPERTY(EditAnywhere) TArray<FElysiumSkinFamily> Families;

	// The stem's true family count (`materialBindings.skinFamilies.Num()`), so `Find` can clamp an
	// out-of-range placement index to the last family without loading the mesh -- "the skin-index
	// clamp is engine behaviour, and the import reproduces it" (VtMB clamps rather than falling
	// back to 0 or refusing to draw). Left at its default (0) on an asset authored before this
	// field existed, which `Find` reads as "no clamp table for this stem" and falls back to the
	// plain array bound it always used, so an old asset's behaviour is unchanged.
	UPROPERTY(EditAnywhere) int32 FamilyCount = 0;
};

// The corpus prop-skin table. Authored either by pipeline/unreal/bake_map.py (the legacy shared
// bake, one asset for the whole corpus under `/ElysiumBaked/Shared/Meshes`) or by
// pipeline/unreal/import_models.py (the V2 lane, `/ElysiumBaked/Meshes/DA_ElysiumPropSkins`,
// docs/architecture/seam_map_model.md -> "Import" -> "Skins table") -- one asset either way, never
// per map, so a single load reaches every alternate material in the corpus and the hard references
// keep them all reachable from a level that places any of them.
UCLASS(BlueprintType)
class ELYSIUMUE_API UElysiumPropSkinSet : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere) TArray<FElysiumPropSkinModel> Models;

	// The overrides for one model's skin family, or null when the model has no alternate families,
	// the resolved family repaints nothing, or (family 0) it IS the authored set. `Family` is
	// clamped to `Model.FamilyCount - 1` first when it names an index at or past `FamilyCount` --
	// VtMB's `skin` keyfield/input is an unclamped int write, so a placement can and does (59 of
	// them in the corpus) name a family past the model's own count, and the engine clamps to the
	// last family rather than falling back to 0 or refusing to draw. A model with no `FamilyCount`
	// on record (an asset authored before that field existed) skips the clamp and is bounded by the
	// array alone, exactly as before.
	const FElysiumSkinFamily* Find(FName Stem, int32 Family) const;

private:
	// Stem -> index into Models, built on first lookup. Transient: it is derived from Models and
	// must not be saved into the asset.
	UPROPERTY(Transient) mutable TMap<FName, int32> Index;
	mutable bool bIndexed = false;
};
