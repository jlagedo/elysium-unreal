#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntityHandle.h"
#include "ElysiumGaitSpeeds.h"
#include "ElysiumGraphState.h"
#include "ElysiumLocomotionSample.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumNpcMindTypes.h"

// What a body asks the animation layer for, and what it is told back (CCC4).
//
// `FElysiumAnimationIntent` in, `FElysiumAnimationSelection` out, over steps 2, 4, 5 and 6 of
// `docs/architecture/animation-architecture.md` section 3.3. This header owns the pure half: the
// records (the intent, the channel request and the selection), step 1's priority table, the
// locomotion classifier and the jump latch. It reads `FElysiumLocomotionSample` and
// nothing else, so it is asserted with no world, no catalog and no UObject —
// `Elysium.Substrate.AnimationIntent`, the same pure-rules/engine-half split as `ElysiumMoveSolve.h`
// and `ElysiumCameraSolve.h`.
//
// The resolution that consumes an intent lives in `Visual/ElysiumAnimationResolve.h`, which needs the
// character catalog; the two are apart because the catalog types are private and this record crosses
// the substrate boundary. **Step 3, the activity translation, lives there too** — the committed
// weapon ladders are availability-probed per rung against the body's own clip vocabulary, so a
// translation that cannot see the vocabulary is not the recovered translation.
//
// The record is not decoration. Six things can produce a wrong pose — input, classification,
// translation, model data, asset shape and blending — and without one line naming each, a wrong pose
// is a guess.

// Who asked. The producer, not the body: an NPC's damage reaction and its patrol both address the
// same body through different sources. It carries arbitration and diagnostics — the priority band a
// request defaults to, the holder a held pose names — and decides no translation.
enum class EElysiumAnimSource : uint8
{
	Player,
	Npc,
	Scene,
	Damage,
	Interaction,
	Debug,
};

// Which chain the body itself translates through. **The body, not the producer**: `CBasePlayer` and
// `CAI_BaseNPC` are two different translators, not two configurations of one, and every source
// addresses either kind — a `Damage` reaction on a human combatant walks the cast's `+0x5dc`
// pre-translation, its class/weapon alternation and its four-way availability probe, while a
// `Scene` beat on the player walks the same one-pass chain the player's own locomotion does.
enum class EElysiumAnimBodyKind : uint8
{
	Player,
	Cast,
};

// Which slot the request owns. The driver holds one request slot per channel
// (`FElysiumAnimationRequest`); the base channel's slot is arbitrated against the locomotion
// publish by the priority table below, and the other channels' slots stand ready for the layer
// families to read.
enum class EElysiumAnimChannel : uint8
{
	Base,
	FullBody,
	UpperBody,
	Additive,
	Gesture,
};

namespace ElysiumAnimIntent
{
	// How many channels the slot array carries — one slot per `EElysiumAnimChannel` value.
	inline constexpr int32 NumChannels = 5;
}

// Step 1's priority table, whole and in one place: the ORDER of these values IS the table, and a
// claim owns the base pose exactly while nothing above it asks. The two locomotion rows are the
// implicit claim every body with a mover publishes each anim tick and are never submitted as
// requests; everything else is a band a producer writes on its request.
//
// The one recovered relationship inside it is Ambient vs the two locomotion rows: an ambient
// stance, fidget or dialogue clip holds against a standing body's every-tick publish and yields
// the moment the body travels — which is retail's own observable behaviour for the ambient cast.
// The rest of the order is ours until capture-verified (`docs/architecture/animation-architecture.md`
// section 3.3 step 1): a scripted beat outranks travel, a damage reaction outranks a scripted
// beat, a choreographed scene owns the body outright, and the owner's hand outranks everything.
enum class EElysiumAnimPriority : uint8
{
	// The floor: the locomotion publish of a body that is standing still. Implicit, never submitted.
	LocomotionIdle = 0,
	// Ambient stances, fidgets, dialogue line clips — everything the schedule arms on a body that is
	// otherwise idle.
	Ambient,
	// The locomotion publish of a travelling body. Implicit, never submitted.
	LocomotionTravel,
	// A scripted beat: scripted_sequence phases, SetAnimation, an interaction's custom move.
	Scripted,
	// The damage and combat action families (LIFE5).
	Reaction,
	// A choreographed scene, which owns the body outright for its duration.
	Scene,
	// The owner's hand — a green-room or debug stand judged over a live body.
	Debug,
};

// One producer's claim on a channel, written into the driver's request slot. The request is the
// discrete half only — who, which channel, at what rank, for how long; the continuous state and
// the activity resolution stay on the intent, and the slot decides nothing about WHAT plays, only
// WHO owns the channel while it does.
struct FElysiumAnimationRequest
{
	EElysiumAnimSource Source = EElysiumAnimSource::Npc;
	EElysiumAnimChannel Channel = EElysiumAnimChannel::Base;
	EElysiumAnimPriority Priority = EElysiumAnimPriority::Ambient;
	// What the claim stands for — the clip or activity the producer armed. Diagnostics only: the
	// record's `BaseHold` line is built from it, so a held pose names its holder.
	FString Label;
	// How long the claim stands, seconds. <= 0 holds until released, replaced or outranked — a
	// looping clip and a scene-pinned one have no natural end; a one-shot passes its clip length so
	// an armer that never comes back cannot park the channel.
	float HoldSeconds = 0.0f;
};

// How the request reached the layer. **These are the producer routes the 22-map corpus actually
// contains** (`docs/vtmb/animation_and_movers.md`), and collapsing any of them into "activity" would
// erase behaviour already confirmed: 198 map-authored exact labels and 18 `SetAnimation` wires do not
// go through weighted activity selection at all.
enum class EElysiumAnimRoute : uint8
{
	Activity,       // ordinary weighted activity resolution
	ExactLabel,     // scripted m_iszIdle/m_iszPlay/m_iszPostIdle/m_iszCustomMove, prop LoopSequence
	SetAnimation,   // the I/O SetAnimation input, addressed on a prop model
	Gesture,        // SetGesture, whose miss path animates nothing at all
};

