// `CAI_BaseNPC`'s bodies of the `Hints` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseHints.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "Substrate/ElysiumRetailActivities.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumHint.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcHintsShared.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumPlaceSet.h"
#include "Substrate/ElysiumSchedule.h"

// --- Moved from `ElysiumNpcHints.cpp` (story 5 step 5) ---

bool FElysiumNpcBase::HintWords(int32 HintNode, FHintWords& Out) const
{
	// Retail's hint words live on the `CAI_Hint` ENTITY reached through `m_pHintNode` (`+0x5ddc`).
	// This runtime carries a hint reference as that entity's index (0018 story 2), so the words are
	// the live `ai_hint`'s own. An index that is not a live hint answers false and leaves `Out`
	// untouched.
	if (World == nullptr || !World->Entities().IsValidIndex(HintNode))
	{
		return false;
	}
	const FElysiumHint* Hint = FElysiumHint::Cast(World->Entities()[HintNode].Get());
	if (Hint == nullptr || Hint->IsDead())
	{
		return false;
	}
	Out = Hint->ToWords();
	return true;
}

// --- The hint searches and the claim (0018 story 8) ---
//
// `docs/vtmb/npc-ai/shape.md` § "The hint list and its four searches" and § "The claim primitives,
// the hint LOS check and the idle gate", re-read against the listings of `0x102d1af0`, `0x102d24b0`,
// `0x102d2980`, `0x102d1760` and the claim trio.

namespace ElysiumNpcBaseHintSearch
{
	// The three cursor searches share one walk; which body walks decides the start/stop rule, the
	// admission word, the scoring and the stop.
	enum class EBody : uint8
	{
		Near,          // 0x102d1af0: wraps to its START element; type; scores; stops unless scoring
		OfTypeNear,    // 0x102d24b0: stops ON the cursor; type, from the anchor; first admitted
		ByClassMask,   // 0x102d2980: stops ON the cursor; class word; scores; stops unless tracing
	};

	struct FQuery
	{
		EBody Body = EBody::Near;
		int32 HintType = 0;          // Near / OfTypeNear: 0 = any (`param_2 == 0 ||`)
		int32 ClassMask = 0;         // ByClassMask: `(mask & hint+0x474) != 0`
		uint8 Flags = 0;
		float RadiusUnits = 0.f;
		// Near / ByClassMask: where the distance is measured from, centimetres -- the caller's
		// origin, else the NPC's `GetAbsOrigin` (vtable `+0x364`), read once before the walk.
		FVector OriginCm = FVector::ZeroVector;
		// Whose eye the bit-0 trace leaves from and whose `m_vecViewOffset` lifts its end: the NPC,
		// or OfTypeNear's anchor, whose origin is also the distance origin there. Null only for a
		// null anchor.
		const FElysiumEntity* Viewer = nullptr;
	};

	constexpr uint8 TraceBit = 0x1;          // bit 0: a clear eye trace
	constexpr uint8 ScoreDistSqrBit = 0x2;   // bit 1: score by d^2
	constexpr uint8 RandomBit = 0x4;         // bit 2: divert to 0x102d1760 (not from 0x102d2980)
	constexpr uint8 ScoreRatedBit = 0x8;     // bit 3: score by d x m_flHintRating (wins over bit 1)
	constexpr uint8 ScoringBits = 0xa;       // 102d1e7c TEST AL,0xa
	constexpr int32 TraceMask = 0x2400b;     // the mask all four bodies push

	enum class EGate : uint8
	{
		Admitted,
		Rejected,
		// OfTypeNear with a null anchor reached the distance gate, where retail calls the anchor's
		// vtable `+0x364` through NULL. CRASH GUARD: the search answers `INDEX_NONE` there and
		// leaves the cursor alone. Before that point (an empty list, no element past gates 1-2)
		// retail never touches the anchor, and neither does this.
		NoAnchor,
	};

	// `gpGlobals->curtime` (`*(float*)(DAT_1070b228 + 0xc)`) is a FLOAT; the claim words compare
	// and store against it, so the port rounds its clock the same way before it does.
	float CurTime(const FElysiumEntityWorld& World)
	{
		return static_cast<float>(World.NowSeconds());
	}

	// `m_hHintOwner` resolved: retail's EHANDLE serial test and the slot's entity pointer, which is
	// `FElysiumEntityWorld::Resolve` (a removed entity resolves to null, as an empty slot does).
	const FElysiumEntity* OwnerEntity(const FElysiumEntityWorld* World, const FElysiumEntityHandle& Owner)
	{
		return World != nullptr ? World->Resolve(Owner) : nullptr;
	}

