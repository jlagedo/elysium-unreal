// AElysiumMapActor's lifecycle — the runtime-phase decision engine, BeginPlay and the map/stage
// build, spawn placement resolution, EndPlay, and the activation poll that admits gameplay.

#include "ElysiumMapActor.h"

#include "ElysiumAudioSubsystem.h"        // the audio-catalog activation prerequisite
#include "ElysiumCameraService.h"         // the LocalPlayer-scoped camera service handed to the world
#include "ElysiumContentPaths.h"          // the map's sidecar paths (.sky/.ents/.spawn)
#include "ElysiumEntity.h"                // FElysiumEntity — the landmark resolve
#include "ElysiumEntityDefs.h"            // FElysiumEntityDefs — the .ents parse
#include "ElysiumEntityWorld.h"           // the Track-B world this actor builds and owns
#include "ElysiumGameStateSubsystem.h"    // the clock, the level script, map snapshots
#include "ElysiumMapSubsystem.h"          // epochs, backdrop state, landmark/restore placements
#include "ElysiumPlayerBody.h"            // IElysiumPlayerBody — placement and the movement freeze
#include "ElysiumPresentationSubsystem.h" // the fourth world service
#include "Audio/ElysiumSoundScheme.h"     // FElysiumSoundSchemeManager — built at load, stopped at EndPlay
#include "Map/ElysiumMapCollision.h"      // the walkable-surface build and its readiness states
#include "Map/ElysiumMapLog.h"
#include "Visual/ElysiumEntityBodies.h"   // SetMap and the map animation preload
#include "Visual/ElysiumMapVisuals.h"     // the baked-level adoption and the material audit
#include "Visual/ElysiumNpcBody.h"        // SetRuntimeReady at the activation barrier

#include "Components/BoxComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/FileHelper.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"

namespace
{
	UElysiumCameraService* LocalCameraService(const UObject* Context)
	{
		UWorld* World = Context ? Context->GetWorld() : nullptr;
		UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		ULocalPlayer* Player = GI ? GI->GetFirstGamePlayer() : nullptr;
		return Player ? Player->GetSubsystem<UElysiumCameraService>() : nullptr;
	}
}

const TCHAR* ElysiumMapRuntimePhaseName(EElysiumMapRuntimePhase Phase)
{
	switch (Phase)
	{
	case EElysiumMapRuntimePhase::Building:                return TEXT("Building");
	case EElysiumMapRuntimePhase::WaitingForPrerequisites: return TEXT("WaitingForPrerequisites");
	case EElysiumMapRuntimePhase::Activating:              return TEXT("Activating");
	case EElysiumMapRuntimePhase::Active:                  return TEXT("Active");
	case EElysiumMapRuntimePhase::Failed:                  return TEXT("Failed");
	default:                                               return TEXT("Unknown");
	}
}

FString FElysiumMapRuntimePrerequisites::Missing() const
{
	TArray<FString> MissingItems;
	if (!bConstructionComplete) { MissingItems.Add(TEXT("runtime construction")); }
	if (!bEntityWorldReady)     { MissingItems.Add(TEXT("entity substrate")); }
	if (!bAnimationPreloadReady){ MissingItems.Add(TEXT("map animation residency")); }
	if (!bAudioCatalogReady)    { MissingItems.Add(TEXT("audio catalog")); }
	if (bCollisionFailed)       { MissingItems.Add(TEXT("world collision failed")); }
	else if (!bCollisionReady)  { MissingItems.Add(TEXT("world collision cooking")); }
	if (bNavigationFailed)      { MissingItems.Add(TEXT("runtime navigation failed")); }
	else if (bNavigationRequired && !bNavigationReady)
	{
		MissingItems.Add(TEXT("runtime navigation building"));
	}

	if (!bMenuBackdrop)
	{
		if (!bSpawnTransformReady)    { MissingItems.Add(TEXT("final spawn transform")); }
		if (!bPlayerEntityReady)      { MissingItems.Add(TEXT("player entity")); }
		if (!bPossessedPawnReady)     { MissingItems.Add(TEXT("possessed player pawn")); }
		if (!bPlayerBodyReady)        { MissingItems.Add(TEXT("player body freeze contract")); }
		if (!bFinalPlacementReady)    { MissingItems.Add(TEXT("final player placement")); }
		if (!bTickPrerequisitesReady) { MissingItems.Add(TEXT("player tick prerequisites")); }
	}
	return FString::Join(MissingItems, TEXT(", "));
}

