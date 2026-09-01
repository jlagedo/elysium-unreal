#include "ElysiumMapActor.h"

#include "ElysiumAudioSubsystem.h"
#include "ElysiumBrushComponent.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumMovementComponent.h"   // the gait-speed push
#include "ElysiumMoveSolve.h"           // ElysiumMove::U — the one Source-unit conversion
#include "ElysiumPlayer.h"              // FElysiumCombatCharacter — the feed probe's candidate set
#include "ElysiumPlayerBody.h"
#include "ElysiumSkeletalBasis.h"
#include "ElysiumUseIcons.h"
#include "Audio/ElysiumSoundScheme.h"
#include "Map/ElysiumFeedTargeting.h"
#include "Map/ElysiumMapCollision.h"
#include "Map/ElysiumMapLog.h"
#include "Player/ElysiumCameraShots.h"
#include "ElysiumCameraComponent.h"
#include "Visual/ElysiumAnimationDriver.h"
#include "Visual/ElysiumBipedAnimInstance.h"
#include "Visual/ElysiumEntityBodies.h"
#include "Visual/ElysiumAnimSubsystem.h"
#include "Substrate/ElysiumDisposition.h"   // FElysiumEyeTargetTuning — the gaze layer's content
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Visual/ElysiumLightRig.h"        // the authored light set the light query estimates from
#include "Visual/ElysiumMapVisuals.h"
#include "Visual/ElysiumMeleeTrail.h"
#include "Visual/ElysiumNpcVisual.h"

#include "Components/BoxComponent.h"
#include "Components/LightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY(LogElysium);

namespace
{
	// The standing hull a BODILESS candidate is measured by: VtMB's own 32x32x72-unit character box,
	// built off the entity's feet. Only reachable with `elysium.NpcBodies 0` or a failed model,
	// where a rendered bound does not exist; a standing body is measured by its own rendered bounds
	// like every `+use` candidate is.
	//
	// **Feet-anchored, not centred.** An entity's origin is its feet in this runtime, so the box
	// runs origin-half to origin+half+height. Centring it would sink the hull half a body into the
	// floor and put its top at chest height.
	//
	// The three character queries below — feed, aim and swing contact — each build this, and
	// `IsNpcMakerSpawnAreaOccupied` builds the same box from the same constants. One function so a
	// change to VtMB's hull cannot land in three of the four.
	FBox ElysiumStandHullAt(const FVector& FeetOriginCm)
	{
		const FVector Half(ElysiumMove::HullHalfWidth, ElysiumMove::HullHalfWidth, 0.0f);
		return FBox(FeetOriginCm - Half,
			FeetOriginCm + Half + FVector(0.0f, 0.0f, ElysiumMove::StandHeight));
	}
}

// The pre-move tick function (`docs/architecture/runtime-architecture.md` §3, steps 2-3).

void FElysiumPreMoveTickFunction::ExecuteTick(float DeltaTime, ELevelTick TickType,
	ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	if (Target && IsValidChecked(Target) && !Target->IsUnreachable())
	{
		FScopeCycleCounterUObject ActorScope(Target);
		Target->PreMoveTick(DeltaTime);
	}
}

FString FElysiumPreMoveTickFunction::DiagnosticMessage()
{
	return GetFullNameSafe(Target) + TEXT("[AElysiumMapActor::PreMoveTick]");
}

FName FElysiumPreMoveTickFunction::DiagnosticContext(bool bDetailed)
{
	if (bDetailed)
	{
		return FName(*FString::Printf(TEXT("ElysiumMapActorPreMove/%s"), *GetFullNameSafe(Target)));
	}
	return FName(TEXT("ElysiumMapActorPreMove"));
}

// The gameplay tick function (`docs/architecture/runtime-architecture.md` §3, steps 5-6).

void FElysiumGameplayTickFunction::ExecuteTick(float DeltaTime, ELevelTick TickType,
	ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	if (Target && IsValidChecked(Target) && !Target->IsUnreachable())
	{
		FScopeCycleCounterUObject ActorScope(Target);
		Target->GameplayTick(DeltaTime);
	}
}

FString FElysiumGameplayTickFunction::DiagnosticMessage()
{
	return GetFullNameSafe(Target) + TEXT("[AElysiumMapActor::GameplayTick]");
}

FName FElysiumGameplayTickFunction::DiagnosticContext(bool bDetailed)
{
	if (bDetailed)
	{
		return FName(*FString::Printf(TEXT("ElysiumMapActorGameplay/%s"), *GetFullNameSafe(Target)));
	}
	return FName(TEXT("ElysiumMapActorGameplay"));
}

// The post-move tick function (`docs/architecture/runtime-architecture.md` §3, step 8).

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
	// The frame order is declared with tick groups and prerequisites, not left to registration
	// order. Pre-move advances the clock/player think; the actor tick is the map-owned floor's
	// movement-base barrier; gameplay runs after player and NPC movement; post-move runs after
	// physics. Before activation the first three may poll through a hold; activation restores the
	// normal pause-stops-gameplay rule (§4).
	PreMoveTickFunction.bCanEverTick = true;
	PreMoveTickFunction.bStartWithTickEnabled = true;
	PreMoveTickFunction.TickGroup = TG_PrePhysics;
	// The lifecycle-bearing pre-physics functions must continue through the activation barrier if a
	// persistent dev hold survived travel. ActivateRuntime restores normal pause behaviour atomically.
	PreMoveTickFunction.bTickEvenWhenPaused = true;

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	PrimaryActorTick.bTickEvenWhenPaused = true;

	GameplayTickFunction.bCanEverTick = true;
	GameplayTickFunction.bStartWithTickEnabled = true;
	GameplayTickFunction.TickGroup = TG_PrePhysics;
	GameplayTickFunction.bTickEvenWhenPaused = true;

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
		if (PreMoveTickFunction.bCanEverTick)
		{
			PreMoveTickFunction.Target = this;
			PreMoveTickFunction.SetTickFunctionEnable(PreMoveTickFunction.bStartWithTickEnabled);
			PreMoveTickFunction.RegisterTickFunction(GetLevel());
			// Pre-move before the map-owned floor barrier, unconditionally. The pawn's movement
			// component is wired between them when one exists (EnsureTickPrerequisites); gameplay
			// then depends on this barrier even on a backdrop/headless world with no pawn.
			PrimaryActorTick.AddPrerequisite(this, PreMoveTickFunction);
		}
		if (GameplayTickFunction.bCanEverTick)
		{
			GameplayTickFunction.Target = this;
			GameplayTickFunction.SetTickFunctionEnable(GameplayTickFunction.bStartWithTickEnabled);
			GameplayTickFunction.RegisterTickFunction(GetLevel());
			// The actor tick is the movement-base barrier for characters standing on this map's
			// collision. GameFrame is a separate dependent node so NPC movement can sit between them.
			GameplayTickFunction.AddPrerequisite(this, PrimaryActorTick);
		}
		if (PostMoveTickFunction.bCanEverTick)
		{
			PostMoveTickFunction.Target = this;
			PostMoveTickFunction.SetTickFunctionEnable(PostMoveTickFunction.bStartWithTickEnabled);
			PostMoveTickFunction.RegisterTickFunction(GetLevel());
			// The tick groups already separate these two passes; the prerequisite says so in the
			// graph as well, so the dependency survives anyone re-grouping either end.
			PostMoveTickFunction.AddPrerequisite(this, GameplayTickFunction);
		}
	}
	else
	{
		if (PreMoveTickFunction.IsTickFunctionRegistered())
		{
			PreMoveTickFunction.UnRegisterTickFunction();
		}
		if (GameplayTickFunction.IsTickFunctionRegistered())
		{
			GameplayTickFunction.UnRegisterTickFunction();
		}
		if (PostMoveTickFunction.IsTickFunctionRegistered())
		{
			PostMoveTickFunction.UnRegisterTickFunction();
		}
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

	// Step 1 -> steps 2-3: the frame's input sample lands before the clock advances, because the
	// command that sample builds is what the move about to follow is timed against.
	if (PrereqController.Get() != PC)
	{
		PreMoveTickFunction.AddPrerequisite(PC, PC->PrimaryActorTick);
		PrereqController = PC;
	}

	// Steps 2-3 -> step 4 -> steps 5-6 (RE21). Retail never runs the player move inside `GameFrame`:
	// the engine drains the client's `clc_move` message and runs the whole ProcessUsercmds ->
	// CPlayerMove::RunCommand chain there, so the pawn has already moved by the time the first think
	// or queued event runs. The tunnelling hazard the old order guarded against is absorbed on the
	// mover's side instead — a mover sweeps its own body and resolves what it hits.
	UPawnMovementComponent* Move = PC->GetPawn() ? PC->GetPawn()->GetMovementComponent() : nullptr;
	if (Move && PrereqMovement.Get() != Move)
	{
		// A replaced pawn leaves the outgoing component's edge behind, and the gameplay pass would
		// then wait on a tick function that is never going to run again. Drop it before wiring the
		// new one.
		if (UPawnMovementComponent* Previous = PrereqMovement.Get())
		{
			PrimaryActorTick.RemovePrerequisite(Previous, Previous->PrimaryComponentTick);
		}
		Move->PrimaryComponentTick.AddPrerequisite(this, PreMoveTickFunction);
		PrimaryActorTick.AddPrerequisite(Move, Move->PrimaryComponentTick);
		PrereqMovement = Move;
	}

	// Tell the body which entity it embodies. Resynced every tick (cheap: one handle
	// assignment) rather than gated on the movement-prerequisite wiring above: a fresh world has no
	// pawn yet when the map builds, and SpawnPlayer can land on a later tick than the one where this
	// pawn's movement component first appears, so a one-shot assignment here can permanently capture
	// an Invalid() handle and starve every RouteBrushTouch of an activator.
	if (IElysiumPlayerBody* Body = Cast<IElysiumPlayerBody>(PC->GetPawn()))
	{
		Body->SetPlayerEntity(EntityWorld ? EntityWorld->PlayerHandle() : FElysiumEntityHandle::Invalid());
	}
}

