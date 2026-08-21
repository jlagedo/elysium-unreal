#include "Visual/ElysiumBodyAnimInstance.h"

#include "Visual/ElysiumCompositionRig.h"
#include "Visual/ElysiumFacialRig.h"

#include "Animation/AnimCurveElementFlags.h"
#include "BonePose.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumComposition, Log, All);

namespace
{
	// **The composition stage's only observable.** A body whose procedural bones are not driven
	// holds them at their BIND, and the bind is the T-pose — so the helper bones skinned into each
	// arm keep pointing sideways while the arm swings down, and the geometry tears into pieces.
	// Nothing reports it: the rig is optional by design, an unresolved rule is silently skipped, and
	// a model carrying no rules at all is the normal case for 36 of the 166 bodies.
	//
	// Three independent causes produce that one symptom, so all three are printed together —
	// whether a rig was installed at all, how many of its rules resolved BOTH bones against the
	// live bone container, and whether the A/B cvar is on. Rules declared but none resolved means
	// the posed mesh does not carry the bones the sidecar names, which is a bake question rather
	// than a frame one.
	FAutoConsoleCommand GCompositionReport(
		TEXT("elysium.CompositionReport"),
		TEXT("Per posed body: whether a composition rig is installed, how many axis-interpolation "
		     "rules it declares, and how many resolved against the mesh."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			int32 Bodies = 0;
			for (TObjectIterator<UElysiumBodyAnimInstance> It; It; ++It)
			{
				UElysiumBodyAnimInstance* Instance = *It;
				if (Instance == nullptr || Instance->HasAnyFlags(RF_ClassDefaultObject)
					|| Instance->GetWorld() == nullptr)
				{
					continue;
				}
				const USkeletalMeshComponent* Owner = Instance->GetSkelMeshComponent();
				const USkeletalMesh* Mesh = Owner != nullptr ? Owner->GetSkeletalMeshAsset() : nullptr;
				const FElysiumCompositionRig* Rig = Instance->GetCompositionRig();
				++Bodies;
				UE_LOG(LogElysiumComposition, Display,
					TEXT("[composition] %s mesh=%s bones=%d rig=%s declared=%d resolved=%d"),
					*Instance->GetClass()->GetName(),
					Mesh != nullptr ? *Mesh->GetName() : TEXT("<none>"),
					Mesh != nullptr ? Mesh->GetRefSkeleton().GetRawBoneNum() : 0,
					Rig != nullptr ? *Rig->Stem : TEXT("NONE"),
					Rig != nullptr ? Rig->AxisRules.Num() : 0,
					Instance->GetResolvedAxisInterpRules());
			}
			UE_LOG(LogElysiumComposition, Display,
				TEXT("[composition] %d posed body(ies)"), Bodies);
		}));

	FAutoConsoleCommand GHairDynamicsReport(
		TEXT("elysium.HairDynamicsReport"),
		TEXT("Report installed stock AnimDynamics hair chains and breast bodies per body."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			int32 Bodies = 0;
			for (TObjectIterator<UElysiumBodyAnimInstance> It; It; ++It)
			{
				UElysiumBodyAnimInstance* Instance = *It;
				if (Instance == nullptr || Instance->HasAnyFlags(RF_ClassDefaultObject)
					|| Instance->GetWorld() == nullptr)
				{
					continue;
				}
				const USkeletalMeshComponent* Owner = Instance->GetSkelMeshComponent();
				const USkeletalMesh* Mesh = Owner != nullptr ? Owner->GetSkeletalMeshAsset() : nullptr;
				++Bodies;
				UE_LOG(LogElysiumComposition, Display,
					TEXT("[hair-dynamics] instance=%s mesh=%s chains=%d bodies=%d"),
					*Instance->GetClass()->GetName(),
					Mesh != nullptr ? *Mesh->GetName() : TEXT("<none>"),
					Instance->GetHairDynamicsChainCount(),
					Instance->GetHairDynamicsBodyCount());
			}
			UE_LOG(LogElysiumComposition, Display,
				TEXT("[hair-dynamics] %d posed body(ies)"), Bodies);
		}));

	// Where the bones actually ARE, in centimetres relative to `Bip01 Pelvis`, for every bone whose
	// name contains the argument. A screenshot cannot separate "the arm is posed wrongly" from "the
	// arm is posed correctly and the skinning is torn", and both look like the same broken picture.
	//
	// The reference to compare against is the container's own FK: composing `walk_0` out of
	// `character_shared_female_move_and_ranged.eskm` by ordinary parent-relative hierarchy puts the
	// hands 50.07 cm apart and just below the pelvis, against 108.58 cm apart and level with the
	// chest in the bind T-pose. So a live reading near 108 means the arms are not being posed at
	// all, one near 50 means they are and the fault is downstream of the pose.
	FAutoConsoleCommand GBoneReport(
		TEXT("elysium.BoneReport"),
		TEXT("elysium.BoneReport <substring> - each matching bone's live position relative to "
		     "Bip01 Pelvis, in centimetres."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const FString Filter = Args.Num() > 0 ? Args[0] : TEXT("Bip01");
			for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
			{
				USkeletalMeshComponent* Comp = *It;
				if (Comp == nullptr || Comp->GetWorld() == nullptr
					|| Comp->GetSkinnedAsset() == nullptr
					|| Cast<UElysiumBodyAnimInstance>(Comp->GetAnimInstance()) == nullptr)
				{
					continue;
				}
				const FReferenceSkeleton& Ref = Comp->GetSkinnedAsset()->GetRefSkeleton();
				const int32 Pelvis = Ref.FindBoneIndex(TEXT("Bip01 Pelvis"));
				if (Pelvis == INDEX_NONE)
				{
					continue;
				}
				const FVector Origin = Comp->GetBoneTransform(Pelvis).GetLocation();
				UE_LOG(LogElysiumComposition, Display, TEXT("[bones] %s (%d bones)"),
					*Comp->GetSkinnedAsset()->GetName(), Ref.GetNum());
				for (int32 Bone = 0; Bone < Ref.GetNum(); ++Bone)
				{
					const FName Name = Ref.GetBoneName(Bone);
					if (!Name.ToString().Contains(Filter))
					{
						continue;
					}
					const FTransform Live = Comp->GetBoneTransform(Bone);
					const FVector Where = Live.GetLocation() - Origin;
					// Position alone cannot tell a bone that is misplaced from one that is in the
					// right place and twisted — and a twisted bone is what drags skinned geometry
					// off a limb while leaving the limb itself looking correct. So the bone's own
					// axes are reported beside it: `fwd` is its local X in world space, which for a
					// Bip01 bone points down the limb at its child.
					const int32 ParentIndex = Ref.GetParentIndex(Bone);
					const double FromParentDeg = ParentIndex == INDEX_NONE ? 0.0
						: FMath::RadiansToDegrees(Live.GetRotation().AngularDistance(
							Comp->GetBoneTransform(ParentIndex).GetRotation()));
					const FVector Forward = Live.GetRotation().GetForwardVector();
					UE_LOG(LogElysiumComposition, Display,
						TEXT("[bones]   %-24s parent=%-20s x %7.2f y %7.2f z %7.2f  ")
						TEXT("fwd %5.2f %5.2f %5.2f  %6.1f deg from parent"),
						*Name.ToString(),
						ParentIndex == INDEX_NONE
							? TEXT("-") : *Ref.GetBoneName(ParentIndex).ToString(),
						Where.X, Where.Y, Where.Z,
						Forward.X, Forward.Y, Forward.Z, FromParentDeg);
				}
				const int32 LeftHand = Ref.FindBoneIndex(TEXT("Bip01 L Hand"));
				const int32 RightHand = Ref.FindBoneIndex(TEXT("Bip01 R Hand"));
				if (LeftHand != INDEX_NONE && RightHand != INDEX_NONE)
				{
					UE_LOG(LogElysiumComposition, Display,
						TEXT("[bones]   hand separation %.2f cm (bind T-pose is 108.58, walk is 50.07)"),
						FVector::Distance(Comp->GetBoneTransform(LeftHand).GetLocation(),
							Comp->GetBoneTransform(RightHand).GetLocation()));
				}
			}
		}));
}

