#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntityHandle.h"

// The Discipline runtime: selection, the shared cast authority, the two execution families
// and the one teardown.
//
// `docs/vtmb/disciplines.md` owns the behaviour, `docs/architecture/gameplay-systems-architecture.md`
// §5.6 owns the decomposition. This is a plain-C++ domain service beside `ElysiumDamage` and
// `ElysiumSheetMath`: it reaches the engine only through `FElysiumWorldServices`, adds no clock and
// no scheduler, and every timed step is an owned event on the one queue.
//
// The state it drives lives on the chain: `FElysiumDisciplineState` on `FElysiumCombatCharacter`
// (the native active slots and the tracked targeted effects) and the three selection members on
// `FElysiumPlayer`, both mirrored onto `FElysiumPlayerRecord`.
//
// Where the recovered record is open, this file marks a **SEAM** (a channel parsed and carried but
// not executed, named with what would close it) or states a **CHOSEN** rule with its citation.
// Neither is ever presented as recovered.

class FElysiumCombatCharacter;
class FElysiumPlayer;
class FElysiumEntityWorld;
struct FElysiumDiscHit;
struct FElysiumDisciplineTgt;
struct FElysiumSheet;

namespace ElysiumDisciplines
{
	// The compiled Discipline order (`docs/vtmb/disciplines.md` § "The system in one page"). It is
	// also the slot order of both the `Disciplines` and `Active_Disciplines` sheet containers, so
	// one index addresses the learned rating, the active value and this service's own arrays.
	enum EIndex : int32
	{
		Animalism = 0,
		Auspex,
		BloodHealing,
		Celerity,
		CorpusVampirus,
		Dementation,
		Dominate,
		Fortitude,
		Obfuscate,
		Potence,
		Presence,
		Protean,
		Thaumaturgy,
		Count,
	};

	// The `stats.txt` InternalName of a compiled index's learned trait (`"Corpus_Vampirus"`), or
	// null when the index is out of range.
	const TCHAR* InternalName(int32 Index);
	// The compiled index for a learned trait's InternalName or its datamap spelling, case
	// insensitively; INDEX_NONE when nothing owns the name.
	int32 IndexFromName(const FString& Name);

	// Why a request did not commit. Every refusal is reported at its own site, so a caller that
	// only needs "did it happen" tests `bAccepted`.
	enum class EResult : uint8
	{
		Accepted,
		RefusedIndex,          // not one of the thirteen
		RefusedNoCharacter,
		RefusedDead,           // inert / dormant
		RefusedNoRules,        // no rulebook is up, so nothing can be resolved
		RefusedNotLearned,     // the learned value is below 1
		RefusedWorldArea,      // the eligibility virtual refused (world area type 2, Elysium)
		RefusedPredependency,  // an `IncPredependency` on the `Active_*` stat read false
		RefusedNoRecord,       // no `DisciplineTgt` matches (Discipline, tier)
		RefusedBlood,          // the adjusted blood cost exceeds the pool
		RefusedNoTargets,      // step 4: no legal target remained
		RefusedRecovering,     // the record's own `RecoveryTime` has not elapsed
	};
	const TCHAR* ResultName(EResult Result);
	inline bool Accepted(EResult Result) { return Result == EResult::Accepted; }

	// --- Selection and the shared authority ---------------------------------------------------
	// `vdiscipline_int <index>`: store the selection, then run the shared authority with the
	// remembered pair. A newly selected index resets the remembered tier; reselecting the same
	// index preserves it. An index whose learned value is below one is rejected.
	EResult Select(FElysiumPlayer& Player, int32 Index);

	// `vdiscipline_last`: only the last step, with the remembered pair.
	EResult UseLast(FElysiumPlayer& Player);

	// The shared authority: the eligibility virtual, the learned check, then the `Is_Instant`
	// branch into the native active-state path or the targeted `DisciplineTgt` transaction. Public
	// because it is the testable door — a Substrate test drives (character, index, tier) directly rather
	// than through the verb.
	EResult Use(FElysiumCombatCharacter& Char, int32 Index, int32 Tier);

