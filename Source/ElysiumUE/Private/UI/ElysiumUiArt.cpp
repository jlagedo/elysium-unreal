#include "UI/ElysiumUiArt.h"

#include "ElysiumContentPaths.h"
#include "ElysiumSurfaceParams.h"
#include "ElysiumUseIcons.h"

#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumUiArt, Log, All);

namespace
{
	// The object path's package half: everything before the `.AssetName` tail.
	FString PackageOf(const FString& ObjectPath)
	{
		int32 Dot = INDEX_NONE;
		return ObjectPath.FindLastChar(TEXT('.'), Dot) ? ObjectPath.Left(Dot) : ObjectPath;
	}

	template <typename T>
	T* LoadIfPresent(const FString& ObjectPath)
	{
		if (ObjectPath.IsEmpty() || !FPackageName::DoesPackageExist(PackageOf(ObjectPath)))
		{
			return nullptr;
		}
		return LoadObject<T>(nullptr, *ObjectPath);
	}
}

FString ElysiumUI::ArtKey(const FString& MaterialPath)
{
	FString Key = MaterialPath.ToLower().Replace(TEXT("\\"), TEXT("/"));
	Key.RemoveFromStart(TEXT("materials/"));
	Key.RemoveFromEnd(TEXT(".png"));
	Key.TrimStartAndEndInline();
	return Key;
}

UTexture2D* ElysiumUI::ArtTexture(const FString& MaterialPath)
{
	const FString Key = ArtKey(MaterialPath);
	if (Key.IsEmpty())
	{
		return nullptr;
	}

	if (UTexture2D* Direct = LoadIfPresent<UTexture2D>(FElysiumContentPaths::BakedTexture(Key)))
	{
		return Direct;
	}

	// A material whose VMT names another texture: the `MI_` carries the join.
	if (UMaterialInterface* Material = LoadIfPresent<UMaterialInterface>(
		FElysiumContentPaths::BakedMaterial(TEXT("vtmb:material:") + Key)))
	{
		UTexture* Bound = nullptr;
		if (Material->GetTextureParameterValue(ElysiumSurfaceParamsUnlit::Textures::BaseTexture, Bound))
		{
			if (UTexture2D* Texture = Cast<UTexture2D>(Bound))
			{
				return Texture;
			}
		}
	}

	UE_LOG(LogElysiumUiArt, Verbose,
		TEXT("no imported art for '%s' (%s) -- run: uv run elysium import textures"),
		*Key, *FElysiumContentPaths::BakedTexture(Key));
	return nullptr;
}

FString ElysiumUI::UseIconArt(int32 N)
{
	if (N < 1 || N > 72)
	{
		return FString();
	}
	FString Name = FString(ElysiumUseIconName(N)).ToLower();
	// The engine label is not always the file name; the table stores the label.
	if (Name == TEXT("stakeable"))
	{
		Name = TEXT("stakable");
	}
	else if (Name == TEXT("valve"))
	{
		Name = TEXT("valvewheel");
	}
	return TEXT("hud/context_icons/") + Name;
}

FString ElysiumUI::ClanSigilArt(int32 Clan)
{
	// VtMB's clan encoding starts at 2 -- 0 and 1 are unused, and a run with no clan yet flies
	// nothing rather than flying Brujah.
	static const TCHAR* Stems[] = {
		nullptr, nullptr,
		TEXT("brujah"), TEXT("gangrel"), TEXT("malkavian"), TEXT("nosferatu"),
		TEXT("toreador"), TEXT("tremere"), TEXT("ventrue"),
	};
	if (Clan < 0 || Clan >= UE_ARRAY_COUNT(Stems) || Stems[Clan] == nullptr)
	{
		return FString();
	}
	return FString::Printf(TEXT("interface/charactermaintenance/cm_clan_symbol_%s"), Stems[Clan]);
}
