#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
// Reuses the two generic provenance-row structs the material lane already declared --
// `FElysiumProvenanceAnomaly` (`Kind` + `Extra`) and `FElysiumProvenanceNote` (`Reason` + `Extra`)
// are not material-specific despite living in that header; the model lane's
// `anomalies[]`/`omissions[]` rows are the same shape, and
// UHT forbids two reflected structs of the same name in one module, so this reuses rather than
// redeclares them.
#include "ElysiumMaterialProvenance.h"

#include "ElysiumModelProvenance.generated.h"

class FJsonObject;
class UStaticMesh;

/**
 * One row of `materialBindings.slots` plus this lane's own resolution.
 */
USTRUCT(BlueprintType)
struct FElysiumModelSlotProvenance
{
	GENERATED_BODY()

	/** Position in `materialBindings.slots` -- also the skin-table column and `UStaticMesh` slot index. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Model") int32 Index = 0;
	/** `safe_name(sourceName)`, disambiguated per "Identity and naming" -- the `UStaticMesh` slot name. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Model") FString SlotName;
	/** The studio texture name exactly as the MDL's skin table spelled it, before folding. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Model") FString SourceName;
	/** The VMT path the header's search paths resolved this slot to, when one was found. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Model") FString SourcePath;
	/** A `vtmb:material:` id, or a `vtmb:missing-material:<slot>:<name>` sentinel. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Model") FString MaterialAssetId;
	/** The landed V2 instance's object path (`MI_V2_Missing` for a sentinel slot). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Model") FString MaterialAsset;
	/** False only for a `vtmb:missing-material:` sentinel slot. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Model") bool Resolved = false;
	/** True when `MaterialAssetId` is a `vtmb:missing-material:` sentinel rather than a resolved id. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Model") bool IsSentinel = false;
};

/** One slot this skin family repaints away from family 0 -- one row of the skin table's own shape. */
USTRUCT(BlueprintType)
struct FElysiumModelSkinOverride
{
	GENERATED_BODY()

	/** The disambiguated slot name this override lands on, matching `FElysiumModelSlotProvenance::SlotName`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Model") FString SlotName;
	/** The landed V2 instance this family substitutes at `SlotName`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Model") FString MaterialAsset;
};

/**
 * One row of `materialBindings.skinFamilies[]`, as written into `DA_ElysiumPropSkins`.
 * Family 0 is the mesh's own
 * default slots and carries no row in the skin table itself, but is still recorded here so the
 * asset states its full family count without a second lookup.
 */
USTRUCT(BlueprintType)
struct FElysiumModelSkinFamilyProvenance
{
	GENERATED_BODY()

	/** The family index a placement's `skin` keyfield names (clamped per "Material binding"). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Model") int32 Family = 0;
	/** Only the slots that differ from family 0; empty when this family is identical to family 0. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Model") TArray<FElysiumModelSkinOverride> Overrides;
};

/** One row of `vtx.lods[]` plus the `switchPoints` -> `ScreenSize` mapping ("Geometry"). */
USTRUCT(BlueprintType)
struct FElysiumModelLodProvenance
{
	GENERATED_BODY()

	/** The Unreal LOD index after a dropped shadow LOD (`switchPoints == -1.0`) renumbers the chain. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Model") int32 Index = 0;
	/** The source `switchPoints` value this row was computed from. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Model") float SwitchPoint = 0.0f;
	/** `clamp(LodSwitchConstant / SwitchPoint, LodScreenSizeFloor, LodScreenSizeCeiling)`, monotone-corrected. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Model") float ScreenSize = 0.0f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Model") int32 Sections = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Model") int32 Triangles = 0;
	/** True for a `switchPoints == -1.0` shadow-LOD row this lane dropped rather than built. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Model") bool Dropped = false;
};

/** The static-header box a no-`.phy` model's bbox collision (and `noPhysicsSolidsBoxFallback`) is built from. */
USTRUCT(BlueprintType)
struct FElysiumModelHullBounds
{
	GENERATED_BODY()

