// AElysiumMapActor's embodiment seam — the IElysiumEmbodiment body/clip forwards onto
// UElysiumEntityBodies plus the three with logic of their own (the NPC motor factory and the
// attached one-shot effect), the scripted camera-shot channel, the IElysiumAudio voice and
// scheme forwards, and IElysiumTravel with the subsystem accessors behind all of them.

#include "ElysiumMapActor.h"
#include "Substrate/ElysiumNpc.h"

#include "ElysiumAudioSubsystem.h"       // the GI-scoped voice mixer every audio forward reaches
#include "ElysiumCameraComponent.h"      // the pawn camera the shot channel drives
#include "ElysiumContentPaths.h"         // FElysiumContentPaths::BakedParticleSystem — attached effects
#include "ElysiumDecalSubsystem.h"       // R7.2: the shot's forward trace and its impact decal
#include "ElysiumEntityDefs.h"           // FElysiumEntityDef — BodyScaleFor's sky-scope read
#include "ElysiumEntityWorld.h"          // Resolve — a body sound is placed on its owner entity
#include "ElysiumSoundLevel.h"           // A2: the Source sound-level -> falloff model
#include "ElysiumSurfaceSounds.h"        // A2: FElysiumSurfaceSounds — the surface table's row
#include "Audio/ElysiumSurfaceSoundTable.h" // A2: the baked PM_<name> loader behind it
#include "ElysiumMapSubsystem.h"         // the travel owner behind IElysiumTravel
#include "ElysiumMovementComponent.h"    // the death think's ground friction writes its velocity
#include "ElysiumPlayerBody.h"           // IElysiumPlayerBody — the pawn's camera accessor
#include "Audio/ElysiumSoundScheme.h"    // FElysiumSoundSchemeManager — the scheme fade forwards
#include "Map/ElysiumMapLog.h"
#include "Player/ElysiumCameraShots.h"   // FElysiumCameraDirector — the scripted-shot stack
#include "Visual/ElysiumAnimSubsystem.h" // the cinematic bank index
#include "Visual/ElysiumNativeAnimationData.h"
#include "ElysiumBodyData.h"
#include "Visual/ElysiumEntityBodies.h"  // the body factory every mesh forward lands on
#include "Visual/ElysiumLightRig.h"      // R6.2: lightstyle patterns and the runtime light source
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Visual/ElysiumMapVisuals.h"    // RegisterRuntimeBrush — runtime brush visuals join the look
#include "Visual/ElysiumNpcBody.h"       // AElysiumNpcBody — the NPC motor actor
#include "Visual/ElysiumPlacedAttachments.h" // a placed model's `$attachment` off the baked asset
#include "Visual/ElysiumPreparedPropModels.h" // the model row the attachment asset comes from
#include "ElysiumUseIcons.h"             // ELYSIUM_USE_CHANNEL — the pin sweep's channel

#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "NiagaraFunctionLibrary.h"      // SpawnSystemAttached — the one-shot attached effect
#include "NiagaraSystem.h"

namespace
{
	FElysiumClipIdentity CinematicIdentity(UGameInstance* Game, const FString& BodyModel,
		const FString& AnimSetModel, const FString& Root, const FString& Label)
	{
		if (!Game) return {};
		const auto* Native=Game->GetSubsystem<UElysiumNativeAnimationData>();
		const auto* Owner=Native?Native->CinematicBody(AnimSetModel,Root):nullptr;
		if (Owner) return {Owner->AssetId,Label,Owner->OwnerRoot};
		UE_LOG(LogElysium,Warning,TEXT("cinematic owner is absent or not prepared: %s [%s] for %s"),
			*AnimSetModel,*Root,*BodyModel);
		return {};
	}
}

float AElysiumMapActor::BodyScaleFor(const FElysiumEntityDef& Def) const
{
	return Def.bSky ? SkyDef.Scale : 1.f;
}

// The world services — the substrate's engine side. Everything here is a forward: the
// body factory, the player's pawn, the GI-scoped audio subsystem, this map's scheme manager, the
// map subsystem. Nothing under FElysiumEntityWorld knows any of those exist.

USkeletalMeshComponent* AElysiumMapActor::BuildNpcVisual(const FString& Stem, const FVector& Location,
	const FRotator& Rotation, float UniformScale, const FString& Disposition, int32 IdleVariant)
{
	return Bodies->BuildNpcVisual(Stem, Location, Rotation, UniformScale, Disposition, IdleVariant);
}

