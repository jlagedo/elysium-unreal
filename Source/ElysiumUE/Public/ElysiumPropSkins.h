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

	// Index = VtMB's skin number. Element 0 is the authored set and is always empty.
	UPROPERTY(EditAnywhere) TArray<FElysiumSkinFamily> Families;
};

// The map's whole prop-skin table, authored by pipeline/unreal/bake_map.py next to the prop meshes. One asset
// per map rather than one per model: a map carries ~23 multi-family models, so a single asset loads
// once at map load and costs one lookup, and the hard references keep every alternate material
// reachable from the level.
UCLASS(BlueprintType)
class ELYSIUMUE_API UElysiumPropSkinSet : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere) TArray<FElysiumPropSkinModel> Models;

	// The overrides for one model's skin family, or null when the model has no alternate families,
	// the family is out of range, or it repaints nothing. Out-of-range is not an error: VtMB's
	// `skin` keyfield/input is an unclamped int write, so a map can name a family the model lacks.
	const FElysiumSkinFamily* Find(FName Stem, int32 Family) const;

private:
	// Stem -> index into Models, built on first lookup. Transient: it is derived from Models and
	// must not be saved into the asset.
	UPROPERTY(Transient) mutable TMap<FName, int32> Index;
	mutable bool bIndexed = false;
};
