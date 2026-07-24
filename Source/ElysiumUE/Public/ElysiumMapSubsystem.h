#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Templates/PimplPtr.h"
#include "ElysiumMapSubsystem.generated.h"

class AElysiumMapActor;
class FElysiumProfileRun;
class FElysiumShotRun;

// The only owner of VtMB-map lifecycle. Map change is UE5 hard travel (roadmap 10.8): Travel
// stows the target map + landmark in this GI-scoped state and OpenLevels the one reused shell
// `.umap`; the engine tears down the current UWorld and runs GC, and the fresh world's game mode
// spawns the AElysiumMapActor for the pending map (SpawnPendingMap), which reads the map + landmark
// from here on BeginPlay. Cross-map state lives at GameInstance scope and survives the travel. The
// P4.6 landmark transition (trigger_changelevel / scripted ChangeMap) places the player at the
// destination `info_landmark`, preserving their offset from the source landmark.
UCLASS()
class UElysiumMapSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Load a map, replacing the current one. Returns false if the map has no exported .obj. A
	// non-empty Landmark makes the fresh map place the player at that `info_landmark` (at the
	// landmark's facing) instead of info_player_start — the console/direct entry to the P4.6 path.
	bool Travel(const FString& Map, const FString& Landmark = FString());

	// P4.6 — a landmark transition (from a trigger_changelevel touch / scripted ChangeMap). Safe to
	// call from inside the entity-world tick: it records the destination placement and Travels, and
	// OpenLevel defers the actual world teardown to end of frame (UEngine::TickWorldTravel), so
	// nothing is freed under the caller's stack. `PlayerOffset` is the player's position relative to
	// the SOURCE landmark; the fresh map re-adds it to the destination landmark. `PlayerYaw` is the
	// player's view yaw, preserved across the transition. First transition per frame wins.
	void RequestLandmarkTravel(const FString& Map, const FString& Landmark,
		const FVector& PlayerOffset, float PlayerYaw);

	// True while a map load is pending (Travel has stowed a target that a fresh world's game mode
	// will spawn). HasPendingTravel is the debug-UI alias; SpawnPendingMap consumes it.
	bool HasPendingMapLoad() const { return PendingMapLoad.bValid; }
	bool HasPendingTravel() const { return PendingMapLoad.bValid; }
	// The pending load's destination, for the debug UI ("<map> @ <landmark>" or empty).
	FString PendingTravelDesc() const;

	// Spawn the AElysiumMapActor for the pending map into the current world and consume the pending
	// state. Called by the game mode on BeginPlay after a Travel OpenLevel lands in the fresh shell
	// world (and directly by Travel on cold boot, when we are already in an empty shell). No-op with
	// no pending load.
	void SpawnPendingMap();

	// Consumed once by the freshly-loaded map actor (P4.6): if this load is a landmark transition,
	// returns true and fills the destination `info_landmark` name + the player offset/yaw to place
	// them at. `bOutHasYaw` distinguishes a real transition (true: preserve the player's view yaw,
	// offset already carries capsule height) from a direct/console landmark entry (false: face the
	// landmark, lift onto it). Returns false for a plain info_player_start load.
	bool ConsumeLandmarkSpawn(FString& OutLandmark, FVector& OutOffset, float& OutYaw, bool& bOutHasYaw);

	// Re-Travel the current map (the export->reload hot loop). Returns false with no map loaded.
	bool Reload();

	// The map after Current in the sorted exported list (wrapping), for `map next`.
	FString NextMapName() const;

	AElysiumMapActor* GetCurrentMap() const { return CurrentMap.Get(); }
	FString GetCurrentMapName() const;

	// Names of maps the pipeline has exported (a folder under Root holding <name>.obj).
	TArray<FString> ExportedMaps() const;

	// The map to boot into: -ElysiumMap=<name> (play.bat <name>) or the default.
	FString ResolveBootMap() const;

	// --- New Game (story entry) --------------------------------------------------------
	// Seed a fresh story context (UElysiumGameStateSubsystem::BeginNewGame) and travel to the
	// story entry: `sp_tutorial_1` at the `tutorial` info_landmark, offset zero. Retail reaches
	// that landmark from `sp_theatre`; chargen and the intro are unbuilt (8.6 / P9), so the seeded
	// context stands in for them and the landmark entry is the same one the real transition uses.
	// Clan is the level-script 2..8 encoding. Returns false if the entry map isn't exported.
	bool NewGame(int32 Clan = 2, bool bMale = true);

	// Whether boot should run NewGame rather than a bare Travel. True unless an explicit
	// -ElysiumMap= was given (play.bat <map> — the dev path, which must stay unseeded) or
	// -ElysiumNewGame=0 was passed (boot the story map bare, to A/B against the seeded run).
	bool ShouldBootNewGame() const;

	static const TCHAR* StoryEntryMap() { return TEXT("sp_tutorial_1"); }
	static const TCHAR* StoryEntryLandmark() { return TEXT("tutorial"); }

private:
	TWeakObjectPtr<AElysiumMapActor> CurrentMap;
	TArray<IConsoleObject*> ConsoleObjects;

	// The map Travel stowed for the fresh world to build. Set by Travel (survives OpenLevel — this
	// subsystem is GI-scoped); consumed by SpawnPendingMap when the new world's game mode runs.
	struct FPendingMapLoad
	{
		bool    bValid = false;
		FString Map;
		FString Landmark;
	};
	FPendingMapLoad PendingMapLoad;

	// The landmark placement the next map load consumes (dest = landmark origin + Offset). Set by a
	// transition (RequestLandmarkTravel) or by a direct Travel(map, landmark); cleared on consume.
	struct FLandmarkSpawn
	{
		bool    bValid = false;
		FString Landmark;
		FVector Offset = FVector::ZeroVector;
		float   Yaw = 0.0f;
		bool    bHasYaw = false;   // true: transition (keep view yaw); false: direct entry (face landmark)
	};
	FLandmarkSpawn NextLandmarkSpawn;

	// Headless profiling harness (task 0.1/0.2), created only under -ElysiumProfile.
	// TPimplPtr keeps the deleter type-erased, so the forward declaration suffices.
	TPimplPtr<FElysiumProfileRun> ProfileRun;

	// Headless screenshot-regression harness (P2.9), created only under -ElysiumShots.
	TPimplPtr<FElysiumShotRun> ShotRun;
};
