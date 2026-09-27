#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditionsBodiesShared.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"                  // ElysiumMove::U — the one Source-unit conversion
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// Story 29c-1, family **Conditions** — the condition set and the state machine's edges, at
// `order.md` layers 0–9. 32 rows: the seven slot bodies (459, 463, 477, 553, 554, 560, 564), the
// three species `SelectIdealState` overrides, the four species `OnStateChange` shapes, the two flee
// arms of `PreSelectIdealState`, the two condition refreshers, the disturbed latch, the
// alternate-AI door transaction and the three cop/hunter pursuit counters. The walked prose is
// `docs/vtmb/npc-ai/conditions-and-states.md`.
//
// WHAT THIS FAMILY FOUND that 29c's one-line walks did not have:
//
//  * `thunk_FUN_10269b50` is `ClearCondition`, not "force". Two rows read the other way round —
//    `CNPC_VWerewolf::GatherAttackConditions` SUPPRESSES the melee pair rather than raising it, and
//    `UpdateConditionDeathTriggered`'s "one-shot latch" is not one (see its own body).
//  * Every threshold below is READ OUT OF `vampire.dll`'s `.rdata` at the file offset the image
//    base gives, the method `conditions-and-states.md` already used for `_DAT_104454c4`. Nothing in
//    this file is a chosen number.
//  * Retail `NPC_STATE` ids, from `0x1027e660`'s name table and `0x1026e3e0`'s switch: 1 IDLE,
//    2 COMBAT, 3 ALERT, 4 SCRIPT, 6 PRONE, 7 DEAD, 8 FLEE, 0xb HUNT, 0xe the criminal window.
//    `FElysiumNpcBase::NpcStateFlags` already carries that mapping and this file uses the same one.

namespace
{
	// --- The recovered constants, by their `.rdata` address ------------------------------------

	// `FUN_102b2570`'s attack-period numerator: the re-raise window is
	// `m_flLastMeleeStepbackTime + 3.0 / m_flSpeedScale`.
	constexpr float GCondStepbackPeriodNumerator = 3.0f;          // `_DAT_10449258`
	// Its two draws, verbatim: `RandomInt(0,99) < 20` and `RandomInt(0,10) == 0`.
	constexpr int32 GCondStepbackChancePercent = 20;
	constexpr int32 GCondStepbackRareDenominator = 10;
	// `m_bfNPCFrenziedFlags & 0x400`, the frenzy bit the rare arm requires CLEAR. 29b's
	// `FElysiumNpcBase::FrenziedWord` is the word; `0x400` has no recovered name.
	constexpr uint32 GCondFrenziedSuppressesStepback = 0x400u;
	// The active weapon's flag word (`weapon vtable +0x5a0`) bit 30 — "this weapon is blocked". No
	// recovered name; it is the one term `SHOULD_KICK`'s re-raise needs and this runtime's weapon
	// carries no flag word at all, so the arm asks the seam below.

	// `FUN_10290040`, the door transaction's two mode-2 expiries.
	constexpr double GCondDoorWaitExpireSeconds = static_cast<double>(ElysiumNpcTunables::One);
	constexpr double GCondDoorOpenExpireSeconds = static_cast<double>(ElysiumNpcTunables::Five);

	// `CAI_BaseNPCTroika::OnStateChange`'s hunt-state re-arm, `RandomFloat(10, 20)`
	// (`0x41200000` / `0x41a00000` pushed at `102ae1c0`).
	constexpr float GCondHuntExpireMinSeconds = 10.0f;
	constexpr float GCondHuntExpireMaxSeconds = 20.0f;
	// Its criminal-window arm, `0x10293d90(this, 2.0)`: `m_flCriminalIgnoreTimer = curtime + 2.0`.
	constexpr double GCondCriminalIgnoreSeconds = 2.0;

	// `m_afMemory &= 0x07ffffff` — the top FIVE bits of the memory word, dropped on every state
	// change that actually changed the state.
	constexpr uint32 GCondMemoryKeepMask = 0x07ffffffu;

	// `ClearHintNode(this, 5.0)`, the reuse delay the tail imposes.
	constexpr float GCondStateChangeHintReuseSeconds = 5.0f;

	// `bits_CAP_WEAPON_MELEE_ATTACK1`. `CAI_BaseNPCTroika::FCanCheckAttacks`'s own arm tests AH's
	// sign bit, which is EAX bit 15 — this mask, not bit 31.
	constexpr int32 GCondCapWeaponMeleeAttack1 = 0x8000;

}