USkeletalMeshComponent* AElysiumMapActor::BuildPlayerVisual(const FString& Stem,
	const FString& Disposition, int32 IdleVariant)
{
	APawn* Pawn = ResolvePlayerPawn();
	IElysiumPlayerBody* Body = Pawn ? Cast<IElysiumPlayerBody>(Pawn) : nullptr;
	if (!Body || !Bodies || Stem.IsEmpty())
	{
		return nullptr;
	}
	ClearPlayerVisual();

	FVector Feet = Pawn->GetActorLocation();
	Feet.Z -= Body->GetBodyHalfHeight();
	USkeletalMeshComponent* Visual = Bodies->BuildNpcVisual(Stem, Feet,
		ElysiumSkeletalBasis::FromUnrealYaw(Pawn->GetActorRotation().Yaw),
		/*UniformScale*/ 1.0f, Disposition, IdleVariant, /*bPlayerMaterial=*/true);
	if (!Visual || !Pawn->GetRootComponent())
	{
		return Visual;
	}

	// The glTF model's root is authored at Source absorigin (the feet), while both movement pawns
	// are centred. Attach after loading through the shared skeletal cache, then offset one body
	// half-height so movement, crouching and controller yaw carry the surface automatically.
	Visual->AttachToComponent(Pawn->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
	Visual->SetRelativeLocation(FVector(0.0f, 0.0f, -Body->GetBodyHalfHeight()));
	Visual->SetRelativeRotation(ElysiumSkeletalBasis::RelativeToParentFacing());

	// The mesh animates from the body sample the mover publishes at its tick tail, so it has
	// to tick after the mover. `ACharacter` installs this prerequisite itself in
	// PostInitializeComponents — which covers every NPC — but `AElysiumPawn`
	// is a plain `APawn` whose visual is built at runtime, so a skeletal mesh sharing the mover's
	// tick group would otherwise be ordered by registration, i.e. not at all.
	if (UPawnMovementComponent* Move = Pawn->GetMovementComponent())
	{
		Visual->AddTickPrerequisiteComponent(Move);
	}

	// Built hidden, and shown by the first draw policy the camera publishes. The alternative — show
	// it here and let the camera put it away a frame later — is a one-frame flash of the bind pose
	// every time a body is built or a model swapped.
	Visual->SetVisibility(true);
	Visual->SetComponentTickEnabled(true);
	Visual->SetHiddenInGame(true);
	Body->SetPlayerVisual(Visual);
	// The stem the animation driver resolves its catalog from. Kept here rather than re-derived from
	// the entity record each frame, because this is the one place that knows which model was built.
	PlayerVisualStem = Stem;
	if (PlayerAnimDriver.IsValid())
	{
		PlayerAnimDriver->Reset();
	}
	return Visual;
}

void AElysiumMapActor::TickPlayerAnimation(float DeltaSeconds)
{
	APawn* Pawn = ResolvePlayerPawn();
	IElysiumPlayerBody* Body = Pawn ? Cast<IElysiumPlayerBody>(Pawn) : nullptr;
	if (Body == nullptr)
	{
		// A backdrop map seats no pawn. The driver simply does not advance; its record keeps saying
		// what it last said rather than reporting a body that is not there.
		return;
	}

	if (!PlayerAnimDriver.IsValid())
	{
		PlayerAnimDriver = MakePimpl<FElysiumAnimationDriver>();
		PlayerAnimDriver->Source = EElysiumAnimSource::Player;
		PlayerAnimDriver->BodyKind = EElysiumAnimBodyKind::Player;
	}

	PlayerAnimDriver->Stem = PlayerVisualStem;
	if (EntityWorld)
	{
		PlayerAnimDriver->Character = EntityWorld->PlayerHandle();
		// The variant is the body's own index, so the same character resolves the same idle every load
		// — repeatability is what makes a weighted pick assertable at all.
		PlayerAnimDriver->Variant = FMath::Max(0, PlayerAnimDriver->Character.Index);
		// What the drawn weapon does to the gait. Refreshed here rather than on the equip, because
		// this is the pass that already reads the character and the driver keys its own re-resolve on
		// the value changing — a swap therefore costs one frame of latency and no per-equip wiring.
		PlayerAnimDriver->SetTranslationContext(EntityWorld->FindPlayer());
	}

	USkeletalMeshComponent* Visual = Body->GetPlayerVisual();
	UElysiumAnimSubsystem* Anims = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
	UElysiumBipedAnimInstance* Graph = Visual
		? Cast<UElysiumBipedAnimInstance>(Visual->GetAnimInstance()) : nullptr;

	// **What the graph said about the clip the latch is about to advance past.** Read BEFORE the
	// tick, because the report describes the request that was published last frame and the latch is
	// about to decide whether that request is over.
	//
	// The generation gate is the whole safety of it: a report stamped with a generation the driver
	// has already moved past describes a clip that is no longer playing, and letting it answer would
	// end the request that replaced it. A stale or absent report reads `Unknown`, which is exactly
	// the timer fallback a body with no graph gets.
	// The one-shot answer is `ElysiumAnimGraph::OneShotStateFor`'s, so the two collapses it exists to
	// prevent — a missing clip read as unanswerable, and a stale report ending the request that
	// replaced it — are asserted in the Substrate tier rather than living here.
	static const FElysiumOneShotReport NoReport;
	const FElysiumOneShotReport& Report = Graph ? Graph->GetOneShotReport() : NoReport;
	const EElysiumOneShotState OneShot = ElysiumAnimGraph::OneShotStateFor(
		/*bHasAsset*/ PlayerAnimDriver->Assets.Sequence != nullptr
			|| PlayerAnimDriver->Assets.Space != nullptr,
		/*bGenerationMatches*/ Graph != nullptr
			&& Report.Generation == PlayerAnimDriver->Selection.Generation,
		Report.bInOneShotState, Report.bComplete);

	// **Where the forced sequence stands, read before the driver ticks.** The driver's
	// animation-driven predicate is rebuilt from this plus the base claim's own forced activity every
	// frame, so it is pushed here rather than remembered anywhere — and a body with no pose layer
	// pushes the default, which reports no sequence and is never animation-driven.
	PlayerAnimDriver->BaseClipCycle = ReadPlayerBaseClipCycle(Visual);

	// **The melee stop, between the pose read and the selector.** Retail zeroes the body's
	// velocity in `PostThink` and then, in the very next instruction block, asks the classifier and
	// calls `SetAnimation` — so the frame the swing's lock releases is a frame the selector sees a
	// STANDING body on. Here that is the same seam: the freshly-read cycle above is what the rule's
	// predicate is rebuilt over, and the sample handed to `Tick` below is read after the stop has
	// had its chance at it.
	//
	// The rule itself is the substrate's (`FElysiumEntityWorld::UpdatePlayerMeleeMovementStop`); all
	// this order decides is when it is asked.
	// The driver's own rebuild, not its published copy: the published pair still describes the frame
	// before, and this rule needs where the forced sequence stands RIGHT NOW.
	if (EntityWorld)
	{
		EntityWorld->UpdatePlayerMeleeMovementStop(PlayerAnimDriver->ForcedIdealActivity());
	}

	PlayerAnimDriver->Tick(DeltaSeconds, Body->GetLocomotionSample(), Anims,
		Visual ? Visual->GetSkeletalMeshAsset() : nullptr, OneShot);

	// The movement half of the same mechanism, handed to the mover for the frame after this one —
	// the same one-frame push the gait tables below take, and the same lag retail's own `SetupMove`
	// reads the cycle with.
	PushPlayerAnimMovementLock(Pawn);

	// **The speed authority's push.** The mover runs in the pre-physics pass and this driver
	// in the post-move one, so the mover cannot ask for a table — it has to be handed one, and the
	// tables are a property of the body rather than of the frame, so handing one over on change is
	// the whole of it. The generation gate is what keeps it from being a per-frame struct copy.
	//
	// The driver has already rebuilt its own gait reference off these tables, so the classifier's
	// walk/run threshold and the mover's commanded speed cannot come from two different numbers.
	if (PlayerAnimDriver->GaitGeneration != PushedGaitGeneration)
	{
		PushedGaitGeneration = PlayerAnimDriver->GaitGeneration;
		if (UElysiumMovementComponent* Move = Pawn->FindComponentByClass<UElysiumMovementComponent>())
		{
			Move->SetGaitSpeeds(PlayerAnimDriver->GaitSpeeds);
		}
	}
	// Hand the settled record to the graph. The push is here rather than a pull from the
	// instance because the driver lives on this actor behind a pimpl while the visual is a component
	// of the pawn: an instance reaching for it would invert the layering and carry a null branch for
	// every map that seats no pawn. A cast body, or a player body whose graph package is missing,
	// simply is not a biped instance and is skipped.
	if (Graph)
	{
		Graph->PublishSelection(PlayerAnimDriver->Selection, PlayerAnimDriver->Assets);
	}
}

FElysiumBaseClipCycle AElysiumMapActor::ReadPlayerBaseClipCycle(
	USkeletalMeshComponent* Visual) const
{
	FElysiumBaseClipCycle Cycle;
	if (!PlayerAnimDriver.IsValid() || Visual == nullptr || Bodies == nullptr)
	{
		return Cycle;   // no body and no pose layer: nothing is standing on the base channel
	}
	const FElysiumAnimationRequest* Claim =
		PlayerAnimDriver->ActiveRequest(EElysiumAnimChannel::Base);
	if (Claim == nullptr || Claim->Activity.IsEmpty())
	{
		// No forced activity on the channel. There is a clip there on plenty of frames — an ambient
		// stance, a scripted beat — but none of them is retail's forced sequence, and timing a lock
		// off one would answer for a body that is not acting.
		return Cycle;
	}

	FElysiumClipPhase Phase;
	if (!Bodies->GetBodyClipPhase(Visual, EElysiumAnimChannel::Base, Phase) || !Phase.IsValid())
	{
		return Cycle;
	}
	if (!Phase.Label.Equals(Claim->Label, ESearchCase::IgnoreCase))
	{
		// The channel is standing on a different clip than the claim named — the play was refused,
		// replaced, or the pose layer has not reached it yet. `m_nSequence` is not the forced one, so
		// the predicate's own sequence guard fails and the lock is simply not on this frame.
		return Cycle;
	}

	Cycle.bPlaying = true;
	Cycle.Cycle = Phase.Cycle;
	Cycle.LengthSeconds = Phase.Length;
	// `m_flPlaybackRate`, as the pose layer is actually running it — the other half of retail's
	// `GetSequenceCycleRate(seq) * m_flPlaybackRate`.
	Cycle.PlayRate = Phase.PlayRate;

	// **`w_hold` is the release cycle, and it is the playing sequence's own.** It is exported beside
	// `w_open`/`w_close` on the clip manifest's `combo` column; a sequence that authors no combo
	// block authors no `w_hold` either and takes 1.0, which is why a heavy finisher locks for its
	// whole clip.
	UElysiumAnimSubsystem* Anims = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
	const FElysiumNpcClipSet* Set = Anims != nullptr ? Anims->GetClipSet(PlayerVisualStem) : nullptr;
	if (const FElysiumNpcClip* Clip = Set != nullptr ? Set->Find(Phase.Label) : nullptr)
	{
		Cycle.HoldCycle = Clip->HasCombo() ? Clip->Combo.HoldCycle : 1.0f;
		// The frame count the cycle is mapped onto for the movement sample — the clip's own, because
		// the authored `mstudiomovement_t` records index frames rather than seconds.
		Cycle.FrameCount = Clip->Frames;
	}
	return Cycle;
}

void AElysiumMapActor::ClearPlayerAnimMovementLock(APawn* Pawn)
{
	if (PushedLockClip.IsEmpty())
	{
		// Nothing was ever handed over, so there is nothing to take back — and the component lookup
		// below is skipped on every ordinary frame rather than run to clear a zero.
		return;
	}
	PushedLockClip.Reset();
	PushedLockPath.Reset();
	if (UElysiumMovementComponent* Move = Pawn != nullptr
		? Pawn->FindComponentByClass<UElysiumMovementComponent>() : nullptr)
	{
		Move->ClearAnimMovementLock();
	}
}

void AElysiumMapActor::PushPlayerAnimMovementLock(APawn* Pawn)
{
	// `vt+0x674` is the arm that discards the movement command, so it is the one that decides whether
	// a lock is handed over at all.
	if (!PlayerAnimDriver.IsValid() || !PlayerAnimDriver->bMovementLocked)
	{
		ClearPlayerAnimMovementLock(Pawn);
		return;
	}

	UElysiumMovementComponent* Move = Pawn != nullptr
		? Pawn->FindComponentByClass<UElysiumMovementComponent>() : nullptr;
	if (Move == nullptr)
	{
		return;   // a pawn with no `CGameMovement` port (a backdrop map, or a capsule body) has nothing to lock
	}

	const FElysiumBaseClipCycle& Cycle = PlayerAnimDriver->BaseClipCycle;
	const FElysiumAnimationRequest* Claim =
		PlayerAnimDriver->ActiveRequest(EElysiumAnimChannel::Base);
	FElysiumAnimMovementLock Lock;
	Lock.bActive = true;
	// The narrower arm travels beside it: `vt+0x670` refuses the jump press, `vt+0x674` discards the
	// movement command, and the difference is exactly `ACT_LAND_HARD`. Every row this rung implements
	// is a melee one, so the two are equal on every frame today.
	Lock.bRefusesJump = PlayerAnimDriver->bAnimationDriven;
	Lock.Cycle = Cycle.Cycle;
	// Retail's `GetSequenceCycleRate(seq) * m_flPlaybackRate`, in cycles per second, and BOTH factors
	// are the clip the pose layer is actually running. The cycle rate is the reciprocal of the
	// authored length — reading the played length rather than the model's own fps/frame count is what
	// keeps the window the mover samples aligned with the cycle it was handed — and the playback rate
	// is the one the swing wrote onto the play, so the drawn clip, the cycle and the lunge advance
	// together instead of at three speeds.
	Lock.CycleRate = Cycle.LengthSeconds > 0.0f ? Cycle.PlayRate / Cycle.LengthSeconds : 0.0f;
	Lock.FrameCount = Cycle.FrameCount;

	// The clip's authored displacement path, resolved once per clip and shared by pointer after.
	const FString OwnerStem = Bodies != nullptr && Claim != nullptr
		? Bodies->NpcClipOwner(PlayerVisualStem, Claim->Label) : FString();
	const FString Key = FString::Printf(TEXT("%s|%s"), *OwnerStem,
		Claim != nullptr ? *Claim->Label : TEXT(""));
	if (Key != PushedLockClip)
	{
		PushedLockClip = Key;
		PushedLockPath.Reset();
		UElysiumAnimSubsystem* Anims = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
		const TSharedPtr<const FElysiumBlendTable> Table = Anims != nullptr && !OwnerStem.IsEmpty()
			? Anims->GetBlendTable(OwnerStem) : nullptr;
		if (Table.IsValid() && !Table->bMovementStated)
		{
			// **The gap, and it is not the same thing as a clip authoring no movement.** Either way
			// the sidecar says nothing about any of its clips, so reporting it as "no records" would
			// turn a broken input into a silent behaviour change. The two causes have opposite
			// remedies, so each is named with its own: a file that carries no `movement_fields`
			// predates the column and a re-export fixes it, while a file whose schema this build
			// cannot address is a READER that is behind, and re-exporting the same columns changes
			// nothing.
			if (!ReportedMovementGaps.Contains(OwnerStem))
			{
				ReportedMovementGaps.Add(OwnerStem);
				if (Table->bMovementSchemaUnreadable)
				{
					UE_LOG(LogElysium, Warning,
						TEXT("bank '%s' states a `movement_fields` schema this build cannot address, "
							 "so no clip of it can say whether it authors a lunge — the reader is "
							 "behind the file's column set and a re-export will not change it; the "
							 "swing holds the body in place meanwhile"), *OwnerStem);
				}
				else
				{
					UE_LOG(LogElysium, Warning,
						TEXT("bank '%s' carries no `movement_fields` at all, so no clip of it can say "
							 "whether it authors a lunge — re-export this model's blend sidecar; the "
							 "swing holds the body in place meanwhile"), *OwnerStem);
				}
			}
		}
		else if (Table.IsValid() && Claim != nullptr)
		{
			if (const FElysiumClipMovementPath* Path = Table->FindMovement(Claim->Label))
			{
				PushedLockPath = MakeShared<FElysiumClipMovementPath>(*Path);

				// **The frame count and the records come from two different files** — the count off
				// the clip's own play length, the records off the blend sidecar — so they can
				// disagree, and the disagreement is invisible in the frame: the cycle maps onto the
				// count, so a count short of the records simply never samples the tail of the lunge
				// while the clip plays to its end. Once per clip, at the one place both are in scope.
				const int32 Authored = Path->LastFrame();
				if (Authored > 0 && Lock.FrameCount > 0 && Lock.FrameCount - 1 != Authored)
				{
					UE_LOG(LogElysium, Warning,
						TEXT("clip '%s'@'%s' authors movement out to frame %d while the clip states %d "
							 "frames; the cycle maps onto the clip, so %s"),
						*Claim->Label, *OwnerStem, Authored, Lock.FrameCount,
						Lock.FrameCount - 1 < Authored
							? TEXT("the tail of the authored motion is never reached")
							: TEXT("the last records are held past the end of the path"));
				}
			}
			// A stated file with no row for this label is the authored absence: the clip really does
			// author no movement, and the null path is that value rather than a miss.
		}
	}

	// A clip whose length could not be read answers a zero rate, and a zero rate makes every substep
	// a zero-width window: `SampleDelta` succeeds with no delta, `WalkMove` assigns zero and the body
	// stops dead for the whole swing. That is indistinguishable in the frame from a clip that
	// deliberately authors no motion, so it is named here where the cause is still known.
	if (PushedLockPath.IsValid() && !(Lock.CycleRate > 0.0f)
		&& !ReportedMovementGaps.Contains(PushedLockClip))
	{
		ReportedMovementGaps.Add(PushedLockClip);
		UE_LOG(LogElysium, Warning,
			TEXT("clip '%s'@'%s' authors a movement path but its played length reads %.3fs, so the "
				 "cycle cannot advance and the swing holds the body in place"),
			Claim != nullptr ? *Claim->Label : TEXT(""), *OwnerStem, Cycle.LengthSeconds);
	}

	Lock.Path = PushedLockPath;
	Move->SetAnimMovementLock(Lock);
}

const FElysiumAnimationSelection& AElysiumMapActor::GetPlayerAnimSelection() const
{
	// A shared empty record rather than a null pointer: every reader — the recorder, Cog, the MCP
	// surface — wants a row, and a default record's outcome already says there is no vocabulary.
	static const FElysiumAnimationSelection Empty;
	return PlayerAnimDriver.IsValid() ? PlayerAnimDriver->Selection : Empty;
}

const FElysiumLocomotionSample& AElysiumMapActor::GetPlayerAnimSample() const
{
	// Same shape and same reason as the record above: a shared empty sample rather than a pointer,
	// and the two are always read together.
	static const FElysiumLocomotionSample Empty;
	return PlayerAnimDriver.IsValid() ? PlayerAnimDriver->Sample : Empty;
}

uint32 AElysiumMapActor::SubmitPlayerAnimRequest(const FElysiumAnimationRequest& Request)
{
	if (!PlayerAnimDriver.IsValid())
	{
		// The driver is normally built by the first player anim pass; a claim arriving ahead of it
		// — a scene that opens on the load frame — builds the same driver rather than being dropped.
		PlayerAnimDriver = MakePimpl<FElysiumAnimationDriver>();
		PlayerAnimDriver->Source = EElysiumAnimSource::Player;
		PlayerAnimDriver->BodyKind = EElysiumAnimBodyKind::Player;
	}
	return PlayerAnimDriver->SubmitRequest(Request);
}

bool AElysiumMapActor::ReleasePlayerAnimRequest(uint32 Handle)
{
	return PlayerAnimDriver.IsValid() && PlayerAnimDriver->ReleaseRequest(Handle);
}

int32 AElysiumMapActor::PlayerOverlaySlotForHandle(uint32 Handle) const
{
	return PlayerAnimDriver.IsValid() ? PlayerAnimDriver->Overlay.FindByHandle(Handle) : INDEX_NONE;
}

int32 AElysiumMapActor::ReleaseAllPlayerAnimRequests()
{
	// Deliberately NOT built on demand, unlike the submit above: releasing nothing needs no driver.
	if (!PlayerAnimDriver.IsValid())
	{
		return 0;
	}
	bool bDroppedSlotLayer = false;
	const int32 Released = PlayerAnimDriver->ReleaseAllRequests(&bDroppedSlotLayer);
	// **The claim going back is not what ends a layer's pose; the next publish is.** A body released
	// wholesale is a body whose producers have stopped, so that publish may never come — and the
	// overlay slot would keep composing at whatever weight and phase it was dropped on. The driver
	// cannot reach an anim instance by design, so this is where the pose is taken down.
	if (bDroppedSlotLayer)
	{
		APawn* Pawn = ResolvePlayerPawn();
		IElysiumPlayerBody* PlayerBody = Pawn ? Cast<IElysiumPlayerBody>(Pawn) : nullptr;
		UElysiumBipedAnimInstance::StopSlotLayerOn(
			PlayerBody != nullptr ? PlayerBody->GetPlayerVisual() : nullptr);
	}
	return Released;
}

const FElysiumAnimationRequest* AElysiumMapActor::ActivePlayerAnimRequest(
	EElysiumAnimChannel Channel) const
{
	// Not built on demand either, and for the same reason: a driver that does not exist has granted
	// nothing, so the honest answer is null rather than a fresh empty slot.
	return PlayerAnimDriver.IsValid() ? PlayerAnimDriver->ActiveRequest(Channel) : nullptr;
}

bool AElysiumMapActor::IsPlayerVisual(const USkeletalMeshComponent* Body) const
{
	if (Body == nullptr)
	{
		return false;
	}
	APawn* Pawn = ResolvePlayerPawn();
	IElysiumPlayerBody* PlayerBody = Pawn ? Cast<IElysiumPlayerBody>(Pawn) : nullptr;
	return PlayerBody != nullptr && PlayerBody->GetPlayerVisual() == Body;
}

void AElysiumMapActor::ClearPlayerVisual()
{
	APawn* Pawn = ResolvePlayerPawn();
	IElysiumPlayerBody* Body = Pawn ? Cast<IElysiumPlayerBody>(Pawn) : nullptr;
	if (!Body)
	{
		return;
	}
	if (USkeletalMeshComponent* Visual = Body->GetPlayerVisual())
	{
		// The wield mesh and its trail are owned by the PAWN and merely attached to this visual
		// (`ElysiumNpcVisual::SweepWieldModels`' ownership trap), so destroying the visual alone
		// detaches them into floating orphans nothing would ever sweep on this path.
		ElysiumNpcVisual::ClearWieldModel(Visual);
		ElysiumMeleeTrail::ClearTrail(Visual);
		Body->SetPlayerVisual(nullptr);
		Visual->DestroyComponent();
	}
	PlayerVisualStem.Reset();
	if (PlayerAnimDriver.IsValid())
	{
		PlayerAnimDriver->Reset();
	}
	// The body is gone, so its authored speeds go with it — a mover left holding them would steer the
	// next body by the last one's gait.
	PushedGaitGeneration = 0;
	if (UElysiumMovementComponent* Move = Pawn->FindComponentByClass<UElysiumMovementComponent>())
	{
		Move->ClearGaitSpeeds();
	}
}

void AElysiumMapActor::SetPlayerBodyEntityHidden(bool bInHidden)
{
	APawn* Pawn = ResolvePlayerPawn();
	if (IElysiumPlayerBody* Body = Pawn ? Cast<IElysiumPlayerBody>(Pawn) : nullptr)
	{
		// Forwarded, not applied. The pawn holds both gates and is the only writer of the flags, so a
		// dormancy change and a mode switch cannot land on the component in either order and disagree.
		Body->SetBodyEntityHidden(bInHidden);
	}
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

bool AElysiumMapActor::GetPlayerUseOrigin(FVector& OutLocation) const
{
	const APawn* Pawn = ResolvePlayerPawn();
	const IElysiumPlayerBody* Body = Cast<IElysiumPlayerBody>(Pawn);
	const UElysiumCameraComponent* Camera = Body ? Body->GetCameraComponent() : nullptr;
	if (!Camera)
	{
		return false;
	}
	OutLocation = Camera->GetComponentLocation();
	return true;
}

bool AElysiumMapActor::GetPlayerCapsuleTransform(FVector& OutCapsuleCenter, FRotator& OutViewRotation) const
{
	const APawn* Pawn = ResolvePlayerPawn();
	if (!Pawn)
	{
		return false;
	}
	OutCapsuleCenter = Pawn->GetActorLocation();
	const APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
	OutViewRotation = PC ? PC->GetControlRotation() : Pawn->GetActorRotation();
	return true;
}

bool AElysiumMapActor::GetPlayerFeetTransform(FVector& OutFeetOrigin, FRotator& OutViewRotation) const
{
	if (!GetPlayerCapsuleTransform(OutFeetOrigin, OutViewRotation))
	{
		return false;
	}
	if (const APawn* Pawn = ResolvePlayerPawn())
	{
		if (const IElysiumPlayerBody* Body = Cast<IElysiumPlayerBody>(Pawn))
		{
			OutFeetOrigin.Z -= Body->GetBodyHalfHeight();
		}
	}
	return true;
}

void AElysiumMapActor::TeleportPlayer(const FVector& FeetOrigin, const FRotator& ViewRotation)
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
	{
		TGuardValue<bool> SuppressIngress(bSuppressPlayerTouchIngress, true);
		bPlayerTouchReconcilePending = true;
		Pawn->SetActorLocationAndRotation(Dest, FRotator(0.0f, ViewRotation.Yaw, 0.0f),
			false, nullptr, ETeleportType::TeleportPhysics);
		if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
		{
			PC->SetControlRotation(ViewRotation);
		}
	}
}

void AElysiumMapActor::RouteBrushTouch(const FElysiumEntityHandle& Brush,
	const FElysiumEntityHandle& Activator, bool bBegin)
{
	if (bSuppressPlayerTouchIngress)
	{
		return;
	}
	if (EntityWorld)
	{
		EntityWorld->RouteBrushTouch(Brush, Activator, bBegin);
	}
}

void AElysiumMapActor::ReconcilePlayerBrushTouches(APawn* Pawn)
{
	if (!Pawn || !EntityWorld)
	{
		return;
	}

	// AActor::SetActorLocation returns early for a zero transform delta. That is observable on map
	// entry because the baked APlayerStart and the runtime .spawn placement name the same place:
	// the pawn can already be inside a newly registered trigger, with no movement edge left to wake
	// it. Dirty UE's overlap-skip cache and perform the query now that both player and entity world
	// are live.
	TGuardValue<bool> SuppressIngress(bSuppressPlayerTouchIngress, true);
	if (USceneComponent* Root = Pawn->GetRootComponent())
	{
		Root->ClearSkipUpdateOverlaps();
	}
	Pawn->UpdateOverlaps(/*bDoNotifies*/ true);

	TArray<FElysiumEntityHandle> CurrentBrushes;
	TInlineComponentArray<UElysiumBrushComponent*> BrushComponents(this);
	for (UElysiumBrushComponent* Brush : BrushComponents)
	{
		if (Brush && Brush->GetSolidity() == EElysiumBrushSolidity::Trigger
			&& Brush->IsOverlappingActor(Pawn))
		{
			CurrentBrushes.Add(Brush->GetOwningEntity());
		}
	}
	for (const FTouchAnchorRecord& Record : TouchAnchors)
	{
		UPrimitiveComponent* Component = Record.Component.Get();
		if (Record.bEnabled && Component && Component->IsOverlappingActor(Pawn))
		{
			CurrentBrushes.Add(Record.Owner);
		}
	}
	EntityWorld->ReconcilePlayerTouches(CurrentBrushes);
	bPlayerTouchReconcilePending = false;
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

void AElysiumMapActor::RegisterUseAnchor(UPrimitiveComponent* Source,
	const FElysiumEntityHandle& OwnerHandle)
{
	if (!Source || !OwnerHandle.IsSet())
	{
		return;
	}
	for (const FUseAnchorRecord& Record : UseAnchors)
	{
		if (Record.Component.Get() == Source && Record.Owner == OwnerHandle)
		{
			return;
		}
	}

	UPrimitiveComponent* Anchor = Source;
	if (!Source->IsA<UElysiumBrushComponent>())
	{
		// A visual prop remains non-solid. Give only its rendered bounds a query body: this is a
		// target surface, not a proximity/action volume, and it follows the source component through
		// elevator attachment and animation transforms.
		const FBoxSphereBounds LocalBounds = Source->CalcBounds(FTransform::Identity);
		UBoxComponent* Proxy = NewObject<UBoxComponent>(this);
		Proxy->SetCanEverAffectNavigation(false);
		Proxy->SetMobility(EComponentMobility::Movable);
		Proxy->InitBoxExtent(LocalBounds.BoxExtent.ComponentMax(FVector(2.0f)));
		Proxy->SetupAttachment(Source);
		Proxy->SetRelativeLocation(LocalBounds.Origin);
		Proxy->SetRelativeRotation(FRotator::ZeroRotator);
		Proxy->SetCollisionObjectType(ECC_WorldDynamic);
		Proxy->SetCollisionResponseToAllChannels(ECR_Ignore);
		Proxy->SetCollisionResponseToChannel(ELYSIUM_USE_CHANNEL, ECR_Block);
		Proxy->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Proxy->SetGenerateOverlapEvents(false);
		Proxy->RegisterComponent();
		AddInstanceComponent(Proxy);
		OwnedUseAnchorComponents.Add(Proxy);
		Anchor = Proxy;
	}

	FUseAnchorRecord& Record = UseAnchors.AddDefaulted_GetRef();
	Record.Component = Anchor;
	Record.Visual = Source;
	Record.Owner = OwnerHandle;
}

UPrimitiveComponent* AElysiumMapActor::FindUseVisual(
	const FElysiumEntityHandle& OwnerHandle) const
{
	const FUseAnchorRecord* Record = UseAnchors.FindByPredicate(
		[OwnerHandle](const FUseAnchorRecord& Candidate)
		{
			return Candidate.Owner == OwnerHandle && Candidate.bEnabled
				&& Candidate.Visual.IsValid();
		});
	return Record ? Record->Visual.Get() : nullptr;
}

void AElysiumMapActor::SetUseAnchorEnabled(const FElysiumEntityHandle& OwnerHandle, bool bEnabled)
{
	for (FUseAnchorRecord& Record : UseAnchors)
	{
		if (Record.Owner != OwnerHandle)
		{
			continue;
		}
		Record.bEnabled = bEnabled;
		UPrimitiveComponent* Component = Record.Component.Get();
		if (!Component)
		{
			continue;
		}
		if (OwnedUseAnchorComponents.Contains(Component))
		{
			Component->SetCollisionEnabled(
				bEnabled ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
			continue;
		}
		// Brush slabs keep pawn/world collision. A disabled use owner (a knobbed door) must not
		// remain on ElysiumUse or the slab eats the exact ray and occludes its own knobs.
		Component->SetCollisionResponseToChannel(
			ELYSIUM_USE_CHANNEL, bEnabled ? ECR_Block : ECR_Ignore);
	}
}

void AElysiumMapActor::ClearUseAnchors()
{
	UseAnchors.Reset();
	for (UPrimitiveComponent* Component : OwnedUseAnchorComponents)
	{
		if (Component)
		{
			Component->DestroyComponent();
		}
	}
	OwnedUseAnchorComponents.Reset();
}

void AElysiumMapActor::RegisterTouchAnchor(UPrimitiveComponent* Source,
	const FElysiumEntityHandle& OwnerHandle)
{
	if (!Source || !OwnerHandle.IsSet())
	{
		return;
	}
	const FBoxSphereBounds LocalBounds = Source->CalcBounds(FTransform::Identity);
	UBoxComponent* Proxy = NewObject<UBoxComponent>(this);
	Proxy->SetCanEverAffectNavigation(false);
	Proxy->SetMobility(EComponentMobility::Movable);
	Proxy->InitBoxExtent(LocalBounds.BoxExtent.ComponentMax(FVector(4.0f)));
	Proxy->SetupAttachment(Source);
	Proxy->SetRelativeLocation(LocalBounds.Origin);
	Proxy->SetRelativeRotation(FRotator::ZeroRotator);
	Proxy->SetCollisionObjectType(ECC_WorldDynamic);
	Proxy->SetCollisionResponseToAllChannels(ECR_Ignore);
	Proxy->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Proxy->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Proxy->SetGenerateOverlapEvents(true);
	Proxy->OnComponentBeginOverlap.AddDynamic(this, &AElysiumMapActor::HandleTouchAnchorBegin);
	Proxy->RegisterComponent();
	AddInstanceComponent(Proxy);
	OwnedTouchAnchorComponents.Add(Proxy);

	FTouchAnchorRecord& Record = TouchAnchors.AddDefaulted_GetRef();
	Record.Component = Proxy;
	Record.Owner = OwnerHandle;
}

void AElysiumMapActor::SetTouchAnchorEnabled(const FElysiumEntityHandle& OwnerHandle, bool bEnabled)
{
	for (FTouchAnchorRecord& Record : TouchAnchors)
	{
		if (Record.Owner != OwnerHandle)
		{
			continue;
		}
		Record.bEnabled = bEnabled;
		if (UPrimitiveComponent* Component = Record.Component.Get())
		{
			Component->SetCollisionEnabled(
				bEnabled ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
		}
	}
}

void AElysiumMapActor::ClearTouchAnchors()
{
	TouchAnchors.Reset();
	for (UPrimitiveComponent* Component : OwnedTouchAnchorComponents)
	{
		if (Component)
		{
			Component->DestroyComponent();
		}
	}
	OwnedTouchAnchorComponents.Reset();
}

void AElysiumMapActor::HandleTouchAnchorBegin(UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	if (!EntityWorld || OtherActor != ResolvePlayerPawn())
	{
		return;
	}
	const FTouchAnchorRecord* Record = TouchAnchors.FindByPredicate(
		[OverlappedComponent](const FTouchAnchorRecord& Candidate)
		{
			return Candidate.bEnabled && Candidate.Component.Get() == OverlappedComponent;
		});
	if (Record)
	{
		EntityWorld->RouteEntityTouch(
			Record->Owner, EntityWorld->PlayerHandle(), /*bBegin*/ true);
	}
}

FElysiumUseQueryResult AElysiumMapActor::QueryPlayerUse(
	const FElysiumEntityHandle& /*CurrentFocus*/) const
{
	// vampire.dll 0x10167470 → FindEntityFOV 0x10341c30. Eye + look (the boom is view-only).
	// 80u look-ray wins if the hit is a use anchor; else an 80u sphere ranked by view-dot with
	// AABB-closest-to-eye rescue against cos(player_use_arc=30°); else a 160u fallback ray.
	// Solvers are Unreal's: LineTraceSingleByChannel, OverlapMultiByObjectType, GetClosestPointTo.
	constexpr float UseReachCm = 80.0f * ElysiumMove::U;
	constexpr float FallbackReachCm = 160.0f * ElysiumMove::U;
	constexpr float MinDot = 0.86602540378f; // cos(30°)

	FElysiumUseQueryResult Result;
	UWorld* World = GetWorld();
	const APawn* Pawn = ResolvePlayerPawn();
	FVector Eye;
	if (!World || !Pawn || !GetPlayerUseOrigin(Eye))
	{
		return Result;
	}
	const FVector Forward = Pawn->GetViewRotation().Vector().GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		return Result;
	}

	FCollisionQueryParams Params(FName(TEXT("ElysiumPlayerUse")), /*bTraceComplex*/ false);
	Params.AddIgnoredActor(Pawn);

	auto FindAnchor = [this](const UPrimitiveComponent* Component) -> const FUseAnchorRecord*
	{
		return UseAnchors.FindByPredicate([Component](const FUseAnchorRecord& Record)
		{
			return Record.bEnabled && Record.Component.Get() == Component;
		});
	};

	auto MakeCandidate = [](const FUseAnchorRecord& Record, const FVector& Point,
		EElysiumUseSelection Selection, float EyeDistance, float Dot) -> FElysiumUseCandidate
	{
		FElysiumUseCandidate Out;
		Out.Owner = Record.Owner;
		Out.AnchorPoint = Point;
		Out.Selection = Selection;
		Out.BodyDistance = EyeDistance;
		Out.CameraDistance = EyeDistance;
		Out.CameraDepth = EyeDistance;
		Out.AimError = 1.0f - Dot;
		return Out;
	};

	auto TraceUse = [World, &Eye, &Forward, &Params](float Length, FHitResult& Hit) -> bool
	{
		return World->LineTraceSingleByChannel(
			Hit, Eye, Eye + Forward * Length, ELYSIUM_USE_CHANNEL, Params);
	};

	auto VisibleTo = [World, &Eye, &Params, &FindAnchor](const FVector& Point,
		const FElysiumEntityHandle& Target) -> bool
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Eye, Point, ELYSIUM_USE_CHANNEL, Params))
		{
			return true;
		}
		const FUseAnchorRecord* Blocking = FindAnchor(Hit.GetComponent());
		return Blocking && Blocking->Owner == Target;
	};

	FHitResult RayHit;
	if (TraceUse(UseReachCm, RayHit))
	{
		if (const FUseAnchorRecord* Anchor = FindAnchor(RayHit.GetComponent()))
		{
			Result.Candidates.Add(MakeCandidate(*Anchor, RayHit.ImpactPoint,
				EElysiumUseSelection::Exact, FVector::Distance(Eye, RayHit.ImpactPoint), 1.0f));
			return Result;
		}
	}

	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, Eye, FQuat::Identity, Objects,
		FCollisionShape::MakeSphere(UseReachCm), Params);

	const FUseAnchorRecord* BestAnchor = nullptr;
	FVector BestPoint = FVector::ZeroVector;
	float BestDot = MinDot;
	bool bSawOccluded = false;
	TSet<FElysiumEntityHandle> Seen;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		const FUseAnchorRecord* Anchor = FindAnchor(Overlap.GetComponent());
		if (!Anchor || Seen.Contains(Anchor->Owner))
		{
			continue;
		}
		Seen.Add(Anchor->Owner);
		const UPrimitiveComponent* Component = Anchor->Component.Get();
		if (!Component)
		{
			continue;
		}
		const FBox Bounds = Component->Bounds.GetBox();
		const FVector Closest = Bounds.GetClosestPointTo(Eye);
		if (FVector::DistSquared(Eye, Closest) > FMath::Square(UseReachCm))
		{
			continue;
		}

		auto Consider = [&](const FVector& Point)
		{
			const FVector Delta = Point - Eye;
			const float Dist = Delta.Size();
			if (Dist <= KINDA_SMALL_NUMBER)
			{
				return;
			}
			const float Dot = FVector::DotProduct(Delta / Dist, Forward);
			if (Dot <= 0.0f || Dot <= BestDot)
			{
				return;
			}
			if (!VisibleTo(Point, Anchor->Owner))
			{
				bSawOccluded = true;
				return;
			}
			BestDot = Dot;
			BestAnchor = Anchor;
			BestPoint = Point;
		};

		Consider(Bounds.GetCenter());
		Consider(Closest);
	}

	if (BestAnchor)
	{
		Result.Candidates.Add(MakeCandidate(*BestAnchor, BestPoint,
			EElysiumUseSelection::Assisted, FVector::Distance(Eye, BestPoint), BestDot));
		return Result;
	}

	if (TraceUse(FallbackReachCm, RayHit))
	{
		if (const FUseAnchorRecord* Anchor = FindAnchor(RayHit.GetComponent()))
		{
			Result.Candidates.Add(MakeCandidate(*Anchor, RayHit.ImpactPoint,
				EElysiumUseSelection::Exact, FVector::Distance(Eye, RayHit.ImpactPoint), 1.0f));
			return Result;
		}
	}

	if (bSawOccluded)
	{
		Result.MissOutcome = EElysiumUseOutcome::Occluded;
	}
	return Result;
}