// ================================================================================================
// FElysiumBodyAnimProxy
// ================================================================================================

void FElysiumBodyAnimProxy::Initialize(UAnimInstance* InAnimInstance)
{
	FAnimInstanceProxy::Initialize(InAnimInstance);
	bHairNeedsInitialize = !HairDynamics.IsEmpty();
}

void FElysiumBodyAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	// InitializeCachedClassData rebuilds the engine-owned list during an AnimInstance reinitialize.
	// These nodes live on the native proxy rather than in the compiled graph, so restore their reset
	// registrations before continuing with the manually hosted pre-update path.
	RegisterHairDynamicResetNodes();
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	for (FAnimNode_ElysiumHairDynamics& Node : HairDynamics)
	{
		Node.PreUpdate(InAnimInstance);
	}
}

void FElysiumBodyAnimProxy::UpdateAnimationNode(const FAnimationUpdateContext& InContext)
{
	if (bHairNeedsInitialize)
	{
		bHairNeedsInitialize = false;
		FAnimationInitializeContext InitContext(this);
		FAnimationCacheBonesContext BoneContext(this);
		for (FAnimNode_ElysiumHairDynamics& Node : HairDynamics)
		{
			Node.Initialize_AnyThread(InitContext);
			Node.CacheBones_AnyThread(BoneContext);
		}
	}
	for (FAnimNode_ElysiumHairDynamics& Node : HairDynamics)
	{
		Node.Update_AnyThread(InContext);
	}
	FAnimInstanceProxy::UpdateAnimationNode(InContext);
}

