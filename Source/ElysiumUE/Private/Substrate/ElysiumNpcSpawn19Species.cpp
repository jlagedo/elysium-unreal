// Story 0019/8 (29e under the strict verdict), family **Spawn19** -- the species classes' bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// A `STORY8-FORWARD` block is a forwarding override declared on its class (the header's `0019/8
// shape` section): it calls the port base, which is what the inherited dispatch ran, so it changes
// nothing. The porter replaces the body, keeps the declaration, and drops the marker.
//
// Owns (Spawn19's `rule` rows): 0x103c60a0 CNPC_VVampireBoss::TransformationStart, 0x103c75f0
// CNPC_VVampireBoss::InputTransformModel, 0x103dfbb0 CNPC_VZombie::CreateCorpse, 0x103ab310
// CNPC_VSabbatLeader::TransformationStart, 0x10378da0 CNPC_VGargoyle::Event_Killed, 0x10380390
// CNPC_VHengeyokai::Event_Killed, 0x1038e8c0 CNPC_VManBat::Event_Killed, 0x1039e900
// CNPC_VMingXiaoTentacle::Event_Killed, 0x103be010 CNPC_VTzimisce::Event_Killed, 0x103c1d50
// CNPC_VTzimisceHeadClaw::Event_Killed, 0x10368b70 CNPC_VCamera::Spawn, 0x10395ba0
// CNPC_VMingXiao::Event_Killed, 0x101aa9c0 CPayphone::Spawn, 0x1035f510 CNPC_VAnimal::Spawn,
// 0x10384690 CNPC_VHuman::Spawn, 0x103927a0 CNPC_VMingXiao::Spawn, 0x1039c380
// CNPC_VMingXiaoTentacle::Spawn, 0x103b9060 CNPC_VTzimisce::Spawn, 0x103c1b90
// CNPC_VTzimisceHeadClaw::Spawn, 0x103c3b30 CNPC_VTzimisceRunner::Spawn, 0x103caa30
// CNPC_VWerewolf::Spawn, 0x10374000 CNPC_VDog::Spawn, 0x1037cda0 CNPC_VGuard1::Spawn, 0x10387110
// CNPC_VHumanCombatant::Spawn, 0x103a2540 CNPC_VPedestrian::Spawn, 0x103ac430
// CNPC_VScurrying::Spawn, 0x103c4ef0 CNPC_VVampire::Spawn, 0x103df170 CNPC_VZombie::Spawn,
// 0x1035cc20 CNPC_VAndreiBlood::Spawn, 0x10360c50 CNPC_VAsianVampire::Spawn, 0x10363850
// CNPC_VBach::Spawn, 0x1036afc0 CNPC_VChangBros::Spawn, 0x10371a20 CNPC_VCop::Spawn, 0x1037b040
// CNPC_VGhoulCroucher::Spawn, 0x1037fa00 CNPC_VHengeyokai::Spawn, 0x103887a0 CNPC_VHunter::Spawn,
// 0x10389390 CNPC_VLasombra::Spawn, 0x1038b030 CNPC_VManBat::Spawn, 0x103a4510
// CNPC_VPlayerController::Spawn, 0x103a6c80 CNPC_VSabbatLeader::Spawn, 0x103ad630 CNPC_VRat::Spawn,
// 0x103ae630 CNPC_VSheriffMan::Spawn, 0x103dd620 CNPC_VYukie::Spawn, 0x10375c50
// CNPC_VFrenzyShadow::Spawn.

#include "Substrate/ElysiumNpcAndreiBlood.h"
#include "Substrate/ElysiumNpcAnimal.h"
#include "Substrate/ElysiumNpcAsianVampire.h"
#include "Substrate/ElysiumNpcBach.h"
#include "Substrate/ElysiumNpcCamera.h"
#include "Substrate/ElysiumNpcChangBros.h"
#include "Substrate/ElysiumNpcCop.h"
#include "Substrate/ElysiumNpcDog.h"
#include "Substrate/ElysiumNpcGargoyle.h"
#include "Substrate/ElysiumNpcGhoulCroucher.h"
#include "Substrate/ElysiumNpcGuard1.h"
#include "Substrate/ElysiumNpcHengeyokai.h"
#include "Substrate/ElysiumNpcHuman.h"
#include "Substrate/ElysiumNpcHumanCombatant.h"
#include "Substrate/ElysiumNpcHunter.h"
#include "Substrate/ElysiumNpcLasombra.h"
#include "Substrate/ElysiumNpcManBat.h"
#include "Substrate/ElysiumNpcMingXiao.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcPayphone.h"
#include "Substrate/ElysiumNpcPedestrian.h"
#include "Substrate/ElysiumNpcRat.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcScurrying.h"
#include "Substrate/ElysiumNpcSheriffMan.h"
#include "Substrate/ElysiumNpcTzimisce.h"
#include "Substrate/ElysiumNpcTzimisceHeadClaw.h"
#include "Substrate/ElysiumNpcTzimisceRunner.h"
#include "Substrate/ElysiumNpcVampire.h"
#include "Substrate/ElysiumNpcWerewolf.h"
#include "Substrate/ElysiumNpcYukie.h"
#include "Substrate/ElysiumNpcZombie.h"

