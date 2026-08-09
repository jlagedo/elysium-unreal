#pragma once

#include "CoreMinimal.h"
#include "Visual/ElysiumAnimGraph.h"
#include "Visual/ElysiumBodyAnimInstance.h"

#include "ElysiumBipedAnimInstance.generated.h"

class UAnimMontage;
class UAnimSequence;
class UBlendSpace;
struct FElysiumResolvedAnimation;

// The player body's animation host (CCC5) — the native base an Animation Blueprint compiles
// against, and the only thing between the resolver's published selection and Unreal's own graph.
//
// **It decides nothing.** `FElysiumAnimationDriver` has already classified, translated, picked and
// resolved by the time anything here runs; this projects that record onto the properties the graph
// reads and asks the graph for one blend. That is step 6 of
// `docs/architecture/animation-architecture.md` section 3.3 — "publish graph parameters, it does not
// repeat selection" — made structural rather than remembered.
//
// The graph asset is a TEMPLATE Animation Blueprint: it carries no target skeleton and no asset
// reference, and every node takes its asset from the pins below. One graph therefore plays every
// model the resolver picks assets for, and the tracked graph source encodes nothing derived from
// the user's game.

// What the graph can say back about a one-shot it is playing.
//
// Generation-stamped because the answer outlives the question by a frame: a report describing a leap
// that has already been replaced must not be allowed to end the request that replaced it.
struct FElysiumOneShotReport
{
	uint32 Generation = 0;
	bool bInOneShotState = false;
	bool bComplete = false;
	// Negative for "cannot say", which is a different answer from "none left" — a state with no
	// resolved clip would otherwise report zero remaining and read as finished on its first frame.
	float RemainingSeconds = -1.0f;
};

USTRUCT()
struct FElysiumBipedAnimProxy : public FElysiumBodyAnimProxy
{
	GENERATED_BODY()

	FElysiumBipedAnimProxy() = default;
	explicit FElysiumBipedAnimProxy(UAnimInstance* Instance) : FElysiumBodyAnimProxy(Instance) {}

	// The compiled graph, then the shared tail over its output pose.
	//
	// `FAnimInstanceProxy::Evaluate` is a stub that returns false to mean "I did not handle this,
	// go run the graph" — it is NOT the graph. So the graph is run explicitly here and `true` is
	// returned, or the caller would run it a second time.
	virtual bool Evaluate(FPoseContext& Output) override;

	// The graph's own nodes advance through the base implementation. The garment does not — it is
	// not a graph node — so it takes its timestep first, ahead of anything the graph does, because a
	// body standing in its reference pose still has a garment that has to hang.
	virtual void UpdateAnimationNode(const FAnimationUpdateContext& InContext) override;
};

UCLASS(Transient)
class UElysiumBipedAnimInstance : public UElysiumBodyAnimInstance
{
	GENERATED_BODY()

public:
	// --- what the graph reads ---------------------------------------------------------------------
	//
	// Written once per frame on the game thread in `NativeUpdateAnimation`; read on the worker by the
	// transition rules and by the asset pins' generated property copies. That generated copy IS the
	// latch — there is no second copy on the proxy, because a member the graph never reads would be
	// exactly the dead scaffolding this rung exists not to build.

	// Which of the eight states should own the body. The transition rules are equality tests against
	// this and nothing else, so the graph holds no classification of its own.
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	EElysiumGraphState RequestedState = EElysiumGraphState::Idle;

	// The resolved assets. Exactly one is non-null: a movement fan resolves to its baked blend space
	// and a plain label to a sequence. `elysium.BlendSpaces 0` makes the resolver answer with the
	// single selected cell instead, which arrives here as a sequence with no blend space — so the
	// A/B needs no knowledge in the graph beyond an "is valid" branch.
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	TObjectPtr<UBlendSpace> RequestedBlendSpace = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	TObjectPtr<UAnimSequence> RequestedSequence = nullptr;

	// **Whether the graph should repeat this clip** — which is the model's own loop bit for
	// everything except a held stance. `crouch` is a 61-frame NON-looping into-pose while `walk_0`
	// loops, and both play through the same states, so the bit is read off the model rather than
	// authored on the node.
	//
	// The exception is `Crouch`, and it is faithful rather than a convenience. Retail holds a
	// sustained unarmed crouch by **reselecting** sequence 8: `StudioFrameAdvance` clamps the
	// non-looping cycle and sets `m_bSequenceFinished`, the next unchanged `ACT_CROUCH` request sees
	// that flag and marks the selection dirty, and `ResetSequenceInfo` clears the cycle so the same
	// clip plays again (`docs/vtmb/animation_and_movers.md`). Repeated indefinitely that is a loop,
	// reached by a different mechanism — so the pin says loop, and the body does what retail's does
	// instead of freezing on the terminal frame. Holding that last frame is recorded as **not
	// faithful and therefore not an owner divergence**; this is the fix, not a choice.
	//
	// The **record is not touched**: `FElysiumAnimationSelection::bLooping` keeps the authored
	// `false`, so Cog, the channel recorder and the MCP surface still report what the model says.
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	bool bRequestedLooping = true;

