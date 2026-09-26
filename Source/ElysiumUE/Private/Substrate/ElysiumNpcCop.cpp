#include "Substrate/ElysiumNpcCop.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumKeyValues.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumOverlayStack.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumStub.h"
#include "ElysiumWorldServices.h"
#include "Misc/FileHelper.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcConditions10Shared.h"
#include "Substrate/ElysiumNpcDebug10Shared.h"
#include "Substrate/ElysiumNpcDebug10_2Shared.h"
#include "Substrate/ElysiumNpcEntityChainShared.h"
#include "Substrate/ElysiumNpcLifecycle19_2Shared.h"
#include "Substrate/ElysiumNpcSenses10Shared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcSpeciesMisc10Shared.h"
#include "Substrate/ElysiumNpcState19Shared.h"
#include "Substrate/ElysiumNpcState19_2Shared.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
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
	constexpr int32 GDebug10_2BitText = 0x1;              // 0x10372f0b `TEST byte [+0x224],0x1`
	constexpr float GDebug10_2CopLabelLiftUnits = 8.f;      // _DAT_1045597c
	// `CNPC_VCop#123`'s five relationship labels, read out of `.rdata`.
	constexpr TCHAR GDebug10_2CopLabelHate[] = TEXT("D_HT");     // 0x105cc520
	constexpr TCHAR GDebug10_2CopLabelFear[] = TEXT("D_FR");     // 0x105cc518
	constexpr TCHAR GDebug10_2CopLabelLike[] = TEXT("D_LI");     // 0x105cc510
	constexpr TCHAR GDebug10_2CopLabelNeutral[] = TEXT("D_NU");  // 0x105cc508
	constexpr TCHAR GDebug10_2CopLabelError[] = TEXT("D_ER");    // 0x10636728
	constexpr TCHAR GDebug10_2CopSuspect[] = TEXT(" Suspect");   // 0x10636750
	constexpr TCHAR GDebug10_2CopAlert[] = TEXT(" Alert");       // 0x10636748
	constexpr TCHAR GDebug10_2CopCount[] = TEXT(" Count%d");     // 0x1063673c
	constexpr TCHAR GDebug10_2CopPursuit[] = TEXT(" Pursuit");   // 0x10636730
	constexpr TCHAR GDebug10_2CopTally[] = TEXT("  %d  %d");     // 0x1063671c
	// `DAT_1093acac` and `DAT_1093acb0`, the two process-wide cop censuses. File statics because
	// retail's are file statics — the same shape family Lifecycle gave the Werewolf's shared
	// `rdtsc` pair.
	int32 GCopAliveCensus = 0;
	int32 GCopSecondCensus = 0;
	// The two relationship literals. They differ only in the case of the target word, and both are
	// handed to `InputSetRelationship` (`0x10273790`) with priority argument 0.
	const TCHAR* const GCopRelationshipLiteral = TEXT("Player D_HT 10");      // 0x106366f4
	/** `CNPC_VCop::vfunc461`'s repeated law arm (`0x103727ec` and three siblings): slot 597 with the
	 *  literal 10, then slot 596, both on `m_hClosestPlayer`. Unlike Guard1's there is no offender
	 *  compare — the cop's crazy state acts on the interrupt alone. */
	void State19_2CopLawArm(FElysiumNpc& Npc, int32 IdealRetail, int32 Line)
	{
		FElysiumEntity* const Closest = NpcKernelState19_2Shared::State19_2Resolve(Npc, Npc.Senses.Memory.ClosestPlayer);
		Npc.Slot597(Closest, 10);
		Npc.Slot596(Closest);
		NpcKernelState19_2Shared::State19_2Stamp(Npc, IdealRetail, Line);
	}
}

const FElysiumNpcClass* FElysiumNpcCop::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(RetailClassName);
	return Row;
}

// Slot 597: `0x10372cc0`, the `m_hPursuitPlayer` latch and the "Player D_HT 10" relationship in
// front of the Troika body `0x102b4fb0`, which it calls directly.
void FElysiumNpcCop::Slot597(FElysiumEntity* Other, int32 Priority)
{
	CopSlot597Prologue(Other);
	FElysiumNpc::Slot597(Other, Priority);
}