	/** `mdl.header.hullMin` through `source_to_unreal`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Model") FVector Min = FVector::ZeroVector;
	/** `mdl.header.hullMax` through `source_to_unreal`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Model") FVector Max = FVector::ZeroVector;
};

/**
 * Everything a `vtmb:model:` unit's import decided that a `UStaticMesh` has no slot for, carried on
 * the baked asset as AssetUserData so a packaged game can read it and the editor can inspect it.
 * `UStaticMesh`
 * implements `IInterface_AssetUserData` directly, so no carrier class is needed the way
 * `UElysiumPhysicalMaterial` needed one for `UPhysicalMaterial`.
 *
 * `uv run elysium import models` attaches one per mesh through `ApplyJson`, from the provenance
 * sidecar the offline stage writes beside each unit, mirroring `UElysiumMaterialProvenance` and
 * `UElysiumTextureProvenance` exactly: outer the record to the mesh, replace rather than accumulate,
 * tolerate a missing optional key at its default, and fail closed only on a body that does not
 * parse as a JSON object.
 */
UCLASS(BlueprintType)
class ELYSIUMUE_API UElysiumModelProvenance : public UAssetUserData
{
	GENERATED_BODY()

public:
	// --- identity ---------------------------------------------------------------------------
	/** `vtmb:model:<path>`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Identity") FString AssetId;
	/** The unit's own model path below `models/`, `.mdl` included. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Identity") FString ModelPath;
	/** `static_stem("models/<path>.mdl")` -- the `SM_` name's own body, before the `SM_` prefix. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Identity") FString Stem;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Identity") FString UnitSchemaVersion;
	/** sha256 of the whole GLB file: the back-pointer from asset to unit. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Identity") FString UnitSha256;
	/**
	 * `sourceResolution.members[].sha256`, one entry per source member this unit reads (the MDL,
	 * both VTX variants, and the PHY when the model ships one) -- distinct from the material and
	 * texture lanes' single merged `SourceSha256`, because a model unit is assembled from several
	 * independently-resolved members ("Source closure").
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Identity") TArray<FString> SourceSha256;
	/** The import lane's settings version the asset was authored under (`elysium-model-import-v2`). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Identity") FString SettingsVersion;

	// --- identity classification ----------------------------------------------------------------
	/** `bank`, `static` or `skeletal` ("Unit identity"). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Identity") FString Shape;
	/** The unit's first path segment below `models/` (`character`, `items`, `scenery`, `weapons`, ...). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Identity") FString Family;
	/** `character-body`, `animation-bank`, `wield`, `view-model`, `ground-item`, `placed-prop`, `static-prop`, `include-only`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Identity") TArray<FString> Roles;

	// --- material binding -------------------------------------------------------------------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") TArray<FElysiumModelSlotProvenance> Slots;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") TArray<FElysiumModelSkinFamilyProvenance> SkinFamilies;

	// --- geometry -----------------------------------------------------------------------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Geometry") TArray<FElysiumModelLodProvenance> Lods;
	/** The opacity rule's result over every slot of every skin family, never just family 0. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Geometry") bool bNanite = false;
	/** The slot name that vetoed Nanite; empty when `bNanite` is true or nothing vetoed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Geometry") FString NaniteVetoSlot;
	/** The material asset that vetoed Nanite (translucent/additive/modulated/refractive, or `M_V2_Refract`/`M_V2_Water`). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Geometry") FString NaniteVetoMaterial;

	// --- collision ----------------------------------------------------------------------------
	/** `phy` (per-solid convex hulls) or `bbox` (the header hull box) -- "Collision". */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Collision") FString CollisionMode;
	/** The PHY ledge count this mesh's simple collision was built from; 0 under the bbox rule. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Collision") int32 HullCount = 0;
	/** The cooked convex-shape count; must equal `HullCount` under the `phy` mode or the unit failed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Collision") int32 ShapeCount = 0;
	/** `physics.solids[0].properties.mass`, authored; 0 when the mesh ships no `.phy`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Collision") float MassKg = 0.0f;
	/** The header hull box the bbox rule (or `noPhysicsSolidsBoxFallback`) used; zero under `phy` mode. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Collision") FElysiumModelHullBounds HullBounds;

	// --- surface property -----------------------------------------------------------------------
	/** The resolved, lower-cased surface name; `default` when nothing resolved. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Surface") FString SurfaceProperty;
	/** `physSolid`, `mdlHeader` or `default` -- which resolution tier answered ("Surface properties"). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Surface") FString SurfacePropertySource;
	/** `/ElysiumBaked/SurfaceProperties/PM_<name>`, bound to `UBodySetup::PhysMaterial`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Surface") FString PhysMaterial;

	// --- content ------------------------------------------------------------------------------
	/** Every row this unit's own export carries plus whatever this lane added (`degenerate-normal`, `missingMaterialSentinel`, `skinIndexClamped`, `multiSubmodelBakedZero`, `noPhysicsSolidsBoxFallback`, `surfacePropertyUnknown`, `duplicateSlotName`, ...). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Content") TArray<FElysiumProvenanceAnomaly> Anomalies;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Content") TArray<FElysiumProvenanceNote> Omissions;
	/**
	 * The unit's own `coverage` object, carried field-by-field and stringified the same tolerant way
	 * `FElysiumMaterialParameter::Value` is: the coverage object's exact shape is owned by the unit's
	 * own contract and by whatever summary this lane's own sidecar publishes, neither
	 * of which this reader pins a field list to, so nothing in it is silently dropped either way.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Content") TMap<FString, FString> Coverage;

	/**
	 * Parse a provenance sidecar (the JSON `uv run elysium import models` stages beside each unit),
	 * create the record with `Mesh` as its outer, fill it, and attach it -- replacing any record
	 * already on the mesh. Returns null and a reason when the JSON does not parse or is not an
	 * object; individual missing keys are tolerated and left at their defaults, exactly as
	 * `UElysiumMaterialProvenance::ApplyJson` does.
	 *
	 * Python: `unreal.ElysiumModelProvenance.apply_json(mesh, text)` -> `(record, error)`.
	 */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Models")
	static UElysiumModelProvenance* ApplyJson(UStaticMesh* Mesh, const FString& Json, FString& OutError);