EElysiumMapReadinessResult FElysiumMapRuntimePrerequisites::Evaluate(
	double WaitSeconds, FString& OutFailure) const
{
	OutFailure.Reset();
	if (bCollisionFailed)
	{
		OutFailure = TEXT("required world collision failed");
		return EElysiumMapReadinessResult::Failed;
	}
	if (bNavigationFailed)
	{
		OutFailure = TEXT("required runtime navigation failed");
		return EElysiumMapReadinessResult::Failed;
	}
	// Once construction is declared complete, these inputs cannot arrive on a later engine tick.
	if (bConstructionComplete && !bEntityWorldReady)
	{
		OutFailure = TEXT("runtime construction produced no entity substrate");
		return EElysiumMapReadinessResult::Failed;
	}
	if (bConstructionComplete && !bAnimationPreloadReady)
	{
		OutFailure = TEXT("runtime construction did not complete map animation residency");
		return EElysiumMapReadinessResult::Failed;
	}
	if (bConstructionComplete && !bAudioCatalogReady && WaitSeconds >= WatchdogSeconds)
	{
		OutFailure = TEXT("audio catalog did not become ready");
		return EElysiumMapReadinessResult::Failed;
	}
	if (bConstructionComplete && !bMenuBackdrop
		&& (!bSpawnTransformReady || !bPlayerEntityReady))
	{
		OutFailure = !bSpawnTransformReady
			? TEXT("gameplay map has no final spawn transform")
			: TEXT("gameplay map did not construct its player entity");
		return EElysiumMapReadinessResult::Failed;
	}

	if (Missing().IsEmpty())
	{
		return EElysiumMapReadinessResult::Ready;
	}
	if (WaitSeconds >= WatchdogSeconds)
	{
		OutFailure = FString::Printf(TEXT("activation watchdog expired; missing: %s"), *Missing());
		return EElysiumMapReadinessResult::Failed;
	}
	return EElysiumMapReadinessResult::Waiting;
}

void AElysiumMapActor::BeginPlay()
{
	Super::BeginPlay();
	// Open this map's epoch (S4). The subsystem mints it and tells every application-lifetime
	// subscriber; a bare world with no subsystem leaves it 0, which matches no owner and retires
	// nothing.
	if (UElysiumMapSubsystem* Maps = GetMapSubsystem())
	{
		MapEpoch = Maps->BeginMapEpoch();
	}
	RuntimePhase = EElysiumMapRuntimePhase::Building;
	RuntimeWaitStartSeconds = FPlatformTime::Seconds();

	// Engine pause and time dilation are per-world; the clock is not. Re-stamp them onto this
	// world so a hold or a time scale set before travel survives the map change (S1).
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (UElysiumGameStateSubsystem* GameState = GI->GetSubsystem<UElysiumGameStateSubsystem>())
		{
			GameState->TimeControl().ApplyToWorld();
		}
	}
	EnsureTickPrerequisites();

	// The look-tuning cvar callbacks are bound by UElysiumMapVisuals::BeginPlay, which Super has
	// already run — so a knob turned during the load still reaches the surfaces it built.
	LoadMap();
	bRuntimeConstructionComplete = true;
	RuntimePhase = EElysiumMapRuntimePhase::WaitingForPrerequisites;
	UE_LOG(LogElysium, Log, TEXT("map runtime %s: %s (collision %s)"), *MapName,
		ElysiumMapRuntimePhaseName(RuntimePhase),
		ElysiumCollisionBuildStateName(Collision->GetBuildState()));
}

void AElysiumMapActor::BuildStageWorld()
{
	const double Start = FPlatformTime::Seconds();
	LoadPhases.Reset();
	LoadedMap = MapName;
	Bodies->SetMap(MapName);
	SkyDef = FElysiumSkyDef();

	// Nothing is adopted, built or parsed: there is no baked level, no `.hulls`, no `.ents`. The
	// collision component is left at its Disabled default, which the activation barrier counts as a
	// satisfied input and which also drops the runtime Recast requirement — a stage with no walkable
	// surface has nothing to navigate.
	//
	// The pawn is seated at the world origin rather than on the stage: the stage stands far out at
	// FElysiumGreenRoomRun::StageOrigin, and a player model standing in the same place as the body
	// being reviewed would be in every frame of it.
	PendingSpawnLoc = FVector(0.0f, 0.0f, 100.0f);
	PendingSpawnYaw = 0.0f;
	PendingSpawnSpace = EElysiumPlayerPlacementSpace::CapsuleCenter;
	bSpawnPending = true;

	// An empty entity world, not the absence of one. The green room reaches the map through the same
	// IElysiumEmbodiment seam a map's own NPCs do, the substrate clock is what `SeekCinematicClip`
	// and the audio pass run against, and the activation barrier requires both a world and a player
	// entity in it. All of that holds with zero entity definitions.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UElysiumGameStateSubsystem* GameState = GI->GetSubsystem<UElysiumGameStateSubsystem>())
		{
			SchemeManager = MakePimpl<FElysiumSoundSchemeManager>();
			SchemeManager->SetMapEpoch(MapEpoch);

			FElysiumWorldServices Services;
			Services.Embodiment = this;
			Services.Audio      = this;
			Services.Travel     = this;
			Services.Presenter  = UElysiumPresentationSubsystem::Get(GetWorld());
			Services.Weather    = this;
			Services.Camera     = LocalCameraService(this);
			EntityWorld = MakePimpl<FElysiumEntityWorld>(this, GameState, Services);
			EntityWorld->Load(FElysiumEntityDefs());
			EntityWorld->SpawnPlayer();
		}
	}

	// No map animations to make resident — the bodies the green room stands up resolve their own
	// clips on demand through the same caches.
	bAnimationPreloadReady = true;

	const double TotalMs = (FPlatformTime::Seconds() - Start) * 1000.0;
	LoadPhases.Add({ TEXT("Total"), TotalMs });
	UE_LOG(LogElysium, Log, TEXT("built the green-room stage world in %.0f ms (no map)"), TotalMs);
}

