#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "ElysiumTextureProvenance.generated.h"

class UTexture;
class FJsonObject;

/** The eight Source sampling flags a texture unit decodes (`seam_map_texture.md` → `sampling`). */
USTRUCT(BlueprintType)
struct FElysiumTextureSampling
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Texture") bool bPointSample = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Texture") bool bTrilinear = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Texture") bool bClampS = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Texture") bool bClampT = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Texture") bool bAnisotropic = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Texture") bool bNoMip = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Texture") bool bNoLod = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Texture") bool bAllMips = false;
};

/** One row of the unit's `sourceResolution.members`: which install bytes the texture came from. */
USTRUCT(BlueprintType)
struct FElysiumTextureSourceMember
{
	GENERATED_BODY()

	/** `tth` or `ttz`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Texture") FString Role;
	/** Install-relative member path, e.g. `materials/hud/signs/notepad_yellow.tth`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Texture") FString Path;
	/** `vpk` or `loose`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Texture") FString OriginKind;
	/** The VPK file for a `vpk` origin, the install root label for a `loose` one. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Texture") FString Container;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Texture") int64 Offset = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Texture") int64 Size = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Texture") int64 ByteLength = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Texture") FString Sha256;
};

/** For a cubemap: which KTX2 (glTF-ordered) face landed in which Unreal face, and how it was turned. */
USTRUCT(BlueprintType)
struct FElysiumCubeFaceMapping
{
	GENERATED_BODY()

	/** Unreal face axis name in slice order: `+X`, `-X`, `+Y`, `-Y`, `+Z`, `-Z`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Texture") FString UnrealFace;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Texture") int32 KtxFace = 0;
	/** The VTF face index the pixels originally came from. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Texture") int32 SourceFace = 0;
	/** `identity`, `rotate-cw`, `rotate-ccw` or `rotate-180`, applied to the KTX2 face. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Texture") FString Transform;
};

/**
 * Everything a `vtmb:texture:` unit knows that a UTexture has no slot for, carried on the baked
 * asset as AssetUserData so a packaged game can read it and the editor can inspect it
 * (`docs/architecture/seam_map_texture.md` → "Import" → "Provenance").
 *
 * `uv run elysium import textures` attaches one per asset through `ApplyJson`, from the
 * provenance sidecar the offline stage writes beside each DDS. A texture carries at most one:
 * UTexture::AddAssetUserData replaces an instance of the same class.
 */
UCLASS(BlueprintType)
class ELYSIUMUE_API UElysiumTextureProvenance : public UAssetUserData
{
	GENERATED_BODY()

public:
	// --- identity ---------------------------------------------------------------------------
	/** `vtmb:texture:<path>`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Identity") FString AssetId;
	/** The path below `materials/`, lowercased, no suffix. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Identity") FString TexturePath;
	/** Object path of the asset this record was applied to, as the manifest named it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Identity") FString AssetPath;
	/** The unit file below the export_v2 root, e.g. `textures/hud/signs/notepad_yellow.glb`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Identity") FString UnitGlb;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Identity") FString UnitSchemaVersion;
	/** sha256 of the whole GLB file: the back-pointer from asset to unit. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Identity") FString UnitSha256;
	/** sha256 of the KTX2 payload: the bit-exact blocks this asset was re-encoded from. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Identity") FString PayloadSha256;
	/** The import lane's settings version the asset was authored under (`elysium-texture-import-v1`). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Identity") FString SettingsVersion;

	// --- source format ----------------------------------------------------------------------
	/** VTF format name, e.g. `DXT5`, `BGR888`, `UVWQ8888`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Source") FString SourceFormat;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Source") int32 SourceFormatEnum = 0;
	/** The KTX2 payload's Vulkan format value and name. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Source") int32 VkFormat = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Source") FString VkFormatName;
	/** Embedded VTF header version, `7.0` or `7.1`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Source") FString VtfVersion;
	/** Troika's outer TTH container version; 1 across the install. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Source") int32 TthVersion = 0;
	/** The raw VTF flag word; the decoded sampling bits are in `Sampling`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Source") int32 Flags = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Source") int32 StartFrame = 0;
	/** Average linear albedo vtex computed; the ambient-cube input (`docs/vtmb/sky-ambience.md`). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Source") FVector3f Reflectivity = FVector3f::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Source") float BumpScale = 1.0f;

	// --- extent -----------------------------------------------------------------------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Extent") int32 Width = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Extent") int32 Height = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Extent") int32 Frames = 1;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Extent") int32 Faces = 1;
	/** Levels the KTX2 payload carries (what was imported). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Extent") int32 MipCount = 0;
	/** Levels the VTF header declared, which may exceed what the file physically stores. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Extent") int32 SourceMipCount = 0;
	/** The payload's chain ends above 1x1: the source never stored the tail. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Extent") bool PartialMipChain = false;
	/** An RGB8 source was widened to RGBA8 (opaque alpha) for the DDS the asset was imported from. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Extent") bool ExpandedFromRgb8 = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Source") FElysiumTextureSampling Sampling;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Source") TArray<FElysiumTextureSourceMember> Members;

	// --- role -------------------------------------------------------------------------------
	/** `colour`, `data-normal` or `data-mask`: what decided this asset's sRGB switch. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Role") FString Role;
	/** The material parameters that bind this texture across the corpus; empty means no evidence. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Role") TArray<FString> RoleEvidence;
	/** Bound as colour by one material and as data by another; a `_linear` twin exists. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Role") bool RoleConflict = false;
	/** For a `_linear` twin: the object path of the colour asset it duplicates. Empty otherwise. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Role") FString TwinOf;

	/** Cubemaps only: the glTF→Unreal face permutation and rotations the stage applied. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Extent") TArray<FElysiumCubeFaceMapping> FaceMapping;

	/**
	 * Parse a provenance sidecar (the JSON `uv run elysium import textures` stages beside each DDS),
	 * create the record with `Texture` as its outer, fill it, and attach it — replacing any record
	 * already on the texture. Returns null and a reason when the JSON does not parse or is not an
	 * object; individual missing keys are tolerated and left at their defaults.
	 *
	 * Python: `unreal.ElysiumTextureProvenance.apply_json(texture, text)` → `(record, error)`.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Textures")
	static UElysiumTextureProvenance* ApplyJson(UTexture* Texture, const FString& Json, FString& OutError);

	/** The record a texture carries, or null. */
	UFUNCTION(BlueprintCallable, Category="Elysium|Textures")
	static const UElysiumTextureProvenance* Find(const UTexture* Texture);

