#include "ElysiumMapActor.h"

#include "ElysiumAudioSubsystem.h"
#include "ElysiumBrushComponent.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumPlayerBody.h"
#include "ElysiumPresentationSubsystem.h"
#include "ElysiumUseIcons.h"
#include "Audio/ElysiumSoundScheme.h"
#include "Map/ElysiumMapCollision.h"
#include "Player/ElysiumCameraShots.h"
#include "Visual/ElysiumEntityBodies.h"
#include "Visual/ElysiumMapVisuals.h"

#include "Engine/GameInstance.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysium, Log, All);

// ------------------------------------------------------------------------------------------
// S2 — the post-move tick function (runtime-architecture.md §3, step 7).
// ------------------------------------------------------------------------------------------

void FElysiumPostMoveTickFunction::ExecuteTick(float DeltaTime, ELevelTick TickType,
	ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	if (Target && IsValidChecked(Target) && !Target->IsUnreachable())
	{
		FScopeCycleCounterUObject ActorScope(Target);
		Target->PostMoveTick(DeltaTime);
	}
}

FString FElysiumPostMoveTickFunction::DiagnosticMessage()
{
	return GetFullNameSafe(Target) + TEXT("[AElysiumMapActor::PostMoveTick]");
}

FName FElysiumPostMoveTickFunction::DiagnosticContext(bool bDetailed)
{
	if (bDetailed)
	{
		return FName(*FString::Printf(TEXT("ElysiumMapActorPostMove/%s"), *GetFullNameSafe(Target)));
	}
	return FName(TEXT("ElysiumMapActorPostMove"));
}

AElysiumMapActor::AElysiumMapActor()
{
	// S2 — the frame order is declared with tick groups, not left to registration order. The
	// gameplay pass (clock, thinks, queue) runs before physics; the post-move pass runs after it
	// and after the pawn's move. Neither ticks while the game is held: pause is meant to stop the
	// world, and the presentation side is what keeps drawing (§4).
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	PrimaryActorTick.bTickEvenWhenPaused = false;

	PostMoveTickFunction.bCanEverTick = true;
	PostMoveTickFunction.bStartWithTickEnabled = true;
	PostMoveTickFunction.TickGroup = TG_PostPhysics;
	PostMoveTickFunction.bTickEvenWhenPaused = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	// The three halves this actor is not. Each builds its own child components at load, so a map
	// with no sky builds no backdrop and a map with no `.hulls` builds no collider. The sun, sky
	// light and height fog are actors in the baked level, adopted by the visuals — this actor owns
	// no lighting, no geometry and no material of its own.
	Visuals = CreateDefaultSubobject<UElysiumMapVisuals>(TEXT("Visuals"));
	Visuals->SetupAttachment(SceneRoot);
	Collision = CreateDefaultSubobject<UElysiumMapCollision>(TEXT("Collision"));
	Collision->SetupAttachment(SceneRoot);
	Bodies = CreateDefaultSubobject<UElysiumEntityBodies>(TEXT("Bodies"));
}

void AElysiumMapActor::RegisterActorTickFunctions(bool bRegister)
{
	Super::RegisterActorTickFunctions(bRegister);

	if (bRegister)
	{
		if (PostMoveTickFunction.bCanEverTick)
		{
			PostMoveTickFunction.Target = this;
			PostMoveTickFunction.SetTickFunctionEnable(PostMoveTickFunction.bStartWithTickEnabled);
			PostMoveTickFunction.RegisterTickFunction(GetLevel());
			// The tick groups already separate the two passes; the prerequisite says so in the
			// graph as well, so the dependency survives anyone re-grouping either end.
			PostMoveTickFunction.AddPrerequisite(this, PrimaryActorTick);
		}
	}
	else if (PostMoveTickFunction.IsTickFunctionRegistered())
	{
		PostMoveTickFunction.UnRegisterTickFunction();
	}
}

