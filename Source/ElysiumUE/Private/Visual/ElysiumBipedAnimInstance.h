#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Visual/ElysiumAnimGraph.h"
#include "Visual/ElysiumBodyAnimInstance.h"

#include "ElysiumBipedAnimInstance.generated.h"

class UAnimMontage;
class UAnimSequence;
class UBlendSpace;
class UElysiumAnimLayerMask;
struct FElysiumResolvedAnimation;

// Every VtMB body's animation host — the native base an Animation Blueprint compiles against, and
// the only thing between the resolver's published selection and Unreal's own graph.
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
//
// Two stages ride beside the graph rather than inside it, and both are temporary by design:
//
// - the **autolayer accumulator**, VtMB's own local-space combine, because no graph node owns a
//   mask today. `CCC10`'s layered bone blend replaces it.
// - the **cinematic clip player**, one standalone sequence player the theatre pins to absolute
//   scene time. A montage cannot hold that phase lock, so the clip path survives until `ANM6`
//   migrates choreographed playback. While it holds a clip it IS the body pose — the graph's
//   output is not consumed at all — which is what makes a scene's pose a function of scene time
//   rather than of accumulated animation delta.
//
// It also carries the answer for a body whose generated graph package is absent: the instance is
// then the plain native class with no compiled graph, and the clip player is what still poses it.

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

	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual void CacheBones() override;

	// The compiled graph, then the shared tail over its output pose.
	//
	// `FAnimInstanceProxy::Evaluate` is a stub that returns false to mean "I did not handle this,
	// go run the graph" — it is NOT the graph. So the graph is run explicitly here and `true` is
	// returned, or the caller would run it a second time.
	virtual bool Evaluate(FPoseContext& Output) override;

	// The graph's own nodes advance through the base implementation. The garment does not — it is
	// not a graph node — so it takes its timestep first, ahead of anything the graph does, because a
	// body standing in its reference pose still has a garment that has to hang.
	//
	// The two node stages this proxy owns itself advance here as well. A node that is never
	// `Update_AnyThread`'d sits at its start position forever and evaluates one frozen frame, and
	// the base call cannot reach a node that is not in the compiled graph.
	virtual void UpdateAnimationNode(const FAnimationUpdateContext& InContext) override;

	// --- the cinematic clip path (survives until `ANM6`) ------------------------------------------
	//
	// Stand one clip as the whole body pose, replacing the graph's output for as long as it holds.
	// The theatre's own path: a scene starts a clip and then pins it to absolute scene time every
	// frame. There is no crossfade here on purpose — the graph owns every blend a gait or a one-shot
	// needs, and a scene's clip boundaries are the scene's to time.
	void PlayDirect(UAnimSequence* Sequence, bool bLoop);
	// Pin the current clip to an absolute authored time, freezing its play rate. A scene frame's
	// pose becomes a function of scene time rather than of accumulated animation delta.
	void Seek(float PositionSeconds);
	// Re-phase a clip that is still PLAYING, without pinning it: this touches neither the play rate
	// nor the start position, so a free-running clip keeps running — it only moves where from.
	void ResyncPosition(float PositionSeconds);
	// Where the clip sits, in clip seconds. Negative when nothing is playing — "cannot say" is a
	// different answer from "at zero", and a caller measuring drift must not read the second as the
	// first.
	float GetClipPosition() const;
	void StopDirect();
	UAnimSequence* GetPlaying() const { return Playing; }
	bool IsPlayingLoop() const { return bPlayingLoop; }

	// --- autolayers (retired by `CCC10`) ----------------------------------------------------------
	//
	// Start or re-weight one autolayer. Weight is retail's `layer_weight`, the scalar the
	// accumulator receives from its caller; 1 is the whole layer. Asking for a sequence already on
	// a layer only re-weights it, so a caller may drive the weight every frame without restarting
	// the clip.
	//
	// Which combine it goes through is the SEQUENCE's property, not the caller's — retail's one
	// accumulator branches on the clip's own flags. An additive `_delta` accumulates
	// post-multiplied over every bone; an ordinary `*_layer` is a complementary-weight blend gated
	// by the bones that clip owns. False when the sequence is neither, which is the gate that keeps
	// the second honest — see the definition.
	bool RequestLayer(UAnimSequence* Sequence, bool bLoop, float Weight);
	// Drop one layer, or every layer. Dropping is immediate: an autolayer carries no fade of its
	// own, and the weight IS the ramp.
	void StopLayer(const UAnimSequence* Sequence);
	void StopAllLayers();
	int32 NumLayers() const;

