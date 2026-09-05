#include "Audio/ElysiumWaterAudio.h"

#include "ElysiumContentPaths.h"
#include "ElysiumPhysicalMaterial.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"

namespace
{
	// The two baked surfaceprop assets D3 names. `PM_water` carries the impact/scrape scripts and
	// the ankle-deep footsteps; `PM_wade` carries the wading ones.
	const TCHAR* WaterMaterialPath = TEXT("/ElysiumBaked/SurfaceProperties/PM_water.PM_water");
	const TCHAR* WadeMaterialPath = TEXT("/ElysiumBaked/SurfaceProperties/PM_wade.PM_wade");

	const UElysiumPhysicalMaterial* LoadSurface(const TCHAR* Path)
	{
		// Loaded on first use and kept: two assets for the whole session, and the alternative is a
		// synchronous load inside a footstep.
		static TMap<FString, TWeakObjectPtr<UElysiumPhysicalMaterial>> Loaded;
		if (const TWeakObjectPtr<UElysiumPhysicalMaterial>* Found = Loaded.Find(Path))
		{
			if (Found->IsValid())
			{
				return Found->Get();
			}
		}
		UElysiumPhysicalMaterial* Material = LoadObject<UElysiumPhysicalMaterial>(
			nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		Loaded.Add(Path, Material);
		return Material;
	}

	// `vtmb:sound:surfaces/water/stepleft1.wav` -> `surfaces/water/stepleft1.wav`, which is exactly
	// the engine-relative path under `sound/` that `PlayVoice` takes.
	FString SoundRel(const FString& AssetId)
	{
		static const FString Prefix(TEXT("vtmb:sound:"));
		return AssetId.StartsWith(Prefix, ESearchCase::IgnoreCase)
			? AssetId.RightChop(Prefix.Len()) : AssetId;
	}

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
				return FPaths::GetPath(SoundRel((*Pool)[0]));
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

float ElysiumWaterAudio::StepIntervalSeconds(int32 WaterLevel, float Speed3dIn)
{
	// `1011ec5e`: the pool writes 600 ms wading, else 400 ms walking / 300 ms running against the
	// water pair's run speed, and the pair's minimum is added to whichever was written.
	const float Ms = (WaterLevel >= 2)
		? StepIntervalWadeMs
		: (Speed3dIn < StepRunSpeedIn ? StepIntervalWalkMs : StepIntervalRunMs);
	return (Ms + StepIntervalBiasMs) / 1000.f;
}

bool ElysiumWaterAudio::IsSoundingStep(int32 StepIndex, int32 WaterLevel)
{
	if (StepIndex < 0 || WaterLevel <= 0)
	{
		return false;
	}
	// Level 1 is the plain water pool: `UpdateStepSound` plays it every time the clock comes due.
	// Only the wade branch carries the four-phase counter, and only its phase 0 is silent.
	return WaterLevel < 2 || (StepIndex % StepsPerSound) != 0;
}

ElysiumWaterAudio::ECue ElysiumWaterAudio::StepCue(int32 WaterLevel)
{
	// Level 1 is ankle deep and level 2-3 is wading; a dry body is not this lane's business, and
	// the caller is expected not to ask -- `StepWater` is the harmless answer if it does.
	return WaterLevel >= 2 ? ECue::StepWade : ECue::StepWater;
}

FString ElysiumWaterAudio::Resolve(ECue Cue, int32 Variation, bool bRightFoot)
{
	if (Cue == ECue::Exit)
	{
		return ExitSoundExists() ? FString(ExitSound) : FString();
	}

	const UElysiumPhysicalMaterial* Material =
		LoadSurface(Cue == ECue::StepWade ? WadeMaterialPath : WaterMaterialPath);
	if (Material == nullptr)
	{
		return FString();
	}

	if (Cue == ECue::StepWater || Cue == ECue::StepWade)
	{
		const TArray<FString>& Pool = bRightFoot ? Material->FootstepsRight : Material->FootstepsLeft;
		const FString Id = Pick(Pool, Variation);
		return Id.IsEmpty() ? FString() : SoundRel(Id);
	}

	return Pick(ScriptPool(SurfaceFolder(Material), Cue == ECue::Scrape ? TEXT("scrape") : TEXT("impact")),
		Variation);
}
