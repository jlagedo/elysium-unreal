#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntityHandle.h"
#include "ElysiumGaitSpeeds.h"
#include "ElysiumLocomotionSample.h"
#include "ElysiumMoveSolve.h"

// What a body asks the animation layer for, and what it is told back (CCC4).
//
// `FElysiumAnimationIntent` in, `FElysiumAnimationSelection` out, over steps 2, 4, 5 and 6 of
// `docs/architecture/animation-architecture.md` section 3.3. This header owns the pure half: the two
// records, the locomotion classifier, the jump latch and the activity translation pass. It reads
// `FElysiumLocomotionSample` and nothing else, so it is asserted with no world, no catalog and no
// UObject — `Elysium.Substrate.AnimationIntent`, the same pure-rules/engine-half split as
// `ElysiumMoveSolve.h` and `ElysiumCameraSolve.h`.
//
// The resolution that consumes an intent lives in `Visual/ElysiumAnimationResolve.h`, which needs the
// character catalog; the two are apart because the catalog types are private and this record crosses
// the substrate boundary.
//
// The record is not decoration. Six things can produce a wrong pose — input, classification,
// translation, model data, asset shape and blending — and without one line naming each, a wrong pose
// is a guess.

// Who asked. The producer, not the body: an NPC's damage reaction and its patrol both address the
// same body through different sources.
enum class EElysiumAnimSource : uint8
{
	Player,
	Npc,
	Scene,
	Damage,
	Interaction,
	Debug,
};

// Which slot the request owns. Only `Base` is arbitrated today; the rest exist because step 1's
// priority table is a seam this rung opens rather than invents later.
enum class EElysiumAnimChannel : uint8
{
	Base,
	FullBody,
	UpperBody,
	Additive,
	Gesture,
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
	// A translation row's override has no sequence, so the incoming activity stands. The row's
	// authored `required` bit does not change this: the pinned server translator never reads it.
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
	// The latch's answer, which is the only thing that can distinguish retail's jump phases.
	EElysiumAirPhase AirPhase = EElysiumAirPhase::Grounded;

	// --- Translation context ---------------------------------------------------------------------
	// Both empty today. The translation pass runs over them regardless, and terminates on retail's
	// own stop condition — an empty table is a table, not a bypass.
	FString FormTag;
	FString WeaponTag;

	// --- Completion --------------------------------------------------------------------------
	bool bLoop = true;
	// Whether a miss may walk `CAI_BaseNPC`'s recovered fallback ladder — run to walk, then the whole
	// request as a disposition, then sequence zero. It is an NPC rule and the player has none (the
	// controlled corpus records a ducked ACT_LAND_CROUCH request simply returning -1), so the ladder
	// needs both this and an `Npc` source. A caller clears it when its own contract predates the
	// ladder and its callers read the miss.
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
	EElysiumAnimChannel Channel = EElysiumAnimChannel::Base;
	EElysiumAnimRoute Route = EElysiumAnimRoute::Activity;
	uint32 Generation = 0;
	FString Stem;

	// --- Steps 2 and 3: the activity chain, one line per witnessed hop ---------------------------
	// The LOGICAL request, un-translated. Retail's `m_Activity` stays this: translation changes the
	// sequence set that realizes a request, not the AI-visible state.
	FString RequestedActivity;
	// Virtual +0x5dc. Always empty today — its semantics are unrecovered, and the player path's
	// pinned order is +0x5f4 then +0x5e0 with nothing before them. Empty is a true statement.
	FString PreTranslationActivity;
	// The FIRST weapon answer, kept apart from the last on purpose: retail alternates NPC-class and
	// weapon translation for up to five iterations, and only the first weapon result is retained
	// separately.
	FString FirstWeaponActivity;
	int32 TranslationIterations = 0;
	// What the sequence set was actually chosen for.
	FString ResolvedActivity;
	FString WeaponActivity;

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
	// to be looked up there.
	FString AnimationName;
	// The ideal being advanced to. Equal to SequenceLabel until a state machine exists to advance.
	FString TargetSequence;
	// ACT_TRANSITION's intermediate. Empty until CCC5 owns the traversal.
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
	int32 Axes = 0;

	// --- The verdict ------------------------------------------------------------------------------
	EElysiumAnimOutcome Outcome = EElysiumAnimOutcome::NoVocabulary;
	// One line naming what missed, in words. Read straight out of Cog and the MCP surface.
	FString Detail;

	bool IsResolved() const { return Outcome == EElysiumAnimOutcome::Resolved; }
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
	const TCHAR* ChannelName(EElysiumAnimChannel Channel);
	const TCHAR* RouteName(EElysiumAnimRoute Route);
	const TCHAR* AssetKindName(EElysiumAnimAssetKind Kind);
	const TCHAR* OutcomeName(EElysiumAnimOutcome Outcome);
	const TCHAR* AirPhaseName(EElysiumAirPhase Phase);

