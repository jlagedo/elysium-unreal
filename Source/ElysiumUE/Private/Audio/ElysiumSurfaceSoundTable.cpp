#include "Audio/ElysiumSurfaceSoundTable.h"

#include "ElysiumPhysicalMaterial.h"
#include "ElysiumSurfaceSounds.h"

#include "UObject/UObjectGlobals.h"   // LoadObject
#include "UObject/WeakObjectPtr.h"

namespace
{
	// The package root every surfaceprop entry is baked into.
	const TCHAR* SurfacePackageRoot = TEXT("/ElysiumBaked/SurfaceProperties/");

	// The first entry of a sound-script pool, as the script NAME the table authored. The pools are
	// `vtmb:sound-script:` ids rather than wav paths, and nothing in this runtime resolves a script
	// yet (`ElysiumWaterAudio.h` divergence 1 says so at length); the name is carried so the
	// consumer that lands the script table has the authored key to look up.
	FString FirstScript(const TArray<FString>& Pool)
	{
		static const FString Prefix(TEXT("vtmb:sound-script:"));
		if (Pool.IsEmpty())
		{
			return FString();
		}
		return Pool[0].StartsWith(Prefix, ESearchCase::IgnoreCase)
			? Pool[0].RightChop(Prefix.Len()) : Pool[0];
	}
}

namespace ElysiumSurfaceSoundTable
{

const UElysiumPhysicalMaterial* Load(const TCHAR* ObjectPath)
{
	if (ObjectPath == nullptr || *ObjectPath == TEXT('\0'))
	{
		return nullptr;
	}
	// Loaded on first use and kept: 63 assets for the whole session, and the alternative is a
	// synchronous load inside a footstep. A miss is cached as a null weak pointer too, so an
	// unbaked name costs one failed load rather than one per step.
	static TMap<FString, TWeakObjectPtr<UElysiumPhysicalMaterial>> Loaded;
	const FString Key(ObjectPath);
	if (const TWeakObjectPtr<UElysiumPhysicalMaterial>* Found = Loaded.Find(Key))
	{
		if (Found->IsValid())
		{
			return Found->Get();
		}
	}
	UElysiumPhysicalMaterial* Material = LoadObject<UElysiumPhysicalMaterial>(
		nullptr, ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	Loaded.Add(Key, Material);
	return Material;
}

const UElysiumPhysicalMaterial* Find(FName Surface)
{
	if (Surface.IsNone())
	{
		return nullptr;
	}
	// `Kitchen_Pan` bakes as `PM_kitchen_pan`: the importer folds the entry name, and the sample
	// publishes whatever `SourceName` the hit material carries, which may be either spelling.
	const FString Folded = Surface.ToString().ToLower();
	const FString Path = FString(SurfacePackageRoot) + TEXT("PM_") + Folded
		+ TEXT(".PM_") + Folded;
	return Load(*Path);
}

FString SoundRel(const FString& AssetId)
{
	static const FString Prefix(TEXT("vtmb:sound:"));
	return AssetId.StartsWith(Prefix, ESearchCase::IgnoreCase)
		? AssetId.RightChop(Prefix.Len()) : AssetId;
}

bool Resolve(FName Surface, FElysiumSurfaceSounds& Out)
{
	const UElysiumPhysicalMaterial* Material = Find(Surface);
	if (Material == nullptr)
	{
		return false;
	}

	// Pools keep their duplicates and their source order: a key repeated inside one entry block is
	// an alternate the engine picks between, so collapsing them would silently shrink the variation
	// set (`docs/vtmb/surface_properties.md`).
	Out.StepLeft.Reset(Material->FootstepsLeft.Num());
	for (const FString& Id : Material->FootstepsLeft)
	{
		Out.StepLeft.Add(SoundRel(Id));
	}
	Out.StepRight.Reset(Material->FootstepsRight.Num());
	for (const FString& Id : Material->FootstepsRight)
	{
		Out.StepRight.Add(SoundRel(Id));
	}
	Out.GameMaterial = Material->GameMaterial;
	Out.Impact = FirstScript(Material->SoundScriptImpact);
	Out.Scrape = FirstScript(Material->SoundScriptScrape);
	return true;
}

} // namespace ElysiumSurfaceSoundTable
