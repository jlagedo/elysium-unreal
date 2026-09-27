#include "Substrate/ElysiumNpcCamera.h"

#include "ElysiumAnimEvent.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumOverlayStack.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumStub.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcDialogue.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcLifecycle19Shared.h"
#include "Substrate/ElysiumNpcLifecycle19_2Shared.h"
#include "Substrate/ElysiumNpcPrecache10Shared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcState19Shared.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSceneData.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// --- File-scope helpers moved with this class's bodies (story 5 step 4) ---

namespace
{
	// The camera's model fallback (`0x103689c0`), verbatim from `.rdata` `0x1062f790`.
	const TCHAR* const GCameraNullModel = TEXT("models/null.mdl");
}

// `CNPC_VCamera`'s constructor `0x10368060` writes both hull words at `0x1036807e`, after the
// `CAI_BaseNPC` constructor `0x1027c300` zeroed both; the port's constructor chain runs in the same
// order.
FElysiumNpcCamera::FElysiumNpcCamera()
{
	HullKind = 7;
	PathingHullKind = 7;
}

// Slots 497 / 506: `0x103681d0` / `0x103682f0`, one-byte `ret` bodies.
/** `0x103681d0` / `0x103682f0` — `CNPC_VCamera`'s empty slots 497 and 506. */
void FElysiumNpcCamera::Slot497()
{
	// `0x103681d0`, `CNPC_VCamera`'s slot 497 — ONE byte, a bare `ret`. The class replaces the
	// sound hook with nothing, so a security camera makes none of whatever sound slot 497 plays.
	// Reproduced as the empty body it is: the point of the row is that the base does NOT run.
}

void FElysiumNpcCamera::Slot506()
{
	// `0x103682f0`, `CNPC_VCamera`'s slot 506 — the same empty body at the other end of the same
	// sound-hook cluster. `CNPC_VCameraSecurity` inherits both.
}

// Slot 420: `0x103692c0`.
// `0x103692c0`
void FElysiumNpcCamera::NPCInit()
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
	if (BaseScheduleHost.bMotorAnimationMovement)
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
	BaseScheduleHost.bShouldMove = false;
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
	BaseScheduleHost.HintNode = INDEX_NONE;
	BaseScheduleHost.MemoryBits = 0;
	ElysiumNpcEnemy::SetEnemy(*this, FElysiumEntityHandle::Invalid());
	bKeepSound = false;
	Cognition.Conditions.Reset();
	SetDefaultEyeOffset();
	++BaseInitTailCalls;
	const double Now = NpcKernelLifecycle19_2Shared::Lifecycle19_2Now(*this);
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
	SetFrenziedWord(0);
	NpcInitTime = Now;
	WeaponBlockedByFriendTimer = 0.0;
	ExtendedBlockedByFriendTimer = static_cast<double>(NeverThinkSentinel);
	BaseMemory.EnemyOccludedCheck = 10;
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