IElysiumNpcMotor* AElysiumMapActor::BuildNpcMotor(USkeletalMeshComponent* Body,
	const FElysiumEntityHandle& EntityOwner, const FVector& FeetOrigin, float YawDegrees,
	const FString& Stem, int32 Variant)
{
	if (!Body || bMenuBackdrop || !GetWorld())
	{
		return nullptr;
	}
	if (!EntityOwner.IsSet())
	{
		UE_LOG(LogElysium, Warning, TEXT("failed to build native NPC body at %s: invalid entity owner"),
			*FeetOrigin.ToString());
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.OverrideLevel = GetLevel();
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AElysiumNpcBody* Motor = GetWorld()->SpawnActor<AElysiumNpcBody>(
		AElysiumNpcBody::StaticClass(), FTransform::Identity, Params);
	if (!Motor)
	{
		UE_LOG(LogElysium, Warning, TEXT("failed to spawn native NPC body at %s"), *FeetOrigin.ToString());
		return nullptr;
	}

	Motor->SetOwningEntity(this, EntityOwner);
	Motor->InitializeAtFeet(FeetOrigin, YawDegrees);
	Motor->SetRuntimeReady(RuntimePhase == EElysiumMapRuntimePhase::Active);
	Motor->SetModelStem(Stem, Body, Variant);
	NpcMotors.Add(Motor);
	Body->AttachToComponent(Motor->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
	if (UCharacterMovementComponent* Movement = Motor->GetCharacterMovement())
	{
		// CharacterMovement adds the map actor's primary tick as a prerequisite when this character
		// stands on map-owned collision. The separate gameplay tick can safely form the forward edge;
		// using PrimaryActorTick here would close a cycle through that automatic movement-base edge.
		GameplayTickFunction.AddPrerequisite(Movement, Movement->PrimaryComponentTick);
	}
	return Motor;
}

void AElysiumMapActor::DestroyNpcMotor(IElysiumNpcMotor* Motor)
{
	if (bMotorsRetired)
	{
		// EndPlay already released them and the engine owns the actors now. This is the teardown
		// call: ~AElysiumMapActor destroys the entity world, every FElysiumNpc destructor on the way
		// out calls here, and by then the motor's UObject index has been freed — at which point even
		// IsValid() asserts, because it reaches FUObjectArray::IndexToObject with index -1.
		return;
	}
	AElysiumNpcBody* Body = static_cast<AElysiumNpcBody*>(Motor);
	if (!Body)
	{
		return;
	}
	if (!IsValid(Body))
	{
		NpcMotors.RemoveSingleSwap(Body);
		return; // world teardown already owns the pending-kill actor
	}
	if (UCharacterMovementComponent* Movement = Body->GetCharacterMovement())
	{
		GameplayTickFunction.RemovePrerequisite(Movement, Movement->PrimaryComponentTick);
	}
	NpcMotors.RemoveSingleSwap(Body);
	Body->Destroy();
}

bool AElysiumMapActor::RefreshNpcIdle(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& Disposition, int32 DispositionLevel, int32 IdleVariant)
{
	return Bodies->RefreshNpcIdle(Body, Stem, Disposition, DispositionLevel, IdleVariant);
}

void AElysiumMapActor::UpdateNpcDisposition(USkeletalMeshComponent* Body,
	const FString& Disposition, int32 DispositionLevel)
{
	Bodies->UpdateNpcDisposition(Body, Disposition, DispositionLevel);
}

bool AElysiumMapActor::ResolveStanceClips(const FString& Stem, const FString& AnimName,
	FElysiumStanceClips& OutClips)
{
	return Bodies->ResolveStanceClips(Stem, AnimName, OutClips);
}

bool AElysiumMapActor::ResolveDisposition(const FString& Disposition, int32 DispositionLevel,
	FElysiumDisposition& OutRow)
{
	return Bodies->ResolveDisposition(Disposition, DispositionLevel, OutRow);
}

bool AElysiumMapActor::IsNpcBodyVisible(USkeletalMeshComponent* Body)
{
	return Bodies->IsNpcBodyVisible(Body);
}

bool AElysiumMapActor::ResolveNpcActivityClip(const FElysiumActivityClipRequest& Request,
	FElysiumActivityClip& Out)
{
	return Bodies->ResolveNpcActivityClip(Request, Out);
}

bool AElysiumMapActor::PlayNpcOneShot(USkeletalMeshComponent* Body,
	const FElysiumOneShotClipRequest& Request, float* OutSeconds)
{
	return Bodies->PlayNpcOneShot(Body, Request, OutSeconds);
}

void AElysiumMapActor::ReleaseNpcReaction(USkeletalMeshComponent* Body)
{
	if (Bodies)
	{
		Bodies->ReleaseNpcReaction(Body);
	}
}

void AElysiumMapActor::DrainPlayerTouchContacts(TArray<FElysiumEntityHandle>& Out)
{
	// No entity walk and no component -> entity map: every NPC body carries its own handle, and
	// the contact is the body's own `NotifyHit` record. Drained here so a contact is reported
	// once per frame of contact, which is how often retail's `Touch` fires.
	Out.Reset();
	for (AElysiumNpcBody* Body : NpcMotors)
	{
		if (Body != nullptr && Body->ConsumePlayerContact())
		{
			Out.Add(Body->GetOwningEntity());
		}
	}
}

EElysiumHeldReactionState AElysiumMapActor::QueryNpcReactionHold(
	USkeletalMeshComponent* Body) const
{
	// A map with no body factory arbitrates nothing, so nothing is held and nothing stands in a
	// resume's way — the same answer the seam's own default gives.
	return Bodies ? Bodies->QueryNpcReactionHold(Body) : EElysiumHeldReactionState::Free;
}

void AElysiumMapActor::ReleaseBodyAnimClaims(USkeletalMeshComponent* Body)
{
	if (Bodies)
	{
		Bodies->ReleaseBodyAnimClaims(Body);
	}
}

bool AElysiumMapActor::StartBodyRagdoll(USkeletalMeshComponent* Body)
{
	return Bodies && Bodies->StartBodyRagdoll(Body);
}

void AElysiumMapActor::HoldBodyFinalPose(USkeletalMeshComponent* Body)
{
	if (Bodies)
	{
		Bodies->HoldBodyFinalPose(Body);
	}
}

bool AElysiumMapActor::ResolveNpcSequenceClip(const FString& Stem, const FString& ClipName,
	EElysiumAnimBodyKind BodyKind, FString& OutAnimName, float& OutGroundSpeedCmPerSecond)
{
	return Bodies->ResolveNpcSequenceClip(Stem, ClipName, BodyKind, OutAnimName,
		OutGroundSpeedCmPerSecond);
}

bool AElysiumMapActor::HasNpcClip(const FString& Stem, const FString& ClipName)
{
	return Bodies->HasNpcClip(Stem, ClipName);
}

FString AElysiumMapActor::NpcClipBlockedReaction(const FString& Stem, const FString& ClipLabel)
{
	return Bodies->NpcClipBlockedReaction(Stem, ClipLabel);
}

const TArray<FElysiumSwingRecord>* AElysiumMapActor::NpcClipSwings(const FString& Stem,
	const FString& ClipLabel)
{
	return Bodies->NpcClipSwings(Stem, ClipLabel);
}

const FElysiumComboChain* AElysiumMapActor::NpcClipCombo(const FString& Stem,
	const FString& ClipLabel)
{
	return Bodies->NpcClipCombo(Stem, ClipLabel);
}

FString AElysiumMapActor::NpcClipOwner(const FString& Stem, const FString& ClipLabel)
{
	return Bodies->NpcClipOwner(Stem, ClipLabel);
}

bool AElysiumMapActor::GetBodyBoneTransform(USkeletalMeshComponent* Body, const FString& BoneName,
	FTransform& OutWorld) const
{
	return Bodies ? Bodies->GetBoneFrame(Body, BoneName, OutWorld) : false;
}

bool AElysiumMapActor::GetBodyAttachment(const FElysiumEntityHandle& OwnerHandle, FName Attachment,
	FTransform& OutWorld) const
{
	// `CBaseAnimating::GetAttachment01` (`docs/vtmb/computer-terminals.md` §7.2). Three routes, in
	// the order that answers with the least composition:
	//   1. a socket table a harness registered explicitly for this owner;
	//   2. the standing body itself, when the placed model was NOT reduced to its static form;
	//   3. the model row's baked `SkeletalMesh` ref pose, composed with the standing body's
	//      transform -- the ordinary production case, because `FElysiumCataloguePlacedModel`
	//      keeps both representations resident and a rigid prop's rig IS its bind pose.
	if (Attachment.IsNone() || !OwnerHandle.IsSet())
	{
		return false;
	}
	const FUseAnchorRecord* Record = UseAnchors.FindByPredicate(
		[OwnerHandle](const FUseAnchorRecord& Candidate)
		{
			return Candidate.Owner == OwnerHandle;
		});
	if (!Record)
	{
		return false;
	}

	if (const USkeletalMeshComponent* Source = Record->AttachmentSource.Get())
	{
		if (Source->DoesSocketExist(Attachment))
		{
			OutWorld = Source->GetSocketTransform(Attachment, RTS_World);
			return true;
		}
	}

	UPrimitiveComponent* Visual = Record->Visual.Get();
	if (!Visual)
	{
		return false;
	}
	if (const USkeletalMeshComponent* Skeletal = Cast<USkeletalMeshComponent>(Visual))
	{
		if (Skeletal->DoesSocketExist(Attachment))
		{
			OutWorld = Skeletal->GetSocketTransform(Attachment, RTS_World);
			return true;
		}
	}

	const FElysiumEntity* Entity = EntityWorld ? EntityWorld->Resolve(OwnerHandle) : nullptr;
	if (!Entity || Entity->Model.IsEmpty())
	{
		return false;
	}
	const TSharedPtr<FElysiumPreparedPropModels> Prepared = ElysiumPreparedProps::ForOwner(this);
	if (!Prepared.IsValid())
	{
		return false;
	}
	FString Error;
	const USkeletalMesh* Mesh = Prepared->SkeletalMesh(ElysiumPreparedProps::ModelId(Entity->Model),
		Error);
	FTransform Local;
	if (!ElysiumPlacedAttachments::RefPoseTransform(Mesh, Attachment, Local))
	{
		return false;
	}
	OutWorld = Local * Visual->GetComponentTransform();
	return true;
}

void AElysiumMapActor::RegisterAttachmentSource(const FElysiumEntityHandle& OwnerHandle,
	USkeletalMeshComponent* Source)
{
	for (FUseAnchorRecord& Record : UseAnchors)
	{
		if (Record.Owner == OwnerHandle)
		{
			Record.AttachmentSource = Source;
		}
	}
}

bool AElysiumMapActor::GetUseBodyWorldBounds(const FElysiumEntityHandle& OwnerHandle,
	FBox& OutWorld) const
{
	const FUseAnchorRecord* Record = UseAnchors.FindByPredicate(
		[OwnerHandle](const FUseAnchorRecord& Candidate)
		{
			return Candidate.Owner == OwnerHandle;
		});
	// The ANCHOR, not the visual. Retail measures all three of the held-use questions against one
	// collision box — `ent+0x274`/`ent+0x284`: the reach clamp (`FUN_10167e00` `10167e59`), the
	// `WorldSpaceCenter()` the near arm snaps the view at (slot 192 = `(mins+maxs)*0.5`,
	// `slice-bc-decompiles.md` §5.2) and the box the pin's `MASK_PLAYERSOLID` sweep stops on. In
	// this port that one box is the registered use anchor (the `ELYSIUM_USE_CHANNEL` proxy, or the
	// brush itself), which is exactly what the sweep already blocks against — so reading the
	// visual's render bounds here would let the reach test and the snap target drift away from the
	// surface the pin actually stops at.
	const UPrimitiveComponent* Anchor = Record ? Record->Component.Get() : nullptr;
	if (!Anchor)
	{
		return false;
	}
	OutWorld = Anchor->Bounds.GetBox();
	return true;
}

bool AElysiumMapActor::SweepPlayerHullToward(const FVector& TargetCm, FVector& OutContactCm)
{
	// `CBaseTerminal` slot 43, `vampire.dll` 0x10218320 (`slice-bc-decompiles.md` §1). The ray retail
	// builds is a verbatim `Ray_t::Init`:
	//   start   = player origin, offset by (collision mins + maxs) * 0.5 -- the hull centre
	//   extents = (collision maxs - mins) * 0.5                          -- the half hull
	//   end     = (terminal.x, terminal.y, PLAYER.z)                     -- the sweep is XY-only
	// with a `CTraceFilterSimple(player, COLLISION_GROUP_PLAYER)`, and the trace's `endpos` goes
	// straight back through `SetAbsOrigin` + `Relink` at 10218571-10218583 -- no fraction test and
	// no start-solid test (correction C1; the branch above that call gates only an
	// `NDebugOverlay::Line`).
	//
	// This runtime's pawn root IS the hull centre, so the start needs no offset and the sweep's
	// result location is directly the actor location.
	//
	// **Named modernization.** Retail's `0x0201400b` is `MASK_PLAYERSOLID`, and what stops the sweep
	// at the machine is the terminal's own `SOLID_BBOX`. This runtime keeps a placed prop body
	// non-solid, so the mask's union is reached as two sweeps: the pawn's own movement channel for
	// world solidity, and `ELYSIUM_USE_CHANNEL` where the registered use anchor stands in for the
	// terminal's box. The nearer contact wins, which is what one union trace would have answered.
	APawn* Pawn = const_cast<APawn*>(ResolvePlayerPawn());
	UPrimitiveComponent* Hull = Pawn ? Cast<UPrimitiveComponent>(Pawn->GetRootComponent()) : nullptr;
	UWorld* World = GetWorld();
	if (!Pawn || !Hull || !World)
	{
		return false;
	}

	const FVector Start = Hull->GetComponentLocation();
	const FVector End(TargetCm.X, TargetCm.Y, Start.Z);
	OutContactCm = End;
	if (!Start.Equals(End))
	{
		FCollisionQueryParams Params(FName(TEXT("ElysiumTerminalPin")), /*bTraceComplex*/ false);
		Params.AddIgnoredActor(Pawn);
		const FCollisionShape Shape = Hull->GetCollisionShape();
		const FQuat Rotation = Hull->GetComponentQuat();
		double BestDistanceSq = FVector::DistSquared(Start, End);
		for (const ECollisionChannel Channel :
			{ Hull->GetCollisionObjectType(), ELYSIUM_USE_CHANNEL })
		{
			FHitResult Hit;
			if (!World->SweepSingleByChannel(Hit, Start, End, Rotation, Channel, Shape, Params))
			{
				continue;
			}
			const double DistanceSq = FVector::DistSquared(Start, Hit.Location);
			if (DistanceSq < BestDistanceSq)
			{
				BestDistanceSq = DistanceSq;
				OutContactCm = Hit.Location;
			}
		}
	}
	Pawn->SetActorLocation(OutContactCm, /*bSweep*/ false, nullptr, ETeleportType::TeleportPhysics);
	return true;
}

bool AElysiumMapActor::TraceCameraHull(const FVector& FromCm, const FVector& ToCm,
	const FVector& HalfExtentCm, const FElysiumEntityHandle& IgnoreEntity,
	float& OutFraction, bool& OutStartSolid) const
{
	// `FUN_1006db10`'s per-anchor trace, verbatim in shape:
	//   `UTIL_TraceHull(anchor, lookAt, (-1,-1,-1), (1,1,1), 0x1400b,
	//                   CTraceFilterSimple(m_hSubject, 0), &tr)`
	// and the predicate above it fails on `fraction < 1 || startsolid || allsolid`.
	//
	// `0x1400b` is `MASK_PLAYERSOLID_BRUSHONLY` (SOLID | WINDOW | GRATE | MOVEABLE | PLAYERCLIP) —
	// `MASK_PLAYERSOLID` without `CONTENTS_MONSTER`. So retail's occluder set is brush geometry, and
	// `ELYSIUM_USE_CHANNEL` — this project's solid-world channel, the one the map's brush bodies and
	// the material-less `.hulls` surface answer on — is the port's expression of it. The named
	// modernization is the channel, never the semantics: a character is not an occluder on either
	// side.
	OutFraction = 1.0f;
	OutStartSolid = false;

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		// No collision world to ask — a `-nullrhi` Substrate run or an unbuilt actor. The stated
		// headless answer is "the trace did not run", which admits the candidate.
		return false;
	}
	if (FromCm.Equals(ToCm))
	{
		// A degenerate segment is trivially clear, and retail's own hull trace over a zero-length
		// ray answers `fraction 1` unless the hull already starts inside something — which is the
		// `startsolid` arm below, and a shot anchor buried in a wall is not a case any shipped file
		// authors.
		return true;
	}

	FCollisionQueryParams Params(FName(TEXT("ElysiumCameraFindBestShot")), /*bTraceComplex*/ false);
	// `CTraceFilterSimple(m_hSubject, 0)` — the shot's subject is excluded from the trace. Its
	// registered use anchor and its body actor are the two things in this runtime that stand for
	// that entity's collision, so both are ignored.
	if (IgnoreEntity.IsSet())
	{
		for (const FUseAnchorRecord& Record : UseAnchors)
		{
			if (Record.Owner == IgnoreEntity)
			{
				if (const UPrimitiveComponent* Component = Record.Component.Get())
				{
					Params.AddIgnoredComponent(Component);
				}
			}
		}
		if (FElysiumEntityWorld* Entities = GetEntityWorld())
		{
			if (const FElysiumEntity* Subject = Entities->Resolve(IgnoreEntity))
			{
				if (const USkeletalMeshComponent* Body = Subject->GetSkeletalBody())
				{
					Params.AddIgnoredActor(Body->GetOwner());
				}
			}
		}
	}

	const FCollisionShape Hull = FCollisionShape::MakeBox(HalfExtentCm.GetAbs());
	FHitResult Hit;
	const bool bHit = World->SweepSingleByChannel(Hit, FromCm, ToCm, FQuat::Identity,
		ELYSIUM_USE_CHANNEL, Hull, Params);
	if (bHit)
	{
		OutFraction = static_cast<float>(Hit.Time);
		// Source distinguishes `startsolid` from `allsolid`; Unreal's `bStartPenetrating` is their
		// union, and `FUN_1006db10` ORs the two anyway.
		OutStartSolid = Hit.bStartPenetrating;
	}
	return true;
}

bool AElysiumMapActor::SnapPlayerViewTo(const FVector& TargetCm)
{
	// `CBaseTerminal` slot 41, `0x102182b0` -> `FUN_10178590(player, WorldSpaceCenter())`:
	// `VectorAngles(target - playerEye)` written onto the player's eye angles every tick
	// (`slice-bc-decompiles.md` §5.2, correction C4). The pitch/yaw pair is the whole write; roll is
	// not part of `VectorAngles`, so the controller's own roll stands.
	APawn* Pawn = const_cast<APawn*>(ResolvePlayerPawn());
	APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	FVector Eye;
	if (!Controller || !GetPlayerUseOrigin(Eye))
	{
		return false;
	}
	const FVector Direction = TargetCm - Eye;
	if (Direction.IsNearlyZero())
	{
		return false;
	}
	FRotator Look = Direction.Rotation();
	Look.Roll = Controller->GetControlRotation().Roll;
	Controller->SetControlRotation(Look);
	return true;
}

bool AElysiumMapActor::GetBodyClipPhase(USkeletalMeshComponent* Body, EElysiumAnimChannel Channel,
	FElysiumClipPhase& Out)
{
	return Bodies->GetBodyClipPhase(Body, Channel, Out);
}

const TArray<FElysiumAnimEvent>* AElysiumMapActor::GetNpcEventTimeline(const FString& OwnerStem,
	const FString& Label, const FString& OwnerRoot)
{
	return Bodies->GetNpcEventTimeline(OwnerStem, Label, OwnerRoot);
}

bool AElysiumMapActor::AttachOrnamentModel(USkeletalMeshComponent* Body, const FString& RetailPath)
{
	return Bodies && Bodies->AttachOrnamentModel(Body, RetailPath);
}

void AElysiumMapActor::DetachOrnamentModel(USkeletalMeshComponent* Body)
{
	if (Bodies)
	{
		Bodies->DetachOrnamentModel(Body);
	}
}

bool AElysiumMapActor::PlayNpcClip(USkeletalMeshComponent* Body, const FString& Stem,
	const FElysiumClipSegment& Segment, float* OutSeconds)
{
	return Bodies->PlayNpcClip(Body, Stem, Segment, OutSeconds);
}

void AElysiumMapActor::ReleaseNpcSegment(USkeletalMeshComponent* Body)
{
	if (Bodies)
	{
		Bodies->ReleaseNpcSegment(Body);
	}
}

bool AElysiumMapActor::PreloadNpcClip(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& ClipName)
{
	return Bodies && Bodies->PreloadNpcClip(Body, Stem, ClipName);
}

bool AElysiumMapActor::PreloadNpcClipForModel(const FString& Stem, bool bPlayerMaterial,
	const FString& ClipName)
{
	return Bodies && Bodies->PreloadNpcClipForModel(Stem, bPlayerMaterial, ClipName);
}

bool AElysiumMapActor::PlayCinematicClip(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& AnimSetModel, const FString& BoneRoot, const FString& ClipName,
	bool bLoop, float* OutSeconds)
{
	const auto Identity=CinematicIdentity(GetGameInstance(),Stem,AnimSetModel,BoneRoot,ClipName);
	return Bodies && Identity.IsValid() && Bodies->PlayCinematicClip(Body,Stem,Identity.OwnerStem,
		ClipName,bLoop,OutSeconds,Identity.OwnerRoot);
}

bool AElysiumMapActor::PreloadCinematicClip(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& AnimSetModel, const FString& BoneRoot, const FString& ClipName)
{
	const auto Identity=CinematicIdentity(GetGameInstance(),Stem,AnimSetModel,BoneRoot,ClipName);
	return Bodies && Identity.IsValid() && Bodies->PreloadCinematicClip(Body,Stem,Identity.OwnerStem,
		ClipName,Identity.OwnerRoot);
}

bool AElysiumMapActor::PreloadCinematicClipForModel(const FString& Stem, bool bPlayerMaterial,
	const FString& AnimSetModel, const FString& BoneRoot, const FString& ClipName)
{
	const auto Identity=CinematicIdentity(GetGameInstance(),Stem,AnimSetModel,BoneRoot,ClipName);
	return Bodies && Identity.IsValid() && Bodies->PreloadCinematicClipForModel(Stem,bPlayerMaterial,
		Identity.OwnerStem,ClipName,Identity.OwnerRoot);
}

bool AElysiumMapActor::SeekCinematicClip(USkeletalMeshComponent* Body, float PositionSeconds)
{
	return Bodies && Bodies->SeekCinematicClip(Body, PositionSeconds);
}

void AElysiumMapActor::StopCinematicClip(USkeletalMeshComponent* Body)
{
	if (Bodies)
	{
		Bodies->StopCinematicClip(Body);
	}
}

void AElysiumMapActor::ReleaseCinematicClaim(USkeletalMeshComponent* Body)
{
	if (Bodies)
	{
		Bodies->ReleaseCinematicClaim(Body);
	}
}

bool AElysiumMapActor::GetCinematicClipPosition(USkeletalMeshComponent* Body, float& OutSeconds) const
{
	return Bodies && Bodies->GetCinematicClipPosition(Body, OutSeconds);
}

bool AElysiumMapActor::ResyncCinematicClip(USkeletalMeshComponent* Body, float PositionSeconds)
{
	return Bodies && Bodies->ResyncCinematicClip(Body, PositionSeconds);
}

int32 AElysiumMapActor::SetFlexControllers(USkeletalMeshComponent* Body,
	TArrayView<const FElysiumFlexWrite> Writes, TArray<FString>* OutMissing)
{
	return Bodies ? Bodies->SetFlexControllers(Body, Writes, OutMissing) : INDEX_NONE;
}

bool AElysiumMapActor::SetMouthOpen(USkeletalMeshComponent* Body, float Open)
{
	return Bodies ? Bodies->SetMouthOpen(Body, Open) : false;
}

bool AElysiumMapActor::PlayAttachedEffect(USkeletalMeshComponent* Body,
	const FString& Definition, FName Attachment)
{
	auto WarnOnce = [this, &Definition](const FString& Key, const FString& Message)
	{
		const FString Failure = TEXT("oneshot|") + Key;
		if (!ReportedEmitterFailures.Contains(Failure))
		{
			ReportedEmitterFailures.Add(Failure);
			UE_LOG(LogElysium, Warning, TEXT("%s"), *Message);
		}
	};
	if (!Body || Definition.IsEmpty() || Attachment.IsNone())
	{
		WarnOnce(TEXT("invalid|") + Definition,
			FString::Printf(TEXT("cannot play attached effect '%s' on map '%s': invalid body or attachment"),
				*Definition, *MapName));
		return false;
	}
	if (!Body->DoesSocketExist(Attachment))
	{
		WarnOnce(TEXT("socket|") + Definition + TEXT("|") + Attachment.ToString(),
			FString::Printf(TEXT("cannot play attached effect '%s' on %s: baked socket '%s' is absent"),
				*Definition, *Body->GetName(), *Attachment.ToString()));
		return false;
	}
	const FString Path = FElysiumContentPaths::BakedParticleSystem(MapName, Definition);
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *Path);
	if (!System)
	{
		WarnOnce(TEXT("system|") + Definition,
			FString::Printf(TEXT("cannot play attached effect '%s' on map '%s': Niagara system '%s' is absent"),
				*Definition, *MapName, *Path));
		return false;
	}
	UNiagaraComponent* Spawned = UNiagaraFunctionLibrary::SpawnSystemAttached(
		System, Body, Attachment, FVector::ZeroVector, FRotator::ZeroRotator,
		EAttachLocation::SnapToTarget, /*bAutoDestroy*/ true, /*bAutoActivate*/ true,
		ENCPoolMethod::None, /*bPreCullCheck*/ false);
	if (!Spawned)
	{
		WarnOnce(TEXT("spawn|") + Definition,
			FString::Printf(TEXT("cannot spawn attached effect '%s' on %s.%s"),
				*Definition, *Body->GetName(), *Attachment.ToString()));
		return false;
	}
	return true;
}

