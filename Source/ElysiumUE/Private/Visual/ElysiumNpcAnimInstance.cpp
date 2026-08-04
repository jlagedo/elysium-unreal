#include "Visual/ElysiumNpcAnimInstance.h"

#include "Visual/ElysiumClothRig.h"
#include "Visual/ElysiumCompositionRig.h"
#include "Visual/ElysiumFacialRig.h"

#include "Animation/AnimCurveElementFlags.h"
#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
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
}

void FElysiumNpcAnimProxy::Initialize(UAnimInstance* InAnimInstance)
{
	FAnimInstanceProxy::Initialize(InAnimInstance);

	// Both players loop by default and are driven explicitly; no sync group, because two idle
	// clips of different lengths must not be forced onto a shared normalized time.
	FAnimationInitializeContext Context(this);
	for (FAnimNode_SequencePlayer_Standalone& Player : Players)
	{
		Player.SetLoopAnimation(true);
		Player.SetPlayRate(1.f);
		Player.SetGroupName(NAME_None);
		Player.SetGroupMethod(EAnimSyncMethod::DoNotSync);
		Player.Initialize_AnyThread(Context);
	}
	Incoming = 0;
	BlendAlpha = 1.f;
	BlendRate = 0.f;
	Playing = nullptr;
	bPlayingLoop = true;
	bNeedsReinit[0] = false;
	bNeedsReinit[1] = false;
	bInitialized = false;
	// The facial track is not reset here: the rig is installed once per body, before or after this
	// runs depending on when the component registers, and re-initializing the pose graph does not
	// change which face the body wears.
}

void FElysiumNpcAnimProxy::CacheBones()
{
	FAnimationCacheBonesContext Context(this);
	for (FAnimNode_SequencePlayer_Standalone& Player : Players)
	{
		Player.CacheBones_AnyThread(Context);
	}
	// The bone container is what a bone reference resolves against, and this is the callback its
	// change arrives on — so both composition stages resolve their indices here, once, and never
	// by name per evaluation.
	Split.ResolveBones(GetRequiredBones());
	AxisInterp.ResolveBones(GetRequiredBones());
	// The cloth chains take the context rather than the container: they are constructed here too,
	// because the reference skeleton their body definitions need is only reachable from it.
	Cloth.CacheBones(Context);
}

void FElysiumNpcAnimProxy::PreUpdateCloth(const UAnimInstance* Instance)
{
	Cloth.PreUpdate(Instance);
}

void FElysiumNpcAnimProxy::Request(UAnimSequence* Sequence, bool bLoop, float BlendSeconds)
{
	if (Sequence == nullptr)
	{
		return;
	}
	if (Sequence == Playing)
	{
		// A repeated disposition/idle write must not visibly reset a looping stance. One-shots are
		// commands, though: repeating one means replay it, and changing loop policy must take effect.
		if (bLoop && bPlayingLoop)
		{
			// A body a prior Seek pinned at rate 0 must not stay frozen forever just because the
			// same looping clip was asked for again. SetPlayRate writes the node's own member, read
			// on every UpdateAssetPlayer, so this un-freezes without the visible restart the
			// early-out exists to prevent — no bNeedsReinit, and the play position is preserved.
			Players[Incoming].SetPlayRate(1.f);
			return;
		}
		Players[Incoming].SetLoopAnimation(bLoop);
		Players[Incoming].SetPlayRate(1.f);
		Players[Incoming].SetStartPosition(0.f);
		bNeedsReinit[Incoming] = true;
		bPlayingLoop = bLoop;
		BlendAlpha = 1.f;
		BlendRate = 0.f;
		return;
	}

	// The first clip has nothing to blend from, so it snaps in regardless of BlendSeconds —
	// otherwise every NPC would fade up out of the reference pose on map load.
	const bool bSnap = !bInitialized || BlendSeconds <= 0.f;
	const int32 Next = bInitialized ? (1 - Incoming) : Incoming;

	Players[Next].SetSequence(Sequence);
	Players[Next].SetLoopAnimation(bLoop);
	Players[Next].SetPlayRate(1.f);
	Players[Next].SetStartPosition(0.f);
	bNeedsReinit[Next] = true;   // reset that player's play time on the worker

	Incoming = Next;
	Playing = Sequence;
	bPlayingLoop = bLoop;
	BlendAlpha = bSnap ? 1.f : 0.f;
	BlendRate = bSnap ? 0.f : 1.f / BlendSeconds;
	bInitialized = true;
}

