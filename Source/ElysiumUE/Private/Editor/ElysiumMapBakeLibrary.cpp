#include "ElysiumMapBakeLibrary.h"

#include "Commandlets/Commandlet.h"
#include "Components/ReflectionCaptureComponent.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "RenderingThread.h"
#include "UObject/Package.h"
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
	int32 Built = 0;
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
			if (Data != nullptr && Data->CubemapSize > 0 && Data->FullHDRCapturedData.Num() > 0)
			{
				++Built;
			}
		}
	}
	return Built;
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
