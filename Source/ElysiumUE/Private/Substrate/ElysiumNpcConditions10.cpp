#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions10Shared.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "ElysiumWorldServices.h"

// Story 29d, family **Conditions10** — the flag-word writers, slot 404 `IRelationType` and its
// species arms, `CanBeFedUponBy`, the seven `TaskFail` species arms, and the condition-debug string
// builder. The declarations and this family's standing facts are in
// `Substrate/ElysiumNpcConditions10.inl`; the walked prose is
// `docs/vtmb/npc-ai/conditions-and-states.md`.
//
// Every body below is retail's, arm by arm and in retail's order, with the `0x10……` address of the
// arm in the comment beside it. Where this family's reading corrected the checklist's one-line walk
// the arm is marked **CORRECTION**.

namespace
{
	// Unit-prefixed: the module builds adaptive-unity and anonymous namespaces are merged.

	// --- Retail's `Disposition_t` ----------------------------------------------------------------
	constexpr int32 GCond10_D_LI = 3;
	constexpr int32 GCond10_D_NU = 4;

	// `slot 532`'s reason bits, the jump table at `0x10290604` covering `param_1 - 1` in `[0,7]`.
	constexpr int32 GCond10Slot532Bit1 = 1;
	constexpr int32 GCond10Slot532Bit2 = 2;
	constexpr int32 GCond10Slot532Bit4 = 4;
	constexpr int32 GCond10Slot532Bit8 = 8;
	// `m_eAlternateAI == 4`, the one value every arm singles out, and the `[1,4]` window case 1 takes.
	constexpr int32 GCond10AlternateAiDoor = 4;
	// `vtable +0x700` is slot **448**, so cases 2 and 4 call `TaskFail(0xe)` before clearing the
	// door words. The checklist's walk names the call only by its vtable offset.
	constexpr int32 GCond10Slot532TaskFailReason = 0xe;

	// `CBaseCombatCharacter::HasMiscFlag(0x40000)` — name 18 of the 22 at `0x10619ec8`,
	// `No_Resist_Feeding` (`Substrate/ElysiumMiscFlags.h`).
	// `CBaseCombatCharacter::IsUnconscious` reads name 0, `Unconscious` (bit 0).

	// `m_bfAINPCFlags2 & 0x8000000` — `NOT_FEEDABLE`, the base feed body's second refusal.

	// Slot 35's two ids: `14 + (IsMale != 0)` (`102c024d ADD EAX,0xe`).
	constexpr int32 GCond10Slot35Female = 14;
	constexpr int32 GCond10Slot35Male = 15;

	// Slot 587's two thresholds: `m_iPLSupernaturalFleeLevel < 3` then
	// `m_iPLSupernaturalAttackLevel < 3` (`1028ef6e` / `1028ef7e`).
	constexpr int32 GCond10SupernaturalWitnessThreshold = 3;

	// Slot 419's two UNARMED literals, `0x3e99999a` and `0x3f000000`
	// (`102c552e` / `102c5538`).
	constexpr float GCond10BurstPauseMinDefault = 0.3f;
	constexpr float GCond10BurstPauseMaxDefault = 0.5f;

	// `_DAT_104454c0` = **1.0** and `_DAT_104454c4` = **0.0**, the two `.rdata` cells
	// `0x102c5570` folds: the scale seed and the threshold both its comparisons use.
	constexpr float GCond10WeaponScaleSeed = 1.0f;
	constexpr float GCond10WeaponScaleThreshold = 0.0f;

	// `CAI_BaseNPCTroika::IsInDialog` (`0x102c1170`), the four-term session gate. The same reading
	// families Sounds and SaveRestore10 made: this runtime carries the partner as the open dialogue
	// SESSION rather than as a handle on the NPC.
	bool Cond10IsInDialog(const FElysiumNpc& Npc)
	{
		const double Now = Npc.World != nullptr ? Npc.World->NowSeconds() : 0.0;
		return Npc.Dialogue.bInDialog || Npc.IsTalking(Now);
	}

}

