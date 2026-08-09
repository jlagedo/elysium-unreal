#pragma once

#include "CoreMinimal.h"

#include "ElysiumAnimationIntent.h"
#include "Visual/ElysiumNpcAnimSubsystem.h"

class UglTFRuntimeAsset;
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

	// --- Per-frame state ---------------------------------------------------------------------------
	FElysiumJumpLatch Latch;
	FElysiumGaitReference Gait;
	uint32 Generation = 0;

	// --- The discrete key: what a change of request actually means ---------------------------------
	FString LastActivity;
	FString LastStem;
	FString LastWeaponTag;
	EElysiumAnimRoute LastRoute = EElysiumAnimRoute::Activity;
	bool bResolvedOnce = false;

	// --- The published answers, always valid --------------------------------------------------------
	// A default-constructed record reads `NoVocabulary`, so every consumer — the channel recorder, Cog,
	// the MCP surface — can read this unconditionally rather than testing a pointer.
	FElysiumAnimationSelection Selection;
	FElysiumResolvedAnimation Assets;

	// Advance the latch, classify, and resolve if the request moved. `Anims` may be null (no game
	// instance), and `Mesh`/`OwnAsset` may be null (no body built yet) — the record is produced either
	// way, because it comes out of the sidecars rather than out of a skeleton.
	//
	// `OneShot` is the pose layer's answer about the clip the latch's current phase is riding, read
	// by the **caller** before this runs — the driver never reaches for an anim instance, because it
	// also serves bodies that have none. `Unknown` keeps the timer fallback.
	void Tick(float DeltaSeconds, const FElysiumLocomotionSample& Sample,
		UElysiumNpcAnimSubsystem* Anims, USkeletalMesh* Mesh, UglTFRuntimeAsset* OwnAsset,
		EElysiumOneShotState OneShot = EElysiumOneShotState::Unknown);

	// Forget the latch and the last request. A teleport or a map epoch is not a continuous motion, so
	// carrying a jump phase across one would report a body mid-leap that is standing still.
	void Reset();
};
