#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Visual/ElysiumAnimGraph.h"
#include "ElysiumOverlayStack.h"
#include "Visual/ElysiumBodyAnimInstance.h"

#include "ElysiumBipedAnimInstance.generated.h"

class UAnimationAsset;
class UAnimMontage;
class UAnimSequence;
class UBlendSpace;
class UElysiumAnimLayerMask;
class USkeleton;
class USkeletalMeshComponent;
struct FAnimNode_BlendStack;
struct FElysiumResolvedAnimation;

// Every VtMB body's animation host — the native base an Animation Blueprint compiles against, and
// the only thing between the resolver's published selection and Unreal's own graph.
//
// **It decides nothing.** `FElysiumAnimationDriver` has already classified, translated, picked and
// resolved by the time anything here runs; this projects that record onto the properties the graph
// reads and asks the graph for one blend. That is step 6 of
// — "publish graph parameters, it does not
// repeat selection" — made structural rather than remembered.
//
// The graph asset is a TEMPLATE Animation Blueprint: it carries no target skeleton and no asset
// reference, and every node takes its asset from the pins below. One graph therefore plays every
// model the resolver picks assets for, and the tracked graph source encodes nothing derived from
// the user's game.
//
// One stage rides beside the graph rather than inside it: the **cinematic clip player**, one
// standalone sequence player the theatre pins to absolute scene time. A montage cannot hold that
// phase lock. While it holds a clip it IS the body pose — the graph's output is not consumed at
// all — which is what makes a scene's pose a function of scene time rather than of accumulated
// animation delta.
//
// VtMB's autolayers live in the compiled graph, where the bone mask is a property of the blend
// node rather than of the pose feeding it. The mask itself is the one thing the graph cannot carry
// as a pin — it is edit-time
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

// What the graph can say back about the last REAL transition it asked for — one record per blend
// requested, never per frame.
//
// A blend is decided in exactly one place (a new generation on the applied record), and the answer
// is otherwise unobservable: the duration goes out on a graph pin and the pose it produces is a fade
// a reader cannot distinguish from a correct hard cut after the fact. So the decision is recorded
// where it is made rather than sampled afterward.
//
// Generation-stamped for the same reason `FElysiumOneShotReport` is: the record describes the
// transition INTO that generation, so a reader can tell a stale readout from a live one.
struct FElysiumBlendReport
{
	uint32 Generation = 0;
	// What was actually put on the stack's `BlendTime` pin, after the combine and both refusals.
	float RequestedSeconds = 0.0f;
	// Zero seconds for a stated reason. `flags & 0x2` on the incoming clip is an authored hard cut.
	bool bSnap = false;
	// The other zero: there was no outgoing CLIP to fade FROM — either nothing had ever been
	// published, or what was published resolved no asset and left the stack holding nothing at all.
	// The two are one fact for this purpose, and both refuse the fade. It is carried rather than
	// inferred from an empty `FromAnimation`, which a record that resolved no clip also leaves empty.
	bool bFirstPublish = false;
	// Whether this blend crossed the state vocabulary or stayed inside it — `Pending.GraphState`
	// against `Applied.GraphState`, taken at the moment the blend was asked for.
	//
	// **A readout, not a rule.** The graph carries no state machine to leave, so nothing reads this
	// to decide anything; it is recorded because the trace, the Cog row and the channel recorder all
	// report a walk-to-run transition differently from a walk-to-walk republish, and this record is
	// the only place that distinction is stated.
	bool bStateTransition = false;
	FString FromAnimation;
	FString ToAnimation;
	// World seconds when the transition was requested. Negative is "no transition yet", the same
	// "never" sentinel every aged readout in this repo uses; printing it as an age would read as a bug.
	double StampSeconds = -1.0;
};

// One reaction, whole, as the graph's reaction branch needs it.
//
// **`Space` and `Sequence` are never both set**, the same "exactly one of these two" shape the base
// channel and the upper-body overlay take: a directional hit resolves to its baked fan and a plain
// reaction label to a single clip.
//
// `LengthSeconds` is what the phase clock is armed from, and it is the ENGINE's answer for a fan —
// the blended length of the samples the axis value selects — because a fan's cells do not share a
// length and the pose the graph strikes is the blend of two of them.
//
// `OwnerStem`/`Label` are the clip identity the branch publishes as its phase — the vocabulary key
// the fan or the clip was reached by, never the resolved cell (`FElysiumClipIdentity`). Empty is
// legal and means this reaction carries no timeline to walk.
struct FElysiumReactionPlay
{
	UBlendSpace* Space = nullptr;
	UAnimSequence* Sequence = nullptr;
	FString OwnerStem;
	FString Label;
	// Where the fan is sampled, in the pose parameter's own degrees. Ignored when `Sequence` is set.
	float AxisValue = 0.0f;
	float LengthSeconds = 0.0f;
	float BlendInSeconds = 0.1f;
	float BlendOutSeconds = 0.3f;

	// What ends this play. The whole arithmetic below forks on it, because the three families end for
	// three different reasons and none of them is a wall-clock stamp.
	EElysiumReactionRelease Release = EElysiumReactionRelease::ClipCompletion;

	// Whether the branch repeats its clip for as long as it stands. True only for a `Predicate` play:
	// a pose that has to be on screen for an unbounded hold cannot be a one-shot's terminal frame.
	bool bLoop = false;

	// A `Predicate` play is released by its producer, never by a clock.
	bool IsHeld() const { return Release == EElysiumReactionRelease::Predicate; }

	bool IsValid() const
	{
		if ((Space != nullptr) == (Sequence != nullptr) || !(LengthSeconds > 0.0f))
		{
			return false;
		}
		// An `Envelope` play's whole life IS its stated fades, so a pair that sums to nothing describes
		// no reaction at all — and the claim it would take reads a non-positive hold as "until
		// released", which parks the channel. Refused by name rather than parked.
		return !(Release == EElysiumReactionRelease::Envelope && !(TotalSeconds() > 0.0f));
	}

	// How long `bReactionActive` stands — the phase clock's own seed, and one of three answers:
	//
	// - `ClipCompletion`: the clip's own length less the out-fade, so the fade completes ON the clip's
	//   end. A cell shorter than its own out-fade answers zero and fades straight back out, which is
	//   the honest reading of a clip that ends inside its own transition.
	// - `Envelope`: the stated blend-in, and NOTHING after it. This is retail's `DamageFlinch` weight
	//   triangle — fade in over 0.1, fade out over 0.3, no hold, ended by the clock alone — so the
	//   peak is at the blend-in and the whole thing is gone at 0.4 whatever the cell's own length is.
	//   The value used is the blend-in the producer STATED, not the one the instance ends up applying:
	//   a body with nothing to blend from snaps in and holds the peak for the same span.
	// - `Predicate`: no answer. Negative is the sentinel every "never" in this repo uses, and the
	//   instance never seeds a countdown from it.
	float ActiveSeconds() const
	{
		switch (Release)
		{
		case EElysiumReactionRelease::Predicate:
			return -1.0f;
		case EElysiumReactionRelease::Envelope:
			return FMath::Max(BlendInSeconds, 0.0f);
		default:
			return FMath::Max(LengthSeconds - FMath::Max(BlendOutSeconds, 0.0f), 0.0f);
		}
	}

