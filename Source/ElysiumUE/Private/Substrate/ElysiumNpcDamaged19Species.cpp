// Story 0019/8 (29e under the strict verdict), family **Damaged19** -- the species classes' bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// A `STORY8-FORWARD` block is a forwarding override declared on its class (the header's `0019/8
// shape` section): it calls the port base, which is what the inherited dispatch ran, so it changes
// nothing. The porter replaces the body, keeps the declaration, and drops the marker.
//
// Owns (Damaged19's `rule` rows): 0x103c43b0 CNPC_VTzimisceRunner::vfunc330 ‼, 0x1037e240
// CNPC_VGuard1::NPCInit ‼, 0x10387140 CNPC_VHumanCombatant::NPCInit ‼, 0x103dd800
// CNPC_VYukie::NPCInit ‼, 0x103c32c0 CNPC_VTzimisceRunner::HandleAnimEvent ‼, 0x103a4700
// CNPC_VPlayerController::NPCThink ‼, 0x103cb590 CNPC_VWerewolf::NPCThink ‼, 0x10371b70
// CNPC_VCop::StartTask ‼.

#include "Substrate/ElysiumNpcCop.h"
#include "Substrate/ElysiumNpcTzimisceRunner.h"
#include "Substrate/ElysiumNpcWerewolf.h"

// Slot 330 `0x103c43b0` `CNPC_VTzimisceRunner::vfunc330(float, FireBulletsInfo_t*)` — story 8, lane
// L12. 42 bytes and NO jump table (the decompiler's "indirect jump" is the tail `JMP [vtbl+0x500]`):
// the near-miss bullet reaction becomes slot 320 `PlayerKnockbackReaction(shooter, 0x79)` on this
// Runner, in place of the base's distance-graded flinch (`0x1029fbe0`). The float is never read.
void FElysiumNpcTzimisceRunner::Slot330(float Arg0, void* Arg1)
{
	(void)Arg0;
	const FFireBulletsInfo* Info = static_cast<const FFireBulletsInfo*>(Arg1);
	if (Info == nullptr)
	{
		// NAMED CRASH GUARD: retail dereferences the info pointer unconditionally (`0x103c43b6`).
		return;
	}
	// `arg = info->+0x94 ? info->+0x94->+0x9c : 0` — the attacker's cached combat-character pointer.
	FElysiumEntity* Shooter = nullptr;                                      // 0x103c43b4
	if (Info->Attacker != nullptr)                                          // 0x103c43b6 / 0x103c43be
	{
		Shooter = Info->Attacker->AsCombatCharacter();                      // 0x103c43c0 +0x9c
	}
	// The two incoming stack slots are overwritten in place (`0x103c43c8` activity `0x79`,
	// `0x103c43d0` the shooter) and slot 320 is tail-jumped on THIS object's own vtable.
	PlayerKnockbackReaction(Shooter, 0x79);                                 // 0x103c43d4 slot 320
}

// STORY8-FORWARD slot 431 0x103cb590 CNPC_VWerewolf::NPCThink — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcWerewolf::NPCThink()
{
	FElysiumNpcBaseBoss::NPCThink();
}

// STORY8-FORWARD slot 442 0x10371b70 CNPC_VCop::StartTaskSlot442 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcCop::StartTaskSlot442(void* Arg0)
{
	return FElysiumNpcHumanCombatant::StartTaskSlot442(Arg0);
}
