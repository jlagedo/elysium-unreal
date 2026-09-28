// Story 0019/8 (29e under the strict verdict), family **Select19** -- `CAI_BaseNPCTroika`'s bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Declarations are in `ElysiumNpcSelect19.inl` (included inside `class FElysiumNpc`) or generated
// in `ElysiumNpcSlots.inl` for a slot body.
//
// Owns (Select19's `rule` rows): 0x102ae920 CAI_BaseNPCTroika::PreSelectSchedule, 0x102af660
// CAI_BaseNPCTroika::SelectSchedule.
//
// Every arm carries the address of the listing instruction it came from (`vtmb_asm`). Condition and
// schedule ids are retail's registered numbers: conditions through `EElysiumNpcCond` where the port
// names one (a condition word is class-local, so an unnamed one is its number); schedules as named
// constants below, each beside its `cai_basenpctroika` registration (`space.json`). The `+0x1b30` /
// `+0x1b34` file/line pair stays ABSENT (`SelectTrace` records it); the selector id `+0x1b2c` is the
// carried word `SelectScheduleSelector`.

#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumDisciplines.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcKernelTunables.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumScheduleNumbers.h"

namespace NpcSelect19
{
	// `s_E__Vampire_main_dlls_AI_BaseNPCT_105da024`.
	const TCHAR* const GTroikaFile = TEXT("AI_BaseNPCTroika.cpp");

	// Cells this family reads that `ElysiumNpcKernelTunables.h` does not carry, read out of the
	// pinned image (the docs cite each).
	constexpr double GInvestigateSoundDelay = 2.0;      // `_DAT_10452dc4` f32
	constexpr double GSoundSourceRetryDelay = 20.0;     // `_DAT_1044eb0c` f32
	constexpr float GRangeStepBackDistance = 800.0f;    // `_DAT_10457ac4` f32, `0x102aff07`
	constexpr float GSoundSourceLookAhead = 128.0f;     // `_DAT_1046dcd0` f32, `0x102b8ea4`
	constexpr float GFleeSoundMin = 10.0f;              // `PUSH 0x41200000`, `0x102b03c5`
	constexpr float GFleeSoundMax = 20.0f;              // `PUSH 0x41a00000`, `0x102b03b4`

	// `IRelationType` answers (`Disposition_t`).
	constexpr int32 GDispositionHate = 1;
	constexpr int32 GDispositionFear = 2;

	// Retail NPC states (`m_NPCState`).
	constexpr int32 GStateIdle = 1;
	constexpr int32 GStateCombat = 2;
	constexpr int32 GStateAlert = 3;
	constexpr int32 GStateCrimSuspicion = 0xe;

	// Conditions the port has no enumerator for (`cai_basenpc` registrations).
	constexpr EElysiumNpcCond GCondShouldLoiter = static_cast<EElysiumNpcCond>(0x11);   // COND_SHOULD_LOITER
	constexpr EElysiumNpcCond GCondCoverFailure = static_cast<EElysiumNpcCond>(0x39);   // COND_COVER_FAILURE
	constexpr EElysiumNpcCond GCondPlayerOnHead = static_cast<EElysiumNpcCond>(0x3b);   // COND_PLAYER_ON_HEAD

	// Base schedules (`cai_basenpc`).
	constexpr int32 GSchedCombatFace = 0x0b;             // COMBAT_FACE
	constexpr int32 GSchedFearFace = 0x0d;               // FEAR_FACE
	constexpr int32 GSchedTakeCoverFromEnemy = 0x17;     // TAKE_COVER_FROM_ENEMY
	constexpr int32 GSchedRunFromEnemy = 0x1b;           // RUN_FROM_ENEMY
	constexpr int32 GSchedHideAndReload = 0x28;          // HIDE_AND_RELOAD

	// Troika schedules (`cai_basenpctroika`).
	constexpr int32 GSchedIdleStand = ElysiumSched::SCHED_TROIKA_IDLE_STAND;                  // 0x44
	constexpr int32 GSchedIdleReturnToInitial = 0x45;    // SCHED_TROIKA_IDLE_RETURN_TO_INITIAL
	constexpr int32 GSchedTurnToSound = 0x48;            // SCHED_VTROIKA_TURN_TO_SOUND
	constexpr int32 GSchedAlertWait = 0x4b;              // SCHED_TROIKA_ALERT_WAIT
	constexpr int32 GSchedAlertLookAround = ElysiumSched::SCHED_TROIKA_ALERT_LOOK_AROUND_NI;  // 0x4f
	constexpr int32 GSchedInvestigateSoundFlinch = 0x50; // SCHED_TROIKA_INVESTIGATE_SOUND_FLINCH
	constexpr int32 GSchedInvestigateSound = 0x51;       // SCHED_TROIKA_INVESTIGATE_SOUND
	constexpr int32 GSchedInvestigateOtherSound = 0x52;  // SCHED_TROIKA_INVESTIGATE_OTHER_SOUND
	constexpr int32 GSchedTurnToDetectedAttack = 0x56;   // SCHED_TROIKA_ALERT_TURN_TO_DETECTED_ATTACK
	constexpr int32 GSchedInvestigateUnknown = 0x59;     // SCHED_TROIKA_INVESTIGATE_UNKNOWN
	constexpr int32 GSchedInvestigateUnknownQuick = 0x5a;
	constexpr int32 GSchedInvestigateUnknownAttack = 0x5b;
	constexpr int32 GSchedInvestigateUnknownOther = 0x5c;
	constexpr int32 GSchedInvestigateUnknownLost = 0x5d;
	constexpr int32 GSchedInvestigateUnknownOtherRun = 0x5e;
	constexpr int32 GSchedInvestigateUnknownLostRun = 0x5f;
	constexpr int32 GSchedInvestigateUnknownIgnore = 0x60;
	constexpr int32 GSchedRunDialog = 0x6a;              // SCHED_TROIKA_RUN_DIALOG
	constexpr int32 GSchedIdleDisposition = ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION;      // 0x6b
	constexpr int32 GSchedFleeTurnToPlayer = 0x70;       // SCHED_TROIKA_FLEE_AND_COWER_TURN_TO_PLAYER
	constexpr int32 GSchedFleeTurnToPlayerNear = 0x71;
	constexpr int32 GSchedFleeScream = 0x72;             // SCHED_TROIKA_FLEE_AND_COWER_SCREAM
	constexpr int32 GSchedFleeAndCower = 0x73;           // SCHED_TROIKA_FLEE_AND_COWER
	constexpr int32 GSchedFleeNoEnemy = 0x76;            // SCHED_TROIKA_FLEE_AND_COWER_NO_ENEMY
	constexpr int32 GSchedCower = 0x77;                  // SCHED_TROIKA_COWER
	constexpr int32 GSchedPlayerOnHeadDive = 0x79;
	constexpr int32 GSchedPlayerOnHeadDiveForward = 0x7a;
	constexpr int32 GSchedPlayerOnHeadRun = 0x7b;
	constexpr int32 GSchedHuntSetup = 0x7c;
	constexpr int32 GSchedHuntSetupNoEnemy = 0x7d;
	constexpr int32 GSchedHunt = 0x7e;
	constexpr int32 GSchedHuntInvestigateFlinch = 0x7f;
	constexpr int32 GSchedHuntInvestigate = 0x80;
	constexpr int32 GSchedHuntInvestigateUnknown = 0x81;
	constexpr int32 GSchedHuntInvestigateUnknownLost = 0x82;
	constexpr int32 GSchedHuntRunToSaved = 0x84;
	constexpr int32 GSchedHuntFinish = 0x85;
	constexpr int32 GSchedRunToSaved = 0x89;             // SCHED_TROIKA_RUN_TO_SAVED
	constexpr int32 GSchedChaseEnemy = ElysiumSched::SCHED_TROIKA_CHASE_ENEMY;                // 0xb1
	constexpr int32 GSchedBackAwayFromEnemy = 0xb8;
	constexpr int32 GSchedWaitForClearShot = 0xbc;
	constexpr int32 GSchedAttemptToClose = 0xbf;
	constexpr int32 GSchedMeleeAdvance = ElysiumSched::SCHED_TROIKA_MELEE_ADVANCE;            // 0xca
	constexpr int32 GSchedMeleeAttack1 = ElysiumSched::SCHED_TROIKA_MELEE_ATTACK1;            // 0xdc
	constexpr int32 GSchedMeleeAttack2 = 0xdf;
	constexpr int32 GSchedStartCombat = 0xea;
	constexpr int32 GSchedStartCombatSquad = 0xeb;
	constexpr int32 GSchedShootAtHint = ElysiumSched::SCHED_TROIKA_RANGE_ATTACK1_SHOOT_AT_HINT;   // 0xec
	constexpr int32 GSchedRangeAttack1 = 0xed;
	constexpr int32 GSchedRangeAttack2 = 0xee;
	constexpr int32 GSchedStepBackRangeAttack1 = 0xef;
	constexpr int32 GSchedForcedRangeAttack1 = 0xf0;
	constexpr int32 GSchedStartled = 0xf1;
	constexpr int32 GSchedFinishClimb = 0xfc;
	constexpr int32 GSchedFinishJump = 0xfd;
	constexpr int32 GSchedWalkToPlaceSetup = 0xff;       // SCHED_TROIKA_WALK_TO_INTERESTING_PLACE_SETUP
	constexpr int32 GSchedWalkToPlace = 0x100;
	constexpr int32 GSchedWaitAtCrosswalk = 0x102;
	constexpr int32 GSchedLoiter = 0x105;
	constexpr int32 GSchedInteract = 0x106;
	constexpr int32 GSchedCrimSuspApproach = 0x116;      // SCHED_TROIKA_CRIMSUSP_APPROACH_ENEMY
	constexpr int32 GSchedCrimSuspFace = 0x117;
	constexpr int32 GSchedCrimSuspWaitForOcclusion = 0x118;
	constexpr int32 GSchedCrimSuspGoToLkp = 0x119;
	constexpr int32 GSchedCrimSuspWaitLookAround = 0x11a;
	constexpr int32 GSchedCrimSuspWaitAtLkp = 0x11b;
	constexpr int32 GSchedComfort = 0x12f;
	constexpr int32 GSchedCalmed = 0x130;
	constexpr int32 GSchedFollow = 0x131;
	constexpr int32 GSchedMesmerize = 0x14a;             // SCHED_TROIKA_D_MESMERIZE
	constexpr int32 GSchedKnockback = 0x14c;
	constexpr int32 GSchedOnFire = 0x151;