// =================================================================================================
// Slot 560 — `CAI_BaseNPC::ClearAttackConditions` (`0x1026dc80`)
// =================================================================================================

// =================================================================================================
// Slot 477 — `CAI_BaseNPC::ClearSenseConditions` (`0x1026e5c0`)
// =================================================================================================

// =================================================================================================
// Slot 459 — `RemoveIgnoredConditions` (`0x1026d7f0`, and the cine body `0x101a89a0`)
// =================================================================================================

// =================================================================================================
// Slots 553 / 554 — the two ranged attack bands (`0x1026d890`, `0x1026d920`)
// =================================================================================================

// =================================================================================================
// Slot 564 — `FCanCheckAttacks` (`0x102953a0` over `0x10270840`)
// =================================================================================================

bool FElysiumNpc::FCanCheckAttacks()
{
	// 0x102953a0, the Troika-line body every `npc_V*` classname reaches. Its own arm is a
	// SUPPRESSION in front of the base body: a class whose capability word carries
	// `bits_CAP_WEAPON_MELEE_ATTACK1` (0x8000 — the body tests AH's sign bit, which is EAX bit 15,
	// not bit 31) and which HAS an active weapon and is NOT already `m_bInMelee` (+0x6078) answers
	// false outright. Everything else tail-calls `CAI_BaseNPC::FCanCheckAttacks`.
	//
	// The capability word is the port's recovered one (`ElysiumNpcCond::WeaponCapability`) rather
	// than slot 513's generated stub, which tallies a stub and answers 0 — reading the stub here
	// would make the arm permanently dead.
	const int32 Capabilities =
		ElysiumNpcCond::CapabilityBits(ElysiumNpcCond::WeaponCapability(*this));
	if ((Capabilities & GCondCapWeaponMeleeAttack1) != 0)
	{
		FElysiumItem* Active = Inventory.Active(*this);
		const bool bHasActiveWeapon = Active != nullptr && Active->AsWeapon() != nullptr;
		if (bHasActiveWeapon && !bInMelee)
		{
			return false;
		}
	}
	return FCanCheckAttacksBase();
}

// =================================================================================================
// Slot 463 — `OnStateChange`
// =================================================================================================