FElysiumEntityHandle AElysiumMapActor::QueryFeedTarget() const
{
	// `CBasePlayer::Replenish`'s direct victim search, reproduced in shape
	// (`docs/vtmb/feeding.md` § "Target acquisition and acceptance"): a hull trace from the body-eye
	// use origin toward the control-relative offset (32 forward, 0 right, -32 vertical) with extents
	// (-8,-8,-8)..(8,8,8). The retail figures are Source units and are converted once, here, by the
	// same `ElysiumMove::U` every other recovered distance in this runtime goes through.
	//
	// The recovered mask is `0x0201400b`. Source content masks are not portable to Unreal's channel
	// set, so the semantics are adapted rather than the number: candidacy is restricted to live
	// characters (the mask's player/NPC bits), and occlusion is tested on `ELYSIUM_USE_CHANNEL`
	// (its solid-world bits) — the same channel `+use` reaches the world through, and the one the
	// map's brush bodies and the walkable surface already answer on.

	FVector UseOrigin;
	FVector IgnoredCapsuleCenter;
	FRotator ControlRotation;
	if (!EntityWorld || !GetPlayerUseOrigin(UseOrigin)
		|| !GetPlayerCapsuleTransform(IgnoredCapsuleCenter, ControlRotation))
	{
		return FElysiumEntityHandle::Invalid();
	}
	const ElysiumFeedTargeting::FProbe Probe =
		ElysiumFeedTargeting::MakeProbe(UseOrigin, ControlRotation);

	UWorld* World = GetWorld();
	FCollisionQueryParams Params(FName(TEXT("ElysiumFeedTarget")), /*bTraceComplex*/ false);
	Params.AddIgnoredActor(ResolvePlayerPawn());

	const FElysiumEntityHandle PlayerHandle = EntityWorld->PlayerHandle();
	FElysiumEntityHandle Best = FElysiumEntityHandle::Invalid();
	double BestDistanceSq = TNumericLimits<double>::Max();

	for (const TUniquePtr<FElysiumEntity>& EntPtr : EntityWorld->Entities())
	{
		FElysiumEntity* Ent = EntPtr.Get();
		if (!Ent || Ent->IsInert() || Ent->Handle == PlayerHandle || !Ent->AsCombatCharacter())
		{
			continue;
		}
		FBox Candidate(ForceInit);
		const USkeletalMeshComponent* CandidateBody = Ent->GetSkeletalBody();
		if (CandidateBody)
		{
			Candidate = CandidateBody->Bounds.GetBox();
		}
		else
		{
			Candidate = ElysiumStandHullAt(Ent->Origin);
		}
		// Sweeping a box along a segment against an AABB is exactly a segment test against the AABB
		// grown by the hull's extents, so the recovered 16-cube is applied without a physics query.
		const FBox Swept = Candidate.ExpandBy(Probe.HullExtent);
		if (!FMath::LineBoxIntersection(Swept, Probe.Start, Probe.End, Probe.End - Probe.Start))
		{
			continue;
		}
		const FVector Chest = Candidate.GetCenter();
		if (World)
		{
			FHitResult Blocked;
			if (World->LineTraceSingleByChannel(Blocked, Probe.Start, Chest,
				ELYSIUM_USE_CHANNEL, Params)
				&& !ElysiumFeedTargeting::HitBelongsToCandidate(
					Blocked.GetComponent(), CandidateBody))
			{
				continue;   // a wall between the mouth and the neck
			}
		}
		const double DistanceSq = FVector::DistSquared(Probe.Start, Chest);
		if (DistanceSq < BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			Best = Ent->Handle;
		}
	}
	return Best;
}