bool AElysiumMapActor::GetPhonemeFilter(USkeletalMeshComponent* Body, float& OutMin,
	float& OutMax) const
{
	return Bodies ? Bodies->GetPhonemeFilter(Body, OutMin, OutMax) : false;
}

bool AElysiumMapActor::SetViewTarget(USkeletalMeshComponent* Body, const FVector& WorldTarget)
{
	return Bodies ? Bodies->SetViewTarget(Body, WorldTarget) : false;
}

bool AElysiumMapActor::GetHeadFrame(USkeletalMeshComponent* Body, FVector& OutPosition,
	FVector& OutForward) const
{
	return Bodies ? Bodies->GetHeadFrame(Body, OutPosition, OutForward) : false;
}

// Every placed-model entry below stands down while a late admission is in flight
// (EnsurePlacedModelAdmitted): the entity gets an empty answer now, no warning, and a rebuild
// when the model lands. A model the catalogues never carried falls through and reports once.
FString AElysiumMapActor::AnimatedPropStemForModel(const FString& ModelPath) const
{
	if (!const_cast<AElysiumMapActor*>(this)->EnsurePlacedModelAdmitted(ModelPath)) return FString();
	return Bodies ? Bodies->AnimatedPropStemForModel(ModelPath) : FString();
}

FElysiumPlacedModelBody AElysiumMapActor::BuildPlacedModelBody(
	const FElysiumPlacedModelRequest& Request)
{
	if (!EnsurePlacedModelAdmitted(Request.ModelPath)) return FElysiumPlacedModelBody{};
	return Bodies ? Bodies->BuildPlacedModelBody(Request) : FElysiumPlacedModelBody{};
}