	// The live `ai_hint` an index names, for the claim trio's WRITES (never a copy of its words).
	FElysiumHint* LiveHint(FElysiumEntityWorld* World, int32 HintNode)
	{
		if (World == nullptr || !World->Entities().IsValidIndex(HintNode))
		{
			return nullptr;
		}
		FElysiumHint* Hint = FElysiumHint::Cast(World->Entities()[HintNode].Get());
		return Hint != nullptr && !Hint->IsDead() ? Hint : nullptr;
	}

	// The 3-D squared distance in SOURCE UNITS, formed as retail forms it: float positions in units,
	// component differences, the sum of the three squares.
	float DistSqrUnits(const FVector& ACm, const FVector& BCm)
	{
		const FVector3f A(ACm / ElysiumMove::U);
		const FVector3f B(BCm / ElysiumMove::U);
		const FVector3f D = A - B;
		return D.X * D.X + D.Y * D.Y + D.Z * D.Z;
	}

	// Flags bit 0: `UTIL_TraceLine(viewer->EyePosition() (slot 193, vtable +0x304), hint origin +
	// viewer->m_vecViewOffset (+0x184), 0x2400b, CTraceFilterSimple(viewer, 0) (0x101d3190))`,
	// admitted only on `fraction == 1.0` (`TEST AH,0x44 / JP`). The port's `m_vecViewOffset` is the
	// eye point less the origin (`FElysiumNpc::DefaultEyeOffsetCm`'s reading); an entity with no eye
	// of its own answers its origin as its eye (`FElysiumEntity::EyePosition`), offset zero. The mask
	// carries no MONSTER bit (`0x2000000`), so the character list is empty and the world answer is
	// the whole trace.
	bool LineClear(const FElysiumNpcBase& Npc, const FElysiumEntity& Viewer,
		const FElysiumNpcBase::FHintWords& Words)
	{
		IElysiumEmbodiment* const Embodiment = Npc.World != nullptr ? Npc.World->Embodiment() : nullptr;
		if (Embodiment == nullptr)
		{
			// SEAM: no collision world behind this NPC reads as clear, and is counted.
			++Npc.HintSearchUnansweredTraces;
			return true;
		}
		const FVector EyeCm = Viewer.EyePosition();
		FElysiumRetailTrace Trace;
		Trace.StartCm = EyeCm;
		Trace.EndCm = Words.OriginCm + (EyeCm - Viewer.Origin);
		Trace.RetailMask = TraceMask;
		Trace.Ignore.Add(Viewer.Handle);
		Trace.Filter = EElysiumRetailTraceFilter::Simple;
		FElysiumRetailTraceResult Result;
		if (!Embodiment->TraceRetail(Trace, Result))
		{
			++Npc.HintSearchUnansweredTraces;   // no collision world: the clear defaults
			return true;
		}
		return Result.Fraction == 1.0f;
	}

	// Admission gates 1-4, in retail's order.
	EGate Gates(const FElysiumNpcBase& Npc, const FQuery& Query, FElysiumNpcBase::FHintWords& Words)
	{
		// 1. `0x102d14c0`, not unusable.
		if (FElysiumNpcBase::IsHintUnusable(Words, CurTime(*Npc.World),
			OwnerEntity(Npc.World, Words.HintOwner) != nullptr))
		{
			return EGate::Rejected;
		}
		// 2. The type (`piVar[0x177]`, `+0x5dc`), or the class word (`piVar[0x11d]`, `+0x474`).
		if (Query.Body == EBody::ByClassMask)
		{
			if ((Query.ClassMask & Words.ClassMask) == 0)
			{
				return EGate::Rejected;
			}
		}
		else if (Query.HintType != 0 && Words.HintType != Query.HintType)
		{
			return EGate::Rejected;
		}
		// 3. `d^2 < radius^2` (`local_a8 = radius * radius`), strict, from the origin -- the anchor's
		// in `0x102d24b0`. Written so an unordered distance fails, as `TEST AH,5 / JP` does.
		FVector FromCm = Query.OriginCm;
		if (Query.Body == EBody::OfTypeNear)
		{
			if (Query.Viewer == nullptr)
			{
				return EGate::NoAnchor;
			}
			FromCm = Query.Viewer->Origin;
		}
		const float RadiusSqr = Query.RadiusUnits * Query.RadiusUnits;
		if (!(DistSqrUnits(Words.OriginCm, FromCm) < RadiusSqr))
		{
			return EGate::Rejected;
		}
		// 4. Slot 566 `FValidateHintType(hint)` on the NPC (vtable `+0x8d8`).
		if (!const_cast<FElysiumNpcBase&>(Npc).FValidateHintType(&Words))
		{
			return EGate::Rejected;
		}
		return EGate::Admitted;
	}

