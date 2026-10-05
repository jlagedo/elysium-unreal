#include "Substrate/ElysiumMeleeSequenceChoice.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"                  // ElysiumMove::U — the one Source-unit conversion
#include "ElysiumPlayer.h"                     // FElysiumCombatCharacter, FElysiumGrappleState
#include "ElysiumRng.h"
#include "ElysiumSkeletalBasis.h"              // FromSourceAngles — the body's coordinate frame
#include "ElysiumWorldServices.h"              // IElysiumEmbodiment::GetBodyClipByRawIndex (read, not edited)
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"    // EElysiumNpcCond::EnemyBlocked (0x3a)
#include "Substrate/ElysiumNpcPlaceholder.h"   // CNPC_VPlaceholder — m_edtDerivedType bit 10's one writer
#include "Substrate/ElysiumWeaponClasses.h"    // FElysiumWeapon::RangeWords (+0x8b8 / +0x8c0)
#include "Visual/ElysiumNpcClips.h"            // FElysiumNpcClip — the baked sequence descriptor

// -------------------------------------------------------------------------------------------------
// `ChooseSequenceFromList 0x10348100`
// -------------------------------------------------------------------------------------------------

int32 ElysiumMeleeSequenceChoice::ChooseSequenceFromList(const TArray<FCandidate>& Candidates,
	const TArray<int32>& Flags, int32 Mask, FRandomStream* Rng)
{
	// `0x10348191..0x103481c3`: the admitted list, its weights and their total.
	TArray<int32, TInlineAllocator<16>> Admitted;
	int32 Total = 0;
	for (int32 Index = 0; Index < Candidates.Num() && Index < Flags.Num(); ++Index)
	{
		if ((Flags[Index] & Mask) == Mask)                                       // 0x10348197 AND / 0x10348199 CMP
		{
			Admitted.Add(Index);
			Total += Candidates[Index].Weight;                                   // 0x103481a9
		}
	}
	const int32 Count = Admitted.Num();
	if (Count < 1)                                                               // 0x103481c6 CMP ESI,1 / JGE
	{
		return INDEX_NONE;                                                       // 0x103481d8 OR EAX,-1
	}
	if (Count == 1)                                                              // 0x103481e2 JNZ
	{
		return Candidates[Admitted[0]].Sequence;                                 // 0x103481ef
	}
	if (Rng == nullptr)
	{
		// No stream (a caller that states none): no draw; the first admitted candidate. Not retail's.
		return Candidates[Admitted[0]].Sequence;
	}
	if (Total < 1)                                                               // 0x103481fe CMP EAX,1 / JGE
	{
		// `RandomInt(0, n - 1)` (`0x10348203 DEC ESI / PUSH ESI / PUSH 0`).
		return Candidates[Admitted[Rng->RandRange(0, Count - 1)]].Sequence;      // 0x10348207
	}
	// `RandomInt(0, total - 1)` (`0x10348227 DEC EAX / PUSH EAX / PUSH 0`), walked down the first
	// `n - 1` weights: `r -= w[i]`, stop on a negative (`0x1034823b SUB / JS`); the last candidate is
	// whatever the walk falls off onto.
	int32 Roll = Rng->RandRange(0, Total - 1);                                   // 0x1034822b
	int32 Pick = 0;
	while (Pick < Count - 1)                                                     // 0x10348236..0x10348247
	{
		Roll -= Candidates[Admitted[Pick]].Weight;                               // 0x1034823b
		if (Roll < 0)                                                            // 0x10348242 JS
		{
			break;
		}
		++Pick;
	}
	return Candidates[Admitted[Pick]].Sequence;                                  // 0x10348254
}

// -------------------------------------------------------------------------------------------------
// `ChooseMeleeAttackSequence 0x10347180`
// -------------------------------------------------------------------------------------------------