	// The branch's whole life: the hold above, then the out-fade that follows it. The channel claim
	// takes this rather than the clip's length, so a claim cannot expire while the branch is still
	// fading — one expression, so the two cannot disagree. Negative for a held play, which has no
	// life to state: its claim stands until the producer gives it back.
	float TotalSeconds() const
	{
		return IsHeld() ? -1.0f : ActiveSeconds() + FMath::Max(BlendOutSeconds, 0.0f);
	}
};

// Which producer a base-channel phase is being read off, in the order the pose composes.
//
// The four run CONCURRENTLY, which is the whole reason this is an enumeration of arms rather than a
// mode: a reaction replaces the locomotion pose without stopping the montage under it, and that
// montage rides over a blend stack which never stopped either. Only one of them can be the body's
// server timeline at a time — retail has exactly one — but all four clocks keep running, so which
// one publishes changes while none of them ends.
enum class EElysiumBasePhaseSource : uint8
{
	None = 0,
	Clip,        // the proxy's cinematic clip player, which replaces the graph outright
	Reaction,    // the graph's reaction branch, which replaces the locomotion pose
	Montage,     // the DefaultSlot dynamic montage, which rides over the blend stack
	Locomotion,  // the locomotion blend stack underneath everything
	Count
};

// One clip a single producer has standing on the base channel.
//
// **One record per producer, never one shared record.** A single slot would let the newest arm
// erase a clip that is still playing, and the displaced one could never take the channel back — so
// a swing armed while a flinch stands would publish, be overwritten on the next update, and its
// commit event would never fire. The four records are independent; precedence decides which one is
// published, not which one exists.
struct FElysiumArmedClip
{
	FElysiumClipIdentity Identity;
	float LengthSeconds = 0.0f;
	// `m_flPlaybackRate` for this arm. The length is the clip's own and the position the host reports
	// is in clip time, so the cycle is still `position / length`; the rate is carried so a consumer
	// timing a window FORWARD from that cycle knows how fast it is advancing.
	float PlayRate = 1.0f;
	bool bLooping = false;
	// Bumped once per genuine (re)start. **A displaced arm keeps its id**: the same play resumed is
	// not a new play, and a bump would restart its timeline and re-fire every record behind it.
	uint32 PlayId = 0;
	// Where the dispatcher last saw this arm — `FElysiumClipPhase::AnchorCycle`, published with the
	// phase and frozen the moment a higher arm takes the channel. It is zero for a clip a play seam
	// started, and the observed fraction for the blend stack, which starts nothing this class sees.
	float AnchorCycle = 0.0f;

	bool IsArmed() const { return Identity.IsValid(); }
};

// One overlay slot's staged state, between the driver's publish and the pins the graph evaluates.
//
// A `USTRUCT` rather than nine parallel arrays because three of its members are object pointers the
// graph holds across frames and have to be GC-rooted; a static array of a reflected struct is the one
// shape that roots them without spelling `UPROPERTY` twelve times.
//
// It is staging, never the layer: the layer's own cycle, weight and lifetime belong to
// `FElysiumOverlayLayer` on the driver. What is here is what this instance has been told and what it
// has managed to hand the graph, which are different questions — a mask the playing skeleton cannot
// answer for is refused here while the layer goes on standing.
USTRUCT()
struct FElysiumOverlaySlotStaging
{
	GENERATED_BODY()

	UPROPERTY(Transient) TObjectPtr<UAnimSequence> Sequence = nullptr;
	UPROPERTY(Transient) TObjectPtr<UBlendSpace> Space = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> Additive = nullptr;
	// The layer the evaluator is currently standing on. A publish that swaps the ASSET while still
	// carrying the previous layer's cycle would otherwise seat a fresh clip mid-motion, so the
	// playhead is re-seated at the head whenever this moves.
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> Posed = nullptr;

	FName MaskName;
	FName AimMaskName;
	float Weight = 0.0f;
	float Cycle = 0.0f;

	// What this slot's blend node was last actually given, so the mask is written on change rather
	// than per frame — the setter invalidates the node's cached per-bone weights.
	FName AppliedMaskName;
	// And the name a refusal was last reported for: a refused mask never reaches the node, so the
	// applied name cannot latch it and the warning would repeat every frame the layer stands.
	FName ReportedMaskName;
	// The aim blend's own pair, for the second masked node inside this slot's branch.
	FName AppliedAimMaskName;
	FName ReportedAimMaskName;

	// The source skeleton and retarget-source name this slot's own bank-remap node was last given a
	// table for — read off the playing asset every frame so the node is re-resolved and re-written
	// only on CHANGE, the same reason the mask above is. Both identify the donor pose: names are not
	// globally unique across bank-family skeletons.
	TWeakObjectPtr<USkeleton> AppliedBankRemapSkeleton;
	FName AppliedBankRemapSource;
	bool bReportedBankRemapNodeFault = false;

	// A maskless layer is refused rather than composed, and said once: it is a bake or vocabulary
	// fault that does not change frame to frame, and a per-frame line would bury it.
	bool bReportedMaskless = false;
	// A fault in the compiled graph itself — no node under this slot's tag, or a node whose shape
	// `SetBlendMask` cannot take.
	bool bReportedNodeFault = false;
	bool bReportedAimNodeFault = false;

	// This slot's published phase record and the arm behind it. **One arm per slot, because a slot
	// has one producer at a time**: allocation gives a layer its own slot, so the layer standing here
	// is the only one there is — unlike the base, where four clocks run concurrently and only one of
	// them is the timeline.
	FElysiumClipPhase Phase;
	FElysiumArmedClip Arm;
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

