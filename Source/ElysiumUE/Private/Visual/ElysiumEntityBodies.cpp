#include "Visual/ElysiumEntityBodies.h"

#include "ElysiumContentPaths.h"
#include "ElysiumPropSkins.h"
#include "Visual/ElysiumNpcAnimInstance.h"
#include "Visual/ElysiumNpcAnimSubsystem.h"
#include "Visual/ElysiumNpcVisual.h"

#include "Animation/AnimSequence.h"
#if WITH_EDITOR
#include "Animation/IAnimationSequenceCompiler.h"
#endif
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumBodies, Log, All);

namespace ElysiumPropBounds
{
	bool ExtensionFor(const FBoxSphereBounds& Bind, double RadiusCm,
		FVector& OutPositive, FVector& OutNegative)
	{
		OutPositive = FVector::ZeroVector;
		OutNegative = FVector::ZeroVector;
		if (!(RadiusCm > 0.0))
		{
			return false;
		}
		bool bWidened = false;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			// The box has to contain [-R, +R] about the origin, and it is centred on `Bind.Origin`,
			// so each side carries the centre offset with its own sign.
			const double Positive = FMath::Max(0.0, RadiusCm - Bind.Origin[Axis] - Bind.BoxExtent[Axis]);
			const double Negative = FMath::Max(0.0, RadiusCm + Bind.Origin[Axis] - Bind.BoxExtent[Axis]);
			OutPositive[Axis] = Positive;
			OutNegative[Axis] = Negative;
			bWidened |= Positive > 0.0 || Negative > 0.0;
		}
		return bWidened;
	}
}

// The NPC animation host. 1 = UElysiumNpcAnimInstance (two sequence players + a crossfade, and the
// facial flex track over them), 0 = Unreal's single-node instance, which cannot blend, so every clip
// change pops — and which has no facial track at all, so 0 also stands the cast with still faces.
// Applied at map load, per body.
static TAutoConsoleVariable<int32> CVarNpcAnim(
	TEXT("elysium.NpcAnim"), 1,
	TEXT("NPC animation host: the crossfading Elysium anim instance and its facial track (1) or single-node, no face (0). Applied at map load."),
	ECVF_Default);

// A/B toggle for the prop skin pass (8.3/8.4). 1 applies alternate skin families; 0 leaves every
// prop on its authored materials, so a look change can be attributed. Read per apply, so it takes
// effect on the next Skin input without a reload.
static TAutoConsoleVariable<int32> CVarPropSkins(
	TEXT("elysium.PropSkins"), 1,
	TEXT("Apply alternate prop skin families (1, default) or keep every prop on skin 0 (0)."),
	ECVF_Default);

// 12.4 — an unambiguous visual check on the eye basis before the gaze cascade exists. Aiming every
// eye at a target that moves is the only way to tell a correct basis from one that merely looks
// plausible while parked on the record's authored resting aim.
static TAutoConsoleVariable<int32> CVarEyeTrackPlayer(
	TEXT("elysium.EyeTrackPlayer"), 0,
	TEXT("Aim every NPC's eyes at the player camera (1) instead of the eyeball record's authored resting aim (0, default)."),
	ECVF_Cheat);

// 12.4 — A/B for the whole eye pass: the iris aim and the eyelid write-back together. 0 leaves the
// irises on their authored resting aim and the lids on the FElysiumFlexLid reconstruction, which is
// how the face evaluated before the eyeball records were decoded.
static TAutoConsoleVariable<int32> CVarEyes(
	TEXT("elysium.Eyes"), 1,
	TEXT("Run the eye pass: iris aiming, the blink envelope and the authored eyelid write-back (1, default) or none of it (0)."),
	ECVF_Default);