void AElysiumMapActor::EnsureTickPrerequisites()
{
	UWorld* W = GetWorld();
	APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return;
	}

	// Step 1 -> steps 2-4: the frame's input sample lands before the substrate runs.
	if (PrereqController.Get() != PC)
	{
		AddTickPrerequisiteActor(PC);
		PrereqController = PC;
	}

	// Steps 2-4 -> step 5: the pawn moves against the positions this frame's thinks produced.
	// A door's think issues its swept move here; moving the pawn first tunnels it on fast movers.
	UPawnMovementComponent* Move = PC->GetPawn() ? PC->GetPawn()->GetMovementComponent() : nullptr;
	if (Move && PrereqMovement.Get() != Move)
	{
		Move->PrimaryComponentTick.AddPrerequisite(this, PrimaryActorTick);
		PrereqMovement = Move;

		// 11.4 — tell the body which entity it embodies. Done here rather than at SpawnPlayer
		// because a fresh world has no pawn yet when the map builds, and this already runs each
		// gameplay tick until the pawn appears (and again if it is replaced).
		if (IElysiumPlayerBody* Body = Cast<IElysiumPlayerBody>(PC->GetPawn()))
		{
			Body->SetPlayerEntity(EntityWorld ? EntityWorld->PlayerHandle() : FElysiumEntityHandle::Invalid());
		}
	}
}

void AElysiumMapActor::BeginPlay()
{
	Super::BeginPlay();

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
}

void AElysiumMapActor::LoadMap()
{
	const double Start = FPlatformTime::Seconds();

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
	// `docs/decisions.md`). What a backdrop skips is only the player's placement — it seats no pawn.
	UElysiumMapSubsystem* MapSubsystem =
		GetGameInstance() ? GetGameInstance()->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
	const bool bMenuBackdrop = MapSubsystem && MapSubsystem->IsMenuBackdrop();

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

				// 11.2 — hand the substrate its outbound seam. This actor is three of the four
				// services; the fourth is the world-scoped presentation subsystem (11.8), which is
				// null only where there is no publisher at all (an editor preview world, a
				// Substrate-tier world with no engine behind it).
				FElysiumWorldServices Services;
				Services.Embodiment = this;
				Services.Audio      = this;
				Services.Travel     = this;
				Services.Presenter  = UElysiumPresentationSubsystem::Get(GetWorld());
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
					// (`save-architecture.md` §5). After SpawnPlayer, so the player exists for the
					// records that reference it, and before the first Tick, so nothing has run yet.
					if (const FElysiumMapSnapshot* Snapshot = GameState->FindMapSnapshot(MapName))
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

	const double TotalMs = (FPlatformTime::Seconds() - Start) * 1000.0;
	LoadPhases.Add({ TEXT("Total"), TotalMs });
	UE_LOG(LogElysium, Log, TEXT("loaded %s in %.2fs"), *MapName, TotalMs / 1000.0);
}

float AElysiumMapActor::BodyScaleFor(const FElysiumEntityDef& Def) const
{
	return Def.bSky ? SkyDef.Scale : 1.f;
}

// ============================================================================================
// The world services (11.2) — the substrate's engine side. Everything here is a forward: the
// body factory, the player's pawn, the GI-scoped audio subsystem, this map's scheme manager, the
// map subsystem. Nothing under FElysiumEntityWorld knows any of those exist.
// ============================================================================================

USkeletalMeshComponent* AElysiumMapActor::BuildNpcVisual(const FString& Stem, const FVector& Location,
	const FRotator& Rotation, float UniformScale, const FString& Disposition, int32 IdleVariant)
{
	return Bodies->BuildNpcVisual(Stem, Location, Rotation, UniformScale, Disposition, IdleVariant);
}

bool AElysiumMapActor::RefreshNpcIdle(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& Disposition, int32 IdleVariant)
{
	return Bodies->RefreshNpcIdle(Body, Stem, Disposition, IdleVariant);
}

bool AElysiumMapActor::PlayNpcClip(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& ClipName, bool bLoop, float* OutSeconds)
{
	return Bodies->PlayNpcClip(Body, Stem, ClipName, bLoop, OutSeconds);
}

UStaticMeshComponent* AElysiumMapActor::BuildPropVisual(const FString& Stem, const FVector& Location,
	const FQuat& Rotation, float UniformScale)
{
	return Bodies->BuildPropVisual(Stem, Location, Rotation, UniformScale);
}

UStaticMeshComponent* AElysiumMapActor::BuildPhysPropVisual(const FString& Stem, const FVector& Location,
	const FQuat& Rotation, float UniformScale)
{
	return Bodies->BuildPhysPropVisual(Stem, Location, Rotation, UniformScale);
}

void AElysiumMapActor::ApplyPropSkin(UStaticMeshComponent* Comp, const FString& Stem, int32 Family)
{
	Bodies->ApplyPropSkin(Comp, Stem, Family);
}

APawn* AElysiumMapActor::ResolvePlayerPawn() const
{
	const UWorld* W = GetWorld();
	const APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	return PC ? PC->GetPawn() : nullptr;
}