// =================================================================================================
// Slot 404 `IRelationType` — `CAI_BaseNPCTroika::IRelationType` `0x10299da0` and four species arms.
// =================================================================================================

int32 FElysiumNpc::IRelationType(FElysiumEntity* Candidate)
{
	// slot 404, `vtable +0x650`. Cop `0x10372b70`, Hunter `0x10388bb0`, Pedestrian `0x103a2930`,
	// Yukie `0x103dd880` and Newscaster `0x103a01b0` override it on their C++ classes (story 5 step 3),
	// and the controller line's `0x103a48b0` on `FElysiumNpcPlayerController` (fold A2).
	return TroikaIRelationType(Candidate);
}

int32 FElysiumNpc::BaseCombatCharacterIRelationType(const FElysiumEntity* Candidate) const
{
	// `CBaseCombatCharacter::IRelationType` (`10299fb1`), the relationship-table tail. The store is
	// `FElysiumRelationships`, which never answers `D_ER`, so retail's error arm is unreachable
	// through this tail — the Troika body's own two null tests are what produce `D_ER`.
	if (Candidate == nullptr)
	{
		return NpcKernelConditions10Shared::GCond10_D_ER;
	}
	const FString Classname = Candidate->Def != nullptr ? Candidate->Def->Classname : FString();
	switch (Relationships.Resolve(Candidate->Handle, Classname))
	{
	case EElysiumRelationship::Hate:  return NpcKernelConditions10Shared::GCond10_D_HT;
	case EElysiumRelationship::Fear:  return NpcKernelConditions10Shared::GCond10_D_FR;
	case EElysiumRelationship::Like:  return GCond10_D_LI;
	default:                          return GCond10_D_NU;
	}
}

