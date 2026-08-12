#include "Visual/ElysiumEntityBodies.h"

#include "ElysiumContentPaths.h"
#include "ElysiumPropSkins.h"
#include "Visual/ElysiumAnimLayerMask.h"
#include "Visual/ElysiumBipedAnimInstance.h"
#include "Visual/ElysiumAnimSubsystem.h"
#include "Visual/ElysiumNpcVisual.h"
#include "ElysiumStanceTypes.h"
#include "Substrate/ElysiumDisposition.h"
#include "Substrate/ElysiumRulebookSubsystem.h"

#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
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
#include "Misc/App.h"

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

	const AActor* Owner = GetOwner();
	UGameInstance* GI = Owner ? Owner->GetGameInstance() : nullptr;
	UElysiumAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;

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
	const AActor* Owner = GetOwner();
	UGameInstance* GI = Owner ? Owner->GetGameInstance() : nullptr;
	UElysiumAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
	const FElysiumNpcClipSet* Set = Anims ? Anims->GetClipSet(Stem) : nullptr;
	const FElysiumNpcClip* Clip = Set ? Set->Find(ClipName) : nullptr;
	// A clip the vocabulary does not carry — a bank clip reached by name, a prop, the green room —
	// transitions on the shipped default rather than snapping, which is what 5,762 of the 5,836
	// shipped sequences authored anyway.
	return Clip != nullptr ? Clip->FadeSeconds() : UElysiumBodyAnimInstance::DefaultBlendSeconds;
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
		return false;
	}
	Inst->PlayOneShot(Anim, bLoop, ClipFadeSeconds(Stem, ClipName));
	return true;
}

