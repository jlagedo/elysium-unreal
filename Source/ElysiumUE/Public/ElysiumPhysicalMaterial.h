#pragma once

#include "CoreMinimal.h"
#include "Interfaces/Interface_AssetUserData.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "UObject/ObjectPtr.h"

#include "ElysiumPhysicalMaterial.generated.h"

class FJsonObject;
class UElysiumSurfacePropertyProvenance;

/**
 * One weapon class's row of VtMB's impact matrix: three ordered variation pools, one per damage
 * outcome (`docs/vtmb/surface_properties.md` → "Impacts — a weapon × outcome matrix").
 *
 * Each pool is a list because a key repeated inside one entry block is an alternate the engine
 * picks between at random, not an overriding assignment. Entries are `vtmb:sound:` asset IDs for
 * now; the sound slice flips them to hard `USoundWave` references.
 */
USTRUCT(BlueprintType)
struct FElysiumImpactOutcomes
{
	GENERATED_BODY()

	/** The hit the target soaked. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Surface") TArray<FString> Soak;
	/** An ordinary hit. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Surface") TArray<FString> Norm;
	/** A critical hit. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Surface") TArray<FString> Crit;
};

/**
 * One entry of `scripts/surfaceproperties.txt` as an Unreal physical material, with the fields
 * Unreal has no slot for (`docs/architecture/seam_map_surface_property.md` → "Import").
 *
 * **Identity is the asset.** There is one of these per surface entry, at
 * `/ElysiumBaked/SurfaceProperties/PM_<name>`, and a hit's `PhysMaterial` *is* this object — so
 * nothing needs an `EPhysicalSurface` slot per entry, and the 63 entries do not consume 63 of the
 * engine's 62 surface rows. `SurfaceType` instead carries VtMB's own compact material class, the
 * single-letter `gamematerial` code, which 26 entries declare and 17 distinct values cover; the
 * rows are declared in `Config/DefaultEngine.ini` as `VtmbGameMaterial_<letter>`. An entry with no
 * `gamematerial` anywhere in its chain keeps `SurfaceType_Default`.
 *
 * **The values are flattened.** 42 of the 63 entries state only their deltas, so the stage walks
 * each `base` chain root-first and this asset carries the resolved value for every field. Which
 * unit in the chain supplied each one is in the attached `UElysiumSurfacePropertyProvenance`.
 *
 * Native slots this fills from the unit: `Friction` ← `friction`, `Restitution` ← `elasticity`
 * (clamped to 0–1; the raw value stays in `RawElasticity`), `Density` ← `density`, already
 * converted at the stage from the table's kg/m³ to this engine property's own g/cm³ (the raw
 * authored value stays in `RawDensity`, mirroring `RawElasticity`).
 */
UCLASS(BlueprintType)
class ELYSIUMUE_API UElysiumPhysicalMaterial : public UPhysicalMaterial, public IInterface_AssetUserData
{
	GENERATED_BODY()

public:
	// --- physics beyond the engine's slots ------------------------------------------------------
	/**
	 * `thickness`, on 10 entries. When present the material is **not volumetrically solid**: its
	 * volume is surface area × this, and the space beneath it is air. Zero means the entry (and
	 * its whole chain) declared none, which is the solid case.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Surface") float Thickness = 0.0f;
	/**
	 * `elasticity` exactly as the table wrote it. The shipped range is 0.001–2, and `Restitution`
	 * is the 0–1 clamp of this, so a value the engine cannot hold is recorded rather than lost.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Surface") float RawElasticity = 0.0f;
	/**
	 * `density` exactly as the table wrote it, in kg/m³ (water is 1000). `Density` is the engine's
	 * own g/cm³ (`UPhysicalMaterial::Density`, consumed as `Density * 0.001` in `BodySetup.cpp`),
	 * so this is the authored number the g/cm³ conversion was computed from -- mirroring
	 * `RawElasticity`.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Surface") float RawDensity = 0.0f;

	// --- movement -------------------------------------------------------------------------------
	/**
	 * `maxspeedfactor`. Declared on `default` alone, and `default` is a childless root that no
	 * entry bases on -- not an implicit parent -- so the other 62 entries never inherit it; they
	 * keep this member initialiser, which is `default`'s own value.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Surface") float MaxSpeedFactor = 1.0f;
	/** `jumpfactor`. Same as `MaxSpeedFactor`: `default` declares it alone and inherits to no one. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Surface") float JumpFactor = 1.0f;
	/**
	 * `climbable`. Declared on `default` alone; no entry bases on `default`, so no shipped surface
	 * carries a declared `true` and every surface keeps this member initialiser.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Surface") bool bClimbable = false;

	// --- audio references (asset IDs until the sound slice) ---------------------------------------
	/** `stepleft` pool, in source order: `vtmb:sound:` IDs. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Surface") TArray<FString> FootstepsLeft;
	/** `stepright` pool, in source order: `vtmb:sound:` IDs. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Surface") TArray<FString> FootstepsRight;
	/** The weapon × outcome impact matrix, keyed `bullet`, `metal`, `wood`, `blade`, `fist`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Surface") TMap<FString, FElysiumImpactOutcomes> Impacts;
	/** The pre-matrix `bulletimpact` key: a legacy pool with no outcome axis. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Surface") TArray<FString> BulletImpactLegacy;
	/** `impact`: `vtmb:sound-script:` names in `scripts/game_sounds_surfaceproperties.txt`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Surface") TArray<FString> SoundScriptImpact;
	/** `scrape`: `vtmb:sound-script:` names in `scripts/game_sounds_surfaceproperties.txt`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Surface") TArray<FString> SoundScriptScrape;

	// --- identity --------------------------------------------------------------------------------
	/** The single-letter `gamematerial` code, resolved through the chain; empty when none. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Surface") FString GameMaterial;
	/** The entry name as the table spells it (`Kitchen_Pan`); the asset is named from the fold. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Surface") FString SourceName;
	/** `vtmb:surface-property:<name>` — what a model's `SurfacePropIndex` and a VMT's
	 * `$surfaceprop` resolve to. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Surface") FString AssetId;
	/** The `base` chain, root first, excluding this unit. Empty for one of the 21 roots. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Surface") TArray<FString> BaseChain;

	/**
	 * Apply a staged surface-property sidecar to `Material`: fill every native and Elysium field
	 * from the flattened values, and attach a `UElysiumSurfacePropertyProvenance` record built
	 * from the same document — replacing any record the material already carries, so a re-import
	 * replaces rather than accumulates.
	 *
	 * Refuses a null material, a material that is not a `UElysiumPhysicalMaterial`, and a body
	 * that does not parse as JSON or is not a JSON object. Individual missing keys are tolerated:
	 * a missing or `null` **physics/movement scalar** resets that field to its class default (the
	 * CDO value) rather than keeping whatever the asset already had, because the stage emits every
	 * one of those keys on every apply — a scalar that is absent means no unit in the chain
	 * declares it, not that the field grew after this sidecar was written, so an asset a source
	 * stops declaring a value for reverts rather than keeping a stale one. Every **list, map and
	 * chain** is replaced by what the sidecar carries — a stale variation pool surviving a
	 * re-import would be worse than an empty one — and `SurfaceType` is derived from the sidecar's
	 * row name every time.
	 *
	 * Python: `unreal.ElysiumPhysicalMaterial.apply_json(material, text)` → `(ok, error)`.
	 * Success travels in an out-parameter rather than the return value on purpose: the editor's
	 * Python binding turns a `bool` return plus out-parameters into "None on false, the
	 * out-parameters on true" (PyGenUtil.cpp, PackReturnValues), which drops the error text.
	 * A `void` with `(bool&, FString&)` reaches Python as the explicit `(ok, error)` tuple.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Surfaces")
	static void ApplyJson(UPhysicalMaterial* Material, const FString& Json, bool& bOutOk, FString& OutError);

	/** The provenance record a physical material carries, or null. */
	UFUNCTION(BlueprintCallable, Category="Elysium|Surfaces")
	static const UElysiumSurfacePropertyProvenance* FindProvenance(const UPhysicalMaterial* Material);