void FElysiumBodyAnimProxy::CacheBones()
{
	// A compiled graph's own nodes first. Guarded on `RootNode` inside, so this is a no-op on the
	// native path where there is no graph at all.
	FAnimInstanceProxy::CacheBones();

	// The bone container is what a bone reference resolves against, and this is the callback its
	// change arrives on — so both composition stages resolve their indices here, once, and never
	// by name per evaluation.
	AxisInterp.ResolveBones(GetRequiredBones());
	FAnimationCacheBonesContext Context(this);
	for (FAnimNode_ElysiumHairDynamics& Node : HairDynamics)
	{
		Node.CacheBones_AnyThread(Context);
	}
}

void FElysiumBodyAnimProxy::EvaluateComposition(FPoseContext& Output)
{
	if (!AxisInterp.HasWork() && HairDynamics.IsEmpty())
	{
		return;
	}

	// Retail's slot: the locals are decoded and blended, the hierarchy composes, then these run,
	// then skinning.
	//
	// Axis interpolation is the ONLY rule that belongs here, and the reason is a property rather
	// than a convention: a driven bone reads its control bone's LIVE orientation, so it has no
	// value at all until there is a finished pose to read one from and no offline pass can produce
	// one. Every other VtMB rule names a value the file carries somewhere and is resolved by the
	// bake instead (repo-root `CLAUDE.md`, "Poses are baked native").
	// Copied in, not moved: the conversion back writes *into* Output.Pose and addresses it by bone
	// index, so it has to still be a sized pose when we get there. Moving it out leaves it empty and
	// the first write indexes an array of size zero.
	FComponentSpacePoseContext Composed(this);
	Composed.Pose.InitPose(Output.Pose);

	AxisInterp.Apply(Composed);
	for (FAnimNode_ElysiumHairDynamics& Node : HairDynamics)
	{
		Node.Apply(Composed);
	}
	// Safe, not the plain form: both stages leave a bone in local space whose parent may never have
	// been asked for in component space, and the plain conversion ensures against exactly that.
	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPosesSafe(Composed.Pose, Output.Pose);
}

void FElysiumBodyAnimProxy::EvaluateTail(FPoseContext& Output)
{
	EvaluateComposition(Output);

	// The face is written last, over whatever the body produced — including the ref pose a body
	// with no clip falls back to, so a facial-only preview still moves. VtMB's clips carry no curves
	// at all, so nothing is being overwritten here; these names exist only because 12.3 puts them
	// there. The curves reach the component's morph weights through the skeleton's morph-target
	// curve metadata (`ElysiumNpcVisual::RegisterMorphTargetCurves`), and every morph is written
	// every frame — including the zeros, which is what releases a controller that went back to rest.
	const int32 Num = FMath::Min(FacialCurves.Num(), FacialWeights.Num());
	for (int32 i = 0; i < Num; ++i)
	{
		Output.Curve.Set(FacialCurves[i], FacialWeights[i]);
		Output.Curve.SetFlags(FacialCurves[i], UE::Anim::ECurveElementFlags::MorphTarget);
	}
}

void FElysiumBodyAnimProxy::SetFacialTrack(TArray<FName>&& InCurves)
{
	FacialCurves = MoveTemp(InCurves);
	FacialWeights.Reset(FacialCurves.Num());
	FacialWeights.AddZeroed(FacialCurves.Num());
}

void FElysiumBodyAnimProxy::SetFacialWeights(TArrayView<const float> InWeights)
{
	const int32 Num = FMath::Min(FacialWeights.Num(), InWeights.Num());
	for (int32 i = 0; i < Num; ++i)
	{
		FacialWeights[i] = InWeights[i];
	}
}