void AElysiumMapActor::LoadMap()
{
	if (bStageOnly)
	{
		BuildStageWorld();
		return;
	}

	const double Start = FPlatformTime::Seconds();
	bAnimationPreloadReady = false;
	if (NavigationBounds)
	{
		NavigationBounds->Destroy();
		NavigationBounds = nullptr;
	}
	bNavigationBuildRequested = false;
	bNavigationBuildFailed = false;

	// Per-phase timing for the Maps Cog window: stamp closes the running phase and opens the next.
	LoadPhases.Reset();
	double PhaseStart = Start;
	auto Phase = [this, &PhaseStart](const TCHAR* Name)
	{
		const double Now = FPlatformTime::Seconds();
		LoadPhases.Add({ Name, (Now - PhaseStart) * 1000.0 });
		PhaseStart = Now;
	};

	LoadedMap = MapName;
	Bodies->SetMap(MapName);

	// B7 — the 3D-skybox miniature's placement transform (`<map>.sky`), read first because three
	// later steps need it: the light rig scales a miniature source's reach by it, the `.ents`
	// parser carries sky-scope entities through it, and a miniature body takes its mesh scale
	// from it. The identity (scale 1) on the 65 maps with no `sky_camera`.
	SkyDef = FElysiumSkyDef();
	FElysiumSkyDef::Parse(FElysiumContentPaths::MapSky(MapName), SkyDef);

	// P1.7 — label the map actor and drop it in an Elysium Outliner folder, so the PIE World
	// Outliner reads as a live scene browser (debug-tooling.md Layer 0).
#if WITH_EDITOR
	SetActorLabel(FString::Printf(TEXT("Map:%s"), *MapName));
	SetFolderPath(TEXT("Elysium"));
#endif

	// The look is already here — this actor was spawned into the map's baked level. The visuals
	// take hold of its actors, hand the light rig its sources, and stand the material overrides up.
	const int32 Adopted = Visuals->AdoptBakedLevel(MapName, SkyDef);
	Phase(TEXT("Adopt baked level"));

	// 8.6 — a menu backdrop builds the map in full, entity substrate included: the NPCs standing and
	// idling in frame *are* entities, so a look-only build has no one in it (owner call, see
	// What a backdrop skips is only the player's placement — it seats no pawn.
	UElysiumMapSubsystem* MapSubsystem =
		GetGameInstance() ? GetGameInstance()->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
	bMenuBackdrop = MapSubsystem && MapSubsystem->IsMenuBackdrop();

	// The walkable surface. Baked world geometry carries no gameplay collision, so the brush
	// sidecars are the only world collider: .hulls convex (which carries the invisible PLAYERCLIP
	// volumes and drops geometry the designer clipped off) plus the .dispcol displacement trimesh.
	Collision->Build(MapName);
	Phase(TEXT("Collision"));

	Visuals->BuildRopes(MapName);
	Phase(TEXT("Ropes"));

	// Sky cubemap + backdrop, the sky light's IBL off the same cube, and the map's PPV knobs.
	Visuals->ApplyEnvironment(MapName);
	Visuals->ApplyPostProcessKnobs();
	Phase(TEXT("Environment"));

	UE_LOG(LogElysium, Log,
		TEXT("baked '%s': %d actors (%d world, %d sky, %d props, %d decals), %d lights, %d hulls"),
		*MapName, Adopted, Visuals->WorldSurfaceCount, Visuals->SkySurfaceCount,
		Visuals->PropInstanceCount, Visuals->DecalCount, Visuals->WorldLightCount,
		Collision->HullCount);

	if (!bMenuBackdrop && ReadSpawn(PendingSpawnLoc, PendingSpawnYaw))
	{
		PendingSpawnSpace = EElysiumPlayerPlacementSpace::Feet;
		bSpawnPending = true;
	}

	if (bMenuBackdrop)
	{
		UE_LOG(LogElysium, Log, TEXT("menu backdrop '%s': full build, no player placement"), *MapName);
	}

	// Track-B entity substrate (P1.4): parse `.ents`, build the live world, run the spawn pass.
	// Map-load ignition (OnMapLoad) is the logic_auto class's own first-think (P1.6), not a
	// separate pass. The world ticks from AElysiumMapActor::Tick.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UElysiumGameStateSubsystem* GameState = GI->GetSubsystem<UElysiumGameStateSubsystem>())
		{
			FElysiumEntityDefs EntDefs;
			if (FElysiumEntityDefs::Parse(FElysiumContentPaths::MapEnts(MapName), EntDefs,
				SkyDef.Scale, SkyDef.OriginCm))
			{
				EntityCount = EntDefs.Num();

				// P9 9.3 — import this map's `worldspawn.levelscript` module before anything can
				// evaluate against it. VtMB's own load order: the level script's top-level code
				// (constants like cCelerity, `from vamputil import *`, the On* defs) runs first,
				// then entities spawn and fire their field-6 payloads into that namespace.
				GameState->LoadLevelScript(EntDefs.LevelScriptModule());

				// The scheme manager must exist before the spawn pass: a start_enabled
				// ambient_soundscheme fades its scheme in from its own Spawn() (P6.3), and it
				// reaches it through this actor's IElysiumAudio.
				SchemeManager = MakePimpl<FElysiumSoundSchemeManager>();
				SchemeManager->SetMapEpoch(MapEpoch);

				// 11.2 — hand the substrate its outbound seam. This actor is three of the four
				// services; the fourth is the world-scoped presentation subsystem (11.8), which is
				// null only where there is no publisher at all (an editor preview world, a
				// Substrate-tier world with no engine behind it).
				FElysiumWorldServices Services;
				Services.Embodiment = this;
				Services.Audio      = this;
				Services.Travel     = this;
				Services.Presenter  = UElysiumPresentationSubsystem::Get(GetWorld());
				// The weather seam is live. rain_follow_emitter drives one viewer-volume Niagara
				// system; other env_particle definitions still load their baked closures.
				Services.Weather    = this;
				Services.Camera     = LocalCameraService(this);
				EntityWorld = MakePimpl<FElysiumEntityWorld>(this, GameState, Services);
				EntityWorld->Load(MoveTemp(EntDefs));
				BrushBodyCount = EntityWorld->NumBrushBodies();

				// 11.4 (S3) — the player is an entity, created here because the map is where a
				// player exists at all: a backdrop seats no pawn, so it gets no player entity and
				// everything that looks for one handles its absence. Created after the spawn pass
				// and before the first tick, so `!player` resolves for the map's own logic_auto
				// ignition; it hydrates from the session record the previous map dehydrated into.
				if (!bMenuBackdrop)
				{
					EntityWorld->SpawnPlayer();

					// 11.9 — if the run has been here before (this session, or a loaded save), the
					// map is not new: apply the frozen snapshot over the freshly-built world
					// (`docs/architecture/save-architecture.md` §5). After SpawnPlayer, so the player exists for the
					// records that reference it, and before the first Tick, so nothing has run yet.
					//
					// A dev fresh-state entry (`elysium.newgame_ttd`) drops the snapshot here rather
					// than at the command, because the travel it issued tore this map down on the way
					// out and froze it again.
					UElysiumMapSubsystem* MapsForState =
						GetGameInstance() ? GetGameInstance()->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
					if (MapsForState && MapsForState->ConsumeFreshMapState())
					{
						GameState->ClearMapSnapshot(MapName);
						UE_LOG(LogElysium, Log, TEXT("fresh map state: %s forgotten"), *MapName);
					}
					else if (const FElysiumMapSnapshot* Snapshot = GameState->FindMapSnapshot(MapName))
					{
						EntityWorld->ApplySnapshot(*Snapshot);
					}
				}
			}
			else
			{
				UE_LOG(LogElysium, Log, TEXT("no %s.ents — entity world not built"), *MapName);
			}
		}
	}

	// P4.6 — a landmark transition places the player against the destination info_landmark instead of
	// info_player_start. Runs after the entity world is built (the landmark is one of its entities),
	// and not at all on a backdrop, which has no entity world and seats no player.
	if (!bMenuBackdrop)
	{
		ResolveLandmarkSpawn();
		ResolveRestorePlacement();
	}

	Phase(TEXT("Entities"));

	// The entity world is still dormant here: no logic_auto, trigger, scene, camera or game-clock
	// work can run. Walk every map-authored animation reference, retain the skeleton-bound sequences
	// in Bodies' map-epoch caches, then wait on only those editor compilation jobs. In a packaged
	// build the same walk performs the synchronous loose-asset loads and the finish is a no-op.
	if (EntityWorld)
	{
		EntityWorld->PreloadMapAnimations();
	}
	const int32 ResidentAnimations = FinishAnimationPreload();
	bAnimationPreloadReady = true;
	Phase(TEXT("Animations"));
	UE_LOG(LogElysium, Log, TEXT("map animation residency %s: %d sequence(s) ready before activation"),
		*MapName, ResidentAnimations);

	const double TotalMs = (FPlatformTime::Seconds() - Start) * 1000.0;
	LoadPhases.Add({ TEXT("Total"), TotalMs });
	UE_LOG(LogElysium, Log, TEXT("loaded %s in %.2fs"), *MapName, TotalMs / 1000.0);
}