// Slot 420: `0x10372b00`.
// `0x10372b00`
void FElysiumNpcCop::NPCInit()
{
	HumanCombatantNPCInit();
	bWasEverInCombat = false;                                            // +0x6670 BYTE
}

// Slot 180: `0x10371a90`, which ends in `TroikaUpdateOnRemove`.
/** `CNPC_VCop::UpdateOnRemove` (`0x10371a90`), read off the listing — the decompiled C mis-renders
 *  the two census bytes as `this+1`. Retail, instruction for instruction:
 *
 *      DL = m_bCountedAlive (+0x6671);  AL = 0
 *      if (DL != 0) --DAT_1093acac
 *      DL = +0x6672;  m_bCountedAlive = 0
 *      if (DL != 0) --DAT_1093acb0
 *      +0x6672 = 0
 *      JMP CAI_BaseNPCTroika::UpdateOnRemove
 *
 *  Note the interleave: `+0x6672` is READ before `+0x6671` is cleared, so the two arms cannot
 *  interfere. Both counters are program-visible — `DAT_1093acac` is written by `CNPC_VCop::Spawn`
 *  and read by `CNPC_VCop::SelectSchedule`, `0x103707e0` and `0x10370850`; `DAT_1093acb0` by
 *  `CNPC_VCop::OnStateChange` and `SelectSchedule`. */
void FElysiumNpcCop::UpdateOnRemove()
{
	// `CNPC_VCop::UpdateOnRemove` `0x10371a90`, instruction for instruction off the listing — the
	// decompiled C mis-renders the two census bytes as `this+1`.
	//
	//     10371a90  MOV DL, [ECX + 0x6671]        ; m_bCountedAlive
	//     10371a96  XOR AL, AL
	//     10371a9a  JZ  ...  / DEC [0x1093acac]
	//     10371aa2  MOV DL, [ECX + 0x6672]        ; READ before the first byte is cleared
	//     10371aa8  MOV [ECX + 0x6671], AL
	//     10371ab0  JZ  ...  / DEC [0x1093acb0]
	//     10371ab8  MOV [ECX + 0x6672], AL
	//     10371abe  JMP CAI_BaseNPCTroika::UpdateOnRemove
	const bool bWasCountedAlive = bCopCountedAlive;
	if (bWasCountedAlive)
	{
		--CopAliveCensus();
	}
	const bool bWasCountedSecond = bCopCountedSecond;
	bCopCountedAlive = false;
	if (bWasCountedSecond)
	{
		--CopSecondCensus();
	}
	bCopCountedSecond = false;
	// The tail jump is a CALL to the Troika body, not a re-dispatch.
	TroikaUpdateOnRemove();
}

// Slot 463: `0x10371c20`, the pursuit latch, census and holster/draw arms (`CopOnStateChange`, whose
// tail `CopHumanCombatantOnStateChange` is `CNPC_VHumanCombatant::OnStateChange`'s weapon half), then
// that body's own direct call into the Troika body.
void FElysiumNpcCop::OnStateChange(EElysiumNpcState OldState, EElysiumNpcState NewState)
{
	CopOnStateChange(LastOnStateChangeOldRetail, LastOnStateChangeNewRetail);
	OnStateChangeTroika(OldState, NewState);
}

// Slot 461: `0x103726c0`, chaining the combatant's `0x10387380` directly.
// `CNPC_VCop::vfunc461` (`0x103726c0`) — slot 461. Two of its own arms and a chain: idle and
// alert run the pre-pass above and answer COMBAT when it takes, the crazy state `0xc` runs the
// four law conditions, and everything else goes to `CNPC_VHumanCombatPatrol` (`0x10387380`).
int32 FElysiumNpcCop::SelectIdealStateRetail()
{
	const int32 State = NpcStateRetail();
	SelectIdealStateSelector = 0xc;
	if (State == 1 || State == 3)
	{
		const int32 PrePass = CopSelectIdealStatePrePass();
		if (PrePass == 0)
		{
			return HumanCombatPatrolSelectIdealState();
		}
		// `1037299f`: the pre-pass's answer is written a second time as the ideal state.
		Mind.WriteIdealStateRetail(PrePass);
		return PrePass;
	}
	if (State != 0xc)
	{
		return HumanCombatPatrolSelectIdealState();
	}
	// `103726f4`: the crazy ladder. Unlike Guard1's there is no offender compare — the interrupt
	// alone decides, and the cop always acts on the closest player.
	if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::SupernaturalFleeLevel))
	{
		State19_2CopLawArm(*this, 8, 0x5e1);
		return 8;
	}
	if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::CriminalFleeLevel))
	{
		State19_2CopLawArm(*this, 8, 0x5f1);
		return 8;
	}
	if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::SupernaturalAttackLevel))
	{
		State19_2CopLawArm(*this, 2, 0x601);
		return 2;
	}
	if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::CriminalAttackLevel))
	{
		State19_2CopLawArm(*this, 2, 0x611);
		return 2;
	}
	// `1037298a`: nothing stands — the crazy state ends in IDLE and never reaches the chain.
	NpcKernelState19_2Shared::State19_2Stamp(*this, 1, 0x616);
	return 1;
}