enum class EElysiumAnimAssetKind : uint8
{
	None,
	Sequence,
	BlendSpace,
	Layer,
};

// The verdict. `Resolved` is the only clean answer; every other value names the rung of VtMB's own
// fallback ladder that was taken, so a wrong pose is traceable to a rule rather than guessed at.
enum class EElysiumAnimOutcome : uint8
{
	Resolved = 0,
	// The cast's four-way availability probe answered below its first rung: what the translation
	// named has no sequence, so the class answer, the first weapon answer or the original request
	// stands instead. The answering row's authored `required` bit does not change this — the pinned
	// server translator never reads it.
	TranslatedFallback,
	// A missing translated ACT_RUN retried weighted ACT_WALK.
	RunToWalk,
	// The whole request retried as ACT_DISPOSITION.
	Disposition,
	// The hard fallback: sequence index 0.
	SequenceZero,
	// The scripted-label miss: LookupSequence returned -1, so the helper set sequence 0, zeroed the
	// cycle and reset sequence info. An ACT_* token in this route is NOT sent through the activity
	// resolver, which is why it is a separate value from SequenceZero.
	ScriptedSequenceZero,
	// SetGesture simply returned. Both gesture calls the corpus ships do this.
	GestureNoOp,
	// A named miss with no fallback at all — ACT_LAND_CROUCH on a validated player body. Reported so
	// the graph can name a fallback rather than being handed an invented clip.
	MissingSequence,
	// A masked or additive sequence was asked for on the base channel, which is never legal.
	MaskedRejected,
	// The stem resolves no clip vocabulary at all — the gym, a menu backdrop, an unexported model.
	NoVocabulary,
	// The label resolved and the asset did not load, which is the ordinary answer before a body has a
	// skeletal mesh to bind against.
	NoAsset,
	// A blend-space selection whose target graph state has no blend-space player. Named rather than
	// projected: a sequence-only state with a null sequence pin evaluates to the reference pose, and
	// the blend-space pointer would defeat the hold-pose guard.
	GridStateRefused,
};

// The locomotion slice's activities as a small ordered code, so a per-frame channel can carry the
// classifier's answer as a number a baseline diff can read.
//
// **It is neither of VtMB's two enums and must never be merged with either.** The activity registry
// is 4,460 entries whose numeric IDs are pinned-build diagnostics (the ACT_* literal is the stable
// key), and the ordinary selector's compact codes (0, 1, 2, 7, 8, 9, 10, 11, 13) are dispatcher keys
// from which one code selects several activities. This is a projection of what this slice can emit.
// Anything outside the slice reads `Unknown`, which is a true statement rather than a hole.
enum class EElysiumAnimActivityCode : uint8
{
	Unknown = 0,
	Idle,
	WalkRelaxed,
	Walk,
	RunRelaxed,
	Run,
	Sneak,
	Crouch,
	Leap,
	Falling,
	Land,
	LandCrouch,
	Swim,
	Treadwater,
};

// The latched air state. Distinct from `EElysiumJumpPhase`, which the sample derives from velocity
// alone and therefore cannot tell a jump from walking off a ledge — that is exactly what this latch
// exists to answer.
enum class EElysiumAirPhase : uint8
{
	Grounded,
	Leap,
	Falling,
	Landing,
};

struct FElysiumAnimationIntent
{
	// --- Who ---------------------------------------------------------------------------------
	FElysiumEntityHandle Character;
	// The model. Empty is a real state rather than an error: the gym stands a body with no entity
	// world and no exported model behind it.
	FString Stem;
	EElysiumAnimSource Source = EElysiumAnimSource::Player;
	// Which translator the request walks, stated by the producer from the body it drives rather than
	// read off `Source`. It is the fork the recovered cast chain and its fallback ladder are
	// discriminated by, so a producer that leaves it at the default hands a cast body the player's
	// one-pass chain.
	EElysiumAnimBodyKind BodyKind = EElysiumAnimBodyKind::Player;
	EElysiumAnimChannel Channel = EElysiumAnimChannel::Base;
	// Advances only when the DISCRETE request changes. Two things need it: a completed one-shot must
	// not cancel its replacement, and the driver must be able to tell "the same request, still
	// running" from "the same activity, asked for again".
	uint32 Generation = 0;

	// --- What: exactly one of these two, never both ---------------------------------------------
	// A stable ACT_* name. An explicit label is the escape hatch for content that actually names one
	// — a scripted sequence, a SetAnimation wire, a choreographed event. A gameplay system naming
	// `walk_0` here is a layer violation: it has skipped weighted choice, include ownership and the
	// blend grid.
	FString Activity;
	FString SequenceLabel;
	EElysiumAnimRoute Route = EElysiumAnimRoute::Activity;

	// The repeatable selection token weighted choice keys on. Same token, same pick, forever.
	int32 Variant = 0;

	// --- Continuous state -----------------------------------------------------------------------
	// Carried whole rather than as a handful of copied scalars. The record's job is to make a wrong
	// pose traceable, and "speed 3.2, wish scale 0, stance Rising -> ACT_CROUCH" is a diagnosis where
	// "ACT_CROUCH" alone is a guess.
	FElysiumLocomotionSample Body;
	// Zero until the weapon rung: the sample carries no aim, because nothing aims yet.
	float AimYaw = 0.0f;
	float AimPitch = 0.0f;
	// Where the hit came from, in the `hit_yaw` pose parameter's own degrees — (-180,180],
	// right-positive with zero forward, the same convention move_yaw uses. Zero on every request that
	// is not a directional reaction (the parameter's resting value).
	float HitYaw = 0.0f;
	// The latch's answer, which is the only thing that can distinguish retail's jump phases.
	EElysiumAirPhase AirPhase = EElysiumAirPhase::Grounded;

