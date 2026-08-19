#include "Visual/ElysiumEntityBodies.h"

#include "ElysiumContentPaths.h"
#include "Visual/ElysiumBipedAnimInstance.h"
#include "Visual/ElysiumAnimSubsystem.h"
#include "Visual/ElysiumEntityBodiesLog.h"
#include "Visual/ElysiumNpcBody.h"
#include "Visual/ElysiumNpcVisual.h"
#include "ElysiumMapActor.h"
#include "ElysiumStanceTypes.h"
#include "Substrate/ElysiumDisposition.h"
#include "Substrate/ElysiumRulebookSubsystem.h"

#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "Misc/App.h"

DEFINE_LOG_CATEGORY(LogElysiumBodies);

namespace
{
	// Resolved once and cached, including the failure: a missing generated package is a build-step
	// problem rather than something to retry per body.
	//
	// The cache outlives every map epoch, so the class it holds has to as well: it is **rooted**.
	// A `-game` run's map travel collects garbage with `GARBAGE_COLLECTION_KEEPFLAGS`, which is
	// `RF_NoFlags` outside the editor — the loaded Blueprint's `RF_Standalone` does not survive it,
	// and once the previous map's body component is gone nothing else references the generated
	// class. Without the root, the second map to seat a player body hands `SetAnimInstanceClass` a
	// freed class and `UAnimInstance::InitializeAnimation` faults reading it.
	UClass* BodyGraphClass()
	{
		static bool bResolved = false;
		static UClass* Cached = nullptr;
		if (!bResolved)
		{
			bResolved = true;
			const FString Path = FElysiumContentPaths::PlayerAnimBlueprintClass();
			Cached = LoadClass<UAnimInstance>(nullptr, *Path);
			if (Cached != nullptr)
			{
				Cached->AddToRoot();
			}
			else
			{
				// Named rather than substituted, the same rule `ElysiumNpcVisual::LoadMesh` follows:
				// the body still stands and the line says what to run. Without the graph there is no
				// state machine and no montage slot, so a body poses only what its own clip player is
				// given — the standing idle, with no locomotion behind it.
				UE_LOG(LogElysiumBodies, Warning,
					TEXT("animation graph '%s' is not on the mount -- every body falls back to the "
					     "native instance and poses clips only. Run `uv run elysium export bundle policy`."),
					*Path);
			}
		}
		return Cached;
	}
}

UElysiumEntityBodies::UElysiumEntityBodies()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UElysiumAnimSubsystem* UElysiumEntityBodies::GetAnims() const
{
	const AActor* Owner = GetOwner();
	UGameInstance* GI = Owner ? Owner->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
}

FString ElysiumEntityAnimation::NpcVisualCacheKey(const FString& Stem, bool bPlayerMaterial)
{
	return Stem.ToLower() + (bPlayerMaterial ? TEXT("|player") : TEXT("|npc"));
}

FString ElysiumEntityAnimation::NpcClipCacheKey(const FString& Stem, const FString& ClipName)
{
	return Stem + TEXT("|") + ClipName;
}

FString ElysiumEntityAnimation::CinematicClipCacheKey(
	const FString& Stem, const FString& BankStem, const FString& ClipName)
{
	return Stem + TEXT("|") + BankStem + TEXT("|") + ClipName;
}

FString UElysiumEntityBodies::NpcVisualKeyForMesh(const FString& Stem, const USkeletalMesh* Mesh) const
{
	const FString PlayerKey = ElysiumEntityAnimation::NpcVisualCacheKey(Stem, true);
	if (const TObjectPtr<USkeletalMesh>* PlayerMesh = NpcMeshCache.Find(PlayerKey))
	{
		if (PlayerMesh->Get() == Mesh)
		{
			return PlayerKey;
		}
	}
	return ElysiumEntityAnimation::NpcVisualCacheKey(Stem, false);
}