	// The shared cursor walk. Retail's list is singly linked through `+0x5d8` and NULL-terminated;
	// the port's is `FElysiumEntityWorld::HintList`, a `TArray` head first, so an element's `next` is
	// the one at position + 1 and the tail's is NULL.
	int32 Walk(const FElysiumNpcBase& Npc, const FQuery& Query, float* OutScore)
	{
		FElysiumEntityWorld* const World = Npc.World;
		if (World == nullptr)
		{
			return INDEX_NONE;
		}
		const TArray<int32>& List = World->HintList();
		const int32 Num = List.Num();
		if (Num == 0)
		{
			return INDEX_NONE;   // `DAT_10925450 == NULL`: NULL, the cursor untouched
		}
		// The start: `cursor->next`, or the head when the cursor or its next is NULL. A cursor index
		// that is not on the list reads as NULL (retail's destructor `0x102d3040` zeroes a cursor on
		// the node it unlinks, so retail's cursor is always on the list).
		const int32 Cursor = World->HintCursor();
		const int32 CursorPos = Cursor != INDEX_NONE ? List.Find(Cursor) : INDEX_NONE;
		const int32 StartPos = CursorPos != INDEX_NONE && CursorPos + 1 < Num ? CursorPos + 1 : 0;
		// `0x102d1af0` wraps unconditionally and stops on its START element: every element once, the
		// cursor's last. `0x102d24b0` / `0x102d2980` wrap only while the cursor is set and stop ON the
		// cursor's element (examined only when it is also the start: a one-element list); with no
		// cursor they run head to tail once (`INDEX_NONE` is the NULL past the tail).
		const bool bWrapsToStart = Query.Body == EBody::Near;
		const int32 StopPos = bWrapsToStart ? StartPos : CursorPos;
		const bool bScores = Query.Body != EBody::OfTypeNear;

		int32 Best = INDEX_NONE;          // local_bc / local_b8
		float BestScore = MAX_FLT;        // local_c0 / local_bc = 3.4028235e+38
		int32 Pos = StartPos;
		do
		{
			const int32 HintIndex = List[Pos];
			FElysiumNpcBase::FHintWords Words;
			if (Npc.HintWords(HintIndex, Words))
			{
				const EGate Gate = Gates(Npc, Query, Words);
				if (Gate == EGate::NoAnchor)
				{
					return INDEX_NONE;
				}
				bool bAdmit = Gate == EGate::Admitted;
				// 5. Scoring, from the NPC's OWN origin (not the caller's): bit 3 before bit 1. A
				// candidate is dropped only when STRICTLY worse (`best < score`), so a tie -- and an
				// unordered score -- replaces.
				float Score = MAX_FLT;            // fStack_c4 / fStack_c0
				if (bAdmit && bScores)
				{
					if ((Query.Flags & ScoreRatedBit) != 0)
					{
						// `thunk 0x101371d0` (sqrt) x `+0x464`, compared in x87 extended precision.
						const double Rated = FMath::Sqrt(static_cast<double>(DistSqrUnits(Words.OriginCm, Npc.Origin)))
							* static_cast<double>(Words.HintRating);
						Score = static_cast<float>(Rated);
						bAdmit = !(static_cast<double>(BestScore) < Rated);
					}
					else if ((Query.Flags & ScoreDistSqrBit) != 0)
					{
						Score = DistSqrUnits(Words.OriginCm, Npc.Origin);
						bAdmit = !(BestScore < Score);
					}
				}
				// 6. Bit 0, the trace, from the viewer.
				if (bAdmit && (Query.Flags & TraceBit) != 0)
				{
					bAdmit = LineClear(Npc, *Query.Viewer, Words);
				}
				if (bAdmit)
				{
					if (Query.Body == EBody::OfTypeNear)
					{
						// `0x102d24b0` returns the first admitted element.
						World->SetHintCursor(HintIndex);
						return HintIndex;
					}
					Best = HintIndex;
					if (Query.Body == EBody::Near)
					{
						// `0x102d1af0`: keep it and its score; stop unless a scoring bit is set.
						BestScore = Score;
						if ((Query.Flags & ScoringBits) == 0)
						{
							break;
						}
					}
					else
					{
						// `0x102d2980`: stop iff bit 0 is CLEAR, the best score left at FLT_MAX; with
						// bit 0 set the walk runs on and the score is kept (`local_bc = fStack_c0`).
						if ((Query.Flags & TraceBit) == 0)
						{
							break;
						}
						BestScore = Score;
					}
				}
			}
			++Pos;
			if (Pos == Num)
			{
				Pos = bWrapsToStart || CursorPos != INDEX_NONE ? 0 : INDEX_NONE;
			}
		}
		while (Pos != StopPos);

		// The cursor is written on exit only: the winner, or NULL on a miss. `*outScore` is written
		// only with a winner (`if (best != NULL) { if (param_6 != NULL) *param_6 = score; ... }`).
		if (Best != INDEX_NONE)
		{
			if (OutScore != nullptr)
			{
				*OutScore = BestScore;
			}
			World->SetHintCursor(Best);
			return Best;
		}
		World->SetHintCursor(INDEX_NONE);
		return INDEX_NONE;
	}
}

