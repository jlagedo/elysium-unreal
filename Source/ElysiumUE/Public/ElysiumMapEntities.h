#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumMapEntities.generated.h"

// One map's entity table as cooked content (R4.1, `docs/architecture/seam_map_map_entities.md` ->
// "Import"). This is a transport change and nothing else: the rows below carry exactly what the
// `<map>.ents` document carries, in lump order, and deserialize into exactly the plain
// `FElysiumEntityDef` array the JSON reader has always produced. No field is added, dropped,
// retyped or re-derived on the way through.
//
// Authored by `pipeline/unreal/import_map_entities.py` from the stage
// (`elysium_pipeline.importers.map_entities`), which runs the R3.2 producer's own entity join over
// the published GLB units and asserts parity against the `.ents` file this asset replaces.

// One row of an entity's `outputs[]`, verbatim from the sidecar's own seven-field split. `Times` is
// stored as authored -- the retail `0` -> unlimited rewrite is `Deserialize`'s, so this asset and
// the sidecar hold the same number and exactly one owner normalises it.
USTRUCT()
struct FElysiumMapEntityOutputRow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Output") FString Name;
	UPROPERTY(EditAnywhere, Category = "Output") FString Target;
	UPROPERTY(EditAnywhere, Category = "Output") FString Input;
	UPROPERTY(EditAnywhere, Category = "Output") FString Param;
	UPROPERTY(EditAnywhere, Category = "Output") float Delay = 0.f;
	UPROPERTY(EditAnywhere, Category = "Output") int32 Times = -1;
	UPROPERTY(EditAnywhere, Category = "Output") FString Python;
};

// One brush entity's convex volume, entity-local Unreal centimetres (world = def origin + vertex).
// A nested struct rather than a flat array because a brush entity carries several hulls and the
// per-hull boundary is what the collision cooker consumes.
USTRUCT()
struct FElysiumMapEntityHullRow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Hull") TArray<FVector> Vertices;
};

// One entity, one lump block. The array position is the lump ordinal: the running game's entity
// handle and a save key, so rows are never sorted, filtered or renumbered.
USTRUCT()
struct FElysiumMapEntityRow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Entity") FString Classname;
	UPROPERTY(EditAnywhere, Category = "Entity") FString TargetName;

	// Unreal cm, as the exporter emitted it. A 3D-skybox row's origin is the *miniature*
	// coordinate; the placement transform is applied by `Deserialize`, not stored here.
	UPROPERTY(EditAnywhere, Category = "Entity") FVector Origin = FVector::ZeroVector;

	// The raw keyvalues minus classname/targetname, authored spelling, last-wins.
	UPROPERTY(EditAnywhere, Category = "Entity") TMap<FString, FString> Keys;

	UPROPERTY(EditAnywhere, Category = "Brush") int32 Model = INDEX_NONE;
	UPROPERTY(EditAnywhere, Category = "Brush") TArray<FElysiumMapEntityHullRow> Hulls;
	UPROPERTY(EditAnywhere, Category = "Brush") int32 Contents = 0;
	UPROPERTY(EditAnywhere, Category = "Brush") bool bBlocksPlayer = false;
	UPROPERTY(EditAnywhere, Category = "Brush") FString BrushMesh;
	// R6.4: `cull_max_cm`, 0 when the `.ents` row carries none.
	UPROPERTY(EditAnywhere, Category = "Brush") float CullMaxCm = 0.f;

	UPROPERTY(EditAnywhere, Category = "Entity") TArray<float> ElevatorFloors;
	UPROPERTY(EditAnywhere, Category = "Entity") bool bStartHidden = false;
	UPROPERTY(EditAnywhere, Category = "Entity") bool bSky = false;

	UPROPERTY(EditAnywhere, Category = "Body") FString ModelMesh;

	// The placement rotation, four doubles rather than one `FQuat`. Measured on the real corpus,
	// 2026-09-01: a reflected `FQuat` property does NOT survive a package save at its authored
	// width -- an authored 0.707107 reads back 0.7071070075035095, the binary32 nearest -- while
	// `FVector` (three doubles) round-trips exactly. Storing the components as plain doubles is
	// what makes this transport lossless against the `.ents` document's own numbers, which is the
	// whole claim the asset makes; `Elysium.Content.MapEntities.FieldParity` is what caught it.
	UPROPERTY(EditAnywhere, Category = "Body") double ModelQuatX = 0.0;
	UPROPERTY(EditAnywhere, Category = "Body") double ModelQuatY = 0.0;
	UPROPERTY(EditAnywhere, Category = "Body") double ModelQuatZ = 0.0;
	UPROPERTY(EditAnywhere, Category = "Body") double ModelQuatW = 1.0;

	UPROPERTY(EditAnywhere, Category = "Body") FVector HingeAxis = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Entity") TArray<FElysiumMapEntityOutputRow> Outputs;

	// The stored components as the rotation the def carries. Read verbatim -- the exporter already
	// emits Unreal space and a normalized quaternion, so nothing is re-normalized here.
	FQuat ModelRotation() const { return FQuat(ModelQuatX, ModelQuatY, ModelQuatZ, ModelQuatW); }
};

