// Story 0019/8 (29e under the strict verdict), family **Damaged19** -- the species classes' bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// A `STORY8-FORWARD` block is a forwarding override declared on its class (the header's `0019/8
// shape` section): it calls the port base, which is what the inherited dispatch ran, so it changes
// nothing. The porter replaces the body, keeps the declaration, and drops the marker.
//
// Owns (Damaged19's `rule` rows): 0x103c43b0 CNPC_VTzimisceRunner::vfunc330 ‼, 0x1037e240
// CNPC_VGuard1::NPCInit ‼, 0x10387140 CNPC_VHumanCombatant::NPCInit ‼, 0x103dd800
// CNPC_VYukie::NPCInit ‼, 0x103c32c0 CNPC_VTzimisceRunner::HandleAnimEvent ‼, 0x103a4700
// CNPC_VPlayerController::NPCThink ‼, 0x103cb590 CNPC_VWerewolf::NPCThink ‼, 0x10371b70
// CNPC_VCop::StartTask ‼.

#include "Substrate/ElysiumNpcCop.h"
#include "Substrate/ElysiumNpcTzimisceRunner.h"
#include "Substrate/ElysiumNpcWerewolf.h"

#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumNpcKernelTunables.h"

// Slot 330 `0x103c43b0` `CNPC_VTzimisceRunner::vfunc330(float, FireBulletsInfo_t*)` — story 8, lane
// L12. 42 bytes and NO jump table (the decompiler's "indirect jump" is the tail `JMP [vtbl+0x500]`):
// the near-miss bullet reaction becomes slot 320 `PlayerKnockbackReaction(shooter, 0x79)` on this
// Runner, in place of the base's distance-graded flinch (`0x1029fbe0`). The float is never read.
void FElysiumNpcTzimisceRunner::Slot330(float Arg0, void* Arg1)
{
	(void)Arg0;
	const FFireBulletsInfo* Info = static_cast<const FFireBulletsInfo*>(Arg1);
	if (Info == nullptr)
	{
		// NAMED CRASH GUARD: retail dereferences the info pointer unconditionally (`0x103c43b6`).
		return;
	}
	// `arg = info->+0x94 ? info->+0x94->+0x9c : 0` — the attacker's cached combat-character pointer.
	FElysiumEntity* Shooter = nullptr;                                      // 0x103c43b4
	if (Info->Attacker != nullptr)                                          // 0x103c43b6 / 0x103c43be
	{
		Shooter = Info->Attacker->AsCombatCharacter();                      // 0x103c43c0 +0x9c
	}
	// The two incoming stack slots are overwritten in place (`0x103c43c8` activity `0x79`,
	// `0x103c43d0` the shooter) and slot 320 is tail-jumped on THIS object's own vtable.
	PlayerKnockbackReaction(Shooter, 0x79);                                 // 0x103c43d4 slot 320
}

void FElysiumNpcWerewolf::WerewolfEnableDebugStuff()
{
	// SEAM: `EnableDebugStuff` `0x103dbad0` (see the declaration).
	++WerewolfEnableDebugStuffCalls;
}

void FElysiumNpcWerewolf::WerewolfDrawHintOverlay()
{
	// SEAM: the hint overlay `0x103cb4b0` (see the declaration).
	++WerewolfDrawHintOverlayCalls;
}

int32 FElysiumNpcWerewolf::WerewolfThinkEngineTick() const
{
	// NAMED DIVERGENCE (see the declaration): whole frames on the world clock.
	if (World == nullptr)
	{
		return 0;
	}
	const double Frame = World->FrameSeconds();
	return Frame > 0.0 ? FMath::FloorToInt32(World->NowSeconds() / Frame) : 0;
}