	/**
	 * Publish `ElysiumAssetId`, `ElysiumGameMaterial` and `ElysiumSourceName` as package metadata
	 * on the material's package. `Config/DefaultGame.ini` lists those three under
	 * `MetaDataTagsForAssetRegistry`, so a saved asset surfaces them as asset-registry tags the
	 * Content Browser filters on without loading it. Editor-only data: outside the editor this
	 * returns false with a reason and changes nothing.
	 *
	 * The out-parameter convention is `ApplyJson`'s, for the same reason.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Surfaces")
	static void StampRegistryTags(UPhysicalMaterial* Material, bool& bOutStamped, FString& OutError);

	/** The metadata keys `StampRegistryTags` writes, in the order it writes them. */
	static const FName TagAssetId;
	static const FName TagGameMaterial;
	static const FName TagSourceName;

	//~ Begin IInterface_AssetUserData
	// UPhysicalMaterial carries no AssetUserData of its own, so the provenance carrier is
	// implemented here, exactly the way UTexture and UStaticMesh implement it.
	virtual void AddAssetUserData(UAssetUserData* InUserData) override;
	virtual void RemoveUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass) override;
	virtual UAssetUserData* GetAssetUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass) override;
	virtual const TArray<UAssetUserData*>* GetAssetUserDataArray() const override;
	//~ End IInterface_AssetUserData

private:
	/**
	 * Fill this material from a parsed sidecar object. A missing or `null` physics/movement scalar
	 * resets to the class default; every list, map and chain is replaced wholesale.
	 */
	void FromJson(const TSharedRef<FJsonObject>& Object);

	UPROPERTY()
	TArray<TObjectPtr<UAssetUserData>> AssetUserData;
};
