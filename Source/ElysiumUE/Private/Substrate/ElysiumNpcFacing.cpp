#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcFacingShared.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumSkeletalBasis.h"
#include "Substrate/ElysiumNpcKernelShape.h"

// Story 29c-1, family **Facing** — the facing-target queue and the turn-activity ladder.
//
// 44 rows of `order.md` layers 0–9, ported arm by arm in retail's order. Every threshold below was
// read out of the pinned retail `vampire.dll`'s `.rdata` at its cited address (image base
// `0x10000000`, `.rdata` RVA == file offset in this image), so the numbers are recovered facts, not
// estimates. The walked prose is `docs/vtmb/npc-ai/shape.md`.
//
// Two coordinate facts govern every angle here and are stated once:
//
//   * `FElysiumEntity::Angles` is **Source** `[pitch yaw roll]` in degrees, exactly as retail's
//     `GetAngles()` (slot 221) answers it, while `Origin` is this world's axes, where
//     `bsp.source_to_unreal` has negated Y. A yaw derived from a port-space delta is therefore the
//     NEGATED Unreal one, and the ladders are written against retail's sign, in which a positive
//     yaw delta turns LEFT. `RetailYawOf` below is the one place that conversion happens.
//   * Retail's distance constants are Source units; this world is centimetres. `ElysiumMove::U` is
//     the 2.54 that bridges them, applied at the point of use so the recovered constant stays
//     visible.
// --- The `CAI_Motor` seam -----------------------------------------------------------------------

bool FElysiumNpc::TurningAnimsEnabled() const
{
	// The retail cvar at `0x109247ec`, the same `ConVar::GetBool()` shape, ORed with
	// `m_bAllowTurningAnims` by `SetTurnActivity` and read by both `MaxYawSpeed` overrides:
	// `debug_turning`, shipped "0", so the authored `m_bAllowTurningAnims` (+0x65f9) is the live gate.
	return ElysiumNpcTunables::ConVarInt(ElysiumNpcTunables::EConVar::DebugTurning) != 0;
}

// --- Slots 517/518/519 `AddFacingTarget` --------------------------------------------------------
//
// All three are the same 71-byte shape: the cvar gate, then a TAIL JUMP into `m_pMotor` (+0x5d44)
// with `this` swapped for the motor and the arguments untouched. 29c's walk read the frame fix-up
// after `POP ESI` as an argument shift; the listing of the dead debug twin (0019/6) shows the four stack slots
// written back to themselves, so nothing shifts.

// --- Slot 520 `GetFacingDirection`, slot 526 `OverrideMoveFacing` --------------------------------

// --- Slots 372/373 `EyeDirection2D` / `EyeDirection3D` -------------------------------------------

// --- Slot 572 `SetTurnActivity` -----------------------------------------------------------------

FElysiumNpcBase::FTurnActivityPick FElysiumNpc::TurnActivityTroikaLadder(float YawDelta,
	TFunctionRef<bool(int32)> HasSequence)
{
	// `CAI_BaseNPCTroika::SetTurnActivity` `0x10297640`, the body slot 572 carries for every
	// spawnable species. Wider bands than the base, two rungs the base has no equivalent of, and the
	// 180 rungs are ORDERED PAIRS — past 140 degrees retail asks for the turn that matches the sign
	// first and accepts the other one if the body does not author it.
	//
	// `_DAT_1049ae40 = -70.0f`, `_DAT_1049ae3c = -140.0f`, `_DAT_104528d4 = 70.0f`,
	// `_DAT_1049ae38 = 140.0f`, `_DAT_1049ae34 = -15.0f`, `_DAT_10463584 = 15.0f`.
	if (YawDelta < ElysiumNpcTunables::MinusSeventy && ElysiumNpcTunables::MinusOneForty <= YawDelta && HasSequence(0xa2))
	{
		return FTurnActivityPick{ 0xa2, true };   // ACT_90_RIGHT
	}
	if (ElysiumNpcTunables::Seventy <= YawDelta && YawDelta < ElysiumNpcTunables::OneForty && HasSequence(0xa1))
	{
		return FTurnActivityPick{ 0xa1, true };   // ACT_90_LEFT
	}
	if (YawDelta < ElysiumNpcTunables::MinusOneForty)
	{
		if (HasSequence(0x9e))
		{
			return FTurnActivityPick{ 0x9e, true };   // ACT_180_RIGHT
		}
		if (HasSequence(0x9d))
		{
			return FTurnActivityPick{ 0x9d, true };   // ACT_180_LEFT
		}
	}
	if (ElysiumNpcTunables::OneForty < YawDelta)
	{
		if (HasSequence(0x9d))
		{
			return FTurnActivityPick{ 0x9d, true };
		}
		if (HasSequence(0x9e))
		{
			return FTurnActivityPick{ 0x9e, true };
		}
	}
	if (YawDelta < ElysiumNpcTunables::MinusFifteen && HasSequence(0x3c))
	{
		return FTurnActivityPick{ 0x3c, true };   // ACT_TURN_RIGHT — tagged here, unlike the base
	}
	if (ElysiumNpcTunables::Fifteen <= YawDelta && HasSequence(0x3b))
	{
		return FTurnActivityPick{ 0x3b, true };   // ACT_TURN_LEFT
	}
	return FTurnActivityPick{ 1, false };         // ACT_IDLE
}

void FElysiumNpc::SetTurnActivity()
{
	// Slot 572's Troika-line body, `0x10297640`. The gate is `cvar(0x109247ec) ||
	// m_bAllowTurningAnims`, and when it is closed the body still falls through to the `ACT_IDLE`
	// tail — the turn is simply never animated.
	if (TurningAnimsEnabled() || bAllowTurningAnims)
	{
		const FTurnActivityPick Pick = TurnActivityTroikaLadder(MotorDeltaIdealYaw(),
			[this](int32 Activity) { return SelectWeightedSequenceForActivity(Activity) != -1; });
		if (Pick.Activity != 1)
		{
			if (Pick.bTagsTurnMemory)
			{
				BaseScheduleHost.MemoryBits |= 0x2000;   // +0x5d8c m_afMemory
			}
			SetIdealActivityNumber(Pick.Activity);
			return;
		}
	}
	SetIdealActivityNumber(1);
}

// --- The facing readers and writers that fill no slot --------------------------------------------