// Slot 472: `0x10371ae0`
/** `CNPC_VCop::OnSeeEntity` (`0x10371ae0`) and `CNPC_VHunter::OnSeeEntity` (`0x103887d0`), the two
 *  54-byte twins. Each stamps its OWN class-static suspect pair and then runs the Troika body
 *  (`0x102b3e00`) unconditionally. The cop's stamp (`0x10370560`) is guarded on the seen entity
 *  carrying a player record at `+0xa8`; the hunter's (`0x10387fd0`) is NOT — recorded because it is
 *  the only difference between them. */
void FElysiumNpcCop::OnSeeEntity(FElysiumEntity* Seen)
{
	// `10371ae0`: when `+0x6081` is CLEAR and slot 404 answers D_HT, stamp; then the Troika body
	// runs UNCONDITIONALLY.
	if (!Senses.Memory.bPlayerInOuterBand && IRelationTypeOf(Seen) == NpcKernelSenses10Shared::GD_HT)
	{
		StampCopSuspect(Seen);
	}
	FElysiumNpc::OnSeeEntity(Seen);
}

// Slot 404: `0x10372b70`.
/** `CNPC_VCop::IRelationType` (`0x10372b70`), 170 bytes — three arms in front of the Troika body:
 *  a null candidate answers `D_ER` outright, the cop class's shared timed grudge answers `D_HT`,
 *  and a player candidate answers `D_HT` on `m_flHeightenedAlertExpireTimer` or a non-zero
 *  `m_iCopsInPursuitCount`. Reached by every placed `npc_VCop` since story 5 step 2 registered
 *  the classname as `CNPC_VCop` (factory `0x103704f0`); before, the census gave the class no
 *  classname and a cop took the Troika line. */
int32 FElysiumNpcCop::IRelationType(FElysiumEntity* Candidate)
{
	// `CNPC_VCop::IRelationType` (`0x10372b70`), 170 bytes — three arms in front of the Troika body.

	// `10372b76`: a null candidate answers `D_NO`/`D_ER` 0 here rather than reaching the base.
	if (Candidate == nullptr)
	{
		return NpcKernelConditions10Shared::GCond10_D_ER;
	}

	// `10372b84`: the cop class's SHARED provoker handle `DAT_1093ac3c` and its expiry
	// `_DAT_1093aca8`, written by `CNPC_VCop::OnSeeEntity`'s stamp (`0x10370560`, family Senses10)
	// and read here and by `CNPC_VCop::DrawDebugGeometryOverlays`. One grudge for every cop in the
	// map, which is why it is a file static in family Senses10 and reached through its accessors.
	if (World != nullptr)
	{
		const FElysiumEntity* const Suspect = World->Resolve(CopSuspectHandle());
		if (Suspect == Candidate && NpcKernelConditions10Shared::Cond10Now(*this) < CopSuspectExpiry())
		{
			return NpcKernelConditions10Shared::GCond10_D_HT;
		}
	}

	// `10372bc6`: the candidate's player record (`+0xa8`). Both words are real on `FElysiumPlayer`;
	// a non-player candidate answers false / 0, which is retail's null-`+0xa8` arm.
	// `0x1017f8d0` is `curtime < player->m_flHeightenedAlertExpireTimer` (`+0x1d1c`).
	if (PlayerHeightenedAlert(Candidate))
	{
		return NpcKernelConditions10Shared::GCond10_D_HT;
	}
	// `0x1017f770` is `player->m_iCopsInPursuitCount` (`+0x1d10`), and the test is `> 0`.
	if (PlayerCopsInPursuitCount(Candidate) > 0)
	{
		return NpcKernelConditions10Shared::GCond10_D_HT;
	}

	// `10372c0a`: the DIRECT thunk to `CAI_BaseNPCTroika::IRelationType`.
	return TroikaIRelationType(Candidate);
}

