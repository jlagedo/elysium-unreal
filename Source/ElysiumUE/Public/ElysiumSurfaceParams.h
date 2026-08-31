#pragma once

#include "CoreMinimal.h"

// The exposed-parameter contract for the V2 material-import masters (SF-4.3,
// docs/architecture/seam_map_material.md -> "Import" -> "Exposed parameters, by master"). The
// stage (SF-4.4) writes exactly these names onto an imported MI_ instance and the masters
// (`pipeline/unreal/make_v2_materials.py`) expose exactly these names; a name on one side and not
// the other is a build error, not a silent default. Declared here, rather than as string literals
// at each call site, so the generator, the runtime importer (SF-4.5) and
// `Elysium.Policy.V2MasterParams` (Private/Tests/ElysiumContentTests.cpp) read the same FNames.
//
// Only M_V2_Lit is declared so far -- the other eight V2 families land family-by-family
// (mechanics doc "Ordering, concurrency, tests, risks").
//
// Every V2 master additionally exposes `SurfaceClassIndex` and reads `MPC_ElysiumSurfaces`
// (SF-4.1, not yet landed); those two shared names live in `ElysiumSurfaceParamsShared` below
// rather than duplicated per family.
namespace ElysiumSurfaceParamsShared
{
	inline const FName SurfaceClassIndex(TEXT("SurfaceClassIndex"));
	inline const FName SurfaceClassLUT(TEXT("SurfaceClassLUT"));
}

// `M_V2_Lit` / `M_V2_LitTranslucent` (the same graph; LitTranslucent differs only in material
// domain/blend, not in parameter set) -- the design doc's exposed-parameter table for the two
// masters, verbatim.
namespace ElysiumSurfaceParamsLit
{
	namespace Textures
	{
		inline const FName BaseTexture(TEXT("BaseTexture"));
		inline const FName Detail(TEXT("Detail"));
		inline const FName NormalMap(TEXT("NormalMap"));
		inline const FName EnvMapMask(TEXT("EnvMapMask"));
		inline const FName EnvMap(TEXT("EnvMap"));
	}

	namespace Scalars
	{
		inline const FName Alpha(TEXT("Alpha"));
		inline const FName SelfIllumAmount(TEXT("SelfIllumAmount"));
		inline const FName DetailScale(TEXT("DetailScale"));
		inline const FName EnvMapMaskScale(TEXT("EnvMapMaskScale"));
		inline const FName BumpScale(TEXT("BumpScale"));
		inline const FName MinLight(TEXT("MinLight"));
		inline const FName MaxLight(TEXT("MaxLight"));
		inline const FName FrameRate(TEXT("FrameRate"));
		inline const FName ScrollRateU(TEXT("ScrollRateU"));
		inline const FName ScrollRateV(TEXT("ScrollRateV"));
		inline const FName SineMin(TEXT("SineMin"));
		inline const FName SineMax(TEXT("SineMax"));
		inline const FName SinePeriod(TEXT("SinePeriod"));
		inline const FName SineTimeOffset(TEXT("SineTimeOffset"));
		inline const FName WetnessScale(TEXT("WetnessScale"));
		inline const FName DecalDepthOffset(TEXT("DecalDepthOffset"));
	}

	namespace Vectors
	{
		inline const FName Color(TEXT("Color"));
		inline const FName SelfIllumTint(TEXT("SelfIllumTint"));
		inline const FName EnvMapTint(TEXT("EnvMapTint"));
		inline const FName TexScaleOffset(TEXT("TexScaleOffset"));
	}

	// Named `bUse...` nowhere in the design's own table (docs/architecture/seam_map_material.md
	// -> "Exposed parameters, by master") -- see the generator's module docstring for the
	// naming-convention conflict this resolves against `phase4_mechanics.md` section 3d.
	namespace Switches
	{
		inline const FName UseBaseTexture(TEXT("UseBaseTexture"));
		inline const FName UseDetail(TEXT("UseDetail"));
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
		inline const FName UseScroll(TEXT("UseScroll"));
		inline const FName IsDecalSurface(TEXT("IsDecalSurface"));
	}
}