	// The cinematic clip path.
	//
	// Stand one clip as the whole body pose, replacing the graph's output for as long as it holds.
	// The theatre's own path: a scene starts a clip and then pins it to absolute scene time every
	// frame. There is no crossfade here on purpose — the graph owns every blend a gait or a one-shot
	// needs, and a scene's clip boundaries are the scene's to time.
	//
	// **A repeated identical LOOPING request holds its clip rather than resetting it**, which is
	// retail's own rule for the ordinary ideal route: the controlled trace re-requests a held
	// `ACT_CROUCH` 151 times and the clip runs 1.791 s uninterrupted
	// (`docs/vtmb/animation_and_movers.md`). `bRestart` is the recovered restart helper's answer to
	// the same request — `RestartIdealActivity` clears the current activity first, so an attack,
	// reload, pre-jump or land asked for again re-fires from frame one. Returns whether the clip
	// actually (re)started, because the phase arm above it must not name a new play the pose refused.
	bool PlayDirect(UAnimSequence* Sequence, bool bLoop, bool bRestart, float PlayRate = 1.0f);
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
	// Whether the position above still belongs to the PREVIOUS clip. `SetSequence` does not reset the
	// time accumulator, so between a `PlayDirect`/`Seek` and the worker consuming the restart,
	// `GetClipPosition` answers where the clip that was replaced had run to. A caller that divides
	// that by the NEW clip's length gets a phase for a clip nobody played.
	bool HasPendingClipRestart() const { return bClipNeedsReinit; }

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
	// What the graph reads.
	//
	// Written once per frame on the game thread in `NativeUpdateAnimation`; read on the worker by the
	// transition rules and by the asset pins' generated property copies. That generated copy IS the
	// latch — there is no second copy on the proxy, because a member the graph never reads would be
	// exactly the dead scaffolding this rung exists not to build.

	// Which of the eight states should own the body. **Read by no pin.** The base channel is one
	// blend stack rather than eight states, so nothing in the graph branches on this; it is published
	// because the record, the Cog row, the trace and the channel recorder all name the state the
	// resolver projected, and they must all name the same one.
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	EElysiumGraphState RequestedState = EElysiumGraphState::Idle;

	// The resolved base-channel asset, whichever shape it took: a movement fan is a `UBlendSpace` and
	// a plain label a `UAnimSequence`, and the blend stack plays either off this one pin. The two are
	// staged separately below because `ShouldHoldPose` asks about the pair; only the answer is joined.
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	TObjectPtr<UAnimationAsset> RequestedAsset = nullptr;

	// The authored fade this request is entitled to, as `ElysiumAnimGraph::TransitionSeconds`
	// combined it — whole, and capped by nothing. Written only when a new generation actually asks
	// for a blend, because the stack consumes it at the instant it pushes a player and a value that
	// drifted between requests would be read by the wrong one.
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float RequestedBlendSeconds = 0.0f;

	// Where a fan is sampled, in the shape the stack takes it: `(GridAxis0, GridAxis1, 0)`. Ignored
	// outright when the asset is a sequence — a sequence player reports the zero vector back, which
	// is why the node's re-blend threshold is set past anything this can reach.
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	FVector RequestedBlendParameters = FVector::ZeroVector;

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

	// Whether the resolver answered with a fan or with a single cell. **Read by no pin either**: the
	// stack takes one asset and asks the asset what it is. Published for the same reason the state
	// is — the debug surface and the channel recorder read what the graph was given.
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	bool bHasBlendSpace = false;

	// The upper-body layer.
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
	// Retail's per-layer caller weight has no recovered value:
	// 1.0 is the named stand-in whenever `PublishSelection` hands over a layer, and
	// `SetUpperBodyLayerWeight`/`SetAdditiveLayerWeight` below are the seam a caller ramps instead.
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float UpperBodyLayerWeight = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	TObjectPtr<UAnimSequence> RequestedAdditiveSequence = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float AdditiveLayerWeight = 0.0f;

	// The overlay SLOT, which is a different mechanism from the three layers above.
	//
	// Retail's `CBaseAnimatingOverlay` layer stack: four slots, in composition order.
	//
	// The three properties above are the bake-time autolayers the base channel's own resolved HOST
	// declares, and they travel with the base clip. These are layers a PRODUCER armed on the UpperBody
	// channel, each outliving any number of base selections underneath it. They therefore ride their
	// own chained layered blends, placed after the autolayer blend — the order retail accumulates in.
	//
	// **Flat names rather than an array, because the graph binds a pin to a VARIABLE.** Every asset
	// this graph plays arrives on a pin driven by a `Get <variable>` node the generator places
	// (`ElysiumAnimGraphLibrary`), and an array element would need a second node kind in generated
	// text for no behavioural gain. The generator and `ProjectSlotLayer` both index them through the
	// accessors below, so the flatness stops at the declaration.
	//
	// Per slot, in the order the branch composes them:
	//
	//  * `Slot<N>Sequence`      the layer's motion, posed by an evaluator pinned to `Slot<N>Time`
	//  * `Slot<N>BlendSpace`    the aim grid the layer's own clip declares, over that motion
	//  * `Slot<N>AimWeight`     1 while that grid stands, 0 otherwise — retail's autolayers within a
	//                           sequence ride at a hardcoded 1.0, and the layer's own envelope is
	//                           applied once by `Slot<N>Weight` below
	//  * `Slot<N>Additive`      the `_delta` that clip declares, additively after the grid
	//  * `Slot<N>AdditiveWeight`  the same 1-or-0
	//  * `Slot<N>Weight`        the ENVELOPED weight the record carries
	//                           (`ElysiumOverlay::WeightForCycle`), never the `m_flWeightMax` ceiling:
	//                           an attack layer snaps to full on the frame it is armed while a reload
	//                           ramps over a fifth of its cycle at each end, and writing the ceiling
	//                           would compose both the same way
	//  * `Slot<N>Time`          where the evaluators are pinned, in the layer clip's own seconds —
	//                           the LAYER's cycle projected onto the clip, never a clock the graph
	//                           advances
	//  * `Slot<N>NormalizedTime`  the same instant as the fraction a blend space states time in,
	//                           derived from the seconds rather than tracked beside them so the two
	//                           branches cannot disagree about where in the shot the body is
	//  * `Slot<N>MaskName`      the layer's own baked bone mask. **Read by no pin**, for exactly the
	//                           reason `RequestedUpperBodyMaskName` is: `BlendMasks` is edit-time
	//                           state, so it reaches the graph through `ApplySlotMask` instead

	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	TObjectPtr<UAnimSequence> Slot0Sequence = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	TObjectPtr<UBlendSpace> Slot0BlendSpace = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float Slot0AimWeight = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	TObjectPtr<UAnimSequence> Slot0Additive = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float Slot0AdditiveWeight = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float Slot0Weight = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float Slot0Time = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float Slot0NormalizedTime = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	FName Slot0MaskName;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	TObjectPtr<UAnimSequence> Slot1Sequence = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	TObjectPtr<UBlendSpace> Slot1BlendSpace = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float Slot1AimWeight = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	TObjectPtr<UAnimSequence> Slot1Additive = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float Slot1AdditiveWeight = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float Slot1Weight = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float Slot1Time = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float Slot1NormalizedTime = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	FName Slot1MaskName;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	TObjectPtr<UAnimSequence> Slot2Sequence = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	TObjectPtr<UBlendSpace> Slot2BlendSpace = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float Slot2AimWeight = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	TObjectPtr<UAnimSequence> Slot2Additive = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float Slot2AdditiveWeight = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float Slot2Weight = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float Slot2Time = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float Slot2NormalizedTime = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	FName Slot2MaskName;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	TObjectPtr<UAnimSequence> Slot3Sequence = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	TObjectPtr<UBlendSpace> Slot3BlendSpace = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float Slot3AimWeight = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	TObjectPtr<UAnimSequence> Slot3Additive = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float Slot3AdditiveWeight = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float Slot3Weight = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float Slot3Time = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float Slot3NormalizedTime = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	FName Slot3MaskName;

