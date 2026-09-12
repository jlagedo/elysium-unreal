#pragma once

#include "CoreMinimal.h"

class FElysiumNpc;
struct FElysiumNpcScheduleHost;

// VtMB's NPC think cadence: four independent clocks, four interval laws, one due test.
//
// A `CAI_BaseNPCTroika` carries four deadlines and their four mirrors --
// `m_flNextUpdate/Normal/Move/AIThink` at `+0x6244..+0x6250` and `m_flLast*Think` at
// `+0x6254..+0x6260` -- and `NPCThink` (`0x10292de0`) runs different work off each. The normal
// clock is the master gate: nothing in the body runs unless it is due. The AI clock decides
// whether that body runs in FULL (`GatherConditions`, ten task completions) or REDUCED (no
// gathering, one completion). The update clock drives `UpdateCharacter` alone. The move clock is
// written and never read: its due test `0x102906e0` is a bare `return true`.
//
// The entity's own next think is `min(NextUpdate, NextNormal)`, so the two fastest clocks decide
// when the body is woken and the other two decide what happens once it is.
//
// **The laws read no NPC state.** Not the schedule, not the enemy, not `m_NPCState`. Their inputs
// are the distance to the closest player, the PVS and LOS bytes `SetPlayerLOS` (`0x10291610`)
// caches, the `SCHEDULE_CHANGED` bit, the frenzy word, and `ShouldThinkFrequently()`. State
// changes the rate only through those. There are no `ai_think_*` cvars.
//
// Oracle: `docs/vtmb/npc-ai-reverse-engineering.md` -> "The think cadence, decoded".
namespace ElysiumNpcThink
{
	// A corpse's death program is not on any of the four clocks -- retail's dead body carries no
	// think at all -- so this is the one interval in the NPC leaf that is not derived from a stamp.
	// 0.1 s because the handoff to physics happens at the program's end and a slower poll is
	// visible as a corpse hanging on its last animated frame.
	inline constexpr double DeadProgramPollSeconds = 0.1;

	// `IsThinkDue` `0x10290660`. Equality IS due: the compare is `FCOMP` + `TEST AH,0x41`.
	inline bool IsDue(double Stamp, double Now, double FrameSeconds)
	{
		return (Stamp - Now) <= FrameSeconds;
	}

	// Everything the four laws read, gathered once. Distances are in SOURCE UNITS, because the
	// recovered constants (512, 704, 896, 2048, 4096) are.
	struct FInputs
	{
		bool  bHasClosestPlayer = false;
		float PlayerDistUnits = 0.f;
		bool  bInPlayerPvs = true;
		bool  bInPlayerLos = true;
		bool  bScheduleChanged = false;
		bool  bThinkFrequently = false;
		// `m_bfNPCFrenziedFlags & 0x8`. Both 16c discipline arms set it; nothing else does, so it
		// reads false until that story lands.
		bool  bAlwaysInPlayerView = false;
	};
	FInputs GatherInputs(const FElysiumNpc& Npc);

	// `ShouldThinkFrequently()` `0x102c2430`.
	bool ShouldThinkFrequently(const FElysiumNpc& Npc);

	// The four laws. Each is SELF-GATING: it returns without writing anything unless its own stamp
	// is due, which is what stops a think that fired on one clock from advancing the others.
	void CalcNextUpdateThink(FElysiumNpcScheduleHost& Host, const FInputs& In, double Now,
		double FrameSeconds);
	void CalcNextNormalThink(FElysiumNpcScheduleHost& Host, const FInputs& In, double Now,
		double FrameSeconds);
	void CalcNextAiThink(FElysiumNpcScheduleHost& Host, const FInputs& In, double Now,
		double FrameSeconds);
	// The exception: no gate at all (`0x10290fc0` calls no `IsThinkDue`), so it always writes.
	void CalcNextMoveThink(FElysiumNpcScheduleHost& Host, double Now);

	// The intervals the three gated laws would choose right now, exposed so a case can assert a
	// law without also asserting the stamp bookkeeping around it.
	double UpdateInterval(const FInputs& In);
	double NormalInterval(const FInputs& In);
	double AiInterval(const FInputs& In);
}
