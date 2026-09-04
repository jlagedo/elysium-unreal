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
// DecalDepthOffset/IsDecalSurface, MinLight/MaxLight and the single ScrollRateU/V + UseScroll
// lane are gone (each has a new, non-material home -- see the design doc's "Four parameters that
// left the masters" and "`Detail` is dropped"). WetnessScale left with them on that revision but
// is back (R5.3, "Decal fog and wetness homes") as a real per-instance scalar, alongside its
// WetnessDriven gate -- see the Scalars namespace below; BaseScrollRateU/V,
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
		// R5.3 (docs/architecture/seam_map_material.md -> "Decal fog and wetness homes"):
		// WetnessScale is back as a real per-instance scalar, reversing the 2026-08-31 revision's
		// "gone" call above -- the master now reads the live global wetness value off
		// MPC_ElysiumEnvironment itself (`_wetness_response` in make_v2_materials.py), so no
		// per-map material instance is needed for either half of the term. WetnessDriven gates it
		// (0 on every instance the stage never marked wetness-driven).
		inline const FName WetnessScale(TEXT("WetnessScale"));
		inline const FName WetnessDriven(TEXT("WetnessDriven"));
		// R5.4 (docs/architecture/seam_map_material.md -> "Scene fog on the world masters"):
		// Source's per-map distance fog, read off the primitive's Custom Primitive Data (slots
		// ElysiumFog::SlotStart / SlotInvRange, `mat_fog.fog_from_primitive`), never off the
		// instance -- the stage writes neither. `FogInscatter` is the instance half: 0 on an
		// Additive blend (Source fogs additive surfaces to black), 1 everywhere else.
		inline const FName FogStart(TEXT("FogStart"));
		inline const FName FogInvRange(TEXT("FogInvRange"));
		inline const FName FogInscatter(TEXT("FogInscatter"));
	}

	namespace Vectors
	{
		inline const FName SelfIllumTint(TEXT("SelfIllumTint"));
		inline const FName EnvMapTint(TEXT("EnvMapTint"));
		inline const FName TexScaleOffset(TEXT("TexScaleOffset"));
		inline const FName SineTargetMask(TEXT("SineTargetMask"));
		inline const FName SineChannelMask(TEXT("SineChannelMask"));
		// R7.1 ruling J (docs/architecture/water-architecture.md -> "Surf sine UV translate"):
		// (ampU, ampV, offU, offV), the base texture's UV slide a `sine` -> `texturetransform`
		// proxy chain authors (`objects/surf`, the sm_pier_1 wave cards). Added to the base
		// coordinate as `amp x wave + off`, `wave` being the sine lane's own 0..1 wave, so the
		// default (0,0,0,0) is neutral by construction.
		inline const FName SineUVTranslate(TEXT("SineUVTranslate"));
		// R5.4: the fog colour, Custom Primitive Data slots ElysiumFog::SlotColor .. +3.
		inline const FName FogColor(TEXT("FogColor"));
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
		// R6.3 (docs/architecture/seam_map_material.md -> "Detail sway on the model masters"):
		// the World Position Offset term's gate. Never the stage's to set -- only the map bake's
		// `MI_DetailSway_*` child of an imported instance turns it on.
		inline const FName UseDetailSway(TEXT("UseDetailSway"));
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
		// R5.4 (docs/architecture/seam_map_material.md -> "Scene fog on the world masters"):
		// Source's per-map distance fog, read off the primitive's Custom Primitive Data (slots
		// ElysiumFog::SlotStart / SlotInvRange, `mat_fog.fog_from_primitive`), never off the
		// instance -- the stage writes neither. `FogInscatter` is the instance half: 0 on an
		// Additive blend (Source fogs additive surfaces to black), 1 everywhere else.
		inline const FName FogStart(TEXT("FogStart"));
		inline const FName FogInvRange(TEXT("FogInvRange"));
		inline const FName FogInscatter(TEXT("FogInscatter"));
	}

	namespace Vectors
	{
		inline const FName EnvMapTint(TEXT("EnvMapTint"));
		inline const FName TexScaleOffset(TEXT("TexScaleOffset"));
		inline const FName CloudScale(TEXT("CloudScale"));
		inline const FName SineTargetMask(TEXT("SineTargetMask"));
		inline const FName SineChannelMask(TEXT("SineChannelMask"));
		// R5.4: the fog colour, Custom Primitive Data slots ElysiumFog::SlotColor .. +3.
		inline const FName FogColor(TEXT("FogColor"));
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
		// R6.3: as on M_V2_Lit -- every corpus detail material is `unlitgeneric`.
		inline const FName UseDetailSway(TEXT("UseDetailSway"));
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
		// R5.4 (docs/architecture/seam_map_material.md -> "Scene fog on the world masters"):
		// Source's per-map distance fog, read off the primitive's Custom Primitive Data (slots
		// ElysiumFog::SlotStart / SlotInvRange, `mat_fog.fog_from_primitive`), never off the
		// instance -- the stage writes neither. `FogInscatter` is the instance half: 0 on an
		// Additive blend (Source fogs additive surfaces to black), 1 everywhere else.
		inline const FName FogStart(TEXT("FogStart"));
		inline const FName FogInvRange(TEXT("FogInvRange"));
		inline const FName FogInscatter(TEXT("FogInscatter"));
	}

	namespace Vectors
	{
		inline const FName TexScaleOffset(TEXT("TexScaleOffset"));
		inline const FName Texture2ScaleOffset(TEXT("Texture2ScaleOffset"));
		inline const FName SineTargetMask(TEXT("SineTargetMask"));
		inline const FName SineChannelMask(TEXT("SineChannelMask"));
		// R5.4: the fog colour, Custom Primitive Data slots ElysiumFog::SlotColor .. +3.
		inline const FName FogColor(TEXT("FogColor"));
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
// the class-LUT row. `IrisFrame` is declared per the design's table but has nothing in the corpus
// to wire (SF-6's runtime lane writes it on the MID) -- declared, not wired. `VampireEyes` is
// wired against the `psh/eyes_vampire` disassembly (docs/vtmb/facial_animation.md:503): the iris
// term moves to Emissive (self-illuminated) and BaseColor keeps only the sclera, darkened by the
// iris coverage it lost -- see `make_v2_materials.py::_build_eyes`'s docstring.
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

// `M_V2_Water` -- `water` family only (22 units). Since R7.1 a Single Layer Water master
// (`docs/architecture/water-architecture.md` section 4): `UseFogEnable`/`FogColor`/`FogStart`/
// `FogEnd` are the VMT's own water-fog keys and become the SLW volume's scattering/absorption
// (`WaterFogScale / ((FogEnd - FogStart) x 2.54)` per cm, split by the decoded colour);
// `RefractTint` is Color Scale Behind Water; `ReflectTint`'s luma scales the class specular;
// `Underside` (the `$bottommaterial` instance) zeroes specular and extinction; `CheapWater`
// multiplies the extinction by 16. No `BottomMaterial` slot (`$bottommaterial` names a material,
// not a texture -- provenance, and the source of `Underside`). Declared, not wired: `DuDvMap`,
// `RefractAmount`/`ReflectAmount`, `BaseReflectFract`, `UseEnvMap`, the wave-animation scalars,
// `CheapWaterStartDistance/EndDistance` and `WaterDepth` (see `_build_water`'s docstring).
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
		// R7.1 (`water-architecture.md` ruling E): the `$bottommaterial` instance -- a water unit
		// whose `$bottommaterial` names itself (`dev/dev_waterbeneath2`). The engine strips the
		// reflection from every down-facing water face and 5.8's SLW has no camera-under-water
		// branch, so the underside draws with zero specular and zero volume extinction.
		inline const FName Underside(TEXT("Underside"));
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
		// R5.4 (docs/architecture/seam_map_material.md -> "Scene fog on the world masters"):
		// Source's per-map distance fog, read off the primitive's Custom Primitive Data (slots
		// ElysiumFog::SlotStart / SlotInvRange, `mat_fog.fog_from_primitive`), never off the
		// instance -- the stage writes neither. `FogInscatter` is the instance half: 0 on an
		// Additive blend (Source fogs additive surfaces to black), 1 everywhere else.
		inline const FName FogStart(TEXT("FogStart"));
		inline const FName FogInvRange(TEXT("FogInvRange"));
		inline const FName FogInscatter(TEXT("FogInscatter"));
	}

	namespace Vectors
	{
		inline const FName RefractTint(TEXT("RefractTint"));
		inline const FName EnvMapTint(TEXT("EnvMapTint"));
		// R5.4: the fog colour, Custom Primitive Data slots ElysiumFog::SlotColor .. +3.
		inline const FName FogColor(TEXT("FogColor"));
	}

	namespace Switches
	{
		inline const FName UseBaseTexture(TEXT("UseBaseTexture"));
		inline const FName UseNormalMap(TEXT("UseNormalMap"));
		inline const FName UseEnvMap(TEXT("UseEnvMap"));
		inline const FName UseFixedCube(TEXT("UseFixedCube"));
	}
}

