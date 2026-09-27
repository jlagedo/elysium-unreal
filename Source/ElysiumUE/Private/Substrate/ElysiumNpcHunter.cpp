#include "Substrate/ElysiumNpcHunter.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumStub.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcConditions10Shared.h"
#include "Substrate/ElysiumNpcLifecycle19_2Shared.h"
#include "Substrate/ElysiumNpcSenses10Shared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcSpeciesLifecycle10Shared.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// Slot 420: `0x10388b30`.
// `0x10388b30`
void FElysiumNpcHunter::NPCInit()
{
	HumanCombatantNPCInit();                                             // hides once inside
	HideActiveWeaponIfAny();                                             // second Hide
}

// Slot 463: `0x10388880`, the pursuit pre-step, then a direct call into
// `CNPC_VHumanCombatant::OnStateChange` (`0x103871c0`, thunk `0x10014a10`).
/** `CNPC_VHunter::OnStateChange` (`0x10388880`)'s pre-step, two independent arms in this order:
 *    1. `if (GetEnemy() && GetEnemy() && GetEnemy()->m_pPlayer && NewState == 2)` —
 *       `0x10388c40(this)` (the relationship, no latch), `OnHunterPursuitStart(player)`, then
 *       `m_hPursuitPlayer = player`'s own handle.
 *    2. `if (OldState == 2 && m_hPursuitPlayer resolves live && its m_pPlayer != 0)` —
 *       `m_hPursuitPlayer` = invalid, then `OnHunterPursuitStop(player)`, IN THAT ORDER (the clear
 *       is `1038891e`, the release after it).
 *  State 2 is `COMBAT`. Both arms can fire on the same call, and retail evaluates arm 2 against the
 *  handle arm 1 may just have written. */
void FElysiumNpcHunter::OnStateChange(EElysiumNpcState OldState, EElysiumNpcState NewState)
{
	// `CNPC_VHunter::OnStateChange` `0x10388880`, two independent arms in retail's order.
	// Retail's states are `m_NPCState` ids; 2 is COMBAT.
	if (GetEnemyEntity() != nullptr && GetEnemyEntity() != nullptr)
	{
		FElysiumEntity* Enemy = GetEnemyEntity();
		if (NpcKernelSpeciesLifecycle10Shared::SpeciesLifecycle10IsPlayer(*this, Enemy) && NewState == EElysiumNpcState::Combat)
		{
			HunterHatePlayer();                                 // 0x10388c40
			// `0x1017f7b0`, ported by family Conditions with its receiver correction: the
			// refcount is the PLAYER's `+0x1d14`, this runtime's
			// `FElysiumPoliceState::HuntersInPursuit`. Called, not restated.
			if (FElysiumPlayer* PlayerRecord = World != nullptr ? World->FindPlayer() : nullptr)
			{
				OnHunterPursuitStart(*PlayerRecord);
			}
			++HunterPursuitStarts;
			// `1038890a MOV EAX,[EAX]` off the player's `GetRefEHandle()` — the player's own
			// handle, cached on the hunter.
			HunterPursuitPlayer = Enemy->Handle;
		}
	}
	if (OldState == EElysiumNpcState::Combat && HunterPursuitPlayer.IsSet())
	{
		FElysiumEntity* Pursued = World != nullptr ? World->Resolve(HunterPursuitPlayer) : nullptr;
		if (NpcKernelSpeciesLifecycle10Shared::SpeciesLifecycle10IsPlayer(*this, Pursued))
		{
			// The CLEAR comes first (`1038891e`), the release second.
			HunterPursuitPlayer = FElysiumEntityHandle();
			if (FElysiumPlayer* PlayerRecord = World != nullptr ? World->FindPlayer() : nullptr)
			{
				OnHunterPursuitStop(*PlayerRecord);             // 0x1017f830
			}
			++HunterPursuitStops;
		}
	}
	FElysiumNpcHumanCombatant::OnStateChange(OldState, NewState);
}

// Slot 461: `0x10388ab0`, the selector tag 0x17 and then a direct call into the combatant's `0x10387380`.
int32 FElysiumNpcHunter::SelectIdealStateRetail()
{
	SelectIdealStateSelector = 0x17;
	return HumanCombatPatrolSelectIdealState();
}

// Slot 472: `0x103887d0`
void FElysiumNpcHunter::OnSeeEntity(FElysiumEntity* Seen)
{
	// `103887d0`: the twin.
	if (!Senses.Memory.bPlayerInOuterBand && IRelationTypeOf(Seen) == NpcKernelSenses10Shared::GD_HT)
	{
		StampHunterSuspect(Seen);
	}
	FElysiumNpc::OnSeeEntity(Seen);
}