void FElysiumNpcAnimProxy::Seek(float PositionSeconds)
{
	if (!bInitialized || Playing == nullptr)
	{
		return;
	}
	const float Length = Playing->GetPlayLength();
	const float Position = bPlayingLoop && Length > SMALL_NUMBER
		? FMath::Fmod(FMath::Max(0.f, PositionSeconds), Length)
		: FMath::Clamp(PositionSeconds, 0.f, Length);
	Players[Incoming].SetStartPosition(Position);
	Players[Incoming].SetPlayRate(0.f);
	bNeedsReinit[Incoming] = true;
	BlendAlpha = 1.f;
	BlendRate = 0.f;
}

float FElysiumNpcAnimProxy::GetClipPosition() const
{
	if (!bInitialized || Playing == nullptr)
	{
		return -1.f;
	}
	return Players[Incoming].GetAccumulatedTime();
}

void FElysiumNpcAnimProxy::ResyncPosition(float PositionSeconds)
{
	if (!bInitialized || Playing == nullptr)
	{
		return;
	}
	// A pending restart wins. The worker consumes bNeedsReinit in UpdateAnimationNode and
	// Initialize_AnyThread then sets the accumulator to the node's start position, so a resync
	// written in the same frame as a Request would be silently discarded. Bailing makes that
	// deterministic instead of dependent on which ran first; the next resync corrects it.
	if (bNeedsReinit[Incoming])
	{
		return;
	}
	const float Length = Playing->GetPlayLength();
	const float Position = bPlayingLoop && Length > SMALL_NUMBER
		? FMath::Fmod(FMath::Max(0.f, PositionSeconds), Length)
		: FMath::Clamp(PositionSeconds, 0.f, Length);
	// SetAccumulatedTime writes the node's play time and nothing else — no play rate, no start
	// position, no reinit, no blend state. That is the whole reason this is not Seek: a resync
	// landing inside a crossfade leaves the crossfade running, and only the incoming player is
	// re-phased because the outgoing pose is fading out and its phase no longer matters.
	//
	// This is a game-thread write to a field the worker advances. It is safe only because every
	// caller arrives through UElysiumNpcAnimInstance's GetProxyOnGameThread, which blocks on any
	// in-flight parallel evaluation. A future path reaching the proxy without that accessor turns
	// this into a silent data race on a float.
	Players[Incoming].SetAccumulatedTime(Position);
}

void FElysiumNpcAnimProxy::Stop()
{
	Playing = nullptr;
	bInitialized = false;
	BlendAlpha = 1.f;
	BlendRate = 0.f;
}

void FElysiumNpcAnimProxy::UpdateAnimationNode(const FAnimationUpdateContext& InContext)
{
	// Ahead of the initialised gate: the simulation's only source of a timestep is this call, and a
	// body with no clip still evaluates — against the ref pose — so its garment must still hang and
	// settle rather than freeze mid-air until something plays.
	Cloth.Update(InContext);

	if (!bInitialized)
	{
		return;
	}

	// A player whose clip changed restarts from its start position. Done here rather than in
	// Request() because this is the thread and the context the node expects.
	for (int32 i = 0; i < 2; ++i)
	{
		if (bNeedsReinit[i])
		{
			bNeedsReinit[i] = false;
			FAnimationInitializeContext InitContext(this);
			Players[i].Initialize_AnyThread(InitContext);
			FAnimationCacheBonesContext BoneContext(this);
			Players[i].CacheBones_AnyThread(BoneContext);
		}
	}

	if (BlendAlpha < 1.f)
	{
		BlendAlpha = FMath::Min(1.f, BlendAlpha + BlendRate * InContext.GetDeltaTime());
	}

	// Advancing play time is what this call is for. Weighting each player by its blend share keeps
	// notifies and root motion proportional, even though neither is consumed yet.
	Players[Incoming].Update_AnyThread(InContext.FractionalWeight(BlendAlpha));
	if (BlendAlpha < 1.f && Players[1 - Incoming].GetSequence() != nullptr)
	{
		Players[1 - Incoming].Update_AnyThread(InContext.FractionalWeight(1.f - BlendAlpha));
	}
}

void FElysiumNpcAnimProxy::EvaluateBody(FPoseContext& Output)
{
	if (!bInitialized || Players[Incoming].GetSequence() == nullptr)
	{
		Output.ResetToRefPose();
		return;
	}

	// The incoming player always contributes; the outgoing one only while the crossfade runs,
	// so a settled NPC evaluates exactly one sequence.
	FPoseContext Incoming_(this);
	Players[Incoming].Evaluate_AnyThread(Incoming_);

	if (BlendAlpha >= 1.f || Players[1 - Incoming].GetSequence() == nullptr)
	{
		Output = Incoming_;
		return;
	}

	FPoseContext Outgoing(this);
	Players[1 - Incoming].Evaluate_AnyThread(Outgoing);

	FAnimationPoseData OutData(Output);
	const FAnimationPoseData OutgoingData(Outgoing);
	const FAnimationPoseData IncomingData(Incoming_);
	// WeightOfPoseOne is the *first* argument's share, so the outgoing pose leads and the
	// incoming one takes BlendAlpha.
	FAnimationRuntime::BlendTwoPosesTogether(OutgoingData, IncomingData, 1.f - BlendAlpha, OutData);
}

