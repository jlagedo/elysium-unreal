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
// This revision (SF-4.3 part 2) reconciled M_V2_Lit to the design revision 2026-08-31 ("Revise
// the material import design after review") and added M_V2_LitTranslucent, M_V2_Unlit and
// M_V2_TwoTexture. Part 3 added the remaining five masters family-by-family (mechanics doc
// "Ordering, concurrency, tests, risks"): M_V2_Eyes, M_V2_Water, M_V2_Sprite, M_V2_Refract and,
// this revision, M_V2_Decal -- the ninth and last master.
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

// `M_V2_Unlit` -- no NormalMap/bump lane (design doc "M_V2_Unlit": "The two-slot rule ... is
// retracted for Unlit rather than a dead slot added"). CloudAlphaTexture/CloudScale/UseCloudAlpha
// are the `cloud` family's slots.
namespace ElysiumSurfaceParamsUnlit
{
	namespace Textures
	{
		inline const FName BaseTexture(TEXT("BaseTexture"));
		inline const FName EnvMapMask(TEXT("EnvMapMask"));
		inline const FName EnvMap(TEXT("EnvMap"));
		inline const FName CloudAlphaTexture(TEXT("CloudAlphaTexture"));
		inline const FName BaseTextureFrames(TEXT("BaseTextureFrames"));
	}

	namespace Scalars
	{
		inline const FName EnvMapMaskScale(TEXT("EnvMapMaskScale"));
		inline const FName BaseScrollRateU(TEXT("BaseScrollRateU"));
		inline const FName BaseScrollRateV(TEXT("BaseScrollRateV"));
		inline const FName FrameRate(TEXT("FrameRate"));
		inline const FName FrameCount(TEXT("FrameCount"));
		inline const FName SineMin(TEXT("SineMin"));
		inline const FName SineMax(TEXT("SineMax"));
		inline const FName SinePeriod(TEXT("SinePeriod"));
		inline const FName SineTimeOffset(TEXT("SineTimeOffset"));
	}

	namespace Vectors
	{
		inline const FName EnvMapTint(TEXT("EnvMapTint"));
		inline const FName TexScaleOffset(TEXT("TexScaleOffset"));
		inline const FName CloudScale(TEXT("CloudScale"));
		inline const FName SineTargetMask(TEXT("SineTargetMask"));
		inline const FName SineChannelMask(TEXT("SineChannelMask"));
	}

	namespace Switches
	{
		inline const FName UseBaseTexture(TEXT("UseBaseTexture"));
		inline const FName UseVertexColor(TEXT("UseVertexColor"));
		inline const FName UseVertexAlpha(TEXT("UseVertexAlpha"));
		inline const FName UseEnvMap(TEXT("UseEnvMap"));
		inline const FName UseEnvMapMask(TEXT("UseEnvMapMask"));
		inline const FName UseBaseAlphaEnvMapMask(TEXT("UseBaseAlphaEnvMapMask"));
		inline const FName UseFixedCube(TEXT("UseFixedCube"));
		inline const FName MetallicTint(TEXT("MetallicTint"));
		inline const FName UseAnimatedFrames(TEXT("UseAnimatedFrames"));
		inline const FName UseCloudAlpha(TEXT("UseCloudAlpha"));
	}
}

// `M_V2_TwoTexture` -- `worldvertextransition`/`worldtwotextureblend`/`unlittwotexture`. One
// NormalMap slot shared by both texture layers (UseBumpOnBaseTexture2 is declared, per the
// design's table, but has no second slot to switch onto). No envmap/reflection lane at all (not
// in the design's table for this master) and no flipbook/normal-animation lane either -- only the
// base-scroll and sine lanes.
namespace ElysiumSurfaceParamsTwoTexture
{
	namespace Textures
	{
		inline const FName BaseTexture(TEXT("BaseTexture"));
		inline const FName BaseTexture2(TEXT("BaseTexture2"));
		inline const FName NormalMap(TEXT("NormalMap"));
	}

	namespace Scalars
	{
		inline const FName AlphaBias(TEXT("AlphaBias"));
		inline const FName BaseScrollRateU(TEXT("BaseScrollRateU"));
		inline const FName BaseScrollRateV(TEXT("BaseScrollRateV"));
		inline const FName SineMin(TEXT("SineMin"));
		inline const FName SineMax(TEXT("SineMax"));
		inline const FName SinePeriod(TEXT("SinePeriod"));
		inline const FName SineTimeOffset(TEXT("SineTimeOffset"));
	}

