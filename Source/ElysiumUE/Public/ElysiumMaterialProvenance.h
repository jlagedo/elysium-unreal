#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "UObject/SoftObjectPath.h"

#include "ElysiumMaterialProvenance.generated.h"

class FJsonObject;
class UMaterialInterface;

/**
 * One row of the unit's `parameters`, in source order (`docs/architecture/seam_map_material.md` →
 * "Import" → "Provenance"). Carried for every parameter the unit authors, including the ones this
 * lane does not consume, so no VMT key is silently lost between the sidecar and the asset.
 */
USTRUCT(BlueprintType)
struct FElysiumMaterialParameter
{
	GENERATED_BODY()

	/** Position in the unit's own `parameters` array. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") int32 Index = 0;
	/** The VMT block this key was authored in (root, a proxy block, ...). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString Block;
	/** The key as this lane resolved it (`$` stripped, PascalCased where it binds a parameter). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString Key;
	/** The key exactly as the VMT spelled it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString SourceKey;
	/** The value, carried as text regardless of `ValueType` (a provenance record, not a binding). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString Value;
	/** `string`, `float`, `int`, `vector`, ... as the unit's `parameters` entry declared it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString ValueType;
	/** The unit's own byte offset for this parameter, for provenance back to the source VMT text. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") int32 Offset = 0;
};

/** One row of the unit's `blocks`: a VMT block's own identity and inheritance. */
USTRUCT(BlueprintType)
struct FElysiumMaterialBlock
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString Name;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString SourceName;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString Path;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString Parent;
};

/**
 * One row of the unit's `proxies` (`docs/architecture/seam_map_material.md` → "Import" → "Proxy
 * policy"): a Source proxy chain instance and where it landed -- a material-graph node, a
 * runtime-factory binding, or provenance only.
 */
USTRUCT(BlueprintType)
struct FElysiumMaterialProxy
{
	GENERATED_BODY()

	/** The proxy kind (`sine`, `texturescroll`, `animatedtexture`, ...) as this lane resolved it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString Kind;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString Name;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString SourceName;
	/** Indices into `Parameters` this proxy instance reads or writes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") TArray<int32> ParameterIndices;
	/** Free-form proxy arguments not carried as ordinary parameters (`camoboundingboxmin`, ...). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") TMap<FString, FString> Arguments;
	/** `graph-node`, `runtime-factory` or `provenance-only` (the proxy policy's three destinations). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString Destination;
};

/** One row of `shaderResolution.resolvedPrograms`: a concrete pixel+vertex program pair this unit admits. */
USTRUCT(BlueprintType)
struct FElysiumMaterialProgram
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString PixelShader;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString VertexShader;
	/** The static-switch condition this pair resolves under (`default` for the unconditional pick). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString Condition;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString DrawPass;
};

/** One row of `textureBindings`: a texture parameter's resolution, plus the linear-twin choice. */
USTRUCT(BlueprintType)
struct FElysiumMaterialTextureBinding
{
	GENERATED_BODY()

	/** The master's texture parameter this binds (`BaseTexture`, `EnvMask`, ...). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString Parameter;
	/** The VMT's own texture path value. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString Value;
	/** `color`, `mask`, `normal` or `linear`: the role this binding was resolved under. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString Kind;
	/** The resolved `vtmb:texture:` asset id, or empty when unresolved. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString Asset;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") bool Resolved = false;
	/** Whether the `_linear` twin was bound instead of the colour asset (a role-conflict texture). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") bool UsedLinearTwin = false;
};

/** One row of `dependencies`: a non-texture asset reference this unit carries (a phys material, a crack material, ...). */
USTRUCT(BlueprintType)
struct FElysiumMaterialDependency
{
	GENERATED_BODY()