UElysiumEntityBodies::UElysiumEntityBodies()
{
	PrimaryComponentTick.bCanEverTick = false;
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

	AActor* Owner = GetOwner();
	UGameInstance* GI = Owner ? Owner->GetGameInstance() : nullptr;
	UElysiumNpcAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumNpcAnimSubsystem>() : nullptr;
	const TSharedPtr<const FElysiumEyeSet> EyeSet = Anims ? Anims->GetEyeSet(Stem) : nullptr;
	TArray<FString> EyeMaterials;
	if (EyeSet.IsValid())
	{
		for (const FElysiumEyeball& Eye : EyeSet->Eyeballs)
		{
			if (!Eye.Material.IsEmpty())
			{
				EyeMaterials.AddUnique(Eye.Material);
			}
		}
	}

	UglTFRuntimeAsset* Asset = nullptr;
	FString Error;
	USkeletalMesh* Mesh = ElysiumNpcVisual::LoadMesh(
		Stem, Asset, Error, bPlayerMaterial, &EyeMaterials);
	if (Mesh == nullptr)
	{
		UE_LOG(LogElysiumBodies, Warning, TEXT("ResolveNpcMesh '%s': %s"), *Stem, *Error);
		return nullptr;
	}
	NpcMeshCache.Add(VisualKey, Mesh);
	NpcAssetCache.Add(VisualKey, Asset);
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

	const AActor* Owner = GetOwner();
	UGameInstance* GI = Owner ? Owner->GetGameInstance() : nullptr;
	UElysiumNpcAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumNpcAnimSubsystem>() : nullptr;

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
	const TObjectPtr<UglTFRuntimeAsset>* Own = NpcAssetCache.Find(VisualKey);
	if (Anims != nullptr && Mesh != nullptr && *Mesh != nullptr)
	{
		FString Error;
		Anim = Anims->ResolveClip(Stem, ClipName, Mesh->Get(), Own ? Own->Get() : nullptr, Error);
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
	const AActor* Owner = GetOwner();
	UGameInstance* GI = Owner ? Owner->GetGameInstance() : nullptr;
	UElysiumNpcAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumNpcAnimSubsystem>() : nullptr;
	const FElysiumNpcClipSet* Set = Anims ? Anims->GetClipSet(Stem) : nullptr;
	const FElysiumNpcClip* Clip = Set ? Set->Find(ClipName) : nullptr;
	// A clip the vocabulary does not carry — a bank clip reached by name, a prop, the green room —
	// transitions on the shipped default rather than snapping, which is what 5,762 of the 5,836
	// shipped sequences authored anyway.
	return Clip != nullptr ? Clip->FadeSeconds() : UElysiumNpcAnimInstance::DefaultBlendSeconds;
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
	if (UElysiumNpcAnimInstance* Inst = Cast<UElysiumNpcAnimInstance>(Body->GetAnimInstance()))
	{
		Inst->PlayClip(Anim, bLoop, ClipFadeSeconds(Stem, ClipName));
	}
	else
	{
		Body->PlayAnimation(Anim, bLoop);   // elysium.NpcAnim 0 — single-node A/B, no crossfade
	}
	return true;
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
	const AActor* Owner = GetOwner();
	UGameInstance* GI = Owner ? Owner->GetGameInstance() : nullptr;
	UElysiumNpcAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumNpcAnimSubsystem>() : nullptr;
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
	if (UElysiumNpcAnimInstance* Inst = Cast<UElysiumNpcAnimInstance>(Body->GetAnimInstance()))
	{
		Inst->PlayClip(Anim, bLoop);
	}
	else
	{
		Body->PlayAnimation(Anim, bLoop);
	}
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
	if (UElysiumNpcAnimInstance* Inst = Cast<UElysiumNpcAnimInstance>(Body->GetAnimInstance()))
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
	if (const UElysiumNpcAnimInstance* Inst = Cast<UElysiumNpcAnimInstance>(Body->GetAnimInstance()))
	{
		const float Position = Inst->GetClipPosition();
		if (Position < 0.f)
		{
			return false;   // nothing playing — "cannot say", not "at zero"
		}
		OutSeconds = Position;
		return true;
	}
	// The single-node fallback (elysium.NpcAnim 0). GetPosition answers 0 for a component with no
	// player at all, which is indistinguishable from a clip genuinely at frame 0 — so the presence
	// of a sequence is the test, not the value.
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
	if (UElysiumNpcAnimInstance* Inst = Cast<UElysiumNpcAnimInstance>(Body->GetAnimInstance()))
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
	if (UElysiumNpcAnimInstance* Inst = Cast<UElysiumNpcAnimInstance>(Body->GetAnimInstance()))
	{
		Inst->StopClip();
	}
	else
	{
		Body->Stop();
	}
}

int32 UElysiumEntityBodies::SetFlexControllers(USkeletalMeshComponent* Body,
	TArrayView<const FElysiumFlexWrite> Writes, TArray<FString>* OutMissing)
{
	// No host under `elysium.NpcAnim 0`, and no rig on a model with no facial sidecar. Both stand a
	// body with a still face rather than failing, so both answer the same way.
	UElysiumNpcAnimInstance* Inst = Body
		? Cast<UElysiumNpcAnimInstance>(Body->GetAnimInstance()) : nullptr;
	return Inst != nullptr ? Inst->SetFlexControllers(Writes, OutMissing) : INDEX_NONE;
}

bool UElysiumEntityBodies::SetMouthOpen(USkeletalMeshComponent* Body, float Open)
{
	UElysiumNpcAnimInstance* Inst = Body
		? Cast<UElysiumNpcAnimInstance>(Body->GetAnimInstance()) : nullptr;
	return Inst != nullptr && Inst->SetMouthOpen(Open);
}

bool UElysiumEntityBodies::GetPhonemeFilter(USkeletalMeshComponent* Body, float& OutMin,
	float& OutMax) const
{
	const UElysiumNpcAnimInstance* Inst = Body
		? Cast<UElysiumNpcAnimInstance>(Body->GetAnimInstance()) : nullptr;
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
	if (Body == nullptr)
	{
		return false;
	}
	for (FElysiumEyeBinding& Binding : EyeBindings)
	{
		if (Binding.Comp.Get() == Body)
		{
			Binding.ViewTarget = WorldTarget;
			Binding.bHasViewTarget = true;
			return true;
		}
	}
	// Most of the cast authors no eyeball record, so this is the ordinary answer rather than an
	// error: the character still decides where it is looking, there is simply nothing to aim.
	return false;
}

bool UElysiumEntityBodies::GetHeadFrame(USkeletalMeshComponent* Body, FVector& OutPosition,
	FVector& OutForward) const
{
	if (Body == nullptr)
	{
		return false;
	}
	for (const FElysiumEyeBinding& Binding : EyeBindings)
	{
		if (Binding.Comp.Get() != Body || Binding.HeadBoneIndex == INDEX_NONE)
		{
			continue;
		}
		// Component-space bone transform lifted to world. Read here rather than in the substrate
		// because this is the settled post-move pose, and because a bone index only means anything
		// beside the component it was resolved against.
		const FTransform BoneToWorld =
			Body->GetBoneTransform(Binding.HeadBoneIndex, Body->GetComponentTransform());
		OutPosition = BoneToWorld.GetLocation();
		// VtMB's head bone points down the model's own axis, not the character's facing, so the
		// forward the cone is measured along is the component's, rotated by the head bone's yaw.
		// Taking the bone's raw X would tilt the cone with every idle head bob.
		OutForward = BoneToWorld.GetRotation().GetForwardVector();
		return true;
	}
	return false;
}

bool UElysiumEntityBodies::RefreshNpcIdle(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& Disposition, int32 IdleVariant)
{
	const AActor* Owner = GetOwner();
	UGameInstance* GI = Owner ? Owner->GetGameInstance() : nullptr;
	UElysiumNpcAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumNpcAnimSubsystem>() : nullptr;
	if (Body == nullptr || Anims == nullptr)
	{
		return false;
	}
	EElysiumIdleTier Tier = EElysiumIdleTier::None;
	const FString Clip = Anims->PickIdleClip(Stem, Disposition, Tier, IdleVariant);
	return !Clip.IsEmpty() && PlayNpcClip(Body, Stem, Clip, /*bLoop=*/true, /*OutSeconds=*/nullptr);
}

void UElysiumEntityBodies::InstallEyes(USkeletalMeshComponent* Comp,
	const TSharedPtr<const FElysiumEyeSet>& Set, const FString& Disposition)
{
	if (Comp == nullptr || !Set.IsValid())
	{
		return;
	}
	UMaterialInterface* Master = ElysiumNpcVisual::EyeMaster();
	USkeletalMesh* Mesh = Comp->GetSkeletalMeshAsset();
	if (Master == nullptr || Mesh == nullptr)
	{
		return;
	}

	FElysiumEyeBinding Binding;
	Binding.Comp = Comp;
	Binding.Set = Set;
	Binding.Disposition = Disposition;

	// The head bone the gaze cone and the fidget grid are measured in. Resolved once, by name,
	// against this component's own skeleton: the exporter appends a synthetic root on models with
	// more than one parent-less bone, so the `.mdl`'s bone ordering is not the USkeleton's and an
	// index carried across from the sidecar would aim off the wrong bone. The eye records name the
	// bone they hang from, which for every rigged character is the head, so take it from there
	// rather than hardcoding a string.
	for (const FElysiumEyeball& Candidate : Set->Eyeballs)
	{
		if (Candidate.Bone.IsNone())
		{
			continue;
		}
		const int32 Index = Comp->GetBoneIndex(Candidate.Bone);
		if (Index != INDEX_NONE)
		{
			Binding.HeadBoneIndex = Index;
			break;
		}
	}

	const TArray<FSkeletalMaterial>& Slots = Mesh->GetMaterials();
	for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
	{
		// Two independent tests, because either alone can be defeated. The base-material test
		// survives any change to the plugin's slot naming; the name test says *which* eye.
		UMaterialInterface* Existing = Comp->GetMaterial(Slot);
		const bool bIsEye = Existing != nullptr && Existing->GetBaseMaterial() == Master;
		if (!bIsEye)
		{
			continue;
		}
		// glTFRuntime names a slot `LOD_<n>_Section_<n>_<glTF material name>`.
		const FString SlotName = Slots[Slot].MaterialSlotName.ToString();
		const FElysiumEyeball* Eye = nullptr;
		for (const FElysiumEyeball& Candidate : Set->Eyeballs)
		{
			if (!Candidate.Material.IsEmpty()
				&& SlotName.EndsWith(TEXT("_") + Candidate.Material, ESearchCase::IgnoreCase))
			{
				Eye = &Candidate;
				break;
			}
		}
		if (Eye == nullptr)
		{
			UE_LOG(LogElysiumBodies, Warning,
				TEXT("eyes '%s': slot %d ('%s') draws M_Eyes but matches no record"),
				*Set->Stem, Slot, *SlotName);
			continue;
		}

		UMaterialInstanceDynamic* Mid = Comp->CreateDynamicMaterialInstance(Slot);
		if (Mid == nullptr)
		{
			continue;
		}
		// The iris texture is the .vmt's `$iris`, not anything in the glb — the exporter decodes it
		// beside the mesh's own textures and names it here.
		if (!Eye->IrisTexture.IsEmpty())
		{
			const FString Dir = FPaths::GetPath(FElysiumContentPaths::NpcGlb(Set->Stem));
			if (UTexture2D* Iris = EyeTextures.LoadTex(Dir, Eye->IrisTexture, /*bSRGB=*/true))
			{
				Mid->SetTextureParameterValue(TEXT("IrisTexture"), Iris);
			}
		}
		Mid->SetScalarParameterValue(TEXT("Vampire"), Eye->bVampire ? 1.f : 0.f);

		FElysiumEyeSlot Bound;
		Bound.Mid = Mid;
		Bound.EyeIndex = Eye->Index;
		// Resolved once, by name: a synthetic skeleton root makes the .mdl's own bone index wrong
		// on some models, and the reference-skeleton index is what GetBoneTransform takes.
		Bound.BoneIndex = Comp->GetBoneIndex(Eye->Bone);
		if (Bound.BoneIndex == INDEX_NONE)
		{
			UE_LOG(LogElysiumBodies, Warning, TEXT("eyes '%s': bone '%s' is not on this skeleton"),
				*Set->Stem, *Eye->Bone.ToString());
			continue;
		}
		FVector Zero = FVector::ZeroVector;
		Mid->InitializeVectorParameterAndGetIndex(TEXT("IrisU"), FLinearColor(Zero), Bound.ParamIrisU);
		Mid->InitializeVectorParameterAndGetIndex(TEXT("IrisV"), FLinearColor(Zero), Bound.ParamIrisV);
		Mid->InitializeVectorParameterAndGetIndex(TEXT("IrisOrigin"), FLinearColor(Zero), Bound.ParamIrisOrigin);
		Mid->InitializeVectorParameterAndGetIndex(TEXT("NormalOrigin"), FLinearColor(Zero), Bound.ParamNormalOrigin);
		Mid->InitializeVectorParameterAndGetIndex(TEXT("EyeUpN"), FLinearColor(Zero), Bound.ParamEyeUp);
		Binding.Slots.Add(Bound);
	}

	if (!Binding.Slots.IsEmpty())
	{
		EyeBindings.Add(MoveTemp(Binding));
	}
}

void UElysiumEntityBodies::TickEyes(float)
{
	// Everything here reads this frame's settled component-space pose, which is why it runs in the
	// post-move pass rather than in the component's own tick: at TG_PrePhysics the transforms are
	// last frame's, and GetProxyOnGameThread would flush a live parallel evaluation.
	// Until 12.4's gaze cascade supplies a target, an eye sits on the record's own authored resting
	// aim — the state `bEyeMove` off produces, which is a real retail configuration but is NOT
	// guaranteed to point out of the face: it is whatever the model's QC authored.
	//
	// `elysium.EyeTrackPlayer` overrides that with the player's camera, which is the cheapest
	// unambiguous check that the basis math is right: if the irises converge on the camera as it
	// moves, the record, the import transform, the solve and the plane parameters are all correct.
	if (CVarEyes.GetValueOnGameThread() == 0 || EyeBindings.IsEmpty())
	{
		return;
	}
	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;

	// The blink cadence is content, and it is per disposition: most rows sit at 2.5/6.0 s, `Anger`
	// blinks slowly and `Error` — the row a character falls to when its own disposition does not
	// resolve — blinks fast enough to read as a tell.
	const AActor* Owner = GetOwner();
	const UGameInstance* GI = Owner ? Owner->GetGameInstance() : nullptr;
	UElysiumNpcAnimSubsystem* Anims = GI
		? const_cast<UGameInstance*>(GI)->GetSubsystem<UElysiumNpcAnimSubsystem>() : nullptr;

	// `elysium.EyeTrackPlayer` is the debug override, and it outranks the gaze cascade on purpose:
	// it is the cheapest unambiguous check that the basis math is right, and it has to keep working
	// when the cascade is the thing under suspicion.
	const bool bTrackPlayer = CVarEyeTrackPlayer.GetValueOnGameThread() != 0;
	FVector TrackWorld = FVector::ZeroVector;
	bool bHaveCamera = false;
	if (bTrackPlayer)
	{
		if (const APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0))
		{
			TrackWorld = Cam->GetCameraLocation();
			bHaveCamera = true;
		}
	}
	for (int32 i = EyeBindings.Num() - 1; i >= 0; --i)
	{
		FElysiumEyeBinding& Binding = EyeBindings[i];
		USkeletalMeshComponent* Comp = Binding.Comp.Get();
		if (Comp == nullptr || !Binding.Set.IsValid())
		{
			EyeBindings.RemoveAtSwap(i);
			continue;
		}
		// Retail runs the eye pass per *drawn* model, so skipping an unseen body is faithful as
		// well as cheap.
		if (!Comp->WasRecentlyRendered(0.2f))
		{
			continue;
		}
		// Blink: schedule, then evaluate the envelope. The interval is content — retail reads it
		// from `vdata/system/dispositiontable.txt`, which the disposition table already carries.
		float BlinkMin = 2.5f;
		float BlinkMax = 6.f;
		if (Anims != nullptr)
		{
			if (const FElysiumDisposition* Row = Anims->GetDispositions().Resolve(Binding.Disposition))
			{
				BlinkMin = Row->MinBlinkInterval;
				BlinkMax = Row->MaxBlinkInterval;
			}
		}
		FElysiumEyeInput EyeInput;
		if (Binding.NextBlinkTime <= 0.f)
		{
			Binding.NextBlinkTime = Now + FMath::FRandRange(BlinkMin, BlinkMax);
		}
		else if (Now >= Binding.NextBlinkTime)
		{
			Binding.BlinkEndsAt = Now + ElysiumEyes::BlinkSeconds;
			Binding.NextBlinkTime = Now + FMath::FRandRange(BlinkMin, BlinkMax);
		}
		EyeInput.Blink = ElysiumEyes::BlinkWeight(Binding.BlinkEndsAt - Now);

		// Where this body is looking, in priority order: the debug override, then the gaze the
		// substrate pushed for this character, then nothing — which leaves the eye on the record's
		// own authored resting aim, the state retail produces with `bEyeMove` off.
		FElysiumEyeTuning Tuning;
		FVector GazeWorld = FVector::ZeroVector;
		if (bTrackPlayer && bHaveCamera)
		{
			Tuning.bEyeMove = true;
			GazeWorld = TrackWorld;
		}
		else if (Binding.bHasViewTarget)
		{
			Tuning.bEyeMove = true;
			GazeWorld = Binding.ViewTarget;
		}

		for (const FElysiumEyeSlot& Slot : Binding.Slots)
		{
			UMaterialInstanceDynamic* Mid = Slot.Mid.Get();
			const FElysiumEyeball* Eye = Binding.Set->Find(Slot.EyeIndex);
			if (Mid == nullptr || Eye == nullptr)
			{
				continue;
			}
			// Component space throughout: the material measures its planes from the component
			// origin, and it keeps the arithmetic away from large world coordinates.
			const FTransform BoneToComponent = Comp->GetBoneTransform(Slot.BoneIndex, FTransform::Identity);
			const FVector Target = Tuning.bEyeMove
				? Comp->GetComponentTransform().InverseTransformPosition(GazeWorld)
				: FVector::ZeroVector;
			FElysiumEyeState State;
			ElysiumEyes::BuildState(*Eye, BoneToComponent, Target, Tuning, State);
			if (!State.bValid)
			{
				continue;
			}
			Mid->SetVectorParameterByIndex(Slot.ParamIrisU, FLinearColor(State.IrisU));
			Mid->SetVectorParameterByIndex(Slot.ParamIrisV, FLinearColor(State.IrisV));
			Mid->SetVectorParameterByIndex(Slot.ParamIrisOrigin, FLinearColor(State.Org));
			Mid->SetVectorParameterByIndex(Slot.ParamNormalOrigin, FLinearColor(State.NormalOrg));
			Mid->SetVectorParameterByIndex(Slot.ParamEyeUp, FLinearColor(State.AuthoredUp));

			// The lid half of the same pass. Carried in the eye bone's own space, with the record's
			// lid fields beside it, so the flex rig needs no eye state of its own.
			if (Slot.EyeIndex >= 0 && Slot.EyeIndex < 2)
			{
				EyeInput.Eyes[Slot.EyeIndex].FromRecord(*Eye, State);
			}
		}

		// One write per body per frame, which is what re-evaluates the face. A body with no flex
		// rig answers false and keeps its aiming irises — the player-body case.
		if (UElysiumNpcAnimInstance* Inst = Cast<UElysiumNpcAnimInstance>(Comp->GetAnimInstance()))
		{
			Inst->SetEyeInput(EyeInput);
		}
	}
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
	UElysiumNpcAnimSubsystem* Anims = nullptr;
	if (UGameInstance* GI = Owner->GetGameInstance())
	{
		Anims = GI->GetSubsystem<UElysiumNpcAnimSubsystem>();
	}
	TSharedPtr<const FElysiumEyeSet> EyeSet = Anims ? Anims->GetEyeSet(Stem) : nullptr;
	USkeletalMesh* Mesh = ResolveNpcMesh(Stem, bPlayerMaterial);
	if (Mesh == nullptr)
	{
		return nullptr;
	}

	// The standing idle. Two NPCs sharing a model can carry different dispositions and different
	// variants, so the pick is per (stem, disposition, variant) — but the resolved clip caches per
	// (stem, clip), so a crowd spread across three stance idles still resolves three sequences,
	// not one per NPC.
	FString IdleClip;
	{
		if (Anims != nullptr)
		{
			EElysiumIdleTier Tier = EElysiumIdleTier::None;
			IdleClip = Anims->PickIdleClip(Stem, Disposition, Tier, IdleVariant);
			UE_LOG(LogElysiumBodies, Verbose, TEXT("npc '%s' idle: %s (%s, disposition '%s', variant %d)"),
				*Stem, IdleClip.IsEmpty() ? TEXT("<none>") : *IdleClip,
				UElysiumNpcAnimSubsystem::TierName(Tier),
				Disposition.IsEmpty() ? TEXT("<unset>") : *Disposition, IdleVariant);
		}
	}

	// Standard runtime-component recipe (mirrors BuildBrushBody): NewObject → attach → place →
	// RegisterComponent. The hulls-body path uses relative placement against the root at world origin;
	// NPC origins are the same Unreal-space verbatim values, so relative == world here.
	USkeletalMeshComponent* Comp = NewObject<USkeletalMeshComponent>(Owner);
	// Visual-only meshes follow a pawn/motor or mover; they are never navigation geometry. Set this
	// before the mesh and transform so none of those property changes can enqueue an octree update.
	Comp->SetCanEverAffectNavigation(false);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetSkeletalMeshAsset(Mesh);
	Comp->SetupAttachment(Root);
	Comp->SetRelativeLocation(Location);
	Comp->SetRelativeRotation(Rotation);
	if (UniformScale != 1.f)
	{
		Comp->SetRelativeScale3D(FVector(UniformScale));
	}
	// The animation host is installed before the first clip, so it owns the pose from frame one and
	// every later change (stance, gesture, scripted sequence) crossfades instead of popping.
	// `elysium.NpcAnim 0` drops back to the single-node instance for an A/B.
	if (CVarNpcAnim.GetValueOnGameThread() != 0)
	{
		Comp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		Comp->SetAnimInstanceClass(UElysiumNpcAnimInstance::StaticClass());
	}
	Comp->RegisterComponent();
	// The visible mesh never collides; mobile NPCs wrap it in a native character capsule.
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Owner->AddInstanceComponent(Comp);
	// The face (12.3). A model with no facial sidecar gets a null rig and animates with a still
	// face — the normal case for animals, crowd bodies and every player body, none of which carry
	// flex data. Nothing drives the controllers yet: scene expressions are 12.1's and lipsync 12.5's.
	if (UElysiumNpcAnimInstance* Inst = Cast<UElysiumNpcAnimInstance>(Comp->GetAnimInstance()))
	{
		if (Anims != nullptr)
		{
			Inst->SetFacialRig(Anims->GetFacialRig(Stem));
			// The two composition stages (CAP7.2). Null for a model declaring neither a split
			// bone nor a procedural rule, which poses under Unreal's own hierarchy alone.
			Inst->SetCompositionRig(Anims->GetCompositionRig(Stem));
			// The garment spike. Gated on the same predicate the mesh loader used, so the rig is
			// installed only onto a body actually wearing the enhanced mesh — chains naming a
			// lattice the faithful skeleton does not carry would resolve to nothing and cost a
			// per-frame walk to discover it.
			if (ElysiumNpcVisual::UseClothMesh(Stem))
			{
				Inst->SetClothRig(Anims->GetClothRig(Stem));
			}
		}
	}
	// The eyes (12.4). Independent of the facial rig above: a player body binds eyes here and no
	// flex rig at all, which is the shipped state for 57 of the 59 of them.
	InstallEyes(Comp, EyeSet, Disposition);
	if (!IdleClip.IsEmpty())
	{
		PlayNpcClip(Comp, Stem, IdleClip, /*bLoop=*/true, /*OutSeconds=*/nullptr);
	}
	return Comp;
}

