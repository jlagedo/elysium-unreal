#include "ElysiumMaterialFactory.h"

#include "ElysiumObjModel.h"
#include "ElysiumTextureCache.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/StrongObjectPtr.h"

// Emissive brightness for $selfillum surfaces (map_Ke). Multiplies M_VtMB_World's
// EmissiveScale param (which defaults to 0, so non-selfillum surfaces never glow).
// Read at material-build time, so re-travel to A/B a value.
static TAutoConsoleVariable<float> CVarEmissiveScale(
	TEXT("elysium.EmissiveScale"), 1.5f,
	TEXT("LightRig-independent self-illum (map_Ke) emissive brightness on M_VtMB_World."),
	ECVF_Default);

namespace
{
	constexpr const TCHAR* MasterPath = TEXT("/Game/VtMB/Materials/M_VtMB_World.M_VtMB_World");
	const FName EmissiveParam(TEXT("Emissive"));
	const FName EmissiveScaleParam(TEXT("EmissiveScale"));

	// The hand-authored master material, loaded once and kept alive by a strong ref.
	UMaterialInterface* GetMaster()
	{
		static TStrongObjectPtr<UMaterialInterface> Master;
		if (!Master.IsValid())
		{
			if (UMaterialInterface* Loaded = LoadObject<UMaterialInterface>(nullptr, MasterPath))
			{
				Master.Reset(Loaded);
			}
		}
		return Master.Get();
	}

	// The master's albedo texture slot, discovered once by reflection. The master carries two
	// texture parameters (albedo + Emissive); the albedo is "the one that isn't Emissive", so
	// this stays correct regardless of the parameter enumeration order.
	FName GetAlbedoParamName(UMaterialInterface* Master)
	{
		static FName Cached = NAME_None;
		static bool bResolved = false;
		if (!bResolved && Master)
		{
			TArray<FMaterialParameterInfo> Infos;
			TArray<FGuid> Ids;
			Master->GetAllTextureParameterInfo(Infos, Ids);
			for (const FMaterialParameterInfo& Info : Infos)
			{
				if (Info.Name != EmissiveParam)
				{
					Cached = Info.Name;
					break;
				}
			}
			bResolved = true;
		}
		return Cached;
	}
}

UMaterialInstanceDynamic* FElysiumMaterialFactory::Build(const FElysiumMaterialDef* Def, const FString& Dir, UObject* Outer)
{
	UMaterialInterface* Master = GetMaster();
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

	UTexture2D* Albedo = nullptr;
	if (Def && !Def->Albedo.IsEmpty())
	{
		Albedo = FElysiumTextureCache::LoadTex(Dir, Def->Albedo);
	}
	if (!Albedo)
	{
		Albedo = FElysiumTextureCache::SolidTex(Def ? Def->Color : FLinearColor(0.6f, 0.6f, 0.65f));
	}

	const FName ParamName = GetAlbedoParamName(Master);
	if (ParamName != NAME_None && Albedo)
	{
		Mid->SetTextureParameterValue(ParamName, Albedo);
	}

	// $selfillum (map_Ke): bind the alpha-masked emission map and switch the master's
	// EmissiveScale on. Surfaces without an emissive map leave EmissiveScale at its 0
	// default, so they never glow.
	if (Def && !Def->Emissive.IsEmpty())
	{
		if (UTexture2D* EmisTex = FElysiumTextureCache::LoadTex(Dir, Def->Emissive))
		{
			Mid->SetTextureParameterValue(EmissiveParam, EmisTex);
			Mid->SetScalarParameterValue(EmissiveScaleParam,
				FMath::Max(0.f, CVarEmissiveScale.GetValueOnAnyThread()));
		}
	}

	return Mid;
}