	// `SelectSchedule` case 2's `SelectWeightedSequence` activity (`0x102afccf PUSH 0x49`).
	constexpr int32 GActSmallFlinch = 0x49;

	// `m_afMemory` bits (`+0x5d8c`).
	constexpr uint32 GMemoryFlinchSuppressed = 0x40u;    // `0x102afcc4 TEST byte ptr [+0x5d8c],0x40`
	constexpr uint32 GMemoryInvestigating = 0x8000000u;  // `0x102b8a6e`

	// The weapon word's two masks (`TEST AH,0x60` and `TEST EAX,0x18000`).
	constexpr uint32 GWeaponRangedBits = 0x6000u;
	constexpr uint32 GWeaponMeleeBits = 0x18000u;

	// `m_bfNPCFrenziedFlags` bits.
	constexpr uint32 GFrenziedNoCombatStart = 0x80u;     // `0x102ae9a3`, `0x102aedbc`, `0x102aee01`
	constexpr uint32 GFrenziedNoFlinchInvestigate = 0x10000u;   // `0x102b9255`, `0x102b92ca`

	// The twelve ConVars (`ESelect19ConVar`), shipped defaults then the live values.
	constexpr int32 GConVarDefaults[static_cast<int32>(FElysiumNpc::ESelect19ConVar::Count)] = {
		3,   // debug_player_on_head
		1,   // ming_xiao_charge
		0, 0, 0, 0,   // asianvamp_force_jump_up, changbros_force_{united_attack,teleport,ledge_attack}
		0, 0, 0,      // andrei_force_{player_collision,jump_attack,charge_attack}
		0, 0,         // sheriff_force_teleport, werewolf_force_teleport
		1,   // tzimisce_pounce
	};
	int32 GConVarValues[static_cast<int32>(FElysiumNpc::ESelect19ConVar::Count)] = {
		3, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
	};

	int32 Roll(int32 Max)
	{
		return ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, Max);
	}

	// Slot 167 `GetEnemy() const` (`vtable +0x29c`). `FElysiumNpc` declares slot 168's non-const
	// `GetEnemy()` beside it; a const receiver selects 167, as retail's `+0x29c` call does.
	FElysiumEntity* Slot167Enemy(const FElysiumNpc& Npc)
	{
		return Npc.GetEnemy();
	}

	FElysiumEntity* Resolve(const FElysiumNpc& Npc, const FElysiumEntityHandle& Handle)
	{
		return Npc.World != nullptr && Handle.IsSet() ? Npc.World->Resolve(Handle) : nullptr;
	}
}

// =================================================================================================
// The ConVars and the small shared reads.
// =================================================================================================

int32 FElysiumNpc::Select19ConVarInt(ESelect19ConVar ConVar)
{
	return NpcSelect19::GConVarValues[static_cast<int32>(ConVar)];
}

void FElysiumNpc::SetSelect19ConVar(ESelect19ConVar ConVar, int32 Value)
{
	NpcSelect19::GConVarValues[static_cast<int32>(ConVar)] = Value;
}

void FElysiumNpc::ResetSelect19ConVars()
{
	for (int32 i = 0; i < static_cast<int32>(ESelect19ConVar::Count); ++i)
	{
		NpcSelect19::GConVarValues[i] = NpcSelect19::GConVarDefaults[i];
	}
}

bool FElysiumNpc::Select19ConVarEnabled(ESelect19ConVar ConVar)
{
	// `(**(code**)(*cv + 4))() == 0 && cv[0xb] != 0` — slot 1 is `IsCommand`, false on a ConVar.
	return Select19ConVarInt(ConVar) != 0;
}

bool FElysiumNpc::SelectRunningScheduleIs(int32 RetailId)
{
	// `m_pSchedule (+0x5c38) != 0 && m_pSchedule == 0x102cc1f0(this, RetailId)`: the lookup runs the
	// id through slot 440 first, so a species' translation is honoured.
	if (Schedule.Current == ElysiumScheduleId::None)
	{
		return false;
	}
	const int32 Global = ResolveScheduleId(TranslateScheduleRetail(RetailId));
	return Global != INDEX_NONE && Schedule.Current == Global;
}

uint32 FElysiumNpc::SelectActiveWeaponWord() const
{
	// The weapon `+0x5a0` word (slot 360). No weapon, no word (`0x10385008 XOR EAX,EAX`); else the
	// weapon record's capability. DUPLICATE READ, listed for consolidation: family Motor's
	// `ActiveWeaponCapabilityWord` is a seam answering 0 for the same word, and answering the real
	// word there moves the Combat10 ranged pre-pass (`0x102b86ba`) and its tests — not this lane's.
	if (ActiveWeaponEntity() == nullptr)
	{
		return 0;
	}
	return static_cast<uint32>(ElysiumNpcCond::CapabilityBits(ElysiumNpcCond::WeaponCapability(*this)));
}

FElysiumPlayer* FElysiumNpc::SelectResolvePlayer(const FElysiumEntityHandle& PlayerHandle) const
{
	FElysiumEntity* const Entity = NpcSelect19::Resolve(*this, PlayerHandle);
	FElysiumPlayer* const Player = World != nullptr ? World->FindPlayer() : nullptr;
	return Entity != nullptr && Player != nullptr && static_cast<FElysiumEntity*>(Player) == Entity
		? Player : nullptr;
}

bool FElysiumNpc::SelectPatrolPathObject(int32& OutScheduleRetail) const
{
	// `+0x6590 != 0` and its `+0x4` schedule word (`0x102af6b6` / `0x102af6c7`).
	const FPatrolPathRecord* Path = PatrolPathCell.Path;
	OutScheduleRetail = Path != nullptr ? Path->Schedule : 0;
	return Path != nullptr;
}

// =================================================================================================
// Slot 437 — `CAI_BaseNPCTroika::PreSelectSchedule` `0x102ae920`, 2687 bytes.
// =================================================================================================

