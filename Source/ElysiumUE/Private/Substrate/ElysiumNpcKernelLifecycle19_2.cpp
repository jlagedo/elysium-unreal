#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumWeaponClasses.h"

// Story 29e, family **Lifecycle19** — species `NPCInit` arms. Each chains the body it replaces
// through a direct call (retail's non-virtual thunk), never through the slot dispatcher.

namespace
{
	double Lifecycle19_2Now(const FElysiumNpc& Npc)
	{
		return Npc.World != nullptr ? Npc.World->NowSeconds() : 0.0;
	}

	void Lifecycle19_2LawNever(FElysiumNpc& Npc)
	{
		Npc.PlInvestigate = FElysiumNpc::LawThresholdNever;
		Npc.PlCriminalFlee = FElysiumNpc::LawThresholdNever;
		Npc.PlCriminalAttack = FElysiumNpc::LawThresholdNever;
		Npc.PlSupernaturalFlee = FElysiumNpc::LawThresholdNever;
		Npc.PlSupernaturalAttack = FElysiumNpc::LawThresholdNever;
	}

	void Lifecycle19_2HatePlayerClass(FElysiumNpc& Npc)
	{
		Npc.Relationships.SetClass(TEXT("player"), EElysiumRelationship::Hate, 10);
	}

}

void FElysiumNpc::PayphoneNPCInit()
{
	TroikaNPCInit();                                                     // 101aab9x
	bIsBccTargetable = true;                                             // +0x1480
	bInvincible = true;                                                  // +0x63d8
	bNpcIsAlive = false;                                                 // +0x1481
	Senses.bCanPerformSenses = false;                                    // senses+0x80
}

void FElysiumNpc::AndreiBloodNPCInit()
{
	VampireBossNPCInit();                                                // 1035cecx
	AndreiLastTeleportPosition = FVector::ZeroVector;                    // DAT_1070d1b0 origin
}

void FElysiumNpc::AsianVampireNPCInit()
{
	VampireBossNPCInit();                                                // 10360ce0
	const FVector OriginUnits = Origin / ElysiumMove::U;
	LastJumpPosition[0] = OriginUnits;                                   // 10360d45 slot 217
	LastJumpPosition[1] = OriginUnits;
	LastJumpPositionIdx = 0;                                             // +0x66d0 CORRECTION
	ScheduleHost.HintNode = INDEX_NONE;                                  // +0x5ddc inherited
	bAsianVampirePathBlocked = false;                                    // +0x66d4
	MovedTimeStamp = Lifecycle19_2Now(*this);                            // +0x66d8
	MovedPosition = OriginUnits;                                         // +0x66dc
	bAsianVampireSuppressRanged = true;                                  // +0x66e8
	JumpGravity = AsianVampireJumpGravity;                               // +0x64b8
}

void FElysiumNpc::BachNPCInit()
{
	TroikaNPCInit();
	DistTooFar = SwarmDistTooFar;                                        // 0x477fff00
}

void FElysiumNpc::BatSwarmNPCInit()
{
	TroikaNPCInit();
	DistTooFar = SwarmDistTooFar;
}