// Slot 440: `0x10372150`.
// `0x10372150`
// `0x10372150`, `CNPC_VCop::TranslateSchedule`, the body of `FElysiumNpcCop::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpcCop::TranslateScheduleRetail(int32 ScheduleNumber)
{
	if (ScheduleNumber == 0x6b)
	{
		return NpcFlags.Has(EElysiumNpcFlag2::D_MILDLY_CRAZY) ? 0x132 : 0x15b;
	}
	if (ScheduleNumber == 0x89) { return 0x15e; }
	if (ScheduleNumber == 0x94) { return 0x15c; }
	if (ScheduleNumber == 0x96) { return 0x15d; }
	if (ScheduleNumber == 0x103) { return 0x15a; }
	if (ScheduleNumber >= 0xaa && ScheduleNumber <= 0xb7)
	{
		return 0x160 + (ScheduleNumber - 0xaa);
	}
	return TroikaTranslateScheduleRetail(ScheduleNumber);
}

// Slot 123: `0x10372f00`
/** `CNPC_VCop::DrawDebugGeometryOverlays` (`0x10372f00`) — the relationship label above the cop's
 *  head, then the Troika body unconditionally. */
void FElysiumNpcCop::DrawDebugGeometryOverlays()
{
	// `0x10372f00`, 1,020 bytes. Debug-only, but the arms are the recovered statement of what a cop
	// knows about the player: the shared timed grudge, the heightened-alert window, the pursuit
	// count and its own pursuit target, all rendered as one label above its head.
	//
	// Gates, in order: `m_debugOverlays & 1`, then `m_hClosestPlayer` must RESOLVE, then the
	// collision OBB must not be degenerate. Each failure jumps straight to the Troika tail.
	if ((DebugOverlays & GDebug10_2BitText) != 0)
	{
		const FElysiumEntity* const Player =
			World != nullptr ? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;
		FVector ObbMins = FVector::ZeroVector;
		FVector ObbMaxs = FVector::ZeroVector;
		const bool bHasObb = CollisionObbExtentsUnits(ObbMins, ObbMaxs);
		const bool bDegenerate = !bHasObb
			|| (ObbMins.X == ObbMaxs.X && ObbMins.Y == ObbMaxs.Y && ObbMins.Z == ObbMaxs.Z);
		if (Player != nullptr && !bDegenerate)
		{
			// The label sits `(maxs.z - mins.z) + 8.0` above `GetAbsOrigin()`.
			const float LiftUnits = (ObbMaxs.Z - ObbMins.Z) + GDebug10_2CopLabelLiftUnits;
			FVector LabelUnits = Origin / ElysiumMove::U;
			LabelUnits.Z += LiftUnits;

			FString Label;
			switch (IRelationType(const_cast<FElysiumEntity*>(Player)))
			{
			case 1:
				// The `D_HT` arm re-runs the TROIKA `IRelationType` (`0x10299da0`) first and throws
				// the answer away — an artefact of the species override calling its own base, and
				// reproduced only as this comment because it writes nothing.
				Label = GDebug10_2CopLabelHate;
				if (CopSuspectIs(Player))
				{
					Label += GDebug10_2CopSuspect;
				}
				if (PlayerHeightenedAlert(Player))
				{
					Label += GDebug10_2CopAlert;
				}
				{
					const int32 Pursuers = PlayerCopsInPursuitCount(Player);
					if (Pursuers > 0)
					{
						// Retail asks `0x1017f770` a SECOND time for the printed number, after the
						// `> 0` test; one read is the same answer.
						Label += FString::Printf(GDebug10_2CopCount, Pursuers);
					}
				}
				if (CopPursuitPlayer() == Player)
				{
					Label += GDebug10_2CopPursuit;
				}
				break;
			case 2: Label = GDebug10_2CopLabelFear; break;
			case 3: Label = GDebug10_2CopLabelLike; break;
			case 4: Label = GDebug10_2CopLabelNeutral; break;
			default: Label = GDebug10_2CopLabelError; break;
			}

			// `"  %d  %d"` with two further cop-class statics, `DAT_1093acac` and `DAT_1093acb0`.
			// SEAM: neither is stood here and neither has a writer in this band; both read 0.
			Label += FString::Printf(GDebug10_2CopTally, 0, 0);
			EmitOverlayText(NpcKernelDebug10_2Shared::GDebug10_2Text, LabelUnits, Label);
		}
	}

	// The Troika body ALWAYS runs, whichever gate turned the label off.
	TroikaDrawDebugGeometryOverlays();
}