// Slot 404: `0x10388bb0`.
/** `CNPC_VHunter::IRelationType` (`0x10388bb0`), 109 bytes — the cop arm minus both player-side
 *  tests: a null candidate answers `D_ER`, the hunter class's own static grudge answers `D_HT`, and
 *  everything else defers. A hunter's extra hostility comes only from that one shared timer. */
int32 FElysiumNpcHunter::IRelationType(FElysiumEntity* Candidate)
{
	// `CNPC_VHunter::IRelationType` (`0x10388bb0`), 109 bytes — the cop arm minus BOTH player-side
	// tests. A hunter's extra hostility comes only from the one shared timed grudge.
	if (Candidate == nullptr)                                            // 10388bb6
	{
		return NpcKernelConditions10Shared::GCond10_D_ER;
	}
	if (World != nullptr)
	{
		// `DAT_1093b650` / `_DAT_1093b658`, written only by `0x10387fd0` and read by nothing but
		// this body.
		const FElysiumEntity* const Suspect = World->Resolve(HunterSuspectHandle());
		if (Suspect == Candidate && NpcKernelConditions10Shared::Cond10Now(*this) < HunterSuspectExpiry())   // 10388bf0
		{
			return NpcKernelConditions10Shared::GCond10_D_HT;
		}
	}
	return TroikaIRelationType(Candidate);                               // 10388c1a
}

// Slot 440: `0x10388a40`.
// `0x10388a40`
// `0x10388a40`, `CNPC_VHunter::TranslateSchedule`, the body of `FElysiumNpcHunter::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpcHunter::TranslateScheduleRetail(int32 ScheduleNumber)
{
	if (ScheduleNumber == 0x6b)
	{
		return NpcFlags.Has(EElysiumNpcFlag2::D_MILDLY_CRAZY) ? 0x132 : 0x15b;
	}
	if (ScheduleNumber == 0x103) { return 0x15a; }
	return TroikaTranslateScheduleRetail(ScheduleNumber);
}

// Slot 546: `0x10388200`, the class's own schedule id space.
const TCHAR* FElysiumNpcHunter::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1093b5e8`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VHunter"), TEXT("0x10388200"), TEXT("0x1093b5e8") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}

// --- Moved from `ElysiumNpcConditions10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcLifecycle19_2.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcSenses10.cpp` (story 5 step 4) ---

FElysiumEntityHandle FElysiumNpcHunter::HunterSuspectHandle() { return NpcKernelSenses10Shared::GHunterSuspect; }

double FElysiumNpcHunter::HunterSuspectExpiry() { return NpcKernelSenses10Shared::GHunterSuspectExpiry; }

void FElysiumNpcHunter::StampHunterSuspect(FElysiumEntity* Seen)
{
	// `0x10387fd0`: the same pair over the hunter's own globals, with NO `+0xa8` guard — the one
	// difference between the two 54-byte twins.
	if (Seen == nullptr)
	{
		return;
	}
	NpcKernelSenses10Shared::GHunterSuspectExpiry = NpcKernelSenses10Shared::NowOf(*this) + SpeciesSuspectWindowSeconds;
	NpcKernelSenses10Shared::GHunterSuspect = Seen->Handle;
}

// --- Moved from `ElysiumNpcSpeciesLifecycle10.cpp` (story 5 step 4) ---

const TCHAR* FElysiumNpcHunter::PlayerHateRelationshipSpec()
{
	// `s_player_D_HT_10_1063bc28` — `'player D_HT 10'`, with spaces, read off the listing at
	// `1037e2d8` / `10388c48`. Lower-case `player`, unlike `CNPC_VCop`'s `"Player D_HT 10"`
	// (`0x106366f4`); family SpeciesMisc10 recorded the same difference for `0x1037e2d0`.
	return TEXT("player D_HT 10");
}

void FElysiumNpcHunter::HunterHatePlayer()
{
	// `FUN_10388c40` `0x10388c40` — `CNPC_VGuard1`'s `0x1037e2d0` (family SpeciesMisc10's
	// `Guard1HatePlayer`) minus the `+0x6660` latch byte, and the byte is the only difference.
	//
	// `thunk_FUN_10273790(this, "player D_HT 10", 0)` — `InputSetRelationship`, whose port is
	// `FElysiumNpc::InputSetRelationship` and whose parser already takes the three-token grammar.
	FElysiumInputArgs Args;
	Args.Param = FElysiumVariant::String(PlayerHateRelationshipSpec());
	Args.Activator = Handle;
	Args.Caller = Handle;
	Args.Input = FName(TEXT("SetRelationship"));
	InputSetRelationship(Args);
	++PlayerHateRelationshipSets;
}

// --- Moved from `ElysiumNpcTranslate19.cpp` (story 5 step 4) ---