	// Where the fan is sampled, in the pose parameters' own degrees. Axis 1 is ignored by a
	// one-dimensional grid; the aim pair arrives with the weapon rung.
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float GridAxis0 = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float GridAxis1 = 0.0f;

	// Read by nothing in the graph today. Published because the debug surface and the channel
	// recorder read the same values the graph does, so a readout and a pose cannot disagree.
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float Speed = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float MoveYaw = 0.0f;

	// Whether the resolver answered with a fan or with a single cell. `elysium.BlendSpaces 0` makes
	// it answer with the cell, and the gait states branch on this rather than on the cvar — the graph
	// knows nothing about console variables, only about which asset it was handed.
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	bool bHasBlendSpace = false;

	// --- the transition rules ---------------------------------------------------------------------
	//
	// Every rule in the machine is a single read of one of these, and nothing else. The comparison
	// that produces them happens here, in C++, where it is asserted — a rule graph that computed
	// anything would be a second place the body's state is decided.
	//
	// The machine is a hub: each state exits to one conduit when the request no longer matches what
	// is playing, and the conduit enters whichever state does match. That is sixteen transitions
	// instead of fifty-six, with every pair still reachable.
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	bool bStateChanged = false;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	bool bWantsIdle = true;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	bool bWantsWalk = false;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	bool bWantsRun = false;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	bool bWantsSneak = false;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	bool bWantsCrouch = false;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	bool bWantsLeap = false;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	bool bWantsFalling = false;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	bool bWantsLand = false;

	// --- the seam ---------------------------------------------------------------------------------
	//
	// Pushed once per frame by whichever pass owns the driver — the map actor's post-move pass for
	// the player. The instance does not reach for the driver: it lives on `AElysiumMapActor` behind a
	// pimpl while the visual is a component of the pawn, so a pull would invert the layering and
	// carry a null branch for every map that seats no pawn.
	void PublishSelection(const FElysiumAnimationSelection& Selection,
		const FElysiumResolvedAnimation& Assets);

	// What the graph says about the one-shot it is playing. Read by the driver's owner BEFORE the
	// next publish, so the latch consumes a report describing the request it is about to advance past.
	const FElysiumOneShotReport& GetOneShotReport() const { return OneShot; }

	// The last record this instance was handed, for the debug surface.
	const FElysiumAnimationSelection& GetAppliedSelection() const { return Applied; }

	// --- the shared one-shot seam -----------------------------------------------------------------
	//
	// Answered over a dynamic slot montage, which is the design's own shape for a one-shot and needs
	// no baked montage asset. Nothing here reaches the locomotion state machine: a scripted clip
	// plays OVER the gait rather than replacing the thing that owns it.
	virtual bool PlayOneShot(UAnimSequence* Sequence, bool bLoop, float BlendSeconds) override;
	virtual void StopOneShot(float BlendSeconds) override;

protected:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return &Proxy; }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy*) override {}

private:
	// Resolve the state machine and its eight states once, by the names `ElysiumAnimGraph::StateName`
	// spells. A graph whose states were renamed reports no completion rather than reporting a wrong
	// one, and the asset test is what catches the rename.
	void CacheStateMachine();

	UPROPERTY(Transient) FElysiumBipedAnimProxy Proxy;

	// The record whole rather than scattered scalars: the transition duration needs the OUTGOING
	// selection's authored fade, so the previous record has to survive the next push.
	FElysiumAnimationSelection Pending;
	FElysiumAnimationSelection Applied;
	UPROPERTY(Transient) TObjectPtr<UBlendSpace> PendingBlendSpace = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> PendingSequence = nullptr;
	bool bHasApplied = false;

	// The montage the one-shot seam is currently running, so `StopOneShot` ends that one rather than
	// whatever else the slot may have picked up.
	UPROPERTY(Transient) TObjectPtr<UAnimMontage> ActiveSlotMontage = nullptr;

	int32 MachineIndex = INDEX_NONE;
	int32 StateIndex[ElysiumAnimGraph::NumGraphStates];
	FElysiumOneShotReport OneShot;
};
