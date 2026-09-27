// Story 0019/8 (29e under the strict verdict), family **Think19** -- the species classes' bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// A `STORY8-FORWARD` block is a forwarding override declared on its class (the header's `0019/8
// shape` section): it calls the port base, which is what the inherited dispatch ran, so it changes
// nothing. The porter replaces the body, keeps the declaration, and drops the marker.
//
// Owns (Think19's `rule` rows): 0x10369120 CNPC_VCamera::NPCThink, 0x1037b3f0
// CNPC_VGhoulCroucher::NPCThink, 0x10394990 CNPC_VMingXiao::NPCThink, 0x103a05b0
// CNPC_VNewscaster::NPCThink, 0x103b9040 CNPC_VTzimisce::NPCThink, 0x103c6000
// CNPC_VVampireBoss::NPCThink, 0x103dfa20 CNPC_VZombie::NPCThink, 0x1035db20
// CNPC_VAndreiBlood::NPCThink, 0x10361490 CNPC_VAsianVampire::NPCThink, 0x1036c6c0
// CNPC_VChangBros::NPCThink, 0x10375e50 CNPC_VFrenzyShadow::NPCThink, 0x103af830
// CNPC_VSheriffMan::NPCThink.

#include "Substrate/ElysiumNpcAndreiBlood.h"
#include "Substrate/ElysiumNpcAsianVampire.h"
#include "Substrate/ElysiumNpcCamera.h"
#include "Substrate/ElysiumNpcChangBros.h"
#include "Substrate/ElysiumNpcGhoulCroucher.h"
#include "Substrate/ElysiumNpcMingXiao.h"
#include "Substrate/ElysiumNpcNewscaster.h"
#include "Substrate/ElysiumNpcSheriffMan.h"
#include "Substrate/ElysiumNpcTzimisce.h"
#include "Substrate/ElysiumNpcVampireBoss.h"
#include "Substrate/ElysiumNpcZombie.h"

// STORY8-FORWARD slot 431 0x1035db20 CNPC_VAndreiBlood::NPCThink — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcAndreiBlood::NPCThink()
{
	FElysiumNpcVampireBoss::NPCThink();
}

// STORY8-FORWARD slot 431 0x10361490 CNPC_VAsianVampire::NPCThink — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcAsianVampire::NPCThink()
{
	FElysiumNpcVampireBoss::NPCThink();
}

// STORY8-FORWARD slot 431 0x10369120 CNPC_VCamera::NPCThink — forwarding stub, not the port; the porter replaces this body.
// Also carries the inherited body of CNPC_VCameraSecurity.
void FElysiumNpcCamera::NPCThink()
{
	FElysiumNpc::NPCThink();
}

// STORY8-FORWARD slot 431 0x1036c6c0 CNPC_VChangBros::NPCThink — forwarding stub, not the port; the porter replaces this body.
// Also carries the inherited body of CNPC_VChangBrosBlade, CNPC_VChangBrosClaw.
void FElysiumNpcChangBros::NPCThink()
{
	FElysiumNpcVampireBoss::NPCThink();
}

// STORY8-FORWARD slot 431 0x1037b3f0 CNPC_VGhoulCroucher::NPCThink — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcGhoulCroucher::NPCThink()
{
	FElysiumNpcHumanCombatant::NPCThink();
}

// STORY8-FORWARD slot 431 0x10394990 CNPC_VMingXiao::NPCThink — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcMingXiao::NPCThink()
{
	FElysiumNpcBaseBoss::NPCThink();
}

// STORY8-FORWARD slot 431 0x103a05b0 CNPC_VNewscaster::NPCThink — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcNewscaster::NPCThink()
{
	FElysiumNpc::NPCThink();
}

// STORY8-FORWARD slot 431 0x103af830 CNPC_VSheriffMan::NPCThink — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcSheriffMan::NPCThink()
{
	FElysiumNpcVampireBoss::NPCThink();
}

// STORY8-FORWARD slot 431 0x103b9040 CNPC_VTzimisce::NPCThink — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcTzimisce::NPCThink()
{
	FElysiumNpcBaseBoss::NPCThink();
}

// STORY8-FORWARD slot 431 0x103c6000 CNPC_VVampireBoss::NPCThink — forwarding stub, not the port; the porter replaces this body.
// Also carries the inherited body of CNPC_VSabbatLeader.
void FElysiumNpcVampireBoss::NPCThink()
{
	FElysiumNpcVampire::NPCThink();
}

// STORY8-FORWARD slot 431 0x103dfa20 CNPC_VZombie::NPCThink — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcZombie::NPCThink()
{
	FElysiumNpcAnimal::NPCThink();
}
