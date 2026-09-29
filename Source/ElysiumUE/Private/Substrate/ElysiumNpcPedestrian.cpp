#include "Substrate/ElysiumNpcPedestrian.h"
#include "Substrate/ElysiumNpcKernelTunables.h"

#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumStub.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcConditions10Shared.h"
#include "Substrate/ElysiumNpcLifecycle2Shared.h"
#include "Substrate/ElysiumNpcLifecycle2_2Shared.h"
#include "Substrate/ElysiumNpcSensesBodiesShared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcSpeciesMisc10_2Shared.h"
#include "Substrate/ElysiumNpcState_2Shared.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// --- File-scope helpers moved with this class's bodies (story 5 step 4) ---

namespace
{
	// `0x1042fe90`, its inverse, which `CNPC_VPedestrian::vfunc461` (`0x103a2e30`) applies to the
	// unscrambled word.
	uint32 SecureUnhash(uint32 Value)
	{
		return Value
			^ ((((Value & NpcKernelSensesShared::GSecureHashMaskA) ^ NpcKernelSensesShared::GSecureHashXorA) + NpcKernelSensesShared::GSecureHashAddA)
				^ NpcKernelSensesShared::GSecureHashXorB) & NpcKernelSensesShared::GSecureHashMaskB
			^ NpcKernelSensesShared::GSecureHashXor;
	}
	// `_DAT_10483aac` = **512.0f** and `_DAT_1049dfe4` = **262144.0f** (= 512^2), both read out of
	// the pinned `vampire.dll`'s `.rdata` (the corpus holds neither cell). `CNPC_VPedestrian`'s two
	// distance terms are therefore the same 512 units, one against `m_flPlayerDist` and one against
	// a squared separation.
	constexpr float GState19_2PedestrianFleeUnits = ElysiumNpcTunables::FiveHundredTwelve;   // _DAT_10483aac
	constexpr float GState19_2PedestrianFleeUnitsSq = ElysiumNpcTunables::FiveHundredTwelveSquared;   // _DAT_1049dfe4
	/** `CNPC_VPedestrian::vfunc461`'s witness block (`0x103a2f1e`, repeated verbatim at
	 *  `0x103a3068`): the pedestrian that flees a gunshot first REPORTS it —
	 *  `RecordCriminalWitness` (`0x1028ea60`) with the active weapon's crime level and the player's
	 *  own origin, then `PlayerCriminalIncident` (`0x1017f2a0`) with the level read back out of
	 *  `m_iPLCriminalLevelWitnessed` and the location just stored. Retail's whole block is inside
	 *  `if (GetActiveWeapon() != 0)`.
	 *
	 *  SEAM: the level is `weaponData +0x3c4`, resolved by `0x102517e0`. This runtime's weapon
	 *  record carries no crime-level column, so `PedestrianWeaponCrimeLevel` answers `-1` — "the
	 *  player is holding nothing this pedestrian would report" — and the arm's observable half (the
	 *  flee promote and slot 596) runs anyway, exactly as retail's does for an unarmed player. */
	void State19_2PedestrianWitnessWeapon(FElysiumNpc& Npc, FElysiumEntity* Closest)
	{
		if (Closest == nullptr || Npc.PedestrianWeaponCrimeLevel < 0)
		{
			return;
		}
		Npc.RecordCriminalWitness(Npc.PedestrianWeaponCrimeLevel, Closest->Origin, Closest);
		++Npc.PedestrianCrimeReports;
		FElysiumPlayer* const Player = Npc.World != nullptr ? Npc.World->FindPlayer() : nullptr;
		if (Player != nullptr && static_cast<FElysiumEntity*>(Player) == Closest)
		{
			const FElysiumNpcWitnessChannel& Criminal =
				Npc.Witness.Channel(ElysiumNpcWitness::EChannel::Criminal);
			ElysiumLaw::PlayerCriminalIncident(*Player, Criminal.Level, Npc.Handle,
				Criminal.Location);
		}
	}
}