int32 FElysiumNpc::PreSelectSchedule()
{
	using namespace NpcSelect19;
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	const FElysiumNpcConditions& Cond = Cognition.Conditions;
	auto HasInterrupt = [this](EElysiumNpcCond C)
	{
		return ElysiumSchedule::HasInterruptCondition(Schedule, *this, Cognition.Conditions, C);
	};

	SelectScheduleSelector = 2;                                    // 0x102ae92a MOV [ESI+0x1b2c],2
	Senses.Memory.InvestigateSound = FElysiumGameSoundEvent();     // 0x102ae934 CALL 0x101b9880 (reset)

	const int32 Forced = ScheduleHost.ForcedSchedule;              // 0x102ae939
	if (Forced != 0)                                               // 0x102ae941 JZ
	{
		ScheduleHost.ForcedSchedule = ElysiumScheduleId::None;     // 0x102ae943
		return Forced;                                             // 0x102ae94f — no file/line stamp
	}

	// The squad arm. `ConnectedSquad()`/`SquadWord()` stand no squad object today (0002/17), so the
	// arm is not reached; it is transcribed whole for the day one stands.
	if (SquadDisconnected < 1                                      // 0x102ae958 JG
		&& SquadWord() != 0                                        // 0x102ae966 JZ
		&& NpcFlags.Has(EElysiumNpcFlag2::SQUAD_NEW_ENEMY))        // 0x102ae97c
	{
		NpcFlags.Clear(EElysiumNpcFlag2::SQUAD_NEW_ENEMY);         // 0x102ae983 AND 0x7fffdfff
		NpcFlags.ClearRawWord2Bits(FElysiumNpcFlags::Word2UnnamedBit31);
		if (CombatStartActivityId != -1                            // 0x102ae992
			&& !HasFrenzied(GFrenziedNoCombatStart))               // 0x102ae9a3
		{
			return SelectTrace(GTroikaFile, 0x481b, GSchedStartCombatSquad);   // 0x102ae9a5
		}
		if (Slot167Enemy(*this) != nullptr)                        // 0x102ae9c5 CALL [+0x29c] / 0x102ae9cd
		{
			// 0x102ae9d3 CALL [+0x29c] (the enemy re-read as the argument), then SquadNewEnemy.
			++PreSelectSquadNewEnemyCalls;                         // 0x102ae9dc CALL SquadNewEnemy
		}
	}

	if (HasInterrupt(EElysiumNpcCond::WasBumped))                  // 0x102ae9e5 CALL 0x1001482b / 0x102ae9ec
	{
		// `0x101e3df0(&DAT_10739a4c, this)`: every active discipline effect whose record byte
		// `+0x33` is set (`ShouldRemove_OnWasBumped`) is removed.
		ElysiumDisciplines::NotifyBumped(*this);                   // 0x102ae9f4
	}
	if (Schedule.Current != ElysiumScheduleId::None                // 0x102aea01
		&& SelectRunningScheduleIs(GSchedMesmerize))               // 0x102aea0a CALL 0x10014f6a / 0x102aea15
	{
		++PreSelectMesmerizeSweeps;                                // 0x102aea1d CALL 0x101e3ee0 (seam)
	}

	if (NpcStateRetail() == GStateCombat)                          // 0x102aea29
	{
		if (HasInterrupt(EElysiumNpcCond::SupernaturalAttackLevel))   // 0x102aea33 CALL / 0x102aea3a
		{
			FElysiumNpcWitnessChannel& Channel = Witness.Channel(ElysiumNpcWitness::EChannel::Supernatural);
			// `m_hClosestPlayer` (+0x628c) resolved: 0x102aea4f JZ / 0x102aea69 JNZ serial.
			FElysiumEntity* const Player = Resolve(*this, Senses.Memory.ClosestPlayer);
			// The offender (+0x6390) resolved: 0x102aea7a JZ / 0x102aea92 JNZ serial.
			FElysiumEntity* const Offender = Resolve(*this, Channel.Offender);
			if (Player == Offender)                                // 0x102aea9c — equal INCLUDING both null
			{
				// `PlayerSupernaturalIncident(player, m_iPLSupernaturalLevelWitnessed, this,
				// m_vecPLSupernaturalLocation)` (`0x102aead5`, `0x1017f4a0`), the receiver re-resolved
				// from +0x628c (0x102aeaa7 JZ / 0x102aeabe JNZ). Retail calls it on a null player when
				// both handles fail; the port skips the call there (crash guard).
				if (FElysiumPlayer* const Receiver = SelectResolvePlayer(Senses.Memory.ClosestPlayer))
				{
					ElysiumLaw::PlayerSupernaturalIncident(*Receiver, Channel.Level, Handle, Channel.Location);
				}
				// The act count off the player re-resolved again (0x102aeae3 JZ / 0x102aeb00 JNZ),
				// 0x102aeb08 CALL 0x10003116 -> 0x1017e740, stored at 0x102aeb0d (+0x6370).
				Channel.Processed = SupernaturalActCount();
			}
			// Offender re-resolved for each call: 0x102aeb22 JZ / 0x102aeb39 JNZ, 0x102aeb55 JZ /
			// 0x102aeb72 JNZ.
			Slot596(Resolve(*this, Channel.Offender));             // 0x102aeb46 vtable +0x950
			Slot597(Resolve(*this, Channel.Offender), 5);          // 0x102aeb81 vtable +0x954
		}
		if (HasInterrupt(EElysiumNpcCond::CriminalAttackLevel))    // 0x102aeb8b CALL / 0x102aeb92
		{
			FElysiumNpcWitnessChannel& Channel = Witness.Channel(ElysiumNpcWitness::EChannel::Criminal);
			// +0x628c resolved (0x102aeba7 JZ / 0x102aebc1 JNZ); offender +0x638c (0x102aebd2 JZ /
			// 0x102aebea JNZ).
			FElysiumEntity* const Player = Resolve(*this, Senses.Memory.ClosestPlayer);
			FElysiumEntity* const Offender = Resolve(*this, Channel.Offender);
			if (Player == Offender)                                // 0x102aebf4
			{
				// The level is `0x1042fe90` over the `+0x6364` unscramble (0x102aec53 CALL
				// 0x100090f2); the port stores it PLAIN (`FElysiumNpcWitnessChannel::Level`,
				// `SecureUnscrambleLevel`), so the decode is the identity here. Receiver re-resolved
				// from +0x628c (0x102aec09 JZ / 0x102aec20 JNZ).
				if (FElysiumPlayer* const Receiver = SelectResolvePlayer(Senses.Memory.ClosestPlayer))
				{
					ElysiumLaw::PlayerCriminalIncident(*Receiver, Channel.Level, Handle, Channel.Location);   // 0x102aec5e
				}
				// Player re-resolved (0x102aec6c JZ / 0x102aec89 JNZ), 0x102aec91 CALL 0x10006bc2 ->
				// 0x1017e720, stored at 0x102aec96 (+0x636c).
				Channel.Processed = CriminalActCount();
			}
			// Offender re-resolved for each call: 0x102aecab JZ / 0x102aecc5 JNZ, 0x102aece1 JZ /
			// 0x102aecfe JNZ.
			Slot596(Resolve(*this, Channel.Offender));             // 0x102aecd2
			Slot597(Resolve(*this, Channel.Offender), 5);          // 0x102aed0d
		}
		if (Cond.Has(EElysiumNpcCond::OnFire))                     // 0x102aed17 CALL / 0x102aed1e
		{
			return SelectTrace(GTroikaFile, 0x4850, GSchedOnFire); // 0x102aed20
		}
		if (Slot167Enemy(*this) == nullptr)                        // 0x102aed40 CALL [+0x29c] / 0x102aed48
		{
			SetState(bNoAlertState ? GStateIdle : GStateAlert);    // 0x102aed54 / 0x102aed5c CALL 0x1026e340
			SelectTrace(GTroikaFile, 0x4862, 0);                   // 0x102aed61
			return SelectNewScheduleRetail();                      // 0x102aed79 tail JMP 0x1028a260
		}
		if (NpcFlags.Has(EElysiumNpcFlag::ATTACK_UNKNOWN))         // 0x102aed92
		{
			NpcFlags.Clear(EElysiumNpcFlag::ATTACK_UNKNOWN);       // 0x102aed99 AND 0xff7fffff
			if (!NpcFlags.Has(EElysiumNpcFlag2::NO_UNKNOWN_ATTACK) // 0x102aedab JNS
				&& !HasFrenzied(GFrenziedNoCombatStart))           // 0x102aedbc
			{
				return SelectTrace(GTroikaFile, 0x4884, GSchedInvestigateUnknownAttack);   // 0x102aedbe
			}
			NpcFlags.Clear(EElysiumNpcFlag2::NO_UNKNOWN_ATTACK);   // 0x102aeddf AND 0x7fffff7f
			NpcFlags.ClearRawWord2Bits(FElysiumNpcFlags::Word2UnnamedBit31);
		}
		if (Cond.Has(EElysiumNpcCond::NewEnemy)                    // 0x102aede9 CALL / 0x102aedf0
			&& !HasFrenzied(GFrenziedNoCombatStart))               // 0x102aee01
		{
			return SelectTrace(GTroikaFile, 0x488c, GSchedStartCombat);   // 0x102aee03
		}
	}

	if (Cond.Has(GCondPlayerOnHead)                                // 0x102aee23 CALL / 0x102aee2a
		&& !IsBusyWithDiscipline())                                // 0x102aee32 CALL 0x1000caae / 0x102aee39
	{
		Cognition.Conditions.Clear(GCondPlayerOnHead);             // 0x102aee43 CALL 0x10269b50
		if (!HasLiveDialogPartner())                               // 0x102aee51 / 0x102aee6e / 0x102aee73
		{
			// 0x102aee81 `cvar_debug_player_on_head` slot 1 (`IsCommand`, false for a ConVar;
			// 0x102aee86 JZ — true would read mode 0) then `[0xb]`.
			const uint32 Mode = static_cast<uint32>(Select19ConVarInt(ESelect19ConVar::DebugPlayerOnHead));
			if (Mode <= 3)                                         // 0x102aee98 JA
			{
				switch (Mode)                                      // 0x102aee9e table 0x102af3a0
				{
				case 0:
					return SelectTrace(GTroikaFile, 0x4899, GSchedPlayerOnHeadRun);        // 0x102aeea5
				case 1:
					return SelectTrace(GTroikaFile, 0x489d, GSchedPlayerOnHeadDive);       // 0x102aeec1
				case 2:
					return SelectTrace(GTroikaFile, 0x48a1, GSchedPlayerOnHeadDiveForward);   // 0x102aeedd
				default:
					if (Roll(99) < 0x50)                           // 0x102aef05 / 0x102aef15 JGE
					{
						return SelectTrace(GTroikaFile, 0x48a7, GSchedPlayerOnHeadDive);   // 0x102aef17
					}
					return SelectTrace(GTroikaFile, 0x48ab, GSchedPlayerOnHeadDiveForward);   // 0x102aef29
				}
			}
		}
	}

	if (NpcStateRetail() == GStateCrimSuspicion)                   // 0x102aef42
	{
		if (HasInterrupt(EElysiumNpcCond::CriminalAttackLevel))    // 0x102aef4c CALL / 0x102aef53
		{
			FElysiumNpcWitnessChannel& Channel = Witness.Channel(ElysiumNpcWitness::EChannel::Criminal);
			// +0x628c resolved (0x102aef68 JZ / 0x102aef82 JNZ); offender +0x638c (0x102aef93 JZ /
			// 0x102aefab JNZ).
			FElysiumEntity* const Player = Resolve(*this, Senses.Memory.ClosestPlayer);
			FElysiumEntity* const Offender = Resolve(*this, Channel.Offender);
			if (Player == Offender)                                // 0x102aefb5
			{
				// Level decode 0x102aefdf CALL 0x1000dc06 (identity, see the COMBAT arm); receiver
				// re-resolved from +0x628c (0x102aeff0 JZ / 0x102af00d JNZ).
				if (FElysiumPlayer* const Receiver = SelectResolvePlayer(Senses.Memory.ClosestPlayer))
				{
					ElysiumLaw::PlayerCriminalIncident(*Receiver, Channel.Level, Handle, Channel.Location);   // 0x102af01e
				}
				// Player re-resolved (0x102af02c JZ / 0x102af049 JNZ), 0x102af051 CALL 0x10006bc2,
				// stored at 0x102af056 (+0x636c).
				Channel.Processed = CriminalActCount();
			}
			// Offender re-resolved (0x102af06b JZ / 0x102af082 JNZ).
			Slot596(Resolve(*this, Channel.Offender));             // 0x102af08f — no slot 597 in this arm
		}
		if (Cond.Has(EElysiumNpcCond::OnFire))                     // 0x102af099 CALL / 0x102af0a0
		{
			return SelectTrace(GTroikaFile, 0x48c4, GSchedOnFire); // 0x102af0a2
		}
		if (Slot167Enemy(*this) == nullptr)                        // 0x102af0c2 CALL [+0x29c] / 0x102af0ca
		{
			SetState(bNoAlertState ? GStateIdle : GStateAlert);    // 0x102af0d6 / 0x102af0de
			SelectTrace(GTroikaFile, 0x48d6, 0);
			return SelectNewScheduleRetail();                      // 0x102af0fb tail JMP 0x1028a260
		}
	}

	const int32 Base = FElysiumNpcBase::PreSelectSchedule();       // 0x102af102 CALL 0x1028a2a0
	if (Base != 0)                                                 // 0x102af109
	{
		return Base;
	}
	if (NpcFlags.Has(EElysiumNpcFlag2::FINISH_SPECIAL_NAV))        // 0x102af11d
	{
		NpcFlags.Clear(EElysiumNpcFlag::PRESERVE_PATH);            // 0x102af12f AND 0xfffffff7
		NpcFlags.Clear(EElysiumNpcFlag2::FINISH_SPECIAL_NAV);      // 0x102af135 AND 0x7ffffffd
		NpcFlags.ClearRawWord2Bits(FElysiumNpcFlags::Word2UnnamedBit31);
		if (NavGetType() == 3)                                     // 0x102af13b CALL 0x1027d990 / 0x102af143
		{
			return SelectTrace(GTroikaFile, 0x48e8, GSchedFinishClimb);   // 0x102af145
		}
		if (NavGetType() == 1)                                     // 0x102af163 / 0x102af16b
		{
			return SelectTrace(GTroikaFile, 0x48ec, GSchedFinishJump);    // 0x102af16d
		}
	}
	if (NpcStateRetail() == GStateCombat                           // 0x102af190
		&& bStayEntrenched                                         // 0x102af19a
		&& CanSeekCover())                                         // 0x102af1a0 slot 592 / 0x102af1a8
	{
		FScheduleHintSearchRequest Request;                        // 0x102af1b4 CALL 0x102b7690(1,0,0,0)
		Request.bRequest1 = true;
		if (const int32 Cover = SelectCoverOrKickSchedule(Request); Cover != 0)   // 0x102af1bb
		{
			return Cover;
		}
	}
	if (NpcFlags.Has(EElysiumNpcFlag::DO_STARTLED))                // 0x102af1cf
	{
		NpcFlags.Clear(EElysiumNpcFlag::DO_STARTLED);              // 0x102af1dd AND AL,0xfd
		return SelectTrace(GTroikaFile, 0x490b, GSchedStartled);   // 0x102af1e3
	}
	switch (NpcStateRetail())                                      // 0x102af1f5
	{
	case GStateIdle:                                               // 0x102af1fc -> 0x102af26d
		if (Cond.Has(EElysiumNpcCond::Knockback))                  // 0x102af271 CALL / 0x102af278
		{
			return SelectTrace(GTroikaFile, 0x4915, GSchedKnockback);   // 0x102af27a
		}
		if (Cond.Has(EElysiumNpcCond::Comfort))                    // 0x102af29a CALL / 0x102af2a1
		{
			return SelectTrace(GTroikaFile, 0x4919, GSchedComfort);     // 0x102af2a3
		}
		if (NpcFlags.Has(EElysiumNpcFlag2::D_CALM))                // 0x102af2d3
		{
			return SelectTrace(GTroikaFile, 0x491d, GSchedCalmed);      // 0x102af2d5
		}
		if (NpcFlags.Has(EElysiumNpcFlag2::D_FOLLOW))              // 0x102af2ff
		{
			return SelectTrace(GTroikaFile, 0x4921, GSchedFollow);      // 0x102af301
		}
		if (NpcFlags.Has(EElysiumNpcFlag2::D_POSSESSED))           // 0x102af327
		{
			return SelectTrace(GTroikaFile, 0x4925, GSchedFollow);      // 0x102af329
		}
		if (HasLiveDialogPartner())                                // 0x102af34e / 0x102af36f / 0x102af378
		{
			return SelectTrace(GTroikaFile, 0x4929, GSchedRunDialog);   // 0x102af37e
		}
		break;
	case GStateCombat:                                             // 0x102af1ff -> 0x102af244
		if (Cond.Has(EElysiumNpcCond::Knockback))                  // 0x102af248 CALL / 0x102af24f
		{
			return SelectTrace(GTroikaFile, 0x493c, GSchedKnockback);   // 0x102af251
		}
		break;
	case GStateAlert:                                              // 0x102af202 -> 0x102af206
		if (const int32 Gate = IdleSequenceGate(); Gate != 0)      // 0x102af20d (line 0x5f20 is the callee's)
		{
			return Gate;
		}
		break;
	default:
		break;
	}
	if (ScheduleHost.bSavePositionWalk)                            // 0x102af21b
	{
		ScheduleHost.bSavePositionWalk = false;                    // 0x102af221
		return SelectTrace(GTroikaFile, 0x4955, GSchedRunToSaved); // 0x102af228
	}
	(void)Now;
	return 0;                                                      // 0x102af39a XOR EAX,EAX
}

