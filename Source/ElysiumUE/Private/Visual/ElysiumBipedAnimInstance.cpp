#include "Visual/ElysiumBipedAnimInstance.h"

#include "Visual/ElysiumAnimLayerMask.h"
#include "Visual/ElysiumAnimSubsystem.h"   // FElysiumResolvedAnimation

#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimStateMachineTypes.h"
#include "Animation/AnimSubsystem_Tag.h"
#include "Animation/BlendProfile.h"
#include "Animation/BlendSpace.h"
#include "Animation/Skeleton.h"
#include "AnimNodes/AnimNode_LayeredBoneBlend.h"
#include "AnimationRuntime.h"
#include "BonePose.h"
#include "HAL/IConsoleManager.h"

namespace
{
	// The one machine the authored graph carries. The asset test asserts this name, so a graph whose
	// machine was renamed fails in a tier rather than silently reporting no completion.
	const FName GLocomotionMachine(TEXT("Locomotion"));
}

// ================================================================================================
// FElysiumBipedAnimProxy
// ================================================================================================

void FElysiumBipedAnimProxy::Initialize(UAnimInstance* InAnimInstance)
{
	FAnimInstanceProxy::Initialize(InAnimInstance);

	// Every player this proxy owns itself loops by default and is driven explicitly; no sync group,
	// because two clips of different lengths must not be forced onto a shared normalized time — and a
	// layer must never be phase-matched to the pose it rides over, which is what an aim layer and a
	// walk cycle have nothing to say to each other about.
	FAnimationInitializeContext Context(this);
	ClipPlayer.SetLoopAnimation(true);
	ClipPlayer.SetPlayRate(1.f);
	ClipPlayer.SetGroupName(NAME_None);
	ClipPlayer.SetGroupMethod(EAnimSyncMethod::DoNotSync);
	ClipPlayer.Initialize_AnyThread(Context);
	Playing = nullptr;
	bPlayingLoop = true;
	bClipNeedsReinit = false;
	// The facial track is not reset here: the rig is installed once per body, before or after this
	// runs depending on when the component registers, and re-initializing the pose graph does not
	// change which face the body wears.
}

void FElysiumBipedAnimProxy::CacheBones()
{
	// The compiled graph, the composition stages and the garment first, through the shared base.
	FElysiumBodyAnimProxy::CacheBones();

	FAnimationCacheBonesContext Context(this);
	ClipPlayer.CacheBones_AnyThread(Context);
}

bool FElysiumBipedAnimProxy::Evaluate(FPoseContext& Output)
{
	// A standing cinematic clip IS the body pose, replacing the graph rather than blending with it —
	// a scene owns the body outright for its duration. The graph still advanced this frame, so
	// whatever it holds resumes the moment the clip is stopped.
	if (Playing != nullptr && ClipPlayer.GetSequence() != nullptr)
	{
		ClipPlayer.Evaluate_AnyThread(Output);
	}
	else
	{
		// The compiled graph. `FAnimInstanceProxy::Evaluate` returns false to mean "not handled, run
		// the graph", so calling it here would evaluate nothing and pose the reference pose.
		EvaluateAnimationNode(Output);
	}
	// VtMB's autolayers used to be accumulated here, between the body pose and the composition
	// stages. `CCC10` moved them into the compiled graph, where the mask is a property of the blend
	// node rather than of the pose feeding it — so they now arrive inside `EvaluateAnimationNode`
	// above, still under the composition tail, which is retail's own order either way.
	//
	// Then the composition stages and the face, over whatever produced the pose — the same tail every
	// body wears, which is what stops a body shipping frozen eyes and untwisted forearms.
	EvaluateTail(Output);
	return true;
}

void FElysiumBipedAnimProxy::UpdateAnimationNode(const FAnimationUpdateContext& InContext)
{
	if (bClipNeedsReinit)
	{
		bClipNeedsReinit = false;
		FAnimationInitializeContext InitContext(this);
		ClipPlayer.Initialize_AnyThread(InitContext);
		FAnimationCacheBonesContext BoneContext(this);
		ClipPlayer.CacheBones_AnyThread(BoneContext);
	}
	if (Playing != nullptr && ClipPlayer.GetSequence() != nullptr)
	{
		ClipPlayer.Update_AnyThread(InContext);
	}

	// The graph advances whether or not its pose is consumed. A scene that ends hands the body back
	// to a machine that has kept up with the world rather than to one frozen where the scene began.
	FAnimInstanceProxy::UpdateAnimationNode(InContext);
}

