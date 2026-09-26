#include "Substrate/ElysiumNpcHuman.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Debug/ElysiumNpcDebugLogging.h"
#include "ElysiumAnimEvent.h"
#include "ElysiumAnimationIntent.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "ElysiumSchedule.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumSoundLevel.h"
#include "ElysiumStub.h"
#include "ElysiumSurfaceSounds.h"
#include "ElysiumWorldServices.h"
#include "HAL/IConsoleManager.h"
#include "Player/ElysiumCameraShots.h"
#include "Substrate/ElysiumAiScriptedSchedule.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumDisciplines.h"
#include "Substrate/ElysiumFeed.h"
#include "Substrate/ElysiumFootsteps.h"
#include "Substrate/ElysiumHint.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpcCombatSchedules.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcDialogue.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcAnim10Shared.h"
#include "Substrate/ElysiumNpcAnim10_2Shared.h"
#include "Substrate/ElysiumNpcCombat10_2Shared.h"
#include "Substrate/ElysiumNpcLifecycle19Shared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLoadout.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcThinkCadence.h"
#include "Substrate/ElysiumPhysProp.h"
#include "Substrate/ElysiumReactions.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"
#include "Visual/ElysiumActionTables.h"

// --- File-scope helpers moved with this class's bodies (story 5 step 4) ---

namespace
{
	constexpr int32 GAnim10ActAim = 5;              // the armed-idle the human body rewrites 1 into
	constexpr int32 GAnim10ActWalkAim = 0x11;       // ACT_WALK_AIM
	constexpr int32 GAnim10ActRunAim = 0x15;        // ACT_RUN_AIM
	// The six aggressive-set rewrites of `0x103854f0`, as pairs.
	constexpr int32 GAnim10AggressivePairs[][2] = {
		{ 0x3b, 0x3d }, { 0x3c, 0x3e }, { 0x9d, 0x9f },
		{ 0x9e, 0xa0 }, { 0xa1, 0xa3 }, { 0xa2, 0xa4 },
	};
	constexpr uint32 GAnim10FrenziedForceAggressive = 0x200;
	constexpr uint32 GAnim10MemoryAggressive = 0x8000000;
	// The active weapon's `+0x19c` NODRAW bit, which makes the human body treat an armed NPC as
	// unarmed, and its slot-360 (`+0x5a0`) ranged-aim bits.
	constexpr uint32 GAnim10WeaponNoDraw = 0x40;
	constexpr NpcKernelAnim10Shared::EAnim10ConVar GAnim10CvAggressive = NpcKernelAnim10Shared::EAnim10ConVar::DebugAllowMoveFacing; // 0x103854f0's combat-aggression
	constexpr NpcKernelAnim10Shared::EAnim10ConVar GAnim10CvAlert = NpcKernelAnim10Shared::EAnim10ConVar::DebugAlertAggressive;      // ... for m_NPCState 3
	constexpr NpcKernelAnim10Shared::EAnim10ConVar GAnim10CvHunt = NpcKernelAnim10Shared::EAnim10ConVar::DebugHuntingAggressive;     // ... for m_NPCState 0xb
	// The two source-file strings the selector trace stamps into `+0x1b30`.
	constexpr TCHAR GAnim10_2FileHuman[] = TEXT("NPC_VHuman.cpp");        // 0x1063f724
	const TCHAR* const GHumanFile = TEXT("NPC_VHuman.cpp");
}

const FElysiumNpcClass* FElysiumNpcHuman::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(RetailClassName);
	return Row;
}

// Slot 482: `0x103850a0` (the ledger indexes it under `CNPC_VAndreiBlood`; every human-line class
// holds it), a standalone copy with the SCRIPT-state tail: family Bosses' `CanPlaySequenceSpecies`.
int32 FElysiumNpcHuman::CanPlaySequence(bool bDisregardState, int32 InterruptLevel)
{
	return CanPlaySequenceSpecies(bDisregardState, InterruptLevel);
}

// The melee quartet on the human line: 599 `0x10385ab0` and 600 `0x10385c30` are byte-identical
// copies of the Troika bodies and 602 `0x10385d70` drops only the coordinator null test (a named
// divergence family TroikaHelpers applies to both lines), so the port runs the one Troika body for
// each; 601 `0x10385cf0` is family Bosses' own body.
bool FElysiumNpcHuman::Slot599(int32 Arg)
{
	return FElysiumNpc::Slot599(Arg);
}

