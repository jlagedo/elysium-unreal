// Story 0019/8 (29e under the strict verdict), family **Script19** -- the species classes' bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// A `STORY8-FORWARD` block is a forwarding override declared on its class (the header's `0019/8
// shape` section): it calls the port base, which is what the inherited dispatch ran, so it changes
// nothing. The porter replaces the body, keeps the declaration, and drops the marker.
//
// Owns (Script19's `rule` rows): 0x101a7140 CCineNPC::UpdateOnRemove, 0x101a7880
// CCineNPC::vfunc583, 0x101a9080 CCineAI::vfunc583, 0x1037c1c0 CNPC_VGhoulCroucher::ScriptHide,
// 0x1038b120 CNPC_VManBat::OverrideMove, 0x101a82d0 CCineAISchedule::FUN_101a82d0, 0x101a9510
// CCineAI::vfunc584, 0x101a9790 CCineAISchedule::vfunc583.

#include "Substrate/ElysiumNpcManBat.h"

// STORY8-FORWARD slot 525 0x1038b120 CNPC_VManBat::OverrideMove — forwarding stub, not the port; the porter replaces this body.
bool FElysiumNpcManBat::OverrideMove(float Arg0)
{
	return FElysiumNpcVampire::OverrideMove(Arg0);
}