int32 FElysiumNpcBase::FindHintNear(int32 HintType, uint8 SearchFlags, float RadiusUnits) const
{
	// `0x102d1af0(npc, type, flags, radius, NULL, NULL)` -- every recovered call site's shape.
	return FindHintNear(HintType, SearchFlags, RadiusUnits, nullptr, nullptr);
}

int32 FElysiumNpcBase::FindHintNear(int32 HintType, uint8 SearchFlags, float RadiusUnits,
	const FVector* OriginCm, float* OutScore) const
{
	namespace HS = ElysiumNpcBaseHintSearch;
	// `0x102d1af0`. Bit 2 diverts first, before the empty-list test, dropping the origin and score.
	if ((SearchFlags & HS::RandomBit) != 0)
	{
		return FindHintRandom(HintType, SearchFlags, RadiusUnits);
	}
	HS::FQuery Query;
	Query.Body = HS::EBody::Near;
	Query.HintType = HintType;
	Query.Flags = SearchFlags;
	Query.RadiusUnits = RadiusUnits;
	Query.OriginCm = OriginCm != nullptr ? *OriginCm : Origin;   // param_5 NULL -> GetAbsOrigin
	Query.Viewer = this;
	return HS::Walk(*this, Query, OutScore);
}

int32 FElysiumNpcBase::FindHintOfTypeNear(const FElysiumEntity* Near, int32 HintType, uint8 SearchFlags,
	float RadiusUnits) const
{
	namespace HS = ElysiumNpcBaseHintSearch;
	// `0x102d24b0`. Bit 2 diverts to the random pick with the anchor dropped: measured from the NPC.
	if ((SearchFlags & HS::RandomBit) != 0)
	{
		return FindHintRandom(HintType, SearchFlags, RadiusUnits);
	}
	HS::FQuery Query;
	Query.Body = HS::EBody::OfTypeNear;
	Query.HintType = HintType;
	Query.Flags = SearchFlags;
	Query.RadiusUnits = RadiusUnits;
	Query.Viewer = Near;   // null: the crash guard at the distance gate (`EGate::NoAnchor`)
	return HS::Walk(*this, Query, nullptr);
}

int32 FElysiumNpcBase::FindHintByClassMask(uint8 SearchFlags, int32 ClassMask, float RadiusUnits,
	const FVector* OriginCm, float* OutScore) const
{
	namespace HS = ElysiumNpcBaseHintSearch;
	// `0x102d2980`. No bit-2 test: it has no random arm.
	HS::FQuery Query;
	Query.Body = HS::EBody::ByClassMask;
	Query.ClassMask = ClassMask;
	Query.Flags = SearchFlags;
	Query.RadiusUnits = RadiusUnits;
	Query.OriginCm = OriginCm != nullptr ? *OriginCm : Origin;   // param_5 NULL -> GetAbsOrigin
	Query.Viewer = this;
	return HS::Walk(*this, Query, OutScore);
}

int32 FElysiumNpcBase::FindHintByClassMask1(uint8 SearchFlags, float RadiusUnits) const
{
	// `0x102d2940`: `0x102d2980(npc, flags, 1, radius, NULL, NULL)`.
	return FindHintByClassMask(SearchFlags, 1, RadiusUnits, nullptr, nullptr);
}