void FElysiumBodyAnimProxy::SetCompositionRig(TSharedPtr<const FElysiumCompositionRig> InRig)
{
	AxisInterp.SetRig(MoveTemp(InRig));
	// A rig installed before the component has ever cached bones resolves on the first evaluate;
	// one installed after re-resolves here, because the bone container is already valid.
	if (const FBoneContainer& Container = GetRequiredBones(); Container.IsValid())
	{
		AxisInterp.ResolveBones(Container);
	}
}

void FElysiumBodyAnimProxy::SetHairDynamics(
	const TArray<FElysiumHairDynamicsChainConfig>& InChains,
	const TArray<FElysiumHairDynamicsBodyConfig>& InBodies,
	const FReferenceSkeleton& ReferenceSkeleton)
{
	UnregisterHairDynamicResetNodes();
	for (FAnimNode_ElysiumHairDynamics& Node : HairDynamics)
	{
		Node.TermPhysics();
	}
	HairDynamics.Reset(InChains.Num() + InBodies.Num());
	for (const FElysiumHairDynamicsChainConfig& Config : InChains)
	{
		FAnimNode_ElysiumHairDynamics& Node = HairDynamics.AddDefaulted_GetRef();
		Node.Configure(Config, ReferenceSkeleton);
	}
	for (const FElysiumHairDynamicsBodyConfig& Config : InBodies)
	{
		FAnimNode_ElysiumHairDynamics& Node = HairDynamics.AddDefaulted_GetRef();
		Node.ConfigureBody(Config, ReferenceSkeleton);
	}
	InstalledChainCount = InChains.Num();
	InstalledBodyCount = InBodies.Num();
	bHairNeedsInitialize = !HairDynamics.IsEmpty();
	RegisterHairDynamicResetNodes();
}

void FElysiumBodyAnimProxy::RegisterHairDynamicResetNodes()
{
	for (FAnimNode_ElysiumHairDynamics& Node : HairDynamics)
	{
		DynamicResetNodes.AddUnique(&Node);
	}
}

void FElysiumBodyAnimProxy::UnregisterHairDynamicResetNodes()
{
	for (FAnimNode_ElysiumHairDynamics& Node : HairDynamics)
	{
		DynamicResetNodes.Remove(&Node);
	}
}

// ================================================================================================
// UElysiumBodyAnimInstance
// ================================================================================================

void UElysiumBodyAnimInstance::SetFacialRig(TSharedPtr<const FElysiumFacialRig> InRig)
{
	FacialRig = MoveTemp(InRig);
	ControllerValues.Reset();
	FlexWeights.Reset();
	MorphWeights.Reset();
	MouthOpen = 0.f;

	TArray<FName> Curves;
	if (FacialRig.IsValid())
	{
		ControllerValues.AddZeroed(FacialRig->Controllers.Num());
		Curves.Reserve(FacialRig->Morphs.Num());
		for (const FElysiumFlexMorph& Morph : FacialRig->Morphs)
		{
			Curves.Add(Morph.Curve);
		}
	}
	GetProxyOnGameThread<FElysiumBodyAnimProxy>().SetFacialTrack(MoveTemp(Curves));
	// Publish the rest pose immediately: with every controller at zero the rules resolve each lid to
	// its own hinge, so every morph target lands at exactly zero and the face is the authored mesh.
	EvaluateFacial();
}

void UElysiumBodyAnimInstance::SetCompositionRig(TSharedPtr<const FElysiumCompositionRig> InRig)
{
	CompositionRig = MoveTemp(InRig);
	// The state a body starts in, before any pose has been produced — a body posing its ref pose
	// must not have the rule applied either.
	// GetProxyOnGameThread blocks on any in-flight parallel evaluation, so the worker cannot be
	// reading the rig this replaces.
	GetProxyOnGameThread<FElysiumBodyAnimProxy>().SetCompositionRig(CompositionRig);
}

int32 UElysiumBodyAnimInstance::GetResolvedAxisInterpRules() const
{
	return const_cast<UElysiumBodyAnimInstance*>(this)
		->GetProxyOnGameThread<FElysiumBodyAnimProxy>().NumAxisInterpRules();
}

void UElysiumBodyAnimInstance::SetHairDynamics(
	const TArray<FElysiumHairDynamicsChainConfig>& InChains,
	const TArray<FElysiumHairDynamicsBodyConfig>& InBodies,
	const FReferenceSkeleton& ReferenceSkeleton)
{
	GetProxyOnGameThread<FElysiumBodyAnimProxy>().SetHairDynamics(InChains, InBodies, ReferenceSkeleton);
}

