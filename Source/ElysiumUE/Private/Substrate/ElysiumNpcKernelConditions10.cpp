#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "ElysiumWorldServices.h"

// Story 29d, family **Conditions10** — the flag-word writers, slot 404 `IRelationType` and its
// species arms, `CanBeFedUponBy`, the seven `TaskFail` species arms, and the condition-debug string
// builder. The declarations and this family's standing facts are in
// `Substrate/ElysiumNpcKernelConditions10.inl`; the walked prose is
// `docs/vtmb/npc-ai/conditions-and-states.md`.
//
// Every body below is retail's, arm by arm and in retail's order, with the `0x10……` address of the
// arm in the comment beside it. Where this family's reading corrected the checklist's one-line walk
// the arm is marked **CORRECTION**.

namespace
{
	// Unit-prefixed: the module builds adaptive-unity and anonymous namespaces are merged.

	// --- Retail's `Disposition_t` ----------------------------------------------------------------
	constexpr int32 GCond10_D_ER = 0;
	constexpr int32 GCond10_D_HT = 1;
	constexpr int32 GCond10_D_FR = 2;
	constexpr int32 GCond10_D_LI = 3;
	constexpr int32 GCond10_D_NU = 4;

	// --- The census addresses the slot methods dispatch on ---------------------------------------
	constexpr TCHAR GCond10Body_CopRelation[] = TEXT("0x10372b70");
	constexpr TCHAR GCond10Body_HunterRelation[] = TEXT("0x10388bb0");
	constexpr TCHAR GCond10Body_PedestrianRelation[] = TEXT("0x103a2930");
	constexpr TCHAR GCond10Body_YukieRelation[] = TEXT("0x103dd880");
	// The two slot-404 species bodies that are NOT this family's rows but which this family's one
	// slot method has to dispatch, because the census fills slot 404 with them for five classes.
	constexpr TCHAR GCond10Body_NewscasterRelation[] = TEXT("0x103a01b0");
	constexpr TCHAR GCond10Body_FrenzyShadowRelation[] = TEXT("0x103a48b0");

	constexpr TCHAR GCond10Body_AsianVampireTaskFail[] = TEXT("0x10362390");
	constexpr TCHAR GCond10Body_ChangBrosTaskFail[] = TEXT("0x1036d1d0");
	constexpr TCHAR GCond10Body_GargoyleTaskFail[] = TEXT("0x10379060");
	constexpr TCHAR GCond10Body_HengeyokaiTaskFail[] = TEXT("0x10380510");
	constexpr TCHAR GCond10Body_MingXiaoTaskFail[] = TEXT("0x10394090");
	constexpr TCHAR GCond10Body_SheriffManTaskFail[] = TEXT("0x103b0290");
	constexpr TCHAR GCond10Body_TzimisceTaskFail[] = TEXT("0x103ba350");

	// --- `0x1028d990`'s `.rdata`, read out of the pinned image -----------------------------------
	//
	// The decompiler folded the argument lists into the 0x8cc-byte stack frame; every string below
	// was read from the image's `.rdata` at the address named beside it.
	constexpr TCHAR GCond10FmtFull[] = TEXT("%6.2f : %s %s\n%s%s %s%s %s\n\n");        // 0x105d8868
	constexpr TCHAR GCond10FmtShort[] = TEXT("%6.2f : %s %s\n");                       // 0x105d8854
	constexpr TCHAR GCond10FmtNamed[] = TEXT("%-20s  %6.2f : %s %s\n%s%s %s%s %s\n\n");// 0x105d8828
	constexpr TCHAR GCond10CondsHeader[] = TEXT("CONDS:");                             // 0x105d8908
	constexpr TCHAR GCond10FmtCond[] = TEXT(" %s");                                    // 0x105a3060
	constexpr TCHAR GCond10Newline[] = TEXT("\n");                                     // 0x10547e40
	constexpr TCHAR GCond10MemoryLegend[] = TEXT("PIS__PF_T_L__TTEPLM________ICCCC");  // 0x105d88e0
	constexpr TCHAR GCond10FlagsLegend[] = TEXT("RSCPFCNFIPCDHVAEFSBDSLIAMFDPOIO_");   // 0x105d88b8
	constexpr TCHAR GCond10FmtNav[] = TEXT("NAV %s %s");                               // 0x105d888c
	constexpr TCHAR GCond10NavClimb[] = TEXT("CLIMB");                                 // 0x105d88a0
	constexpr TCHAR GCond10NavNotClimb[] = TEXT("     ");                              // 0x105d8898, 5 spaces
	constexpr TCHAR GCond10NavJump[] = TEXT("JUMP");                                   // 0x105d88b0
	constexpr TCHAR GCond10NavNotJump[] = TEXT("    ");                                // 0x105d88a8, 4 spaces

	// **The three retail format strings carry `%*s` where this file spells `%s`.** Retail passes the
	// indent level as the WIDTH and the empty string (`DAT_106b8540`) as the value, so the field is
	// `IndentLevel` spaces; `FString::Printf` is not the shipped CRT's formatter and `%*s` is not
	// part of its contract, so the indent is built as a string and handed in. Same characters, same
	// order, no arm changed. NAMED, spelling-only.