bool AElysiumMapActor::HasPlacedModelCatalogue() const
{
	return Bodies && Bodies->HasPlacedModelCatalogue();
}

USkeletalMeshComponent* AElysiumMapActor::BuildAnimatedPropVisual(const FString& Stem,
	const FVector& Location, const FQuat& Rotation, float UniformScale, int32 PlacementToken)
{
	return Bodies ? Bodies->BuildAnimatedPropVisual(
		Stem, Location, Rotation, UniformScale, PlacementToken) : nullptr;
}

bool AElysiumMapActor::PlayAnimatedPropClip(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& ClipName, bool bLoop, float* OutSeconds)
{
	return Bodies && Bodies->PlayAnimatedPropClip(Body, Stem, ClipName, bLoop, OutSeconds);
}

int32 AElysiumMapActor::PreloadAnimatedPropClips(USkeletalMeshComponent* Body,
	const FString& Stem)
{
	return Bodies ? Bodies->PreloadAnimatedPropClips(Body, Stem) : 0;
}

int32 AElysiumMapActor::FinishAnimationPreload()
{
	return Bodies ? Bodies->FinishAnimationPreload() : 0;
}

FString AElysiumMapActor::AnimatedPropRestClip(const FString& Stem, int32 PlacementToken) const
{
	return Bodies ? Bodies->AnimatedPropRestClip(Stem, PlacementToken) : FString();
}

