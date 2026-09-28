#include "Substrate/ElysiumNpcKernelSpeciesShapeMap.h"

#include "Substrate/ElysiumAiScriptedSchedule.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcAndreiBlood.h"
#include "Substrate/ElysiumNpcAnimal.h"
#include "Substrate/ElysiumNpcAsianVampire.h"
#include "Substrate/ElysiumNpcBach.h"
#include "Substrate/ElysiumNpcCameraSecurity.h"
#include "Substrate/ElysiumNpcChangBros.h"
#include "Substrate/ElysiumNpcCop.h"
#include "Substrate/ElysiumNpcFrenzyShadow.h"
#include "Substrate/ElysiumNpcGargoyle.h"
#include "Substrate/ElysiumNpcGhoulCroucher.h"
#include "Substrate/ElysiumNpcGuard1.h"
#include "Substrate/ElysiumNpcHengeyokai.h"
#include "Substrate/ElysiumNpcHunter.h"
#include "Substrate/ElysiumNpcLasombra.h"
#include "Substrate/ElysiumNpcMaker.h"
#include "Substrate/ElysiumNpcMakerZombie.h"
#include "Substrate/ElysiumNpcManBat.h"
#include "Substrate/ElysiumNpcMingXiao.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcPedestrian.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcScurrying.h"
#include "Substrate/ElysiumNpcSheriffMan.h"
#include "Substrate/ElysiumNpcTaxiDriver.h"
#include "Substrate/ElysiumNpcTzimisce.h"
#include "Substrate/ElysiumNpcTzimisceHeadClaw.h"
#include "Substrate/ElysiumNpcTzimisceRunner.h"
#include "Substrate/ElysiumNpcVampireBoss.h"
#include "Substrate/ElysiumNpcWerewolf.h"
#include "Substrate/ElysiumNpcWolfMorph.h"
#include "Substrate/ElysiumNpcZombie.h"
#include "Substrate/ElysiumScriptedSequence.h"

// One row per record of an introduced species' own datamap, grouped by declaring class and in
// offset order. The datamap replay is the source; the reviewed record was story 5 step 4's
// `fields-step4.tsv`, retired with the migration records (git holds it at `a00cd11b`). Inputs and
// outputs are not words and have no row.
//
// Reading a row: `ELYSIUM_NPC_SPECIES_WORD` compiles the declaring class's member.
// `_NOTED` on `FElysiumNpc` is storage the base still holds for a deferred class or a Troika
// reader.
// `_SHADOW` is a Troika word the species datamap re-declares, bound on the inherited storage.
// `_ABSENT` has no port member and names the word's retail accessors -- a recorded gap.

