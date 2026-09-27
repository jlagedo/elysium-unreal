// Story 0019/8 (29e under the strict verdict), family **RunAi19** -- the species classes' bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// A `STORY8-FORWARD` block is a forwarding override declared on its class (the header's `0019/8
// shape` section): it calls the port base, which is what the inherited dispatch ran, so it changes
// nothing. The porter replaces the body, keeps the declaration, and drops the marker.
//
// Owns (RunAi19's `rule` rows): 0x1039e3d0 CNPC_VMingXiaoTentacle::RunAI, 0x103bdef0
// CNPC_VTzimisce::vfunc432, 0x103c1d20 CNPC_VTzimisceHeadClaw::vfunc432, 0x1035e980
// CNPC_VAndreiBlood::vfunc432, 0x10361110 CNPC_VAsianVampire::RunAI, 0x10363b60
// CNPC_VBach::vfunc432, 0x103747e0 CNPC_VDog::vfunc432, 0x10378b80 CNPC_VGargoyle::vfunc432,
// 0x10380120 CNPC_VHengeyokai::vfunc432, 0x1038e990 CNPC_VManBat::vfunc432, 0x103a3670
// CNPC_VPedestrian::vfunc432, 0x103a75c0 CNPC_VSabbatLeader::RunAI, 0x103aebd0
// CNPC_VSheriffMan::RunAI, 0x103df850 CNPC_VZombie::vfunc432.

#include "Substrate/ElysiumNpcAndreiBlood.h"
#include "Substrate/ElysiumNpcAsianVampire.h"
#include "Substrate/ElysiumNpcBach.h"
#include "Substrate/ElysiumNpcDog.h"
#include "Substrate/ElysiumNpcGargoyle.h"
#include "Substrate/ElysiumNpcHengeyokai.h"
#include "Substrate/ElysiumNpcManBat.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcPedestrian.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcSheriffMan.h"
#include "Substrate/ElysiumNpcTzimisce.h"
#include "Substrate/ElysiumNpcTzimisceHeadClaw.h"
#include "Substrate/ElysiumNpcZombie.h"

// STORY8-FORWARD slot 432 0x1035e980 CNPC_VAndreiBlood::RunAI — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcAndreiBlood::RunAI(bool Arg0)
{
	FElysiumNpcVampireBoss::RunAI(Arg0);
}

// STORY8-FORWARD slot 432 0x10361110 CNPC_VAsianVampire::RunAI — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcAsianVampire::RunAI(bool Arg0)
{
	FElysiumNpcVampireBoss::RunAI(Arg0);
}

// STORY8-FORWARD slot 432 0x10363b60 CNPC_VBach::RunAI — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcBach::RunAI(bool Arg0)
{
	FElysiumNpcVampire::RunAI(Arg0);
}

// STORY8-FORWARD slot 432 0x103747e0 CNPC_VDog::RunAI — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcDog::RunAI(bool Arg0)
{
	FElysiumNpcAnimal::RunAI(Arg0);
}

// STORY8-FORWARD slot 432 0x10378b80 CNPC_VGargoyle::RunAI — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcGargoyle::RunAI(bool Arg0)
{
	FElysiumNpcVampire::RunAI(Arg0);
}

// STORY8-FORWARD slot 432 0x10380120 CNPC_VHengeyokai::RunAI — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcHengeyokai::RunAI(bool Arg0)
{
	FElysiumNpcVampire::RunAI(Arg0);
}

// STORY8-FORWARD slot 432 0x1038e990 CNPC_VManBat::RunAI — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcManBat::RunAI(bool Arg0)
{
	FElysiumNpcVampire::RunAI(Arg0);
}

// STORY8-FORWARD slot 432 0x1039e3d0 CNPC_VMingXiaoTentacle::RunAI — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcMingXiaoTentacle::RunAI(bool Arg0)
{
	FElysiumNpc::RunAI(Arg0);
}

// STORY8-FORWARD slot 432 0x103a3670 CNPC_VPedestrian::RunAI — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcPedestrian::RunAI(bool Arg0)
{
	FElysiumNpcHuman::RunAI(Arg0);
}

// STORY8-FORWARD slot 432 0x103a75c0 CNPC_VSabbatLeader::RunAI — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcSabbatLeader::RunAI(bool Arg0)
{
	FElysiumNpcVampireBoss::RunAI(Arg0);
}

// STORY8-FORWARD slot 432 0x103aebd0 CNPC_VSheriffMan::RunAI — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcSheriffMan::RunAI(bool Arg0)
{
	FElysiumNpcVampireBoss::RunAI(Arg0);
}

// STORY8-FORWARD slot 432 0x103bdef0 CNPC_VTzimisce::RunAI — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcTzimisce::RunAI(bool Arg0)
{
	FElysiumNpcBaseBoss::RunAI(Arg0);
}

// STORY8-FORWARD slot 432 0x103c1d20 CNPC_VTzimisceHeadClaw::RunAI — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcTzimisceHeadClaw::RunAI(bool Arg0)
{
	FElysiumNpcBaseBoss::RunAI(Arg0);
}

// STORY8-FORWARD slot 432 0x103df850 CNPC_VZombie::RunAI — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcZombie::RunAI(bool Arg0)
{
	FElysiumNpcAnimal::RunAI(Arg0);
}