	// One slot's projected pins, addressed by index. **The one door**: the projection, the take-down
	// and the readouts all index through these, so a slot that is added or renamed is one edit rather
	// than a search for every spelling of `Slot2`.
	TObjectPtr<UAnimSequence>& SlotSequenceAt(int32 SlotIndex);
	TObjectPtr<UBlendSpace>& SlotBlendSpaceAt(int32 SlotIndex);
	TObjectPtr<UAnimSequence>& SlotAdditiveAt(int32 SlotIndex);
	float& SlotAimWeightAt(int32 SlotIndex);
	float& SlotAdditiveWeightAt(int32 SlotIndex);
	float& SlotWeightAt(int32 SlotIndex);
	float& SlotTimeAt(int32 SlotIndex);
	float& SlotNormalizedTimeAt(int32 SlotIndex);
	FName& SlotMaskNameAt(int32 SlotIndex);

	// Where the upper-body layer aims, in the pose parameters' own degrees — the aim grid's own axes.
	// The player's own producer pins this at the literal 0.0f/pitch-only
	// (`docs/vtmb/animation_and_movers.md`).
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float AimYaw = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Locomotion")
	float AimPitch = 0.0f;

	// The reaction branch.
	//
	// The publish surface for the branch the graph carries between the one-shot slot and the
	// upper-body layer. Written by `PlayReaction`/`StopReaction` below and by nothing else — a
	// reaction is a discrete producer event, not a per-frame projection like the locomotion half.
	//
	// The pair is the same "exactly one of these two" shape the base channel and the overlay take: a
	// directional hit resolves to its baked fan and a plain reaction label to a single sequence, and
	// `bReactionHasBlendSpace` is which of the two the graph should evaluate.
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Reaction")
	TObjectPtr<UBlendSpace> RequestedReactionBlendSpace = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Reaction")
	TObjectPtr<UAnimSequence> RequestedReactionSequence = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Reaction")
	bool bReactionHasBlendSpace = false;

	// Where the reaction fan is sampled, in the pose parameters' own degrees — `hit_yaw` for a
	// directional flinch. The fans VtMB ships are one-dimensional, so the grid's second axis is not
	// exposed: a two-axis reaction is refused by name rather than half-steered.
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Reaction")
	float ReactionAxis0 = 0.0f;

	// Whether the reaction pose owns the body at all. False here is the whole branch's off switch.
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Reaction")
	bool bReactionActive = false;

	// Retail's own flinch fade, stated as two values because the blend list takes the newly-active
	// child's time: entering the reaction uses the first, returning to the locomotion pose the
	// second.
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Reaction")
	float ReactionBlendInSeconds = 0.1f;
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Reaction")
	float ReactionBlendOutSeconds = 0.3f;

	// Whether the standing reaction REPEATS. Only a held play does — a pose that stands for the whole
	// of a predicate has to loop rather than freeze on its terminal frame, which is the same rule
	// `ElysiumAnimGraph::ShouldRepeatClip` states for a held stance — and every struck reaction is a
	// one-shot whose own end releases it, so a looping flinch would never report complete.
	//
	// It is a graph PIN on both of the branch's players, which is what lets a held FAN repeat there at
	// all: the two loop bits are edit-time node state the compiler folds to a constant, so a branch
	// whose players were built non-looping can never be made to repeat at runtime.
	UPROPERTY(BlueprintReadOnly, Category = "Elysium|Reaction")
	bool bReactionLoops = false;

	// Whether the standing reaction is a HELD one — a claim a predicate releases rather than a
	// duration. Not a graph pin: the branch evaluates the same way either way, and what this changes
	// is the phase clock, which never counts down while it is set.
	bool IsReactionHeld() const { return bReactionActive && bReactionHeld; }

	// The montage the one-shot seam is running RIGHT NOW, or null — an identity, not a handle.
	// `StopOneShot` ends whatever the slot currently holds, which after a later arm is somebody else's
	// play; a caller that armed a one-shot earlier and has to stop only its own compares against this
	// first.
	const UAnimMontage* GetActiveSlotMontage() const { return ActiveSlotMontage; }

	// The seam.
	//
	// Pushed once per frame by whichever pass owns the driver — the map actor's post-move pass for
	// the player. The instance does not reach for the driver: it lives on `AElysiumMapActor` behind a
	// pimpl while the visual is a component of the pawn, so a pull would invert the layering and
	// carry a null branch for every map that seats no pawn.
	//
	// A playable asset ends the DefaultSlot one-shot `BuildNpcVisual` armed as a standing idle: that
	// slot covers the blend stack, which is the pose the driver is publishing.
	void PublishSelection(const FElysiumAnimationSelection& Selection,
		const FElysiumResolvedAnimation& Assets);

	// What the graph says about the one-shot it is playing. Read by the driver's owner BEFORE the
	// next publish, so the latch consumes a report describing the request it is about to advance past.
	const FElysiumOneShotReport& GetOneShotReport() const { return OneShot; }

	// The last real transition this instance asked the blend stack for. Not a per-frame sample: it
	// stands until the next generation change, which is what makes a settled body's readout describe
	// the blend that produced the pose on screen rather than an empty current frame.
	const FElysiumBlendReport& GetBlendReport() const { return Blend; }

	// The last record this instance was handed, for the debug surface.
	const FElysiumAnimationSelection& GetAppliedSelection() const { return Applied; }