namespace
{
	constexpr FElysiumNpcSpeciesWordBinding GSpeciesBindings[] =
	{
	// --- CCineAISchedule (story 5 fold A3) ---
	ELYSIUM_NPC_SPECIES_WORD(CCineAISchedule, 0x608c,
		FElysiumAiScriptedSchedule, GoalEntity),  // m_sGoalEnt
	ELYSIUM_NPC_SPECIES_WORD(CCineAISchedule, 0x6090,
		FElysiumAiScriptedSchedule, Mode),  // m_nSchedule
	ELYSIUM_NPC_SPECIES_WORD(CCineAISchedule, 0x6094,
		FElysiumAiScriptedSchedule, ForceState),  // m_nForceState
	// --- CCineNPC (story 5 fold A3). The same offsets are Troika words; this map is per declaring
	// class, so these bind on `CCineNPC`'s descriptor alone. ---
	ELYSIUM_NPC_SPECIES_WORD(CCineNPC, 0x5f44,
		FElysiumScriptedSequence, PreIdle),  // m_iszPreIdle (key m_iszIdle)
	ELYSIUM_NPC_SPECIES_WORD(CCineNPC, 0x5f48,
		FElysiumScriptedSequence, Play),  // m_iszPlay
	ELYSIUM_NPC_SPECIES_WORD(CCineNPC, 0x5f4c,
		FElysiumScriptedSequence, PostIdle),  // m_iszPostIdle
	ELYSIUM_NPC_SPECIES_WORD(CCineNPC, 0x5f50,
		FElysiumScriptedSequence, CustomMove),  // m_iszCustomMove
	ELYSIUM_NPC_SPECIES_WORD(CCineNPC, 0x5f54,
		FElysiumScriptedSequence, TargetEntity),  // m_iszEntity
	ELYSIUM_NPC_SPECIES_WORD(CCineNPC, 0x5f58,
		FElysiumScriptedSequence, NextScript),  // m_iszNextScript
	ELYSIUM_NPC_SPECIES_WORD(CCineNPC, 0x5f5c,
		FElysiumScriptedSequence, LinkedSequenceName),  // m_iszLinkedSequence
	ELYSIUM_NPC_SPECIES_WORD(CCineNPC, 0x5f60,
		FElysiumScriptedSequence, MoveTo),  // m_fMoveTo
	ELYSIUM_NPC_SPECIES_WORD(CCineNPC, 0x5f64,
		FElysiumScriptedSequence, FinishSchedule),  // m_iFinishSchedule
	ELYSIUM_NPC_SPECIES_WORD(CCineNPC, 0x5f68,
		FElysiumScriptedSequence, Radius),  // m_flRadius
	ELYSIUM_NPC_SPECIES_WORD(CCineNPC, 0x5f6c,
		FElysiumScriptedSequence, Repeat),  // m_flRepeat
	ELYSIUM_NPC_SPECIES_WORD(CCineNPC, 0x5f70,
		FElysiumScriptedSequence, Delay),  // m_iDelay
	ELYSIUM_NPC_SPECIES_WORD(CCineNPC, 0x5f74,
		FElysiumScriptedSequence, StartTime),  // m_startTime
	ELYSIUM_NPC_SPECIES_WORD(CCineNPC, 0x5f78,
		FElysiumScriptedSequence, SavedMoveType),  // m_saved_movetype
	ELYSIUM_NPC_SPECIES_WORD(CCineNPC, 0x5f7c,
		FElysiumScriptedSequence, SavedMoveCollide),  // m_saved_movecollide
	ELYSIUM_NPC_SPECIES_WORD(CCineNPC, 0x5f80,
		FElysiumScriptedSequence, SavedSolid),  // m_saved_solid
	ELYSIUM_NPC_SPECIES_WORD(CCineNPC, 0x5f84,
		FElysiumScriptedSequence, SavedSolidFlags),  // m_saved_solidflags
	ELYSIUM_NPC_SPECIES_WORD(CCineNPC, 0x5f88,
		FElysiumScriptedSequence, SavedEffects),  // m_saved_effects
	ELYSIUM_NPC_SPECIES_WORD(CCineNPC, 0x5f8c,
		FElysiumScriptedSequence, SavedTroikaFlags),  // m_saved_troika_flags
	ELYSIUM_NPC_SPECIES_WORD(CCineNPC, 0x5f90,
		FElysiumScriptedSequence, bInterruptable),  // m_interruptable
	ELYSIUM_NPC_SPECIES_WORD(CCineNPC, 0x5f91,
		FElysiumScriptedSequence, bSequenceStarted),  // m_sequenceStarted
	ELYSIUM_NPC_SPECIES_WORD(CCineNPC, 0x5f94,
		FElysiumScriptedSequence, NextCine),  // m_hNextCine
	// --- CNPCMaker (story 5 fold A4). A Troika NPC in retail: its own words start at `+0x665c`,
	// past the Troika's layout, and the Troika words it also authors (`vision`, `pl_*`, ...) bind on
	// the inherited storage through the Troika map. `+0x66cc` (the 4 KB map-data buffer) is not a
	// datamap row. ---
	ELYSIUM_NPC_SPECIES_WORD(CNPCMaker, 0x665c, FElysiumNpcMaker, NpcType),  // m_iszNPCClassname
	ELYSIUM_NPC_SPECIES_WORD(CNPCMaker, 0x6660, FElysiumNpcMaker, RemainingTotal),  // m_iMaxNumNPCs
	ELYSIUM_NPC_SPECIES_WORD(CNPCMaker, 0x6664, FElysiumNpcMaker, SpawnFrequency),  // m_flSpawnFrequency
	ELYSIUM_NPC_SPECIES_WORD(CNPCMaker, 0x66b0, FElysiumNpcMaker, LiveChildren),  // m_cLiveChildren
	ELYSIUM_NPC_SPECIES_WORD(CNPCMaker, 0x66b4, FElysiumNpcMaker, MaxLiveChildren),  // m_iMaxLiveChildren
	ELYSIUM_NPC_SPECIES_WORD(CNPCMaker, 0x66b8, FElysiumNpcMaker, CachedGroundZ),  // m_flGround
	ELYSIUM_NPC_SPECIES_WORD(CNPCMaker, 0x66bc, FElysiumNpcMaker, ChildTargetName),  // m_ChildTargetName
	ELYSIUM_NPC_SPECIES_WORD(CNPCMaker, 0x66c0, FElysiumNpcMaker, bDisabled),  // m_bDisabled
	ELYSIUM_NPC_SPECIES_WORD(CNPCMaker, 0x66c1, FElysiumNpcMaker, bNpcClip),  // m_bNPCClip
	ELYSIUM_NPC_SPECIES_WORD(CNPCMaker, 0x66c2, FElysiumNpcMaker, bFade),  // m_bFade
	ELYSIUM_NPC_SPECIES_WORD(CNPCMaker, 0x66c3, FElysiumNpcMaker, bInfinite),  // m_bInfChild
	ELYSIUM_NPC_SPECIES_WORD(CNPCMaker, 0x66c4, FElysiumNpcMaker, bNoDrop),  // m_bNoDrop
	ELYSIUM_NPC_SPECIES_WORD(CNPCMaker, 0x66c5, FElysiumNpcMaker, bViewCone),  // m_bViewCone
	ELYSIUM_NPC_SPECIES_WORD(CNPCMaker, 0x66c8, FElysiumNpcMaker, MinPcDistance),  // m_iMinPCDistance
	ELYSIUM_NPC_SPECIES_WORD(CNPCMaker, 0x76cc, FElysiumNpcMaker, RefMapDataBuffer),  // m_sRefMapDataBuffer
	// --- CNPCMaker_Zombie (story 5 fold A4). `+0x76d8` is ONE float word, the only one
	// `CanMakeNPC` `0x1034d0a0` compares. ---
	ELYSIUM_NPC_SPECIES_WORD(CNPCMaker_Zombie, 0x76d0,
		FElysiumNpcMakerZombie, ZombieAiSpawnType),  // m_iZombieAISpawnType
	ELYSIUM_NPC_SPECIES_WORD(CNPCMaker_Zombie, 0x76d4,
		FElysiumNpcMakerZombie, bShouldRagdoll),  // m_bShouldRagdoll
	ELYSIUM_NPC_SPECIES_WORD(CNPCMaker_Zombie, 0x76d8,
		FElysiumNpcMakerZombie, RemoveDistance),  // m_flRemoveDist
	// --- CNPC_VAndreiBlood ---
	ELYSIUM_NPC_SPECIES_WORD_NOTED(CNPC_VAndreiBlood, 0x66b8, FElysiumNpc, ActiveRunnerCount,
		"deferred:8 -- the fleshpile maker (FElysiumNpcMakerFleshpile) reads and writes it"),
	ELYSIUM_NPC_SPECIES_WORD_NOTED(CNPC_VAndreiBlood, 0x66bc, FElysiumNpc, AndreiKillCount,
		"deferred:8 -- the fleshpile maker (FElysiumNpcMakerFleshpile) reads and writes it"),
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VAndreiBlood, 0x66c0,
		FElysiumNpcAndreiBlood, AndreiLastTeleportPosition),  // m_vLastTeleportPosition
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VAndreiBlood, 0x66cc,
		FElysiumNpcAndreiBlood, bAndreiActivated),  // m_bActivated
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VAndreiBlood, 0x66cd,
		FElysiumNpcAndreiBlood, bAndreiDead),  // m_bDead, set at 0x1035e84e (Damage19)
	ELYSIUM_NPC_SPECIES_WORD_ABSENT(CNPC_VAndreiBlood, 0x66ce,  // m_bTriggerUnhide
		"no port member: no ported body reads or writes the word (Andrei unhide bodies are "
		"story-8 residue); retail accessors: 0x1035cc20 AndreiBlood::Spawn (residue), 0x1035d1b0 "
		"AndreiBlood::StartTask (task arm), 0x1035d8b0 AndreiBlood::RunTask (task arm)"),
	ELYSIUM_NPC_SPECIES_WORD_ABSENT(CNPC_VAndreiBlood, 0x66d0,  // m_fTeleportWaitStartTime
		"no port member: no ported body reads or writes the word (Andrei teleport wait is "
		"story-8 residue); retail accessors: 0x1035cc20 AndreiBlood::Spawn (residue), 0x1035d1b0 "
		"AndreiBlood::StartTask (task arm), 0x1035d8b0 AndreiBlood::RunTask (task arm)"),
	ELYSIUM_NPC_SPECIES_WORD_ABSENT(CNPC_VAndreiBlood, 0x66d4,  // m_bForceTeleport
		"no port member: the ported reader is a named SEAM answering the retail default "
		"(AndreiBloodSelectSchedule 0x1035d010 SEAM); retail accessors: 0x1035cc20 "
		"AndreiBlood::Spawn (residue), 0x1035d010 AndreiBlood::SelectSchedule (override), "
		"0x1035d8b0 AndreiBlood::RunTask (task arm)"),
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VAndreiBlood, 0x66d8,
		FElysiumNpcAndreiBlood, AndreiHitCounter),  // m_iHitCounter, ++ at 0x1035e83f (Damage19)
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VAndreiBlood, 0x66dc,
		FElysiumNpcAndreiBlood, AndreiHitMax),  // m_iHitMax

	// --- CNPC_VAnimal ---
	ELYSIUM_NPC_SPECIES_WORD_ABSENT(CNPC_VAnimal, 0x665c,  // m_iPlayerFriendshipState
		"no port member: no ported body reads or writes the word (the friendship state machine "
		"is story-8 residue); retail accessors: 0x10374b00 Dog::GatherConditions (residue)"),
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VAnimal, 0x6660,
		FElysiumNpcAnimal, bPlayerAttackedMe),  // m_bPlayerAttackedMe
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VAnimal, 0x6664,
		FElysiumNpcAnimal, AnimalFriendshipLevel),  // m_iFriendshipLevel
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VAnimal, 0x6668,
		FElysiumNpcAnimal, AnimalWarnRangeUnits),  // m_flWarnRange
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VAnimal, 0x666c,
		FElysiumNpcAnimal, AnimalConflictRangeUnits),  // m_flConflictRange

	// --- CNPC_VAsianVampire ---
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VAsianVampire, 0x66b8,
		FElysiumNpcAsianVampire, LastJumpPosition),  // m_vLastJumpPosition
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VAsianVampire, 0x66d0,
		FElysiumNpcAsianVampire, LastJumpPositionIdx),  // m_iLastJumpPositionIdx
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VAsianVampire, 0x66d4,
		FElysiumNpcAsianVampire, bAsianVampirePathBlocked),  // m_bPathBlocked
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VAsianVampire, 0x66d8,
		FElysiumNpcAsianVampire, MovedTimeStamp),  // m_fMovedTimeStamp
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VAsianVampire, 0x66dc,
		FElysiumNpcAsianVampire, MovedPosition),  // m_vMovedPosition
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VAsianVampire, 0x66e8,
		FElysiumNpcAsianVampire, bSuppressRanged),  // m_bSuppressRanged

	// --- CNPC_VBach ---
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VBach, 0x6664,
		FElysiumNpcBach, BachLastOccludeOriginUnits),  // m_vecLastOccludeOrigin
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VBach, 0x6670,
		FElysiumNpcBach, BachOccludeEnterTime),  // m_flOccludeEnterTime
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VBach, 0x6674,
		FElysiumNpcBach, BachWasOccluded),  // m_iWasOccluded
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VBach, 0x6678,
		FElysiumNpcBach, BachReusedOccludeCount),  // m_iReusedOccludeCount
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VBach, 0x667c,
		FElysiumNpcBach, BachGrenadeActive),  // m_iGrenadeActive
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VBach, 0x6680,
		FElysiumNpcBach, BachLastGrenadeTime),  // m_flLastGrenadeTime
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VBach, 0x6684,
		FElysiumNpcBach, BachShieldTime),  // m_flShieldTime
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VBach, 0x6688,
		FElysiumNpcBach, BachNextShieldTime),  // m_flNextShieldTime
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VBach, 0x668c,
		FElysiumNpcBach, BachNextWeaponSwitchTime),  // m_flNextWeaponSwitchTime
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VBach, 0x6690,
		FElysiumNpcBach, BachNextHolyLightTime),  // m_flNextHolyLightTime
	ELYSIUM_NPC_SPECIES_WORD_ABSENT(CNPC_VBach, 0x6694,  // m_flWarningTime
		"no port member: no ported body reads or writes the word (Bach warning bodies are "
		"story-8 residue); retail accessors: 0x10363850 Bach::Spawn (residue), 0x103645a0 "
		"Bach::StartTask (task arm), 0x103652b0 Bach::RunTask (task arm)"),
	ELYSIUM_NPC_SPECIES_WORD_ABSENT(CNPC_VBach, 0x6698,  // m_flSkipToWarningTime
		"no port member: no ported body reads or writes the word (Bach warning bodies are "
		"story-8 residue); retail accessors: 0x10363850 Bach::Spawn (residue), 0x103645a0 "
		"Bach::StartTask (task arm)"),
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VBach, 0x669c,
		FElysiumNpcBach, BachTeleportState),  // m_iBachTeleportState
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VBach, 0x66a0,
		FElysiumNpcBach, bBachCamperFlag),  // m_bCamperFlag
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VBach, 0x66a1,
		FElysiumNpcBach, bBachInStartingPosition),  // m_bBachInStartingPosition
	ELYSIUM_NPC_SPECIES_WORD_ABSENT(CNPC_VBach, 0x66a2,  // m_bSkipToWarning
		"no port member: no ported body reads or writes the word (Bach warning bodies are "
		"story-8 residue); retail accessors: 0x10363850 Bach::Spawn (residue), 0x103645a0 "
		"Bach::StartTask (task arm)"),
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VBach, 0x66a3,
		FElysiumNpcBach, bBachFireOccluded),  // m_bFireOccluded
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VBach, 0x66a5,
		FElysiumNpcBach, bBachShieldActive),  // m_bShieldActive
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VBach, 0x66a7,
		FElysiumNpcBach, bBachMovementSpot),  // m_bMovementSpot
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VBach, 0x66a8, FElysiumNpcBach, bCanFightYet),  // m_bCanFightYet

	// --- CNPC_VCameraSecurity ---
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VCameraSecurity, 0x6660,
		FElysiumNpcCameraSecurity, LinkedCameraName),  // m_iszLinkedCamera

	// --- CNPC_VChangBros ---
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VChangBros, 0x66b8,
		FElysiumNpcChangBros, ChangType),  // m_ChangType
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VChangBros, 0x66bc,
		FElysiumNpcChangBros, ChangLastTeleportPosition),  // m_vLastTeleportPosition
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VChangBros, 0x66c8,
		FElysiumNpcChangBros, ChangLastTeleportTime),  // m_fLastTeleportTime
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VChangBros, 0x66cc,
		FElysiumNpcChangBros, LastJumpTime),  // m_fLastJumpTime
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VChangBros, 0x66d0,
		FElysiumNpcChangBros, FacingTime),  // m_fFacingTime
	ELYSIUM_NPC_SPECIES_WORD_ABSENT(CNPC_VChangBros, 0x66d4,  // m_fUnitedTime
		"no port member: no ported body reads or writes the word (the united-attack timer bodies "
		"are story-8 residue); retail accessors: 0x1036b750 ChangBros::StartTask (task arm), "
		"0x1036bfc0 ChangBros::RunTask (task arm)"),
	ELYSIUM_NPC_SPECIES_WORD_ABSENT(CNPC_VChangBros, 0x66d8,  // m_bEnergyBallSpawned
		"no port member: no ported body reads or writes the word (the energy-ball bodies are "
		"story-8 residue); retail accessors: 0x1036b750 ChangBros::StartTask (task arm), "
		"0x1036bfc0 ChangBros::RunTask (task arm)"),
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VChangBros, 0x66dc,
		FElysiumNpcChangBros, ChangArenaCenter),  // m_vArenaCenter
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VChangBros, 0x66e8,
		FElysiumNpcChangBros, bChangCenterStored),  // m_bCenterStored
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VChangBros, 0x66ec,
		FElysiumNpcChangBros, ChangLastUnitedAttackTime),  // m_fLastUnitedAttackTime
	ELYSIUM_NPC_SPECIES_WORD_ABSENT(CNPC_VChangBros, 0x66f0,  // m_fEnergyChargeTime
		"no port member: no ported body reads or writes the word (the energy-charge bodies are "
		"story-8 residue); retail accessors: 0x1036b750 ChangBros::StartTask (task arm), "
		"0x1036bfc0 ChangBros::RunTask (task arm)"),
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VChangBros, 0x66f4,
		FElysiumNpcChangBros, ChangCenterEmitter),  // m_hCenterEmitter

	// --- CNPC_VCop ---
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VCop, 0x6664,
		FElysiumNpcCop, CopPursuitHandle),  // m_hPursuitPlayer
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VCop, 0x6668,
		FElysiumNpcCop, CopOldPlayerRelationType),  // m_eOldPlayerRelationType
	ELYSIUM_NPC_SPECIES_WORD_ABSENT(CNPC_VCop, 0x666c,  // m_iOldPlayerRelationPriority
		"no port member: no ported body reads or writes the word (the pursuit relationship "
		"save/restore is story-8 residue); retail accessors: none recorded"),
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VCop, 0x6670,
		FElysiumNpcCop, bWasEverInCombat),  // m_bWasEverInCombat
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VCop, 0x6671,
		FElysiumNpcCop, bCopCountedAlive),  // m_bCountedAlive

	// --- CNPC_VFrenzyShadow ---
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VFrenzyShadow, 0x6664,
		FElysiumNpcFrenzyShadow, HostileEnemyCount),  // m_iHostileEnemyCount
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VFrenzyShadow, 0x6668,
		FElysiumNpcFrenzyShadow, bFailedGrapple),  // m_bFailedGrapple
	// --- CNPC_VGargoyle ---
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VGargoyle, 0x667c,
		FElysiumNpcGargoyle, GargoylePillarTarget),  // m_hPillarTarget
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VGargoyle, 0x6680,
		FElysiumNpcGargoyle, GargoyleShunnedFindPillar),  // m_iShunnedFindPillar
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VGargoyle, 0x6684,
		FElysiumNpcGargoyle, GargoyleDoingGibDeath),  // m_iDoingGibDeath
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VGargoyle, 0x6688,
		FElysiumNpcGargoyle, GargoyleCanKnockback),  // m_iCanKnockback

	// --- CNPC_VGhoulCroucher ---
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VGhoulCroucher, 0x6664,
		FElysiumNpcGhoulCroucher, bGhoulSpawnDisturbed),  // m_bSpawnDisturbed
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VGhoulCroucher, 0x6665,
		FElysiumNpcGhoulCroucher, bGhoulSpawnBurning),  // m_bSpawnBurning
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VGhoulCroucher, 0x6666,
		FElysiumNpcGhoulCroucher, bWasDisturbed),  // m_bWasDisturbed
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VGhoulCroucher, 0x6667,
		FElysiumNpcGhoulCroucher, bUnawareExited),  // m_bUnawareExited
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VGhoulCroucher, 0x6668,
		FElysiumNpcGhoulCroucher, UnawareType),  // m_nUnawareType
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VGhoulCroucher, 0x666c,
		FElysiumNpcGhoulCroucher, GhoulNextTouchBurnTime),  // m_flNextTouchBurnTime
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VGhoulCroucher, 0x6670,
		FElysiumNpcGhoulCroucher, BurningParticle),  // m_hBurningParticle

	// --- CNPC_VGuard1 ---
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VGuard1, 0x6660,
		FElysiumNpcGuard1, bGuard1HatesPlayer),  // m_fHatesPlayer

	// --- CNPC_VHengeyokai ---
	// m_flIgnoreCollisionTimer
	ELYSIUM_NPC_SPECIES_WORD_SHADOW(CNPC_VHengeyokai, 0x6458, IgnoreCollisionUntil),
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VHengeyokai, 0x6664,
		FElysiumNpcHengeyokai, HengeyokaiPickupTarget),  // m_hPickupTarget
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VHengeyokai, 0x6668,
		FElysiumNpcHengeyokai, HengeyokaiPickupTargetGrabBone),  // m_iPickupTargetGrabBone
	ELYSIUM_NPC_SPECIES_WORD_NOTED(CNPC_VHengeyokai, 0x666c, FElysiumNpc, HengeyokaiFishTimer,
		"stay:troika reader -- an activity predicate reads it"),
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VHengeyokai, 0x6670,
		FElysiumNpcHengeyokai, HengeyokaiShunnedFishTimer),  // m_flShunnedFishTimer
	ELYSIUM_NPC_SPECIES_WORD_ABSENT(CNPC_VHengeyokai, 0x6674,  // m_flTaskFailTimer
		"no port member: no ported body reads or writes the word (the task-fail timer bodies are "
		"story-8 residue); retail accessors: 0x103805d0 Hengeyokai::StartTask (task arm), "
		"0x10380cb0 Hengeyokai::RunTask (task arm)"),
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VHengeyokai, 0x6678,
		FElysiumNpcHengeyokai, HengeyokaiShunnedFindFish),  // m_iShunnedFindFish
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VHengeyokai, 0x667c,
		FElysiumNpcHengeyokai, bHengeyokaiJustFoundFish),  // m_bJustFoundFish
	ELYSIUM_NPC_SPECIES_WORD_NOTED(CNPC_VHengeyokai, 0x667d, FElysiumNpc, bHengeyokaiDidFakeThrow,
		"stay:troika reader -- an activity predicate reads it"),
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VHengeyokai, 0x6684,
		FElysiumNpcHengeyokai, HengeyokaiPickupTargetPos),  // m_vecPickupTargetPos
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VHengeyokai, 0x6690,
		FElysiumNpcHengeyokai, HengeyokaiPhysicsAnimlink),  // m_hPhysicsAnimlink
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VHengeyokai, 0x6694,
		FElysiumNpcHengeyokai, bHengeyokaiInSharkForm),  // m_bInSharkForm

	// --- CNPC_VHunter ---
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VHunter, 0x6664,
		FElysiumNpcHunter, HunterPursuitPlayer),  // m_hPursuitPlayer

	// --- CNPC_VLasombra ---
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VLasombra, 0x6664,
		FElysiumNpcLasombra, LasombraCoverDisableOverride),  // m_flCoverDisableOverride

	// --- CNPC_VManBat ---
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VManBat, 0x6664,
		FElysiumNpcManBat, bManBatReachedMoveGoal),  // m_bReachedMoveGoal
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VManBat, 0x6668,
		FElysiumNpcManBat, ManBatMoveGoalNodeMode),  // m_iMoveGoalNodeMode
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VManBat, 0x6674,
		FElysiumNpcManBat, ManBatMoveGoalNodeId),  // m_iMoveGoalNodeID
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VManBat, 0x6678,
		FElysiumNpcManBat, ManBatFlapTimer),  // m_flFlapTimer
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VManBat, 0x667c,
		FElysiumNpcManBat, ManBatFlyTimer),  // m_flFlyTimer
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VManBat, 0x6680,
		FElysiumNpcManBat, ManBatSlowedEntity),  // m_hSlowedEntity
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VManBat, 0x6684,
		FElysiumNpcManBat, ManBatSlowedExpire),  // m_flSlowedExpire
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VManBat, 0x6688, FElysiumNpcManBat, ManBatFlyNode),  // m_pFlyNode
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VManBat, 0x668c,
		FElysiumNpcManBat, ManBatPickupTarget),  // m_hPickupTarget
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VManBat, 0x6690,
		FElysiumNpcManBat, ManBatPhysicsAnimlink),  // m_hPhysicsAnimlink
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VManBat, 0x6694,
		FElysiumNpcManBat, bManBatPickupTargetBreakable),  // m_bPickupTargetBreakable
	ELYSIUM_NPC_SPECIES_WORD_ABSENT(CNPC_VManBat, 0x6698,  // m_iMaxScriptNode
		"no port member: no ported body reads or writes the word (the script-node fly path is "
		"story-8 residue); retail accessors: 0x1038d130 ManBat::RunTask (task arm), 0x1038fa90 "
		"ManBat::InputManBatFlyBegin (no override row)"),
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VManBat, 0x66ac,
		FElysiumNpcManBat, ManBatFlyByTarget),  // m_hFlyByTarget
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VManBat, 0x66b0,
		FElysiumNpcManBat, bManBatHasScaredMinions),  // m_bHasScaredMinions
	ELYSIUM_NPC_SPECIES_WORD_ABSENT(CNPC_VManBat, 0x66b4,  // m_flCoastTimer
		"no port member: no ported body reads or writes the word (the coast bodies are story-8 "
		"residue); retail accessors: 0x1038c390 ManBat::StartTask (task arm), 0x1038d130 "
		"ManBat::RunTask (task arm)"),
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VManBat, 0x66b8,
		FElysiumNpcManBat, bHasPlayedFlyBySound),  // m_bHasPlayedFlyBySound

	// --- CNPC_VMingXiao ---
	ELYSIUM_NPC_SPECIES_WORD_ABSENT(CNPC_VMingXiao, 0x6670,  // m_hParentMingZhao
		"no port member: no ported body reads or writes the word (the Ming Zhao parent link is "
		"story-8 residue); retail accessors: 0x103927a0 MingXiao::Spawn (residue), 0x10395ba0 "
		"MingXiao::Event_Killed (residue)"),
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiao, 0x6674,
		FElysiumNpcMingXiao, MingXiaoTentacleId),  // m_iTentacleID
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiao, 0x6678,
		FElysiumNpcMingXiao, bMingXiaoHasTransformed),  // m_bHasTransformed
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiao, 0x667c,
		FElysiumNpcMingXiao, MingXiaoMeleeWeapon),  // m_hMeleeWeapon
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiao, 0x6680,
		FElysiumNpcMingXiao, MingXiaoRangedWeapon),  // m_hRangedWeapon
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiao, 0x6684,
		FElysiumNpcMingXiao, bProxyRegistered),  // m_rbProxyRegistered[6]
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiao, 0x668c, FElysiumNpcMingXiao, Proxies),  // m_rhProxies
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiao, 0x66a4,
		FElysiumNpcMingXiao, MingXiaoProxyReadyTimer),  // m_flProxyReadyTimer
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiao, 0x66a8,
		FElysiumNpcMingXiao, SeveredTentacles),  // m_rhSeveredTentacles
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiao, 0x66c0,
		FElysiumNpcMingXiao, MingXiaoSpitAttackTimer),  // m_flSpitAttackTimer
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiao, 0x66c4,
		FElysiumNpcMingXiao, MingXiaoAttackTimers),  // m_rflAttackTimers
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiao, 0x66dc,
		FElysiumNpcMingXiao, MingXiaoHitPoints),  // m_rflHitPoints (story 8 lane L12)
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiao, 0x66f4,
		FElysiumNpcMingXiao, MingXiaoRegrowTimers),  // m_rflRegrowTimers
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiao, 0x670c,
		FElysiumNpcMingXiao, MingXiaoConnectedTentacleCount),  // m_iConnectedTentacleCount
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiao, 0x6710,
		FElysiumNpcMingXiao, MingXiaoSeveredTentacleMask),  // m_iSeveredTentacleMask
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiao, 0x6714,
		FElysiumNpcMingXiao, MingXiaoLastLostTentacle),  // m_eLastLostTentacle (story 8 lane L12)
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiao, 0x6718,
		FElysiumNpcMingXiao, MingXiaoThrowObject),  // m_hThrowObject
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiao, 0x671c,
		FElysiumNpcMingXiao, MingXiaoThrowingTentacle),  // m_eThrowingTentacle
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiao, 0x6720,
		FElysiumNpcMingXiao, MingXiaoPickupTargetPos),  // m_vecPickupTargetPos
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiao, 0x672c,
		FElysiumNpcMingXiao, MingXiaoPickupSavedForward),  // m_vecPickupSavedForward
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiao, 0x6738,
		FElysiumNpcMingXiao, MingXiaoPhysicsAnimlink),  // m_hPhysicsAnimlink
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiao, 0x673c,
		FElysiumNpcMingXiao, MingXiaoThrowableObjectMode),  // m_eThrowableObjectMode
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiao, 0x6740,
		FElysiumNpcMingXiao, CoordinateTentacleId),  // m_iCoordinateTentacleID
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiao, 0x6744,
		FElysiumNpcMingXiao, bMingXiaoPlayedDeathAnim),  // m_bPlayedDeathAnim (story 8 lane L12)
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiao, 0x6748,
		FElysiumNpcMingXiao, MingXiaoIdealRange),  // m_flIdealRange (story 8 lane L12)
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiao, 0x674c,
		FElysiumNpcMingXiao, MingXiaoChargeReadyTime),  // m_flChargeReadyTime
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiao, 0x6750,
		FElysiumNpcMingXiao, bBlockedByFriend),  // m_bBlockedByFriend

	// --- CNPC_VMingXiaoTentacle ---
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiaoTentacle, 0x665c,
		FElysiumNpcMingXiaoTentacle, TentacleMingXiao),  // m_hMingXiao
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiaoTentacle, 0x6660,
		FElysiumNpcMingXiaoTentacle, TentacleId),  // m_iTentacleID
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiaoTentacle, 0x6664,
		FElysiumNpcMingXiaoTentacle, ModeIndexTentacleToGrub),  // m_iModeIndexTentacleToGrub
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiaoTentacle, 0x6668,
		FElysiumNpcMingXiaoTentacle, ModeIndexGrub),  // m_iModeIndexGrub
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiaoTentacle, 0x666c,
		FElysiumNpcMingXiaoTentacle, ModeIndexGrubToProxy),  // m_iModeIndexGrubToProxy
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiaoTentacle, 0x6670,
		FElysiumNpcMingXiaoTentacle, TentaclePhase),  // m_ePhase
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiaoTentacle, 0x6674,
		FElysiumNpcMingXiaoTentacle, MingXiaoTentaclePhaseExpireTimer),  // m_flPhaseExpireTimer
	ELYSIUM_NPC_SPECIES_WORD_ABSENT(CNPC_VMingXiaoTentacle, 0x6678,  // m_flFailedEvadeTimer
		"no port member: no ported body reads or writes the word (the evade bodies are story-8 "
		"residue); retail accessors: 0x1039c380 MingXiaoTentacle::Spawn (residue), 0x1039de20 "
		"MingXiaoTentacle::SelectSchedule (override), 0x1039ec10 "
		"MingXiaoTentacle::GatherConditions (residue)"),
	ELYSIUM_NPC_SPECIES_WORD_ABSENT(CNPC_VMingXiaoTentacle, 0x667c,  // m_flUpdateEvadeTimer
		"no port member: no ported body reads or writes the word (the evade bodies are story-8 "
		"residue); retail accessors: 0x1039c380 MingXiaoTentacle::Spawn (residue), 0x1039c4c0 "
		"MingXiaoTentacle::StartTask (task arm), 0x1039e3d0 MingXiaoTentacle::RunAI (residue)"),
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiaoTentacle, 0x6680,
		FElysiumNpcMingXiaoTentacle, TentacleHideReadyTimer),  // m_flHideReadyTimer, zeroed at 0x1039e89a (Damage19)
	ELYSIUM_NPC_SPECIES_WORD_ABSENT(CNPC_VMingXiaoTentacle, 0x6684,  // m_flIgnoreCollisionTimer
		"no port member: no ported body reads or writes the word (the tentacle's own collision "
		"timer (not Troika's +0x6458); its bodies are story-8 residue); retail accessors: none "
		"recorded"),
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiaoTentacle, 0x6688,
		FElysiumNpcMingXiaoTentacle, bIgnoreCollisionSpecies),  // m_bIgnoreCollision
	ELYSIUM_NPC_SPECIES_WORD_NOTED(CNPC_VMingXiaoTentacle, 0x668c, FElysiumNpc,
		TentacleScatterCenterUnits,
		"stay:troika reader -- written by NotifyScatterCenter, a Troika notification"),
	ELYSIUM_NPC_SPECIES_WORD_ABSENT(CNPC_VMingXiaoTentacle, 0x6698,  // m_bHitGroundSound
		"no port member: no ported body reads or writes the word (the ground-hit sound bodies "
		"are story-8 residue); retail accessors: 0x1039c380 MingXiaoTentacle::Spawn (residue), "
		"0x1039e3d0 MingXiaoTentacle::RunAI (residue)"),
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VMingXiaoTentacle, 0x6699,
		FElysiumNpcMingXiaoTentacle, bTentaclePlayedDeathAnim),  // m_bPlayedDeathAnim (story 8 lane L12)

	// --- CNPC_VPedestrian ---
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VPedestrian, 0x6660,
		FElysiumNpcPedestrian, PedestrianPreDeathMinsUnits),  // m_vecPreDeathMins
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VPedestrian, 0x666c,
		FElysiumNpcPedestrian, PedestrianPreDeathMaxsUnits),  // m_vecPreDeathMaxs
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VPedestrian, 0x6678,
		FElysiumNpcPedestrian, bPedestrianFirstThink),  // m_bFirstThink
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VPedestrian, 0x667c,
		FElysiumNpcPedestrian, PedestrianLevelResetType),  // m_eLevelResetType

	// --- CNPC_VSabbatLeader ---
	ELYSIUM_NPC_SPECIES_WORD_SHADOW(CNPC_VSabbatLeader, 0x6498, bJumping),  // m_bJumping
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VSabbatLeader, 0x66b8,
		FElysiumNpcSabbatLeader, bSabbatLeaderActivated),  // m_bActivated
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VSabbatLeader, 0x66bc,
		FElysiumNpcSabbatLeader, SabbatLeaderRouteFailCount),  // m_RouteFailCount
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VSabbatLeader, 0x66c0,
		FElysiumNpcSabbatLeader, FailureType),  // m_FailureType
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VSabbatLeader, 0x66c4,
		FElysiumNpcSabbatLeader, SabbatLastWaterLevel),  // m_nLastWaterLevel
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VSabbatLeader, 0x66c8,
		FElysiumNpcSabbatLeader, SabbatLastSplashTime),  // m_fLastSplashTime
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VSabbatLeader, 0x66cc,
		FElysiumNpcSabbatLeader, SabbatLastPlayerHealth),  // m_LastPlayerHealth
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VSabbatLeader, 0x66d4,
		FElysiumNpcSabbatLeader, bSabbatLeaderTrackPlayer),  // m_bTrackPlayer
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VSabbatLeader, 0x66d5,
		FElysiumNpcSabbatLeader, bSabbatDiving),  // m_bDiving
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VSabbatLeader, 0x66d6,
		FElysiumNpcSabbatLeader, bSabbatLeaderLargeSplash),  // m_bLargeSplashSpawned
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VSabbatLeader, 0x66d8,
		FElysiumNpcSabbatLeader, SabbatLeaderJumpBloodBalance),  // m_JumpBloodBalanceCount
	ELYSIUM_NPC_SPECIES_WORD_ABSENT(CNPC_VSabbatLeader, 0x66dc,  // m_fWarningFinishTime
		"no port member: no ported body reads or writes the word (the warning bodies are story-8 "
		"residue); retail accessors: 0x103a78c0 SabbatLeader::StartTask (task arm), 0x103a8990 "
		"SabbatLeader::RunTask (task arm)"),
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VSabbatLeader, 0x66e0,
		FElysiumNpcSabbatLeader, SabbatLeaderRoarAttackCount),  // m_RoarAttackCount
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VSabbatLeader, 0x66e4,
		FElysiumNpcSabbatLeader, bSabbatLeaderParticleSpawned),  // m_bParticleSpawned
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VSabbatLeader, 0x66e5,
		FElysiumNpcSabbatLeader, bSabbatLeaderLastAttackWasNova),  // m_bLastAttackWasNova

	// --- CNPC_VScurrying ---
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VScurrying, 0x6690,
		FElysiumNpcScurrying, ScurryingDetectionDistanceUnits),  // m_flDetectionDistance
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VScurrying, 0x6694,
		FElysiumNpcScurrying, bScurryingIgnoreNosferatu),  // m_fIgnoreNosferatu
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VScurrying, 0x6695,
		FElysiumNpcScurrying, bScurryingMustDetect),  // m_fMustDetect
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VScurrying, 0x6698,
		FElysiumNpcScurrying, ScurryingFrightDistanceUnits),  // m_flFrightDistance
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VScurrying, 0x669c,
		FElysiumNpcScurrying, ScurryingFrightDurationSeconds),  // m_flFrightDurationSeconds

	// --- CNPC_VSheriffMan ---
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VSheriffMan, 0x66d0,
		FElysiumNpcSheriffMan, SheriffTeleportSwarm),  // m_hTeleportSwarm
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VSheriffMan, 0x66d4,
		FElysiumNpcSheriffMan, SheriffLastTeleportPosition),  // m_vLastTeleportPosition
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VSheriffMan, 0x66e0,
		FElysiumNpcSheriffMan, SheriffLastTeleportTime),  // m_fLastTeleportTime
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VSheriffMan, 0x66e4,
		FElysiumNpcSheriffMan, bSheriffTeleporting),  // m_bTeleporting
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VSheriffMan, 0x66e5,
		FElysiumNpcSheriffMan, bSheriffDead),  // m_bDead
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VSheriffMan, 0x66e6,
		FElysiumNpcSheriffMan, bSheriffActivated),  // m_bActivated
	ELYSIUM_NPC_SPECIES_WORD_ABSENT(CNPC_VSheriffMan, 0x66f0,  // m_fTeleportSpeed
		"no port member: no ported body reads or writes the word (the teleport-speed bodies are "
		"story-8 residue); retail accessors: 0x103b1860 SheriffMan::SetTeleportVelocity (no "
		"override row)"),
	ELYSIUM_NPC_SPECIES_WORD_ABSENT(CNPC_VSheriffMan, 0x66f4,  // m_fTeleportStartTime
		"no port member: no ported body reads or writes the word (the teleport-start bodies are "
		"story-8 residue); retail accessors: none recorded"),

	// --- CNPC_VTaxiDriver ---
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VTaxiDriver, 0x6660,
		FElysiumNpcTaxiDriver, bTaxiFirstThink),  // m_bFirstThink

	// --- CNPC_VTzimisce ---
	// m_flIgnoreCollisionTimer
	ELYSIUM_NPC_SPECIES_WORD_SHADOW(CNPC_VTzimisce, 0x6458, IgnoreCollisionUntil),
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VTzimisce, 0x6670,
		FElysiumNpcTzimisce, PickupTarget),  // m_hPickupTarget
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VTzimisce, 0x6674,
		FElysiumNpcTzimisce, PickupTargetPos),  // m_vecPickupTargetPos
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VTzimisce, 0x6680,
		FElysiumNpcTzimisce, TzimiscePickupGrabBone),  // m_iPickupTargetGrabBone
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VTzimisce, 0x6684,
		FElysiumNpcTzimisce, TzimiscePhysicsAnimlink),  // m_hPhysicsAnimlink
	ELYSIUM_NPC_SPECIES_WORD_NOTED(CNPC_VTzimisce, 0x6688, FElysiumNpc, bHeavyBodyTarget,
		"stay:troika reader -- an activity predicate reads it"),
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VTzimisce, 0x6689,
		FElysiumNpcTzimisce, bTzimisceFirstEnemy),  // m_bFirstEnemy
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VTzimisce, 0x668c, FElysiumNpcTzimisce, PathMode),  // m_ePathMode
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VTzimisce, 0x66a4,
		FElysiumNpcTzimisce, TzimisceBodyTimer),  // m_flBodyTimer
	ELYSIUM_NPC_SPECIES_WORD_ABSENT(CNPC_VTzimisce, 0x66a8,  // m_flTaskFailTimer
		"no port member: no ported body reads or writes the word (the task-fail timer bodies are "
		"story-8 residue); retail accessors: 0x103ba7c0 Tzimisce::StartTask (task arm), "
		"0x103bb1e0 Tzimisce::RunTask (task arm)"),
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VTzimisce, 0x66ac,
		FElysiumNpcTzimisce, TzimiscePounceCheckTimer),  // m_flPounceCheckTimer
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VTzimisce, 0x66b0,
		FElysiumNpcTzimisce, TzimisceShunnedBodyTimer),  // m_flShunnedBodyTimer
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VTzimisce, 0x66b4,
		FElysiumNpcTzimisce, bTzimisceDidFakeThrow),  // m_bDidFakeThrow
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VTzimisce, 0x66b8,
		FElysiumNpcTzimisce, TzimisceShunnedFindBody),  // m_iShunnedFindBody
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VTzimisce, 0x66bc,
		FElysiumNpcTzimisce, bTzimisceJustFoundBody),  // m_bJustFoundBody

	// --- CNPC_VTzimisceHeadClaw ---
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VTzimisceHeadClaw, 0x6674,
		FElysiumNpcTzimisceHeadClaw, HeadClawSlowedEntity),  // m_hSlowedEntity
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VTzimisceHeadClaw, 0x6678,
		FElysiumNpcTzimisceHeadClaw, HeadClawSlowedExpire),  // m_flSlowedExpire

	// --- CNPC_VTzimisceRunner ---
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VTzimisceRunner, 0x6671,
		FElysiumNpcTzimisceRunner, bRunnerDeathNoticeProcessed),  // m_bDeathNoticeProcessed
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VTzimisceRunner, 0x6678,
		FElysiumNpcTzimisceRunner, RunnerPotentialEnemy),  // m_hPotentialEnemy

	// --- CNPC_VVampireBoss ---
	ELYSIUM_NPC_SPECIES_WORD_SHADOW(CNPC_VVampireBoss, 0x6498, bJumping),  // m_bJumping
	ELYSIUM_NPC_SPECIES_WORD_SHADOW(CNPC_VVampireBoss, 0x649c, JumpOrigin),  // m_vJumpOrigin
	ELYSIUM_NPC_SPECIES_WORD_SHADOW(CNPC_VVampireBoss, 0x64a8, JumpTarget),  // m_vJumpTarget
	ELYSIUM_NPC_SPECIES_WORD_SHADOW(CNPC_VVampireBoss, 0x64b4, JumpHeight),  // m_fJumpHeight
	ELYSIUM_NPC_SPECIES_WORD_SHADOW(CNPC_VVampireBoss, 0x64b8, JumpGravity),  // m_fJumpGravity
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VVampireBoss, 0x667c,
		FElysiumNpcVampireBoss, VampireBossMorphModelName),  // m_MorphModelName
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VVampireBoss, 0x6680,
		FElysiumNpcVampireBoss, VampireBossMonsterModelName),  // m_pMonsterModelName
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VVampireBoss, 0x6684,
		FElysiumNpcVampireBoss, BodyEmitterNames),  // m_pBodyEmitterNames
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VVampireBoss, 0x6698,
		FElysiumNpcVampireBoss, BossHealthPercentRecord),  // m_HealthPercentRecord
	ELYSIUM_NPC_SPECIES_WORD_ABSENT(CNPC_VVampireBoss, 0x669c,  // m_fTaskStartTime
		"no port member: no ported body reads or writes the word (the task-start stamp bodies "
		"are story-8 residue); retail accessors: 0x103a8990 SabbatLeader::RunTask (task arm), "
		"0x103c5ac0 VampireBoss::StartTask (task arm)"),
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VVampireBoss, 0x66a0,
		FElysiumNpcVampireBoss, ParticleEmitters),  // m_hParticleEmitters
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VVampireBoss, 0x66b0,
		FElysiumNpcVampireBoss, TransformPartner),  // m_hTransformPartner

	// --- CNPC_VWerewolf ---
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VWerewolf, 0x6680,
		FElysiumNpcWerewolf, WerewolfDoorState),  // m_DoorState
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VWerewolf, 0x66b0,
		FElysiumNpcWerewolf, TeleportHintNode),  // m_pTeleportHint
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VWerewolf, 0x66b4,
		FElysiumNpcWerewolf, WerewolfLastUsedTeleportHint),  // m_pLastUsedTeleportHint
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VWerewolf, 0x66bc,
		FElysiumNpcWerewolf, MoveHintNode),  // m_pMoveHint
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VWerewolf, 0x66c0,
		FElysiumNpcWerewolf, WerewolfLastUsedMoveHint),  // m_pLastUsedMoveHint
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VWerewolf, 0x66c4,
		FElysiumNpcWerewolf, WerewolfBreakHintNode),  // m_pBreakHint
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VWerewolf, 0x66c8,
		FElysiumNpcWerewolf, bRandomHint),  // m_bRandomHint
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VWerewolf, 0x66f0,
		FElysiumNpcWerewolf, WerewolfTimeTeleportedOut),  // m_flTimeTeleportedOut

	// --- CNPC_VWolfMorph ---
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VWolfMorph, 0x6664,
		FElysiumNpcWolfMorph, bFirstThink),  // m_bFirstThink
	ELYSIUM_NPC_SPECIES_WORD_ABSENT(CNPC_VWolfMorph, 0x6668,  // m_flNextFleeSoundTime
		"no port member: its SAVE name is the Troika's m_flNextFleeSoundTime (+0x641c), and the "
		"registry's save walk keeps one row per name, so binding it would stop the Troika word "
		"saving on this class; no CNPC_VWolfMorph body reads or writes it (fold A2 walked all "
		"eleven own bodies)"),
	// --- CNPC_VZombie ---
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VZombie, 0x6675,
		FElysiumNpcZombie, bZombieShouldRagdoll),  // m_bShouldRagdoll
	ELYSIUM_NPC_SPECIES_WORD_NOTED(CNPC_VZombie, 0x6678, FElysiumNpc, ZombieAiType,
		"deferred:8 -- the zombie maker writes it through SetZombieAIType 0x1034cf20"),
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VZombie, 0x667c,
		FElysiumNpcZombie, bZombieNeedsCrawlOutOfGround),  // m_iNeedsCrawlOutOfGround
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VZombie, 0x6680,
		FElysiumNpcZombie, ZombieDeathForceVector),  // m_vecDeathForceVector
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VZombie, 0x66d8,
		FElysiumNpcZombie, ZombieGrappleReadyTimer),  // m_flGrappleReadyTimer
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VZombie, 0x66dc,
		FElysiumNpcZombie, ZombieRemoveDistUnits),  // m_flRemoveDist
	ELYSIUM_NPC_SPECIES_WORD(CNPC_VZombie, 0x66e0,
		FElysiumNpcZombie, bZombieShouldGib),  // m_bShouldGib, set at 0x103e0801 (Damage19)
	};
}

TArrayView<const FElysiumNpcSpeciesWordBinding> ElysiumNpcKernelSpeciesShapeMap::Bindings()
{
	return MakeArrayView(GSpeciesBindings);
}
