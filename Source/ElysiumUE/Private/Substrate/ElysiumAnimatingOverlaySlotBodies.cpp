// `CBaseAnimatingOverlay`'s hand-written slot bodies and the helpers they reach (story 5 step 6),
// moved up the chain from `FElysiumNpcBase`. Declarations are generated in
// `ElysiumAnimatingOverlaySlots.inl` (a slot body) or in `ElysiumAnimatingOverlaySlotBodies.inl`.

#include "ElysiumAnimatingOverlay.h"
#include "ElysiumSaveArchive.h"

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

namespace
{
	// `CAnimationLayer::StudioFrameAdvance` `0x10098830`'s envelope gate: the FLOAT 0.95 at
	// `0x1045001c` (`FCOMP float ptr`, `0x100988d6` / `0x100988ed`).
	constexpr float GOverlayEnvelopeGate = 0.95f;
}

// --- Moved from `ElysiumNpcBaseAnim.cpp` (story 5 step 6) ---

int32 FElysiumAnimatingOverlay::SequenceFlagsOf(int32 Sequence) const
{
	// `CBaseAnimating::GetSequenceFlags(int)` / `GetSeqDesc(seq)->flags` (studio `+0x8`), from the
	// sequence bridge's row (spec 0002 V4o): bit 0 the row's own `STUDIO_LOOPING`, bit 1 its baked
	// SNAP bit. A body with no bridge has no descriptor to read: 0.
	if (const FElysiumNpc* const Npc = AsNpc())
	{
		return (Npc->SequenceLoops(Sequence) ? 0x1 : 0) | (Npc->SequenceSnaps(Sequence) ? 0x2 : 0);
	}
	return 0;
}

float FElysiumAnimatingOverlay::OverlaySequenceCycleRate(int32 Sequence) const
{
	// `GetSequenceCycleRate(owner, layer+8)` (`0x1009883e` -> `0x10091230`): the bridge row's.
	if (const FElysiumNpc* const Npc = AsNpc())
	{
		return Npc->SequenceCycleRateOf(Sequence);
	}
	return 0.f;
}

// --- The gesture layers' own bodies (spec 0002 V4o, lane O1) -----------------------------------

