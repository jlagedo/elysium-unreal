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
// One stage rides beside the graph rather than inside it, and it is temporary by design: the
// **cinematic clip player**, one standalone sequence player the theatre pins to absolute scene
// time. A montage cannot hold that phase lock, so the clip path survives until `ANM6` migrates
// choreographed playback. While it holds a clip it IS the body pose — the graph's output is not
// consumed at all — which is what makes a scene's pose a function of scene time rather than of
// accumulated animation delta.
//
// The autolayer accumulator that used to sit beside it is gone: `CCC10` moved VtMB's layers into
// the compiled graph, where the bone mask is a property of the blend node rather than of the pose
// feeding it. The mask itself is the one thing the graph cannot carry as a pin — it is edit-time
// state on `FAnimNode_LayeredBoneBlend` — so it is written at runtime through
// `ApplyUpperBodyMask`, which is the same door Epic's own `ULayeredBoneBlendLibrary` uses.
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

private:
	// Standalone (not `_Standalone`-suffixed by accident): the plain-C++ variant of the sequence
	// player whose setters actually write, unlike the Blueprint-bound `FAnimNode_SequencePlayer`
	// whose `SetSequence` is a no-op outside a compiled anim graph.
	UPROPERTY(Transient) FAnimNode_SequencePlayer_Standalone ClipPlayer;

	UPROPERTY(Transient) TObjectPtr<UAnimSequence> Playing = nullptr;
	bool bPlayingLoop = true;
	// A player whose sequence changed needs `Initialize_AnyThread` to reset its time accumulator —
	// `SetSequence` alone leaves it wherever the previous clip had run to. Flagged on the game
	// thread, consumed on the worker where the contexts are valid.
	bool bClipNeedsReinit = false;
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

	// --- the upper-body layer (CCC10) ---------------------------------------------------------------
	//
	// Either the bake-time autolayer binding the base channel's own resolved host declared, or an
	// activity-keyed `UpperBody`/`Additive` selection's own single asset — the caller decides which
	// by what it hands `PublishSelection`'s `Assets`. Exactly one of the two overlay fields is
	// non-null: a melee `*_bobble_layer` is a plain sequence, an aim grid is a blend space (steered by
	// `AimYaw`/`AimPitch` below). The `_delta` additive composes independently, on top, and carries no
	// mask of its own.
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	TObjectPtr<UBlendSpace> RequestedUpperBodyBlendSpace = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	TObjectPtr<UAnimSequence> RequestedUpperBodySequence = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	bool bUpperBodyHasBlendSpace = false;
	// The overlay's own baked mask, as the name its clip's metadata carries — never guessed from a
	// weapon's grip; the mask is per-clip data, and a resolver that picked one from "is this melee"
	// would get the two-handed melee weapons wrong (`docs/vtmb/animation_and_movers.md` A.4).
	//
	// **Read by no pin.** The layered blend's mask is edit-time state on the node rather than an
	// input, so this reaches the graph through `ApplyUpperBodyMask` below instead of through a
	// generated property copy. Published because the debug surface reads what the graph was given.
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	FName RequestedUpperBodyMaskName;
	// Retail's per-layer caller weight has no recovered value (`docs/project/animation-roadmap.md`
	// ANM2): 1.0 is the named stand-in whenever `PublishSelection` hands over a layer, and
	// `SetUpperBodyLayerWeight`/`SetAdditiveLayerWeight` below are the seam a caller ramps instead.
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float UpperBodyLayerWeight = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	TObjectPtr<UAnimSequence> RequestedAdditiveSequence = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float AdditiveLayerWeight = 0.0f;

	// Where the upper-body layer aims, in the pose parameters' own degrees — the aim grid's own axes.
	// The player's own producer pins this at the literal 0.0f/pitch-only
	// (`docs/vtmb/animation_and_movers.md`); an NPC's own aim producer arrives with `CCC11`.
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float AimYaw = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float AimPitch = 0.0f;

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
	//
	// A playable asset ends the DefaultSlot one-shot `BuildNpcVisual` armed as a standing idle: that
	// slot covers the state machine, which is the pose the driver is publishing.
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

	// How many upper-body layers are composing — the overlay and the additive are separate slots, so
	// this is 0, 1 or 2. Read off the published state rather than off a node, so a readout and the
	// pose cannot disagree.
	int32 GetActiveLayers() const
	{
		return ((RequestedUpperBodyBlendSpace != nullptr || RequestedUpperBodySequence != nullptr)
				&& UpperBodyLayerWeight > 0.f ? 1 : 0)
			+ (RequestedAdditiveSequence != nullptr && AdditiveLayerWeight > 0.f ? 1 : 0);
	}

	// The ramp seam a manual driver (the green room's layer lab) uses instead of the 1.0 stand-in
	// `PublishSelection` writes whenever it hands over a layer. Written to the STAGED value, not the
	// published one — `NativeUpdateAnimation` copies the staged value through every frame, so writing
	// the published property directly would be undone on the very next tick.
	void SetUpperBodyLayerWeight(float Weight)
	{
		PendingUpperBodyLayerWeight = FMath::Clamp(Weight, 0.f, 1.f);
	}
	void SetAdditiveLayerWeight(float Weight)
	{
		PendingAdditiveLayerWeight = FMath::Clamp(Weight, 0.f, 1.f);
	}

	// --- the layer lab's hand driver (CCC10) --------------------------------------------------------
	//
	// Hold an upper-body layer OVER whatever the driver publishes. It is an override rather than a
	// second publisher for one reason: a driven body republishes a selection every frame, so a write
	// that only landed on the pending record would be overwritten before it was ever evaluated — and
	// judging a layer over a moving host is exactly what the green room's layer lab exists to do.
	//
	// **The overlay and the additive are separate slots and do not displace each other**, which is
	// what lets a host's declared pair be armed one entry at a time — the shape retail's own
	// two-entry autolayer binding takes, and the shape the retired accumulator's two slots had.
	//
	// `Sequence` and `Space` are the two shapes an overlay takes and are never both set; `MaskName`
	// is the bone mask the clip's own metadata names, resolved against the playing skeleton like
	// every other mask.
	void ArmDebugUpperBodyOverlay(UAnimSequence* Sequence, UBlendSpace* Space, FName MaskName,
		float Weight);
	void ArmDebugUpperBodyAdditive(UAnimSequence* Sequence, float Weight);
	// The aim grid's own axes, which the ordinary player producer pins at zero — so a lab that could
	// not steer them could not tell an aim grid from a still pose.
	void SetDebugUpperBodyAim(float Yaw, float Pitch);
	void ClearDebugUpperBodyLayer();
	bool HasDebugUpperBodyLayer() const { return bDebugUpperBody; }

	// Whether this instance was built from a compiled Animation Blueprint. False for the plain
	// native class, which is what a body falls back to when the generated graph package is not on
	// the mount: there is no state machine and no montage slot to play into, so the one-shot seam
	// routes through the clip player instead.
	bool HasCompiledGraph() const;

	// Whether this compiled class's named state actually has a blend-space player. The authored
	// helper `ElysiumAnimGraph::StateCanPlayBlendSpace` is the same question against the live
	// pair list; this one reads the baked state so a stale generated class cannot silently
	// evaluate a grid as a null sequence.
	bool CompiledStateCanPlayBlendSpace(EElysiumGraphState State) const;

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

	// Project the upper-body half of the record — the layer, its mask, the aim pair and the two
	// weights — with the lab's hand driver overriding it. Separate from the base projection because
	// it runs ahead of the hold branch: a held gait must not freeze a weapon layer.
	void ProjectUpperBodyLayer();

	// Hand the layered blend its bone mask (CCC10). The node's mask is edit-time state with no pin,
	// so it is written directly on the node — found through `FAnimSubsystem_Tag` under
	// `ElysiumAnimGraph::UpperBodyLayerTag` — which is the same door Epic's own
	// `ULayeredBoneBlendLibrary::SetBlendMask` goes through.
	//
	// Called from `NativeUpdateAnimation`, on the game thread, before the worker is dispatched, and
	// **only when the name actually changes**: the setter invalidates the node's cached per-bone
	// weights, so writing it every frame would rebuild them every frame.
	void ApplyUpperBodyMask();

	UPROPERTY(Transient) FElysiumBipedAnimProxy Proxy;

	// The record whole rather than scattered scalars: the transition duration needs the OUTGOING
	// selection's authored fade, so the previous record has to survive the next push.
	FElysiumAnimationSelection Pending;
	FElysiumAnimationSelection Applied;
	UPROPERTY(Transient) TObjectPtr<UBlendSpace> PendingBlendSpace = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> PendingSequence = nullptr;
	// CCC10 — the upper-body layer half of the same publish, staged the same way.
	UPROPERTY(Transient) TObjectPtr<UBlendSpace> PendingUpperBodySpace = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> PendingUpperBodySequence = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> PendingAdditiveSequence = nullptr;
	FName PendingUpperBodyMaskName;
	// What the node was last actually given, so the mask is written on change rather than per frame.
	// `NAME_None` before the first write, which is also the name a body with no layer resolves to —
	// the two agree by construction, so a body that never had a layer never touches the node.
	FName AppliedUpperBodyMaskName;
	float PendingUpperBodyLayerWeight = 0.0f;
	float PendingAdditiveLayerWeight = 0.0f;
	bool bHasApplied = false;

	// The layer lab's override, applied over the published record every frame while it is armed.
	bool bDebugUpperBody = false;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> DebugOverlaySequence = nullptr;
	UPROPERTY(Transient) TObjectPtr<UBlendSpace> DebugOverlaySpace = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> DebugAdditiveSequence = nullptr;
	FName DebugOverlayMaskName;
	float DebugLayerWeight = 1.0f;
	float DebugAimYaw = 0.0f;
	float DebugAimPitch = 0.0f;
	// The branch the last update took, for `IsHoldingPose` above.
	bool bHoldingPose = false;

	// The montage the one-shot seam is currently running, so `StopOneShot` ends that one rather than
	// whatever else the slot may have picked up.
	UPROPERTY(Transient) TObjectPtr<UAnimMontage> ActiveSlotMontage = nullptr;

	int32 MachineIndex = INDEX_NONE;
	int32 StateIndex[ElysiumAnimGraph::NumGraphStates];
	bool bStateHasBlendSpacePlayer[ElysiumAnimGraph::NumGraphStates] = {};
	bool bRecordedAnyBlendSpacePlayer = false;
	// A compiled class that does not carry the machine is reported once per instance. The lookup
	// itself still retries — a class can answer later — but the report is a defect in the generated
	// asset, and a defect restated every frame by every body drowns the log it is meant to reach.
	bool bReportedMissingMachine = false;
	FElysiumOneShotReport OneShot;
};