// The A/B for the two composition stages. 1 = VtMB's own composition, 0 = Unreal's ordinary
// hierarchy alone, which is what every body posed under before CAP7.2. Dropping it is visible
// exactly where the docs measure it: up to 44.9 degrees on a shoulder, 26.9 on a bicep, 6.4 on a
// wrist, and a `Flags & 0x2` spine rooted in its parent rather than the component.
static TAutoConsoleVariable<int32> CVarCompositionStages(
	TEXT("elysium.CompositionStages"), 1,
	TEXT("1 = apply VtMB split inheritance + axis interpolation over the blended pose (CAP7.2), ")
	TEXT("0 = ordinary Unreal hierarchy composition only."),
	ECVF_Default);

void FElysiumNpcAnimProxy::EvaluateComposition(FPoseContext& Output)
{
	const bool bStages = (Split.HasWork() || AxisInterp.HasWork())
		&& CVarCompositionStages.GetValueOnAnyThread() != 0;
	if (!bStages && !Cloth.HasWork())
	{
		return;
	}

	// Retail's slot: the locals are decoded and blended, the hierarchy composes, then these run,
	// then skinning. The rule is non-linear, so it has to see the blended pose rather than each
	// clip's — which is exactly why it cannot be baked into the clips instead.
	// Copied in, not moved: the conversion back writes *into* Output.Pose and addresses it by bone
	// index, so it has to still be a sized pose when we get there. Moving it out leaves it empty and
	// the first write indexes an array of size zero.
	FComponentSpacePoseContext Composed(this);
	Composed.Pose.InitPose(Output.Pose);

	// ORDER IS LOAD-BEARING. Split inheritance re-roots `Bip01 Spine1`'s orientation, and every
	// driven arm bone hangs off it, so running these the other way produces a different skeleton.
	if (bStages)
	{
		Split.Apply(Composed);
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

bool FElysiumNpcAnimProxy::Evaluate(FPoseContext& Output)
{
	EvaluateBody(Output);
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
	return true;
}

void FElysiumNpcAnimProxy::SetFacialTrack(TArray<FName>&& InCurves)
{
	FacialCurves = MoveTemp(InCurves);
	FacialWeights.Reset(FacialCurves.Num());
	FacialWeights.AddZeroed(FacialCurves.Num());
}

void FElysiumNpcAnimProxy::SetFacialWeights(TArrayView<const float> InWeights)
{
	const int32 Num = FMath::Min(FacialWeights.Num(), InWeights.Num());
	for (int32 i = 0; i < Num; ++i)
	{
		FacialWeights[i] = InWeights[i];
	}
}

void FElysiumNpcAnimProxy::SetCompositionRig(TSharedPtr<const FElysiumCompositionRig> InRig)
{
	Split.SetRig(InRig);
	AxisInterp.SetRig(MoveTemp(InRig));
	// A rig installed before the component has ever cached bones resolves on the first evaluate;
	// one installed after re-resolves here, because the bone container is already valid.
	if (const FBoneContainer& Container = GetRequiredBones(); Container.IsValid())
	{
		Split.ResolveBones(Container);
		AxisInterp.ResolveBones(Container);
	}
}

void FElysiumNpcAnimProxy::SetClothRig(TSharedPtr<const FElysiumClothRig> InRig)
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

void UElysiumNpcAnimInstance::PlayClip(UAnimSequence* Sequence, bool bLoop, float BlendSeconds)
{
	if (Sequence == nullptr)
	{
		return;
	}
	// GetProxyOnGameThread blocks on any in-flight parallel evaluation, so the write cannot race
	// the worker reading the same fields.
	GetProxyOnGameThread<FElysiumNpcAnimProxy>().Request(Sequence, bLoop, BlendSeconds);
}

void UElysiumNpcAnimInstance::SeekClip(float PositionSeconds)
{
	GetProxyOnGameThread<FElysiumNpcAnimProxy>().Seek(PositionSeconds);
}

void UElysiumNpcAnimInstance::StopClip()
{
	GetProxyOnGameThread<FElysiumNpcAnimProxy>().Stop();
}

float UElysiumNpcAnimInstance::GetClipPosition() const
{
	return const_cast<UElysiumNpcAnimInstance*>(this)
		->GetProxyOnGameThread<FElysiumNpcAnimProxy>().GetClipPosition();
}

void UElysiumNpcAnimInstance::ResyncClip(float PositionSeconds)
{
	GetProxyOnGameThread<FElysiumNpcAnimProxy>().ResyncPosition(PositionSeconds);
}

void UElysiumNpcAnimInstance::SetFacialRig(TSharedPtr<const FElysiumFacialRig> InRig)
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
	GetProxyOnGameThread<FElysiumNpcAnimProxy>().SetFacialTrack(MoveTemp(Curves));
	// Publish the rest pose immediately: with every controller at zero the rules resolve each lid to
	// its own hinge, so every morph target lands at exactly zero and the face is the authored mesh.
	EvaluateFacial();
}

void UElysiumNpcAnimInstance::SetCompositionRig(TSharedPtr<const FElysiumCompositionRig> InRig)
{
	CompositionRig = MoveTemp(InRig);
	// GetProxyOnGameThread blocks on any in-flight parallel evaluation, so the worker cannot be
	// reading the rig this replaces.
	GetProxyOnGameThread<FElysiumNpcAnimProxy>().SetCompositionRig(CompositionRig);
}

int32 UElysiumNpcAnimInstance::GetResolvedAxisInterpRules() const
{
	return const_cast<UElysiumNpcAnimInstance*>(this)
		->GetProxyOnGameThread<FElysiumNpcAnimProxy>().NumAxisInterpRules();
}

void UElysiumNpcAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	// Skipped entirely for the overwhelming majority of bodies, which carry no garment rig at all.
	if (ClothRig.IsValid())
	{
		GetProxyOnGameThread<FElysiumNpcAnimProxy>().PreUpdateCloth(this);
	}
}

