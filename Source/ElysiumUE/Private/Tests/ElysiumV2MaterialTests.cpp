// Elysium.Policy.V2MasterParams -- SF-4.3's binding-contract check for the V2 material-import
// masters (`pipeline/unreal/make_v2_materials.py`,
// docs/architecture/seam_map_material.md -> "Import" -> "Exposed parameters, by master"). The
// stage (SF-4.4) writes exactly the names in `ElysiumSurfaceParams.h` onto an imported MI_
// instance; a master that drifts off one of them fails an import silently at runtime unless
// something asserts the binding here, in a generated-content tier that needs no corpus export.
//
// Deliberately its own translation unit, not folded into ElysiumContentTests.cpp: SF-4.3
// (this file) and other in-flight work land in the same window and touch that file for unrelated
// reasons, so a new file is the low-conflict surface. Not built or run by the agent that wrote
// this revision either -- see `docs/project/seam_migration.md` for the concurrency rule (no
// editor/engine build runs alongside another agent's). The next agent to touch Source/ builds
// and runs this tier; the parameter lists below are unverified against a real compile until then.
//
// Masters land family-by-family (mechanics doc "Ordering, concurrency, tests, risks"); this test
// loads whichever of the ten V2 masters already exists and abstains on the rest, so it grows
// coverage as `make_v2_materials.py` grows without needing a matching edit here.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumFog.h"             // ElysiumLightStyle::ParameterName -- the CPD 6 scalar
#include "ElysiumSurfaceParams.h"
#include "Materials/MaterialInterface.h"