// ================================================================================================
// The cinematic clip path
// ================================================================================================

void FElysiumBipedAnimProxy::PlayDirect(UAnimSequence* Sequence, bool bLoop)
{
	if (Sequence == nullptr)
	{
		return;
	}
	if (Sequence == Playing && bLoop && bPlayingLoop)
	{
		// A repeated looping request must not visibly reset the clip. A body a prior Seek pinned at
		// rate 0 must not stay frozen forever because of it, though: SetPlayRate writes the node's
		// own member, read on every UpdateAssetPlayer, so this un-freezes without the restart the
		// early-out exists to prevent — no reinit, and the play position is preserved.
		ClipPlayer.SetPlayRate(1.f);
		return;
	}
	ClipPlayer.SetSequence(Sequence);
	ClipPlayer.SetLoopAnimation(bLoop);
	ClipPlayer.SetPlayRate(1.f);
	ClipPlayer.SetStartPosition(0.f);
	bClipNeedsReinit = true;   // reset the play time on the worker
	Playing = Sequence;
	bPlayingLoop = bLoop;
}

void FElysiumBipedAnimProxy::Seek(float PositionSeconds)
{
	if (Playing == nullptr)
	{
		return;
	}
	const float Length = Playing->GetPlayLength();
	const float Position = bPlayingLoop && Length > SMALL_NUMBER
		? FMath::Fmod(FMath::Max(0.f, PositionSeconds), Length)
		: FMath::Clamp(PositionSeconds, 0.f, Length);
	ClipPlayer.SetStartPosition(Position);
	ClipPlayer.SetPlayRate(0.f);
	bClipNeedsReinit = true;
}

float FElysiumBipedAnimProxy::GetClipPosition() const
{
	if (Playing == nullptr)
	{
		return -1.f;
	}
	return ClipPlayer.GetAccumulatedTime();
}

void FElysiumBipedAnimProxy::ResyncPosition(float PositionSeconds)
{
	if (Playing == nullptr)
	{
		return;
	}
	// A pending restart wins. The worker consumes bClipNeedsReinit in UpdateAnimationNode and
	// Initialize_AnyThread then sets the accumulator to the node's start position, so a resync
	// written in the same frame as a PlayDirect would be silently discarded. Bailing makes that
	// deterministic instead of dependent on which ran first; the next resync corrects it.
	if (bClipNeedsReinit)
	{
		return;
	}
	const float Length = Playing->GetPlayLength();
	const float Position = bPlayingLoop && Length > SMALL_NUMBER
		? FMath::Fmod(FMath::Max(0.f, PositionSeconds), Length)
		: FMath::Clamp(PositionSeconds, 0.f, Length);
	// SetAccumulatedTime writes the node's play time and nothing else — no play rate, no start
	// position, no reinit. That is the whole reason this is not Seek: the clip keeps running from
	// its corrected phase rather than freezing at it.
	//
	// This is a game-thread write to a field the worker advances. It is safe only because every
	// caller arrives through UElysiumBipedAnimInstance's GetProxyOnGameThread, which blocks on any
	// in-flight parallel evaluation. A future path reaching the proxy without that accessor turns
	// this into a silent data race on a float.
	ClipPlayer.SetAccumulatedTime(Position);
}

void FElysiumBipedAnimProxy::StopDirect()
{
	Playing = nullptr;
	ClipPlayer.SetSequence(nullptr);
}


// ================================================================================================
// UElysiumBipedAnimInstance
// ================================================================================================

void UElysiumBipedAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	CacheStateMachine();
}

void UElysiumBipedAnimInstance::CacheStateMachine()
{
	MachineIndex = INDEX_NONE;
	for (int32& Index : StateIndex)
	{
		Index = INDEX_NONE;
	}

	const FBakedAnimationStateMachine* Machine = nullptr;
	GetStateMachineIndexAndDescription(GLocomotionMachine, MachineIndex, &Machine);
	if (Machine == nullptr)
	{
		return;
	}
	// By name rather than by declaration order: a state's index is whatever the compiler assigned,
	// and the names are the contract `ElysiumAnimGraph::StateName` and the authored asset share.
	for (int32 State = 0; State < ElysiumAnimGraph::NumGraphStates; ++State)
	{
		const FName Name(ElysiumAnimGraph::StateName(static_cast<EElysiumGraphState>(State)));
		for (int32 i = 0; i < Machine->States.Num(); ++i)
		{
			if (Machine->States[i].StateName == Name)
			{
				StateIndex[State] = i;
				break;
			}
		}
	}
}

