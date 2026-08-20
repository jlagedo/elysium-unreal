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

// What the graph can say back about the last REAL transition it asked for — one record per blend
// requested, never per frame.
//
// A blend is decided in exactly one place (a new generation on the applied record), and the answer
// is otherwise unobservable: `RequestSlotGroupInertialization` returns nothing, and the pose it
// produces is a fade a reader cannot distinguish from a correct hard cut after the fact. So the
// decision is recorded where it is made rather than sampled afterward.
//
// Generation-stamped for the same reason `FElysiumOneShotReport` is: the record describes the
// transition INTO that generation, so a reader can tell a stale readout from a live one.
struct FElysiumBlendReport
{
	uint32 Generation = 0;
	// What was actually handed to the inertializer, after the combine and both refusals.
	float RequestedSeconds = 0.0f;
	// Zero seconds for a stated reason. `flags & 0x2` on the incoming clip is an authored hard cut.
	bool bSnap = false;
	// The other zero: there was no outgoing CLIP to fade FROM — either nothing had ever been
	// published, or what was published resolved no asset and left the machine posing the bind pose.
	// The two are one fact for this purpose, and both refuse the fade. It is carried rather than
	// inferred from an empty `FromAnimation`, which a record that resolved no clip also leaves empty.
	bool bFirstPublish = false;
	// Whether the machine was leaving one state or re-entering the one it was already in — the same
	// `bStateChanged` the graph reads, captured at the moment the blend was asked for.
	bool bStateTransition = false;
	FString FromAnimation;
	FString ToAnimation;
	// World seconds when the transition was requested. Negative is "no transition yet", the same
	// "never" sentinel every aged readout in this repo uses; printing it as an age would read as a bug.
	double StampSeconds = -1.0;
};

// One reaction, whole, as the graph's reaction branch needs it (LIFE5).
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

	bool IsValid() const
	{
		return (Space != nullptr) != (Sequence != nullptr) && LengthSeconds > 0.0f;
	}

	// How long `bReactionActive` stands — the phase clock's own seed. It is the clip's length less
	// the out-fade, floored at a MINIMUM HOLD equal to the blend-in, so the pose is on screen for at
	// least the time it takes to fade to it. Identity for any clip longer than
	// `BlendInSeconds + BlendOutSeconds`. The floor is the blend-in the producer STATED, not the one
	// the instance ends up using: a body with nothing to blend from snaps in and holds it anyway.
	//
	// **PENDING RE.** The floor is here because every hit cell VtMB ships bakes to 0.033s — two
	// frames — which is shorter than the out-fade alone, so the unfloored seed is zero and the branch
	// is dropped on the update that armed it. The recovered 0.1/0.3 may be `CAnimationLayer` cycle
	// FRACTIONS rather than seconds, which would make a two-frame cell's real fade two frames long
	// and this floor unnecessary; the decomp read of `DamageFlinch`'s layer setup closes it. Until
	// then this is the measured-safe interim.
	float ActiveSeconds() const
	{
		return FMath::Max(LengthSeconds - FMath::Max(BlendOutSeconds, 0.0f),
			FMath::Max(BlendInSeconds, 0.0f));
	}

	// The branch's whole life: the hold above, then the out-fade that follows it. The channel claim
	// takes this rather than the clip's length, so a claim cannot expire while the branch is still
	// fading — one expression, so the two cannot disagree.
	float TotalSeconds() const { return ActiveSeconds() + FMath::Max(BlendOutSeconds, 0.0f); }
};

// Which producer a base-channel phase is being read off (LIFE5), in the order the pose composes.
//
// The four run CONCURRENTLY, which is the whole reason this is an enumeration of arms rather than a
// mode: a reaction replaces the locomotion pose without stopping the montage under it, and that
// montage rides over a state machine which never stopped either. Only one of them can be the body's
// server timeline at a time — retail has exactly one — but all four clocks keep running, so which
// one publishes changes while none of them ends.
enum class EElysiumBasePhaseSource : uint8
{
	None = 0,
	Clip,       // the proxy's cinematic clip player, which replaces the graph outright
	Reaction,   // the graph's reaction branch, which replaces the locomotion pose
	Montage,    // the DefaultSlot dynamic montage, which rides over the state machine
	Machine,    // the locomotion state machine underneath everything
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
	bool bLooping = false;
	// Bumped once per genuine (re)start. **A displaced arm keeps its id**: the same play resumed is
	// not a new play, and a bump would restart its timeline and re-fire every record behind it.
	uint32 PlayId = 0;
	// Where the dispatcher last saw this arm — `FElysiumClipPhase::AnchorCycle`, published with the
	// phase and frozen the moment a higher arm takes the channel. It is zero for a clip a play seam
	// started, and the observed fraction for the state machine, which starts nothing.
	float AnchorCycle = 0.0f;

	bool IsArmed() const { return Identity.IsValid(); }
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

	// --- the reaction branch (LIFE5) ----------------------------------------------------------------
	//
	// The publish surface for the branch the graph carries between the inertializer and the
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

	// The last real transition this instance asked the inertializer for. Not a per-frame sample: it
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