	namespace Vectors
	{
		inline const FName TexScaleOffset(TEXT("TexScaleOffset"));
		inline const FName Texture2ScaleOffset(TEXT("Texture2ScaleOffset"));
		inline const FName SineTargetMask(TEXT("SineTargetMask"));
		inline const FName SineChannelMask(TEXT("SineChannelMask"));
	}

	namespace Switches
	{
		inline const FName UseBaseTexture2(TEXT("UseBaseTexture2"));
		inline const FName UseNormalMap(TEXT("UseNormalMap"));
		inline const FName UseBumpOnBaseTexture2(TEXT("UseBumpOnBaseTexture2"));
		inline const FName UseVertexColor(TEXT("UseVertexColor"));
		inline const FName UseVertexAlpha(TEXT("UseVertexAlpha"));
	}
}

// `M_V2_Eyes` -- `eyes` family only (406 units). No NormalMap, no reflection lane at all (not in
// the design's exposed-parameter table for this master): Roughness/Specular/Metallic are always
// the class-LUT row. `IrisFrame` and `VampireEyes` are declared per the design's table but have
// nothing in the corpus to wire (see `make_v2_materials.py::_build_eyes`'s docstring) --
// declared, not wired.
namespace ElysiumSurfaceParamsEyes
{
	namespace Textures
	{
		inline const FName BaseTexture(TEXT("BaseTexture"));
		inline const FName Iris(TEXT("Iris"));
		inline const FName Glint(TEXT("Glint"));
	}

	namespace Scalars
	{
		inline const FName IrisFrame(TEXT("IrisFrame"));
	}

	namespace Switches
	{
		inline const FName VampireEyes(TEXT("VampireEyes"));
		inline const FName UseGlint(TEXT("UseGlint"));
	}
}

// `M_V2_Water` -- `water` family only (24 units). No `BottomMaterial` slot (`$bottommaterial`
// names a material, not a texture -- provenance only, design doc "M_V2_Water"). `UseFogEnable`/
// `FogColor`/`FogStart`/`FogEnd` and the wave-animation scalars are declared per the design's
// table but have no pixel-graph formula stated anywhere in the design (fog is a distance/height
// effect and the wave terms are vertex/World-Position-Offset concerns, both out of this master's
// scope) -- declared, not wired (see `make_v2_materials.py::_build_water`'s docstring).
namespace ElysiumSurfaceParamsWater
{
	namespace Textures
	{
		inline const FName BaseTexture(TEXT("BaseTexture"));
		inline const FName DuDvMap(TEXT("DuDvMap"));
		inline const FName NormalMap(TEXT("NormalMap"));
		inline const FName EnvMap(TEXT("EnvMap"));
		inline const FName NormalMapFrames(TEXT("NormalMapFrames"));
	}

	namespace Scalars
	{
		inline const FName RefractAmount(TEXT("RefractAmount"));
		inline const FName ReflectAmount(TEXT("ReflectAmount"));
		inline const FName BaseReflectFract(TEXT("BaseReflectFract"));
		inline const FName WaterDepth(TEXT("WaterDepth"));
		inline const FName WaterMurkiness(TEXT("WaterMurkiness"));
		inline const FName WaterBaseFactor(TEXT("WaterBaseFactor"));
		inline const FName WaterBaseMovementDist(TEXT("WaterBaseMovementDist"));
		inline const FName WaterBaseMovementFreq(TEXT("WaterBaseMovementFreq"));
		inline const FName WaterSpecularMin(TEXT("WaterSpecularMin"));
		inline const FName WaterSpecularMax(TEXT("WaterSpecularMax"));
		inline const FName WaterTimeFreq1(TEXT("WaterTimeFreq1"));
		inline const FName WaterTimeFreq2(TEXT("WaterTimeFreq2"));
		inline const FName WaterWaveHeight(TEXT("WaterWaveHeight"));
		inline const FName WaterWaveLength(TEXT("WaterWaveLength"));
		inline const FName CheapWaterStartDistance(TEXT("CheapWaterStartDistance"));
		inline const FName CheapWaterEndDistance(TEXT("CheapWaterEndDistance"));
		inline const FName FogStart(TEXT("FogStart"));
		inline const FName FogEnd(TEXT("FogEnd"));
		inline const FName BumpScrollRateU(TEXT("BumpScrollRateU"));
		inline const FName BumpScrollRateV(TEXT("BumpScrollRateV"));
		inline const FName NormalFrameRate(TEXT("NormalFrameRate"));
		inline const FName NormalFrameCount(TEXT("NormalFrameCount"));
	}