int32 FElysiumNpcBase::DryRunHintByClassMask(uint8 SearchFlags, int32 ClassMask, float RadiusUnits,
	TFunctionRef<void(const FHintAdmissionRow&)> OnRow) const
{
	namespace HS = ElysiumNpcBaseHintSearch;
	// Debug only. `FindHintByClassMask`'s query (`0x102d2980`, origin NULL -> my `GetAbsOrigin`), put
	// through the SAME gates `HS::Gates` runs, in its order and over the same helpers, but reporting
	// the first gate a hint fails; then `HS::Walk`'s ByClassMask order over the rows. Nothing here
	// writes the cursor or a hint. (Slot 566 is the live rule: its own side effects, the refusal
	// counter and any `ai_debug_npc` record, are the caller's to account for.)
	FElysiumEntityWorld* const EntityWorld = World;
	if (EntityWorld == nullptr)
	{
		return INDEX_NONE;
	}
	const TArray<int32>& List = EntityWorld->HintList();
	const int32 Num = List.Num();
	if (Num == 0)
	{
		return INDEX_NONE;
	}
	const FVector FromCm = Origin;
	const float RadiusSqr = RadiusUnits * RadiusUnits;
	TArray<FHintAdmissionRow, TInlineAllocator<16>> Rows;
	Rows.SetNum(Num);
	TArray<bool, TInlineAllocator<16>> PassedGates;
	PassedGates.Init(false, Num);
	for (int32 Pos = 0; Pos < Num; ++Pos)
	{
		FHintAdmissionRow& Row = Rows[Pos];
		Row.HintIndex = List[Pos];
		FHintWords Words;
		if (!HintWords(Row.HintIndex, Words))
		{
			Row.Gate = EHintAdmissionGate::NotLive;
			OnRow(Row);
			continue;
		}
		const float DistSqr = HS::DistSqrUnits(Words.OriginCm, FromCm);
		Row.DistanceUnits = FMath::Sqrt(DistSqr);
		if (IsHintUnusable(Words, HS::CurTime(*EntityWorld), HS::OwnerEntity(EntityWorld, Words.HintOwner) != nullptr))
		{
			Row.Gate = EHintAdmissionGate::Unusable;                     // gate 1
		}
		else if ((ClassMask & Words.ClassMask) == 0)
		{
			Row.Gate = EHintAdmissionGate::ClassMask;                    // gate 2
		}
		else if (!(DistSqr < RadiusSqr))
		{
			Row.Gate = EHintAdmissionGate::Distance;                     // gate 3
		}
		else if (!const_cast<FElysiumNpcBase*>(this)->FValidateHintType(&Words))
		{
			Row.Gate = EHintAdmissionGate::ValidateHintType;             // gate 4
		}
		else
		{
			PassedGates[Pos] = true;
			// Scoring, from my OWN origin, bit 3 before bit 1 (`HS::Walk` step 5).
			if ((SearchFlags & HS::ScoreRatedBit) != 0)
			{
				Row.ScoreRated = FMath::Sqrt(static_cast<double>(HS::DistSqrUnits(Words.OriginCm, Origin)))
					* static_cast<double>(Words.HintRating);
				Row.Score = static_cast<float>(Row.ScoreRated);
				Row.bScored = true;
			}
			else if ((SearchFlags & HS::ScoreDistSqrBit) != 0)
			{
				Row.Score = HS::DistSqrUnits(Words.OriginCm, Origin);
				Row.bScored = true;
			}
			// Bit 0, the trace — asked of every hint that passed gates 1-4 (the walk asks it only of
			// one its score did not drop; the answer is the hint's own either way).
			Row.Gate = (SearchFlags & HS::TraceBit) != 0 && !HS::LineClear(*this, *this, Words)
				? EHintAdmissionGate::Trace
				: EHintAdmissionGate::Admitted;
		}
		OnRow(Row);
	}

	// `HS::Walk`'s ByClassMask order, over the rows: start after the cursor (the head without one),
	// stop ON the cursor, wrap only while it is set; a candidate is dropped only when strictly worse;
	// with bit 0 clear the first admitted wins.
	const int32 Cursor = EntityWorld->HintCursor();
	const int32 CursorPos = Cursor != INDEX_NONE ? List.Find(Cursor) : INDEX_NONE;
	const int32 StartPos = CursorPos != INDEX_NONE && CursorPos + 1 < Num ? CursorPos + 1 : 0;
	int32 Best = INDEX_NONE;
	float BestScore = MAX_FLT;
	int32 Pos = StartPos;
	do
	{
		const FHintAdmissionRow& Row = Rows[Pos];
		if (PassedGates[Pos])
		{
			bool bAdmit = true;
			if ((SearchFlags & HS::ScoreRatedBit) != 0)
			{
				bAdmit = !(static_cast<double>(BestScore) < Row.ScoreRated);
			}
			else if ((SearchFlags & HS::ScoreDistSqrBit) != 0)
			{
				bAdmit = !(BestScore < Row.Score);
			}
			if (bAdmit && (SearchFlags & HS::TraceBit) != 0)
			{
				bAdmit = Row.Gate == EHintAdmissionGate::Admitted;
			}
			if (bAdmit)
			{
				Best = Row.HintIndex;
				if ((SearchFlags & HS::TraceBit) == 0)
				{
					break;
				}
				BestScore = Row.Score;
			}
		}
		++Pos;
		if (Pos == Num)
		{
			Pos = CursorPos != INDEX_NONE ? 0 : INDEX_NONE;
		}
	}
	while (Pos != CursorPos);
	return Best;
}