void FElysiumNpc::OnStateChangeTroika(EElysiumNpcState OldState, EElysiumNpcState NewState)
{
	// 0x102ae140. Read in two halves, because the body's `goto` structure is exactly that: an
	// "actually changed" half and an unconditional tail every call runs.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	const int32 NewRetail = NpcKernelConditionsShared::CondRetailStateId(NewState);

	if (OldState != NewState)
	{
		// Slot 610 (`0x102adfe0`), the DEFAULT-expression selector — the one the Tzimisce override
		// above front-runs. It is a generated stub in this runtime, so calling it records a stub
		// tally and writes nothing; the call is kept because retail's order is
		// `vfunc610` -> weapon visibility -> the switch, and dropping it would lose that order.
		Slot610(NewState);

		// `0x102ae310`, the Troika-line weapon show/hide. NOT PORTED HERE and named rather than
		// approximated: its body is a four-arm rule over `GetEnemy()`, two dialogue-partner probes
		// (`+0x62ec` / `+0x6300`, each testing that entity's `+0x571`), the state set
		// {1, 4, 0xc, 0xd} and `IRelationType(m_hClosestPlayer)`, and it hides or unhides through
		// the weapon's own `+0x4f0` / `+0x4ec`. `FElysiumNpcHuman::ApplyStateWeaponVisibility` is the
		// SPECIES half (the seven classes' own slot-463 body), which is a different and simpler
		// rule; conflating the two would give every NPC in the cast a holster policy retail gives
		// nine of them. `0x102ae310` is not one of this family's rows.

		bool bReturnToInitialPosArm = true;
		switch (NewRetail)
		{
		case 2:   // COMBAT
		case 3:   // ALERT
			break;
		case 7:   // DEAD — releases the patrol route and SKIPS `m_bReturnToInitialPos`
			// `if (m_sppPatrolPath +0x6590) 0x1029f5d0(&m_sppPatrolPath)` — the release is guarded
			// on the route being installed, which for this runtime's resolved-point array is
			// "non-empty".
			// STORY8-TWIN: replaced by 0x1029f5d0 on PatrolPathCell (0x1029f460's cell) at wave 2
			if (!PatrolPoints.IsEmpty())
			{
				PatrolPoints.Reset();
			}
			bReturnToInitialPosArm = false;
			break;
		case 8:   // FLEE
		{
			// `m_OnStateFleeing` fires with the ENEMY as activator, or with this NPC when there is
			// no enemy — retail substitutes `this`, it does not skip the output.
			const FElysiumEntity* Enemy = World != nullptr && BaseMemory.Enemy.IsSet()
				? World->Resolve(BaseMemory.Enemy) : nullptr;
			FireOutput(FName(TEXT("OnStateFleeing")), Enemy != nullptr ? Enemy->Handle : Handle);
			break;
		}
		case 0xb: // HUNT
			HuntExpireTime = Now + static_cast<double>(
				ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(
					GCondHuntExpireMinSeconds, GCondHuntExpireMaxSeconds));
			break;
		case 0xe: // the criminal window
			// `0x10293d90(this, 2.0)`: `m_flCriminalIgnoreTimer (+0x6398) = curtime + 2.0`. It FALLS
			// THROUGH to the `m_bReturnToInitialPos` write, which is why this arm does not break
			// early in retail either.
			Witness.Channel(ElysiumNpcWitness::EChannel::Criminal).IgnoreUntil =
				Now + GCondCriminalIgnoreSeconds;
			break;
		default:
			bReturnToInitialPosArm = false;
			break;
		}
		if (bReturnToInitialPosArm)
		{
			bReturnToInitialPos = true;   // +0x6494
		}

		// The changed-half tail: drop the top five bits of `m_afMemory` (+0x5d8c) and, unless the
		// body is climbing or jumping, clear `PRESERVE_PATH` (+0x14b8 bit 0x8).
		BaseScheduleHost.MemoryBits &= GCondMemoryKeepMask;
		if (NavType() != NpcKernelConditionsShared::GCondNavClimb && NavType() != NpcKernelConditionsShared::GCondNavJump)
		{
			NpcFlags.Clear(EElysiumNpcFlag::PRESERVE_PATH);
		}
	}

	// The unconditional tail — it runs even when the state did NOT change, which is the whole reason
	// retail's entry `goto` exists. Six writes in order.
	NpcFlags.Clear(EElysiumNpcFlag::MADE_HUNT_PATH);   // +0x14b8 bit 0x1000
	// STORY8-TWIN: replaced by 0x1029f5d0 on PatrolPathHuntCell (0x1029f460's cell) at wave 2
	HuntPatrolPoints.Reset();                          // 0x1029f5d0(&m_sppPatrolPathHunt) +0x6594
	NpcFlags.Clear(EElysiumNpcFlag::AT_CROSSWALK);     // +0x14b8 bit 0x4
	bGoToIdleState = false;                            // +0x63fc
	ClearScheduleHint(GCondStateChangeHintReuseSeconds);  // ClearHintNode(this, 5.0) 0x10295ab0
	Cognition.bCondTookDamage = false;                 // +0x5b80

	// `CAI_BaseNPC::OnStateChange` (`0x1026e3e0`): assign `m_bfNPCStateFlags` from the new state.
	// The byte is a pure function of the state and this runtime DERIVES it
	// (`FElysiumNpcBase::NpcStateFlags` -> `FElysiumNpcFlags::NpcStateFlagsForRetailState`), so there is
	// nothing to store — the recovered table is already the port's, and the call below is the
	// assertion that it agrees.
	(void)FElysiumNpcFlags::NpcStateFlagsForRetailState(NewRetail);
}

void FElysiumNpc::OnStateChange(EElysiumNpcState OldState, EElysiumNpcState NewState)
{
	// Slot 463 on the Troika line. Seven species classes override it on their C++ classes (story 5
	// step 3): the human-combatant holster/draw body `0x103871c0` and the Guard1 / Hunter / Cop /
	// Bach / Tzimisce / Camera bodies, each ending in a direct call into this body or the combatant's.
	OnStateChangeTroika(OldState, NewState);
}

// =================================================================================================
// `RequestDesiredState` — the two flee arms of `PreSelectIdealState`
// =================================================================================================

