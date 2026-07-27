#include "Visual/ElysiumMaterialFactory.h"

#include "Visual/ElysiumObjModel.h"
#include "ElysiumReflections.h"
#include "Visual/ElysiumTextureCache.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/StrongObjectPtr.h"

// Emissive brightness for $selfillum surfaces (map_Ke). Multiplies the world master's
// EmissiveScale param (which defaults to 0, so non-selfillum surfaces never glow).
// Read at material-build time, so re-travel to A/B a value.
static TAutoConsoleVariable<float> CVarEmissiveScale(
	TEXT("elysium.EmissiveScale"), 1.5f,
	TEXT("LightRig-independent self-illum (map_Ke) emissive brightness on the world masters."),
	ECVF_Default);

// $bumpmap strength: blends the master's Normal from flat (0) to the full tangent-space normal (1).
static TAutoConsoleVariable<float> CVarBumpScale(
	TEXT("elysium.BumpScale"), 1.0f,
	TEXT("$bumpmap normal-map strength on the world masters (0 = flat, 1 = full)."),
	ECVF_Default);

// $envmap reflectivity: scales how far a reflective surface's Roughness drops toward glossy, so
// the Lumen reflection appears. 0 disables the reflection channel entirely (A/B the whole path).
static TAutoConsoleVariable<float> CVarEnvReflect(
	TEXT("elysium.EnvReflect"), 1.0f,
	TEXT("$envmap reflectivity (Lumen roughness path) on the world masters. 0 = matte (off)."),
	ECVF_Default);

namespace
{
	// The hand-authored masters (make_world_materials.py). Selected per surface by the OBJ
	// material's blend flags.
	constexpr const TCHAR* OpaqueMasterPath = TEXT("/Game/VtMB/Materials/M_World_Opaque.M_World_Opaque");
	constexpr const TCHAR* MaskedMasterPath = TEXT("/Game/VtMB/Materials/M_World_Masked.M_World_Masked");
	constexpr const TCHAR* TranslucentMasterPath = TEXT("/Game/VtMB/Materials/M_World_Translucent.M_World_Translucent");
	constexpr const TCHAR* AdditiveMasterPath = TEXT("/Game/VtMB/Materials/M_Additive.M_Additive");

	// Parameter names, identical across the four world masters (build_world_graph authors them).
	const FName AlbedoParam(TEXT("Albedo"));
	const FName EmissiveParam(TEXT("Emissive"));
	const FName EmissiveScaleParam(TEXT("EmissiveScale"));
	const FName BumpMapParam(TEXT("BumpMap"));
	const FName BumpAmountParam(TEXT("BumpAmount"));
	// The reflection channel's names live in ElysiumReflections.h, shared with the map actor's
	// live overrides and the tests, so a rename cannot drift between the three.
	const FName& EnvMaskParam = ElysiumReflections::Params::EnvMask;
	const FName& EnvStrengthParam = ElysiumReflections::Params::EnvStrength;
	const FName& EnvTintParam = ElysiumReflections::Params::EnvTint;
	const FName& MetalMaskParam = ElysiumReflections::Params::MetalMask;
	const FName& RoughBaseParam = ElysiumReflections::Params::RoughBase;
	const FName& RoughReflectParam = ElysiumReflections::Params::RoughReflect;
	const FName& SpecBaseParam = ElysiumReflections::Params::SpecBase;
	const FName& SpecReflectParam = ElysiumReflections::Params::SpecReflect;
	const FName BaseTex2Param(TEXT("BaseTex2"));
	const FName BlendAmountParam(TEXT("BlendAmount"));

	// Load a master by path once and pin it with a strong ref (indexed by path so each of the
	// four masters gets its own cached slot).
	UMaterialInterface* GetMaster(const TCHAR* Path)
	{
		static TMap<FString, TStrongObjectPtr<UMaterialInterface>> Cache;
		TStrongObjectPtr<UMaterialInterface>& Slot = Cache.FindOrAdd(Path);
		if (!Slot.IsValid())
		{
			if (UMaterialInterface* Loaded = LoadObject<UMaterialInterface>(nullptr, Path))
			{
				Slot.Reset(Loaded);
			}
		}
		return Slot.Get();
	}

	// The world master this surface's blend flags select. Additive is the most specific (unlit,
	// added onto the framebuffer); then translucent, then masked; opaque is the common default.
	const TCHAR* SelectWorldMaster(const FElysiumMaterialDef* Def)
	{
		if (Def)
		{
			if (Def->bAdditive) { return AdditiveMasterPath; }
			if (Def->bBlend)    { return TranslucentMasterPath; }
			if (Def->bScissor)  { return MaskedMasterPath; }
		}
		return OpaqueMasterPath;
	}
}