	// --- Translation context ---------------------------------------------------------------------
	// The active weapon's ENTITY CLASSNAME (`item_w_glock_17c`), which is the key authored content
	// spells and the key the committed ladders are joined to. Empty is a body with empty hands, and
	// its translation is the empty table retail's own unarmed body walks.
	FString WeaponClassname;
	// The actor's entity classname (`npc_gangbanger_a`), which selects its recovered translation
	// bodies. On the player it is the registered class's own name (`player`), which reaches no
	// recovered class body — the player's actor translation is the two committed `CBasePlayer` rows.
	FString ActorClassname;
	// The cast body's own state, which the recovered human pre-translation reads as `m_NPCState` to
	// decide whether the body stands in its alert set or its relaxed one. Meaningless on the player,
	// whose chain has no class body.
	EElysiumNpcState ActorState = EElysiumNpcState::Idle;
	// The form the body is wearing. No recovered translation row reads it; it rides so the seam takes
	// it rather than growing a parameter later.
	FString FormTag;

	// --- Completion --------------------------------------------------------------------------
	bool bLoop = true;
	// Whether a miss may walk `CAI_BaseNPC`'s recovered fallback ladder — the translation's own
	// four-way availability probe and its run-to-walk last resort, then the whole request as a
	// disposition, then sequence zero. It is an NPC rule and the player has none (the controlled
	// corpus records a ducked ACT_LAND_CROUCH request simply returning -1), so the ladder needs both
	// this and a `Cast` body kind. A caller clears it when its own contract predates the ladder and
	// its callers read the miss: a gait resolved through a fallback rung is not that gait, and the
	// weighted pick would hand the body walking speeds while it plays a crouch.
	bool bAllowFallbackLadder = true;
	EElysiumAnimSource CompletionOwner = EElysiumAnimSource::Player;

	// No blend time and no asset reference live here, by design: the authored fade comes back OUT on
	// the selection, read off the model, so nothing upstream can hand-author one.

	// An activity or a label, never both and never neither.
	bool IsWellFormed() const { return Activity.IsEmpty() != SequenceLabel.IsEmpty(); }
};

struct FElysiumAnimationSelection
{
	// --- Who asked ------------------------------------------------------------------------------
	EElysiumAnimSource Source = EElysiumAnimSource::Player;
	// Which chain the walk below actually took. Carried beside the producer because the two answer
	// different questions on the same readout — a `Damage` request that walked the cast chain and
	// one that walked the player's are the same source and different translations.
	EElysiumAnimBodyKind BodyKind = EElysiumAnimBodyKind::Player;
	EElysiumAnimChannel Channel = EElysiumAnimChannel::Base;
	EElysiumAnimRoute Route = EElysiumAnimRoute::Activity;
	uint32 Generation = 0;
	FString Stem;

	// --- Step 1: the base-channel arbitration verdict ---------------------------------------------
	// Whether the publish carrying this record owns the base pose. The driver computes it from the
	// priority table — the locomotion publish's own rank (`LocomotionIdle` standing,
	// `LocomotionTravel` travelling) against the base slot's active claim — and the graph obeys it:
	// a publish ends a foreign one-shot or clip only when this is true. Defaults to true because a
	// record built by hand IS a deliberate stand and takes the pose it publishes.
	bool bBasePoseOwned = true;
	// Who holds the base instead, as one readable line ("scene 'jack_wave' (scene)"). Empty while
	// owned — the record's job is to make a held pose name its holder rather than read as silence.
	FString BaseHold;
	// How long the holding claim has stood, seconds. What separates a legitimate hold from a leaked
	// one on the readouts: a scene mid-performance reads its own running time, a stuck claim only
	// grows. Zero while owned.
	float BaseHoldSeconds = 0.0f;

	// --- Steps 2 and 3: the activity chain, one line per witnessed hop ---------------------------
	// The LOGICAL request, un-translated. Retail's `m_Activity` stays this: translation changes the
	// sequence set that realizes a request, not the AI-visible state.
	FString RequestedActivity;
	// Virtual +0x5dc, the cast's pre-translation. Empty on the player, whose pinned order is +0x5f4
	// then +0x5e0 with nothing before them, and empty for a cast body whose entity classname reaches
	// no recovered class.
	FString PreTranslationActivity;
	// The FIRST weapon answer, kept apart from the last on purpose: retail alternates NPC-class and
	// weapon translation for up to five iterations, and only the first weapon result is retained
	// separately — it is rung 3 of the availability probe that follows.
	FString FirstWeaponActivity;
	int32 TranslationIterations = 0;
	// What the sequence set was actually chosen for.
	FString ResolvedActivity;
	FString WeaponActivity;
	// The latest CHANGED class/NPC answer — availability rung 2. Empty when no class row rewrote
	// anything, and empty on the player, whose chain has no class alternation.
	FString ClassActivity;
	// Which rung of the weapon ladder answered, 1-based over the rungs declaring the base. Zero
	// when no rung was playable and the base passed through untranslated — the same answer
	// retail's own empty table gives.
	int32 WeaponRung = 0;
	// Which rung of the cast's four-way availability probe answered — final weapon answer, class
	// answer, first weapon answer, original request. Zero on the player, who has no probe, and
	// zero on a request nothing could play.
	int32 AvailabilityRung = 0;

	// --- Step 4: the model vocabulary -------------------------------------------------------------
	// The vocabulary key — `walk`, not `walk_0`. This is the key that owns the include-DAG mapping to
	// a bank; the concrete animation is downstream of it and does not identify anything.
	FString SequenceLabel;
	// Exact identity is (owner, raw index) because 1,430 of 1,484 label groups carry more than one
	// owner/sequence identity. The character export writes no raw index yet, so this is set only on
	// the prop route, where the sidecar carries declaration order. The identity this rung can prove
	// is (OwnerStem, SequenceLabel), which is exactly what bank ownership needs.
	int32 RawSequenceIndex = INDEX_NONE;
	// The bank the include DAG named. **The whole point of the record**: the same label `run` reaches
	// a PC-only bank on a player body and the shared cast bank on an NPC, and the two fans disagree by
	// 209 cm/s at 180 degrees.
	FString OwnerStem;
	int32 Variant = 0;
	int32 Weight = 0;
	int32 Candidates = 0;

