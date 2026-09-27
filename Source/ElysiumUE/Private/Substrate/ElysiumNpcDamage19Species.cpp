// Story 0019/8 (29e under the strict verdict), family **Damage19** -- the species classes' bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// A `STORY8-FORWARD` block is a forwarding override declared on its class (the header's `0019/8
// shape` section): it calls the port base, which is what the inherited dispatch ran, so it changes
// nothing. The porter replaces the body, keeps the declaration, and drops the marker.
//
// Owns (Damage19's `rule` rows): 0x10378d30 CNPC_VGargoyle::PlayerKnockbackReaction, 0x1037a5b0
// CNPC_VGargoyle::UpdatePresenceEffect, 0x10380320 CNPC_VHengeyokai::PlayerKnockbackReaction,
// 0x10381b10 CNPC_VHengeyokai::UpdatePresenceEffect, 0x103ab270
// CNPC_VSabbatLeader::UpdatePresenceEffect, 0x103c43f0
// CNPC_VTzimisceRunner::PlayerKnockbackReaction, 0x102bed30 CNPC_VVampire::OnTakeDamage, 0x1035e6d0
// CNPC_VAndreiBlood::OnTakeDamage_Alive, 0x103601a0 CNPC_VAnimal::FUN_103601a0, 0x10363c70
// CNPC_VBach::vfunc390, 0x10378c10 CNPC_VGargoyle::vfunc390, 0x1037bc90
// CNPC_VGhoulCroucher::OnTakeDamage_Alive, 0x103801d0 CNPC_VHengeyokai::vfunc390, 0x1038e880
// CNPC_VManBat::vfunc390, 0x10395ae0 CNPC_VMingXiao::vfunc390, 0x1039e890
// CNPC_VMingXiaoTentacle::vfunc390, 0x103aa480 CNPC_VSabbatLeader::OnTakeDamage_Alive, 0x103b0e90
// CNPC_VSheriffMan::OnTakeDamage_Alive, 0x103cccc0 CNPC_VWerewolf::OnTakeDamage, 0x103e06d0
// CNPC_VZombie::OnTakeDamage.

#include "Substrate/ElysiumNpcAndreiBlood.h"
#include "Substrate/ElysiumNpcAnimal.h"
#include "Substrate/ElysiumNpcBach.h"
#include "Substrate/ElysiumNpcGargoyle.h"
#include "Substrate/ElysiumNpcGhoulCroucher.h"
#include "Substrate/ElysiumNpcHengeyokai.h"
#include "Substrate/ElysiumNpcManBat.h"
#include "Substrate/ElysiumNpcMingXiao.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcSheriffMan.h"
#include "Substrate/ElysiumNpcTzimisceRunner.h"
#include "Substrate/ElysiumNpcWerewolf.h"
#include "Substrate/ElysiumNpcZombie.h"

// STORY8-FORWARD slot 142 0x103cccc0 CNPC_VWerewolf::OnTakeDamage — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcWerewolf::OnTakeDamage(void* Arg0)
{
	return FElysiumNpcBaseBoss::OnTakeDamage(Arg0);
}

// STORY8-FORWARD slot 142 0x103e06d0 CNPC_VZombie::OnTakeDamage — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcZombie::OnTakeDamage(void* Arg0)
{
	return FElysiumNpcAnimal::OnTakeDamage(Arg0);
}

// STORY8-FORWARD slot 313 0x1037a5b0 CNPC_VGargoyle::UpdatePresenceEffect — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcGargoyle::UpdatePresenceEffect()
{
	FElysiumNpcVampire::UpdatePresenceEffect();
}

// STORY8-FORWARD slot 313 0x10381b10 CNPC_VHengeyokai::UpdatePresenceEffect — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcHengeyokai::UpdatePresenceEffect()
{
	FElysiumNpcVampire::UpdatePresenceEffect();
}

// STORY8-FORWARD slot 313 0x103ab270 CNPC_VSabbatLeader::UpdatePresenceEffect — forwarding stub, not the port; the porter replaces this body.
void FElysiumNpcSabbatLeader::UpdatePresenceEffect()
{
	FElysiumNpcVampireBoss::UpdatePresenceEffect();
}

// STORY8-FORWARD slot 320 0x10378d30 CNPC_VGargoyle::PlayerKnockbackReaction — forwarding stub, not the port; the porter replaces this body.
bool FElysiumNpcGargoyle::PlayerKnockbackReaction(FElysiumEntity* Arg0, int32 Arg1)
{
	return FElysiumNpcVampire::PlayerKnockbackReaction(Arg0, Arg1);
}

