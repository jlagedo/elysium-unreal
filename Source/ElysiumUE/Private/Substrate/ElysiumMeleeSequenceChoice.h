#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

#include <limits>

class FElysiumEntity;

// Slot 331, `CBaseCombatCharacter::ChooseMeleeAttackSequence 0x10347180` (spec 0002 V11-3;
// `docs/specs/0002-npc-ai/stories/v4/packets-S5.md` item 4, `packets-S6.md` items 2-4), and its
// picker `ChooseSequenceFromList 0x10348100`.
//
// `bool (this, CBaseCombatWeapon* weapon, CBaseEntity* enemy, int activity, int* outSequence)`: the
// melee range test behind the `0x51` arm of the weapon's band `0x103ea7e0`. The body is stated here
// over a plain record (`FQuery`) so that every arm is driven without a world; the slot itself
// (`FElysiumCombatCharacter::ChooseMeleeAttackSequence`, defined in the `.cpp`) fills the record
// from the live character.
//
// **Frame**: SOURCE units in the PORT's axes (a position is `Cm / ElysiumMove::U`, Y not negated),
// the frame `FElysiumNpcBase::KernelHullTrace` takes. `debug_melee_npc_range`, the range pair
// `+0x2cc` / `+0x2d0` and the listing's constants are Source units and are compared as such.
namespace ElysiumMeleeSequenceChoice
{
	// The flag word's bits (`0x10347180`, per candidate).
	inline constexpr int32 FlagClosesOnEnemy = 0x01;   // 0x10347b9a: the movement ran into the enemy, +0xfe0 set
	inline constexpr int32 FlagEnvelopeReaches = 0x02; // 0x10347c52: a `+0x2bc` box overlaps the enemy's line-frame box
	inline constexpr int32 FlagMovementClear = 0x04;   // 0x1034763f OR EDX,0x4
	inline constexpr int32 FlagInRange = 0x08;         // 0x103475df MOV EDX,0x8
	inline constexpr int32 FlagNoMovement = 0x10;      // 0x10347baa: `0x100c6020` answered false

	// studiomdl's unset markers for `+0x2cc` / `+0x2d0` (S6 item 4): the raw words `0x00800000` and
	// `0x7f7fffff`. Compared raw by the body, never tested for.
	inline constexpr float LowEdgeUnstated = std::numeric_limits<float>::min();    // FLT_MIN
	inline constexpr float HighEdgeUnstated = std::numeric_limits<float>::max();   // FLT_MAX

	// `debug_melee_npc_range`'s default, `"128"` (strings `0x10623f88` / `0x10623fa4`), Source units.
	inline constexpr float DebugMeleeNpcRangeUnits = 128.0f;
	// The mask of both probes (`0x10347256 PUSH 0x202400b`, and `0x102e3450`'s third argument).
	inline constexpr int32 ProbeMask = 0x202400b;
	// `EBP == 0` (not an NPC): `0x1034744d MOV [ESP+0x60],0x40800000`.
	inline constexpr float StepWithoutNpcUnits = 4.0f;
	// Weapon slot 360 (`+0x5a0`) `& 0x18000` (`0x1034747f`).
	inline constexpr uint32 WeaponMeleeCapabilityMask = 0x18000u;
	// The player arm's two constants: `fraction >= 0.9` (f32 `0x10450a9c`) and the remainder
	// `<= 8.0` (f32 `0x1045597c`).
	inline constexpr float PlayerClearFraction = 0.9f;
	inline constexpr float PlayerRemainderUnits = 8.0f;

	// One `+0x2bc` record at `+0x2c0` (24 bytes: mins, maxs), Source units. The axes are reach,
	// lateral, vertical (`Public/ElysiumMeleeEnvelope.h`): a scale off the baked centimetres, no
	// reflection.
	struct FEnvelope
	{
		FVector Min = FVector::ZeroVector;
		FVector Max = FVector::ZeroVector;
	};

	// One sequence `GetSequencesForActivity` handed, as the body reads its descriptor.
	struct FCandidate
	{
		// What `*outSequence` receives when this candidate is picked.
		int32 Sequence = INDEX_NONE;
		// The weight `GetSequencesForActivity` writes beside it (`weights[]`).
		int32 Weight = 0;
		// `seq+0x2cc` / `seq+0x2d0`, raw: an unstated edge is its marker above.
		float LowEdgeUnits = LowEdgeUnstated;
		float HighEdgeUnits = HighEdgeUnstated;
		// `seq+0x2d4 >= 0` (`0x1034795d`).
		bool bStatesButtonMask = false;
		// `0x100c6020(model, seq, 1.0, pose parameters, &delta, &angles)`: its answer and `delta`, the
		// sequence's whole movement in retail's own sequence frame (Source axes).
		bool bHasMovement = false;
		FVector MovementUnits = FVector::ZeroVector;
		TArray<FEnvelope> Envelopes;
	};

	// A `trace_t` as the body reads it.
	struct FTrace
	{
		float Fraction = 1.0f;                  // +0x2c
		bool bAllSolid = false;                 // +0x36
		bool bStartSolid = false;               // +0x37
		const FElysiumEntity* Hit = nullptr;    // m_pEnt
	};

