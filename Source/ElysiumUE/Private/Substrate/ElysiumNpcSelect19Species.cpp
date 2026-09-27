// Story 0019/8 (29e under the strict verdict), family **Select19** -- the species classes' bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// A `STORY8-FORWARD` block is a forwarding override declared on its class (the header's `0019/8
// shape` section): it calls the port base, which is what the inherited dispatch ran, so it changes
// nothing. The porter replaces the body, keeps the declaration, and drops the marker.
//
// Owns (Select19's `rule` rows): 0x10394120 CNPC_VMingXiao::PreSelectSchedule, 0x1035fb50
// CNPC_VAnimal::SelectSchedule, 0x10384ee0 CNPC_VHuman::SelectSchedule, 0x103941e0
// CNPC_VMingXiao::SelectSchedule, 0x103a46b0 CNPC_VPlayerController::PreSelectSchedule, 0x103aa510
// CNPC_VSabbatLeader::PreSelectSchedule, 0x103bb7c0 CNPC_VTzimisce::SelectSchedule, 0x103c1610
// CNPC_VTzimisceHeadClaw::SelectSchedule, 0x103c3310 CNPC_VTzimisceRunner::SelectSchedule,
// 0x10360eb0 CNPC_VAsianVampire::SelectSchedule, 0x1036b250 CNPC_VChangBros::SelectSchedule,
// 0x103742d0 CNPC_VDog::SelectSchedule, 0x10375d90 CNPC_VFrenzyShadow::SelectSchedule, 0x103788d0
// CNPC_VGargoyle::SelectSchedule, 0x1037d130 CNPC_VGuard1::SelectSchedule, 0x1037fca0
// CNPC_VHengeyokai::SelectSchedule, 0x103872d0 CNPC_VHumanCombatant::SelectSchedule, 0x103a29f0
// CNPC_VPedestrian::SelectSchedule, 0x103a70c0 CNPC_VSabbatLeader::SelectSchedule, 0x103ac610
// CNPC_VScurrying::SelectSchedule, 0x103ae8c0 CNPC_VSheriffMan::SelectSchedule, 0x103cee70
// CNPC_VWerewolf::SelectSchedule, 0x103dceb0 CNPC_VWolfMorph::SelectSchedule, 0x103df2e0
// CNPC_VZombie::SelectSchedule, 0x10371ee0 CNPC_VCop::SelectSchedule, 0x1037bd60
// CNPC_VGhoulCroucher::SelectSchedule, 0x10387d20 CNPC_VHumanCombatPatrol::SelectSchedule,
// 0x103dd6b0 CNPC_VYukie::SelectSchedule.

#include "Substrate/ElysiumNpcAnimal.h"
#include "Substrate/ElysiumNpcAsianVampire.h"
#include "Substrate/ElysiumNpcChangBros.h"
#include "Substrate/ElysiumNpcCop.h"
#include "Substrate/ElysiumNpcDog.h"
#include "Substrate/ElysiumNpcGargoyle.h"
#include "Substrate/ElysiumNpcGhoulCroucher.h"
#include "Substrate/ElysiumNpcGuard1.h"
#include "Substrate/ElysiumNpcHengeyokai.h"
#include "Substrate/ElysiumNpcHuman.h"
#include "Substrate/ElysiumNpcHumanCombatPatrol.h"
#include "Substrate/ElysiumNpcHumanCombatant.h"
#include "Substrate/ElysiumNpcMingXiao.h"
#include "Substrate/ElysiumNpcPedestrian.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcScurrying.h"
#include "Substrate/ElysiumNpcSheriffMan.h"
#include "Substrate/ElysiumNpcTzimisce.h"
#include "Substrate/ElysiumNpcTzimisceHeadClaw.h"
#include "Substrate/ElysiumNpcTzimisceRunner.h"
#include "Substrate/ElysiumNpcWerewolf.h"
#include "Substrate/ElysiumNpcYukie.h"
#include "Substrate/ElysiumNpcZombie.h"

// STORY8-FORWARD slot 437 0x10394120 CNPC_VMingXiao::PreSelectSchedule — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcMingXiao::PreSelectSchedule()
{
	return FElysiumNpcBaseBoss::PreSelectSchedule();
}

// STORY8-FORWARD slot 437 0x103aa510 CNPC_VSabbatLeader::PreSelectSchedule — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcSabbatLeader::PreSelectSchedule()
{
	return FElysiumNpcVampireBoss::PreSelectSchedule();
}

// STORY8-FORWARD slot 438 0x1035fb50 CNPC_VAnimal::SpeciesSelectSchedule — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcAnimal::SpeciesSelectSchedule()
{
	return FElysiumNpc::SpeciesSelectSchedule();
}

// STORY8-FORWARD slot 438 0x10360eb0 CNPC_VAsianVampire::SpeciesSelectSchedule — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcAsianVampire::SpeciesSelectSchedule()
{
	return FElysiumNpcVampireBoss::SpeciesSelectSchedule();
}

// STORY8-FORWARD slot 438 0x1036b250 CNPC_VChangBros::SpeciesSelectSchedule — forwarding stub, not the port; the porter replaces this body.
// Also carries the inherited body of CNPC_VChangBrosBlade, CNPC_VChangBrosClaw.
int32 FElysiumNpcChangBros::SpeciesSelectSchedule()
{
	return FElysiumNpcVampireBoss::SpeciesSelectSchedule();
}

// STORY8-FORWARD slot 438 0x10371ee0 CNPC_VCop::SpeciesSelectSchedule — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcCop::SpeciesSelectSchedule()
{
	return FElysiumNpcHumanCombatant::SpeciesSelectSchedule();
}