void UElysiumBipedAnimInstance::PublishSelection(const FElysiumAnimationSelection& Selection,
	const FElysiumResolvedAnimation& Assets)
{
	Pending = Selection;
	PendingBlendSpace = Assets.Space;
	PendingSequence = Assets.Sequence;
	PendingUpperBodySpace = Assets.OverlaySpace;
	PendingUpperBodySequence = Assets.OverlaySequence;
	PendingUpperBodyMaskName = Assets.OverlayMaskName;
	PendingAdditiveSequence = Assets.AdditiveSequence;
	// 1.0 is the named stand-in for retail's unrecovered per-layer weight (ANM2); a manual driver
	// overrides it afterward through SetUpperBodyLayerWeight/SetAdditiveLayerWeight, which is why
	// this only resets the default on a NEW publish rather than every frame.
	PendingUpperBodyLayerWeight = (PendingUpperBodySpace != nullptr
		|| PendingUpperBodySequence != nullptr) ? 1.0f : 0.0f;
	PendingAdditiveLayerWeight = PendingAdditiveSequence != nullptr ? 1.0f : 0.0f;
}

void UElysiumBipedAnimInstance::ProjectUpperBodyLayer()
{
	RequestedUpperBodyBlendSpace = PendingUpperBodySpace;
	RequestedUpperBodySequence = PendingUpperBodySequence;
	RequestedUpperBodyMaskName = PendingUpperBodyMaskName;
	RequestedAdditiveSequence = PendingAdditiveSequence;
	AimYaw = Pending.AimYaw;
	AimPitch = Pending.AimPitch;
	UpperBodyLayerWeight = PendingUpperBodyLayerWeight;
	AdditiveLayerWeight = PendingAdditiveLayerWeight;

	// The layer lab's hand driver wins over the published record, and it has to: the player's own
	// driver republishes a selection every frame, so an override that merely wrote the pending record
	// would be overwritten before it was ever evaluated. This is what lets the owner judge a layer
	// over a MOVING host, which is the whole reason the lab survives this rung.
	if (bDebugUpperBody)
	{
		RequestedUpperBodyBlendSpace = DebugOverlaySpace;
		RequestedUpperBodySequence = DebugOverlaySequence;
		RequestedUpperBodyMaskName = DebugOverlayMaskName;
		RequestedAdditiveSequence = DebugAdditiveSequence;
		AimYaw = DebugAimYaw;
		AimPitch = DebugAimPitch;
		UpperBodyLayerWeight = (DebugOverlaySpace != nullptr || DebugOverlaySequence != nullptr)
			? DebugLayerWeight : 0.0f;
		AdditiveLayerWeight = DebugAdditiveSequence != nullptr ? DebugLayerWeight : 0.0f;
	}

	bUpperBodyHasBlendSpace = RequestedUpperBodyBlendSpace != nullptr;
	// The one pin-less write, and the only thing here that touches a node rather than a property.
	ApplyUpperBodyMask();
}

void UElysiumBipedAnimInstance::ArmDebugUpperBodyOverlay(UAnimSequence* Sequence, UBlendSpace* Space,
	FName MaskName, float Weight)
{
	bDebugUpperBody = true;
	DebugOverlaySequence = Sequence;
	DebugOverlaySpace = Space;
	DebugOverlayMaskName = MaskName;
	DebugLayerWeight = FMath::Clamp(Weight, 0.0f, 1.0f);
}

void UElysiumBipedAnimInstance::ArmDebugUpperBodyAdditive(UAnimSequence* Sequence, float Weight)
{
	bDebugUpperBody = true;
	DebugAdditiveSequence = Sequence;
	DebugLayerWeight = FMath::Clamp(Weight, 0.0f, 1.0f);
}

void UElysiumBipedAnimInstance::SetDebugUpperBodyAim(float Yaw, float Pitch)
{
	DebugAimYaw = Yaw;
	DebugAimPitch = Pitch;
}

void UElysiumBipedAnimInstance::ClearDebugUpperBodyLayer()
{
	bDebugUpperBody = false;
	DebugOverlaySequence = nullptr;
	DebugOverlaySpace = nullptr;
	DebugAdditiveSequence = nullptr;
	DebugOverlayMaskName = NAME_None;
}