USkeletalMesh* UElysiumEntityBodies::ResolveNpcMesh(const FString& Stem, bool bPlayerMaterial)
{
	if (Stem.IsEmpty())
	{
		return nullptr;
	}
	const FString VisualKey = ElysiumEntityAnimation::NpcVisualCacheKey(Stem, bPlayerMaterial);
	if (const TObjectPtr<USkeletalMesh>* Cached = NpcMeshCache.Find(VisualKey))
	{
		return Cached->Get();
	}

	// The eye sections carry M_Eyes from the bake, which is where the eye sidecar's material names
	// are read; nothing about the material is decided here any more.
	FString Error;
	USkeletalMesh* Mesh = ElysiumNpcVisual::LoadMesh(Stem, Error, bPlayerMaterial);
	if (Mesh == nullptr)
	{
		UE_LOG(LogElysiumBodies, Warning, TEXT("ResolveNpcMesh '%s': %s"), *Stem, *Error);
		return nullptr;
	}
	NpcMeshCache.Add(VisualKey, Mesh);
	return Mesh;
}

UAnimSequence* UElysiumEntityBodies::ResolveNpcClip(const FString& Stem, const FString& ClipName,
	USkeletalMesh* TargetMesh)
{
	if (Stem.IsEmpty() || ClipName.IsEmpty())
	{
		return nullptr;
	}
	// A material permutation creates a distinct runtime mesh and USkeleton even when both meshes
	// came from the same GLB. Keep its animation cache identity separate from the ordinary NPC.
	const FString VisualKey = NpcVisualKeyForMesh(Stem, TargetMesh);

	UElysiumAnimSubsystem* Anims = GetAnims();

	// CAP7.3 — key on the animation the label will actually load, not the label. A blend-grid label
	// selects a cell from the pose parameters, so a cache keyed on `walk` would pin whichever cell was
	// resolved first and no parameter could ever move it again.
	const FString Key = ElysiumEntityAnimation::NpcClipCacheKey(VisualKey,
		Anims != nullptr ? Anims->ResolveClipAnimName(Stem, ClipName) : ClipName);
	if (const TObjectPtr<UAnimSequence>* Cached = NpcAnimCache.Find(Key))
	{
		return Cached->Get();
	}

	UAnimSequence* Anim = nullptr;
	const TObjectPtr<USkeletalMesh>* Mesh = NpcMeshCache.Find(VisualKey);
	if (Anims != nullptr && Mesh != nullptr && *Mesh != nullptr)
	{
		FString Error;
		Anim = Anims->ResolveClip(Stem, ClipName, Mesh->Get(), Error);
		if (Anim == nullptr)
		{
			UE_LOG(LogElysiumBodies, Warning, TEXT("npc '%s' clip '%s': %s"), *Stem, *ClipName, *Error);
		}
	}
	NpcAnimCache.Add(Key, Anim);
	return Anim;
}

float UElysiumEntityBodies::ClipFadeSeconds(const FString& Stem, const FString& ClipName) const
{
	UElysiumAnimSubsystem* Anims = GetAnims();
	const FElysiumNpcClipSet* Set = Anims ? Anims->GetClipSet(Stem) : nullptr;
	const FElysiumNpcClip* Clip = Set ? Set->Find(ClipName) : nullptr;
	// A clip the vocabulary does not carry — a bank clip reached by name, a prop, the green room —
	// transitions on the shipped default rather than snapping, which is what 5,762 of the 5,836
	// shipped sequences authored anyway.
	return Clip != nullptr ? Clip->FadeSeconds() : UElysiumBodyAnimInstance::DefaultBlendSeconds;
}

uint32 UElysiumEntityBodies::SubmitBodyAnimRequest(USkeletalMeshComponent* Body,
	const FElysiumAnimationRequest& Request)
{
	if (Body == nullptr)
	{
		return 0;
	}
	// An NPC visual hangs off its motor's root; the player's hangs off the pawn and routes through
	// the map actor's own driver. Anything else — a green-room stand, a preview body, a prop — has
	// no driver publishing locomotion against it, so there is nothing to arbitrate and no claim to
	// hold: an explicitly optional absence, not a failure.
	if (AElysiumNpcBody* Motor = Cast<AElysiumNpcBody>(Body->GetAttachParentActor()))
	{
		return Motor->SubmitAnimRequest(Request);
	}
	if (AElysiumMapActor* Map = Cast<AElysiumMapActor>(GetOwner()); Map != nullptr
		&& Map->IsPlayerVisual(Body))
	{
		return Map->SubmitPlayerAnimRequest(Request);
	}
	return 0;
}