bool UElysiumEntityBodies::PlayNpcActivity(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& Activity, int32 Variant, bool bLoop, float* OutSeconds)
{
	AActor* Owner = GetOwner();
	UGameInstance* GI = Owner ? Owner->GetGameInstance() : nullptr;
	UElysiumNpcAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumNpcAnimSubsystem>() : nullptr;
	const FString Clip = Anims ? Anims->PickActivityClip(Stem, Activity, Variant) : FString();
	return !Clip.IsEmpty() && PlayNpcClip(Body, Stem, Clip, bLoop, OutSeconds);
}

bool UElysiumEntityBodies::ResolveNpcActivityClip(const FString& Stem, const FString& Activity,
	int32 Variant, FString& OutLabel, FString& OutAnimName, float& OutGroundSpeedCmPerSecond)
{
	AActor* Owner = GetOwner();
	UGameInstance* GI = Owner ? Owner->GetGameInstance() : nullptr;
	UElysiumNpcAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumNpcAnimSubsystem>() : nullptr;
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

FString UElysiumEntityBodies::AnimatedPropStemForModel(const FString& ModelPath) const
{
	const AActor* Owner = GetOwner();
	UGameInstance* GI = Owner ? Owner->GetGameInstance() : nullptr;
	UElysiumNpcAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumNpcAnimSubsystem>() : nullptr;
	const FElysiumAnimatedPropEntry* Entry = Anims
		? Anims->GetIndex().FindAnimatedProp(ModelPath) : nullptr;
	return Entry ? Entry->Stem : FString();
}

const FElysiumAnimatedPropEntry* UElysiumEntityBodies::FindAnimatedPropEntry(const FString& Stem) const
{
	const AActor* Owner = GetOwner();
	UGameInstance* GI = Owner ? Owner->GetGameInstance() : nullptr;
	UElysiumNpcAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumNpcAnimSubsystem>() : nullptr;
	return Anims ? Anims->GetIndex().AnimatedProps.Find(Stem) : nullptr;
}

FString UElysiumEntityBodies::AnimatedPropRestClip(const FString& Stem) const
{
	const FElysiumAnimatedPropEntry* Entry = FindAnimatedPropEntry(Stem);
	return Entry ? Entry->RestSequence() : FString();
}

bool UElysiumEntityBodies::FindAnimatedPropClip(const FString& Stem, const FString& ClipName,
	bool& bOutLoops) const
{
	const FElysiumAnimatedPropEntry* Entry = FindAnimatedPropEntry(Stem);
	const FElysiumPropClip* Clip = Entry ? Entry->FindClip(ClipName) : nullptr;
	bOutLoops = Clip && Clip->IsLooping();
	return Clip != nullptr;
}

USkeletalMeshComponent* UElysiumEntityBodies::BuildAnimatedPropVisual(const FString& Stem,
	const FVector& Location, const FQuat& Rotation, float UniformScale)
{
	AActor* Owner = GetOwner();
	USceneComponent* Root = Owner ? Owner->GetRootComponent() : nullptr;
	UGameInstance* GI = Owner ? Owner->GetGameInstance() : nullptr;
	UElysiumNpcAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumNpcAnimSubsystem>() : nullptr;
	const FElysiumAnimatedPropEntry* Entry = Anims ? Anims->GetIndex().AnimatedProps.Find(Stem) : nullptr;
	if (!Root || !Entry)
	{
		return nullptr;
	}

	USkeletalMesh* Mesh = nullptr;
	if (const TObjectPtr<USkeletalMesh>* Cached = AnimatedPropMeshCache.Find(Stem))
	{
		Mesh = Cached->Get();
	}
	if (!Mesh)
	{
		UglTFRuntimeAsset* Asset = nullptr;
		FString Error;
		Mesh = ElysiumNpcVisual::LoadMeshFromPath(
			FElysiumContentPaths::AnimatedPropGlb(Entry->Glb), Asset, Error);
		if (!Mesh)
		{
			UE_LOG(LogElysiumBodies, Warning, TEXT("animated prop '%s': %s"), *Stem, *Error);
			return nullptr;
		}
		AnimatedPropMeshCache.Add(Stem, Mesh);
		AnimatedPropAssetCache.Add(Stem, Asset);

		// Widen the bind-pose bounds to the furthest reach of any clip this model owns, once, here —
		// see ElysiumPropBounds. The union rather than the playing clip's own radius: one mesh is
		// cached per stem and serves every prop standing that model, and the haven stake's five
		// entities play five different clips off this one asset. `GetImportedBounds` stays the true
		// bind pose; only the extended bounds move.
		double ReachCm = 0.0;
		for (const FElysiumPropClip& Clip : Entry->Clips)
		{
			// glTFRuntime loads with SceneScale 100 (ElysiumNpcVisual's AssetConfig), so the mesh is
			// in centimetres while the index states the reach in the glb's own metres.
			ReachCm = FMath::Max(ReachCm, static_cast<double>(Clip.BoundsRadiusMeters) * 100.0);
		}
		FVector Positive, Negative;
		if (ElysiumPropBounds::ExtensionFor(Mesh->GetImportedBounds(), ReachCm, Positive, Negative))
		{
			Mesh->SetPositiveBoundsExtension(Positive);
			Mesh->SetNegativeBoundsExtension(Negative);
			Mesh->CalculateExtendedBounds();
			UE_LOG(LogElysiumBodies, Verbose,
				TEXT("animated prop '%s': bounds %.0f -> %.0f cm for a %.0f cm clip reach"),
				*Stem, Mesh->GetImportedBounds().SphereRadius, Mesh->GetBounds().SphereRadius, ReachCm);
		}
	}

	USkeletalMeshComponent* Comp = NewObject<USkeletalMeshComponent>(Owner);
	Comp->SetCanEverAffectNavigation(false);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetSkeletalMeshAsset(Mesh);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetupAttachment(Root);
	Comp->SetRelativeLocationAndRotation(Location, Rotation);
	if (UniformScale != 1.0f)
	{
		Comp->SetRelativeScale3D(FVector(UniformScale));
	}
	if (CVarNpcAnim.GetValueOnGameThread() != 0)
	{
		Comp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		Comp->SetAnimInstanceClass(UElysiumNpcAnimInstance::StaticClass());
	}
	Comp->RegisterComponent();
	Owner->AddInstanceComponent(Comp);
	// A skeletal prop declares the same two composition stages a character does — 19 of them carry
	// a rule table — so it takes the same install (CAP7.2).
	if (UElysiumNpcAnimInstance* Inst = Cast<UElysiumNpcAnimInstance>(Comp->GetAnimInstance()))
	{
		Inst->SetCompositionRig(Anims->GetAnimatedPropCompositionRig(Entry->Model));
	}
	return Comp;
}

UAnimSequence* UElysiumEntityBodies::ResolveAnimatedPropClip(USkeletalMesh* Mesh,
	const FString& Stem, const FString& ClipName)
{
	if (!Mesh || Stem.IsEmpty() || ClipName.IsEmpty())
	{
		return nullptr;
	}
	const AActor* Owner = GetOwner();
	UGameInstance* GI = Owner ? Owner->GetGameInstance() : nullptr;
	UElysiumNpcAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumNpcAnimSubsystem>() : nullptr;
	const FElysiumAnimatedPropEntry* Entry = Anims ? Anims->GetIndex().AnimatedProps.Find(Stem) : nullptr;
	if (!Entry || !Entry->HasClip(ClipName))
	{
		return nullptr;
	}

	// CAP7.3 — collapse a grid label to its cell BEFORE the key is built, or the label would cache one
	// cell forever and no pose parameter could ever move it. `wolf_form` is the one skeletal prop
	// declaring a grid; every other prop label resolves to itself.
	const FString AnimName = Anims->ResolveGridClip(Stem, ClipName);
	const FString Key = Stem + TEXT("|") + AnimName.ToLower();
	UAnimSequence* Anim = nullptr;
	if (const TObjectPtr<UAnimSequence>* Cached = AnimatedPropAnimCache.Find(Key))
	{
		Anim = Cached->Get();
	}
	else
	{
		const TObjectPtr<UglTFRuntimeAsset>* Asset = AnimatedPropAssetCache.Find(Stem);
		FString Error;
		if (Asset)
		{
			Anim = ElysiumNpcVisual::RetargetClip(Asset->Get(), Mesh, AnimName, Error);
		}
		if (!Anim)
		{
			UE_LOG(LogElysiumBodies, Warning, TEXT("animated prop '%s' clip '%s': %s"),
				*Stem, *ClipName, Error.IsEmpty() ? TEXT("asset not loaded") : *Error);
		}
		AnimatedPropAnimCache.Add(Key, Anim);
	}
	return Anim;
}

bool UElysiumEntityBodies::PlayAnimatedPropClip(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& ClipName, bool bLoop, float* OutSeconds)
{
	UAnimSequence* Anim = Body
		? ResolveAnimatedPropClip(Body->GetSkeletalMeshAsset(), Stem, ClipName)
		: nullptr;
	if (Anim == nullptr)
	{
		return false;
	}
	if (OutSeconds)
	{
		*OutSeconds = Anim->GetPlayLength();
	}
	if (UElysiumNpcAnimInstance* Inst = Cast<UElysiumNpcAnimInstance>(Body->GetAnimInstance()))
	{
		Inst->PlayClip(Anim, bLoop);
	}
	else
	{
		Body->PlayAnimation(Anim, bLoop);
	}
	return true;
}

int32 UElysiumEntityBodies::PreloadAnimatedPropClips(USkeletalMeshComponent* Body,
	const FString& Stem)
{
	if (!Body || Stem.IsEmpty())
	{
		return 0;
	}
	const FElysiumAnimatedPropEntry* Entry = FindAnimatedPropEntry(Stem);
	USkeletalMesh* Mesh = Body->GetSkeletalMeshAsset();
	if (!Entry || !Mesh)
	{
		return 0;
	}
	int32 Resolved = 0;
	for (const FElysiumPropClip& Clip : Entry->Clips)
	{
		Resolved += ResolveAnimatedPropClip(Mesh, Stem, Clip.Name) != nullptr ? 1 : 0;
	}
	return Resolved;
}

int32 UElysiumEntityBodies::FinishAnimationPreload()
{
	TSet<UAnimSequence*> Unique;
	for (const TPair<FString, TObjectPtr<UAnimSequence>>& Pair : NpcAnimCache)
	{
		if (Pair.Value)
		{
			Unique.Add(Pair.Value.Get());
		}
	}
	for (const TPair<FString, TObjectPtr<UAnimSequence>>& Pair : AnimatedPropAnimCache)
	{
		if (Pair.Value)
		{
			Unique.Add(Pair.Value.Get());
		}
	}
	TArray<UAnimSequence*> Sequences = Unique.Array();
#if WITH_EDITOR
	if (!Sequences.IsEmpty())
	{
		UE::Anim::IAnimSequenceCompilingManager::FinishCompilation(Sequences);
	}
#endif
	return Sequences.Num();
}

void UElysiumEntityBodies::ApplyAnimatedPropSkin(USkeletalMeshComponent* Comp,
	const FString& Stem, int32 Family)
{
	if (!Comp || CVarPropSkins.GetValueOnGameThread() == 0)
	{
		return;
	}
	// The skin set is baked per map, so a stage world (no map, hence no name) has none to read and
	// the prop draws its own authored material set.
	if (!bPropSkinsLoaded && !MapName.IsEmpty())
	{
		bPropSkinsLoaded = true;
		PropSkins = LoadObject<UElysiumPropSkinSet>(
			nullptr, *FElysiumContentPaths::BakedPropSkins(MapName));
	}
	Comp->EmptyOverrideMaterials();
	const FElysiumSkinFamily* Row = PropSkins ? PropSkins->Find(FName(*Stem), Family) : nullptr;
	if (!Row)
	{
		return;
	}
	for (const FElysiumSkinOverride& Override : Row->Overrides)
	{
		const int32 Slot = Comp->GetMaterialIndex(Override.SlotName);
		if (Override.Material && Slot != INDEX_NONE)
		{
			Comp->SetMaterial(Slot, Override.Material);
		}
	}
}

// The bake already produced each BSP submodel as a local-pivot asset, so the moving collision body
// can carry the same authored surfaces without rebuilding render geometry at runtime. Cached per
// stem; a failed load is not cached, so it can retry after a bake.
UStaticMesh* UElysiumEntityBodies::ResolveBrushMesh(const FString& Stem)
{
	const TObjectPtr<UStaticMesh>* Cached = BrushMeshCache.Find(Stem);
	if (UStaticMesh* Mesh = Cached ? Cached->Get() : nullptr)
	{
		return Mesh;
	}
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(
		nullptr, *FElysiumContentPaths::BakedBrushMesh(MapName, Stem));
	if (!Mesh)
	{
		UE_LOG(LogElysiumBodies, Warning,
			TEXT("brush '%s': no baked mesh (run: uv run elysium export map %s --force)"), *Stem, *MapName);
		return nullptr;
	}
	BrushMeshCache.Add(Stem, Mesh);
	return Mesh;
}

UStaticMeshComponent* UElysiumEntityBodies::BuildBrushVisual(const FString& Stem,
	USceneComponent* ParentBody, float UniformScale, bool bSky)
{
	AActor* Owner = GetOwner();
	if (!Owner || !ParentBody || Stem.IsEmpty())
	{
		return nullptr;
	}
	UStaticMesh* Mesh = ResolveBrushMesh(Stem);
	if (!Mesh)
	{
		return nullptr;
	}

	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(Owner);
	Comp->SetCanEverAffectNavigation(false);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetStaticMesh(Mesh);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetupAttachment(ParentBody);
	Comp->SetRelativeLocationAndRotation(FVector::ZeroVector, FQuat::Identity);
	if (UniformScale != 1.f)
	{
		Comp->SetRelativeScale3D(FVector(UniformScale));
	}
	if (bSky)
	{
		Comp->SetCastShadow(false);
		Comp->SetVisibleInRayTracing(false);
	}
	Comp->RegisterComponent();
	Owner->AddInstanceComponent(Comp);
	return Comp;
}

UStaticMesh* UElysiumEntityBodies::ResolvePropMesh(const FString& Stem)
{
	const TObjectPtr<UStaticMesh>* Cached = PropMeshCache.Find(Stem);
	if (UStaticMesh* Mesh = Cached ? Cached->Get() : nullptr)
	{
		return Mesh;
	}
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *FElysiumContentPaths::BakedPropMesh(MapName, Stem));
	if (Mesh == nullptr)
	{
		UE_LOG(LogElysiumBodies, Warning, TEXT("prop '%s': no baked mesh (run: uv run elysium export map %s --force)"),
			*Stem, *MapName);
		return nullptr;
	}
	PropMeshCache.Add(Stem, Mesh);
	return Mesh;
}

