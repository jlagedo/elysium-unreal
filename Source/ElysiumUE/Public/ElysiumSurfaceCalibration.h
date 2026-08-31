#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "UObject/ObjectPtr.h"

#include "ElysiumSurfaceCalibration.generated.h"

class UTexture2D;

/**
 * One surface class's roughness/specular/metallic row (`docs/architecture/seam_map_material.md` →
 * "Import" → "Knob contract"). The class key is `$surfaceprop` when present, else the VMT's top
 * directory, else the shader family's default row (`seam_migration.md` 2026-08-31); `Name` is
 * that resolved key.
 */
USTRUCT(BlueprintType)
struct FElysiumSurfaceClassRow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Elysium|Surface")
	FName Name;

	/**
	 * The LUT texel this row writes, assigned once at seeding time (`SeedDefaultRows`) and never
	 * derived from the row's position in `Rows`. `RegenerateLut` writes each row at `Index`, and
	 * `IndexOf` answers `Index` rather than an array position, so an owner reordering or inserting
	 * rows in the details-panel grid can never remap an already-imported instance's
	 * `SurfaceClassIndex` out from under it.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Elysium|Surface")
	int32 Index = 0;

	UPROPERTY(EditAnywhere, Category = "Elysium|Surface", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Roughness = 0.6f;

	UPROPERTY(EditAnywhere, Category = "Elysium|Surface", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Specular = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Elysium|Surface", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Metallic = 0.0f;
};

/**
 * The per-surface-class roughness/specular/metallic table, edited as a grid and baked to a 128x1
 * lookup texture every V2 master samples by `SurfaceClassIndex`
 * (`docs/architecture/seam_map_material.md` → "Import" → "Knob contract";
 * `docs/project/seam_migration.md` 2026-08-31, "Calibration happens on knobs inside the editor").
 *
 * A saved asset, not transient: `/Game/ElysiumGenerated/Materials/V2/DA_SurfaceCalibration`, with
 * its `Lut` texture saved beside it at `T_SurfaceClassLUT` and hard-referenced from `Lut`, so
 * neither can go stale relative to the other. `RegenerateLut` is editor-only (`FTextureSource` is
 * C++-only, `Texture.h:197`) and creates the LUT package on first run when the data asset predates
 * a texture, so `pipeline/unreal/make_surface_knobs.py` can call it right after seeding the rows.
 */
UCLASS(BlueprintType)
class ELYSIUMUE_API UElysiumSurfaceCalibration : public UDataAsset
{
	GENERATED_BODY()

public:
	/** The 128-texel lookup's width; `SurfaceClassIndex` in every master is a row in `[0, MaxRows)`. */
	static constexpr int32 MaxRows = 128;

	UPROPERTY(EditAnywhere, Category = "Elysium|Surface", meta = (TitleProperty = "Name"))
	TArray<FElysiumSurfaceClassRow> Rows;

	/** The baked 128x1 BGRA8 lookup, saved beside this asset. */
	UPROPERTY(VisibleAnywhere, Category = "Elysium|Surface")
	TObjectPtr<UTexture2D> Lut;

	/** `ClassKey`'s row's `Index` (case-insensitive name match), or `INDEX_NONE` when no row names it. */
	int32 IndexOf(FName ClassKey) const;

	/**
	 * Rebuild `Lut` from `Rows`: one BGRA8 texel per row, written at that row's own `Index` rather
	 * than its position in `Rows` (B = Metallic, G = Specular, R = Roughness, A = 255), texels no
	 * row claims holding the class-table's own struct default (`FElysiumSurfaceClassRow{}` —
	 * roughness 0.6, specular 0.5, metallic 0), so an unindexed lookup reads a defined neutral
	 * value rather than zeroed memory. Fails when two rows share an `Index` or a `Name`, or when a
	 * row's `Index` falls outside `[0, MaxRows)`. Editor-only: outside the editor this sets
	 * `bOutOk` false with a reason and changes nothing.
	 *
	 * Python: `asset.regenerate_lut()` → `(ok, error)`.
	 */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Surfaces")
	void RegenerateLut(bool& bOutOk, FString& OutError);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/**
	 * Replace `Rows` with one default-valued row per name in `ClassNames`, in the given order,
	 * `Index` assigned `0..ClassNames.Num()-1` in that same order -- what
	 * `pipeline/unreal/make_surface_knobs.py` calls right after creating the data asset, seeded
	 * from the pipeline's `SURFACE_CLASSES` list (`default` already first there), so the editor
	 * and the importer agree on row order and index without either hand-authoring the other's
	 * data. Fails rather than truncating when `ClassNames` exceeds `MaxRows` or contains a
	 * duplicate name, so a corpus-list growth or collision is a loud stage failure and never a
	 * silently dropped or aliased class.
	 *
	 * Python: `unreal.ElysiumSurfaceCalibration.seed_default_rows(asset, class_names)` → `(ok, error)`.
	 */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Surfaces")
	static void SeedDefaultRows(UElysiumSurfaceCalibration* Calibration, const TArray<FString>& ClassNames,
		bool& bOutOk, FString& OutError);
};