	// --- Step 5: the asset shape ------------------------------------------------------------------
	EElysiumAnimAssetKind AssetKind = EElysiumAnimAssetKind::None;
	// The concrete animation in the owner's glb — `walk_0`. Not in the character's vocabulary and not
	// to be looked up there. On a grid it is the FLOOR cell of the pair below.
	FString AnimationName;
	// The second half of that pair — the cell one step along axis 0, which `AxisFraction[0]` weighs.
	// Empty when the selection names one animation, and empty at the top of a fan, where the axis
	// clamps and the fraction is zero.
	FString NextAnimationName;
	// The ideal being advanced to. Equal to SequenceLabel until a state machine exists to advance.
	FString TargetSequence;
	// ACT_TRANSITION's intermediate. Always empty: the graph retail would traverse to fill it is
	// unauthored on every shipped model, so the ideal activity is reached directly.
	FString TransitionSequence;
	// The exported base-to-layer binding, in DECLARATION order — the order is data, never sorted.
	TArray<FString> LayerLabels;
	bool bMasked = false;
	bool bAdditive = false;
	bool bLooping = true;
	// Flags & 0x2 — a hard cut, which the transitioner honours by discarding the whole fading set.
	bool bSnap = false;
	// The authored fade, as the model stores it. The `max(outgoing, incoming)` combine is the
	// player's rule and lives with the graph, not here.
	float FadeSeconds = 0.0f;

	// --- Step 6: the published parameters. It does not repeat selection. --------------------------
	// Which of the eight graph states realizes this selection. **Projected once, here, and read
	// everywhere else**: the anim instance, the Cog row, the trace and the MCP surface all have to
	// agree about where the body is standing, and a projection each of them derived for itself is
	// four answers waiting to disagree. The rule that produces it is
	// `ElysiumAnimGraph::StateForActivity` over the LOGICAL request — translation changes which
	// sequences realize a request, never what the body is doing.
	//
	// It is named even on a miss, because a miss still has to stand somewhere: that is what lets a
	// request that resolved nothing be read as "held the pose in state X" rather than as silence.
	EElysiumGraphState GraphState = EElysiumGraphState::Idle;
	// The latch state the classification was made under, carried so a reader can tell an ACT_FALLING
	// that came from a jump from one that came from walking off a ledge — which is the whole thing the
	// latch exists to distinguish, and it would be invisible on the record without it.
	EElysiumAirPhase AirPhase = EElysiumAirPhase::Grounded;
	float MoveYaw = 0.0f;
	float Speed = 0.0f;
	float AimYaw = 0.0f;
	float AimPitch = 0.0f;
	// The selected cell's authored ground speed, cm/s. Zero when the cell carries no movement record.
	float GroundSpeedCmPerSecond = 0.0f;
	FString AxisName[2];
	float AxisValue[2] = { 0.0f, 0.0f };
	// How far past the named cell the parameters sat, per axis, 0..1. Together with the pair below it
	// is what makes the record a statement about a BLEND rather than about a snap: a fan is sampled
	// between two cells, and a record naming only the floor cell describes a pose the graph never
	// strikes. Zero on a selection that is not a grid.
	float AxisFraction[2] = { 0.0f, 0.0f };
	int32 Axes = 0;

	// --- The verdict ------------------------------------------------------------------------------
	EElysiumAnimOutcome Outcome = EElysiumAnimOutcome::NoVocabulary;
	// One line naming what missed, in words. Read straight out of Cog and the MCP surface.
	FString Detail;

	bool IsResolved() const { return Outcome == EElysiumAnimOutcome::Resolved; }

	// Whether the record names a base asset to load — a different question from how it came to name
	// it, and the one a bind has to ask. Every fallback rung applies a real label first and then
	// restates the outcome as the rung that answered (`RunToWalk`, `TranslatedFallback`,
	// `Disposition`), so a bind keyed on `IsResolved` drops the clip the record is naming and the
	// body poses nothing while the record says otherwise. Every path with no clip clears `AssetKind`
	// on its way out, so this reads the asset half directly and leaves the outcome as the *how*.
	bool NamesBaseAsset() const
	{
		return AssetKind != EElysiumAnimAssetKind::None
			&& !SequenceLabel.IsEmpty()
			&& !OwnerStem.IsEmpty();
	}
};

// The speed authority, injected rather than read.
//
// **Nothing in the classifier names an absolute speed.** Every threshold comes from this struct, so
// the whole classifier moves with the authority and no number in it has to be found and edited.
// `ElysiumAnimIntent::GaitFrom` builds one from a body's own fans; the defaults are the same
// constants a body with no fan falls back to, so the classifier and the mover agree about where the
// gait flips either way.
struct FElysiumGaitReference
{
	float WalkSpeedCmPerSecond = ElysiumMove::WalkSpeed;
	float RunSpeedCmPerSecond = ElysiumMove::RunSpeed;

	// Below this the body is standing rather than moving slowly. **Retail's is a flat 5 u/s**, not a
	// fraction of the gait: the same absolute cut decides idle-versus-moving and crouch-versus-sneak
	// however fast the body's authored walk happens to be.
	float StillSpeedCmPerSecond = 5.0f * ElysiumMove::U;

	// Where the gait flips. Retail's is the body's own **forward walk cell plus one unit** — not a
	// point between walk and run — so it sits just above the fastest walk the body can author and a
	// walk fan can never reach it. Zero falls back to `RunSplitFraction`, which is what a body with
	// no resolved fans uses.
	float RunSplitAbsoluteCmPerSecond = 0.0f;
	float RunSplitFraction = 0.50f;

	// How far past the split the speed must travel before the gait flips back, as a fraction of the
	// walk-to-run span. **Retail holds no gait memory at all** — it recomputes every operand each
	// call — so any non-zero value here is a divergence. It defaults to zero because the commanded
	// term below makes the input a step function rather than a ramp, which is what stopped the
	// flicker this existed for.
	float HysteresisFraction = 0.0f;