bool AElysiumMapActor::FindAnimatedPropClip(const FString& Stem, const FString& ClipName,
	bool& bOutLoops) const
{
	bOutLoops = false;
	return Bodies && Bodies->FindAnimatedPropClip(Stem, ClipName, bOutLoops);
}

void AElysiumMapActor::ApplyAnimatedPropSkin(USkeletalMeshComponent* Comp,
	const FString& StaticStem, int32 Family)
{
	if (Bodies)
	{
		Bodies->ApplyAnimatedPropSkin(Comp, StaticStem, Family);
	}
}

void AElysiumMapActor::SetLightStylePattern(int32 Style, const FString& Pattern)
{
	if (UElysiumLightRig* Rig = Visuals ? Visuals->GetLightRig() : nullptr)
	{
		Rig->SetStylePattern(Style, Pattern);
	}
}

FString AElysiumMapActor::LightStylePattern(int32 Style) const
{
	const UElysiumLightRig* Rig = Visuals ? Visuals->GetLightRig() : nullptr;
	return Rig ? Rig->StylePattern(Style) : FString();
}

ULightComponent* AElysiumMapActor::BuildDynamicLight(const FElysiumDynamicLightSpec& Spec,
	USceneComponent* Parent)
{
	UElysiumLightRig* Rig = Visuals ? Visuals->GetLightRig() : nullptr;
	USceneComponent* Root = GetRootComponent();
	if (!Rig || !Root || Spec.Mag <= 0.f)
	{
		return nullptr;
	}
	ULocalLightComponent* Light = Spec.bSpot
		? static_cast<ULocalLightComponent*>(NewObject<USpotLightComponent>(this))
		: static_cast<ULocalLightComponent*>(NewObject<UPointLightComponent>(this));
	Light->SetMobility(EComponentMobility::Movable);
	Light->SetupAttachment(Parent ? Parent : Root);
	Light->SetWorldLocationAndRotation(Spec.LocationCm, FRotationMatrix::MakeFromX(Spec.Forward).ToQuat());
	Light->RegisterComponent();
	AddInstanceComponent(Light);
	Rig->AddRuntimeSource(Light, Spec.bSpot ? 2 : 1, Spec.Color, Spec.Mag, Spec.RadiusCm,
		Spec.StopDot, Spec.StopDot2, Spec.Style);
	return Light;
}