UStaticMeshComponent* UElysiumEntityBodies::BuildPropVisual(const FString& Stem, const FVector& Location,
	const FQuat& Rotation, float UniformScale)
{
	AActor* Owner = GetOwner();
	USceneComponent* Root = Owner ? Owner->GetRootComponent() : nullptr;
	if (Stem.IsEmpty() || Root == nullptr)
	{
		return nullptr;
	}

	UStaticMesh* Mesh = ResolvePropMesh(Stem);
	if (Mesh == nullptr)
	{
		return nullptr;
	}

	// Standard runtime-component recipe (mirrors BuildNpcVisual): NewObject → mesh → attach → place →
	// register. The map actor sits at the origin, so relative == world for these Unreal-space values.
	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(Owner);
	Comp->SetCanEverAffectNavigation(false);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetStaticMesh(Mesh);
	// prop_dynamic is visual-only (8.3); prop_physics owns collision (8.4). The baked mesh carries
	// collision geometry for the props that do need it, so it is switched off here per component.
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetupAttachment(Root);
	Comp->SetRelativeLocationAndRotation(Location, Rotation);
	// B7 — a 3D-skybox body is the miniature at its own scale: scenery the player can never
	// reach, so it is never solid, casts nothing, and stays out of the ray-tracing scene (a mesh
	// blown up 16x overlaps the whole playable space, the canonical HWRT overlap cost).
	if (UniformScale != 1.f)
	{
		Comp->SetRelativeScale3D(FVector(UniformScale));
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCastShadow(false);
		Comp->SetVisibleInRayTracing(false);
	}
	Comp->RegisterComponent();
	Owner->AddInstanceComponent(Comp);
	return Comp;
}