bool UElysiumEntityBodies::PlayNpcLayer(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& ClipName, float Weight, FString* OutError)
{
	// Every refusal below says which one it was. Five paths answered one bare `false` before, and a
	// caller cannot tell "this body has no graph" from "this label is not in the vocabulary" from
	// "the mount has no asset" — three different fixes behind one silence.
	auto Refuse = [OutError](FString&& Why) -> bool
	{
		if (OutError != nullptr)
		{
			*OutError = MoveTemp(Why);
		}
		return false;
	};

	UElysiumBipedAnimInstance* Inst = Body
		? Cast<UElysiumBipedAnimInstance>(Body->GetAnimInstance()) : nullptr;
	if (Inst == nullptr)
	{
		return Refuse(TEXT("this body has no biped animation host"));
	}
	if (!Inst->HasCompiledGraph())
	{
		// The layer is composed by the graph's own layered blend (CCC10), so a body with no compiled
		// graph has nowhere to put one — the same refusal `PlayNpcGrid` gives for the same reason.
		return Refuse(TEXT("this body is on the native host, which carries no compiled graph — the "
			"generated ABP is not on the mount. Run `uv run elysium export bundle policy`"));
	}

	const AActor* Owner = GetOwner();
	UGameInstance* GI = Owner ? Owner->GetGameInstance() : nullptr;
	UElysiumAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
	if (Anims == nullptr)
	{
		return Refuse(TEXT("no animation subsystem"));
	}

	USkeletalMesh* Mesh = Body->GetSkeletalMeshAsset();
	const FElysiumNpcClipSet* Set = Anims->GetClipSet(Stem);
	const FElysiumNpcClip* LayerClip = Set != nullptr ? Set->Find(ClipName) : nullptr;
	if (LayerClip == nullptr)
	{
		return Refuse(FString::Printf(
			TEXT("'%s' is not in %s's vocabulary (%d clips)"), *ClipName, *Stem,
			Set != nullptr ? Set->Clips.Num() : 0));
	}
	const FString LayerOwner = LayerClip->IsOwnedBy(Stem) ? Stem : LayerClip->Owner;

	// **A masked overlay ships once per DECLARING HOST, not under its plain label.** An overlay whose
	// mask owns the shared ancestor split bone has to be written against the chain that bone's
	// rotation is expressed in, and that chain is the host's — so the exporter emits it as
	// `<clip>@<host>` per host and suppresses the raw form outright, because ordinary FK reads the
	// raw one as the upper body folded about the waist. Asking for the plain label therefore finds
	// nothing for every aim layer, which is not a missing export.
	//
	// The host is whichever one the model's own autolayer table binds this layer to. Sorted, so the
	// same click stands the same derived asset twice running.
	FString Host;
	if (const TSharedPtr<const FElysiumBlendTable> Table = Anims->GetBlendTable(LayerOwner))
	{
		TArray<FString> Hosts;
		for (const TPair<FString, FElysiumAutoLayerBinding>& Entry : Table->AutoLayers)
		{
			if (Entry.Value.Clips.Contains(ClipName))
			{
				Hosts.Add(Entry.Key);
			}
		}
		Hosts.Sort();
		Host = Hosts.IsEmpty() ? FString() : Hosts[0];
	}

	// An aim grid stands as a blend space and a melee overlay as a plain sequence — the same two
	// shapes the resolver's own layer path produces, decided the same way: does the label name a grid.
	if (UBlendSpace* Space = ElysiumNpcVisual::LoadBakedBlendSpace(Mesh, LayerOwner, ClipName, Host))
	{
		// Every cell of a grid shares one mask (`docs/vtmb/animation_and_movers.md` A.4), so the
		// first sample that carries one names the whole grid's.
		FName MaskName;
		for (const FBlendSample& Sample : Space->GetBlendSamples())
		{
			if (Sample.Animation != nullptr)
			{
				if (const UElysiumAnimLayerMask* Mask =
					Sample.Animation->FindMetaDataByClass<UElysiumAnimLayerMask>())
				{
					MaskName = Mask->Profile;
					break;
				}
			}
		}
		Inst->ArmDebugUpperBodyOverlay(nullptr, Space, MaskName, Weight);
		return true;
	}

	// The derived form first for the same reason, then the plain label — an additive ships both ways
	// and a mask-free overlay ships only plain, so trying both covers either without knowing which.
	UAnimSequence* Anim = Host.IsEmpty() ? nullptr
		: ElysiumNpcVisual::LoadBakedClip(Mesh, LayerOwner,
			FString::Printf(TEXT("%s@%s"), *ClipName, *Host));
	if (Anim == nullptr)
	{
		Anim = ResolveNpcClip(Stem, ClipName, Mesh);
	}
	if (Anim == nullptr)
	{
		return Refuse(FString::Printf(
			TEXT("'%s' is owned by '%s' but neither its grid, its derived form '%s@%s' nor its plain "
				"label is on the mount (host %s)"),
			*ClipName, *LayerOwner, *ClipName, *Host,
			Host.IsEmpty() ? TEXT("was not found in the owner's autolayer table") : *Host));
	}
	// The same two-sided gate the retired accumulator carried, and it still keeps the composition
	// honest: an additive is read as a delta and needs no mask, while an ordinary layer is read as a
	// pose and is meaningless without one — composed unmasked it would pull every bone it does not
	// own toward the reference pose and lose the body's stance from the waist down.
	if (Anim->IsValidAdditive())
	{
		Inst->ArmDebugUpperBodyAdditive(Anim, Weight);
		return true;
	}
	if (const UElysiumAnimLayerMask* Mask = Anim->FindMetaDataByClass<UElysiumAnimLayerMask>())
	{
		Inst->ArmDebugUpperBodyOverlay(Anim, nullptr, Mask->Profile, Weight);
		return true;
	}
	return Refuse(FString::Printf(
		TEXT("'%s' loaded but is neither additive nor masked — an unmasked pose clip cannot be a "
			"layer, it would drag every bone it does not own to the reference pose"),
		*ClipName));
}