// =================================================================================================
// Slot 438 Troika — `CAI_BaseNPCTroika::SelectSchedule` `0x102af660`, 5520 bytes.
// =================================================================================================

int32 FElysiumNpc::TroikaSelectSchedule()
{
	using namespace NpcSelect19;
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	const FElysiumNpcConditions& Cond = Cognition.Conditions;
	auto HasInterrupt = [this](EElysiumNpcCond C)
	{
		return ElysiumSchedule::HasInterruptCondition(Schedule, *this, Cognition.Conditions, C);
	};

	SelectScheduleSelector = 2;                                    // 0x102af66c MOV [ESI+0x1b2c],2
	switch (NpcStateRetail())                                      // 0x102af676 DEC / 0x102af67a JA / 0x102af680 table 0x102b0bf0
	{
	// ---------------------------------------------------------------------------------------------
	case GStateIdle:                                               // 0x102af687
	{
		if (IsBusyWithDiscipline()                                 // 0x102af689 / 0x102af690
			|| bInChoreoScene)                                     // 0x102af69e
		{
			return SelectTrace(GTroikaFile, 0x49d2, GSchedIdleDisposition);   // 0x102af8dc
		}
		// The follower ladder (slot 607, `vtable +0x97c`): a non-zero answer is RETURNED
		// (`0x102af6b0 JNZ 0x102b0af5`), not handed to the base.
		if (const int32 Follower = Slot607(); Follower != 0)       // 0x102af6a8 / 0x102af6b0
		{
			return Follower;
		}
		int32 PatrolSchedule = 0;
		if (SelectPatrolPathObject(PatrolSchedule))                // 0x102af6b6 / 0x102af6be
		{
			if (PatrolSchedule != 0)                               // 0x102af6c7
			{
				// `0x1029f650(this, this+0x658c)` -- the patrol node's interesting-place draw: it
				// resets `m_bPatrolPathUseHint` (+0x65a0) first and rolls `ip_percent` against the
				// node the patrol cell points at, exactly as `BuildPatrolPath`'s own call (0x1029f547).
				++SelectPatrolPathDraws;
				FUN_1029f650(PatrolCurrentNode(PatrolPathCell));   // 0x102af731 LEA / 0x102af738 CALL 0x100151f4
				return SelectTrace(GTroikaFile, 0x498d, PatrolSchedule);   // 0x102af743
			}
			UE_LOG(LogElysiumNpcEnt, Log, TEXT("WARNING:  Patrol path for '%s' has no schedule."),
				*DebugString());                                   // 0x102af6c9 GetDebugName / 0x102af6d4 DevMsg
			++SelectPatrolPathReleases;                            // 0x102af6e6 CALL 0x1029f5d0
			ReleasePatrolPath(&PatrolPathCell);
		}
		if (bUseInteresting)                                       // 0x102af6f3
		{
			if (NavigatorPathType() != 8                           // 0x102af6ff CALL 0x102ee620 / 0x102af707
				&& CurrentSpotIndex == INDEX_NONE)                 // 0x102af711 m_pInterestingPlace (+0x62ec)
			{
				return SelectTrace(GTroikaFile, 0x49a9, GSchedWalkToPlaceSetup);   // 0x102af713
			}
			if (HasInterrupt(EElysiumNpcCond::CrosswalkDontWalk))  // 0x102af763 / 0x102af76a
			{
				return SelectTrace(GTroikaFile, 0x4998, GSchedWaitAtCrosswalk);    // 0x102af76c
			}
			if (HasInterrupt(EElysiumNpcCond::ShouldInteract))     // 0x102af78e / 0x102af795
			{
				return SelectTrace(GTroikaFile, 0x499c, GSchedInteract);           // 0x102af797
			}
			if (HasInterrupt(GCondShouldLoiter))                   // 0x102af7b9 / 0x102af7ca
			{
				return SelectTrace(GTroikaFile, 0x49a0, GSchedLoiter);             // 0x102af7cc
			}
			return SelectTrace(GTroikaFile, 0x49a4, GSchedWalkToPlace);            // 0x102af7e0
		}
		if (bAllowAlertLookaround)                                 // 0x102af7fc
		{
			const int32 Chance = FMath::Min((EnemySightings + 2) * 5, 0x1e);   // 0x102af804..0x102af811 (0x102af80f JL)
			if (Roll(99) < Chance)                                 // 0x102af822 / 0x102af827 JLE
			{
				return SelectTrace(GTroikaFile, 0x49b4, GSchedAlertLookAround);    // 0x102af829
			}
		}
		if (Resolve(*this, BlockedDoor) != nullptr                 // 0x102af856 / 0x102af870 / 0x102af875
			|| Resolve(*this, CondHitByDoor) != nullptr)           // 0x102af880 / 0x102af897 / 0x102af89c
		{
			if (const int32 Door = SelectDoorObstructionSchedule(); Door != 0)   // 0x102af8a0 / 0x102af8a7
			{
				return Door;
			}
		}
		if (bReturnToInitialPos)                                   // 0x102af8b5
		{
			bReturnToInitialPos = false;                           // 0x102af8b7
			return SelectTrace(GTroikaFile, 0x49c6, GSchedIdleReturnToInitial);    // 0x102af8be
		}
		return SelectTrace(GTroikaFile, 0x49d2, GSchedIdleDisposition);            // 0x102af8dc
	}
	// ---------------------------------------------------------------------------------------------
	case GStateCombat:                                             // 0x102afc24
	{
		Cognition.bCondTookDamage = false;                         // 0x102afc28
		if (Cond.Has(EElysiumNpcCond::EnemyDead))                  // 0x102afc2f / 0x102afc38
		{
			ElysiumNpcEnemy::SetEnemy(*this, FElysiumEntityHandle::Invalid());   // 0x102afc3c
			if (ElysiumNpcEnemy::ChooseEnemy(*this))                   // 0x102afc43 / 0x102afc4c
			{
				Cognition.Conditions.Clear(EElysiumNpcCond::EnemyDead);   // 0x102afc50
				SelectTrace(GTroikaFile, 0x4abf, 0);
				return SelectNewScheduleRetail();                  // 0x102afc71 JMP 0x10008f80
			}
			SetState(bNoAlertState ? GStateIdle : GStateAlert);    // 0x102afc7e / 0x102afc86
			SelectTrace(GTroikaFile, 0x4acb, 0);
			return SelectNewScheduleRetail();                      // 0x102afca7 JMP 0x10008f80
		}
		if ((Cond.Has(EElysiumNpcCond::LightDamage)                // 0x102afcae / 0x102afcb5
				|| Cond.Has(EElysiumNpcCond::HeavyDamage))         // 0x102afcbb / 0x102afcc2
			&& (BaseScheduleHost.MemoryBits & GMemoryFlinchSuppressed) == 0   // 0x102afccb
			&& SelectWeightedSequenceForActivity(GActSmallFlinch) != INDEX_NONE)  // 0x102afcd3 / 0x102afcdb
		{
			Cognition.bCondTookDamage = false;                     // 0x102afcdd
			return SelectTrace(GTroikaFile, 0x4ad5, ElysiumSched::SMALL_FLINCH);   // 0x102afce4
		}
		if (IRelationType(Slot167Enemy(*this)) == GDispositionFear)   // 0x102afd06 / 0x102afd0f / 0x102afd1a
		{
			if (!Cond.Has(EElysiumNpcCond::SeeEnemy)               // 0x102afd1e / 0x102afd25
				&& !Cond.Has(EElysiumNpcCond::LightDamage)         // 0x102afd2b / 0x102afd32
				&& !Cond.Has(EElysiumNpcCond::HeavyDamage))        // 0x102afd38 / 0x102afd3f
			{
				return SelectTrace(GTroikaFile, 0x4ae6, GSchedFearFace);   // 0x102afd41
			}
			Cognition.bCondTookDamage = false;                     // 0x102afd63
			FearSound();                                           // 0x102afd6a slot 492
			return SelectTrace(GTroikaFile, 0x4ae2, GSchedRunFromEnemy);   // 0x102afd70
		}
		if (Cond.Has(EElysiumNpcCond::NoPrimaryAmmo)               // 0x102afd90 / 0x102afd97
			&& ActiveWeaponEntity() != nullptr                     // 0x102afd9b / 0x102afda2
			&& ActiveWeaponReserveAmmo() > 0)                      // 0x102afda6 / 0x102afdb4 CALL 0x103346c0(weapon+0x744) / 0x102afdbb JLE
		{
			return SelectTrace(GTroikaFile, 0x4aeb, GSchedHideAndReload);   // 0x102afdbd
		}
		// The gate is the unnamed dword `+0x6444` (`ShootAtHintNode`); the ladder runs when it is
		// zero or `WAITING_ATTACK_TIME` stands (second judge, pass R).
		if (ScheduleHost.ShootAtHintNode != 0                      // 0x102afddb / 0x102afde3
			&& !Cond.Has(EElysiumNpcCond::WaitingAttackTime))      // 0x102afde9 / 0x102afdf0
		{
			// 0x102afdf4 / 0x102afe03 weapon, 0x102afe0c `+0x5a0` capability word.
			if ((SelectActiveWeaponWord() & GWeaponRangedBits) != 0)   // 0x102afdfb / 0x102afe15 TEST AH,0x60
			{
				return SelectTrace(GTroikaFile, 0x4af3, GSchedShootAtHint);   // 0x102afe1b
			}
			break;                                                 // 0x102afdfb / 0x102afe15 -> 0x102b0be3
		}
		if (!Cond.Has(EElysiumNpcCond::SeeEnemy))                  // 0x102afe3d / 0x102afe46
		{
			if (!Cond.Has(EElysiumNpcCond::EnemyOccluded))         // 0x102afe4a / 0x102afe5b
			{
				return SelectTrace(GTroikaFile, 0x4b02, GSchedCombatFace);    // 0x102afe5d
			}
			return SelectTrace(GTroikaFile, 0x4b07, GSchedChaseEnemy);        // 0x102afe71
		}
		if (Cond.Has(EElysiumNpcCond::TooCloseForRanged)           // 0x102afe87 / 0x102afe8e
			|| Cond.Has(EElysiumNpcCond::TooCloseToAttack))        // 0x102afe98 / 0x102afe9f
		{
			// 0x102b0180 / 0x102b018b weapon, 0x102b0194 `+0x5a0` capability word.
			if ((SelectActiveWeaponWord() & GWeaponRangedBits) != 0   // 0x102b0187 / 0x102b019d
				&& !Cond.Has(EElysiumNpcCond::WaitingAttackTime)   // 0x102b01a3 / 0x102b01aa
				&& !Cond.Has(EElysiumNpcCond::WeaponBlockedByFriend))   // 0x102b01b0 / 0x102b01b7
			{
				if (ShouldDodgeRangedAttack())                     // 0x102b01bb CALL 0x102b7f40 / 0x102b01cc
				{
					return SelectTrace(GTroikaFile, 0x4b15, GSchedStepBackRangeAttack1);   // 0x102b01ce
				}
				return SelectTrace(GTroikaFile, 0x4b19, GSchedForcedRangeAttack1);         // 0x102b01e2
			}
			return SelectTrace(GTroikaFile, 0x4b1f, GSchedBackAwayFromEnemy);              // 0x102b01f6
		}
		if (Cond.Has(EElysiumNpcCond::CanRangeAttack1))            // 0x102afea9 / 0x102afeb0
		{
			if (RangedDisciplineGate(this))                        // 0x102afebc CALL 0x101e3f50 / 0x102afec3
			{
				return SelectTrace(GTroikaFile, 0x4b29, GSchedStepBackRangeAttack1);       // 0x102afec5
			}
			if (Roll(99) < 0x14                                    // 0x102afeef / 0x102afef5 JGE
				&& BaseScheduleHost.HintNode == INDEX_NONE         // 0x102afeff m_pHintNode (+0x5ddc)
				&& ScheduleHost.EnemyDistUnits < GRangeStepBackDistance)   // 0x102aff01..0x102aff12 JP
			{
				return SelectTrace(GTroikaFile, 0x4b2e, GSchedStepBackRangeAttack1);       // 0x102aff14
			}
			return SelectTrace(GTroikaFile, 0x4b32, GSchedRangeAttack1);                   // 0x102aff32
		}
		if (Cond.Has(EElysiumNpcCond::CanRangeAttack2))            // 0x102aff54 / 0x102aff5b
		{
			return SelectTrace(GTroikaFile, 0x4b37, GSchedRangeAttack2);      // 0x102aff5d
		}
		if (Cond.Has(EElysiumNpcCond::CanMeleeAttack1))            // 0x102aff7f / 0x102aff86
		{
			return SelectTrace(GTroikaFile, 0x4b3b, GSchedMeleeAttack1);      // 0x102aff88
		}
		if (Cond.Has(EElysiumNpcCond::CanMeleeAttack2))            // 0x102affaa / 0x102affb1
		{
			return SelectTrace(GTroikaFile, 0x4b3f, GSchedMeleeAttack2);      // 0x102affb3
		}
		if (Cond.Has(EElysiumNpcCond::NotFacingAttack))            // 0x102affd5 / 0x102affdc
		{
			return SelectTrace(GTroikaFile, 0x4b43, GSchedCombatFace);        // 0x102affde
		}
		if (Cond.Has(EElysiumNpcCond::TooFarToAttack))             // 0x102b0000 / 0x102b0007
		{
			return SelectTrace(GTroikaFile, 0x4b48, GSchedChaseEnemy);        // 0x102b0009
		}
		if (Cond.Has(EElysiumNpcCond::WeaponBlockedByFriend)       // 0x102b002b / 0x102b0032
			|| Cond.Has(EElysiumNpcCond::WaitingAttackTime))       // 0x102b003c / 0x102b0043
		{
			return SelectTrace(GTroikaFile, 0x4b4d, GSchedWaitForClearShot);  // 0x102b0160
		}
		if (Cond.Has(EElysiumNpcCond::EnemyUnreachable))           // 0x102b004d / 0x102b0054
		{
			return SelectTrace(GTroikaFile, 0x4b52, GSchedTakeCoverFromEnemy);   // 0x102b0056
		}
		if (Cond.Has(EElysiumNpcCond::WeaponSightOccluded)         // 0x102b0078 / 0x102b007f
			&& (SelectActiveWeaponWord() & GWeaponRangedBits) != 0)   // 0x102b0083 / 0x102b008e / 0x102b0097 / 0x102b008a / 0x102b00a0
		{
			return SelectTrace(GTroikaFile, 0x4b60, GSchedForcedRangeAttack1);   // 0x102b00a2
		}
		if (Cond.Has(EElysiumNpcCond::HaveEnemyLos)                // 0x102b00c4 / 0x102b00cb
			&& ActiveWeaponEntity() != nullptr)                    // 0x102b00cf / 0x102b00d6
		{
			if ((SelectActiveWeaponWord() & GWeaponRangedBits) != 0)   // 0x102b00da / 0x102b00e3 / 0x102b00ec
			{
				return SelectTrace(GTroikaFile, 0x4b73, GSchedForcedRangeAttack1);   // 0x102b00ee
			}
			if ((SelectActiveWeaponWord() & GWeaponMeleeBits) != 0)    // 0x102b010e / 0x102b0117 / 0x102b0122
			{
				return SelectTrace(GTroikaFile, 0x4b77, GSchedMeleeAdvance);         // 0x102b0124
			}
		}
		return SelectTrace(GTroikaFile, 0x4b81, GSchedAttemptToClose);    // 0x102b0142
	}
	// ---------------------------------------------------------------------------------------------
	case GStateAlert:                                              // 0x102af8fa
	{
		if (const int32 Unknown = SelectUnknownAlertSchedule(); Unknown != 0)   // 0x102af8fc / 0x102af903
		{
			return Unknown;
		}
		if (const int32 Damage = CacheDamagePosition(); Damage != 0)   // 0x102af90b CALL 0x102b8c40 / 0x102af912
		{
			return Damage;
		}
		if (Cond.Has(EElysiumNpcCond::DetectedAttack))             // 0x102af91c / 0x102af923
		{
			return SelectTrace(GTroikaFile, 0x49e8, GSchedTurnToDetectedAttack);   // 0x102af925
		}
		if (const int32 Door = SelectDoorObstructionSchedule(); Door != 0)   // 0x102af945 / 0x102af94c
		{
			return Door;
		}
		if (const int32 Sound = SelectSoundAlertSchedule(); Sound != 0)   // 0x102af954 / 0x102af95b
		{
			return Sound;
		}
		bGoToIdleState = true;                                     // 0x102af96d
		Mind.ForceStateChange();                                   // 0x102af973 MOV [+0x1b28],1
		return SelectTrace(GTroikaFile, 0x4a08, GSchedAlertWait);  // 0x102af979
	}
	// ---------------------------------------------------------------------------------------------
	case 8:                                                        // 0x102b0250 — FLEE
	{
		if (Cond.Has(GCondCoverFailure))                           // 0x102b0254 / 0x102b025b
		{
			return SelectTrace(GTroikaFile, 0x4be4, GSchedFleeAndCower);   // 0x102b025d
		}
		if (!Cond.Has(EElysiumNpcCond::SeeEnemy)                   // 0x102b027f / 0x102b0286
			&& !Cond.Has(EElysiumNpcCond::SeeFear)                 // 0x102b0290 / 0x102b0297
			&& !Cond.Has(EElysiumNpcCond::LightDamage)             // 0x102b02a1 / 0x102b02a8
			&& !Cond.Has(EElysiumNpcCond::HeavyDamage)             // 0x102b02b2 / 0x102b02b9
			&& !Cond.Has(EElysiumNpcCond::RepeatedDamage)          // 0x102b02c3 / 0x102b02ca
			&& !Cond.Has(EElysiumNpcCond::SupernaturalFleeLevel)   // 0x102b02d4 / 0x102b02db
			&& !Cond.Has(EElysiumNpcCond::CriminalFleeLevel))      // 0x102b02e5 / 0x102b02ec
		{
			if (Cond.Has(EElysiumNpcCond::DetectedAttack))         // 0x102b02f6 / 0x102b02fd
			{
				return SelectTrace(GTroikaFile, 0x4c78, GSchedTurnToDetectedAttack);   // 0x102b02ff
			}
			if (Cond.Has(EElysiumNpcCond::InvestigateSound))       // 0x102b0321 / 0x102b0328
			{
				NpcFlags.Clear(EElysiumNpcFlag::INITIAL_FLEE);     // 0x102b0340 AND AH,0xfe
				Senses.Memory.NextInvestigateSoundTime = Now + GInvestigateSoundDelay;   // 0x102b0349
				Senses.CommitBestSound(*this, Cognition.Conditions);   // 0x102b034f CALL 0x102b4090
				return SelectTrace(GTroikaFile, 0x4c83, GSchedTurnToSound);   // 0x102b0354
			}
			return SelectTrace(GTroikaFile, 0x4c86, GSchedCower); // 0x102b0372
		}
		Cognition.bCondTookDamage = false;                         // 0x102b0396
		if (!NpcFlags.Has(EElysiumNpcFlag::INITIAL_FLEE))          // 0x102b03ab
		{
			return SelectTrace(GTroikaFile, 0x4c72, GSchedFleeAndCower);   // 0x102b0a9e
		}
		NpcFlags.Clear(EElysiumNpcFlag::INITIAL_FLEE);             // 0x102b03b9 AND AH,0xfe
		Senses.Memory.NextFleeSoundTime = Now                      // 0x102b03cc RandomFloat / 0x102b03db
			+ ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(GFleeSoundMin, GFleeSoundMax);
		FleeSound();                                               // 0x102b03e1 slot 498
		if (Cond.Has(EElysiumNpcCond::LightDamage)                 // 0x102b03eb / 0x102b03f2
			|| Cond.Has(EElysiumNpcCond::HeavyDamage)              // 0x102b03fc / 0x102b0403
			|| Cond.Has(EElysiumNpcCond::RepeatedDamage))          // 0x102b040d / 0x102b0414
		{
			return SelectTrace(GTroikaFile, 0x4c00, GSchedFleeScream);   // 0x102b0a80
		}
		if (Cond.Has(EElysiumNpcCond::SupernaturalFleeLevel))      // 0x102b041e / 0x102b0425
		{
			FElysiumNpcWitnessChannel& Channel = Witness.Channel(ElysiumNpcWitness::EChannel::Supernatural);
			if (FElysiumEntity* const Offender = Resolve(*this, Channel.Offender))   // 0x102b043a..59 (0x102b0454 / 0x102b0459)
			{
				Slot596(Offender);                                 // 0x102b0464 / 0x102b047b re-resolve, 0x102b0488 vtable +0x950
			}
			FElysiumEntity* const Player = Resolve(*this, Senses.Memory.ClosestPlayer);   // 0x102b049d..b7 (0x102b04b7)
			if (Player != Resolve(*this, Channel.Offender))        // 0x102b04c8..0x102b04ec (0x102b04e2)
			{
				if (Resolve(*this, Channel.Offender) != nullptr)   // 0x102b06bd..0x102b06d9 (0x102b06d4)
				{
					return SelectTrace(GTroikaFile, 0x4c2c, GSchedFleeAndCower);   // 0x102b06db
				}
				SavePosition = Channel.Location;                   // 0x102b06f9..0x102b0718
				return SelectTrace(GTroikaFile, 0x4c31, GSchedFleeNoEnemy);        // 0x102b071e
			}
			if (Player != nullptr)                                 // 0x102b04fb..0x102b051a (0x102b0515)
			{
				// 0x102b0534 / 0x102b054b re-resolve, 0x102b0561 slot 217 (`+0x364`) on the player.
				++SelectFleeDangerSounds;                          // 0x102b056a CALL 0x1000bca8 (seam)
			}
			if (!Witness.bSupernaturalFleeOnly)                    // 0x102b0580
			{
				// 0x102b058b / 0x102b05a2 the player re-resolved as the receiver.
				if (FElysiumPlayer* const Receiver = SelectResolvePlayer(Senses.Memory.ClosestPlayer))
				{
					ElysiumLaw::PlayerSupernaturalIncident(*Receiver, Channel.Level, Handle, Channel.Location);   // 0x102b05b9
				}
				Channel.Processed = SupernaturalActCount();        // 0x102b05c7 / 0x102b05e4 / 0x102b0646 / 0x102b064b
			}
			else if (Cond.Has(EElysiumNpcCond::SeePlayer))         // 0x102b05ee / 0x102b05f5
			{
				RememberScaredNpc(Channel.Level, this);            // 0x102b0600 / 0x102b061d / 0x102b0629 / 0x102b063a CALL 0x1017fd60
				Channel.Processed = SupernaturalActCount();        // 0x102b05be -> 0x102b064b
			}
			// 0x102b0651 `m_flPlayerDist (+0x6264) <= 512.0` (FCOMP / TEST AH,0x41 / JP), then the roll.
			if (Senses.Memory.ClosestPlayerDistanceCm / ElysiumMove::U <= ElysiumNpcTunables::FiveHundredTwelve
				&& Roll(99) < 0x50)                                // 0x102b0662 / 0x102b0670 / 0x102b0676 JGE
			{
				return SelectTrace(GTroikaFile, 0x4c23, GSchedFleeTurnToPlayerNear);   // 0x102b0678
			}
			return SelectTrace(GTroikaFile, 0x4c27, GSchedFleeTurnToPlayer);           // 0x102b0696
		}
		if (Cond.Has(EElysiumNpcCond::CriminalFleeLevel))          // 0x102b073f / 0x102b0746
		{
			FElysiumNpcWitnessChannel& Channel = Witness.Channel(ElysiumNpcWitness::EChannel::Criminal);
			if (FElysiumEntity* const Offender = Resolve(*this, Channel.Offender))   // 0x102b075b..7a (0x102b0775 / 0x102b077a)
			{
				Slot596(Offender);                                 // 0x102b0785 / 0x102b079c re-resolve, 0x102b07a9
			}
			FElysiumEntity* const Player = Resolve(*this, Senses.Memory.ClosestPlayer);   // 0x102b07be..d8 (0x102b07d8)
			if (Player != Resolve(*this, Channel.Offender))        // 0x102b07e9..0x102b080d (0x102b0803)
			{
				if (Resolve(*this, Channel.Offender) != nullptr)   // 0x102b09a0..0x102b09bc (0x102b09b7)
				{
					return SelectTrace(GTroikaFile, 0x4c56, GSchedFleeAndCower);   // 0x102b09be
				}
				SavePosition = Channel.Location;                   // 0x102b09dc..0x102b09fb
				return SelectTrace(GTroikaFile, 0x4c5b, GSchedFleeNoEnemy);        // 0x102b0a01
			}
			if (Player != nullptr)                                 // 0x102b081c..0x102b083b (0x102b0836)
			{
				// 0x102b0855 / 0x102b086c re-resolve, 0x102b0882 slot 217 (`+0x364`) on the player.
				++SelectFleeDangerSounds;                          // 0x102b088b CALL 0x1000bca8 (seam)
			}
			// 0x102b08b7 `0x1000dc06` decodes the `+0x6364` scrambled level (stored plain here);
			// 0x102b08c8 / 0x102b08e5 the player re-resolved as the receiver.
			if (FElysiumPlayer* const Receiver = SelectResolvePlayer(Senses.Memory.ClosestPlayer))
			{
				ElysiumLaw::PlayerCriminalIncident(*Receiver, Channel.Level, Handle, Channel.Location);   // 0x102b08f6
			}
			Channel.Processed = CriminalActCount();                // 0x102b0904 / 0x102b0921 / 0x102b0929 / 0x102b093a
			if (Senses.Memory.ClosestPlayerDistanceCm / ElysiumMove::U <= ElysiumNpcTunables::FiveHundredTwelve
				&& Roll(99) < 0x50)                                // 0x102b0945 / 0x102b0953 / 0x102b0959
			{
				return SelectTrace(GTroikaFile, 0x4c4d, GSchedFleeTurnToPlayerNear);   // 0x102b095b
			}
			return SelectTrace(GTroikaFile, 0x4c51, GSchedFleeTurnToPlayer);           // 0x102b0979
		}
		if (Cond.Has(EElysiumNpcCond::SeeFear))                    // 0x102b0a22 / 0x102b0a29
		{
			// 0x102b0a34 / 0x102b0a4f resolve (a stale handle passes NULL, 0x102b0a55).
			Slot596(Resolve(*this, BaseMemory.Seen(FElysiumNpcBaseMemory::ESeen::Fear)));   // 0x102b0a5c m_hLastSeenFearEnt
		}
		return SelectTrace(GTroikaFile, 0x4c69, GSchedFleeScream);     // 0x102b0a62
	}
	// ---------------------------------------------------------------------------------------------
	case 0xb:                                                      // 0x102af98d — HUNT
	{
		if (HasInterrupt(EElysiumNpcCond::SeeUnknown)              // 0x102af991 / 0x102af998
			|| HasInterrupt(EElysiumNpcCond::InvestigateSight))    // 0x102af9a2 / 0x102af9a9
		{
			return SelectTrace(GTroikaFile, 0x4a1a, GSchedHuntInvestigateUnknown);   // 0x102afc06
		}
		if (const int32 Damage = CacheDamagePosition(); Damage != 0)   // 0x102af9b1 / 0x102af9b8
		{
			return Damage;
		}
		if (HasInterrupt(EElysiumNpcCond::LostUnknown))            // 0x102af9c2 / 0x102af9c9
		{
			return SelectTrace(GTroikaFile, 0x4a26, GSchedHuntInvestigateUnknownLost);   // 0x102af9cb
		}
		if (HasInterrupt(EElysiumNpcCond::InvestigateSound)        // 0x102af9ed / 0x102af9f4
			|| HasInterrupt(EElysiumNpcCond::HearDanger)           // 0x102af9fe / 0x102afa05
			|| HasInterrupt(EElysiumNpcCond::HearCombat)           // 0x102afa0f / 0x102afa16
			|| HasInterrupt(EElysiumNpcCond::HearWorld)            // 0x102afa20 / 0x102afa27
			|| HasInterrupt(EElysiumNpcCond::HearBulletImpact)     // 0x102afa31 / 0x102afa38
			|| HasInterrupt(EElysiumNpcCond::HearPlayer))          // 0x102afa42 / 0x102afa49
		{
			Senses.Memory.NextInvestigateSoundTime = Now + GInvestigateSoundDelay;   // 0x102afb91
			Senses.CommitBestSound(*this, Cognition.Conditions);   // 0x102afb97
			// `RandomInt(0, 99) < 100` always passes; the draw is retail's and is consumed.
			if ((Cond.Has(EElysiumNpcCond::HearCombat)             // 0x102afba0 / 0x102afba7
					|| Cond.Has(EElysiumNpcCond::HearBulletImpact))   // 0x102afbad / 0x102afbb4
				&& Roll(99) < 100)                                 // 0x102afbc2 / 0x102afbc8
			{
				return SelectTrace(GTroikaFile, 0x4a39, GSchedHuntInvestigateFlinch);   // 0x102afbca
			}
			return SelectTrace(GTroikaFile, 0x4a3d, GSchedHuntInvestigate);             // 0x102afbe8
		}
		if (HasInterrupt(EElysiumNpcCond::HearFlinch))             // 0x102afa53 / 0x102afa5c
		{
			Senses.CommitBestSound(*this, Cognition.Conditions);   // 0x102afa5e
			return SelectTrace(GTroikaFile, 0x4a43, GSchedInvestigateSoundFlinch);      // 0x102afa63
		}
		if (Cond.Has(EElysiumNpcCond::SeeSoundSource))             // 0x102afa83 / 0x102afa8a
		{
			if (const int32 Source = SelectSoundSourceSchedule(GSchedHuntRunToSaved, GSchedFleeAndCower);
				Source != 0)                                       // 0x102afa95 PUSH 0x73 / PUSH 0x84 / 0x102afa9c
			{
				return Source;
			}
		}
		if (PatrolPathHuntCell.Path == nullptr)                   // 0x102afaa2 / 0x102afaaa m_sppPatrolPathHunt.m_pPath (+0x6598)
		{
			NpcFlags.Clear(EElysiumNpcFlag::MADE_HUNT_PATH);       // 0x102afab2 AND AH,0xef
		}
		if (!NpcFlags.Has(EElysiumNpcFlag::MADE_HUNT_PATH))        // 0x102afac1 NOT / TEST DH,0x10 / 0x102afac6
		{
			// FLD curtime / FCOMP m_flHuntExpireTimer / AND EAX,0x100 (C0): below the stamp (or
			// unordered) asks for the enemy; at or past it the hunt is finished.
			if (!(Now < HuntExpireTime) && !FMath::IsNaN(HuntExpireTime))   // 0x102afad4 / 0x102afae1 JNZ
			{
				return SelectTrace(GTroikaFile, 0x4a9a, GSchedHuntFinish);   // 0x102afae3
			}
			FElysiumEntity* const Enemy = GetEnemy();              // 0x102afb05 slot 168, vtable +0x2a0
			if (Enemy != nullptr && Enemy->IsAlive())              // 0x102afb0d / 0x102afb13 slot 168 again / 0x102afb1d slot 158 / 0x102afb25
			{
				return SelectTrace(GTroikaFile, 0x4aa3, GSchedHuntSetup);        // 0x102afb27
			}
			return SelectTrace(GTroikaFile, 0x4a9f, GSchedHuntSetupNoEnemy);     // 0x102afb45
		}
		return SelectTrace(GTroikaFile, 0x4aa8, GSchedHunt);       // 0x102afb63
	}
	// ---------------------------------------------------------------------------------------------
	case 0xc:                                                      // 0x102b0214
		return SelectTrace(GTroikaFile, 0x4bd7, GSchedIdleDisposition);
	case 0xd:                                                      // 0x102b0232
		return SelectTrace(GTroikaFile, 0x4bdb, GSchedIdleStand);
	// ---------------------------------------------------------------------------------------------
	case GStateCrimSuspicion:                                      // 0x102b0abc — the m_iSubState walk
		switch (SubState)                                          // 0x102b0abc / 0x102b0ac5 JA / 0x102b0acb table 0x102b0c28
		{
		case 1:
			SubState = 2;                                          // 0x102b0ad2
			return SelectTrace(GTroikaFile, 0x4c9f, GSchedCrimSuspFace);
		case 2:
			SubState = 3;                                          // 0x102b0afa
			return SelectTrace(GTroikaFile, 0x4ca4, GSchedCrimSuspWaitForOcclusion);
		case 3:
			SubState = 4;                                          // 0x102b0b22
			return SelectTrace(GTroikaFile, 0x4ca9, GSchedCrimSuspGoToLkp);
		case 4:
			SubState = 5;                                          // 0x102b0b4e
			if (Cond.Has(EElysiumNpcCond::SeeEnemy))               // 0x102b0b58 / 0x102b0b69
			{
				return SelectTrace(GTroikaFile, 0x4cb0, GSchedCrimSuspWaitAtLkp);    // 0x102b0b6b
			}
			return SelectTrace(GTroikaFile, 0x4cb4, GSchedCrimSuspWaitLookAround);   // 0x102b0b7f
		case 5:
			SubState = 2;                                          // 0x102b0b93
			return SelectTrace(GTroikaFile, 0x4cba, GSchedCrimSuspFace);
		default:                                                   // 0 and > 5
			SubState = 1;                                          // 0x102b0bbb
			return SelectTrace(GTroikaFile, 0x4c9a, GSchedCrimSuspApproach);
		}
	default:                                                       // 0, 4..7, 9, 0xa, > 0xe
		break;
	}
	return BaseSelectSchedule();                                   // 0x102b0be3 / 0x102b0beb JMP 0x10001d57
}