bool UElysiumEntityBodies::ReleaseBodyAnimRequest(USkeletalMeshComponent* Body, uint32 Handle)
{
	if (Body == nullptr || Handle == 0)
	{
		return false;
	}
	if (AElysiumNpcBody* Motor = Cast<AElysiumNpcBody>(Body->GetAttachParentActor()))
	{
		return Motor->ReleaseAnimRequest(Handle);
	}
	if (AElysiumMapActor* Map = Cast<AElysiumMapActor>(GetOwner()); Map != nullptr
		&& Map->IsPlayerVisual(Body))
	{
		return Map->ReleasePlayerAnimRequest(Handle);
	}
	return false;
}

bool UElysiumEntityBodies::PlayNpcClip(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& ClipName, bool bLoop, float* OutSeconds)
{
	UAnimSequence* Anim = Body
		? ResolveNpcClip(Stem, ClipName, Body->GetSkeletalMeshAsset())
		: nullptr;
	if (Anim == nullptr)
	{
		return false;
	}
	if (OutSeconds != nullptr)
	{
		*OutSeconds = Anim->GetPlayLength();
	}
	// The shared one-shot seam rather than the clip API below it: a graph-backed body answers it over
	// a montage slot and a body with no compiled graph over its clip player, and a caller holding an
	// `IElysiumEmbodiment` body has no way to know which it has. Falling through to `PlayAnimation`
	// instead would switch the component to single-node mode and destroy the anim graph for the rest
	// of the map.
	UElysiumBodyAnimInstance* Inst = Cast<UElysiumBodyAnimInstance>(Body->GetAnimInstance());
	if (Inst == nullptr)
	{
		UE_LOG(LogElysiumBodies, Warning,
			TEXT("npc '%s' clip '%s': this body carries no Elysium animation host, so nothing can "
			     "play it"), *Stem, *ClipName);
		return false;
	}
	// The one-shot's answer is the difference between a clip that is playing and a body standing in
	// the reference pose: on a graph-backed body the slot's source is the state machine, so a refused
	// montage poses whatever that machine holds -- which for a body handed no selection is the bind
	// pose, advancing nothing. Discarding the answer made that a silent T-pose.
	if (!Inst->PlayOneShot(Anim, bLoop, ClipFadeSeconds(Stem, ClipName)))
	{
		UE_LOG(LogElysiumBodies, Warning,
			TEXT("npc '%s' clip '%s' (loop=%d, %.3fs): the animation host refused to play it, so the "
			     "body keeps posing whatever it already held"),
			*Stem, *ClipName, bLoop ? 1 : 0, Anim->GetPlayLength());
		return false;
	}
	UE_LOG(LogElysiumBodies, Verbose, TEXT("clip '%s' on %s (loop=%d, %.3fs)"),
		*ClipName, *Stem, bLoop ? 1 : 0, Anim->GetPlayLength());

	// LIFE4 — the clip's claim on the base channel. Everything this funnel arms today — the ambient
	// schedule's stances and fidgets, dialogue line clips, scripted beats still on the adapter — is
	// the ambient band: it holds the pose against a standing body's every-tick publish and yields
	// the moment the body travels, which is the priority-table row the interim while-locomoting
	// rule became. A looping clip holds until replaced or outranked; a one-shot's claim runs its
	// clip length so an armer that never returns cannot park the channel. LIFE5 producers that
	// deserve a higher band submit their own claims through the same slot.
	FElysiumAnimationRequest Claim;
	Claim.Source = EElysiumAnimSource::Npc;
	Claim.Channel = EElysiumAnimChannel::Base;
	Claim.Priority = EElysiumAnimPriority::Ambient;
	Claim.Label = ClipName;
	Claim.HoldSeconds = bLoop ? 0.0f : Anim->GetPlayLength();
	SubmitBodyAnimRequest(Body, Claim);

	Body->TickAnimation(0.0f, false);
	Body->RefreshBoneTransforms();
	Body->SetVisibility(true, true);
	return true;
}