ElysiumMeleeSequenceChoice::FEnemyGeometry ElysiumMeleeSequenceChoice::EnemyGeometry(
	const FQuery& Query)
{
	// `0x10347300..0x10347448`: the enemy's collision bounds (`+0x270` slot 15), their centre and
	// half-extents (f32 `0x104454d0` = 0.5), `rel = centre - GetAbsOrigin()` (slot 217).
	FEnemyGeometry Geometry;
	const FVector Centre = (Query.EnemyBoundsMinUnits + Query.EnemyBoundsMaxUnits) * 0.5;
	const FVector Half = (Query.EnemyBoundsMaxUnits - Query.EnemyBoundsMinUnits) * 0.5;
	const FVector Rel = Centre - Query.OriginUnits;
	// `dist2D = sqrt(rel.x^2 + rel.y^2)`: the vertical term is outside the root.
	Geometry.Dist2DUnits = static_cast<float>(FMath::Sqrt(Rel.X * Rel.X + Rel.Y * Rel.Y));
	// The enemy's box in the attacker's line frame: reach, lateral, vertical.
	Geometry.BoxMinUnits = FVector(Geometry.Dist2DUnits - Half.X, -Half.Y, Rel.Z - Half.Z);
	Geometry.BoxMaxUnits = FVector(Geometry.Dist2DUnits + Half.X, Half.Y, Rel.Z + Half.Z);
	if (Query.bNpc)
	{
		// `0x103474c7..0x1034751e`: `dir = normalize(enemy origin - origin)`, all three components
		// (`VectorNormalize 0x10137220` leaves a zero vector alone).
		Geometry.Dir = (Query.EnemyOriginUnits - Query.OriginUnits).GetSafeNormal();
		// `0x1034755f..0x10347577`: `CrossProduct 0x1011e010(dir, (0, 0, 1), &right)` writes
		// `[ESP+0xdc]`, which no later instruction reads. See `CandidateFlags`.
	}
	return Geometry;
}