void UElysiumEntityBodies::SetNpcLayerAim(USkeletalMeshComponent* Body, float Yaw, float Pitch)
{
	if (UElysiumBipedAnimInstance* Inst = Body
		? Cast<UElysiumBipedAnimInstance>(Body->GetAnimInstance()) : nullptr)
	{
		Inst->SetDebugUpperBodyAim(Yaw, Pitch);
	}
}

bool UElysiumEntityBodies::PlayNpcGrid(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& ClipName, FElysiumResolvedGrid& OutGrid)
{
	OutGrid = FElysiumResolvedGrid();
	const AActor* Owner = GetOwner();
	UGameInstance* GI = Owner ? Owner->GetGameInstance() : nullptr;
	UElysiumAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
	UElysiumBipedAnimInstance* Inst = Body
		? Cast<UElysiumBipedAnimInstance>(Body->GetAnimInstance()) : nullptr;
	if (Anims == nullptr || Inst == nullptr || !Inst->HasCompiledGraph())
	{
		// A grid is stood by publishing a selection that names it, so it needs the graph's own
		// blend-space player. A body with no compiled graph has nowhere to put one.
		return false;
	}
	if (!Anims->ResolveGrid(Stem, ClipName, Body->GetSkeletalMeshAsset(), OutGrid))
	{
		return false;
	}

	// The mirror of `PlayNpcLayer`'s gate, and it fails the same way from the other side. A grid whose
	// cells are partial-body `*_layer` overlays owns only the bones its mask names; stood as a BASE
	// pose there is no mask in the path at all, so every bone it does not own arrives at the shared
	// skeleton's reference pose and the body loses its stance from the waist down. Retail composes
	// those as layers and never as a base — a masked sequence reaching the base path is a defect,
	// not a mode. Arming one as a layer is what `PlayNpcLayer` above is for.
	for (const FBlendSample& Sample : OutGrid.Space->GetBlendSamples())
	{
		if (Sample.Animation != nullptr
			&& Sample.Animation->FindMetaDataByClass<UElysiumAnimLayerMask>() != nullptr)
		{
			OutGrid = FElysiumResolvedGrid();
			return false;
		}
	}

	StandGridSelection(*Inst, OutGrid, /*Axis0=*/0.f, /*Axis1=*/0.f);
	return true;
}

void UElysiumEntityBodies::SetNpcGridPosition(USkeletalMeshComponent* Body, float Axis0, float Axis1)
{
	UElysiumBipedAnimInstance* Inst = Body
		? Cast<UElysiumBipedAnimInstance>(Body->GetAnimInstance()) : nullptr;
	if (Inst == nullptr || StandingGrid.Space == nullptr)
	{
		return;
	}
	// Re-published rather than written straight onto the instance: the graph reads its sample point
	// off the same record every other consumer reads, so a readout and a pose cannot disagree.
	// Moving the point does not restart the animations underneath it, because the generation is
	// unchanged and only a generation change asks the graph for a blend.
	StandGridSelection(*Inst, StandingGrid, Axis0, Axis1);
}

void UElysiumEntityBodies::StandGridSelection(UElysiumBipedAnimInstance& Inst,
	const FElysiumResolvedGrid& Grid, float Axis0, float Axis1)
{
	const bool bNewGrid = StandingGrid.Space != Grid.Space;
	StandingGrid = Grid;

	FElysiumAnimationSelection Selection;
	Selection.Source = EElysiumAnimSource::Debug;
	Selection.Route = EElysiumAnimRoute::ExactLabel;
	// The gait the fan belongs to is not knowable from the label alone, and it does not need to be:
	// every movement state in the graph plays whatever blend space it is handed. `ACT_WALK` is the
	// one that does so without a one-shot's completion contract attached.
	Selection.ResolvedActivity = TEXT("ACT_WALK");
	Selection.RequestedActivity = Selection.ResolvedActivity;
	Selection.SequenceLabel = Grid.Label;
	Selection.AssetKind = EElysiumAnimAssetKind::BlendSpace;
	Selection.Outcome = EElysiumAnimOutcome::Resolved;
	Selection.Axes = Grid.Axes;
	Selection.AxisValue[0] = Axis0;
	Selection.AxisValue[1] = Axis1;
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		Selection.AxisName[Axis] = Grid.AxisName[Axis];
	}
	// Advanced only when the grid itself changes. A slider drag re-publishes the same generation, so
	// the graph moves the sample point instead of asking for a transition on every frame of the drag.
	StandingGridGeneration += bNewGrid ? 1 : 0;
	Selection.Generation = StandingGridGeneration;

	FElysiumResolvedAnimation Assets;
	Assets.Space = Grid.Space;
	Inst.PublishSelection(Selection, Assets);
}

