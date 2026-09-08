#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "UObject/SoftObjectPath.h"

#include "ElysiumMaterialProvenance.generated.h"

class FJsonObject;
class UMaterialInterface;

/**
 * One row of the unit's `parameters`, in source order. Carried for every parameter the unit
 * authors, including the ones this lane does not consume, so no VMT key is silently lost between
 * the sidecar and the asset.
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
 * One row of the unit's `proxies`: a Source proxy chain instance and where it landed -- a material-graph node, a
 * runtime-factory binding, or provenance only. `Name` and `ParameterIndices` are not published by
 * the stage today (it writes `sourceName` and pre-resolved `arguments`, not raw parameter
 * indices); they stay at their defaults.
 */
USTRUCT(BlueprintType)
struct FElysiumMaterialProxy
{
	GENERATED_BODY()

	/** The proxy kind (`sine`, `texturescroll`, `globalwetness`, ...) as this lane resolved it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString Kind;
	/** Not published by the stage today; stays empty. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString Name;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString SourceName;
	/** Not published by the stage today; stays empty. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") TArray<int32> ParameterIndices;
	/** The proxy's resolved `key -> value` arguments (`sinemin`, `scale`, `resultvar`, ...). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") TMap<FString, FString> Arguments;
	/** `runtime`, `graph`, `provenance` or `scalar` (the proxy policy's destinations). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString Destination;
};

/**
 * One row of `omissions[]`: a VMT key or a keyvalues-shape note this lane recorded but applied
 * nowhere. A real
 * staged sidecar carries at least two shapes -- a blanket `{reason, role}` insignificant-
 * whitespace note on every unit, and a named `{key, kind, reason}` row for a per-unit divergence
 * (`UNIT_DIVERGENCES`) -- both always carry `reason`; every other field lands in `Extra`
 * (stringified the same tolerant way `FElysiumMaterialParameter::Value` is), so a shape this
 * reader has not seen yet is carried, not dropped.
 */
USTRUCT(BlueprintType)
struct FElysiumProvenanceNote
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString Reason;
	/** Every field besides `reason` (`role`, `key`, `kind`, ...), keyed by its own JSON key. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") TMap<FString, FString> Extra;
};

/**
 * One row of `anomalies[]`: something this lane noticed but could not (or should not) resolve into
 * a binding (C-2). Every row carries `kind` (`selfIllumOnUnlitSurface`, `unknownProxy`,
 * `envMapMaskPrecedenceLoser`, ...); the remaining field differs per kind (`value`, `proxy`,
 * `switch`, `target`, ...) and lands in `Extra`, stringified the same way `Omissions`' does.
 */
USTRUCT(BlueprintType)
struct FElysiumProvenanceAnomaly
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString Kind;
	/** Every field besides `kind`, keyed by its own JSON key. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") TMap<FString, FString> Extra;
};

/** One row of `comments[]`: a VMT source comment, carried verbatim with its byte offset. */
USTRUCT(BlueprintType)
struct FElysiumProvenanceComment
{
	GENERATED_BODY()

	/** The unit's own byte offset for this comment, mirroring `FElysiumMaterialParameter::Offset`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") int32 Offset = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString Text;
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

/**
 * One row of `textureBindings`: `{parameter, asset}` exactly as `stage_unit` writes it. `Value`,
 * `Kind`, `Resolved` and `UsedLinearTwin` are carried for a future stage revision that writes them
 * but stay at their defaults against today's sidecar -- `FromJson` never invents data the stage did
 * not publish.
 */
USTRUCT(BlueprintType)
struct FElysiumMaterialTextureBinding
{
	GENERATED_BODY()