int32 FElysiumNpc::TroikaIRelationType(FElysiumEntity* Candidate)
{
	// `0x10299da0`, 541 bytes.

	// `10299daa`: the candidate IS me. `10299db7`: the candidate is null. Both answer `D_ER`, and
	// both are tested BEFORE anything else, which is why a species arm that answers `D_ER` on null
	// (all four of them) changes nothing for null and everything for the arms after it.
	if (Candidate == static_cast<FElysiumEntity*>(this))
	{
		return NpcKernelConditions10Shared::GCond10_D_ER;
	}
	if (Candidate == nullptr)
	{
		return NpcKernelConditions10Shared::GCond10_D_ER;
	}

	// `10299dc4`: `EBX = candidate->m_pNPC (+0x9c)`. The value is kept live all the way to the
	// LAST arm, which is why a candidate that is not an NPC skips two separate blocks.
	FElysiumNpc* const CandidateNpc = Candidate->AsNpc();

	// --- Arm A, `10299dd2`: the INSANE forwarding ------------------------------------------------
	// The candidate's NPC carries `D_INSANE` (`m_bfAINPCFlags2 & 0x20000`, tested as
	// `AND EAX,0x20000 / CMP EAX,0x20000` — an all-bits test, which for a single bit is the same
	// question), AND `m_hClosestPlayer` (`+0x628c`) resolves to a live entity.
	if (CandidateNpc != nullptr && CandidateNpc->NpcFlags.Has(EElysiumNpcFlag2::D_INSANE))
	{
		FElysiumEntity* const ClosestPlayer =
			World != nullptr ? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;
		if (ClosestPlayer != nullptr)
		{
			// `10299e51`: `this->vtable[0x650](player)` — slot 404 again, VIRTUALLY, so a cop asks
			// its own species body about its own closest player. Terminates because a player has no
			// `+0x9c` and therefore never re-enters this arm.
			if (IRelationType(ClosestPlayer) == NpcKernelConditions10Shared::GCond10_D_HT)
			{
				return NpcKernelConditions10Shared::GCond10_D_HT;
			}
			// `10299e8e`: `this->vtable[0x2a0]()` — slot **168**, the Troika line's mutable
			// `GetEnemy` WITH the last-enemy fallback, not slot 167.
			if (GetEnemy() == ClosestPlayer)
			{
				return NpcKernelConditions10Shared::GCond10_D_HT;
			}
		}
	}

	// --- Arm B, `10299eaa`: the CANDIDATE's follower boss -----------------------------------------
	// Retail reads `candidate->+0x98` (`m_pCombatCharacter`) and then `+0x647c` off it. `+0x647c` is
	// a `CAI_BaseNPCTroika` member, so the read is only meaningful when the combat character is an
	// NPC; for the player it lands on an unrelated word of `CBasePlayer`. **SEAM**: that word has no
	// counterpart here and no recoverable value, so a non-NPC combat character answers "no boss",
	// which is what a zeroed handle gives.
	if (Candidate->AsCombatCharacter() != nullptr)
	{
		FElysiumEntity* const TheirBoss = (CandidateNpc != nullptr && World != nullptr)
			? World->Resolve(CandidateNpc->FollowerBoss) : nullptr;
		if (TheirBoss != nullptr)
		{
			// `10299ee4` then `10299ef3`: the same pair as arm A — slot 404 virtually, then slot 168.
			if (IRelationType(TheirBoss) == NpcKernelConditions10Shared::GCond10_D_HT)
			{
				return NpcKernelConditions10Shared::GCond10_D_HT;
			}
			if (GetEnemy() == TheirBoss)
			{
				return NpcKernelConditions10Shared::GCond10_D_HT;
			}
		}
	}

	// --- Arm C, `10299f0f`: MY follower boss ------------------------------------------------------
	// `m_hFollowerBoss` (`+0x647c`) read RAW, not through slot 293 — the same resolve either way.
	FElysiumEntity* const MyBossEntity =
		World != nullptr ? World->Resolve(FollowerBoss) : nullptr;
	// `10299f3b`: the boss's own `+0x9c`. A boss that is not an NPC is the same as no boss at all.
	FElysiumNpc* const MyBoss = MyBossEntity != nullptr ? MyBossEntity->AsNpc() : nullptr;
	if (MyBoss == nullptr)
	{
		// `10299fae`: chain `CBaseCombatCharacter::IRelationType`.
		return BaseCombatCharacterIRelationType(Candidate);
	}
	// `10299f45`: my boss IS the candidate — `D_LI`, unconditionally, table or no table.
	if (Candidate == static_cast<FElysiumEntity*>(MyBoss))
	{
		return GCond10_D_LI;
	}
	// `10299f5a`: the BOSS's slot 404 toward the candidate, virtually.
	int32 Answer = MyBoss->IRelationType(Candidate);
	if (Answer == NpcKernelConditions10Shared::GCond10_D_HT)
	{
		return NpcKernelConditions10Shared::GCond10_D_HT;
	}
	// `10299f6b`: the boss's `vtable +0x29c` — slot **167**, the CONST `GetEnemy` with no
	// last-enemy fallback. The asymmetry against arms A and B (which use `+0x2a0`) is retail's.
	if (static_cast<const FElysiumNpc*>(MyBoss)->GetEnemy() == Candidate)
	{
		return NpcKernelConditions10Shared::GCond10_D_HT;
	}
	// `10299f77`: a candidate that is not an NPC stops here with the BOSS's answer.
	if (CandidateNpc == nullptr)
	{
		return Answer;
	}
	// **CORRECTION.** `10299f84` is `MOV EDI,EAX` — the return register is REASSIGNED with the
	// CANDIDATE's opinion of the boss. So the fall-through answer below is the candidate NPC's
	// relation toward my boss, not the boss's toward the candidate; the checklist's walk says "the
	// answer is the BOSS's slot 0x650 toward the target", which holds only for a non-NPC candidate.
	Answer = CandidateNpc->IRelationType(MyBossEntity);
	if (Answer == NpcKernelConditions10Shared::GCond10_D_HT)
	{
		return NpcKernelConditions10Shared::GCond10_D_HT;
	}
	// `10299f8f`: the candidate's `vtable +0x2a0` — slot 168 again, the mutable overload.
	if (CandidateNpc->GetEnemy() == MyBossEntity)
	{
		return NpcKernelConditions10Shared::GCond10_D_HT;
	}
	// `10299fa5`: `MOV EAX,EDI`.
	return Answer;
}