bool FElysiumNpcHuman::Slot600(FElysiumEntity* Enemy)
{
	return FElysiumNpc::Slot600(Enemy);
}

void FElysiumNpcHuman::Slot601(FElysiumEntity* Enemy)
{
	(void)Enemy;
	FUN_10385cf0();
}

bool FElysiumNpcHuman::Slot602()
{
	return FElysiumNpc::Slot602();
}

// Slot 375: `0x103854f0`, which calls the Troika body `0x10295590` directly.
int32 FElysiumNpcHuman::NPC_EarlyTranslateActivity(int32 Activity)
{
	return HumanNpcEarlyTranslateActivity(Activity);
}

// Slot 461: `0x103851e0`, chaining the Troika body directly.
int32 FElysiumNpcHuman::SelectIdealStateRetail()
{
	return HumanSelectIdealState();
}

// Slot 604: `0x10385e40`, which replaces the Troika body wholesale; its argument is read by no arm.
int32 FElysiumNpcHuman::SelectScheduleMeleeCombat(int32 Unused)
{
	(void)Unused;
	return SelectScheduleMeleeCombatHuman();
}

// Slot 605: `0x10386560`
int32 FElysiumNpcHuman::SelectScheduleRangedCombat(int32 Arg)
{
	return HumanSelectScheduleRangedCombat(Arg);
}