// =================================================================================================
// `0x102b8a60` — the see-unknown ladder.
// =================================================================================================

int32 FElysiumNpc::SelectUnknownAlertSchedule()
{
	using namespace NpcSelect19;
	const FElysiumNpcConditions& Cond = Cognition.Conditions;
	auto HasInterrupt = [this](EElysiumNpcCond C)
	{
		return ElysiumSchedule::HasInterruptCondition(Schedule, *this, Cognition.Conditions, C);
	};
	const bool bInvestigating = (BaseScheduleHost.MemoryBits & GMemoryInvestigating) != 0;

	if (HasInterrupt(EElysiumNpcCond::IgnoreUnknown)               // 0x102b8a65 / 0x102b8a6c
		&& !bInvestigating)                                        // 0x102b8a6e / 0x102b8a78
	{
		return SelectTrace(GTroikaFile, 0x5f38, GSchedInvestigateUnknownIgnore);   // 0x102b8a7a
	}
	if (HasInterrupt(EElysiumNpcCond::SeeUnknown)                  // 0x102b8aa0
		|| HasInterrupt(EElysiumNpcCond::UnknownAdvancing)         // 0x102b8ab1
		|| Cond.Has(EElysiumNpcCond::InvestigateSight))            // 0x102b8ac2
	{
		AlertLevel = 3;                                            // 0x102b8b53 CALL 0x102b5dc0(3)
		if (!bInvestigating)                                       // 0x102b8b58 / 0x102b8b62
		{
			return SelectTrace(GTroikaFile, 0x5f56, GSchedInvestigateUnknown);   // 0x102b8bc2
		}
		if (NpcFlags.Has(EElysiumNpcFlag::LOOKED_AT_UNKNOWN))      // 0x102b8b64 / 0x102b8b6e
		{
			if (Cond.Has(EElysiumNpcCond::UnknownRunTimer))        // 0x102b8b72 / 0x102b8b83
			{
				return SelectTrace(GTroikaFile, 0x5f48, GSchedInvestigateUnknownOtherRun);   // 0x102b8b85
			}
			return SelectTrace(GTroikaFile, 0x5f4c, GSchedInvestigateUnknownOther);          // 0x102b8b96
		}
		return SelectTrace(GTroikaFile, 0x5f51, GSchedInvestigateUnknownQuick);   // 0x102b8ba7
	}
	if (HasInterrupt(EElysiumNpcCond::LostUnknown))                // 0x102b8ad3
	{
		if (bInvestigating                                         // 0x102b8adf
			&& Cond.Has(EElysiumNpcCond::UnknownRunTimer))         // 0x102b8aec
		{
			return SelectTrace(GTroikaFile, 0x5f5e, GSchedInvestigateUnknownLostRun);   // 0x102b8aee
		}
		return SelectTrace(GTroikaFile, 0x5f62, GSchedInvestigateUnknownLost);          // 0x102b8b09
	}
	if (NpcFlags.Has(EElysiumNpcFlag::LOOKED_AT_UNKNOWN))          // 0x102b8b24 / 0x102b8b2e
	{
		return SelectTrace(GTroikaFile, 0x5f71, GSchedInvestigateUnknownOther);         // 0x102b8b30
	}
	return 0;                                                      // 0x102b8b4b
}