void FElysiumNpc::CameraNPCInit()
{
	// `0x103692c0` — replacement. Never calls Troika.
	InNpcInit() = true;
	++CameraEngineQueries;
	InitialPosition = Origin;                                            // slot 220
	if (!bCameraEngineQueryAnswer)
	{
		// `10369302 CALL UTIL_Remove` then `RET`. The refuse arm does NOT clear `DAT_10937cf1` —
		// there is no `MOV byte ptr [0x10937cf1],0` on this path — so the process-wide in-NPCInit
		// latch stays SET after a camera deletes itself. Reproduced.
		++CameraSelfRemovals;
		return;
	}
	bIsBccTargetable = false;
	bNpcIsAlive = true;
	TakeDamageMode = 0;
	// `1036932d`: `thunk_FUN_102e0b40(m_pMotor)` — `*(motor+0x2c) = -1.0`, the same motor-reset seam
	// the base body names. Then the yaw block, `1036937a` gating `motor+0x1c == 180.0`.
	ScheduleHost.DesiredMoveYaw = static_cast<float>(Angles.Y);
	if (ScheduleHost.bMotorAnimationMovement)
	{
		if (ScheduleHost.DesiredMoveYaw < MotorYawHalfTurn)
		{
			ScheduleHost.DesiredMoveYaw += MotorYawHalfTurn;
		}
		else
		{
			ScheduleHost.DesiredMoveYaw -= MotorYawHalfTurn;
		}
	}
	MaxHealth = 100;
	bDead = false;
	SetDeathReportedForRestore(false);
	WriteIdealStateRetail(1);
	ScheduleHost.bShouldMove = false;
	// `103693d5`: `*(m_pNavigator + 0x2c) = DAT_1093407c`, the node network — the same seam the base
	// body names. Then `ClearSchedule`, the navigator goal clear and `0x1008f540`
	// `ResetActivityIndexes`, which this runtime counts with the other animating resets.
	ClearSchedule();
	if (Motor != nullptr)
	{
		Motor->ClearNavigationGoal();
	}
	++NavigationGoalClears;
	++BaseInitAnimatingResets;                                           // 103693f3 1008f540
	ScheduleHost.HintNode = INDEX_NONE;
	ScheduleHost.MemoryBits = 0;
	ElysiumNpcEnemy::SetEnemy(*this, FElysiumEntityHandle::Invalid());
	bKeepSound = false;
	Cognition.Conditions.Reset();
	SetDefaultEyeOffset();
	++BaseInitTailCalls;
	const double Now = Lifecycle19_2Now(*this);
	if (Now <= MapFirstSecond)
	{
		ThinkSet(NpcInitThinkFunction(), 0.0);
		ArmThinkAt(Now + NpcInitThinkDelay);
	}
	else
	{
		++NpcInitInlineThinkCalls;
	}
	Mind.ClearForceStateChange();
	Unknown5b58 = 0;
	NpcFlags.SetFrenziedWord(0);
	NpcInitTime = Now;
	WeaponBlockedByFriendTimer = 0.0;
	ExtendedBlockedByFriendTimer = static_cast<double>(NeverThinkSentinel);
	Senses.Memory.EnemyOccludedCheck = 10;
	ShootTargetOverride = FElysiumEntityHandle::Invalid();
	// `103694c2`/`103694c9`/`103694d0`: the camera seeds the two PVS/LOS bytes and clears
	// `m_flNextPlayerLOS` ONLY. The three stamps Troika writes beside them (`+0x627c`, `+0x6280`,
	// `+0x6288`) are not touched here, so they keep their spawn values.
	Senses.Memory.bPlayerInPvs = true;                                   // 103694c2 +0x6278
	Senses.Memory.bPlayerLos = true;                                     // 103694c9 +0x6279
	Senses.Memory.PlayerLosNextUpdateTime = 0.0;                         // 103694d0 +0x6284
	NpcFlags.AssignAiFlagsWord(0);
	bForceNpcCheck = false;
	OccludedDelayNormal = CameraOccludedDelayNormal;                     // HARD-CODED
	OccludedDelayCover = CameraOccludedDelayCover;
	OccludedDelay = CameraOccludedDelayNormal;
	OccludedReportTimeE = 0.0;                                           // not curtime
	OccludedReportTimeT = 0.0;
	OccludedReportTimeW = 0.0;
	ScheduleHost.NextUpdate = Now;
	ScheduleHost.NextNormal = Now;
	ScheduleHost.NextAI = Now;
	ScheduleHost.NextMove = Now;
	ScheduleHost.LastUpdate = Now;
	ScheduleHost.LastNormal = Now;
	ScheduleHost.LastMove = Now;
	ScheduleHost.LastAI = Now;
	LeaveInterestingPlaceOnRemove();
	++TroikaInitInterestingPlaceReleases;
	LastSpotIndex = 0;
	ScheduleHost.Unknown6300 = 0;
	NextCrosswalkUpdateTime = 0.0;
	NextPedInteractTime = 0.0;
	ScheduleHost.Unknown659c = 0;
	ScheduleHost.GoalToleranceCm = 0.f;
	ScheduleHost.InsideInterruptDistanceSqr = 0.f;
	ScheduleHost.OutsideInterruptDistanceSqr = 0.f;
	ScheduleHost.InterruptTime = 0.0;
	ScheduleHost.WaitFinishedDelta = 0.f;
	ScheduleHost.bWaitFinishedSet = false;
	ScheduleHost.MoveTarget = FElysiumEntityHandle::Invalid();
	WeaponScareTime = -1.0;
	SpawnEquipLoadout();
	// `103696e8`/`103696f7`: `*m_pEnemyStore = this` and `m_pEnemyStore->+0x10 = 0.5f` — the camera
	// hard-codes the interval where Troika reads the tuning record. This runtime's enemy store is
	// the NPC's own `FElysiumNpcEnemyMemory`, so the back-pointer is implicit and the literal lands
	// on the interval it carries.
	EnemyMemory.FreeKnowledgeDuration = CameraEnemyStoreInterval;        // 103696f7 0x3f000000
	if (!PlayerReaction.IsEmpty())
	{
		FElysiumInputArgs Args;
		Args.Param = FElysiumVariant::String(FString::Printf(TEXT("Player %s"), *PlayerReaction));
		InputSetRelationship(Args);
	}
	InNpcInit() = false;
	ScheduleHost.bSavePositionWalk = false;
	AlertLevel = 0;
	++TroikaInitAlertLevelResets;
	bGoToIdleState = false;
	bLeaningLeft = false;
	ScheduleHost.NextCoverLosCheck = 0.0;
	ScheduleHost.FailedCoverLosChecks = 0;
	ScheduleHost.bForceCoverLosCheck = false;
	CowerAnimOffset = 0;                                                 // 10369763 +0x6414
	Senses.Memory.NextSeeSoundSourceTime = 0.0;                          // 10369769 +0x6418
	Senses.Memory.NextInvestigateSoundTime = 0.0;                        // 1036976f +0x623c
	Senses.Memory.SeeUnknownRepeatSightings = 0;                         // 10369775 +0x60a4
	EnemySightings = 0;                                                  // 1036977b +0x60a8
	Witness.Channel(ElysiumNpcWitness::EChannel::Criminal).IgnoreUntil = 0.0;        // 10369781
	Witness.Channel(ElysiumNpcWitness::EChannel::Supernatural).IgnoreUntil = 0.0;    // 10369787
	Witness.CriminalWitnessedTime = 0.0;                                 // 1036978d +0x63a4
	Witness.SupernaturalWitnessedTime = 0.0;                             // 10369793 +0x63a8
	// `+0x606c LastMeleeStepbackTime` is NOT written here: the listing jumps `+0x63a8` to `+0x6070`.
	MeleeCanEnterTimer = 0.0;                                            // 10369799 +0x6070
	MeleeMustLeaveTimer = 0.0;
	bInMelee = false;
	CanSeekCoverTimer = 0.0;
	ScheduleHost.KickPropSearchTimer = 0.0;
	ScheduleHost.KickProp = FElysiumEntityHandle::Invalid();
	ScheduleHost.NextShootAtHintSearchTime = 0.0;
	ScheduleHost.ShootAtHintNode = 0;
	AlternateAi = 0;
}

