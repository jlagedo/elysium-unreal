// Story 0019/8 (29e under the strict verdict), family **RunTask19** -- the species classes' bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// A `STORY8-FORWARD` block is a forwarding override declared on its class (the header's `0019/8
// shape` section): it calls the port base, which is what the inherited dispatch ran, so it changes
// nothing. The porter replaces the body, keeps the declaration, and drops the marker.
//
// Owns (RunTask19's `rule` rows): 0x1038d130 CNPC_VManBat::RunTask, 0x1035f940
// CNPC_VAnimal::RunTask, 0x10384ab0 CNPC_VHuman::RunTask, 0x10393930 CNPC_VMingXiao::RunTask,
// 0x1039d750 CNPC_VMingXiaoTentacle::RunTask, 0x103bb1e0 CNPC_VTzimisce::RunTask, 0x103c3870
// CNPC_VTzimisceRunner::RunTask, 0x103cdfb0 CNPC_VWerewolf::RunTask, 0x103652b0
// CNPC_VBach::RunTask, 0x10374a20 CNPC_VDog::RunTask, 0x103793e0 CNPC_VGargoyle::RunTask,
// 0x1037b9f0 CNPC_VGhoulCroucher::RunTask, 0x10380cb0 CNPC_VHengeyokai::RunTask, 0x103b38a0
// CNPC_VTaxiDriver::RunTask, 0x103c5f40 CNPC_VVampireBoss::RunTask, 0x103e01d0
// CNPC_VZombie::RunTask, 0x1035d8b0 CNPC_VAndreiBlood::RunTask, 0x103612e0
// CNPC_VAsianVampire::RunTask, 0x1036bfc0 CNPC_VChangBros::RunTask, 0x103a8990
// CNPC_VSabbatLeader::RunTask, 0x103af780 CNPC_VSheriffMan::RunTask.

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
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcSheriffMan.h"
#include "Substrate/ElysiumNpcTaxiDriver.h"
#include "Substrate/ElysiumNpcTzimisce.h"
#include "Substrate/ElysiumNpcTzimisceRunner.h"
#include "Substrate/ElysiumNpcVampireBoss.h"
#include "Substrate/ElysiumNpcWerewolf.h"
#include "Substrate/ElysiumNpcZombie.h"

// STORY8-FORWARD slot 444 0x1035d8b0 CNPC_VAndreiBlood::RunTaskSlot444 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcAndreiBlood::RunTaskSlot444(void* Arg0)
{
	return FElysiumNpcVampireBoss::RunTaskSlot444(Arg0);
}

// STORY8-FORWARD slot 444 0x1035f940 CNPC_VAnimal::RunTaskSlot444 — forwarding stub, not the port; the porter replaces this body.
// Also carries the inherited body of CNPC_VRat, CNPC_VScurrying.
int32 FElysiumNpcAnimal::RunTaskSlot444(void* Arg0)
{
	return FElysiumNpc::RunTaskSlot444(Arg0);
}

// STORY8-FORWARD slot 444 0x103612e0 CNPC_VAsianVampire::RunTaskSlot444 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcAsianVampire::RunTaskSlot444(void* Arg0)
{
	return FElysiumNpcVampireBoss::RunTaskSlot444(Arg0);
}

// STORY8-FORWARD slot 444 0x103652b0 CNPC_VBach::RunTaskSlot444 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcBach::RunTaskSlot444(void* Arg0)
{
	return FElysiumNpcVampire::RunTaskSlot444(Arg0);
}

// STORY8-FORWARD slot 444 0x1036bfc0 CNPC_VChangBros::RunTaskSlot444 — forwarding stub, not the port; the porter replaces this body.
// Also carries the inherited body of CNPC_VChangBrosBlade, CNPC_VChangBrosClaw.
int32 FElysiumNpcChangBros::RunTaskSlot444(void* Arg0)
{
	return FElysiumNpcVampireBoss::RunTaskSlot444(Arg0);
}

// STORY8-FORWARD slot 444 0x10374a20 CNPC_VDog::RunTaskSlot444 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcDog::RunTaskSlot444(void* Arg0)
{
	return FElysiumNpcAnimal::RunTaskSlot444(Arg0);
}

// STORY8-FORWARD slot 444 0x103793e0 CNPC_VGargoyle::RunTaskSlot444 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcGargoyle::RunTaskSlot444(void* Arg0)
{
	return FElysiumNpcVampire::RunTaskSlot444(Arg0);
}