// =================================================================================================
// `0x102b9060` — the sound ladder.
// =================================================================================================

int32 FElysiumNpc::SelectSoundAlertSchedule()
{
	using namespace NpcSelect19;
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	const FElysiumNpcConditions& Cond = Cognition.Conditions;
	auto HasInterrupt = [this](EElysiumNpcCond C)
	{
		return ElysiumSchedule::HasInterruptCondition(Schedule, *this, Cognition.Conditions, C);
	};

	if (HasInterrupt(EElysiumNpcCond::InvestigateSound))           // 0x102b9065 / 0x102b906c
	{
		if (Cond.Has(EElysiumNpcCond::HearCombat)                  // 0x102b907d
			|| Cond.Has(EElysiumNpcCond::HearBulletImpact))        // 0x102b908e
		{
			Senses.Memory.NextInvestigateSoundTime = Now + GInvestigateSoundDelay;   // 0x102b921a
			Senses.CommitBestSound(*this, Cognition.Conditions);   // 0x102b9220
			AlertLevel = 3;                                        // 0x102b9229 CALL 0x102b5dc0(3)
			if ((BaseScheduleHost.MemoryBits & GMemoryInvestigating) != 0)   // 0x102b922e / 0x102b9238
			{
				return SelectTrace(GTroikaFile, 0x6047, GSchedInvestigateOtherSound);   // 0x102b923a
			}
			// `RandomInt(0, 99) < 100` always passes; the draw is retail's and is consumed.
			if (!HasFrenzied(GFrenziedNoFlinchInvestigate)         // 0x102b9255 / 0x102b925f
				&& Roll(99) < 100)                                 // 0x102b926d / 0x102b9273
			{
				return SelectTrace(GTroikaFile, 0x604d, GSchedInvestigateSoundFlinch);  // 0x102b9275
			}
			return SelectTrace(GTroikaFile, 0x6051, GSchedInvestigateSound);            // 0x102b9290
		}
		if (Cond.Has(EElysiumNpcCond::HearWorld))                  // 0x102b909f
		{
			// `m_BestSound (+0x60b0) = m_LastSoundWorld (+0x61e4)`, then
			// `m_InvestigateSound (+0x60dc) = m_BestSound` (`0x102b90a5`..`0x102b91a7`).
			Senses.Memory.BestSound = Senses.Memory.LastSoundWorld;
			Senses.Memory.InvestigateSound = Senses.Memory.BestSound;
			return SelectTrace(GTroikaFile, 0x605c, GSchedInvestigateSound);            // 0x102b91ad
		}
		if (Cond.Has(EElysiumNpcCond::HearPlayer)                  // 0x102b91d3
			|| Cond.Has(EElysiumNpcCond::HearDanger))              // 0x102b91e0
		{
			Senses.CommitBestSound(*this, Cognition.Conditions);   // 0x102b91e8
			// `0x102b91ed` stamps line 0x6063, then TAIL-JUMPS into `0x102b8980`, whose answer IS the
			// schedule (0x4c / 0x4d / 0x51 / 0x52).
			SelectTrace(GTroikaFile, 0x6063, 0);
			return AdvanceAlertLevelGrade();                       // 0x102b9204 JMP 0x100158a7
		}
	}
	if (Cond.Has(EElysiumNpcCond::SeeSoundSource))                 // 0x102b92b6
	{
		if (const int32 Source = SelectSoundSourceSchedule(GSchedRunToSaved, GSchedFleeAndCower);
			Source != 0)                                           // 0x102b92ba PUSH 0x73 / PUSH 0x89 / 0x102b92c8
		{
			return Source;
		}
	}
	if (!HasFrenzied(GFrenziedNoFlinchInvestigate)                 // 0x102b92ca / 0x102b92d4
		&& HasInterrupt(EElysiumNpcCond::HearFlinch))              // 0x102b92da / 0x102b92e1
	{
		Senses.CommitBestSound(*this, Cognition.Conditions);       // 0x102b92e5
		return SelectTrace(GTroikaFile, 0x6074, GSchedInvestigateSoundFlinch);   // 0x102b92ea
	}
	return 0;                                                      // 0x102b9305
}