bool AElysiumMapActor::ReadSpawn(FVector& OutLocation, float& OutYaw) const
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *FElysiumContentPaths::MapSpawn(MapName)))
	{
		return false;
	}

	bool bHasOrigin = false;
	float YawSrc = 0.f;
	FVector Origin = FVector::ZeroVector;

	for (const FString& Line : Lines)
	{
		TArray<FString> Tok;
		Line.ParseIntoArray(Tok, TEXT(" "), true);
		if (Tok.Num() == 4 && Tok[0] == TEXT("origin"))
		{
			Origin = FVector(FCString::Atod(*Tok[1]), FCString::Atod(*Tok[2]), FCString::Atod(*Tok[3]));
			bHasOrigin = true;
		}
		else if (Tok.Num() == 2 && Tok[0] == TEXT("yaw"))
		{
			YawSrc = FCString::Atof(*Tok[1]);
		}
	}

	if (!bHasOrigin)
	{
		return false;
	}

	// Authored Source origins are feet. The readiness poll adds the active body's exact half-height
	// once the pawn exists; keeping the logical placement in feet avoids a magic 100 cm lift.
	OutLocation = Origin;
	// .spawn already carries Unreal-space yaw (UE_bsp_to_scene negates it at export).
	OutYaw = YawSrc;
	return true;
}