// Slot 422: `0x10369930`.
// `0x10369930`
void FElysiumNpcCamera::StartNPC()
{
	// `0x10369930` — replacement, not a chain. Drop-to-floor only when the model is `null.mdl`.
	FString ModelName = Model;
	if (ModelName.IsEmpty())
	{
		ModelName = TEXT("");
	}
	if (ModelName.Equals(TEXT("models/null.mdl"), ESearchCase::IgnoreCase))
	{
		// `10369994 CALL [EDX + 0x804]` is slot 513 `CapabilitiesGet`, `TEST AL,0x4` — the same
		// capability bit the base body tests, NOT a solid-flags read.
		const int32 MoveType = GetMoveType();
		const uint32 Caps = static_cast<uint32>(CapabilitiesGet());
		const bool bSkipDrop = MoveType == 5 || MoveType == 6
			|| (Caps & CapabilityNoFloorDrop) != 0
			|| (static_cast<uint32>(SpawnFlags) & SpawnFlagNoFloorDrop) != 0;
		if (!bSkipDrop)
		{
			FVector OriginUnits = Origin / ElysiumMove::U;
			const bool bHit = MoveProbeFloorDrop(OriginUnits);
			if (!bHit)
			{
				++FloorDropWarnings;
				UE_LOG(LogElysiumNpcEnt, Warning,
					TEXT("NPC %s stuck in wall--level design error"),   // 105cc558
					Def != nullptr ? *Def->Classname : TEXT(""));
			}
			Origin = OriginUnits * ElysiumMove::U;
			++FloorDropPerformed;
		}
		else
		{
			Flags &= ~NpcKernelLifecycle19Shared::GFlOnGround;
			++FloorDropSkipped;
		}
	}
	if (!Target.IsEmpty())                                               // gated m_target != 0
	{
		FElysiumEntity* Found = World != nullptr ? World->FindByName(Target) : nullptr;
		BaseScheduleHost.GoalEnt = Found != nullptr ? Found->Handle : FElysiumEntityHandle();
		if (Found == nullptr)
		{
			UE_LOG(LogElysiumNpcEnt, Warning,
				TEXT("ReadyNPC()--%s couldn't find target %s"),        // 10369a58 105cc528
				Def != nullptr ? *Def->Classname : TEXT(""), *Target);
		}
		else
		{
			SetState(1);
			InstallScheduleRetail(StartNpcGoalEntityRetailId, false);
		}
	}
	InitSquad();
	const double Now = NpcKernelLifecycle19Shared::Lifecycle19Now(*this);
	if (Now <= MapFirstSecond)
	{
		ThinkSet(StartNpcThinkFunction(), 0.0);
		const float Jitter = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
			.FRandRange(StartNpcDelayMin, StartNpcDelayMax);
		ArmThinkAt(Now + static_cast<double>(Jitter));
	}
	else
	{
		ThinkSet(StartNpcThinkFunction(), 0.0);
		ArmThinkAt(Now);
	}
	ScriptArrivalActivity = static_cast<int32>(0xffffffff);
	ScriptArrivalSequence.Reset();
	if ((static_cast<uint32>(SpawnFlags) & SpawnFlagPreAimed) != 0)
	{
		SetState(1);
		SetActivity(1);
		InstallScheduleRetail(StartNpcAmbushRetailId, false);
	}
	ThinkSet(StartNpcThinkFunction(), 0.0);                              // final re-arm, no stamp
}

// Slot 104: `0x103689c0`.
// 0x103689c0 — CNPC_VCamera / …Security, via `CameraPrecacheModel`
void FElysiumNpcCamera::Precache()
{
	// `CNPC_VCamera::Precache` `0x103689c0`, shared with `CNPC_VCameraSecurity`. The model fallback
	// is family Lifecycle's `CameraPrecacheModel` (`models/null.mdl` when the keyfield is unset or
	// empty) and is called rather than re-recovered; the rest of the body is the TAIL that family
	// explicitly left for "a later story", which is this one.
	Model = CameraPrecacheModel(Model);
	NpcKernelPrecache10Shared::Precache10Model(*this, *Model, /*Preload=*/0);

	// The slot-452 reject arm, byte for byte the one `0x1027bb50` runs — except that retail reaches
	// `Msg` here and `DevMsg` there, the same format string `0x105cd21c`. Unreachable for the same
	// reason: `LoadedSchedules` answers true for every class in this runtime.
	if (!LoadedSchedules())
	{
		UE_LOG(LogElysiumNpcEnt, Error,
			TEXT("ERROR: Rejecting spawn of %s as error in NPC's schedules."), *DebugString());
		Kill();
		return;
	}

	// `m_iInterestingPlaceGroups = 0` (`+0x62dc`) — the camera clears its interesting-place group
	// mask at precache, so `AcceptsAmbientGroup` answers false for every place and a camera never
	// claims one. The authored STRING beside it is left alone: retail's write is to the parsed int,
	// and `0x10298910` has already run off the keyvalue by now.
	InterestingPlaceGroupMask = 0;

	// SEAM, restated from family Lifecycle: the AI-node link-table integrity check
	// (`0x102f9970` / `0x102f9920` / `0x102f9950`) and its five-line `"is being spawned after links
	// have been..."` `DevMsg`. This substrate stands no AI node graph and no link table, so there is
	// nothing to check and nothing the kernel reads changes.
}

// Slot 463: `0x10368ea0`, an EMPTY body that does not chain: a camera's state change writes nothing,
// not even the base state-flag byte. Inherited by `CNPC_VCameraSecurity`.
void FElysiumNpcCamera::OnStateChange(EElysiumNpcState OldState, EElysiumNpcState NewState)
{
	(void)OldState;
	(void)NewState;
}