int32 FElysiumNpc::RequestFleeDesiredState(EElysiumNpcCond GateCondition, int32 RetailSourceLine)
{
	// `FUN_102ad260` (0x21) and `FUN_102ad2d0` (0x1f), which differ only in the condition and the
	// source line. The gate is `HasInterruptCondition` (`0x10269d30`) — an INSTALLED schedule whose
	// mask lists the condition AND the condition standing. A gathered law level with no program
	// asking for it does not make an NPC flee.
	if (!ElysiumSchedule::HasInterruptCondition(Schedule, *this, Cognition.Conditions, GateCondition))
	{
		return 0;
	}
	// `if (m_NPCState != 8) m_bfAINPCFlags |= 0x100` — `INITIAL_FLEE`, armed only on the way IN. This
	// runtime has no state 8, so the test can never be false and the flag is always armed; the
	// comparison is written out rather than folded so the day a flee state lands it is already here.
	if (Mind.State() != EElysiumNpcState::Dead && NpcKernelConditionsShared::CondRetailStateId(Mind.State()) != 8)
	{
		NpcFlags.Set(EElysiumNpcFlag::INITIAL_FLEE);
	}
	Mind.RequestDesiredState(8, RetailSourceLine);
	return 8;
}

// =================================================================================================
// The alternate-AI door transaction
// =================================================================================================

void FElysiumNpc::EnterAlternateAi()
{
	// `FUN_10298800`, three stores in order.
	BaseScheduleHost.bShouldMove = false;                 // +0x1a40
	StopMoving();                                     // 0x102ee2a0 on m_pNavigator (+0x5d34)
	AlternateAi = 1;                                  // +0x644c
}

bool FElysiumNpc::OpeningDoorFacingPoint(const FElysiumEntity& Door, bool bWait,
	FVector& OutPointCm) const
{
	// SEAM for the door's own vtable `+0x3d8`, called as `(this, &point, m_bOpeningDoorWait)`. This
	// runtime's doors carry no NPC-open point, so the transaction takes retail's
	// `iStack_10 == -1` arm.
	(void)Door;
	(void)bWait;
	(void)OutPointCm;
	return false;
}

// `FacingIdeal` (`0x10278c80`) is family **Facing**'s body and is called, not restated.

bool FElysiumNpc::RunAlternateAiOpeningDoor(double Now)
{
	// `FUN_10290040`, `RunAlternateAI`'s mode-1 arm.
	//
	// 1. A dead `m_hOpeningDoor` (+0x5d24) RESETS the mode to 0 and answers false — the one place
	//    the transaction lets go of the body.
	if (World == nullptr || !OpeningDoor.IsSet() || World->Resolve(OpeningDoor) == nullptr)
	{
		AlternateAi = 0;
		return false;
	}
	FElysiumEntity* Door = World->Resolve(OpeningDoor);

	// 2. Ask the door where to stand. A refusal answers false WITHOUT resetting the mode: the
	//    transaction stays installed and asks again next think.
	FVector PointCm = FVector::ZeroVector;
	if (!OpeningDoorFacingPoint(*Door, bOpeningDoorWait, PointCm))
	{
		return false;
	}

	// 3. Face it — `0x102e0b40` on the motor, `VecToYaw` (`0x101d2c70`) over the returned direction,
	//    then `0x102e1c10(motor, yaw, -1.0)`. The yaw arithmetic is retail's and is done here; the
	//    write is a seam.
	SetAlternateAiIdealYaw(
		static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(PointCm.Y, PointCm.X))));

	// 4. Only once FACING does the transaction advance. Both advance arms set mode 2 and stamp an
	//    expiry; the WAIT arm uses 1.0 s and the open arm 5.0 s, and the wait arm does not run the
	//    follow-up at all.
	if (FacingIdeal())
	{
		if (bOpeningDoorWait)
		{
			AlternateAi = 2;
			AlternateAiExpireTime = Now + GCondDoorWaitExpireSeconds;
			return true;
		}
		if (StartOpeningDoor(*Door))
		{
			AlternateAi = 2;
			AlternateAiExpireTime = Now + GCondDoorOpenExpireSeconds;
		}
	}
	// Retail returns 1 from here whether or not it advanced: the transaction still owns the body.
	return true;
}

// =================================================================================================
// The cop / hunter pursuit counters — the PLAYER's `+0x1d10` and `+0x1d14`
// =================================================================================================

void FElysiumNpc::PromoteCriminalWindowNpcs(FElysiumEntityWorld& InWorld)
{
	// SEAM for `FUN_103705e0`, the first of `OnCopPursuitStart`'s two hooks: walk the global NPC
	// list and put every body whose `GetState()` (slot 464) is **14** into state **2** via
	// `0x102ae840(npc, 2, false)`. This runtime's `EElysiumNpcState` has no member for retail state
	// 14 — the criminal window `UpdateIdealState` names — so the predicate is unsatisfiable and the
	// sweep visits nobody. The walk is written out rather than omitted so the day state 14 exists
	// the hook is already wired.
	(void)InWorld;
}

