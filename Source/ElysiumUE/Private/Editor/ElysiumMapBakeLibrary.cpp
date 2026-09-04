#include "ElysiumMapBakeLibrary.h"

#include "Commandlets/Commandlet.h"
#include "Components/ReflectionCaptureComponent.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "RenderingThread.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectIterator.h"

#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "PackageTools.h"
#include "ShaderCompiler.h"
#endif // WITH_EDITOR

DEFINE_LOG_CATEGORY_STATIC(LogElysiumMapBake, Log, All);

int32 UElysiumMapBakeLibrary::BuildReflectionCaptures(UWorld* World)
{
#if WITH_EDITOR
	if (World == nullptr || GEditor == nullptr)
	{
		UE_LOG(LogElysiumMapBake, Warning,
			TEXT("BuildReflectionCaptures: %s -- nothing built"),
			World == nullptr ? TEXT("null world") : TEXT("no editor engine"));
		return -1;
	}
	// The editor's own Build -> Reflection Captures: waits for every pending shader and asset
	// compile (so no capture sees the default material), refreshes the sky captures, then
	// renders every capture that is dirty or has no build data into the level's registry.
	GEditor->BuildReflectionCaptures(World);
	int32 Components = 0;
	const int32 Built = CountBuiltReflectionCaptures(World, Components);
	UE_LOG(LogElysiumMapBake, Log,
		TEXT("BuildReflectionCaptures: %d of %d capture component(s) carry MapBuildData"),
		Built, Components);
	return Built;
#else
	return -1;
#endif // WITH_EDITOR
}

int32 UElysiumMapBakeLibrary::CountBuiltReflectionCaptures(UWorld* World, int32& OutComponents)
{
	OutComponents = 0;
	if (World == nullptr || World->PersistentLevel == nullptr)
	{
		return 0;
	}
	ULevel* Level = World->PersistentLevel;
	// The level's own registry, read directly rather than through
	// `UReflectionCaptureComponent::GetMapBuildData`, which needs `ULevel::OwningWorld` and a
	// lighting-scenario walk that a package-loaded world outside any world context never gets.
	const UMapBuildDataRegistry* Registry = Level->MapBuildData;
	// **Why a miss missed, tallied.** "0 of 24" is four different defects wearing one number --
	// the level reloaded without a registry at all, the registry loaded but holds no entry under
	// this component's `MapBuildDataId`, the entry exists with no cube size, or it has a size and
	// no bytes. Each points somewhere else (the level's import, the id, the render, the save's
	// strip), and reading the number without them is guessing.
	int32 NoEntry = 0;
	int32 NoCubemap = 0;
	int32 NoBytes = 0;
	int32 Built = 0;
	// The first id the registry could not answer for, and the first id there is. Printed together
	// because "no entry" has two very different causes and only the guids tell them apart: an id
	// the level carries that the registry never held (the render keyed something else), or an id
	// the level REWROTE after the registry was keyed (a save-time regeneration, which no amount of
	// re-saving the registry can fix).
	FGuid FirstMissingId;
	FGuid FirstId;
	for (const AActor* Actor : Level->Actors)
	{
		if (Actor == nullptr)
		{
			continue;
		}
		for (const UActorComponent* Component : Actor->GetComponents())
		{
			const UReflectionCaptureComponent* Capture = Cast<UReflectionCaptureComponent>(Component);
			if (Capture == nullptr)
			{
				continue;
			}
			if (OutComponents == 0)
			{
				FirstId = Capture->MapBuildDataId;
			}
			++OutComponents;
			if (Registry == nullptr)
			{
				continue;
			}
			const FReflectionCaptureMapBuildData* Data =
				Registry->GetReflectionCaptureBuildData(Capture->MapBuildDataId);
			// A rendered cube: a size and the full-HDR bytes an uncooked package serializes. Every
			// caller is an editor process (the bake, `bake_verify.py`, the Content test); a `-game`
			// process empties both after the GPU upload and never asks.
			if (Data == nullptr)
			{
				if (NoEntry == 0)
				{
					FirstMissingId = Capture->MapBuildDataId;
				}
				++NoEntry;
			}
			else if (Data->CubemapSize <= 0)
			{
				++NoCubemap;
			}
			else if (Data->FullHDRCapturedData.Num() == 0)
			{
				++NoBytes;
			}
			else
			{
				++Built;
			}
		}
	}
	if (Built < OutComponents)
	{
		UE_LOG(LogElysiumMapBake, Warning,
			TEXT("CountBuiltReflectionCaptures: %s -- %d of %d built; registry %s, %d id(s) with ")
			TEXT("no entry, %d entry(ies) with no cube size, %d with a size and no full-HDR bytes"),
			*World->GetOutermost()->GetName(), Built, OutComponents,
			Registry == nullptr ? TEXT("ABSENT on the level")
				: *FString::Printf(TEXT("%s #%u"), *Registry->GetPathName(),
					Registry->GetUniqueID()),
			NoEntry, NoCubemap, NoBytes);
		UE_LOG(LogElysiumMapBake, Warning,
			TEXT("CountBuiltReflectionCaptures: first component id %s, first unanswered id %s"),
			*FirstId.ToString(EGuidFormats::DigitsWithHyphens),
			*FirstMissingId.ToString(EGuidFormats::DigitsWithHyphens));
	}
	else
	{
		UE_LOG(LogElysiumMapBake, Log,
			TEXT("CountBuiltReflectionCaptures: %d of %d built, registry %s #%u, first component ")
			TEXT("id %s"),
			Built, OutComponents,
			Registry == nullptr ? TEXT("none") : *Registry->GetPathName(),
			Registry == nullptr ? 0u : Registry->GetUniqueID(),
			*FirstId.ToString(EGuidFormats::DigitsWithHyphens));
	}
	return Built;
}