int32 UElysiumBodyAnimInstance::GetHairDynamicsChainCount() const
{
	return const_cast<UElysiumBodyAnimInstance*>(this)
		->GetProxyOnGameThread<FElysiumBodyAnimProxy>().NumHairDynamicsChains();
}

int32 UElysiumBodyAnimInstance::GetHairDynamicsBodyCount() const
{
	return const_cast<UElysiumBodyAnimInstance*>(this)
		->GetProxyOnGameThread<FElysiumBodyAnimProxy>().NumHairDynamicsBodies();
}

bool UElysiumBodyAnimInstance::SetFlexController(const FString& Name, float Value)
{
	const int32 Index = FacialRig.IsValid() ? FacialRig->FindController(Name) : INDEX_NONE;
	return Index != INDEX_NONE && SetFlexControllerByIndex(Index, Value);
}

bool UElysiumBodyAnimInstance::SetFlexControllerByIndex(int32 Index, float Value)
{
	if (!FacialRig.IsValid() || !ControllerValues.IsValidIndex(Index))
	{
		return false;
	}
	const float Normalized = FacialRig->Controllers[Index].Normalize(Value);
	if (ControllerValues[Index] != Normalized)
	{
		ControllerValues[Index] = Normalized;
		EvaluateFacial();
	}
	return true;
}

int32 UElysiumBodyAnimInstance::SetFlexControllers(TArrayView<const FElysiumFlexWrite> Writes,
	TArray<FString>* OutMissing)
{
	if (!FacialRig.IsValid())
	{
		return INDEX_NONE;
	}
	int32 Applied = 0;
	bool bChanged = false;
	for (const FElysiumFlexWrite& Write : Writes)
	{
		const int32 Index = FacialRig->FindController(Write.Name);
		if (!ControllerValues.IsValidIndex(Index))
		{
			// A key this model does not carry. Reported, never guessed at: the 249 shipped tables draw
			// on 48 distinct key names and no single rig carries all of them.
			if (OutMissing != nullptr)
			{
				OutMissing->AddUnique(Write.Name);
			}
			continue;
		}
		++Applied;
		const float Normalized = FacialRig->Controllers[Index].Normalize(Write.Value);
		if (ControllerValues[Index] != Normalized)
		{
			ControllerValues[Index] = Normalized;
			bChanged = true;
		}
	}
	if (bChanged)
	{
		EvaluateFacial();
	}
	return Applied;
}

void UElysiumBodyAnimInstance::ResetFlexControllers()
{
	if (ControllerValues.IsEmpty() && MouthOpen == 0.f)
	{
		return;
	}
	FMemory::Memzero(ControllerValues.GetData(), ControllerValues.Num() * sizeof(float));
	MouthOpen = 0.f;
	EvaluateFacial();
}

bool UElysiumBodyAnimInstance::HasMouth() const
{
	return FacialRig.IsValid() && FacialRig->Mouth.IsValid();
}

bool UElysiumBodyAnimInstance::SetMouthOpen(float Open)
{
	if (!HasMouth())
	{
		return false;
	}
	const float Clamped = FMath::Clamp(Open, 0.f, 1.f);
	if (MouthOpen != Clamped)
	{
		MouthOpen = Clamped;
		EvaluateFacial();
	}
	return true;
}

bool UElysiumBodyAnimInstance::SetEyeInput(const FElysiumEyeInput& Eyes)
{
	if (!FacialRig.IsValid())
	{
		return false;
	}
	EyeInput = Eyes;
	EvaluateFacial();
	return true;
}

void UElysiumBodyAnimInstance::EvaluateFacial()
{
	if (!FacialRig.IsValid())
	{
		return;
	}
	FElysiumJawInput Jaw;
	Jaw.Open = MouthOpen;
	// The amplitude jaw's weight is also raised into the `jaw_drop` controller: the flexdesc
	// `mstudiomouth_t` names carries no flex record on any shipped model, so it moves nothing on its
	// own (`Visual/ElysiumFacialRig.h`). A stated divergence.
	Jaw.bBridge = true;
	FacialRig->Evaluate(ControllerValues, Jaw, EyeInput, FlexWeights, MorphWeights);
	// GetProxyOnGameThread blocks on any in-flight parallel evaluation, so the worker cannot be
	// reading the weight array this overwrites.
	GetProxyOnGameThread<FElysiumBodyAnimProxy>().SetFacialWeights(MorphWeights);
}