	float StillSpeed() const { return StillSpeedCmPerSecond; }
	float RunSplitSpeed() const
	{
		if (RunSplitAbsoluteCmPerSecond > 0.0f)
		{
			return RunSplitAbsoluteCmPerSecond;
		}
		return WalkSpeedCmPerSecond
			+ RunSplitFraction * (RunSpeedCmPerSecond - WalkSpeedCmPerSecond);
	}
	float HysteresisSpeed() const
	{
		return HysteresisFraction * FMath::Abs(RunSpeedCmPerSecond - WalkSpeedCmPerSecond);
	}
};

// The latched air state, plus the one bit of gait memory the hysteresis needs.
//
// It exists because `FElysiumLocomotionSample::JumpPhase()` cannot answer step 2: a descent and
// walking off a ledge are identical in the state the sample carries, while retail distinguishes
// phase 1 (ACT_LEAP), phase 7 (ACT_FALLING) and phase 8 (a gait or ACT_LAND).
struct FElysiumJumpLatch
{
	EElysiumAirPhase Phase = EElysiumAirPhase::Grounded;
	float PhaseSeconds = 0.0f;
	bool bWasOnGround = true;
	// The jump push window last frame. Its RISING edge is the press, which is the only thing that
	// distinguishes a jump from a fall.
	bool bWasHolding = false;
	bool bLastGaitWasRun = false;

	// **The fallback, not the rule.** A still, grounded body leaves ACT_LAND when the pose layer says
	// the landing clip finished; this timer answers only for a body that has no pose layer to ask —
	// the gym stands bodies with no visual at all, and `Elysium.Substrate.AnimationIntent` asserts
	// that path. It is deliberately not deleted: a body with no graph still has to stand up.
	float LandHoldSeconds = 0.35f;
};

// A weapon's grip, which selects the upper-body mask a layer composes against (CCC10). It is not
// melee-versus-ranged: every firearm and thrown weapon is two-handed, and so are the melee
// `bushhook` and `sledgehammer` — a resolver keyed on "is this melee" gets those two wrong
// (`docs/vtmb/animation_and_movers.md` A.4).
enum class EElysiumWeaponGrip : uint8
{
	TwoHanded,
	OneHanded,
};

// What the pose layer can say about the one-shot the latch's phase is riding.
//
// Three values, not a bool, because "no answer" and "not finished" are different facts and
// collapsing them picks the wrong one in both directions: a body with no graph would hold ACT_LAND
// forever, and a body whose landing resolved no clip would leave it on the frame it began.
// `Unknown` is what routes the latch back to `LandHoldSeconds`; `Playing` suppresses the timer
// entirely, because a graph that is answering is the authority and a stopwatch racing it would cut
// a long clip short.
enum class EElysiumOneShotState : uint8
{
	Unknown,
	Playing,
	Complete,
};

// What a body has to state to be given its tables (CCC7).
//
// **The key is the same chain the POSE resolves through**, and nothing else: everything that can
// change which sequence `ACT_WALK` resolves to is here, and everything that cannot is absent — the
// current gait, the current `move_yaw` and the body's speed are all missing, which is what lets one
// resolve serve every frame until the body itself changes.
//
// The body kind, the actor classname and the actor state are members for exactly that reason: they
// select the `+0x5dc`/`+0x5e0` class bodies and the alert/relaxed branch, so a set resolved without
// them is a set resolved for a different body than the one being posed.
struct FElysiumGaitSpeedRequest
{
	FString Stem;
	// Which producer asked, for the diagnostics the miss report renders.
	EElysiumAnimSource Source = EElysiumAnimSource::Player;
	// Which pre-translation chain the request walks — the two committed `CBasePlayer` rows for
	// `Player`, the recovered NPC class bodies for `Cast`. The pose walks one of them; the speeds
	// have to walk the same one, which is why this is part of the key.
	EElysiumAnimBodyKind BodyKind = EElysiumAnimBodyKind::Player;
	// The actor's own entity classname, which is what finds its recovered class bodies. On the player
	// it is the registered class's own name (`player`), which reaches no recovered class body — the
	// player's actor translation is the two committed rows.
	FString ActorClassname;
	// The active weapon's entity classname and the body's form, exactly as the intent spells them —
	// they change which sequence `ACT_WALK` resolves to, which is the whole membership rule here.
	FString WeaponClassname;
	FString FormTag;
	// The body's own state, which the recovered human pre-translation reads to choose between the
	// alert and relaxed animation sets — and a relaxed walk and an alert one are different fans.
	EElysiumNpcState ActorState = EElysiumNpcState::Idle;
	int32 Variant = 0;

	// `m_flSpeedScale` — the character's own rate multiplier. **It scales run and sneak and not
	// walk**, which is faithful: retail passes it to two of the three extractor calls. The visible
	// consequence is that a speed buff raises the run while leaving the walk/run threshold where it
	// was, so a buffed body pins to the run.
	float SpeedScale = 1.0f;

	bool operator==(const FElysiumGaitSpeedRequest& Other) const
	{
		return Variant == Other.Variant
			&& Source == Other.Source
			&& BodyKind == Other.BodyKind
			&& ActorState == Other.ActorState
			&& FMath::IsNearlyEqual(SpeedScale, Other.SpeedScale)
			&& Stem.Equals(Other.Stem, ESearchCase::IgnoreCase)
			&& ActorClassname.Equals(Other.ActorClassname, ESearchCase::IgnoreCase)
			&& WeaponClassname.Equals(Other.WeaponClassname, ESearchCase::IgnoreCase)
			&& FormTag.Equals(Other.FormTag, ESearchCase::IgnoreCase);
	}
	bool operator!=(const FElysiumGaitSpeedRequest& Other) const { return !(*this == Other); }

	bool IsValid() const { return !Stem.IsEmpty(); }
};

