// Story 0019/8 (29e under the strict verdict), family **Script19** -- `CAI_BaseNPC`'s bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Declarations are in `ElysiumNpcBaseScript19.inl` (included inside `class FElysiumNpcBase`) or
// generated in `ElysiumNpcBaseSlots.inl` for a slot body. A `STORY8-FORWARD` block is the generated
// stub moved here unchanged (its overlay row reads `hand:`); the porter replaces the body and drops
// the marker.
//
// Owns (Script19's `rule` rows): 0x1027d0a0 FUN_1027d0a0. Walked prose:
// `docs/vtmb/npc-ai/story8/Script19.md`.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumScriptedSequence.h"

namespace
{
	// `NPC_STATE_DEAD`, the ideal state `0x1027d0a0` writes (`0x1027d0bd`). Named apart from
	// `ElysiumNpcScript19.cpp`'s twin constant: both files share one unity blob.
	constexpr int32 GScript19BaseNpcStateDead = 7;
}

// `0x1027d0a0`, 153 bytes, 46 instructions. Three returns: `0x1027d0c9` (false), `0x1027d12d` and
// `0x1027d138` (true).
bool FElysiumNpcBase::ExitScriptedSequence()
{
	if (LifeStateIsDying())                                    // 0x1027d0a0 CMP [+0x200],1 / 0x1027d0a7 JNZ
	{
		// `+0x1b3c = "AI_BaseNPC.cpp"`, `+0x1b40 = 0x2a70` (the selector trace, carried by the mind's
		// transition trace) and `m_IdealNPCState (+0x5cc4) := 7`. The script stays installed.
		RequestIdealStateRetail(GScript19BaseNpcStateDead, ExitScriptDyingLine); // 0x1027d0a9..0x1027d0bd
		return false;                                          // 0x1027d0c7 XOR AL,AL / 0x1027d0c9
	}
	// `m_hCine (+0x5d74)` resolved: -1, a stale serial or a null slot all skip the call and answer
	// TRUE (`0x1027d0d4` / `0x1027d0f6` / `0x1027d0fc` -> `0x1027d135`). The re-read at
	// `0x1027d0fe..0x1027d121` (its -1 test `0x1027d107 JZ 0x1027d12e`) whose failure would call
	// `CancelScript(NULL)` (`0x1027d12e` /
	// `0x1027d130`) cannot fail: nothing runs between the two reads of the same handle.
	if (FElysiumScriptedSequence* Cine = ResolveCine())       // 0x1027d0ca..0x1027d0fc
	{
		Cine->CancelScript();                                  // 0x1027d123 / 0x1027d125 CancelScript 0x101a8c30
		return true;                                           // 0x1027d12a / 0x1027d12d
	}
	return true;                                               // 0x1027d135 / 0x1027d138
}

bool FElysiumNpcBase::LifeStateIsDying() const
{
	// `m_lifeState == LIFE_DYING` on the word itself (story 8 wave 2: `Event_Killed` `0x1032b9b0`
	// writes it); `Kill` removes the entity (`bDead`), which retail's `UTIL_Remove` does.
	return AnimEventLifeStateWord == 1 && !IsDead();
}