void UElysiumEntityBodies::ForgetNpcVisuals()
{
	// All three together. The clip cache is keyed off the visual key and every sequence in it is
	// bound to the mesh that key names, so keeping it across a path change would hand the new body
	// sequences bound to the old body's skeleton -- which is a worse failure than the one this
	// exists to fix, because it looks like a rig bug rather than a stale cache.
	const int32 Meshes = NpcMeshCache.Num();
	NpcMeshCache.Empty();
	NpcAnimCache.Empty();
	UE_LOG(LogElysiumBodies, Log, TEXT("forgot %d cached NPC visual(s); the next build re-resolves"),
		Meshes);
}

bool UElysiumEntityBodies::PreloadNpcClip(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& ClipName)
{
	return Body && ResolveNpcClip(Stem, ClipName, Body->GetSkeletalMeshAsset()) != nullptr;
}

bool UElysiumEntityBodies::PreloadNpcClipForModel(const FString& Stem, bool bPlayerMaterial,
	const FString& ClipName)
{
	USkeletalMesh* Mesh = ResolveNpcMesh(Stem, bPlayerMaterial);
	return Mesh && ResolveNpcClip(Stem, ClipName, Mesh) != nullptr;
}

UAnimSequence* UElysiumEntityBodies::ResolveCinematicClip(USkeletalMesh* Mesh, const FString& Stem,
	const FString& BankStem, const FString& ClipName)
{
	if (Mesh == nullptr || BankStem.IsEmpty() || ClipName.IsEmpty())
	{
		return nullptr;
	}
	UElysiumAnimSubsystem* Anims = GetAnims();
	if (Anims == nullptr)
	{
		return nullptr;
	}

	const FString VisualKey = NpcVisualKeyForMesh(Stem, Mesh);
	const FString Key = ElysiumEntityAnimation::CinematicClipCacheKey(
		VisualKey, BankStem, Anims->ResolveGridClip(BankStem, ClipName));
	if (const TObjectPtr<UAnimSequence>* Found = NpcAnimCache.Find(Key))
	{
		return Found->Get();
	}

	FString Error;
	UAnimSequence* Anim = Anims->ResolveClipFromBank(BankStem, ClipName, Mesh, Error);
	if (Anim == nullptr)
	{
		UE_LOG(LogElysiumBodies, Warning, TEXT("cinematic bank '%s' clip '%s': %s"),
			*BankStem, *ClipName, *Error);
	}
	NpcAnimCache.Add(Key, Anim);
	return Anim;
}

bool UElysiumEntityBodies::PlayCinematicClip(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& BankStem, const FString& ClipName, bool bLoop, float* OutSeconds)
{
	UAnimSequence* Anim = Body
		? ResolveCinematicClip(Body->GetSkeletalMeshAsset(), Stem, BankStem, ClipName)
		: nullptr;
	if (Anim == nullptr)
	{
		return false;
	}
	if (OutSeconds != nullptr)
	{
		*OutSeconds = Anim->GetPlayLength();
	}
	if (UElysiumBipedAnimInstance* Inst = Cast<UElysiumBipedAnimInstance>(Body->GetAnimInstance()))
	{
		Inst->PlayClip(Anim, bLoop);
	}
	else
	{
		Body->PlayAnimation(Anim, bLoop);
	}

	// LIFE4 — the scene's claim on the base channel. A choreographed clip is pinned to scene time
	// and can be held past its own length, so the claim has no expiry: the scene owns the body until
	// `StopCinematicClip` gives the claim back, and the every-tick locomotion publish — idle or
	// travelling — yields to it in between.
	FElysiumAnimationRequest Claim;
	Claim.Source = EElysiumAnimSource::Scene;
	Claim.Channel = EElysiumAnimChannel::Base;
	Claim.Priority = EElysiumAnimPriority::Scene;
	Claim.Label = ClipName;
	if (const uint32 Handle = SubmitBodyAnimRequest(Body, Claim))
	{
		CinematicClaims.Add(FObjectKey(Body), Handle);
	}

	Body->TickAnimation(0.0f, false);
	Body->RefreshBoneTransforms();
	Body->SetVisibility(true, true);
	return true;
}

bool UElysiumEntityBodies::PreloadCinematicClip(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& BankStem, const FString& ClipName)
{
	return Body && ResolveCinematicClip(
		Body->GetSkeletalMeshAsset(), Stem, BankStem, ClipName) != nullptr;
}