void UElysiumEntityBodies::ApplyPropSkin(UStaticMeshComponent* Comp, const FString& Stem, int32 Family)
{
	UStaticMesh* Mesh = Comp ? Comp->GetStaticMesh() : nullptr;
	if (!Mesh || CVarPropSkins.GetValueOnGameThread() == 0)
	{
		return;
	}

	if (!bPropSkinsLoaded)
	{
		bPropSkinsLoaded = true;
		PropSkins = LoadObject<UElysiumPropSkinSet>(
			nullptr, *FElysiumContentPaths::BakedPropSkins(MapName));
	}

	// Restore first, so a swap back to skin 0 -- or to a family this model does not carry, which
	// Source draws as the authored set -- undoes whatever the previous skin painted. Clearing the
	// overrides puts every slot back on the mesh's own material.
	Comp->EmptyOverrideMaterials();
	const FElysiumSkinFamily* Row = PropSkins ? PropSkins->Find(FName(*Stem), Family) : nullptr;
	if (!Row)
	{
		return;
	}

	for (const FElysiumSkinOverride& Override : Row->Overrides)
	{
		if (!Override.Material)
		{
			continue;
		}
		// Every prop body stands a baked mesh, whose slots the bake named safe_name(material) --
		// the same key the skin table is written with, so the slot name resolves directly.
		const int32 Slot = Mesh->GetMaterialIndex(Override.SlotName);
		if (Slot != INDEX_NONE)
		{
			Comp->SetMaterial(Slot, Override.Material);
		}
	}
}