	namespace Vectors
	{
		inline const FName WaterColor(TEXT("WaterColor"));
		inline const FName RefractTint(TEXT("RefractTint"));
		inline const FName ReflectTint(TEXT("ReflectTint"));
		inline const FName FogColor(TEXT("FogColor"));
		inline const FName EnvMapTint(TEXT("EnvMapTint"));
		inline const FName TexScaleOffset(TEXT("TexScaleOffset"));
	}

	namespace Switches
	{
		inline const FName CheapWater(TEXT("CheapWater"));
		inline const FName UseFogEnable(TEXT("UseFogEnable"));
		inline const FName UseEnvMap(TEXT("UseEnvMap"));
		inline const FName UseFixedCube(TEXT("UseFixedCube"));
		inline const FName UseBaseTexture(TEXT("UseBaseTexture"));
		inline const FName UseNormalMap(TEXT("UseNormalMap"));
		inline const FName UseAnimatedNormalFrames(TEXT("UseAnimatedNormalFrames"));
	}
}

// `M_V2_Sprite` -- `sprite` family plus the 5 `unlitgeneric` `$ignorez` world units re-routed here
// (design doc "Master inventory"). Unlit, two-sided, `bDisableDepthTest` set on the master itself
// (material-only, never per-instance). `UseVertexAlpha` is the SF-4.3-part-3 orchestrator ruling:
// it absorbs the rerouted `$ignorez` unit (`engine/vertexcolorblend`) that authors `$vertexalpha`.
namespace ElysiumSurfaceParamsSprite
{
	namespace Textures
	{
		inline const FName BaseTexture(TEXT("BaseTexture"));
		inline const FName BaseTextureFrames(TEXT("BaseTextureFrames"));
	}

	namespace Scalars
	{
		inline const FName FrameRate(TEXT("FrameRate"));
		inline const FName FrameCount(TEXT("FrameCount"));
	}

	namespace Switches
	{
		inline const FName UseVertexColor(TEXT("UseVertexColor"));
		inline const FName UseVertexAlpha(TEXT("UseVertexAlpha"));
		inline const FName UseAnimatedFrames(TEXT("UseAnimatedFrames"));
	}
}

// `M_V2_Refract` -- `refract`/`heatglow` families (11 units). No shipped source and no
// transcribed selector; the master is a reconstruction (design doc "M_V2_Refract"). No
// `ForceRefract` parameter -- `$forcerefract` has zero corpus authors and no proxy, so there is
// nothing to expose it for.
namespace ElysiumSurfaceParamsRefract
{
	namespace Textures
	{
		inline const FName BaseTexture(TEXT("BaseTexture"));
		inline const FName DuDvMap(TEXT("DuDvMap"));
		inline const FName NormalMap(TEXT("NormalMap"));
		inline const FName EnvMap(TEXT("EnvMap"));
	}

	namespace Scalars
	{
		inline const FName RefractAmount(TEXT("RefractAmount"));
	}

	namespace Vectors
	{
		inline const FName RefractTint(TEXT("RefractTint"));
		inline const FName EnvMapTint(TEXT("EnvMapTint"));
	}

	namespace Switches
	{
		inline const FName UseBaseTexture(TEXT("UseBaseTexture"));
		inline const FName UseNormalMap(TEXT("UseNormalMap"));
		inline const FName UseEnvMap(TEXT("UseEnvMap"));
		inline const FName UseFixedCube(TEXT("UseFixedCube"));
	}
}

// `M_V2_Decal` -- `decalmodulate` (38 units, no shipped program at all -- retail fell back to
// wireframe). Unlit, `BLEND_Modulate` on the master. `DecalDepthOffset` is not on this master (a
// knob only, in `MPC_ElysiumSurfaces`, applied by the placement lane's decal component -- design
// doc "Four parameters that left the masters"). Two known UE limitations worth stating rather
// than working around: modulate-blend surfaces are excluded from the Lumen surface cache (no GI
// contribution, invisible in a Lumen reflection), and `BLEND_Modulate` is not Nanite-compatible,
// so this master does not set `used_with_nanite`.
namespace ElysiumSurfaceParamsDecal
{
	namespace Textures
	{
		inline const FName BaseTexture(TEXT("BaseTexture"));
	}

	namespace Switches
	{
		inline const FName UseVertexColor(TEXT("UseVertexColor"));
	}
}