void AElysiumMapActor::DestroyDynamicLight(ULightComponent* Light)
{
	if (!Light)
	{
		return;
	}
	if (UElysiumLightRig* Rig = Visuals ? Visuals->GetLightRig() : nullptr)
	{
		Rig->RemoveRuntimeSource(Light);
	}
	RemoveInstanceComponent(Light);
	Light->DestroyComponent();
}

void AElysiumMapActor::SetBakedSpriteVisible(int32 EntityIndex, bool bVisible)
{
	if (!Visuals)
	{
		return;
	}
	if (Visuals->SetSpriteVisible(EntityIndex, bVisible))
	{
		return;
	}
	// The write landed nowhere. On a map that bakes no sprites at all this is the ruled
	// legacy-lane case (`ElysiumEnvSprite.cpp`, R6.1) and stays silent; on a map that has the
	// lane, an index with no billboard means the bake and the `.ents` disagree and the corona
	// simply never appears -- say so once per sprite, not once per input.
	if (Visuals->SpriteCount > 0 && !SpriteMissWarned.Contains(EntityIndex))
	{
		SpriteMissWarned.Add(EntityIndex);
		UE_LOG(LogElysium, Warning,
			TEXT("'%s': env_sprite #%d has no baked billboard (%d adopted) -- %s lands nowhere"),
			*MapName, EntityIndex, Visuals->SpriteCount, bVisible ? TEXT("show") : TEXT("hide"));
	}
}

UStaticMeshComponent* AElysiumMapActor::BuildBrushVisual(const FString& Stem,
	USceneComponent* ParentBody, float UniformScale, bool bSky)
{
	UStaticMeshComponent* Comp = Bodies
		? Bodies->BuildBrushVisual(Stem, ParentBody, UniformScale, bSky) : nullptr;
	if (Comp && Visuals)
	{
		Visuals->RegisterRuntimeBrush(Comp, bSky);
	}
	return Comp;
}

UStaticMeshComponent* AElysiumMapActor::BuildPropVisual(const FString& Stem, const FVector& Location,
	const FQuat& Rotation, float UniformScale)
{
	if (!EnsurePlacedModelAdmitted(Stem)) return nullptr;
	return Bodies->BuildPropVisual(Stem, Location, Rotation, UniformScale);
}

EElysiumItemGroundModelState AElysiumMapActor::ItemGroundModelState(const FString& ModelPath)
{
	if (!EnsurePlacedModelAdmitted(ModelPath)) return EElysiumItemGroundModelState::Unavailable;
	return Bodies ? Bodies->ItemGroundModelState(ModelPath)
		: EElysiumItemGroundModelState::Unavailable;
}

UStaticMeshComponent* AElysiumMapActor::BuildPhysPropVisual(const FString& Stem, const FVector& Location,
	const FQuat& Rotation, float UniformScale)
{
	if (!EnsurePlacedModelAdmitted(Stem)) return nullptr;
	return Bodies->BuildPhysPropVisual(Stem, Location, Rotation, UniformScale);
}

void AElysiumMapActor::ApplyPropSkin(UStaticMeshComponent* Comp, const FString& Stem, int32 Family)
{
	Bodies->ApplyPropSkin(Comp, Stem, Family);
}

UElysiumCameraComponent* AElysiumMapActor::PlayerCamera() const
{
	const APawn* Pawn = ResolvePlayerPawn();
	const IElysiumPlayerBody* Body = Cast<IElysiumPlayerBody>(Pawn);
	return Body ? Body->GetCameraComponent() : nullptr;
}

