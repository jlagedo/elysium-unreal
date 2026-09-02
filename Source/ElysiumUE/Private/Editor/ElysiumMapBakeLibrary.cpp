#include "ElysiumMapBakeLibrary.h"

#include "Components/ReflectionCaptureComponent.h"
#include "Engine/Level.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Editor/EditorEngine.h"
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
