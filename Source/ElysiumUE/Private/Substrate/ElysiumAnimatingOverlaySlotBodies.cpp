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
	// answering 0: no LOOPING bit (so `AddSceneEvent`'s gesture arm raises no vcd warning).
	(void)Sequence;
	return 0;
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

int32 FElysiumAnimatingOverlay::LookupPoseParameter(const TCHAR* Name) const
{
	// `CBaseAnimating::LookupPoseParameter(const char*)`. **SEAM**, answering -1. Family Facing's
	// `PoseParameterWrites` is where a pose-parameter write actually lands in this runtime.
	(void)Name;
	return INDEX_NONE;
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

int32 FElysiumAnimatingOverlay::FirstGestureLayerOrRefusal() const
{
	// `GetFirstGestureLayer()` (slot 267) answers 0 for every class in the hierarchy,
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

int32 FElysiumAnimatingOverlay::SelectWeightedSequenceForActivity(int32 Activity) const
{
	// `CBaseAnimating::SelectWeightedSequence(Activity, -1)` on the base line, `0x10295460` on the
	// Troika line. The sequence bridge (story 8 wave 2, a named modernization): a Troika body's
	// resolver numbers the clip it answers for the activity; any other body stands no sequence
	// table and answers retail's own -1.
	if (const FElysiumNpc* Npc = AsNpc())
	{
		return const_cast<FElysiumNpc*>(Npc)->SequenceForActivity(Activity);
	}
	return -1;
}
