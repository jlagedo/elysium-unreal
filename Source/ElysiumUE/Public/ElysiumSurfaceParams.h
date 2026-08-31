#pragma once

#include "CoreMinimal.h"

// The exposed-parameter contract for the V2 material-import masters (SF-4.3,
// docs/architecture/seam_map_material.md -> "Import" -> "Exposed parameters, by master"). The
// stage (SF-4.4, pipeline/src/elysium_pipeline/importers/materials.py's EXPOSED_PARAMS) writes
// exactly these names onto an imported MI_ instance and the masters
// (`pipeline/unreal/make_v2_materials.py`) expose exactly these names; a name on one side and not
// the other is a build error, not a silent default. Declared here, rather than as string literals
// at each call site, so the generator, the runtime importer (SF-4.5) and
// `Elysium.Policy.V2MasterParams` (Private/Tests/ElysiumV2MaterialTests.cpp) read the same FNames.
//
// This revision (SF-4.3 part 2) reconciles M_V2_Lit to the design revision 2026-08-31 ("Revise
// the material import design after review"). M_V2_Unlit and M_V2_TwoTexture land in the two
// commits that follow this one; M_V2_Eyes, M_V2_Water, M_V2_Sprite, M_V2_Refract and M_V2_Decal
// are not authored yet and land family-by-family (mechanics doc "Ordering, concurrency, tests,
// risks").
//
// Every V2 master exposes SurfaceClassIndex, SurfaceClassLUT, Alpha and Color -- the four
// parameters the design doc states once, "on every master", rather than repeating in each
// per-master table -- so they live here rather than duplicated in every namespace below.
namespace ElysiumSurfaceParamsShared
{
	namespace Textures
	{
		inline const FName SurfaceClassLUT(TEXT("SurfaceClassLUT"));
	}

	namespace Scalars
	{
		inline const FName SurfaceClassIndex(TEXT("SurfaceClassIndex"));
		inline const FName Alpha(TEXT("Alpha"));
	}

	namespace Vectors
	{
		inline const FName Color(TEXT("Color"));
	}
}

// `M_V2_Lit` / `M_V2_LitTranslucent` (the same graph; LitTranslucent differs only in material
// domain/blend, not in parameter set) -- the design doc's exposed-parameter table for the two
// masters, verbatim, after the 2026-08-31 revision: Detail/DetailScale/UseDetail,
// DecalDepthOffset/IsDecalSurface, MinLight/MaxLight, WetnessScale and the single ScrollRateU/V +
// UseScroll lane are gone (each has a new, non-material home -- see the design doc's "Four
// parameters that left the masters" and "`Detail` is dropped"); BaseScrollRateU/V,
// BumpScrollRateU/V (two independent scroll lanes, no switch -- (0,0) is an exact Panner no-op),
// FrameCount, BaseTextureFrames, NormalFrameRate/NormalFrameCount/NormalMapFrames/
// UseAnimatedNormalFrames (the flipbook lanes) and SineTargetMask/SineChannelMask (the sine lane)
// are new.
namespace ElysiumSurfaceParamsLit
{
	namespace Textures
	{
		inline const FName BaseTexture(TEXT("BaseTexture"));
		inline const FName NormalMap(TEXT("NormalMap"));
		inline const FName EnvMapMask(TEXT("EnvMapMask"));
		inline const FName EnvMap(TEXT("EnvMap"));
		inline const FName BaseTextureFrames(TEXT("BaseTextureFrames"));
		inline const FName NormalMapFrames(TEXT("NormalMapFrames"));
	}

	namespace Scalars
	{
		inline const FName SelfIllumAmount(TEXT("SelfIllumAmount"));
		inline const FName EnvMapMaskScale(TEXT("EnvMapMaskScale"));
		inline const FName BumpScale(TEXT("BumpScale"));
		inline const FName BaseScrollRateU(TEXT("BaseScrollRateU"));
		inline const FName BaseScrollRateV(TEXT("BaseScrollRateV"));
		inline const FName BumpScrollRateU(TEXT("BumpScrollRateU"));
		inline const FName BumpScrollRateV(TEXT("BumpScrollRateV"));
		inline const FName FrameRate(TEXT("FrameRate"));
		inline const FName FrameCount(TEXT("FrameCount"));
		inline const FName NormalFrameRate(TEXT("NormalFrameRate"));
		inline const FName NormalFrameCount(TEXT("NormalFrameCount"));
		inline const FName SineMin(TEXT("SineMin"));
		inline const FName SineMax(TEXT("SineMax"));
		inline const FName SinePeriod(TEXT("SinePeriod"));
		inline const FName SineTimeOffset(TEXT("SineTimeOffset"));
	}

	namespace Vectors
	{
		inline const FName SelfIllumTint(TEXT("SelfIllumTint"));
		inline const FName EnvMapTint(TEXT("EnvMapTint"));
		inline const FName TexScaleOffset(TEXT("TexScaleOffset"));
		inline const FName SineTargetMask(TEXT("SineTargetMask"));
		inline const FName SineChannelMask(TEXT("SineChannelMask"));
	}

	// Named `Use...`/`MetallicTint` nowhere `bUse...` -- the design's own table
	// (docs/architecture/seam_map_material.md -> "Exposed parameters, by master") spells every
	// switch this way, and this header mirrors it verbatim rather than a C++ convention.
	namespace Switches
	{
		inline const FName UseBaseTexture(TEXT("UseBaseTexture"));
		inline const FName UseNormalMap(TEXT("UseNormalMap"));
		inline const FName UseSelfIllum(TEXT("UseSelfIllum"));
		inline const FName UseVertexColor(TEXT("UseVertexColor"));
		inline const FName UseVertexAlpha(TEXT("UseVertexAlpha"));
		inline const FName UseEnvMap(TEXT("UseEnvMap"));
		inline const FName UseEnvMapMask(TEXT("UseEnvMapMask"));
		inline const FName UseBaseAlphaEnvMapMask(TEXT("UseBaseAlphaEnvMapMask"));
		inline const FName UseNormalMapAlphaEnvMapMask(TEXT("UseNormalMapAlphaEnvMapMask"));
		inline const FName UseFixedCube(TEXT("UseFixedCube"));
		inline const FName MetallicTint(TEXT("MetallicTint"));
		inline const FName UseAnimatedFrames(TEXT("UseAnimatedFrames"));
		inline const FName UseAnimatedNormalFrames(TEXT("UseAnimatedNormalFrames"));
	}
}