// STORY8-FORWARD slot 103 0x101aa9c0 CPayphone::Spawn — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcPayphone::Spawn()
{
	FElysiumNpc::Spawn();
}

// STORY8-FORWARD slot 103 0x1035cc20 CNPC_VAndreiBlood::Spawn — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcAndreiBlood::Spawn()
{
	FElysiumNpcVampireBoss::Spawn();
}

// STORY8-FORWARD slot 103 0x1035f510 CNPC_VAnimal::Spawn — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcAnimal::Spawn()
{
	FElysiumNpc::Spawn();
}

// STORY8-FORWARD slot 103 0x10360c50 CNPC_VAsianVampire::Spawn — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcAsianVampire::Spawn()
{
	FElysiumNpcVampireBoss::Spawn();
}

// STORY8-FORWARD slot 103 0x10363850 CNPC_VBach::Spawn — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcBach::Spawn()
{
	FElysiumNpcVampire::Spawn();
}

// STORY8-FORWARD slot 103 0x10368b70 CNPC_VCamera::Spawn — forwarding stub, not the port; the porter replaces this body.
// Also carries the inherited body of CNPC_VCameraSecurity.
void FElysiumNpcCamera::Spawn()
{
	FElysiumNpc::Spawn();
}

// STORY8-FORWARD slot 103 0x1036afc0 CNPC_VChangBros::Spawn — forwarding stub, not the port; the porter replaces this body.
// Also carries the inherited body of CNPC_VChangBrosBlade, CNPC_VChangBrosClaw.
void FElysiumNpcChangBros::Spawn()
{
	FElysiumNpcVampireBoss::Spawn();
}

// STORY8-FORWARD slot 103 0x10371a20 CNPC_VCop::Spawn — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcCop::Spawn()
{
	FElysiumNpcHumanCombatant::Spawn();
}

// STORY8-FORWARD slot 103 0x10374000 CNPC_VDog::Spawn — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcDog::Spawn()
{
	FElysiumNpcAnimal::Spawn();
}

// STORY8-FORWARD slot 103 0x1037b040 CNPC_VGhoulCroucher::Spawn — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcGhoulCroucher::Spawn()
{
	FElysiumNpcHumanCombatant::Spawn();
}

// STORY8-FORWARD slot 103 0x1037cda0 CNPC_VGuard1::Spawn — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcGuard1::Spawn()
{
	FElysiumNpcHuman::Spawn();
}

// STORY8-FORWARD slot 103 0x1037fa00 CNPC_VHengeyokai::Spawn — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcHengeyokai::Spawn()
{
	FElysiumNpcVampire::Spawn();
}

// STORY8-FORWARD slot 103 0x10384690 CNPC_VHuman::Spawn — forwarding stub, not the port; the porter replaces this body.
// Also carries the inherited body of CNPC_VTaxiDriver.
void FElysiumNpcHuman::Spawn()
{
	FElysiumNpc::Spawn();
}

// STORY8-FORWARD slot 103 0x10387110 CNPC_VHumanCombatant::Spawn — forwarding stub, not the port; the porter replaces this body.
// Also carries the inherited body of CNPC_ProneDialog, CNPC_VHumanCombatPatrol, CNPC_VSabbatGunman.
void FElysiumNpcHumanCombatant::Spawn()
{
	FElysiumNpcHuman::Spawn();
}

// STORY8-FORWARD slot 103 0x103887a0 CNPC_VHunter::Spawn — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcHunter::Spawn()
{
	FElysiumNpcHumanCombatant::Spawn();
}

// STORY8-FORWARD slot 103 0x10389390 CNPC_VLasombra::Spawn — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcLasombra::Spawn()
{
	FElysiumNpcVampire::Spawn();
}

// STORY8-FORWARD slot 103 0x1038b030 CNPC_VManBat::Spawn — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcManBat::Spawn()
{
	FElysiumNpcVampire::Spawn();
}

// STORY8-FORWARD slot 103 0x103927a0 CNPC_VMingXiao::Spawn — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcMingXiao::Spawn()
{
	FElysiumNpcBaseBoss::Spawn();
}

// STORY8-FORWARD slot 103 0x1039c380 CNPC_VMingXiaoTentacle::Spawn — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcMingXiaoTentacle::Spawn()
{
	FElysiumNpc::Spawn();
}