	// Whether the last update took the hold branch — a request that resolved no asset, left playing
	// what it already had. Published rather than recomputed: the inputs to `ShouldHoldPose` are
	// private, and a reader deriving its own answer would be a second copy of the rule that decided
	// the pose on screen. A held frame is the correct behaviour and looks exactly like the T-pose
	// defect it prevents, so a debug surface has to be able to tell a reader which it is looking at.
	bool IsHoldingPose() const { return bHoldingPose; }

	// The shared one-shot seam.
	//
	// Answered over a dynamic slot montage, which is the design's own shape for a one-shot and needs
	// no baked montage asset. Nothing here reaches the locomotion blend stack: a scripted clip plays
	// OVER the gait rather than replacing the thing that owns it.
	virtual bool PlayOneShot(const FElysiumClipIdentity& Identity, UAnimSequence* Sequence,
		bool bLoop, float BlendInSeconds, float BlendOutSeconds, bool bRestart = false,
		float PlayRate = 1.0f) override;
	virtual void StopOneShot(float BlendSeconds) override;

	// The overlay slot seam.
	//
	// Arm retail's `CBaseAnimatingOverlay` slot 0 on this body. **This is not a second one-shot
	// slot**: the layer COMPOSES over whatever owns the base pose through its own bone mask, so
	// nothing the blend stack, the montage slot or the reaction branch is doing is stopped, and a
	// bone the mask leaves out keeps the base pose exactly as it was.
	//
	// `Claim` is the channel claim the producer just took, and it carries the only duration the
	// mechanism has: `ElysiumAnimIntent::SlotWeightAt` and `SlotCycle` are both read off it here, so
	// the frame this arms on is already enveloped rather than snapped to the ceiling. The claim's
	// own driver republishes both numbers every frame afterwards through `PublishSelection`, which is
	// what walks the layer to its end and drops it — this states the frame it starts on.
	//
	// False when there is no sequence to compose, when it carries no baked bone mask (a maskless
	// layer composes at zero weight on every bone and poses nothing), or when the
	// claim states no clip length — a layer with no phase to ride is one still frame held at a fixed
	// weight for as long as the producer holds the channel.
	//
	// `Identity` is the (bank, label) pair the layer's event timeline is addressed by, taken first
	// exactly as `PlayOneShot` takes it and for the same reason: the ranged families ride this slot,
	// their clips carry the 3030-3044 commit ids, and a layer armed without a name has a timeline
	// nothing can look up. The claim's `Label` cannot stand in for it — a claim names no bank, and
	// the owner is the include DAG's answer rather than the body's own stem.
	// The full trio the clip composes as — its motion, its declared aim grid (with that grid's own
	// mask), and its declared additive — because an arm that staged only the sequence would pose one
	// frame of bare shot before the driver's first publish filled the rest in.
	bool PlaySlotLayer(int32 SlotIndex, const FElysiumClipIdentity& Identity, UAnimSequence* Sequence,
		FName MaskName, const FElysiumAnimationRequest& Claim, bool bSnap,
		UBlendSpace* AimSpace = nullptr, FName AimMaskName = NAME_None,
		UAnimSequence* Additive = nullptr);
	// Take the layer down now, rather than waiting for the next publish to stop naming it — the
	// staged record AND the pins the graph evaluates, because the bodies this is called on are the
	// ones that may never publish again. The release half of a producer that armed a layer and is
	// ending early.
	void StopSlotLayer(int32 SlotIndex);
	// Every slot at once.
	void StopAllSlotLayers();
	// The same call, addressed at a BODY rather than at a host.
	//
	// Static because every producer that has to end a layer holds a mesh and not a graph: a segment
	// run's stop path, an NPC motor giving back every claim at once, and the map actor doing the same
	// for the player. One door, because "the claim went back but the pose did not" is the defect, and
	// three spellings of the take-down are three places it can be forgotten. A body with no compiled
	// biped graph carries no slot at all, which is an ordinary absence rather than a failure.
	// `INDEX_NONE` stops every slot, which is what a body being released ENTIRELY means: a corpse, a
	// re-modelled body, a driver whose claims all went back at once, with no producer left to come
	// back for any of them. A named slot stops that layer alone, which is what one run's stop path
	// means — a shot standing in another slot is a different producer's and is not this one's to end.
	static void StopSlotLayerOn(USkeletalMeshComponent* Body, int32 SlotIndex = INDEX_NONE);

	// The phase seam.
	//
	// Where a channel stands on its clip. A pure member read: the snapshot is armed at play time and
	// its cycle refreshed once per update, so every reader in a frame — the event pass, the weapon's
	// `HasLiveAnimEventDispatch`, a debug surface — is handed the same record.
	//
	// Two channels answer. `Base` is the body's server timeline, published off whichever of its four
	// concurrent producers is posing it. `UpperBody` is the overlay slot, published off the claim
	// that armed the layer — it is a SECOND record rather than a second producer of the first,
	// because the slot composes over the base instead of competing for it, and both are standing on
	// their own clips at once. Every other channel is an ordinary negative: nothing publishes a phase
	// for it.
	virtual bool GetClipPhase(EElysiumAnimChannel Channel, FElysiumClipPhase& Out) const override;
	// The locomotion stack's own normalized accumulator -- what its blend-space player evaluated
	// the pose at, before any division of one clock by another. A harness records it beside the
	// published phase so the two can be held to the reference compositor separately. False when
	// no stack stands or it holds no asset.
	bool GetLocomotionNormalizedTime(float& OutNormalized);
	// One slot's evaluator time as the fraction its grid states time in -- the instant the slot's
	// pose was evaluated at, which the record's published cycle runs one tick ahead of. False when
	// the slot holds no sequence.
	bool GetSlotNormalizedTime(int32 SlotIndex, float& OutNormalized);
	// One overlay slot's phase, addressed by index. The channel accessor above answers the LOWEST live
	// slot, which is the only single answer a four-slot stack has; a producer holding a particular
	// layer's slot reads its own timeline through this so a shot fired during a reload does not walk
	// the reload's records.
	bool GetSlotClipPhase(int32 SlotIndex, FElysiumClipPhase& Out) const;

	// The reaction seam.
	//
	// Hand the graph's reaction branch a fan or a clip and switch it on. The branch REPLACES the base
	// pose rather than riding over it, so this is not a second one-shot slot: nothing the blend stack
	// or the montage slot is doing is stopped, and the moment the branch fades back out the
	// locomotion pose is exactly where it would have been.
	//
	// Returns false for a malformed play or a class with no compiled branch. Both are real refusals a
	// caller acts on — the second is a stale generated asset, and the caller's own fallback is what
	// keeps the body reacting at all.
	bool PlayReaction(const FElysiumReactionPlay& Play);
	// Drop the branch. The engine's own out-fade still runs: this clears `bReactionActive`, which is
	// what makes the base child the newly-active one, and the blend list takes `BlendTime_1` from
	// there. A caller wanting an instant stop is asking for a hard cut and does not have one.
	void StopReaction();
	// Whether this compiled class actually carries the tagged reaction branch. False on the plain
	// native class and on a generated class built before the branch existed — a real case, and the
	// reason the caller has a collapse path rather than an assertion.
	bool HasCompiledReactionBranch() const;