void AElysiumMapActor::ResolveLandmarkSpawn()
{
	UGameInstance* GI = GetGameInstance();
	UElysiumMapSubsystem* Maps = GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
	if (!Maps || !EntityWorld)
	{
		return;
	}

	FString Landmark; FVector Offset; float Yaw; bool bHasYaw;
	if (!Maps->ConsumeLandmarkSpawn(Landmark, Offset, Yaw, bHasYaw))
	{
		return;   // not a landmark transition — keep the info_player_start placement (ReadSpawn)
	}

	FElysiumEntity* Lm = EntityWorld->FindLandmark(Landmark);
	if (!Lm || !Lm->Def)
	{
		// Both maps must carry an info_landmark of the same name; a missing one is a data error.
		// Fall back to info_player_start rather than dumping the player at the origin (matches the
		// decompiled "can't find landmark" warning path).
		UE_LOG(LogElysium, Warning,
			TEXT("landmark '%s' not found in %s — spawning at info_player_start"), *Landmark, *MapName);
		return;
	}

	// New feet = destination landmark origin + the Source-feet offset captured at the source.
	PendingSpawnLoc = Lm->Def->Origin + Offset;
	if (bHasYaw)
	{
		PendingSpawnYaw = Yaw;   // preserve the player's view yaw across the transition
	}
	else
	{
		PendingSpawnYaw = -Lm->Angles.Y;   // face the landmark's angles (Source yaw negated to Unreal)
	}
	PendingSpawnSpace = EElysiumPlayerPlacementSpace::Feet;
	bSpawnPending = true;
	EntryLandmark = Landmark;

	// Fire the landmark's OnEnterMapHere (e.g. pawnshop's newgame/haven -> Radio2.Deactivate). The
	// spawn pass is complete, so every wire target exists; FireOutput queues it on the event queue,
	// serviced on the first tick (after any logic_auto OnMapLoad, matching the map-enter ordering).
	static const FName OnEnterMapHere(TEXT("OnEnterMapHere"));
	Lm->FireOutput(OnEnterMapHere, Lm->Handle);

	UE_LOG(LogElysium, Log, TEXT("landmark spawn: %s @ %s -> %s (yaw %.0f)"),
		*MapName, *Landmark, *PendingSpawnLoc.ToString(), PendingSpawnYaw);
}

void AElysiumMapActor::ResolveRestorePlacement()
{
	UGameInstance* GI = GetGameInstance();
	UElysiumMapSubsystem* Maps = GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
	if (!Maps)
	{
		return;
	}

	// 11.9 — a loaded save carries the pose the player was actually standing in, so it outranks both
	// info_player_start and a landmark offset. It is already a pawn-space (capsule-centre) location:
	// the save read it off the body, so it goes back verbatim with no lift.
	FVector Origin; float Yaw;
	if (!Maps->ConsumeRestorePlacement(Origin, Yaw))
	{
		return;
	}
	PendingSpawnLoc = Origin;
	PendingSpawnYaw = Yaw;
	PendingSpawnSpace = EElysiumPlayerPlacementSpace::CapsuleCenter; // legacy save payload contract
	bSpawnPending = true;
	UE_LOG(LogElysium, Log, TEXT("restore placement: %s @ %s (yaw %.0f)"),
		*MapName, *PendingSpawnLoc.ToString(), PendingSpawnYaw);
}

void AElysiumMapActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Retire the NPC motors while they are still real objects. The entity world is destroyed with
	// this actor, and every FElysiumNpc destructor on that path calls DestroyNpcMotor — but by then
	// the level's actors are gone and their UObject indices freed, so touching one at all is fatal.
	// The engine destroys the motor actors itself; releasing the tracking array is the whole job.
	bMotorsRetired = true;
	NpcMotors.Reset();

	// The substrate owns plain C++ entities, but its teardown reaches map-owned UObjects through the
	// embodiment seam (interaction anchors, bodies and scripted cameras). Run that teardown while
	// EndPlay still guarantees those objects have valid UObject indices; waiting for this actor's C++
	// destructor is too late because world cleanup may already have reclaimed its components.
	EntityWorld.Reset();

	// The scheme manager is this actor's own, so its voices are stopped here rather than at the
	// epoch boundary below — it will not exist to be asked once this actor is gone.
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (UElysiumAudioSubsystem* Audio = GI->GetSubsystem<UElysiumAudioSubsystem>())
		{
			if (SchemeManager)
			{
				SchemeManager->StopAll(Audio);
			}
		}
	}

	// Close this map's epoch (S4). Every application-lifetime object holding state on this map's
	// behalf — voices, camera requests, the character stage's actors, debug NPC bodies, the level
	// script's path entry — frees it from this one broadcast. It happens here, and not in this
	// actor's destructor, because the world is still standing: a subscriber may destroy actors and
	// components rather than merely dropping references to them.
	if (UElysiumMapSubsystem* Maps = GetMapSubsystem())
	{
		Maps->RetireMapEpoch(MapEpoch);
	}
	Super::EndPlay(EndPlayReason);
}

double AElysiumMapActor::GetRuntimeWaitSeconds() const
{
	if (RuntimePhase == EElysiumMapRuntimePhase::Active
		|| RuntimePhase == EElysiumMapRuntimePhase::Failed)
	{
		return RuntimeWaitDurationSeconds;
	}
	return RuntimeWaitStartSeconds > 0.0
		? FMath::Max(0.0, FPlatformTime::Seconds() - RuntimeWaitStartSeconds)
		: 0.0;
}

FString AElysiumMapActor::GetMissingRuntimePrerequisites() const
{
	return CollectRuntimePrerequisites().Missing();
}

FElysiumMapRuntimePrerequisites AElysiumMapActor::CollectRuntimePrerequisites() const
{
	FElysiumMapRuntimePrerequisites P;
	P.bConstructionComplete = bRuntimeConstructionComplete;
	P.bEntityWorldReady = EntityWorld.Get() != nullptr;
	P.bAnimationPreloadReady = bAnimationPreloadReady;
	const UElysiumAudioSubsystem* Audio = GetAudioSubsystem();
	P.bAudioCatalogReady = !Audio || Audio->IsReadyForMapActivation();
	P.bMenuBackdrop = bMenuBackdrop;
	const EElysiumCollisionBuildState CollisionState = Collision
		? Collision->GetBuildState() : EElysiumCollisionBuildState::Failed;
	P.bCollisionReady = CollisionState == EElysiumCollisionBuildState::Ready
		|| CollisionState == EElysiumCollisionBuildState::Disabled;
	P.bCollisionFailed = CollisionState == EElysiumCollisionBuildState::Failed;
	P.bNavigationRequired = !bMenuBackdrop
		&& CollisionState != EElysiumCollisionBuildState::Disabled;
	P.bNavigationReady = !P.bNavigationRequired || IsRuntimeNavigationReady();
	P.bNavigationFailed = bNavigationBuildFailed;
	P.bSpawnTransformReady = bSpawnPending;
	P.bPlayerEntityReady = EntityWorld && EntityWorld->PlayerHandle().IsSet();

	if (!bMenuBackdrop)
	{
		const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		P.bPossessedPawnReady = Pawn && Pawn->GetController() == PC;
		P.bPlayerBodyReady = Pawn && Cast<IElysiumPlayerBody>(Pawn) != nullptr;
		P.bFinalPlacementReady = bSpawnPlaced;
		UPawnMovementComponent* Move = Pawn ? Pawn->GetMovementComponent() : nullptr;
		P.bTickPrerequisitesReady = Move && PrereqController.Get() == PC
			&& PrereqMovement.Get() == Move;
	}
	return P;
}