	// The two ladder lengths: 32 over `m_afMemory` (`1028dae1 CMP ECX,0x20`) and 30 over
	// `m_bfAINPCFlags` (`1028db3f CMP ECX,0x1e`).
	constexpr int32 GCond10MemoryLadderBits = 0x20;
	constexpr int32 GCond10FlagsLadderBits = 0x1e;

	// The two debug BYTES the block gates on — family Debug10's `DebugTraceByte`, keyed on the
	// global's spelling — and the schedule-debug ConVar `ent_trace_conditions`, which ships "1".
	constexpr TCHAR GCond10CvTraceRing[] = TEXT("DAT_10920534");
	constexpr TCHAR GCond10CvTraceVerbose[] = TEXT("DAT_10920535");
	constexpr ElysiumNpcTunables::EConVar GCond10CvScheduleDebug =
		ElysiumNpcTunables::EConVar::EntTraceConditions;          // DAT_10924a6c

	// `GetLastSharedCondition()` (slot 409, `0x1027ee00`) — `return 0x77;`.
	constexpr int32 GCond10LastSharedCondition = 0x77;

	// `m_afMemory`'s top bit, the one the three `TaskFail` species arms clear
	// (`10379077`/`10380536`/`103ba376`: `AND dword ptr [ESI + 0x5d8c],0x7fffffff`).
	constexpr uint32 GCond10MemoryTopBit = 0x80000000u;

	// `CNPC_VChangBros::TaskFail`'s write, `0x15d` (`1036d20a MOV dword ptr [ESI+0x5c54],0x15d`).
	constexpr int32 GCond10ChangBrosFailSchedule = 0x15d;

	// The failure-code window both the AsianVampire and the ChangBros arms gate on:
	// `if (0xb < code && code < 0x10)`, i.e. 12..15 (`103623ca` / `1036d20a`).
	constexpr int32 GCond10PathFailFirst = 0xc;
	constexpr int32 GCond10PathFailLast = 0xf;

	// `m_eThrowableObjectMode` values 3 and 4, the MingXiao arm's motor case (`103940a5`), and the
	// 180.0 the motor's steering is reset to (`0x43340000`).
	constexpr int32 GCond10ThrowModeMotorA = 3;
	constexpr int32 GCond10ThrowModeMotorB = 4;
	constexpr float GCond10MingXiaoSteeringYaw = 180.f;

	// `m_NPCState == 2` — retail's COMBAT ordinal (`10379063 CMP dword ptr [ESI+0x5cc0],0x2`).
	constexpr int32 GCond10NpcStateCombat = 2;

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

	// The 0.75 s ignore-collision re-arm the Hengeyokai and Tzimisce arms pass to `0x102c43b0`
	// (`103805a6` / `103ba3e6`, `PUSH 0x3f400000`).
	constexpr float GCond10PickupReuseDelay = 0.75f;

	double Cond10Now(const FElysiumNpc& Npc)
	{
		// `gpGlobals->curtime`, `*(float*)(DAT_1070b228 + 0xc)`.
		return Npc.World != nullptr ? Npc.World->NowSeconds() : 0.0;
	}

	// `CBaseEntity::GetDebugName()` (`0x1000b5cd`): `m_iName` when set, the classname otherwise,
	// the empty string for a null pointer on either.
	FString Cond10DebugName(const FElysiumEntity* Entity)
	{
		if (Entity == nullptr)
		{
			return FString();
		}
		if (!Entity->TargetName.IsEmpty())
		{
			return Entity->TargetName;
		}
		return Entity->Def != nullptr ? Entity->Def->Classname : FString();
	}

	// `CAI_BaseNPCTroika::IsInDialog` (`0x102c1170`), the four-term session gate. The same reading
	// families Sounds and SaveRestore10 made: this runtime carries the partner as the open dialogue
	// SESSION rather than as a handle on the NPC.
	bool Cond10IsInDialog(const FElysiumNpc& Npc)
	{
		const double Now = Npc.World != nullptr ? Npc.World->NowSeconds() : 0.0;
		return Npc.Dialogue.bInDialog || Npc.IsTalking(Now);
	}

	// `m_NPCState` in RETAIL's ordinals — the same mapping family Sounds recovered from
	// `0x1026e3e0`'s table.
	int32 Cond10RetailNpcState(EElysiumNpcState State)
	{
		switch (State)
		{
		case EElysiumNpcState::Idle:     return 1;
		case EElysiumNpcState::Combat:   return 2;
		case EElysiumNpcState::Alert:    return 3;
		case EElysiumNpcState::Scripted: return 4;
		case EElysiumNpcState::Prone:    return 6;
		case EElysiumNpcState::Dead:     return 7;
		default:                         return 0;
		}
	}
}

// =================================================================================================
// Slot 404 `IRelationType` — `CAI_BaseNPCTroika::IRelationType` `0x10299da0` and four species arms.
// =================================================================================================