bool UElysiumEntityBodies::PreloadCinematicClipForModel(const FString& Stem, bool bPlayerMaterial,
	const FString& BankStem, const FString& ClipName)
{
	USkeletalMesh* Mesh = ResolveNpcMesh(Stem, bPlayerMaterial);
	return ResolveCinematicClip(Mesh, Stem, BankStem, ClipName) != nullptr;
}

bool UElysiumEntityBodies::SeekCinematicClip(USkeletalMeshComponent* Body, float PositionSeconds)
{
	if (Body == nullptr)
	{
		return false;
	}
	if (UElysiumBipedAnimInstance* Inst = Cast<UElysiumBipedAnimInstance>(Body->GetAnimInstance()))
	{
		Inst->SeekClip(PositionSeconds);
	}
	else
	{
		Body->SetPosition(FMath::Max(0.f, PositionSeconds), /*bFireNotifies=*/false);
		Body->SetPlayRate(0.f);
	}
	return true;
}

bool UElysiumEntityBodies::GetCinematicClipPosition(USkeletalMeshComponent* Body, float& OutSeconds) const
{
	if (Body == nullptr)
	{
		return false;
	}
	if (const UElysiumBipedAnimInstance* Inst = Cast<UElysiumBipedAnimInstance>(Body->GetAnimInstance()))
	{
		const float Position = Inst->GetClipPosition();
		if (Position < 0.f)
		{
			return false;   // nothing playing — "cannot say", not "at zero"
		}
		OutSeconds = Position;
		return true;
	}
	// A component not on one of our hosts — a preview or chargen stage body driven straight through
	// `PlayAnimation`. GetPosition answers 0 for a component with no player at all, which is
	// indistinguishable from a clip genuinely at frame 0 — so the presence of a sequence is the
	// test, not the value.
	if (Body->GetAnimationMode() != EAnimationMode::AnimationSingleNode || Body->GetSingleNodeInstance() == nullptr)
	{
		return false;
	}
	OutSeconds = Body->GetPosition();
	return true;
}

bool UElysiumEntityBodies::ResyncCinematicClip(USkeletalMeshComponent* Body, float PositionSeconds)
{
	if (Body == nullptr)
	{
		return false;
	}
	if (UElysiumBipedAnimInstance* Inst = Cast<UElysiumBipedAnimInstance>(Body->GetAnimInstance()))
	{
		Inst->ResyncClip(PositionSeconds);
		return true;
	}
	if (Body->GetAnimationMode() != EAnimationMode::AnimationSingleNode || Body->GetSingleNodeInstance() == nullptr)
	{
		return false;
	}
	// Deliberately WITHOUT the SetPlayRate(0.f) that SeekCinematicClip pairs with SetPosition: the
	// clip is meant to keep running from its corrected phase, not freeze at it.
	Body->SetPosition(FMath::Max(0.f, PositionSeconds), /*bFireNotifies=*/false);
	return true;
}

void UElysiumEntityBodies::StopCinematicClip(USkeletalMeshComponent* Body)
{
	if (Body == nullptr)
	{
		return;
	}
	if (UElysiumBipedAnimInstance* Inst = Cast<UElysiumBipedAnimInstance>(Body->GetAnimInstance()))
	{
		Inst->StopClip();
	}
	else
	{
		Body->Stop();
	}
	// The scene gives the base channel back; the next locomotion publish takes the pose again.
	ReleaseCinematicClaim(Body);
}

void UElysiumEntityBodies::ReleaseCinematicClaim(USkeletalMeshComponent* Body)
{
	// A claim that already lapsed — outranked, released once already, or the driver reset across a
	// map epoch — releases nothing, which is the ordinary end of a claim rather than a failure.
	uint32 Handle = 0;
	if (Body != nullptr && CinematicClaims.RemoveAndCopyValue(FObjectKey(Body), Handle))
	{
		ReleaseBodyAnimRequest(Body, Handle);
	}
}

int32 UElysiumEntityBodies::SetFlexControllers(USkeletalMeshComponent* Body,
	TArrayView<const FElysiumFlexWrite> Writes, TArray<FString>* OutMissing)
{
	// No host on a component that is not one of ours, and no rig on a model with no facial sidecar.
	// Both stand a body with a still face rather than failing, so both answer the same way.
	UElysiumBodyAnimInstance* Inst = Body
		? Cast<UElysiumBodyAnimInstance>(Body->GetAnimInstance()) : nullptr;
	return Inst != nullptr ? Inst->SetFlexControllers(Writes, OutMissing) : INDEX_NONE;
}