// =================================================================================================
// Slot 342 `CanBeFedUponBy` — `CAI_BaseNPCTroika::CanBeFedUponBy` `0x102c4a60`.
// =================================================================================================

bool FElysiumNpc::CanBeFedUponBy(FElysiumEntity* Feeder)
{
	// slot 342, `CAI_BaseNPCTroika::CanBeFedUponBy` (`0x102c4a60`), 77 bytes, three arms.

	// `102c4a66`: `m_bInvincible` (`+0x63d8`) non-zero refuses immediately. Retail clears only the
	// LOW BYTE of `EAX` here (`AND EAX,0xffffff00`), so the upper bits are stale; the bool is false
	// and that is the whole observable answer.
	if (bInvincible)
	{
		return false;
	}

	// `102c4a74`: `GetFollowerBoss()` (slot 293, `vtable +0x494`). When the boss stands AND the
	// boss IS the feeder, the feed is refused unless `HasMiscFlag(0x40000)` — name 18 of the misc
	// table, `No_Resist_Feeding`. So your own follower or ghoul may only feed on you while that flag
	// stands, and the refusal again carries only the cleared low byte.
	{
		const FElysiumEntity* const Boss = GetFollowerBoss();
		if (Boss != nullptr && Boss == Feeder)
		{
			if (!ElysiumMiscFlags::Has(MiscFlags, ElysiumMiscFlags::NoResistFeeding))
			{
				return false;
			}
		}
	}

	// `102c4a9e`: everything else defers entirely to the base body WITH the feeder.
	return FElysiumCombatCharacter::CanBeFedUponBy(Feeder);
}

// =================================================================================================
// Slot 587 `CanWitnessSupernatural` — `0x1028ef20`.
// =================================================================================================

bool FElysiumNpc::CanWitnessSupernatural(int32 Level)
{
	// `0x1028ef20`, 118 bytes. **The argument is never read** — retail's `param_1` is dead in every
	// arm, and the two thresholds it compares are the NPC's own authored keyfields.
	(void)Level;

	// Five refusals in this exact order, each answering false on its own.
	if (IsKindred())                                                     // 1028ef27
	{
		return false;
	}
	if (!DialogName.IsEmpty())                                         // 1028ef35, m_iDialog +0x0128
	{
		return false;
	}
	if (IsOblivious())                                          // 1028ef44, m_iIsOblivious > 0
	{
		return false;
	}
	// `1028ef53`: `m_bfNPCFrenziedFlags & 0x10` — the "does not witness" bit `DoFrenzy`'s
	// `0x9fbd` word carries.
	if (HasFrenzied(FElysiumNpcBase::FrenziedDoesNotWitness))
	{
		return false;
	}
	if (IsBusyWithDiscipline())                                          // 1028ef60
	{
		return false;
	}

	// `1028ef6e`: past all five, a flee level below 3 answers TRUE outright; otherwise the answer is
	// whether the ATTACK level is below 3. So a body authored with both at 3 or more cannot witness.
	// Both are the RAW authored keyfields, not `ElysiumNpcWitness::ResolveThreshold`'s resolved form
	// — retail reads `+0x6354` and `+0x6358` directly.
	if (PlSupernaturalFlee < GCond10SupernaturalWitnessThreshold)
	{
		return true;
	}
	return PlSupernaturalAttack < GCond10SupernaturalWitnessThreshold;
}

