#include "Substrate/ElysiumPropLeaves.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSaveArchive.h"
#include "Substrate/ElysiumSignData.h"
#include "Substrate/ElysiumMoverSounds.h"
#include "ElysiumEntityQueryBox.h"
#include "ElysiumModelPhysicsInputs.h"
#include "ElysiumCollisionChannels.h"
#include "ElysiumUseIcons.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumModelPhysics.h"
#include "Substrate/ElysiumCollisionPairs.h"
#include "Visual/ElysiumPreparedPropModels.h"
#include "Map/ElysiumRetailMaskRecipe.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "GameFramework/Actor.h"

void FElysiumPropButton::Spawn()
{
	FElysiumProp::Spawn();
	MaxStates = FMath::Clamp(MaxStates, 0, 7);
	CurrentState = FMath::Clamp(CurrentState, 0, MaxStates);
	SetSkin(CurrentState);
	Flags |= 0x40000;
	EffectsWord |= 0xa0;
	SetOccludesSound(false);
	BuildStudioQuery(false); //10215750: model header success unconditionally produces OBB/group1.
}

void FElysiumPropButton::InputSetState(int32 ExternalState, const FElysiumEntityHandle& Activator)
{
	const int32 LastSettable = FMath::Max(0, MaxStates - 1);
	SetButtonState(FMath::Clamp(ExternalState - 1, 0, LastSettable), Activator);
}

void FElysiumPropButton::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	FElysiumProp::GetDebugState(Out);
	Out.Emplace(TEXT("Locked"), bLocked ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Button state"), FString::Printf(TEXT("%d / %d"), CurrentState, MaxStates));
}

void FElysiumPropButton::ButtonUse(const FElysiumEntityHandle& Activator)
{
	if (IsInert())
	{
		return;
	}
	if (bLocked)
	{
		static const FName OnPressedLocked(TEXT("OnPressedLocked"));
		FireOutput(OnPressedLocked, Activator);
		return;
	}

	static const FName OnPressed(TEXT("OnPressed"));
	FireOutput(OnPressed, Activator);
	SetButtonState(CurrentState >= MaxStates ? 0 : CurrentState + 1, Activator);
}

void FElysiumPropButton::SetButtonState(int32 NewState, const FElysiumEntityHandle& Activator)
{
	CurrentState = FMath::Clamp(NewState, 0, MaxStates);
	const FName Output(*FString::Printf(TEXT("OnSetState%d"), CurrentState + 1));
	FireOutput(Output, Activator);
	SetSkin(CurrentState);
}



void FElysiumPropSwitch::Spawn()
{
	FElysiumProp::Spawn();
	EffectsWord = 0xa0; //1020d850 assigns, rather than ORs
	BuildStudioQuery(false);
	bActivated = (SpawnFlags & 0x2000) != 0;
	bLocked = (SpawnFlags & 0x4000) != 0;
	SetSkin(bActivated ? 1 : 0);
	SequenceCycle = 0.f;
	AnimationPlaybackRate = 0.f;
	SequenceAnimTime = World ? static_cast<float>(World->NowSeconds()) : 0.f;
	if (const auto Ready = World ? ElysiumPreparedProps::ForOwner(World->GetOwnerActor()) : nullptr)
	{
		FString Error;
		if (const auto* Rest = Ready->Clip(ElysiumPreparedProps::ModelId(Model), CurrentAnimation, Error)) SequenceIndex = Rest->Index;
	}
}

FElysiumPropSwitch::~FElysiumPropSwitch() { RetireStudioQuery(); }
void FElysiumPropSwitch::ReleaseForRemoval() { RetireStudioQuery(); FElysiumProp::ReleaseForRemoval(); }
UPrimitiveComponent* FElysiumPropSwitch::GetAttachBody() const
{ return FElysiumProp::GetAttachBody(); }
UPrimitiveComponent* FElysiumPropSwitch::GetCollisionQueryBody() const { return StudioQuery; }
bool FElysiumPropSwitch::ShouldIgnoreCollision(FElysiumEntity* Other)
{ return World && World->CollisionPairs().ContainsEntities(Handle, Other ? Other->Handle : FElysiumEntityHandle::Invalid()); }