void FElysiumNpc::ChangBrosNPCInit()
{
	VampireBossNPCInit();
	ChangLastTeleportPosition = FVector::ZeroVector;                     // +0x66bc CORRECTION
	ChangLastTeleportTime = 0.0;                                         // +0x66c8
	LastJumpTime = 0.0;                                                  // +0x66cc
	FacingTime = 0.0;                                                    // +0x66d0
	bChangCenterStored = false;                                          // +0x66e8
	ChangLastUnitedAttackTime = Lifecycle19_2Now(*this);                 // +0x66ec CURTIME
	JumpGravity = ChangBrosJumpGravity;
	SetBodyEmitterName(0, TEXT("chang_powerup_emitter"));
	SetBodyEmitterName(1, TEXT("chang_powerup_emitter"));
	SetBodyEmitterName(2, TEXT("chang_spine_emitter"));
}

void FElysiumNpc::ChangBrosBladeNPCInit()
{
	FUN_1036c7f0(0);                                                     // BEFORE the chain
	ChangBrosNPCInit();
}

void FElysiumNpc::ChangBrosClawNPCInit()
{
	FUN_1036c7f0(1);
	ChangBrosNPCInit();
}

void FElysiumNpc::HumanCombatantNPCInit()
{
	// `0x10387140`. Listing: Troika then Hide on the active weapon (slot 66 tail JMP).
	TroikaNPCInit();
	HideActiveWeaponIfAny();                                             // 1038715f JMP [weapon+0x108]
}