int32 ElysiumMeleeSequenceChoice::CandidateFlags(const FQuery& Query,
	const FEnemyGeometry& Geometry, const FCandidate& Candidate, float StepUnits)
{
	int32 Flags = 0;                                                             // 0x103475ad XOR EDX,EDX
	// Bit 8 (`0x103475b1..0x103475df`): an enemy, and `seq+0x2cc <= dist2D <= seq+0x2d0`, both raw
	// (`AND 0x100 / JNZ`; `TEST AH,0x41 / JP`). An unstated far edge (`FLT_MAX`) passes at any
	// distance; an unstated near edge (`FLT_MIN`) fails only at `dist2D == 0`.
	if (Query.Enemy != nullptr && Candidate.LowEdgeUnits <= Geometry.Dist2DUnits
		&& Geometry.Dist2DUnits <= Candidate.HighEdgeUnits)
	{
		Flags = FlagInRange;                                                     // 0x103475df
	}
	Flags |= FlagMovementClear;                                                  // 0x1034763f OR EDX,0x4
	if (!Candidate.bHasMovement)                                                 // 0x1034766d 0x100c6020 / 0x10347677 JZ
	{
		Flags |= FlagNoMovement;                                                 // 0x10347baa OR AL,0x10: no sweep
	}
	else
	{
		FVector Start = Query.OriginUnits;                                       // 0x10347681 slot 217
		FVector End;
		if (Query.Enemy != nullptr && Query.bNpc)                                // 0x10347697 / 0x103476a3
		{
			// `0x103476af..0x10347758`. The listing multiplies `delta.y` by `[ESP+0x90..0x98]`, which
			// is the COPY of `dir` made at `0x10347533..0x10347548` as the cross product's input; the
			// product itself (`right`, `[ESP+0xdc]`) is never read. So both terms lie along the line
			// to the enemy: `end = origin + dir * delta.x + dir * delta.y`. Reproduced as read
			// (packet S5 item 4 states `right * delta.y`; the listing is the authority).
			End = Start + Geometry.Dir * Candidate.MovementUnits.X               // 0x103476e0..0x10347704
				+ Geometry.Dir * Candidate.MovementUnits.Y;                      // 0x103476af..0x103476d2
		}
		else
		{
			// `0x10347765..`: `VectorTransform 0x10137fe0(delta, m_rgflCoordinateFrame, &end)`.
			End = Query.BodyFrameEnd ? Query.BodyFrameEnd(Candidate.MovementUnits)
				: Start + Candidate.MovementUnits;
		}
		Start.Z += StepUnits;                                                    // both ends raised by `step`
		End.Z += StepUnits;
		const FTrace Trace = Query.HullSweep ? Query.HullSweep(Start, End) : FTrace();
		if (!Query.bPlayer && Candidate.bStatesButtonMask)                       // 0x1034795d..0x10347967 seq+0x2d4 >= 0
		{
			// NPC arm only, after the sweep and before its outcome: bit 8 and bit 4 are both dropped.
			// The outcome below and the envelope test still write into the zeroed word.
			Flags = 0;
		}
		if (Trace.Fraction < 1.0f || Trace.bAllSolid || Trace.bStartSolid)       // _DAT_104454c0 = 1.0
		{
			// The entity of the enemy's `+0x1538` handle counts as the enemy.
			const FElysiumEntity* Hit = Trace.Hit;
			if (Hit != nullptr && Query.Enemy != nullptr && Query.EnemyGrapplePartner != nullptr
				&& Hit == Query.EnemyGrapplePartner)
			{
				Hit = Query.Enemy;
			}
			const float Remainder = (1.0f - Trace.Fraction)
				* static_cast<float>((End - Start).Size());                      // 0x101371d0
			bool bKeep = false;
			if (!Query.bPlayer)                                                  // this+0xa8 == 0
			{
				// `hit == 0 || hit == enemy`, not `startsolid`, and the remainder within
				// `debug_melee_npc_range`.
				bKeep = (Hit == nullptr || Hit == Query.Enemy) && !Trace.bStartSolid
					&& Remainder <= Query.NpcRangeUnits;                         // 0x10347b5e..0x10347b82
			}
			else
			{
				// Not `startsolid`, and `fraction >= 0.9` (`0x10450a9c`) or the remainder `<= 8.0`
				// (`0x1045597c`).
				bKeep = !Trace.bStartSolid
					&& (PlayerClearFraction <= Trace.Fraction || Remainder <= PlayerRemainderUnits);
			}
			if (!bKeep)
			{
				if (Hit != nullptr && Hit == Query.Enemy && Query.bAllowsInterpenetratingAttacks)   // 0x10347b84..0x10347b94 +0xfe0
				{
					Flags |= FlagClosesOnEnemy;                                  // 0x10347b9a OR AL,0x1
				}
				else
				{
					Flags &= ~FlagMovementClear;                                 // 0x10347ba2 AND AL,0xfb
				}
			}
		}
	}
	// Bit 2 (`0x10347bb0..0x10347c52`): an enemy, and any `+0x2bc` record at `+0x2c0` overlapping
	// the enemy's line-frame box, strict on all six sides. The first overlap ends the walk.
	if (Query.Enemy != nullptr)
	{
		for (const FEnvelope& Envelope : Candidate.Envelopes)
		{
			if (Envelope.Min.X < Geometry.BoxMaxUnits.X && Geometry.BoxMinUnits.X < Envelope.Max.X
				&& Envelope.Min.Y < Geometry.BoxMaxUnits.Y && Geometry.BoxMinUnits.Y < Envelope.Max.Y
				&& Envelope.Min.Z < Geometry.BoxMaxUnits.Z && Geometry.BoxMinUnits.Z < Envelope.Max.Z)
			{
				Flags |= FlagEnvelopeReaches;
				break;
			}
		}
	}
	return Flags;
}