int32 FElysiumNpc::IRelationType(FElysiumEntity* Candidate)
{
	// slot 404, `vtable +0x650`. Five retail bodies fill it across the census and this leaf resolves
	// between them by the address the census says fills the slot for this NPC's retail class, the
	// same way story 29c-1's slot-76 dispatcher does.
	//
	// `CNPC_VCop` has a NULL classname list in the census, so a spawned `npc_VCop` answers a null
	// `RetailClass()` and lands on the Troika-line body — the recovered answer, not a gap (story
	// 29c-1's cleanup). `CopIRelationType` is therefore UNREACHABLE through this dispatcher today;
	// it is ported and driven directly by its test.
	const TCHAR* const SlotBody = ElysiumNpcKernelClass::BodyOf(RetailClass(), 404);
	if (SlotBody != nullptr)
	{
		if (FCString::Strcmp(SlotBody, GCond10Body_CopRelation) == 0)
		{
			return CopIRelationType(Candidate);
		}
		if (FCString::Strcmp(SlotBody, GCond10Body_HunterRelation) == 0)
		{
			return HunterIRelationType(Candidate);
		}
		if (FCString::Strcmp(SlotBody, GCond10Body_PedestrianRelation) == 0)
		{
			return PedestrianIRelationType(Candidate);
		}
		if (FCString::Strcmp(SlotBody, GCond10Body_YukieRelation) == 0)
		{
			return YukieIRelationType(Candidate);
		}
		if (FCString::Strcmp(SlotBody, GCond10Body_NewscasterRelation) == 0)
		{
			// `CNPC_VNewscaster::IRelationType` (`0x103a01b0`) — eight bytes, `return 4;`. It never
			// looks at the candidate, never reaches the table and never reaches the Troika body, so
			// a newscaster is `D_NU` toward absolutely everything including itself and null. Not one
			// of this family's rows; it is dispatched here because the census fills slot 404 with it
			// and `npc_VNewscaster` IS a registered spawn leaf.
			return GCond10_D_NU;
		}
		if (FCString::Strcmp(SlotBody, GCond10Body_FrenzyShadowRelation) == 0)
		{
			// `0x103a48b0`, story 29c-1's `SpeciesIRelationType` (family Squad) — slot 404's body
			// for `CNPC_VFrenzyShadow`, `CNPC_VPlayerController` and `CNPC_VWolfMorph`. Dispatched
			// to rather than re-ported; it too never chains the Troika body.
			return SpeciesIRelationType(Candidate);
		}
	}
	return TroikaIRelationType(Candidate);
}