UMaterialInstanceDynamic* FElysiumMaterialFactory::Build(const FElysiumMaterialDef* Def, const FString& Dir,
	UObject* Outer, FElysiumTextureCache& Cache)
{
	UMaterialInterface* Master = GetMaster(SelectWorldMaster(Def));
	if (!Master)
	{
		// Master asset missing: fall back to the engine default so geometry still draws.
		Master = UMaterial::GetDefaultMaterial(MD_Surface);
	}

	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Master, Outer);
	if (!Mid)
	{
		return nullptr;
	}

	// Albedo: the base colour, on every master. A 1x1 Kd fallback keeps an unresolved surface drawn.
	UTexture2D* Albedo = nullptr;
	if (Def && !Def->Albedo.IsEmpty())
	{
		Albedo = Cache.LoadTex(Dir, Def->Albedo);
	}
	if (!Albedo)
	{
		Albedo = Cache.SolidTex(Def ? Def->Color : FLinearColor(0.6f, 0.6f, 0.65f));
	}
	if (Albedo)
	{
		Mid->SetTextureParameterValue(AlbedoParam, Albedo);
	}

	// The additive master is unlit (Emissive = Albedo x its own EmissiveScale default); it carries
	// none of the lit feature parameters below, so binding stops at the albedo.
	if (Def && Def->bAdditive)
	{
		return Mid;
	}

	// $selfillum (map_Ke): bind the alpha-masked emission map and switch EmissiveScale on. Surfaces
	// without an emissive map leave EmissiveScale at its 0 default, so they never glow.
	if (Def && !Def->Emissive.IsEmpty())
	{
		if (UTexture2D* EmisTex = Cache.LoadTex(Dir, Def->Emissive))
		{
			Mid->SetTextureParameterValue(EmissiveParam, EmisTex);
			Mid->SetScalarParameterValue(EmissiveScaleParam,
				FMath::Max(0.f, CVarEmissiveScale.GetValueOnAnyThread()));
		}
	}

	// $bumpmap: tangent-space normal map (loaded linear, not gamma-decoded). BumpAmount defaults to
	// 0 in the master, so a surface with no bump stays geometrically smooth.
	if (Def && !Def->Bump.IsEmpty())
	{
		if (UTexture2D* BumpTex = Cache.LoadTex(Dir, Def->Bump, /*bSRGB=*/false))
		{
			Mid->SetTextureParameterValue(BumpMapParam, BumpTex);
			Mid->SetScalarParameterValue(BumpAmountParam,
				FMath::Max(0.f, CVarBumpScale.GetValueOnAnyThread()));
		}
	}

	// $envmap: the modern Lumen path. The mask's .r raises the reflection channel -- Roughness
	// down, Specular up -- so the fully-dynamic Lumen reflection appears. A reflective surface
	// with no $envmapmask reflects uniformly, so a 1x1 white mask stands in. Masks are linear
	// reflectivity, not colour. Non-reflective surfaces never enter here, so they keep the
	// master's Lambert base (RoughBase 1 / SpecBase 0). Full term: docs/reflections.md.
	if (Def && Def->bEnvmap)
	{
		UTexture2D* Mask = Def->EnvMask.IsEmpty()
			? Cache.SolidTex(FLinearColor::White)
			: Cache.LoadTex(Dir, Def->EnvMask, /*bSRGB=*/false);
		if (Mask)
		{
			Mid->SetTextureParameterValue(EnvMaskParam, Mask);
			Mid->SetScalarParameterValue(EnvStrengthParam,
				FMath::Max(0.f, CVarEnvReflect.GetValueOnAnyThread()));
			Mid->SetScalarParameterValue(RoughBaseParam, ElysiumReflections::RoughBase);
			Mid->SetScalarParameterValue(RoughReflectParam, ElysiumReflections::RoughReflect);
			Mid->SetScalarParameterValue(SpecBaseParam, ElysiumReflections::SpecBase);
			// Grey $envmaptint dims the reflection; a chromatic one names a metal, whose
			// reflection colour lives in BaseColor via MetalMask + EnvTint. Specular is
			// ignored once Metallic is up, so the two paths do not overlap.
			if (Def->IsChromatic())
			{
				Mid->SetScalarParameterValue(MetalMaskParam, 1.f);
				Mid->SetVectorParameterValue(EnvTintParam, Def->EnvTint);
			}
			else
			{
				Mid->SetScalarParameterValue(SpecReflectParam,
					ElysiumReflections::SpecReflect * Def->TintLuma());
			}
		}
	}

	// WorldVertexTransition ($basetexture2): the master blends lerp(Albedo, BaseTex2, VertexColor.r x
	// BlendAmount). BlendAmount defaults to 0, so only a WVT surface (which also carries per-vertex
	// blend weights on COLOR.r) mixes in the second texture.
	if (Def && !Def->BaseTex2.IsEmpty())
	{
		if (UTexture2D* Tex2 = Cache.LoadTex(Dir, Def->BaseTex2))
		{
			Mid->SetTextureParameterValue(BaseTex2Param, Tex2);
			Mid->SetScalarParameterValue(BlendAmountParam, 1.0f);
		}
	}

	return Mid;
}
