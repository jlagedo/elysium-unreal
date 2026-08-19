// AElysiumMapActor's embodiment seam — the IElysiumEmbodiment body/clip forwards onto
// UElysiumEntityBodies plus the three with logic of their own (the NPC motor factory and the
// attached one-shot effect), the scripted camera-shot channel, the IElysiumAudio voice and
// scheme forwards, and IElysiumTravel with the subsystem accessors behind all of them.

#include "ElysiumMapActor.h"

#include "ElysiumAudioSubsystem.h"       // the GI-scoped voice mixer every audio forward reaches
#include "ElysiumCameraComponent.h"      // the pawn camera the shot channel drives
#include "ElysiumContentPaths.h"         // FElysiumContentPaths::BakedParticleSystem — attached effects
#include "ElysiumEntityDefs.h"           // FElysiumEntityDef — BodyScaleFor's sky-scope read
#include "ElysiumMapSubsystem.h"         // the travel owner behind IElysiumTravel
#include "ElysiumPlayerBody.h"           // IElysiumPlayerBody — the pawn's camera accessor
#include "Audio/ElysiumSoundScheme.h"    // FElysiumSoundSchemeManager — the scheme fade forwards
#include "Map/ElysiumMapLog.h"
#include "Player/ElysiumCameraShots.h"   // FElysiumCameraDirector — the scripted-shot stack
#include "Visual/ElysiumAnimSubsystem.h" // the cinematic bank index
#include "Visual/ElysiumEntityBodies.h"  // the body factory every mesh forward lands on
#include "Visual/ElysiumMapVisuals.h"    // RegisterRuntimeBrush — runtime brush visuals join the look
#include "Visual/ElysiumNpcBody.h"       // AElysiumNpcBody — the NPC motor actor

#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "NiagaraFunctionLibrary.h"      // SpawnSystemAttached — the one-shot attached effect
#include "NiagaraSystem.h"

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

bool AElysiumMapActor::PlayNpcActivity(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& Activity, int32 Variant, bool bLoop, float* OutSeconds)
{
	return Bodies->PlayNpcActivity(Body, Stem, Activity, Variant, bLoop, OutSeconds);
}

bool AElysiumMapActor::ResolveNpcActivityClip(const FString& Stem, const FString& Activity,
	int32 Variant, FString& OutLabel, FString& OutAnimName, float& OutGroundSpeedCmPerSecond)
{
	return Bodies->ResolveNpcActivityClip(Stem, Activity, Variant, OutLabel, OutAnimName,
		OutGroundSpeedCmPerSecond);
}

bool AElysiumMapActor::ResolveNpcSequenceClip(const FString& Stem, const FString& ClipName,
	FString& OutAnimName, float& OutGroundSpeedCmPerSecond)
{
	return Bodies->ResolveNpcSequenceClip(Stem, ClipName, OutAnimName, OutGroundSpeedCmPerSecond);
}

bool AElysiumMapActor::HasNpcClip(const FString& Stem, const FString& ClipName)
{
	return Bodies->HasNpcClip(Stem, ClipName);
}

bool AElysiumMapActor::PlayNpcClip(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& ClipName, bool bLoop, float* OutSeconds)
{
	return Bodies->PlayNpcClip(Body, Stem, ClipName, bLoop, OutSeconds);
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
	// The anim-set model + the actor's bonerename root name a bank the offline split produced.
	UGameInstance* GI = GetGameInstance();
	UElysiumAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
	if (Anims == nullptr)
	{
		return false;
	}
	const FString Bank = Anims->GetIndex().CinematicBank(AnimSetModel, BoneRoot);
	if (Bank.IsEmpty())
	{
		return false;
	}
	return Bodies->PlayCinematicClip(Body, Stem, Bank, ClipName, bLoop, OutSeconds);
}

bool AElysiumMapActor::PreloadCinematicClip(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& AnimSetModel, const FString& BoneRoot, const FString& ClipName)
{
	UGameInstance* GI = GetGameInstance();
	UElysiumAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
	const FString Bank = Anims ? Anims->GetIndex().CinematicBank(AnimSetModel, BoneRoot) : FString();
	return Bodies && !Bank.IsEmpty()
		&& Bodies->PreloadCinematicClip(Body, Stem, Bank, ClipName);
}

bool AElysiumMapActor::PreloadCinematicClipForModel(const FString& Stem, bool bPlayerMaterial,
	const FString& AnimSetModel, const FString& BoneRoot, const FString& ClipName)
{
	UGameInstance* GI = GetGameInstance();
	UElysiumAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
	const FString Bank = Anims ? Anims->GetIndex().CinematicBank(AnimSetModel, BoneRoot) : FString();
	return Bodies && !Bank.IsEmpty()
		&& Bodies->PreloadCinematicClipForModel(Stem, bPlayerMaterial, Bank, ClipName);
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