// =================================================================================================
// `0x102b8d20` — the third-party sound source.
// =================================================================================================

int32 FElysiumNpc::SelectSoundSourceSchedule(int32 HatedAnswer, int32 FearedAnswer)
{
	using namespace NpcSelect19;
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;

	FElysiumEntity* const Source = Resolve(*this, BaseMemory.BestSoundSource);   // 0x102b8d38
	if (Source == nullptr)                                         // 0x102b8d3f -> 0x102b8f9b, no re-arm
	{
		return 0;
	}
	// `source->vtable[+0x29c]()` — slot 167 on whatever the source is; an entity that is no NPC
	// answers `CBaseEntity::GetEnemy`'s NULL.
	const FElysiumNpcBase* const SourceNpc = Source->AsNpcBase();
	FElysiumEntity* const Enemy = SourceNpc != nullptr ? SourceNpc->GetEnemy() : nullptr;   // 0x102b8d51
	if (Enemy != nullptr)                                          // 0x102b8d5b
	{
		if (IRelationType(Enemy) == GDispositionHate)              // 0x102b8d66 / 0x102b8d6f
		{
			// `source+0x94` (`m_pBaseNPC`) against the owners of the last combat (`+0x6160`) and
			// bullet-impact (`+0x618c`) sounds this NPC heard.
			if (SourceNpc != nullptr)                              // 0x102b8d89
			{
				const FElysiumEntity* const CombatOwner = Resolve(*this, Senses.Memory.LastSoundCombat.Source);
				const FElysiumEntity* const BulletOwner = Resolve(*this, Senses.Memory.LastSoundBulletImpact.Source);
				if (Source == CombatOwner || Source == BulletOwner)   // 0x102b8dbe / 0x102b8ded
				{
					// `source->GetEnemies()` (`+0x874`) `0x102dfa20` has-memory / `0x102dfed0` LKP.
					if (const FElysiumNpcEnemyMemoryRecord* Record = SourceNpc->EnemyMemory.Find(Enemy->Handle))   // 0x102b8dfc / 0x102b8e03
					{
						SavePosition = Record->LastPosition;       // 0x102b8e1f..0x102b8e31
						SelectTrace(GTroikaFile, 0x5ff7, HatedAnswer);
						return HatedAnswer;                        // 0x102b8e37 MOV EAX,[ESP+0x28]
					}
				}
			}
			// 0x102b8e5e: `AngleVectors(source->GetAngles())` (slot 221, `0x10139610`) × 128.0 +
			// `source->GetAbsOrigin()` (slot 217). Source units scaled onto this runtime's cm.
			const FVector SourceForward = AngleVectorsForward(Source->Angles);
			SavePosition = Source->Origin + SourceForward * (GSoundSourceLookAhead * ElysiumMove::U);   // 0x102b8f26..0x102b8f32
			SelectTrace(GTroikaFile, 0x6003, HatedAnswer);
			return HatedAnswer;                                    // 0x102b8f17 MOV EAX,[ESP+0x2c]
		}
		if (IRelationType(Enemy) == GDispositionFear)              // 0x102b8f5a / 0x102b8f63
		{
			SelectTrace(GTroikaFile, 0x600c, FearedAnswer);
			return FearedAnswer;                                   // 0x102b8f65 MOV EAX,[ESP+0x30]
		}
	}
	Senses.Memory.NextInvestigateSoundTime = Now + GSoundSourceRetryDelay;   // 0x102b8f87..0x102b8f95
	return 0;                                                      // 0x102b8f9e
}