FElysiumPropButton::~FElysiumPropButton() { RetireStudioQuery(); }
UPrimitiveComponent* FElysiumPropButton::GetCollisionQueryBody() const { return StudioQuery; }
void FElysiumPropButton::ReleaseForRemoval() { RetireStudioQuery(); FElysiumProp::ReleaseForRemoval(); }
void FElysiumPropButton::RetireStudioQuery()
{
	if (World) World->UnregisterUseAnchor(Handle);
	if (StudioQuery) { StudioQuery->Entity = {}; StudioQuery->DestroyComponent(); StudioQuery = nullptr; }
}
void FElysiumPropButton::BuildStudioQuery(bool bUseSavedBounds)
{
	RetireStudioQuery();
	if (!World || IsDead()) return;

	if (bUseSavedBounds && GetSolid() != 3 && !(bHidden && ScriptSavedSolid == 3)) return;
	AActor* Outer = World->GetOwnerActor();
	if (!Outer) return;
	const UStaticMesh* StudioMesh = Visual ? Visual->GetStaticMesh() : nullptr;
	if (!StudioMesh) if (auto Prepared = ElysiumPreparedProps::ForOwner(Outer))
	{
		FString Error;
		StudioMesh = Prepared->StaticMesh(ElysiumPreparedProps::ModelId(Model), Error);
	}
	const auto* Inputs = UElysiumModelPhysicsInputs::Find(StudioMesh);
	if (!bUseSavedBounds && (!Inputs || !Inputs->bStudioBounds)) return; //Spawn-only header arm
	if (!bUseSavedBounds)
	{
		if (bSpawnQueryPending) SetSolid(3);
		UtilSetSize(FVector(Inputs->StudioMinsCm.X, -Inputs->StudioMaxsCm.Y, Inputs->StudioMinsCm.Z) / ElysiumMove::U,
			FVector(Inputs->StudioMaxsCm.X, -Inputs->StudioMinsCm.Y, Inputs->StudioMaxsCm.Z) / ElysiumMove::U);
		if (bSpawnQueryPending) CollisionGroup = 1;
		bSpawnQueryPending = false;
	}
	const FVector Minimum = FVector(CollMins.X, -CollMaxs.Y, CollMins.Z) * ElysiumMove::U;
	const FVector Maximum = FVector(CollMaxs.X, -CollMins.Y, CollMaxs.Z) * ElysiumMove::U;
	if (Minimum.ContainsNaN() || Maximum.ContainsNaN() || (Maximum - Minimum).GetMin() < 0.) return;
	StudioQuery = NewObject<UElysiumEntityQueryBox>(Outer);
	StudioQuery->Entity = Handle;
	StudioQuery->SetMobility(EComponentMobility::Movable);
	StudioQuery->SetCanEverAffectNavigation(false);
	StudioQuery->InitBoxExtent((Maximum - Minimum) * .5);
	const FQuat Rotation = ElysiumModelPhysics::SourceRotation(GetAbsAngles());
	StudioQuery->SetWorldLocationAndRotation(GetAbsOrigin() + Rotation.RotateVector((Minimum + Maximum) * .5), Rotation);
	StudioQuery->SetCollisionObjectType(ECC_WorldDynamic);
	StudioQuery->SetCollisionResponseToAllChannels(ECR_Ignore);
	StudioQuery->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	StudioQuery->SetCollisionResponseToChannel(ElysiumCollision::PlayerChannel, ECR_Block);
	StudioQuery->SetCollisionResponseToChannel(ElysiumCollision::SightChannel, ECR_Block);
	StudioQuery->SetCollisionResponseToChannel(ELYSIUM_USE_CHANNEL, ECR_Block);
	const bool bBlocks = Def && FCString::Atoi(*Def->Keys.FindRef(TEXT("blocks_traces"))) != 0;
	const bool bTransparent = Def && FCString::Atoi(*Def->Keys.FindRef(TEXT("npc_transparent"))) != 0;
	StudioQuery->SetMaskFilterOnBodyInstance(ElysiumRetailMask::PropBodyMaskBits(bBlocks, bTransparent));
	StudioQuery->SetGenerateOverlapEvents(false);
	StudioQuery->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	StudioQuery->RegisterComponent(); Outer->AddInstanceComponent(StudioQuery);
	World->RegisterPhysicalUseAnchor(StudioQuery, FElysiumProp::GetAttachBody(), Handle);
	GateStudioQuery();
}
void FElysiumPropButton::GateStudioQuery()
{
	if (StudioQuery) StudioQuery->SetCollisionEnabled(!IsInert() && GetSolid() == 3
		&& (RetailSolidFlags & 4) == 0 ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
}
void FElysiumPropButton::OnRuntimeTransformChanged()
{
	FElysiumProp::OnRuntimeTransformChanged();
	OnRuntimeCollisionChanged();
}
void FElysiumPropButton::OnRuntimeCollisionChanged()
{
	if (!StudioQuery) return;
	StudioQuery->SetBoxExtent((CollMaxs - CollMins) * (.5 * ElysiumMove::U));
	const FVector Center = FVector(CollMins.X + CollMaxs.X, -CollMaxs.Y - CollMins.Y, CollMins.Z + CollMaxs.Z) * (.5 * ElysiumMove::U);
	const FQuat Rotation = ElysiumModelPhysics::SourceRotation(GetAbsAngles());
	StudioQuery->SetWorldLocationAndRotation(GetAbsOrigin() + Rotation.RotateVector(Center), Rotation);
	GateStudioQuery();
}
void FElysiumPropButton::OnRuntimeModelChanged()
{ bSpawnQueryPending = false; bSavedQueryBounds = World && World->IsApplyingSnapshot(); RetireStudioQuery(); FElysiumProp::OnRuntimeModelChanged(); BuildStudioQuery(bSavedQueryBounds); }
void FElysiumPropButton::OnRuntimeModelAdmitted()
{ RetireStudioQuery(); FElysiumProp::OnRuntimeModelAdmitted(); BuildStudioQuery(bSavedQueryBounds); }
void FElysiumPropButton::OnDormancyChanged()
{ FElysiumProp::OnDormancyChanged(); if (IsDead()) RetireStudioQuery(); else { GateStudioQuery(); if (!IsInert() && World && StudioQuery) World->RegisterPhysicalUseAnchor(StudioQuery, FElysiumProp::GetAttachBody(), Handle); } }
void FElysiumPropButton::OnPostRestore(FElysiumEntityWorld&)
{ bSpawnQueryPending = false; bSavedQueryBounds = true; BuildStudioQuery(true); }

void FElysiumPropSwitch::RetireStudioQuery()
{
	if (World) World->UnregisterUseAnchor(Handle);
	if (StudioQuery) { StudioQuery->Entity = {}; StudioQuery->DestroyComponent(); StudioQuery = nullptr; }
}
void FElysiumPropSwitch::BuildStudioQuery(bool bUseSavedBounds)
{
	RetireStudioQuery();
	if (!World || IsDead()) return;
	if (!bUseSavedBounds && bSpawnQueryPending && (SpawnFlags & 0x8000) != 0) return;
	if (bUseSavedBounds && GetSolid() != 3 && !(bHidden && ScriptSavedSolid == 3)) return;
	AActor* Outer = World->GetOwnerActor();
	if (!Outer) return;
	const UStaticMesh* StudioMesh = Visual ? Visual->GetStaticMesh() : nullptr;
	if (!StudioMesh) if (auto Prepared = ElysiumPreparedProps::ForOwner(Outer))
	{
		FString Error;
		StudioMesh = Prepared->StaticMesh(ElysiumPreparedProps::ModelId(Model), Error);
	}
	const auto* Inputs = UElysiumModelPhysicsInputs::Find(StudioMesh);
	if (!bUseSavedBounds && (!Inputs || !Inputs->bStudioBounds)) return; //Spawn-only header arm
	if (!bUseSavedBounds)
	{
		if (bSpawnQueryPending) SetSolid(3);
		UtilSetSize(FVector(Inputs->StudioMinsCm.X, -Inputs->StudioMaxsCm.Y, Inputs->StudioMinsCm.Z) / ElysiumMove::U,
			FVector(Inputs->StudioMaxsCm.X, -Inputs->StudioMinsCm.Y, Inputs->StudioMaxsCm.Z) / ElysiumMove::U);
		if (bSpawnQueryPending) CollisionGroup = 1;
		bSpawnQueryPending = false;
	}
	const FVector Minimum = FVector(CollMins.X, -CollMaxs.Y, CollMins.Z) * ElysiumMove::U;
	const FVector Maximum = FVector(CollMaxs.X, -CollMins.Y, CollMaxs.Z) * ElysiumMove::U;
	if (Minimum.ContainsNaN() || Maximum.ContainsNaN() || (Maximum - Minimum).GetMin() < 0.) return;
	StudioQuery = NewObject<UElysiumEntityQueryBox>(Outer);
	StudioQuery->Entity = Handle;
	StudioQuery->SetMobility(EComponentMobility::Movable);
	StudioQuery->SetCanEverAffectNavigation(false);
	StudioQuery->InitBoxExtent((Maximum - Minimum) * .5);
	const FQuat Rotation = ElysiumModelPhysics::SourceRotation(GetAbsAngles());
	StudioQuery->SetWorldLocationAndRotation(GetAbsOrigin() + Rotation.RotateVector((Minimum + Maximum) * .5), Rotation);
	StudioQuery->SetCollisionObjectType(ECC_WorldDynamic);
	StudioQuery->SetCollisionResponseToAllChannels(ECR_Ignore);
	StudioQuery->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	StudioQuery->SetCollisionResponseToChannel(ElysiumCollision::PlayerChannel, ECR_Block);
	StudioQuery->SetCollisionResponseToChannel(ElysiumCollision::SightChannel, ECR_Block);
	StudioQuery->SetCollisionResponseToChannel(ELYSIUM_USE_CHANNEL, ECR_Block);
	const bool bBlocks = Def && FCString::Atoi(*Def->Keys.FindRef(TEXT("blocks_traces"))) != 0;
	const bool bTransparent = Def && FCString::Atoi(*Def->Keys.FindRef(TEXT("npc_transparent"))) != 0;
	StudioQuery->SetMaskFilterOnBodyInstance(ElysiumRetailMask::PropBodyMaskBits(bBlocks, bTransparent));
	StudioQuery->SetGenerateOverlapEvents(false);
	StudioQuery->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	StudioQuery->RegisterComponent(); Outer->AddInstanceComponent(StudioQuery);
	World->RegisterPhysicalUseAnchor(StudioQuery, FElysiumProp::GetAttachBody(), Handle);
	GateStudioQuery();
}
void FElysiumPropSwitch::GateStudioQuery()
{
	if (StudioQuery) StudioQuery->SetCollisionEnabled(!IsInert() && GetSolid() == 3
		&& (RetailSolidFlags & 4) == 0 ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
}
void FElysiumPropSwitch::OnRuntimeTransformChanged()
{
	FElysiumProp::OnRuntimeTransformChanged();
	OnRuntimeCollisionChanged();
}
void FElysiumPropSwitch::OnRuntimeCollisionChanged()
{
	if (!StudioQuery) return;
	StudioQuery->SetBoxExtent((CollMaxs - CollMins) * (.5 * ElysiumMove::U));
	const FVector Center = FVector(CollMins.X + CollMaxs.X, -CollMaxs.Y - CollMins.Y, CollMins.Z + CollMaxs.Z) * (.5 * ElysiumMove::U);
	const FQuat Rotation = ElysiumModelPhysics::SourceRotation(GetAbsAngles());
	StudioQuery->SetWorldLocationAndRotation(GetAbsOrigin() + Rotation.RotateVector(Center), Rotation);
	GateStudioQuery();
}
void FElysiumPropSwitch::OnRuntimeModelChanged()
{ bSpawnQueryPending = false; bSavedQueryBounds = World && World->IsApplyingSnapshot(); RetireStudioQuery(); FElysiumProp::OnRuntimeModelChanged(); BuildStudioQuery(bSavedQueryBounds); }
void FElysiumPropSwitch::OnRuntimeModelAdmitted()
{ RetireStudioQuery(); FElysiumProp::OnRuntimeModelAdmitted(); BuildStudioQuery(bSavedQueryBounds); }
void FElysiumPropSwitch::OnDormancyChanged()
{ FElysiumProp::OnDormancyChanged(); if (IsDead()) RetireStudioQuery(); else GateStudioQuery(); }
void FElysiumPropSwitch::OnPostRestore(FElysiumEntityWorld& InWorld)
{
	//1020e100: reset only on the bool supplied by1011a710; skin is an independent SAVE word.
	if (InWorld.IsLevelTransitionRestore())
	{
		if (ResetState == 2) bActivated = true;
		else if (ResetState == 1) bActivated = false;
	}
	SetCheckUntouch(false); //1008df10 ->100aa5a0
	bSpawnQueryPending = false; bSavedQueryBounds = true;
	BuildStudioQuery(true);
}
void FElysiumPropSwitch::Activate()
{
	FElysiumEntity::Activate();
	if (World && !LinkedSwitchName.IsEmpty())
		for (const auto& Candidate : World->Entities())
			if (Candidate && !Candidate->IsDead() && Candidate->TargetName.Equals(LinkedSwitchName,ESearchCase::IgnoreCase) && Candidate->AsPropSwitch())
			{ LinkedSwitch = Candidate->Handle; break; }
	const auto Ready = World ? ElysiumPreparedProps::ForOwner(World->GetOwnerActor()) : nullptr;
	const TCHAR* Names[] = { TEXT("activate"), TEXT("deactivate"), TEXT("idle_on"), TEXT("idle_off") };
	for (int32 Slot = 0; Slot < 4; ++Slot)
	{
		FString Error;
		const auto* Clip = Ready ? Ready->Clip(ElysiumPreparedProps::ModelId(Model), Names[Slot], Error) : nullptr;
		SwitchSequences[Slot] = Clip ? Clip->Index : INDEX_NONE;
	}
	PlayIdle(); //1020d9c0, after all reverse OnRestore
}

void FElysiumPropSwitch::Use(const FElysiumEntityHandle& Activator)
{
	if (IsInert())
	{
		return;
	}
	if (bLocked)
	{
		static const FName OnLockedUse(TEXT("OnLockedUse"));
		FireOutput(OnLockedUse, Activator);
		return;
	}
	static const FName OnUse(TEXT("OnUse"));
	FireOutput(OnUse, Activator);
	SetSwitchState(!bActivated, Activator, /*bMirrorLinked=*/true);
}

void FElysiumPropSwitch::InputActivate(const FElysiumEntityHandle& Activator)
{
	SetSwitchState(true, Activator, /*bMirrorLinked=*/false); //1020e0a0 does not run Use's linked arm
}

void FElysiumPropSwitch::InputDeactivate(const FElysiumEntityHandle& Activator)
{
	SetSwitchState(false, Activator, /*bMirrorLinked=*/false); //1020e0d0
}

void FElysiumPropSwitch::Think()
{
	const double Now = World ? World->NowSeconds() : 0.0;
	//1020dd70 reads the existing finished word BEFORE StudioFrameAdvance.
	if (bSequenceFinished)
	{
		if (SequenceIndex == SwitchSequences[0])
		{
			SetSkin(1); bTransitioning = false;
			FireOutput(TEXT("OnActivate"), OwnerEntity); //+10c owner EHANDLE, not the input activator
			PlaySwitchSequence(SwitchSequences[2], TEXT("idle_on"));
		}
		else if (SequenceIndex == SwitchSequences[1])
		{
			SetSkin(0); bTransitioning = false;
			FireOutput(TEXT("OnDeactivate"), OwnerEntity);
			PlaySwitchSequence(SwitchSequences[3], TEXT("idle_off"));
		}
		else if (!bAnimationLoop)
		{
			Flags |= 0x40000;
			ThinkCallback = NAME_None;
		}
		if (bSequenceFinished && !bAnimationLoop) return;
	}
	AdvanceSwitchSequence();
	NextThink = static_cast<float>(Now + .05);
}

void FElysiumPropSwitch::Serialize(FElysiumSaveArchive& Ar)
{
	FElysiumProp::Serialize(Ar);
	Ar << bLocked << bActivated << bTransitioning << TransitionActivator << SequenceIndex << LinkedSwitch;
	Ar << SequenceCycle << SequenceCycleRate << bSequenceFinished;
	Ar.Time(SequenceAnimTime); Ar.Time(SequencePrevAnimTime);
	for (int32& Index : SwitchSequences) Ar << Index;
	if (Ar.IsLoading() && World) TransitionActivator = World->RestoreHandle(TransitionActivator);
	if (Ar.IsLoading() && World) LinkedSwitch = World->RestoreHandle(LinkedSwitch);
}

void FElysiumPropSwitch::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	FElysiumProp::GetDebugState(Out);
	Out.Emplace(TEXT("Switch"), bActivated ? TEXT("activated") : TEXT("deactivated"));
	Out.Emplace(TEXT("Locked"), bLocked ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Linked switch"), LinkedSwitchName.IsEmpty() ? TEXT("(none)") : LinkedSwitchName);
	Out.Emplace(TEXT("Transition"), bTransitioning ? TEXT("playing") : TEXT("idle"));
}

void FElysiumPropSwitch::SetSwitchState(bool bNewActivated, const FElysiumEntityHandle& Activator, bool bMirrorLinked)
{
	if (bNewActivated == bActivated)
	{
		return;
	}
	//1020dce0 ->101f5950 ->IEngineSound#3 attenuation overload: channel2,1.0,1.25.
	// Source ATTN_TO_SNDLVL trunc(20/1.25+50)=66. Retain the owner/channel replacement seam.
	if (World) if (auto* Audio = World->Audio())
	{
		const auto& Sounds = ElysiumSoundGroups::Resolve(ElysiumSoundGroups::Switches, SoundGroup);
		if (const auto* Rel = Sounds.Find(bNewActivated ? FName(TEXT("on")) : FName(TEXT("off"))))
		{
			FElysiumBodySound Sound; Sound.Rel = *Rel; Sound.Channel = EElysiumSoundChannel::Voice;
			Sound.Volume = 1.f; Sound.SoundLevelDb = 66; Sound.Pitch = 1.f;
			Audio->PlayBodySound(Handle, Sound);
		}
	}
	bActivated = bNewActivated;
	TransitionActivator = Activator;
	bTransitioning = true;
	PlaySwitchSequence(SwitchSequences[bActivated ? 0 : 1], bActivated ? TEXT("activate") : TEXT("deactivate"));

	if (bMirrorLinked && World)
	{
		FElysiumEntity* Linked = World->Resolve(LinkedSwitch); //Use consumes Activate's saved EHANDLE
		if (Linked && Linked != this && Linked->AsPropSwitch())
		{
			static_cast<FElysiumPropSwitch*>(Linked)->SetSwitchState(
				bActivated, Activator, /*bMirrorLinked=*/false);
		}
	}

}

void FElysiumPropSwitch::PlayIdle()
{
	PlaySwitchSequence(SwitchSequences[bActivated ? 2 : 3], bActivated ? TEXT("idle_on") : TEXT("idle_off"));
}

void FElysiumPropSwitch::PlaySwitchSequence(int32 Index, const FString& Clip)
{
	bool bLoops = false;
	auto* Embodiment = World ? World->Embodiment() : nullptr;
	SequenceIndex = Index >= 0 ? Index : 0;
	const auto Ready = World ? ElysiumPreparedProps::ForOwner(World->GetOwnerActor()) : nullptr;
	FString Error;
	const auto* ModelRow = Ready ? Ready->Model(ElysiumPreparedProps::ModelId(Model), Error) : nullptr;
	const auto* SourceClip = ModelRow ? ModelRow->Clips.FindByPredicate([&](const auto& Candidate) { return Candidate.Index == SequenceIndex; }) : nullptr;
	if (Index >= 0)
	{
		bLoops = SourceClip && (SourceClip->Flags & 1) != 0; //STUDIO_LOOPING source header
		PlayAnimation(Clip, bLoops);
		AnimLengthSeconds = SourceClip && SourceClip->Fps > 0. ? FMath::Max(0, SourceClip->Frames - 1) / SourceClip->Fps : 0.;
		SequenceCycleRate = AnimLengthSeconds > 0. ? static_cast<float>(1. / AnimLengthSeconds) : 10.f;
		bAnimationLoop = bLoops;
		SequenceCycle = 0.f; bSequenceFinished = false;
		SequenceAnimTime = World ? static_cast<float>(World->NowSeconds()) : 0.f;
		AnimationPlaybackRate = 1.f; FrozenAnimationPhase = 0.f;
		Flags &= ~0x40000; //1020df00 valid sequence ResetSequenceInfo
	}
	else
	{
		if (SourceClip && AnimatedVisual && Embodiment)
		{
			const float Rate = AnimationPlaybackRate;
			if (PlayAnimation(SourceClip->Label, bAnimationLoop))
			{
				FrozenAnimationPhase = static_cast<float>(SequenceCycle * AnimLengthSeconds);
				AnimationPlaybackRate = Rate;
				AnimStartTime = Rate != 0.f ? World->NowSeconds() - FrozenAnimationPhase / Rate : World->NowSeconds();
				Embodiment->SeekCinematicClip(AnimatedVisual, FrozenAnimationPhase);
				Embodiment->SetBodySequencePlaybackRate(AnimatedVisual, Rate);
				AnimationEndTime = Rate > 0.f && !bAnimationLoop ? World->NowSeconds() + (AnimLengthSeconds - FrozenAnimationPhase) / Rate : 0.;
			}
		}
		AnimLengthSeconds = SourceClip && SourceClip->Fps > 0. ? FMath::Max(0, SourceClip->Frames - 1) / SourceClip->Fps : 0.;
	}
	ForceTransmit(); //1020df00, both idle arms
	ThinkCallback = TEXT("EntityThink");
	if (World) NextThink = static_cast<float>(World->NowSeconds() + .05);
}

void FElysiumPropSwitch::AdvanceSwitchSequence()
{
	//1008f120, the same source-cycle producer used by the NPC base; no engine animation clock.
	const float Now = World ? static_cast<float>(World->NowSeconds()) : 0.f;
	if (SequencePrevAnimTime == 0.f) SequenceAnimTime = SequencePrevAnimTime = Now;
	const float Interval = (.1f + Now) - SequenceAnimTime;
	if (!(static_cast<double>(Interval) > .001)) return;
	SequencePrevAnimTime = SequenceAnimTime;
	const float Cycle = SequenceCycle + SequenceCycleRate * AnimationPlaybackRate * Interval;
	SequenceCycle = Cycle;
	SequenceAnimTime += Interval;
	if (Cycle < 0.f || Cycle >= 1.f)
	{
		SequenceCycle = bAnimationLoop ? Cycle - static_cast<float>(static_cast<int32>(Cycle)) : (Cycle < 0.f ? 0.f : 1.f);
		bSequenceFinished = true; //loop wrap also sets this latch; only ResetSequenceInfo clears it
	}
	FrozenAnimationPhase = static_cast<float>(SequenceCycle * AnimLengthSeconds);
	if (World && AnimatedVisual && World->Embodiment())
	{
		World->Embodiment()->SeekCinematicClip(AnimatedVisual, FrozenAnimationPhase);
		World->Embodiment()->SetBodySequencePlaybackRate(AnimatedVisual, AnimationPlaybackRate);
	}
}

void FElysiumPropSwitch::SetSequencePlaybackRate(float Rate)
{
	AnimationPlaybackRate = Rate;
	if (World && AnimatedVisual && World->Embodiment()) World->Embodiment()->SetBodySequencePlaybackRate(AnimatedVisual, Rate);
}



// CPropSign::Spawn vampire.dll10211b40: header-success MOVETYPE_NONE, SOLID_OBB, size, group1, relink.
void FElysiumPropSign::Spawn() { FElysiumProp::Spawn(); BuildStudioQuery(false); }
FElysiumPropSign::~FElysiumPropSign() { RetireStudioQuery(); }
UPrimitiveComponent* FElysiumPropSign::GetCollisionQueryBody() const { return StudioQuery; }
void FElysiumPropSign::ReleaseForRemoval() { RetireStudioQuery(); FElysiumProp::ReleaseForRemoval(); }
void FElysiumPropSign::RetireStudioQuery()
{
	if (World) World->UnregisterUseAnchor(Handle);
	if (StudioQuery) { StudioQuery->Entity = {}; StudioQuery->DestroyComponent(); StudioQuery = nullptr; }
}
void FElysiumPropSign::BuildStudioQuery(bool bUseSavedBounds)
{
	RetireStudioQuery();
	if (!World || IsDead()) return;

	if (bUseSavedBounds && GetSolid() != 3 && !(bHidden && ScriptSavedSolid == 3)) return;
	AActor* Outer = World->GetOwnerActor();
	if (!Outer) return;
	const UStaticMesh* StudioMesh = Visual ? Visual->GetStaticMesh() : nullptr;
	if (!StudioMesh) if (auto Prepared = ElysiumPreparedProps::ForOwner(Outer))
	{
		FString Error;
		StudioMesh = Prepared->StaticMesh(ElysiumPreparedProps::ModelId(Model), Error);
	}
	const auto* Inputs = UElysiumModelPhysicsInputs::Find(StudioMesh);
	if (!bUseSavedBounds && (!Inputs || !Inputs->bStudioBounds)) return; //Spawn-only header arm
	if (!bUseSavedBounds)
	{
		if (bSpawnQueryPending) { SetMoveType(0,0); SetSolid(3); } //10211b40 header-success arm
		UtilSetSize(FVector(Inputs->StudioMinsCm.X, -Inputs->StudioMaxsCm.Y, Inputs->StudioMinsCm.Z) / ElysiumMove::U,
			FVector(Inputs->StudioMaxsCm.X, -Inputs->StudioMinsCm.Y, Inputs->StudioMaxsCm.Z) / ElysiumMove::U);
		if (bSpawnQueryPending) CollisionGroup = 1;
		bSpawnQueryPending = false;
	}
	const FVector Minimum = FVector(CollMins.X, -CollMaxs.Y, CollMins.Z) * ElysiumMove::U;
	const FVector Maximum = FVector(CollMaxs.X, -CollMins.Y, CollMaxs.Z) * ElysiumMove::U;
	if (Minimum.ContainsNaN() || Maximum.ContainsNaN() || (Maximum - Minimum).GetMin() < 0.) return;
	StudioQuery = NewObject<UElysiumEntityQueryBox>(Outer);
	StudioQuery->Entity = Handle;
	StudioQuery->SetMobility(EComponentMobility::Movable);
	StudioQuery->SetCanEverAffectNavigation(false);
	StudioQuery->InitBoxExtent((Maximum - Minimum) * .5);
	const FQuat Rotation = ElysiumModelPhysics::SourceRotation(GetAbsAngles());
	StudioQuery->SetWorldLocationAndRotation(GetAbsOrigin() + Rotation.RotateVector((Minimum + Maximum) * .5), Rotation);
	StudioQuery->SetCollisionObjectType(ECC_WorldDynamic);
	StudioQuery->SetCollisionResponseToAllChannels(ECR_Ignore);
	StudioQuery->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	StudioQuery->SetCollisionResponseToChannel(ElysiumCollision::PlayerChannel, ECR_Block);
	StudioQuery->SetCollisionResponseToChannel(ElysiumCollision::SightChannel, ECR_Block);
	StudioQuery->SetCollisionResponseToChannel(ELYSIUM_USE_CHANNEL, ECR_Block);
	const bool bBlocks = Def && FCString::Atoi(*Def->Keys.FindRef(TEXT("blocks_traces"))) != 0;
	const bool bTransparent = Def && FCString::Atoi(*Def->Keys.FindRef(TEXT("npc_transparent"))) != 0;
	StudioQuery->SetMaskFilterOnBodyInstance(ElysiumRetailMask::PropBodyMaskBits(bBlocks, bTransparent));
	StudioQuery->SetGenerateOverlapEvents(false);
	StudioQuery->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	StudioQuery->RegisterComponent(); Outer->AddInstanceComponent(StudioQuery);
	World->RegisterPhysicalUseAnchor(StudioQuery, FElysiumProp::GetAttachBody(), Handle);
	GateStudioQuery();
}
void FElysiumPropSign::GateStudioQuery()
{
	if (StudioQuery) StudioQuery->SetCollisionEnabled(!IsInert() && GetSolid() == 3
		&& (RetailSolidFlags & 4) == 0 ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
}
void FElysiumPropSign::OnRuntimeTransformChanged()
{
	FElysiumProp::OnRuntimeTransformChanged();
	OnRuntimeCollisionChanged();
}
void FElysiumPropSign::OnRuntimeCollisionChanged()
{
	if (!StudioQuery) return;
	StudioQuery->SetBoxExtent((CollMaxs - CollMins) * (.5 * ElysiumMove::U));
	const FVector Center = FVector(CollMins.X + CollMaxs.X, -CollMaxs.Y - CollMins.Y, CollMins.Z + CollMaxs.Z) * (.5 * ElysiumMove::U);
	const FQuat Rotation = ElysiumModelPhysics::SourceRotation(GetAbsAngles());
	StudioQuery->SetWorldLocationAndRotation(GetAbsOrigin() + Rotation.RotateVector(Center), Rotation);
	GateStudioQuery();
}
void FElysiumPropSign::OnRuntimeModelChanged()
{ bSpawnQueryPending = false; bSavedQueryBounds = World && World->IsApplyingSnapshot(); RetireStudioQuery(); FElysiumProp::OnRuntimeModelChanged(); BuildStudioQuery(bSavedQueryBounds); }
void FElysiumPropSign::OnRuntimeModelAdmitted()
{ RetireStudioQuery(); FElysiumProp::OnRuntimeModelAdmitted(); BuildStudioQuery(bSavedQueryBounds); }
void FElysiumPropSign::OnPostRestore(FElysiumEntityWorld&)
{ bSpawnQueryPending = false; bSavedQueryBounds = true; BuildStudioQuery(true); }

FElysiumUseBeginResult FElysiumPropSign::BeginPlayerUse(const FElysiumUseContext& Context)
{
	if (!World || Context.Activator != World->PlayerHandle() || IsInert())
	{
		return FElysiumUseBeginResult::Refused(EElysiumUseOutcome::Unavailable);
	}
	const FElysiumEntityHandle Existing = World->GetOpenSign();
	if (Existing.IsSet() && Existing != Handle)
	{
		return FElysiumUseBeginResult::Refused(EElysiumUseOutcome::Busy);
	}
	if (!EnsurePanel())
	{
		return FElysiumUseBeginResult::Refused(EElysiumUseOutcome::Unavailable);
	}
	World->OpenSign(Handle, Panel, 0.0f);
	static const FName OnUseBegin(TEXT("OnUseBegin"));
	static const FName OnReadBegin(TEXT("OnReadBegin"));
	FireOutput(OnUseBegin, Context.Activator);
	FireOutput(OnReadBegin, Context.Activator);
	return FElysiumUseBeginResult::Started(EElysiumUseSessionKind::Explicit);
}

void FElysiumPropSign::EndPlayerUse(const FElysiumUseContext& Context, EElysiumUseEndReason)
{
	if (World && World->GetOpenSign() == Handle)
	{
		World->CloseSign(/*bSilent=*/true);
	}
	static const FName OnUseEnd(TEXT("OnUseEnd"));
	static const FName OnReadEnd(TEXT("OnReadEnd"));
	FireOutput(OnUseEnd, Context.Activator);
	FireOutput(OnReadEnd, Context.Activator);
}

void FElysiumPropSign::OnDormancyChanged()
{
	FElysiumProp::OnDormancyChanged();
	if (IsDead()) RetireStudioQuery();
	else { GateStudioQuery(); if (!IsInert() && World && StudioQuery) World->RegisterPhysicalUseAnchor(StudioQuery, FElysiumProp::GetAttachBody(), Handle); }
	if (IsInert() && World && World->GetOpenSign() == Handle)
	{
		World->EndPlayerUseSession(Handle, EElysiumUseEndReason::TargetInvalid);
	}
}

void FElysiumPropSign::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	FElysiumProp::GetDebugState(Out);
	Out.Emplace(TEXT("Definition"), DefinitionFile.IsEmpty() ? TEXT("(none)") : DefinitionFile);
	Out.Emplace(TEXT("On screen"), World && World->GetOpenSign() == Handle ? TEXT("YES") : TEXT("no"));
}

bool FElysiumPropSign::EnsurePanel()
{
	if (Panel.IsValid())
	{
		return true;
	}
	if (!World || DefinitionFile.IsEmpty())
	{
		UE_LOG(LogElysiumProp, Warning,
			TEXT("%s cannot open prop_sign: %s"), *DebugString(),
			!World ? TEXT("entity world is unavailable") : TEXT("definition_file is empty"));
		return false;
	}
	TSharedPtr<FElysiumSignData> Parsed = MakeShared<FElysiumSignData>();
	if (!FElysiumSignData::Load(DefinitionFile, *Parsed, World))
	{
		UE_LOG(LogElysiumProp, Warning, TEXT("%s: could not load prop_sign data '%s'"),
			*DebugString(), *DefinitionFile);
		return false;
	}
	Panel = Parsed;
	return true;
}
