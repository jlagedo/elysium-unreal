#include "Visual/ElysiumNpcAnimInstance.h"

#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"

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
}

void FElysiumNpcAnimProxy::CacheBones()
{
	FAnimationCacheBonesContext Context(this);
	for (FAnimNode_SequencePlayer_Standalone& Player : Players)
	{
		Player.CacheBones_AnyThread(Context);
	}
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
			return;
		}
		Players[Incoming].SetLoopAnimation(bLoop);
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
	Players[Next].SetStartPosition(0.f);
	bNeedsReinit[Next] = true;   // reset that player's play time on the worker

	Incoming = Next;
	Playing = Sequence;
	bPlayingLoop = bLoop;
	BlendAlpha = bSnap ? 1.f : 0.f;
	BlendRate = bSnap ? 0.f : 1.f / BlendSeconds;
	bInitialized = true;
}

void FElysiumNpcAnimProxy::UpdateAnimationNode(const FAnimationUpdateContext& InContext)
{
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

bool FElysiumNpcAnimProxy::Evaluate(FPoseContext& Output)
{
	if (!bInitialized || Players[Incoming].GetSequence() == nullptr)
	{
		Output.ResetToRefPose();
		return true;
	}

	// The incoming player always contributes; the outgoing one only while the crossfade runs,
	// so a settled NPC evaluates exactly one sequence.
	FPoseContext Incoming_(this);
	Players[Incoming].Evaluate_AnyThread(Incoming_);

	if (BlendAlpha >= 1.f || Players[1 - Incoming].GetSequence() == nullptr)
	{
		Output = Incoming_;
		return true;
	}

	FPoseContext Outgoing(this);
	Players[1 - Incoming].Evaluate_AnyThread(Outgoing);

	FAnimationPoseData OutData(Output);
	const FAnimationPoseData OutgoingData(Outgoing);
	const FAnimationPoseData IncomingData(Incoming_);
	// WeightOfPoseOne is the *first* argument's share, so the outgoing pose leads and the
	// incoming one takes BlendAlpha.
	FAnimationRuntime::BlendTwoPosesTogether(OutgoingData, IncomingData, 1.f - BlendAlpha, OutData);
	return true;
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