int32 FElysiumNpc::BaseCombatCharacterIRelationType(const FElysiumEntity* Candidate) const
{
	// `CBaseCombatCharacter::IRelationType` (`10299fb1`), the relationship-table tail. The store is
	// `FElysiumRelationships`, which never answers `D_ER`, so retail's error arm is unreachable
	// through this tail — the Troika body's own two null tests are what produce `D_ER`.
	if (Candidate == nullptr)
	{
		return GCond10_D_ER;
	}
	const FString Classname = Candidate->Def != nullptr ? Candidate->Def->Classname : FString();
	switch (Relationships.Resolve(Candidate->Handle, Classname))
	{
	case EElysiumRelationship::Hate:  return GCond10_D_HT;
	case EElysiumRelationship::Fear:  return GCond10_D_FR;
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
		return GCond10_D_ER;
	}
	if (Candidate == nullptr)
	{
		return GCond10_D_ER;
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
			if (IRelationType(ClosestPlayer) == GCond10_D_HT)
			{
				return GCond10_D_HT;
			}
			// `10299e8e`: `this->vtable[0x2a0]()` — slot **168**, the Troika line's mutable
			// `GetEnemy` WITH the last-enemy fallback, not slot 167.
			if (GetEnemy() == ClosestPlayer)
			{
				return GCond10_D_HT;
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
			if (IRelationType(TheirBoss) == GCond10_D_HT)
			{
				return GCond10_D_HT;
			}
			if (GetEnemy() == TheirBoss)
			{
				return GCond10_D_HT;
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
	if (Answer == GCond10_D_HT)
	{
		return GCond10_D_HT;
	}
	// `10299f6b`: the boss's `vtable +0x29c` — slot **167**, the CONST `GetEnemy` with no
	// last-enemy fallback. The asymmetry against arms A and B (which use `+0x2a0`) is retail's.
	if (static_cast<const FElysiumNpc*>(MyBoss)->GetEnemy() == Candidate)
	{
		return GCond10_D_HT;
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
	if (Answer == GCond10_D_HT)
	{
		return GCond10_D_HT;
	}
	// `10299f8f`: the candidate's `vtable +0x2a0` — slot 168 again, the mutable overload.
	if (CandidateNpc->GetEnemy() == MyBossEntity)
	{
		return GCond10_D_HT;
	}
	// `10299fa5`: `MOV EAX,EDI`.
	return Answer;
}

int32 FElysiumNpc::CopIRelationType(FElysiumEntity* Candidate)
{
	// `CNPC_VCop::IRelationType` (`0x10372b70`), 170 bytes — three arms in front of the Troika body.

	// `10372b76`: a null candidate answers `D_NO`/`D_ER` 0 here rather than reaching the base.
	if (Candidate == nullptr)
	{
		return GCond10_D_ER;
	}

	// `10372b84`: the cop class's SHARED provoker handle `DAT_1093ac3c` and its expiry
	// `_DAT_1093aca8`, written by `CNPC_VCop::OnSeeEntity`'s stamp (`0x10370560`, family Senses10)
	// and read here and by `CNPC_VCop::DrawDebugGeometryOverlays`. One grudge for every cop in the
	// map, which is why it is a file static in family Senses10 and reached through its accessors.
	if (World != nullptr)
	{
		const FElysiumEntity* const Suspect = World->Resolve(CopSuspectHandle());
		if (Suspect == Candidate && Cond10Now(*this) < CopSuspectExpiry())
		{
			return GCond10_D_HT;
		}
	}

	// `10372bc6`: the candidate's player record (`+0xa8`). Both words are real on `FElysiumPlayer`;
	// a non-player candidate answers false / 0, which is retail's null-`+0xa8` arm.
	// `0x1017f8d0` is `curtime < player->m_flHeightenedAlertExpireTimer` (`+0x1d1c`).
	if (PlayerHeightenedAlert(Candidate))
	{
		return GCond10_D_HT;
	}
	// `0x1017f770` is `player->m_iCopsInPursuitCount` (`+0x1d10`), and the test is `> 0`.
	if (PlayerCopsInPursuitCount(Candidate) > 0)
	{
		return GCond10_D_HT;
	}

	// `10372c0a`: the DIRECT thunk to `CAI_BaseNPCTroika::IRelationType`.
	return TroikaIRelationType(Candidate);
}

int32 FElysiumNpc::HunterIRelationType(FElysiumEntity* Candidate)
{
	// `CNPC_VHunter::IRelationType` (`0x10388bb0`), 109 bytes — the cop arm minus BOTH player-side
	// tests. A hunter's extra hostility comes only from the one shared timed grudge.
	if (Candidate == nullptr)                                            // 10388bb6
	{
		return GCond10_D_ER;
	}
	if (World != nullptr)
	{
		// `DAT_1093b650` / `_DAT_1093b658`, written only by `0x10387fd0` and read by nothing but
		// this body.
		const FElysiumEntity* const Suspect = World->Resolve(HunterSuspectHandle());
		if (Suspect == Candidate && Cond10Now(*this) < HunterSuspectExpiry())   // 10388bf0
		{
			return GCond10_D_HT;
		}
	}
	return TroikaIRelationType(Candidate);                               // 10388c1a
}

int32 FElysiumNpc::PedestrianIRelationType(FElysiumEntity* Candidate)
{
	// `CNPC_VPedestrian::IRelationType` (`0x103a2930`), 58 bytes.
	if (Candidate == nullptr)                                            // 103a2936
	{
		return GCond10_D_ER;
	}
	// `103a2946`: the candidate's `+0x9c` carrying `D_INSANE` (`m_bfAINPCFlags2 & 0x20000`) answers
	// `D_FR` WITHOUT consulting the relationship table at all.
	const FElysiumNpc* const CandidateNpc = Candidate->AsNpc();
	if (CandidateNpc != nullptr && CandidateNpc->NpcFlags.Has(EElysiumNpcFlag2::D_INSANE))
	{
		return GCond10_D_FR;
	}
	return TroikaIRelationType(Candidate);                               // 103a2960
}

int32 FElysiumNpc::YukieIRelationType(FElysiumEntity* Candidate)
{
	// `CNPC_VYukie::IRelationType` (`0x103dd880`), 20 bytes, read off the LISTING: `MOV EAX,[ESP+4]
	// / TEST EAX,EAX / JNZ` then `RET 0x4` with `EAX` still holding the null — so a null candidate
	// answers 0, `D_ER`. Anything else is `JMP 0x10001adc`, a tail jump to the Troika body.
	if (Candidate == nullptr)
	{
		return GCond10_D_ER;
	}
	return TroikaIRelationType(Candidate);
}

// =================================================================================================
// Slot 342 `CanBeFedUponBy` — `CAI_BaseNPCTroika::CanBeFedUponBy` `0x102c4a60`.
// =================================================================================================

bool FElysiumNpc::CanBeFedUponTemplate() const
{
	// `CBaseCombatCharacter::CanBeFedUpon` (`0x10339a90`): `GetCharTemplate(this)->+0x95 == 0`.
	// **SEAM**: `+0x95` has no recovered column name and `FElysiumClanTemplate` exposes none, so
	// this answers TRUE — retail's own answer for a template byte of zero, and the ADMITTING arm.
	return true;
}

bool FElysiumNpc::IsUnconsciousMiscFlag() const
{
	// `CBaseCombatCharacter::IsUnconscious` (`0x10341aa0`) past its scope-trace push:
	// `return (m_iMiscFlags & 1) != 0`. Bit 0 of the name table at `0x10619ec8` is `Unconscious`.
	return ElysiumMiscFlags::Has(MiscFlags, ElysiumMiscFlags::Unconscious);
}

bool FElysiumNpc::BaseCanBeFedUponBy(FElysiumEntity* Feeder)
{
	// `CBaseCombatCharacter::CanBeFedUponBy` (`0x10339800`), 237 bytes. **The feeder argument is
	// never read**: every one of the five terms is about the victim, which is exactly why the Troika
	// override above it has to make the follower test itself.
	(void)Feeder;

	// `1033988a`: `CanBeFedUpon()`.
	if (!CanBeFedUponTemplate())
	{
		return false;
	}
	// `1033989a`: `m_bfAINPCFlags2 & 0x8000000` — `NOT_FEEDABLE`.
	if (NpcFlags.Has(EElysiumNpcFlag2::NOT_FEEDABLE))
	{
		return false;
	}
	// `103398a6`: a live grapple refuses. The handle `m_GrapplePartner` (`+0x1538`) must fail to
	// resolve, OR `m_GrappleRole` (`+0x153c`) must be -1; anything else is a body already in a pair.
	{
		const FElysiumEntity* const Partner =
			World != nullptr ? World->Resolve(Grapple.Partner) : nullptr;
		if (Partner != nullptr && Grapple.Role != EElysiumGrappleRole::None)
		{
			return false;
		}
	}
	// `103398f1`: slot 158 `IsAlive()` (`vtable +0x278`).
	if (!IsAlive())
	{
		return false;
	}
	// `103398fa`: `IsUnconscious()`.
	if (IsUnconsciousMiscFlag())
	{
		return false;
	}
	return true;
}

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
	return BaseCanBeFedUponBy(Feeder);
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
	if (NpcFlags.IsOblivious())                                          // 1028ef44, m_iIsOblivious > 0
	{
		return false;
	}
	// `1028ef53`: `m_bfNPCFrenziedFlags & 0x10` — the "does not witness" bit `DoFrenzy`'s
	// `0x9fbd` word carries.
	if (NpcFlags.HasFrenzied(FElysiumNpcFlags::FrenziedDoesNotWitness))
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

bool FElysiumNpc::NavIsGoalSet() const
{
	// `0x102ee2e0` — `CAI_Navigator::IsGoalSet()`, `m_pPath(+0x30)->GoalType(+0x10) != 0`. DISTINCT
	// from `0x102ee680` (`IsGoalActive`, the current-waypoint test) which family Motor wires to the
	// same latch. **SEAM**: the mover keeps one goal latch and no goal-type word, so this answers
	// that latch — the admitting value, since `IsGoalActive` implies `IsGoalSet`. The one case it
	// under-admits is a goal set with no current waypoint, which the mover cannot represent.
	return NavIsGoalActive();
}

void FElysiumNpc::NavigatorDoorCleanup()
{
	// `0x102bf7e0`: `if (nav->IsGoalSet()) nav->StopMoving(); m_bShouldMove = 1;`. The `IsGoalSet`
	// test is made TWICE — once by the caller at `102905da` and once here — and both are kept.
	if (NavIsGoalSet())
	{
		NavStopMoving();
	}
	ScheduleHost.bShouldMove = true;                                     // +0x1a40
}

void FElysiumNpc::BaseSlot532(int32 FailureBits)
{
	// `CAI_BaseNPC::vfunc532` (`0x1027e0f0`): the whole body is the two door words and `return 1`.
	(void)FailureBits;
	OpeningDoor = FElysiumEntityHandle::Invalid();                       // +0x5d24
	bOpeningDoorWait = false;                                            // +0x5d30
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
	BaseSlot532(FailureBits);
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
		FSpeciesBlacklistEntry{ BlacklistTarget, Cond10Now(*this) + SpeciesBlacklistSeconds });
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
	const double Now = Cond10Now(*this);
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

void FElysiumNpc::SpeciesTaskFail(int32 Reason)
{
	// slot 448's species prologue, called from the first line of `FElysiumNpc::TaskFail`. Every one
	// of the seven retail bodies runs its arm and then chains `0x1029adb0` UNCONDITIONALLY, so the
	// arms are a prologue and the Troika body follows — retail's order exactly.
	const TCHAR* const SlotBody = ElysiumNpcKernelClass::BodyOf(RetailClass(), 448);
	if (SlotBody == nullptr)
	{
		return;
	}
	if (FCString::Strcmp(SlotBody, GCond10Body_AsianVampireTaskFail) == 0)
	{
		AsianVampireTaskFail(Reason);
	}
	else if (FCString::Strcmp(SlotBody, GCond10Body_ChangBrosTaskFail) == 0)
	{
		ChangBrosTaskFail(Reason);
	}
	else if (FCString::Strcmp(SlotBody, GCond10Body_GargoyleTaskFail) == 0)
	{
		GargoyleTaskFail(Reason);
	}
	else if (FCString::Strcmp(SlotBody, GCond10Body_HengeyokaiTaskFail) == 0)
	{
		HengeyokaiTaskFail(Reason);
	}
	else if (FCString::Strcmp(SlotBody, GCond10Body_MingXiaoTaskFail) == 0)
	{
		MingXiaoTaskFail(Reason);
	}
	else if (FCString::Strcmp(SlotBody, GCond10Body_SheriffManTaskFail) == 0)
	{
		SheriffManTaskFail(Reason);
	}
	else if (FCString::Strcmp(SlotBody, GCond10Body_TzimisceTaskFail) == 0)
	{
		TzimisceTaskFail(Reason);
	}
	// Story 29d, family **SpeciesMisc10**: `CNPC_VWerewolf#448` (`0x103ce750`) is the EIGHTH body at
	// this slot and has the same shape — its own arm, then `0x1029adb0` unconditionally — so it joins
	// the prologue here rather than standing a second dispatcher.
	else if (FCString::Strcmp(SlotBody, TEXT("0x103ce750")) == 0)
	{
		WerewolfTaskFail(Reason);
	}
}

void FElysiumNpc::AsianVampireTaskFail(int32 Reason)
{
	// `CNPC_VAsianVampire::TaskFail` (`0x10362390`), 117 bytes, of which the scope-trace push is
	// most. `103623c5`: `if (0xb < code && code < 0x10) m_bPathBlocked = 1;`.
	if (Reason >= GCond10PathFailFirst && Reason <= GCond10PathFailLast)
	{
		bSpeciesPathBlocked = true;                                      // +0x66d4
	}
}

void FElysiumNpc::ChangBrosTaskFail(int32 Reason)
{
	// `CNPC_VChangBros::TaskFail` (`0x1036d1d0`), shared by `CNPC_VChangBrosBlade` and
	// `CNPC_VChangBrosClaw`. The same 12..15 gate as the AsianVampire arm, but the write is
	// `m_failSchedule` (`+0x5c54`) `= 0x15d` rather than the path-blocked flag.
	if (Reason >= GCond10PathFailFirst && Reason <= GCond10PathFailLast)
	{
		Schedule.FailScheduleOverride = GCond10ChangBrosFailSchedule;
	}
}

void FElysiumNpc::GargoyleTaskFail(int32 Reason)
{
	// `CNPC_VGargoyle::TaskFail` (`0x10379060`), 77 bytes.
	(void)Reason;

	// `10379063`: in COMBAT with `TASK_FAILED` (0x5c) standing as an INTERRUPT condition
	// (`0x10269d30`, not the plain `HasCondition`), clear the top bit of `m_afMemory`.
	if (Cond10RetailNpcState(Mind.State()) == GCond10NpcStateCombat
		&& ElysiumSchedule::HasInterruptCondition(Schedule, *this, Cognition.Conditions,
			EElysiumNpcCond::TaskFailed))
	{
		ScheduleHost.MemoryBits &= ~GCond10MemoryTopBit;                 // 10379077
	}

	// **CORRECTION.** The checklist's walk records `1037908c CALL 0x10006613` as reached with a
	// `this` that "has no visible prior assignment in the decompile (likely a lost this alias rather
	// than a confirmed retail bug — needs an asm check before this arm is ported)". The asm check:
	// `0x10379040` is FOUR instructions (`MOV EAX,[ECX+0x14b8] / SHR EAX,4 / AND AL,1 / RET`) and
	// never writes `ECX`, so `ECX` still holds `this` from `10379081`. There is no bug. The two
	// bodies are plain `m_bfAINPCFlags` accessors: `0x10379040` reads bit `0x10` and `0x10379000`
	// writes it — `FINDING_BODY`.
	if (NpcFlags.Has(EElysiumNpcFlag::FINDING_BODY))                     // 10379083, 0x10379040
	{
		NpcFlags.Clear(EElysiumNpcFlag::FINDING_BODY);                   // 1037908e, 0x10379000(this, 0)
	}

	SpeciesShunnedFindCount = 0;                                         // 1037909a, m_iShunnedFindPillar
}

void FElysiumNpc::HengeyokaiTaskFail(int32 Reason)
{
	// `CNPC_VHengeyokai::TaskFail` (`0x10380510`), 130 bytes.
	(void)Reason;

	if (Cond10RetailNpcState(Mind.State()) == GCond10NpcStateCombat
		&& ElysiumSchedule::HasInterruptCondition(Schedule, *this, Cognition.Conditions,
			EElysiumNpcCond::TaskFailed))
	{
		ScheduleHost.MemoryBits &= ~GCond10MemoryTopBit;                 // 10380536
	}

	// `10380548`: `0x10381be0` is `(m_bfAINPCFlags >> 4) & 1` — `FINDING_BODY`, NOT a species word.
	if (NpcFlags.Has(EElysiumNpcFlag::FINDING_BODY))
	{
		BlacklistPickupTarget(SpeciesPickupTarget);                      // 0x10382970
		NpcFlags.Clear(EElysiumNpcFlag::FINDING_BODY);                   // 0x10381ba0(this, 0)
	}

	// `1038057c`: `0x10381c80` is `(m_bfAINPCFlags >> 5) & 1` — `CARRYING_BODY`. The arm runs when
	// it is CLEAR.
	if (!NpcFlags.Has(EElysiumNpcFlag::CARRYING_BODY))
	{
		SetIgnoreCollisionExpiry(GCond10PickupReuseDelay);               // 0x102c43b0(this, 0.75)
		SpeciesPickupTarget = FElysiumEntityHandle::Invalid();           // m_hPickupTarget = -1
	}

	SpeciesShunnedFindCount = 0;                                         // m_iShunnedFindFish +0x6678
}

void FElysiumNpc::TzimisceTaskFail(int32 Reason)
{
	// `CNPC_VTzimisce::TaskFail` (`0x103ba350`), 130 bytes — the Hengeyokai arm with its own words.
	// `0x103be090` / `0x103be130` / `0x103be050` are byte-identical to the Hengeyokai's trio and are
	// the same two `m_bfAINPCFlags` bits.
	(void)Reason;

	if (Cond10RetailNpcState(Mind.State()) == GCond10NpcStateCombat
		&& ElysiumSchedule::HasInterruptCondition(Schedule, *this, Cognition.Conditions,
			EElysiumNpcCond::TaskFailed))
	{
		ScheduleHost.MemoryBits &= ~GCond10MemoryTopBit;                 // 103ba376
	}

	if (NpcFlags.Has(EElysiumNpcFlag::FINDING_BODY))                     // 0x103be090
	{
		BlacklistPickupTarget(SpeciesPickupTarget);                      // 0x103bf200
		NpcFlags.Clear(EElysiumNpcFlag::FINDING_BODY);                   // 0x103be050(this, 0)
	}
	if (!NpcFlags.Has(EElysiumNpcFlag::CARRYING_BODY))                   // 0x103be130
	{
		SetIgnoreCollisionExpiry(GCond10PickupReuseDelay);
		SpeciesPickupTarget = FElysiumEntityHandle::Invalid();           // m_hPickupTarget +0x6670
	}

	SpeciesShunnedFindCount = 0;                                         // m_iShunnedFindBody +0x66b8
}

void FElysiumNpc::MingXiaoTaskFail(int32 Reason)
{
	// `CNPC_VMingXiao::TaskFail` (`0x10394090`), 88 bytes: a switch on `m_eThrowableObjectMode`
	// (`+0x673c`).
	(void)Reason;

	if (SpeciesThrowableObjectMode == GCond10ThrowModeMotorA
		|| SpeciesThrowableObjectMode == GCond10ThrowModeMotorB)
	{
		// `103940a5`: `0x102e0a60(m_pMotor, 0x43340000)` — `m_pMotor->+0x1c = 180.0`, the same
		// steering reset the Troika `TaskFail` body itself makes, which is why the port's
		// `ResetSteering()` (already `0x102e0a60`'s body) carries the constant rather than taking it.
		static_assert(GCond10MingXiaoSteeringYaw == 180.f, "0x43340000 is 180.0f");
		if (Motor != nullptr)
		{
			Motor->ResetSteering();
		}
		return;
	}

	// **CORRECTION.** `0x10398d90` is `m_eThrowableObjectMode = arg` and nothing else; the
	// checklist's walk calls it "clear the throwable prop". The default arm therefore sets the MODE
	// to 0 and then releases the handle.
	SpeciesThrowableObjectMode = 0;                                      // 0x10398d90(this, 0)
	SpeciesThrowObject = FElysiumEntityHandle::Invalid();                // m_hThrowObject +0x6718
}

void FElysiumNpc::SheriffManTaskFail(int32 Reason)
{
	// `CNPC_VSheriffMan::TaskFail` (`0x103b0290`), 100 bytes, of which the whole is the scope-trace
	// push/pop and the chain to the base. The recovered fact is the ABSENCE of an arm: once the
	// Troika body is ported, the sheriff needs only correct dispatch.
	(void)Reason;
}

// =================================================================================================
// `0x1028d990` — the condition/flag debug string.
// =================================================================================================

bool FElysiumNpc::ScheduleDebugConditionsEnabled() const
{
	// `1028d9fd`: `DAT_10924a6c`'s `vtable[4]()` must answer 0 (the object is a variable, not a
	// command) and the int at `+0x2c` must be `> 0`. `ent_trace_conditions` ships "1", so the
	// `CONDS:` block is on as shipped.
	return ElysiumNpcTunables::ConVarInt(GCond10CvScheduleDebug) > 0;
}

FString FElysiumNpc::ConditionDebugList() const
{
	// `1028da19`..`1028da88`. `sprintf("CONDS:")` then, for every id in `[0, GetLastSharedCondition())`
	// whose `HasCondition` stands, `sprintf(" %s", GetShortConditionName(id))`, then a trailing
	// `"\n"`. Slot 409 is RE-READ on every iteration (`1028da73`), which is observable only for a
	// species that changes its answer mid-walk; none does.
	FString Out = GCond10CondsHeader;
	for (int32 Id = 0; Id < GCond10LastSharedCondition; ++Id)
	{
		// `1028da47`: `0x10269aa0 HasCondition` — the RAW condition set (`+0x5c5c`), not the
		// interrupt mask.
		if (Cognition.Conditions.Has(static_cast<EElysiumNpcCond>(Id)))
		{
			Out += FString::Printf(GCond10FmtCond,
				const_cast<FElysiumNpc*>(this)->GetShortConditionName(Id));
		}
	}
	Out += GCond10Newline;
	return Out;
}

FString FElysiumNpc::DebugMaskLadder(const TCHAR* Template, uint32 Mask, int32 Count)
{
	// `1028dac6` and `1028db20`: a set bit takes the template's character at that index, a clear bit
	// takes `'.'` (`0x2e`), and the buffer is NUL-terminated at `Count`.
	//
	// The two loops differ only in their test spelling — `TEST EAX,EDX / JZ` on the memory ladder,
	// `AND ESI,EAX / CMP ESI,EAX / JNZ` on the flags ladder — which for a single bit is the same
	// question. Both are reproduced as one function.
	FString Out;
	Out.Reserve(Count);
	const int32 TemplateLength = FCString::Strlen(Template);
	for (int32 Bit = 0; Bit < Count; ++Bit)
	{
		const bool bSet = (Mask & (1u << static_cast<uint32>(Bit))) != 0;
		Out.AppendChar(bSet && Bit < TemplateLength ? Template[Bit] : TEXT('.'));
	}
	return Out;
}

FString FElysiumNpc::NavDebugPair() const
{
	// `1028db6b`..`1028dbcd`, read from the LISTING: the decompiler dropped both `%s` arguments.
	//
	//   nav = GetNavType();  if (nav != 3 && nav != 1) -> the empty string
	//   first  %s = (nav == 3) ? "CLIMB" : "     "     (0x105d88a0 / 0x105d8898)
	//   second %s = (nav == 1) ? "JUMP"  : "    "      (0x105d88b0 / 0x105d88a8)
	//
	// So a climbing body prints `NAV CLIMB     ` and a jumping one `NAV       JUMP`; the two are
	// mutually exclusive and the blanks keep the columns aligned. `GetNavType()` (`0x1027d990`) is
	// called FIVE times building it — `NavGetType()` is this runtime's real store for that word.
	const int32 Nav = NavGetType();
	if (Nav != 3 && Nav != 1)
	{
		return FString();
	}
	return FString::Printf(GCond10FmtNav,
		Nav == 3 ? GCond10NavClimb : GCond10NavNotClimb,
		Nav == 1 ? GCond10NavJump : GCond10NavNotJump);
}

FString FElysiumNpc::BuildConditionDebugString(const TCHAR* Message, int32 IndentLevel,
	int32 BufferSize) const
{
	// `0x1028d990`, 906 bytes.

	// `1028d9a0` / `1028d9a8`: a null output buffer or a size of zero or less writes NOTHING AT ALL
	// — not even an empty string — and the whole body is skipped. The port carries the size as an
	// argument so that arm stays reachable; a negative or zero size answers the empty string.
	if (BufferSize <= 0)
	{
		return FString();
	}

	// `1028d9c1`: a null message is replaced by the empty string (a local NUL, not `DAT_106b8540`).
	const TCHAR* const SafeMessage = Message != nullptr ? Message : TEXT("");
	// `1028d9d4`: a negative indent is clamped to 0.
	const int32 Indent = FMath::Max(0, IndentLevel);
	// Retail's `%*s` field: `Indent` wide, filled with the empty string, so `Indent` spaces.
	const FString IndentField = FString::ChrN(Indent, TEXT(' '));

	// `1028da06`: the `CONDS:` block, gated by the schedule-debug ConVar alone.
	FString Conds;
	if (ScheduleDebugConditionsEnabled())
	{
		Conds = ConditionDebugList();
	}

	// The three optional blocks below share ONE gate, `DAT_10920534 == 0 || DAT_10920535 != 0`,
	// which is spelled out four separate times in the listing (`1028daad`, `1028db07`, `1028db49`,
	// `1028db5e`) against the SAME two bytes. `BL` is loaded once at `1028da99` and reloaded at
	// `1028dbd0`, so a byte that changed mid-body would be read twice — nothing changes it.
	const int32 TraceRing = DebugTraceByte(GCond10CvTraceRing);
	const int32 TraceVerbose = DebugTraceByte(GCond10CvTraceVerbose);
	const bool bBuildBlocks = TraceRing == 0 || TraceVerbose != 0;

	FString MemoryLadder;
	FString FlagsLadder;
	// **CORRECTION.** The checklist's walk says the body "renders three bitmask-to-glyph ladders".
	// There are TWO. The third buffer (`auStack_828`, `1028db56`) is only ever NUL-terminated and
	// never written, so the fifth `%s` of the full format always prints nothing. It is carried here
	// as an empty string rather than dropped, because it is a positional argument of the format.
	const FString AlwaysEmptyBlock;
	FString Nav;
	if (bBuildBlocks)
	{
		MemoryLadder = DebugMaskLadder(GCond10MemoryLegend, ScheduleHost.MemoryBits,
			GCond10MemoryLadderBits);                                    // +0x5d8c
		FlagsLadder = DebugMaskLadder(GCond10FlagsLegend, NpcFlags.RawWord1(),
			GCond10FlagsLadderBits);                                     // +0x14b8
		Nav = NavDebugPair();
	}

	const double Now = Cond10Now(*this);

	// `1028dbd6`: which of the three format strings.
	if (TraceRing != 0)
	{
		if (TraceVerbose != 0)
		{
			// `1028dc3c`, `0x105d8868`.
			return FString::Printf(GCond10FmtFull, Now, *IndentField, SafeMessage, *Conds,
				*MemoryLadder, *FlagsLadder, *AlwaysEmptyBlock, *Nav);
		}
		// `1028dc89`, `0x105d8854` — the SHORT arm, and the one case in which none of the four
		// optional blocks was built either.
		return FString::Printf(GCond10FmtShort, Now, *IndentField, SafeMessage);
	}
	// `1028dcff`, `0x105d8828` — the `DevMsg` arm, prefixed with `GetDebugName()` in a 20-wide
	// left-aligned field.
	return FString::Printf(GCond10FmtNamed, *Cond10DebugName(this), Now, *IndentField, SafeMessage,
		*Conds, *MemoryLadder, *FlagsLadder, *AlwaysEmptyBlock, *Nav);
}
