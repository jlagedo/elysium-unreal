// Story 0019/8 (29e under the strict verdict), family **StartTask19** -- the species classes'
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
// Owns (StartTask19's `rule` rows): 0x1035f650 CNPC_VAnimal::StartTask, 0x103847f0
// CNPC_VHuman::StartTask, 0x10392d80 CNPC_VMingXiao::StartTask, 0x1039c4c0
// CNPC_VMingXiaoTentacle::StartTask, 0x103ba7c0 CNPC_VTzimisce::StartTask, 0x103c1820
// CNPC_VTzimisceHeadClaw::StartTask, 0x103c35d0 CNPC_VTzimisceRunner::StartTask, 0x103ccda0
// CNPC_VWerewolf::StartTask, 0x103645a0 CNPC_VBach::StartTask, 0x10374940 CNPC_VDog::StartTask,
// 0x10375f50 CNPC_VFrenzyShadow::StartTask, 0x103790d0 CNPC_VGargoyle::StartTask, 0x1037b8b0
// CNPC_VGhoulCroucher::StartTask, 0x103805d0 CNPC_VHengeyokai::StartTask, 0x1038c390
// CNPC_VManBat::StartTask, 0x103a5650 CNPC_VSabbatGunman::StartTask, 0x103ac740
// CNPC_VScurrying::StartTask, 0x103b36d0 CNPC_VTaxiDriver::StartTask, 0x103c5ac0
// CNPC_VVampireBoss::StartTask, 0x103dfd80 CNPC_VZombie::StartTask, 0x1035d1b0
// CNPC_VAndreiBlood::StartTask, 0x103611a0 CNPC_VAsianVampire::StartTask, 0x1036b750
// CNPC_VChangBros::StartTask, 0x103a78c0 CNPC_VSabbatLeader::StartTask, 0x103aec70
// CNPC_VSheriffMan::StartTask.

#include "Substrate/ElysiumNpcAndreiBlood.h"
#include "Substrate/ElysiumNpcAnimal.h"
#include "Substrate/ElysiumNpcAsianVampire.h"
#include "Substrate/ElysiumNpcBach.h"
#include "Substrate/ElysiumNpcChangBros.h"
#include "Substrate/ElysiumNpcDog.h"
#include "Substrate/ElysiumNpcGargoyle.h"
#include "Substrate/ElysiumNpcGhoulCroucher.h"
#include "Substrate/ElysiumNpcHengeyokai.h"
#include "Substrate/ElysiumNpcHuman.h"
#include "Substrate/ElysiumNpcManBat.h"
#include "Substrate/ElysiumNpcMingXiao.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcSabbatGunman.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcScurrying.h"
#include "Substrate/ElysiumNpcSheriffMan.h"
#include "Substrate/ElysiumNpcTaxiDriver.h"
#include "Substrate/ElysiumNpcTzimisce.h"
#include "Substrate/ElysiumNpcTzimisceHeadClaw.h"
#include "Substrate/ElysiumNpcTzimisceRunner.h"
#include "Substrate/ElysiumNpcVampireBoss.h"
#include "Substrate/ElysiumNpcWerewolf.h"
#include "Substrate/ElysiumNpcZombie.h"

// STORY8-FORWARD slot 442 0x1035d1b0 CNPC_VAndreiBlood::StartTaskSlot442 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcAndreiBlood::StartTaskSlot442(void* Arg0)
{
	return FElysiumNpcVampireBoss::StartTaskSlot442(Arg0);
}

// STORY8-FORWARD slot 442 0x1035f650 CNPC_VAnimal::StartTaskSlot442 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcAnimal::StartTaskSlot442(void* Arg0)
{
	return FElysiumNpc::StartTaskSlot442(Arg0);
}

// STORY8-FORWARD slot 442 0x103611a0 CNPC_VAsianVampire::StartTaskSlot442 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcAsianVampire::StartTaskSlot442(void* Arg0)
{
	return FElysiumNpcVampireBoss::StartTaskSlot442(Arg0);
}

// STORY8-FORWARD slot 442 0x103645a0 CNPC_VBach::StartTaskSlot442 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcBach::StartTaskSlot442(void* Arg0)
{
	return FElysiumNpcVampire::StartTaskSlot442(Arg0);
}

// STORY8-FORWARD slot 442 0x1036b750 CNPC_VChangBros::StartTaskSlot442 — forwarding stub, not the port; the porter replaces this body.
// Also carries the inherited body of CNPC_VChangBrosBlade, CNPC_VChangBrosClaw.
int32 FElysiumNpcChangBros::StartTaskSlot442(void* Arg0)
{
	return FElysiumNpcVampireBoss::StartTaskSlot442(Arg0);
}

// STORY8-FORWARD slot 442 0x10374940 CNPC_VDog::StartTaskSlot442 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcDog::StartTaskSlot442(void* Arg0)
{
	return FElysiumNpcAnimal::StartTaskSlot442(Arg0);
}

// STORY8-FORWARD slot 442 0x103790d0 CNPC_VGargoyle::StartTaskSlot442 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcGargoyle::StartTaskSlot442(void* Arg0)
{
	return FElysiumNpcVampire::StartTaskSlot442(Arg0);
}

