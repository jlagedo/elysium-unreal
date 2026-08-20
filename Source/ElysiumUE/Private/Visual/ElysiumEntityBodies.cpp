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
#include "Animation/BlendSpace.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "Misc/App.h"
#include "PhysicsEngine/PhysicsAsset.h"

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

float ElysiumEntityAnimation::BlendedGridLengthSeconds(UBlendSpace* Space, float AxisValue)
{
	if (Space == nullptr)
	{
		return 0.f;
	}
	// The same two calls `FAnimNode_BlendSpacePlayerBase` makes to find the length it plays at, in the
	// same order: the samples the blend input selects, then the length those samples blend to. Asking
	// the asset anything else would be a second answer to a question the evaluator already has one for.
	TArray<FBlendSampleData> Samples;
	int32 CachedTriangulationIndex = INDEX_NONE;
	if (!Space->GetSamplesFromBlendInput(FVector(AxisValue, 0.f, 0.f), Samples,
		CachedTriangulationIndex, /*bCombineAnimations=*/true))
	{
		// A space with no samples, or one whose `ResampleData` never ran, answers nothing here rather
		// than reporting a zero-length clip. The caller reads it as "cannot say" and refuses.
		return 0.f;
	}
	return Space->GetAnimationLengthFromSampleData(Samples);
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

EElysiumAnimClaim UElysiumEntityBodies::SubmitBodyAnimRequest(USkeletalMeshComponent* Body,
	const FElysiumAnimationRequest& Request, uint32& OutHandle)
{
	OutHandle = 0;
	if (Body == nullptr)
	{
		return EElysiumAnimClaim::NoArbiter;
	}
	// An NPC visual hangs off its motor's root; the player's hangs off the pawn and routes through
	// the map actor's own driver. Anything else — a green-room stand, a preview body, a prop — has
	// no driver publishing locomotion against it, so there is nothing to arbitrate and no claim to
	// hold: an explicitly optional absence, not a failure.
	//
	// A driver that answers 0 REFUSED the claim — a lower band asking for a channel a scene owns —
	// which is the opposite instruction to a body that has no driver at all.
	if (AElysiumNpcBody* Motor = Cast<AElysiumNpcBody>(Body->GetAttachParentActor()))
	{
		OutHandle = Motor->SubmitAnimRequest(Request);
		NoteHeldReactionPreempted(Body, Request, OutHandle);
		return OutHandle != 0 ? EElysiumAnimClaim::Granted : EElysiumAnimClaim::Refused;
	}
	if (AElysiumMapActor* Map = Cast<AElysiumMapActor>(GetOwner()); Map != nullptr
		&& Map->IsPlayerVisual(Body))
	{
		OutHandle = Map->SubmitPlayerAnimRequest(Request);
		NoteHeldReactionPreempted(Body, Request, OutHandle);
		return OutHandle != 0 ? EElysiumAnimClaim::Granted : EElysiumAnimClaim::Refused;
	}
	return EElysiumAnimClaim::NoArbiter;
}

void UElysiumEntityBodies::NoteHeldReactionPreempted(USkeletalMeshComponent* Body,
	const FElysiumAnimationRequest& Request, uint32 GrantedHandle)
{
	// A granted BASE claim replaces whatever the slot held (`FElysiumAnimationDriver::SubmitRequest`
	// takes the slot on `>=`), so a held reaction standing on that slot is gone the moment this
	// returns. The record here has to go with it, or the release path would hand the driver a handle
	// naming a claim that belongs to someone else now.
	//
	// **The pose goes down with the claim.** A held reaction that no longer owns the channel must not
	// keep posing over the producer that took it — the block pose outliving its claim is the same
	// defect as the claim outliving its pose, read the other way round.
	if (GrantedHandle == 0 || Request.Channel != EElysiumAnimChannel::Base)
	{
		return;
	}
	if (ForgetHeldReaction(Body, GrantedHandle))
	{
		UE_LOG(LogElysiumBodies, Verbose,
			TEXT("held reaction on %s released: %s '%s' (%s) took the base channel"),
			*GetNameSafe(Body), ElysiumAnimIntent::SourceName(Request.Source), *Request.Label,
			ElysiumAnimIntent::PriorityName(Request.Priority));
	}
}

bool UElysiumEntityBodies::ForgetHeldReaction(USkeletalMeshComponent* Body, uint32 GrantedHandle)
{
	if (Body == nullptr)
	{
		return false;
	}
	const FElysiumHeldReaction* Held = HeldReactionClaims.Find(FObjectKey(Body));
	if (Held == nullptr || Held->Handle == GrantedHandle)
	{
		return false;
	}
	const FElysiumHeldReaction Record = *Held;
	HeldReactionClaims.Remove(FObjectKey(Body));
	StopHeldReactionPose(Body, Record);
	return true;
}

void UElysiumEntityBodies::StopHeldReactionPose(USkeletalMeshComponent* Body,
	const FElysiumHeldReaction& Held)
{
	UElysiumBodyAnimInstance* Inst = Body != nullptr
		? Cast<UElysiumBodyAnimInstance>(Body->GetAnimInstance()) : nullptr;
	if (Inst == nullptr)
	{
		return;
	}
	if (Held.bOnReactionBranch)
	{
		if (UElysiumBipedAnimInstance* Biped = Cast<UElysiumBipedAnimInstance>(Inst))
		{
			Biped->StopReaction();
		}
		return;
	}
	// The montage half. `StopOneShot` ends whatever montage the one-shot seam is running on this body
	// — which is THIS play's only until something else arms the slot — so the identity is checked
	// first rather than assumed. The ordinary route to a later arm is a producer that claimed the base
	// channel, which drops this record before it ever gets here; a body nothing arbitrates has no such
	// gate, and that is the case this comparison actually covers.
	//
	// **`IsExplicitlyNull`, not `!IsValid`.** The two answers a null weak pointer can carry are
	// opposite instructions here: never assigned means the host named no montage (the clip-player
	// fallback), where stopping is the only answer, while a montage that has since been collected is
	// proof the slot moved on — the exact case this guard exists for, and the one `IsValid` alone
	// would answer by stopping somebody else's play.
	const UElysiumBipedAnimInstance* Biped = Cast<UElysiumBipedAnimInstance>(Inst);
	if (Biped != nullptr && !Held.Montage.IsExplicitlyNull()
		&& Biped->GetActiveSlotMontage() != Held.Montage.Get())
	{
		UE_LOG(LogElysiumBodies, Verbose,
			TEXT("held reaction on %s is not stopped: the one-shot slot has moved on to another play"),
			*GetNameSafe(Body));
		return;
	}
	Inst->StopOneShot(Held.BlendOutSeconds);
}

const FElysiumAnimationRequest* UElysiumEntityBodies::ActiveBodyAnimRequest(
	USkeletalMeshComponent* Body, EElysiumAnimChannel Channel) const
{
	if (Body == nullptr)
	{
		return nullptr;
	}
	if (const AElysiumNpcBody* Motor = Cast<AElysiumNpcBody>(Body->GetAttachParentActor()))
	{
		return Motor->ActiveAnimRequest(Channel);
	}
	if (const AElysiumMapActor* Map = Cast<AElysiumMapActor>(GetOwner()); Map != nullptr
		&& Map->IsPlayerVisual(Body))
	{
		return Map->ActivePlayerAnimRequest(Channel);
	}
	// A body with no driver arbitrates nothing and therefore holds nothing. An explicitly optional
	// absence, exactly as the submit and release routes above answer it.
	return nullptr;
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
	// The authored fade both ways: this funnel carries one number and passes it as both (in = the
	// fade, out = the fade unless the clip loops).
	const float Fade = ClipFadeSeconds(Stem, ClipName);
	// LIFE5 — the identity the clip's event timeline is keyed by. The OWNER is the include DAG's
	// answer (this body's own stem, or the bank that carries the clip) and the label is the name the
	// caller asked for, never the animation the cell resolved to. It is the same vocabulary lookup
	// `ClipFadeSeconds` just made; a clip the vocabulary does not carry falls back to this stem,
	// which is what a prop, a bank clip reached by name and the green room all are.
	//
	// **The expression is `FElysiumAnimationSelection::OwnerStem`'s, character for character**
	// (`Visual/ElysiumAnimationResolve.cpp`). A weapon stands its estimate down by asking whether the
	// clip it just played is the one a polled channel is standing on, and that question joins the
	// owner the resolver recorded against the owner published here — so two spellings of one rule is
	// a silently lost commit, not a cosmetic difference.
	UElysiumAnimSubsystem* Anims = GetAnims();
	const FElysiumNpcClipSet* Set = Anims != nullptr ? Anims->GetClipSet(Stem) : nullptr;
	const FElysiumNpcClip* Clip = Set != nullptr ? Set->Find(ClipName) : nullptr;
	const FElysiumClipIdentity Identity(
		Clip == nullptr || Clip->IsOwnedBy(Stem) ? Stem : Clip->Owner, ClipName);

	// LIFE4 — the clip's claim on the base channel. Everything this funnel arms today — the ambient
	// schedule's stances and fidgets, dialogue line clips, scripted beats still on the adapter — is
	// the ambient band: it holds the pose against a standing body's every-tick publish and yields
	// the moment the body travels, which is the priority-table row the interim while-locomoting
	// rule became. A looping clip holds until replaced or outranked; a one-shot's claim runs its
	// clip length so an armer that never returns cannot park the channel. LIFE5 producers that
	// deserve a higher band submit their own claims through the same slot.
	//
	// **The claim comes FIRST, and it decides whether the clip plays at all** — the same order
	// `PlayNpcOneShot` takes, and for the same reason. Playing first and claiming after means a
	// refused claim has already stomped the pose it was refused the right to replace: an ambient
	// fidget would take the slot from a standing reaction, be told it does not own the channel, and
	// leave the body posing the fidget anyway with nothing reporting it.
	FElysiumAnimationRequest Claim;
	Claim.Source = EElysiumAnimSource::Npc;
	Claim.Channel = EElysiumAnimChannel::Base;
	Claim.Priority = EElysiumAnimPriority::Ambient;
	Claim.Label = ClipName;
	Claim.HoldSeconds = bLoop ? 0.0f : Anim->GetPlayLength();
	uint32 ClaimHandle = 0;
	if (SubmitBodyAnimRequest(Body, Claim, ClaimHandle) == EElysiumAnimClaim::Refused)
	{
		// An ordinary negative outcome, not a failure: the priority table answered and a higher band
		// owns the pose. Verbose for the same reason the one-shot seam's refusal is — a body a scene
		// or a reaction owns refuses ambient clips for as long as it holds them.
		UE_LOG(LogElysiumBodies, Verbose,
			TEXT("clip '%s' on %s was refused the base channel"), *ClipName, *Stem);
		return false;
	}

	if (!Inst->PlayOneShot(Identity, Anim, bLoop, Fade, Fade))
	{
		UE_LOG(LogElysiumBodies, Warning,
			TEXT("npc '%s' clip '%s' (loop=%d, %.3fs): the animation host refused to play it, so the "
			     "body keeps posing whatever it already held"),
			*Stem, *ClipName, bLoop ? 1 : 0, Anim->GetPlayLength());
		// The claim it was granted goes straight back: a channel held for a clip that never started
		// is exactly the leaked claim the hold report exists to make visible.
		if (ClaimHandle != 0)
		{
			ReleaseBodyAnimRequest(Body, ClaimHandle);
		}
		return false;
	}
	UE_LOG(LogElysiumBodies, Verbose, TEXT("clip '%s' on %s (loop=%d, %.3fs)"),
		*ClipName, *Stem, bLoop ? 1 : 0, Anim->GetPlayLength());

	Body->TickAnimation(0.0f, false);
	Body->RefreshBoneTransforms();
	Body->SetVisibility(true, true);
	return true;
}

UAnimSequence* UElysiumEntityBodies::ResolveOneShotClip(USkeletalMesh* Mesh,
	const FString& OwnerStem, const FString& AnimationName)
{
	if (Mesh == nullptr || OwnerStem.IsEmpty() || AnimationName.IsEmpty())
	{
		return nullptr;
	}
	UElysiumAnimSubsystem* Anims = GetAnims();
	if (Anims == nullptr)
	{
		return nullptr;
	}

	// Keyed on the MESH rather than on a stem: a one-shot request names no model, and a material
	// permutation is a distinct runtime mesh and USkeleton — so the object's own path is the identity
	// that keeps two bodies' retargeted sequences apart.
	const FString Key = ElysiumEntityAnimation::CinematicClipCacheKey(
		Mesh->GetPathName(), OwnerStem, AnimationName);
	if (const TObjectPtr<UAnimSequence>* Found = NpcAnimCache.Find(Key))
	{
		return Found->Get();
	}

	// The owner+name door, which never consults the clip vocabulary: the cell is already resolved,
	// and a vocabulary lookup would re-resolve the label at neutral pose parameters.
	FString Error;
	UAnimSequence* Anim = Anims->ResolveClipFromBank(OwnerStem, AnimationName, Mesh, Error);
	if (Anim == nullptr && !ReportedMissingOneShots.Contains(Key))
	{
		// A missing bank or asset is a real failure — the body plays nothing where content says it
		// should — so it is warned. Once per (mesh, owner, clip): a reaction re-arms on every hit.
		ReportedMissingOneShots.Add(Key);
		UE_LOG(LogElysiumBodies, Warning, TEXT("one-shot '%s'@'%s' on %s: %s"),
			*AnimationName, *OwnerStem, *GetNameSafe(Mesh), *Error);
	}
	NpcAnimCache.Add(Key, Anim);
	return Anim;
}

bool UElysiumEntityBodies::PlayNpcOneShot(USkeletalMeshComponent* Body,
	const FElysiumOneShotClipRequest& Request, float* OutSeconds)
{
	if (Body == nullptr || !Request.IsValid())
	{
		// Neither is an ordinary negative: this seam takes an ALREADY-RESOLVED cell, so a null body or
		// a request missing half the (owner, animation name) pair is a producer that built its request
		// wrong. Once per spelling, because a reaction re-arms on every hit.
		const FString Key = FString::Printf(TEXT("defect|%s|%s|%d"),
			*Request.OwnerStem, *Request.AnimationName, Body != nullptr ? 1 : 0);
		if (!ReportedMissingOneShots.Contains(Key))
		{
			ReportedMissingOneShots.Add(Key);
			UE_LOG(LogElysiumBodies, Warning,
				TEXT("one-shot request is not playable: owner '%s', animation '%s', body %s"),
				Request.OwnerStem.IsEmpty() ? TEXT("(none)") : *Request.OwnerStem,
				Request.AnimationName.IsEmpty() ? TEXT("(none)") : *Request.AnimationName,
				Body != nullptr ? TEXT("present") : TEXT("null"));
		}
		return false;
	}

	UElysiumBodyAnimInstance* Inst = Cast<UElysiumBodyAnimInstance>(Body->GetAnimInstance());
	if (Inst == nullptr)
	{
		UE_LOG(LogElysiumBodies, Warning,
			TEXT("one-shot '%s'@'%s': this body carries no Elysium animation host, so nothing can "
			     "play it"), *Request.AnimationName, *Request.OwnerStem);
		return false;
	}

	// --- which channel plays it, decided before anything is resolved (LIFE5) ----------------------
	//
	// The reaction route needs the graph's own branch, which only a compiled biped class carries. A
	// body without one — the plain native host, or a generated class built before the branch existed —
	// falls back to the montage slot with a single cell, which is a lesser pose rather than none.
	UElysiumBipedAnimInstance* Biped = Cast<UElysiumBipedAnimInstance>(Inst);
	// A HELD reaction — one a predicate releases — has to REPEAT for as long as it stands, and the
	// graph's reaction branch cannot: its sequence player's `bLoopAnimation` is edit-time state the
	// compiler folds to a constant, so nothing at runtime can flip it and a finished cell freezes on
	// its terminal frame. The DefaultSlot montage is the one host in this runtime that repeats a clip
	// (`PlayOneShot` already asks for it), and it covers the blend stack exactly as the branch does,
	// so a held single-cell reaction is played there.
	//
	// A held FAN would still need the branch — a montage plays one sequence — so the fork is on the
	// grid, not on the hold. Nothing produces a held fan today: the block family is authored per
	// activity and carries no direction (`docs/vtmb/combat-and-damage.md` § "Block and stagger
	// reactions"), which is why this is a fork rather than a refusal.
	const bool bHeldReaction = Request.Route == EElysiumOneShotRoute::Reaction
		&& Request.Release == EElysiumReactionRelease::Predicate;
	const bool bReactionBranch = Request.Route == EElysiumOneShotRoute::Reaction
		&& !(bHeldReaction && !Request.bGrid)
		&& Biped != nullptr && Biped->HasCompiledGraph() && Biped->HasCompiledReactionBranch();

	// The fan, when the branch can evaluate one. `ResolveGrid` answers the baked `UBlendSpace` and the
	// axis binding; the LENGTH is asked of the engine at the sampled parameter, because a fan's cells
	// do not share a length and the pose the graph strikes is a blend of two of them.
	FElysiumResolvedGrid Fan;
	UAnimSequence* Anim = nullptr;
	float LengthSeconds = 0.0f;
	FString AnimationName = Request.AnimationName;
	USkeletalMesh* Mesh = Body->GetSkeletalMeshAsset();
	// Once per (mesh, label): a reaction re-arms on every hit, and a defect restated per blow buries
	// the load it belongs to.
	auto ReportOnce = [this, Mesh, &Request](const TCHAR* Kind, FString&& Line)
	{
		const FString Key = FString::Printf(TEXT("%s|%s|%s|%s"), Kind, *GetNameSafe(Mesh),
			*Request.OwnerStem, *Request.Label);
		if (!ReportedMissingOneShots.Contains(Key))
		{
			ReportedMissingOneShots.Add(Key);
			UE_LOG(LogElysiumBodies, Warning, TEXT("reaction '%s'@'%s' on %s: %s"), *Request.Label,
				*Request.OwnerStem, *GetNameSafe(Mesh), *Line);
		}
	};

	if (bReactionBranch && Request.bGrid)
	{
		UElysiumAnimSubsystem* Anims = GetAnims();
		FString Why;
		if (Anims == nullptr
			|| !Anims->ResolveGrid(Request.BodyStem, Request.Label, Mesh, Fan,
				/*Host=*/FString(), &Why))
		{
			ReportOnce(TEXT("fan"), FString::Printf(TEXT("the fan is not on the mount: %s"),
				Why.IsEmpty() ? TEXT("no animation subsystem") : *Why));
			return false;
		}
		LengthSeconds = ElysiumEntityAnimation::BlendedGridLengthSeconds(Fan.Space, Request.AxisValue);
		if (LengthSeconds <= 0.0f)
		{
			ReportOnce(TEXT("fanlen"), FString::Printf(
				TEXT("'%s' reports no blended length at %.1f, so nothing can be timed off it"),
				*GetNameSafe(Fan.Space), Request.AxisValue));
			return false;
		}
	}
	else
	{
		// The single-cell path, both routes. A reaction that cannot reach the branch collapses its fan
		// to the NEARER of the two cells the angle sits between rather than to the floor cell, which
		// would bias every such reaction one cell counter-clockwise.
		if (Request.Route == EElysiumOneShotRoute::Reaction && Request.bGrid)
		{
			UElysiumAnimSubsystem* Anims = GetAnims();
			const FString Cell = Anims != nullptr
				? Anims->ResolveNearestGridClip(Request.OwnerStem, Request.Label, Request.AxisValue)
				: FString();
			if (!Cell.IsEmpty())
			{
				AnimationName = Cell;
			}
			// A real loss of fidelity, named rather than taken quietly: the body strikes one authored
			// direction instead of the blend between two, which is +-22.5 degrees of error on a
			// nine-cell fan. The repair is a graph that carries the branch, not a resolver change.
			ReportOnce(TEXT("collapse"), FString::Printf(
				TEXT("this body has no reaction branch, so the fan collapses to its nearest cell "
				     "'%s' at %.1f"),
				Cell.IsEmpty() ? *Request.AnimationName : *Cell, Request.AxisValue));
		}
		Anim = ResolveOneShotClip(Mesh, Request.OwnerStem, AnimationName);
		if (Anim == nullptr)
		{
			// `ResolveOneShotClip` already warned once per (mesh, owner, clip) naming the bank or asset.
			return false;
		}
		LengthSeconds = Anim->GetPlayLength();
	}

	// The reaction, built before the claim because the claim's own duration is derived from it: the
	// three release conditions each state a different life, and one expression answers both halves
	// (`ActiveSeconds`/`TotalSeconds` on the play), which is what stops the claim expiring mid-fade.
	// LIFE5 — the identity both routes publish their phase under. The LABEL is the vocabulary key the
	// request was addressed by, which is what `FElysiumBlendTable::Events` is keyed on; a request
	// that carries none was addressed by the animation name directly, so that is the key. It is the
	// same expression the channel claim below takes for the same reason.
	const FElysiumClipIdentity Identity(Request.OwnerStem,
		Request.Label.IsEmpty() ? AnimationName : Request.Label);

	FElysiumReactionPlay Play;
	Play.Space = Fan.Space;                 // null on the single-clip reaction
	Play.Sequence = Fan.Space != nullptr ? nullptr : Anim;
	Play.OwnerStem = Identity.OwnerStem;
	Play.Label = Identity.Label;
	Play.AxisValue = Request.AxisValue;
	Play.LengthSeconds = LengthSeconds;
	Play.BlendInSeconds = Request.BlendInSeconds;
	Play.BlendOutSeconds = Request.BlendOutSeconds;
	Play.Release = Request.Route == EElysiumOneShotRoute::Reaction
		? Request.Release : EElysiumReactionRelease::ClipCompletion;
	// A held pose repeats; every other reaction is a one-shot whose own end is what releases it. The
	// caller's `bLoop` stands for the routes that predate the release condition.
	Play.bLoop = bHeldReaction || Request.bLoop;

	// **The claim first, and it decides whether the clip plays at all.** A body a choreographed scene
	// owns refuses a Reaction claim, and a reaction must not ride over a scene — so a refusal returns
	// before the montage rather than arming a clip whose channel it does not hold. A body with no
	// driver has nothing arbitrating, and plays.
	//
	// A HELD reaction claims with no duration at all (`HoldSeconds <= 0` is "until released, replaced
	// or outranked"), which is the whole repair: the pose stands exactly as long as the predicate
	// that asked for it, and `ReleaseNpcReaction` is what gives it back.
	FElysiumAnimationRequest Claim;
	Claim.Source = Request.Source;
	Claim.Channel = EElysiumAnimChannel::Base;
	Claim.Priority = Request.Priority;
	Claim.Label = Identity.Label;
	Claim.HoldSeconds = (Play.bLoop || bHeldReaction)
		? 0.0f
		: (bReactionBranch ? Play.TotalSeconds() : LengthSeconds);
	uint32 Handle = 0;
	const EElysiumAnimClaim Verdict = SubmitBodyAnimRequest(Body, Claim, Handle);
	if (Verdict == EElysiumAnimClaim::Refused)
	{
		// An ordinary negative outcome, not a failure: the priority table answered, and the caller
		// keeps whatever fallback it stated. Verbose so a refused reaction is still readable, without
		// a warning per hit on a body a scene owns.
		UE_LOG(LogElysiumBodies, Verbose,
			TEXT("one-shot '%s'@'%s' (%s, %s) was refused the base channel"),
			*AnimationName, *Request.OwnerStem,
			ElysiumAnimIntent::SourceName(Request.Source),
			ElysiumAnimIntent::PriorityName(Request.Priority));
		return false;
	}

	bool bPlaying = false;
	if (bReactionBranch)
	{
		bPlaying = Biped->PlayReaction(Play);
	}
	else
	{
		bPlaying = Inst->PlayOneShot(Identity, Anim, Play.bLoop, Request.BlendInSeconds,
			Request.BlendOutSeconds, Request.bRestart);
	}
	if (!bPlaying)
	{
		UE_LOG(LogElysiumBodies, Warning,
			TEXT("one-shot '%s'@'%s' (loop=%d, %.3fs, %s): the animation host refused to play it, so "
			     "the body keeps posing whatever it already held"),
			*AnimationName, *Request.OwnerStem, Request.bLoop ? 1 : 0, LengthSeconds,
			bReactionBranch ? TEXT("reaction") : TEXT("slot"));
		// The claim it was granted goes straight back: a channel held for a clip that never started
		// is exactly the leaked claim the hold report exists to make visible.
		if (Handle != 0)
		{
			ReleaseBodyAnimRequest(Body, Handle);
		}
		return false;
	}
	UE_LOG(LogElysiumBodies, Verbose,
		TEXT("one-shot '%s'@'%s' (loop=%d, %.3fs, in %.2f out %.2f, %s%s, release=%s)"),
		*AnimationName, *Request.OwnerStem, Play.bLoop ? 1 : 0, LengthSeconds,
		Request.BlendInSeconds, Request.BlendOutSeconds,
		bReactionBranch ? TEXT("reaction") : TEXT("slot"),
		Fan.Space != nullptr ? TEXT(" fan") : TEXT(""),
		ElysiumAnimIntent::ReactionReleaseName(Play.Release));

	// The held claim is remembered so its producer can give it back. Recorded AFTER the play started,
	// for the same reason the claim is released when the play fails: a record naming a pose nobody is
	// striking is the leak this map exists to make impossible.
	//
	// Recorded even on a ZERO handle, which is the body nothing arbitrates — a preview stand, a lab
	// body. It holds no claim to give back, but it IS striking a repeating pose, and the release path
	// is the only thing that can ever take that pose down.
	if (bHeldReaction)
	{
		FElysiumHeldReaction Record;
		Record.Handle = Handle;
		Record.bOnReactionBranch = bReactionBranch;
		Record.BlendOutSeconds = Request.BlendOutSeconds;
		// The montage this play just armed, read back off the host rather than guessed: it is the
		// identity the release compares against, and the clip-player fallback names none.
		Record.Montage = (!bReactionBranch && Biped != nullptr)
			? const_cast<UAnimMontage*>(Biped->GetActiveSlotMontage()) : nullptr;
		HeldReactionClaims.Add(FObjectKey(Body), MoveTemp(Record));
	}

	// Written on the success path alone. A caller that schedules off the length — a reaction whose
	// recovery beat waits it out — must not be handed the length of a clip that a refused claim or a
	// refused montage means is not playing. It is the same number the claim holds for, which on the
	// reaction route is the branch's whole life rather than the clip's own length.
	//
	// A HELD reaction answers ZERO seconds, and that is the answer rather than a missing one: its
	// claim has no duration, so there is no time for a caller to schedule against — the predicate is.
	if (OutSeconds != nullptr)
	{
		*OutSeconds = bHeldReaction
			? 0.0f
			: (bReactionBranch ? Play.TotalSeconds() : LengthSeconds);
	}

	Body->TickAnimation(0.0f, false);
	Body->RefreshBoneTransforms();
	// The Slot route alone forces the body visible; the rule and why it is a rule are stated once, in
	// `ElysiumAnimIntent::OneShotForcesVisibility`.
	if (ElysiumAnimIntent::OneShotForcesVisibility(Request.Route))
	{
		Body->SetVisibility(true, true);
	}
	return true;
}

bool UElysiumEntityBodies::GetBodyClipPhase(USkeletalMeshComponent* Body,
	EElysiumAnimChannel Channel, FElysiumClipPhase& Out)
{
	Out = FElysiumClipPhase();
	const UElysiumBodyAnimInstance* Inst = Body != nullptr
		? Cast<UElysiumBodyAnimInstance>(Body->GetAnimInstance())
		: nullptr;
	if (Inst == nullptr)
	{
		// A body with no Elysium animation host has no phase to report. Ordinary rather than a
		// failure: a prop body, a body built before its host was installed, and every body in a
		// headless run answer here.
		return false;
	}
	return Inst->GetClipPhase(Channel, Out);
}

const TArray<FElysiumAnimEvent>* UElysiumEntityBodies::GetNpcEventTimeline(const FString& OwnerStem,
	const FString& Label)
{
	UElysiumAnimSubsystem* Anims = GetAnims();
	if (Anims == nullptr || OwnerStem.IsEmpty() || Label.IsEmpty())
	{
		return nullptr;
	}
	// The table is cached whole and immutable by the subsystem, so the array this points into
	// outlives the frame the caller asked in. A model that declares no sidecar at all answers null
	// here, which is the same absence as a sequence that declares no timeline.
	const TSharedPtr<const FElysiumBlendTable> Table = Anims->GetBlendTable(OwnerStem);
	return Table.IsValid() ? Table->FindEvents(Label) : nullptr;
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
	// The warn-once set goes with them: a re-export that fixes a missing one-shot must be able to
	// report the next miss rather than staying silent about it.
	ReportedMissingOneShots.Empty();
	// And the held-reaction records, whose bodies are exactly what is being forgotten: a record naming
	// a component nothing holds any more can never be released by its producer, which is the inert
	// entry a claim map has to be swept of rather than left to a map epoch.
	HeldReactionClaims.Empty();
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

	// **The claim first, and it decides whether the clip plays at all** — the same order
	// `PlayNpcOneShot` takes, and for the same reason: a body another scene already owns must not have
	// its pose replaced by a second one, and standing the clip before asking would make the refusal a
	// report about a body that had already been taken over.
	FElysiumAnimationRequest Claim;
	Claim.Source = EElysiumAnimSource::Scene;
	Claim.Channel = EElysiumAnimChannel::Base;
	Claim.Priority = EElysiumAnimPriority::Scene;
	Claim.Label = ClipName;
	uint32 Handle = 0;
	const EElysiumAnimClaim Verdict = SubmitBodyAnimRequest(Body, Claim, Handle);
	if (Verdict == EElysiumAnimClaim::Refused)
	{
		UE_LOG(LogElysiumBodies, Verbose,
			TEXT("cinematic clip '%s'@'%s' was refused the base channel"), *ClipName, *BankStem);
		return false;
	}

	if (OutSeconds != nullptr)
	{
		*OutSeconds = Anim->GetPlayLength();
	}
	if (UElysiumBipedAnimInstance* Inst = Cast<UElysiumBipedAnimInstance>(Body->GetAnimInstance()))
	{
		// LIFE5 — the cinematic bank owns the clip and the scene addressed it by name, so the two
		// together are the key its event timeline is filed under.
		Inst->PlayClip(FElysiumClipIdentity(BankStem, ClipName), Anim, bLoop);
	}
	else
	{
		Body->PlayAnimation(Anim, bLoop);
	}

	// LIFE4 — the scene's claim on the base channel. A choreographed clip is pinned to scene time
	// and can be held past its own length, so the claim has no expiry: the scene owns the body until
	// `StopCinematicClip` gives the claim back, and the every-tick locomotion publish — idle or
	// travelling — yields to it in between.
	if (Handle != 0)
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

// ================================================================================================
// The death handoff (LIFE5)
// ================================================================================================

void UElysiumEntityBodies::ReleaseNpcReaction(USkeletalMeshComponent* Body)
{
	FElysiumHeldReaction Held;
	if (Body == nullptr || !HeldReactionClaims.RemoveAndCopyValue(FObjectKey(Body), Held))
	{
		// No held reaction on this body — released already, outranked, or never taken because the
		// producer had no body. The ordinary end of a claim, not a failure.
		return;
	}
	StopHeldReactionPose(Body, Held);
	ReleaseBodyAnimRequest(Body, Held.Handle);
	UE_LOG(LogElysiumBodies, Verbose, TEXT("held reaction on %s released by its producer (%s)"),
		*GetNameSafe(Body), Held.bOnReactionBranch ? TEXT("reaction branch") : TEXT("slot"));
}

EElysiumHeldReactionState UElysiumEntityBodies::QueryNpcReactionHold(
	USkeletalMeshComponent* Body) const
{
	if (Body != nullptr && HeldReactionClaims.Contains(FObjectKey(Body)))
	{
		return EElysiumHeldReactionState::Held;
	}
	// The claim is gone. WHY decides what the producer may do next: a base channel another producer
	// holds must not be taken back, because an equal band replaces on `>=` and the resume would cut
	// short the very reaction that displaced it.
	return ActiveBodyAnimRequest(Body, EElysiumAnimChannel::Base) != nullptr
		? EElysiumHeldReactionState::Displaced
		: EElysiumHeldReactionState::Free;
}

void UElysiumEntityBodies::ReleaseBodyAnimClaims(USkeletalMeshComponent* Body)
{
	if (Body == nullptr)
	{
		return;
	}
	// The scene's claim is tracked HERE as well as in the driver slot, so dropping the slot alone
	// would leave this map holding a handle naming a claim that no longer exists. Ahead of the
	// wholesale release for that reason: after it the handle would release nothing.
	ReleaseCinematicClaim(Body);
	// The held reaction is tracked here for exactly the same reason, and death is the one transaction
	// that ends every claim at once — a corpse holding a block pose is a predicate nobody will ever
	// release.
	ReleaseNpcReaction(Body);
	if (AElysiumNpcBody* Motor = Cast<AElysiumNpcBody>(Body->GetAttachParentActor()))
	{
		Motor->ReleaseAllAnimRequests();
		return;
	}
	if (AElysiumMapActor* Map = Cast<AElysiumMapActor>(GetOwner()); Map != nullptr
		&& Map->IsPlayerVisual(Body))
	{
		Map->ReleaseAllPlayerAnimRequests();
	}
	// Anything else — a green-room stand, a preview body, a prop — has no driver arbitrating
	// anything and therefore holds no claim. An explicitly optional absence, not a failure.
}

bool UElysiumEntityBodies::StartBodyRagdoll(USkeletalMeshComponent* Body)
{
	if (Body == nullptr)
	{
		return false;
	}
	const UPhysicsAsset* Physics = Body->GetPhysicsAsset();
	if (Physics == nullptr || Physics->SkeletalBodySetups.IsEmpty())
	{
		// Reported ONCE per process rather than once per corpse: the character bake writes no physics
		// asset for a body at all, so this is one absent pipeline product and not a per-body fault.
		// The caller's stated fallback — hold the final pose — runs either way.
		static bool bReportedMissingPhysics = false;
		if (!bReportedMissingPhysics)
		{
			bReportedMissingPhysics = true;
			UE_LOG(LogElysiumBodies, Warning,
				TEXT("'%s' carries no physics asset, so a killed character holds its final pose "
					 "instead of handing to a ragdoll. Reported once per process; the character bake "
					 "writes no physics asset for any body."),
				*GetNameSafe(Body->GetSkinnedAsset()));
		}
		return false;
	}
	// Unreal owns the physics from here: the collision profile, the solver and the constraint set are
	// the engine's, and nothing of Source's ragdoll is reproduced. `SetSimulatePhysics` initialises
	// every body at its CURRENT bone transform, which is what makes the pose the death sequence left
	// behind the simulation's first frame.
	// The engine's own shipped `Ragdoll` profile (`BaseEngine.ini`), which is `QueryAndPhysics` on the
	// `PhysicsBody` object type and ignores the Pawn and Visibility channels — the same
	// character-versus-character release the death transaction already made on the capsule. It has no
	// `UCollisionProfile` constant, so the name is spelled; `bCanModify=False` keeps it stable.
	// The profile already declares `QueryAndPhysics`; a name the engine does not know reports
	// itself (`COLLISION PROFILE [...] is not found`, LogPhysics), so a missing profile is not a
	// silent no-op.
	Body->SetCollisionProfileName(TEXT("Ragdoll"));
	Body->SetSimulatePhysics(true);
	if (!Body->IsSimulatingPhysics())
	{
		// `SetSimulatePhysics` reports failure only by not simulating: an unregistered component or
		// an asset whose bodies did not instantiate both leave it exactly where it was. Answering
		// true here would take the caller past its own fallback and leave a corpse with a live pose
		// nothing advances.
		UE_LOG(LogElysiumBodies, Warning,
			TEXT("'%s' carries a physics asset but refused to simulate; the killed character holds "
				 "its final pose instead"),
			*GetNameSafe(Body->GetSkinnedAsset()));
		return false;
	}
	Body->WakeAllRigidBodies();
	return true;
}

void UElysiumEntityBodies::HoldBodyFinalPose(USkeletalMeshComponent* Body)
{
	if (Body == nullptr)
	{
		return;
	}
	// `bPauseAnims` rather than disabling the component tick: the tick also drives the transform and
	// bounds update the render thread reads, and a body that stopped publishing those would stop
	// being drawn correctly rather than stop moving. The last evaluated pose stays exactly as it is.
	Body->bPauseAnims = true;
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

bool UElysiumEntityBodies::ResolveNpcActivityClip(const FElysiumActivityClipRequest& Request,
	FElysiumActivityClip& Out)
{
	UElysiumAnimSubsystem* Anims = GetAnims();
	if (Anims == nullptr)
	{
		Out = FElysiumActivityClip();
		return false;
	}
	return Anims->ResolveActivityClip(Request, Out);
}

bool UElysiumEntityBodies::ResolveNpcSequenceClip(const FString& Stem, const FString& ClipName,
	EElysiumAnimBodyKind BodyKind, FString& OutAnimName, float& OutGroundSpeedCmPerSecond)
{
	UElysiumAnimSubsystem* Anims = GetAnims();
	if (Anims == nullptr)
	{
		OutAnimName.Reset();
		OutGroundSpeedCmPerSecond = 0.f;
		return false;
	}
	return Anims->ResolveSequenceClip(Stem, ClipName, BodyKind, OutAnimName,
		OutGroundSpeedCmPerSecond);
}

bool UElysiumEntityBodies::HasNpcClip(const FString& Stem, const FString& ClipName)
{
	UElysiumAnimSubsystem* Anims = GetAnims();
	const FElysiumNpcClipSet* Set = Anims ? Anims->GetClipSet(Stem) : nullptr;
	return Set != nullptr && Set->Find(ClipName) != nullptr;
}

FString UElysiumEntityBodies::NpcClipBlockedReaction(const FString& Stem, const FString& ClipLabel)
{
	UElysiumAnimSubsystem* Anims = GetAnims();
	const FElysiumNpcClipSet* Set = Anims ? Anims->GetClipSet(Stem) : nullptr;
	const FElysiumNpcClip* Clip = Set != nullptr ? Set->Find(ClipLabel) : nullptr;
	// Empty all the way down: no vocabulary, no such label, or a sequence whose descriptor names no
	// blocked reaction all mean the same thing to the caller, and none of them is a fault.
	return Clip != nullptr ? Clip->BlockedReaction : FString();
}

const TArray<FElysiumSwingRecord>* UElysiumEntityBodies::NpcClipSwings(const FString& Stem,
	const FString& ClipLabel)
{
	UElysiumAnimSubsystem* Anims = GetAnims();
	const FElysiumNpcClipSet* Set = Anims ? Anims->GetClipSet(Stem) : nullptr;
	const FElysiumNpcClip* Clip = Set != nullptr ? Set->Find(ClipLabel) : nullptr;
	// Null all the way down, like the blocked reaction above: no vocabulary, no such label, a
	// sequence declaring no records, and a slice written before the column existed are one answer to
	// the caller — this swing opens no contact window — and none of them is a fault.
	return (Clip != nullptr && Clip->HasSwings()) ? &Clip->Swings : nullptr;
}

bool UElysiumEntityBodies::GetBoneFrame(const USkeletalMeshComponent* Body,
	const FString& BoneName, FTransform& OutWorld) const
{
	OutWorld = FTransform::Identity;
	if (Body == nullptr || BoneName.IsEmpty())
	{
		return false;
	}
	const int32 BoneIndex = Body->GetBoneIndex(FName(*BoneName));
	if (BoneIndex == INDEX_NONE)
	{
		return false;
	}
	// The component transform goes in rather than being composed after, which is what makes this the
	// bone's WORLD frame — the same call `FElysiumEyePass::GetHeadFrame` reaches the head bone with.
	OutWorld = Body->GetBoneTransform(BoneIndex, Body->GetComponentTransform());
	return true;
}
