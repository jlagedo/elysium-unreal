// Story 0019/8 (29e under the strict verdict), family **Spawn19** -- `CAI_BaseNPC`'s bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Declarations are in `ElysiumNpcBaseSpawn19.inl` (included inside `class FElysiumNpcBase`) or
// generated in `ElysiumNpcBaseSlots.inl` for a slot body.
//
// Owns (Spawn19's `rule` rows): 0x10265ad0 CAI_BaseNPC::Event_Killed, 0x10273200
// CAI_BaseNPC::Spawn. Walked prose: `docs/vtmb/npc-ai/story8/Spawn19.md`.

#include "Substrate/ElysiumNpcBase.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumScriptedSequence.h"
#include "Substrate/ElysiumSchedule.h"

// -------------------------------------------------------------------------------------------------
// Slot 144 -- `CAI_BaseNPC::Event_Killed` `0x10265ad0`
// -------------------------------------------------------------------------------------------------

void FElysiumNpcBase::Event_Killed(void* InInfo)
{
	// `void Event_Killed(CTakeDamageInfo& info)`. The lowered `void*` is the port's
	// `FElysiumTakeDamageInfo`. Retail dereferences it without a test; a null here is a caller that
	// has no damage packet at all, and the body reads it as "no attacker" (named crash guard).
	FElysiumTakeDamageInfo* const Info = static_cast<FElysiumTakeDamageInfo*>(InInfo);

	// The re-entry refusal: an NPC already running `NPC_FREEZE` ignores the kill outright.
	if (Schedule.Current != ElysiumScheduleId::None)                                     // 0x10265adf
	{
		if (Schedule.Current == Spawn19ScheduleOfType(Spawn19SchedNpcFreeze))           // 0x10265aee
		{
			return;                                                                      // 0x10265dc4
		}
	}

	// The scripted-sequence arm, only in NPC_STATE_SCRIPT with a live `m_hCine` (`+0x5d74`).
	if (NpcStateRetail() == Spawn19StateScript)                                          // 0x10265afb
	{
		if (FElysiumScriptedSequence* const Cine = ResolveCine())                        // 0x10265b15
		{
			// A started sequence (`+0x5f91`) whose spawnflags are not exactly `0x80` of `0x2080`
			// DEFERS the death: the whole `CTakeDamageInfo` is copied into `m_DeferredDeathInfo`
			// (`+0x1a48..+0x1a92`) and the body returns with the NPC alive.
			if (Cine->bSequenceStarted                                                   // 0x10265b2f
				&& (Cine->SpawnFlags & Spawn19CineDeferMask) != Spawn19CineDeferNoDefer) // 0x10265b51
			{
				// `0x10265b57..0x10265bf9`: the packet's word 0 is the `CVDmg_t*`; this port's
				// `m_DeferredDeathInfo` is typed as the damage descriptor that word points at, so the
				// copy is the descriptor. The scalar words (inflictor, attacker, damage, bits, ammo,
				// the three flag bytes) have no slot on `FElysiumDmg` -- listed as unrecovered.
				DeferredDeathInfo = (Info != nullptr && Info->Dmg != nullptr) ? *Info->Dmg : FElysiumDmg();
				return;                                                                  // 0x10265c04
			}
			// Otherwise the director is cancelled while the NPC is still in SCRIPT.
			Cine->CancelScript();                                                        // 0x10265c15
			// The death impulse: `npc_vphysics` and a physics object, then slot 39 on the object
			// with slot 263 `GetGroundSpeedVelocity()`.
			if (Spawn19DeathVPhysicsArmed())                                             // 0x10265c27 / 0x10265c34 / 0x10265c3e
			{
				(void)GetGroundSpeedVelocity();                                          // 0x10265c49
				// slot 39 on `m_pPhysicsObject` (`0x10265c6e`) -- unreachable, see the seam.
			}
		}
	}

	StopLoopingSounds();                                                                 // 0x10265c78 slot 511

	// `DeathSound` unless a live grapple partner stands: role `-1`, partner `-1`, a stale serial or a
	// null entity all take the sound.
	bool bPartnerLive = false;
	if (Grapple.Role != EElysiumGrappleRole::None)                                       // 0x10265c85
	{
		if (Grapple.Partner.IsSet())                                                     // 0x10265c90
		{
			bPartnerLive = World != nullptr && World->Resolve(Grapple.Partner) != nullptr; // 0x10265cad / 0x10265cb2
		}
	}
	if (!bPartnerLive)
	{
		DeathSound();                                                                    // 0x10265cb8 slot 488
	}

	const FElysiumEntityHandle Attacker = Info != nullptr ? Info->Attacker : FElysiumEntityHandle::Invalid();
	FireOnDeathOnce(Attacker);                                                           // 0x10265cc8 -> 0x10265a90

	if ((Flags & Spawn19BecomeDeadFlag) != 0)                                            // 0x10265cd7
	{
		// `m_pfnTouch = NULL` (`+0x1ec`, `0x10265cdb`): this runtime has no per-entity touch
		// function word (`ElysiumNpcBaseSpeciesLifecycle10.cpp`'s `TouchFunctionCalls` seam), so
		// there is nothing to clear.
		BecomeDead();                                                                    // 0x10265ce5
	}

	FElysiumCombatCharacter::Event_Killed(InInfo);                                       // 0x10265ced -> 0x1032b9b0

	// `m_SelectIdealStateTrace` = `AI_BaseNPC.cpp:0x217` (absent words), `m_IdealNPCState = DEAD`.
	WriteIdealStateRetail(Spawn19StateDead);                                             // 0x10265d06
	RecordScheduleEvent(FString::Printf(TEXT("SelectIdealState AI_BaseNPC.cpp:%d -> %d"),
		Spawn19KilledLineA, Spawn19StateDead));
	// `(*DAT_10924a6c)->vfunc1()` (`0x10265d18`): a global object's slot 1, no argument, answer
	// discarded -- the same unrecovered call `NextScheduledTask` names; no observable here.

	Cognition.Conditions.Set(EElysiumNpcCond::LightDamage);                              // 0x10265d1f SetCondition(0x4c)

	// `m_hLastDamageEnt` (`+0x5b7c`): the attacker's handle, or `-1` with no attacker.
	BaseMemory.LastDamageAttacker = Attacker.IsSet() ? Attacker : FElysiumEntityHandle::Invalid(); // 0x10265d29 / 0x10265d32 / 0x10265d3a
	Cognition.bCondTookDamage = true;                                                    // 0x10265d46

	VacateSquadSlot();                                                                   // 0x10265d4d -> 0x1028ae60
	if (SquadWord() != 0u)                                                               // 0x10265d5a m_pSquad
	{
		RemoveFromSquad(nullptr);                                                        // 0x10265d5d -> 0x103158f0
	}

	if (ShouldFadeOnDeath())                                                             // 0x10265d66 slot 552 / 0x10265d70
	{
		Spawn19StartFadeOut();                                                           // 0x10265d72 -> 0x102695d0
	}
	else
	{
		Spawn19InsertCarcassSound();                                                     // 0x10265d90 -> 0x101babc0
	}

	// The second stamp, `AI_BaseNPC.cpp:0x241`, the DEAD write again, then `SetState(DEAD)`.
	WriteIdealStateRetail(Spawn19StateDead);                                             // 0x10265db0
	RecordScheduleEvent(FString::Printf(TEXT("SelectIdealState AI_BaseNPC.cpp:%d -> %d"),
		Spawn19KilledLineB, Spawn19StateDead));
	SetState(Spawn19StateDead);                                                          // 0x10265dba -> 0x1026e340
}