UStaticMeshComponent* UElysiumEntityBodies::BuildPhysPropVisual(const FString& Stem, const FVector& Location,
	const FQuat& Rotation, float UniformScale)
{
	AActor* Owner = GetOwner();
	USceneComponent* Root = Owner ? Owner->GetRootComponent() : nullptr;
	if (Stem.IsEmpty() || Root == nullptr)
	{
		return nullptr;
	}

	// The same baked asset every other prop stands: the bake gave a physics model VtMB's own
	// convex collision (props/<stem>.phys, one shape per `.phy` ledge) and its authored mass on
	// the body setup, under CTF_UseSimpleAndComplex — so one mesh serves both a simulating body
	// and a static placement of the same model, and the cache is shared with BuildPropVisual.
	UStaticMesh* Mesh = ResolvePropMesh(Stem);
	if (Mesh == nullptr)
	{
		return nullptr;
	}

	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(Owner);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetStaticMesh(Mesh);
	Comp->SetupAttachment(Root);
	Comp->SetRelativeLocationAndRotation(Location, Rotation);
	// Collide as a physics body (blocks the world's BlockAll hull colliders). Simulation, mass and
	// the elysium.PhysicsProps gate are the leaf's call — the body stands here inert until it decides.
	Comp->SetCollisionProfileName(TEXT("PhysicsActor"));
	// B7 — a 3D-skybox body is the miniature at its own scale: scenery the player can never
	// reach, so it is never solid, casts nothing, and stays out of the ray-tracing scene (a mesh
	// blown up 16x overlaps the whole playable space, the canonical HWRT overlap cost).
	if (UniformScale != 1.f)
	{
		Comp->SetRelativeScale3D(FVector(UniformScale));
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCastShadow(false);
		Comp->SetVisibleInRayTracing(false);
	}
	Comp->RegisterComponent();
	Owner->AddInstanceComponent(Comp);
	return Comp;
}