	struct FQuery
	{
		// `GetModelPtr(-1) != 0` (`0x10347208`).
		bool bHasModel = true;
		// `this+0x98 != 0` (`EBP`): the line gate, `dir`, slot 522.
		bool bNpc = true;
		// `this+0xa8 != 0`: the player's hull trace and its two clear tests.
		bool bPlayer = false;
		// `this.GetAbsOrigin()` (slot 217, `+0x364`).
		FVector OriginUnits = FVector::ZeroVector;

		// `param_2`. Null: the no-enemy arm.
		const FElysiumEntity* Enemy = nullptr;
		// The entity of the enemy's handle `+0x1538` (`m_GrapplePartner`), null when `+0x153c` is -1
		// or the handle does not resolve.
		const FElysiumEntity* EnemyGrapplePartner = nullptr;
		FVector EnemyOriginUnits = FVector::ZeroVector;
		// The enemy's collision bounds (`+0x270` slot 15), world space.
		FVector EnemyBoundsMinUnits = FVector::ZeroVector;
		FVector EnemyBoundsMaxUnits = FVector::ZeroVector;
		// The two slot-192 (`+0x300`) points of the line gate.
		FVector LineStartUnits = FVector::ZeroVector;
		FVector LineEndUnits = FVector::ZeroVector;

		// NPC slot 522 (`+0x828`); read only when `bNpc`.
		float NpcStepUnits = 0.0f;
		// `param_1 != 0` and its slot 360 word.
		bool bHasWeapon = false;
		uint32 WeaponCapabilityWord = 0;
		// The weapon's `m_fMinRange1 +0x8b8` / `m_fMaxRange1 +0x8c0` on entry.
		float WeaponMinRange1Units = 0.0f;
		float WeaponMaxRange1Units = 0.0f;
		// `m_bAllowsInterpenetratingAttacks` (`+0xfe0`).
		bool bAllowsInterpenetratingAttacks = false;
		// The ConVar's float; 0 when its `+4` virtual answers true.
		float NpcRangeUnits = DebugMeleeNpcRangeUnits;

		// Ascending sequence number, as `GetSequencesForActivity` emits them.
		TArray<FCandidate> Candidates;

		// `0x102e37b0(move probe, start, end, 0x202400b, 1, &tr)`: a line.
		TFunction<FTrace(const FVector& StartUnits, const FVector& EndUnits)> LineTrace;
		// NPC: `0x102e3450(move probe, start, end, 0x202400b, &tr, 0)`, the NPC's hull. Player:
		// `UTIL_TraceHull 0x1006dec0`.
		TFunction<FTrace(const FVector& StartUnits, const FVector& EndUnits)> HullSweep;
		// `hit+0x4c` (`m_edtDerivedType`) bit 10 (`0x103472dc`): set by the `CNPC_VPlaceholder`
		// constructor alone (`0x103a40c0`).
		TFunction<bool(const FElysiumEntity* Hit)> HitDerivedTypeBit10;
		// `VectorTransform 0x10137fe0(delta, m_rgflCoordinateFrame, &end)`: the no-enemy (or not an
		// NPC) arm's end point. Unset: `origin + delta`.
		TFunction<FVector(const FVector& MovementUnits)> BodyFrameEnd;
		// The picker's stream. Null: no draw is possible and the first admitted candidate answers.
		FRandomStream* Rng = nullptr;
	};

	struct FResult
	{
		// The slot's `bool`.
		bool bChosen = false;
		// `*outSequence`.
		int32 Sequence = INDEX_NONE;
		// The line gate raised `SetCondition(0x3a)` (`0x103472f6`).
		bool bEnemyBlocked = false;
		// The weapon's `+0x8b8` / `+0x8c0` after the candidates were folded in.
		float WeaponMinRange1Units = 0.0f;
		float WeaponMaxRange1Units = 0.0f;
		// The mask of the pass that picked (`-1`: none).
		int32 PickedMask = INDEX_NONE;
		// Each candidate's flag word, index-aligned with `FQuery::Candidates`. Empty when the body
		// left before scoring.
		TArray<int32> Flags;
	};

	// `ChooseSequenceFromList 0x10348100(count, seqs, weights, flags, mask)`: the candidates whose
	// `(flags & mask) == mask`; none -> -1; one -> it; total weight `< 1` -> `RandomInt(0, n - 1)`;
	// else `RandomInt(0, total - 1)` walked down the weights.
	int32 ChooseSequenceFromList(const TArray<FCandidate>& Candidates, const TArray<int32>& Flags,
		int32 Mask, FRandomStream* Rng);

	// One candidate's flag word (`0x103475ad..0x10347c6c`), given the enemy geometry of step 3.
	// Exposed for the arm tests; `Choose` is its only other caller.
	struct FEnemyGeometry
	{
		float Dist2DUnits = 0.0f;
		FVector BoxMinUnits = FVector::ZeroVector;   // (dist2D - h.x, -h.y, rel.z - h.z)
		FVector BoxMaxUnits = FVector::ZeroVector;   // (dist2D + h.x, +h.y, rel.z + h.z)
		FVector Dir = FVector::ZeroVector;           // normalize(enemy origin - origin), enemy and NPC only
	};
	FEnemyGeometry EnemyGeometry(const FQuery& Query);
	int32 CandidateFlags(const FQuery& Query, const FEnemyGeometry& Geometry,
		const FCandidate& Candidate, float StepUnits);

	// The whole body.
	FResult Choose(const FQuery& Query);
}