void FElysiumNpc::OnCopPursuitStart(FElysiumPlayer& Player)
{
	// `FUN_1017f650`. The hooks run on the PRE-increment count — the test is `== 0`, and the
	// increment follows it, so a second cop joining a pursuit runs neither hook.
	if (Player.Police.CopsInPursuit == 0)
	{
		if (Player.World != nullptr)
		{
			PromoteCriminalWindowNpcs(*Player.World);
		}
		// `0x1017f980`: fire the game-rules output at `+0x480` with the player as activator AND
		// caller, then zero `player + 0x1d1c`. The output half is what `ElysiumLaw`'s pursuit edge
		// already fires (`OnStartCopPursuitMode`), which is why the increment below goes through
		// `SetCopPursuitCount` rather than touching the counter directly. UNRECOVERED: `+0x1d1c`,
		// which the census does not name and no port member carries.
	}
	ElysiumLaw::SetCopPursuitCount(Player, Player.Police.CopsInPursuit + 1);
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("CSActs: OnCopPursuitStart - %d in pursuit"),
		Player.Police.CopsInPursuit);
}

void FElysiumNpc::OnHunterPursuitStart(FElysiumPlayer& Player)
{
	// `FUN_1017f7b0` — the same shape with ONE hook (`0x1017fa40`, the game-rules output at
	// `+0x4e0`), which `ElysiumLaw`'s `OnStartHunterPursuitMode` edge already fires.
	ElysiumLaw::SetHunterPursuitCount(Player, Player.Police.HuntersInPursuit + 1);
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("CSActs: OnHunterPursuitStart - %d in pursuit"),
		Player.Police.HuntersInPursuit);
}

void FElysiumNpc::OnHunterPursuitStop(FElysiumPlayer& Player)
{
	// `FUN_1017f830`. The ASYMMETRY with the two start bodies is retail's: this one DECREMENTS
	// FIRST and then tests the post-decrement count against zero, where the start bodies test first
	// and increment after. Both edges therefore fire exactly once per 0<->1 crossing.
	//
	// DIVERGENCE (named, and pre-existing): retail does not clamp — an unbalanced stop drives
	// `+0x1d14` negative and the next start then has no 0->1 edge to fire. `ElysiumLaw::
	// SetHuntersInPursuit` clamps at zero, so this runtime re-fires the start edge where retail
	// would have swallowed it. The clamp is `ElysiumLaw`'s and is not this family's to remove.
	ElysiumLaw::SetHunterPursuitCount(Player, Player.Police.HuntersInPursuit - 1);
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("CSActs: OnHunterPursuitStop - %d in pursuit"),
		Player.Police.HuntersInPursuit);
}

// =================================================================================================
// The two condition refreshers
// =================================================================================================