FElysiumEntityHandle AElysiumMapActor::QueryAimTarget(float MaxRangeCm) const
{
	// Eye + look, the same origin/direction pair `QueryPlayerUse` reaches the world with above: the
	// boom is view-only, so the rendered third-person camera is behind the pawn and would answer for
	// a ray that starts in a different room. `GetPlayerUseOrigin()` is the body's own eye and
	// `GetViewRotation()` is the aim, and a shot leaves along exactly that.
	//
	// A straight ray, which is the zero-spread case of retail's fire packet — the cone's
	// interpolation input is unrecovered (RE-A3), so what is written here is the one member of the
	// cone family that needs no unrecovered value. The `Ammo_Fired` ray count is deliberately NOT
	// walked: one handle is the shape the attack transaction's victim already has, and a per-victim
	// pellet grouping is a change to that transaction rather than to this query.
	//
	// The standing hull a bodiless candidate is measured by is `QueryFeedTarget`'s own: VtMB's
	// 32x32x72-unit character box, reachable with `elysium.NpcBodies 0` or a failed model, where a
	// rendered bound does not exist.

	FVector Eye;
	const APawn* Pawn = ResolvePlayerPawn();
	if (!EntityWorld || !Pawn || !GetPlayerUseOrigin(Eye))
	{
		return FElysiumEntityHandle::Invalid();
	}
	const FVector Forward = Pawn->GetViewRotation().Vector().GetSafeNormal();
	if (Forward.IsNearlyZero() || MaxRangeCm <= 0.0f)
	{
		// A zero direction is a pawn with no view to aim along, and a non-positive range is a caller
		// that resolved no distance. Both are the caller's own reported cases, not this query's.
		return FElysiumEntityHandle::Invalid();
	}
	const FVector End = Eye + Forward * MaxRangeCm;

	UWorld* World = GetWorld();
	FCollisionQueryParams Params(FName(TEXT("ElysiumAimTarget")), /*bTraceComplex*/ false);
	Params.AddIgnoredActor(Pawn);

	const FElysiumEntityHandle PlayerHandle = EntityWorld->PlayerHandle();
	FElysiumEntityHandle Best = FElysiumEntityHandle::Invalid();
	double BestDistanceSq = TNumericLimits<double>::Max();

	for (const TUniquePtr<FElysiumEntity>& EntPtr : EntityWorld->Entities())
	{
		FElysiumEntity* Ent = EntPtr.Get();
		// A loot container re-registers on the combat-character base (it owns the same inventory), so
		// it is a combat character in this runtime without being a body anything can be shot at.
		const FElysiumCombatCharacter* AsChar = Ent ? Ent->AsCombatCharacter() : nullptr;
		if (!Ent || Ent->IsInert() || Ent->Handle == PlayerHandle || AsChar == nullptr
			|| Ent->AsItemContainer() != nullptr)
		{
			continue;
		}
		// A corpse is skipped HERE rather than left to the commit's own alive-path filter: it is
		// still a rendered body standing between the muzzle and a live one, and a query that returned
		// it would answer "nothing to shoot" for a shot that had a target behind it.
		if (AsChar->HasReportedDeath())
		{
			continue;
		}
		FBox Candidate(ForceInit);
		const USkeletalMeshComponent* CandidateBody = Ent->GetSkeletalBody();
		if (CandidateBody)
		{
			Candidate = CandidateBody->Bounds.GetBox();
		}
		else
		{
			Candidate = ElysiumStandHullAt(Ent->Origin);
		}
		if (!FMath::LineBoxIntersection(Candidate, Eye, End, End - Eye))
		{
			continue;
		}
		const FVector Chest = Candidate.GetCenter();
		if (World)
		{
			FHitResult Blocked;
			if (World->LineTraceSingleByChannel(Blocked, Eye, Chest, ELYSIUM_USE_CHANNEL, Params)
				&& !ElysiumFeedTargeting::HitBelongsToCandidate(
					Blocked.GetComponent(), CandidateBody))
			{
				continue;   // a wall between the muzzle and the body
			}
		}
		const double DistanceSq = FVector::DistSquared(Eye, Chest);
		if (DistanceSq < BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			Best = Ent->Handle;
		}
	}
	return Best;
}