// Slot 431: `0x103cb590`, 404 bytes (story 8 lane L13b). The decompiler's "unrecovered jumptable" is
// the tick-mod-5 round robin through the five-dword table `0x103cb754`; every arm rejoins `0x103cb6cf`.
void FElysiumNpcWerewolf::NPCThink()
{
	// Scope trace "CNPC_VWerewolf::NPCThink" (`0x10661ac4`): absent.
	// `werewolf_show_debug` (`*0x1093f73c`, `IsCommand() ? 0 : +0x2c`) -> `EnableDebugStuff(this)`; the
	// think falls through.
	if (ElysiumNpcTunables::ConVarInt(ElysiumNpcTunables::EConVar::WerewolfShowDebug) != 0)  // 0x103cb602 / 0x103cb607 / 0x103cb613
	{
		WerewolfEnableDebugStuff();                                         // 0x103cb616 0x103dbad0
	}
	// `werewolf_draw_hints` (`*0x1093f95c`) -> the overlay, slot 614, and RETURN: no AI this think.
	if (ElysiumNpcTunables::ConVarInt(ElysiumNpcTunables::EConVar::WerewolfDrawHints) != 0)  // 0x103cb626 / 0x103cb62b / 0x103cb637
	{
		WerewolfDrawHintOverlay();                                          // 0x103cb63b 0x103cb4b0
		ResetThinkTimers(World != nullptr ? World->NowSeconds() : 0.0);     // 0x103cb644 slot 614
		return;                                                             // 0x103cb652
	}
	// `+0x66a0 == 0` OR the hint-data count `+0x6720 == 0` -> build the hint data and open the zones.
	if (!bWerewolfHintDataInitialized || WerewolfHintGroundpoints.Num() == 0)  // 0x103cb65b / 0x103cb665
	{
		InitializeHintData();                                               // 0x103cb669 0x103d7710
		TriggerWerewolfZone();                                              // 0x103cb670 0x103cade0
		bWerewolfHintDataInitialized = true;                                // 0x103cb675
	}
	// `g_AIDisabled` (`DAT_1092053c`) bit 0 SET skips EVERYTHING to slot 614 -- the Troika body
	// included (the checklist prose implies only the teleport and round robin are skipped).
	if (World == nullptr || World->IsAiEnabled())                           // 0x103cb67c / 0x103cb683
	{
		if ((WerewolfHintFlags & 0x20u) == 0x20u)                           // 0x103cb689..0x103cb694
		{
			TeleportOut();                                                  // 0x103cb698 0x103d4a60
		}
		if (WerewolfSlot167Enemy() != nullptr)                              // 0x103cb6a1 slot 167 / 0x103cb6a9
		{
			// `tick % 5`, a signed `IDIV`. NAMED CRASH GUARD: a negative remainder would index before
			// the table in retail; it dispatches nothing here.
			const int32 Arm = WerewolfThinkEngineTick() % 5;                // 0x103cb6b3 slot 120 / 0x103cb6b9..0x103cb6bf
			WerewolfLastRoundRobinArm = Arm >= 0 ? Arm : INDEX_NONE;
			switch (Arm)                                                    // 0x103cb6c1 JMP [EDX*4 + 0x103cb754]
			{
			case 0:
				UpdateConditionCanTeleport();                               // 0x103cb6c8 0x103cc0d0
				break;
			case 1:
				UpdateConditionEnemyUnreachable();                          // 0x103cb72b 0x103cc320
				break;
			case 2:
				UpdateConditionDeathTriggered();                            // 0x103cb734 0x103cc890
				break;
			case 3:
				UpdateConditionCanSpecialMove();                            // 0x103cb73d 0x103cc5c0
				break;
			case 4:
				// `CheckStuck(1)` -- the one caller that passes 1 (the header's "no shipped caller"
				// missed it: this arm is not in the decompiled C).
				WerewolfCheckStuck(EStuckEscape::MayTeleport);              // 0x103cb746 0x103cb920
				break;
			default:
				break;
			}
		}
		FElysiumEntity* Enemy = WerewolfSlot167Enemy();                     // 0x103cb6d3 slot 167
		if (Enemy != nullptr)                                               // 0x103cb6db
		{
			Slot600(WerewolfSlot167Enemy());                                // 0x103cb6e2 slot 167 / 0x103cb6eb slot 600
		}
		FElysiumNpc::NPCThink();                                            // 0x103cb6f4 0x10292de0
		if (bIsUsingSmallHull && IsViewable())                              // 0x103cb6f9 / 0x103cb701 / 0x103cb707 slot 163 / 0x103cb70f
		{
			UpdateFakeHull(World != nullptr ? World->NowSeconds() : 0.0);   // 0x103cb713 0x103d93b0
		}
	}
	ResetThinkTimers(World != nullptr ? World->NowSeconds() : 0.0);         // 0x103cb71c slot 614
}

// `0x10371b70 CNPC_VCop::StartTask` (slot 442) is ported by lane L04 in
// `ElysiumNpcStartTask19Species.cpp`, beside the other species StartTask bodies.