bool AElysiumMapActor::GetPlayerViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
	const APawn* Pawn = ResolvePlayerPawn();
	if (!Pawn)
	{
		return false;
	}
	if (const APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
	{
		PC->GetPlayerViewPoint(OutLocation, OutRotation);
	}
	else
	{
		// No controller (a detached/possessed-later pawn): the body's own transform is the best
		// available eye, which is what the trigger_look path has always fallen back to.
		OutLocation = Pawn->GetActorLocation();
		OutRotation = Pawn->GetActorRotation();
	}
	return true;
}

bool AElysiumMapActor::GetPlayerOrigin(FVector& OutLocation, float& OutYaw) const
{
	const APawn* Pawn = ResolvePlayerPawn();
	if (!Pawn)
	{
		return false;
	}
	OutLocation = Pawn->GetActorLocation();
	const APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
	OutYaw = PC ? (float)PC->GetControlRotation().Yaw : (float)Pawn->GetActorRotation().Yaw;
	return true;
}

void AElysiumMapActor::TeleportPlayer(const FVector& FeetOrigin, float Yaw)
{
	APawn* Pawn = ResolvePlayerPawn();
	if (!Pawn)
	{
		return;
	}
	// Source places the entity's absorigin (feet); both Unreal bodies are centred, so lift by the
	// body's half-height to seat the player on the destination rather than in the floor. This is
	// the body's own geometry, which is why the compensation lives here and not in point_teleport,
	// and why the number comes from the body rather than from an assumed shape.
	FVector Dest = FeetOrigin;
	if (const IElysiumPlayerBody* Body = Cast<IElysiumPlayerBody>(Pawn))
	{
		Dest.Z += Body->GetBodyHalfHeight();
	}
	Pawn->SetActorLocation(Dest, false, nullptr, ETeleportType::TeleportPhysics);
	if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
	{
		PC->SetControlRotation(FRotator(0.0f, Yaw, 0.0f));
	}
}

void AElysiumMapActor::DamagePlayer(float Amount)
{
	APawn* Pawn = ResolvePlayerPawn();
	if (Pawn && Amount > 0.f)
	{
		// This actor is the damage causer: every entity body is one of its components, so it is
		// what the old per-call `Body->GetOwner()` resolved to anyway.
		UGameplayStatics::ApplyDamage(Pawn, Amount, nullptr, const_cast<AElysiumMapActor*>(this),
			UDamageType::StaticClass());
	}
}

FElysiumEntityHandle AElysiumMapActor::TraceUseCursor(const FVector& Start, const FVector& End) const
{
	UWorld* W = GetWorld();
	if (!W)
	{
		return FElysiumEntityHandle::Invalid();
	}

	// A single blocking trace naturally handles occlusion: a wall (or any solid) closer than the
	// button ends the ray. The dedicated +use channel (ELYSIUM_USE_CHANNEL, default-Block) keeps
	// world + solid bodies as occluders while staying isolated from ECC_Visibility;
	// func_button/func_door bodies are Solid (BlockAll), so they block it.
	FCollisionQueryParams Params(FName(TEXT("ElysiumUseCursor")), /*bTraceComplex*/ false);
	Params.AddIgnoredActor(ResolvePlayerPawn());
	FHitResult H;
	if (W->LineTraceSingleByChannel(H, Start, End, ELYSIUM_USE_CHANNEL, Params))
	{
		if (const UElysiumBrushComponent* B = Cast<UElysiumBrushComponent>(H.GetComponent()))
		{
			return B->GetOwningEntity();
		}
	}
	return FElysiumEntityHandle::Invalid();
}

UElysiumCameraComponent* AElysiumMapActor::PlayerCamera() const
{
	const APawn* Pawn = ResolvePlayerPawn();
	const IElysiumPlayerBody* Body = Cast<IElysiumPlayerBody>(Pawn);
	return Body ? Body->GetCameraComponent() : nullptr;
}

int32 AElysiumMapActor::PushCameraShot(const FString& ShotFile, const FElysiumEntityHandle& Subject)
{
	if (!CameraDirector)
	{
		CameraDirector = MakePimpl<FElysiumCameraDirector>();
	}
	return CameraDirector->Push(EntityWorld.Get(), PlayerCamera(), ShotFile, Subject);
}

bool AElysiumMapActor::PopCameraShot(int32 ShotId)
{
	return CameraDirector ? CameraDirector->Pop(PlayerCamera(), ShotId) : false;
}