// Slot 461: `0x10369060`, the ideal state HARDCODED to ALERT with no test (inherited by `CNPC_VCameraSecurity`).
int32 FElysiumNpcCamera::SelectIdealStateRetail()
{
	// `FUN_10369060`: three writes to the file/line ideal-state trace (`+0x1b38` / `+0x1b3c` /
	// `+0x1b40`, which the shape map records ABSENT) and then `m_IdealNPCState = 3` (ALERT). There is
	// NO test: a camera's ideal state is a constant, even on a dead camera.
	Mind.WriteIdealStateRetail(3);
	return IdealStateRetail();
}

// Slot 437: `0x10368f20`.
/** The three SPECIES bodies of slot 437 this story carries, each its class's `PreSelectSchedule`
 *  override's body (story 5 step 3): `CNPC_VCamera` / `CNPC_VCameraSecurity` (`0x10368f20`),
 *  `CNPC_VMingXiaoTentacle` (`0x1039de00`) and `CNPC_VPlaceholder` (`0x103a43f0`). Each writes
 *  retail's selector-trace tag and answers a fixed raw schedule number. */
// Slot 437: `0x10368f20`, the body of its class's `PreSelectSchedule` override (story 5 step 3).
int32 FElysiumNpcCamera::PreSelectSchedule()
{
	// CNPC_VCamera / CNPC_VCameraSecurity: `field_0x1b2c = 9; return 0x156;`
	RecordScheduleEvent(TEXT("PreSelectSchedule trace 9 (CNPC_VCamera 0x10368f20) -> 0x156"));
	return 0x156;
}

// Slot 438: `0x10368f40`, which replaces the whole selector (the Troika selector's species hook).
// Slot 438: `0x10368f40`, the body of its class's `SpeciesSelectSchedule` override (story 5 step 3).
int32 FElysiumNpcCamera::SpeciesSelectSchedule()
{
	// CNPC_VCamera / CNPC_VCameraSecurity: `field_0x1b2c = 9; return 0x156;` — the same hardcode
	// as its slot-437 body, so a camera never reaches the state switch at all.
	RecordScheduleEvent(TEXT("SelectSchedule trace 9 (CNPC_VCamera 0x10368f40) -> 0x156"));
	return 0x156;
}

// Slot 460: `0x10368f80`, a replacement that does not chain (inherited by `CNPC_VCameraSecurity`).
/** `CNPC_VCamera::PreSelectIdealState` (`0x10368f80`) / `CNPC_VDog::PreSelectIdealState`
 *  (`0x10374d80`). Reached from slot 460's species prologue. */
int32 FElysiumNpcCamera::PreSelectIdealStateRetail()
{
	SelectIdealStateSelector = 9;
	if (SquadDisconnected < 1 && SquadWord() != 0)
	{
		if (NpcKernelState19Shared::State19HasCondition(*this, EElysiumNpcCond::NewEnemy)
			|| NpcFlags.Has(EElysiumNpcFlag2::SQUAD_NEW_ENEMY))
		{
			NpcFlags.Clear(EElysiumNpcFlag2::SQUAD_NEW_ENEMY);
			if (GetEnemy() != nullptr)
			{
				++SelectIdealStateSquadNewEnemyCalls;
			}
		}
	}
	NpcKernelState19Shared::State19StampIdeal(*this, 3, 0x19c);
	return 3;
}

// Slot 337: `0x10368e80`.
int32 FElysiumNpcCamera::GetUsedHullBits()
{
	// A bare `return 0x80`: no call up the chain, so the Troika line's bit 0 is absent.
	// `CNPC_VCameraSecurity` inherits this body.
	return 0x80;
}