// Slot 420: `0x103a2570`.
// `0x103a2570`
void FElysiumNpcPedestrian::NPCInit()
{
	TroikaNPCInit();
	bPedestrianFirstThink = false;
	Senses.Memory.NextFleeSoundTime = 0.0;
}

// Slot 130: `0x103a25a0`, which calls the Troika body first.
// `0x103a25a0`
void FElysiumNpcPedestrian::OnRestore(bool bFromLoad)
{
	TroikaOnRestore(bFromLoad);
	if (!bFromLoad)
	{
		return;
	}
	if (PedestrianLevelResetType == 2)
	{
		return;
	}
	if (!IsAlive() && PedestrianLevelResetType != 0)
	{
		return;
	}
	if (bHidden)                                                         // 100b5190
	{
		return;
	}
	++InventoryDestroys;
	if (!IsAlive())
	{
		// `0x101cf390` with pre-death mins/maxs at +0x6660/+0x666c. No collision OBB
		// member on this leaf; the two vectors are the restored words.
		(void)PedestrianPreDeathMinsUnits;
		(void)PedestrianPreDeathMaxsUnits;
	}
	NPCInit();                                                           // slot 420
	SetFollowerBossName(FString());                                      // 102c4430 ""
	Origin = InitialPosition;                                            // 101cf5c0 +0x62a8
	Angles = InitialAngles;                                              // 103a264e slot 64 +0x62b4
	// `103a26b4`–`103a27ff`: the collision block. `SetSolidFlags(0)`, then two `AddSolidFlags` of
	// the CURRENT 16-bit word (`+0x2b4`) ORed with `1` and then `0x40`, then `SetSolid(SOLID_BBOX)`.
	// SEAM: family Motor10's solid record is what this substrate has for a collision property, and
	// it reproduces retail's read-OR-pass-back-and-OR-again shape.
	RetailSolidFlags = 0;                                                // 103a26b4
	RetailSolidFlags |= (RetailSolidFlags & 0xffffu) | 1u;               // 103a2726
	RetailSolidFlags |= (RetailSolidFlags & 0xffffu) | 0x40u;            // 103a2798
	RetailSolidType = 2;                                                 // 103a27ff SOLID_BBOX
	++RetailSolidSets;
	SetMoveType(4, 0);                                                   // 103a2816 slot 93
	SetHullSizeNormal(false);                                            // 103a2820 10273070
	++RestoreRelinkCalls;                                                // 103a2826 CBaseEntity::Relink
	CreateVPhysics();                                                    // 103a2832 slot 223
}

