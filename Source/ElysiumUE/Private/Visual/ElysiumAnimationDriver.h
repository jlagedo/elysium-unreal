#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"

#include "ElysiumAnimationIntent.h"
#include "ElysiumClipMovement.h"
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
struct FElysiumAnimationDriver : public FGCObject
{
	// **The resolved assets are rooted here, because nothing else can root them.** This struct is
	// plain C++ held through a `TPimplPtr` on an actor, so `Assets` is invisible to reflection: the
	// base pair is re-published to the anim instance every frame and survives on that, but the
	// overlay, the additive and the slot's trio are deliberately NOT re-copied on a frame the driver
	// does not own the base pose — a scene, a reaction, an ambient hold — and would be collected out
	// from under the next publish in a cooked build.
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FElysiumAnimationDriver"); }

	// --- Identity, set once when the body is built ------------------------------------------------
	FString Stem;
	EElysiumAnimSource Source = EElysiumAnimSource::Player;
	// The chain this body's requests translate through, set beside `Source` by whoever builds the
	// driver: the map actor drives the player pawn, an `AElysiumNpcBody` drives a cast stem. Every
	// intent this driver publishes carries it, so a reaction or a scene beat arriving on the same
	// body walks the same translator its locomotion does.
	EElysiumAnimBodyKind BodyKind = EElysiumAnimBodyKind::Player;
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
	// Give back every standing claim at once, whoever took it. The one caller is the death
	// transaction: a character that stops having behaviour stops owning every channel in the same
	// instant, and there is no producer left to come back with its handle. Returns how many claims
	// were standing, so the caller can report an unexpectedly held body.
	//
	// `OutDroppedSlotLayer` reports whether the overlay slot's own channel was among them, and it is
	// not a convenience: this struct never reaches for an anim instance — it also serves bodies that
	// have none — so it cannot take a layer's POSE down, and the caller that can has no other way to
	// learn one was standing. A layer dropped here with nothing taking its pose down keeps composing
	// at its last weight and phase, and the driver's own next publish is what would clear it — which
	// a frozen corpse never runs.
	int32 ReleaseAllRequests(bool* OutDroppedSlotLayer = nullptr);
	// The channel's standing claim, or null. The layer families read their channels through this.
	const FElysiumAnimationRequest* ActiveRequest(EElysiumAnimChannel Channel) const;

	// --- The forced ideal activity, and the movement lock over it (LIFE5) --------------------------
	// Where the base channel's clip stands, read off the pose layer by the body's owner and pushed
	// here before `Tick` — the driver never reaches for an anim instance, because it also serves
	// bodies that have none. Zeroed by `Reset`, like every other per-frame input.
	FElysiumBaseClipCycle BaseClipCycle;

	// **Retail's `m_IdealActivity`, assembled**: the activity the base-channel claim FORCES onto this
	// body plus the cycle its clip is standing on. The claim's `Activity` is the whole difference
	// between a body that is swinging and a body that is walking underneath a swing clip, and it is
	// what the movement lock, the reselection guard and the airborne-attack fork all read.
	//
	// Rebuilt every call rather than remembered. That is the recovered mechanism and not an
	// optimisation: bit `0x80` has one write site with both arms present, so a reaction that
	// overwrites the ideal activity mid-swing releases the lock in the same frame.
	FElysiumIdealActivityState ForcedIdealActivity() const;

	// Age the slots by one frame and drop expired claims. Split from the verdict because expiry is
	// time and the verdict is state — `Tick` runs both, in that order.
	//
	// **This is the overlay slot's whole lifetime too, and it needs no mechanism of its own.**
	// `ElysiumAnimIntent::ClaimForSegment` set a layer's `HoldSeconds` to its clip's authored length
	// over its playback rate, so the claim ends exactly when the clip's cycle reaches 1 — which is
	// retail's own rule for a layer slot's weight dropping to 0. A re-fire submits a new claim on the
	// same channel, which replaces the standing one and restarts the layer from zero.
	void AdvanceRequests(float DeltaSeconds);
	// Write the base-channel verdict onto `Selection`. Runs on every `Tick` exit path, because a
	// claim can expire or be outranked on a frame whose discrete request never moved.
	void ArbitrateBase();
	// Publish the overlay slot's identity and phase onto `Selection`. Runs beside `ArbitrateBase` on
	// every `Tick` exit path, for the same reason: a layer claim expires on its own clock.
	//
	// **It arbitrates nothing, despite the name it shares with the base pass, and that asymmetry is
	// the mechanism.** The slot never competes with the locomotion publish — it is accumulated ON TOP
	// of whatever owns the base pose, gated per bone by the layer clip's mask — so there is no rank
	// to compare and no claim to consume. All it does is state who is layering, at what weight and
	// where on its clip.
	void ArbitrateSlot();
	// Answer for the standing slot claim on an exit that does NOT re-enter the base resolve.
	//
	// **`Tick` has two player-only exits that skip the selection pass entirely** — an
	// animation-driven frame, and a frame whose reselection an unfinished swing refuses — and the
	// slot does not belong to either: it composes over whatever owns the base pose rather than
	// participating in the choice of one, which is the premise `UElysiumAnimSubsystem::ResolveAnimation`
	// states when it resolves the slot ahead of every base rung. A claim armed on one of those frames
	// is a shot fired mid-swing; leaving it unanswered would let `ArbitrateSlot` publish it as a named
	// label with no bank and no asset for the whole swing, which is a body that stops shooting with
	// nothing saying so.
	//
	// Keyed on `LastSlotHandle` exactly as the ordinary path is, so a claim already answered for is
	// not re-loaded per frame.
	void ResolveSlotClaim(UElysiumAnimSubsystem* Anims, USkeletalMesh* Mesh);

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
	// The overlay slot's own discrete key, kept apart from the base's above because the two move for
	// unrelated reasons: a body fires without changing what it is doing, and it changes what it is
	// doing without firing. The HANDLE and not the label, because a re-fire of the same layer is a new
	// play that has to resolve again — the handle is the only thing that tells one from the other.
	//
	// It does NOT advance `Generation`. That number means "the base request moved" and every reader
	// downstream treats it as a transition to blend, so folding a shot into it would restate the
	// body's gait as a new selection once per trigger pull.
	uint32 LastSlotHandle = 0;

	// --- The published answers, always valid --------------------------------------------------------
	// A default-constructed record reads `NoVocabulary`, so every consumer — the channel recorder, Cog,
	// the MCP surface — can read this unconditionally rather than testing a pointer.
	FElysiumAnimationSelection Selection;
	FElysiumResolvedAnimation Assets;
	// The ideal activity that stood while this frame's record was produced, and whether the
	// animation-driven predicate held over it. Published beside the record because both answers are
	// consumed OUTSIDE the driver — the mover's substituted command and the weapon's airborne fork —
	// and a second evaluation of the predicate elsewhere is two answers waiting to disagree.
	FElysiumIdealActivityState IdealActivity;
	// `vt+0x670` — the raw predicate. It gates reselection here and the JUMP refusal in the mover.
	bool bAnimationDriven = false;
	// `vt+0x674` — the same predicate OR `ideal == ACT_LAND_HARD`, which is the arm the MOVEMENT
	// substitution reads. Identical to the flag above on every row this rung implements, because all
	// of them are melee rows and `ACT_LAND_HARD` is not one.
	bool bMovementLocked = false;
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