void AElysiumMapActor::PollRuntimeActivation()
{
	EnsureRuntimeNavigation();
	if (!bMenuBackdrop && bRuntimeConstructionComplete)
	{
		APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		IElysiumPlayerBody* Body = Pawn ? Cast<IElysiumPlayerBody>(Pawn) : nullptr;
		if (Pawn && Pawn->GetController() == PC && Body && !bSpawnPlaced)
		{
			// Freeze before changing the transform: even if movement's prerequisite runs later in this
			// frame, the newly placed pawn cannot take an unguarded step.
			Body->SetMovementFrozen(true);
			const FVector Placement = ElysiumPlayerPlacement::ToCapsuleCenter(
				PendingSpawnLoc, PendingSpawnSpace, Body->GetBodyHalfHeight());
			Pawn->SetActorLocation(Placement, false, nullptr, ETeleportType::TeleportPhysics);
			PC->SetControlRotation(FRotator(0.f, PendingSpawnYaw, 0.f));
			bSpawnPlaced = true;
			EnsureTickPrerequisites();
			UE_LOG(LogElysium, Log, TEXT("map runtime %s: player placed and frozen at %s"),
				*MapName, *Placement.ToString());
		}
	}

	const FElysiumMapRuntimePrerequisites P = CollectRuntimePrerequisites();
	FString Failure;
	switch (P.Evaluate(GetRuntimeWaitSeconds(), Failure))
	{
	case EElysiumMapReadinessResult::Ready:
		RuntimePhase = EElysiumMapRuntimePhase::Activating;
		UE_LOG(LogElysium, Log, TEXT("map runtime %s: %s after %.3fs"), *MapName,
			ElysiumMapRuntimePhaseName(RuntimePhase), GetRuntimeWaitSeconds());
		return;
	case EElysiumMapReadinessResult::Failed:
		if (P.bCollisionFailed && Collision && !Collision->GetFailureReason().IsEmpty())
		{
			Failure = Collision->GetFailureReason();
		}
		FailRuntime(Failure);
		return;
	case EElysiumMapReadinessResult::Waiting:
	default:
		return;
	}
}

void AElysiumMapActor::EnsureRuntimeNavigation()
{
	if (bMenuBackdrop || bNavigationBuildRequested || bNavigationBuildFailed || !Collision
		|| Collision->GetBuildState() != EElysiumCollisionBuildState::Ready)
	{
		return;
	}

	const FBox CollisionBounds = Collision->GetWorldBounds();
	UWorld* World = GetWorld();
	UNavigationSystemV1* Navigation = World
		? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!CollisionBounds.IsValid || !World || !Navigation)
	{
		bNavigationBuildFailed = true;
		UE_LOG(LogElysium, Error, TEXT("runtime navigation %s: no valid collision bounds/navigation system"),
			*MapName);
		return;
	}

	// The nav-bounds actor normally carries an editor-authored brush. Generated maps intentionally
	// carry no nav asset, so a no-collision UBoxComponent contributes the equivalent runtime bounds;
	// UNavigationSystemV1 reads GetComponentsBoundingBox and Recast projects the actual colliders.
	const FVector Center = CollisionBounds.GetCenter();
	FVector Extent = CollisionBounds.GetExtent();
	Extent.X += 500.0f;
	Extent.Y += 500.0f;
	Extent.Z += 300.0f;

	const FTransform BoundsTransform(FRotator::ZeroRotator, Center);
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.OverrideLevel = GetLevel();
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.bDeferConstruction = true;
	NavigationBounds = World->SpawnActor<ANavMeshBoundsVolume>(
		ANavMeshBoundsVolume::StaticClass(), BoundsTransform, Params);
	if (!NavigationBounds)
	{
		bNavigationBuildFailed = true;
		UE_LOG(LogElysium, Error, TEXT("runtime navigation %s: failed to create bounds"), *MapName);
		return;
	}

	UBoxComponent* BoundsBox = NewObject<UBoxComponent>(NavigationBounds, TEXT("ElysiumNavigationBounds"));
	BoundsBox->SetMobility(EComponentMobility::Static);
	BoundsBox->SetBoxExtent(Extent);
	BoundsBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoundsBox->SetCanEverAffectNavigation(false);
	BoundsBox->SetupAttachment(NavigationBounds->GetRootComponent());
	NavigationBounds->AddInstanceComponent(BoundsBox);
	NavigationBounds->FinishSpawning(BoundsTransform);
	if (!BoundsBox->IsRegistered())
	{
		BoundsBox->RegisterComponent();
	}

	// Both colliders cook asynchronously after their components register. Refresh their octree data
	// now that the activation barrier has observed completed BodySetups, then build exactly once.
	Collision->RefreshNavigationData();
	Navigation->OnNavigationBoundsUpdated(NavigationBounds);
	Navigation->Build();
	bNavigationBuildRequested = true;
	UE_LOG(LogElysium, Log, TEXT("runtime navigation %s: Recast build requested over %s"),
		*MapName, *CollisionBounds.ToString());
}