// STORY8-FORWARD slot 444 0x1037b9f0 CNPC_VGhoulCroucher::RunTaskSlot444 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcGhoulCroucher::RunTaskSlot444(void* Arg0)
{
	return FElysiumNpcHumanCombatant::RunTaskSlot444(Arg0);
}

// STORY8-FORWARD slot 444 0x10380cb0 CNPC_VHengeyokai::RunTaskSlot444 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcHengeyokai::RunTaskSlot444(void* Arg0)
{
	return FElysiumNpcVampire::RunTaskSlot444(Arg0);
}

// STORY8-FORWARD slot 444 0x10384ab0 CNPC_VHuman::RunTaskSlot444 — forwarding stub, not the port; the porter replaces this body.
// Also carries the inherited body of CNPC_ProneDialog, CNPC_VBrujah, CNPC_VCop, CNPC_VFrenzyShadow,
// CNPC_VGuard1, CNPC_VHumanCombatPatrol, CNPC_VHumanCombatant, CNPC_VHunter, CNPC_VLasombra,
// CNPC_VPedestrian, CNPC_VPlayerController, CNPC_VSabbatGunman, CNPC_VVampire, CNPC_VWolfMorph,
// CNPC_VYukie.
int32 FElysiumNpcHuman::RunTaskSlot444(void* Arg0)
{
	return FElysiumNpc::RunTaskSlot444(Arg0);
}

// STORY8-FORWARD slot 444 0x1038d130 CNPC_VManBat::RunTaskSlot444 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcManBat::RunTaskSlot444(void* Arg0)
{
	return FElysiumNpcVampire::RunTaskSlot444(Arg0);
}

// STORY8-FORWARD slot 444 0x10393930 CNPC_VMingXiao::RunTaskSlot444 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcMingXiao::RunTaskSlot444(void* Arg0)
{
	return FElysiumNpcBaseBoss::RunTaskSlot444(Arg0);
}

// STORY8-FORWARD slot 444 0x1039d750 CNPC_VMingXiaoTentacle::RunTaskSlot444 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcMingXiaoTentacle::RunTaskSlot444(void* Arg0)
{
	return FElysiumNpc::RunTaskSlot444(Arg0);
}

// STORY8-FORWARD slot 444 0x103a8990 CNPC_VSabbatLeader::RunTaskSlot444 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcSabbatLeader::RunTaskSlot444(void* Arg0)
{
	return FElysiumNpcVampireBoss::RunTaskSlot444(Arg0);
}

// STORY8-FORWARD slot 444 0x103af780 CNPC_VSheriffMan::RunTaskSlot444 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcSheriffMan::RunTaskSlot444(void* Arg0)
{
	return FElysiumNpcVampireBoss::RunTaskSlot444(Arg0);
}

// STORY8-FORWARD slot 444 0x103b38a0 CNPC_VTaxiDriver::RunTaskSlot444 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcTaxiDriver::RunTaskSlot444(void* Arg0)
{
	return FElysiumNpcHuman::RunTaskSlot444(Arg0);
}

// STORY8-FORWARD slot 444 0x103bb1e0 CNPC_VTzimisce::RunTaskSlot444 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcTzimisce::RunTaskSlot444(void* Arg0)
{
	return FElysiumNpcBaseBoss::RunTaskSlot444(Arg0);
}

// STORY8-FORWARD slot 444 0x103c3870 CNPC_VTzimisceRunner::RunTaskSlot444 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcTzimisceRunner::RunTaskSlot444(void* Arg0)
{
	return FElysiumNpcBaseBoss::RunTaskSlot444(Arg0);
}

// STORY8-FORWARD slot 444 0x103c5f40 CNPC_VVampireBoss::RunTaskSlot444 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcVampireBoss::RunTaskSlot444(void* Arg0)
{
	return FElysiumNpcVampire::RunTaskSlot444(Arg0);
}

// STORY8-FORWARD slot 444 0x103cdfb0 CNPC_VWerewolf::RunTaskSlot444 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcWerewolf::RunTaskSlot444(void* Arg0)
{
	return FElysiumNpcBaseBoss::RunTaskSlot444(Arg0);
}

// STORY8-FORWARD slot 444 0x103e01d0 CNPC_VZombie::RunTaskSlot444 — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcZombie::RunTaskSlot444(void* Arg0)
{
	return FElysiumNpcAnimal::RunTaskSlot444(Arg0);
}