void AElysiumMapActor::BleedPlayerBodyVelocity(float StepCm)
{
	APawn* Pawn = ResolvePlayerPawn();
	UElysiumMovementComponent* Move = Pawn != nullptr
		? Pawn->FindComponentByClass<UElysiumMovementComponent>() : nullptr;
	if (Move == nullptr)
	{
		// No mover on the pawn is the ordinary headless/backdrop answer here, unlike the melee stop
		// next door: the death think asks every frame for the whole death, and a run with no
		// movement port has no carried motion for the friction to take off in the first place.
		return;
	}
	// `VectorLength - 20`, then `VectorNormalize * speed` or `vec3_origin` — the component's own
	// velocity, which is what `SetLocalVelocity` writes and what the published sample is derived
	// from. The vertical term is inside the length exactly as it is in the original: retail takes
	// the whole vector, and the arm only runs while the body is on the ground anyway.
	const float Speed = static_cast<float>(Move->Velocity.Size()) - StepCm;
	Move->Velocity = Speed > 0.f ? Move->Velocity.GetSafeNormal() * Speed : FVector::ZeroVector;
}

void AElysiumMapActor::SetPlayerFovOverride(int32 SourceFov)
{
	// The replicated integer, handed to the camera that resolves it. A frame with no pawn drops it:
	// `m_iFOV` is player state and a run with no player is exactly the case retail's `default_fov`
	// fallback covers.
	if (UElysiumCameraComponent* Camera = PlayerCamera())
	{
		if (SourceFov < 0)
		{
			Camera->ClearPlayerFovOverride();   // the seam's "no producer has spoken"
		}
		else
		{
			Camera->SetPlayerFovOverride(SourceFov);
		}
	}
}

int32 AElysiumMapActor::PushCameraShot(const FString& ShotFile, const FElysiumEntityHandle& Subject)
{
	if (!CameraDirector)
	{
		CameraDirector = MakePimpl<FElysiumCameraDirector>();
	}
	return CameraDirector->Push(EntityWorld.Get(), PlayerCamera(), ShotFile, Subject);
}

int32 AElysiumMapActor::PushCameraShotNamed(const FString& ShotFile, const FString& ShotName,
	const FElysiumEntityHandle& Subject, EElysiumShotExposure Exposure)
{
	if (!CameraDirector)
	{
		CameraDirector = MakePimpl<FElysiumCameraDirector>();
	}
	return CameraDirector->PushNamed(EntityWorld.Get(), PlayerCamera(), ShotFile, ShotName, Subject,
		Exposure);
}

int32 AElysiumMapActor::PushCameraShotValue(const FElysiumCameraShot& Shot)
{
	if (!CameraDirector)
	{
		CameraDirector = MakePimpl<FElysiumCameraDirector>();
	}
	return CameraDirector->PushValue(PlayerCamera(), Shot);
}

bool AElysiumMapActor::UpdateCameraShotValue(int32 ShotId, const FElysiumCameraShot& Shot)
{
	return CameraDirector
		? CameraDirector->UpdateValue(PlayerCamera(), ShotId, Shot)
		: false;
}

bool AElysiumMapActor::RestartCameraShot(int32 ShotId)
{
	return CameraDirector ? CameraDirector->RestartValue(PlayerCamera(), ShotId) : false;
}

bool AElysiumMapActor::PopCameraShot(int32 ShotId, float BlendOutSeconds)
{
	return CameraDirector ? CameraDirector->Pop(PlayerCamera(), ShotId, BlendOutSeconds) : false;
}

void AElysiumMapActor::SetEquippedCameraClass(int32 CameraClass)
{
	if (UElysiumCameraComponent* Camera = PlayerCamera())
	{
		Camera->SetEquippedCameraClass(CameraClass);
	}
}

FElysiumVoiceHandle AElysiumMapActor::Submit(FElysiumAudioRequest Request)
{
	Request.Owner.MapEpoch = MapEpoch;
	UElysiumAudioSubsystem* Audio = GetAudioSubsystem();
	return Audio ? Audio->Submit(Request) : FElysiumVoiceHandle::Invalid();
}

void AElysiumMapActor::Prefetch(const FElysiumAudioSource& Source)
{
	if (UElysiumAudioSubsystem* Audio = GetAudioSubsystem())
	{
		Audio->Prefetch(Source);
	}
}

void AElysiumMapActor::PauseVoice(FElysiumVoiceHandle Handle, bool bPaused)
{
	if (UElysiumAudioSubsystem* Audio = GetAudioSubsystem())
	{
		Audio->Pause(Handle, bPaused);
	}
}

void AElysiumMapActor::SeekVoice(FElysiumVoiceHandle Handle, float MediaOffsetSeconds)
{
	if (UElysiumAudioSubsystem* Audio = GetAudioSubsystem())
	{
		Audio->Seek(Handle, MediaOffsetSeconds);
	}
}

void AElysiumMapActor::SetVoicePitch(FElysiumVoiceHandle Handle, float Pitch)
{
	if (UElysiumAudioSubsystem* Audio = GetAudioSubsystem())
	{
		Audio->SetPitch(Handle, Pitch);
	}
}

void AElysiumMapActor::CancelAudioOwner(FElysiumAudioOwner AudioOwner, float FadeSeconds)
{
	AudioOwner.MapEpoch = MapEpoch;
	if (UElysiumAudioSubsystem* Audio = GetAudioSubsystem())
	{
		Audio->CancelOwner(AudioOwner, FadeSeconds);
	}
}

FElysiumAudioVoiceHandle AElysiumMapActor::PlayVoice(const FString& Rel, const FElysiumPlayParams& Params)
{
	FElysiumAudioRequest Request;
	Request.Source = FElysiumAudioSource::Path(Rel);
	Request.Owner.Kind = EElysiumAudioOwnerKind::GameplaySystem;
	Request.Owner.StableId = TEXT("legacy.map");
	Request.Gain = Params.Volume;
	Request.Pitch = Params.Pitch;
	Request.bLooping = Params.bLooping;
	Request.Placement.bSpatialized = Params.b3D;
	Request.Placement.Location = Params.Location;
	Request.Placement.AttachTo = Params.AttachTo;
	Request.AttenuationRadiusCm = Params.AttenuationRadiusCm;
	Request.AttenuationOverride = Params.AttenuationOverride;
	Request.FadeInSeconds = Params.FadeInSeconds;
	Request.StartOffsetSeconds = Params.StartTimeSeconds;
	return Submit(MoveTemp(Request));
}

bool AElysiumMapActor::ResolveSurfaceSounds(FName Surface, FElysiumSurfaceSounds& Out) const
{
	// The whole implementation is the baked-asset read; the load cache behind it is session-scoped
	// (the 63 surface entries are map-independent authored data) and shared with the water lane.
	return ElysiumSurfaceSoundTable::Resolve(Surface, Out);
}