// Slot 546: `0x10384200`, the class's own schedule id space.
const TCHAR* FElysiumNpcHuman::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1093b3c4`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VHuman"), TEXT("0x10384200"), TEXT("0x1093b3c4") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}

// Slot 563: `0x10384760`, the `OffsetOnly` shape; a replacement that does not chain.
void FElysiumNpcHuman::TranslateEnemyChasePosition(FElysiumEntity* Enemy, FVector& ChasePositionCm,
	void* Tolerance, void* SecondTolerance)
{
	TranslateEnemyChasePositionShaped(EChaseTranslateShape::OffsetOnly, Enemy, ChasePositionCm, Tolerance,
		SecondTolerance);
}

// Slot 366: `0x10385a70` (every human-line class holds it; the ledger indexes it under
// `CNPC_VAndreiBlood`) — the whole body is `return 0;`.
bool FElysiumNpcHuman::HandleInteraction(int32 Interaction, void* Data, FElysiumEntity* Other)
{
	// `vtmb_code` shows the whole function is `return 0;`; 59 census classes share it.
	(void)Interaction;
	(void)Data;
	(void)Other;
	return false;
}

// --- Moved from `ElysiumNpc.cpp` (story 5 step 4) ---

void FElysiumNpcHuman::ApplyStateWeaponVisibility(EElysiumNpcState NewState)
{
	// The holster/draw switch of `CNPC_VHumanCombatant::OnStateChange` (`0x103871c0`) and its copy
	// in `CNPC_VGuard1`'s (`0x1037d020`). Only those classes' `OnStateChange` overrides call it
	// (story 5 step 3); the Troika body `0x102ae140` does not touch the weapon.
	FElysiumItem* Active = Inventory.Active(*this);
	FElysiumWeapon* Weapon = Active != nullptr ? Active->AsWeapon() : nullptr;
	if (Weapon == nullptr)
	{
		// `GetActiveWeapon()` answered null, and both arms of the retail switch are guarded by it.
		return;
	}

	switch (NewState)
	{
	case EElysiumNpcState::Idle:
		// State 1. `GetActiveWeapon()->Hide()`.
		Weapon->Hide(this);
		break;
	case EElysiumNpcState::Alert:
	case EElysiumNpcState::Combat:
		// States 2 and 3. `GetActiveWeapon()->Unhide()`.
		//
		// SEAM: retail's third unhide arm is state 11, which this runtime's `EElysiumNpcState` has
		// no equivalent for — the enum's other members (Scripted, Prone, Dead) are this port's own
		// and none of them is retail's 11. Nothing is known about what 11 means beyond the fact that
		// it draws the weapon, so no port state is mapped onto it rather than guessing one.
		Weapon->Unhide(this);
		break;
	default:
		// Every other state falls straight through to the Troika base, which writes nothing. That
		// includes this runtime's Scripted, Prone and Dead.
		break;
	}
}

// --- Moved from `ElysiumNpcAnim10.cpp` (story 5 step 4) ---

uint32 FElysiumNpcHuman::ActiveWeaponDrawFlags() const
{
	// The active weapon's `+0x19c`, whose bit `0x40 NODRAW` makes `0x103854f0` treat an armed body as
	// unarmed. SEAM: no port member carries the weapon's draw flags — the port's weapon state is the
	// item catalogue row, which has no such word — so this answers 0, the arm in which the weapon
	// DOES draw and the aggressive decision tree runs. Answering `0x40` would clear
	// `m_bAggressiveAnims` for every armed body and make the whole tree unreachable.
	return 0u;
}

int32 FElysiumNpcHuman::HumanNpcEarlyTranslateActivity(int32 Activity)
{
	// `CNPC_VHuman::NPC_EarlyTranslateActivity` `0x103854f0`, 565 bytes, slot 375 for 39 census
	// classes. The armed/alert decision tree, then the rewrites.
	int32 Request = Activity;

	// 1. Capability bit 0x40 removes aim from a gait request.
	if ((CapabilitiesGet() & NpcKernelAnim10Shared::GAnim10CapNoAimGait) != 0)
	{
		if (Request == GAnim10ActWalkAim)
		{
			Request = NpcKernelAnim10Shared::GAnim10ActWalk;
		}
		else if (Request == GAnim10ActRunAim)
		{
			Request = NpcKernelAnim10Shared::GAnim10ActRun;
		}
	}

	// 2. No active weapon, or one whose `+0x19c` carries `0x40 NODRAW`: `m_bAggressiveAnims` is
	//    CLEARED and the request goes straight to the Troika pre-translate, skipping BOTH rewrite
	//    blocks. Retail calls `GetActiveWeapon()` twice here; the second call is what reads `+0x19c`.
	const FElysiumEntity* Weapon = ActiveWeaponEntity();
	if (Weapon == nullptr || (ActiveWeaponDrawFlags() & GAnim10WeaponNoDraw) == GAnim10WeaponNoDraw)
	{
		bAggressiveAnims = false;
		return TroikaNpcEarlyTranslateActivity(Request);
	}

	// 3. The decision tree, in retail's own nesting. The OUTER test is a four-term OR: the aggressive
	//    arm is taken only when capability `0x40` is set AND the ConVar `debug_allow_move_facing`
	//    (`DAT_10924f74`, ships 1) is non-zero AND `m_bfAINPCFlags2` carries `0x400 MOVE_FACE_ENEMY`.
	const bool bCombatAggressive = (CapabilitiesGet() & NpcKernelAnim10Shared::GAnim10CapNoAimGait) != 0
		&& ElysiumNpcTunables::ConVarInt(GAnim10CvAggressive) != 0
		&& NpcFlags.Has(EElysiumNpcFlag2::MOVE_FACE_ENEMY);
	if (bCombatAggressive)
	{
		bAggressiveAnims = true;
	}
	else if (NpcFlags.Has(EElysiumNpcFlag::FORCE_RELAXED_ANIMS))
	{
		// `m_bfAINPCFlags & 0x10000` wins over everything below it.
		bAggressiveAnims = false;
	}
	else if (NpcFlags.HasFrenzied(GAnim10FrenziedForceAggressive))
	{
		// `m_bfNPCFrenziedFlags & 0x200` skips the state ladder entirely and sets the flag.
		bAggressiveAnims = true;
	}
	else
	{
		// The state ladder. Retail CLEARS the flag first and then decides, which is why a state
		// outside {2, 3, 0xb} leaves it clear.
		bAggressiveAnims = false;
		const int32 State = NpcKernelAnim10Shared::Anim10RetailNpcState(Mind.State());
		if (State == 2)
		{
			bAggressiveAnims = true;
		}
		else if (State == 3 || State == 0xb)
		{
			// **THE POLARITY, read at the listing.** `goto LAB_10385611` — which leaves the flag
			// CLEAR — requires `(probe != 0 || cv[0xb] == 0) && (m_afMemory & 0x8000000) == 0`, i.e.
			// the ConVar is dead or zero AND the memory bit is clear. So the flag is SET when the
			// ConVar is live and non-zero, OR `m_afMemory` carries `0x8000000`. The pack-07 walk had
			// this arm inverted; the body wins.
			const int32 StateConVar = State == 3
				? ElysiumNpcTunables::ConVarInt(GAnim10CvAlert)   // `debug_alert_aggressive`, 0
				: ElysiumNpcTunables::ConVarInt(GAnim10CvHunt);   // `debug_hunting_aggressive`, 1
			const bool bMemory =
				(ScheduleHost.MemoryBits & GAnim10MemoryAggressive) != 0;
			if (StateConVar != 0 || bMemory)
			{
				bAggressiveAnims = true;
			}
		}
	}

	// 4. Aggressive CLEAR rewrites the two gaits into their relaxed forms — through the Troika body,
	//    not by returning directly.
	if (!bAggressiveAnims)
	{
		if (Request == NpcKernelAnim10Shared::GAnim10ActWalk)
		{
			return TroikaNpcEarlyTranslateActivity(NpcKernelAnim10Shared::GAnim10ActWalkRelaxed);
		}
		if (Request == NpcKernelAnim10Shared::GAnim10ActRun)
		{
			return TroikaNpcEarlyTranslateActivity(NpcKernelAnim10Shared::GAnim10ActRunRelaxed);
		}
		return TroikaNpcEarlyTranslateActivity(Request);
	}

	// 5. Aggressive SET. `ACT_IDLE` becomes the armed idle ONLY when there is an active weapon whose
	//    slot 360 answer carries `0x6000`; retail re-fetches the weapon twice here too, and a null
	//    one falls out of the switch rather than taking the rewrite.
	if (Request == NpcKernelAnim10Shared::GAnim10ActIdle)
	{
		if (ActiveWeaponEntity() != nullptr
			&& (ActiveWeaponCapabilityWord() & NpcKernelAnim10Shared::GAnim10WeaponRangedAim) != 0)
		{
			return TroikaNpcEarlyTranslateActivity(GAnim10ActAim);
		}
		return TroikaNpcEarlyTranslateActivity(Request);
	}
	for (const int32(&Pair)[2] : GAnim10AggressivePairs)
	{
		if (Request == Pair[0])
		{
			return TroikaNpcEarlyTranslateActivity(Pair[1]);
		}
	}
	// 6. Everything else falls to the Troika pre-translate unchanged.
	return TroikaNpcEarlyTranslateActivity(Request);
}

// --- Moved from `ElysiumNpcKernelAnim10_2.cpp` (story 5 step 4) ---

int32 FElysiumNpcHuman::SelectScheduleMeleeCombatHuman()
{
	// `CNPC_VHuman::SelectScheduleMeleeCombat` `0x10385e40`, 1,449 bytes, slot 604 for 34 census
	// classes. It REPLACES the Troika body `0x102b6c30` wholesale and never chains it.
	//
	// Every one of the float compares below was decoded off the LISTING, because the decompiler
	// renders three of them as `(a < b) != (a == b)` and `(a < b) == (a == b)`, which are `a <= b`
	// and `a > b` and are easy to read the wrong way round. The instruction is cited at each.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	FElysiumEntity* Enemy = GetEnemy();   // vtable +0x29c, fetched ONCE and reused
	const float Range = MeleeRangeUnits();              // DAT_10924a1c
	const float Distance = ScheduleHost.EnemyDistUnits; // +0x6268 m_flEnemyDist
	const FElysiumNpcConditions& Conds = Cognition.Conditions;

	if (!bInMelee)   // +0x6078 m_bInMelee
	{
		if (!Slot599(0))   // vtable +0x95c — "should I enter melee"
		{
			// `10385f25`: the melee failure gate `0x102b6fe0` is offered FIRST and any non-zero
			// answer returns.
			const int32 Gate = MeleeScheduleFailureGate(Enemy);
			if (Gate != 0)
			{
				return Gate;
			}
			// `10385f5b TEST AH,0x5; JP` — the roll is reached when `range + 200 >= distance`, i.e.
			// the far arm needs `range + 200 < distance` STRICTLY.
			if (Range + NpcKernelAnim10_2Shared::GAnim10_2FarMargin < Distance)
			{
				RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe7"), GAnim10_2FileHuman, 1563));
				return 0xe7;
			}
			// `10385f99 CMP EAX,0x19; JL` — the roll must EXCEED 24, and `10385fc6 AND EAX,0x4100;
			// JZ` — the second distance test passes on `range <= distance`.
			if (NpcKernelAnim10_2Shared::Anim10_2Rng().RandRange(0, 99) > NpcKernelAnim10_2Shared::GAnim10_2RollFloor && Range <= Distance)
			{
				RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe5"), GAnim10_2FileHuman, 1575));
				return 0xe5;
			}
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe4"), GAnim10_2FileHuman, 1571));
			return 0xe4;
		}
	}
	else if (Slot602())   // vtable +0x968 — "should I leave melee"
	{
		Slot601(Enemy);   // vtable +0x964
		if (NpcKernelAnim10_2Shared::Anim10_2HasRangedWeapon(*this))   // vtable +0x4d0, slot 308
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe9"), GAnim10_2FileHuman, 1535));
			return 0xe9;
		}
		// `10385ee1 TEST AH,0x41; JP` — `2 * range <= distance` takes the far arm. **AT OR beyond**
		// twice the range, not strictly beyond; the checklist's walk said "exceeds".
		if (Range + Range <= Distance)
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe7"), GAnim10_2FileHuman, 1541));
			return 0xe7;
		}
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe4"), GAnim10_2FileHuman, 1545));
		return 0xe4;
	}

	// --- The common tail (10386011) -----------------------------------------------------------------
	//
	// TWO offers, `0x102b7370` then `0x102b6fe0`, and either non-zero answer returns. The first has
	// no port body of its own; family Schedule left it as the entrenched-cover helper's sibling and
	// it is reached here through `ScheduleEntrenchedCoverOffer`, which answers 0.
	// `thunk_FUN_102b7370(this)` — `SelectDoorObstructionSchedule`, which `FElysiumNpc` already
	// carries (`ElysiumNpc.cpp`) as an `int32`; converted back to retail's number
	// because retail's `if (answer != 0) return answer` is over the raw one.
	const int32 DoorOffer =
		SelectDoorObstructionSchedule();
	if (DoorOffer != 0)
	{
		return DoorOffer;
	}
	const int32 Gate = MeleeScheduleFailureGate(Enemy);
	if (Gate != 0)
	{
		return Gate;
	}

	// The condition ladder, in strict retail order. The first two go through `0x10269d30`
	// (`HasInterruptCondition`) and the rest through `0x10269aa0` (`HasCondition`) — two different
	// questions, and the split is retail's.
	if (ElysiumSchedule::HasInterruptCondition(Schedule, *this, Conds,
			EElysiumNpcCond::ShouldDodge))
	{
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd5"), GAnim10_2FileHuman, 1597));
		return 0xd5;
	}
	if (ElysiumSchedule::HasInterruptCondition(Schedule, *this, Conds,
			EElysiumNpcCond::ShouldBlock))
	{
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd6"), GAnim10_2FileHuman, 1601));
		return 0xd6;
	}
	// BOTH conditions are read before either is tested (`1038608e` / `10386097`), so the second read
	// happens even when the first is set.
	const bool bKick = Conds.Has(EElysiumNpcCond::ShouldKick);
	const bool bStepback = Conds.Has(EElysiumNpcCond::ShouldStepback);
	if (bKick)
	{
		// `1038638f`: kick alone answers 0xdb; kick AND stepback flips a coin and answers 0xdb on a
		// 1 and 0xd3 on anything else.
		if (!bStepback || NpcKernelAnim10_2Shared::Anim10_2Rng().RandRange(0, 1) == 1)
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xdb"), GAnim10_2FileHuman, 1611));
			return 0xdb;
		}
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd3"), GAnim10_2FileHuman, 1615));
		return 0xd3;
	}
	if (bStepback)
	{
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd3"), GAnim10_2FileHuman, 1615));
		return 0xd3;
	}
	if (Conds.Has(EElysiumNpcCond::CanMeleeAttack1))
	{
		if (NpcKernelAnim10_2Shared::Anim10_2HasRangedWeapon(*this))
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xdc"), GAnim10_2FileHuman, 1625));
			return 0xdc;
		}
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xdd"), GAnim10_2FileHuman, 1629));
		return 0xdd;
	}

	// The height-difference retry stamp. It is TICKED here whatever the arms below decide, which is
	// why it is not folded into the test that reads it.
	const bool bRetryExpired = NpcKernelAnim10_2Shared::Anim10_2TickHeightDiffTimer(*this, Now);

	if (NpcKernelAnim10_2Shared::Anim10_2HasRangedWeapon(*this)
		&& (Conds.Has(EElysiumNpcCond::TooFarForMelee) || Conds.Has(EElysiumNpcCond::InterruptTime)
			|| Conds.Has(EElysiumNpcCond::EnemyUnreachable) || bRetryExpired))
	{
		Slot601(Enemy);
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe9"), GAnim10_2FileHuman, 1663));
		return 0xe9;
	}
	if (Conds.Has(EElysiumNpcCond::EnemyUnreachable))
	{
		Slot601(Enemy);
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0x17"), GAnim10_2FileHuman, 1671));
		return 0x17;
	}
	if (!Conds.Has(EElysiumNpcCond::TooFarForMelee) && !Conds.Has(EElysiumNpcCond::TooFarToAttack))
	{
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 199"), GAnim10_2FileHuman, 1729));
		return 199;
	}
	if (Enemy != nullptr)
	{
		// `10386254`: the enemy's `WorldSpaceCenter` (slot 192) is read into a stack vector and
		// DISCARDED, then `0x102a11d0` decides.
		(void)ElysiumCameraShots::SurroundingBounds(*Enemy).GetCenter();   // slot 192
		if (ScheduleMeleeReachGate())
		{
			if (NpcKernelAnim10_2Shared::Anim10_2HasRangedWeapon(*this))
			{
				RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe0"), GAnim10_2FileHuman, 1688));
				return 0xe0;
			}
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe1"), GAnim10_2FileHuman, 1692));
			return 0xe1;
		}
	}
	FScheduleHintSearchRequest CoverRequest;
	CoverRequest.bRequest2 = true;
	CoverRequest.bRequest4 = true;   // `thunk_FUN_102b7690(this, 0, 1, 0, 1)`
	const int32 CoverOffer = SelectCoverOrKickSchedule(CoverRequest);
	if (CoverOffer != 0)
	{
		return CoverOffer;
	}
	// `102862f6 TEST AH,0x41; JNP` — the near pair needs `distance < range` STRICTLY (the checklist's
	// walk said "at or below") and the retry stamp NOT expired.
	if (Distance < Range && !bRetryExpired)
	{
		if (NpcKernelAnim10_2Shared::Anim10_2HasRangedWeapon(*this))
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd1"), GAnim10_2FileHuman, 1719));
			return 0xd1;
		}
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd2"), GAnim10_2FileHuman, 1723));
		return 0xd2;
	}
	if (NpcKernelAnim10_2Shared::Anim10_2HasRangedWeapon(*this))
	{
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xca"), GAnim10_2FileHuman, 1708));
		return 0xca;
	}
	RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xcb"), GAnim10_2FileHuman, 1712));
	return 0xcb;
}