	// Advance the latch by one frame. Pure: previous latch and this frame's sample in, next latch
	// out, so the whole transition table is asserted without a body.
	//
	// `OneShot` is what the pose layer said about the clip the current phase is riding, and it
	// defaults to `Unknown` so every caller that has no graph — the gym, a headless think, a test —
	// keeps the timer path it always had without naming it.
	FElysiumJumpLatch AdvanceJumpLatch(const FElysiumJumpLatch& Prev,
		const FElysiumLocomotionSample& Sample, float DeltaSeconds, const FElysiumGaitReference& Gait,
		EElysiumOneShotState OneShot = EElysiumOneShotState::Unknown);

	// Step 2 — choose a base activity from the settled body sample plus the latch.
	//
	// It emits the RELAXED gait forms, which is what retail's own classifier emits: the trace shows
	// walk and run entering translation as ACT_WALK_RELAXED / ACT_RUN_RELAXED, and a player body
	// carries no sequence for either, so the translation pass is load-bearing rather than decorative.
	EElysiumAnimActivityCode Classify(const FElysiumLocomotionSample& Sample,
		const FElysiumJumpLatch& Latch, const FElysiumGaitReference& Gait);

	// One row of a translation table: the incoming activity, the override, and the authored `required`
	// bit.
	//
	// **`bRequired` is provenance, and nothing branches on it.**
	// `CBaseCombatWeapon::ActivityOverride` never reads the third dword of a table row, so the 201
	// flagged rows and the 9,013 optional ones take the same availability path; the bit is carried
	// because it is authored data that `activitydump` prints, not because a remake may give it
	// behaviour retail does not have (`docs/vtmb/animation_and_movers.md` A.3).
	//
	// **Ordered duplicates are load-bearing, and this table cannot yet express them.** Retail keeps
	// walking past a matching row whose output has no sequence, so a later duplicate-base row is an
	// availability fallback. With an empty weapon table and no duplicate actor rows there is nothing
	// to walk today; the ordered probe belongs with the weapon rung, where the data that needs it
	// arrives.
	struct FElysiumActivityTranslation
	{
		const TCHAR* From;
		const TCHAR* To;
		bool bRequired;
	};

	// The actor/form table (`CBasePlayer::NPC_TranslateActivity`, virtual +0x5e0). Two recovered rows.
	// A player body carries no ACT_WALK_RELAXED or ACT_RUN_RELAXED sequence, so without the
	// translation an unarmed request for either selects nothing at all.
	TArrayView<const FElysiumActivityTranslation> ActorTranslations();

	// The weapon table (`Weapon_TranslateActivity`, virtual +0x5f4) for a weapon tag. Carries the
	// layer rung's seeded rows — `ACT_RANGE_ATTACK1_LAYER` to a family-specific
	// `ACT_RANGE_ATTACK_LAYER_*` (`docs/vtmb/combat-and-damage.md`) — for the small set of ranged
	// families CCC10 exercises; empty for an unarmed body and for any tag this rung has not seeded,
	// same as retail's own empty table for an unarmed body.
	TArrayView<const FElysiumActivityTranslation> WeaponTranslations(const FString& WeaponTag);

	// The weapon's grip (`docs/vtmb/animation_and_movers.md` A.4), which picks the 49-bone or
	// 24-bone upper-body mask profile a layer composes against. Defaults to `TwoHanded`: every
	// firearm and thrown weapon takes it, and so do the two-handed melee weapons, so an unlisted
	// tag takes the mask every aim grid already assumes.
	EElysiumWeaponGrip WeaponGrip(const FString& WeaponTag);

	// What the translation pass answered, in the shape the record keeps it.
	struct FElysiumTranslationResult
	{
		// What the vocabulary is searched for.
		FString Resolved;
		// The first and last weapon answers, kept apart because retail retains them separately.
		FString FirstWeaponActivity;
		FString WeaponActivity;
		int32 Iterations = 0;
		// The last applied row's authored `required` bit, reported and never acted on.
		bool bRequired = false;
		// What a missed override falls back to.
		FString Incoming;
	};

	// Step 3 — apply the tables in their witnessed order: weapon (+0x5f4) then actor (+0x5e0), the
	// pinned player order. With an empty weapon table the loop runs once and terminates on retail's
	// own stop condition, which is a translation pass over an empty table rather than a bypass.
	FElysiumTranslationResult TranslateActivity(const FString& Activity, const FString& WeaponTag,
		const FString& FormTag);

	// Build the frame's intent from the settled sample. The player path and the NPC motor both come
	// through here, which is what stops the cast's locomotion and the player's becoming two systems
	// that happen to play the same files.
	FElysiumAnimationIntent BuildLocomotionIntent(const FElysiumLocomotionSample& Sample,
		const FElysiumJumpLatch& Latch, const FElysiumGaitReference& Gait,
		EElysiumAnimSource Source, const FString& Stem, const FElysiumEntityHandle& Character,
		int32 Variant);
}
