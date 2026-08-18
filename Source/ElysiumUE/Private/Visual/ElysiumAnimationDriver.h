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
	float GaitSpeedForSelection(float MoveYawDegrees) const;

	// Read `ActorClassname`/`WeaponClassname` off the character the body embodies. One place, because
	// the player and the cast take the same two names off the same chain node — and a body whose
	// character has gone reads empty hands rather than keeping the last weapon it held.
	void SetTranslationContext(const FElysiumCombatCharacter* Character);

	// Re-resolve the tables if the body key moved, and rebuild `Gait` from them. True when they
	// moved; `Anims` may be null, and a body with no game instance keeps what it has.
	//
	// Callable outside `Tick` because a body's speeds are wanted before its first animation pass: an
	// NPC's first travel request is issued in the frame its motor is built, and a request with no
	// tables behind it travels at a constant while the body's own cycle authors something else.
	bool RefreshGaitSpeeds(UElysiumAnimSubsystem* Anims, const FString& InWeaponClassname,
		const FString& InFormTag);

	// --- The discrete key: what a change of request actually means ---------------------------------
	FString LastActivity;
	FString LastStem;
	FString LastWeaponClassname;
	EElysiumAnimRoute LastRoute = EElysiumAnimRoute::Activity;
	bool bResolvedOnce = false;

	// --- The published answers, always valid --------------------------------------------------------
	// A default-constructed record reads `NoVocabulary`, so every consumer — the channel recorder, Cog,
	// the MCP surface — can read this unconditionally rather than testing a pointer.
	FElysiumAnimationSelection Selection;
	FElysiumResolvedAnimation Assets;

	// Advance the latch, classify, and resolve if the request moved. `Anims` may be null (no game
	// instance), and `Mesh` may be null (no body built yet) — the record is produced either way,
	// because it comes out of the sidecars rather than out of a skeleton.
	//
	// `OneShot` is the pose layer's answer about the clip the latch's current phase is riding, read
	// by the **caller** before this runs — the driver never reaches for an anim instance, because it
	// also serves bodies that have none. `Unknown` keeps the timer fallback.
	void Tick(float DeltaSeconds, const FElysiumLocomotionSample& Sample,
		UElysiumAnimSubsystem* Anims, USkeletalMesh* Mesh,
		EElysiumOneShotState OneShot = EElysiumOneShotState::Unknown);

	// Forget the latch and the last request. A teleport or a map epoch is not a continuous motion, so
	// carrying a jump phase across one would report a body mid-leap that is standing still.
	void Reset();
};