// `OwnerHandle` rather than `Owner`: `AActor::Owner` is a member of this class, and a parameter
// spelled the same shadows it (C4458).
FElysiumAudioVoiceHandle AElysiumMapActor::PlayBodySound(const FElysiumEntityHandle& OwnerHandle,
	const FElysiumBodySound& Sound)
{
	if (Sound.Rel.IsEmpty())
	{
		return FElysiumAudioVoiceHandle::Invalid();
	}
	// The owner is what places the sound. No entity world, or a handle that no longer resolves,
	// means there is nothing to hang it on — retail's `EmitSound` takes an entindex and an entity
	// that has been removed emits nothing.
	const FElysiumEntity* Entity = EntityWorld ? EntityWorld->Resolve(OwnerHandle) : nullptr;
	if (Entity == nullptr)
	{
		return FElysiumAudioVoiceHandle::Invalid();
	}

	// **Channel replacement.** Source holds one voice per (entity, channel): a second `CHAN_BODY`
	// sound stops the first rather than layering on it, which is why two footsteps 40 ms apart
	// never overlap in retail. Stopped with no fade, because that is what taking a channel does.
	const FBodySoundKey Key{ OwnerHandle, Sound.Channel };
	if (const FElysiumAudioVoiceHandle* Previous = BodySoundVoices.Find(Key))
	{
		StopVoice(*Previous, 0.f);
	}

	FElysiumPlayParams Params;
	Params.b3D = true;
	Params.Volume = Sound.Volume;
	Params.Pitch = Sound.Pitch;
	// Attached to the body when the entity has one, so a walking NPC's step follows it for the
	// length of the wav; at the entity's origin otherwise (a logical entity with no primitive).
	Params.AttachTo = Entity->GetAttachBody();
	Params.Location = Entity->Origin;
	// The sound level IS the reach: full gain inside `snd_refdist`, -6 dB per doubling, silent
	// where the level reaches the 40 dB floor (`ElysiumSoundLevel::MakeAttenuation`). Built once
	// per level and shared: the curve is 66 keys, it is a pure function of the level, and a walking
	// cast asks two or three times a second per body.
	TSharedPtr<const FSoundAttenuationSettings>& Attenuation =
		BodySoundAttenuations.FindOrAdd(Sound.SoundLevelDb);
	if (!Attenuation.IsValid())
	{
		Attenuation = MakeShared<FSoundAttenuationSettings>(
			ElysiumSoundLevel::MakeAttenuation(Sound.SoundLevelDb));
	}
	Params.AttenuationOverride = Attenuation;

	const FElysiumAudioVoiceHandle Handle = PlayVoice(Sound.Rel, Params);
	if (!Handle.IsValid())
	{
		BodySoundVoices.Remove(Key);
		return Handle;
	}
	BodySoundVoices.Add(Key, Handle);

	// The ledger is bounded by (entities that have ever made a body sound) x (channels), and a map
	// reload on a surviving actor would carry the previous epoch's keys forward. Compact when it
	// grows past a body's worth of entries rather than scanning on every step: a handle whose voice
	// has finished can never be replaced again, and a stale-epoch one never plays by definition.
	if (BodySoundVoices.Num() > 64)
	{
		for (auto It = BodySoundVoices.CreateIterator(); It; ++It)
		{
			if (!(It.Key() == Key) && !IsVoicePlaying(It.Value()))
			{
				It.RemoveCurrent();
			}
		}
	}
	return Handle;
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

bool AElysiumMapActor::SampleGrappleRoot(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& Clip, FVector& OutPosition)
{
	return Bodies && Bodies->SampleGrappleRoot(Body, Stem, Clip, OutPosition);
}

bool AElysiumMapActor::SyncGrappleClip(USkeletalMeshComponent* Body, float PositionSeconds, bool bTerminal)
{
	return Bodies && Bodies->SyncGrappleClip(Body, PositionSeconds, bTerminal);
}

float AElysiumMapActor::SoundDurationSeconds(const FString& Rel) const
{
	UElysiumAudioSubsystem* Audio = GetAudioSubsystem();
	const FElysiumSoundAssetRow* Row = Audio ? Audio->Probe(Rel) : nullptr;
	return Row ? Row->DurationSeconds : 0.f;
}

void AElysiumMapActor::FadeInScheme(const FString& SchemeRel, const FVector& Anchor, float FadeSeconds)
{
	if (RuntimePhase != EElysiumMapRuntimePhase::Active)
	{
		if (SchemeManager)
		{
			SchemeManager->PrimeScheme(GetAudioSubsystem(), SchemeRel);
		}
		bHasDeferredSchemeFadeIn = true;
		DeferredSchemeRel = SchemeRel;
		DeferredSchemeAnchor = Anchor;
		DeferredSchemeFadeSeconds = FadeSeconds;
		return;
	}
	if (SchemeManager)
	{
		SchemeManager->FadeInScheme(GetAudioSubsystem(), SchemeRel, Anchor, FadeSeconds);
	}
}

void AElysiumMapActor::FadeOutScheme(const FString& SchemeRel, float FadeSeconds)
{
	if (bHasDeferredSchemeFadeIn && DeferredSchemeRel == SchemeRel)
	{
		bHasDeferredSchemeFadeIn = false;
		DeferredSchemeRel.Reset();
	}
	if (SchemeManager)
	{
		SchemeManager->FadeOutScheme(GetAudioSubsystem(), SchemeRel, FadeSeconds);
	}
}

FString AElysiumMapActor::ActiveSchemeRel() const
{
	return SchemeManager ? SchemeManager->ActiveSchemeRel() : FString();
}

float AElysiumMapActor::OutputLeadSeconds() const
{
	const UElysiumAudioSubsystem* Audio = GetAudioSubsystem();
	return Audio ? Audio->OutputLeadSeconds() : ElysiumAudioLatency::FallbackLeadSeconds;
}

// The substrate asked for a shot's mark; the trace, the surface character and the decal are all on this side of the seam.
// A world with no decal subsystem (a headless substrate run) answers false and marks nothing.
bool AElysiumMapActor::LayShotImpactDecal(const FVector& FromCm, const FVector& Direction,
	float RangeCm, int32 Variation)
{
	UWorld* World = GetWorld();
	UElysiumDecalSubsystem* Decals = World ? World->GetSubsystem<UElysiumDecalSubsystem>() : nullptr;
	if (!Decals)
	{
		return false;
	}
	FElysiumDecalRequest Request;
	FHitResult Hit;
	if (!ElysiumImpactDecals::BuildImpactRequest(World, FromCm, Direction, RangeCm, Variation,
		Request, Hit))
	{
		return false;
	}
	return Decals->Lay(Request) != nullptr;
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