	// The cinematic clip path.
	//
	// Stand one clip as the whole body pose, over the proxy's own player rather than over the graph.
	// A choreographed scene owns the body outright for its duration and pins the clip to scene time,
	// which is the one thing the slot-montage seam above cannot do.
	//
	// `Identity` is the clip's vocabulary key, on the same contract `PlayOneShot` takes it: empty is
	// legal and publishes no phase, which is what every preview and lab stand hands over.
	//
	// `bRestart` is the recovered restart rule for this path: a repeated identical LOOPING request
	// holds its clip by default, and a restart-route request (an attack, a reload, a pre-jump, a land)
	// re-fires it from frame one with a new `PlayId`.
	void PlayClip(const FElysiumClipIdentity& Identity, UAnimSequence* Sequence, bool bLoop = true,
		bool bRestart = false, float PlayRate = 1.0f);
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

	// The layer lab's hand driver.
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
	// the mount: there is no blend stack and no montage slot to play into, so the one-shot seam
	// routes through the clip player instead.
	bool HasCompiledGraph() const;

	// Whether this compiled class actually carries the tagged locomotion blend stack. False on the
	// plain native class and on a generated class built before the stack existed — a real case, and
	// the reason a caller that needs the base channel has a refusal rather than an assertion. The
	// same shape as `HasCompiledReactionBranch` above, resolved once and reported once.
	bool HasCompiledLocomotionStack() const;

protected:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return &Proxy; }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy*) override {}