static constexpr EAutomationTestFlags GElysiumV2MaterialTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	struct FElysiumV2MasterCase
	{
		const TCHAR* Path;
		TArrayView<const FName> TextureParams;
		TArrayView<const FName> ScalarParams;
		TArrayView<const FName> VectorParams;
		TArrayView<const FName> SwitchParams;
	};

	// Every declared name for M_V2_Lit / M_V2_LitTranslucent, read off ElysiumSurfaceParams.h
	// rather than restated as string literals -- a rename there is a compile error here, not a
	// silently stale test. Each master's own array below inlines the four
	// ElysiumSurfaceParamsShared names ("on every master") directly rather than concatenating a
	// separate shared array, since TArrayView gives no cheap way to concatenate two fixed arrays
	// at file scope.
	static const FName LitTextures[] = {
		ElysiumSurfaceParamsShared::Textures::SurfaceClassLUT,
		ElysiumSurfaceParamsLit::Textures::BaseTexture,
		ElysiumSurfaceParamsLit::Textures::NormalMap,
		ElysiumSurfaceParamsLit::Textures::EnvMapMask,
		ElysiumSurfaceParamsLit::Textures::EnvMap,
		ElysiumSurfaceParamsLit::Textures::BaseTextureFrames,
		ElysiumSurfaceParamsLit::Textures::NormalMapFrames,
	};
	static const FName LitScalars[] = {
		ElysiumSurfaceParamsShared::Scalars::SurfaceClassIndex,
		ElysiumSurfaceParamsShared::Scalars::Alpha,
		ElysiumSurfaceParamsLit::Scalars::SelfIllumAmount,
		ElysiumSurfaceParamsLit::Scalars::EnvMapMaskScale,
		ElysiumSurfaceParamsLit::Scalars::BumpScale,
		ElysiumSurfaceParamsLit::Scalars::BaseScrollRateU,
		ElysiumSurfaceParamsLit::Scalars::BaseScrollRateV,
		ElysiumSurfaceParamsLit::Scalars::BumpScrollRateU,
		ElysiumSurfaceParamsLit::Scalars::BumpScrollRateV,
		ElysiumSurfaceParamsLit::Scalars::FrameRate,
		ElysiumSurfaceParamsLit::Scalars::FrameCount,
		ElysiumSurfaceParamsLit::Scalars::NormalFrameRate,
		ElysiumSurfaceParamsLit::Scalars::NormalFrameCount,
		ElysiumSurfaceParamsLit::Scalars::SineMin,
		ElysiumSurfaceParamsLit::Scalars::SineMax,
		ElysiumSurfaceParamsLit::Scalars::SinePeriod,
		ElysiumSurfaceParamsLit::Scalars::SineTimeOffset,
		ElysiumSurfaceParamsLit::Scalars::WetnessScale,
		ElysiumSurfaceParamsLit::Scalars::WetnessDriven,
		ElysiumSurfaceParamsLit::Scalars::FogStart,
		ElysiumSurfaceParamsLit::Scalars::FogInvRange,
		ElysiumSurfaceParamsLit::Scalars::FogInscatter,
		// R7.4 (G6, owner decision 4): the lightstyle brightness, Custom Primitive Data slot
		// ElysiumLightStyle::SlotBrightness. Named through the runtime's own slot contract rather
		// than through ElysiumSurfaceParams, because the slot and the name are one fact and the
		// rig writes the slot -- the header mirror is the material stage's copy of the same string,
		// pinned equal to this one in `Elysium.Substrate.Water`.
		ElysiumLightStyle::ParameterName,
		// R7.5 G5: `$envmapcontrast`, restored as a named modernization on the envmap path.
		ElysiumSurfaceParamsLit::Scalars::EnvMapContrast,
	};
	static const FName LitVectors[] = {
		ElysiumSurfaceParamsShared::Vectors::Color,
		ElysiumSurfaceParamsLit::Vectors::SelfIllumTint,
		ElysiumSurfaceParamsLit::Vectors::EnvMapTint,
		ElysiumSurfaceParamsLit::Vectors::TexScaleOffset,
		ElysiumSurfaceParamsLit::Vectors::SineTargetMask,
		ElysiumSurfaceParamsLit::Vectors::SineChannelMask,
		ElysiumSurfaceParamsLit::Vectors::FogColor,
	};
	static const FName LitSwitches[] = {
		ElysiumSurfaceParamsLit::Switches::UseBaseTexture,
		ElysiumSurfaceParamsLit::Switches::UseNormalMap,
		ElysiumSurfaceParamsLit::Switches::UseSelfIllum,
		ElysiumSurfaceParamsLit::Switches::UseVertexColor,
		ElysiumSurfaceParamsLit::Switches::UseVertexAlpha,
		ElysiumSurfaceParamsLit::Switches::UseEnvMap,
		ElysiumSurfaceParamsLit::Switches::UseEnvMapMask,
		ElysiumSurfaceParamsLit::Switches::UseBaseAlphaEnvMapMask,
		ElysiumSurfaceParamsLit::Switches::UseNormalMapAlphaEnvMapMask,
		ElysiumSurfaceParamsLit::Switches::UseFixedCube,
		ElysiumSurfaceParamsLit::Switches::MetallicTint,
		ElysiumSurfaceParamsLit::Switches::UseAnimatedFrames,
		ElysiumSurfaceParamsLit::Switches::UseAnimatedNormalFrames,
		ElysiumSurfaceParamsLit::Switches::UseDetailSway,
	};

	static const FName UnlitTextures[] = {
		ElysiumSurfaceParamsShared::Textures::SurfaceClassLUT,
		ElysiumSurfaceParamsUnlit::Textures::BaseTexture,
		ElysiumSurfaceParamsUnlit::Textures::EnvMapMask,
		ElysiumSurfaceParamsUnlit::Textures::EnvMap,
		ElysiumSurfaceParamsUnlit::Textures::CloudAlphaTexture,
		ElysiumSurfaceParamsUnlit::Textures::BaseTextureFrames,
	};
	static const FName UnlitScalars[] = {
		ElysiumSurfaceParamsShared::Scalars::SurfaceClassIndex,
		ElysiumSurfaceParamsShared::Scalars::Alpha,
		ElysiumSurfaceParamsUnlit::Scalars::EnvMapMaskScale,
		ElysiumSurfaceParamsUnlit::Scalars::BaseScrollRateU,
		ElysiumSurfaceParamsUnlit::Scalars::BaseScrollRateV,
		ElysiumSurfaceParamsUnlit::Scalars::FrameRate,
		ElysiumSurfaceParamsUnlit::Scalars::FrameCount,
		ElysiumSurfaceParamsUnlit::Scalars::SineMin,
		ElysiumSurfaceParamsUnlit::Scalars::SineMax,
		ElysiumSurfaceParamsUnlit::Scalars::SinePeriod,
		ElysiumSurfaceParamsUnlit::Scalars::SineTimeOffset,
		ElysiumSurfaceParamsUnlit::Scalars::FogStart,
		ElysiumSurfaceParamsUnlit::Scalars::FogInvRange,
		ElysiumSurfaceParamsUnlit::Scalars::FogInscatter,
	};
	static const FName UnlitVectors[] = {
		ElysiumSurfaceParamsShared::Vectors::Color,
		ElysiumSurfaceParamsUnlit::Vectors::EnvMapTint,
		ElysiumSurfaceParamsUnlit::Vectors::TexScaleOffset,
		ElysiumSurfaceParamsUnlit::Vectors::CloudScale,
		ElysiumSurfaceParamsUnlit::Vectors::SineTargetMask,
		ElysiumSurfaceParamsUnlit::Vectors::SineChannelMask,
		ElysiumSurfaceParamsUnlit::Vectors::FogColor,
	};
	static const FName UnlitSwitches[] = {
		ElysiumSurfaceParamsUnlit::Switches::UseBaseTexture,
		ElysiumSurfaceParamsUnlit::Switches::UseVertexColor,
		ElysiumSurfaceParamsUnlit::Switches::UseVertexAlpha,
		ElysiumSurfaceParamsUnlit::Switches::UseEnvMap,
		ElysiumSurfaceParamsUnlit::Switches::UseEnvMapMask,
		ElysiumSurfaceParamsUnlit::Switches::UseBaseAlphaEnvMapMask,
		ElysiumSurfaceParamsUnlit::Switches::UseFixedCube,
		ElysiumSurfaceParamsUnlit::Switches::MetallicTint,
		ElysiumSurfaceParamsUnlit::Switches::UseAnimatedFrames,
		ElysiumSurfaceParamsUnlit::Switches::UseCloudAlpha,
		ElysiumSurfaceParamsUnlit::Switches::UseDetailSway,
	};

	static const FName TwoTextureTextures[] = {
		ElysiumSurfaceParamsShared::Textures::SurfaceClassLUT,
		ElysiumSurfaceParamsTwoTexture::Textures::BaseTexture,
		ElysiumSurfaceParamsTwoTexture::Textures::BaseTexture2,
		ElysiumSurfaceParamsTwoTexture::Textures::NormalMap,
	};
	static const FName TwoTextureScalars[] = {
		ElysiumSurfaceParamsShared::Scalars::SurfaceClassIndex,
		ElysiumSurfaceParamsShared::Scalars::Alpha,
		ElysiumSurfaceParamsTwoTexture::Scalars::AlphaBias,
		ElysiumSurfaceParamsTwoTexture::Scalars::BaseScrollRateU,
		ElysiumSurfaceParamsTwoTexture::Scalars::BaseScrollRateV,
		ElysiumSurfaceParamsTwoTexture::Scalars::SineMin,
		ElysiumSurfaceParamsTwoTexture::Scalars::SineMax,
		// R7.4 G6: the styled blend sections on `sm_pier_1` / `sp_soc_3` bind this master, so it
		// reads the same slot-6 brightness the Lit pair and Water do.
		ElysiumLightStyle::ParameterName,
		ElysiumSurfaceParamsTwoTexture::Scalars::SinePeriod,
		ElysiumSurfaceParamsTwoTexture::Scalars::SineTimeOffset,
		ElysiumSurfaceParamsTwoTexture::Scalars::FogStart,
		ElysiumSurfaceParamsTwoTexture::Scalars::FogInvRange,
		ElysiumSurfaceParamsTwoTexture::Scalars::FogInscatter,
	};
	static const FName TwoTextureVectors[] = {
		ElysiumSurfaceParamsShared::Vectors::Color,
		ElysiumSurfaceParamsTwoTexture::Vectors::TexScaleOffset,
		ElysiumSurfaceParamsTwoTexture::Vectors::Texture2ScaleOffset,
		ElysiumSurfaceParamsTwoTexture::Vectors::SineTargetMask,
		ElysiumSurfaceParamsTwoTexture::Vectors::SineChannelMask,
		ElysiumSurfaceParamsTwoTexture::Vectors::FogColor,
	};
	static const FName TwoTextureSwitches[] = {
		ElysiumSurfaceParamsTwoTexture::Switches::UseBaseTexture2,
		ElysiumSurfaceParamsTwoTexture::Switches::UseNormalMap,
		ElysiumSurfaceParamsTwoTexture::Switches::UseBumpOnBaseTexture2,
		ElysiumSurfaceParamsTwoTexture::Switches::UseVertexColor,
		ElysiumSurfaceParamsTwoTexture::Switches::UseVertexAlpha,
	};

	static const FName EyesTextures[] = {
		ElysiumSurfaceParamsShared::Textures::SurfaceClassLUT,
		ElysiumSurfaceParamsEyes::Textures::BaseTexture,
		ElysiumSurfaceParamsEyes::Textures::Iris,
		ElysiumSurfaceParamsEyes::Textures::Glint,
	};
	static const FName EyesScalars[] = {
		ElysiumSurfaceParamsShared::Scalars::SurfaceClassIndex,
		ElysiumSurfaceParamsShared::Scalars::Alpha,
		ElysiumSurfaceParamsEyes::Scalars::IrisFrame,
	};
	static const FName EyesVectors[] = {
		ElysiumSurfaceParamsShared::Vectors::Color,
	};
	static const FName EyesSwitches[] = {
		ElysiumSurfaceParamsEyes::Switches::VampireEyes,
		ElysiumSurfaceParamsEyes::Switches::UseGlint,
	};

	static const FName WaterTextures[] = {
		ElysiumSurfaceParamsShared::Textures::SurfaceClassLUT,
		ElysiumSurfaceParamsWater::Textures::BaseTexture,
		ElysiumSurfaceParamsWater::Textures::DuDvMap,
		ElysiumSurfaceParamsWater::Textures::NormalMap,
		ElysiumSurfaceParamsWater::Textures::EnvMap,
		ElysiumSurfaceParamsWater::Textures::NormalMapFrames,
		// R7.5 G3: the 29-slice DUDV array, the lane the plain `DuDvMap` slot could never bind.
		ElysiumSurfaceParamsWater::Textures::DuDvMapFrames,
	};
	static const FName WaterScalars[] = {
		ElysiumSurfaceParamsShared::Scalars::SurfaceClassIndex,
		ElysiumSurfaceParamsShared::Scalars::Alpha,
		ElysiumSurfaceParamsWater::Scalars::RefractAmount,
		ElysiumSurfaceParamsWater::Scalars::ReflectAmount,
		ElysiumSurfaceParamsWater::Scalars::BaseReflectFract,
		ElysiumSurfaceParamsWater::Scalars::WaterDepth,
		ElysiumSurfaceParamsWater::Scalars::WaterMurkiness,
		ElysiumSurfaceParamsWater::Scalars::WaterBaseFactor,
		ElysiumSurfaceParamsWater::Scalars::WaterBaseMovementDist,
		ElysiumSurfaceParamsWater::Scalars::WaterBaseMovementFreq,
		ElysiumSurfaceParamsWater::Scalars::WaterSpecularMin,
		ElysiumSurfaceParamsWater::Scalars::WaterSpecularMax,
		ElysiumSurfaceParamsWater::Scalars::WaterTimeFreq1,
		ElysiumSurfaceParamsWater::Scalars::WaterTimeFreq2,
		ElysiumSurfaceParamsWater::Scalars::WaterWaveHeight,
		ElysiumSurfaceParamsWater::Scalars::WaterWaveLength,
		ElysiumSurfaceParamsWater::Scalars::CheapWaterStartDistance,
		ElysiumSurfaceParamsWater::Scalars::CheapWaterEndDistance,
		ElysiumSurfaceParamsWater::Scalars::FogStart,
		ElysiumSurfaceParamsWater::Scalars::FogEnd,
		ElysiumSurfaceParamsWater::Scalars::BumpScrollRateU,
		ElysiumSurfaceParamsWater::Scalars::BumpScrollRateV,
		ElysiumSurfaceParamsWater::Scalars::NormalFrameRate,
		ElysiumSurfaceParamsWater::Scalars::NormalFrameCount,
		// R7.5 G3: the DUDV flipbook's own rate and count, off the shared `$bumpframe` proxy.
		ElysiumSurfaceParamsWater::Scalars::DuDvFrameRate,
		ElysiumSurfaceParamsWater::Scalars::DuDvFrameCount,
		// R7.4: the same slot-6 brightness the Lit masters carry -- the pier's foam cards are Lit,
		// but a styled water face is a face like any other.
		ElysiumLightStyle::ParameterName,
	};
	static const FName WaterVectors[] = {
		ElysiumSurfaceParamsShared::Vectors::Color,
		ElysiumSurfaceParamsWater::Vectors::WaterColor,
		ElysiumSurfaceParamsWater::Vectors::RefractTint,
		ElysiumSurfaceParamsWater::Vectors::ReflectTint,
		ElysiumSurfaceParamsWater::Vectors::FogColor,
		ElysiumSurfaceParamsWater::Vectors::EnvMapTint,
		ElysiumSurfaceParamsWater::Vectors::TexScaleOffset,
	};
	static const FName WaterSwitches[] = {
		ElysiumSurfaceParamsWater::Switches::CheapWater,
		ElysiumSurfaceParamsWater::Switches::UseFogEnable,
		ElysiumSurfaceParamsWater::Switches::UseEnvMap,
		ElysiumSurfaceParamsWater::Switches::UseFixedCube,
		ElysiumSurfaceParamsWater::Switches::UseBaseTexture,
		ElysiumSurfaceParamsWater::Switches::UseNormalMap,
		ElysiumSurfaceParamsWater::Switches::UseAnimatedNormalFrames,
		ElysiumSurfaceParamsWater::Switches::Underside,
	};

	// R7.1: `M_ElysiumUnderwater` is a post-process master, not a surface one, so it carries no
	// class LUT, no textures and no switches -- only the fog triple `ElysiumFog::Pack` fills
	// (`water-architecture.md` ruling D). The same three names the decal master declares, because
	// one packer serves the scene fog, the decals and the view under the plane.
	static const FName UnderwaterScalars[] = {
		ElysiumSurfaceParamsDecal::Scalars::FogStart,
		ElysiumSurfaceParamsDecal::Scalars::FogInvRange,
	};
	static const FName UnderwaterVectors[] = {
		ElysiumSurfaceParamsDecal::Vectors::FogColor,
	};

	static const FName SpriteTextures[] = {
		ElysiumSurfaceParamsShared::Textures::SurfaceClassLUT,
		ElysiumSurfaceParamsSprite::Textures::BaseTexture,
		ElysiumSurfaceParamsSprite::Textures::BaseTextureFrames,
	};
	static const FName SpriteScalars[] = {
		ElysiumSurfaceParamsShared::Scalars::SurfaceClassIndex,
		ElysiumSurfaceParamsShared::Scalars::Alpha,
		ElysiumSurfaceParamsSprite::Scalars::FrameRate,
		ElysiumSurfaceParamsSprite::Scalars::FrameCount,
	};
	static const FName SpriteVectors[] = {
		ElysiumSurfaceParamsShared::Vectors::Color,
	};
	static const FName SpriteSwitches[] = {
		ElysiumSurfaceParamsSprite::Switches::UseVertexColor,
		ElysiumSurfaceParamsSprite::Switches::UseVertexAlpha,
		ElysiumSurfaceParamsSprite::Switches::UseAnimatedFrames,
	};

	static const FName RefractTextures[] = {
		ElysiumSurfaceParamsShared::Textures::SurfaceClassLUT,
		ElysiumSurfaceParamsRefract::Textures::BaseTexture,
		ElysiumSurfaceParamsRefract::Textures::DuDvMap,
		ElysiumSurfaceParamsRefract::Textures::NormalMap,
		ElysiumSurfaceParamsRefract::Textures::EnvMap,
	};
	static const FName RefractScalars[] = {
		ElysiumSurfaceParamsShared::Scalars::SurfaceClassIndex,
		ElysiumSurfaceParamsShared::Scalars::Alpha,
		ElysiumSurfaceParamsRefract::Scalars::RefractAmount,
		ElysiumSurfaceParamsRefract::Scalars::FogStart,
		ElysiumSurfaceParamsRefract::Scalars::FogInvRange,
		ElysiumSurfaceParamsRefract::Scalars::FogInscatter,
	};
	static const FName RefractVectors[] = {
		ElysiumSurfaceParamsShared::Vectors::Color,
		ElysiumSurfaceParamsRefract::Vectors::RefractTint,
		ElysiumSurfaceParamsRefract::Vectors::EnvMapTint,
		ElysiumSurfaceParamsRefract::Vectors::FogColor,
	};
	static const FName RefractSwitches[] = {
		ElysiumSurfaceParamsRefract::Switches::UseBaseTexture,
		ElysiumSurfaceParamsRefract::Switches::UseNormalMap,
		ElysiumSurfaceParamsRefract::Switches::UseEnvMap,
		ElysiumSurfaceParamsRefract::Switches::UseFixedCube,
	};

	static const FName DecalTextures[] = {
		ElysiumSurfaceParamsShared::Textures::SurfaceClassLUT,
		ElysiumSurfaceParamsDecal::Textures::BaseTexture,
		ElysiumSurfaceParamsDecal::Textures::Emissive,
	};
	static const FName DecalScalars[] = {
		ElysiumSurfaceParamsShared::Scalars::SurfaceClassIndex,
		ElysiumSurfaceParamsShared::Scalars::Alpha,
		ElysiumSurfaceParamsDecal::Scalars::EmissiveScale,
		ElysiumSurfaceParamsDecal::Scalars::FogStart,
		ElysiumSurfaceParamsDecal::Scalars::FogInvRange,
	};
	static const FName DecalVectors[] = {
		ElysiumSurfaceParamsShared::Vectors::Color,
		ElysiumSurfaceParamsDecal::Vectors::FogColor,
	};
	static const FName DecalSwitches[] = {
		ElysiumSurfaceParamsDecal::Switches::Unlit,
	};

	// `M_V2_LitTranslucent` is the same graph under a different material-only property set
	// (mechanics doc / design "Master inventory" -- blend mode, two-sidedness and the opacity
	// clip value are per-instance overrides, so they never multiply masters).
	static const FElysiumV2MasterCase Cases[] = {
		{TEXT("/Game/ElysiumGenerated/Materials/V2/M_V2_Lit.M_V2_Lit"),
			LitTextures, LitScalars, LitVectors, LitSwitches},
		{TEXT("/Game/ElysiumGenerated/Materials/V2/M_V2_LitTranslucent.M_V2_LitTranslucent"),
			LitTextures, LitScalars, LitVectors, LitSwitches},
		{TEXT("/Game/ElysiumGenerated/Materials/V2/M_V2_Unlit.M_V2_Unlit"),
			UnlitTextures, UnlitScalars, UnlitVectors, UnlitSwitches},
		{TEXT("/Game/ElysiumGenerated/Materials/V2/M_V2_TwoTexture.M_V2_TwoTexture"),
			TwoTextureTextures, TwoTextureScalars, TwoTextureVectors, TwoTextureSwitches},
		{TEXT("/Game/ElysiumGenerated/Materials/V2/M_V2_Eyes.M_V2_Eyes"),
			EyesTextures, EyesScalars, EyesVectors, EyesSwitches},
		{TEXT("/Game/ElysiumGenerated/Materials/V2/M_V2_Water.M_V2_Water"),
			WaterTextures, WaterScalars, WaterVectors, WaterSwitches},
		{TEXT("/Game/ElysiumGenerated/Materials/V2/M_V2_Sprite.M_V2_Sprite"),
			SpriteTextures, SpriteScalars, SpriteVectors, SpriteSwitches},
		{TEXT("/Game/ElysiumGenerated/Materials/V2/M_V2_Refract.M_V2_Refract"),
			RefractTextures, RefractScalars, RefractVectors, RefractSwitches},
		{TEXT("/Game/ElysiumGenerated/Materials/V2/M_V2_Decal.M_V2_Decal"),
			DecalTextures, DecalScalars, DecalVectors, DecalSwitches},
		{TEXT("/Game/ElysiumGenerated/Materials/V2/M_ElysiumUnderwater.M_ElysiumUnderwater"),
			{}, UnderwaterScalars, UnderwaterVectors, {}},
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumV2MasterParamsTest,
	"Elysium.Policy.V2MasterParams", GElysiumV2MaterialTestFlags)