// One producer's ACT_* request, stated whole (LIFE5).
//
// **The same membership key as `FElysiumGaitSpeedRequest` above**, plus the activity being asked
// for: everything that decides which sequence an `ACT_*` resolves to travels together, so a
// producer selects through the same chain the body's own per-frame publish does. A producer that
// stated only the stem and the activity would resolve past the recovered `+0x5dc` class body, the
// committed weapon ladders and the armed/alert branch with nothing saying they never ran.
struct FElysiumActivityClipRequest
{
	FString Stem;
	// A stable ACT_* name — the logical request, before any translation.
	FString Activity;
	// The repeatable selection token weighted choice keys on. Same token, same pick, forever.
	int32 Variant = 0;
	// Who asked, carried onto the record so a resolve names its true producer. It decides no
	// translation — that is `BodyKind`'s fork — and defaults to `Npc` because plain NPC AI (patrol,
	// ambient, a schedule task) is what most of these requests are; a player-owned weapon and a
	// scripted beat state their own.
	EElysiumAnimSource Source = EElysiumAnimSource::Npc;
	// Which chain the BODY translates through. Stated by the producer from the body it drives: a
	// player weapon and an NPC's are the same `FElysiumWeapon` entity, so nothing downstream can
	// derive it.
	//
	// The default is the CAST chain, the pair of the `Npc` source above: the two answer for one body,
	// and a default pair that named an NPC producer walking `CBasePlayer`'s one pass would describe a
	// body that does not exist. Every producer states both explicitly; this is what an unstated
	// request means, not what one is expected to leave.
	EElysiumAnimBodyKind BodyKind = EElysiumAnimBodyKind::Cast;
	// The actor's own entity classname, which finds its recovered class bodies. On the player it is
	// the registered class's own name (`player`), which reaches no recovered class body — the player's
	// actor translation is the two committed `CBasePlayer` rows.
	FString ActorClassname;
	// The active weapon's entity classname — the key the committed ladders are joined to. Empty is a
	// body with empty hands, and its translation is the empty table retail's unarmed body walks.
	FString WeaponClassname;
	// The cast body's own state, which the recovered human pre-translation reads to choose between
	// the alert animation set and the relaxed one. Meaningless on the player.
	EElysiumNpcState ActorState = EElysiumNpcState::Idle;
	// Where the hit came from, in the `hit_yaw` pose parameter's own degrees — (-180,180],
	// right-positive with zero forward, the same convention move_yaw uses. Zero on every request that
	// is not a directional reaction (the parameter's resting value).
	float HitYaw = 0.0f;
	// Whether a miss may walk `CAI_BaseNPC`'s recovered fallback ladder. The availability probe and
	// the run-to-walk, disposition and sequence-zero rungs are the cast activity chain's own
	// unconditional steps, so the default is true and matches the per-frame publish — but retail's
	// gesture path (`AddGesture` -> `SelectWeightedSequence`, which simply returns on -1) never walks
	// any of them, so a reaction producer on that path sets this false and reads the miss.
	bool bAllowFallbackLadder = true;

	bool IsValid() const { return !Stem.IsEmpty() && !Activity.IsEmpty(); }
};

// What the activity seam answers with — one selection, reduced to the four things a producer acts
// on. The whole `FElysiumAnimationSelection` stays behind the seam: it is the diagnostic record,
// and a substrate caller that could read it would be a second reader of the resolution.
struct FElysiumActivityClip
{
	// The vocabulary key (`walk`). **This is what goes back through the clip player**: it owns the
	// include-DAG mapping to a bank, and the concrete cell below identifies nothing.
	FString Label;
	// The concrete cell in the owning bank (`walk_0`), whose authored ground speed configures a motor.
	FString AnimationName;
	// The bank the include DAG named. A grid cell is addressed by (owner, animation name), and this
	// is the owner half: handing the vocabulary LABEL back instead re-resolves the grid at neutral
	// pose parameters, which collapses a nine-cell directional fan onto its forward cell.
	FString OwnerStem;
	// Zero when that cell carries no authored movement record.
	float GroundSpeedCmPerSecond = 0.0f;
	// The selected row's OWN loop bit. VtMB reads `m_bSequenceLoops` off the sequence's flags, so an
	// authored loop keeps looping however it was asked for — a caller ORs this into its own request
	// rather than overriding it, and a body-language idle authored as a loop is why: without it the
	// clip plays once and then stands frozen on its last frame.
	bool bLooping = false;

	// --- what the label resolved to, when it named a FAN (LIFE5) ---------------------------------
	//
	// Whether the label names a multi-cell grid at all. A producer routing a reaction needs it: a fan
	// is played as a blend space steered by its axis, and a single cell as a plain clip.
	bool bGrid = false;
	// Where the fan was sampled, in the axis parameter's own degrees — `hit_yaw` on a hit fan. This is
	// the value the graph steers the blend space by, so it is the axis the grid BINDS rather than
	// whatever the producer happened to state.
	float AxisValue = 0.0f;
	// The pair the fan evaluates: `AnimationName` is the floor cell, this is the one after it, and
	// `AxisFraction` is the second one's weight. Carried out of the seam because a producer that saw
	// only the floor cell could not tell a two-cell mix from a snap — and a body with no fan to
	// evaluate has to collapse the pair itself.
	FString NextAnimationName;
	float AxisFraction = 0.0f;
};

// Which channel of the graph a one-shot is played through (LIFE5).
//
// **Two doors, not one with a flag.** `Slot` rides the DefaultSlot montage OVER the locomotion pose,
// which is what a scripted beat or an ambient stance wants. `Reaction` REPLACES the base pose on the
// graph's own reaction branch, which is the only channel that can hold a directional fan: a montage
// plays one sequence, and a hit reaction is a blend between the two cells its angle sits between.
enum class EElysiumOneShotRoute : uint8
{
	Slot,
	Reaction,
};

