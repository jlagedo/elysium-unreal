// AElysiumMapActor's embodiment seam — the IElysiumEmbodiment body/clip forwards onto
// UElysiumEntityBodies plus the three with logic of their own (the NPC motor factory and the
// attached one-shot effect), the scripted camera-shot channel, the IElysiumAudio voice and
// scheme forwards, and IElysiumTravel with the subsystem accessors behind all of them.

#include "ElysiumMapActor.h"

#include "ElysiumAudioSubsystem.h"       // the GI-scoped voice mixer every audio forward reaches
#include "ElysiumCameraComponent.h"      // the pawn camera the shot channel drives
#include "ElysiumContentPaths.h"         // FElysiumContentPaths::BakedParticleSystem — attached effects
#include "ElysiumDecalSubsystem.h"       // R7.2: the shot's forward trace and its impact decal
#include "ElysiumEntityDefs.h"           // FElysiumEntityDef — BodyScaleFor's sky-scope read
#include "ElysiumMapSubsystem.h"         // the travel owner behind IElysiumTravel
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

#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "NiagaraFunctionLibrary.h"      // SpawnSystemAttached — the one-shot attached effect
#include "NiagaraSystem.h"

namespace
{
	FElysiumClipIdentity CinematicIdentity(UGameInstance* Game, const FString& BodyModel,
		const FString& AnimSetModel, const FString& Root, const FString& Label)
	{
		if (!Game) return {};
		if (BodyModel.StartsWith(TEXT("vtmb:model:")))
		{
			const auto* Native=Game->GetSubsystem<UElysiumNativeAnimationData>();
			const auto* Owner=Native?Native->CinematicBody(AnimSetModel,Root):nullptr;
			if (Owner) return {Owner->AssetId,Label,Owner->OwnerRoot};
			UE_LOG(LogElysium,Warning,TEXT("cinematic owner is absent or not prepared: %s [%s] for %s"),
				*AnimSetModel,*Root,*BodyModel);
			return {};
		}
		auto* Anims=Game->GetSubsystem<UElysiumAnimSubsystem>();
		return Anims?FElysiumClipIdentity(Anims->GetIndex().CinematicBank(AnimSetModel,Root),Label)
			:FElysiumClipIdentity();
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

FString AElysiumMapActor::AnimatedPropStemForModel(const FString& ModelPath) const
{
	return Bodies ? Bodies->AnimatedPropStemForModel(ModelPath) : FString();
}

FElysiumPlacedModelBody AElysiumMapActor::BuildPlacedModelBody(
	const FElysiumPlacedModelRequest& Request)
{
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
	return Bodies->BuildPropVisual(Stem, Location, Rotation, UniformScale);
}

EElysiumItemGroundModelState AElysiumMapActor::ItemGroundModelState(const FString& ModelPath)
{
	return Bodies ? Bodies->ItemGroundModelState(ModelPath)
		: EElysiumItemGroundModelState::Unavailable;
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
	Request.FadeInSeconds = Params.FadeInSeconds;
	Request.StartOffsetSeconds = Params.StartTimeSeconds;
	return Submit(MoveTemp(Request));
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

// R7.2 (`docs/project/seam_migration.md` -> "R7.2 Decals", owner call B). The substrate asked for
// a shot's mark; the trace, the surface character and the decal are all on this side of the seam.
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
