#include "Visual/ElysiumBipedAnimInstance.h"

#include "Visual/ElysiumNpcAnimSubsystem.h"   // FElysiumResolvedAnimation

#include "Animation/AnimMontage.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimStateMachineTypes.h"
#include "Animation/BlendSpace.h"
#include "Animation/Skeleton.h"

namespace
{
	// The one machine the authored graph carries. The asset test asserts this name, so a graph whose
	// machine was renamed fails in a tier rather than silently reporting no completion.
	const FName GLocomotionMachine(TEXT("Locomotion"));
}

// ================================================================================================
// FElysiumBipedAnimProxy
// ================================================================================================

bool FElysiumBipedAnimProxy::Evaluate(FPoseContext& Output)
{
	// The compiled graph. `FAnimInstanceProxy::Evaluate` returns false to mean "not handled, run the
	// graph", so calling it here would evaluate nothing and pose the reference pose.
	EvaluateAnimationNode(Output);
	// Then the composition stages and the face, over whatever the graph produced — the same tail the
	// cast wears, which is what stops a player body shipping frozen eyes and untwisted forearms.
	EvaluateTail(Output);
	return true;
}

void FElysiumBipedAnimProxy::UpdateAnimationNode(const FAnimationUpdateContext& InContext)
{
	FAnimInstanceProxy::UpdateAnimationNode(InContext);
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

bool UElysiumBipedAnimInstance::PlayOneShot(UAnimSequence* Sequence, bool bLoop, float BlendSeconds)
{
	if (Sequence == nullptr)
	{
		return false;
	}
	// A loop count of 0 is infinite. The blend in and out are the clip's own authored fade — the same
	// number the locomotion transition uses, so one authority serves both consumers.
	ActiveSlotMontage = PlaySlotAnimationAsDynamicMontage(Sequence, FAnimSlotGroup::DefaultSlotName,
		BlendSeconds, BlendSeconds, /*InPlayRate=*/1.0f, /*LoopCount=*/ bLoop ? 0 : 1);
	return ActiveSlotMontage != nullptr;
}

void UElysiumBipedAnimInstance::StopOneShot(float BlendSeconds)
{
	if (ActiveSlotMontage != nullptr)
	{
		Montage_Stop(BlendSeconds, ActiveSlotMontage);
		ActiveSlotMontage = nullptr;
	}
}