void FElysiumNpc::CopNPCInit()
{
	HumanCombatantNPCInit();
	bWasEverInCombat = false;                                            // +0x6670 BYTE
}

void FElysiumNpc::Guard1NPCInit()
{
	// `0x1037e240`. Not a TSV row; dispatcher completeness.
	bGuard1HatesPlayer = false;                                          // 1037e24a +0x6660 FIRST
	FElysiumInputArgs Args;
	Args.Param = FElysiumVariant::String(TEXT("player D_NU 0"));
	InputSetRelationship(Args);
	TroikaNPCInit();
	HideActiveWeaponIfAny();
}

void FElysiumNpc::HunterNPCInit()
{
	HumanCombatantNPCInit();                                             // hides once inside
	HideActiveWeaponIfAny();                                             // second Hide
}

void FElysiumNpc::YukieNPCInit()
{
	HumanCombatantNPCInit();
	HideActiveWeaponIfAny();
}

void FElysiumNpc::FrenzyShadowNPCInit()
{
	// `10375ca4` `Weapon_Create` then `10375cb6 OR [EDI+0x19c],0x40` — the NODRAW bit is ORed into
	// whatever `Weapon_Create` answered, with NO null test: a create that fails faults here. Item
	// creation is deferred to `ResolveLoadout` in this port (see `SpawnEquipLoadout`), so the write
	// lands when a weapon is already carried and is counted as retail's fault when none is.
	++SpawnEquipRequests;
	++FrenzyShadowWeaponFlagOrs;
	{
		FElysiumItem* const Active = Inventory.Active(*this);
		FElysiumWeapon* const Weapon = Active != nullptr ? Active->AsWeapon() : nullptr;
		if (Weapon != nullptr)
		{
			Weapon->Hide(this);                                          // m_fEffects |= EF_NODRAW
		}
		else
		{
			++FrenzyShadowNullWeaponFaults;                              // 10375cb6, retail faults
		}
	}
	PlayerControllerNPCInit();
	WriteIdealStateRetail(0xb);
	SetState(0xb);                                                       // 1026e340(0xb)
	NpcFlags.SetFrenziedWord(FrenzyShadowFrenziedFlags);
	Senses.bCanPerformSenses = true;
	FrenzyShadowHostileEnemyCount = 0;
	bFrenzyShadowFailedGrapple = false;
	NpcSpeedScale = FrenzyShadowSpeedScale;
	bNavIgnorePhysicsProps = true;
	FrenzyShadowHostileRecount();                                        // tail JMP 10376c10
}