bool FElysiumV2MasterParamsTest::RunTest(const FString&)
{
	int32 MastersChecked = 0;
	for (const FElysiumV2MasterCase& Case : Cases)
	{
		UMaterialInterface* Master = LoadObject<UMaterialInterface>(nullptr, Case.Path);
		if (!Master)
		{
			// Not every family lands in the same slice; a master that does not exist yet is
			// not this test's failure. See the file header.
			continue;
		}
		++MastersChecked;

		for (const FName& Param : Case.TextureParams)
		{
			UTexture* Texture = nullptr;
			TestTrue(*FString::Printf(TEXT("%s carries texture %s"), Case.Path, *Param.ToString()),
				Master->GetTextureParameterValue(Param, Texture));
		}
		for (const FName& Param : Case.ScalarParams)
		{
			float Value = 0.f;
			TestTrue(*FString::Printf(TEXT("%s carries scalar %s"), Case.Path, *Param.ToString()),
				Master->GetScalarParameterValue(Param, Value));
		}
		for (const FName& Param : Case.VectorParams)
		{
			FLinearColor Value = FLinearColor::Black;
			TestTrue(*FString::Printf(TEXT("%s carries vector %s"), Case.Path, *Param.ToString()),
				Master->GetVectorParameterValue(Param, Value));
		}

		// `GetAllStaticSwitchParameterInfo` is `WITH_EDITORONLY_DATA`-gated
		// (`MaterialInterface.cpp`) -- static-switch parameter metadata is editor-only, unlike the
		// texture/scalar/vector getters above, which are compiled for every configuration. This
		// tier otherwise runs in the editor process, where the guard is always true, but the
		// minor is one line and removes the assumption.
#if WITH_EDITORONLY_DATA
		TArray<FMaterialParameterInfo> Switches;
		TArray<FGuid> SwitchIds;
		Master->GetAllStaticSwitchParameterInfo(Switches, SwitchIds);
		for (const FName& Param : Case.SwitchParams)
		{
			TestTrue(*FString::Printf(TEXT("%s carries static switch %s"), Case.Path, *Param.ToString()),
				Switches.ContainsByPredicate([&Param](const FMaterialParameterInfo& Info)
				{
					return Info.Name == Param;
				}));
		}
#endif // WITH_EDITORONLY_DATA

		// A second, independent compile-error check via `GetMaterialResource` was tried here
		// (SF-4.3 part 3, run live for the first time) and removed: every tier in this project
		// runs `-nullrhi` (`Source/ElysiumUE/CLAUDE.md` -> "Tests": "no test covers a rendered
		// frame"), under which `GetMaterialResource` returns null for every master uniformly,
		// confirmed live -- not a defect in any one master's graph, and not something this tier
		// can ever observe. Emitting an abstain marker per master here tripped the runner's own
		// "a tier that abstained entirely" rule (`.claude/rules/tests.md`) even though the
		// parameter-presence checks above, which do not depend on a compiled resource, ran and
		// passed for all nine -- so the compile-error half of the gate is not reproduced in this
		// tier at all. Compile-error coverage for the V2 masters comes from the editor build log
		// during generation instead (`mel.recompile_material` inside `make_v2_materials.py`,
		// which does return a real error list when the umbrella launch carries
		// `-AllowCommandletRendering`, already fixed in `unreal.py`'s `generate_policy_content`).
	}

	if (MastersChecked == 0)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no V2 master is generated yet "
			"(run make_v2_materials.py via uv run elysium export bundle policy)"));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