void AElysiumMapActor::QuerySwingContacts(const FElysiumSwingSweep& Sweep,
	TArray<FElysiumEntityHandle>& OutHits) const
{
	// The standing hull a bodiless candidate is measured by is `QueryFeedTarget`/`QueryAimTarget`'s
	// own: VtMB's 32x32x72-unit character box, reachable with `elysium.NpcBodies 0` or a failed
	// model, where a rendered bound does not exist.

	OutHits.Reset();
	if (!EntityWorld.IsValid())
	{
		return;
	}

	// The swept segment's boundary: the segment where it started, where it ended, and the path each
	// of its two endpoints took between. The walk sub-steps at 100 Hz, so the patch these four edges
	// bound is thin enough that a candidate box inside it without touching an edge is not a case the
	// authored corpus produces — and testing the edges is what keeps this a segment query rather
	// than a bilinear-patch solver in a layer that owns no geometry.
	const FVector Edges[4][2] = {
		{ Sweep.PrevA, Sweep.PrevB },
		{ Sweep.CurA,  Sweep.CurB  },
		{ Sweep.PrevA, Sweep.CurA  },
		{ Sweep.PrevB, Sweep.CurB  },
	};

	UWorld* World = GetWorld();
	FCollisionQueryParams Params(FName(TEXT("ElysiumSwingContact")), /*bTraceComplex*/ false);
	// The SWINGER is what the occlusion trace must not stop on, and the swinger is whoever holds the
	// weapon — not the player, who is merely the source of the aim and feed queries beside this one.
	// The trace starts on the limb, which rides inside its owner's own hull for part of every swing,
	// and a `Pawn`-profile capsule blocks `ELYSIUM_USE_CHANNEL`: an attacker left in the query would
	// report itself as the wall between its fist and the body it just reached.
	if (const FElysiumEntity* AttackerEnt = EntityWorld->Resolve(Sweep.Attacker))
	{
		if (const USkeletalMeshComponent* AttackerBody = AttackerEnt->GetSkeletalBody())
		{
			if (const AActor* BodyActor = AttackerBody->GetOwner())
			{
				Params.AddIgnoredActor(BodyActor);
			}
		}
	}
	if (Sweep.Attacker == EntityWorld->PlayerHandle())
	{
		// The player's hull is the pawn's own component and stands whether or not a body was built,
		// so it is named directly rather than reached through one.
		if (const APawn* PlayerPawn = ResolvePlayerPawn())
		{
			Params.AddIgnoredActor(PlayerPawn);
		}
	}

	for (const TUniquePtr<FElysiumEntity>& EntPtr : EntityWorld->Entities())
	{
		FElysiumEntity* Ent = EntPtr.Get();
		// A loot container re-registers on the combat-character base (it owns the same inventory), so
		// it is a combat character in this runtime without being a body a swing can land on.
		const FElysiumCombatCharacter* AsChar = Ent ? Ent->AsCombatCharacter() : nullptr;
		if (!Ent || Ent->IsInert() || AsChar == nullptr || Ent->AsItemContainer() != nullptr
			|| Ent->Handle == Sweep.Attacker)
		{
			continue;
		}
		// A corpse is skipped here rather than left to the contact's own alive test, for the same
		// reason the aim query skips one: it is still a rendered body in the way, and a swing that
		// stopped on it would report a contact the commit then discards.
		if (AsChar->HasReportedDeath())
		{
			continue;
		}

		FBox Candidate(ForceInit);
		const USkeletalMeshComponent* CandidateBody = Ent->GetSkeletalBody();
		if (CandidateBody)
		{
			Candidate = CandidateBody->Bounds.GetBox();
		}
		else
		{
			Candidate = ElysiumStandHullAt(Ent->Origin);
		}

		bool bTouched = false;
		for (const FVector (&Edge)[2] : Edges)
		{
			if (FMath::LineBoxIntersection(Candidate, Edge[0], Edge[1], Edge[1] - Edge[0]))
			{
				bTouched = true;
				break;
			}
		}
		if (!bTouched)
		{
			continue;
		}

		// The engine's own answer to the one question the substrate cannot have: is there solid
		// world between the limb and the body it just reached through? Same channel and same
		// belongs-to test as the aim and feed queries, for the same reason — the walkable `.hulls`
		// collider is material-less and would answer on the visibility channel instead of the wall.
		const FVector LimbMid = (Sweep.CurA + Sweep.CurB) * 0.5;
		const FVector Chest = Candidate.GetCenter();
		if (World)
		{
			FHitResult Blocked;
			if (World->LineTraceSingleByChannel(Blocked, LimbMid, Chest, ELYSIUM_USE_CHANNEL, Params)
				&& !ElysiumFeedTargeting::HitBelongsToCandidate(
					Blocked.GetComponent(), CandidateBody))
			{
				continue;   // a wall between the swing and the body
			}
		}
		OutHits.Add(Ent->Handle);
	}
}

