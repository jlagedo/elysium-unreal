#pragma once

#include "CoreMinimal.h"

#include "ElysiumAnimationIntent.h"
#include "Visual/ElysiumAnimSubsystem.h"

class FElysiumCombatCharacter;
class USkeletalMesh;

// One body's animation selection, driven once per frame from its settled locomotion sample (CCC4).
//
// The same struct serves both producers: the player's lives on the map actor and ticks in the
// post-move pass, an NPC's lives on its own body and ticks in its own post-physics pass. One
// contract, two producers, so the cast's locomotion and the player's cannot become two systems that
// happen to play the same files.
//
// **It resolves only when the discrete request changes.** The weighted pick and the asset load happen
// on an activity change; every other frame rewrites the continuous parameters in place. That is
// `docs/architecture/animation-architecture.md` section 3.7 made structural rather than remembered.
//
// This rung resolves and records; it drives no pose. What consumes the published selection is the
// player graph, and building a second driver into the native proxy would build scaffolding that
// `CCC9` exists to delete.
struct FElysiumAnimationDriver
{
	// --- Identity, set once when the body is built ------------------------------------------------
	FString Stem;
	EElysiumAnimSource Source = EElysiumAnimSource::Player;
	FElysiumEntityHandle Character;
	// The repeatable selection token. Fixed per body — the entity handle's index — because
	// repeatability is the contract: the same body picks the same idle every load. Ambient re-rolls
	// belong where one-shot completion lives.
	int32 Variant = 0;

	// The character's own rate multiplier (`m_flSpeedScale`). Scales the run and sneak fans and not
	// the walk, which is faithful. Nothing writes it yet — the discipline that would is not built —
	// and it is modelled here so the asymmetry is structural rather than a comment.
	float SpeedScale = 1.0f;

	// --- What the activity translation keys on ------------------------------------------------------
	// The body's own entity classname and its active weapon's, pushed by whoever owns the body
	// because only they can see the entity. They select the recovered translation bodies and the
	// weapon ladder, so they change which sequence set every request resolves against — and they are
	// part of the discrete key below for exactly that reason.
	FString ActorClassname;
	FString WeaponClassname;
	FString FormTag;
	// The body's own state. The recovered human pre-translation reads it to choose between the alert
	// and relaxed animation sets, so it belongs to the same push and moves the discrete key with it.
	EElysiumNpcState ActorState = EElysiumNpcState::Idle;
	// The live `IsInCombatStance` answer (LIFE4), read off the character in the same push as the
	// weapon because the player gait ladder's `CombatReady`/`Relaxed` predicates consume the two
	// together. Not part of the gait key: stance moves which ladder row fires — and so the
	// activity, which already IS the discrete key — never which fan tables answer, exactly as
	// retail's `T` reads the walk cell in and out of stance alike.
	bool bCombatStance = false;

	// --- Per-frame state ---------------------------------------------------------------------------
	FElysiumJumpLatch Latch;
	FElysiumGaitReference Gait;
	// The pose parameter's slew and hold. Owned here because this ticks once per body per frame,
	// which a pull-style locomotion getter does not.
	FElysiumMoveYawFilter MoveYawFilter;
	uint32 Generation = 0;

	// --- The body key, and the gait speed tables it resolves to (CCC7) -----------------------------
	// Separate from the request key below because it moves for different reasons: the tables depend
	// on the **body**, never on what the body is doing, so a turn, a sprint or a strafe cannot move
	// them. Re-resolved only when this key does, which is what lets the mover be handed a table
	// rather than asking for one per tick — and the mover runs before this driver, so a per-frame
	// answer would always be a frame stale.
	FElysiumGaitSpeedRequest GaitKey;
	FElysiumGaitSpeeds GaitSpeeds;
	// Advances on every re-resolve, so a consumer pushes on change instead of copying per frame.
	// Zero until the first one.
	uint32 GaitGeneration = 0;

	// The stride the current selection commands at a direction, cm/s. Falls back to the resolved
	// cell's own authored speed for anything that is not one of the three gaits.
	//
	// The gait is read off the record's `GraphState`, which is projected once from the LOGICAL
	// request. A translated name cannot answer this: `ACT_WALK_RELAXED_PISTOL` is a walk and is not
	// in the slice's small code vocabulary, so switching on the resolved activity classifies every
	// armed or class-translated body as "not a gait" and freezes its stride at the resolve-time cell.
	float GaitSpeedForSelection(float MoveYawDegrees) const;

	// --- The standing-with-weapon call (LIFE4, Option A) -------------------------------------------
	// **The live selector split, stated once:** the player's grounded stand/gait comes off the
	// COMMITTED retail gait ladder — `ElysiumActionTables::PlayerGaitLadder()` walked by
	// `SelectRule` against a live state query — while water, the air phases and the whole cast
	// keep `ElysiumAnimIntent::Classify`. Returns the fired row's activity (`ACT_AIM` for a
	// combat-ready stand, the plain gaits in stance, the relaxed ones out of it — translation
	// then renames per weapon, with no Combat special case anywhere in the chain), or empty
	// where the grounded branch does not decide and the classifier's answer stands.
	FString SelectPlayerGroundActivity(const FElysiumLocomotionSample& InSample) const;
	// The ladder's last row is unconditional, so a missing-row warning is unreachable today; guarded
	// to log once rather than per frame if a future regeneration ever makes it reachable.
	mutable bool bWarnedNoGroundActivityRow = false;