// --- Moved from `ElysiumNpcBosses.cpp` (story 5 step 4) ---

// -------------------------------------------------------------------------------------------------
// Slot 601's `CNPC_VAndreiBlood`-line body — `0x10385cf0`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpcHuman::FUN_10385cf0()
{
	// `0x10385cf0`, in retail's order and with the argument ignored, as retail ignores it:
	//     (*DAT_10924edc)->vfunc1();                          // the global melee-left event, FIRST
	//     m_bInMelee = 0;                                     // +0x6078
	//     if (HasUsableRangedWeapon())                        // slot 308 (+0x4d0)
	//         m_flMeleeCanEnterTimer = curtime + RandomFloat(5.0, 10.0);   // +0x6070
	//     ReleaseMeleeSlot(m_pAttackCoordinator, this);       // 0x1025ddd0, NO null guard
	//
	// Two DIFFERENCES from the Troika line's `0x102b5880`, both recovered and both ported: the
	// global event fires here and not there, and the coordinator forward is unguarded here while the
	// Troika body tests `m_pAttackCoordinator != 0` first. The unguarded forward is retail's, and on
	// the Troika line the same call site is guarded — that asymmetry is the fact, not a slip.
	//
	// The `RandomFloat(5.0, 10.0)` draw is NOT taken here: this substrate's melee timer is owned by
	// the Troika-line body 29d will land, the event and the release are seams, and a draw taken on
	// a path whose consumer does not exist would walk the schedule stream off the map. The arm that
	// WOULD draw is reproduced as the `bRangedArm` record below so the gate is measurable.
	// Slot 308 `HasUsableRangedWeapon` (`0x10336d70`) is still a generated stub answering false, so
	// the timer arm is not reached today; it is wired, not inlined, so the day the slot lands the
	// arm opens without a change here.
	++MeleeEventFires;
	bInMelee = false;
	if (HasUsableRangedWeapon())
	{
		// `m_flMeleeCanEnterTimer = curtime + RandomFloat(5.0, 10.0)` (`0x40a00000`, `0x41200000`).
		MeleeCanEnterTimer = (World != nullptr ? World->NowSeconds() : 0.0) + 5.0;
	}
	++MeleeCoordinatorReleases;
}