void FElysiumNpc::GargoyleNPCInit()
{
	TroikaNPCInit();
	GargoylePillarTarget = FElysiumEntityHandle::Invalid();
	SpeciesShunnedFindCount = 0;
	GargoyleDoingGibDeath = 0;
	GargoyleCanKnockback = 0;
	NodeGraphHullIndex() = HullIndexGargoyle;
}

void FElysiumNpc::GhoulCroucherNPCInit()
{
	// Scope-trace name is `"CNPC_VWerewolf::NPCInit"` — retail copy-paste, reproduced as nothing
	// but the comment.
	HumanCombatantNPCInit();
	++InventoryDestroys;
	const TCHAR* Item = bGhoulSpawnDisturbed
		? TEXT("item_w_claws_ghoul")
		: TEXT("item_w_knife");
	GiveNamedFightingItem(Item);                                         // 1021fe50
	ElysiumMiscFlags::Set(MiscFlags, MiscFlagBaseFightingItems);
	if (bGhoulSpawnBurning)
	{
		// `thunk_FUN_100fbc90("la_malkavian_4_ghoul_body_fire_e", origin, angles)`, then the emitter's
		// `+0x3cc(this, 1, "")` attach and `m_hBurningParticle = *emitter->+0x4()`. The create and the
		// attach go through family Damage's emitter seam, which this runtime HAS; the handle store
		// does not, because `SpawnParticleRoot` answers an effect handle and not an entity — so
		// `ScriptUnhide`'s consumer still finds nothing. Stated, not hidden behind the counter.
		++GhoulBurningParticleCreates;
		CreateNamedEmitter(TEXT("la_malkavian_4_ghoul_body_fire_e"), Origin / ElysiumMove::U,
			/*AttachMode=*/1, Handle, TEXT(""));
	}
	Lifecycle19_2HatePlayerClass(*this);
}

void FElysiumNpc::HengeyokaiNPCInit()
{
	TroikaNPCInit();
	PathMode = 0;                                                        // +0x6680
	HengeyokaiPickupTarget = FElysiumEntityHandle::Invalid();            // +0x6664
	SpeciesShunnedFindCount = 0;
	bHengeyokaiJustFoundFish = false;
	bHengeyokaiInSharkForm = false;
	HengeyokaiShunnedFishTimer = Lifecycle19_2Now(*this) + SpeciesShunWindowSeconds;
	NodeGraphHullIndex() = HullIndexHengeyokai;
}

void FElysiumNpc::ManBatNPCInit()
{
	const double Now = Lifecycle19_2Now(*this);
	ManBatFlapTimer = Now + ManBatFlapDelaySeconds;                      // BEFORE the base
	bManBatHasScaredMinions = false;
	bHasPlayedFlyBySound = false;
	ManBatFlyTimer = Now + NpcInitThinkDelay;
	TroikaNPCInit();
	NodeGraphHullIndex() = HullIndexManBat;
}

void FElysiumNpc::NewscasterNPCInit()
{
	TroikaNPCInit();
	Senses.Memory.NextFleeSoundTime = 0.0;
	Lifecycle19_2LawNever(*this);
	SeedCriminalLevelWitnessed();
	InvestigateMode = 0;
	InvestigateModeCombat = 0;
	NpcFlags.SetFrenziedWord(0);
	Senses.bCanPerformSenses = false;
	bIsBccTargetable = false;
}

void FElysiumNpc::PedestrianNPCInit()
{
	TroikaNPCInit();
	bPedestrianFirstThink = false;
	Senses.Memory.NextFleeSoundTime = 0.0;
}

void FElysiumNpc::PlaceholderNPCInit()
{
	// `103a435x`: `m_pInterestingPlace = NULL`, before the base. This runtime spells "no place" as
	// `INDEX_NONE` (`FElysiumNpc::CurrentSpotIndex`), because 0 is a valid entity index here.
	CurrentSpotIndex = INDEX_NONE;                                       // +0x62ec, BEFORE the base
	TroikaNPCInit();
	bIsBccTargetable = true;
	ThinkSet(nullptr, 0.0);
}