	// Read `ActorClassname`/`WeaponClassname` off the character the body embodies. One place, because
	// the player and the cast take the same two names off the same chain node — and a body whose
	// character has gone reads empty hands rather than keeping the last weapon it held.
	void SetTranslationContext(const FElysiumCombatCharacter* Character);

	// The body key this driver's tables resolve under — **the same chain the pose walks**: the same
	// source, the same actor classname, the same alert/relaxed state, the same weapon and form. Pure,
	// so `Elysium.Substrate.AnimationDriver` can assert the membership rule with no subsystem behind
	// it; keyed on less than this, an idle cast human poses `ACT_WALK_RELAXED` off its class body and
	// travels at the un-relaxed player fan.
	FElysiumGaitSpeedRequest BuildGaitKey() const;

	// Re-resolve the tables if the body key moved, and rebuild `Gait` from them. True when they
	// moved; `Anims` may be null, and a body with no game instance keeps what it has.
	//
	// Callable outside `Tick` because a body's speeds are wanted before its first animation pass: an
	// NPC's first travel request is issued in the frame its motor is built, and a request with no
	// tables behind it travels at a constant while the body's own cycle authors something else.
	bool RefreshGaitSpeeds(UElysiumAnimSubsystem* Anims);

	// --- The channel arbitration slot (LIFE4) -------------------------------------------------------
	// One request slot per channel, written by the action families and read by the arbitration:
	// each `Tick` ranks the base slot's claim against the locomotion publish's own row of the
	// priority table and writes the verdict onto the record (`bBasePoseOwned`/`BaseHold`), which is
	// what the graph obeys — a publish ends a foreign one-shot only where the table says it wins.
	// This replaces the interim while-locomoting rule, whose behaviour survives as the
	// Ambient-vs-locomotion rows of the table.
	struct FElysiumAnimRequestSlot
	{
		FElysiumAnimationRequest Request;
		float AgeSeconds = 0.0f;
		uint32 Handle = 0;
		bool bActive = false;
	};
	FElysiumAnimRequestSlot Requests[ElysiumAnimIntent::NumChannels];
	// Handles stay unique for the driver's life so a stale release finds nothing rather than the
	// claim that replaced its target. Never reset.
	uint32 RequestSerial = 0;

	// Claim a channel. Replaces the channel's standing claim when the new one ranks at least as
	// high; a lower-ranked claim is refused (returns 0) so a dialogue stance cannot displace the
	// scene that owns the body. Returns the non-zero handle that names the claim while it stands.
	uint32 SubmitRequest(const FElysiumAnimationRequest& Request);
	// Give a claim back by the handle `SubmitRequest` returned. False when the claim is already
	// gone — expired, outranked or replaced — which is an ordinary answer, not an error.
	bool ReleaseRequest(uint32 Handle);
	// The channel's standing claim, or null. The layer families read their channels through this.
	const FElysiumAnimationRequest* ActiveRequest(EElysiumAnimChannel Channel) const;

	// Age the slots by one frame and drop expired claims. Split from the verdict because expiry is
	// time and the verdict is state — `Tick` runs both, in that order.
	void AdvanceRequests(float DeltaSeconds);
	// Write the base-channel verdict onto `Selection`. Runs on every `Tick` exit path, because a
	// claim can expire or be outranked on a frame whose discrete request never moved.
	void ArbitrateBase();

	// --- The discrete key: what a change of request actually means ---------------------------------
	FString LastActivity;
	FString LastStem;
	// The actor's own classname, which is what finds its recovered `+0x5dc`/`+0x5e0` class bodies —
	// so it changes which sequence set the SAME request resolves against, exactly as the weapon and
	// the state below do. It is pushed a frame after the body is built (the entity has to resolve
	// first), and without it here that first tick's class-less answer would be kept for the life of
	// the request.
	FString LastActorClassname;
	FString LastWeaponClassname;
	EElysiumNpcState LastActorState = EElysiumNpcState::Idle;
	EElysiumAnimRoute LastRoute = EElysiumAnimRoute::Activity;
	bool bResolvedOnce = false;

	// --- The published answers, always valid --------------------------------------------------------
	// A default-constructed record reads `NoVocabulary`, so every consumer — the channel recorder, Cog,
	// the MCP surface — can read this unconditionally rather than testing a pointer.
	FElysiumAnimationSelection Selection;
	FElysiumResolvedAnimation Assets;
	// **The sample this driver classified**, filtered pose parameter and all. A producer's own
	// locomotion getter recomputes from live component state, so a reader that calls one after this
	// has ticked is describing a different frame than the record beside it; the trace's contract is
	// the published pair and nothing else, so the pair has to exist. Zeroed until the first tick,
	// exactly as `Selection` reads `NoVocabulary` until then.
	FElysiumLocomotionSample Sample;

	// Advance the latch, classify, and resolve if the request moved. `Anims` may be null (no game
	// instance), and `Mesh` may be null (no body built yet) — the record is produced either way,
	// because it comes out of the sidecars rather than out of a skeleton.
	//
	// `OneShot` is the pose layer's answer about the clip the latch's current phase is riding, read
	// by the **caller** before this runs — the driver never reaches for an anim instance, because it
	// also serves bodies that have none. `Unknown` keeps the timer fallback.
	void Tick(float DeltaSeconds, const FElysiumLocomotionSample& InSample,
		UElysiumAnimSubsystem* Anims, USkeletalMesh* Mesh,
		EElysiumOneShotState OneShot = EElysiumOneShotState::Unknown);

	// Forget the latch and the last request. A teleport or a map epoch is not a continuous motion, so
	// carrying a jump phase across one would report a body mid-leap that is standing still.
	void Reset();
};
