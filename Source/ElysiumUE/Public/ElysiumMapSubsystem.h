#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Templates/PimplPtr.h"
#include "ElysiumMapEpoch.h"   // FElysiumMapEpoch + its two delegates — a by-value member
#include "ElysiumMapSubsystem.generated.h"

class AElysiumMapActor;
class FElysiumProfileRun;
class FElysiumGreenRoomConsole;
class FElysiumGreenRoomRun;
class FElysiumProbeRun;
class FElysiumShotRun;
class FElysiumMoveRun;
class FElysiumCastRun;
class FElysiumComposeRun;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnElysiumCurrentMapReady, AElysiumMapActor*);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnElysiumCurrentMapFailed, AElysiumMapActor*, const FString&);

// The only owner of VtMB-map lifecycle. Map change is UE5 hard travel: Travel stows the target
// map + landmark in this GI-scoped state and OpenLevels the map's own baked `.umap` under
// /ElysiumBaked (which carries the map's whole look as real assets); the engine tears down the
// current UWorld and runs GC, and the fresh world's game mode spawns the AElysiumMapActor for
// the pending map (SpawnPendingMap), which reads the map + landmark from here on BeginPlay and
// builds what is not baked — collision, ropes, the entity substrate, entity-driven bodies.
// Cross-map state lives at GameInstance scope and survives the travel. A landmark transition
// (trigger_changelevel / scripted ChangeMap) places the player at the destination
// `info_landmark`, preserving their offset from the source landmark.
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
	// landmark's facing) instead of info_player_start — the console/direct entry to the landmark path.
	bool Travel(const FString& Map, const FString& Landmark = FString());

	// Whether Travel would accept Map: the producer's own proof that it ran is on disk for it --
	// the new lane's readiness marker (the export-readiness gate), or, for a map still on the legacy
	// geometry lane, the legacy exporter's `.obj`. A map on `MapsOnV2Models` needs the marker: nothing
	// reads its `.obj` any more, so a stale one cannot vouch for the sidecars beside it. Static and file-only, so a
	// test can drive it with a scratch content root and no UWorld or subsystem instance; `Travel` and
	// `ExportedMaps` both route through this one predicate so the two can never disagree.
	static bool HasTravelableExport(const FString& Map);

	// Enter the empty `/Game/ElysiumGenerated/Boot` front-end shell. If it is already the current
	// world (cold boot), this only latches front-end mode and reports no travel. From a game map it
	// hard-travels back to the shell and reports that travel through bOutTravelStarted.
	bool EnterFrontEnd(bool& bOutTravelStarted);

	// True while the current world is the front-end shell. The historical name remains because map
	// and save call sites use it as the established "not a playable map" predicate.
	bool IsMenuBackdrop() const { return bCurrentIsMenuBackdrop; }

	// A landmark transition (from a trigger_changelevel touch / scripted ChangeMap). Safe to
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

	// The map-epoch boundary.
	// Mint an epoch for a map actor entering play, and retire it when that actor leaves. The map
	// actor drives both — it is the only object that knows the ordered teardown — but the epoch
	// itself belongs here, with the rest of map lifecycle.
	//
	// Every application-lifetime object holding state on a map's behalf subscribes to
	// OnMapEpochRetired and frees it there. Retire is broadcast from AElysiumMapActor::EndPlay,
	// where the world is still standing and every UObject index is still valid, so a subscriber may
	// destroy actors and components rather than merely dropping references to them.
	uint64 BeginMapEpoch();
	void RetireMapEpoch(uint64 Epoch);
	uint64 CurrentMapEpoch() const { return MapEpoch.Current(); }
	FOnElysiumMapEpochBegin& OnMapEpochBegin() { return MapEpochBegin; }
	FOnElysiumMapEpochRetired& OnMapEpochRetired() { return MapEpochRetired; }

	// Consumed once by the freshly-loaded map actor: if this load is a landmark transition,
	// returns true and fills the destination `info_landmark` name + the player offset/yaw to place
	// them at. `bOutHasYaw` distinguishes a real transition (true: preserve the player's view yaw,
	// offset already carries capsule height) from a direct/console landmark entry (false: face the
	// landmark, lift onto it). Returns false for a plain info_player_start load.
	bool ConsumeLandmarkSpawn(FString& OutLandmark, FVector& OutOffset, float& OutYaw, bool& bOutHasYaw);

	// A loaded save places the player where they were standing, which is neither
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

	// Enter the green room: travel into the empty boot level, build a **stage world** in it (an
	// AElysiumMapActor carrying the body factory, the camera director and an empty entity world, with
	// no VtMB map behind any of them), and arm the interactive lab over it. This is what `elysium.gr`
	// fires, from a cold boot or from the middle of a session — in the latter case the running map is
	// torn down, and an ordinary `elysium.map <name>` is the way back. Re-entering a stage world that
	// already exists only re-arms the lab. False with the reason in OutError.
	bool EnterGreenRoom(FString& OutError);

	// The travel half of the above, with no lab armed over it: build the stage world and stop
	// there. What a caller that brings its own geometry wants — the movement gym stands itself up
	// in this same empty level and runs under `-nullrhi`, where the lab refuses outright.
	bool EnterStageWorld(FString& OutError);

	// True while the current world is a stage world (see EnterStageWorld): an empty level with no
	// map in it. Read by the green room, which lights and frames its stage differently when nothing
	// else is contributing either.
	bool IsStageWorld() const { return bCurrentIsStageOnly; }

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

	// Drop an interactive lab when the world it was standing in stops being its stage. The run is
	// GI-scoped and ticks off the core ticker, so nothing else ends it: left armed, it would go on
	// hiding the HUD and writing control rotation into whatever world came next. A capture run is
	// left alone — it owns the process and exits on its own.
	void RetireGreenRoomLab(const TCHAR* Reason);

	TWeakObjectPtr<AElysiumMapActor> CurrentMap;
	FOnElysiumCurrentMapReady CurrentMapReady;
	FOnElysiumCurrentMapFailed CurrentMapFailed;
	FElysiumMapEpoch MapEpoch;
	FOnElysiumMapEpochBegin MapEpochBegin;
	FOnElysiumMapEpochRetired MapEpochRetired;
	TArray<IConsoleObject*> ConsoleObjects;

	// The map Travel stowed for the fresh world to build. Set by Travel (survives OpenLevel — this
	// subsystem is GI-scoped); consumed by SpawnPendingMap when the new world's game mode runs.
	struct FPendingMapLoad
	{
		bool    bValid = false;
		FString Map;
		FString Landmark;
		bool    bMenuBackdrop = false;   // build the full runtime world, omit player seating
		bool    bStageOnly = false;      // the green room: no map at all, Map is empty
	};
	FPendingMapLoad PendingMapLoad;

	// Mirrors the consumed load's bMenuBackdrop for the lifetime of the built world, so the map
	// actor and game mode can ask what kind of world this is after SpawnPendingMap has cleared
	// the pending record.
	bool bCurrentIsMenuBackdrop = false;
	// The same mirror for a stage world (EnterGreenRoom).
	bool bCurrentIsStageOnly = false;

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

	// The absolute pose a loaded save places the player at; cleared on consume.
	struct FRestorePlacement
	{
		bool    bValid = false;
		FVector Origin = FVector::ZeroVector;
		float   Yaw = 0.0f;
	};
	FRestorePlacement NextRestorePlacement;

	// Headless profiling harness, created only under -ElysiumProfile.
	// TPimplPtr keeps the deleter type-erased, so the forward declaration suffices.
	TPimplPtr<FElysiumProfileRun> ProfileRun;

	// Headless screenshot-regression harness, created only under -ElysiumShots.
	TPimplPtr<FElysiumShotRun> ShotRun;

	// Rendered skeletal body/clip validation, created only under -ElysiumGreenRoom.
	TPimplPtr<FElysiumGreenRoomRun> GreenRoomRun;
	// The lab's `elysium.gr_*` verbs. Registered for the session rather than with the lab, because
	// they have to be callable before one is armed in order to say so; they resolve the lab per call.
	TPimplPtr<FElysiumGreenRoomConsole> GreenRoomConsole;
	TPimplPtr<FElysiumProbeRun> ProbeRun;

	// Headless movement-regression harness, created only under -ElysiumMove.
	TPimplPtr<FElysiumMoveRun> MoveRun;

	// Headless cast-locomotion harness, created only under -ElysiumCast: the body trace's second
	// producer, recording the cast's selection where the movement run records the player's.
	TPimplPtr<FElysiumCastRun> CastRun;

	// Headless composed-pose harness, created only under -ElysiumCompose: the pose the animation
	// graph produced, recorded off a driven body so a composition defect is a number rather than a
	// screenshot.
	TPimplPtr<FElysiumComposeRun> ComposeRun;
};
