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

// STORY8-FORWARD slot 330 0x103c43b0 CNPC_VTzimisceRunner::Slot330 — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcTzimisceRunner::Slot330(float Arg0, void* Arg1)
{
	FElysiumNpcBaseBoss::Slot330(Arg0, Arg1);
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