// STORY8-FORWARD slot 103 0x103a2540 CNPC_VPedestrian::Spawn — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcPedestrian::Spawn()
{
	FElysiumNpcHuman::Spawn();
}

// STORY8-FORWARD slot 103 0x103a6c80 CNPC_VSabbatLeader::Spawn — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcSabbatLeader::Spawn()
{
	FElysiumNpcVampireBoss::Spawn();
}

// STORY8-FORWARD slot 103 0x103ac430 CNPC_VScurrying::Spawn — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcScurrying::Spawn()
{
	FElysiumNpcAnimal::Spawn();
}

// STORY8-FORWARD slot 103 0x103ad630 CNPC_VRat::Spawn — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcRat::Spawn()
{
	FElysiumNpcScurrying::Spawn();
}

// STORY8-FORWARD slot 103 0x103ae630 CNPC_VSheriffMan::Spawn — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcSheriffMan::Spawn()
{
	FElysiumNpcVampireBoss::Spawn();
}

// STORY8-FORWARD slot 103 0x103b9060 CNPC_VTzimisce::Spawn — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcTzimisce::Spawn()
{
	FElysiumNpcBaseBoss::Spawn();
}

// STORY8-FORWARD slot 103 0x103c1b90 CNPC_VTzimisceHeadClaw::Spawn — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcTzimisceHeadClaw::Spawn()
{
	FElysiumNpcBaseBoss::Spawn();
}

// STORY8-FORWARD slot 103 0x103c3b30 CNPC_VTzimisceRunner::Spawn — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcTzimisceRunner::Spawn()
{
	FElysiumNpcBaseBoss::Spawn();
}

// STORY8-FORWARD slot 103 0x103c4ef0 CNPC_VVampire::Spawn — forwarding stub, not the port; the porter replaces this body.
// Also carries the inherited body of CNPC_VBrujah, CNPC_VGargoyle, CNPC_VVampireBoss.
void FElysiumNpcVampire::Spawn()
{
	FElysiumNpcHuman::Spawn();
}

// STORY8-FORWARD slot 103 0x103caa30 CNPC_VWerewolf::Spawn — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcWerewolf::Spawn()
{
	FElysiumNpcBaseBoss::Spawn();
}

// STORY8-FORWARD slot 103 0x103dd620 CNPC_VYukie::Spawn — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcYukie::Spawn()
{
	FElysiumNpcHumanCombatant::Spawn();
}

// STORY8-FORWARD slot 103 0x103df170 CNPC_VZombie::Spawn — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcZombie::Spawn()
{
	FElysiumNpcAnimal::Spawn();
}

// STORY8-FORWARD slot 144 0x10378da0 CNPC_VGargoyle::Event_Killed — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcGargoyle::Event_Killed(void* Arg0)
{
	FElysiumNpcVampire::Event_Killed(Arg0);
}

// STORY8-FORWARD slot 144 0x10380390 CNPC_VHengeyokai::Event_Killed — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcHengeyokai::Event_Killed(void* Arg0)
{
	FElysiumNpcVampire::Event_Killed(Arg0);
}

// STORY8-FORWARD slot 144 0x1038e8c0 CNPC_VManBat::Event_Killed — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcManBat::Event_Killed(void* Arg0)
{
	FElysiumNpcVampire::Event_Killed(Arg0);
}

// STORY8-FORWARD slot 144 0x10395ba0 CNPC_VMingXiao::Event_Killed — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcMingXiao::Event_Killed(void* Arg0)
{
	FElysiumNpcBaseBoss::Event_Killed(Arg0);
}

// STORY8-FORWARD slot 144 0x1039e900 CNPC_VMingXiaoTentacle::Event_Killed — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcMingXiaoTentacle::Event_Killed(void* Arg0)
{
	FElysiumNpc::Event_Killed(Arg0);
}

// STORY8-FORWARD slot 144 0x103be010 CNPC_VTzimisce::Event_Killed — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcTzimisce::Event_Killed(void* Arg0)
{
	FElysiumNpcBaseBoss::Event_Killed(Arg0);
}

// STORY8-FORWARD slot 144 0x103c1d50 CNPC_VTzimisceHeadClaw::Event_Killed — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcTzimisceHeadClaw::Event_Killed(void* Arg0)
{
	FElysiumNpcBaseBoss::Event_Killed(Arg0);
}

// STORY8-FORWARD slot 301 0x103dfbb0 CNPC_VZombie::CreateCorpse — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcZombie::CreateCorpse(const FVector& Arg0, void* Arg1)
{
	FElysiumNpcAnimal::CreateCorpse(Arg0, Arg1);
}