int32 FElysiumAnimatingOverlay::AllocateLayer()
{
	// `0x10099470`, slot 272: from slot 267 `GetFirstGestureLayer()` (`+0x42c`), the lowest slot
	// whose `m_flWeight` (`+0x748`) equals 0.0 (`_DAT_104454c4`); -1 when none, or when the first
	// gesture layer is 4 or more.
	for (int32 Index = GetFirstGestureLayer(); Index < ElysiumOverlay::NumSlots; ++Index)
	{
		if (Index >= 0 && AnimOverlay[Index].Weight == 0.f)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

void FElysiumAnimatingOverlay::SetLayer(int32 Index, int32 Activity, int32 Sequence, bool bAutoKill)
{
	// `0x10099020`, slot 268, the eleven writes in the listing's order. Retail indexes the table
	// unchecked; every caller hands an index `AllocateLayer` or `FindGestureLayer` answered.
	if (Index < 0 || Index >= ElysiumOverlay::NumSlots)
	{
		return;
	}
	FAnimOverlayLayer& Layer = AnimOverlay[Index];
	Layer.Activity = Activity;                          // 0x10099042 +0x758 m_nActivity
	Layer.Cycle = 0.f;                                  // 0x1009904f +0x740 m_flCycle
	Layer.PlaybackRate = 1.f;                           // 0x10099055 +0x744 m_flPlaybackRate = 1.0
	Layer.Sequence = Sequence;                          // 0x1009905f +0x73c m_nSequence
	Layer.BlendIn = ElysiumOverlay::DefaultBlendFraction;    // 0x10099065 +0x750 m_flBlendIn = 0.2
	Layer.BlendOut = ElysiumOverlay::DefaultBlendFraction;   // 0x1009906b +0x754 m_flBlendOut = 0.2
	Layer.Weight = ElysiumOverlay::SeedWeight;          // 0x10099075 +0x748 m_flWeight = 0.1
	Layer.WeightMax = 1.f;                              // 0x1009907f +0x74c m_flWeightMax = 1.0
	Layer.bAutoKillWhenFinished = bAutoKill;            // 0x10099089 +0x75c
	Layer.SequenceFinished = 0;                         // 0x1009908f +0x738 m_fSequenceFinished
	Layer.LastEventCheck = 0.f;                         // 0x10099095 +0x760 m_flLastEventCheck
	// `GetSeqDesc(seq)` (`0x1009909b`) non-null with `flags & 2` (`0x100990a4`): a SNAP sequence
	// takes no envelope. `m_fFlags` (+0x734) is not written.
	if ((SequenceFlagsOf(Sequence) & 0x2) != 0)
	{
		Layer.BlendIn = 0.f;                            // 0x100990aa
		Layer.BlendOut = 0.f;                           // 0x100990ac
	}
	// Not retail's: the body starts drawing the layer's clip (visual-only).
	OnOverlayLayerSet(Index);
}

void FElysiumAnimatingOverlay::RemoveLayer(int32 Index)
{
	// `0x10099660`, slot 269: `m_flWeight = 0`, then `m_nSequence = 0`; nothing else.
	if (Index < 0 || Index >= ElysiumOverlay::NumSlots)
	{
		return;
	}
	AnimOverlay[Index].Weight = 0.f;                    // +0x748
	AnimOverlay[Index].Sequence = 0;                    // +0x73c
}

bool FElysiumAnimatingOverlay::HasLayer(int32 Activity)
{
	// `0x10099540`, slot 270: slot 271 `FindGestureLayer` (`+0x43c`) `!= -1`.
	return FindLayerByOwner(Activity) != INDEX_NONE;
}

int32 FElysiumAnimatingOverlay::AddGesture(int32 Activity, bool bAutoKill)
{
	// `0x100991b0`, in its order.
	if (HasLayer(Activity))                                         // slot 270 `+0x438`
	{
		return FindLayerByOwner(Activity);                          // slot 271 `+0x43c`
	}
	const int32 Sequence = SelectWeightedSequenceForActivity(Activity);   // 0x1008dc40 (act, -1)
	if (Sequence < 1)                                               // `TEST EAX,EAX / JG`: `< 1` refuses
	{
		// Retail: `DevMsg("CBaseAnimatingOverlay::AddGesture: ... %s", activity name)`.
		UE_LOG(LogElysiumNpcEnt, Verbose,
			TEXT("CBaseAnimatingOverlay::AddGesture: model has no sequence for act %d"), Activity);
		return INDEX_NONE;
	}
	// `0x100990f0(seq, autokill)`: slot 272, then slot 268 `(i, -1, seq, autokill)`.
	const int32 Index = AllocateLayer();                            // slot 272 `+0x440`
	if (Index != INDEX_NONE)
	{
		SetLayer(Index, -1, Sequence, bAutoKill);                   // slot 268 `+0x430`
		AnimOverlay[Index].Activity = Activity;                     // `+0x758 = act`, after
	}
	return Index;
}

void FElysiumAnimatingOverlay::AdvanceOverlayLayers(float LayerInterval)
{
	// `CBaseAnimatingOverlay::StudioFrameAdvance` `0x10098bb0` past its call of the base body: the
	// four records in order, each on the interval the base returned (0.0 on its early-out).
	for (int32 Index = 0; Index < ElysiumOverlay::NumSlots; ++Index)
	{
		FAnimOverlayLayer& Layer = AnimOverlay[Index];
		if (Layer.Weight == 0.f)                                    // `m_flWeight != _DAT_104454c4`
		{
			continue;
		}
		// --- `CAnimationLayer::StudioFrameAdvance` `0x10098830` ---
		// `GetSequenceCycleRate(owner, seq) x m_flPlaybackRate x interval + m_flCycle`
		// (`0x1009883e..0x1009884a`), stored (`0x10098853`).
		const double AdvancedCycle = static_cast<double>(OverlaySequenceCycleRate(Layer.Sequence))
			* static_cast<double>(Layer.PlaybackRate) * static_cast<double>(LayerInterval)
			+ static_cast<double>(Layer.Cycle);
		Layer.Cycle = static_cast<float>(AdvancedCycle);
		if (AdvancedCycle < 0.0)                                        // 0x1009884d FCOM 0.0 (0x1044fab0)
		{
			// Below 0: no finish flag. `GetSequenceFlags(seq) & 1` (`0x10098865`) drops the integer
			// part (`0x1009886e..0x10098881`), else 0 (`0x10098886`).
			Layer.Cycle = (SequenceFlagsOf(Layer.Sequence) & 0x1) != 0
				? Layer.Cycle - static_cast<float>(static_cast<int32>(Layer.Cycle))
				: 0.f;
		}
		else if (!(AdvancedCycle < 1.0))                               // 0x1009888f FCOMP 1.0 (0x10449280)
		{
			Layer.SequenceFinished = 1;                             // 0x100988a4 layer+4
			// Looping drops the integer part (`0x100988b4..0x100988c7`), else 1.0 (`0x100988cc`).
			Layer.Cycle = (SequenceFlagsOf(Layer.Sequence) & 0x1) != 0
				? Layer.Cycle - static_cast<float>(static_cast<int32>(Layer.Cycle))
				: 1.f;
		}
		// The weight: 1.0 (`0x100988dc`), and the envelope only when a blend is under the FLOAT
		// 0.95 at `0x1045001c` (`0x100988d6` / `0x100988ed`).
		Layer.Weight = 1.f;
		if (Layer.BlendIn < GOverlayEnvelopeGate || Layer.BlendOut < GOverlayEnvelopeGate)
		{
			if (Layer.BlendIn != 0.f && Layer.Cycle < Layer.BlendIn)            // 0x100988fa / 0x1009890a
			{
				Layer.Weight = Layer.Cycle / Layer.BlendIn;                     // 0x10098917
			}
			// `1.0 - blendOut < cycle` (`0x10098930..0x1009893c`); the later write wins.
			if (Layer.BlendOut != 0.f
				&& 1.0 - static_cast<double>(Layer.BlendOut) < static_cast<double>(Layer.Cycle))
			{
				Layer.Weight = static_cast<float>(
					(1.0 - static_cast<double>(Layer.Cycle)) / static_cast<double>(Layer.BlendOut));   // 0x10098945
			}
			// `3w^2 - 2w^3` (`0x10098954..0x1009896c`, the double 3.0 at `0x10450010`).
			const double RawWeight = static_cast<double>(Layer.Weight);
			const double RawSquared = RawWeight * RawWeight;
			Layer.Weight = static_cast<float>(
				3.0 * RawSquared - (RawWeight * RawSquared + RawWeight * RawSquared));
		}
		if (Layer.WeightMax < Layer.Weight)                         // 0x10098971 the clamp
		{
			Layer.Weight = Layer.WeightMax;                         // 0x10098982
		}
		// --- back in `0x10098bb0` ---
		if (Layer.SequenceFinished != 0 && Layer.bAutoKillWhenFinished)
		{
			// The weight and nothing else: the sequence, the activity, the cycle and the cursor stay.
			Layer.Weight = 0.f;                                     // `piVar2[4] = 0`
			Slot112(Index, Layer.Activity);                         // `+0x1c0` slot 112 (empty on the NPC line)
		}
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

void FElysiumAnimatingOverlay::SerializeNativeOverlay(FElysiumSaveArchive& Ar)
{
	// 0x10098c80: twelve SAVE words per 0x30-byte layer; LastEventCheck is FLOAT.
	for (FAnimOverlayLayer& Layer : AnimOverlay)
	{
		Ar << Layer.Flags << Layer.SequenceFinished << Layer.Sequence;
		Ar << Layer.Cycle << Layer.PlaybackRate << Layer.Weight << Layer.WeightMax;
		Ar << Layer.BlendIn << Layer.BlendOut << Layer.Activity;
		Ar << Layer.bAutoKillWhenFinished << Layer.LastEventCheck;
	}
	// 0x1008df10 / slot265: retain every landed flinch word; no slot141 producer is invented.
	for (FFlinchRecord& Reaction : Flinch)
	{
		Ar << Reaction.Sequence << Reaction.Latch << Reaction.FadeIn << Reaction.FadeOut;
		Ar << Reaction.PoseParamIndex << Reaction.PoseParamValue;
		Ar.Time(Reaction.ExpireTime); // +0x80c/+0x828/+0x844 TIME SAVE, 0x101a0a80
	}
}

// V6 slot78 hand body (0x102c1ec0); the lane manifest admits this hand SlotBodies file.

void FElysiumNpc::ScriptUnhide()
{
	// 0x102c1ec0: four getter dispatches precede base; no floor or ground write.
	if (bCineScriptHidden && ScriptOwnerIsLive())
	{
		(void)GetMoveType(); (void)GetMoveCollide(); (void)GetSolid(); (void)GetSolidFlags();
	}
	const bool bWasScriptHidden = bHidden;
	FElysiumEntity::ScriptUnhide(); // 0x100a8990 callback due NOW, dormancy hook supplies slot614 once
	if (!bWasScriptHidden) ResetThinkTimers(World != nullptr ? World->NowSeconds() : 0.0); // 0x102c1ec0 unconditional slot614
	TroikaScriptUnhideTail(); // weapon unhide, six-word cine handback, latch clear
}