int32 FElysiumNpcBase::FindHintRandom(int32 HintType, uint8 SearchFlags, float RadiusUnits) const
{
	namespace HS = ElysiumNpcBaseHintSearch;
	// `0x102d1760(npc, type, flags, radius, NULL)`.
	if (World == nullptr || World->HintList().Num() == 0)
	{
		return INDEX_NONE;   // 102d176e: an empty list answers NULL and leaves the cursor alone
	}
	HS::FQuery Query;
	Query.Body = HS::EBody::Near;          // the type gate, measured from `OriginCm`
	Query.HintType = HintType;
	Query.Flags = SearchFlags;
	Query.RadiusUnits = RadiusUnits;
	Query.OriginCm = Origin;           // param_5 NULL -> the NPC's GetAbsOrigin
	Query.Viewer = this;
	// Head to tail once (`102d1977 MOV EDI,[EDI+0x5d8]` until NULL): gates 1-4, then bit 0. No scoring.
	TArray<int32, TInlineAllocator<16>> Picks;
	for (const int32 HintIndex : World->HintList())
	{
		FHintWords Words;
		if (!HintWords(HintIndex, Words) || HS::Gates(*this, Query, Words) != HS::EGate::Admitted)
		{
			continue;
		}
		if ((SearchFlags & HS::TraceBit) != 0 && !HS::LineClear(*this, *this, Words))
		{
			continue;
		}
		Picks.Add(HintIndex);
	}
	if (Picks.Num() < 1)
	{
		World->SetHintCursor(INDEX_NONE);   // 102d1997
		return INDEX_NONE;
	}
	// ONE `RandomInt(0, count - 1)` (`DAT_1070b244` vtable `+0x8`, `102d19d6`), only with a candidate.
	const int32 Pick = Picks[ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, Picks.Num() - 1)];
	World->SetHintCursor(Pick);            // 102d19e4
	return Pick;
}

bool FElysiumNpcBase::ClaimHint(int32 HintNode)
{
	namespace HS = ElysiumNpcBaseHintSearch;
	// `0x102d1350(hint, npc)`.
	FElysiumHint* Hint = HS::LiveHint(World, HintNode);
	if (Hint == nullptr)
	{
		return false;   // CRASH GUARD: retail dereferences the `CAI_Hint*`
	}
	// Refused only when `m_hHintOwner` resolves to a live entity that is not the requester.
	const FElysiumEntity* Owner = HS::OwnerEntity(World, Hint->HintOwner);
	if (Owner != nullptr && Owner != this)
	{
		return false;
	}
	// `m_hHintOwner = *npc->GetRefEHandle()` (vtable `+4`). Retail's NULL-requester arm (owner := -1)
	// has no port caller: the requester is this NPC.
	Hint->HintOwner = Handle;
	return true;
}

bool FElysiumNpcBase::OwnsHint(int32 HintNode) const
{
	namespace HS = ElysiumNpcBaseHintSearch;
	// `0x102d1450(hint, npc)`: the owner resolved by serial (a mismatch or -1 is NULL) compared with
	// the NPC. The `NULL == NULL` arm (a null requester, the network walkers' "is it unowned") has no
	// port caller: the requester is this NPC, never null.
	const FElysiumHint* Hint = HS::LiveHint(World, HintNode);
	if (Hint == nullptr)
	{
		return false;   // CRASH GUARD: retail dereferences the `CAI_Hint*`
	}
	return HS::OwnerEntity(World, Hint->HintOwner) == this;
}