private:
	// The autolayer half: VtMB's layers composed onto the body pose in LOCAL space — a `_delta`
	// accumulated post-multiplied, a `*_layer` blended under its bone mask. Runs between the body
	// and the composition stages, which is retail's own order — see the definition.
	void EvaluateLayers(FPoseContext& Output);
	// Read one slot's bone gate out of the sequence's own metadata and the skeleton's blend
	// profile. Game thread, at the request: the worker has no business resolving an asset.
	void ResolveLayerMask(int32 Layer, const UAnimSequence* Sequence,
		const UElysiumAnimLayerMask* Mask);
	// `elysium.LayerDump`'s one-shot readout: what the layer pose actually contains, per bone, so
	// what the applier reads can be checked against the container's own numbers instead of against
	// a screenshot.
	void DumpLayerPose(int32 Layer, const FPoseContext& Pose);

	// Two, because retail's autolayer pattern is one masked `<weapon>_aim_layer` plus one unmasked
	// `<weapon>_<action>_delta` and never more; a third request replaces the weakest, which is the
	// smallest contribution by construction.
	static constexpr int32 MaxLayers = 2;

	// Standalone (not `_Standalone`-suffixed by accident): the plain-C++ variant of the sequence
	// player whose setters actually write, unlike the Blueprint-bound `FAnimNode_SequencePlayer`
	// whose `SetSequence` is a no-op outside a compiled anim graph.
	UPROPERTY(Transient) FAnimNode_SequencePlayer_Standalone ClipPlayer;
	UPROPERTY(Transient) FAnimNode_SequencePlayer_Standalone LayerPlayers[MaxLayers];

	UPROPERTY(Transient) TObjectPtr<UAnimSequence> Playing = nullptr;
	bool bPlayingLoop = true;
	// A player whose sequence changed needs `Initialize_AnyThread` to reset its time accumulator —
	// `SetSequence` alone leaves it wherever the previous clip had run to. Flagged on the game
	// thread, consumed on the worker where the contexts are valid.
	bool bClipNeedsReinit = false;

	// Retail's `layer_weight` per layer, 0 for a slot carrying nothing. Not a fade: an autolayer
	// has no authored transition, and a caller that wants one ramps this itself.
	float LayerWeights[MaxLayers] = {};
	bool bLayerNeedsReinit[MaxLayers] = {};
	// Which combine this slot's sequence asks for, latched at the request rather than re-derived
	// per frame: `IsValidAdditive()` walks the sequence's additive settings, and the answer cannot
	// change while the slot holds it.
	bool bLayerAdditive[MaxLayers] = {};
	// The bones an ordinary layer owns, as retail's per-bone `weight`@0 gate: 1 inside the mask, 0
	// outside, indexed by SKELETON bone index. Empty for a layer that owns the whole rig and for
	// every additive, where the mask is already expressed by the additive identity.
	TArray<float> LayerMasks[MaxLayers];
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
	// and a plain label to a sequence. A label with no baked fan arrives here as a sequence with no
	// blend space, so the graph needs no knowledge beyond an "is valid" branch.
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

	// Whether the resolver answered with a fan or with a single cell. The gait states branch on this
	// — the graph knows only which asset it was handed.
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

	// Whether the last update took the hold branch — a request that resolved no asset, left playing
	// what it already had. Published rather than recomputed: the inputs to `ShouldHoldPose` are
	// private, and a reader deriving its own answer would be a second copy of the rule that decided
	// the pose on screen. A held frame is the correct behaviour and looks exactly like the T-pose
	// defect it prevents, so a debug surface has to be able to tell a reader which it is looking at.
	bool IsHoldingPose() const { return bHoldingPose; }

	// --- the shared one-shot seam -----------------------------------------------------------------
	//
	// Answered over a dynamic slot montage, which is the design's own shape for a one-shot and needs
	// no baked montage asset. Nothing here reaches the locomotion state machine: a scripted clip
	// plays OVER the gait rather than replacing the thing that owns it.
	virtual bool PlayOneShot(UAnimSequence* Sequence, bool bLoop, float BlendSeconds) override;
	virtual void StopOneShot(float BlendSeconds) override;

	// --- the cinematic clip path ------------------------------------------------------------------
	//
	// Stand one clip as the whole body pose, over the proxy's own player rather than over the graph.
	// A choreographed scene owns the body outright for its duration and pins the clip to scene time,
	// which is the one thing the slot-montage seam above cannot do.
	void PlayClip(UAnimSequence* Sequence, bool bLoop = true);
	void SeekClip(float PositionSeconds);
	void StopClip();
	UAnimSequence* GetPlayingClip() const;

	// Read and correct the phase of a clip that is still playing. This is the measurement half of
	// the substrate-clock phase lock: a caller compares the position against elapsed game time and
	// re-phases only when the two have parted company.
	float GetClipPosition() const;
	void ResyncClip(float PositionSeconds);

	// --- autolayers -------------------------------------------------------------------------------
	//
	// Compose a VtMB autolayer over whatever owns the body pose. Weight is retail's own layer scalar;
	// re-asking with a new weight re-weights the running layer rather than restarting it. Independent
	// of everything above it: a stance change underneath does not touch a layer, and a layer does not
	// transition.
	//
	// Either kind, decided from the sequence: a `_delta` accumulates as an additive over the whole
	// rig, a partial-body `*_layer` blends in under the bones it owns. False when the sequence is
	// neither — a plain pose clip, or one built by glTFRuntime. Only the `.eskm` bake writes a delta
	// in the form Unreal hands back as a delta (`AdditiveAnimType`) and only it carries the bone mask
	// (`UElysiumAnimLayerMask`), so an unbaked layer is refused rather than composed out of a pose
	// that is really the reference pose for every bone it does not touch.
	bool PlayLayer(UAnimSequence* Sequence, float Weight = 1.f, bool bLoop = true);
	void StopLayer(UAnimSequence* Sequence);
	void StopAllLayers();
	int32 GetActiveLayers() const;

	// Whether this instance was built from a compiled Animation Blueprint. False for the plain
	// native class, which is what a body falls back to when the generated graph package is not on
	// the mount: there is no state machine and no montage slot to play into, so the one-shot seam
	// routes through the clip player instead.
	bool HasCompiledGraph() const;

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
	// The branch the last update took, for `IsHoldingPose` above.
	bool bHoldingPose = false;

	// The montage the one-shot seam is currently running, so `StopOneShot` ends that one rather than
	// whatever else the slot may have picked up.
	UPROPERTY(Transient) TObjectPtr<UAnimMontage> ActiveSlotMontage = nullptr;

	int32 MachineIndex = INDEX_NONE;
	int32 StateIndex[ElysiumAnimGraph::NumGraphStates];
	FElysiumOneShotReport OneShot;
};