ElysiumMeleeSequenceChoice::FResult ElysiumMeleeSequenceChoice::Choose(const FQuery& Query)
{
	FResult Result;                                                              // *out = -1, ahead of 0x10347208
	Result.WeaponMinRange1Units = Query.WeaponMinRange1Units;
	Result.WeaponMaxRange1Units = Query.WeaponMaxRange1Units;
	if (!Query.bHasModel)                                                        // 0x10347208 GetModelPtr / 0x10347216 JZ 0x10347d97
	{
		return Result;
	}

	// --- The line gate (`0x1034723d..0x103472fb`): an NPC with an enemy ----------------------------
	// (`0x1034722f`: not an NPC goes straight to the geometry; `0x10347237`: an NPC with no enemy
	// skips both.)
	if (Query.bNpc && Query.Enemy != nullptr)
	{
		const FTrace Trace = Query.LineTrace
			? Query.LineTrace(Query.LineStartUnits, Query.LineEndUnits) : FTrace();   // 0x1034727b -> 0x102e37b0
		if ((Trace.Fraction < 1.0f || Trace.bAllSolid || Trace.bStartSolid)      // 0x10347280..0x103472a8
			&& Trace.Hit != nullptr && Trace.Hit != Query.Enemy)                 // 0x103472b1 / 0x103472b5
		{
			// Not the entity of the enemy's handle `+0x1538` (`0x103472b9..0x103472da`), and
			// `hit+0x4c` bit 10 clear (`0x103472dc..0x103472e5`).
			const bool bBit10 = Query.HitDerivedTypeBit10 ? Query.HitDerivedTypeBit10(Trace.Hit) : false;
			if (Trace.Hit != Query.EnemyGrapplePartner && !bBit10)
			{
				Result.bEnemyBlocked = true;                                     // 0x103472f2..0x103472f6 SetCondition(0x3a)
				return Result;                                                   // false, *out = -1
			}
		}
	}

	// --- Enemy geometry (`0x10347300`), whenever there is an enemy ---------------------------------
	const FEnemyGeometry Geometry = Query.Enemy != nullptr ? EnemyGeometry(Query) : FEnemyGeometry();

	// `0x1034744b..0x10347462`: `step` = 4.0, or NPC slot 522 (`+0x828`).
	const float StepUnits = Query.bNpc ? Query.NpcStepUnits : StepWithoutNpcUnits;

	// `0x1034746d` / `0x1034747f`: no weapon, or weapon slot 360 `& 0x18000 == 0` -> false.
	if (!Query.bHasWeapon || (Query.WeaponCapabilityWord & WeaponMeleeCapabilityMask) == 0)
	{
		return Result;
	}

	// --- The candidates (`0x10347583..0x10347c7e`) -------------------------------------------------
	Result.Flags.Reserve(Query.Candidates.Num());
	for (const FCandidate& Candidate : Query.Candidates)
	{
		// The weapon's running pair, folded RAW and for every candidate (`0x103475e4..0x10347639`):
		// `+0x8b8 = min(+0x8b8, seq+0x2cc)` (`AND 0x4100 / JNZ`: the sequence's on equal),
		// `+0x8c0 = max(+0x8c0, seq+0x2d0)` (`TEST AH,5 / JP`).
		if (Candidate.LowEdgeUnits <= Result.WeaponMinRange1Units)
		{
			Result.WeaponMinRange1Units = Candidate.LowEdgeUnits;                // 0x10347610
		}
		if (Result.WeaponMaxRange1Units <= Candidate.HighEdgeUnits)
		{
			Result.WeaponMaxRange1Units = Candidate.HighEdgeUnits;               // 0x10347639
		}
		Result.Flags.Add(CandidateFlags(Query, Geometry, Candidate, StepUnits)); // 0x10347c6c
	}

	// --- The passes (`0x10347c84..0x10347d97`) -----------------------------------------------------
	if (Query.Enemy == nullptr)
	{
		// No enemy (`0x10347c88..0x10347ce5`): mask `0x10` (`0x10347ca7`), then mask `0`
		// (`0x10347cdb`); the pick is written and the answer is FALSE.
		Result.PickedMask = FlagNoMovement;
		Result.Sequence = ChooseSequenceFromList(Query.Candidates, Result.Flags, FlagNoMovement, Query.Rng);
		if (Result.Sequence < 0)
		{
			Result.PickedMask = 0;
			Result.Sequence = ChooseSequenceFromList(Query.Candidates, Result.Flags, 0, Query.Rng);
		}
		if (Result.Sequence < 0)
		{
			Result.PickedMask = INDEX_NONE;
		}
		return Result;
	}
	static constexpr int32 MaskOrder[8] = { 7, 5, 6, 3, 1, 2, 4, 0 };            // 0x10347cf3.. the eight immediates
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (const int32 Order : MaskOrder)
		{
			const int32 Mask = Pass == 0 ? (Order | FlagInRange) : Order;        // the first pass `| 8`
			const int32 Pick = ChooseSequenceFromList(Query.Candidates, Result.Flags, Mask, Query.Rng);
			Result.Sequence = Pick;
			if (Pick >= 0)
			{
				Result.PickedMask = Mask;
				// True only when the mask carries a bit of 3, bit 4 and bit 8: the first pass's 7, 5, 6.
				Result.bChosen = (Mask & 3) != 0 && (Mask & FlagMovementClear) != 0
					&& (Mask & FlagInRange) != 0;
				return Result;
			}
		}
	}
	Result.Sequence = INDEX_NONE;                                                // nothing picked: *out = -1, false
	return Result;
}

