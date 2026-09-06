#include "Audio/ElysiumWaterAudio.h"

#include "Audio/ElysiumSurfaceSoundTable.h"
#include "ElysiumContentPaths.h"
#include "ElysiumPhysicalMaterial.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"

namespace
{
	// `PM_water` carries the impact/scrape scripts D3 names. Its footstep pools -- and `PM_wade`'s
	// -- are reached by the substrate step clock through `ResolveSurfaceSounds` instead, so this
	// lane names only the one asset it still reads.
	const TCHAR* WaterMaterialPath = TEXT("/ElysiumBaked/SurfaceProperties/PM_water.PM_water");

	// The cached surfaceprop loader and the `vtmb:sound:` -> engine-relative strip both moved to
	// `Audio/ElysiumSurfaceSoundTable.h` when the footstep subsystem needed the same two operations
	// for all 63 entries. Called qualified below: one cache, so the water lane and a footstep
	// resolve the same asset object.

	// The folder a surfaceprop's own pool lives in, taken off whichever id it publishes first.
	FString SurfaceFolder(const UElysiumPhysicalMaterial* Material)
	{
		if (Material == nullptr)
		{
			return FString();
		}
		for (const TArray<FString>* Pool :
			{ &Material->FootstepsLeft, &Material->FootstepsRight, &Material->BulletImpactLegacy })
		{
			if (!Pool->IsEmpty())
			{
				return FPaths::GetPath(ElysiumSurfaceSoundTable::SoundRel((*Pool)[0]));
			}
		}
		return FString();
	}

	// Divergence 1 in the header: the script's wavs, enumerated out of the surfaceprop's folder.
	// One listing per (folder, verb) for the session -- a footstep must not touch the disk.
	const TArray<FString>& ScriptPool(const FString& Folder, const TCHAR* Verb)
	{
		static TMap<FString, TArray<FString>> Pools;
		const FString Key = Folder / Verb;
		if (const TArray<FString>* Found = Pools.Find(Key))
		{
			return *Found;
		}
		TArray<FString> Names;
		if (!Folder.IsEmpty())
		{
			const FString Directory = FElysiumContentPaths::SoundDir() / Folder;
			const FString Wildcard = Directory / (FString(Verb) + TEXT("*.wav"));
			IFileManager::Get().FindFiles(Names, *Wildcard, true, false);
			// Deterministic order: the variation index is a draw into this list, and a list whose
			// order came off the filesystem would make the same draw a different sound per machine.
			Names.Sort();
			for (FString& Name : Names)
			{
				Name = Folder / Name;
			}
		}
		return Pools.Add(Key, MoveTemp(Names));
	}

	// Divergence 0 in the header: `player/pl_wade2.wav` is stock Source naming a Half-Life 2 asset
	// no VtMB install ships, so the exit cue resolves to nothing on every measured install. Probed
	// once per session against the mirror rather than assumed, so an install that DOES carry the
	// file plays exactly what `CBaseEntity::PhysicsCheckWaterTransition` names.
	bool ExitSoundExists()
	{
		static const bool bExists = IFileManager::Get().FileExists(
			*(FElysiumContentPaths::SoundDir() / ElysiumWaterAudio::ExitSound));
		return bExists;
	}

	FString Pick(const TArray<FString>& Pool, int32 Variation)
	{
		if (Pool.IsEmpty())
		{
			return FString();
		}
		return Pool[FMath::Abs(Variation) % Pool.Num()];
	}
}

FString ElysiumWaterAudio::Resolve(ECue Cue, int32 Variation)
{
	if (Cue == ECue::Exit)
	{
		return ExitSoundExists() ? FString(ExitSound) : FString();
	}

	// Impact and scrape both live on `PM_water`; the wade prop carries footsteps only, and those
	// moved to the substrate step clock (`Substrate/ElysiumFootsteps.h`).
	const UElysiumPhysicalMaterial* Material = ElysiumSurfaceSoundTable::Load(WaterMaterialPath);
	if (Material == nullptr)
	{
		return FString();
	}

	return Pick(ScriptPool(SurfaceFolder(Material), Cue == ECue::Scrape ? TEXT("scrape") : TEXT("impact")),
		Variation);
}
