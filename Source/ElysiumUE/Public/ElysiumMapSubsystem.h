#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Templates/PimplPtr.h"
#include "ElysiumMapSubsystem.generated.h"

class AElysiumMapActor;
class FElysiumProfileRun;
class FElysiumGreenRoomRun;
class FElysiumProbeRun;
class FElysiumShotRun;
class FElysiumMoveRun;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnElysiumCurrentMapReady, AElysiumMapActor*);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnElysiumCurrentMapFailed, AElysiumMapActor*, const FString&);

// The only owner of VtMB-map lifecycle. Map change is UE5 hard travel (roadmap 10.8): Travel
// stows the target map + landmark in this GI-scoped state and OpenLevels the map's own baked
// `.umap` under /ElysiumBaked (which carries the map's whole look as real assets); the engine tears
// down the current UWorld and runs GC, and the fresh world's game mode spawns the AElysiumMapActor
// for the pending map (SpawnPendingMap), which reads the map + landmark from here on BeginPlay and
// builds what is not baked — collision, ropes, the entity substrate, entity-driven bodies.
// Cross-map state lives at GameInstance scope and survives the travel. The
// P4.6 landmark transition (trigger_changelevel / scripted ChangeMap) places the player at the
// destination `info_landmark`, preserving their offset from the source landmark.
UCLASS()
class UElysiumMapSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Load a map, replacing the current one. Returns false unless the map has both a baked level
	// and an export (the runtime reads sidecars from the export beside the baked look). A
	// non-empty Landmark makes the fresh map place the player at that `info_landmark` (at the
	// landmark's facing) instead of info_player_start — the console/direct entry to the P4.6 path.
	bool Travel(const FString& Map, const FString& Landmark = FString());

	// 8.6 — load a map as the **menu backdrop**. The map actor builds the ordinary runtime world so
	// NPCs and authored ambience can live behind the menu, but the activation barrier omits the
	// possessed-pawn/final-placement requirement. No player entity or pawn is seated.
	//
	// Leaving this mode is an ordinary Travel: New Game re-opens the destination as a play world.
	bool TravelForMenu(const FString& Map);

	// True while the current world is a menu backdrop (see TravelForMenu). Read by the map actor
	// to skip the gameplay half of its build, and by the game mode to seat a camera instead of a
	// pawn.
	bool IsMenuBackdrop() const { return bCurrentIsMenuBackdrop; }

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
	// state. Returns false and publishes MapFailed if the actor could not be created. Success means
	// only that construction started; MapReady is published after the actor's activation barrier.
	bool SpawnPendingMap();
	FOnElysiumCurrentMapReady& OnCurrentMapReady() { return CurrentMapReady; }
	FOnElysiumCurrentMapFailed& OnCurrentMapFailed() { return CurrentMapFailed; }

	// Consumed once by the freshly-loaded map actor (P4.6): if this load is a landmark transition,
	// returns true and fills the destination `info_landmark` name + the player offset/yaw to place
	// them at. `bOutHasYaw` distinguishes a real transition (true: preserve the player's view yaw,
	// offset already carries capsule height) from a direct/console landmark entry (false: face the
	// landmark, lift onto it). Returns false for a plain info_player_start load.
	bool ConsumeLandmarkSpawn(FString& OutLandmark, FVector& OutOffset, float& OutYaw, bool& bOutHasYaw);

	// 11.9 — a loaded save places the player where they were standing, which is neither
	// info_player_start nor a landmark offset but an absolute pose the World block carried. Set by
	// UElysiumSaveSubsystem before it travels; consumed once by the freshly-loaded map actor, after
	// the landmark pass, so it wins over both.
	void RequestRestorePlacement(const FVector& Origin, float Yaw);
	bool ConsumeRestorePlacement(FVector& OutOrigin, float& OutYaw);

	// Re-Travel the current map (the export->reload hot loop). Returns false with no map loaded.
	bool Reload();

	// Ask the next map build to forget the destination's stored snapshot instead of replaying it,
	// so its openings fire again. Consumed once by the freshly-loaded map actor, at the point it
	// would have applied the snapshot — NOT when the command runs, because travel defers teardown
	// to end of frame and that teardown freezes the map we are leaving.
	void RequestFreshMapState() { bFreshMapState = true; }
	bool ConsumeFreshMapState();

	// The map after Current in the sorted exported list (wrapping), for `map next`.
	FString NextMapName() const;

	AElysiumMapActor* GetCurrentMap() const { return CurrentMap.Get(); }
	FString GetCurrentMapName() const;

	// Names of maps that are both exported and baked — i.e. the maps Travel will accept.
	TArray<FString> ExportedMaps() const;

	// The story entry, for the flow subsystem's New Game. Retail reaches this landmark from
	// `sp_theatre`, and a direct entry uses the same one the real transition does.
	static const TCHAR* StoryEntryMap() { return TEXT("sp_tutorial_1"); }
	static const TCHAR* StoryEntryLandmark() { return TEXT("tutorial"); }

	// The green room, if one is armed. Null in every ordinary session — the harness only exists
	// under `-ElysiumGreenRoom` or after `elysium.gr` has stood one up.
	FElysiumGreenRoomRun* GetGreenRoom() const { return GreenRoomRun.Get(); }
	// Arm the interactive green-room lab in a session that did not ask for one on the command
	// line, so `elysium.gr` works from a running game. A green room already armed is returned as
	// it stands rather than replaced; a capture run in progress is left alone and reported.
	FElysiumGreenRoomRun* EnsureGreenRoomLab(FString& OutError);