// =================================================================================================
// Slot 532 — the door-failure cleanup, `0x10290570`.
// =================================================================================================

void FElysiumNpc::NavigatorDoorCleanup()
{
	// `0x102bf7e0`: `if (nav->IsPaused()) nav->Unpause(); m_bShouldMove = 1;` — `0x102ee2e0` reads
	// the path's PAUSED byte (`path+0x10`, 0018 story 5 R1) and `0x102ee2c0` clears it (`0x1030bea0`);
	// nothing is stopped (the port's `NavStopMoving` is that unpause, misnamed). The test is made
	// TWICE — once by the caller at `102905da` and once here — and both are kept.
	if (NavigatorIsPaused())
	{
		NavStopMoving();
	}
	BaseScheduleHost.bShouldMove = true;                                     // +0x1a40
}

void FElysiumNpc::Slot532(int32 FailureBits)
{
	// slot 532, `CAI_BaseNPCTroika::vfunc532` (`0x10290570`), 145 bytes. The jump table at
	// `0x10290604` covers `param_1 - 1` in `[0,7]`; anything else falls straight to the tail.
	//
	// **CORRECTION.** The checklist's walk says case 8 clears `m_eAlternateAI` "only when it is 4,
	// otherwise no clear for values 1-3". The listing says the opposite: `102905ca CMP EAX,0x3 /
	// JLE 0x102905ea` sends 1..3 straight to the CLEAR, and only 4 also runs the navigator arm.
	// Values below 1 and above 4 take no clear at all.
	bool bClearAlternateAi = false;
	bool bRunNavigatorArm = false;

	switch (FailureBits)
	{
	case GCond10Slot532Bit1:
		// `10290587`: `m_eAlternateAI` in `[1,4]`, then the navigator arm, then the clear.
		if (AlternateAi >= 1 && AlternateAi <= GCond10AlternateAiDoor)
		{
			bRunNavigatorArm = true;
			bClearAlternateAi = true;
		}
		break;
	case GCond10Slot532Bit2:
	case GCond10Slot532Bit4:
		// `10290598`: only `m_eAlternateAI == 4`.
		if (AlternateAi == GCond10AlternateAiDoor)
		{
			// `102905a7`: `vtable +0x700` is slot **448** — `TaskFail(0xe)`, which runs the WHOLE
			// failure chain from inside the door cleanup and BEFORE the two door words are written.
			TaskFail(GCond10Slot532TaskFailReason);
			OpeningDoor = FElysiumEntityHandle::Invalid();                // 102905ad
			bOpeningDoorWait = false;                                     // 102905b7
			bClearAlternateAi = true;
		}
		break;
	case GCond10Slot532Bit8:
		// `102905c0`: 0 and below take nothing; 1..3 take the clear alone; exactly 4 also takes the
		// navigator arm; 5 and above take nothing.
		if (AlternateAi >= 1)
		{
			if (AlternateAi <= 3)
			{
				bClearAlternateAi = true;
			}
			else if (AlternateAi == GCond10AlternateAiDoor)
			{
				bRunNavigatorArm = true;
				bClearAlternateAi = true;
			}
		}
		break;
	default:
		break;
	}

	if (bRunNavigatorArm)
	{
		// `102905d4`: `m_pNavigator->IsGoalSet()` gates the door cleanup `0x102bf7e0`.
		if (NavIsGoalSet())
		{
			NavigatorDoorCleanup();
		}
	}
	if (bClearAlternateAi)
	{
		AlternateAi = 0;                                                 // 102905ea
	}

	// `102905f4`: every path chains the base body with the caller's bits unchanged.
	FElysiumNpcBase::Slot532(FailureBits);
}

// =================================================================================================
// Slot 35 — `0x102c0220`, the gendered small id.
// =================================================================================================