// Slot 461: `0x103a2e30`, chaining the human line's `0x103851e0` directly.
// `CNPC_VPedestrian::vfunc461` (`0x103a2e30`) — slot 461. Every arm that takes answers FLEE (8);
// the body is a ladder of reasons to run. Only states 1, 3 and 8 are its own.
int32 FElysiumNpcPedestrian::SelectIdealStateRetail()
{
	const int32 State = NpcStateRetail();
	if (State != 1 && State != 3)
	{
		if (State != 8)
		{
			return NpcKernelState19_2Shared::State19_2ChainHuman(*this);
		}
		// `103a3450`: already fleeing — stay fleeing, with no test at all.
		NpcKernelState19_2Shared::State19_2Stamp(*this, 8, 0x375);
		return IdealStateRetail();
	}

	// `103a2e6b`: three damage interrupts, one answer. The attacker, not the closest player, is
	// what slot 596 is handed here.
	if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::LightDamage)
		|| NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::HeavyDamage)
		|| NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::RepeatedDamage))
	{
		Cognition.bCondTookDamage = false;
		Slot596(NpcKernelState19_2Shared::State19_2Resolve(*this, BaseMemory.LastDamageAttacker));
		NpcKernelState19_2Shared::State19_2Stamp(*this, 8, 0x308);
		return IdealStateRetail();
	}

	FElysiumEntity* const Closest = NpcKernelState19_2Shared::State19_2Resolve(*this, Senses.Memory.ClosestPlayer);
	const float PlayerDistUnits =
		static_cast<float>(Senses.Memory.ClosestPlayerDistanceCm / ElysiumMove::U);

	// `103a2ecb`: the combat-sound ladder, gated on the BARE `HEAR_COMBAT`.
	if (NpcKernelState19_2Shared::State19_2HasCondition(*this, EElysiumNpcCond::HearCombat))
	{
		bool bTookTheClosestPlayerBranch = false;
		if (Closest != nullptr
			&& NpcKernelState19_2Shared::State19_2Resolve(*this, Senses.Memory.LastSoundCombat.Source) == Closest)
		{
			bTookTheClosestPlayerBranch = true;
			if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::HearCombat)
				&& NpcKernelState19_2Shared::State19_2Rand99() < 0x32)
			{
				State19_2PedestrianWitnessWeapon(*this, Closest);
				Slot596(Closest);
				NpcKernelState19_2Shared::State19_2Stamp(*this, 8, 0x321);
				return IdealStateRetail();
			}
			if (PlayerDistUnits < GState19_2PedestrianFleeUnits && NpcKernelState19_2Shared::State19_2Rand99() < 0x4b)
			{
				State19_2PedestrianWitnessWeapon(*this, Closest);
				Slot596(Closest);
				NpcKernelState19_2Shared::State19_2Stamp(*this, 8, 0x338);
				return IdealStateRetail();
			}
			// `103a338d`: a miss here SKIPS the heard-entity ladder outright.
		}
		if (!bTookTheClosestPlayerBranch)
		{
			// `103a3299`: the same two arms against `m_hLastHeardEnt`, and neither of them
			// reports a crime or touches slot 596 — the pedestrian just runs.
			FElysiumEntity* const Heard =
				NpcKernelState19_2Shared::State19_2Resolve(*this, Senses.Memory.LastHeardSource);
			if (Heard != nullptr
				&& NpcKernelState19_2Shared::State19_2Resolve(*this, Senses.Memory.LastSoundCombat.Source) == Heard)
			{
				if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::HearCombat)
					&& NpcKernelState19_2Shared::State19_2Rand99() < 0x32)
				{
					NpcKernelState19_2Shared::State19_2Stamp(*this, 8, 0x344);
					return IdealStateRetail();
				}
				const double SeparationUnitsSq =
					FVector::DistSquared(Origin, Heard->Origin)
						/ (ElysiumMove::U * ElysiumMove::U);
				if (SeparationUnitsSq < GState19_2PedestrianFleeUnitsSq
					&& NpcKernelState19_2Shared::State19_2Rand99() < 0x4b)
				{
					NpcKernelState19_2Shared::State19_2Stamp(*this, 8, 0x352);
					return IdealStateRetail();
				}
			}
		}
	}

	// `103a338d`: the bullet-impact tail, again on the BARE condition.
	if (!NpcKernelState19_2Shared::State19_2HasCondition(*this, EElysiumNpcCond::HearBulletImpact)
		|| NpcKernelState19_2Shared::State19_2Resolve(*this, Senses.Memory.LastSoundBulletImpact.Source) != Closest
		|| !(PlayerDistUnits < GState19_2PedestrianFleeUnits)
		|| NpcKernelState19_2Shared::State19_2Rand99() > 0x4a)
	{
		return NpcKernelState19_2Shared::State19_2ChainHuman(*this);
	}
	Slot596(Closest);
	NpcKernelState19_2Shared::State19_2Stamp(*this, 8, 0x366);
	return IdealStateRetail();
}

// Slot 453: `0x103a2980`, a direct call into the Troika body `0x102ad140` first, then its own bits.
// Slot 453: `0x103a2980`'s own bits, the body of its class's `BuildScheduleTestBits` override (story 5 step 3).

void FElysiumNpcPedestrian::BuildScheduleTestBits(FElysiumNpcConditions& InOutMask)
{
	FElysiumNpc::BuildScheduleTestBits(InOutMask);
	if (!IsBusyWithDiscipline())
	{
		InOutMask.Set(EElysiumNpcCond::PassOut);
	}
}