bool UElysiumEntityBodies::SetMouthOpen(USkeletalMeshComponent* Body, float Open)
{
	UElysiumBodyAnimInstance* Inst = Body
		? Cast<UElysiumBodyAnimInstance>(Body->GetAnimInstance()) : nullptr;
	return Inst != nullptr && Inst->SetMouthOpen(Open);
}

bool UElysiumEntityBodies::GetPhonemeFilter(USkeletalMeshComponent* Body, float& OutMin,
	float& OutMax) const
{
	const UElysiumBodyAnimInstance* Inst = Body
		? Cast<UElysiumBodyAnimInstance>(Body->GetAnimInstance()) : nullptr;
	const FElysiumFacialRig* Rig = Inst ? Inst->GetFacialRig() : nullptr;
	if (Rig == nullptr)
	{
		return false;
	}
	OutMin = Rig->PhonemeFilterMin;
	OutMax = Rig->PhonemeFilterMax;
	return true;
}

bool UElysiumEntityBodies::SetViewTarget(USkeletalMeshComponent* Body, const FVector& WorldTarget)
{
	return EyePass.SetViewTarget(Body, WorldTarget);
}

bool UElysiumEntityBodies::GetHeadFrame(USkeletalMeshComponent* Body, FVector& OutPosition,
	FVector& OutForward) const
{
	return EyePass.GetHeadFrame(Body, OutPosition, OutForward);
}

bool UElysiumEntityBodies::RefreshNpcIdle(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& Disposition, int32 DispositionLevel, int32 IdleVariant)
{
	UElysiumAnimSubsystem* Anims = GetAnims();
	if (Body == nullptr || Anims == nullptr)
	{
		return false;
	}
	EElysiumIdleTier Tier = EElysiumIdleTier::None;
	const FString Clip = Anims->PickIdleClip(
		Stem, Disposition, Tier, IdleVariant, DispositionLevel);
	return !Clip.IsEmpty() && PlayNpcClip(Body, Stem, Clip, /*bLoop=*/true, /*OutSeconds=*/nullptr);
}

bool UElysiumEntityBodies::ResolveStanceClips(const FString& Stem, const FString& AnimName,
	FElysiumStanceClips& OutClips)
{
	OutClips = FElysiumStanceClips();
	UElysiumAnimSubsystem* Anims = GetAnims();
	return Anims != nullptr && Anims->ResolveStanceClips(Stem, AnimName, OutClips);
}

bool UElysiumEntityBodies::ResolveDisposition(const FString& Disposition,
	int32 DispositionLevel, FElysiumDisposition& OutRow)
{
	OutRow = FElysiumDisposition();
	const AActor* Owner = GetOwner();
	UGameInstance* GI = Owner ? Owner->GetGameInstance() : nullptr;
	UElysiumRulebookSubsystem* Rules = GI ? GI->GetSubsystem<UElysiumRulebookSubsystem>() : nullptr;
	if (Rules == nullptr)
	{
		return false;
	}
	// `Resolve` already falls back to Neutral for a name the table does not carry, which is the
	// table's own documented rule rather than a repair -- so a null here means the table failed to
	// load at all, not that the disposition was unknown.
	const FElysiumDisposition* Row = Rules->Dispositions().Resolve(Disposition, DispositionLevel);
	if (Row == nullptr)
	{
		return false;
	}
	OutRow = *Row;
	return true;
}

void UElysiumEntityBodies::UpdateNpcDisposition(USkeletalMeshComponent* Body,
	const FString& Disposition, int32 DispositionLevel)
{
	// The blink cadence is the one piece of animation state a disposition change moves without a
	// body rebuild.
	EyePass.UpdateDisposition(Body, Disposition, DispositionLevel);
}