int32 FElysiumNpc::Slot35(FElysiumEntity* Argument)
{
	// slot 35, 57 bytes, three arms. It sits beside slot 34 `GetHighlightMaterial` and answers a
	// small id, not a boolean.
	if (Cond10IsInDialog(*this))                                         // 102c0223, 0x102c1170
	{
		return 0;
	}
	// `102c0235`: slot 295 `CanTalk`, dispatched WITH the caller's argument (`PUSH ECX` where
	// `ECX = [ESP+8]`) — the decompiled C drops the push.
	if (!CanTalk(Argument))
	{
		return 0;
	}
	// `102c024d`: `EAX = (IsMale != 0); ADD EAX,0xe` — 15 male, 14 female.
	return Sheet.IsMale() ? GCond10Slot35Male : GCond10Slot35Female;
}

// =================================================================================================
// Slot 419 `UpdateBurstShootPause` — `0x102c5500`.
// =================================================================================================

float FElysiumNpc::ScaleWeaponBurstPause(float Value, float Base, float Range, float DistanceUnits,
	bool bHasTarget)
{
	// `0x102c5570`, read from the LISTING because the decompiler turned the x87 comparison chain
	// into a `ushort` of status-word bits and invented a return-storage parameter.
	//
	//   102c5573  scale = 1.0                      (_DAT_104454c0)
	//   102c5582  if (Range <= 0.0) goto tail      (_DAT_104454c4, the FCOMP/FNSTSW 0x4100 test)
	//   102c559f  distance = |target - me|         (m_hShootTargetOverride, else GetEnemy's body target)
	//   102c568d  if (distance <= 0.0) scale = distance; else scale = sqrt(distance / Range)
	//   102c56a1  with NO enemy at all,            scale = sqrt(1.0 / Range)
	//   102c56ad  return scale * (Value - Base)
	float Scale = GCond10WeaponScaleSeed;
	if (Range > GCond10WeaponScaleThreshold)
	{
		if (!bHasTarget)
		{
			// `102c56a1`: the seeded 1.0 is reloaded and divided.
			Scale = FMath::Sqrt(GCond10WeaponScaleSeed / Range);
		}
		else if (DistanceUnits > GCond10WeaponScaleThreshold)
		{
			Scale = FMath::Sqrt(DistanceUnits / Range);
		}
		else
		{
			// `102c569d`: a distance at or below the threshold is carried through unscaled.
			Scale = DistanceUnits;
		}
	}
	return Scale * (Value - Base);
}

bool FElysiumNpc::ActiveWeaponBurstPauseWords(float& OutMin, float& OutMax) const
{
	// **SEAM** for `0x102c5780` / `0x102c57c0` — `wpndata + 0x264` and `wpndata + 0x268` resolved
	// through `0x102517e0` and scaled by `ScaleWeaponBurstPause` above. Story 29c-1's
	// `ActiveWeaponEntity()` answers null and no weapon-data record is stood, so this answers false
	// and slot 419 takes retail's own UNARMED arm.
	OutMin = 0.f;
	OutMax = 0.f;
	return ActiveWeaponEntity() != nullptr;
}

void FElysiumNpc::UpdateBurstShootPause()
{
	// slot 419, `0x102c5500`, 69 bytes.
	//
	// **CORRECTION.** The checklist's walk says "no port member currently carries the burst pause
	// pair"; `m_flBurstShootPauseMin` (`+0x5bbc`) and `m_flBurstShootPauseMax` (`+0x5bc0`) are both
	// bound on `FElysiumNpc` and in the shape map, and this body is what writes them.
	float Min = 0.f;
	float Max = 0.f;
	if (ActiveWeaponBurstPauseWords(Min, Max))                           // 102c5504 GetActiveWeapon
	{
		BurstShootPauseMin = Min;                                        // 102c5517
		BurstShootPauseMax = Max;                                        // 102c5525
		return;
	}
	// `102c552e` / `102c5538`: the two retail literals, written as raw dwords.
	BurstShootPauseMin = GCond10BurstPauseMinDefault;                    // 0x3e99999a
	BurstShootPauseMax = GCond10BurstPauseMaxDefault;                    // 0x3f000000
}