void UElysiumBipedAnimInstance::ApplyUpperBodyMask()
{
	if (RequestedUpperBodyMaskName == AppliedUpperBodyMaskName)
	{
		return;
	}

	// The compiled graph's tag table. Absent on the plain native class — a body with no generated
	// graph package has no node to write to, which is a state rather than an error.
	IAnimClassInterface* AnimClass = IAnimClassInterface::GetFromClass(GetClass());
	const FAnimSubsystem_Tag* Tags = AnimClass != nullptr
		? AnimClass->FindSubsystem<FAnimSubsystem_Tag>() : nullptr;
	FAnimNode_LayeredBoneBlend* Layer = Tags != nullptr
		? Tags->FindNodeByTag<FAnimNode_LayeredBoneBlend>(
			FName(ElysiumAnimGraph::UpperBodyLayerTag), this)
		: nullptr;
	if (Layer == nullptr)
	{
		return;
	}

	// `SetBlendMask` asserts all three of these, so they are tested rather than assumed: a graph
	// rebuilt with a different node shape must not turn a mask write into a crash.
	if (Layer->BlendMode != ELayeredBoneBlendMode::BlendMask || !Layer->BlendPoses.IsValidIndex(0)
		|| !Layer->BlendMasks.IsValidIndex(0))
	{
		return;
	}

	// Resolved against the PLAYING skeleton, which is the whole reason the mask travels as a name:
	// the profile the node rebuilds its per-bone weights from has to belong to the skeleton those
	// bone indices are in. This is the same resolution Epic's own `ULayeredBoneBlendLibrary` performs.
	UBlendProfile* Profile = nullptr;
	if (!RequestedUpperBodyMaskName.IsNone())
	{
		USkeleton* Skeleton = GetProxyOnGameThread<FElysiumBipedAnimProxy>().GetSkeleton();
		Profile = Skeleton != nullptr
			? Skeleton->GetBlendProfile(RequestedUpperBodyMaskName) : nullptr;
		if (Profile == nullptr || Profile->Mode != EBlendProfileMode::BlendMask)
		{
			// A named mask the body's own skeleton does not carry. Refused rather than composed
			// unmasked, which would pull the whole rig toward the layer instead of the bones it owns
			// — the same failure the retired accumulator's gate existed to prevent.
			UE_LOG(LogTemp, Warning,
				TEXT("[elysium] upper-body mask '%s' is absent from this body's skeleton or is not a "
					"blend mask; the layer is left unmasked-refused"),
				*RequestedUpperBodyMaskName.ToString());
			return;
		}
	}

	// Null is a legal write: it is how a body that stopped carrying a layer gives the mask back.
	Layer->SetBlendMask(0, Profile);
	AppliedUpperBodyMaskName = RequestedUpperBodyMaskName;
}

void UElysiumBipedAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	// The garment's game-thread pass.
	Super::NativeUpdateAnimation(DeltaSeconds);

	// A component that was built before any graph existed, or whose class was swapped, resolves its
	// machine on the first update rather than staying inert for the body's whole life.
	if (MachineIndex == INDEX_NONE)
	{
		CacheStateMachine();
	}

	// --- a request that resolved no asset holds the pose it had -----------------------------------
	//
	// **This is retail's behaviour, not a guard bolted on.** A failed selection never reaches
	// `ResetSequenceInfo`: `m_nSequence` keeps whatever it held and the body goes on playing it. The
	// controlled corpus records exactly one such request on a validated player body — a ducked
	// phase-8 landing asking for `ACT_LAND_CROUCH`, whose selection returns `-1` and for which no
	// clip was ever observed (`docs/vtmb/animation_and_movers.md`).
	//
	// Projecting it anyway is what produced a visible **reference pose**: the state it routes to
	// takes its clip from the pin below, an unresolved request leaves that pin null, and a sequence
	// player with no asset evaluates to the skeleton's bind pose — a T-pose flash for as long as the
	// landing lasts. Declaring a state was never the problem; entering it with nothing to play was.
	//
	// The **record is untouched** and still names the miss, which is the whole reason a player miss
	// is a named one. Holding also leaves `OneShot` describing the pose that is actually on screen,
	// so the generation gate in the driver's owner reads stale and the latch falls back to its
	// timer — which is what ends the landing.
	// --- the upper-body layer, projected AHEAD of the hold branch ----------------------------------
	//
	// It rides beside the locomotion state rather than through it, so it does not wait on a gait
	// transition — and, for the same reason, a base pose that is being HELD must not freeze it. The
	// two are independent requests that happen to arrive on one record.
	ProjectUpperBodyLayer();

	bHoldingPose = ElysiumAnimGraph::ShouldHoldPose(bHasApplied, PendingSequence != nullptr,
		PendingBlendSpace != nullptr);
	if (bHoldingPose)
	{
		return;
	}

	// --- project the record onto what the graph reads --------------------------------------------
	RequestedState = ElysiumAnimGraph::StateFor(Pending);
	RequestedBlendSpace = PendingBlendSpace;
	RequestedSequence = PendingSequence;
	// A held stance repeats its into-pose, which is retail's reselect-and-restart expressed as a
	// loop (see `bRequestedLooping`). Every other state takes the model's own bit unchanged, and the
	// record keeps the authored value either way.
	bRequestedLooping = ElysiumAnimGraph::ShouldRepeatClip(RequestedState, Pending.bLooping);
	GridAxis0 = Pending.AxisValue[0];
	GridAxis1 = Pending.AxisValue[1];
	Speed = Pending.Speed;
	MoveYaw = Pending.MoveYaw;
	bHasBlendSpace = RequestedBlendSpace != nullptr;

	// The rules, decided here rather than in a rule graph. `bStateChanged` is measured against what
	// the machine is actually playing rather than against the last request, so a body whose graph
	// was rebuilt or whose state was entered from somewhere else still converges.
	bWantsIdle = RequestedState == EElysiumGraphState::Idle;
	bWantsWalk = RequestedState == EElysiumGraphState::Walk;
	bWantsRun = RequestedState == EElysiumGraphState::Run;
	bWantsSneak = RequestedState == EElysiumGraphState::Sneak;
	bWantsCrouch = RequestedState == EElysiumGraphState::Crouch;
	bWantsLeap = RequestedState == EElysiumGraphState::Leap;
	bWantsFalling = RequestedState == EElysiumGraphState::Falling;
	bWantsLand = RequestedState == EElysiumGraphState::Land;
	bStateChanged = MachineIndex != INDEX_NONE
		&& GetCurrentStateName(MachineIndex) != ElysiumAnimGraph::StateName(RequestedState);

	// --- one blend per discrete request change ---------------------------------------------------
	if (!bHasApplied || Pending.Generation != Applied.Generation)
	{
		// The authored fade, combined as retail combines it. It reaches the graph's inertialization
		// node through the slot node, which forwards whatever this writes into the proxy's slot-group
		// map — the supported native route, and the reason the graph asset carries only a ceiling.
		//
		// A duration of 0 is a legal request rather than a refusal, so `flags & 0x2`'s hard cut falls
		// out of the same call instead of needing a branch of its own.
		const float Blend = ElysiumAnimGraph::TransitionSeconds(
			bHasApplied ? &Applied : nullptr, Pending);
		RequestSlotGroupInertialization(FAnimSlotGroup::DefaultGroupName, Blend);

		Applied = Pending;
		bHasApplied = true;
	}

	// --- read the graph back ---------------------------------------------------------------------
	OneShot = FElysiumOneShotReport();
	OneShot.Generation = Pending.Generation;
	const int32 State = StateIndex[static_cast<uint8>(RequestedState)];
	// **`GetRelevantAnimTimeRemaining` answers `MAX_flt` when it finds no relevant asset player**,
	// not 0 — `FAnimNode_StateMachine::GetRelevantAnimTimeRemaining` returns it from the bottom of
	// the function, and `FAnimInstanceProxy` returns it again for an unknown machine. So the failure
	// direction is "infinitely long", and reporting that as a clip still playing is what hangs a
	// consumer: `Playing` is an answer, and an answer suppresses the latch's own timer. A body whose
	// landing resolved nothing would then stay in ACT_LAND forever rather than standing up.
	//
	// Anything that is not a sane finite duration therefore reports **nothing at all**, which leaves
	// `RemainingSeconds` at its "cannot say" −1 and routes the latch back to the fallback. The state
	// weight and the resolved clip are checked for the same reason and in the same direction.
	if (MachineIndex != INDEX_NONE && State != INDEX_NONE
		&& ElysiumAnimGraph::IsOneShotState(RequestedState)
		&& RequestedSequence != nullptr
		&& GetInstanceStateWeight(MachineIndex, State) > 0.99f)
	{
		const float Remaining = GetRelevantAnimTimeRemaining(MachineIndex, State);
		if (ElysiumAnimGraph::IsPlayableRemaining(Remaining, RequestedSequence->GetPlayLength()))
		{
			OneShot.bInOneShotState = true;
			OneShot.RemainingSeconds = Remaining;
			OneShot.bComplete = Remaining <= KINDA_SMALL_NUMBER;
		}
	}
}