bool UElysiumEntityBodies::IsNpcBodyVisible(USkeletalMeshComponent* Body) const
{
	if (Body == nullptr)
	{
		return false;
	}
	const UWorld* W = Body->GetWorld();
	if (W == nullptr)
	{
		return true;
	}
	// A run with no renderer never advances any render time, so every body would read as invisible
	// and every idle schedule would stall on `TASK_WAIT_PVS`. Report visible instead: a headless run
	// is not a run in which everything is off-screen, it is a run in which the question has no
	// meaning.
	if (!FApp::CanEverRender())
	{
		return true;
	}
	// The tolerance is a frame budget, not a dwell time -- long enough that a body skipped by one
	// frame's occlusion query does not flicker out of its schedule, short enough that turning away
	// stops the selector within a think.
	constexpr float ToleranceSeconds = 0.25f;
	return W->GetTimeSeconds() - Body->GetLastRenderTimeOnScreen() <= ToleranceSeconds;
}

bool UElysiumEntityBodies::DescribeEyes(const USkeletalMeshComponent* Comp,
	FElysiumEyeReadout& Out) const
{
	return EyePass.DescribeEyes(Comp, Out);
}

void UElysiumEntityBodies::TickEyes(float DeltaSeconds)
{
	// `this` is the pass's world/camera context: it is not a UObject, so the component lends it one.
	EyePass.TickEyes(this, DeltaSeconds);
}

USkeletalMeshComponent* UElysiumEntityBodies::BuildNpcVisual(const FString& Stem, const FVector& Location,
	const FRotator& Rotation, float UniformScale, const FString& Disposition, int32 IdleVariant,
	bool bPlayerMaterial)
{
	AActor* Owner = GetOwner();
	USceneComponent* Root = Owner ? Owner->GetRootComponent() : nullptr;
	if (Stem.IsEmpty() || Root == nullptr)
	{
		return nullptr;
	}

	// Cache-checked load: a map-load preload may already have stood this skeleton in the cache even
	// though no component existed yet (the future !playercontroller case). The eyeball data is still
	// needed below to install this component's independent material instances.
	UElysiumAnimSubsystem* Anims = GetAnims();
	USkeletalMesh* Mesh = ResolveNpcMesh(Stem, bPlayerMaterial);
	if (Mesh == nullptr)
	{
		return nullptr;
	}
	// After the mesh, not before: the eye geometry is carried into the frame the body actually
	// landed in, and only the loaded mesh can say which that is.
	TSharedPtr<const FElysiumEyeSet> EyeSet = Anims
		? Anims->GetEyeSet(Stem) : nullptr;

	// Standard runtime-component recipe (mirrors BuildBrushBody): NewObject → attach → place →
	// RegisterComponent. The hulls-body path uses relative placement against the root at world origin;
	// NPC origins are the same Unreal-space verbatim values, so relative == world here.
	USkeletalMeshComponent* Comp = NewObject<USkeletalMeshComponent>(Owner);
	// Visual-only meshes follow a pawn/motor or mover; they are never navigation geometry. Set this
	// before the mesh and transform so none of those property changes can enqueue an octree update.
	Comp->SetCanEverAffectNavigation(false);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetSkeletalMeshAsset(Mesh);
	Comp->SetVisibility(false, true);
	Comp->SetupAttachment(Root);
	Comp->SetRelativeLocation(Location);
	Comp->SetRelativeRotation(Rotation);
	if (UniformScale != 1.f)
	{
		Comp->SetRelativeScale3D(FVector(UniformScale));
	}
	// The animation host is installed before the first clip, so it owns the pose from frame one and
	// every later change (stance, gesture, scripted sequence) crossfades instead of popping.
	//
	// One host for every body, player and cast alike: the graph owns the crossfade, the gait fans
	// and the one-shot slot, so there is no second pose composition to diverge from it.
	{
		UClass* Graph = BodyGraphClass();
		Comp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		Comp->SetAnimInstanceClass(
			Graph != nullptr ? Graph : UElysiumBipedAnimInstance::StaticClass());
	}
	Comp->RegisterComponent();
	// The visible mesh never collides; mobile NPCs wrap it in a native character capsule.
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Owner->AddInstanceComponent(Comp);
	// The face (12.3). A model with no facial sidecar gets a null rig and animates with a still
	// face — the normal case for animals, crowd bodies and every player body, none of which carry
	// flex data. Nothing drives the controllers yet: scene expressions are 12.1's and lipsync 12.5's.
	if (UElysiumBodyAnimInstance* Inst = Cast<UElysiumBodyAnimInstance>(Comp->GetAnimInstance()))
	{
		if (Anims != nullptr)
		{
			Inst->SetFacialRig(Anims->GetFacialRig(Stem));
			// The composition stage (CAP7.2). Null for a model declaring no procedural rule,
			// which poses under Unreal's own hierarchy alone.
			Inst->SetCompositionRig(Anims->GetCompositionRig(Stem));
		}
	}
	ElysiumNpcVisual::InstallHairDynamics(Comp, Stem);
	// The authored garment, if this model has one. After the anim instance is installed, because
	// the cloth component follows this body as its leader pose and needs it already posed.
	ElysiumNpcVisual::InstallGarment(Comp, Stem);
	// The eyes (12.4). Independent of the facial rig above: a player body binds eyes here and no
	// flex rig at all, which is the shipped state for 57 of the 59 of them.
	EyePass.InstallEyes(Comp, EyeSet, Disposition);
	// Visibility is a pose-commit boundary for characters as well as placed models. This is the
	// existing disposition/variant intent, installed before the component can draw; later scene and
	// locomotion writes still replace it through the same animation host.
	if (RefreshNpcIdle(Comp, Stem, Disposition, /*DispositionLevel=*/1, IdleVariant))
	{
		Comp->TickAnimation(0.0f, false);
		Comp->RefreshBoneTransforms();
		Comp->SetVisibility(true, true);
	}
	return Comp;
}