// STORY8-FORWARD slot 438 0x103742d0 CNPC_VDog::SpeciesSelectSchedule — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcDog::SpeciesSelectSchedule()
{
	return FElysiumNpcAnimal::SpeciesSelectSchedule();
}

// STORY8-FORWARD slot 438 0x103788d0 CNPC_VGargoyle::SpeciesSelectSchedule — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcGargoyle::SpeciesSelectSchedule()
{
	return FElysiumNpcVampire::SpeciesSelectSchedule();
}

// STORY8-FORWARD slot 438 0x1037bd60 CNPC_VGhoulCroucher::SpeciesSelectSchedule — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcGhoulCroucher::SpeciesSelectSchedule()
{
	return FElysiumNpcHumanCombatant::SpeciesSelectSchedule();
}

// STORY8-FORWARD slot 438 0x1037d130 CNPC_VGuard1::SpeciesSelectSchedule — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcGuard1::SpeciesSelectSchedule()
{
	return FElysiumNpcHuman::SpeciesSelectSchedule();
}

// STORY8-FORWARD slot 438 0x1037fca0 CNPC_VHengeyokai::SpeciesSelectSchedule — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcHengeyokai::SpeciesSelectSchedule()
{
	return FElysiumNpcVampire::SpeciesSelectSchedule();
}

// STORY8-FORWARD slot 438 0x10384ee0 CNPC_VHuman::SpeciesSelectSchedule — forwarding stub, not the port; the porter replaces this body.
// Also carries the inherited body of CNPC_VBrujah, CNPC_VLasombra, CNPC_VPlayerController,
// CNPC_VVampire, CNPC_VVampireBoss.
int32 FElysiumNpcHuman::SpeciesSelectSchedule()
{
	return FElysiumNpc::SpeciesSelectSchedule();
}

// STORY8-FORWARD slot 438 0x103872d0 CNPC_VHumanCombatant::SpeciesSelectSchedule — forwarding stub, not the port; the porter replaces this body.
// Also carries the inherited body of CNPC_ProneDialog, CNPC_VSabbatGunman.
int32 FElysiumNpcHumanCombatant::SpeciesSelectSchedule()
{
	return FElysiumNpcHuman::SpeciesSelectSchedule();
}

// STORY8-FORWARD slot 438 0x10387d20 CNPC_VHumanCombatPatrol::SpeciesSelectSchedule — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcHumanCombatPatrol::SpeciesSelectSchedule()
{
	return FElysiumNpcHumanCombatant::SpeciesSelectSchedule();
}

// STORY8-FORWARD slot 438 0x103941e0 CNPC_VMingXiao::SpeciesSelectSchedule — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcMingXiao::SpeciesSelectSchedule()
{
	return FElysiumNpcBaseBoss::SpeciesSelectSchedule();
}

// STORY8-FORWARD slot 438 0x103a29f0 CNPC_VPedestrian::SpeciesSelectSchedule — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcPedestrian::SpeciesSelectSchedule()
{
	return FElysiumNpcHuman::SpeciesSelectSchedule();
}

// STORY8-FORWARD slot 438 0x103a70c0 CNPC_VSabbatLeader::SpeciesSelectSchedule — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcSabbatLeader::SpeciesSelectSchedule()
{
	return FElysiumNpcVampireBoss::SpeciesSelectSchedule();
}

// STORY8-FORWARD slot 438 0x103ac610 CNPC_VScurrying::SpeciesSelectSchedule — forwarding stub, not the port; the porter replaces this body.
// Also carries the inherited body of CNPC_VRat.
int32 FElysiumNpcScurrying::SpeciesSelectSchedule()
{
	return FElysiumNpcAnimal::SpeciesSelectSchedule();
}

// STORY8-FORWARD slot 438 0x103ae8c0 CNPC_VSheriffMan::SpeciesSelectSchedule — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcSheriffMan::SpeciesSelectSchedule()
{
	return FElysiumNpcVampireBoss::SpeciesSelectSchedule();
}

// STORY8-FORWARD slot 438 0x103bb7c0 CNPC_VTzimisce::SpeciesSelectSchedule — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcTzimisce::SpeciesSelectSchedule()
{
	return FElysiumNpcBaseBoss::SpeciesSelectSchedule();
}

// STORY8-FORWARD slot 438 0x103c1610 CNPC_VTzimisceHeadClaw::SpeciesSelectSchedule — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcTzimisceHeadClaw::SpeciesSelectSchedule()
{
	return FElysiumNpcBaseBoss::SpeciesSelectSchedule();
}

// STORY8-FORWARD slot 438 0x103c3310 CNPC_VTzimisceRunner::SpeciesSelectSchedule — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcTzimisceRunner::SpeciesSelectSchedule()
{
	return FElysiumNpcBaseBoss::SpeciesSelectSchedule();
}

// STORY8-FORWARD slot 438 0x103cee70 CNPC_VWerewolf::SpeciesSelectSchedule — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcWerewolf::SpeciesSelectSchedule()
{
	return FElysiumNpcBaseBoss::SpeciesSelectSchedule();
}

// STORY8-FORWARD slot 438 0x103dd6b0 CNPC_VYukie::SpeciesSelectSchedule — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcYukie::SpeciesSelectSchedule()
{
	return FElysiumNpcHumanCombatant::SpeciesSelectSchedule();
}

// STORY8-FORWARD slot 438 0x103df2e0 CNPC_VZombie::SpeciesSelectSchedule — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcZombie::SpeciesSelectSchedule()
{
	return FElysiumNpcAnimal::SpeciesSelectSchedule();
}