bool AElysiumMapActor::IsRuntimeNavigationReady() const
{
	if (!bNavigationBuildRequested || bNavigationBuildFailed || !NavigationBounds)
	{
		return false;
	}
	UNavigationSystemV1* Navigation = GetWorld()
		? FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()) : nullptr;
	const ARecastNavMesh* Recast = Navigation
		? Cast<ARecastNavMesh>(Navigation->GetMainNavData()) : nullptr;
	return Recast && Recast->GetNumActiveTiles() > 0
		&& !Navigation->IsNavigationBuildInProgress();
}

void AElysiumMapActor::ActivateRuntime()
{
	if (RuntimePhase != EElysiumMapRuntimePhase::Activating)
	{
		return;
	}

	double Now = 0.0;
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UElysiumGameStateSubsystem* GameState = GI->GetSubsystem<UElysiumGameStateSubsystem>())
		{
			Now = GameState->GameClock().GetNow();
		}
	}
	// Characters are constructed with the entity world, before asynchronous collision and Recast
	// exist. Admit their visibility/collision only at the same atomic activation barrier as the
	// player. Idle CharacterMovement stays asleep; the first MoveTo wakes it against the complete graph.
	for (AElysiumNpcBody* Motor : NpcMotors)
	{
		if (IsValid(Motor))
		{
			Motor->SetRuntimeReady(true);
		}
	}

	if (EntityWorld)
	{
		EntityWorld->Activate(Now);
	}
	if (APawn* Pawn = ResolvePlayerPawn())
	{
		ReconcilePlayerBrushTouches(Pawn);
	}
	if (EntityWorld)
	{
		// One frozen-time pass ignites logic_auto, map-entry outputs, initial containment touches,
		// and every zero-delay event they produce before the partial world is ever shown.
		EntityWorld->RunPlayerThink(Now);
		EntityWorld->Tick(Now);
	}
	if (bHasDeferredSchemeFadeIn && SchemeManager)
	{
		SchemeManager->FadeInScheme(GetAudioSubsystem(), DeferredSchemeRel,
			DeferredSchemeAnchor, DeferredSchemeFadeSeconds);
		bHasDeferredSchemeFadeIn = false;
		DeferredSchemeRel.Reset();
	}
	TickAudio(0.0f);

	RuntimeWaitDurationSeconds = GetRuntimeWaitSeconds();
	RuntimePhase = EElysiumMapRuntimePhase::Active;
	bSpawnDone = true;
	if (APawn* Pawn = ResolvePlayerPawn())
	{
		if (IElysiumPlayerBody* Body = Cast<IElysiumPlayerBody>(Pawn))
		{
			// A stage world has no walkable surface at all, so releasing the pawn drops it out of the
			// level for as long as the green room is open. It stays frozen where the spawn seated it;
			// the view belongs to the camera shot stack, which does not consult the pawn's feet.
			Body->SetMovementFrozen(bStageOnly);
		}
	}
	PreMoveTickFunction.bTickEvenWhenPaused = false;
	PrimaryActorTick.bTickEvenWhenPaused = false;
	GameplayTickFunction.bTickEvenWhenPaused = false;

	// Everything this map places is standing by now — the adopted baked actors and the NPC bodies
	// the entity world built — so this is the one point where the level's material bindings can be
	// read whole. A body spawned after activation is not covered; the audit is a load-time listing,
	// not a live watch.
	if (Visuals)
	{
		Visuals->AuditMaterials(MapName);
	}

	UE_LOG(LogElysium, Log, TEXT("map runtime %s: Active after %.3fs at game time %.3f"),
		*MapName, GetRuntimeWaitSeconds(), Now);
	RuntimeReady.Broadcast(this);
}

void AElysiumMapActor::FailRuntime(const FString& Reason)
{
	if (RuntimePhase == EElysiumMapRuntimePhase::Failed
		|| RuntimePhase == EElysiumMapRuntimePhase::Active)
	{
		return;
	}
	RuntimeFailureReason = Reason;
	RuntimeWaitDurationSeconds = GetRuntimeWaitSeconds();
	RuntimePhase = EElysiumMapRuntimePhase::Failed;
	UE_LOG(LogElysium, Error,
		TEXT("map runtime %s: Failed after %.3fs: %s [missing: %s]"),
		*MapName, GetRuntimeWaitSeconds(), *RuntimeFailureReason,
		*GetMissingRuntimePrerequisites());
	RuntimeFailed.Broadcast(this, RuntimeFailureReason);
}