bool AElysiumMapActor::QueryLineOfSight(const FVector& FromCm, const FVector& ToCm) const
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		// No collision world to ask. The interface's stated headless answer is "clear", and this is
		// the same case reached through a live-but-unbuilt actor.
		return true;
	}
	// Degenerate segments are trivially clear, and a zero-length trace is not a query worth paying
	// for during a per-NPC sense pass.
	if (FromCm.Equals(ToCm))
	{
		return true;
	}
	// `ELYSIUM_USE_CHANNEL` is this project's solid-world channel: the walkable `.hulls` collider is
	// material-less and would be reported instead of the wall on the visibility channel, which is
	// exactly why `+use` has its own. Complex tracing is off — the brush bodies are convex hulls and
	// the query runs per NPC per sense pass.
	FCollisionQueryParams Params(FName(TEXT("ElysiumLineOfSight")), /*bTraceComplex*/ false);
	FHitResult Hit;
	return !World->LineTraceSingleByChannel(Hit, FromCm, ToCm, ELYSIUM_USE_CHANNEL, Params);
}

float AElysiumMapActor::QueryLightAtPoint(const FVector& PointCm) const
{
	// How many contributing sources are folded in before the answer is called good enough. A point
	// standing in more than this many overlapping authored radii is already saturated, so the cap
	// bounds the cost without changing the verdict. Nothing here allocates.
	constexpr int32 MaxContributors = 24;
	// The single-source intensity that reads as fully lit. `UElysiumLightRig::MaxBrightness` is the
	// calibrated ceiling one source is clipped to, so a point sitting at the centre of one
	// full-strength light is 1.0 and everything dimmer is a fraction of it.
	constexpr float MinReferenceIntensity = 0.01f;
	// `UElysiumLightRig`'s own source-type numbering: 3 is the sun/skylight directional.
	constexpr int32 SunSourceType = 3;

	const UElysiumMapVisuals* MapVisuals = GetVisuals();
	const UElysiumLightRig* Rig = MapVisuals ? MapVisuals->GetLightRig() : nullptr;
	if (Rig == nullptr)
	{
		return 1.0f;   // the stated headless answer: no rig, no darkness to claim
	}

	const float Reference = FMath::Max(Rig->MaxBrightness, MinReferenceIntensity);
	const TArray<UElysiumLightRig::FLightSource>& Sources = Rig->Sources();
	float Total = 0.0f;
	int32 Contributors = 0;
	for (int32 Index = 0; Index < Sources.Num() && Contributors < MaxContributors; ++Index)
	{
		const UElysiumLightRig::FLightSource& Source = Sources[Index];
		// A sky source lights the 3D-skybox miniature and never the playable world; the sun (type 3)
		// is a directional term with no position, and folding it in untraced would read every
		// interior as fully lit — the occlusion half of this query's stated divergence.
		//
		// A source a `UElysiumLightCalibration` row (or a stale in-session hand edit) switched off is
		// genuinely dark and is skipped. The MASTER visibility toggle deliberately is not consulted:
		// `elysium.lights 0` is a debug view and must not change what an NPC perceives.
		if (Source.bSky || Source.Type == SunSourceType || Rig->IsSourceDisabled(Index))
		{
			continue;
		}
		const ULightComponent* Light = Source.Light.Get();
		if (Light == nullptr)
		{
			continue;
		}
		const float Reach = Source.RadiusCm > 1.0f ? Source.RadiusCm : Rig->FallbackRadiusCm;
		const float Distance = static_cast<float>(
			FVector::Dist(Light->GetComponentLocation(), PointCm));
		if (Reach <= 0.0f || Distance >= Reach)
		{
			continue;
		}
		// The rig's own falloff shape, not inverse-square: VtMB's authored light is nearly flat
		// inside its radius and stops at it, which is what `FalloffExponent` (1.0 today) encodes.
		const float Attenuation = FMath::Pow(1.0f - (Distance / Reach),
			FMath::Max(Rig->FalloffExponent, UE_KINDA_SMALL_NUMBER));
		Total += Source.BaseIntensity * Attenuation;
		++Contributors;
	}
	return FMath::Clamp(Total / Reference, 0.0f, 1.0f);
}

