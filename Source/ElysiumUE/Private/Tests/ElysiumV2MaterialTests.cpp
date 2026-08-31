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
// loads whichever of the nine V2 masters already exists and abstains on the rest, so it grows
// coverage as `make_v2_materials.py` grows without needing a matching edit here.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumSurfaceParams.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
#include "MaterialShared.h"
#include "RHIDefinitions.h"

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
	};
	static const FName LitVectors[] = {
		ElysiumSurfaceParamsShared::Vectors::Color,
		ElysiumSurfaceParamsLit::Vectors::SelfIllumTint,
		ElysiumSurfaceParamsLit::Vectors::EnvMapTint,
		ElysiumSurfaceParamsLit::Vectors::TexScaleOffset,
		ElysiumSurfaceParamsLit::Vectors::SineTargetMask,
		ElysiumSurfaceParamsLit::Vectors::SineChannelMask,
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
	};
	static const FName UnlitVectors[] = {
		ElysiumSurfaceParamsShared::Vectors::Color,
		ElysiumSurfaceParamsUnlit::Vectors::EnvMapTint,
		ElysiumSurfaceParamsUnlit::Vectors::TexScaleOffset,
		ElysiumSurfaceParamsUnlit::Vectors::CloudScale,
		ElysiumSurfaceParamsUnlit::Vectors::SineTargetMask,
		ElysiumSurfaceParamsUnlit::Vectors::SineChannelMask,
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
		ElysiumSurfaceParamsTwoTexture::Scalars::SinePeriod,
		ElysiumSurfaceParamsTwoTexture::Scalars::SineTimeOffset,
	};
	static const FName TwoTextureVectors[] = {
		ElysiumSurfaceParamsShared::Vectors::Color,
		ElysiumSurfaceParamsTwoTexture::Vectors::TexScaleOffset,
		ElysiumSurfaceParamsTwoTexture::Vectors::Texture2ScaleOffset,
		ElysiumSurfaceParamsTwoTexture::Vectors::SineTargetMask,
		ElysiumSurfaceParamsTwoTexture::Vectors::SineChannelMask,
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

		// The compile gate this test closes: `UMaterialEditingLibrary::RecompileMaterial` only
		// ever returns a real error list when the editor process that authored the asset was
		// launched with `-AllowCommandletRendering` (an independent review of
		// `make_v2_materials.py` found the umbrella launch was missing it, since fixed in
		// `pipeline/src/elysium_pipeline/unreal.py`'s `generate_policy_content`). This tier runs
		// with rendering, so asserting the saved master's own compiled resource carries no
		// compile error is a second, independent check that does not depend on that launch flag
		// having been present when the asset was generated.
		if (UMaterial* MasterMaterial = Cast<UMaterial>(Master))
		{
			const FMaterialResource* Resource = MasterMaterial->GetMaterialResource(GMaxRHIShaderPlatform);
			if (TestNotNull(*FString::Printf(TEXT("%s has a material resource"), Case.Path), Resource))
			{
				TestTrue(*FString::Printf(TEXT("%s compiles with no errors"), Case.Path),
					Resource->GetCompileErrors().IsEmpty());
			}
		}
	}

	if (MastersChecked == 0)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no V2 master is generated yet "
			"(run make_v2_materials.py via uv run elysium export bundle policy)"));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
