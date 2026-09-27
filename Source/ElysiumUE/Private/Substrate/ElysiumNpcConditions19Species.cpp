// Story 0019/8 (29e under the strict verdict), family **Conditions19** -- the species classes'
// bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// A `STORY8-FORWARD` block is a forwarding override declared on its class (the header's `0019/8
// shape` section): it calls the port base, which is what the inherited dispatch ran, so it changes
// nothing. The porter replaces the body, keeps the declaration, and drops the marker.
//
// Owns (Conditions19's `rule` rows): 0x1035d180 CNPC_VAndreiBlood::GatherConditions, 0x10365a70
// CNPC_VBach::GatherConditions, 0x1036b590 CNPC_VChangBros::GatherConditions, 0x10374b00
// CNPC_VDog::GatherConditions, 0x10375ed0 CNPC_VFrenzyShadow::GatherConditions, 0x10378df0
// CNPC_VGargoyle::GatherConditions, 0x1037b570 CNPC_VGhoulCroucher::GatherConditions, 0x103803d0
// CNPC_VHengeyokai::GatherConditions, 0x10394e40 CNPC_VMingXiao::GatherConditions, 0x1039ec10
// CNPC_VMingXiaoTentacle::GatherConditions, 0x103a2c30 CNPC_VPedestrian::GatherConditions,
// 0x103a77f0 CNPC_VSabbatLeader::GatherConditions, 0x103ac500 CNPC_VScurrying::GatherConditions,
// 0x103bce40 CNPC_VTzimisce::GatherConditions, 0x103c17f0 CNPC_VTzimisceHeadClaw::GatherConditions,
// 0x103c35a0 CNPC_VTzimisceRunner::GatherConditions, 0x103d0410 CNPC_VWerewolf::GatherConditions.

#include "Substrate/ElysiumNpcAndreiBlood.h"
#include "Substrate/ElysiumNpcBach.h"
#include "Substrate/ElysiumNpcChangBros.h"
#include "Substrate/ElysiumNpcDog.h"
#include "Substrate/ElysiumNpcGargoyle.h"
#include "Substrate/ElysiumNpcGhoulCroucher.h"
#include "Substrate/ElysiumNpcHengeyokai.h"
#include "Substrate/ElysiumNpcMingXiao.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcPedestrian.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcScurrying.h"
#include "Substrate/ElysiumNpcTzimisce.h"
#include "Substrate/ElysiumNpcTzimisceHeadClaw.h"
#include "Substrate/ElysiumNpcTzimisceRunner.h"
#include "Substrate/ElysiumNpcWerewolf.h"

// STORY8-FORWARD slot 433 0x1035d180 CNPC_VAndreiBlood::GatherConditions — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcAndreiBlood::GatherConditions()
{
	FElysiumNpcVampireBoss::GatherConditions();
}

// STORY8-FORWARD slot 433 0x10365a70 CNPC_VBach::GatherConditions — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcBach::GatherConditions()
{
	FElysiumNpcVampire::GatherConditions();
}

// STORY8-FORWARD slot 433 0x1036b590 CNPC_VChangBros::GatherConditions — forwarding stub, not the port; the porter replaces this body.
// Also carries the inherited body of CNPC_VChangBrosBlade, CNPC_VChangBrosClaw.
void FElysiumNpcChangBros::GatherConditions()
{
	FElysiumNpcVampireBoss::GatherConditions();
}

// STORY8-FORWARD slot 433 0x10374b00 CNPC_VDog::GatherConditions — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcDog::GatherConditions()
{
	FElysiumNpcAnimal::GatherConditions();
}

// STORY8-FORWARD slot 433 0x10378df0 CNPC_VGargoyle::GatherConditions — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcGargoyle::GatherConditions()
{
	FElysiumNpcVampire::GatherConditions();
}

// STORY8-FORWARD slot 433 0x1037b570 CNPC_VGhoulCroucher::GatherConditions — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcGhoulCroucher::GatherConditions()
{
	FElysiumNpcHumanCombatant::GatherConditions();
}

// STORY8-FORWARD slot 433 0x103803d0 CNPC_VHengeyokai::GatherConditions — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcHengeyokai::GatherConditions()
{
	FElysiumNpcVampire::GatherConditions();
}

// STORY8-FORWARD slot 433 0x10394e40 CNPC_VMingXiao::GatherConditions — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcMingXiao::GatherConditions()
{
	FElysiumNpcBaseBoss::GatherConditions();
}

// STORY8-FORWARD slot 433 0x1039ec10 CNPC_VMingXiaoTentacle::GatherConditions — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcMingXiaoTentacle::GatherConditions()
{
	FElysiumNpc::GatherConditions();
}

// STORY8-FORWARD slot 433 0x103a2c30 CNPC_VPedestrian::GatherConditions — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcPedestrian::GatherConditions()
{
	FElysiumNpcHuman::GatherConditions();
}

// STORY8-FORWARD slot 433 0x103a77f0 CNPC_VSabbatLeader::GatherConditions — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcSabbatLeader::GatherConditions()
{
	FElysiumNpcVampireBoss::GatherConditions();
}

// STORY8-FORWARD slot 433 0x103ac500 CNPC_VScurrying::GatherConditions — forwarding stub, not the port; the porter replaces this body.
// Also carries the inherited body of CNPC_VRat.
void FElysiumNpcScurrying::GatherConditions()
{
	FElysiumNpcAnimal::GatherConditions();
}

// STORY8-FORWARD slot 433 0x103bce40 CNPC_VTzimisce::GatherConditions — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcTzimisce::GatherConditions()
{
	FElysiumNpcBaseBoss::GatherConditions();
}

// STORY8-FORWARD slot 433 0x103c17f0 CNPC_VTzimisceHeadClaw::GatherConditions — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcTzimisceHeadClaw::GatherConditions()
{
	FElysiumNpcBaseBoss::GatherConditions();
}

// STORY8-FORWARD slot 433 0x103c35a0 CNPC_VTzimisceRunner::GatherConditions — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcTzimisceRunner::GatherConditions()
{
	FElysiumNpcBaseBoss::GatherConditions();
}

// STORY8-FORWARD slot 433 0x103d0410 CNPC_VWerewolf::GatherConditions — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcWerewolf::GatherConditions()
{
	FElysiumNpcBaseBoss::GatherConditions();
}
