#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcLifecycle19Shared.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "ElysiumSessionSubsystem.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// Story 29e, family **Lifecycle19** — the spine: Base/Troika `NPCInit`, `StartNPC`, `OnRestore`,
// the three slot dispatchers, and the helpers those bodies share. Species `NPCInit` arms live in
// `ElysiumNpcLifecycle19_2.cpp`.

namespace
{
	bool GLifecycle19InNpcInit = false;
	int32 GLifecycle19NodeGraphHull = 0;
	FElysiumEntityHandle GLifecycle19FleshpileAndrei;

	bool Lifecycle19IsNoneSentinel(const FString& Authored)
	{
		if (Authored.IsEmpty())
		{
			return true;
		}
		if (Authored.Len() >= 1 && Authored[0] == TEXT('0')
			&& (Authored.Len() == 1 || Authored[1] == 0))
		{
			return true;
		}
		return Authored.Equals(TEXT("0"), ESearchCase::CaseSensitive);
	}

	bool Lifecycle19IsUnarmedSentinel(const FString& Authored)
	{
		// 15-byte `strncmp` against `"item_w_unarmed"`.
		const TCHAR* Literal = TEXT("item_w_unarmed");
		int32 Remain = 15;
		int32 Index = 0;
		while (Remain > 0)
		{
			const TCHAR A = Index < Authored.Len() ? Authored[Index] : 0;
			const TCHAR B = Literal[Index];
			if (A != B)
			{
				return false;
			}
			if (A == 0)
			{
				return true;
			}
			++Index;
			--Remain;
		}
		return true;
	}

}

bool& FElysiumNpc::InNpcInit()
{
	return GLifecycle19InNpcInit;
}

int32& FElysiumNpc::NodeGraphHullIndex()
{
	return GLifecycle19NodeGraphHull;
}

int32& FElysiumNpc::NodeIndexErrorCount()
{
	// `DAT_106c994c` is ONE global: the ped-link restore, the node-network validation `0x10307ac0`
	// and the patrol steps all bump the same word, which family Script19 carries in the patrol pool.
	return PatrolNodeMissCounter();
}

FElysiumEntityHandle& FElysiumNpc::FleshpileAndreiSingleton()
{
	return GLifecycle19FleshpileAndrei;
}

int32 FElysiumNpc::ResolveCombatStartActivity(const FString& Name) const
{
	// `0x1029f340` whole: `if (name == NULL) return -1; return ActivityNameToId(name);`. Its only
	// callee is `0x10412520`, which family Hints already stands as `ActivityIdForName`, so this is
	// the two-line body and not a second seam beside it.
	if (Name.IsEmpty())
	{
		return INDEX_NONE;                                               // 1029f34x null arm
	}
	return ActivityIdForName(Name);                                      // 10412520
}

void FElysiumNpc::SeedStatListOnNpcInit()
{
	++StatListSeeds;
	// `1029a5a1`–`1029a67f`. The walk over `+0x13bc`/`+0x13c0` picks the stat list whose `+0x10` is
	// 0 — the ATTRIBUTES container — and falls back to the process-wide `DAT_109f0b40` when the NPC
	// carries none. This runtime's sheet IS that container set, so the two writes land on it:
	// `CVStatList_t::Set(0xf, 0)` is slot 15 Health (damage taken) and `SetBase(0xc, x)` slot 12
	// BloodPool, with `x` the stat template's `+0xd0 -> +0x30`, which `SeedSheet` already resolved
	// into `TemplateBloodPoolValue`.
	Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Health, 0);          // 1029a5f4
	// `1029a661`: the blood value comes from `0x101d5f10(DAT_10738d10, this)->+0xd0->+0x30`, a
	// per-NPC record retail always has. This runtime resolves that record from the NPC's
	// `stattemplate`, so an NPC with no template has none — and writing 0 there would DRAIN it,
	// which is not what the missing record means. The seeded pool stands in that case.
	if (TemplateBloodPoolValue > 0)
	{
		Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::BloodPool,
			TemplateBloodPoolValue);                                                   // 1029a67a
	}
}