void UElysiumNpcAnimInstance::SetClothRig(TSharedPtr<const FElysiumClothRig> InRig)
{
	ClothRig = MoveTemp(InRig);
	// Same guarantee as the composition rig: GetProxyOnGameThread blocks on any in-flight parallel
	// evaluation, so no worker can be simulating against the chains this replaces.
	GetProxyOnGameThread<FElysiumNpcAnimProxy>().SetClothRig(ClothRig);
}

void UElysiumNpcAnimInstance::SetClothTuning(const FElysiumClothTuning& InTuning)
{
	GetProxyOnGameThread<FElysiumNpcAnimProxy>().SetClothTuning(InTuning);
}

FElysiumClothTuning UElysiumNpcAnimInstance::GetClothTuning() const
{
	return const_cast<UElysiumNpcAnimInstance*>(this)
		->GetProxyOnGameThread<FElysiumNpcAnimProxy>().GetClothTuning();
}

int32 UElysiumNpcAnimInstance::GetResolvedClothChains() const
{
	return const_cast<UElysiumNpcAnimInstance*>(this)
		->GetProxyOnGameThread<FElysiumNpcAnimProxy>().NumClothChains();
}

bool UElysiumNpcAnimInstance::SetFlexController(const FString& Name, float Value)
{
	const int32 Index = FacialRig.IsValid() ? FacialRig->FindController(Name) : INDEX_NONE;
	return Index != INDEX_NONE && SetFlexControllerByIndex(Index, Value);
}

bool UElysiumNpcAnimInstance::SetFlexControllerByIndex(int32 Index, float Value)
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

int32 UElysiumNpcAnimInstance::SetFlexControllers(TArrayView<const FElysiumFlexWrite> Writes,
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

void UElysiumNpcAnimInstance::ResetFlexControllers()
{
	if (ControllerValues.IsEmpty() && MouthOpen == 0.f)
	{
		return;
	}
	FMemory::Memzero(ControllerValues.GetData(), ControllerValues.Num() * sizeof(float));
	MouthOpen = 0.f;
	EvaluateFacial();
}

bool UElysiumNpcAnimInstance::HasMouth() const
{
	return FacialRig.IsValid() && FacialRig->Mouth.IsValid();
}

bool UElysiumNpcAnimInstance::SetMouthOpen(float Open)
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

bool UElysiumNpcAnimInstance::SetEyeInput(const FElysiumEyeInput& Eyes)
{
	if (!FacialRig.IsValid())
	{
		return false;
	}
	EyeInput = Eyes;
	EvaluateFacial();
	return true;
}

void UElysiumNpcAnimInstance::EvaluateFacial()
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
	GetProxyOnGameThread<FElysiumNpcAnimProxy>().SetFacialWeights(MorphWeights);
}