FElysiumAudioVoiceHandle AElysiumMapActor::PlayVoice(const FString& Rel, const FElysiumPlayParams& Params)
{
	UElysiumAudioSubsystem* Audio = GetAudioSubsystem();
	return Audio ? Audio->PlayVoice(Rel, Params) : FElysiumAudioVoiceHandle::Invalid();
}

void AElysiumMapActor::StopVoice(FElysiumAudioVoiceHandle Handle, float FadeSeconds)
{
	if (UElysiumAudioSubsystem* Audio = GetAudioSubsystem())
	{
		Audio->StopVoice(Handle, FadeSeconds);
	}
}

void AElysiumMapActor::SetVoiceVolume(FElysiumAudioVoiceHandle Handle, float Volume)
{
	if (UElysiumAudioSubsystem* Audio = GetAudioSubsystem())
	{
		Audio->SetVoiceVolume(Handle, Volume);
	}
}

bool AElysiumMapActor::IsVoicePlaying(FElysiumAudioVoiceHandle Handle) const
{
	const UElysiumAudioSubsystem* Audio = GetAudioSubsystem();
	return Audio && Audio->IsVoicePlaying(Handle);
}

void AElysiumMapActor::FadeInScheme(const FString& SchemeRel, const FVector& Anchor, float FadeSeconds)
{
	if (SchemeManager)
	{
		SchemeManager->FadeInScheme(GetAudioSubsystem(), SchemeRel, Anchor, FadeSeconds);
	}
}

void AElysiumMapActor::FadeOutScheme(const FString& SchemeRel, float FadeSeconds)
{
	if (SchemeManager)
	{
		SchemeManager->FadeOutScheme(GetAudioSubsystem(), SchemeRel, FadeSeconds);
	}
}

FString AElysiumMapActor::ActiveSchemeRel() const
{
	return SchemeManager ? SchemeManager->ActiveSchemeRel() : FString();
}

void AElysiumMapActor::RequestLandmarkTravel(const FString& Map, const FString& Landmark,
	const FVector& Offset, float Yaw)
{
	if (UElysiumMapSubsystem* Maps = GetMapSubsystem())
	{
		Maps->RequestLandmarkTravel(Map, Landmark, Offset, Yaw);
	}
}

void AElysiumMapActor::ChangeMap(const FString& Map)
{
	if (UElysiumMapSubsystem* Maps = GetMapSubsystem())
	{
		Maps->Travel(Map);
	}
}

UElysiumAudioSubsystem* AElysiumMapActor::GetAudioSubsystem() const
{
	const UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UElysiumAudioSubsystem>() : nullptr;
}

UElysiumMapSubsystem* AElysiumMapActor::GetMapSubsystem() const
{
	const UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
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

	// Lift off the floor so the pawn's collision capsule clears the ground on spawn.
	OutLocation = Origin + FVector(0.f, 0.f, 100.f);
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

	// new pos = destination landmark origin + the offset captured at the source landmark. The offset
	// already carries the player's capsule-centre height above the landmark, so a real transition
	// needs no extra lift; a direct/console landmark entry (offset zero) seats the capsule centre by
	// lifting off the landmark's feet origin, and faces the landmark's own angles.
	PendingSpawnLoc = Lm->Def->Origin + Offset;
	if (bHasYaw)
	{
		PendingSpawnYaw = Yaw;   // preserve the player's view yaw across the transition
	}
	else
	{
		PendingSpawnLoc.Z += 100.0f;   // lift the capsule off the landmark feet (as ReadSpawn does)
		PendingSpawnYaw = -Lm->Angles.Y;   // face the landmark's angles (Source yaw negated to Unreal)
	}
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
	bSpawnPending = true;
	UE_LOG(LogElysium, Log, TEXT("restore placement: %s @ %s (yaw %.0f)"),
		*MapName, *PendingSpawnLoc.ToString(), PendingSpawnYaw);
}

void AElysiumMapActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// The audio subsystem is GameInstance-scoped and outlives this map actor, but every voice it
	// holds is map-scoped (ambient_generic + the scheme bed/music/random one-shots). Stop them all
	// on unload so nothing bleeds into the next map. StopAllVoices also covers the scheme voices, so
	// the scheme manager only needs to drop its (now-dead) handles.
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (UElysiumAudioSubsystem* Audio = GI->GetSubsystem<UElysiumAudioSubsystem>())
		{
			if (SchemeManager)
			{
				SchemeManager->StopAll(Audio);
			}
			Audio->StopAllVoices();
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AElysiumMapActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// The controller and the pawn appear after this actor does, so keep looking until the frame
	// order is fully declared (a menu backdrop map never seats a pawn, and that is fine).
	EnsureTickPrerequisites();

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UElysiumGameStateSubsystem* GameState = GI->GetSubsystem<UElysiumGameStateSubsystem>())
		{
			// Step 2 — the only place `Now` moves (S1). DeltaSeconds is already dilated by the
			// engine, and the clock applies no factor of its own, so a time scale is applied once.
			GameState->TimeControl().AdvanceFrame(DeltaSeconds);

			// Steps 3-4 — the substrate, think-first (retail order: Physics_RunThinkFunctions,
			// then CEventQueue::ServiceEvents). Runs every frame, independent of the spawn-hold
			// below, so the map-load I/O chains service immediately.
			if (EntityWorld)
			{
				EntityWorld->Tick(GameState->GameClock().GetNow());
			}

			// P6.3 — reap finished voices, then advance the SoundScheme manager (random-one-shot
			// scheduler + music crossfade) with the listener position for polar placement/attenuation.
			if (UElysiumAudioSubsystem* Audio = GI->GetSubsystem<UElysiumAudioSubsystem>())
			{
				Audio->TickAudio(DeltaSeconds);
				if (SchemeManager)
				{
					FVector ListenerLoc = GetActorLocation();
					if (const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
					{
						FVector VLoc; FRotator VRot;
						PC->GetPlayerViewPoint(VLoc, VRot);
						ListenerLoc = VLoc;
					}
					SchemeManager->Tick(Audio, ListenerLoc, DeltaSeconds);
				}
			}
		}
	}

	if (!bSpawnPending || bSpawnDone)
	{
		return;
	}
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}

	// Place the pawn immediately, but hold it frozen (no gravity) until the async
	// collision cook produces ground under the spawn point — otherwise it falls
	// through the not-yet-cooked floor. A timeout releases it regardless.
	IElysiumPlayerBody* Body = Cast<IElysiumPlayerBody>(Pawn);
	if (!bSpawnPlaced)
	{
		Pawn->SetActorLocation(PendingSpawnLoc, false, nullptr, ETeleportType::TeleportPhysics);
		PC->SetControlRotation(FRotator(0.f, PendingSpawnYaw, 0.f));
		if (Body)
		{
			Body->SetMovementFrozen(true);
		}
		bSpawnPlaced = true;
	}

	SpawnHoldSeconds += DeltaSeconds;
	FHitResult Hit;
	const bool bGround = GetWorld()->LineTraceSingleByChannel(Hit,
		PendingSpawnLoc, PendingSpawnLoc - FVector(0.f, 0.f, 100000.f), ECC_Pawn);
	if (bGround || SpawnHoldSeconds > 8.f)
	{
		if (Body)
		{
			Body->SetMovementFrozen(false);
		}
		UE_LOG(LogElysium, Log, TEXT("spawn released after %.2fs (%s)"),
			SpawnHoldSeconds, bGround ? TEXT("ground ready") : TEXT("timeout"));
		bSpawnDone = true;
	}
}

void AElysiumMapActor::PostMoveTick(float DeltaSeconds)
{
	// Step 7 — everything here reads the frame's final positions.
	//
	// P4.2 — the minimal +use look-cursor: re-pick the aimed usable and fire OnIn/OnOut on the
	// transitions. It traces, so it belongs after physics: before the pawn's move it would pick
	// against last frame's geometry, which reads as a door you cannot use until you stop walking.
	// Its outputs enqueue against the same `now` the gameplay pass advanced to, so they service on
	// the next frame's queue pass exactly like any other zero-delay wire.
	if (EntityWorld)
	{
		EntityWorld->UpdateUseCursor();
	}

	// 11.7 — re-resolve every `Follow` camera shot against this frame's final entity positions. Same
	// reason as the use cursor: a shot framed on where an NPC *was* reads as a camera that lags the
	// subject it is supposed to be locked onto.
	if (CameraDirector)
	{
		CameraDirector->Tick(EntityWorld.Get(), PlayerCamera());
	}

	// The tail of a released frame: a dev step spends one here, and the last one re-holds the world.
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (UElysiumGameStateSubsystem* GameState = GI->GetSubsystem<UElysiumGameStateSubsystem>())
		{
			GameState->TimeControl().EndFrame();
		}
	}
}