void UElysiumEntityBodies::StopNpcGrid(USkeletalMeshComponent* Body)
{
	StandingGrid = FElysiumResolvedGrid();
	UElysiumBipedAnimInstance* Inst = Body
		? Cast<UElysiumBipedAnimInstance>(Body->GetAnimInstance()) : nullptr;
	if (Inst == nullptr)
	{
		return;
	}
	// A selection naming no asset, which the graph answers by HOLDING the pose it has rather than by
	// entering a state with an empty pin. Leaving the grid's selection published instead would keep
	// the fan playing underneath whatever clip is started next, and it would reappear the moment that
	// clip ended.
	FElysiumAnimationSelection Cleared;
	Cleared.Source = EElysiumAnimSource::Debug;
	Cleared.Outcome = EElysiumAnimOutcome::NoAsset;
	Cleared.Generation = ++StandingGridGeneration;
	Inst->PublishSelection(Cleared, FElysiumResolvedAnimation());
}

void UElysiumEntityBodies::StopNpcLayers(USkeletalMeshComponent* Body)
{
	if (UElysiumBipedAnimInstance* Inst = Body
		? Cast<UElysiumBipedAnimInstance>(Body->GetAnimInstance()) : nullptr)
	{
		Inst->ClearDebugUpperBodyLayer();
	}
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
	const AActor* Owner = GetOwner();
	UGameInstance* GI = Owner ? Owner->GetGameInstance() : nullptr;
	UElysiumAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
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
	UElysiumAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
	if (Body == nullptr || Anims == nullptr)
	{
		return false;
	}
	EElysiumIdleTier Tier = EElysiumIdleTier::None;
	const FString Clip = Anims->PickIdleClip(Stem, Disposition, Tier, IdleVariant);
	return !Clip.IsEmpty() && PlayNpcClip(Body, Stem, Clip, /*bLoop=*/true, /*OutSeconds=*/nullptr);
}

bool UElysiumEntityBodies::ResolveStanceClips(const FString& Stem, const FString& AnimName,
	FElysiumStanceClips& OutClips)
{
	OutClips = FElysiumStanceClips();
	const AActor* Owner = GetOwner();
	UGameInstance* GI = Owner ? Owner->GetGameInstance() : nullptr;
	UElysiumAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
	return Anims != nullptr && Anims->ResolveStanceClips(Stem, AnimName, OutClips);
}

bool UElysiumEntityBodies::ResolveDisposition(const FString& Disposition,
	FElysiumDisposition& OutRow)
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
	const FElysiumDisposition* Row = Rules->Dispositions().Resolve(Disposition);
	if (Row == nullptr)
	{
		return false;
	}
	OutRow = *Row;
	return true;
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
		// Both slot spellings reduce to the material name the sidecar keys on, and the join is exact:
		// the baked slot is the container's own `Eyeball_r` against a sidecar that lowercases it, so a
		// suffix test misses by the separator it has no room for.
		const FString SlotName = Slots[Slot].MaterialSlotName.ToString();
		const FElysiumEyeball* Eye = Set->FindByMaterial(
			ElysiumEyes::MaterialNameFromSlot(Slots[Slot].MaterialSlotName));
		Binding.SlotJoins.Emplace(SlotName, Eye != nullptr ? Eye->Index : INDEX_NONE);
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

	// Registered on the presence of eye SECTIONS, not of bound slots: a body whose sections joined no
	// record still has to be findable, because it is drawing the eye master's default iris and nothing
	// else about it says so. Such a binding is skipped by the pass below.
	if (!Binding.SlotJoins.IsEmpty())
	{
		EyeBindings.Add(MoveTemp(Binding));
	}
}