	/** The record a mesh carries, or null. */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Models")
	static const UElysiumModelProvenance* Find(const UStaticMesh* Mesh);

	/**
	 * Publish `ElysiumAssetId`, `ElysiumModelShape` and `ElysiumNanite` as package metadata on the
	 * mesh's package, beside `ElysiumRecipe` (`Config/DefaultGame.ini`'s `MetaDataTagsForAssetRegistry`),
	 * so the Content Browser filters on them without loading the asset. Editor-only data: outside the
	 * editor this returns false with a reason and changes nothing.
	 *
	 * Success travels in an out-parameter rather than the return value on purpose: the editor's
	 * Python binding turns a `bool` return plus out-parameters into "None on false, the
	 * out-parameters on true" (PyGenUtil.cpp, PackReturnValues), which drops the error text. A
	 * `void` with `(bool&, FString&)` reaches Python as the explicit `(ok, error)` tuple.
	 */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Models")
	static void StampRegistryTags(UStaticMesh* Mesh, bool& bOutStamped, FString& OutError);

	/** The metadata keys `StampRegistryTags` writes, in the order it writes them. */
	static const FName TagAssetId;
	static const FName TagModelShape;
	static const FName TagNanite;

	/** Fill this record from a parsed sidecar object. Missing keys keep their defaults. */
	void FromJson(const TSharedRef<FJsonObject>& Object);
};