bool UElysiumBipedAnimInstance::HasCompiledGraph() const
{
	// The class itself is the answer, and it is available before the first update: a generated
	// Animation Blueprint class carries the compiled graph, the plain native class carries none.
	// Asking the proxy for a root node would be the same question a frame later and only on the
	// worker's terms.
	//
	// **A cast of the class, not `IsChildOf` on it, and the difference is the whole answer.**
	// `UAnimBlueprintGeneratedClass` is the METACLASS of a generated graph class, not an ancestor of
	// it: `ABP_ElysiumBiped_C` derives from `UElysiumBipedAnimInstance`, and it is
	// `UAnimBlueprintGeneratedClass` that it is an instance OF. `IsChildOf` walks the superclass chain
	// and so answers false for every body that has a graph and every body that does not — a predicate
	// that is never true, on a path where false is the quiet fallback. `UClass::IsA` is private
	// precisely to stop the same confusion being written the other way round, so the cast is the door.
	return Cast<UAnimBlueprintGeneratedClass>(GetClass()) != nullptr;
}

bool UElysiumBipedAnimInstance::PlayOneShot(UAnimSequence* Sequence, bool bLoop, float BlendSeconds)
{
	if (Sequence == nullptr)
	{
		return false;
	}
	// No compiled graph means no slot node to play a montage into, so the clip player answers
	// instead. That is the body whose generated graph package is not on the mount — a named failure
	// rather than a silent one, and it still animates.
	if (!HasCompiledGraph())
	{
		PlayClip(Sequence, bLoop);
		return true;
	}
	// The FIRST clip has nothing to blend from and snaps in regardless, which is retail's own rule:
	// otherwise every body would fade up out of the reference pose on map load, because the slot's
	// source pose is a state machine that has been handed no asset yet.
	const float BlendIn = Montage_IsPlaying(ActiveSlotMontage) ? BlendSeconds : 0.0f;
	// A loop count of 0 is infinite. The blend in and out are the clip's own authored fade — the same
	// number the locomotion transition uses, so one authority serves both consumers.
	ActiveSlotMontage = PlaySlotAnimationAsDynamicMontage(Sequence, FAnimSlotGroup::DefaultSlotName,
		BlendIn, BlendSeconds, /*InPlayRate=*/1.0f, /*LoopCount=*/ bLoop ? 0 : 1);
	return ActiveSlotMontage != nullptr;
}

void UElysiumBipedAnimInstance::StopOneShot(float BlendSeconds)
{
	if (!HasCompiledGraph())
	{
		StopClip();
		return;
	}
	if (ActiveSlotMontage != nullptr)
	{
		Montage_Stop(BlendSeconds, ActiveSlotMontage);
		ActiveSlotMontage = nullptr;
	}
}

// ================================================================================================
// The cinematic clip path and the autolayers, over the proxy
//
// Every one of these goes through GetProxyOnGameThread, which blocks on any in-flight parallel
// evaluation — that block is the whole reason the writes below cannot race the worker.
// ================================================================================================

void UElysiumBipedAnimInstance::PlayClip(UAnimSequence* Sequence, bool bLoop)
{
	if (Sequence == nullptr)
	{
		return;
	}
	GetProxyOnGameThread<FElysiumBipedAnimProxy>().PlayDirect(Sequence, bLoop);
}

void UElysiumBipedAnimInstance::SeekClip(float PositionSeconds)
{
	GetProxyOnGameThread<FElysiumBipedAnimProxy>().Seek(PositionSeconds);
}

void UElysiumBipedAnimInstance::StopClip()
{
	GetProxyOnGameThread<FElysiumBipedAnimProxy>().StopDirect();
}

UAnimSequence* UElysiumBipedAnimInstance::GetPlayingClip() const
{
	return const_cast<UElysiumBipedAnimInstance*>(this)
		->GetProxyOnGameThread<FElysiumBipedAnimProxy>().GetPlaying();
}

float UElysiumBipedAnimInstance::GetClipPosition() const
{
	return const_cast<UElysiumBipedAnimInstance*>(this)
		->GetProxyOnGameThread<FElysiumBipedAnimProxy>().GetClipPosition();
}

void UElysiumBipedAnimInstance::ResyncClip(float PositionSeconds)
{
	GetProxyOnGameThread<FElysiumBipedAnimProxy>().ResyncPosition(PositionSeconds);
}