bool AElysiumMapActor::IsPlayerSneaking() const
{
	// The body's own settled posture, read off the same locomotion record the animation graph is
	// steered by — so "sneaking" is one fact with one producer rather than a second definition
	// living in the substrate. `Lowering` counts with `Ducked`: the duck is engaged the moment the
	// ramp starts, which is what the movement solve's own gait tables already branch on.
	const APawn* Pawn = ResolvePlayerPawn();
	const IElysiumPlayerBody* Body = Pawn ? Cast<IElysiumPlayerBody>(Pawn) : nullptr;
	if (Body == nullptr)
	{
		return false;   // no body, no posture — the stated non-stealth answer
	}
	const EElysiumStance Stance = Body->GetLocomotionSample().Stance;
	return Stance == EElysiumStance::Ducked || Stance == EElysiumStance::Lowering;
}

bool AElysiumMapActor::IsPlayerOnGround() const
{
	// The same locomotion record `IsPlayerSneaking` above reads, for the same reason: ground contact
	// is the mover's own published fact, and the block predicate must not re-derive it from a trace.
	const APawn* Pawn = ResolvePlayerPawn();
	const IElysiumPlayerBody* Body = Pawn ? Cast<IElysiumPlayerBody>(Pawn) : nullptr;
	return Body != nullptr && Body->GetLocomotionSample().bOnGround;
}

FString AElysiumMapActor::GetPlayerBaseActivity() const
{
	// The driver's own published record, read through the same accessor Cog, the trace and the MCP
	// surface read — so "what the player is doing" is one answer with one producer.
	//
	// `RequestedActivity` and not `ResolvedActivity`: the fork is a question about the LOGICAL
	// request, and translation only changes which sequence set realizes it. Not `GraphState`
	// either — that projection collapses eight states over the whole vocabulary, so the landing
	// activities this fork must exclude are indistinguishable from the jump phases it must include.
	//
	// A driver that has never ticked publishes an empty request, which is the stated
	// no-body answer of the seam rather than a failure: there is no body to have left the floor.
	return GetPlayerAnimSelection().RequestedActivity;
}

void AElysiumMapActor::StopPlayerBody()
{
	APawn* Pawn = ResolvePlayerPawn();
	// Cached per pawn, the same reason the gait push gates on its generation counter: this runs
	// every frame of a melee tail window, and the component set on a pawn cannot change under it —
	// only the pawn itself can be replaced.
	if (Pawn != StopBodyPawn.Get() || !StopBodyMove.IsValid())
	{
		StopBodyPawn = Pawn;
		StopBodyMove = Pawn != nullptr
			? Pawn->FindComponentByClass<UElysiumMovementComponent>() : nullptr;
	}
	UElysiumMovementComponent* Move = StopBodyMove.Get();
	if (Move == nullptr)
	{
		// The substrate only asks after its own predicate held, which needs a pose layer standing on
		// the player's forced swing — so a body with no `CGameMovement` port at that point is a
		// mismatched pawn rather than the ordinary backdrop absence, and the swing's lunge is about
		// to survive into the gait ladder with nothing else to catch it.
		if (!bReportedNoStoppableBody)
		{
			bReportedNoStoppableBody = true;
			UE_LOG(LogElysium, Warning,
				TEXT("the melee movement lock stands released on '%s', but the player pawn carries no ")
				TEXT("UElysiumMovementComponent to stop — the swing's authored lunge stays on the ")
				TEXT("body and the gait ladder will read it as movement"),
				Pawn != nullptr ? *Pawn->GetName() : TEXT("(no pawn)"));
		}
		return;
	}
	Move->StopBody();
}

float AElysiumMapActor::ResolveNpcMakerGroundZ(const FVector& MakerOriginCm,
	float TraceDepthCm) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return MakerOriginCm.Z;
	}
	const FVector End = MakerOriginCm - FVector::UpVector * TraceDepthCm;
	FCollisionQueryParams Params(FName(TEXT("ElysiumNpcMakerGround")), /*bTraceComplex*/ false);
	Params.AddIgnoredActor(ResolvePlayerPawn());
	FHitResult Hit;
	return World->LineTraceSingleByChannel(Hit, MakerOriginCm, End, ELYSIUM_USE_CHANNEL, Params)
		? Hit.ImpactPoint.Z : End.Z;
}

bool AElysiumMapActor::IsNpcMakerVisibleFromPlayer(const FVector& MakerOriginCm) const
{
	FVector ViewLocation;
	FRotator ViewRotation;
	UWorld* World = GetWorld();
	if (!World || !GetPlayerViewPoint(ViewLocation, ViewRotation))
	{
		return false;
	}
	FCollisionQueryParams Params(FName(TEXT("ElysiumNpcMakerVisible")), /*bTraceComplex*/ false);
	Params.AddIgnoredActor(ResolvePlayerPawn());
	FHitResult Hit;
	return !World->LineTraceSingleByChannel(
		Hit, ViewLocation, MakerOriginCm, ELYSIUM_USE_CHANNEL, Params);
}

bool AElysiumMapActor::IsNpcMakerInPlayerViewCone(const FVector& MakerOriginCm) const
{
	FVector ViewLocation;
	FRotator ViewRotation;
	if (!GetPlayerViewPoint(ViewLocation, ViewRotation))
	{
		return false;
	}
	const FVector Local = ViewRotation.UnrotateVector(MakerOriginCm - ViewLocation);
	if (Local.X <= UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}

	float HorizontalFov = 90.0f;
	float Aspect = 16.0f / 9.0f;
	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			if (PC->PlayerCameraManager)
			{
				HorizontalFov = PC->PlayerCameraManager->GetFOVAngle();
			}
			int32 SizeX = 0;
			int32 SizeY = 0;
			PC->GetViewportSize(SizeX, SizeY);
			if (SizeX > 0 && SizeY > 0)
			{
				Aspect = static_cast<float>(SizeX) / static_cast<float>(SizeY);
			}
		}
	}
	const float TanHalfHorizontal = FMath::Tan(FMath::DegreesToRadians(HorizontalFov * 0.5f));
	const float TanHalfVertical = TanHalfHorizontal / FMath::Max(Aspect, UE_KINDA_SMALL_NUMBER);
	return FMath::Abs(Local.Y / Local.X) <= TanHalfHorizontal
		&& FMath::Abs(Local.Z / Local.X) <= TanHalfVertical;
}

bool AElysiumMapActor::IsNpcMakerSpawnAreaOccupied(const FVector& GroundOriginCm,
	float HalfExtentCm) const
{
	// Native enumerates solid entities through a 2D 68-unit square at the cached ground. A thin
	// plane is sufficient here: any standing body that owns that ground point crosses it.
	const FBox SpawnArea(
		GroundOriginCm - FVector(HalfExtentCm, HalfExtentCm, 1.0f),
		GroundOriginCm + FVector(HalfExtentCm, HalfExtentCm, 1.0f));
	if (const APawn* Pawn = ResolvePlayerPawn())
	{
		if (SpawnArea.Intersect(Pawn->GetComponentsBoundingBox(/*bNonColliding*/ false)))
		{
			return true;
		}
	}
	if (!EntityWorld)
	{
		return false;
	}
	for (const TUniquePtr<FElysiumEntity>& EntPtr : EntityWorld->Entities())
	{
		const FElysiumEntity* Ent = EntPtr.Get();
		if (!Ent || Ent->IsInert() || Ent->Handle == EntityWorld->PlayerHandle())
		{
			continue;
		}
		FBox Bounds(ForceInit);
		if (const USkeletalMeshComponent* Skeletal = Ent->GetSkeletalBody())
		{
			if (Skeletal->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
			{
				Bounds = Skeletal->Bounds.GetBox();
			}
		}
		else if (Ent->Body && Ent->Body->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
		{
			Bounds = Ent->Body->Bounds.GetBox();
		}
		else if (Ent->AsCombatCharacter())
		{
			Bounds = ElysiumStandHullAt(Ent->Origin);
		}
		if (Bounds.IsValid && SpawnArea.Intersect(Bounds))
		{
			return true;
		}
	}
	return false;
}

void AElysiumMapActor::PreMoveTick(float DeltaSeconds)
{
	// Steps 2-3 — everything that must be settled before the pawn moves.

	// The controller and the pawn appear after this actor does, so keep looking until the frame
	// order is fully declared (a menu backdrop map never seats a pawn, and that is fine). This is
	// the frame's first pass, so an edge wired here is in force from the same frame it is bound.
	EnsureTickPrerequisites();
	if (RuntimePhase != EElysiumMapRuntimePhase::Active)
	{
		if (RuntimePhase == EElysiumMapRuntimePhase::WaitingForPrerequisites)
		{
			PollRuntimeActivation();
		}
		return;
	}

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UElysiumGameStateSubsystem* GameState = GI->GetSubsystem<UElysiumGameStateSubsystem>())
		{
			// Step 2 — the only place `Now` moves. DeltaSeconds is already dilated by the
			// engine, and the clock applies no factor of its own, so a time scale is applied once.
			// It sits ahead of the move because retail rebinds `frametime`/`curtime` to the user
			// command's own timing for the move's duration: the move runs at this frame's `now`,
			// never the previous frame's.
			GameState->TimeControl().AdvanceFrame(DeltaSeconds);

			// Step 3 — the player's OWN think, which retail runs inside CPlayerMove::RunCommand
			// (PreThink -> think -> move -> PostThink) rather than in Physics_RunThinkFunctions.
			// Every other entity thinks in the gameplay pass, after the move.
			if (EntityWorld)
			{
				EntityWorld->RunPlayerThink(GameState->GameClock().GetNow());
			}
		}
	}

}

void AElysiumMapActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// ACharacter movement automatically depends on the primary tick of the actor owning its
	// movement base. Runtime world collision is owned here, so this native tick is deliberately an
	// empty barrier between player movement and NPC movement. GameplayTick performs GameFrame after
	// both without creating the reverse edge that caused the patrol-era tick cycle.
}

void AElysiumMapActor::GameplayTick(float DeltaSeconds)
{
	if (RuntimePhase == EElysiumMapRuntimePhase::Activating)
	{
		ActivateRuntime();
		return;
	}
	if (RuntimePhase != EElysiumMapRuntimePhase::Active)
	{
		return;
	}

	// Steps 5-6 — `GameFrame` itself: the pawn has already moved (step 4), which is the order
	// RE21 pins. This whole branch is admitted only after the activation transaction.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UElysiumGameStateSubsystem* GameState = GI->GetSubsystem<UElysiumGameStateSubsystem>())
		{
			// Steps 5-6 — the substrate, think-first (retail order: Physics_RunThinkFunctions,
			// then CEventQueue::ServiceEvents).
			if (EntityWorld)
			{
				// A runtime teleport performed during the prior event pass moved the body immediately but
				// deliberately left its touch links dirty. Movement has now had its retail opportunity;
				// settle the final containment before thinks and queued output delivery.
				if (bPlayerTouchReconcilePending && FElysiumEntityWorld::IsTriggerResolutionEnabled())
				{
					ReconcilePlayerBrushTouches(ResolvePlayerPawn());
				}
				EntityWorld->Tick(GameState->GameClock().GetNow());
			}

			TickAudio(DeltaSeconds);
			TickWeatherPresentation();
		}
	}
}

void AElysiumMapActor::TickAudio(float DeltaSeconds)
{
	UElysiumAudioSubsystem* Audio = GetAudioSubsystem();
	if (!Audio)
	{
		return;
	}
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

void AElysiumMapActor::TickGaze(float DeltaSeconds)
{
	if (EntityWorld == nullptr || Bodies == nullptr)
	{
		return;
	}
	const UGameInstance* GI = GetGameInstance();
	UElysiumRulebookSubsystem* Rules = GI
		? const_cast<UGameInstance*>(GI)->GetSubsystem<UElysiumRulebookSubsystem>() : nullptr;
	const float Now = EntityWorld->NowSeconds();

	// `DialogPOV` is a property of the shot in effect, not of any one character, so it resolves once
	// per frame. It is the dominant look-at surface in the game: 51 of the 66 shipped shot files set
	// it, against 40 authored `LookAtEntity*` wires in the whole of VtMB.
	FVector DialogPovValue = FVector::ZeroVector;
	const FVector* DialogPovPoint = nullptr;
	if (EntityWorld->GetDialogueCameraGaze(DialogPovValue))
	{
		DialogPovPoint = &DialogPovValue;
	}
	else if (CameraDirector && CameraDirector->WantsDialogPOV())
	{
		if (const UElysiumCameraComponent* Cam = PlayerCamera())
		{
			DialogPovValue = Cam->GetComponentLocation();
			DialogPovPoint = &DialogPovValue;
		}
	}

	for (const TUniquePtr<FElysiumEntity>& Entity : EntityWorld->Entities())
	{
		FElysiumEntity* Raw = Entity.Get();
		if (Raw == nullptr || Raw->IsInert())
		{
			continue;
		}
		FElysiumCombatCharacter* Character = Raw->AsCombatCharacter();
		USkeletalMeshComponent* Body = Raw->GetSkeletalBody();
		if (Character == nullptr || Body == nullptr)
		{
			continue;
		}
		// Retail runs the whole eye path per *drawn* model, so an unseen character neither aims nor
		// fidgets. Skipping here rather than inside the eye pass also keeps the cascade's own cost
		// off the frame, which is the half that walks the entity list.
		if (!Body->WasRecentlyRendered(0.2f))
		{
			continue;
		}
		// The head frame the cone and the fidget grid are measured in, with retail's own fallback to
		// EyePosition()/EyeAngles() when the model has no head bone.
		FVector HeadPos = FVector::ZeroVector;
		FVector HeadForward = FVector::ZeroVector;
		if (!Bodies->GetHeadFrame(Body, HeadPos, HeadForward))
		{
			HeadPos = Character->EyePosition();
			HeadForward = FRotator(0.f, Character->Angles.Y, 0.f).Vector();
		}

		// Every rate and interval is content. A character whose disposition does not resolve gets
		// the table's own `Neutral`, which is what the table says it should.
		FElysiumEyeTargetTuning Tuning;
		if (Rules != nullptr)
		{
			if (const FElysiumDisposition* Row = Rules->Dispositions().Resolve(
				Character->Disposition, Character->DispositionLevel))
			{
				Tuning = Row->EyeTarget;
			}
		}
		const FVector Smoothed =
			Character->TickGaze(Now, DeltaSeconds, HeadPos, HeadForward, Tuning, DialogPovPoint);
		Bodies->SetViewTarget(Body, Smoothed);
	}
}

void AElysiumMapActor::PostMoveTick(float DeltaSeconds)
{
	if (RuntimePhase != EElysiumMapRuntimePhase::Active)
	{
		// The lock is an INPUT the mover keeps until it is handed a new one, and the mover's own tick
		// is not gated by this phase. Bailing without taking it back leaves a swing's window frozen
		// at the cycle it stopped at: `SetupMove` re-samples the same interval every step and assigns
		// one constant velocity, which is a slide rather than a lunge. Nothing else in this pass runs
		// outside `Active`.
		ClearPlayerAnimMovementLock(ResolvePlayerPawn());
		return;
	}

	// Step 8 — everything here reads the frame's final positions.
	//
	// Settle camera/body interaction focus and consume this frame's queued command edges. The query
	// belongs after physics: before the pawn's move it would target last frame's geometry, which
	// reads as a door you cannot use until you stop walking. Focus outputs enqueue against the same
	// `now` advanced by the pre-move pass and service on the next frame like any zero-delay wire.
	if (EntityWorld)
	{
		EntityWorld->UpdatePlayerInteraction();
		// The feed request is acquired against the same settled frame the use focus is, and for the
		// same reason: retail's victim search is a trace off the player's final view position.
		EntityWorld->UpdatePlayerFeed();
		// The player's weapon frame, in retail's own `PostThink` order: the controlled-use
		// first refusal and `ItemPostFrame` come after the move that just completed
		// (`docs/vtmb/player-entity.md` § "Recovered `PostThink` body"). A shot accepted here queues
		// its commit for the NEXT frame's queue service, which is immaterial: a ranged commit either
		// carries a delay or arrives from an animation event, and `CommitArrivesFromAnimEvent` reads
		// the phase of the clip the transaction just armed, so it answers correctly on the arming
		// frame.
		EntityWorld->UpdatePlayerWeaponFrame();
		// The melee contact walk, for the player and every swinging NPC alike. It runs
		// AFTER the weapon frame, so a swing accepted this frame starts its walk on the next one:
		// the body's pose layer has not ticked since the clip was armed, and the first frame that
		// reports the clip playing is the first frame the walk has a cycle to test. That is the
		// swing's first live frame, and the frame the opposed roll is staged on.
		//
		// It is the only substrate call in this pass that takes the frame's delta, because the
		// sub-step count is `floor(dt * 100)` and nothing else in the layer measures a frame.
		EntityWorld->AdvanceMeleeSwings(DeltaSeconds);
		// The melee weapon-trail VFX rides the same frame's Swing state the contact walk just
		// advanced, and the same fresh render data the contact walk's own bone queries just read.
		// Presentation only -- no substrate mutation, so it runs beside the walk rather than inside
		// FElysiumEntityWorld.
		ElysiumMeleeTrail::Advance(DeltaSeconds, *EntityWorld);
	}

	// Re-resolve every `Follow` camera shot against this frame's final entity positions. Same
	// reason as the use cursor: a shot framed on where an NPC *was* reads as a camera that lags the
	// subject it is supposed to be locked onto.
	if (CameraDirector)
	{
		CameraDirector->Tick(EntityWorld.Get(), PlayerCamera());
	}
	if (EntityWorld)
	{
		EntityWorld->RefreshDialogueCamera();
	}

	// Decide where each character is looking, then rebuild each eye's basis against this
	// frame's settled pose and publish it to the material. Both halves are here for the same reason
	// as the two above, and for one of their own: the head-bone transform the cascade measures its
	// cone in, and the bone transforms the eye pass reads, are only stable once the frame's parallel
	// animation evaluation has completed, which at this tick group it has.
	//
	// The gaze runs from this pass rather than from NextThink deliberately: NextThink is a
	// single-slot scheduler already shared with patrol, ambient and scripted move on FElysiumNpc,
	// and a per-frame gaze update would fight ThinkPatrol's 0.05 s cadence for the slot.
	TickGaze(DeltaSeconds);
	if (Bodies)
	{
		Bodies->TickEyes(DeltaSeconds);
	}

	// The frame's animation selection, taken from the body sample the mover published at its
	// tick tail. It belongs in this pass for the reason the three above do: the sample is settled only
	// after the last stepper substep, and a selection read before that is a selection made from a
	// half-integrated frame (`docs/architecture/animation-architecture.md` section 3.2).
	TickPlayerAnimation(DeltaSeconds);

	// The tail of a released frame: a dev step spends one here, and the last one re-holds the world.
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (UElysiumGameStateSubsystem* GameState = GI->GetSubsystem<UElysiumGameStateSubsystem>())
		{
			GameState->TimeControl().EndFrame();
		}
	}
}
