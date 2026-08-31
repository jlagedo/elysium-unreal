#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"

#include "ElysiumSurfacePropertyProvenance.generated.h"

class FJsonObject;

/**
 * Everything a `vtmb:surface-property:` unit knows that a UPhysicalMaterial has no slot for, and
 * everything about *where each value came from*, carried on the baked asset as AssetUserData so a
 * packaged game can read it and the editor can inspect it
 * (`docs/architecture/seam_map_surface_property.md` → "Import" → "Provenance").
 *
 * The record exists because a surface-property asset is a **flattened** view: 42 of the 63 entries
 * state only their deltas and inherit the rest, so an asset's Friction may have been authored four
 * entries up the chain. `FieldOrigins` is what makes that auditable — one row per field the stage
 * filled, naming the unit in the chain that declared it — and `BaseChain` is the chain itself,
 * root first.
 *
 * `uv run elysium import surface-properties` attaches one per asset through
 * `UElysiumPhysicalMaterial::ApplyJson`, from the sidecar the offline stage writes beside the
 * manifest. A material carries at most one: `AddAssetUserData` replaces an instance of the same
 * class.
 */
UCLASS(BlueprintType)
class ELYSIUMUE_API UElysiumSurfacePropertyProvenance : public UAssetUserData
{
	GENERATED_BODY()

public:
	// --- identity -----------------------------------------------------------------------------
	/** `vtmb:surface-property:<name>`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Identity") FString AssetId;
	/** The unit key: the table entry name folded to lower case. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Identity") FString Name;
	/** The entry name as `scripts/surfaceproperties.txt` spells it (`Kitchen_Pan`). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Identity") FString SourceName;
	/** Object path of the asset this record was applied to, as the manifest named it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Identity") FString AssetPath;
	/** The unit file below the export_v2 root, e.g. `surface-properties/canister.glb`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Identity") FString UnitGlb;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Identity") FString UnitSchemaVersion;
	/** sha256 of the whole GLB file: the back-pointer from asset to unit. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Identity") FString UnitSha256;
	/** The import lane's settings version the asset was authored under. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Identity") FString SettingsVersion;

	// --- inheritance --------------------------------------------------------------------------
	/**
	 * The `base` chain this asset was flattened over, **root first**, excluding the unit itself:
	 * `canister` carries `[metal, metalgrate, metalpanel]`. Empty for one of the 21 roots.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Inheritance") TArray<FString> BaseChain;
	/**
	 * Field path → the unit that declared the value the asset carries, for every field the stage
	 * filled. Keys are the sidecar's own paths (`physics.friction`, `movement.jumpFactor`,
	 * `footsteps.left`, `impacts.bullet.norm`, `sounds.impact`, `gameMaterial`), so a reader can
	 * walk from an asset field back to the entry that authored it. A field no unit in the chain
	 * declares has no row.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Inheritance") TMap<FString, FString> FieldOrigins;

	// --- what the decode found ------------------------------------------------------------------
	/**
	 * The unit's own anomalies, one line each (`repeated-scalar-key key=friction offset=42`).
	 * A repeated *scalar* key resolves to its last value and is recorded here rather than failing;
	 * a repeated pool key is a variation pool and is not an anomaly at all.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Coverage") TArray<FString> Anomalies;
	/** The unit's byte-ledger coverage over its own entry span; 100 for every shipped unit. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Coverage") float CoveragePercent = 0.0f;
	/** Keys the unit could not resolve; a non-empty list failed the export, so this is empty here. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Coverage") TArray<FString> Unresolved;
	/** Keys the table's closed vocabulary has no field for. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Coverage") TArray<FString> Unsupported;

	/** Fill this record from a parsed sidecar object. Missing keys keep their defaults. */
	void FromJson(const TSharedRef<FJsonObject>& Object);
};