private:
	void HandleRuntimeReady(AElysiumMapActor* Map);
	void HandleRuntimeFailed(AElysiumMapActor* Map, const FString& Reason);

	TWeakObjectPtr<AElysiumMapActor> CurrentMap;
	FOnElysiumCurrentMapReady CurrentMapReady;
	FOnElysiumCurrentMapFailed CurrentMapFailed;
	TArray<IConsoleObject*> ConsoleObjects;

	// The map Travel stowed for the fresh world to build. Set by Travel (survives OpenLevel — this
	// subsystem is GI-scoped); consumed by SpawnPendingMap when the new world's game mode runs.
	struct FPendingMapLoad
	{
		bool    bValid = false;
		FString Map;
		FString Landmark;
		bool    bMenuBackdrop = false;   // 8.6: build the full runtime world, omit player seating
	};
	FPendingMapLoad PendingMapLoad;

	// Mirrors the consumed load's bMenuBackdrop for the lifetime of the built world, so the map
	// actor and game mode can ask what kind of world this is after SpawnPendingMap has cleared
	// the pending record.
	bool bCurrentIsMenuBackdrop = false;

	// One-shot: the next map build discards the destination's snapshot (RequestFreshMapState).
	bool bFreshMapState = false;

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

	// The absolute pose a loaded save places the player at (11.9); cleared on consume.
	struct FRestorePlacement
	{
		bool    bValid = false;
		FVector Origin = FVector::ZeroVector;
		float   Yaw = 0.0f;
	};
	FRestorePlacement NextRestorePlacement;

	// Headless profiling harness (task 0.1/0.2), created only under -ElysiumProfile.
	// TPimplPtr keeps the deleter type-erased, so the forward declaration suffices.
	TPimplPtr<FElysiumProfileRun> ProfileRun;

	// Headless screenshot-regression harness (P2.9), created only under -ElysiumShots.
	TPimplPtr<FElysiumShotRun> ShotRun;

	// Rendered skeletal body/clip validation, created only under -ElysiumGreenRoom.
	TPimplPtr<FElysiumGreenRoomRun> GreenRoomRun;
	TPimplPtr<FElysiumProbeRun> ProbeRun;

	// Headless movement-regression harness (4.7), created only under -ElysiumMove.
	TPimplPtr<FElysiumMoveRun> MoveRun;
};
