#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ElysiumMapSubsystem.generated.h"

class AElysiumMapActor;

// The only owner of VtMB-map lifecycle inside the single persistent Unreal level.
// Travel destroys the current AElysiumMapActor (unloading everything map-scoped),
// flushes the texture cache, and spawns a fresh map actor for the target. Landmark
// selection (info_landmark spawn points) arrives with entity decoding; the parameter
// is accepted and ignored until then.
UCLASS()
class UElysiumMapSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Load a map, replacing the current one. Returns false if the map has no exported .obj.
	bool Travel(const FString& Map, const FString& Landmark = FString());

	// The map after Current in the sorted exported list (wrapping), for `map next`.
	FString NextMapName() const;

	AElysiumMapActor* GetCurrentMap() const { return CurrentMap.Get(); }
	FString GetCurrentMapName() const;

	// Names of maps the pipeline has exported (a folder under Root holding <name>.obj).
	TArray<FString> ExportedMaps() const;

	// The map to boot into: -ElysiumMap=<name> (play.bat <name>) or the default.
	FString ResolveBootMap() const;

private:
	TWeakObjectPtr<AElysiumMapActor> CurrentMap;
	TArray<IConsoleObject*> ConsoleObjects;
};