bool UElysiumEntityBodies::PlayNpcActivity(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& Activity, int32 Variant, bool bLoop, float* OutSeconds)
{
	UElysiumAnimSubsystem* Anims = GetAnims();
	const FString Clip = Anims ? Anims->PickActivityClip(Stem, Activity, Variant) : FString();
	if (Clip.IsEmpty())
	{
		return false;
	}
	// The CLIP decides whether it loops, not the caller. VtMB reads `m_bSequenceLoops` off the
	// sequence's own flags (RE35, `FElysiumNpcClip::IsLooping`), so an authored loop keeps looping
	// however it was asked for -- and the ambient callers ask for every activity with bLoop false.
	// Without this a body-language idle authored as a loop plays once and then stands on its last
	// frame, which is what a held one-shot means: frozen, not resting.
	bool bLoops = bLoop;
	if (const FElysiumNpcClipSet* Set = Anims->GetClipSet(Stem))
	{
		if (const FElysiumNpcClip* Row = Set->Find(Clip))
		{
			bLoops = bLoop || Row->IsLooping();
		}
	}
	return PlayNpcClip(Body, Stem, Clip, bLoops, OutSeconds);
}

bool UElysiumEntityBodies::ResolveNpcActivityClip(const FString& Stem, const FString& Activity,
	int32 Variant, FString& OutLabel, FString& OutAnimName, float& OutGroundSpeedCmPerSecond)
{
	UElysiumAnimSubsystem* Anims = GetAnims();
	if (Anims == nullptr)
	{
		OutLabel.Reset();
		OutAnimName.Reset();
		OutGroundSpeedCmPerSecond = 0.f;
		return false;
	}
	return Anims->ResolveActivityClip(Stem, Activity, Variant, OutLabel, OutAnimName,
		OutGroundSpeedCmPerSecond);
}

bool UElysiumEntityBodies::ResolveNpcSequenceClip(const FString& Stem, const FString& ClipName,
	FString& OutAnimName, float& OutGroundSpeedCmPerSecond)
{
	UElysiumAnimSubsystem* Anims = GetAnims();
	if (Anims == nullptr)
	{
		OutAnimName.Reset();
		OutGroundSpeedCmPerSecond = 0.f;
		return false;
	}
	return Anims->ResolveSequenceClip(Stem, ClipName, OutAnimName, OutGroundSpeedCmPerSecond);
}

bool UElysiumEntityBodies::HasNpcClip(const FString& Stem, const FString& ClipName)
{
	UElysiumAnimSubsystem* Anims = GetAnims();
	const FElysiumNpcClipSet* Set = Anims ? Anims->GetClipSet(Stem) : nullptr;
	return Set != nullptr && Set->Find(ClipName) != nullptr;
}