	// --- the shared one-shot seam -----------------------------------------------------------------
	//
	// Answered over a dynamic slot montage, which is the design's own shape for a one-shot and needs
	// no baked montage asset. Nothing here reaches the locomotion state machine: a scripted clip
	// plays OVER the gait rather than replacing the thing that owns it.
	virtual bool PlayOneShot(const FElysiumClipIdentity& Identity, UAnimSequence* Sequence,
		bool bLoop, float BlendInSeconds, float BlendOutSeconds) override;
	virtual void StopOneShot(float BlendSeconds) override;

	// --- the phase seam (LIFE5) ---------------------------------------------------------------
	//
	// Where the base channel stands on its clip. A pure member read: the snapshot is armed at play
	// time and its cycle refreshed once per update, so every reader in a frame — the event pass, the
	// weapon's `HasLiveAnimEventDispatch`, a debug surface — is handed the same record.
	virtual bool GetClipPhase(EElysiumAnimChannel Channel, FElysiumClipPhase& Out) const override;

	// --- the reaction seam (LIFE5) ----------------------------------------------------------------
	//
	// Hand the graph's reaction branch a fan or a clip and switch it on. The branch REPLACES the base
	// pose rather than riding over it, so this is not a second one-shot slot: nothing the state
	// machine or the montage slot is doing is stopped, and the moment the branch fades back out the
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

	// --- the cinematic clip path ------------------------------------------------------------------
	//
	// Stand one clip as the whole body pose, over the proxy's own player rather than over the graph.
	// A choreographed scene owns the body outright for its duration and pins the clip to scene time,
	// which is the one thing the slot-montage seam above cannot do.
	//
	// `Identity` is the clip's vocabulary key, on the same contract `PlayOneShot` takes it: empty is
	// legal and publishes no phase, which is what every preview and lab stand hands over.
	void PlayClip(const FElysiumClipIdentity& Identity, UAnimSequence* Sequence, bool bLoop = true);
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

	// --- the base channel's phase clock (LIFE5) ---------------------------------------------------

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
		float LengthSeconds, bool bLoop);
	// Disarm one producer. The others are untouched, so a stop hands the channel back to whatever is
	// still playing underneath rather than emptying it.
	void DisarmBasePhase(EElysiumBasePhaseSource Source);

	// Which producer is posing the body right now, in the order the pose itself composes. It takes
	// the proxy because two of the four answers live on it, and one fetch per update is one
	// parallel-evaluation barrier instead of several.
	EElysiumBasePhaseSource LiveBaseSource(FElysiumBipedAnimProxy& InProxy);
	// The live producer's cycle, off whatever clock it owns.
	float LiveBaseCycle(EElysiumBasePhaseSource Source, FElysiumBipedAnimProxy& InProxy) const;
	// Copy the live producer's armed record into `BasePhase`, cycle included, and remember where the
	// dispatcher was left. `bReadClocks` false publishes each arm's own anchor instead of asking the
	// engine — which is what a play seam needs, because the node it just armed has not run yet.
	void PublishBasePhase(bool bReadClocks);
	// Re-arm the locomotion arm off the applied record, then publish. Once per update.
	void RefreshBasePhase();
	// The locomotion arm alone: it is a per-frame projection rather than a discrete play, so nothing
	// calls a seam for it and it maintains its own record here — live or not.
	void RefreshMachineArm();
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

	// --- the reaction's phase clock (LIFE5) --------------------------------------------------------
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
	float ReactionSecondsLeft = 0.0f;
	// How long the branch has been standing, counted UP. The countdown above is seeded from
	// `ActiveSeconds` — the clip's length less its out-fade, floored at the blend-in — so it cannot
	// answer where in the CLIP the branch is; the phase needs elapsed against the clip's own length.
	float ReactionElapsedSeconds = 0.0f;
	// Resolved once per class, like the layer's node: the tag table is compiled state and cannot
	// change under a live instance. `false` in the pair means "not looked up yet", never "absent".
	mutable bool bReactionBranchResolved = false;
	mutable bool bHasReactionBranch = false;

	int32 MachineIndex = INDEX_NONE;
	int32 StateIndex[ElysiumAnimGraph::NumGraphStates];
	bool bStateHasBlendSpacePlayer[ElysiumAnimGraph::NumGraphStates] = {};
	bool bRecordedAnyBlendSpacePlayer = false;
	// A compiled class that does not carry the machine is reported once per instance. The lookup
	// itself still retries — a class can answer later — but the report is a defect in the generated
	// asset, and a defect restated every frame by every body drowns the log it is meant to reach.
	bool bReportedMissingMachine = false;
	FElysiumOneShotReport OneShot;
	FElysiumBlendReport Blend;

	// --- what the base channel publishes (LIFE5) --------------------------------------------------
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
	// Whether the APPLIED record actually put an asset on a pin. `bHasApplied` cannot answer it: a
	// record that resolved nothing is still a record applied, so the generation after a resolved-
	// nothing first publish would present a non-null outgoing descriptor while the machine beneath
	// is still posing the bind pose — and inertialize the first real clip up out of a T-pose.
	bool bAppliedPosedAnAsset = false;
};