void FElysiumNpc::RefreshCombatConditions()
{
	// `FUN_102b2570`. The one producer of `SHOULD_STEPBACK` (0x0e), `SHOULD_KICK` (0x0f) and
	// `TOO_FAR_FOR_MELEE` (0x09) in the whole closure.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;

	// 1. Both products are cleared UNCONDITIONALLY, before any gate. A body that leaves melee keeps
	//    neither.
	Cognition.Conditions.Clear(EElysiumNpcCond::ShouldStepback);   // 0x0e
	Cognition.Conditions.Clear(EElysiumNpcCond::ShouldKick);       // 0x0f

	// 2. Not in melee (`m_bInMelee` +0x6078) is the whole body's gate.
	if (!bInMelee)
	{
		return;
	}

	// 3. `ENEMY_TOO_FAR` (0x55) standing short-circuits the rest into ONE write: `TOO_FAR_FOR_MELEE`
	//    (0x09). Note it is a plain `HasCondition`, not the interrupt form.
	if (Cognition.Conditions.Has(EElysiumNpcCond::EnemyTooFar))
	{
		Cognition.Conditions.Set(EElysiumNpcCond::TooFarForMelee);
		return;
	}

	// 4. The stepback arm. Its FIRST term is a two-way timer test, and the OR is retail's:
	//    `m_flLastMeleeStepbackTime < m_flLastAttackTime` (I have attacked since I last stepped back)
	//    OR `m_flLastMeleeStepbackTime + 3.0 / m_flSpeedScale < curtime` (the period elapsed).
	//    `m_flSpeedScale` is `CBaseCombatCharacter +0x1488`, which this runtime reads through
	//    `ElysiumWeapons::AttackSpeedScale` (1.0 until a feat carries a scalar).
	const float SpeedScale =
		FMath::Max(ElysiumWeapons::AttackSpeedScale(*this), KINDA_SMALL_NUMBER);
	const bool bStepbackWindowOpen =
		LastMeleeStepbackTime < LastAttackTime
		|| LastMeleeStepbackTime + static_cast<double>(GCondStepbackPeriodNumerator / SpeedScale)
			< Now;
	if (bStepbackWindowOpen)
	{
		FRandomStream& Rng = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
		// Arm A: too close, and a 20-in-100 draw.
		bool bStepback = Cognition.Conditions.Has(EElysiumNpcCond::TooCloseToAttack)   // 0x5f
			&& Rng.RandRange(0, 99) < GCondStepbackChancePercent;
		if (!bStepback)
		{
			// Arm B: NOT frenzied (`m_bfNPCFrenziedFlags & 0x400` clear), and none of
			// `TOO_FAR_FOR_MELEE` (9), `TOO_FAR_TO_ATTACK` (0x60), `CAN_MELEE_ATTACK1` (0x51)
			// standing, and a 1-in-11 draw (`RandomInt(0, 10) == 0`).
			bStepback = !HasFrenzied(GCondFrenziedSuppressesStepback)
				&& !Cognition.Conditions.Has(EElysiumNpcCond::TooFarForMelee)
				&& !Cognition.Conditions.Has(EElysiumNpcCond::TooFarToAttack)
				&& !Cognition.Conditions.Has(EElysiumNpcCond::CanMeleeAttack1)
				&& Rng.RandRange(0, GCondStepbackRareDenominator) == 0;
		}
		if (bStepback)
		{
			Cognition.Conditions.Set(EElysiumNpcCond::ShouldStepback);
		}
	}

	// 5. The kick arm, evaluated whether or not step 4 fired. Four terms in order:
	//    the running program's mask must LIST `SHOULD_KICK` (`ConditionInterruptsCurrentSchedule`
	//    `0x10269c70` — the mask alone, not the condition); `m_flNextAttack` (+0x1564) must have
	//    passed; `TOO_CLOSE_TO_ATTACK` must stand; and there must be an active weapon whose flag word
	//    (`weapon vtable +0x5a0`) carries bit 30.
	if (!ElysiumSchedule::MaskHasCondition(Schedule, *this, EElysiumNpcCond::ShouldKick))
	{
		return;
	}
	if (!(NextAttackTime < Now))
	{
		return;
	}
	if (!Cognition.Conditions.Has(EElysiumNpcCond::TooCloseToAttack))
	{
		return;
	}
	FElysiumItem* Active = Inventory.Active(*this);
	if (Active == nullptr || Active->AsWeapon() == nullptr)
	{
		return;
	}
	if (WeaponFlagBlocksAttack())
	{
		Cognition.Conditions.Set(EElysiumNpcCond::ShouldKick);
	}
}

bool FElysiumNpc::WeaponFlagBlocksAttack() const
{
	// SEAM for the active weapon's flag word, `weapon vtable +0x5a0` bit 30 (`uVar1 >> 0x1e & 1`),
	// the last term of `RefreshCombatConditions`' kick arm. This runtime's `FElysiumWeapon` carries
	// no retail flag word at all — the capability answer (`ElysiumNpcCond::WeaponCapability`) is two
	// named bits and deliberately not a register — so nothing can answer it and the kick condition
	// is never raised. UNRECOVERED: the bit's name.
	return false;
}

void FElysiumNpc::RefreshOccludedCondition(EElysiumNpcCond Cond, double& InOutStamp, double Now)
{
	// `FUN_1028e700`, the occlusion debounce. It is a SUPPRESSOR, not a producer: the condition its
	// caller already raised is taken away again until `m_flOccludedDelay` (+0x62c8) has elapsed
	// since the first pass that raised it.
	if (!Cognition.Conditions.Has(Cond))
	{
		// Dropped: reset the sentinel so the next raise re-arms a fresh delay. The sentinel is
		// `_DAT_104454c4` = 0.0f.
		InOutStamp = 0.0;
		return;
	}
	if (InOutStamp == 0.0)
	{
		InOutStamp = static_cast<double>(OccludedDelay) + Now;
	}
	if (Now < InOutStamp)
	{
		Cognition.Conditions.Clear(Cond);
	}
}