void FElysiumNpcBase::BecomeDead()
{
	// `0x10265a40`, in its own order.
	Health = MaxHealth / 2;                                                              // param_1[0x84] = param_1[0x82] / 2
	TakeDamageMode = Spawn19BecomeDeadTakeDamage;                                        // param_1[0x7f] = 2
	MaxHealth = Spawn19BecomeDeadMaxHealth;                                              // param_1[0x82] = 5
	SetMoveType(Spawn19MoveTypeFlyGravity, 0);                                           // (+0x174)(6, 0) slot 93
}

void FElysiumNpcBase::FireOnDeathOnce(const FElysiumEntityHandle& Attacker)
{
	// `0x10265a90`: `if (!+0x5bd4) { m_OnDeath.FireOutput(attacker, this, 0); +0x5bd4 = 1; }`.
	if (HasReportedDeath())
	{
		return;
	}
	static const FName OnDeath(TEXT("OnDeath"));
	FireOutput(OnDeath, Attacker);
	SetDeathReportedForRestore(true);
}

int32 FElysiumNpcBase::Spawn19ScheduleOfType(int32 RetailId)
{
	// `0x102cc1f0`: slot 440 `TranslateSchedule`, slot 446 `GetScheduleOfType`; a miss DevMsgs
	// `"GetScheduleOfType(): No CASE for %d"` and answers `GetScheduleOfType(1)` -- which is the value
	// left in EAX for `Event_Killed`'s compare.
	const int32 Translated = TranslateSchedule(RetailId);
	const int32 GlobalId = ResolveScheduleId(Translated);
	if (ElysiumScheduleFor(GlobalId) != nullptr)
	{
		return GlobalId;
	}
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s GetScheduleOfType(): No CASE for %d"),
		*DebugString(), Translated);
	return ResolveScheduleId(Spawn19SchedMissFallback);
}