private:
	// The compiled graph's locomotion `FAnimNode_BlendStack`, found through `FAnimSubsystem_Tag`
	// under `ElysiumAnimGraph::LocomotionStackTag` — the same door `ApplyUpperBodyMask` uses. Null
	// on the plain native class, which is a state rather than a failure; null on a class that DOES
	// carry a compiled graph is a defect, and it is reported once per instance.
	//
	// **The lookup has no barrier of its own.** It reads compiled class data — the tag table and the
	// node property's offset — so resolving the POINTER is safe anywhere on the game thread, which is
	// what lets `HasCompiledLocomotionStack` above answer without one. Reading node STATE off the
	// result is not: that races an in-flight parallel evaluation, so `NativeUpdateAnimation` and
	// `PublishBasePhase` resolve it beside their `GetProxyOnGameThread` and thread the node down
	// rather than letting each reader ask.
	FAnimNode_BlendStack* FindLocomotionStack();

	// Project the upper-body half of the record — the layer, its mask, the aim pair and the two
	// weights — with the lab's hand driver overriding it. Separate from the base projection because
	// it runs ahead of the hold branch: a held gait must not freeze a weapon layer.
	void ProjectUpperBodyLayer();

	// Hand the layered blend its bone mask. The node's mask is edit-time state with no pin,
	// so it is written directly on the node — found through `FAnimSubsystem_Tag` under
	// `ElysiumAnimGraph::UpperBodyLayerTag` — which is the same door Epic's own
	// `ULayeredBoneBlendLibrary::SetBlendMask` goes through.
	//
	// Called from `NativeUpdateAnimation`, on the game thread, before the worker is dispatched, and
	// **only when the name actually changes**: the setter invalidates the node's cached per-bone
	// weights, so writing it every frame would rebuild them every frame.
	//
	// **Returns whether the layer may compose.** False means the node still holds some other mask —
	// the previous clip's, or the graph's saved null — so the caller has to take the pose down; the
	// verdict is never stored, so a skeleton that is only late binding refuses this frame and applies
	// the next.
	bool ApplyUpperBodyMask();

	// Hand the BASE channel's own bank-remap node (`ElysiumAnimGraph::BankRemapTag(INDEX_NONE)`) the
	// table for whatever retarget source `PendingSequence`/`PendingBlendSpace` currently plays —
	// read off the asset itself, never off a stem, so this is exactly as good on a cinematic body as
	// on an ordinary one. Correction-only: unlike the mask appliers above, a refusal here never has
	// to take the pose down, because a node that cannot be reached simply leaves the incoming pose
	// exactly as Unreal's own retargeting-off composition produced it — a defect in the compiled
	// graph, never a reason to blank the body.
	void ApplyBaseBankRemap();

	// Project the overlay slot — the layer, its mask, its enveloped weight and the explicit time its
	// evaluator is pinned to. Beside `ProjectUpperBodyLayer` and ahead of the hold branch for the
	// same reason: a base pose that is being HELD is a locomotion answer, and a producer's layer is a
	// separate request that must not freeze with it.
	//
	// **It is NOT gated on `Selection.bBasePoseOwned`, and that asymmetry is deliberate.** The
	// autolayer overlay above IS gated, because an autolayer belongs to the host sequence that owns
	// the base pose and installing one from a publish that yielded would drive bones off a clip
	// nothing is playing. A slot layer belongs to nothing of the kind: retail accumulates it over
	// whatever owns the base at the time, it survives the base changing hands underneath it, and it
	// dies with its own clip. Gating it would silence every shot fired while a scene, a reaction or
	// an ambient stance held the base.
	void ProjectSlotLayer();
	// One slot's projection. Split out because every rule in it is per-layer — the mask admission
	// test, the playhead re-seat, the trio's shared fate — and a loop body that long inside the caller
	// hides that they are.
	void ProjectSlot(int32 SlotIndex);

	// Hand one SLOT's layered blend its bone mask, through `ElysiumAnimGraph::SlotLayerTag(N)`. The same
	// door, the same assertions and the same refusal as `ApplyUpperBodyMask`, on the second blend
	// node — and separate rather than parameterized because the two nodes carry two independent
	// masks and a shared applier would have to be told which, which is the tag it already is.
	bool ApplySlotMask(int32 SlotIndex);
	// The third masked blend's applier — the aim grid the slot clip declares rides its own node with
	// the GRID's mask, not the slot clip's, because the two gate different bone sets by design.
	bool ApplySlotAimMask(int32 SlotIndex);
	// The slot's own bank-remap node, through `ElysiumAnimGraph::BankRemapTag(SlotIndex)`. The
	// retarget source is read off THIS SLOT's own `Staging.Sequence` — never the base channel's: a
	// slot's clip routinely comes from a different bank than whatever is posing the legs underneath
	// it, and each closure's correction is independent by construction.
	void ApplySlotBankRemap(int32 SlotIndex);

	// Say once per instance that the compiled graph cannot carry a mask this record names, and answer
	// true so the callers above can spell a refusal as one expression. A node fault is a property of
	// the generated package rather than of the frame, so it never changes and never repeats.
	bool ReportNodeFaultOnce(bool& Latch, const TCHAR* Layer, const TCHAR* Tag, const TCHAR* Fault);

	// True the first time this instance refuses one arm for one reason. `PlaySlotLayer` is reached
	// once per trigger pull, so an unguarded refusal there restates a bake fault at the weapon's own
	// fire rate; the key carries the reason so three different gates answering for one clip are three
	// lines rather than one.
	bool ShouldReportSlotArmRefusalOnce(const TCHAR* Reason, const FElysiumClipIdentity& Identity,
		const FElysiumAnimationRequest& Claim, const UAnimSequence* Sequence);

	// The base channel's phase clock.

	// Stand a new play on one producer's arm: identity, length and loop bit in, a fresh `PlayId`,
	// and an anchor of zero because a play seam STARTED this clip. It writes that producer's slot
	// and nothing else — no other producer's clock is touched — then republishes whichever arm is
	// live, so the phase names the new clip at the instant the seam accepted it.
	//
	// **`LengthSeconds` is the SEQUENCE's own length**, never a montage's — a looping dynamic
	// montage is `LoopingHoldCount` segments long, and a cycle divided by that never leaves zero.
	//
	// An identity naming no clip disarms that producer instead, which is the ordinary stand for a
	// preview or lab body.
	void ArmBasePhase(EElysiumBasePhaseSource Source, const FElysiumClipIdentity& Identity,
		float LengthSeconds, bool bLoop, float PlayRate = 1.0f);
	// Disarm one producer. The others are untouched, so a stop hands the channel back to whatever is
	// still playing underneath rather than emptying it.
	void DisarmBasePhase(EElysiumBasePhaseSource Source);

	// Which producer is posing the body right now, in the order the pose itself composes. It takes
	// the proxy and the stack because the four answers live on them, and one fetch per update is one
	// parallel-evaluation barrier instead of several.
	EElysiumBasePhaseSource LiveBaseSource(FElysiumBipedAnimProxy& InProxy,
		const FAnimNode_BlendStack* Stack) const;
	// The live producer's cycle, off whatever clock it owns.
	float LiveBaseCycle(EElysiumBasePhaseSource Source, FElysiumBipedAnimProxy& InProxy,
		const FAnimNode_BlendStack* Stack) const;
	// Copy the live producer's armed record into `BasePhase`, cycle included, and remember where the
	// dispatcher was left. `bReadClocks` false publishes each arm's own anchor instead of asking the
	// engine — which is what a play seam needs, because the node it just armed has not run yet.
	//
	// The one-argument form is what every play seam calls: it takes the proxy barrier and resolves
	// the stack inside it, then hands both to the threaded form.
	void PublishBasePhase(bool bReadClocks);
	void PublishBasePhase(FElysiumBipedAnimProxy& InProxy, const FAnimNode_BlendStack* Stack,
		bool bReadClocks);
	// Re-arm the locomotion arm off the applied record, then publish. Once per update.
	void RefreshBasePhase(FElysiumBipedAnimProxy& InProxy, const FAnimNode_BlendStack* Stack);
	// The locomotion arm alone: it is a per-frame projection rather than a discrete play, so nothing
	// calls a seam for it and it maintains its own record here — live or not.
	void RefreshLocomotionArm(const FAnimNode_BlendStack* Stack);

	// The overlay slot's own phase clock.
	//
	// **A second published record, not a fifth arm on the first.** The base arms are four producers
	// competing for ONE timeline, so precedence picks the one that publishes; the slot is not
	// competing at all — it composes over whichever of them won and is standing on its own clip at
	// the same time. Folding it into `PublishBasePhase` would make one of the two invisible on every
	// frame a shot is fired over a gait, which is every frame a shot is fired.
	//
	// Armed by `PlaySlotLayer` at the instant the producer's claim is granted, because the ranged
	// transactions call `ResolveAndPlay` and `CommitArrivesFromAnimEvent` in the same statement pair
	// (`Substrate/ElysiumWeaponClasses.cpp`) — a phase that only appeared on the next update would
	// answer for the play before this one and the shot's commit would silently take the estimate.
	void ArmSlotPhase(int32 SlotIndex, const FElysiumClipIdentity& Identity, float LengthSeconds,
		bool bLoop, float PlayRate, float Cycle);
	// Move the armed layer to the cycle the driver's record just published, and drop it when the
	// record has stopped naming a layer at all. Once per publish.
	void RefreshSlotPhase(int32 SlotIndex, const FElysiumOverlaySlotRecord& Row);
	// Copy the arm into `SlotPhase` at the given cycle, or empty the record when nothing is armed —
	// the same contract `PublishBasePhase` honours for a channel standing on nothing.
	void PublishSlotPhase(int32 SlotIndex, float Cycle);
	FElysiumArmedClip& Armed(EElysiumBasePhaseSource Source)
	{
		return ArmedClips[static_cast<uint8>(Source)];
	}
	const FElysiumArmedClip& Armed(EElysiumBasePhaseSource Source) const
	{
		return ArmedClips[static_cast<uint8>(Source)];
	}

	UPROPERTY(Transient) FElysiumBipedAnimProxy Proxy;

	// The record whole rather than scattered scalars: the transition duration needs the OUTGOING
	// selection's authored fade, so the previous record has to survive the next push.
	FElysiumAnimationSelection Pending;
	FElysiumAnimationSelection Applied;
	UPROPERTY(Transient) TObjectPtr<UBlendSpace> PendingBlendSpace = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> PendingSequence = nullptr;
	// The upper-body layer half of the same publish, staged the same way.
	UPROPERTY(Transient) TObjectPtr<UBlendSpace> PendingUpperBodySpace = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> PendingUpperBodySequence = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAnimSequence> PendingAdditiveSequence = nullptr;
	FName PendingUpperBodyMaskName;
	// What the node was last actually given, so the mask is written on change rather than per frame.
	// `NAME_None` before the first write, which is also the name a body with no layer resolves to —
	// the two agree by construction, so a body that never had a layer never touches the node.
	FName AppliedUpperBodyMaskName;
	// And the mask name a REFUSAL was last reported for, which is a different question from what the
	// node holds. A named mask the playing skeleton does not carry never reaches `SetBlendMask`, so
	// `AppliedUpperBodyMaskName` cannot latch it — and the requested name does not change frame to
	// frame, so the warning would repeat for as long as the layer stood. Kept apart rather than
	// latched onto the applied name so the resolve is still RETRIED: a proxy whose skeleton is not
	// bound yet on an early frame refuses once and applies the moment it is.
	FName ReportedUpperBodyMaskName;
	float PendingUpperBodyLayerWeight = 0.0f;
	float PendingAdditiveLayerWeight = 0.0f;
	bool bHasApplied = false;

	// The source skeleton and retarget-source name the BASE channel's remap node was last given a
	// table for — read off
	// whatever `PendingSequence`/`PendingBlendSpace` names, never installed once: a different bank
	// posing the base channel is a different (mesh, source skeleton, source name) tuple, resolved
	// fresh through
	// `UElysiumAnimSubsystem::GetBankRemap` each time it changes. `NAME_None` before the first write
	// and on a body with no retarget source resolved yet, which agree by construction the same way
	// the mask latch above does.
	TWeakObjectPtr<USkeleton> AppliedBaseBankRemapSkeleton;
	FName AppliedBaseBankRemapSource;
	bool bReportedBaseBankRemapNodeFault = false;

	// The overlay stack's staging, one row per slot — written by `PublishSelection` off the driver's
	// record and by `PlaySlotLayer` off a producer's own layer, which are the same numbers taken from
	// the same pure helper rather than two envelopes.
	FElysiumOverlaySlotStaging SlotStaging[ElysiumOverlay::NumSlots];
	// The same maskless fault on the upper-body autolayer, which the resolver can publish for a layer
	// sequence whose clip carries no `UElysiumAnimLayerMask`.
	bool bReportedMasklessUpperBody = false;
	// One latch per masked node for a fault in the compiled graph itself — no node under the tag, or
	// a node whose shape `SetBlendMask` cannot take. A property of the generated package, so it is
	// stated once and never re-evaluated. The slots carry their own, one pair per row.
	bool bReportedUpperBodyNodeFault = false;
	// The arm seam's own refusals, keyed by reason and clip. Separate from the projection latch above
	// because they answer a different question — that one is about the record the driver published,
	// these are about a claim a producer just took — and because the arm reaches one key per weapon
	// clip while the projection reaches one per instance.
	TSet<FString> ReportedSlotArmRefusals;

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

	// The reaction's phase clock.
	//
	// **A phase clock, not a weight.** The engine owns every frame of the fade: `FAnimNode_BlendListBase`
	// takes the newly-active child's own blend time and drives the weight itself, and nothing here ever
	// writes one. What this counts down is when to ask for the fade BACK, which is the one thing the
	// node cannot know — a reaction ends because its clip ended, and the node has no clip.
	//
	// It is seeded from `FElysiumReactionPlay::ActiveSeconds`, so `bReactionActive` drops exactly one
	// out-fade before the branch's own end and the fade completes ON that end — the same instant the
	// Reaction claim's `HoldSeconds` (`TotalSeconds`, the one other reading of that expression)
	// expires and the locomotion publish takes the base back. The zero-blend stop that publish
	// performs therefore finds a branch that has already faded, which is why the handover is
	// structurally invisible rather than a tuned threshold.
	//
	// **A HELD play has no countdown at all.** `FElysiumReactionPlay::TotalSeconds` answers negative
	// for one, and the flag below is what makes the tick skip the subtraction rather than a sentinel
	// smuggled through the same float: a clock that expired would drop the pose while the predicate
	// that asked for it still stands, which is the exact defect the held claim exists to close.
	float ReactionSecondsLeft = 0.0f;
	// How long the branch has been standing, counted UP. The countdown above is seeded from
	// `ActiveSeconds`, which is a different span for each release condition, so it cannot answer where
	// in the CLIP the branch is; the phase needs elapsed against the clip's own length.
	float ReactionElapsedSeconds = 0.0f;
	// Whether the standing play is released by a predicate rather than by the clock. Set by
	// `PlayReaction`, cleared by `StopReaction` and by nothing else.
	bool bReactionHeld = false;
	// Resolved once per class, like the layer's node: the tag table is compiled state and cannot
	// change under a live instance. `false` in the pair means "not looked up yet", never "absent".
	mutable bool bReactionBranchResolved = false;
	mutable bool bHasReactionBranch = false;

	// The stack's own pair, exactly the reaction branch's idiom above. An instance's class is fixed
	// at construction and the tag table is compiled state on it, so the answer is invariant for the
	// instance's whole life; the first flag distinguishes "not looked up yet" from "absent", which
	// `false` in the second cannot. A compiled class that does not carry the stack is a defect in the
	// generated asset, and it is reported ONCE — a defect restated every frame by every body drowns
	// the log it is meant to reach.
	bool bLocomotionStackResolved = false;
	bool bHasLocomotionStack = false;
	FElysiumOneShotReport OneShot;
	FElysiumBlendReport Blend;

	// What the base channel publishes.
	//
	// One PUBLISHED record, because retail has one server timeline per body: `DispatchAnimEvents`
	// stores the last checked cycle on the animating object itself at `+0x658`, and nothing advances
	// a LAYER's cycle server-side (`docs/vtmb/animation_and_movers.md` → "Sequence events and native
	// dispatch"). The DefaultSlot montage realizes what retail selects as the base sequence, so it
	// publishes AS the base rather than as a second channel.
	//
	// Four ARMED records behind it, one per producer, because the four clocks run concurrently even
	// though only one of them is the timeline — see `FElysiumArmedClip`.
	FElysiumClipPhase BasePhase;
	FElysiumArmedClip ArmedClips[static_cast<uint8>(EElysiumBasePhaseSource::Count)];
	// The restart discriminator, drawn once per genuine (re)start across every arm. It is what makes
	// a repeated attack fire its timeline again: the owner, the label and even the phase are
	// identical between two plays of one clip, and only this tells the cursor that the second is a
	// new play rather than a lap.
	uint32 NextPlayId = 0;
	// Which producer `BasePhase` was last read off. A readout, not a gate — precedence decides who
	// publishes, and this records who did.
	EElysiumBasePhaseSource PhaseSource = EElysiumBasePhaseSource::None;
	// Each overlay slot's published phase record and the arm behind it live on `SlotStaging` above —
	// one per row, because four layers can stand at once and each rides its own clip.
	// Whether the APPLIED record actually put an asset on the pin. `bHasApplied` cannot answer it: a
	// record that resolved nothing is still a record applied, so the generation after a resolved-
	// nothing first publish would present a non-null outgoing descriptor while the stack beneath is
	// still holding nothing — and fade the first real clip up out of a T-pose.
	bool bAppliedPosedAnAsset = false;
};