// One already-resolved cell, ready to play over whatever owns the base pose (LIFE5).
//
// It carries no translation context and no activity, because nothing here resolves: the (owner,
// animation name) pair names one baked clip outright, and the label rides only so the channel claim
// can say what it stands for. A producer builds one from a `FElysiumActivityClip` the activity seam
// already answered.
struct FElysiumOneShotClipRequest
{
	// The bank the clip is baked into, and the concrete cell in it. Both, because a cell is addressed
	// by the pair — the animation name alone identifies nothing.
	FString OwnerStem;
	FString AnimationName;
	// The vocabulary key the cell came from, for the claim's own diagnostics line. Never re-resolved.
	FString Label;
	bool bLoop = false;
	// Stated apart because retail states them apart: the flinch fades in over 0.1 and out over 0.3,
	// and `PlaySlotAnimationAsDynamicMontage` takes the two separately.
	float BlendInSeconds = 0.2f;
	float BlendOutSeconds = 0.2f;
	EElysiumAnimPriority Priority = EElysiumAnimPriority::Reaction;
	EElysiumAnimSource Source = EElysiumAnimSource::Damage;

	// Which channel plays it. `Slot` by default, because that is what every producer that predates the
	// reaction branch means and what a body with no branch falls back to either way.
	EElysiumOneShotRoute Route = EElysiumOneShotRoute::Slot;

	// The BODY's own model stem, which is a different thing from `OwnerStem`: the owner is the bank the
	// clip is baked into, and this is the character whose vocabulary named the label. The reaction
	// route needs it because a fan is reached through the vocabulary, not through the bank alone.
	FString BodyStem;
	// Whether the label names a multi-cell fan, and where on its axis to sample — the two facts the
	// activity seam already answered, carried rather than re-derived. Both are meaningless on the
	// `Slot` route, whose montage plays exactly the cell the record names.
	bool bGrid = false;
	float AxisValue = 0.0f;

	bool IsValid() const { return !OwnerStem.IsEmpty() && !AnimationName.IsEmpty(); }
};

namespace ElysiumAnimIntent
{
	// Whether arming a one-shot on this route may force its body visible.
	//
	// The `Slot` route may, and does: it inherits the tail of the ordinary clip funnel, where a clip
	// is armed by something that also means the body to be seen — a stance, a scripted beat, a
	// dialogue line.
	//
	// **A `Reaction` may not.** It is involuntary, and the bodies it lands on include ones
	// deliberately taken off screen: `IElysiumNpcMotor::SetEnabled(false)` hides as well as
	// immobilises (`Source/ElysiumUE/CLAUDE.md` → Engine gotchas), so forcing visibility here would
	// flicker a disabled body into the world for the length of a flinch, on a hit nobody was meant
	// to see.
	//
	// One named rule with one owner rather than a branch inside the body factory, so the seam's
	// implementation and anything asserting the contract read the same statement.
	inline bool OneShotForcesVisibility(EElysiumOneShotRoute Route)
	{
		return Route != EElysiumOneShotRoute::Reaction;
	}
}

// WHICH clip a play seam is arming, in the vocabulary the event dispatcher is keyed on (LIFE5).
//
// It travels beside the asset because the asset cannot answer it. A baked `UAnimSequence` is named
// after the ANIMATION the bake wrote, while `FElysiumBlendTable::Events` is keyed by the SEQUENCE
// LABEL the caller reached it by — and the two are different strings wherever a grid label selects
// a cell, or a bank owns the clip a body plays. A phase built from the asset's own name would
// therefore address a timeline that does not exist.
//
// An EMPTY identity is legal and means "no timeline to walk": a preview stand, a green-room grid or
// a lab clip names no vocabulary key, and the channel it plays on publishes no phase at all. That
// is an ordinary absence, not a refusal — `FElysiumClipPhase::IsValid` says the same thing on the
// other side of the seam.
struct FElysiumClipIdentity
{
	// The bank the clip is baked into — the body's own stem, or the bank the include DAG named.
	FString OwnerStem;
	// The vocabulary key, never the resolved cell or animation name.
	FString Label;

	FElysiumClipIdentity() = default;
	FElysiumClipIdentity(FString InOwnerStem, FString InLabel)
		: OwnerStem(MoveTemp(InOwnerStem))
		, Label(MoveTemp(InLabel))
	{
	}

	bool IsValid() const { return !OwnerStem.IsEmpty() && !Label.IsEmpty(); }
};

// Where one channel of a body is standing on its clip, this frame (LIFE5).
//
// **`Cycle` is a phase, never a time.** VtMB's event dispatcher stores and compares a normalized
// `[0,1)` position, and the whole firing rule is an interval test over that number
// (`docs/vtmb/animation_and_movers.md` → "Sequence events and native dispatch"). A reader handed
// seconds would have to divide by a length that a blended fan does not have, so the pose layer
// answers the phase directly and `Length` rides only for diagnostics.
//
// `(OwnerStem, Label, PlayId)` is the identity a cursor is keyed on. The first two name WHICH clip
// — the bank that owns it and the vocabulary key — and `PlayId` is the restart discriminator: the
// same clip re-armed is a new play whose timeline has to fire again from zero, and nothing else on
// the record can tell that from a clip that simply looped.
struct FElysiumClipPhase
{
	// The bank the playing clip is baked into, and the vocabulary key it was reached by — the pair
	// `FElysiumBlendTable::FindEvents` is addressed with.
	FString OwnerStem;
	FString Label;
	// Normalized. A looping clip lives in `[0,1)` and reports the phase it wrapped to rather than 1;
	// a finished one-shot reports exactly 1, which is the one position it has and nowhere to wrap
	// from. Anything outside `[0,1]` is not a phase, and the pass that reads it says so.
	float Cycle = 0.0f;
	// Where this play's timeline is resumed FROM when a cursor meets it for the first time — the
	// lower bound of the interval its first frame walks. Zero, the default, is "this clip started
	// here", which is every clip a play seam started and is the only answer the rule had before.
	//
	// It is not always zero, because not every clip a channel presents was started by the thing
	// presenting it. A pose source that only OBSERVES a running clip — the locomotion state
	// machine, which nothing starts and nothing clocks — first sees it mid-flight, and a clip that
	// was displaced by a higher-priority pose and then took the channel back has been advancing the
	// whole time it was off. Anchoring either at zero would fire every record below the current
	// phase in one burst; anchoring at the phase they were last dispatched from walks exactly the
	// interval they really passed through.
	float AnchorCycle = 0.0f;
	// The clip's authored length in seconds, for readouts. Nothing in the firing rule reads it.
	float Length = 0.0f;
	bool bLooping = false;
	// Bumped on every (re)start of a clip on this channel. Zero is "nothing has ever played here".
	uint32 PlayId = 0;
	EElysiumAnimChannel Channel = EElysiumAnimChannel::Base;

