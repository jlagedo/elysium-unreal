// `CBaseAnimatingOverlay`'s hand-written slot bodies and the helpers they reach (story 5 step 6),
// moved up the chain from `FElysiumNpcBase`. Declarations are generated in
// `ElysiumAnimatingOverlaySlots.inl` (a slot body) or in `ElysiumAnimatingOverlaySlotBodies.inl`.

#include "ElysiumAnimatingOverlay.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumOverlayStack.h"
#include "ElysiumRng.h"
#include "ElysiumSkeletalBasis.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcAnimShared.h"
#include "Substrate/ElysiumNpcBase.h"
#include "Substrate/ElysiumNpcBaseEntityChainShared.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEntityChainShared.h"
#include "Substrate/ElysiumNpcFacingShared.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumSceneData.h"
#include "Substrate/ElysiumSchedule.h"

// --- Moved from `ElysiumNpcBaseAnim.cpp` (story 5 step 6) ---

int32 FElysiumAnimatingOverlay::SequenceFlagsOf(int32 Sequence) const
{
	// `CBaseAnimating::GetSequenceFlags(int)` / `GetSeqDesc(seq)->flags` (studio `+0x8`). **SEAM**,
	// answering 0: no LOOPING bit (so `AddSceneEvent`'s gesture arm raises no vcd warning) and no
	// SNAP bit (so `SetOverlayLayer` keeps retail's 0.2 envelope).
	(void)Sequence;
	return 0;
}

void FElysiumAnimatingOverlay::SetOverlayLayer(int32 SlotIndex, int32 Activity, int32 Sequence, bool bAutoKill)
{
	// `CBaseAnimatingOverlay::SetLayer` `0x10099020`, field for field and in retail's own write
	// order. The four numbers are `ElysiumOverlay::`'s, which is where the render-side stack reads
	// the same constants from — one spelling, so the two tables cannot drift.
	if (SlotIndex < 0 || SlotIndex >= ElysiumOverlay::NumSlots)
	{
		return;
	}
	FAnimOverlayLayer& Layer = AnimOverlay[SlotIndex];
	Layer.Activity = Activity;                                  // +0x24
	Layer.Cycle = 0.f;                                          // +0x0c
	Layer.PlaybackRate = 1.f;                                   // +0x10
	Layer.Sequence = Sequence;                                  // +0x08
	Layer.BlendIn = ElysiumOverlay::DefaultBlendFraction;       // +0x1c
	Layer.BlendOut = ElysiumOverlay::DefaultBlendFraction;      // +0x20
	Layer.Weight = ElysiumOverlay::SeedWeight;                  // +0x14 — the occupancy marker
	Layer.WeightMax = ElysiumOverlay::WeightMax;                // +0x18
	Layer.bAutoKillWhenFinished = bAutoKill;                    // +0x28
	Layer.SequenceFinished = 0;                                 // +0x04
	Layer.LastEventCheck = 0.f;                                 // +0x2c
	// `m_fFlags` (+0x00) is NOT written: retail leaves it alone, and so does this.

	// The envelope comes from the SEQUENCE, never from the pusher: a SNAP sequence (studio
	// `flags@8 & 0x2`) gets no blend at all and stands at full weight from the frame it is armed.
	if ((SequenceFlagsOf(Sequence) & 0x2) != 0)
	{
		Layer.BlendIn = 0.f;
		Layer.BlendOut = 0.f;
	}
}