// Slot 546: `0x10370ad0`, the class's own schedule id space.
const TCHAR* FElysiumNpcCop::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1093ac40`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VCop"), TEXT("0x10370ad0"), TEXT("0x1093ac40") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}

// --- Moved from `ElysiumNpcConditions10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcDebug10.cpp` (story 5 step 4) ---

FElysiumEntity* FElysiumNpcCop::CopPursuitPlayer() const
{
	// `CNPC_VCop::m_hPursuitPlayer` (`+0x6664`). **No longer a seam**: story 29d's family
	// SpeciesMisc10 landed the WRITER (`CNPC_VCop#597`, `0x10372cc0`) and the word with it, so this
	// resolves it. A cop that has not latched a pursuit still answers null, which is retail's own
	// answer for the `0xffffffff` the latch writes when the seen entity carries no player record.
	return World != nullptr ? World->Resolve(CopPursuitHandle) : nullptr;
}

bool FElysiumNpcCop::CopSuspectIs(const FElysiumEntity* Candidate) const
{
	// SEAM for the cop class's two statics, `DAT_1093ac3c` (the shared provoker handle) and
	// `_DAT_1093aca8` (its expiry). Family Senses10's `CNPC_VCop::OnSeeEntity` (`0x10370560`) is the
	// writer and family Conditions10's `CNPC_VCop::IRelationType` the other reader.
	(void)Candidate;
	return false;
}

// --- Moved from `ElysiumNpcDebug10_2.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcEntityChain.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcLifecycle19_2.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcSaveRestore10.cpp` (story 5 step 4) ---

int32& FElysiumNpcCop::CopAliveCensus()
{
	return GCopAliveCensus;
}

int32& FElysiumNpcCop::CopSecondCensus()
{
	return GCopSecondCensus;
}

// --- Moved from `ElysiumNpcSenses10.cpp` (story 5 step 4) ---

FElysiumEntityHandle FElysiumNpcCop::CopSuspectHandle() { return NpcKernelSenses10Shared::GCopSuspect; }

double FElysiumNpcCop::CopSuspectExpiry() { return NpcKernelSenses10Shared::GCopSuspectExpiry; }

void FElysiumNpcCop::StampCopSuspect(FElysiumEntity* Seen)
{
	// `0x10370560`: `_DAT_1093aca8 = curtime + _DAT_104492a8` and `DAT_1093ac3c = seen->handle`,
	// but ONLY when the seen entity carries a non-null `+0xa8` player record.
	if (!NpcKernelSenses10Shared::IsPlayerRecord(*this, Seen))
	{
		return;
	}
	NpcKernelSenses10Shared::GCopSuspectExpiry = NpcKernelSenses10Shared::NowOf(*this) + SpeciesSuspectWindowSeconds;
	NpcKernelSenses10Shared::GCopSuspect = Seen->Handle;
}

// --- Moved from `ElysiumNpcSpeciesMisc10.cpp` (story 5 step 4) ---