bool UElysiumEntityBodies::DescribeEyes(const USkeletalMeshComponent* Comp,
	FElysiumEyeReadout& Out) const
{
	Out = FElysiumEyeReadout();
	if (Comp == nullptr)
	{
		return false;
	}
	const FElysiumEyeBinding* Binding = EyeBindings.FindByPredicate(
		[Comp](const FElysiumEyeBinding& B) { return B.Comp.Get() == Comp; });
	if (Binding == nullptr)
	{
		return false;
	}
	Out.bHasSet = Binding->Set.IsValid();
	Out.RecordCount = Out.bHasSet ? Binding->Set->Eyeballs.Num() : 0;
	Out.EyeSlotCount = Binding->SlotJoins.Num();
	Out.BoundCount = Binding->Slots.Num();
	Out.Slots = Binding->SlotJoins;
	Out.Blink = Binding->LastBlink;
	Out.bAiming = Binding->bLastAiming;
	return true;
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
	if (EyeBindings.IsEmpty())
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
	UElysiumRulebookSubsystem* Rules = GI
		? const_cast<UGameInstance*>(GI)->GetSubsystem<UElysiumRulebookSubsystem>() : nullptr;

	// The green room's override, consumed once for the whole pass. `bBlinkNow` is an edge, so it is
	// cleared here rather than per body: one press is one blink on everything bound, not one per body.
	FElysiumEyeDebug& Debug = EyeDebugState;
	const bool bBlinkNow = Debug.bBlinkNow;
	Debug.bBlinkNow = false;

	// `elysium.EyeTrackPlayer` is the debug override, and it outranks the gaze cascade on purpose:
	// it is the cheapest unambiguous check that the basis math is right, and it has to keep working
	// when the cascade is the thing under suspicion. The green room's Camera mode aims at the same
	// place, so the two resolve one camera between them.
	const bool bTrackPlayer = CVarEyeTrackPlayer.GetValueOnGameThread() != 0;
	const bool bWantCamera = bTrackPlayer || Debug.Gaze == FElysiumEyeDebug::EGaze::Camera;
	FVector TrackWorld = FVector::ZeroVector;
	bool bHaveCamera = false;
	if (bWantCamera)
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
		// A binding whose sections joined no record is a diagnostic entry: there is no MID to write
		// and no aim to solve, and writing its blink would move lids the eye pass does not own.
		if (Binding.Slots.IsEmpty())
		{
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
		if (Rules != nullptr)
		{
			if (const FElysiumDisposition* Row = Rules->Dispositions().Resolve(Binding.Disposition))
			{
				BlinkMin = Row->MinBlinkInterval;
				BlinkMax = Row->MaxBlinkInterval;
			}
		}
		FElysiumEyeInput EyeInput;
		if (bBlinkNow)
		{
			Binding.BlinkEndsAt = Now + ElysiumEyes::BlinkSeconds;
		}
		else if (Debug.bHoldBlink)
		{
			// Held open, and the schedule is held with it: releasing the hold should not fire every
			// blink the window was open for.
			Binding.BlinkEndsAt = 0.f;
			Binding.NextBlinkTime = Now + FMath::FRandRange(BlinkMin, BlinkMax);
		}
		else if (Binding.NextBlinkTime <= 0.f)
		{
			Binding.NextBlinkTime = Now + FMath::FRandRange(BlinkMin, BlinkMax);
		}
		else if (Now >= Binding.NextBlinkTime)
		{
			Binding.BlinkEndsAt = Now + ElysiumEyes::BlinkSeconds;
			Binding.NextBlinkTime = Now + FMath::FRandRange(BlinkMin, BlinkMax);
		}
		EyeInput.Blink = ElysiumEyes::BlinkWeight(Binding.BlinkEndsAt - Now);

		// Where this body is looking, in priority order: the green room's override, then the cvar,
		// then the gaze the substrate pushed for this character, then nothing. The override outranks
		// the cvar for the same reason the cvar outranks the cascade: it is the hand on the control,
		// and it has to win over whatever a session was left set to.
		//
		// `bEyeMove` is assigned on every branch INCLUDING the last, and the last is what makes the
		// fallback true. It defaults on, so leaving it alone with no gaze point does not rest the eye —
		// it aims at the target a zero vector names, which is the world origin. That reads as a whole
		// cast staring at one arbitrary point in the map and at nothing on a stage built far from it.
		FElysiumEyeTuning Tuning = Debug.Tuning;
		FVector GazeWorld = FVector::ZeroVector;
		bool bHaveGaze = false;
		if (Debug.Gaze == FElysiumEyeDebug::EGaze::Rest)
		{
			bHaveGaze = false;
		}
		else if (Debug.Gaze == FElysiumEyeDebug::EGaze::Point)
		{
			GazeWorld = Debug.Target;
			bHaveGaze = true;
		}
		else if (Debug.Gaze == FElysiumEyeDebug::EGaze::Camera && bHaveCamera)
		{
			GazeWorld = TrackWorld;
			bHaveGaze = true;
		}
		else if (Debug.Gaze == FElysiumEyeDebug::EGaze::Off && bTrackPlayer && bHaveCamera)
		{
			GazeWorld = TrackWorld;
			bHaveGaze = true;
		}
		else if (Debug.Gaze == FElysiumEyeDebug::EGaze::Off && Binding.bHasViewTarget)
		{
			GazeWorld = Binding.ViewTarget;
			bHaveGaze = true;
		}
		// The authored resting aim is what `bEyeMove` off selects — a real retail configuration, and
		// the one a body with no gaze source sits in.
		Tuning.bEyeMove = bHaveGaze;
		Binding.LastBlink = EyeInput.Blink;
		Binding.bLastAiming = bHaveGaze;

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
		if (UElysiumBodyAnimInstance* Inst = Cast<UElysiumBodyAnimInstance>(Comp->GetAnimInstance()))
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
	UElysiumAnimSubsystem* Anims = nullptr;
	if (UGameInstance* GI = Owner->GetGameInstance())
	{
		Anims = GI->GetSubsystem<UElysiumAnimSubsystem>();
	}
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
	// The authored garment, if this model has one. After the anim instance is installed, because
	// the cloth component follows this body as its leader pose and needs it already posed.
	ElysiumNpcVisual::InstallGarment(Comp, Stem);
	// The eyes (12.4). Independent of the facial rig above: a player body binds eyes here and no
	// flex rig at all, which is the shipped state for 57 of the 59 of them.
	InstallEyes(Comp, EyeSet, Disposition);
	// The body leaves the factory with no pose producer of its own. A direct clip REPLACES the
	// compiled graph for as long as it is set (`FElysiumBipedAnimProxy::Evaluate`), so a clip
	// installed here would own the body for its whole life and discard everything the animation
	// driver publishes. Whoever owns this body's pose states it: `RefreshNpcIdle` for a standing
	// cast member, a scene through `PlayCinematicClip`, the graph for a body whose driver
	// publishes a selection.
	return Comp;
}

bool UElysiumEntityBodies::PlayNpcActivity(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& Activity, int32 Variant, bool bLoop, float* OutSeconds)
{
	AActor* Owner = GetOwner();
	UGameInstance* GI = Owner ? Owner->GetGameInstance() : nullptr;
	UElysiumAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
	const FString Clip = Anims ? Anims->PickActivityClip(Stem, Activity, Variant) : FString();
	return !Clip.IsEmpty() && PlayNpcClip(Body, Stem, Clip, bLoop, OutSeconds);
}

bool UElysiumEntityBodies::ResolveNpcActivityClip(const FString& Stem, const FString& Activity,
	int32 Variant, FString& OutLabel, FString& OutAnimName, float& OutGroundSpeedCmPerSecond)
{
	AActor* Owner = GetOwner();
	UGameInstance* GI = Owner ? Owner->GetGameInstance() : nullptr;
	UElysiumAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
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
	UElysiumAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
	const FElysiumAnimatedPropEntry* Entry = Anims
		? Anims->GetIndex().FindAnimatedProp(ModelPath) : nullptr;
	return Entry ? Entry->Stem : FString();
}

const FElysiumAnimatedPropEntry* UElysiumEntityBodies::FindAnimatedPropEntry(const FString& Stem) const
{
	const AActor* Owner = GetOwner();
	UGameInstance* GI = Owner ? Owner->GetGameInstance() : nullptr;
	UElysiumAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
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
	UElysiumAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
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
		Mesh = LoadObject<USkeletalMesh>(nullptr,
			*FElysiumContentPaths::BakedPropSkeletalMesh(Stem));
		if (!Mesh)
		{
			UE_LOG(LogElysiumBodies, Warning,
				TEXT("animated prop '%s' is not on the baked mount -- run: uv run elysium export characters"),
				*Stem);
			return nullptr;
		}
		AnimatedPropMeshCache.Add(Stem, Mesh);

		// Widen the bind-pose bounds to the furthest reach of any clip this model owns, once, here —
		// see ElysiumPropBounds. The union rather than the playing clip's own radius: one mesh is
		// cached per stem and serves every prop standing that model, and the haven stake's five
		// entities play five different clips off this one asset. `GetImportedBounds` stays the true
		// bind pose; only the extended bounds move.
		double ReachCm = 0.0;
		for (const FElysiumPropClip& Clip : Entry->Clips)
		{
			// The container is centimetres by the `UE_` convention; the index states the reach in
			// metres, which is a magnitude and survives the change of basis unchanged.
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
	// The NATIVE host, not the graph: a skeletal prop is a named clip and nothing else — no gait, no
	// activity, no selection is ever published for one — so it poses off the clip player alone and a
	// compiled locomotion machine would sit inert behind it.
	Comp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Comp->SetAnimInstanceClass(UElysiumBipedAnimInstance::StaticClass());
	Comp->RegisterComponent();
	Owner->AddInstanceComponent(Comp);
	// A skeletal prop declares the same two composition stages a character does — 19 of them carry
	// a rule table — so it takes the same install (CAP7.2).
	if (UElysiumBodyAnimInstance* Inst = Cast<UElysiumBodyAnimInstance>(Comp->GetAnimInstance()))
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
	UElysiumAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
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
		Anim = LoadObject<UAnimSequence>(nullptr,
			*FElysiumContentPaths::BakedPropAnim(Stem, AnimName));
		if (!Anim)
		{
			UE_LOG(LogElysiumBodies, Warning,
				TEXT("animated prop '%s' clip '%s' (%s) is not on the baked mount"),
				*Stem, *ClipName, *AnimName);
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
	if (UElysiumBipedAnimInstance* Inst = Cast<UElysiumBipedAnimInstance>(Body->GetAnimInstance()))
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
		// An item's ground model is spawnable in any map, so it bakes onto the shared item scope
		// rather than into this map's Props. Same asset shape, same stem — only the package
		// differs, which makes this a lookup fallback and not a second build path. The map's own
		// package wins where both carry the stem: they are the same model either way.
		Mesh = LoadObject<UStaticMesh>(nullptr, *FElysiumContentPaths::BakedItemMesh(Stem));
	}
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