// Slot 546: `0x10368550`, the class's own schedule id space.
const TCHAR* FElysiumNpcCamera::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1093a7bc`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VCamera"), TEXT("0x10368550"), TEXT("0x1093a7bc") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}

// Slot 545: `0x10369bd0`, a replacement that does not chain (inherited by `CNPC_VCameraSecurity`).
/** `CNPC_VCamera::InitSquad` (`0x10369bd0`) — the body of `FElysiumNpcCamera::InitSquad`. */
bool FElysiumNpcCamera::InitSquad()
{
	// `CNPC_VCamera::InitSquad` `0x10369bd0`, the body of `FElysiumNpcCamera::InitSquad`.
	return InitSquadLine(/*bCameraArm*/ true);
}

// Slot 259: `0x10368ec0`, an empty replacement (inherited by `CNPC_VCameraSecurity`).
// `CNPC_VCamera::HandleAnimEvent` `0x10368ec0` — slot 259's camera body, an EMPTY body that
// swallows every animation event: the body of `FElysiumNpcCamera::HandleAnimEvent`.
bool FElysiumNpcCamera::HandleAnimEvent(const FElysiumAnimEvent& Event)
{
	// `CNPC_VCamera::HandleAnimEvent` `0x10368ec0` — three bytes, an empty body that ignores its
	// event id: the body of `FElysiumNpcCamera::HandleAnimEvent` (inherited by
	// `CNPC_VCameraSecurity`). A retail camera swallows every animation event, footsteps included;
	// the port answers "claimed" so no later chain handles it.
	(void)Event;
	return true;
}

// Slot 563: `0x10368ee0`, the `Empty` shape; a replacement that does not chain.
void FElysiumNpcCamera::TranslateEnemyChasePosition(FElysiumEntity* Enemy, FVector& ChasePositionCm,
	void* Tolerance, void* SecondTolerance)
{
	TranslateEnemyChasePositionShaped(EChaseTranslateShape::Empty, Enemy, ChasePositionCm, Tolerance,
		SecondTolerance);
}

// Slot 434: `0x10369100`, an empty replacement (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::PrescheduleThink()
{
}

// Slot 488: `0x103680b0`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::DeathSound()
{
}

// Slot 489: `0x103680d0`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::AlertSound()
{
}

// Slot 490: `0x103680f0`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::IdleSound()
{
}

// Slot 491: `0x10368110`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::PainSound()
{
}

// Slot 492: `0x10368130`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::FearSound()
{
}

// Slot 493: `0x10368150`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::LostEnemySound()
{
}

// Slot 494: `0x10368170`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::FoundEnemySound()
{
}

// Slot 495: `0x10368190`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::SurprisedSound()
{
}

// Slot 496: `0x103681b0`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::TargetAcquiredSound()
{
}

// Slot 498: `0x103681f0`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::FleeSound()
{
}

// Slot 499: `0x10368210`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::IdleAgitatedSound()
{
}

// Slot 500: `0x10368230`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::ExertHvySound()
{
}

// Slot 501: `0x10368250`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::ExertLightSound()
{
}

// Slot 502: `0x10368270`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::RiledSound()
{
}

// Slot 503: `0x10368290`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::ComfortSound()
{
}

// Slot 504: `0x103682b0`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::UpsetSound()
{
}

// Slot 505: `0x103682d0`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::TargetGiveUpSound()
{
}

// Slot 507: `0x10368310`, an empty body — the camera is silent (inherited by `CNPC_VCameraSecurity`).
void FElysiumNpcCamera::FloatSound()
{
}

// Slot 508: `0x10368330`, three bytes that pop the argument — the camera never speaks a sentence.
void FElysiumNpcCamera::SpeakSentence(int32 SentenceIndex)
{
	(void)SentenceIndex;
}

// --- Moved from `ElysiumNpcAnim.cpp` (story 5 step 4) ---

// --- Slot 259's camera body --------------------------------------------------------------------

// --- Moved from `ElysiumNpcLifecycle.cpp` (story 5 step 4) ---

FString FElysiumNpcCamera::CameraPrecacheModel(const FString& AuthoredModel)
{
	// 0x103689c0, shared with `CNPC_VCameraSecurity`: the model key falls back to `models/null.mdl`
	// through vtable `+0x350` (`SetModelName`) when it is unset OR empty — retail reads the key
	// three times to decide, which is one question.
	//
	// **Not ported here:** the tail. After `PrecacheModel` the body dispatches slot 452 (`+0x710`),
	// rejects the spawn outright (`Msg("ERROR: Rejecting spawn of %s as e...")` plus
	// `thunk_FUN_101cd940`) when it answers false, zeroes `m_iInterestingPlaceGroups` (`+0x62dc`),
	// and then runs the AI-node link-table integrity check (`0x102f9970` / `0x102f9920` /
	// `0x102f9950`) that `DevMsg`s "is being spawned after links have been...". **Unrecovered here:**
	// this substrate has no AI node graph and no link table, so there is nothing to check; the
	// rejection arm needs slot 452, which is a later story's.
	return AuthoredModel.IsEmpty() ? FString(GCameraNullModel) : AuthoredModel;
}

// --- Moved from `ElysiumNpcPrecache10.cpp` (story 5 step 4) ---

// -------------------------------------------------------------------------------------------------
// The arm story 29c-1 ported and left unwired.
// -------------------------------------------------------------------------------------------------
