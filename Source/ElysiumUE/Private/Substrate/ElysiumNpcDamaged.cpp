// Story 0019/8 (29e under the strict verdict), family **Damaged19** -- `CAI_BaseNPCTroika`'s
// bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Declarations are in `ElysiumNpcDamaged.inl` (included inside `class FElysiumNpc`) or generated
// in `ElysiumNpcSlots.inl` for a slot body. A `STORY8-FORWARD` block is the generated stub moved
// here unchanged (its overlay row reads `hand:`); the porter replaces the body and drops the
// marker.
//
// Owns (Damaged19's `rule` rows): 0x102c1ce0 CAI_BaseNPCTroika::ScriptHide ‼.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumScheduleNumbers.h"
#include "Substrate/ElysiumScriptedSequence.h"

namespace
{
	constexpr int32 GDamaged19NpcStateScript = 4;   // `NPC_STATE_SCRIPT`, `0x102c1cea CMP [+0x5cc0],4`
	constexpr int32 GDamaged19NpcStateDead = 7;     // `NPC_STATE_DEAD`, `0x102c1e17 CMP [+0x5cc0],7`
}

// Slot 77 `CAI_BaseNPCTroika::ScriptHide` `0x102c1ce0`, 369 bytes, 112 instructions. No jump table:
// the decompiler's "indirect jump" is the tail `JMP [vtbl+0x134]` at `0x102c1e48`. `m_hCine`
// (`+0x5d74`) is `ScriptOwner`, resolved through the handle table (`0x10566458`: -1, a stale serial
// and a null slot all fail) FOUR times; nothing runs between the first and the second or between
// the third and the fourth, so the re-resolves' failure arms are dead: the `GetDebugName(NULL)` name
// (`0x102c1d5c` / `0x102c1d73` -> `0x102c1d7e` / `0x102c1d80`) and `CancelScript(NULL)`
// (`0x102c1de5` / `0x102c1dfc` -> `0x102c1e07` / `0x102c1e09`). The two warnings print through
// `[0x109f364c]` (`0x102c1d94`), the tier0 import `ElysiumNpcDamage3.cpp` names DevWarning.
void FElysiumNpc::ScriptHide()
{
	FElysiumEntity* const CineEntity =
		World != nullptr && ScriptOwner.IsSet() ? World->Resolve(ScriptOwner) : nullptr;
	// The warn gate: SCRIPT state (`0x102c1cea` / `0x102c1cf1 JZ 0x102c1d29`), else a LIVE `m_hCine`
	// (`0x102c1cfc` -1 / `0x102c1d1a` serial / `0x102c1d23` null -> `0x102c1e29`, the base half).
	if (NpcStateRetail() == GDamaged19NpcStateScript || CineEntity != nullptr)   // 0x102c1cf1 / 0x102c1d23
	{
		// 0x102c1d32..0x102c1da7: two `Warning()` prints (the cine's `GetDebugName` or `**UNKNOWN**`,
		// `0x105477a4`) -- dead, no output device (0019/6).
		// The third resolve (`0x102c1da9..0x102c1dda`): a live cine is cancelled
		// (`CancelScript` `0x101a8c30`), a dead handle cleans up THIS NPC (`CineCleanup` `0x1027d170`).
		if (FElysiumScriptedSequence* Cine = ResolveCine())             // 0x102c1db5 / 0x102c1dd5 / 0x102c1dda
		{
			Cine->CancelScript();                                       // 0x102c1dfe / 0x102c1e00
		}
		else
		{
			FElysiumScriptedSequence::CineCleanup(*this);               // 0x102c1e10 / 0x102c1e12
		}
		// Both converge at `0x102c1e17`: unless DEAD, `0x102ae7f0(0x6b)`, whose whole body is
		// `m_iForcedSchedule (+0x65c8) := param` -- it installs nothing.
		if (NpcStateRetail() != GDamaged19NpcStateDead)                 // 0x102c1e17 / 0x102c1e1e
		{
			ScheduleHost.ForcedSchedule = ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION;   // 0x102c1e20 / 0x102c1e24
		}
	}
	// The base half, on every path: `CBaseEntity::ScriptHide` `0x100a8710`, called by address.
	FElysiumEntity::ScriptHide();                                       // 0x102c1e29 / 0x102c1e2b
	// `GetActiveWeapon` (`0x1032e7b0`) twice: the null test, then the receiver of slot 77.
	if (FElysiumEntity* Weapon = ActiveWeaponEntity())                  // 0x102c1e32 / 0x102c1e39
	{
		Weapon->ScriptHide();                                           // 0x102c1e3d / 0x102c1e48 JMP [vtbl+0x134]
	}
}                                                                       // 0x102c1e50