// The per-map entity table: `/ElysiumBaked/<map>/DA_<map>_Entities`, one asset per map, beside the
// map's own `.umap`. `FElysiumContentPaths::BakedMapEntities` is the one path accessor; its Python
// twin is `elysium_pipeline.importers.map_entities.asset_path`.
UCLASS(BlueprintType)
class ELYSIUMUE_API UElysiumMapEntities : public UDataAsset
{
	GENERATED_BODY()

public:
	// The map stem this table belongs to (the `.ents` document's own `map` field).
	UPROPERTY(EditAnywhere, Category = "Map") FString MapName;

	// One row per lump block, in lump order, with no drops and no reorders.
	UPROPERTY(EditAnywhere, Category = "Map") TArray<FElysiumMapEntityRow> Entities;

	// Fill `Out` with this table's defs. The two reads the JSON path performs at parse time happen
	// here, so the asset stores what was authored and one owner normalises it: an authored `times`
	// of 0 becomes -1 (unlimited), and a `bSky` row's origin and hulls are carried through the
	// 3D-skybox placement transform `world(v) = scale * (v - skyOrigin)` (hulls take the scale, not
	// the translation). Pass the map's `.sky` values, or leave the identity to read the miniature's
	// raw coordinates unchanged -- exactly `FElysiumEntityDefs::Parse`'s contract.
	void Deserialize(FElysiumEntityDefs& Out, float SkyScale = 1.f,
		const FVector& SkyOrigin = FVector::ZeroVector) const;
};

// Where one map's defs came from. Returned by `Load` so a caller can log or assert the transport
// it actually got rather than the one it assumed.
enum class EElysiumEntityDefSource : uint8
{
	None,      // neither an asset nor a readable sidecar
	Asset,     // /ElysiumBaked/<map>/DA_<map>_Entities
	Sidecar,   // <map>.ents
};

namespace ElysiumEntityDefSource
{
	// The one entry point for "give me this map's defs". A map listed in
	// `UElysiumMapTransportSettings::MapsOnNewTransport` (R4.6) tries the baked asset first, falling
	// back to the `.ents` sidecar if it turns out missing; an unlisted map goes straight to the
	// sidecar (`docs/architecture/seam_map_map_entities.md` -> "Import" -> "Cutover").
	ELYSIUMUE_API EElysiumEntityDefSource Load(const FString& MapName, FElysiumEntityDefs& Out,
		float SkyScale = 1.f, const FVector& SkyOrigin = FVector::ZeroVector);

	// "asset" / "sidecar" / "none", for logs and test messages.
	ELYSIUMUE_API const TCHAR* ToString(EElysiumEntityDefSource Source);
}