	// A phase names a clip when it names the pair a timeline is addressed by. A body standing on
	// nothing answers a default-constructed record, which is an absence rather than a fault.
	bool IsValid() const { return !OwnerStem.IsEmpty() && !Label.IsEmpty(); }
};

namespace ElysiumAnimIntent
{
	// The one place an ACT_* literal is spelled for the slice, in both directions. Keeping the code
	// and the name in one pair is what stops the classifier, the record, the channel and the test
	// from drifting into four spellings of the same activity.
	const TCHAR* ActivityName(EElysiumAnimActivityCode Code);
	EElysiumAnimActivityCode ActivityCode(const FString& Name);

	// The classifier's thresholds, taken from the body's own authored fans (CCC7). The walk/run
	// split becomes the forward walk cell plus one unit, which is retail's rule and a **per-model**
	// number. A set with no resolved fans yields the defaults, so this is safe to call on any body.
	FElysiumGaitReference GaitFrom(const FElysiumGaitSpeeds& Speeds);

	// The record's enums as words. One spelling each, shared by Cog, the MCP surface and any log
	// line, so a reader comparing two of them is comparing the same vocabulary.
	const TCHAR* SourceName(EElysiumAnimSource Source);
	const TCHAR* BodyKindName(EElysiumAnimBodyKind BodyKind);
	const TCHAR* ChannelName(EElysiumAnimChannel Channel);
	const TCHAR* RouteName(EElysiumAnimRoute Route);
	const TCHAR* AssetKindName(EElysiumAnimAssetKind Kind);
	const TCHAR* OutcomeName(EElysiumAnimOutcome Outcome);
	const TCHAR* AirPhaseName(EElysiumAirPhase Phase);
	const TCHAR* PriorityName(EElysiumAnimPriority Priority);

	// The band a source's requests take when the producer does not choose one — the table's own
	// defaults, so a funnel that cannot know its caller still lands on a defensible row.
	EElysiumAnimPriority DefaultPriority(EElysiumAnimSource Source);

	// The rank of the locomotion publish itself, off the projected graph state: a standing body is
	// the floor, and anything travelling — a gait, a jump phase — is the travel row. This is the
	// interim while-locomoting rule restated as two rows of the one table.
	EElysiumAnimPriority LocomotionPriority(EElysiumGraphState State);

	// Advance the latch by one frame. Pure: previous latch and this frame's sample in, next latch
	// out, so the whole transition table is asserted without a body.
	//
	// `OneShot` is what the pose layer said about the clip the current phase is riding, and it
	// defaults to `Unknown` so every caller that has no graph — the gym, a headless think, a test —
	// keeps the timer path it always had without naming it.
	//
	// **`bCommandsJumps` is what makes the air phases the player's.** The whole discriminator this
	// latch exists for is the rising edge of a jump command, and only the player chain carries one;
	// retail reads its jump phase off a `CBasePlayer` field, and the cast's air activities come from
	// scripted tasks that request them outright (ManBat's fall, the Asian Vampire's jump) rather than
	// from any ground poll — there is no generic NPC producer of `ACT_FALLING` in the shipped binary
	// (`docs/vtmb/animation_and_movers.md`). A producer without a jump command therefore holds
	// `Grounded` and never reaches Leap/Falling/Land from a sample, however its mover reports itself.
	// The gait half of the latch still advances: `bLastGaitWasRun` is the walk/run memory `Classify`
	// reads, and suppressing it would stop the whole cast ever running.
	FElysiumJumpLatch AdvanceJumpLatch(const FElysiumJumpLatch& Prev,
		const FElysiumLocomotionSample& Sample, float DeltaSeconds, const FElysiumGaitReference& Gait,
		EElysiumOneShotState OneShot = EElysiumOneShotState::Unknown,
		bool bCommandsJumps = true);

	// Step 2 — choose a base activity from the settled body sample plus the latch.
	//
	// It emits the RELAXED gait forms, which is what retail's own classifier emits: the trace shows
	// walk and run entering translation as ACT_WALK_RELAXED / ACT_RUN_RELAXED, and a player body
	// carries no sequence for either, so the translation pass is load-bearing rather than decorative.
	EElysiumAnimActivityCode Classify(const FElysiumLocomotionSample& Sample,
		const FElysiumJumpLatch& Latch, const FElysiumGaitReference& Gait);

	// The weapon's grip (`docs/vtmb/animation_and_movers.md` A.4), which picks the 49-bone or
	// 24-bone upper-body mask profile a layer composes against. Defaults to `TwoHanded`: every
	// firearm and thrown weapon takes it, and so do the two-handed melee weapons, so an unlisted
	// tag takes the mask every aim grid already assumes.
	EElysiumWeaponGrip WeaponGrip(const FString& WeaponTag);

	// Build the frame's intent from the settled sample. The player path and the NPC motor both come
	// through here, which is what stops the cast's locomotion and the player's becoming two systems
	// that happen to play the same files.
	//
	// `BodyKind` has no default: it is the fork the whole translation chain turns on, and a caller
	// that does not state it is a caller whose body kind nobody checked.
	FElysiumAnimationIntent BuildLocomotionIntent(const FElysiumLocomotionSample& Sample,
		const FElysiumJumpLatch& Latch, const FElysiumGaitReference& Gait,
		EElysiumAnimSource Source, EElysiumAnimBodyKind BodyKind, const FString& Stem,
		const FElysiumEntityHandle& Character, int32 Variant);
}