void FElysiumNpcBase::ReleaseHintNode(int32 HintNode, float ReuseDelaySeconds)
{
	namespace HS = ElysiumNpcBaseHintSearch;
	// `0x102d1420`, exactly the two stores: `hint->m_hHintOwner (+0x5e0) = -1` and
	// `hint->m_flNextUseTime (+0x5ec) = ReuseDelaySeconds + gpGlobals->curtime`. No owner gate.
	FElysiumHint* Hint = HS::LiveHint(World, HintNode);
	if (Hint == nullptr)
	{
		return;         // CRASH GUARD: retail dereferences the `CAI_Hint*`
	}
	Hint->HintOwner = FElysiumEntityHandle::Invalid();
	Hint->NextUseTime = ReuseDelaySeconds + HS::CurTime(*World);
}

bool FElysiumNpcBase::IsHintUnusable(const FHintWords& Hint, double Now, bool bOwnerAlive)
{
	// `0x102d14c0`, arm for arm and in retail's order.
	if (Hint.Disabled != 0)                    // `m_iDisabled != 0`
	{
		return true;
	}
	if (Now < Hint.NextUseTime)                // `curtime < m_flNextUseTime`
	{
		return true;
	}
	// `m_hHintOwner` resolves to a live entity — retail's `EHANDLE` serial check plus a non-null
	// entity pointer, which is what `bOwnerAlive` stands for.
	return bOwnerAlive;
}

bool FElysiumNpcBase::IsHintUnusable(int32 HintNode, double Now) const
{
	FHintWords Hint;
	if (!HintWords(HintNode, Hint))
	{
		// Not a live hint. A hint that is not there is not usable.
		return true;
	}
	// `0x102d14c0`'s third arm is a LIVE owner (the EHANDLE resolving), not a set handle: a stale
	// owner leaves the hint usable.
	return IsHintUnusable(Hint, Now,
		ElysiumNpcBaseHintSearch::OwnerEntity(World, Hint.HintOwner) != nullptr);
}

void FElysiumNpcBase::RestartIdealActivityId(int32 RetailActivityId)
{
	// `CAI_BaseNPC::RestartIdealActivity` `0x10289ee0`, the whole body: when `m_Activity` (`+0xfec`)
	// already IS the activity asked for it is reset to 0 so the change re-triggers
	// (`0x10289ee4..0x10289eee`), then `SetIdealActivity(act)` (`0x10289efc JMP 0x100097d2` ->
	// `0x10272650`). The play that follows is the maintain loop's, off the ideal activity.
	if (ActivityNumber == RetailActivityId)                  // 0x10289eea CMP [ECX+0xfec],EAX
	{
		ActivityNumber = 0;                                  // 0x10289eee
	}
	SetIdealActivity(RetailActivityId);                      // 0x10289efc
}

FElysiumNpcBase::FHintRestoreResult FElysiumNpcBase::HintOnRestore(const FHintWords& Hint,
	const FElysiumEntityHandle& HintHandle, FElysiumPlaceSet& Places)
{
	// `CAI_Hint::OnRestore` — slot 130, `0x102d3ec0`, handed over by family Sounds.
	//
	// SLOT 130 IS `OnRestore`, not a sound handler: `vtmb_slot 130` shows `CAI_BaseNPC::OnRestore`
	// (`0x1027bf50`) and `CAI_BaseNPCTroika::OnRestore` (`0x102998c0`) filling it, and the
	// `CAISound::FUN_100aa5a0` this body opens with is `CBaseEntity::OnRestore`, whose whole body
	// tail-calls slot 6. `docs/vtmb/npc-ai/shape.md` § "Speech and the looping-sound stop" reads it
	// as a reaction to an AI sound; the section this family added supersedes that identification.
	//
	// The body, verbatim:
	//   1. the base `CBaseEntity::OnRestore` (the caller's, `FElysiumHint::OnPostRestore`);
	//   2. resolve the hint's own AI-network node — `0x102d3e60` bounds-checks `m_nNodeID`
	//      (`+0x5e4`) against `(*DAT_1093407c)` and indexes `DAT_1093407c[1]`, bumping
	//      `DAT_106c994c` for an id that is neither -1 nor inside the network;
	//   3. NO NODE: `DevMsg("Warning: AI hint has incorrect origin")` and return;
	//   4. a node: `Teleport(&node->origin /*+0x08..+0x10*/, NULL, NULL)` through vtable `+0x2d4`,
	//      then claim the node by writing `this` into its `CAI_Hint*` slot at `+0xa0`.
	//
	// Nothing about a node is saved (no `CAI_Node` datamap), so this is how a restored hint finds
	// its node again: its saved `m_nNodeID`. Step 4's teleport is the caller's -- it owns the
	// entity's runtime origin writer; the RAW origin, not `GetPosition`'s hull-lifted one.
	FHintRestoreResult Result;
	const int32 Node = Places.ResolveHintNode(Hint.NodeId);                    // 0x102d3ecc
	if (Node == INDEX_NONE)                                                    // 0x102d3ed3
	{
		// Step 3's `DevMsg` has no output device in this port (0019/6): the arm only returns.
		return Result;
	}
	Result.bNodeFound = true;
	Result.NodeIndex = Node;
	Result.NodeOriginCm = Places.Row(Node).OriginCm;                           // node +0x08..+0x10
	Places.SetAttachedHint(Node, HintHandle);                                  // node +0xa0 = this
	Result.bClaimedNode = true;
	return Result;
}