// STORY8-FORWARD slot 320 0x10380320 CNPC_VHengeyokai::PlayerKnockbackReaction — forwarding stub, not the port; the porter replaces this body.
bool FElysiumNpcHengeyokai::PlayerKnockbackReaction(FElysiumEntity* Arg0, int32 Arg1)
{
	return FElysiumNpcVampire::PlayerKnockbackReaction(Arg0, Arg1);
}

// STORY8-FORWARD slot 320 0x103c43f0 CNPC_VTzimisceRunner::PlayerKnockbackReaction — forwarding stub, not the port; the porter replaces this body.
bool FElysiumNpcTzimisceRunner::PlayerKnockbackReaction(FElysiumEntity* Arg0, int32 Arg1)
{
	return FElysiumNpcBaseBoss::PlayerKnockbackReaction(Arg0, Arg1);
}

// STORY8-FORWARD slot 390 0x1035e6d0 CNPC_VAndreiBlood::OnTakeDamage_Alive — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcAndreiBlood::OnTakeDamage_Alive(void* Arg0)
{
	return FElysiumNpcVampireBoss::OnTakeDamage_Alive(Arg0);
}

// STORY8-FORWARD slot 390 0x103601a0 CNPC_VAnimal::OnTakeDamage_Alive — forwarding stub, not the port; the porter replaces this body.
// Also carries the inherited body of CNPC_VDog, CNPC_VRat, CNPC_VScurrying, CNPC_VZombie.
int32 FElysiumNpcAnimal::OnTakeDamage_Alive(void* Arg0)
{
	return FElysiumNpc::OnTakeDamage_Alive(Arg0);
}

// STORY8-FORWARD slot 390 0x10363c70 CNPC_VBach::OnTakeDamage_Alive — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcBach::OnTakeDamage_Alive(void* Arg0)
{
	return FElysiumNpcVampire::OnTakeDamage_Alive(Arg0);
}

// STORY8-FORWARD slot 390 0x10378c10 CNPC_VGargoyle::OnTakeDamage_Alive — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcGargoyle::OnTakeDamage_Alive(void* Arg0)
{
	return FElysiumNpcVampire::OnTakeDamage_Alive(Arg0);
}

// STORY8-FORWARD slot 390 0x1037bc90 CNPC_VGhoulCroucher::OnTakeDamage_Alive — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcGhoulCroucher::OnTakeDamage_Alive(void* Arg0)
{
	return FElysiumNpcHumanCombatant::OnTakeDamage_Alive(Arg0);
}

// STORY8-FORWARD slot 390 0x103801d0 CNPC_VHengeyokai::OnTakeDamage_Alive — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcHengeyokai::OnTakeDamage_Alive(void* Arg0)
{
	return FElysiumNpcVampire::OnTakeDamage_Alive(Arg0);
}

// STORY8-FORWARD slot 390 0x1038e880 CNPC_VManBat::OnTakeDamage_Alive — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcManBat::OnTakeDamage_Alive(void* Arg0)
{
	return FElysiumNpcVampire::OnTakeDamage_Alive(Arg0);
}

// STORY8-FORWARD slot 390 0x10395ae0 CNPC_VMingXiao::OnTakeDamage_Alive — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcMingXiao::OnTakeDamage_Alive(void* Arg0)
{
	return FElysiumNpcBaseBoss::OnTakeDamage_Alive(Arg0);
}

// STORY8-FORWARD slot 390 0x1039e890 CNPC_VMingXiaoTentacle::OnTakeDamage_Alive — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcMingXiaoTentacle::OnTakeDamage_Alive(void* Arg0)
{
	return FElysiumNpc::OnTakeDamage_Alive(Arg0);
}

// STORY8-FORWARD slot 390 0x103aa480 CNPC_VSabbatLeader::OnTakeDamage_Alive — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcSabbatLeader::OnTakeDamage_Alive(void* Arg0)
{
	return FElysiumNpcVampireBoss::OnTakeDamage_Alive(Arg0);
}

// STORY8-FORWARD slot 390 0x103b0e90 CNPC_VSheriffMan::OnTakeDamage_Alive — forwarding stub, not the port; the porter replaces this body.
int32 FElysiumNpcSheriffMan::OnTakeDamage_Alive(void* Arg0)
{
	return FElysiumNpcVampireBoss::OnTakeDamage_Alive(Arg0);
}