	// The tier a bare `vdiscipline_int` casts at.
	//
	// **CHOSEN** — the client sends only the compiled index, and `docs/vtmb/disciplines.md` marks
	// the upper-tier selection handoff **[open]**. Until it is recovered, the tier is the remembered
	// one when it is non-zero and the character's learned rating otherwise, which is the only tier
	// the recovered command surface can name.
	int32 TierFor(const FElysiumPlayer& Player, int32 Index);

	// --- The one teardown ---------------------------------------------------------------------
	// `ClearActiveDisciplines` / `vdiscipline_endall` converge here: retire every owned expiry
	// event (by minting past its serial), remove every trait-effect group both families installed,
	// zero the thirteen active slots, drop the tracked targeted effects, recompute. Idempotent.
	void ClearAll(FElysiumCombatCharacter& Char);

	// End one native active state (its owned event came due, or teardown reached it).
	void EndNative(FElysiumCombatCharacter& Char, int32 Index);

	// --- The owned expiry event ---------------------------------------------------------------
	// The input name the owned event delivers to `!self`. One input serves both families; the
	// serial in the parameter says which owner it belongs to.
	const FName& ExpiryInput();
	// The delivered event's body. A serial that matches nothing is a stale renewal remnant and is
	// dropped with a Verbose line, exactly as a superseded weapon commit is.
	void CommitExpiry(FElysiumCombatCharacter& Char, int32 Serial);

	// --- Interruption -------------------------------------------------------------------------
	// `ShouldRemove_OnTakeDamage`: called from the one typed health commit.
	void NotifyDamaged(FElysiumCombatCharacter& Char);
	// `ShouldRemove_OnHearCombat`: polled against the substrate sound-event bus from the owner's
	// think, which is where every other bus consumer reads (§2.5.3 — the bus delivers nothing).
	void PollHeardCombat(FElysiumCombatCharacter& Char, double Now);
	// `ShouldRemove_OnWasBumped`.
	//
	// **SEAM** — this runtime raises no character-vs-character bump event: `IElysiumNpcMotor` owns
	// locomotion and reports no contact, and the movement solver's own traces are not surfaced to
	// the substrate. The entry exists and is exercised by the Substrate tier so the producer that
	// lands only has to call it.
	void NotifyBumped(FElysiumCombatCharacter& Char);

	// --- Joins ---------------------------------------------------------------------------------
	// The active value of one compiled Discipline — the `Active_*` sheet slot. What
	// `ElysiumWeapons::ActivePotenceRank` reads, and what a HUD read would.
	int32 ActiveRank(const FElysiumCombatCharacter& Char, int32 Index);

	// --- The declared verbs --------------------------------------------------------------------
	void ExecuteSelect(FElysiumEntityWorld& World, const FString& Args);
	void ExecuteLast(FElysiumEntityWorld& World);
	void ExecuteEndAll(FElysiumEntityWorld& World);

	// The `TriggerAISound` category this domain emits is `ElysiumGameSounds::DisciplineAlert()`,
	// one entry in `Substrate/ElysiumGameSound.h` beside every other producer category.

	// --- Stated interim constants ---------------------------------------------------------------
	// Each stands in for a value the recovered record does not carry. They are named, not
	// scattered, so the RE that closes one lands as a single replacement.

	// `AoE.Affects "Cone"` authors a range and no angle, and the native cone half-angle is
	// unrecovered. Five records use the shape.
	inline constexpr float ConeHalfAngleDegrees = 45.f;

	// A `Target`-shaped record needs the caster's aim target. Retail reads the retained action
	// target handle; this runtime has no such handle (the same gap the ranged weapon half reports),
	// so the primary target is acquired the way a melee opponent already is — the nearest live
	// combat character inside the record's own `Range` and this cone.
	inline constexpr float AimConeHalfAngleDegrees = 30.f;
}