	/**
	 * Publish `ElysiumAssetId`, `ElysiumSourceFormat`, `ElysiumRole` and `ElysiumRoleConflict` as
	 * package metadata on the texture's package. `Config/DefaultGame.ini` lists those four under
	 * `MetaDataTagsForAssetRegistry`, so a saved asset surfaces them as asset-registry tags the
	 * Content Browser filters on without loading it. Editor-only data: outside the editor this
	 * returns false with a reason and changes nothing.
	 * Success travels in an out-parameter rather than the return value on purpose: the editor's
	 * Python binding turns a `bool` return plus out-parameters into "None on false, the
	 * out-parameters on true" (PyGenUtil.cpp, PackReturnValues), which drops the error text.
	 * A `void` with `(bool&, FString&)` reaches Python as the explicit `(ok, error)` tuple.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Textures")
	static void StampRegistryTags(UTexture* Texture, bool& bOutStamped, FString& OutError);

	/** The metadata keys `StampRegistryTags` writes, in the order it writes them. */
	static const FName TagAssetId;
	static const FName TagSourceFormat;
	static const FName TagRole;
	static const FName TagRoleConflict;

	/** Fill this record from a parsed sidecar object. Missing keys keep their defaults. */
	void FromJson(const TSharedRef<FJsonObject>& Object);
};

/**
 * The editor-side questions `pipeline/unreal/import_textures.py` asks about what Unreal built
 * from an imported DDS, so the run report can state the re-encode rather than assume it.
 *
 * The class is reflected in every target so the Python surface is stable, but the work is
 * editor work: `WriteBuiltMipZeroAsDds` needs the derived-data build and mip inlining that only
 * exist under `WITH_EDITOR`, so outside the editor it returns false with a reason. The extent
 * queries read whatever platform data the texture holds and answer in any target.
 */
UCLASS()
class ELYSIUMUE_API UElysiumTextureImportLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Write the built platform mip 0 (a 2D texture's whole level; face 0 of a cubemap; slice 0 of a
	 * 2D array) as a DDS at `Path`, so the offline measure phase can decode it beside the staged
	 * source DDS. Blocks on the texture build first. Only the formats the lane can produce are
	 * written — BC1/BC2/BC3 and BGRA8/RGBA8/R8 — any other built format returns false with its
	 * name rather than a guessed header.
	 *
	 * Python: `unreal.ElysiumTextureImportLibrary.write_built_mip_zero_as_dds(texture, path)` →
	 * `(ok, error)`.
	 * Success travels in an out-parameter rather than the return value on purpose: the editor's
	 * Python binding turns a `bool` return plus out-parameters into "None on false, the
	 * out-parameters on true" (PyGenUtil.cpp, PackReturnValues), which drops the error text.
	 * A `void` with `(bool&, FString&)` reaches Python as the explicit `(ok, error)` tuple.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Textures")
	static void WriteBuiltMipZeroAsDds(UTexture* Texture, const FString& Path, bool& bOutWritten, FString& OutError);

	/** Mips in the built platform data, after blocking on the build; -1 when there is none. */
	UFUNCTION(BlueprintCallable, Category="Elysium|Textures")
	static int32 BuiltMipCount(UTexture* Texture);

	/** The built platform pixel format's name (`PF_DXT5`, `PF_B8G8R8A8`, ...); empty when none. */
	UFUNCTION(BlueprintCallable, Category="Elysium|Textures")
	static FString BuiltPixelFormat(UTexture* Texture);

	/**
	 * Width, height, slice count (6 for a cubemap, the frame count for an array, 1 otherwise) and
	 * mip count of the built platform data. All zero when there is none.
	 *
	 * Python: `unreal.ElysiumTextureImportLibrary.built_extent(texture)` → `(w, h, slices, mips)`.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Textures")
	static void BuiltExtent(UTexture* Texture, int32& OutWidth, int32& OutHeight, int32& OutSlices, int32& OutMips);
};