	/** `material`, `surface-property`, `cubemap-origin`, ... */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString Role;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString Parameter;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString Asset;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") bool Resolved = false;
};

/**
 * Everything a `vtmb:material:` unit knows that a `UMaterialInstance` has no slot for, carried on
 * the baked asset as AssetUserData so a packaged game can read it and the editor can inspect it
 * (`docs/architecture/seam_map_material.md` → "Import" → "Provenance"). `UMaterialInterface`
 * implements `IInterface_AssetUserData` directly, so no carrier class is needed the way
 * `UElysiumPhysicalMaterial` needed one for `UPhysicalMaterial`.
 *
 * `uv run elysium import materials` attaches one per instance through `ApplyJson`, from the
 * provenance sidecar the offline stage writes beside each unit. A material carries at most one:
 * `AddAssetUserData` replaces an instance of the same class, so a re-import replaces rather than
 * accumulates.
 */
UCLASS(BlueprintType)
class ELYSIUMUE_API UElysiumMaterialProvenance : public UAssetUserData
{
	GENERATED_BODY()

public:
	// --- identity ---------------------------------------------------------------------------
	/** `vtmb:material:<path>`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Identity") FString AssetId;
	/** The unit's own material path below `materials/`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Identity") FString MaterialPath;
	/** Object path of the asset this record was applied to, as the manifest named it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Identity") FString AssetPath;
	/** The unit file below the export_v2 root, e.g. `materials/brick/brickwall001a.glb`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Identity") FString UnitGlb;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Identity") FString UnitSchemaVersion;
	/** sha256 of the whole GLB file: the back-pointer from asset to unit. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Identity") FString UnitSha256;
	/** sha256 over `sourceResolution.members[].sha256`: the install bytes this unit was read from. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Identity") FString SourceSha256;
	/** The import lane's settings version the asset was authored under (`elysium-material-import-v1`). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Identity") FString SettingsVersion;

	// --- shader -------------------------------------------------------------------------------
	/** The unit's own `shader` name. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Shader") FString Shader;
	/** A patched unit's base `sourceShader`; empty for an install unit. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Shader") FString SourceShader;
	/** `shaderResolution.family`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Shader") FString ResolvedFamily;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Shader") TArray<FElysiumMaterialProgram> ResolvedPrograms;
	/** The static-switch keys the resolution branched on. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Shader") TArray<FString> ResolutionInputs;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Shader") FString ResolutionReason;

	// --- build decisions ------------------------------------------------------------------------
	/** The V2 master this instance is parented to, or a patched unit's base instance. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Build") FString Master;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Build") FString BlendMode;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Build") bool TwoSided = false;
	/** The resolved class key (`$surfaceprop`, else top directory, else family default). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Build") FName SurfaceClass;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Build") int32 SurfaceClassIndex = 0;
	/** `$envmap`'s literal value (`env_cubemap`, a concrete cube path, ...). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Build") FString EnvMapSymbol;
	/** The resolved `vtmb:texture:` probe asset id; never bound to a texture parameter (reflection contract). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Build") FString EnvMapAssetId;
	/** Soft object path of the probe asset, for inspection without resolving the asset id again. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Build") FSoftObjectPath EnvMapProbePath;
	/** `/ElysiumBaked/SurfaceProperties/PM_<name>`, mirroring `PhysMaterial`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Build") FString SurfacePropertyAsset;
	/** A patched unit's base: `vtmb:material:<dir>/<stem>`; empty for an install unit. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Build") FString PatchOf;
	/** `replace` or `insert`; empty for an install unit. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Build") FString PatchKind;

	// --- content --------------------------------------------------------------------------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Content") TArray<FElysiumMaterialParameter> Parameters;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Content") TArray<FElysiumMaterialBlock> Blocks;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Content") TArray<FElysiumMaterialProxy> Proxies;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Content") TArray<FElysiumMaterialTextureBinding> TextureBindings;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Content") TArray<FElysiumMaterialDependency> Dependencies;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Content") TArray<FString> Anomalies;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Content") TArray<FString> Omissions;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Content") TArray<FString> Comments;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Content") float CoveragePercent = 0.0f;

	/**
	 * Parse a provenance sidecar (the JSON `uv run elysium import materials` stages beside each
	 * unit), create the record with `Material` as its outer, fill it, and attach it -- replacing
	 * any record already on the material. Returns null and a reason when the JSON does not parse
	 * or is not an object; individual missing keys are tolerated and left at their defaults,
	 * exactly as `UElysiumTextureProvenance::ApplyJson` does.
	 *
	 * Python: `unreal.ElysiumMaterialProvenance.apply_json(material, text)` → `(record, error)`.
	 */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Materials")
	static UElysiumMaterialProvenance* ApplyJson(UMaterialInterface* Material, const FString& Json, FString& OutError);

	/** The record a material carries, or null. */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Materials")
	static const UElysiumMaterialProvenance* Find(const UMaterialInterface* Material);

	/**
	 * Publish `ElysiumAssetId`, `ElysiumShaderProgram`, `ElysiumMaster` and `ElysiumSurfaceClass`
	 * as package metadata on the material's package. `ElysiumShaderProgram` is the `default`-
	 * condition pixel program's name, or `ResolvedFamily` when the shader did not resolve.
	 * `Config/DefaultGame.ini` lists all four (the first shared with the texture lane) under
	 * `MetaDataTagsForAssetRegistry`, so a saved asset surfaces them as asset-registry tags the
	 * Content Browser filters on without loading it. Editor-only data: outside the editor this
	 * returns false with a reason and changes nothing.
	 *
	 * Success travels in an out-parameter rather than the return value on purpose: the editor's
	 * Python binding turns a `bool` return plus out-parameters into "None on false, the
	 * out-parameters on true" (PyGenUtil.cpp, PackReturnValues), which drops the error text. A
	 * `void` with `(bool&, FString&)` reaches Python as the explicit `(ok, error)` tuple.
	 */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Materials")
	static void StampRegistryTags(UMaterialInterface* Material, bool& bOutStamped, FString& OutError);

	/** The metadata keys `StampRegistryTags` writes, in the order it writes them. */
	static const FName TagAssetId;
	static const FName TagShaderProgram;
	static const FName TagMaster;
	static const FName TagSurfaceClass;

	/** Fill this record from a parsed sidecar object. Missing keys keep their defaults. */
	void FromJson(const TSharedRef<FJsonObject>& Object);
};