// --- Moved from `ElysiumNpcCombat10_2.cpp` (story 5 step 4) ---

// --- `CNPC_VHuman::SelectScheduleRangedCombat` `0x10386560`, 802 bytes ---------------------------

int32 FElysiumNpcHuman::HumanSelectScheduleRangedCombat(int32 Arg)
{
	const FElysiumNpcConditions& Conds = Cognition.Conditions;

	// `10386566`: arm 1 — `m_bInMelee`.
	if (bInMelee)
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xe3"), GHumanFile, 0x6cf));
		return 0xe3;
	}
	// `10386588`: arm 2 — the cover hint. An EMPTY `m_pShootAtHint` (`+0x6444`) is filled from slot
	// 609 (`vt+0x984`, argument 0); when it is STILL empty the whole arm is SKIPPED.
	bool bHasCoverHint = ScheduleHost.ShootAtHintNode != 0;
	if (!bHasCoverHint)
	{
		// Slot 609's port body answers NULL because hints are bare indices here; the INDEX it found
		// is `FindShootAtHintNode`'s, so the virtual is dispatched for its species gate and the store
		// reads the index. The search itself is family Hints' seam over an absent hint list and
		// answers nothing, so running it twice is observationally identical to running it once.
		//
		// Retail's `+0x6444` is a POINTER and `!= 0` means "found"; the port's word is an INDEX and
		// family Hints' miss is `INDEX_NONE`. The miss is therefore folded onto `0`, which is the
		// value this word's other readers (`NPCThink`, `NPCInit`, `OnRestore`) already treat as
		// "no hint" — otherwise `-1` would read as a resolved hint and fire the arm.
		Slot609(false);
		const int32 FoundHint = FindShootAtHintNode(false);
		ScheduleHost.ShootAtHintNode = FoundHint > 0 ? FoundHint : 0;
		bHasCoverHint = ScheduleHost.ShootAtHintNode != 0;
	}
	if (bHasCoverHint && !Conds.Has(EElysiumNpcCond::WaitingAttackTime))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xec"), GHumanFile, 0x6e4));
		return 0xec;
	}
	// `103865d7`: arm 3 — `COND 0x8` AND slot 307 AND slot 599 on the enemy.
	if (Conds.Has(EElysiumNpcCond::TooCloseForRanged) && NpcKernelCombat10_2Shared::HasUsableMeleeWeaponPort(*this)
		&& NpcKernelCombat10_2Shared::EnemySlot599(*this))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xe3"), GHumanFile, 0x6e9));
		return 0xe3;
	}
	// `1038661d`: arm 4 — `COND 0x3c`.
	if (Conds.Has(EElysiumNpcCond::WeaponThroughWall))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xb8"), GHumanFile, 0x6ee));
		return 0xb8;
	}
	// `1038663d`: the three helpers, first non-zero wins, IN THIS ORDER.
	if (const int32 PrePass = RangedWeaponPrePass(); PrePass != 0)
	{
		return PrePass;
	}
	const int32 Door = SelectDoorObstructionSchedule();   // 0x102b7370
	if (Door != ElysiumScheduleId::None)
	{
		return Door;
	}
	if (const int32 Reaction = SelectCombatReactionSchedule(); Reaction != 0)
	{
		return Reaction;
	}
	// `10386675`: the SPLIT. The discipline test is on the ENEMY's `+0x9c` — its combat-character
	// self-downcast, which is the entity itself for a combat character and null otherwise (family
	// Senses10's standing fact one).
	if (!Conds.Has(EElysiumNpcCond::TooCloseToAttack)
		&& !Conds.Has(EElysiumNpcCond::TooCloseForRanged))
	{
		FElysiumEntity* const Enemy = NpcKernelCombat10_2Shared::RangedEnemy(*this);
		const FElysiumEntity* const EnemyCombat =
			Enemy != nullptr ? Enemy->AsCombatCharacter() : nullptr;
		if (!RangedDisciplineGate(EnemyCombat))
		{
			if (const int32 Slot606Answer = Slot606(Arg); Slot606Answer != 0)
			{
				return Slot606Answer;
			}
			if (Conds.Has(EElysiumNpcCond::ExtendedBlockedByFriend))
			{
				RecordScheduleEvent(FString::Printf(
					TEXT("SelectScheduleRangedCombat %s:%d -> 0xbd"), GHumanFile, 0x736));
				return 0xbd;
			}
			if (Conds.Has(EElysiumNpcCond::TooFarToAttack))
			{
				RecordScheduleEvent(FString::Printf(
					TEXT("SelectScheduleRangedCombat %s:%d -> 0xb1"), GHumanFile, 0x73b));
				return 0xb1;
			}
			return 0;
		}
	}
	// `103866f2`: the dodge/spacing branch, identical in shape to the Troika base's.
	if (!Conds.Has(EElysiumNpcCond::WaitingAttackTime)
		&& !Conds.Has(EElysiumNpcCond::WeaponBlockedByFriend))
	{
		if (ShouldDodgeRangedAttack())
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("SelectScheduleRangedCombat %s:%d -> 0xef"), GHumanFile, 0x70f));
			return 0xef;
		}
		if (NpcKernelCombat10_2Shared::HasUsableMeleeWeaponPort(*this) && NpcKernelCombat10_2Shared::RangedRoll() < NpcKernelCombat10_2Shared::GMeleeSwitchRollThreshold
			&& NpcKernelCombat10_2Shared::EnemySlot599(*this))
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("SelectScheduleRangedCombat %s:%d -> 0xe8"), GHumanFile, 0x713));
			return 0xe8;
		}
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xf0"), GHumanFile, 0x717));
		return 0xf0;
	}
	if (ShouldDodgeRangedAttack())
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xb8"), GHumanFile, 0x720));
		return 0xb8;
	}
	if (NpcKernelCombat10_2Shared::HasUsableMeleeWeaponPort(*this) && NpcKernelCombat10_2Shared::EnemySlot599(*this))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xe8"), GHumanFile, 0x724));
		return 0xe8;
	}
	RecordScheduleEvent(FString::Printf(
		TEXT("SelectScheduleRangedCombat %s:%d -> 0xb9"), GHumanFile, 0x728));
	return 0xb9;
}

// --- Moved from `ElysiumNpcLifecycle19.cpp` (story 5 step 4) ---

void FElysiumNpcHuman::HideActiveWeaponIfAny()
{
	// `1038714a CALL GetActiveWeapon` / `TEST EAX,EAX / JZ` / `MOV EDX,[EAX]; JMP [EDX+0x108]` —
	// slot 66 `Hide` on the ACTIVE WEAPON, not on this NPC. A null weapon returns with nothing
	// written. `FElysiumWeapon::Hide` is the same body family State19 already dispatches from
	// `CopHumanCombatantOnStateChange`, so it is called rather than counted.
	FElysiumItem* const Active = Inventory.Active(*this);
	FElysiumWeapon* const Weapon = Active != nullptr ? Active->AsWeapon() : nullptr;
	if (Weapon == nullptr)
	{
		return;                                                          // 1038714f JZ
	}
	++HideActiveWeaponCalls;
	Weapon->Hide(this);                                                  // 1038715f JMP [+0x108]
}