void FElysiumNpcBase::Spawn19InsertCarcassSound()
{
	++Spawn19CarcassSoundInserts;
	if (World == nullptr)
	{
		return;
	}
	FElysiumGameSoundRequest Request;
	Request.Position = GetAbsOrigin();                                                   // 0x10265d87 slot 217
	Request.Category = FName(TEXT("SOUND_CARCASS"));
	Request.TypeMask = ElysiumGameSounds::Carcass;                                       // PUSH 0x20
	Request.RadiusCm = Spawn19CarcassVolumeUnits * ElysiumMove::U;                       // PUSH 0x180
	Request.DurationSeconds = Spawn19CarcassDurationSeconds;                             // PUSH 0x41f00000
	Request.Source = FElysiumEntityHandle::Invalid();                                    // PUSH 0 -- no owner
	Request.bForceNonOccludable = true;
	World->GameSounds().Emit(Request, World->NowSeconds());
}

void FElysiumNpcBase::Spawn19StartFadeOut()
{
	++Spawn19FadeOutStarts;
}

bool FElysiumNpcBase::Spawn19DeathVPhysicsArmed() const
{
	return false;
}

// -------------------------------------------------------------------------------------------------
// Slot 103 -- `CAI_BaseNPC::Spawn` `0x10273200`
// -------------------------------------------------------------------------------------------------

void FElysiumNpcBase::Spawn()
{
	// The scope-trace frame (`0x10273205..0x1027326a`, `m_iName` or `"NULL ENTITY"`) is the debug
	// stack this runtime does not carry.

	// `g_pGameRules->FAllowNPCs()`: false removes the entity and spawns nothing.
	if (!Spawn19GameRulesAllowNpcs())                                                    // 0x10273272 / 0x1027327a
	{
		Kill();                                                                          // 0x1027327d UTIL_Remove
		return;                                                                          // 0x10273290
	}

	// The spawn-equipment grant, for a PLAIN `CAI_BaseNPC` only: `m_pBaseNPCTroika` (`+0x98`) set
	// skips it, so no Troika-line NPC is ever equipped here.
	if (AsNpc() == nullptr)                                                              // 0x1027329b
	{
		if ((CapabilitiesGet() & Spawn19CapUseWeapons) != 0                              // 0x102732a1 / 0x102732ac
			&& !AdditionalEquipment.IsEmpty()                                            // 0x102732b6 (null)
			&& !AdditionalEquipment.Equals(TEXT("0"), ESearchCase::CaseSensitive)        // 0x102732d1 2-byte compare
			&& !AdditionalEquipment.Equals(TEXT("item_w_unarmed"), ESearchCase::CaseSensitive)) // 0x102732f2 15-byte compare
		{
			if (FElysiumEntity* const Weapon = Spawn19WeaponCreate(AdditionalEquipment)) // 0x10273306 / 0x1027330d
			{
				Weapon_Equip(Weapon, false);                                             // 0x10273316 slot 383
			}
		}
	}

	CreateVPhysics();                                                                    // 0x10273320 slot 223
	Spawn19CombatCharacterSpawn();                                                       // 0x10273328 -> 0x10323a90
}

void FElysiumNpcBase::Spawn19CombatCharacterSpawn()
{
	// `CBaseCombatCharacter::Spawn` `0x10323a90`: `AddToTeam` on a non-empty `m_sTeamName`, then
	// `CBaseAnimating::Spawn`. The inherited `Spawn` chain below this class is the port's (a no-op on
	// `FElysiumEntity`); the skeletal body is the Troika leaf's presentation.
	++Spawn19CombatCharacterSpawns;
	const FString TeamName = Spawn19TeamName();
	if (!TeamName.IsEmpty())
	{
		Spawn19AddToTeam(TeamName);
	}
	FElysiumScriptedCharacter::Spawn();
}

bool FElysiumNpcBase::Spawn19GameRulesAllowNpcs() const
{
	return true;
}

FElysiumEntity* FElysiumNpcBase::Spawn19WeaponCreate(const FString& ClassName)
{
	++Spawn19WeaponCreateRequests;
	Spawn19LastWeaponCreate = ClassName;
	return nullptr;
}

FString FElysiumNpcBase::Spawn19TeamName() const
{
	return FString();
}

void FElysiumNpcBase::Spawn19AddToTeam(const FString& TeamName)
{
	(void)TeamName;
	++Spawn19AddToTeamCalls;
}
