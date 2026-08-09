#include "Visual/ElysiumBodyAnimInstance.h"

#include "Visual/ElysiumClothRig.h"
#include "Visual/ElysiumCompositionRig.h"
#include "Visual/ElysiumFacialRig.h"

#include "Animation/AnimCurveElementFlags.h"
#include "BonePose.h"
#include "HAL/IConsoleManager.h"

namespace
{
	// The reconstruction described in `Visual/ElysiumFacialRig.h`: the amplitude jaw's weight is also
	// raised into the `jaw_drop` controller, because the flexdesc `mstudiomouth_t` actually names
	// carries no flex record on any shipped model and so moves nothing on its own. 0 leaves only the
	// faithful write, which is the A/B baseline for the divergence.
	TAutoConsoleVariable<int32> CVarFacialJawBridge(
		TEXT("elysium.FacialJawBridge"),
		1,
		TEXT("Bridge the amplitude jaw into the jaw_drop flex controller (1, default) or write only "
		     "the mouth flexdesc, which no shipped model consumes (0)."),
		ECVF_Default);

	// The A/B for the two composition stages. 1 = VtMB's own composition, 0 = Unreal's ordinary
	// hierarchy alone, which is what every body posed under before CAP7.2. Dropping it is visible
	// exactly where the docs measure it: up to 44.9 degrees on a shoulder, 26.9 on a bicep, 6.4 on a
	// wrist, and a `Flags & 0x2` spine rooted in its parent rather than the component.
	TAutoConsoleVariable<int32> CVarCompositionStages(
		TEXT("elysium.CompositionStages"), 1,
		TEXT("1 = apply VtMB split inheritance + axis interpolation over the blended pose (CAP7.2), ")
		TEXT("0 = ordinary Unreal hierarchy composition only."),
		ECVF_Default);
}

// ================================================================================================
// FElysiumBodyAnimProxy
// ================================================================================================

void FElysiumBodyAnimProxy::CacheBones()
{
	// A compiled graph's own nodes first. Guarded on `RootNode` inside, so this is a no-op on the
	// native path where there is no graph at all.
	FAnimInstanceProxy::CacheBones();

	// The bone container is what a bone reference resolves against, and this is the callback its
	// change arrives on — so both composition stages resolve their indices here, once, and never
	// by name per evaluation.
	AxisInterp.ResolveBones(GetRequiredBones());
	// The cloth chains take the context rather than the container: they are constructed here too,
	// because the reference skeleton their body definitions need is only reachable from it.
	FAnimationCacheBonesContext Context(this);
	Cloth.CacheBones(Context);
}

void FElysiumBodyAnimProxy::PreUpdateCloth(const UAnimInstance* Instance)
{
	Cloth.PreUpdate(Instance);
}

void FElysiumBodyAnimProxy::EvaluateComposition(FPoseContext& Output)
{
	const bool bStages = AxisInterp.HasWork()
		&& CVarCompositionStages.GetValueOnAnyThread() != 0;
	if (!bStages && !Cloth.HasWork())
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

	if (bStages)
	{
		AxisInterp.Apply(Composed);
	}
	// Last, over the finished skeleton. The garment is synthesised geometry hanging off the pelvis
	// and shares no bone with either stage above, but it should still swing from the pose that will
	// actually be drawn rather than one still missing its corrections.
	Cloth.Apply(Composed);

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

void FElysiumBodyAnimProxy::SetClothRig(TSharedPtr<const FElysiumClothRig> InRig)
{
	Cloth.SetRig(MoveTemp(InRig));
	// Same rule as the composition rig, and the same reason: installed before the first CacheBones
	// this resolves there, installed after it has to rebuild against the container already in force.
	// The chains are torn down by SetRig, so this is a construction rather than a re-resolve.
	if (const FBoneContainer& Container = GetRequiredBones(); Container.IsValid())
	{
		FAnimationCacheBonesContext Context(this);
		Cloth.CacheBones(Context);
	}
}

// ================================================================================================
// UElysiumBodyAnimInstance
// ================================================================================================

void UElysiumBodyAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	// Skipped entirely for the overwhelming majority of bodies, which carry no garment rig at all.
	if (ClothRig.IsValid())
	{
		GetProxyOnGameThread<FElysiumBodyAnimProxy>().PreUpdateCloth(this);
	}
}

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

void UElysiumBodyAnimInstance::SetClothRig(TSharedPtr<const FElysiumClothRig> InRig)
{
	ClothRig = MoveTemp(InRig);
	// Same guarantee as the composition rig: GetProxyOnGameThread blocks on any in-flight parallel
	// evaluation, so no worker can be simulating against the chains this replaces.
	GetProxyOnGameThread<FElysiumBodyAnimProxy>().SetClothRig(ClothRig);
}

void UElysiumBodyAnimInstance::SetClothTuning(const FElysiumClothTuning& InTuning)
{
	GetProxyOnGameThread<FElysiumBodyAnimProxy>().SetClothTuning(InTuning);
}

FElysiumClothTuning UElysiumBodyAnimInstance::GetClothTuning() const
{
	return const_cast<UElysiumBodyAnimInstance*>(this)
		->GetProxyOnGameThread<FElysiumBodyAnimProxy>().GetClothTuning();
}

int32 UElysiumBodyAnimInstance::GetResolvedClothChains() const
{
	return const_cast<UElysiumBodyAnimInstance*>(this)
		->GetProxyOnGameThread<FElysiumBodyAnimProxy>().NumClothChains();
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
	// Read here rather than latched at the write, so toggling the cvar takes on the next evaluation
	// of any kind instead of waiting for the next jaw write.
	Jaw.bBridge = CVarFacialJawBridge.GetValueOnGameThread() != 0;
	FacialRig->Evaluate(ControllerValues, Jaw, EyeInput, FlexWeights, MorphWeights);
	// GetProxyOnGameThread blocks on any in-flight parallel evaluation, so the worker cannot be
	// reading the weight array this overwrites.
	GetProxyOnGameThread<FElysiumBodyAnimProxy>().SetFacialWeights(MorphWeights);
}