// =================================================================================================
// `0x102c54c0` — the fake-reload reroll.
// =================================================================================================

bool FElysiumNpc::CharTemplateFakeReloadRange(int32& OutMin, int32& OutMax) const
{
	// **SEAM** for the char template's `+0x34` / `+0x38` pair, resolved by `GetCharTemplate`
	// (`0x10207c40`) through the template manager (`0x101d5e80` over `DAT_10738d10`).
	// `FElysiumClanTemplate` exposes no such columns, so this answers false with both ends zero.
	OutMin = 0;
	OutMax = 0;
	return false;
}

void FElysiumNpc::ResetFakeReloadCount()
{
	// `0x102c54c0`, 43 bytes: `m_iFakeReloadCount = RandomInt(template[0x34], template[0x38])`.
	// `(*DAT_1070b244)->+8` is `IUniformRandomStream::RandomInt`.
	//
	// The roll happens even with no template columns, because retail's body has no arm that skips
	// the write and a body that silently declined its only write would be a refusal this row does
	// not have. `RandomInt(0, 0)` is 0.
	int32 Min = 0;
	int32 Max = 0;
	CharTemplateFakeReloadRange(Min, Max);
	FakeReloadCount = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(Min, Max);
}

// =================================================================================================
// Slot 448 `TaskFail` — the seven species arms in front of story 13's `0x1029adb0`.
// =================================================================================================

void FElysiumNpc::BlacklistPickupTarget(const FElysiumEntityHandle& BlacklistTarget)
{
	// `0x10382970` (Hengeyokai `m_BlacklistedEntities` `+0x66a4`) and `0x103bf200` (Tzimisce
	// `m_FailedPickupTargets` `+0x6690`) — byte-identical `CUtlVector<BlacklistedEntity_t>` grow-
	// and-append of `(handle, curtime + 20.0)`, the 20.0 being `_DAT_1044eb0c`.
	//
	// **CORRECTION.** The checklist's walk calls both "release the pickup target". Neither releases
	// anything: the target is SHUNNED for twenty seconds, and the release is the separate
	// `FINDING_BODY` clear on the next line of the caller.
	SpeciesBlacklistedEntities.Add(
		FSpeciesBlacklistEntry{ BlacklistTarget, NpcKernelConditions10Shared::Cond10Now(*this) + SpeciesBlacklistSeconds });
}

void FElysiumNpc::SetIgnoreCollisionExpiry(float DelaySeconds)
{
	// `0x102c43b0`: `if (GetIgnoreCollisionEntity()) { m_flIgnoreCollisionTimer (+0x6458) = curtime
	// + delay; 0x102c43f0(this); }`, and `0x102c43f0` is `if (timer <= curtime) { <clear the ignored
	// entity>; timer = FLT_MAX; }` — so the re-arm can expire in the same call when the delay is
	// zero or negative.
	//
	// **SEAM** for `GetIgnoreCollisionEntity()` (`CBaseAnimating`): this runtime carries the TIMER
	// (`IgnoreCollisionUntil`) and no ignored ENTITY, so the gate is "the timer is armed", which is
	// the same question for every body that ever armed it.
	const double Now = NpcKernelConditions10Shared::Cond10Now(*this);
	if (IgnoreCollisionUntil <= 0.0)
	{
		return;
	}
	IgnoreCollisionUntil = Now + DelaySeconds;
	if (IgnoreCollisionUntil <= Now)
	{
		IgnoreCollisionUntil = static_cast<double>(TNumericLimits<float>::Max());
	}
}