// Slot 404: `0x103a2930`.
/** `CNPC_VPedestrian::IRelationType` (`0x103a2930`), 58 bytes — a null candidate answers `D_ER`,
 *  a candidate whose NPC carries `D_INSANE` (`m_bfAINPCFlags2 & 0x20000`) answers `D_FR` WITHOUT
 *  consulting the relationship table at all, and everything else defers. Pedestrians fear the
 *  insane, and dialogue and the flee schedules read that through slot 404. */
int32 FElysiumNpcPedestrian::IRelationType(FElysiumEntity* Candidate)
{
	// `CNPC_VPedestrian::IRelationType` (`0x103a2930`), 58 bytes.
	if (Candidate == nullptr)                                            // 103a2936
	{
		return NpcKernelConditions10Shared::GCond10_D_ER;
	}
	// `103a2946`: the candidate's `+0x9c` carrying `D_INSANE` (`m_bfAINPCFlags2 & 0x20000`) answers
	// `D_FR` WITHOUT consulting the relationship table at all.
	const FElysiumNpc* const CandidateNpc = Candidate->AsNpc();
	if (CandidateNpc != nullptr && CandidateNpc->NpcFlags.Has(EElysiumNpcFlag2::D_INSANE))
	{
		return NpcKernelConditions10Shared::GCond10_D_FR;
	}
	return TroikaIRelationType(Candidate);                               // 103a2960
}

// --- Moved from `ElysiumNpcConditions10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcLifecycle2.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcLifecycle19_2.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcSchedule.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcSensesBodies.cpp` (story 5 step 4) ---

uint32 FElysiumNpcPedestrian::DecodeWitnessedLevel(uint32 Stored)
{
	// `CNPC_VPedestrian::vfunc461` (`0x103a2e30`) reading it back:
	//     h = (((stored & 0x068d8635) ^ 0x0ce9f66a) + 0x0ffa91d8) & 0x197279ca ^ stored ^ 0xa641cacd;
	//     level = unhash(h);
	// **The write XORs with `0x0ae8746f` and the read with `0x0ce9f66a`.** Both immediates are in
	// their listings; they are not the same constant mistyped.
	const uint32 Hashed = ((((Stored & NpcKernelSensesShared::GSecureStoreMask) ^ NpcKernelSensesShared::GSecureStoreXorRead) + NpcKernelSensesShared::GSecureStoreAdd)
		& NpcKernelSensesShared::GSecureStoreMask2) ^ Stored ^ NpcKernelSensesShared::GSecureStoreXorTail;
	return SecureUnhash(Hashed);
}

// --- Moved from `ElysiumNpcSpeciesMisc10_2.cpp` (story 5 step 4) ---

void FElysiumNpcPedestrian::PedestrianCreateCorpse()
{
	// `103a38c6` / `103a38e8`: the collision OBB snapshot, BEFORE the base, because the base resizes
	// the hull. `m_Collision` vtable `+4` is the mins and `+8` the maxs, three floats each; family
	// Motor's `CollisionMinsUnits`/`CollisionMaxsUnits` is that pair.
	FVector Mins = FVector::ZeroVector;
	FVector Maxs = FVector::ZeroVector;
	RetailCollisionExtents(*this, Mins, Maxs);
	PedestrianPreDeathMinsUnits = Mins;
	PedestrianPreDeathMaxsUnits = Maxs;
	// `103a3910`: `CBaseCombatCharacter::CreateCorpse`. SEAM: this substrate stands no corpse entity
	// at the kernel tier and slot 301's Troika-line body is another row's, so the call is the record
	// below and nothing else. Named rather than hidden.
	++PedestrianCreateCorpseCalls;
	// `103a391c`: `ThinkSet(NULL, 0.0, NULL)` — the think function is CLEARED, so a pedestrian
	// corpse never thinks again. Nothing in this substrate unbinds a think function; the record is
	// what says the body asked.
	bPedestrianCorpseThinkStopped = true;
	// `103a396b`: `SetSolid(SOLID_NONE)` on the collision, under a `CBaseEntity::SetSolid`
	// scope-trace frame.
	PedestrianCorpseSolid = 0;
}

// --- Moved from `ElysiumNpcState_2.cpp` (story 5 step 4) ---