// -------------------------------------------------------------------------------------------------
// Slot 331 on the live character
// -------------------------------------------------------------------------------------------------

namespace
{
	// `0x100c6020(model, seq, 1.0, m_flPoseParameter, &delta, &angles)` (`0x1034766d`): the
	// sequence's whole movement, cycle 0 -> 1, from the owning bank's baked movement path
	// (`IElysiumEmbodiment::GetBodySequenceMovement`). The bake's frame is this runtime's (X forward,
	// Y right); the body reads retail's sequence frame (Source axes, y to the left), so Y is
	// reflected back and the centimetres scaled to units. False -- no body, no raw index, or a
	// sequence authoring no movement record -- is retail's own false: bit `0x10`, no sweep.
	bool MeleeSequenceWholeMovement(FElysiumCombatCharacter& Character, const FElysiumNpcClip& Clip,
		FVector& OutMovementUnits)
	{
		OutMovementUnits = FVector::ZeroVector;
		IElysiumEmbodiment* const Embodiment =
			Character.World != nullptr ? Character.World->Embodiment() : nullptr;
		FVector DeltaCm = FVector::ZeroVector;
		if (Embodiment == nullptr || !Clip.HasRawIndex()
			|| !Embodiment->GetBodySequenceMovement(Character.Visual, Character.ModelStem(),
				Clip.RawIndex, DeltaCm))
		{
			return false;
		}
		OutMovementUnits = FVector(DeltaCm.X, -DeltaCm.Y, DeltaCm.Z) / ElysiumMove::U;
		return true;
	}

	// The picked clip's number in the body's sequence space, the sequence bridge's row (the number
	// `ResetSequence` plays). A clip the embodiment cannot name by `RawIndex` (a headless world)
	// answers its `RawIndex`, else its place in the list: non-negative, which is all the band reads.
	int32 MeleeSequenceNumberOf(FElysiumCombatCharacter& Character, FElysiumNpc* Troika,
		const FElysiumNpcClip& Clip, int32 Ordinal)
	{
		IElysiumEmbodiment* const Embodiment =
			Character.World != nullptr ? Character.World->Embodiment() : nullptr;
		if (Troika != nullptr && Embodiment != nullptr && Clip.HasRawIndex())
		{
			FString Label;
			FElysiumNpcClip Found;
			if (Embodiment->GetBodyClipByRawIndex(Character.Visual, Character.ModelStem(),
					Clip.RawIndex, Label, Found) && !Label.IsEmpty())
			{
				return Troika->SequenceRowFor(Found.Owner, Label, Found.IsLooping(), Found.RawIndex);
			}
		}
		return Clip.HasRawIndex() ? Clip.RawIndex : Ordinal;
	}
}

