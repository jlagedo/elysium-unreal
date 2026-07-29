#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_SequencePlayer.h"

#include "ElysiumNpcAnimInstance.generated.h"

class UAnimSequence;

// The NPC animation host (roadmap 8.5) — a native C++ anim instance, no Blueprint and no anim
// graph asset.
//
// It exists for one reason: VtMB's stance banks ship almost no authored transitions. Of the 21
// dispositions × 2 gendered banks, exactly one carries a `Stance_<D>_Trans_<a>_<b>` clip
// (`Stance_Neutral_Trans_1_2`, male only), so a stance change, a fidget, or a
// `scripted_sequence`'s clip cannot route through an authored blend the way the naming first
// suggests. Source blended between sequences itself, so a short engine crossfade reproduces the
// original's feel rather than inventing polish.
//
// Two sequence players and a lerp: a request starts a crossfade from whatever is currently
// playing to the new clip. Repeating the same looping stance is a no-op; a one-shot request or a
// loop-mode change restarts from frame zero, which scripted_sequence/choreo playback requires.

USTRUCT()
struct FElysiumNpcAnimProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

	FElysiumNpcAnimProxy() = default;
	explicit FElysiumNpcAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual void CacheBones() override;
	// The node graph's own update — where the sequence players advance their play time. The
	// base-class `Update(float)` is NOT enough: a node that is never Update_AnyThread'd sits at
	// its start position forever and evaluates one frozen frame.
	virtual void UpdateAnimationNode(const FAnimationUpdateContext& InContext) override;
	virtual bool Evaluate(FPoseContext& Output) override;

	// Start playing Sequence, crossfading over BlendSeconds (0 snaps). Called from the game
	// thread through UElysiumNpcAnimInstance, never from the worker.
	void Request(UAnimSequence* Sequence, bool bLoop, float BlendSeconds);
	// Pin the current clip to an absolute authored time. Cinematic scenes call this every scene
	// frame, making the pose a function of scene time rather than accumulated animation delta.
	void Seek(float PositionSeconds);
	void Stop();

	UAnimSequence* GetPlaying() const { return Playing; }
	bool IsPlayingLoop() const { return bPlayingLoop; }
	bool IsBlending() const { return BlendAlpha < 1.f; }

private:
	// Standalone (not _Standalone-suffixed by accident): the plain-C++ variant of the sequence
	// player whose setters actually write, unlike the Blueprint-bound FAnimNode_SequencePlayer
	// whose SetSequence is a no-op outside a compiled anim graph.
	UPROPERTY(Transient) FAnimNode_SequencePlayer_Standalone Players[2];

	// Index of the player the crossfade is moving *to*. The other holds the outgoing pose.
	int32 Incoming = 0;
	// Weight of the incoming player: 1 = settled, <1 = mid-blend.
	float BlendAlpha = 1.f;
	float BlendRate = 0.f;

	UPROPERTY(Transient) TObjectPtr<UAnimSequence> Playing = nullptr;
	bool bPlayingLoop = true;
	bool bInitialized = false;
	// A player whose sequence changed needs Initialize_AnyThread to reset its time accumulator —
	// SetSequence alone leaves it wherever the previous clip had run to. Flagged on the game
	// thread, consumed on the worker where the contexts are valid.
	bool bNeedsReinit[2] = { false, false };
};

UCLASS(Transient)
class UElysiumNpcAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	// Default crossfade for a stance/fidget/gesture change. Short: these are all idle-to-idle
	// swaps on a standing body, and a long blend reads as a drift rather than a change of pose.
	static constexpr float DefaultBlendSeconds = 0.25f;

	// Play a clip, crossfading from whatever is current. Repeating an already-looping clip does
	// nothing; a repeated one-shot or a loop-mode change restarts it from frame zero.
	void PlayClip(UAnimSequence* Sequence, bool bLoop = true, float BlendSeconds = DefaultBlendSeconds);
	void SeekClip(float PositionSeconds);
	void StopClip();

	UAnimSequence* GetPlayingClip() const { return Proxy.GetPlaying(); }

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return &Proxy; }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy*) override {}

private:
	UPROPERTY(Transient) FElysiumNpcAnimProxy Proxy;
	friend struct FElysiumNpcAnimProxy;
};