void FElysiumNpc::PlayerControllerNPCInit()
{
	TroikaNPCInit();
	Senses.Memory.NextFleeSoundTime = 0.0;
	Lifecycle19_2LawNever(*this);
	SeedCriminalLevelWitnessed();
	InvestigateMode = 6;
	InvestigateModeCombat = 6;
	SetForceFrequentThink(true);                                         // slot 416
	const FElysiumEntityHandle Owner = GetOwnerEntity();
	if (!Owner.IsSet())
	{
		FriendPlayer = FElysiumEntityHandle::Invalid();
	}
	else
	{
		FriendPlayer = Owner;
	}
	NpcFlags.SetFrenziedWord(0);
	Senses.bCanPerformSenses = false;
	bIsBccTargetable = false;
}

void FElysiumNpc::SabbatLeaderNPCInit()
{
	VampireBossNPCInit();
	SabbatLeaderRouteFailCount = 0;
	FailureType = 0;
	bSabbatLeaderActivated = false;
	VampireBossMonsterModelName = TEXT("models/character/monster/Andrei/andrei.mdl");
	LastAttackTime = Lifecycle19_2Now(*this);
	VampireBossMonsterClassname = TEXT("npc_VSabbatLeader");
	SetBodyEmitterName(0, TEXT("Andrei_powerup_emitter"));
	SetBodyEmitterName(1, TEXT("Andrei_powerup_emitter"));
	// `103a6de9 MOV dword ptr [ESI + 0x66c4],EBX` is `CNPC_VSabbatLeader::m_nLastWaterLevel`, the
	// splash detector's edge latch — NOT the entity's own `m_nWaterLevel` (`+0x03e0`).
	SabbatLastWaterLevel = 0;                                            // 103a6de9 +0x66c4
	SabbatLeaderLastSplashTime = 0.0;
	RecordPlayerHealth();
	bSabbatLeaderTrackPlayer = false;
	bSabbatLeaderDiving = false;
	bSabbatLeaderLargeSplash = false;
	bSabbatLeaderParticleSpawned = false;
	SabbatLeaderJumpBloodBalance = 0;
	SabbatLeaderRoarAttackCount = 3;
	bSabbatLeaderLastAttackWasNova = false;
}

void FElysiumNpc::SheriffManNPCInit()
{
	SheriffTeleportSwarm = FElysiumEntityHandle::Invalid();              // BEFORE the base
	bSheriffTeleporting = false;
	bSheriffDead = false;
	bSheriffActivated = false;
	SheriffLastTeleportPosition = Origin / ElysiumMove::U;
	SheriffLastTeleportTime = Lifecycle19_2Now(*this);
	bSheriffLedgeHeightStored = false;                                   // +0x66e7
	VampireBossNPCInit();
	VampireBossMonsterModelName = TEXT("models/character/monster/manbat/manbat.mdl");
	VampireBossMonsterClassname = TEXT("npc_VSheriffMan");
	RecordHealthPercent();                                               // 103c6a00
	JumpGravity = SheriffManJumpGravity;
	NodeGraphHullIndex() = HullIndexSheriffMan;
}

void FElysiumNpc::SheriffSwarmNPCInit()
{
	TroikaNPCInit();
	DistTooFar = SwarmDistTooFar;
}

void FElysiumNpc::TaxiDriverNPCInit()
{
	TroikaNPCInit();
	bTaxiFirstThink = false;
	WriteIdealStateRetail(1);
	SetState(1);
	Senses.bCanPerformSenses = true;
}