bool UElysiumMapBakeLibrary::SaveMapBuildData(UWorld* World)
{
#if WITH_EDITOR
	if (World == nullptr || World->PersistentLevel == nullptr)
	{
		UE_LOG(LogElysiumMapBake, Warning, TEXT("SaveMapBuildData: null world -- nothing saved"));
		return false;
	}
	UMapBuildDataRegistry* Registry = World->PersistentLevel->MapBuildData;
	if (Registry == nullptr)
	{
		UE_LOG(LogElysiumMapBake, Warning,
			TEXT("SaveMapBuildData: %s carries no MapBuildData -- nothing saved"),
			*World->GetOutermost()->GetName());
		return false;
	}
	UPackage* Package = Registry->GetOutermost();
	if (Package == nullptr || Registry->IsLegacyBuildData())
	{
		UE_LOG(LogElysiumMapBake, Warning,
			TEXT("SaveMapBuildData: %s holds its build data in the level package itself -- there ")
			TEXT("is no _BuiltData package to save"),
			*World->GetOutermost()->GetName());
		return false;
	}
	// What the registry still answers for at the moment of the save. Between the build and here
	// the world was renamed out of its scratch package by `save_map`, and a registry that lost its
	// entries in that move would be saved faithfully and still be empty -- which reads downstream
	// exactly like a save that failed.
	{
		int32 Components = 0;
		const int32 Live = CountBuiltReflectionCaptures(World, Components);
		UE_LOG(LogElysiumMapBake, Log,
			TEXT("SaveMapBuildData: %s answers for %d of %d capture component(s) before the save"),
			*Package->GetName(), Live, Components);
	}
	// **Fully loaded first, or `UPackage::Save` calls `appError` and takes the commandlet with
	// it.** The mount already carries a `<map>_BuiltData` from the previous bake, and the level
	// load that reached it pulled in only the exports it was asked for, so the package arrives
	// here partially loaded. `FEditorFileUtils::SaveWorld` meets the same thing on the level's own
	// package and answers it with `MarkAsFullyLoaded` for the same stated reason ("usually set
	// implicitly by calling IsFullyLoaded before saving, but that path can get skipped for
	// levels"); the registry needs the real load, because its unloaded exports are the build data.
	Package->FullyLoad();
	const FString FileName = FPackageName::LongPackageNameToFilename(
		Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs Args;
	// The flags `ULevel::GetOrCreateMapBuildData` gives the registry it creates. Without them the
	// save keeps only what `Registry` itself references and drops the registry object, writing a
	// package whose captures are all unbuilt -- the very failure this call exists to prevent.
	Args.TopLevelFlags = RF_Standalone | RF_Public;
	// A commandlet has no slow-task UI to feed, and the progress scope asserts outside one.
	Args.bSlowTask = false;
	const FSavePackageResultStruct Result = UPackage::Save(Package, Registry, *FileName, Args);
	// Both answers, because neither one alone is the truth here. A save that reports success and
	// leaves nothing on disk is indistinguishable downstream from one that never ran -- and the
	// file existing proves nothing on its own, because the mount already carried a
	// `<map>_BuiltData` from the PREVIOUS bake before this call (that is why `FullyLoad` is
	// above), so a failed save would find the old file there and report it as this run's.
	const bool bSaved = Result.IsSuccessful();
	const bool bOnDisk = IFileManager::Get().FileExists(*FileName);
	if (bSaved && bOnDisk)
	{
		UE_LOG(LogElysiumMapBake, Log, TEXT("SaveMapBuildData: %s saved (result %d)"),
			*Package->GetName(), static_cast<int32>(Result.Result));
	}
	else
	{
		UE_LOG(LogElysiumMapBake, Warning,
			TEXT("SaveMapBuildData: %s did not save -- UPackage::Save %s (result %d), file %s"),
			*Package->GetName(), bSaved ? TEXT("succeeded") : TEXT("failed"),
			static_cast<int32>(Result.Result),
			bOnDisk ? TEXT("present (it may be the previous bake's)") : TEXT("absent"));
	}
	return bSaved && bOnDisk;
#else
	return false;
#endif // WITH_EDITOR
}

int32 UElysiumMapBakeLibrary::CountBuiltReflectionCapturesInPackage(const FString& LevelPackagePath)
{
#if WITH_EDITOR
	if (!FPackageName::DoesPackageExist(LevelPackagePath))
	{
		UE_LOG(LogElysiumMapBake, Warning,
			TEXT("CountBuiltReflectionCapturesInPackage: %s does not exist"), *LevelPackagePath);
		return -1;
	}
	// Both halves, because the registry is its own package: dropping only the level would leave
	// the in-memory registry to satisfy the reloaded level's import and the count would answer
	// about memory again. `UnloadBakedPackages` matches on the exact name here -- the sibling is
	// beside the level, not under it.
	UnloadBakedPackages(LevelPackagePath);
	UnloadBakedPackages(LevelPackagePath + TEXT("_BuiltData"));

	UPackage* Package = LoadPackage(nullptr, *LevelPackagePath, LOAD_None);
	UWorld* World = Package != nullptr ? UWorld::FindWorldInPackage(Package) : nullptr;
	if (World == nullptr)
	{
		UE_LOG(LogElysiumMapBake, Warning,
			TEXT("CountBuiltReflectionCapturesInPackage: %s holds no world"), *LevelPackagePath);
		return -1;
	}
	int32 Components = 0;
	const int32 Built = CountBuiltReflectionCaptures(World, Components);
	UE_LOG(LogElysiumMapBake, Log,
		TEXT("CountBuiltReflectionCapturesInPackage: %s -- %d of %d capture component(s) carry ")
		TEXT("MapBuildData on disk"),
		*LevelPackagePath, Built, Components);
	return Built;
#else
	return -1;
#endif // WITH_EDITOR
}

void UElysiumMapBakeLibrary::TickCommandletFrames(const int32 Frames)
{
	if (Frames <= 0 || GEngine == nullptr)
	{
		return;
	}
	// `TickEngine` only guards its *rendering* half against a missing world; the engine tick it
	// runs first is unconditional, and `UEditorEngine::Tick` opens with `check(GWorld)` and
	// `check(GWorld == GetEditorWorldContext().World())` (EditorEngine.cpp:1757, :1768), both live
	// in a development editor build. So no world here means no tick at all, not a cheaper one:
	// without an editor world there is no frame to end and nothing to drain, and ticking anyway
	// would assert.
	UWorld* const World = GWorld;
	if (World == nullptr)
	{
		return;
	}
#if WITH_EDITOR
	if (GEditor != nullptr && World != GEditor->GetEditorWorldContext().World())
	{
		// A package-loaded world some stage parked in `GWorld` without registering it as the
		// editor context world fails the second `check` just as hard as a null one.
		UE_LOG(LogElysiumMapBake, Warning,
			TEXT("TickCommandletFrames: GWorld is not the editor context world -- no frames ticked"));
		return;
	}
#endif // WITH_EDITOR
	for (int32 Frame = 0; Frame < Frames; ++Frame)
	{
		// A fixed 30 Hz delta: nothing here simulates gameplay, and `FApp::SetDeltaTime` only has
		// to be something a timer manager and the VT system can advance on.
		CommandletHelpers::TickEngine(World, 1.0 / 30.0);
	}
}

void UElysiumMapBakeLibrary::FinishAssetCompilation()
{
#if WITH_EDITOR
	// Textures and static meshes compile asynchronously by default in a commandlet, and no
	// `-run=` process pumps `FAssetCompilingManager`; the shader manager is the second half of
	// the same wait (a material instance write queues shader jobs that own their own memory).
	FAssetCompilingManager::Get().FinishAllCompilation();
	if (GShaderCompilingManager != nullptr)
	{
		GShaderCompilingManager->FinishAllCompilation();
	}
	// The finished builds hand their results to the rendering thread; nothing is released until
	// that queue is drained.
	FlushRenderingCommands();
#endif // WITH_EDITOR
}

void UElysiumMapBakeLibrary::ReleaseMeshSourceData(UStaticMesh* Mesh)
{
#if WITH_EDITORONLY_DATA
	if (Mesh == nullptr)
	{
		return;
	}
	// What `UStaticMesh::PostLoad` does once it has built, and `UStaticMesh::Build` -- the path
	// every mesh the bake authors takes -- does not: release the cached source descriptions. The
	// caller has already saved the package, so the geometry lives in its bulk data and any later
	// read loads it back from there.
	Mesh->ClearMeshDescriptions();
#endif // WITH_EDITORONLY_DATA
}

int32 UElysiumMapBakeLibrary::UnloadBakedPackages(const FString& PackagePath)
{
#if WITH_EDITOR
	// `UPackageTools::UnloadPackages` does its whole job through GEditor -- selection, the
	// transaction buffer, the editor world -- and returns early without one.
	if (PackagePath.IsEmpty() || GEditor == nullptr)
	{
		return 0;
	}
	// Matched on the separator so a prefix cannot claim a sibling: `/ElysiumBaked` must not take
	// `/ElysiumBakedSomethingElse` with it.
	const FString Prefix = PackagePath.EndsWith(TEXT("/")) ? PackagePath : PackagePath + TEXT("/");
	TArray<UPackage*> Unloading;
	for (TObjectIterator<UPackage> It; It; ++It)
	{
		UPackage* Package = *It;
		if (Package == nullptr || Package == GetTransientPackage())
		{
			continue;
		}
		// Unsaved output, not slack. `UnloadPackages` would refuse it anyway and answer with an
		// error text naming it; filtering here keeps a failed stage's leftovers out of the log.
		if (Package->IsDirty())
		{
			continue;
		}
		const FString Name = Package->GetName();
		if (Name == PackagePath || Name.StartsWith(Prefix))
		{
			Unloading.Add(Package);
		}
	}
	if (Unloading.IsEmpty())
	{
		return 0;
	}
	// Weak, so the count answers what was FREED rather than what was offered: `UnloadPackages`
	// restores `RF_Standalone` on anything its collect found still reachable, and a package that
	// survives is the interesting number -- something outside these paths is still holding it.
	TArray<TWeakObjectPtr<UPackage>> Offered;
	Offered.Reserve(Unloading.Num());
	for (UPackage* Package : Unloading)
	{
		Offered.Add(Package);
	}

	FText ErrorMessage;
	UPackageTools::UnloadPackages(Unloading, ErrorMessage, /*bUnloadDirtyPackages=*/false);
	Unloading.Reset();
	if (!ErrorMessage.IsEmpty())
	{
		UE_LOG(LogElysiumMapBake, Warning, TEXT("UnloadBakedPackages: %s"), *ErrorMessage.ToString());
	}

	int32 Released = 0;
	for (const TWeakObjectPtr<UPackage>& Package : Offered)
	{
		if (!Package.IsValid())
		{
			++Released;
		}
	}
	UE_LOG(LogElysiumMapBake, Log, TEXT("UnloadBakedPackages: %d of %d package(s) under %s freed"),
		Released, Offered.Num(), *PackagePath);
	return Released;
#else
	return 0;
#endif // WITH_EDITOR
}