// `M_V2_Decal` -- R7.2 (docs/project/seam_migration.md -> "R7.2 Decals", ruling 1;
// docs/architecture/seam_map_material.md -> "M_V2_Decal"): the projector master for every
// `$decal`/`decalmodulate` unit, re-cut to `MD_DeferredDecal` / `BLEND_Translucent` / DefaultLit
// -- the one deferred-decal domain a `UDecalComponent` actually draws (`FDeferredDecalProxy`
// substitutes the engine default for anything else, and DBuffer rewrites a would-be Modulate to
// Translucent regardless, `DecalRenderingCommon.cpp` 47-49). `BaseTexture` RGB x `Color` (shared)
// -> BaseColor, `BaseTexture` A x `Alpha` (shared) -> Opacity, `Emissive` x `EmissiveScale`
// (default 0, the 3 `$selfillum` units) -> EmissiveColor. `Unlit` (the 28 `unlitgeneric`
// projector units) routes the fogged base colour into Emissive and leaves BaseColor black --
// DefaultLit has no unlit shading model to fall back to. Roughness, Specular, Metallic and
// Normal are deliberately NOT connected: the wall keeps its own surface under the decal, which is
// what a lightmapped `$decal` face did. `DecalDepthOffset` is not on this master and is not a
// knob anywhere else either: ruling 3 retires it outright (`UElysiumSurfaceSettings`,
// `MPC_ElysiumSurfaces`, the knob test) -- the `isDecalSurface` mesh-decal pass has nothing left
// to bias once the projector geometry itself is coplanar with the wall. `UseVertexColor` is
// dropped: a projected decal has no vertex colour.
namespace ElysiumSurfaceParamsDecal
{
	namespace Textures
	{
		inline const FName BaseTexture(TEXT("BaseTexture"));
		inline const FName Emissive(TEXT("Emissive"));
	}

	// R5.3 (docs/architecture/seam_map_material.md -> "Decal fog and wetness homes"), unchanged
	// by the R7.2 re-cut: the world's own distance fog as three named instance parameters -- a
	// UDecalComponent carries no Custom Primitive Data of its own, unlike every mesh primitive.
	// No corpus unit authors a fog key on a decalmodulate VMT, so the stage never writes these;
	// the placement lane sets them per decal instance (an MID at load,
	// ElysiumFog::ApplyToDecalMID) from the map's own `UElysiumMapEnvironment` (R4.4) fog, never
	// from a per-map material package. Fade reaches BaseColor only; fade + inscatter reaches
	// Emissive -- the fog specular kill is dropped along with Roughness/Specular/Metallic/Normal.
	namespace Scalars
	{
		inline const FName EmissiveScale(TEXT("EmissiveScale"));
		inline const FName FogStart(TEXT("FogStart"));
		inline const FName FogInvRange(TEXT("FogInvRange"));
	}

	namespace Vectors
	{
		inline const FName FogColor(TEXT("FogColor"));
	}

	namespace Switches
	{
		inline const FName Unlit(TEXT("Unlit"));
	}
}