void FElysiumNpc::SeedCriminalLevelWitnessed()
{
	// `0x1042fde0(0)` then the scramble into `+0x6364`. The port carries the PLAIN level on
	// `Witness.Channel`; the two uninitialised tag bytes write 0 (Senses.inl named defect).
	CriminalWitnessByte6360 = 0;
	CriminalWitnessByte6361 = 0;
	Witness.Channel(ElysiumNpcWitness::EChannel::Criminal).Level = 0;
	Witness.Channel(ElysiumNpcWitness::EChannel::Supernatural).Level = 0;
	(void)EncodeWitnessedLevel(0);
}

void FElysiumNpc::SpawnEquipLoadout()
{
	// Spawn-equip block, gated on slot 513 bit `0x200000`. Item *creation* stays deferred to
	// `ResolveLoadout` on first think (creating an entity inside Activate would invalidate the
	// spawn array). The DECISION is reproduced here: skip `"0"` (2-byte) and `"item_w_unarmed"`
	// (15-byte), else record a request.
	const uint32 Caps = static_cast<uint32>(CapabilitiesGet());          // 1029a1xx slot 513
	if ((Caps & CapabilitySpawnEquip) == 0)
	{
		return;
	}
	auto TryOne = [this](const FString& Authored)
	{
		if (Authored.IsEmpty() || Lifecycle19IsNoneSentinel(Authored)
			|| Lifecycle19IsUnarmedSentinel(Authored))
		{
			return;
		}
		++SpawnEquipRequests;
	};
	TryOne(AlternateEquipment);
	TryOne(AdditionalEquipment);
}

namespace
{
	// `0x101e8c50` / `0x101e8c70` / `0x101e8c30` / `0x101e8bf0` are NOT cvars: each is a seven-byte
	// `__fastcall` getter of a float field on the process-global `CVFeatList_t` (`0x10739d08`,
	// `101e4d90`), whose fields `0x101e6310` loads out of `vdata/system/Rules.txt` through
	// `KeyValues::GetFloat(key, default)`. The defaults below are the immediates in that loader;
	// the live value comes from the rulebook, exactly as `FElysiumNpcSenses::ResolveTuning` already
	// reads `FreeKnowledgeDuration` from the same section.
	float Lifecycle19RulesFloat(const FElysiumNpc& Npc, const TCHAR* Section, const TCHAR* Key,
		float ImageDefault)
	{
		UElysiumSessionSubsystem* GameState = Npc.World != nullptr ? Npc.World->GetGameState() : nullptr;
		UElysiumRulebookSubsystem* Rules = GameState != nullptr ? GameState->Rulebook() : nullptr;
		return Rules != nullptr ? Rules->Rules().Flt(Section, Key, ImageDefault) : ImageDefault;
	}
}

float FElysiumNpc::TuningOccludedDelayNormal() const
{
	// `0x101e8c50` → `CVFeatList_t +0x288`, `Npc_Combat_Info/OccludedDelayNormal`, image default 0.5.
	return Lifecycle19RulesFloat(*this, TEXT("Npc_Combat_Info"), TEXT("OccludedDelayNormal"), 0.5f);
}

float FElysiumNpc::TuningOccludedDelayCover() const
{
	// `0x101e8c70` → `+0x28c`, `Npc_Combat_Info/OccludedDelayCover`, image default 5.0.
	return Lifecycle19RulesFloat(*this, TEXT("Npc_Combat_Info"), TEXT("OccludedDelayCover"), 5.f);
}

float FElysiumNpc::TuningEnemyStoreInterval() const
{
	// `0x101e8c30` → `+0x284`, `Npc_Combat_Info/FreeKnowledgeDuration`, image default 0.25. This is
	// the same word `FElysiumNpcSenses::ResolveTuning` already seeds onto `EnemyMemory`, which is
	// what `*(m_pEnemyStore + 0x10)` IS here; the accessor answers it rather than a second copy.
	return static_cast<float>(EnemyMemory.FreeKnowledgeDuration);
}

float FElysiumNpc::TuningZombieGrappleReadyInterval() const
{
	// `0x101e8bf0` → `+0x27c`, `Zombie_Grapple_Info/DelayInitial`, image default 20.0 (the shipped
	// `rules.txt` authors 10.0, which the rulebook read picks up).
	return Lifecycle19RulesFloat(*this, TEXT("Zombie_Grapple_Info"), TEXT("DelayInitial"), 20.f);
}

