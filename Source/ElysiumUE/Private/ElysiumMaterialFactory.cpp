#include "ElysiumMaterialFactory.h"

#include "ElysiumObjModel.h"
#include "ElysiumTextureCache.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	constexpr const TCHAR* MasterPath = TEXT("/Game/VtMB/Materials/M_VtMB_World.M_VtMB_World");

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

	// First texture parameter on the master (its albedo slot), discovered once.
	FName GetAlbedoParamName(UMaterialInterface* Master)
	{
		static FName Cached = NAME_None;
		static bool bResolved = false;
		if (!bResolved && Master)
		{
			TArray<FMaterialParameterInfo> Infos;
			TArray<FGuid> Ids;
			Master->GetAllTextureParameterInfo(Infos, Ids);
			if (Infos.Num() > 0)
			{
				Cached = Infos[0].Name;
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

	return Mid;
}
