#include "ElysiumSkyBakeLibrary.h"

#if WITH_EDITOR
#include "ElysiumContentPaths.h"
#include "ElysiumEnvironment.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/TextureCube.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumSkyBake, Log, All);
#endif // WITH_EDITOR

UTextureCube* UElysiumSkyBakeLibrary::BakeSkyCubeAsset(const FString& SkyName,
	const FString& PackagePath, float& OutUpperMean)
{
	OutUpperMean = 0.f;
#if WITH_EDITOR
	FString PackageDir, AssetName;
	if (!PackagePath.Split(TEXT("/"), &PackageDir, &AssetName, ESearchCase::IgnoreCase,
		ESearchDir::FromEnd))
	{
		UE_LOG(LogElysiumSkyBake, Warning, TEXT("bad sky cube package path: %s"), *PackagePath);
		return nullptr;
	}

	UPackage* Package = CreatePackage(*PackagePath);
	if (Package == nullptr)
	{
		UE_LOG(LogElysiumSkyBake, Warning, TEXT("could not create package: %s"), *PackagePath);
		return nullptr;
	}
	Package->FullyLoad();

	// Faithful set only (see the header) — always `SharedTexDir`, never `SharedTexHiDir`.
	const FString Prefix = FElysiumContentPaths::SkyFacePrefix(SkyName);
	UTextureCube* Cube = ElysiumEnvironment::BuildSkyCubeFrom(FElysiumContentPaths::SharedTexDir(),
		Prefix, &OutUpperMean, Package, FName(*AssetName));
	if (Cube == nullptr)
	{
		UE_LOG(LogElysiumSkyBake, Warning, TEXT("sky '%s': faces missing under %s, no cube baked"),
			*SkyName, *Prefix);
		return nullptr;
	}

	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Cube);
	return Cube;
#else
	return nullptr;
#endif // WITH_EDITOR
}