	/** The master's texture parameter this binds (`BaseTexture`, `EnvMapMask`, ...). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString Parameter;
	/** The VMT's own texture path value. Not published by the stage today; stays empty. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString Value;
	/** `color`, `mask`, `normal` or `linear`. Not published by the stage today; stays empty. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString Kind;
	/** The resolved `vtmb:texture:` asset id. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString Asset;
	/** Not published by the stage today; stays false. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") bool Resolved = false;
	/** Not published by the stage today; stays false. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") bool UsedLinearTwin = false;
};

/**
 * One row of `materialReferences`: `{parameter, asset}` exactly as `stage_unit` writes it -- a
 * non-texture asset reference this unit carries (a crack material, a cubemap origin, ...). `Role`
 * and `Resolved` mirror the surface-property lane's dependency shape but are not published by this
 * stage today; they stay at their defaults.
 */
USTRUCT(BlueprintType)
struct FElysiumMaterialDependency
{
	GENERATED_BODY()

	/** Not published by the stage today; stays empty. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString Role;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString Parameter;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") FString Asset;
	/** Not published by the stage today; stays false. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Material") bool Resolved = false;
};

/**
 * Everything a `vtmb:material:` unit knows that a `UMaterialInstance` has no slot for, carried on
 * the baked asset as AssetUserData so a packaged game can read it and the editor can inspect it.
 * `UMaterialInterface`
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
	/**
	 * sha256 over `sourceResolution.members[].sha256`: the install bytes this unit was read from.
	 * Not part of the provenance sidecar itself -- the manifest entry's own `sourceSha256`, which
	 * `pipeline/unreal/import_materials.py` merges into the sidecar object as a top-level key
	 * before calling `ApplyJson`, alongside `AssetPath`, `UnitGlb` and `SurfacePropertyAsset`.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Identity") FString SourceSha256;
	/** The import lane's settings version the asset was authored under (`elysium-material-import-v2`). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Identity") FString SettingsVersion;

	// --- shader -------------------------------------------------------------------------------
	/** The unit's own `shader` name. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Shader") FString Shader;
	/** A patched unit's base `sourceShader`; empty for an install unit. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Shader") FString SourceShader;
	/** The sidecar's top-level `shaderFamily` (`shaderResolution.family` in the design doc's older shape). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Shader") FString ResolvedFamily;
	/** Not published by the stage today (no `shaderResolution.resolvedPrograms` in the sidecar); stays empty. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Shader") TArray<FElysiumMaterialProgram> ResolvedPrograms;
	/** Not published by the stage today; stays empty. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Shader") TArray<FString> ResolutionInputs;
	/** Not published by the stage today; stays empty. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Shader") FString ResolutionReason;
	/** The sidecar's top-level `shaderResolved`: whether the shader mapped to a known master. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Shader") bool ShaderResolved = false;

	// --- build decisions ------------------------------------------------------------------------
	/** The V2 master this instance is parented to; empty for a patched unit (its `Parent` is the base instance). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Build") FString Master;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Build") FString SkinnedAsset;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Build") FString SkinnedMaster;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Consumers") TArray<FString> SkeletalConsumers;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Consumers") TArray<FString> StaticConsumers;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Consumers") TArray<FString> MapConsumers;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Build") FString BlendMode;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Build") bool TwoSided = false;
	/** The resolved class key (`$surfaceprop`, else top directory, else family default). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Build") FName SurfaceClass;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Build") int32 SurfaceClassIndex = 0;
	/** `surfaceprop`, `topdir` or `familyDefault`: which tier of the class-key fallback resolved `SurfaceClass`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Build") FString SurfaceClassSource;
	/** Whether `SurfaceClass` fell outside the 63-name `$surfaceprop` table (so `PhysMaterial` fell back to `PM_default`). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Build") bool PhysMaterialFallback = false;
	/** `$envmap`'s literal value (`env_cubemap`, a concrete cube path, ...), from the sidecar's `environment` object. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Build") FString EnvMapSymbol;
	/** The resolved `vtmb:texture:` probe asset id; never bound to a texture parameter (reflection contract). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Build") FString EnvMapAssetId;
	/** Soft object path of the probe asset, for inspection without resolving the asset id again. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Build") FSoftObjectPath EnvMapProbePath;
	/**
	 * H-2: the sidecar's `environment.envMapAsset` -- the authored-fixed-cube instance's own bound
	 * `EnvMap` texture asset path (`/ElysiumBaked/Textures/...`), distinct from `EnvMapAssetId`
	 * (a patched unit's un-bound probe asset id, a `vtmb:texture:` id rather than a baked path).
	 * Only ever set together with `UseFixedCube`; empty for `env_cubemap` and for a patched unit.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Build") FString EnvMapAsset;
	/**
	 * H-2: the sidecar's `environment.envMapTintChromatic` -- whether `$envmaptint` split
	 * chromatic (`ChromaticTintStrength` applies) rather than grey (`EnvTintScale` applies).
	 * Recorded for every `$envmap` unit regardless of which master exposes `MetallicTint`.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Build") bool bEnvMapTintChromatic = false;
	/**
	 * H-2: the sidecar's `environment.patchedProbe` -- true when a map-patched unit's `$envmap`
	 * resolved to a concrete per-instance probe (`EnvMapAssetId`/`EnvMapProbePath`) rather than
	 * the shared `env_cubemap` capture.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Build") bool bPatchedProbe = false;
	/**
	 * `/ElysiumBaked/SurfaceProperties/PM_<name>`, mirroring `PhysMaterial`. Not part of the
	 * provenance sidecar itself -- the manifest entry's own `physMaterial`, merged in the same way
	 * as `SourceSha256`.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Build") FString SurfacePropertyAsset;
	/** A patched unit's base: `vtmb:material:<dir>/<stem>` (the sidecar's `patchBase`); empty for an install unit. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Build") FString PatchOf;
	/** The sidecar's `patchKind` array (`["replace"]`, `["insert"]`, ...), joined with `,`; empty for an install unit. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Build") FString PatchKind;

	// --- placement / map / runtime-factory (keys with a home outside the material) --------------
	/** `$decal`: the placement lane spawns a decal component over this instance instead of a material switch. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Placement") bool IsDecalSurface = false;
	/** `$ignorez`: `bDisableDepthTest`, read by the placement lane; no material property for it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Placement") bool IgnoreZ = false;
	/** `$spriteorigin`'s `(x, y)`, when the unit authored one. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Placement") FVector2D SpriteOrigin = FVector2D::ZeroVector;
	/** `$spriteorientation`; the stage never populates this today (always null in the sidecar). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Placement") float SpriteOrientation = 0.0f;
	/** `$minlight`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Placement") float MinLight = 0.0f;
	/** `$maxlight`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Placement") float MaxLight = 0.0f;
	/** The `globalwetness` proxy's `scale`, read by `FElysiumMaterialFactory` at runtime, never sampled in the graph. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Placement") float WetnessScale = 0.0f;
	/** `$subdivsize`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Placement") float SubdivSize = 0.0f;
	/** `$curve`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Placement") float Curve = 0.0f;

	// --- content --------------------------------------------------------------------------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Content") TArray<FElysiumMaterialParameter> Parameters;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Content") TArray<FElysiumMaterialBlock> Blocks;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Content") TArray<FElysiumMaterialProxy> Proxies;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Content") TArray<FElysiumMaterialTextureBinding> TextureBindings;
	/** From the sidecar's `materialReferences[]`, not `dependencies` (the latter is not part of this stage's sidecar). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Content") TArray<FElysiumMaterialDependency> Dependencies;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Content") TArray<FElysiumProvenanceAnomaly> Anomalies;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Content") TArray<FElysiumProvenanceNote> Omissions;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Content") TArray<FElysiumProvenanceComment> Comments;
	/** `coverage.totalKeys`: the unit's own parameter count the stage walked. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Content") int32 CoverageTotalKeys = 0;
	/** `coverage.unmappedKeys`: always empty in a staged sidecar (an unmapped key is a stage failure, not a warning). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Content") TArray<FString> CoverageUnmappedKeys;

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