// STORY8-FORWARD slot 442 0x1037b8b0 CNPC_VGhoulCroucher::StartTaskSlot442 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcGhoulCroucher::StartTaskSlot442(void* Arg0)
{
	return FElysiumNpcHumanCombatant::StartTaskSlot442(Arg0);
}

// STORY8-FORWARD slot 442 0x103805d0 CNPC_VHengeyokai::StartTaskSlot442 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcHengeyokai::StartTaskSlot442(void* Arg0)
{
	return FElysiumNpcVampire::StartTaskSlot442(Arg0);
}

// STORY8-FORWARD slot 442 0x103847f0 CNPC_VHuman::StartTaskSlot442 — forwarding stub, not the port; the porter replaces this body.
// Also carries the inherited body of CNPC_ProneDialog, CNPC_VBrujah, CNPC_VGuard1,
// CNPC_VHumanCombatPatrol, CNPC_VHumanCombatant, CNPC_VHunter, CNPC_VLasombra, CNPC_VPedestrian,
// CNPC_VPlayerController, CNPC_VVampire, CNPC_VWolfMorph, CNPC_VYukie.
int32 FElysiumNpcHuman::StartTaskSlot442(void* Arg0)
{
	return FElysiumNpc::StartTaskSlot442(Arg0);
}

// STORY8-FORWARD slot 442 0x1038c390 CNPC_VManBat::StartTaskSlot442 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcManBat::StartTaskSlot442(void* Arg0)
{
	return FElysiumNpcVampire::StartTaskSlot442(Arg0);
}

// STORY8-FORWARD slot 442 0x10392d80 CNPC_VMingXiao::StartTaskSlot442 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcMingXiao::StartTaskSlot442(void* Arg0)
{
	return FElysiumNpcBaseBoss::StartTaskSlot442(Arg0);
}

// STORY8-FORWARD slot 442 0x1039c4c0 CNPC_VMingXiaoTentacle::StartTaskSlot442 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcMingXiaoTentacle::StartTaskSlot442(void* Arg0)
{
	return FElysiumNpc::StartTaskSlot442(Arg0);
}

// STORY8-FORWARD slot 442 0x103a5650 CNPC_VSabbatGunman::StartTaskSlot442 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcSabbatGunman::StartTaskSlot442(void* Arg0)
{
	return FElysiumNpcHumanCombatant::StartTaskSlot442(Arg0);
}

// STORY8-FORWARD slot 442 0x103a78c0 CNPC_VSabbatLeader::StartTaskSlot442 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcSabbatLeader::StartTaskSlot442(void* Arg0)
{
	return FElysiumNpcVampireBoss::StartTaskSlot442(Arg0);
}

// STORY8-FORWARD slot 442 0x103ac740 CNPC_VScurrying::StartTaskSlot442 — forwarding stub, not the port; the porter replaces this body.
// Also carries the inherited body of CNPC_VRat.
int32 FElysiumNpcScurrying::StartTaskSlot442(void* Arg0)
{
	return FElysiumNpcAnimal::StartTaskSlot442(Arg0);
}

// STORY8-FORWARD slot 442 0x103aec70 CNPC_VSheriffMan::StartTaskSlot442 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcSheriffMan::StartTaskSlot442(void* Arg0)
{
	return FElysiumNpcVampireBoss::StartTaskSlot442(Arg0);
}

// STORY8-FORWARD slot 442 0x103b36d0 CNPC_VTaxiDriver::StartTaskSlot442 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcTaxiDriver::StartTaskSlot442(void* Arg0)
{
	return FElysiumNpcHuman::StartTaskSlot442(Arg0);
}

// STORY8-FORWARD slot 442 0x103ba7c0 CNPC_VTzimisce::StartTaskSlot442 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcTzimisce::StartTaskSlot442(void* Arg0)
{
	return FElysiumNpcBaseBoss::StartTaskSlot442(Arg0);
}

// STORY8-FORWARD slot 442 0x103c1820 CNPC_VTzimisceHeadClaw::StartTaskSlot442 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcTzimisceHeadClaw::StartTaskSlot442(void* Arg0)
{
	return FElysiumNpcBaseBoss::StartTaskSlot442(Arg0);
}

// STORY8-FORWARD slot 442 0x103c35d0 CNPC_VTzimisceRunner::StartTaskSlot442 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcTzimisceRunner::StartTaskSlot442(void* Arg0)
{
	return FElysiumNpcBaseBoss::StartTaskSlot442(Arg0);
}

// STORY8-FORWARD slot 442 0x103c5ac0 CNPC_VVampireBoss::StartTaskSlot442 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcVampireBoss::StartTaskSlot442(void* Arg0)
{
	return FElysiumNpcVampire::StartTaskSlot442(Arg0);
}

// STORY8-FORWARD slot 442 0x103ccda0 CNPC_VWerewolf::StartTaskSlot442 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcWerewolf::StartTaskSlot442(void* Arg0)
{
	return FElysiumNpcBaseBoss::StartTaskSlot442(Arg0);
}

// STORY8-FORWARD slot 442 0x103dfd80 CNPC_VZombie::StartTaskSlot442 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcZombie::StartTaskSlot442(void* Arg0)
{
	return FElysiumNpcAnimal::StartTaskSlot442(Arg0);
}