int32 FElysiumNpc::FindInterestingPlaceHoldingMe() const
{
	// `0x102db5e0`: walk every interesting place (`DAT_10927194`, `+0x540` next) and answer the LAST
	// one whose marker table (`+0x580`, `+0x588` records of stride `0x1c`, first dword the occupant)
	// holds this NPC. Retail keeps scanning after a hit rather than breaking, so the last wins.
	if (World == nullptr)
	{
		return INDEX_NONE;
	}
	int32 Found = INDEX_NONE;
	for (const TUniquePtr<FElysiumEntity>& Candidate : World->Entities())
	{
		if (!Candidate.IsValid() || Candidate->Def == nullptr
			|| !Candidate->Def->Classname.Equals(TEXT("intersting_place"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		const FElysiumInterestingPlace* Place =
			static_cast<const FElysiumInterestingPlace*>(Candidate.Get());
		for (const FElysiumInterestingPlace::FMarker& Marker : Place->Markers)
		{
			if (Marker.Occupant == Handle)
			{
				Found = Place->Handle.Index;
			}
		}
	}
	return Found;
}

void FElysiumNpc::ValidateRestoredInterestingPlace()
{
	// `0x10299a80`, the pass `CAI_BaseNPCTroika::OnRestore` runs over the place the scan above just
	// wrote. Two refusals, each a `DevMsg` and a cleared `+0x62ec`.
	if (CurrentSpotIndex == INDEX_NONE)
	{
		return;                                                          // 10299a97
	}
	FElysiumInterestingPlace* const Place = CurrentAmbientSpot();
	const FVector OriginUnits = Origin / ElysiumMove::U;
	if (Place == nullptr)
	{
		// `10299aa6`: the place is not on the live list at all.
		++RestorePlaceRejections;
		UE_LOG(LogElysiumNpcEnt, Log,
			TEXT("ERROR: %s loc( %6.2f, %6.2f, %6.2f) thinks they are at an interesting place that ")
			TEXT("no longer seems to be a valid entity!"),
			*DebugString(), OriginUnits.X, OriginUnits.Y, OriginUnits.Z);
		CurrentSpotIndex = INDEX_NONE;                                   // 10299b02
		return;
	}
	for (const FElysiumInterestingPlace::FMarker& Marker : Place->Markers)
	{
		if (Marker.Occupant == Handle)
		{
			return;                                                      // 10299b2c / 10299b3f
		}
	}
	// `10299b45`: on the list, but its marker table does not name this NPC.
	++RestorePlaceRejections;
	const FVector PlaceUnits = Place->Origin / ElysiumMove::U;
	UE_LOG(LogElysiumNpcEnt, Log,
		TEXT("ERROR: %s loc( %6.2f, %6.2f, %6.2f) thinks they are at %s but that InterestingPlace, ")
		TEXT("loc(%6.2f, %6.2f, %6.2f) does not know that."),
		*DebugString(), OriginUnits.X, OriginUnits.Y, OriginUnits.Z, *Place->DebugString(),
		PlaceUnits.X, PlaceUnits.Y, PlaceUnits.Z);
	CurrentSpotIndex = INDEX_NONE;                                       // 10299be2
}

// =================================================================================================
// `0x10273390` — `CAI_BaseNPC::NPCInit`
// =================================================================================================

// =================================================================================================
// `0x1029a0b0` — `CAI_BaseNPCTroika::NPCInit`
// =================================================================================================

void FElysiumNpc::TroikaNPCInit()
{
	InNpcInit() = true;                                                  // 1029a0b3 DAT_10937cf1 = 1
	// `1029a0c0`: `m_fEffects = 0`. This runtime spells that word's one modelled bit — `EF_NODRAW`
	// (`0x40`) — as `FElysiumEntity::bHidden`, which is what `FElysiumWeapon::Hide` writes and what
	// `Weapon_TranslateActivity` reads; clearing the word therefore UNHIDES. Retail does exactly
	// that here, before the base body runs, and `CNPC_VZombie::NPCInit` re-hides itself afterwards.
	bHidden = false;                                                     // 1029a0c0 m_fEffects +0x19c
	// The same store on the kernel's own spelling of the word. `bHidden` and `EffectsWord` bit `0x40`
	// are TWO port spellings of the one retail `m_fEffects` (follow-up: unify them). This zero is why
	// the player controller ends at exactly `0x60`: `CopyAnimationDataFrom` (`0x10097310`, `100973cc`)
	// wrote `player | 0x10`, `DispatchSpawn` reaches this body, and `GetControllerNPC` ORs `0x60` after.
	EffectsWord = 0;                                                     // 1029a0c0 m_fEffects +0x19c
	// `1029a0c6`–`1029a0ef`: `m_iHealth = ftol(DAT_10923f14->IsCommand() ? 0.0 : cvar+0x28)` —
	// `sk_basenpctroika_health`, shipped "10". Written verbatim over what `SeedSheet` derived: this
	// body is the only Troika-typed writer of `+0x210`, and the sheet's next projection
	// (`SyncHealthFromSheet`, on damage) is what replaces it, as retail's `HealthToPercent` does.
	Health = static_cast<int32>(
		ElysiumNpcTunables::ConVarFloat(ElysiumNpcTunables::EConVar::SkBasenpctroikaHealth));
	++NpcInitHealthSeeds;
	WriteNpcStateRetail(0);                                              // 1029a0f5 m_NPCState = NONE
	Senses.ResetListenClock();                                           // 1029a0fb +0x63d0 = 0
	// `1029a101`: `m_pfnTouch = &LAB_1000cb76`, a retail member-function pointer with no port word.
	FElysiumNpcBase::NPCInit();                                                       // 1029a10b -> 10273390
	const double Now = NpcKernelLifecycle19Shared::Lifecycle19Now(*this);
	Senses.Memory.bPlayerInPvs = true;
	Senses.Memory.bPlayerLos = true;
	Senses.Memory.PlayerPvsLastClearTime = Now;
	Senses.Memory.PlayerLosLastClearTime = Now;
	Senses.Memory.PlayerLosNextUpdateTime = 0.0;
	Senses.Memory.NextCheckEnterPvsTime = 0.0;
	NpcFlags.AssignAiFlagsWord(0);                                       // m_bfAINPCFlags = 0
	bForceNpcCheck = false;
	OccludedDelayNormal = TuningOccludedDelayNormal();
	OccludedDelayCover = TuningOccludedDelayCover();
	OccludedDelay = OccludedDelayNormal;
	OccludedReportTimeE = Now;
	OccludedReportTimeT = Now;
	OccludedReportTimeW = Now;
	ScheduleHost.NextUpdate = Now;
	ScheduleHost.NextNormal = Now;
	ScheduleHost.NextAI = Now;
	ScheduleHost.NextMove = Now;
	ScheduleHost.LastUpdate = Now;
	ScheduleHost.LastNormal = Now;
	ScheduleHost.LastMove = Now;
	ScheduleHost.LastAI = Now;
	LeaveInterestingPlaceOnRemove();                                     // 102b53d0(curtime)
	++TroikaInitInterestingPlaceReleases;
	LastSpotIndex = 0;                                                   // +0x62fc
	ScheduleHost.Unknown6300 = 0;
	NextCrosswalkUpdateTime = 0.0;                                       // 1029a22a +0x6318
	NextPedInteractTime = 0.0;                                           // 1029a230 +0x631c
	// `1029a236`–`1029a248`: the two `sPatrolPath` smart-pointer pairs — `m_sppPatrolPath`
	// (`+0x658c` byte, `+0x6590` pointer) and `m_sppPatrolPathHunt` (`+0x6594`, `+0x6598`) — are
	// zeroed whole, WITHOUT a release: a pooled record they held stays marked in use (reproduced).
	PatrolPathCell = FPatrolPathCell();                                  // 1029a236 / 1029a23c
	PatrolPathHuntCell = FPatrolPathCell();                              // 1029a242 / 1029a248
	ScheduleHost.Unknown659c = 0;                                        // 1029a24e
	ScheduleHost.bPatrolPathUseHint = false;                             // 1029a254 +0x65a0
	ScheduleHost.GoalToleranceCm = 0.f;
	ScheduleHost.InsideInterruptDistanceSqr = 0.f;
	ScheduleHost.OutsideInterruptDistanceSqr = 0.f;
	ScheduleHost.InterruptTime = 0.0;
	ScheduleHost.WaitFinishedDelta = 0.f;
	ScheduleHost.bWaitFinishedSet = false;
	ScheduleHost.MoveTarget = FElysiumEntityHandle::Invalid();           // 1029a27e +0x6240
	HitBuildupCount = 0;                                                 // 1029a28b +0x6064
	WeaponScareTime = -1.0;                                              // 1029a291 +0x63dc
	SpawnEquipLoadout();                                                 // 1029a29d slot 513
	// `1029a3b8`: `*m_pEnemyStore = this` and `m_pEnemyStore->+0x10 = FreeKnowledgeDuration`. This
	// runtime's enemy store is `FElysiumNpcEnemyMemory`, owned by the NPC (so its back-pointer is
	// implicit) and already carrying that interval, which `Senses.ResolveTuning` below seeds from
	// the same `Rules.txt` key. Read here so the arm is exercised, written by the seeder.
	(void)TuningEnemyStoreInterval();
	if (!PlayerReaction.IsEmpty())
	{
		FElysiumInputArgs Args;
		Args.Param = FElysiumVariant::String(FString::Printf(TEXT("Player %s"), *PlayerReaction));
		InputSetRelationship(Args);                                      // 10273790
	}
	InNpcInit() = false;                                                 // 1029a406 DAT_10937cf1 = 0
	ScheduleHost.bSavePositionWalk = false;                              // 1029a40f +0x63e0
	AlertLevel = 0;                                                      // 1029a415 102b5dc0(this, 0)
	++TroikaInitAlertLevelResets;
	bGoToIdleState = false;                                              // 1029a41a +0x63fc
	bLeaningLeft = false;                                                // 1029a420 +0x63fd
	ScheduleHost.NextCoverLosCheck = 0.0;                                // 1029a426 +0x6400
	ScheduleHost.FailedCoverLosChecks = 0;                               // 1029a42c +0x6404
	ScheduleHost.bForceCoverLosCheck = false;                            // 1029a432 +0x6408
	CowerAnimOffset = 0;                                                 // 1029a438 +0x6414
	Senses.Memory.NextSeeSoundSourceTime = 0.0;                          // 1029a43e +0x6418
	Senses.Memory.NextInvestigateSoundTime = 0.0;                        // 1029a444 +0x623c
	Senses.Memory.SeeUnknownRepeatSightings = 0;                         // 1029a44a +0x60a4
	EnemySightings = 0;                                                  // 1029a450 +0x60a8
	// `+0x641c NextFleeSoundTime` is NOT written here — the listing runs `+0x6418`, `+0x623c`,
	// `+0x60a4`, `+0x60a8` and stops. Only the four species bodies that clear it do.
	if (!StatTemplate.IsEmpty())
	{
		bIsBccTargetable = true;                                         // 1029a4a2, template only
	}
	bNpcIsAlive = true;                                                  // 1029a4a9 +0x1481
	Witness.Channel(ElysiumNpcWitness::EChannel::Criminal).IgnoreUntil = 0.0;
	Witness.Channel(ElysiumNpcWitness::EChannel::Supernatural).IgnoreUntil = 0.0;
	Witness.NosferatuIgnoreUntil = 0.0;
	Witness.CriminalWitnessedTime = 0.0;
	Witness.SupernaturalWitnessedTime = 0.0;
	Witness.Channel(ElysiumNpcWitness::EChannel::Criminal).Processed = -1;
	Witness.Channel(ElysiumNpcWitness::EChannel::Supernatural).Processed = -1;
	LastMeleeStepbackTime = 0.0;
	MeleeCanEnterTimer = 0.0;
	MeleeMustLeaveTimer = 0.0;
	bInMelee = false;
	CanSeekCoverTimer = 0.0;
	ScheduleHost.KickPropSearchTimer = 0.0;
	ScheduleHost.KickProp = FElysiumEntityHandle::Invalid();
	ScheduleHost.NextShootAtHintSearchTime = 0.0;
	ScheduleHost.ShootAtHintNode = 0;
	AlternateAi = 0;
	IgnoreCollisionUntil = static_cast<double>(NpcKernelLifecycle19Shared::GFltMax);
	Senses.Memory.DetectedAttackAttacker = FElysiumEntityHandle::Invalid();
	Senses.Memory.DetectedAttackTime = 0.0;
	ScheduleHost.ForcedSchedule = ElysiumScheduleId::None;
	Slot593();
	SetAttackExtents(FVector(-1.f, -1.f, -1.f) * ElysiumMove::U);         // source (-1,-1,-1)
	ResetFakeReloadCount();                                              // 102c54c0
	bCameFromSpawner = false;
	StandingOnHeadTimer = 0.f;
	WeaponThroughWallTime = 0.0;
	Senses.Memory.StealthVisionOverrideUntil = 0.0;
	CorpseConditionTime = 0.0;
	bJumping = false;                                                    // 1029a579 +0x6498
	JumpGravity = 1.f;                                                   // 1029a57f +0x64b8
	DisciplineFlags = 0;                                                 // 1029a589 +0x0eb0
	DisciplineFlags2 = 0;                                                // 1029a58f +0x0eb4
	DisciplinePreFlags = 0;                                              // 1029a595 +0x0eb8
	DisciplinePreFlags2 = 0;                                             // 1029a59b +0x0ebc
	SeedStatListOnNpcInit();                                             // 1029a5a1
	bReturnToInitialPos = false;                                         // 1029a684 +0x6494
	Senses.Memory.SeeUnknownGraceUntil = -1.0;                           // 1029a68a +0x6084
	Senses.Memory.SeeUnknownRunTimer = 0.0;                              // 1029a690 +0x609c
	Senses.Memory.SeeUnknownStartTimer = 0.0;                            // 1029a696 +0x60a0
	MeleeHeightDiffTimer = -1.0;                                         // 1029a69e +0x6274
	Senses.ResolveTuning(*this);                                         // 1029a6a4 1028fb70
	// `1029a6a9`: `m_nCurrDisposition = DAT_10924984`. SEAM: that datum has NO writer in the image
	// — its only reader is this body and its three thunks — so the index it holds is not recoverable
	// from the corpus. This runtime's disposition is a NAME, applied by
	// `ApplyDefaultDispositionOnActivate` immediately before this body runs, and is left standing.
	++DispositionSeeds;
	EyeLookTargetHandle = FElysiumEntityHandle::Invalid();               // 1029a6b4 +0x0e64
	RelativeEyeTarget = 0;                                               // 1029a6ba +0x5b94
	if (TeleportMoveTimer > TeleportMoveTimerFloor)                      // 1029a6c1 FCOMP 0.0
	{
		TeleportMoveTimer = static_cast<float>(Now) + TeleportMoveTimer + TeleportMoveTimerExtra;
		// `1029a6f9 CALL 0x10001041` -> `0x102ae7f0`, whose WHOLE body is
		// `*(this + 0x65c8) = param_1`. It writes `m_iForcedSchedule` and installs nothing: the
		// schedule the NPC is running is not touched here.
		ScheduleHost.ForcedSchedule =
			static_cast<int32>(TeleportForcedScheduleRetailId);
		return;
	}
	TeleportMoveTimer = 0.f;                                             // 1029a704
}

// =================================================================================================
// Slot 420
// =================================================================================================

void FElysiumNpc::NPCInit()
{
	// Every species class that fills slot 420 overrides it on its own C++ class (story 5 step 3; the
	// controller line since fold A2), so the Troika line's slot is its own body.
	TroikaNPCInit();
}

// =================================================================================================
// `0x10273ad0` — `CAI_BaseNPC::StartNPC`
// =================================================================================================

void FElysiumNpc::TroikaStartNPC()
{
	FElysiumNpcBase::StartNPC();                                                      // 10273ad0
	ThinkSet(StartNpcThinkFunction(), 0.0);                              // re-arm
	SetFollowerBoss(FollowerBossName);                                   // 102c44e0
	SetFollowerType(FollowerType);                                       // 102c4680
	Senses.SetClosestPlayer(*this, NpcKernelLifecycle19Shared::Lifecycle19Now(*this));               // 10293a80
}

void FElysiumNpc::StartNPC()
{
	// `CNPC_VCamera` (`0x10369930`) and `CNPC_VTzimisce` (`0x103b9270`) override slot 422 on their
	// C++ classes (story 5 step 3).
	TroikaStartNPC();
}

// =================================================================================================
// `0x1027bf50` / `0x102998c0` — OnRestore
// =================================================================================================

void FElysiumNpc::TroikaOnRestore(bool bFromLoad)
{
	// `102998cx`: `0x1029f610` validates a stored route against the node network (`0x10307ac0`) and
	// `0x1029f5d0` releases it when that fails — the two pairs `m_sppPatrolPath` and
	// `m_sppPatrolPathHunt`, each checked and released on its own. The network is this runtime's
	// node seam (`PatrolNodePosition`): a path any of whose ids no longer names a node is released.
	auto Revalidate = [this](FPatrolPathCell& Cell)
	{
		++PatrolPathRevalidations;
		if (Cell.Path == nullptr)                                        // 0x1029f614 / 0x1029f61c
		{
			return;
		}
		// `0x10307ac0(path, m_pNavigator->+0x2c)`: a null network answers false with nothing counted;
		// per node, `id < 0` answers false with nothing counted, `id >= count` bumps `DAT_106c994c`
		// and answers false, a null network slot answers false.
		bool bValid = World != nullptr;
		for (int32 Index = 0; bValid && Index < Cell.Path->Count && Index < PatrolPathNodeCapacity; ++Index)
		{
			const int32 NodeId = Cell.Path->Nodes[Index];
			if (NodeId < 0)                                                  // 0x10307ac0 `(int)id < 0`
			{
				bValid = false;
				break;
			}
			FVector Position = FVector::ZeroVector;
			const EPatrolNode Node = PatrolNodePosition(NodeId, Position);
			if (Node == EPatrolNode::OutOfRange)                             // `*network <= id`
			{
				++PatrolNodeMissCounter();                                   // `DAT_106c994c++`
				bValid = false;
				break;
			}
			if (Node != EPatrolNode::Found)                                  // `network[1][id] == 0`
			{
				bValid = false;
				break;
			}
		}
		if (!bValid)
		{
			ReleasePatrolPath(&Cell);                                    // 1029f5d0
			++PatrolPathReleases;
		}
	};
	Revalidate(PatrolPathCell);
	Revalidate(PatrolPathHuntCell);
	FElysiumNpcBase::OnRestore(bFromLoad);                                            // 1027bf50
	++RestorePlaceScans;
	CurrentSpotIndex = FindInterestingPlaceHoldingMe();                  // 102db5e0 -> +0x62ec
	if (RestorePedLinkNode != -1 && RestorePedLinkDestNode != -1)
	{
		++NodeIndexErrorCount();                                         // always OOB: no node array
	}
	const double Now = NpcKernelLifecycle19Shared::Lifecycle19Now(*this);
	ScheduleHost.ShootAtHintNode = 0;
	const float Jitter = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
		.FRandRange(ShootAtHintRearmMin, ShootAtHintRearmMax);
	ScheduleHost.NextShootAtHintSearchTime = Now + static_cast<double>(Jitter);
	SetFollowerBoss(FollowerBossName);                                   // 102c44e0
	SetFollowerType(FollowerType);                                       // 102c4680
	CombatStartActivityId = ResolveCombatStartActivity(CombatStartActivity);  // 1029f340 -> +0x65e4
	bSpawnCalled = true;                                                 // 10299xxx +0x62e9 = 1
	ValidateRestoredInterestingPlace();                                  // 10299a80
	if (bHidden)                                                         // 100b5190 +0x0f4
	{
		ThinkSet(nullptr, 0.0);
		NextThink = NeverThinkSentinel;
	}
	Slot593();
}

void FElysiumNpc::OnRestore(bool bFromLoad)
{
	// Four classes override slot 130 on their C++ classes (story 5 step 3); each calls
	// `TroikaOnRestore` first, the direct call retail makes.
	TroikaOnRestore(bFromLoad);
}