void FElysiumNpcCop::CopSlot597Prologue(FElysiumEntity* Other)
{
	// `10372ce6`: the whole body is gated on the argument being the very entity `m_hClosestPlayer`
	// (`+0x628c`) resolves to. A stale handle resolves to null, and a null argument then matches it —
	// which is retail's own behaviour and is reproduced rather than guarded.
	const FElysiumEntity* Closest = World != nullptr
		? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;
	if (Other != Closest)
	{
		return;
	}
	// `10372cf8`: `m_hPursuitPlayer` (`+0x6664`) does NOT resolve to a live entity...
	const bool bPursuitLive = World != nullptr && World->Resolve(CopPursuitHandle) != nullptr;
	// `10372d20`: ...AND `GetState` (slot 464, `vt+0x740`) is 2 (COMBAT).
	if (!bPursuitLive && GetState() == EElysiumNpcState::Combat)
	{
		// `10372d2b`: CORRECTION — `param_1[0x2a]` is `+0xa8 m_pPlayer`, the player self-downcast
		// cache, not a "troika sub-object": the latch stores the PLAYER's own handle, and writes
		// `0xffffffff` when the argument carries no player record.
		CopPursuitHandle = NpcKernelSpeciesMisc10Shared::SpeciesMisc10IsPlayer(*this, Other)
			? Other->Handle : FElysiumEntityHandle::Invalid();
	}
	// `10372d5b`: UNCONDITIONALLY for that same argument, `InputSetRelationship("Player D_HT 10", 0)`
	// — note the capital `P`, unlike `CNPC_VGuard1`'s literal.
	FElysiumInputArgs Args;
	Args.Param = FElysiumVariant::String(GCopRelationshipLiteral);
	Args.Activator = Other != nullptr ? Other->Handle : FElysiumEntityHandle::Invalid();
	Args.Caller = Handle;
	Args.Input = FName(TEXT("SetRelationship"));
	InputSetRelationship(Args);
}

// --- Moved from `ElysiumNpcState19.cpp` (story 5 step 4) ---

void FElysiumNpcCop::CopOnStateChange(int32 OldRetail, int32 NewRetail)
{
	FElysiumEntity* const Enemy = GetEnemy();
	if (Enemy != nullptr && NewRetail == 2)
	{
		Slot597(Enemy, 10);
	}
	if (OldRetail == 2)
	{
		// `10371c69`: the pursuit release. `0x1017f6e0` is the counter half — it DECREMENTS the
		// pursued player's `+0x1d10`, and on the zero crossing runs `0x10370630` and `0x1017f9c0`.
		// `CopSlot597Prologue` (family SpeciesMisc10) is the matching increment; without this the
		// counter only ever goes up.
		if (FElysiumEntity* const Pursuit = CopPursuitPlayer())
		{
			Slot598(Pursuit);
			CopPursuitHandle = FElysiumEntityHandle::Invalid();
			RemoveCopInPursuit();
		}
		if (Enemy != nullptr)
		{
			Slot598(Enemy);
		}
	}
	switch (NewRetail)
	{
	case 2:
		bWasEverInCombat = true;
		break;
	case 3:
		if (FElysiumItem* const Active = Inventory.Active(*this))
		{
			if (FElysiumWeapon* const Weapon = Active->AsWeapon())
			{
				Weapon->Unhide(this);
			}
		}
		[[fallthrough]];
	case 1:
	case 8:
		if (Enemy != nullptr)
		{
			Slot598(Enemy);
		}
		// `10371d32`: `m_bWasEverInCombat` AND a closest player that actually resolves. Retail
		// jumps past the call on every failed term — it never dispatches slot 598 with null here.
		if (bWasEverInCombat)
		{
			if (FElysiumEntity* const Closest = World != nullptr
				? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr)
			{
				Slot598(Closest);
			}
		}
		break;
	default:
		break;
	}
	if (NewRetail == 1)
	{
		SetForceFrequentThink(false);
		CopHumanCombatantOnStateChange(NewRetail);
	}
	else
	{
		// `10371dd4`: the census decrement is gated on `+0x6672`, family SaveRestore10's
		// `bCopCountedSecond` — the same word, not a second copy of it.
		if (bCopCountedSecond)
		{
			--NpcKernelState19Shared::GState19CopCensus;
			bCopCountedSecond = false;
		}
		SetForceFrequentThink(true);
		CopHumanCombatantOnStateChange(NewRetail);
	}
}

// --- Moved from `ElysiumNpcState19_2.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcTranslate19.cpp` (story 5 step 4) ---