int32 FElysiumAnimatingOverlay::FindGestureLayerByOwner(int32 Activity) const
{
	// `CBaseAnimatingOverlay::FindGestureLayer` `0x100994c0`, slot 271's body. Three terms in
	// retail's own order: the slot is LIVE (`m_flWeight != 0`), its owner is not `ACT_INVALID` (-1),
	// and its owner is the activity asked for. -1 when nothing matches, which is the "found"
	// convention slots 270 and 274 both test against.
	//
	// Stood as a named method beside slot 271's own stub, which is 29c's and answers 0 — the
	// opposite of retail's miss. Every row of this family that searches the table goes through here.
	for (int32 Index = 0; Index < ElysiumOverlay::NumSlots; ++Index)
	{
		const FAnimOverlayLayer& Layer = AnimOverlay[Index];
		if (Layer.Weight != 0.f && Layer.Activity != -1 && Layer.Activity == Activity)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 FElysiumAnimatingOverlay::AllocateGestureLayer() const
{
	// `CBaseAnimatingOverlay::AllocateLayer` `0x10099470`, slot 272's body: the lowest slot whose
	// weight is exactly zero, starting at `GetFirstGestureLayer()` — which answers 0 for every class
	// in the hierarchy, so no slot is reserved. -1 when all four are held; there is no eviction and
	// no priority displacement.
	for (int32 Index = 0; Index < ElysiumOverlay::NumSlots; ++Index)
	{
		if (AnimOverlay[Index].Weight == 0.f)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

bool FElysiumAnimatingOverlay::HasLayer(int32 Activity)
{
	// `0x10099540`, slot 270: the whole 22-byte body is the lookup and a `!= -1`.
	return FindGestureLayerByOwner(Activity) != INDEX_NONE;
}

void FElysiumAnimatingOverlay::RemoveLayer(int32 SlotIndex)
{
	// `0x10099660`, slot 269: two writes, in retail's own order — the WEIGHT first (index
	// `i*0xc + 10` off `m_angPrevSeqAngles`, i.e. +0x0748 on layer 0) and then the SEQUENCE (index
	// `i*0xc + 7`, +0x073c). Nothing else on the record is touched, so the owner activity survives a
	// removal; zeroing the weight is what frees the slot, because occupancy IS the zero-weight test.
	if (SlotIndex < 0 || SlotIndex >= ElysiumOverlay::NumSlots)
	{
		return;
	}
	AnimOverlay[SlotIndex].Weight = 0.f;
	AnimOverlay[SlotIndex].Sequence = 0;
}

void FElysiumAnimatingOverlay::RemoveLayerByOwner(int32 Activity)
{
	// `0x100995e0`, slot 274: the same two writes as slot 269, behind the owner lookup. It is NOT a
	// call to slot 269 — retail inlines the pair — but the effect is identical and the port says so
	// by going through the one spelling.
	const int32 Index = FindGestureLayerByOwner(Activity);
	if (Index != INDEX_NONE)
	{
		AnimOverlay[Index].Weight = 0.f;
		AnimOverlay[Index].Sequence = 0;
	}
}

void FElysiumAnimatingOverlay::RemoveAllGestures()
{
	// `0x10099630`, slot 275: four iterations over the record stride, zeroing `m_nSequence` (+0x00 of
	// the pair) and `m_flWeight` (+0x0c past it). The bound is FOUR because `m_Flinch[0]` begins
	// exactly where a fifth record would.
	for (FAnimOverlayLayer& Layer : AnimOverlay)
	{
		Layer.Weight = 0.f;
		Layer.Sequence = 0;
	}
}

void FElysiumAnimatingOverlay::RestartGesture(int32 Activity, bool bAddIfMissing, bool bAutoKill)
{
	// `0x10099570`, slot 273, read off the listing rather than the decompilation (Ghidra bound the
	// `addifmissing` test to `param_1`; `MOV AL, [ESP+0x10]` says it is `param_2`):
	//   layer = FindLayerByOwner(activity)
	//   if (layer != -1)  m_AnimOverlay[layer].m_flCycle = 0        // rewind, keep the weight
	//   else if (addifmissing) AddGesture(activity, autokill)
	//
	// The rewind writes the CYCLE and nothing else, so a restarted gesture keeps its envelope and its
	// accumulated weight and simply plays again from the head.
	const int32 Index = FindGestureLayerByOwner(Activity);
	if (Index != INDEX_NONE)
	{
		AnimOverlay[Index].Cycle = 0.f;
		return;
	}
	if (!bAddIfMissing)
	{
		return;
	}

	// `CBaseAnimatingOverlay::AddGesture` `0x100991b0`: `HasLayer` first (which cannot answer true
	// here — the lookup above already missed), then `SelectWeightedSequence(activity)`, and a
	// sequence of **1 or less** is the refusal, not just -1. `AllocateLayer` then takes the lowest
	// zero-weight slot, and a full stack refuses outright.
	const int32 Sequence = SelectWeightedSequenceForActivity(Activity);
	if (Sequence < 1)
	{
		// Retail's `DevMsg("CBaseAnimatingOverlay::AddGesture : unable to find %s\n", …)`.
		UE_LOG(LogElysiumNpcEnt, Verbose,
			TEXT("CBaseAnimatingOverlay::AddGesture : unable to find activity %d"), Activity);
		return;
	}
	const int32 NewSlot = AllocateGestureLayer();
	if (NewSlot == INDEX_NONE)
	{
		return;
	}
	SetOverlayLayer(NewSlot, Activity, Sequence, bAutoKill);
}

void FElysiumAnimatingOverlay::AddFlinchGesture(int32 Activity, float FadeIn, float FadeOut,
	const TCHAR* PoseParameter, float PoseValue)
{
	// `0x10099690`, slot 265. Two gates, then the victim scan, then the write.
	//
	// `IsAlive()` (slot 158, vtable +0x278) and `m_bNoFlinch` (+0x0730) are BOTH refusals, and their
	// order is retail's: a dead body never flinches whatever the flag says.
	if (!IsAlive() || bNoFlinch)
	{
		return;
	}

	// The earliest-expiring of the three records wins, scanning 1 and 2 against the running best and
	// starting at 0 — so with three equal stamps record 0 is reused, and a strictly-earlier later
	// record displaces it.
	int32 Best = 0;
	for (int32 Candidate = 1; Candidate < NumFlinchRecords; ++Candidate)
	{
		if (Flinch[Candidate].ExpireTime < Flinch[Best].ExpireTime)
		{
			Best = Candidate;
		}
	}

	const int32 Sequence = SelectWeightedSequenceForActivity(Activity);
	if (Sequence < 0)
	{
		// No clip for the flinch activity: the record is left exactly as it was, stamp included.
		return;
	}

	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	FFlinchRecord& Record = Flinch[Best];
	Record.Sequence = Sequence;                       // +0x00
	Record.FadeOut = FadeOut;                         // +0x0c
	Record.PoseParamIndex = 0x18;                     // +0x10 — seeded, then overwritten below
	Record.FadeIn = FadeIn;                           // +0x08
	Record.Latch = (Record.Latch + 1) & 3;            // +0x04 — retail's own 2-bit rotation
	Record.ExpireTime = FadeIn + static_cast<float>(Now) + FadeOut;   // +0x18

	// The pose parameter is OPTIONAL and is the last thing written; a name that resolves to nothing
	// leaves the seeded 0x18 standing, which is retail's behaviour and not a fallback this port adds.
	if (PoseParameter != nullptr)
	{
		const int32 Index = LookupPoseParameter(PoseParameter);
		if (Index >= 0)
		{
			Record.PoseParamIndex = Index;
			Record.PoseParamValue = NormalizePoseParameter(Index, PoseValue);
		}
	}
}

int32 FElysiumAnimatingOverlay::LookupPoseParameter(const TCHAR* Name) const
{
	// `CBaseAnimating::LookupPoseParameter(const char*)`. **SEAM**, answering -1. Family Facing's
	// `PoseParameterWrites` is where a pose-parameter write actually lands in this runtime.
	(void)Name;
	return INDEX_NONE;
}

float FElysiumAnimatingOverlay::NormalizePoseParameter(int32 Index, float Value) const
{
	// `0x100c43e0(studiohdr, index, value, &out)` — the studio pose-parameter range normaliser
	// `AddFlinchGesture` runs its authored value through. **SEAM**: with no studio header there is
	// no range, so the identity is what a 0..1 parameter would have given anyway.
	(void)Index;
	return Value;
}

// --- Moved from `ElysiumNpcBaseEntityChain.cpp` (story 5 step 6) ---

void FElysiumAnimatingOverlay::Slot266()
{
	// 0x100997f0, slot 266 — clear the three flinch records at `+0x07f4` (stride 0x1c). Two writes
	// per record and no more: the SEQUENCE to -1 and the EXPIRE TIME (word 6, `+0x18`) to
	// `curtime - 1.0`. The latch, the two fades and the pose parameter survive, which is why a
	// cleared record still remembers which pose parameter it drove.
	//
	// `_DAT_104454c0` is the image's shared `1.0f`, so the stamp is one second in the PAST: already
	// expired, on purpose.
	const float Now = World != nullptr ? static_cast<float>(World->NowSeconds()) : 0.f;
	for (int32 Index = 0; Index < NumFlinchRecords; ++Index)
	{
		Flinch[Index].Sequence = -1;
		Flinch[Index].ExpireTime = Now - NpcBaseEntityChainShared::GChainOne;
	}
}

void FElysiumAnimatingOverlay::SetLayer(int32 SlotIndex, int32 Activity, int32 Sequence, bool bAutoKill)
{
	// 0x10099020, slot 268 — `CBaseAnimatingOverlay::SetLayer`. Family Anim already ported this body
	// field for field as `SetOverlayLayer` (`ElysiumNpcAnim.cpp`), because its own rows
	// dispatch through it and could not do so against a stub. This is the SLOT, and it is that body:
	// one spelling, so the two cannot drift.
	SetOverlayLayer(SlotIndex, Activity, Sequence, bAutoKill);
}

int32 FElysiumAnimatingOverlay::FirstGestureLayerOrRefusal() const
{
	// `GetFirstGestureLayer()` (slot 267, `0x10098a40`) answers 0 for every class in the hierarchy,
	// which family Anim recovered and every scan in that family relies on. Stated here rather than
	// dispatched, because slot 267's own port body is still 29c's stub and dispatching it would
	// report a stub for a fact that is already recovered.
	return 0;
}

int32 FElysiumAnimatingOverlay::FindLayerByOwner(int32 Activity)
{
	// 0x100994c0, slot 271 — `CBaseAnimatingOverlay::FindGestureLayer`. The scan STARTS at
	// `GetFirstGestureLayer()` and refuses outright when that is 4 or more; each slot is tested on
	// three terms in retail's order — the weight is not zero (`_DAT_104454c4`), the owner is not
	// `ACT_INVALID` (-1), and the owner is the activity asked for. -1 on a miss.
	//
	// Family Anim carries the scan as `FindGestureLayerByOwner`; this is the slot, and it is that
	// body behind retail's own starting index.
	if (FirstGestureLayerOrRefusal() >= ElysiumOverlay::NumSlots)
	{
		return INDEX_NONE;
	}
	return FindGestureLayerByOwner(Activity);
}

// --- Moved from `ElysiumNpcBaseFacing.cpp` (story 5 step 6) ---

int32 FElysiumAnimatingOverlay::SelectWeightedSequenceForActivity(int32) const
{
	// `CBaseAnimating::SelectWeightedSequence(Activity, -1)` on the base line, `0x10295460` on the
	// Troika line. **SEAM**: this substrate resolves activities by NAME and stands no sequence
	// index at the kernel tier, so nothing is authored and the ladder walks to its `ACT_IDLE` tail —
	// retail's own answer for a body with no turn clips.
	return -1;
}

// --- Moved from `ElysiumNpcBaseMisc.cpp` (story 5 step 6) ---

// -------------------------------------------------------------------------------------------------
// Slot 272 `AllocateLayer` — `0x10099470`.
// -------------------------------------------------------------------------------------------------

int32 FElysiumAnimatingOverlay::AllocateLayer()
{
	// `0x10099470`. The listing is the authority here, because the decompilation types the array as
	// `m_angPrevSeqAngles`: `EDX = (i + i*2) << 4` then `+ ESI + 0x748`, stride `0x30` — the gesture
	// layer table at `+0x0734` with `m_flWeight` at `+0x14` inside each 0x30-byte record. The scan
	// starts at `GetFirstGestureLayer()` (slot 267, `0x10098a40`), runs while the index is below 4,
	// and answers the first slot whose weight equals `_DAT_104454c4` (0.0f), else -1.
	//
	// Family **Anim** already carries that table and landed this exact body as
	// `AllocateGestureLayer()` beside slot 272's stub, precisely so its rows would not search a stub
	// that answers 0 for "not found". The slot now forwards to it; there is one scan, not two.
	return AllocateGestureLayer();
}