bool FElysiumCombatCharacter::ChooseMeleeAttackSequence(FElysiumEntity* WeaponEntity,
	FElysiumEntity* Enemy, int32 Activity, void* OutSequence)
{
	// Slot 331 `0x10347180` `(weapon, enemy, activity, int* outSequence)`. The body is
	// `ElysiumMeleeSequenceChoice::Choose`; this fills its record. `Activity` is used as given: the
	// caller has run weapon slot 361 and owner slot 376 (`0x103ea810..0x103ea82d`).
	int32* const Out = static_cast<int32*>(OutSequence);
	if (Out != nullptr)
	{
		*Out = INDEX_NONE;                                                       // ahead of 0x10347208
	}
	const double U = ElysiumMove::U;
	FElysiumNpc* const Troika = AsNpc();                                         // this+0x98
	FElysiumNpcBase* const Base = AsNpcBase();

	ElysiumMeleeSequenceChoice::FQuery Query;
	Query.bHasModel = Visual != nullptr;                                         // 0x10347208 GetModelPtr(-1)
	Query.bNpc = Troika != nullptr;
	Query.bPlayer = World != nullptr                                             // this+0xa8
		&& static_cast<const FElysiumEntity*>(World->FindPlayer()) == static_cast<const FElysiumEntity*>(this);
	Query.OriginUnits = Origin / U;                                              // slot 217 (+0x364)
	Query.Enemy = Enemy;
	if (Enemy != nullptr)
	{
		// The enemy's `+0x1538` handle, null when `+0x153c == -1` (`0x103472b9`).
		if (const FElysiumCombatCharacter* const EnemyCharacter = Enemy->AsCombatCharacter())
		{
			if (EnemyCharacter->Grapple.Role != EElysiumGrappleRole::None && World != nullptr)
			{
				Query.EnemyGrapplePartner = World->Resolve(EnemyCharacter->Grapple.Partner);
			}
		}
		Query.EnemyOriginUnits = Enemy->Origin / U;
		// `+0x270` slot 15, the world bounds: the OBB (`RetailCollisionExtents`, Source axes, its Y
		// pair mirrored into this frame) about the origin. An entity with no extents here (the
		// accessor's own seam: props, brush entities) is a point at its origin.
		FVector Mins = FVector::ZeroVector;
		FVector Maxs = FVector::ZeroVector;
		FElysiumNpcBase::RetailCollisionExtents(*Enemy, Mins, Maxs);
		Query.EnemyBoundsMinUnits = Query.EnemyOriginUnits + FVector(Mins.X, -Maxs.Y, Mins.Z);
		Query.EnemyBoundsMaxUnits = Query.EnemyOriginUnits + FVector(Maxs.X, -Mins.Y, Maxs.Z);
		Query.LineStartUnits = WorldSpaceCenter() / U;                           // 0x10347272 slot 192 (+0x300)
		Query.LineEndUnits = Enemy->WorldSpaceCenter() / U;                      // 0x1034725e slot 192
	}
	if (Troika != nullptr)
	{
		Query.NpcStepUnits = Troika->StepHeight();                               // 0x1034745c slot 522 (+0x828)
		Query.bAllowsInterpenetratingAttacks = Troika->bAllowsInterpenetratingAttacks;   // +0xfe0
	}

	// The weapon: slot 360 (`+0x5a0`) as the port answers it (the item record's melee family is the
	// `0x18000` word, `ElysiumNpcCond::CapabilityBits`), and its `+0x8b8` / `+0x8c0`.
	FElysiumItem* const WeaponItem = WeaponEntity != nullptr ? WeaponEntity->AsItem() : nullptr;
	FElysiumWeapon* const Weapon = WeaponItem != nullptr ? WeaponItem->AsWeapon() : nullptr;
	Query.bHasWeapon = WeaponEntity != nullptr;                                  // 0x1034746d
	if (Weapon != nullptr)
	{
		const FElysiumItemDef* const Record = Weapon->Data();
		if (Record != nullptr && Record->IsControllableWeapon())
		{
			Query.WeaponCapabilityWord = Record->Type == EElysiumItemType::WeaponMelee   // 0x10347479
				? static_cast<uint32>(ElysiumNpcCond::CapabilityBits(ElysiumNpcCond::ECapability::Melee))
				: static_cast<uint32>(ElysiumNpcCond::CapabilityBits(ElysiumNpcCond::ECapability::Ranged));
		}
		Query.WeaponMinRange1Units = Weapon->RangeWords.MinRange1;               // +0x8b8
		Query.WeaponMaxRange1Units = Weapon->RangeWords.MaxRange1;               // +0x8c0
	}

	// `0x103474c2 GetSequencesForActivity(this, activity, seqs, weights, v)`. `v` is the owner's
	// attack-feat rank (`0x10204900` over the mode's `CVDmg_t` source, `0x1034748c..0x103474a2`);
	// `seq+0x2b8` is -1 on every shipped attack sequence, so it filters nothing and is not carried
	// (S6 item 2). Ascending sequence number: `RawIndex`, rows without one behind.
	TArray<FElysiumNpcClip> Clips;
	if (Base != nullptr && Query.bHasModel && Query.bHasWeapon
		&& (Query.WeaponCapabilityWord & ElysiumMeleeSequenceChoice::WeaponMeleeCapabilityMask) != 0)
	{
		Base->MeleeSequencesForActivity(Activity, Clips);
		Clips.StableSort([](const FElysiumNpcClip& A, const FElysiumNpcClip& B)
		{
			return A.HasRawIndex() && (!B.HasRawIndex() || A.RawIndex < B.RawIndex);
		});
	}
	Query.Candidates.Reserve(Clips.Num());
	for (int32 Index = 0; Index < Clips.Num(); ++Index)
	{
		const FElysiumNpcClip& Clip = Clips[Index];
		ElysiumMeleeSequenceChoice::FCandidate& Candidate = Query.Candidates.AddDefaulted_GetRef();
		Candidate.Sequence = Index;   // resolved to the sequence number after the pick
		Candidate.Weight = Clip.Weight;
		// The baked edges are CENTIMETRES and stated-or-not; retail's are Source units with a marker
		// (S6 item 4). Only a stated edge is converted.
		Candidate.LowEdgeUnits = Clip.HasLowReach() ? static_cast<float>(Clip.LowReachCm / U)
			: ElysiumMeleeSequenceChoice::LowEdgeUnstated;                       // seq+0x2cc
		Candidate.HighEdgeUnits = Clip.HasReach() ? static_cast<float>(Clip.ReachCm / U)
			: ElysiumMeleeSequenceChoice::HighEdgeUnstated;                      // seq+0x2d0
		Candidate.bStatesButtonMask = Clip.Combo.HasStateMask();                 // seq+0x2d4 >= 0
		Candidate.bHasMovement =
			MeleeSequenceWholeMovement(*this, Clip, Candidate.MovementUnits);    // 0x1034766d 0x100c6020
		Candidate.Envelopes.Reserve(Clip.Envelopes.Num());
		for (const FElysiumMeleeEnvelope& Envelope : Clip.Envelopes)             // seq+0x2bc / +0x2c0
		{
			ElysiumMeleeSequenceChoice::FEnvelope& Units = Candidate.Envelopes.AddDefaulted_GetRef();
			Units.Min = Envelope.Min / U;                                        // a scale, no reflection
			Units.Max = Envelope.Max / U;
		}
	}

	// The two probes. Neither `0x102e37b0` nor `0x102e3450` has a port body; both build
	// `CTraceFilterNav 0x102e30d0(npc, npc collision group)` over the engine trace, which is what
	// `FElysiumNpcBase::KernelHullTrace` (over `IElysiumEmbodiment::TraceRetail`) stands for.
	FElysiumEntityWorld* const TraceWorld = World;
	const auto ToTrace = [TraceWorld](const FElysiumNpcBase::FKernelHullTrace& Kernel)
	{
		ElysiumMeleeSequenceChoice::FTrace Trace;
		Trace.Fraction = Kernel.Fraction;
		Trace.bAllSolid = Kernel.bAllSolid;
		Trace.bStartSolid = Kernel.bStartSolid;
		// The static world answers no entity here (retail's `m_pEnt` is `worldspawn`): a wall on the
		// line is therefore not the gate's "entity that is not the enemy". Named in the report.
		Trace.Hit = TraceWorld != nullptr && Kernel.HitEntity.IsSet()
			? TraceWorld->Resolve(Kernel.HitEntity) : nullptr;
		return Trace;
	};
	if (Troika != nullptr)
	{
		Query.LineTrace = [Troika, ToTrace](const FVector& StartUnits, const FVector& EndUnits)
		{
			// `0x102e37b0(move probe +0x5d40, start, end, 0x202400b, 1, &tr)`: `Ray_t` with no extents.
			FElysiumNpcBase::FKernelHullTrace Kernel;
			Troika->KernelHullTrace(StartUnits, EndUnits, FVector::ZeroVector, FVector::ZeroVector,
				ElysiumMeleeSequenceChoice::ProbeMask, Kernel);
			return ToTrace(Kernel);
		};
		Query.HullSweep = [Troika, ToTrace](const FVector& StartUnits, const FVector& EndUnits)
		{
			// `0x102e3450(move probe, start, end, 0x202400b, &tr, 0)`: the NPC's collision mins
			// (`+0x270` slot 1) and maxs (slot 2) with `maxs.z - 1.0` (`_DAT_104454c0`).
			FVector Mins = FVector::ZeroVector;
			FVector Maxs = FVector::ZeroVector;
			FElysiumNpcBase::RetailCollisionExtents(*Troika, Mins, Maxs);
			Maxs.Z -= 1.0;
			FElysiumNpcBase::FKernelHullTrace Kernel;
			Troika->KernelHullTrace(StartUnits, EndUnits, Mins, Maxs,
				ElysiumMeleeSequenceChoice::ProbeMask, Kernel);
			return ToTrace(Kernel);
		};
	}
	// A non-NPC (the player): `UTIL_TraceHull 0x1006dec0` under `0x101ccec0(this, group 6)` with the
	// game movement's hull. SEAM: no port body; `HullSweep` unset answers a clear trace. The player
	// reaches no candidate through this slot today (`MeleeSequencesForActivity` is the NPC's), and
	// its own selector is `PlayerSelectMeleeSequence`.
	Query.HitDerivedTypeBit10 = [](const FElysiumEntity* Hit)
	{
		// `hit+0x4c` `m_edtDerivedType` bit 10 (`0x103472dc`). The bit's one writer is the
		// `CNPC_VPlaceholder` constructor (`0x103a40c0`: `this[0x13] |= 0x400` after the Troika
		// constructor `0x1028d230`): a placeholder NPC on the line does not block the swing. The
		// port stands no derived-type word (`FElysiumNpc::RetailDerivedType` is a seam for the
		// other bits), so the bit is read off the class that sets it.
		const FElysiumNpcBase* const HitNpc = Hit != nullptr ? Hit->AsNpcBase() : nullptr;
		return HitNpc != nullptr && HitNpc->IsNpcClass(FElysiumNpcPlaceholder::StaticRetailClass());
	};
	// `VectorTransform 0x10137fe0(delta, m_rgflCoordinateFrame, &end)` (`0x10347765`), the no-enemy
	// / non-NPC arm's end point: the sequence-frame delta (Source axes) turned by the body's angles
	// and added to its origin, in this frame.
	const FVector FrameOriginUnits = Query.OriginUnits;
	const FRotator FrameRotation = ElysiumSkeletalBasis::FromSourceAngles(Angles);
	Query.BodyFrameEnd = [FrameOriginUnits, FrameRotation](const FVector& MovementUnits)
	{
		return FrameOriginUnits
			+ FrameRotation.RotateVector(FVector(MovementUnits.X, -MovementUnits.Y, MovementUnits.Z));
	};

	// The picker's stream: `NpcSchedule`, the one the NPC's other sequence picks draw from
	// (`FElysiumNpc::ChangeStanceForReaction`, the disposition stance rolls).
	Query.Rng = &ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);

	const ElysiumMeleeSequenceChoice::FResult Result = ElysiumMeleeSequenceChoice::Choose(Query);

	if (Result.bEnemyBlocked && Troika != nullptr)
	{
		Troika->Cognition.Conditions.Set(EElysiumNpcCond::EnemyBlocked);         // 0x103472e8 SetCondition(0x3a)
	}
	if (Weapon != nullptr)
	{
		Weapon->RangeWords.MinRange1 = Result.WeaponMinRange1Units;              // 0x10347610 +0x8b8
		Weapon->RangeWords.MaxRange1 = Result.WeaponMaxRange1Units;              // 0x10347639 +0x8c0
	}
	if (Out != nullptr && Clips.IsValidIndex(Result.Sequence))
	{
		*Out = MeleeSequenceNumberOf(*this, Troika, Clips[Result.Sequence], Result.Sequence);
	}
	return Result.bChosen;
}