int32 FElysiumNpcBase::ActivityIdForName(const FString& ActivityName) const
{
	// `ActivityList_IndexForName` (`0x10412520`) over retail's registered enum, case-folded; -1 is
	// retail's own "not in the table" answer.
	return ElysiumRetailActivities::ValueOf(ActivityName);
}

void FElysiumNpcBase::SetMotorHintYaw(float Yaw)
{
	// `0x102e2020`'s tail after its yaw computation (`0x10014f0b`, done by the caller): the `+0x28`
	// animation-movement flip (`102e202d..102e205d`, `AND EAX,0x100`: below 180.0 or unordered adds
	// the half turn), then `102e2061 CMP [+0x1c],180.0f` -> `102e206e` the direct store into
	// `motor+0x34`. The clamped arm (`0x102e0a80`) needs the `+0x1c` max-yaw word no port motor
	// carries, so the direct store is the arm taken, as `NPCInit` / `0x102e1c10` take it. No
	// `UpdateYaw`: `0x102e2020` does not call it. `Yaw` is a RETAIL (Source) yaw. (Story 8 L05
	// integration: was an empty seam.)
	float Ideal = Yaw;
	if (BaseScheduleHost.bMotorAnimationMovement)
	{
		Ideal = !(Ideal >= MotorYawHalfTurn) ? Ideal + MotorYawHalfTurn : Ideal - MotorYawHalfTurn;
	}
	MotorIdealYaw = Ideal;                               // 102e206e motor+0x34
}

void FElysiumNpcBase::ReleaseMotorHintYaw()
{
	// `0x102e1e20(motor, -1)` — `UpdateYaw(-1)`, the turn toward `motor+0x34` the interest bodies
	// end with. (The claim's opening `0x102e0b40` is `MotorMoveStop()`, called at its site.) Wired
	// at story 8 wave 2: the navigator step keeps `motor+0x34` at the travel yaw while a route runs
	// (`NavigatorMoveStep`, retail's `MoveExecute`), so the word is live after a walk.
	MotorUpdateYaw(-1);
}

FVector FElysiumNpcBase::HintComparePosition(const FElysiumEntity* Entity) const
{
	// SEAM for the vtable `+0x370` accessor (slot 220). `0x103bfa50` uses it for BOTH sides of its
	// squared-distance compare, so answering the origin keeps the comparison self-consistent even
	// though the accessor itself is unidentified. **Unrecovered:** what slot 220 returns.
	return Entity ? Entity->Origin : FVector::ZeroVector;
}

void FElysiumNpcBase::SetHintGroup(const FString& NewHintGroup)
{
	// `0x102781e0`. Retail reads the old `m_strHintGroup` (`+0x5db0`), assigns the new one, and
	// dispatches slot 551 `OnChangeHintGroup(old, new)` — vtable `+0x89c`, `0x89c / 4 == 551` —
	// ONLY when the two differ. Both arguments are the string_t values, old first.
	const FString Old = BaseScheduleHost.HintGroup;
	BaseScheduleHost.HintGroup = NewHintGroup;
	if (!Old.Equals(NewHintGroup, ESearchCase::CaseSensitive))
	{
		// NAMED MODERNIZATION, one word wide: retail compares the two `string_t` POINTERS, so two
		// separately allocated strings with the same text would dispatch. VtMB interns keyfield
		// strings through `AllocPooledString`, which makes pointer equality text equality for every
		// authored path; the port compares the text.
		OnChangeHintGroup(FName(*Old), FName(*NewHintGroup));
	}
}

