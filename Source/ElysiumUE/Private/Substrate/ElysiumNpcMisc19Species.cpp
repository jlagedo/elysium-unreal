// Story 0019/8 (29e under the strict verdict), family **Misc19** -- the species classes' bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// A `STORY8-FORWARD` block is a forwarding override declared on its class (the header's `0019/8
// shape` section): it calls the port base, which is what the inherited dispatch ran, so it changes
// nothing. The porter replaces the body, keeps the declaration, and drops the marker.
//
// Owns (Misc19's `rule` rows): 0x1035dc30 CNPC_VAndreiBlood::Activate, 0x103cfc50
// CNPC_VWerewolf::CheckAllMoveHints, 0x10372c50 CNPC_VCop::vfunc596, 0x10372dd0
// CNPC_VCop::vfunc598, 0x103a3850 CNPC_VPedestrian::vfunc27, 0x101a98c0 CCineAISchedule::vfunc586,
// 0x101aade0 CPayphone::EnterGrappleState, 0x1037b500 CNPC_VGhoulCroucher::EnterGrappleState,
// 0x10374280 CNPC_VDog::HandleAnimEvent, 0x103786c0 CNPC_VGargoyle::HandleAnimEvent, 0x1037fb60
// CNPC_VHengeyokai::HandleAnimEvent, 0x1038e000 CNPC_VManBat::HandleAnimEvent, 0x10392a70
// CNPC_VMingXiao::HandleAnimEvent, 0x103a7000 CNPC_VSabbatLeader::HandleAnimEvent, 0x103ba410
// CNPC_VTzimisce::HandleAnimEvent, 0x103c1540 CNPC_VTzimisceHeadClaw::HandleAnimEvent, 0x103d88e0
// CNPC_VWerewolf::HandleAnimEvent.

#include "Substrate/ElysiumNpcAndreiBlood.h"
#include "Substrate/ElysiumNpcCop.h"
#include "Substrate/ElysiumNpcDog.h"
#include "Substrate/ElysiumNpcGargoyle.h"
#include "Substrate/ElysiumNpcGhoulCroucher.h"
#include "Substrate/ElysiumNpcManBat.h"
#include "Substrate/ElysiumNpcPayphone.h"
#include "Substrate/ElysiumNpcPedestrian.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcTzimisce.h"
#include "Substrate/ElysiumNpcWerewolf.h"

// STORY8-FORWARD slot 27 0x103a3850 CNPC_VPedestrian::Slot27 — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcPedestrian::Slot27(FElysiumEntity* Arg0)
{
	FElysiumNpcHuman::Slot27(Arg0);
}

// STORY8-FORWARD slot 113 0x1035dc30 CNPC_VAndreiBlood::Activate — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcAndreiBlood::Activate()
{
	FElysiumNpcVampireBoss::Activate();
}

// STORY8-FORWARD slot 259 0x10374280 CNPC_VDog::HandleAnimEvent — forwarding stub, not the port; the porter replaces this body.
bool FElysiumNpcDog::HandleAnimEvent(const FElysiumAnimEvent& Event)
{
	return FElysiumNpcAnimal::HandleAnimEvent(Event);
}

// STORY8-FORWARD slot 259 0x103786c0 CNPC_VGargoyle::HandleAnimEvent — forwarding stub, not the port; the porter replaces this body.
bool FElysiumNpcGargoyle::HandleAnimEvent(const FElysiumAnimEvent& Event)
{
	return FElysiumNpcVampire::HandleAnimEvent(Event);
}

// STORY8-FORWARD slot 259 0x1038e000 CNPC_VManBat::HandleAnimEvent — forwarding stub, not the port; the porter replaces this body.
bool FElysiumNpcManBat::HandleAnimEvent(const FElysiumAnimEvent& Event)
{
	return FElysiumNpcVampire::HandleAnimEvent(Event);
}

// STORY8-FORWARD slot 259 0x103a7000 CNPC_VSabbatLeader::HandleAnimEvent — forwarding stub, not the port; the porter replaces this body.
bool FElysiumNpcSabbatLeader::HandleAnimEvent(const FElysiumAnimEvent& Event)
{
	return FElysiumNpcVampireBoss::HandleAnimEvent(Event);
}

// STORY8-FORWARD slot 259 0x103ba410 CNPC_VTzimisce::HandleAnimEvent — forwarding stub, not the port; the porter replaces this body.
bool FElysiumNpcTzimisce::HandleAnimEvent(const FElysiumAnimEvent& Event)
{
	return FElysiumNpcBaseBoss::HandleAnimEvent(Event);
}

// STORY8-FORWARD slot 259 0x103d88e0 CNPC_VWerewolf::HandleAnimEvent — forwarding stub, not the port; the porter replaces this body.
bool FElysiumNpcWerewolf::HandleAnimEvent(const FElysiumAnimEvent& Event)
{
	return FElysiumNpcBaseBoss::HandleAnimEvent(Event);
}

// STORY8-FORWARD slot 379 0x101aade0 CPayphone::EnterGrappleState — forwarding stub, not the port; the porter replaces this body.
bool FElysiumNpcPayphone::EnterGrappleState(const FElysiumEntityHandle& Partner, EElysiumGrappleRole Role, EElysiumGrappleType Type, int32 Position, bool bHolster)
{
	return FElysiumNpc::EnterGrappleState(Partner, Role, Type, Position, bHolster);
}

// STORY8-FORWARD slot 379 0x1037b500 CNPC_VGhoulCroucher::EnterGrappleState — forwarding stub, not the port; the porter replaces this body.
bool FElysiumNpcGhoulCroucher::EnterGrappleState(const FElysiumEntityHandle& Partner, EElysiumGrappleRole Role, EElysiumGrappleType Type, int32 Position, bool bHolster)
{
	return FElysiumNpcHumanCombatant::EnterGrappleState(Partner, Role, Type, Position, bHolster);
}

// STORY8-FORWARD slot 596 0x10372c50 CNPC_VCop::Slot596 — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcCop::Slot596(FElysiumEntity* Arg0)
{
	FElysiumNpcHumanCombatant::Slot596(Arg0);
}

// STORY8-FORWARD slot 598 0x10372dd0 CNPC_VCop::Slot598 — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcCop::Slot598(FElysiumEntity* Arg0)
{
	FElysiumNpcHumanCombatant::Slot598(Arg0);
}