void FElysiumNpc::TzimisceNPCInit()
{
	TroikaNPCInit();
	bTzimisceFirstEnemy = true;
	PathMode = 0;
	++ExpressionMapResets;                                               // 103b9f50
	SpeciesShunnedFindCount = 0;
	bTzimisceJustFoundBody = false;
	const double Now = Lifecycle19_2Now(*this);
	TzimiscePounceCheckTimer = Now + SpeciesShunWindowSeconds;
	TzimisceShunnedBodyTimer = Now + SpeciesShunWindowSeconds;
	PickupTarget = FElysiumEntityHandle::Invalid();
	FUN_102c43b0(0.f);
}

void FElysiumNpc::TzimisceHeadClawNPCInit()
{
	TroikaNPCInit();
	HeadClawSlowedExpire = 0.0;
}

void FElysiumNpc::VampireBossNPCInit()
{
	TroikaNPCInit();
	VampireBossMonsterModelName.Reset();
	ClearBodyEmitterNames();
	VampireBossMonsterClassname = TEXT("npc_VVampireBoss");
	bJumping = false;
	RecordHealthPercent();                                               // 103c6a00
	JumpGravity = 1.f;
}

void FElysiumNpc::WerewolfNPCInit()
{
	StatTemplate = TEXT("Werewolf");                                     // BEFORE the base
	TroikaNPCInit();
	StatTemplate = TEXT("Werewolf");                                     // and again after
	++InventoryDestroys;
	GiveBaseFightingItems();                                             // slot 304
	Lifecycle19_2HatePlayerClass(*this);
	TakeDamageMode = 1;
	Senses.Memory.NextFleeSoundTime = 0.0;
	Lifecycle19_2LawNever(*this);
	SeedCriminalLevelWitnessed();
	AuthoredVision = WerewolfSeekDistBaseUnits;
	AuthoredHearing = WerewolfHearingScalarBase;
	DistTooFar = FarSightDistTooFar;
	FieldOfViewDot = static_cast<float>(FMath::Cos(WerewolfFieldOfViewRadians));
	SetDistLook(FarSightDistLookUnits * ElysiumMove::U);
	Senses.ResolveTuning(*this);                                         // 1028fb70
	InvestigateMode = 3;
	InvestigateModeCombat = 3;
	CapabilityWord |= 1;
	CapabilityWord |= 0x200000;
	CapabilityWord |= 0x8000;
	CapabilityWord |= 0x10000;
	ScheduleHost.HintNode = INDEX_NONE;
	TeleportHintNode = INDEX_NONE;
	WerewolfLastUsedTeleportHint = INDEX_NONE;
	MoveHintNode = INDEX_NONE;
	WerewolfLastUsedMoveHint = INDEX_NONE;
	WerewolfBreakHintNode = INDEX_NONE;                                   // +0x66c4
	WerewolfRearm();
}

void FElysiumNpc::WolfMorphNPCInit()
{
	PlayerControllerNPCInit();
	WriteIdealStateRetail(WolfMorphRetailState);
	SetActivity(WolfMorphActivity);
}

void FElysiumNpc::ZombieNPCInit()
{
	bZombieNeedsCrawlOutOfGround = true;                                 // FIRST
	TroikaNPCInit();
	++SpawnEquipRequests;                                                // item_w_zombie_fists
	Lifecycle19_2HatePlayerClass(*this);
	Hide();                                                              // slot 66 on self
	FElysiumEntity* Closest = nullptr;
	if (World != nullptr && Senses.Memory.ClosestPlayer.IsSet())
	{
		Closest = World->Resolve(Senses.Memory.ClosestPlayer);
	}
	ElysiumNpcEnemy::SetEnemy(*this, Closest != nullptr ? Closest->Handle : FElysiumEntityHandle::Invalid());
	SetTarget(Closest != nullptr ? Closest->Handle : FElysiumEntityHandle::Invalid());
	ZombieGrappleReadyTimer = Lifecycle19_2Now(*this) + TuningZombieGrappleReadyInterval();
	SetSchedule(ZombieCrawlScheduleRetailId, false);                      // 0x103df04b -> 0x102ae750
}
