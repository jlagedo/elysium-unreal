#include "Substrate/ElysiumNpcWerewolf.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumDecalSubsystem.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumSkeletalBasis.h"
#include "ElysiumStub.h"
#include "ElysiumVariant.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumDisciplines.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumHint.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcDialogue.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcCombat10Shared.h"
#include "Substrate/ElysiumNpcConditionsBodiesShared.h"
#include "Substrate/ElysiumNpcKernelTunables.h"
#include "Substrate/ElysiumNpcDamageShared.h"
#include "Substrate/ElysiumNpcDebug2Shared.h"
#include "Substrate/ElysiumNpcDebugShared.h"
#include "Substrate/ElysiumNpcHintsShared.h"
#include "Substrate/ElysiumNpcLifecycle19Shared.h"
#include "Substrate/ElysiumNpcLifecycle19_2Shared.h"
#include "Substrate/ElysiumNpcLifecycleShared.h"
#include "Substrate/ElysiumNpcMotor2Shared.h"
#include "Substrate/ElysiumNpcMotorShared.h"
#include "Substrate/ElysiumNpcPositions2Shared.h"
#include "Substrate/ElysiumNpcPrecache10Shared.h"
#include "Substrate/ElysiumNpcSensesBodiesShared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcSounds10Shared.h"
#include "Substrate/ElysiumNpcSpecies2Shared.h"
#include "Substrate/ElysiumNpcSpeciesLifecycle10Shared.h"
#include "Substrate/ElysiumNpcSpeciesMisc10_2Shared.h"
#include "Substrate/ElysiumNpcState19_2Shared.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumReactions.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumRetailHullTable.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// --- File-scope helpers moved with this class's bodies (story 5 step 4) ---

namespace
{
	const TCHAR* const GItemWerewolfAttacks = TEXT("item_w_werewolf_attacks");
	// `CNPC_VWerewolf`'s condition `0x7a`. It is above the base registrar's `0x76` and belongs to a
	// Werewolf-line table the census has not decoded, so it is spelled as the number.
	constexpr int32 GCondWerewolfDeathTriggered = 0x7a;
	// `m_Activity == 0x11d` and `m_DoorState == 1`, the pair the latch fires on.
	constexpr int32 GCondWerewolfDeathActivity = 0x11d;
	constexpr int32 GCondWerewolfDoorStateOpen = 1;
	EElysiumNpcCond CondOf(int32 RetailCondition)
	{
		return static_cast<EElysiumNpcCond>(RetailCondition);
	}
	// `_DAT_104454d0` = 0.5 — `DrawDebugHullAtPoint`'s hull midpoint scale.
	constexpr float GNpcKernelDebug2Half = 0.5f;
	const TCHAR* const GNpcKernelDebug2EntityBounds = TEXT("NDebugOverlay::EntityBounds");
	constexpr double GFakeHullPushIntervalSeconds = static_cast<double>(ElysiumNpcTunables::One);
	// `_DAT_10462914` = 1.25f — `UpdateFakeHull` scales the hull's own mins and maxs by it before it
	// builds the box around the `Bip01` bone point.
	constexpr float GFakeHullExtentScale = 1.25f;
	// `_DAT_10457f5c` = 500.0f — the force scale on the damage packet, applied to the bone point's
	// movement since the previous call. SOURCE units.
	constexpr float GFakeHullForceScale = 500.0f;
	// The packet `UpdateFakeHull` builds: `CTakeDamageInfo(this, this, 20.0, 1, 0, 0, -1)` with
	// `0x41a00000` = 20.0 as the scalar, then `0x101c2a10(2)`, `0x101c2b10(1)` and `0x101c2a50(1.0)`.
	constexpr float GFakeHullDamage = 20.0f;
	// The three knockback activities slot 323's classification picks between (`0x79` default, `0x7a`
	// for class 1, `0x7b` for class 3) and the bone `UpdateFakeHull` measures.
	constexpr int32 GFakeHullActivityDefault = 0x79;
	constexpr int32 GFakeHullActivityClassOne = 0x7a;
	constexpr int32 GFakeHullActivityClassThree = 0x7b;
	const TCHAR* const GFakeHullBoneName = TEXT("Bip01");
	// `vec3_invalid`, `DAT_10713de0/de4/de8`. `staticinit_101371a0` writes `0x7f7fffff` — `FLT_MAX` —
	// into all three, and `CNPC_VWerewolf::GetGroundpoint` (`0x103d6a40`) answers it for a ground
	// trace that did not hit. Recovered by family Motor, which owns `GetGroundpoint`.
	constexpr float GHintsVec3Invalid = MAX_FLT;
	// Retail's condition numbers, spelled as the registry numbers the bodies push. `EElysiumNpcCond`
	// carries the base table 0x00..0x76; 0x78 is above it and belongs to a Troika-line registrar the
	// census has not decoded, so it is cast rather than named.
	constexpr int32 GHintsCondMoveHintAvailable = 0x78;  // CNPC_VWerewolf's move-hint signal
	// `DAT_1070d1b0/b4/b8`, the triple `GetHintEndpoint` answers for a null hint.
	// `staticinit_101370b0` writes zero into all three, so it is `vec3_origin`.
	const FVector GHints10Vec3Origin = FVector::ZeroVector;
	// The rdtsc stand-in. This runtime has no cycle counter seam; `FPlatformTime::Cycles64` is the
	// same shape (a monotonic tick count) and the pair is a profiling aid with no game-visible
	// consumer, so the swap changes no event order. Named modernization.
	uint64 LifecycleCycles()
	{
		return FPlatformTime::Cycles64();
	}
	// The three hardcoded map entity names `0x103cade0` carries.
	const TCHAR* const GMiscWerewolfZoneName = TEXT("trigger_werewolf_zone");
	const TCHAR* const GMiscRotDoor1Name = TEXT("rotdoor1");
	const TCHAR* const GMiscRotDoor2Name = TEXT("rotdoor2");
	constexpr float GGroundpointDrop = 1000.0f;     // _DAT_10447ee0 / -_DAT_104d00ac
	// `vec3_invalid` — `DAT_10713de0/de4/de8`, which `staticinit_101371a0` fills with `0x7f7fffff`.
	// `GetGroundpoint`'s no-hit answer, and `CNPC_VWerewolf::GetForwardYawForHint`'s sentinel.
	constexpr float GVecInvalid = 3.4028234663852886e+38f;
	constexpr float GMotorTailTraceClearFraction = static_cast<float>(ElysiumNpcTunables::OneDouble);
	constexpr int32 GMotorTailGroundTraceMask = 0x202400b;
	// `FUN_103d0bf0`'s refresh interval, seconds.
	constexpr float NearestNodeRefreshSeconds = ElysiumNpcTunables::Hundredth;
	constexpr float GPositionsTailRetailZero = ElysiumNpcTunables::Zero;
	// `CNPC_VWerewolf::UpdateConditionCanTeleport` `0x103cc0d0`.
	constexpr float WerewolfCloseEnough = 800.0f;   // _DAT_10457ac4, Source units
	constexpr float WerewolfTeleportDistanceFloor = ElysiumNpcTunables::Hundred;
	// `CNPC_VWerewolf::TeleportIn`/`TeleportOut`'s effects and solid bits.
	constexpr uint32 GPositionsTailEffectNoDraw = 0x20;           // m_fEffects |= / &= ~
	constexpr uint32 GPositionsTailSolidNotSolid = 0x4;           // m_Collision's +0x2b4 |= / &= ~
	// Retail condition 0x77. This runtime's `EElysiumNpcCond` gained the enumerator with this
	// story; the numeric value is retail's own and is what `SetCondition`/`ClearCondition`
	// (`0x10269a20` / `0x10269b50`) index the 256-bit set with.
	constexpr EElysiumNpcCond CondCanTeleport = EElysiumNpcCond::CanTeleport;
	const TCHAR* const GWerewolfSoundDir = TEXT("sound/Character/Monster/Werewolf");
	const TCHAR* const GObservatorySoundDir = TEXT("sound/Area/Special/Observatory");
	const TCHAR* const GWerewolfSoundGroup = TEXT("Werewolf");
	// `m_iVSoundTableIdx` takes the literal 2 before the group row is looked up.
	constexpr int32 GWerewolfVSoundTableIndex = 2;
	// `0x1065f4d0`, four entries — and the listing annotates them `character/monster/TC_FatGuy/
	// Foot_Step1.wav` and `…Foot_Step2.wav`. The WEREWOLF precaches the Tzimisce fat guy's
	// footsteps: its own steps come through the sound GROUP it binds three lines earlier, and this
	// table is a retail copy-paste that is reproduced rather than corrected.
	const TCHAR* const GWerewolfFootsteps[] = {
		TEXT("character/monster/TC_FatGuy/Foot_Step1.wav"),
		TEXT("character/monster/TC_FatGuy/Foot_Step2.wav"),
		TEXT("character/monster/TC_FatGuy/Foot_Step3.wav"),
		TEXT("character/monster/TC_FatGuy/Foot_Step4.wav"),
	};
	const TCHAR* const GWerewolfWeapon = TEXT("item_w_werewolf_attacks");
	// `dev/ww_tele_out.wav` and `dev/ww_tele_in.wav` — a SLASH, not the underscore the checklist's
	// walk spells, and the same two names `TeleportOut`/`TeleportIn` play.
	const TCHAR* const GWerewolfTeleOut = TEXT("dev/ww_tele_out.wav");
	const TCHAR* const GWerewolfTeleIn = TEXT("dev/ww_tele_in.wav");
	// `_DAT_10450568` = **360.0**, the yaw wrap; `_DAT_104454c4` = **0.0**, its lower bound.
	constexpr float GYawWrapDegrees = 360.0f;
	// `_DAT_104492a4` = **60.0** Source units, the Z lift `InitializeHintData` applies before it
	// asks for a groundpoint.
	constexpr float GHintGroundpointLiftUnits = 60.0f;
	// `_DAT_10452dc4` = **2.0** seconds, the random-move arm's last-seen window.
	constexpr float GRandomMoveLastSeenWindow = 2.0f;
	// Both `CNPC_VWerewolf` arms push a literal `0` where the Troika body pushes `0x3fa00000`.
	constexpr float GSounds10WerewolfAttenuation = 0.0f;
	// `0x103d1e50`'s weighted distance and its threshold. The weighting is Source's classic
	// octagonal approximation of a length: the largest axis plus a quarter of the other two.
	constexpr float WerewolfDoorMinorAxisWeight = 0.25f;     // _DAT_1044bef8
	constexpr float WerewolfDoorMaxDistanceUnits = 135.0f;   // _DAT_104cf498
	// `0x103d1e50`'s two door-state numbers, read off family Hints' `WerewolfDoorState` (+0x6680).
	constexpr int32 WerewolfDoorStateNone = 0;
	constexpr int32 WerewolfDoorStateSettled = 2;
	// The three Werewolf schedules whose failure prints the diagnostic block (`103ce7d8`…).
	constexpr int32 GWerewolfDiagnosticSchedules[3] = { 0x160, 0x161, 0x162 };
	// The zone word's bits, in the order the overlay prints them (`103d51f3`…`103d52dd`).
	struct FWerewolfZoneBit { uint32 Bit; const TCHAR* Name; };
	constexpr FWerewolfZoneBit GWerewolfZoneBits[] =
	{
		{ 0x0001u, TEXT("ZONE_NO_TALL_ANIMS") },
		{ 0x0002u, TEXT("ZONE_PLAYER_ON_BREAKABLE") },
		{ 0x0040u, TEXT("ZONE_PLAYER_INSIDE") },
		{ 0x0080u, TEXT("ZONE_PLAYER_OUTSIDE") },
		{ 0x0004u, TEXT("ZONE_PLAYER_ON_PLATFORM") },
		{ 0x0800u, TEXT("ZONE_WOLF_INSIDE") },
		{ 0x1000u, TEXT("ZONE_WOLF_OUTSIDE") },
		{ 0x0100u, TEXT("ZONE_WOLF_ON_PLATFORM") },
	};
	// The five conditions, in the order the overlay tests them (`103d52dd`…`103d535f`). Note `0x7b`
	// comes BEFORE `0x7a`.
	struct FWerewolfCondLine { int32 Cond; const TCHAR* Name; };
	constexpr FWerewolfCondLine GWerewolfCondLines[] =
	{
		{ 0x77, TEXT("COND_VWEREWOLF_CAN_TELEPORT") },
		{ 0x78, TEXT("COND_VWEREWOLF_CAN_SPECIAL_MOVE") },
		{ 0x79, TEXT("COND_VWEREWOLF_ENEMY_REACHABLE") },
		{ 0x7b, TEXT("COND_VWEREWOLF_SHOULD_BREAKHINT") },
		{ 0x7a, TEXT("COND_VWEREWOLF_DEATH_TRIGGERED") },
	};
}

const FElysiumNpcClass* FElysiumNpcWerewolf::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(RetailClassName);
	return Row;
}

// Slot 420: `0x103caef0`.
// `0x103caef0`
void FElysiumNpcWerewolf::NPCInit()
{
	StatTemplate = TEXT("Werewolf");                                     // BEFORE the base
	TroikaNPCInit();
	StatTemplate = TEXT("Werewolf");                                     // and again after
	++InventoryDestroys;
	GiveBaseFightingItems();                                             // slot 304
	NpcKernelLifecycle19_2Shared::Lifecycle19_2HatePlayerClass(*this);
	TakeDamageMode = 1;
	Senses.Memory.NextFleeSoundTime = 0.0;
	NpcKernelLifecycle19_2Shared::Lifecycle19_2LawNever(*this);
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

// Slot 130: `0x103cabf0`, which calls the Troika body first.
// `0x103cabf0`
void FElysiumNpcWerewolf::OnRestore(bool bFromLoad)
{
	TroikaOnRestore(bFromLoad);
	WerewolfRearm();                                                     // 103cac20
}

// Slot 104: `0x103cb2a0`.
// 0x103cb2a0
void FElysiumNpcWerewolf::Precache()
{
	// `CNPC_VWerewolf::Precache` `0x103cb2a0` — a scope-trace frame carrying `m_iName` (`+0x26c`,
	// `"NULL ENTITY"` when `this` is null, which C++ cannot reach), the Troika body, TWO directory
	// globs, THREE state writes, the footstep table, the attacks weapon and two singles.
	TroikaPrecache();

	// Both globs pass `.wav` only, `bStarPrefix` CLEAR and the precache flag **1** (`PUSH 0x1 /
	// PUSH 0x0` — the reverse of the Troika body's pair). The observatory directory is precached by
	// the werewolf because the Observatory fight is where one stands.
	PrecacheDirectory(GWerewolfSoundDir, NpcKernelPrecache10Shared::GExtWav, /*bStarPrefix=*/false, /*Flag=*/1);
	PrecacheDirectory(GObservatorySoundDir, NpcKernelPrecache10Shared::GExtWav, /*bStarPrefix=*/false, /*Flag=*/1);

	// The sound-group binding, in retail's write order: the table index FIRST, then the group name,
	// then the row the name resolves to. The same triple `CNPC_VZombie::SetModel` makes.
	VSoundTableIndex = GWerewolfVSoundTableIndex;             // +0x00bc := 2
	VSoundGroupName = GWerewolfSoundGroup;                    // +0x00c0 := "Werewolf"
	VSoundGroupRow = VSoundGroupRowFor(*VSoundGroupName);     // +0x00b4 := 0x101f55a0(...)

	NpcKernelPrecache10Shared::Precache10SoundTable(*this, GWerewolfFootsteps, UE_ARRAY_COUNT(GWerewolfFootsteps));
	NpcKernelPrecache10Shared::Precache10Other(*this, GWerewolfWeapon);
	NpcKernelPrecache10Shared::Precache10Sound(*this, GWerewolfTeleOut);
	NpcKernelPrecache10Shared::Precache10Sound(*this, GWerewolfTeleIn);
}

// Slot 461: `0x103d0820`, chaining the Troika body directly.
int32 FElysiumNpcWerewolf::SelectIdealStateRetail()
{
	SelectIdealStateSelector = 0x29;
	GatherConditions();
	if (!IsAlive() || NpcKernelState19_2Shared::State19_2HasCondition(*this, EElysiumNpcCond::WerewolfDead))
	{
		NpcKernelState19_2Shared::State19_2Stamp(*this, 7, 0xaec);
		SetState(IdealStateRetail());
		return IdealStateRetail();
	}
	if (!WerewolfShouldPursueEnemy())
	{
		NpcKernelState19_2Shared::State19_2Stamp(*this, 9, 0xaf3);
		SetState(IdealStateRetail());
		return IdealStateRetail();
	}
	if (GetEnemy() == nullptr)
	{
		return NpcKernelState19_2Shared::State19_2ChainTroika(*this);
	}
	if (NpcKernelState19_2Shared::State19_2HasCondition(*this, EElysiumNpcCond::CanMeleeAttack1)
		|| NpcKernelState19_2Shared::State19_2HasCondition(*this, EElysiumNpcCond::CanMeleeAttack2))
	{
		NpcKernelState19_2Shared::State19_2Stamp(*this, 2, 0xafb);
	}
	else if (NpcKernelState19_2Shared::State19_2HasCondition(*this, EElysiumNpcCond::DogCombatLatch)
		|| NpcKernelState19_2Shared::State19_2HasCondition(*this, EElysiumNpcCond::EnemyUnreachable))
	{
		NpcKernelState19_2Shared::State19_2Stamp(*this, 0xb, 0xb00);
	}
	else
	{
		NpcKernelState19_2Shared::State19_2Stamp(*this, 2, 0xb05);
	}
	SetState(IdealStateRetail());
	return IdealStateRetail();
}

// Slot 201: `0x103cb810`
bool FElysiumNpcWerewolf::FVisible(FElysiumEntity* SeenTarget, int32 Mask, FElysiumEntity* Blocker, int32 Arg4)
{
	// `CNPC_VWerewolf#201`, story 29c-1's `WerewolfFVisible`. Dispatched, not re-ported.
	FElysiumEntityHandle Unused;
	return WerewolfFVisible(SeenTarget, &Unused);
}

// Slot 448: `0x103ce750`, its own arm and then a direct call into the Troika body `0x1029adb0`.
/** `CNPC_VWerewolf::TaskFail` (`0x103ce750`), slot 448's species body. The DIAGNOSTIC half runs only
 *  when the running schedule (`+0x5c38`) is one of `0x160`, `0x161` or `0x162`; the rest is
 *  unconditional: `SetHullSizeSmall(1)`, `ClearMoveHint`, `ClearTeleportHint`, `m_pBreakHint` = 0,
 *  `+0x66a4` = 0, `+0x66a1` = 1, the Troika base `0x1029adb0`, `CheckStuck(0)`, then the zone word
 *  `+0x66e8` = 0 and `+0x6708` / `+0x670c` = -1.
 *
 *  The BREAK line prints the TELEPORT hint — a retail copy-paste bug, reproduced. */
void FElysiumNpcWerewolf::TaskFail(int32 Reason)
{
	// `103ce7d8`: the DIAGNOSTIC half runs only when the running schedule (`+0x5c38`) equals one of
	// `0x160`, `0x161` or `0x162` resolved through `0x102cc1f0`.
	// `ElysiumScheduleNumber` is this runtime's retail schedule number. The three ids `0x160`,
	// `0x161` and `0x162` are `CNPC_VWerewolf`'s own programs and have no row in
	// `int32` yet, so the comparison is made on the NUMBER and simply never matches
	// today — which is retail's own arm for a Werewolf running anything else. Named, not stubbed.
	const int32 Running = GetLocalScheduleId(Schedule.Current);
	bool bDiagnostic = false;
	for (const int32 Id : GWerewolfDiagnosticSchedules)
	{
		bDiagnostic = bDiagnostic || (Running != 0 && Running == Id);
	}
	if (bDiagnostic)
	{
		// `103ce838`: `DevWarning("Werewolf failed task '%32s' schedule...")` with the task name
		// from slot `0x704` over the current task, the schedule name at `schedule+0x40` and the
		// failure text from `0x10316fa0`. `INVALID TASK` when there is no current task.
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("Werewolf failed task '%d' schedule %d, reason %d"),
			Schedule.TaskIndex, Running, Reason);
		// `103ce87d` / `103ce89a` / `103ce8b7`: the three hint lines. The BREAK line is GATED on
		// `m_pBreakHint` but PRINTS `m_pTeleportHint` — a retail copy-paste bug, reproduced.
		if (MoveHintNode != INDEX_NONE)
		{
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("Current MOVE Hint: %d"), MoveHintNode);
		}
		if (TeleportHintNode != INDEX_NONE)
		{
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("Current TELEPORT Hint: %d"), TeleportHintNode);
		}
		if (WerewolfBreakHintNode != INDEX_NONE)
		{
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("Current BREAK Hint: %d"), TeleportHintNode);
		}
	}
	// `103ce8c6`: everything below is UNCONDITIONAL, including on the non-diagnostic path.
	SetHullSizeSmall(/*bForce=*/true);
	ClearMoveHint();
	ClearTeleportHint();
	WerewolfBreakHintNode = INDEX_NONE;
	// `103ce8e4`: `+0x66a4 = 0` — family Lifecycle's `WerewolfMorphTimerA`.
	WerewolfMorphTimerA = 0.f;
	// `103ce8eb`: `+0x66a1 = 1`.
	bWerewolfTaskFailed = true;
	// `103ce8fb`: the Troika base `0x1029adb0`, BEFORE `CheckStuck` and the three word writes below
	// (story 5 step 4r: the port ran it last).
	FElysiumNpc::TaskFail(Reason);
	// `103ce900`: `CheckStuck(0)` through thunk `0x10012d46`, which is `CNPC_VWerewolf::CheckStuck`
	// `0x103cb920` (story 5 step 4 CORRECTION: the port called `CNPC_VSabbatLeader::CheckStuck`
	// `0x103ab580`, a class the Werewolf does not derive from).
	WerewolfCheckStuck(EStuckEscape::WarnOnly);
	// `103ce909`: the zone word cleared, then both hint-node caches to -1.
	WerewolfHintFlags = 0;
	WerewolfHintNodeCacheA = INDEX_NONE;
	RandomMoveHintNodeZone = INDEX_NONE;
}

// Slot 304: `0x103cc9b0`
/** `CNPC_VWerewolf::GiveBaseFightingItems` (`0x103cc9b0`) and `::RemoveBaseFightingItems`
 *  (`0x103cca80`), slot 304's and 305's one species pair. The Werewolf REPLACES the base rather than
 *  extending it: where the base gates on slots 307 and 308 BOTH answering false before giving
 *  `item_w_fists`, the Werewolf runs `Inventory_Find("item_w_werewolf_attacks")` and, only when
 *  absent, gives that item and sets misc flag `0x10`. It never consults the two base gates and never
 *  grants fists.
 *
 *  Reached by a spawned `npc_VWerewolf` since story 5 step 2 registered the classname as
 *  `CNPC_VWerewolf` (factory `0x103c8760`); before, the census gave the class no classname. */
void FElysiumNpcWerewolf::GiveBaseFightingItems()
{
	// `CNPC_VWerewolf::GiveBaseFightingItems` `0x103cc9b0`, 155 bytes of which 99 are the scope-trace
	// frame. The arm REPLACES the base: no slot-307 gate, no slot-308 gate, and never fists.
	// `103cca32`: `Inventory_Find("item_w_werewolf_attacks")`, and only an ABSENT item grants.
	if (InventoryFindByClassname(GItemWerewolfAttacks))
	{
		return;
	}
	// `103cca44`: `thunk_FUN_1021fe50(this, "item_w_werewolf_attacks", 0)` then `AddMiscFlag(0x10)`.
	GiveNamedFightingItem(GItemWerewolfAttacks);
	ElysiumMiscFlags::Set(MiscFlags, MiscFlagBaseFightingItems);
}

// Slot 305: `0x103cca80`
void FElysiumNpcWerewolf::RemoveBaseFightingItems()
{
	// `CNPC_VWerewolf::RemoveBaseFightingItems` `0x103cca80`, 146 bytes. Mirrors the base's SHAPE
	// with its own item: the flag gate is the base's, the classname is the Werewolf's.
	if (!ElysiumMiscFlags::Has(MiscFlags, MiscFlagBaseFightingItems))
	{
		return;
	}
	RemoveNamedFightingItem(GItemWerewolfAttacks);
	ElysiumMiscFlags::Clear(MiscFlags, MiscFlagBaseFightingItems);
}

// Slot 440: `0x103d5e00`.
// `0x103d5e00`
// `0x103d5e00`, `CNPC_VWerewolf::TranslateSchedule`, the body of `FElysiumNpcWerewolf::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpcWerewolf::TranslateScheduleRetail(int32 ScheduleNumber)
{
	// `103d5e00`: three rows in retail's order. `0x43 SCHED_FAIL` is one of them, so a
	// werewolf never runs the base FAIL program. Retail stamps its `__FILE__`/`__LINE__`
	// (lines `0x1134`, `0x112f`, `0x112a`) into `+0x1b30`/`+0x1b34` on each; the shape map
	// calls that pair ABSENT.
	struct FWolfRow { int32 From; int32 To; };
	static const FWolfRow WolfRows[] = {
		{ 0x43, 0x15c }, { 0xb7, 0x157 }, { 0xf0, 0xb1 },
	};
	for (const FWolfRow& Row : WolfRows)
	{
		if (Row.From == ScheduleNumber)
		{
			return Row.To;
		}
	}
	return TroikaTranslateScheduleRetail(ScheduleNumber);
}

// Slot 516: `0x103d0a30`, a scope-trace push/pop (the trace words are ABSENT in the shape map)
// around a direct call into the Troika body `0x10297ce0`.
float FElysiumNpcWerewolf::MaxYawSpeed()
{
	return FElysiumNpc::MaxYawSpeed();
}

// Slot 68: `0x103d9ab0`. `m_edtDerivedType & 0x16`, then `GetFlags2() & 8`; otherwise a direct call
// into the Troika body `0x1029afc0`.
bool FElysiumNpcWerewolf::ShouldIgnoreCollision(FElysiumEntity* Other)
{
	if (Other != nullptr)
	{
		if ((RetailDerivedType(*Other) & 0x16) != 0)
		{
			return true;
		}
		if ((RetailFlags2(*Other) & 8) != 0)
		{
			return true;
		}
	}
	return FElysiumNpc::ShouldIgnoreCollision(Other);
}

// Slot 69: `0x103d9ba0`, the same two gates falling to the NAV base `0x1029b180`.
bool FElysiumNpcWerewolf::NavIgnoreCollision(FElysiumEntity* Other)
{
	if (Other != nullptr)
	{
		if ((RetailDerivedType(*Other) & 0x16) != 0)
		{
			return true;
		}
		if ((RetailFlags2(*Other) & 8) != 0)
		{
			return true;
		}
	}
	return FElysiumNpc::NavIgnoreCollision(Other);
}

// Slot 620: `0x103d5050`, a virtual `CNPC_VWerewolf` introduces (no Troika-line body holds the slot).
/** `CNPC_VWerewolf::DrawBBoxOverlay` (`0x103d5050`) — slot 620, introduced by `CNPC_VWerewolf`
 *  alone: the body of `FElysiumNpcWerewolf::DrawBBoxOverlay`, that class's own virtual. */
void FElysiumNpcWerewolf::DrawBBoxOverlay()
{
	// `0x103d5050`, slot 620, filled by `CNPC_VWerewolf` alone:
	//
	//     scope trace push { "CNPC_VWerewolf::DrawBBoxOverlay", m_iName ? m_iName : "", "" }
	//     if (!ShouldPursueEnemy())
	//         NDebugOverlay::EntityBounds(this, 50, 255, 50, 0, 0);     // 0x10142e20
	//     else
	//         CBaseEntity::DrawBBoxOverlay();
	//     scope trace pop
	//
	// So a werewolf that is NOT pursuing gets a green box drawn for it here, and a pursuing one
	// falls through to the ordinary whole-entity box. `WerewolfShouldPursueEnemy` is family Senses'
	// port of `0x103cf5f0`.
	//
	// Slot 620 has no Troika-line body: `CNPC_VWerewolf` introduces it, so this is the body of
	// `FElysiumNpcWerewolf::DrawBBoxOverlay`, that class's own virtual (story 5 step 3).
	UE_LOG(LogElysiumNpcEnt, VeryVerbose, TEXT("CNPC_VWerewolf::DrawBBoxOverlay %s"),
		TargetName.IsEmpty() ? TEXT("") : *TargetName);
	if (!WerewolfShouldPursueEnemy())
	{
		EmitOverlayEntityBounds(GNpcKernelDebug2EntityBounds, 50, 255, 50, 0);
		return;
	}
	EntityDrawBBoxOverlay();
}

// Slot 408: `0x103d0640`, whose miss calls `CAI_BaseNPC::GetShortConditionName` (`0x1027ede0`) directly.
const TCHAR* FElysiumNpcWerewolf::GetShortConditionName(int32 ConditionId)
{
	// The one slot-408 body wrapped in a scope trace: `g_ScopeTraceStack[depth] = {
	// "CNPC_VWerewolf::GetShortConditionName", m_iName ? m_iName : "", "" }` then `++depth`,
	// popped on EVERY arm including the forward. The port has no scope-trace stack; the push is
	// recorded so the arm is visible and the entity it names is the one retail names.
	UE_LOG(LogElysiumNpcEnt, VeryVerbose, TEXT("CNPC_VWerewolf::GetShortConditionName %s"),
		TargetName.IsEmpty() ? TEXT("") : *TargetName);
	// The class's own block over ids 0x77..0x7b, read from `.rdata` `0x106623d0` down to
	// `0x106623c0`.
	static const TCHAR* const Names[] = {
		TEXT("ww0"), TEXT("ww1"), TEXT("ww2"), TEXT("ww3"), TEXT("ww4") };
	const int32 Offset = ConditionId - 0x77;
	if (Offset >= 0 && Offset < UE_ARRAY_COUNT(Names))
	{
		return Names[Offset];
	}
	// The `default:` arm: `CAI_BaseNPC::GetShortConditionName` (`0x1027ede0`) directly.
	return FElysiumNpc::GetShortConditionName(ConditionId);
}

// Slot 76: `0x103d5130`, which chains `CNPC_VBaseBoss::DrawDebugStatOverlays` (`0x10366290`) directly.
/** `CNPC_VWerewolf::DrawDebugStatOverlays` (`0x103d5130`) — the body of
 *  `FElysiumNpcWerewolf::DrawDebugStatOverlays`, chaining the boss body directly. */
void FElysiumNpcWerewolf::DrawDebugStatOverlays()
{
	// `CNPC_VWerewolf::DrawDebugStatOverlays` (`0x103d5130`), the body of
	// `FElysiumNpcWerewolf::DrawDebugStatOverlays`: it PREPENDS two lines, CHAINS
	// `CNPC_VBaseBoss::DrawDebugStatOverlays` (`0x10366290`) directly, then APPENDS the zone word, the
	// five conditions, the door state, the hint dump and the schedule stack.
	TArray<FString> Lines;
	WerewolfDrawDebugStatOverlays(Lines);
	// `103d51ee`: the two prepended lines come out FIRST, then the boss body runs, then the rest.
	// `WerewolfDrawDebugStatOverlays` builds the whole list in retail's order; the chain point is
	// here, between line 2 and line 3.
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		if (Index == 2)
		{
			BossDrawDebugStatOverlays();
		}
		EmitDebugMsg(TEXT("%s"), Lines[Index]);
	}
	if (Lines.Num() < 3)
	{
		BossDrawDebugStatOverlays();
	}
}

// Slot 465: `0x103d5f60`, ending in a direct call into `CAI_BaseNPCTroika::OnChangeActivity` (`0x10295a60`).
// `0x103d5f60`
void FElysiumNpcWerewolf::OnChangeActivity(int32 Activity)
{
	// `CNPC_VWerewolf::OnChangeActivity`, 126 bytes: the scope-trace push/pop and an
	// unconditional forward. There is no species behaviour here at all, and recording that is
	// the point — a reader looking for one stops at this line.

	FElysiumNpc::OnChangeActivity(Activity);   // `0x10295a60`, direct
}

// Slot 337: `0x103cab50`.
int32 FElysiumNpcWerewolf::GetUsedHullBits()
{
	// A bare `return 0x1000`: no call up the chain, so the Troika line's bit 0 is absent.
	return 0x1000;
}

// Slot 566: `0x103d7ce0`, a replacement that does not chain.
bool FElysiumNpcWerewolf::FValidateHintType(void* Hint)
{
	// `t != 0x3a9f && 14999 < t && t < 0x3aab`: 15000..15018 except 15007.
	// Retail dereferences the hint unchecked; a hint the seam could not resolve reads type 0
	// here, which the rule refuses either way.
	const FHintWords* Words = static_cast<const FHintWords*>(Hint);
	const int32 HintType = Words != nullptr ? Words->HintType : 0;
	return HintType != 0x3a9f && 14999 < HintType && HintType < 0x3aab;
}

// Slot 546: `0x103c8ed0`, the class's own schedule id space.
const TCHAR* FElysiumNpcWerewolf::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1093d6d4`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VWerewolf"), TEXT("0x103c8ed0"), TEXT("0x1093d6d4") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}

// Slot 141: `0x103ccbf0`, a prologue ahead of a direct call into `CAI_BaseNPC::TraceAttack` (`0x10266780`).
/** `CNPC_VWerewolf::TraceAttack` (`0x103ccbf0`) and `CNPC_VZombie::TraceAttack` (`0x103e0430`) — the
 *  bodies of their classes' overrides: the prologue, then the base body directly. */
void FElysiumNpcWerewolf::TraceAttack(void* InInfo, const FVector& DirUnits, void* InTrace)
{
	// `0x103ccbf0`, the body of `FElysiumNpcWerewolf::TraceAttack`: `thunk_FUN_101c2a50(info, 0)` —
	// zero the packet's damage-type word — then the base body `0x10266780` directly. Nothing else.
	FElysiumTakeDamageInfo* Info = static_cast<FElysiumTakeDamageInfo*>(InInfo);
	if (Info == nullptr || InTrace == nullptr)
	{
		return;   // the port's one refusal: retail would have dereferenced both
	}
	Info->DamageBits = 0;
	FElysiumNpc::TraceAttack(InInfo, DirUnits, InTrace);
}

// Slot 563: `0x103d9e00`, the `GoalToleranceWerewolfLead` shape; a replacement that does not chain.
void FElysiumNpcWerewolf::TranslateEnemyChasePosition(FElysiumEntity* Enemy, FVector& ChasePositionCm,
	void* Tolerance, void* SecondTolerance)
{
	TranslateEnemyChasePositionShaped(EChaseTranslateShape::GoalToleranceWerewolfLead, Enemy, ChasePositionCm, Tolerance,
		SecondTolerance);
}

// Slot 435: `0x103ced10`, the Troika body `0x102a0940` directly, then the class's own tail.
// `0x103ced10`
void FElysiumNpcWerewolf::OnScheduleChange(int32 NewSchedule)
{
	// `CNPC_VWerewolf::OnScheduleChange` `0x103ced10`: the Troika body directly, first.
	FElysiumNpc::OnScheduleChange(NewSchedule);
	if (WerewolfScheduleStack.Num() > 0x32) // 0x103ced8a
	{
		WerewolfScheduleStack.RemoveAt(0, 1, EAllowShrinking::No); // 0x103ceda3
	}
	WerewolfScheduleStack.Add(NewSchedule == ElysiumScheduleId::None
			? FString()
			: FString(ElysiumScheduleName(NewSchedule))); // 0x103cee0e
}

// Slot 491: `0x103d87a0`, a replacement that does not chain.
/** `CNPC_VWerewolf::vfunc491` (`0x103d87a0`) and `vfunc500` (`0x103d8660`) — the bodies of the
 *  Werewolf's `PainSound` / `ExertHvySound` overrides: the same concept at the Werewolf's attenuation,
 *  with no call into the Troika body. */
// The species arm above, `FElysiumNpcWerewolf::PainSound`'s body (story 5 step 3).
void FElysiumNpcWerewolf::PainSound()
{
	SpeakSoundConcept(NpcKernelSounds10Shared::GSounds10ConceptPain, GSounds10WerewolfAttenuation);
}

// Slot 500: `0x103d8660`, a replacement that does not chain.
// The species arm above, `FElysiumNpcWerewolf::ExertHvySound`'s body (story 5 step 3).
void FElysiumNpcWerewolf::ExertHvySound()
{
	SpeakSoundConcept(NpcKernelSounds10Shared::GSounds10ConceptExertHeavy, GSounds10WerewolfAttenuation);
}

// Slot 561: `0x103d02b0` — the zone melee suppression, else `CAI_BaseNPC::GatherAttackConditions`
// (`0x1026dd10`) directly.
void FElysiumNpcWerewolf::GatherAttackConditions(FElysiumEntity* Enemy, float DistanceUnits)
{
	if (ElysiumNpcCond::WerewolfZoneSuppressesMelee(*this, Cognition.Conditions))
	{
		return;
	}
	FElysiumNpc::GatherAttackConditions(Enemy, DistanceUnits);
}

// --- Moved from `ElysiumNpcCombat10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcConditionsBodies.cpp` (story 5 step 4) ---

void FElysiumNpcWerewolf::UpdateConditionDeathTriggered()
{
	// The body, in order — and the order is the point.
	//
	// `ClearCondition(0x7a)` runs FIRST and UNCONDITIONALLY. The `if (!HasCondition(0x7a))` that
	// guards the output therefore ALWAYS passes: the "fire once" the guard reads as is not one, and
	// `m_OnConditionDeathTriggered` fires on EVERY pass while the activity/door pair holds. That is
	// the shipped behaviour and it is reproduced; the guard is kept in place rather than removed so
	// a reader can see what it was meant to be.
	Cognition.Conditions.Clear(CondOf(GCondWerewolfDeathTriggered));

	if (CurrentRetailActivityId() != GCondWerewolfDeathActivity   // m_Activity +0xfec == 0x11d
		|| WerewolfDoorState != GCondWerewolfDoorStateOpen)       // m_DoorState +0x6680 == 1
	{
		return;
	}

	if (!Cognition.Conditions.Has(CondOf(GCondWerewolfDeathTriggered)))
	{
		// `thunk_FUN_10265a90(this, GetEnemy())` — retail's "myself" self-reference write, then the
		// output with the enemy as activator and this NPC as caller.
		const FElysiumEntity* Enemy = World != nullptr && Senses.Memory.Enemy.IsSet()
			? World->Resolve(Senses.Memory.Enemy) : nullptr;
		FireOutput(FName(TEXT("OnConditionDeathTriggered")),
			Enemy != nullptr ? Enemy->Handle : FElysiumEntityHandle::Invalid());
	}
	Cognition.Conditions.Set(CondOf(GCondWerewolfDeathTriggered));
}

// --- Moved from `ElysiumNpcDamage.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcDebug.cpp` (story 5 step 4) ---

void FElysiumNpcWerewolf::EmitOverlayEntityBounds(const TCHAR* RetailCall, int32 R, int32 G, int32 B,
	int32 A) const
{
	NpcKernelDebugShared::GNpcKernelDebugRecord(NpcKernelDebugShared::GNpcKernelDebugChannelOverlay, RetailCall,
		FString::Printf(TEXT("%s rgba=(%d %d %d %d)"), *DebugString(), R, G, B, A), INDEX_NONE);
}

// --- Moved from `ElysiumNpcDebug2.cpp` (story 5 step 4) ---

void FElysiumNpcWerewolf::DrawDebugHullAtPoint(const FVector& PointUnits, float Duration) const
{
	// `0x103d4820`. `RET 0x10`: a `Vector` by value and the overlay duration.
	//
	//     scope trace push { "CNPC_VWerewolf::DrawDebugHullAtPoint", m_iName ? m_iName : "", "" }
	//     mins = NAI_Hull::Mins(m_eHull);   maxs = NAI_Hull::Maxs(m_eHull);
	//     halfX = (maxs.x + mins.x) * 0.5;  halfY = (maxs.y + mins.y) * 0.5;   // _DAT_104454d0
	//     NDebugOverlay::Box(point, mins, maxs, 255, 100, 0, 100, duration);
	//     NDebugOverlay::Line?(…, sqrt(halfX*halfX + halfY*halfY), 5.0, 255, 255, 0, 20, 0);
	//     scope trace pop
	//
	// The second call (`0x1000566e`) takes the hull's planar RADIUS and the constant `5.0`
	// (`0x40a00000`) with colour (255, 255, 0) at alpha 20; which `NDebugOverlay` entry point it is
	// remains **UNRECOVERED** — the argument shape fits a circle or a swept box and the listing does
	// not name it. It is emitted under its address so the arm is visible.
	//
	// **SEAM**: `RetailHullExtents` (family Motor) answers false with both extents at zero, so the
	// box is degenerate and the radius is 0.
	UE_LOG(LogElysiumNpcEnt, VeryVerbose, TEXT("CNPC_VWerewolf::DrawDebugHullAtPoint %s"),
		TargetName.IsEmpty() ? TEXT("") : *TargetName);
	FVector HullMins = FVector::ZeroVector;
	FVector HullMaxs = FVector::ZeroVector;
	RetailHullExtents(HullKind, EElysiumHullExtents::Full, HullMins, HullMaxs);
	EmitOverlayBox(NpcKernelDebug2Shared::GNpcKernelDebug2Box, PointUnits, HullMins, HullMaxs, 255, 100, 0, 100);
	const float HalfX = (HullMaxs.X + HullMins.X) * GNpcKernelDebug2Half;
	const float HalfY = (HullMaxs.Y + HullMins.Y) * GNpcKernelDebug2Half;
	const float Radius = FMath::Sqrt(HalfX * HalfX + HalfY * HalfY);
	EmitOverlayLine(TEXT("0x1000566e"), PointUnits,
		PointUnits + FVector(Radius, 0.f, 5.f), 255, 255, 0, false);
	(void)Duration;
}

// --- Moved from `ElysiumNpcFacing.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcGeometry.cpp` (story 5 step 4) ---

bool FElysiumNpcWerewolf::BoxesOverlap(const FVector& AMin, const FVector& AMax, const FVector& BMin,
	const FVector& BMax)
{
	// `FUN_10240250`, verbatim and in retail's order: X max, X min, Y max, Y min, Z max, Z min.
	// Every comparison is inclusive (`>=` / `<=`), so two boxes that share a face overlap.
	return BMax.X >= AMin.X && BMin.X <= AMax.X
		&& BMax.Y >= AMin.Y && BMin.Y <= AMax.Y
		&& BMax.Z >= AMin.Z && BMin.Z <= AMax.Z;
}

int32 FElysiumNpcWerewolf::FakeHullKnockbackActivity(int32 DirectionClass)
{
	// `103d971e`..`103d973c`: `EBP = 0x79` before the test, `DEC EAX; JZ -> 0x7a`,
	// `SUB EAX,2; JNZ -> keep 0x79`, else `0x7b`. So class 1 answers `0x7a`, class 3 answers `0x7b`
	// and everything else — including 2 — answers `0x79`.
	switch (DirectionClass)
	{
	case 1:  return GFakeHullActivityClassOne;
	case 3:  return GFakeHullActivityClassThree;
	default: return GFakeHullActivityDefault;
	}
}

int32 FElysiumNpcWerewolf::FakeHullDebugCvar() const
{
	// `DAT_1093f73c` `+0x2c`: `werewolf_show_debug`, shipped "0", which closes the debug draw.
	return ElysiumNpcTunables::ConVarInt(ElysiumNpcTunables::EConVar::WerewolfShowDebug);
}

FElysiumEntity* FElysiumNpcWerewolf::FakeHullPushTarget() const
{
	// SEAM for `GetEnemy()->+0xa8`. **Unrecovered** which field that is. Slot 167 (`+0x29c`) is the
	// CONST `GetEnemy`, which is the one this body dispatches.
	return GetEnemy();
}

bool FElysiumNpcWerewolf::PushedEntityKnockback(FElysiumEntity* Pushed, int32 Activity)
{
	// Slot 320 `PlayerKnockbackReaction(CBaseCombatCharacter*, Activity)`, also on the pushed
	// entity, with THIS npc as the attacker argument.
	++FakeHullSeams.KnockbackCalls;
	FakeHullSeams.LastKnockbackActivity = Activity;
	if (FElysiumNpc* PushedNpc = Pushed ? Pushed->AsNpc() : nullptr)
	{
		return PushedNpc->PlayerKnockbackReaction(this, Activity);
	}
	return false;
}

void FElysiumNpcWerewolf::PushFakeHullDamage(FElysiumEntity* /*Pushed*/, float /*Damage*/,
	const FVector& ForceUnits, const FVector& /*PositionUnits*/)
{
	// SEAM for `CBaseEntity::TakeDamage`. The FORCE and the POSITION are the recovered halves and
	// the ledger carries them; this runtime's damage path (`ElysiumDamage::Apply`) needs a
	// `FElysiumDmg` descriptor and a dice context a geometry body has no source for.
	++FakeHullSeams.DamagePushes;
	FakeHullSeams.LastDamageForceUnits = ForceUnits;
}

void FElysiumNpcWerewolf::UpdateFakeHull(double Now)
{
	// `0x103d93b0`, 1,202 bytes, retail-named. The scope-trace pair is the outer 110.
	//
	// The word this whole body maintains is `+0x66dc` — the world position of the `Bip01` bone as of
	// the previous call — and the ONE cell it compares against is `DAT_1070d1b0`, which is
	// `vec3_origin`. So the three reads of that cell mean three different things and the body only
	// makes sense once that is settled:
	//
	//   * `103d949c` uses it as the INPUT vector of a `VectorTransform`, which makes the answer the
	//     translation column of the `Bip01` bone's local matrix — i.e. the bone's own point.
	//   * `103d9431` writes it into the cache, which RESETS the cache to zero.
	//   * `103d94c0` compares it against the cache, which asks "is the cache still zero?".
	const FElysiumEntity* Enemy = static_cast<const FElysiumNpc*>(this)->GetEnemy();
	if (Enemy == nullptr)
	{
		// No enemy: reset the cache and stop. The werewolf's fake hull only exists while it has
		// something to shoulder out of the way.
		WerewolfFakeHullPosUnits = FVector::ZeroVector;
		return;
	}

	// `103d9459`..`103d94a6`: `LookupBone("Bip01")`, `GetModelPtr(-1)`, `GetBoneTransform(bone, m)`,
	// then `VectorTransform(vec3_origin, studiohdr->bone[bone] + 0x58, local)` followed by
	// `VectorTransform(local, m, world)`. Two transforms, and the first one's input is the zero
	// vector, so the whole chain is "where is the `Bip01` bone in the world".
	FVector BonePosCm = FVector::ZeroVector;
	const bool bHaveBone = BoneWorldPosition(GFakeHullBoneName, BonePosCm);
	const FVector BonePosUnits = BonePosCm / ElysiumMove::U;

	// `103d94c0`..`103d94fa`: three exact float compares of `vec3_origin` against the cache, and the
	// overlap test runs ONLY when at least one component differs — i.e. only when the cache is
	// non-zero, which is only on the second and later calls after an enemy appeared. The first call
	// after every reset does nothing but fill the cache.
	const bool bCacheArmed = WerewolfFakeHullPosUnits != FVector::ZeroVector;
	if (bCacheArmed)
	{
		// `103d9500`: the debug hull draw, behind `!cvar->IsCommand() && cvar->m_nValue != 0`.
		if (FakeHullDebugCvar() != 0)
		{
			++FakeHullSeams.DebugHullDraws;
		}

		// `103d953d`..`103d95fe`: the hull table's mins and maxs for `m_eHull` (`+0x1568`), each
		// scaled by `_DAT_10462914` (1.25) and added to the BONE point — not to the origin. A fake
		// hull, a quarter larger than the real one, hung off the pelvis.
		FVector HullMinsUnits = FVector::ZeroVector;
		FVector HullMaxsUnits = FVector::ZeroVector;
		RetailHullExtents(HullKind, EElysiumHullExtents::Full, HullMinsUnits, HullMaxsUnits);
		const FVector FakeMinUnits = HullMinsUnits * GFakeHullExtentScale + BonePosUnits;
		const FVector FakeMaxUnits = HullMaxsUnits * GFakeHullExtentScale + BonePosUnits;

		// `103d9604`..`103d963d`: the enemy's `m_Collision` slot `+0x3c` (its world-space
		// surrounding bounds) against that box, through `FUN_10240250`.
		FVector EnemyMinsUnits = FVector::ZeroVector;
		FVector EnemyMaxsUnits = FVector::ZeroVector;
		RetailCollisionExtents(*Enemy, EnemyMinsUnits, EnemyMaxsUnits);

		if (BoxesOverlap(FakeMinUnits, FakeMaxUnits, EnemyMinsUnits, EnemyMaxsUnits))
		{
			ApplyFakeHullPush(BonePosUnits, Now);
		}
	}

	// `103d982f`: the cache is written on EVERY path that got an enemy, overlap or not. A failed
	// bone lookup leaves the point at zero, which resets the cache and disarms the next call —
	// retail's own behaviour with no `Bip01`.
	WerewolfFakeHullPosUnits = bHaveBone ? BonePosUnits : FVector::ZeroVector;
}

void FElysiumNpcWerewolf::ApplyFakeHullPush(const FVector& BonePosUnits, double Now)
{
	// `103d9643`..`103d9829`, the overlap arm of `0x103d93b0`.
	//
	// The delta is how far the BONE moved since the previous call, not how far the NPC moved: an
	// animation that swings the pelvis pushes, and a werewolf standing still inside its enemy does
	// not.
	const FVector DeltaUnits = BonePosUnits - WerewolfFakeHullPosUnits;

	// `103d968e`: the body asks slot 167 for the enemy a SECOND time and then reads a pointer out of
	// it at `+0xa8`. That pointer, not the enemy, is what is offset, classified, knocked back and
	// damaged.
	FElysiumEntity* Pushed = FakeHullPushTarget();
	if (Pushed == nullptr)
	{
		return;
	}

	// `103d96a2`: `CalcNearestPoint(bonePos)` on the pushed entity's collision property. The out
	// vector is seeded with `vec3_origin` first, which is dead — the callee writes all three words.
	const FVector NearestCm = NearestPointOnEntity(Pushed, BonePosUnits * ElysiumMove::U);

	// `103d96b7`..`103d9712`: the direction is from MY `WorldSpaceCenter()` (slot 192) to the pushed
	// entity's `GetAbsOrigin()` (slot 217), and it is handed to slot 323 **before** it is
	// normalised — the `VectorNormalize` at `103d9723` runs after the call and its result is
	// discarded (`FSTP ST0`), so it is dead code that the classifier never sees.
	const FVector DirectionCm = Pushed->Origin - SpeciesWorldSpaceCenter();
	const int32 DirectionClass = PushedEntityDirectionClass(Pushed, DirectionCm);
	const int32 Activity = FakeHullKnockbackActivity(DirectionClass);
	PushedEntityKnockback(Pushed, Activity);

	// `103d974d`: the damage is rate-limited to once a second by `+0x66f4` against
	// `gpGlobals->curtime`, and the gate is strict — `stamp + 1.0 < curtime`. The knockback above is
	// NOT rate-limited; only the damage is.
	if (!(WerewolfFakeHullPushTime + GFakeHullPushIntervalSeconds < Now))
	{
		return;
	}

	// `103d976c`..`103d981c`: `CTakeDamageInfo(this, this, 20.0, 1, 0, 0, -1)`, then the sub-type
	// writes `2` and `1`, the force is the bone delta scaled by `_DAT_10457f5c` (500), the position
	// is the nearest point, the scale is 1.0, and `TakeDamage` is called on the pushed entity.
	// **The attacker and the inflictor are both `this`**, so a werewolf shouldering an object
	// credits itself with the damage.
	PushFakeHullDamage(Pushed, GFakeHullDamage, DeltaUnits * GFakeHullForceScale,
		NearestCm / ElysiumMove::U);
	WerewolfFakeHullPushTime = Now;
}

// --- Moved from `ElysiumNpcHints.cpp` (story 5 step 4) ---

bool FElysiumNpcWerewolf::WerewolfHintTrace(const FVector& PositionCm) const
{
	// SEAM for the Werewolf's vtable `+0x9a4` (slot 617) trace. Retail's caller treats a FALSE
	// answer as "the platform is reachable, take the hint"; this answers true, the blocked side,
	// because a trace that was never run must not authorise a teleport.
	(void)PositionCm;
	return true;
}

void FElysiumNpcWerewolf::ClearMoveHint()
{
	// `CNPC_VWerewolf::ClearMoveHint` (`0x103d4690`).
	if (MoveHintNode != INDEX_NONE)
	{
		ReleaseHintNode(MoveHintNode, 0.0f);   // `thunk_FUN_102d1420(m_pMoveHint, 0.0)`
	}
	MoveHintNode = INDEX_NONE;
	// `thunk_FUN_10269b50(this, 0x78)` — the clear is UNCONDITIONAL, outside the null test above.
	Cognition.Conditions.Clear(NpcKernelHintsShared::HintsCond(GHintsCondMoveHintAvailable));
}

void FElysiumNpcWerewolf::SetMoveHint(int32 HintNode, bool bRandom)
{
	// `CNPC_VWerewolf::SetMoveHint` (`0x103d44e0`). Retail's order matters: the existing hint is
	// released first (which CLEARS condition 0x78), then `m_bRandomHint` is written BEFORE
	// `m_pMoveHint`, and only then is 0x78 raised again.
	if (MoveHintNode != INDEX_NONE)
	{
		ClearMoveHint();
	}
	bRandomHint = bRandom;
	MoveHintNode = HintNode;
	// `(**(code **)(*DAT_10924a6c + 4))()` sits between the write and the condition. **Unrecovered**:
	// `DAT_10924a6c` has no other referrer in the corpus and the call takes no visible argument.
	Cognition.Conditions.Set(NpcKernelHintsShared::HintsCond(GHintsCondMoveHintAvailable));
}

void FElysiumNpcWerewolf::ClearTeleportHint()
{
	// `CNPC_VWerewolf::ClearTeleportHint` (`0x103d4760`). No condition is touched, unlike the move
	// hint's clear.
	if (TeleportHintNode != INDEX_NONE)
	{
		ReleaseHintNode(TeleportHintNode, 0.0f);
	}
	TeleportHintNode = INDEX_NONE;
}

void FElysiumNpcWerewolf::SetTeleportHint(int32 HintNode)
{
	// `CNPC_VWerewolf::SetTeleportHint` (`0x103d45c0`). `m_bRandomHint` is cleared here and set by
	// `SetMoveHint` — one flag shared by the two hint kinds, which is why setting a teleport hint
	// makes a previously random move hint non-random.
	if (TeleportHintNode != INDEX_NONE)
	{
		ClearTeleportHint();
	}
	bRandomHint = false;
	TeleportHintNode = HintNode;
}

int32 FElysiumNpcWerewolf::GetHintTeleportPriority(int32 HintType)
{
	// `CNPC_VWerewolf::GetHintTeleportPriority` (`0x103d3220`), a chain of `==` tests in this order.
	// 0x3a9a and 0x3a9b share priority 3; everything unlisted is 0.
	switch (HintType)
	{
	case 0x3a99:  return 2;   // 15001
	case 0x3a9a:  return 3;   // 15002
	case 0x3a9b:  return 3;   // 15003
	case 15000:   return 1;
	case 0x3a9f:  return 4;   // 15007 — the type `FValidateHintType` excludes
	default:      return 0;
	}
}

bool FElysiumNpcWerewolf::IsValidBreakHint(const FHintWords& Hint, double Now) const
{
	// `CNPC_VWerewolf::IsValidBreakHint` (`0x103d8550`). Three arms in order: a null hint is false;
	// a hint `0x102d14c0` calls unusable is false; and the type must be exactly 0x3aa3 (15011).
	if (!Hint.bValid)
	{
		return false;
	}
	if (IsHintUnusable(Hint, Now, Hint.HintOwner.IsSet()))
	{
		return false;
	}
	return Hint.HintType == 0x3aa3;
}

int32 FElysiumNpcWerewolf::SelectScheduleForHint(const FHintWords* Hint, float DistToSavePositionUnits,
	float GoalToleranceUnits)
{
	// `CNPC_VWerewolf::SelectScheduleForHint` (`0x103ce9b0`).
	if (Hint == nullptr || !Hint->bValid)
	{
		return 0x163;
	}
	switch (Hint->HintType)
	{
	case 0x3aa4:  return 0x15f;   // 15012
	case 0x3aa5:  return 0x160;   // 15013
	case 0x3aaa:  return 0x164;   // 15018
	default:      break;
	}
	// The tail: `sqrt(|m_vSavePosition - GetAbsOrigin()|^2) > m_flGoalTolerance`. Retail takes the
	// square root and compares the DISTANCE, not the squared distance, so the threshold is in the
	// same units as `m_flGoalTolerance` — reproduced rather than optimised into a squared compare.
	return DistToSavePositionUnits > GoalToleranceUnits ? 0x15a : 0x15b;
}

int32 FElysiumNpcWerewolf::SelectScheduleForHint(int32 HintNode) const
{
	FHintWords Hint;
	const bool bResolved = HintWords(HintNode, Hint);
	const float DistUnits =
		static_cast<float>(FVector::Dist(SavePosition, Origin) / ElysiumMove::U);
	const float ToleranceUnits = ScheduleHost.GoalToleranceCm / ElysiumMove::U;
	return SelectScheduleForHint(bResolved ? &Hint : nullptr, DistUnits, ToleranceUnits);
}

int32 FElysiumNpcWerewolf::HintActivityForType(int32 HintType, bool bPercentRollPassed, bool bCoinFlip)
{
	// `CNPC_VWerewolf::SetHintActivity` (`0x103d6000`), the switch. `bPercentRollPassed` is
	// `RandomInt(1, 100) <= <cvar>` and subtracts ONE from three of the answers; `bCoinFlip` is
	// `RandomInt(0, 1) != 0` and does the same for 0x3aa9 alone.
	const int32 Lower = bPercentRollPassed ? 1 : 0;
	switch (HintType)
	{
	case 15000:   return 0x10c;
	case 0x3a99:  return 0x119;
	case 0x3a9a:  return 0x11a;
	case 0x3a9b:  return 0x11b;
	case 0x3a9c:  return 0x113 - Lower;
	case 0x3a9d:  return 0x115 - Lower;
	case 0x3a9e:  return 0x117 - Lower;
	case 0x3a9f:  return 0x10d;
	case 0x3aa0:  return 0x10f;
	case 0x3aa1:  return 0x110;
	case 0x3aa2:  return 0x10e;
	case 0x3aa3:  return 0x111;
	case 0x3aa6:  return 0x122;
	case 0x3aa7:  return 0x123;
	case 0x3aa8:  return 0x10b;
	case 0x3aa9:  return 0x125 - (bCoinFlip ? 1 : 0);
	case 0x3aaa:  return 0x121;
	default:      return INDEX_NONE;   // retail's `default:` returns false without positioning
	}
}

bool FElysiumNpcWerewolf::SetHintActivity(const FHintWords& Hint)
{
	// The whole body of `0x103d6000`. Note the ORDER retail draws in: the percentage cvar is read
	// and `RandomInt(1, 100)` is drawn BEFORE the switch, on every call, whether or not the type
	// that lands uses it. The coin flip inside case 0x3aa9 is a second draw.
	if (!Hint.bValid)
	{
		return false;
	}

	// `cVar3 = DAT_1093f85c->vtable[1](); iVar7 = cVar3 ? 0 : DAT_1093f85c[0xb];` — a `ConVar`,
	// `IsCommand()` on slot 1 and the int value at `+0x2c`, used as a percentage:
	// `werewolf_fastbreak_chance`, shipped "40".
	const int32 HintActivityVariantPercent =
		ElysiumNpcTunables::ConVarInt(ElysiumNpcTunables::EConVar::WerewolfFastbreakChance);
	FRandomStream& Stream = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
	const bool bPercentRollPassed = Stream.RandRange(1, 100) <= HintActivityVariantPercent;
	const bool bCoinFlip = Stream.RandRange(0, 1) != 0;

	const int32 Activity = HintActivityForType(Hint.HintType, bPercentRollPassed, bCoinFlip);
	if (Activity == INDEX_NONE)
	{
		return false;
	}
	PositionAtHint(Hint);
	RestartIdealActivityId(Activity);
	return true;
}

bool FElysiumNpcWerewolf::IsVec3Invalid(const FVector& Value)
{
	// `vec3_invalid` is `FLT_MAX` in all three components. Testing one is enough — nothing writes a
	// partial one — but all three are tested because the sentinel is defined as the triple.
	return Value.X >= GHintsVec3Invalid && Value.Y >= GHintsVec3Invalid
		&& Value.Z >= GHintsVec3Invalid;
}

FVector FElysiumNpcWerewolf::GetHintGroundpoint(const FHintWords& Hint) const
{
	// `CNPC_VWerewolf::GetHintGroundpoint` (`0x103d6770`). A linear scan of the authored array at
	// `+0x6714` (count `+0x6720`, stride 0x48, hint pointer at `+0x00`, groundpoint at `+0x08`) for
	// the entry whose hint matches, and on a miss retail's DevWarning plus `GetGroundpoint` of the
	// hint's own origin. SOURCE UNITS throughout, as retail's are.
	for (const FWerewolfHintGroundpoint& Row : WerewolfHintGroundpoints)
	{
		if (Row.HintNode != INDEX_NONE && Row.HintNode == Hint.HintIndex)
		{
			return Row.GroundpointUnits;
		}
	}
	// The array is filled by no producer in this runtime, so this arm is the only one reached. It is
	// still retail's arm and not a refusal.
	UE_LOG(LogElysiumNpcEnt, Warning, TEXT("Werewolf did not find the hint %s"), *Hint.Name);
	// Family Motor's `GetGroundpoint` (`0x103d6a40`) takes and answers SOURCE UNITS, and answers
	// `vec3_invalid` when the ground trace does not hit — which, with no trace on this substrate, is
	// always. Retail's caller does not test it either; `PositionAtHint` is where that lands.
	return GetGroundpoint(Hint.OriginCm / ElysiumMove::U);
}

void FElysiumNpcWerewolf::PositionAtHint(const FHintWords& Hint)
{
	// `CNPC_VWerewolf::PositionAtHint` (`0x103d6280`): the groundpoint into the origin (vtable
	// `+0xf8`), the HINT's own angles into the angles (vtable `+0x368` fed from the hint's `+0x36c`),
	// then `CBaseEntity::Relink`. The port's `SetRuntimeTransform` is the same pair of writes plus
	// the body-follow hook, which is what Relink stands for here.
	if (!Hint.bValid)
	{
		return;   // retail's `if (param_1 != NULL)` guard
	}
	const FVector GroundUnits = GetHintGroundpoint(Hint);
	if (IsVec3Invalid(GroundUnits))
	{
		// NAMED MODERNIZATION, and the only one in this body. Retail does NOT test the groundpoint:
		// a `GetGroundpoint` miss answers `vec3_invalid` (`FLT_MAX` in all three) and retail writes
		// it straight into the origin. This runtime declines the move instead, because `FLT_MAX`
		// centimetres is not a transform the engine can carry — the scene component would take an
		// infinite location and every downstream distance would be a NaN. The recovered fact is
		// recorded here rather than reproduced: retail's Werewolf, handed a hint with no authored
		// groundpoint over no ground, teleports itself out of the world.
		return;
	}
	SetRuntimeTransform(GroundUnits * ElysiumMove::U, Hint.Angles);
}

bool FElysiumNpcWerewolf::IsImperativeTeleportHint(const FHintWords& Hint) const
{
	// `CNPC_VWerewolf::IsImperativeTeleportHint` (`0x103d3360`). Four gates on the Werewolf's own bit
	// word `+0x66e8`, each admitting one or two authored hint NAMES at one hint type. Retail inlines
	// `FStrEq`-with-trailing-`*` at every compare; `FElysiumEntityWorld::NameMatches` is that exact
	// matcher, already recovered, so it is what the compares go through. `0x103d3a40` — the one
	// compare retail left as a call — is the same matcher against `this->m_iName`.
	const bool bBit0x200 = (WerewolfHintFlags & 0x200) == 0x200;
	const bool bBit0x400 = (WerewolfHintFlags & 0x400) == 0x400;
	const bool bBit0x4 = (WerewolfHintFlags & 0x4) == 0x4;

	if (bBit0x200)
	{
		if (Hint.HintType == 0x3aa9
			&& FElysiumEntityWorld::NameMatches(Hint.Name, TEXT("cheater_outside_hint_2")))
		{
			return true;
		}
		if (Hint.HintType == 15000
			&& FElysiumEntityWorld::NameMatches(Hint.Name, TEXT("shard_hint_3")))
		{
			return true;
		}
	}
	if (bBit0x400 && Hint.HintType == 0x3aa9
		&& FElysiumEntityWorld::NameMatches(Hint.Name, TEXT("cheater_outside_hint_3")))
	{
		return true;
	}
	if (bBit0x4)
	{
		// The door-state split. `m_DoorState` 2 or 3 is one pair of arms, anything else the other.
		if (WerewolfDoorState == 2 || WerewolfDoorState == 3)
		{
			if (Hint.HintType == 0x3aa9
				&& FElysiumEntityWorld::NameMatches(Hint.Name, TEXT("cheater_outside_hint_1")))
			{
				return true;
			}
			if (Hint.HintType == 0x3aa4
				&& (FElysiumEntityWorld::NameMatches(Hint.Name, TEXT("jump_to_platform_hint_1"))
					|| FElysiumEntityWorld::NameMatches(Hint.Name, TEXT("jump_to_platform_hint_2"))))
			{
				// The one arm with a trace behind it: retail answers TRUE when the trace comes back
				// CLEAR (`cVar2 == '\0'`) and falls through to false otherwise.
				return !WerewolfHintTrace(Hint.OriginCm);
			}
		}
		else
		{
			if (Hint.HintType == 15000
				&& FElysiumEntityWorld::NameMatches(Hint.Name, TEXT("skylight_2_teleports")))
			{
				return true;
			}
			// `thunk_FUN_103d3a40(param_1, "fulldoor_a_3_breakthrough_f")` — the one compare retail
			// left as a call rather than inlining. Its `this` is `param_1`, the HINT, so it reads
			// the hint's `m_iName` (`+0x26c`) exactly as the inlined compares above do.
			if (Hint.HintType == 0x3a99
				&& FElysiumEntityWorld::NameMatches(Hint.Name, TEXT("fulldoor_a_3_breakthrough_f")))
			{
				return true;
			}
		}
	}
	return false;
}

int32 FElysiumNpcWerewolf::FindHintEndEntity(const FHintWords& Hint) const
{
	// `CNPC_VWerewolf::FindHintEndEntity` (`0x103d6520`). Two hops, and they are NOT symmetric:
	//   1. if the hint carries `m_strTargetName` (`+0x468`), look it up by name and RTTI-cast the
	//      result to `CAI_Hint`; a failed cast leaves null;
	//   2. null falls back to the hint itself;
	//   3. if THAT hint carries a target name, look it up again — with NO cast this time, so retail
	//      can hand back an entity that is not a hint at all;
	//   4. a null second hop returns the result of step 2.
	int32 First = INDEX_NONE;
	if (!Hint.TargetName.IsEmpty())
	{
		First = FindHintByName(Hint.TargetName);
	}
	if (First == INDEX_NONE)
	{
		First = Hint.HintIndex;   // the hint itself, not its network node
	}
	FHintWords FirstWords;
	if (HintWords(First, FirstWords) && !FirstWords.TargetName.IsEmpty())
	{
		const int32 Second = FindHintByName(FirstWords.TargetName);
		if (Second != INDEX_NONE)
		{
			return Second;
		}
	}
	return First;
}

// --- Moved from `ElysiumNpcHints10.cpp` (story 5 step 4) ---

int32 FElysiumNpcWerewolf::GetHintEndEntity(const FHintWords& Hint) const
{
	// `103d63xx`: a linear scan of the `+0x6714` record array, count `+0x6720`, stride `0x48`
	// (0x12 ints). The match is on the record's word at `+0x04` — the HINT — and the answer is its
	// word at `+0x00`, a cached `EHANDLE` to the end entity.
	//
	// Retail validates the handle TWICE: the loop's own guard resolves it and requires a non-null
	// entity, then the hit arm re-reads `base[i * 0x12]` — the SAME word — and returns null if that
	// second resolve fails. The checklist walk called the second read "a second cached handle at the
	// same slot"; the decompiled C shows one word read twice. CORRECTED here and in the prose.
	for (const FWerewolfHintGroundpoint& Row : WerewolfHintGroundpoints)
	{
		if (Row.HintNode == INDEX_NONE || Row.HintNode != Hint.HintIndex)
		{
			continue;
		}
		FHintWords Cached;
		if (!HintWords(Row.CachedEndEntity, Cached))
		{
			// The loop's guard: an unresolvable cached handle does NOT match, so the scan keeps
			// walking rather than answering. `103d63d5` falls through to `iVar5 = iVar5 + 1`.
			continue;
		}
		// The second, redundant resolve. Same word, same answer.
		return HintWords(Row.CachedEndEntity, Cached) ? Row.CachedEndEntity : INDEX_NONE;
	}
	// `103d6410` — the miss and the empty-array case both fall through to `FindHintEndEntity`
	// (`0x103d6520`), which family Hints already ported. Called, not restated.
	return FindHintEndEntity(Hint);
}

FVector FElysiumNpcWerewolf::GetHintEndpoint(const FHintWords* Hint) const
{
	// `103d66xx`. A null hint answers `DAT_1070d1b0/b4/b8`, which `staticinit_101370b0` zeroes —
	// `vec3_origin`, and NOT a "fixed global point" as the checklist walk reads. Recovered by
	// reading the initialiser; recorded in the prose.
	if (Hint == nullptr)
	{
		return GHints10Vec3Origin;
	}
	const int32 End = GetHintEndEntity(*Hint);
	FHintWords EndWords;
	if (!HintWords(End, EndWords))
	{
		// CRASH GUARD, named: retail dereferences the end entity's vtable `+0x364` with NO null
		// check, so a hint whose end entity does not resolve faults. This runtime cannot fault, and
		// there is no origin to answer, so it answers `vec3_origin` — the SAME value retail's own
		// null-hint arm answers, which is the one value this body is already known to produce.
		return GHints10Vec3Origin;
	}
	// `GetAbsOrigin` (vtable `+0x364`) copied into the caller's `Vector`.
	return EndWords.OriginCm;
}

bool FElysiumNpcWerewolf::IsForwardHintExemptType(int32 HintType)
{
	// The switch at `103d70dd`, case for case. FOURTEEN literals, and `0x3aa2` is NOT among them —
	// the checklist walk's "0x3a9f-0x3aaa" would be fifteen. Corrected from the decompiled C.
	switch (HintType)
	{
	case 15000:   // 0x3a98
	case 0x3a99:
	case 0x3a9c:
	case 0x3a9f:
	case 0x3aa0:
	case 0x3aa1:
	case 0x3aa3:
	case 0x3aa4:
	case 0x3aa5:
	case 0x3aa6:
	case 0x3aa7:
	case 0x3aa8:
	case 0x3aa9:
	case 0x3aaa:
		return true;
	default:
		return false;
	}
}

int32 FElysiumNpcWerewolf::GetForwardHintForHint(const FHintWords& Hint) const
{
	// `103d70dd` — the exemption list hands the input hint straight back.
	if (IsForwardHintExemptType(Hint.HintType))
	{
		return Hint.HintIndex;   // the input hint itself
	}

	// `103d7150`: resolve the input's end entity ONCE, then walk the global hint list from its head
	// looking for a hint of type `0x3a9c` whose own end entity is the SAME object.
	const int32 TargetEnd = GetHintEndEntity(Hint);
	for (const int32 Candidate : GlobalHintList())
	{
		FHintWords CandidateWords;
		if (!HintWords(Candidate, CandidateWords))
		{
			continue;
		}
		if (CandidateWords.HintType != 0x3a9c)
		{
			continue;
		}
		if (GetHintEndEntity(CandidateWords) == TargetEnd)
		{
			return Candidate;
		}
	}

	// `103d7176` — the no-match arm warns with the INPUT hint's debug name and returns the loop
	// cursor, which is null because the loop ran to its end. The format at `0x10662f98` reads
	// `"Could Not find forward hint for hint: %s (%s) \n"` in the pinned image — TWO `%s`, which
	// the checklist walk records as one; `CBaseEntity::GetDebugName` supplies both halves of retail's
	// name-and-class pair.
	// Verbose, not Warning: retail's `DevWarning` is developer-gated, this arm is the ONLY one
	// reachable while the hint list is a seam, and family Senses10 logs `0x103d698c`'s twin the same
	// way.
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("Could Not find forward hint for hint: %s (%s) "),
		*Hint.Name, *Hint.Group);
	return INDEX_NONE;
}

bool FElysiumNpcWerewolf::IsTeleportHintExcludedType(int32 HintType)
{
	// `103d83c8`..`103d8430` — six separate `CMP`s, each its own refusal.
	return HintType >= 0x3aa3 && HintType <= 0x3aa8;
}

bool FElysiumNpcWerewolf::HintEndEntityScriptHidden(int32 EndEntityNode) const
{
	// SEAM for `thunk_FUN_100b5190(endEntity)` — `*(byte*)(entity + 0xf4)`, `m_bScriptHidden`. There
	// is no entity behind a hint node on this substrate, so this answers false: retail NEGATES it,
	// so false is the ADMITTING value and nothing is silently refused.
	(void)EndEntityNode;
	return false;
}

bool FElysiumNpcWerewolf::IsValidTeleportHint(const FHintWords* Hint, double Now) const
{
	// `103d83xx`, seven ordered refusals then one negated test. Every gate below falls out to false
	// on the first that fails, in retail's order.

	// 1. `if (param_1 == 0)`.
	if (Hint == nullptr)
	{
		return false;
	}
	// 2. `(field_0x66e8 & 4) == 4` — family Hints' `WerewolfHintFlags`, the same bit
	//    `IsImperativeTeleportHint` reads as `bBit0x4`.
	if ((WerewolfHintFlags & 0x4u) == 0x4u)
	{
		return false;
	}
	// 3. `thunk_FUN_102d14c0(hint)` — family Hints' `IsHintUnusable`, the three-arm rule over the
	//    hint's own words. Called, not restated. `bOwnerAlive` is the `EHANDLE` validity test retail
	//    runs on `m_hHintOwner`; with no hint store the handle never resolves.
	if (IsHintUnusable(*Hint, Now, /*bOwnerAlive=*/false))
	{
		return false;
	}
	// 4. `(**(code **)(*this + 0x8d8))(hint)` — slot 566, VIRTUAL, so a Werewolf takes
	//    `CNPC_VWerewolf`'s own species row (15000..15018 except 15007) through the dispatcher above.
	if (!const_cast<FElysiumNpcWerewolf*>(this)->FValidateHintType(const_cast<FHintWords*>(Hint)))
	{
		return false;
	}
	// 5. the excluded type set.
	if (IsTeleportHintExcludedType(Hint->HintType))
	{
		return false;
	}
	// 6. `(hint->+0x470 == 1) && (field_0x66e8 & 0x40) == 0x40`. Note that `+0x470` is compared for
	//    EQUALITY with 1 here, not masked — the same word slot 566 treats as a bit set. Retail's
	//    asymmetry, reproduced.
	if (Hint->GroupMask == 1 && (WerewolfHintFlags & 0x40u) == 0x40u)
	{
		return false;
	}
	// 7. the endpoint's own `m_bScriptHidden`, NEGATED: a hint is valid only when its far end is not
	//    script-hidden.
	return !HintEndEntityScriptHidden(GetHintEndEntity(*Hint));
}

// --- Moved from `ElysiumNpcLifecycle.cpp` (story 5 step 4) ---

void FElysiumNpcWerewolf::WerewolfScriptUnhideTail(double Now)
{
	// 0x103d4a20 — the base, then four writes in this order: the stamp first, then the three timers
	// high-to-low. Retail's order is preserved because a reader between them would see it.
	WerewolfUnhideStamp = Now;   // +0x66ec := curtime (DAT_1070b228+0xc)
	WerewolfMorphTimerC = 0.f;   // +0x66d8
	WerewolfMorphTimerB = 0.f;   // +0x66d4
	WerewolfMorphTimerA = 0.f;   // +0x66a4
}

void FElysiumNpcWerewolf::StartSearchTimer()
{
	// `CNPC_VWerewolf::StartSearchTimer` (`0x103d1ca0`): `rdtsc` into the STATIC pair
	// `DAT_1093d638`/`DAT_1093d63c`, shared by every werewolf on the map rather than kept per NPC.
	// That is the recovered fact and is why this is a file static here too.
	NpcKernelLifecycleShared::GSearchTimerCycles = LifecycleCycles();
}

bool FElysiumNpcWerewolf::ReportSearchTimer(bool bPassThrough)
{
	// `CNPC_VWerewolf::ReportSearchTimer` (`0x103d1d60`): `rdtsc` again, SUBTRACT the stored pair in
	// place so the statics now hold the elapsed cycles, and pass the second argument through
	// unchanged. Retail reports nothing else — the pair is the report.
	NpcKernelLifecycleShared::GSearchTimerCycles = LifecycleCycles() - NpcKernelLifecycleShared::GSearchTimerCycles;
	return bPassThrough;
}

// --- Moved from `ElysiumNpcLifecycle19.cpp` (story 5 step 4) ---

void FElysiumNpcWerewolf::WerewolfRearm()
{
	// `0x103cac20`, run from BOTH `CNPC_VWerewolf::NPCInit` and `CNPC_VWerewolf::OnRestore`.
	const double Now = NpcKernelLifecycle19Shared::Lifecycle19Now(*this);
	ElysiumNpcEnemy::SetEnemy(*this, FElysiumEntityHandle::Invalid());    // 103cac2a
	Senses.Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();        // 103cac32 +0x628c
	WerewolfMorphTimerA = 0.f;                                           // 103cac38 +0x66a4
	bWerewolfTaskFailed = false;                                         // 103cac3e +0x66a1
	WerewolfSnapWordA = 0;                                               // 103cac44 +0x66a8
	bWerewolfPlayFrustration = false;                                    // 103cac4a +0x66a9
	WerewolfMorphTimerB = 0.f;                                           // 103cac50 +0x66d4
	WerewolfMorphTimerC = 0.f;                                           // 103cac56 +0x66d8
	WerewolfFakeHullPosUnits = FVector::ZeroVector;                      // 103cac62/6e/7a vec3_origin
	// `+0x66ec` is ONE retail word carried twice in this port (`WerewolfUnhideStamp` from family
	// Lifecycle, `WerewolfLastSeenTime` from family Positions). Both take the stamp so the two
	// copies cannot disagree; collapsing them is the owning families' to do.
	WerewolfUnhideStamp = Now;                                           // 103cac89 +0x66ec
	WerewolfLastSeenTime = Now;
	WerewolfFakeHullPushTime = Now;                                      // 103cac9f +0x66f4
	WerewolfMoveHintSearchStart = 0;                                     // 103caca5 +0x66b8 (NULL)
	WerewolfWord66ac = 0;                                                // 103cacab +0x66ac (NULL)
	WerewolfHintNodeCacheA = INDEX_NONE;                                 // 103cacb1 +0x6708
	RandomMoveHintNodeZone = INDEX_NONE;                                 // 103cacb7 +0x670c
	WerewolfHintFlags = 0;                                               // 103cacbd +0x66e8
	WerewolfWord66f8 = 0;                                                // 103cacc3 +0x66f8
	WerewolfWord66fc = 0;                                                // 103cacc9 +0x66fc
	NearestNodeToPlayerRefreshedAt = 0.0;                                // 103caccf +0x6700
	NearestNodeToPlayer = 0;                                             // 103cacd5 +0x6704
	// The teleport-distance floor. BOTH writes are `+=` (`103cad59` / `103cad6d` are `FADD`/`FSTP`),
	// neither word is in the datamap, and the body runs from `NPCInit` AND `OnRestore`: retail
	// defect 2, the floor grows with every load. The first term is the hull's horizontal half-extent
	// (`0x102d6100` mins / `0x102d6120` maxs, `_DAT_104454d0` = 0.5), which this runtime answers
	// through `HullMinsUnits`/`HullMaxsUnits` — a seam that is zero until a hull table stands.
	const FVector HullMins = HullMinsUnits(false);                       // 103cace2 0x102d6100
	const FVector HullMaxs = HullMaxsUnits(false);                       // 103cacf6 0x102d6120
	const float HalfX = static_cast<float>(HullMaxs.X - (HullMaxs.X + HullMins.X) * HullCentreHalf);
	const float HalfY = static_cast<float>(HullMaxs.Y - (HullMins.Y + HullMaxs.Y) * HullCentreHalf);
	WerewolfTeleportDistanceA += FMath::Sqrt(HalfX * HalfX + HalfY * HalfY);   // 103cad59 +0x66cc
	WerewolfTeleportDistanceB +=
		static_cast<float>(FMath::Sqrt(WerewolfTeleportFloorSquare));    // 103cad6d +0x66d0
}

// --- Moved from `ElysiumNpcLifecycle19_2.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcMaintain19.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcMisc.cpp` (story 5 step 4) ---

void FElysiumNpcWerewolf::TriggerWerewolfZone()
{
	// `0x103cade0`, in order:
	//     for (e = FindEntityByName(NULL, "trigger_werewolf_zone"); e;
	//          e = FindEntityByName(e, "trigger_werewolf_zone"))
	//         e->vtable[+0x3ec]();
	//     d1 = dynamic_cast<CFuncMoveLinear*>(FindEntityByName(NULL, "rotdoor1", this, this));
	//     m_hRotDoor1 = d1 ? d1->GetRefEHandle() : INVALID_EHANDLE;    // +0x6684
	//     d2 = dynamic_cast<CFuncMoveLinear*>(FindEntityByName(NULL, "rotdoor2", this, this));
	//     m_hRotDoor2 = d2 ? d2->GetRefEHandle() : INVALID_EHANDLE;    // +0x6688
	//
	// `0x100f7380` compares `entity+0x11c` (`m_iName`), not `+0x26c` (`m_iClassname`), so the zone
	// sweep is by TARGETNAME — three hardcoded map names, which is why this body has no keyfield and
	// no parameter. The two door lookups go through the five-argument form (`0x100f7770`, the one
	// that understands `!self` / `!activator`), passing `this` as both the searching entity and the
	// activator.
	//
	// The RTTI cast is load-bearing: an entity named `rotdoor1` that is NOT a `CFuncMoveLinear`
	// stores the INVALID handle rather than itself, so a mis-typed map disarms the door instead of
	// crashing later. Reproduced as a name lookup plus a refusal, with the class test itself
	// **unrecovered** — this runtime has no `CFuncMoveLinear` leaf to test against, so any entity
	// carrying the name is accepted and the comment says so.
	//
	// Retail name unrecovered; named from the entity names it carries.
	if (World == nullptr)
	{
		return;
	}
	World->ForEachNamed(GMiscWerewolfZoneName, [this](FElysiumEntity& Zone)
	{
		FireWerewolfZoneTrigger(Zone);
	});
	const FElysiumEntity* Door1 = World->FindByName(GMiscRotDoor1Name);
	WerewolfRotDoor1 = Door1 != nullptr ? Door1->Handle : FElysiumEntityHandle::Invalid();
	const FElysiumEntity* Door2 = World->FindByName(GMiscRotDoor2Name);
	WerewolfRotDoor2 = Door2 != nullptr ? Door2->Handle : FElysiumEntityHandle::Invalid();
}

// --- Moved from `ElysiumNpcMotor.cpp` (story 5 step 4) ---

uint32 FElysiumNpcWerewolf::RetailFlags2(const FElysiumEntity& Entity)
{
	// `CBaseEntity::GetFlags2()` (+0x438 `m_fFlags2`). **SEAM**: `FElysiumEntity::Flags` is the
	// first word (+0x434) only.
	(void)Entity;
	return 0;
}

// --- Moved from `ElysiumNpcKernelMotor2.cpp` (story 5 step 4) ---

// -------------------------------------------------------------------------------------------------
// `CNPC_VWerewolf` — the ground snap.
// -------------------------------------------------------------------------------------------------

FVector FElysiumNpcWerewolf::GetGroundpoint(const FVector& PointUnits) const
{
	// `CNPC_VWerewolf::GetGroundpoint` `0x103d6a40`:
	//
	//     Vector mins = HullMins(m_eHull), maxs = HullMaxs(m_eHull);
	//     Vector start = point, end = Vector(point.x, point.y, point.z - 1000.0);   // _DAT_10447ee0
	//     Ray_t ray(start, end, mins, maxs);
	//     TraceRay(ray, 0x202400b, filter(this, 7), &tr);
	//     if (tr.fraction < 1.0)                                                    // _DAT_10449280
	//         return Vector(start.x + tr.fraction * 0.0,                            // _DAT_104454c4
	//                       start.y + tr.fraction * 0.0,
	//                       start.z + tr.fraction * -1000.0);                       // _DAT_104d00ac
	//     return Vector(DAT_10713de0, DAT_10713de4, DAT_10713de8);
	//
	// The two zero terms are the trace delta's X and Y, which the compiler folded to constants
	// because the ray is straight down; the Z term is the drop, negated.
	//
	// **The fallback triple is `vec3_invalid`, not `vec3_origin`.** `staticinit_101371a0` writes
	// `0x7f7fffff` — `FLT_MAX` — into all three, and its only other reader is
	// `CNPC_VWerewolf::GetForwardYawForHint` (`0x103d7210`), which uses it as a sentinel. A no-hit
	// ground snap therefore answers "there is no ground point", and a caller must test for it.
	//
	// **SEAM**: no hull table and no trace, so the body lands on that no-hit arm every time. That is
	// a REAL answer of retail's, not a port refusal, and the caller must treat it the way retail's
	// caller does.
	FVector Mins = FVector::ZeroVector;
	FVector Maxs = FVector::ZeroVector;
	RetailHullExtents(HullKind, EElysiumHullExtents::Full, Mins, Maxs);
	const FVector EndUnits(PointUnits.X, PointUnits.Y, PointUnits.Z - GGroundpointDrop);
	FKernelHullTrace Trace;
	if (KernelHullTrace(PointUnits, EndUnits, Mins, Maxs, GMotorTailGroundTraceMask, Trace)
		&& Trace.Fraction < GMotorTailTraceClearFraction)
	{
		return FVector(PointUnits.X, PointUnits.Y,
			PointUnits.Z + Trace.Fraction * -GGroundpointDrop);
	}
	return FVector(GVecInvalid, GVecInvalid, GVecInvalid);
}

// --- Moved from `ElysiumNpcPositions.cpp` (story 5 step 4) ---

int32 FElysiumNpcWerewolf::GetNearestNodeToPlayer()
{
	// `FUN_103d0bf0`, in retail's order:
	//
	//     if (m_flNodeRefreshed + 0.01 < curtime) m_iNearestNode = 0;   // _DAT_10450aa4, +0x6700/+0x6704
	//     if (m_iNearestNode == 0) {
	//         m_iNearestNode = 0;
	//         if (m_hClosestPlayer resolves) {
	//             Vector p = player->GetLocalOrigin();                  // vtable +0x370, slot 220
	//             nav->field_8 = owner->m_nSomething (+0x156c);         // the two navigator scratch
	//             nav->field_c = gpGlobals->frametime;                  //   words the query reads
	//             int node = FindNearestNode( nav->GetNetwork(), p );
	//             if (node == -1) DevWarning("GetNearestNodeToPlayer failed\n");
	//             else if (node < 0 || node >= network->count) { ++g_counter; m_iNearestNode = 0; }
	//             else m_iNearestNode = network->nodes[node];
	//         }
	//         m_flNodeRefreshed = curtime;
	//     }
	//     return m_iNearestNode;
	//
	// Note what the cache is keyed on: **zero**, not `-1`. A map whose node 0 is the nearest one
	// re-queries every frame, and a query that fails leaves the stamp advanced so the next attempt
	// waits the full interval. Both are reproduced.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (NearestNodeToPlayerRefreshedAt + NearestNodeRefreshSeconds < Now)
	{
		NearestNodeToPlayer = 0;
	}
	if (NearestNodeToPlayer != 0)
	{
		return NearestNodeToPlayer;
	}
	NearestNodeToPlayer = 0;
	const FElysiumPlayer* Player =
		Senses.Memory.ClosestPlayer.IsSet() && World != nullptr ? World->FindPlayer() : nullptr;
	if (Player != nullptr && Player->Handle == Senses.Memory.ClosestPlayer)
	{
		const int32 Node = NavNearestNodeTo(Player->Origin);
		if (Node == -1)
		{
			// `DevWarning(s_GetNearestNodeToPlayer_failed_1066249c)` — the string is
			// "GetNearestNodeToPlayer failed". This is the arm the seam always takes.
			UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("GetNearestNodeToPlayer failed"));
		}
		else
		{
			NearestNodeToPlayer = Node;
		}
	}
	NearestNodeToPlayerRefreshedAt = Now;
	return NearestNodeToPlayer;
}

// --- Moved from `ElysiumNpcPositions2.cpp` (story 5 step 4) ---

bool FElysiumNpcWerewolf::WerewolfSightConVar()
{
	// `(**(code **)(*DAT_1093d694 + 4))()` / `DAT_1093d694[0xb]` — `ConVar::GetBool()` inlined as
	// `!IsCommand() && m_nValue (+0x2c) != 0`: `werewolf_disregard_player_vision`, shipped "0", which
	// CLOSES the Werewolf's gate as shipped.
	return ElysiumNpcTunables::ConVarInt(ElysiumNpcTunables::EConVar::WerewolfDisregardPlayerVision) != 0;
}

bool FElysiumNpcWerewolf::EnemySightPredicate(const FElysiumEntity& Enemy)
{
	// The active enemy's own vtable `+0x278` (slot 158), the second gate of
	// `CNPC_VWerewolf::EnemyCouldSeeHull`. **SEAM**: unreached today, because the cvar above already
	// closed the gate; answering false keeps the refusal honest either way.
	(void)Enemy;
	return false;
}

bool FElysiumNpcWerewolf::EnemyCouldSeeHullWerewolf(const FVector& OriginCm, bool bSkipViewCone,
	bool bUseHitbox, const FVector& ExtentsCm)
{
	// `CNPC_VWerewolf::EnemyCouldSeeHull` `0x103da230`, slot 617 for the Werewolf: two gates in
	// front of `thunk_FUN_10366510`, which is the base body above with every argument forwarded.
	//
	//     if (!ConVar(DAT_1093d694).GetBool()) return false;
	//     if (GetEnemy() && !GetEnemy()->vtable[0x278]()) return false;
	//     return CNPC_VBaseBoss::EnemyCouldSeeHull( ... );
	//
	// Note the asymmetry in the second gate: a Werewolf with NO enemy skips it and reaches the base
	// body, which then refuses for want of an enemy anyway — so the gate only matters when there IS
	// one and it answers false.
	if (!WerewolfSightConVar())
	{
		return false;
	}
	const FElysiumEntity* EnemyEntity =
		World != nullptr ? World->Resolve(Senses.Memory.Enemy) : nullptr;
	if (EnemyEntity != nullptr && !EnemySightPredicate(*EnemyEntity))
	{
		return false;
	}
	return EnemyCouldSeeHull(OriginCm, bSkipViewCone, bUseHitbox, ExtentsCm);
}

bool FElysiumNpcWerewolf::WerewolfTeleportSoundConVar()
{
	// `(**(code **)(*DAT_1093f73c + 4))()` / `DAT_1093f73c[0xb]` — the same inlined
	// `ConVar::GetBool()` shape: `werewolf_show_debug`, shipped "0", the arm that plays nothing.
	return ElysiumNpcTunables::ConVarInt(ElysiumNpcTunables::EConVar::WerewolfShowDebug) != 0;
}

void FElysiumNpcWerewolf::TeleportOut()
{
	// `CNPC_VWerewolf::TeleportOut` `0x103d4a60`, in retail's order — and the order matters, because
	// the effects bit and the solid flag are raised BEFORE the relink, so the engine sees a
	// non-solid invisible body on the same frame:
	//
	//     m_flTimeTeleportedOut = gpGlobals->curtime;      // +0x66f0
	//     vtable[0x108]();                                 // slot 66 Hide
	//     m_fEffects |= 0x20;                              // EF_NODRAW, +0x019c
	//     m_Collision.AddSolidFlags( 0x4 );                // FSOLID_NOT_SOLID, +0x02b4
	//     Relink();
	//     m_??? (+0x66ac) = 0;
	//     ClearTeleportHint();
	//     m_hintFlags (+0x66e8) = 0;
	//     m_OnTeleportOut.FireOutput( GetEnemy(), this, 0 );
	//     if (ConVar(DAT_1093f73c).GetBool())
	//         EmitSound( CSingleUserRecipientFilter(GetEnemy()->entindex()),
	//                    "dev/ww_tele_out.wav", 1.0, level 100 );
	//
	// The sound's recipient filter is the ENEMY, not the werewolf — a dev cue played at whoever it
	// is fighting. Single-player makes that the player, which is why the wav is audible at all.
	WerewolfTimeTeleportedOut = World != nullptr ? World->NowSeconds() : 0.0;
	Hide();                                     // slot 66, the generated virtual
	EffectsWord |= GPositionsTailEffectNoDraw;
	SolidFlagsWord |= GPositionsTailSolidNotSolid;
	// `CBaseEntity::Relink` — this runtime has no spatial partition to relink into; the visibility
	// and solidity the two words above stand for are the visual layer's, and nothing reads them yet.
	WerewolfWord66ac = 0;
	ClearTeleportHint();                        // family Hints' `0x103d4760`
	WerewolfHintFlags = 0;                      // family Hints' +0x66e8
	FireOutput(FName(TEXT("OnTeleportOut")), Senses.Memory.Enemy);
	if (WerewolfTeleportSoundConVar())
	{
		PlayTeleportSound(TEXT("dev/ww_tele_out.wav"));
	}
}

void FElysiumNpcWerewolf::TeleportIn()
{
	// `CNPC_VWerewolf::TeleportIn` `0x103d4d60` — the mirror, with the position FIRST:
	//
	//     PositionAtHint( m_pTeleportHint );               // +0x66b0
	//     vtable[0x10c]();                                 // slot 67 Unhide
	//     m_fEffects &= ~0x20;
	//     m_Collision.RemoveSolidFlags( 0x4 );
	//     Relink();
	//     m_flLastSeen (+0x66ec) = gpGlobals->curtime;
	//     m_OnTeleportIn.FireOutput( GetEnemy(), this, 0 );
	//     if (ConVar(DAT_1093f73c).GetBool()) EmitSound( ..., "dev/ww_tele_in.wav", ... );
	//
	// Note the asymmetry with `TeleportOut`: the hint is NOT cleared on the way in and the stamp
	// written is `+0x66ec`, the same word `UpdateConditionCanTeleport` stamps when the enemy could
	// see the hull — so arriving counts as "just been seen" and the teleport cooldown starts over.
	FHintWords Hint;
	if (HintWords(TeleportHintNode, Hint))
	{
		PositionAtHint(Hint);                   // family Hints' `0x103d6280`
	}
	Unhide();                                   // slot 67, the generated virtual
	EffectsWord &= ~GPositionsTailEffectNoDraw;
	SolidFlagsWord &= ~GPositionsTailSolidNotSolid;
	WerewolfLastSeenTime = World != nullptr ? World->NowSeconds() : 0.0;
	FireOutput(FName(TEXT("OnTeleportIn")), Senses.Memory.Enemy);
	if (WerewolfTeleportSoundConVar())
	{
		PlayTeleportSound(TEXT("dev/ww_tele_in.wav"));
	}
}

void FElysiumNpcWerewolf::PlayTeleportSound(const TCHAR* Rel)
{
	// The shared tail of both halves. The `EmitSound_t` the two bodies fill carries the wav, volume
	// `1.0` (`0x3f800000`) and sound level `0x64` (100 dB); its channel word is `0`, `CHAN_AUTO`.
	// The remaining two words of the record are zero and their FIELDS are unrecovered — the struct
	// is only ever written here, so nothing states which of `m_nFlags` / `m_nPitch` / `m_pOrigin`
	// they are. `IElysiumAudio::PlayBodySound` is this runtime's `EmitSound(..., CHAN_*, ...)` seam.
	IElysiumAudio* Audio = World != nullptr ? World->Audio() : nullptr;
	if (Audio == nullptr)
	{
		return;
	}
	FElysiumBodySound Sound;
	Sound.Rel = Rel;
	Sound.Volume = NpcKernelPositions2Shared::GPositionsTailRetailOne;
	Sound.SoundLevelDb = 100;
	Sound.Channel = EElysiumSoundChannel::Auto;
	Audio->PlayBodySound(Handle, Sound);
}

float FElysiumNpcWerewolf::WerewolfTeleportDelayConVar()
{
	// `ConVar` `DAT_1093d414`, `IsCommand() ? 0.0f : m_fValue (+0x28)`: `werewolf_teleport_out_time`,
	// shipped "4.0" — four seconds out of the enemy's sight before the Werewolf may teleport.
	return ElysiumNpcTunables::ConVarFloat(ElysiumNpcTunables::EConVar::WerewolfTeleportOutTime);
}

void FElysiumNpcWerewolf::UpdateConditionCanTeleport()
{
	// The whole body, and note which way round the condition is written — 29c's one-line walk had it
	// backwards. The condition is CLEARED first and SET only at the end:
	//
	//     ClearCondition( 0x77 );                          // thunk_FUN_10269b50
	//     if (!IsViewable()) return;                       // slot 163, vtable +0x28c
	//     bool close = m_flPlayerDist < 800.0;             // _DAT_10457ac4, +0x6264
	//     Vector bone, ang;  GetBonePosition( "Bip01", &bone, &ang );
	//     if (IsViewable() && EnemyCouldSeeHull( bone, close, true, vec3_origin ))   // slot 617
	//         m_flLastSeen (+0x66ec) = gpGlobals->curtime;
	//     float since = gpGlobals->curtime - m_flLastSeen;  if (since < 0) since = 0;
	//     if (ConVar(DAT_1093d414).GetFloat() < since &&
	//         m_fDistA (+0x66d0) + m_fDistB (+0x66cc) + 100.0 < m_flPlayerDist)      // _DAT_10450564
	//     {
	//         ConVar(DAT_10924a6c).GetXXX();               // result discarded — a folded log gate
	//         SetCondition( 0x77 );                        // thunk_FUN_10269a20
	//     }
	//
	// So the werewolf may teleport when it has been out of the enemy's sight for long enough AND the
	// player is further away than the two stored distances plus a hundred units. `IsViewable` is
	// asked TWICE — once as the entry gate and again immediately before the sight test — which is
	// retail's own redundancy and is reproduced.
	//
	// `DAT_1070d1b0/b4/b8`, the extents handed to slot 617, are `vec3_origin`: `staticinit_101370b0`
	// writes three zeros into them and 369 bodies read them. The box is therefore NOT inflated.
	Cognition.Conditions.Clear(CondCanTeleport);
	if (!IsViewable())
	{
		return;
	}
	const float PlayerDist = Senses.Memory.ClosestPlayerDistanceCm;
	const bool bPlayerClose = PlayerDist < WerewolfCloseEnough * NpcKernelPositions2Shared::GPositionsTailU;

	// `CBaseAnimating::GetBonePosition01("Bip01", &pos, &ang)`. **SEAM**: no bone sampling reaches
	// the kernel here, so the sight point is this body's own origin — the bone's parent transform
	// and retail's own fallback when a model has no such bone.
	const FVector BonePositionCm = Origin;

	if (IsViewable()
		&& EnemyCouldSeeHull(BonePositionCm, bPlayerClose, /*bUseHitbox*/ true,
			/*ExtentsCm*/ FVector::ZeroVector))
	{
		WerewolfLastSeenTime = World != nullptr ? World->NowSeconds() : 0.0;
	}

	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	float Since = static_cast<float>(Now - WerewolfLastSeenTime);
	if (Since < GPositionsTailRetailZero)
	{
		Since = 0.f;
	}
	if (WerewolfTeleportDelayConVar() < Since
		&& WerewolfTeleportDistanceB + WerewolfTeleportDistanceA
			+ WerewolfTeleportDistanceFloor * NpcKernelPositions2Shared::GPositionsTailU < PlayerDist)
	{
		// `(**(code **)(*DAT_10924a6c + 4))()` with its result discarded — a `ConVar` read whose
		// only consumer the compiler folded away, almost certainly a `DevMsg` gate. Reproduced as
		// the nothing it is, and named so a reader does not go looking for it again.
		Cognition.Conditions.Set(CondCanTeleport);
	}
}

// --- Moved from `ElysiumNpcPrecache10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcSensesBodies.cpp` (story 5 step 4) ---

bool FElysiumNpcWerewolf::WerewolfFVisible(const FElysiumEntity* Candidate,
	FElysiumEntityHandle* OutBlocker)
{
	// `0x103cb810`, `CNPC_VWerewolf#201`, 185 bytes of which 120 is the scope trace.
	//
	//     if (candidate != NULL) {
	//         if (!ignore_senses && !(ignore_player && candidate->m_pPlayer)) return true;
	//         if (ppBlocker) *ppBlocker = NULL;
	//     }
	//     return false;
	//
	// **The werewolf has no visibility test.** No range, no cone, no trace, no `m_pSenses` — a live
	// candidate that the two debug ConVars do not veto is visible, full stop. That is what makes
	// the Hollywood chase work and it is not a stub: 185 bytes, seven real callers, and the base
	// slot-201 body it replaces (`0x102b4630`) is 700-odd bytes of exactly the checks it drops.
	//
	// The NULL-candidate arm does NOT write the blocker; only the vetoed arm does. Retail's
	// asymmetry, kept.
	if (Candidate == nullptr)
	{
		return false;
	}
	if (SpeciesStealthSenseGate(Candidate))
	{
		return true;
	}
	if (OutBlocker != nullptr)
	{
		*OutBlocker = FElysiumEntityHandle();
	}
	return false;
}

float FElysiumNpcWerewolf::WerewolfPursueElapsedLimitSeconds()
{
	// `DAT_1093f8ec`, read as `vfunc1() ? _DAT_104454c4 : +0x28`: `werewolf_pursuit_unseen_time` "3.0".
	return ElysiumNpcTunables::ConVarFloat(ElysiumNpcTunables::EConVar::WerewolfPursuitUnseenTime);
}

float FElysiumNpcWerewolf::WerewolfPursuePlayerDistLimitUnits()
{
	// `DAT_1093d574`, the same shape: `werewolf_pursuit_distance` "800".
	return ElysiumNpcTunables::ConVarFloat(ElysiumNpcTunables::EConVar::WerewolfPursuitDistance);
}

bool FElysiumNpcWerewolf::WerewolfShouldPursueEnemy() const
{
	// `0x103cf5f0`, 292 bytes with the scope trace. The recovered shape:
	//
	//     if (!(m_bfWerewolfHintFlags (+0x66e8) & 4)) {
	//         elapsed = curtime - +0x66ec;  if (elapsed < 0.0) elapsed = 0.0;
	//         if (convarA > elapsed)                       -> fall through to true
	//         else if (convarB > m_flPlayerDist (+0x6264))  -> fall through to true
	//         else return false;
	//     }
	//     return true;
	//
	// **Both gates must fail before the werewolf gives up**, and the flag bit skips the test
	// outright. The elapsed clamp is `if (elapsed < 0.0) elapsed = 0.0` — `_DAT_104454c4`, the
	// shared zero — which only matters on the pass after `+0x66ec` is stamped into the future.
	//
	// `+0x66e8` is family **Hints**' `WerewolfHintFlags` and `+0x66ec` is family **Lifecycle**'s
	// `WerewolfUnhideStamp` (`ScriptUnhide` stamps it with `curtime`), so the elapsed term is "how
	// long since the werewolf was last un-hidden by a script".
	//
	// As shipped (3.0 s, 800 units) a werewolf keeps pursuing for three seconds after an unhide,
	// and after that while the player is inside 800 units.
	constexpr uint32 WerewolfFlagSkipPursueTest = 0x4u;
	if ((WerewolfHintFlags & WerewolfFlagSkipPursueTest) == WerewolfFlagSkipPursueTest)
	{
		return true;
	}
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	float Elapsed = static_cast<float>(Now - WerewolfUnhideStamp);
	if (Elapsed < NpcKernelSensesShared::GSharedZero)
	{
		Elapsed = NpcKernelSensesShared::GSharedZero;
	}
	if (WerewolfPursueElapsedLimitSeconds() > Elapsed)
	{
		return true;
	}
	const float PlayerDistUnits = Senses.Memory.ClosestPlayerDistanceCm / ElysiumMove::U;
	if (WerewolfPursuePlayerDistLimitUnits() > PlayerDistUnits)
	{
		return true;
	}
	return false;
}

// --- Moved from `ElysiumNpcSenses10_2.cpp` (story 5 step 4) ---

FVector FElysiumNpcWerewolf::GetHintEndpointUnits(const FHintWords& Hint) const
{
	// SEAM for `CNPC_VWerewolf::GetHintEndpoint`: the hint's END entity's origin, resolved through
	// family Hints' `FindHintEndEntity` (`0x103d6520`). No hint store stands here, so the end entity
	// never resolves and this answers the hint's OWN origin — which is retail's answer for a hint
	// whose `target_name` names nothing.
	if (World != nullptr)
	{
		const int32 EndNode = FindHintEndEntity(Hint);
		FHintWords End;
		if (EndNode != INDEX_NONE && HintWords(EndNode, End))
		{
			return End.OriginCm / ElysiumMove::U;
		}
	}
	return Hint.OriginCm / ElysiumMove::U;
}

float FElysiumNpcWerewolf::GetForwardYawForHint(const FHintWords& Hint) const
{
	// `103d728c`: the working direction is SEEDED with `vec3_invalid` (`DAT_10713de0`…`de8`) and
	// then overwritten outright by `endOrigin - forwardOrigin`; the seed never reaches the answer.
	const FVector EndUnits = GetHintEndpointUnits(Hint);
	FVector ForwardOriginUnits = Hint.OriginCm / ElysiumMove::U;
	const int32 ForwardHint = GetForwardHintForHint(Hint);
	FHintWords ForwardWords;
	if (ForwardHint != INDEX_NONE && HintWords(ForwardHint, ForwardWords))
	{
		ForwardOriginUnits = ForwardWords.OriginCm / ElysiumMove::U;
	}
	// `103d72cf`: the delta, normalised (`0x1057966c`) and converted to a yaw (`0x101d2c70`).
	const FVector Delta = (EndUnits - ForwardOriginUnits).GetSafeNormal();
	float Yaw = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)));

	// `103d730b`: the switch on the hint type at `+0x5dc`. `_DAT_10455050`, the adjust arms' 90
	// degrees, stood at a zero offset until the cell was read (2026-09-21); the two arms that
	// discard the computed yaw entirely were always exact.
	constexpr float GHintYawAdjustDegrees = ElysiumNpcTunables::WerewolfHintYawAdjust;
	switch (Hint.HintType)
	{
	case 0x3a9a:
	case 0x3a9d:
		Yaw += GHintYawAdjustDegrees;
		break;
	case 0x3a9b:
	case 0x3a9e:
		Yaw -= GHintYawAdjustDegrees;
		break;
	case 0x3aa3:
	case 0x3aa5:
		// `103d7328`: the computed yaw is discarded and the HINT's own slot-219 yaw is taken.
		Yaw = static_cast<float>(Hint.Angles.Y);
		break;
	default:
		break;
	}
	// `103d7347`: **read off the listing, because the decompiler lost the `float10` return storage
	// and both tails read alike.** It is a single-step WRAP, not a selection: above `360.0` subtract
	// `360.0`; otherwise, below `0.0` add `360.0`.
	if (Yaw > GYawWrapDegrees)
	{
		Yaw -= GYawWrapDegrees;
	}
	else if (Yaw < 0.f)
	{
		Yaw += GYawWrapDegrees;
	}
	return Yaw;
}

FVector FElysiumNpcWerewolf::GetHintTargetGroundpoint(const FHintWords& Hint) const
{
	// `103d6939`: the linear scan of `m_HintData` (`+0x6714`, count `+0x6720`, stride `0x48`)
	// comparing the ENTITY POINTER at element `+0x04`. The hit answers the Vector at element
	// `+0x14` — the TARGET groundpoint, where family Hints' twin answers `+0x08`.
	for (const FWerewolfHintGroundpoint& Row : WerewolfHintGroundpoints)
	{
		if (Row.HintNode != INDEX_NONE && Row.HintNode == Hint.HintIndex)
		{
			return Row.GroundpointUnits;
		}
	}
	// `103d698c`: the miss `DevWarning`s and falls back to `GetGroundpoint(GetHintEndpoint(hint))`,
	// so a miss STILL answers a point.
	UE_LOG(LogElysiumNpcEnt, Verbose,
		TEXT("%s: 0x103d68d0 Werewolf did not find the hint target groundpoint"), *DebugString());
	return GetGroundpoint(GetHintEndpointUnits(Hint));
}

bool FElysiumNpcWerewolf::IsGroundpointExponentValid(const FVector& PointUnits)
{
	// `103d7a1e`: `(bits & 0x7f800000) == 0x7f800000` on each component — an infinity or a NaN.
	// NOTE `FLT_MAX` (`0x7f7fffff`, which IS `vec3_invalid`) PASSES this test and is stored.
	auto ExponentSaturated = [](double Component)
	{
		const float Value = static_cast<float>(Component);
		uint32 Bits = 0;
		FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
		return (Bits & 0x7f800000u) == 0x7f800000u;
	};
	return !ExponentSaturated(PointUnits.X) && !ExponentSaturated(PointUnits.Y)
		&& !ExponentSaturated(PointUnits.Z);
}

FElysiumNpc::FWerewolfHintGroundpoint FElysiumNpcWerewolf::InitializeHintDataRow(
	const FHintWords& Hint) const
{
	// `103d7800`..`103d7ae4`, one hint's row. `CHintData_WW::Init`, the hint at `+4`, the end-entity
	// handle at `+0` (`0xffffffff` when absent), the forward yaw written back through the hint's own
	// angles (slot `0x104`), `GetGroundpoint(GetAbsOrigin + _DAT_104492a4)` into `+8` and
	// `GetGroundpoint(GetHintEndpoint + _DAT_104492a4)` into `+0x14`, each validated against the
	// `0x7f800000` exponent mask and on failure falling back to the RAW origin / RAW endpoint.
	FWerewolfHintGroundpoint Row;
	// Element `+0x04` is the hint ENTITY (`this_01[1] = this_00`), not its network node.
	Row.HintNode = Hint.HintIndex;
	// The end-entity handle at element `+0x00`, which this comment already named and the row did not
	// carry until story 29d's family Hints10 added the word for `GetHintEndEntity` (`0x103d6390`) to
	// read back. `FindHintEndEntity` (`0x103d6520`) is what retail resolves it with.
	Row.CachedEndEntity = FindHintEndEntity(Hint);

	const FVector OwnUnits = Hint.OriginCm / ElysiumMove::U
		+ FVector(0.0, 0.0, GHintGroundpointLiftUnits);
	const FVector OwnGround = GetGroundpoint(OwnUnits);
	const FVector TargetUnits = GetHintEndpointUnits(Hint) + FVector(0.0, 0.0, GHintGroundpointLiftUnits);
	const FVector TargetGround = GetGroundpoint(TargetUnits);

	// The TARGET groundpoint is what this family's `GetHintTargetGroundpoint` reads back, so it is
	// what the row carries; family Hints' own `GetHintGroundpoint` reads `+8`, whose value is
	// `OwnGround` and is recomputed there.
	if (!IsGroundpointExponentValid(OwnGround))
	{
		UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s: 0x103d7710 %s has invalid ground point"),
			*DebugString(), *Hint.Name);
	}
	if (IsGroundpointExponentValid(TargetGround))
	{
		Row.GroundpointUnits = TargetGround;
	}
	else
	{
		UE_LOG(LogElysiumNpcEnt, Verbose,
			TEXT("%s: 0x103d7710 %s has invalid target ground point"), *DebugString(), *Hint.Name);
		Row.GroundpointUnits = GetHintEndpointUnits(Hint);
	}
	return Row;
}

void FElysiumNpcWerewolf::InitializeHintData()
{
	// `103d77e8`: the whole body runs only while the count `+0x6720` is ZERO — a one-shot build.
	if (WerewolfHintGroundpoints.Num() != 0)
	{
		return;
	}
	// `103d77fa`: the walk is the GLOBAL hint list from `DAT_10925450` through each hint's
	// `+0x5d8` next link (`this_00[1].m_vecViewOffset[1]`), every hint of every type.
	//
	// Not ported here, and recorded as the two live-hint effects this body has: `103d7888` writes the
	// forward yaw back through the hint's own angles (slot `0x104`, `SetLocalAngles`), and `103d7a70`
	// counts the entities named by the hint's `target_name`, `DevWarning`ing when it is not exactly
	// one. Both belong to the Werewolf's program (0002), not to the hint store.
	int32 Count = 0;
	for (const int32 Node : GlobalHintList())
	{
		FHintWords Hint;
		if (!HintWords(Node, Hint) || !Hint.bValid)
		{
			continue;
		}
		WerewolfHintGroundpoints.Add(InitializeHintDataRow(Hint));
		++Count;
	}
	// `103d7b0a`: `DevMsg("There are %d WW hints on this map")`.
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s: 0x103d7710 there are %d WW hints on this map"),
		*DebugString(), Count);
}

bool FElysiumNpcWerewolf::CachedNearestNodeZone(int32& OutZone) const
{
	// SEAM for `0x103d0ad0` — the cached nearest node, whose `+0x94` is the zone the random-move arm
	// compares against `m_iRandomMoveHintNodeZone`. No node graph; answers false, which is retail's
	// own empty-cache arm and refuses the hint.
	OutZone = 0;
	return false;
}

bool FElysiumNpcWerewolf::ValidateHintTypeForWords(const FHintWords& Hint) const
{
	// Slot 566 (`vtable +0x8d8`) over the words already in hand — see the declaration for why the
	// node-index entry point cannot serve these two bodies. Through the vtable: the class's own
	// override answers (story 5 step 3).
	return const_cast<FElysiumNpcWerewolf*>(this)->FValidateHintType(const_cast<FHintWords*>(&Hint));
}

bool FElysiumNpcWerewolf::IsValidRandomMoveHint(const FHintWords& Hint, double Now)
{
	// `103d7dfa`: false unless all four of — the hint is non-null, `0x102d14c0` says it is not
	// taken, slot 566 `FValidateHintType` passes, and the type `+0x5dc` is not `0x3aa9`.
	if (!Hint.bValid)
	{
		return false;
	}
	const bool bOwnerAlive = Hint.HintOwner.IsSet() && World != nullptr
		&& World->Resolve(Hint.HintOwner) != nullptr;
	if (IsHintUnusable(Hint, Now, bOwnerAlive))
	{
		return false;
	}
	if (!ValidateHintTypeForWords(Hint))
	{
		return false;
	}
	if (Hint.HintType == 0x3aa9)
	{
		return false;
	}
	// `103d7e4a`: `0x3aa8` is true only when slot 617 `EnemyCouldSeeHull` at the hint ENDPOINT
	// answers FALSE.
	if (Hint.HintType == 0x3aa8)
	{
		return !WerewolfHintTrace(GetHintEndpointUnits(Hint) * ElysiumMove::U);
	}
	// `103d7ea8`: `0x3a9a`, `0x3a9b` and `0x3a99` are true when `curtime - m_flLastSeenByPlayerTime`
	// is at or below `0` OR below `_DAT_10452dc4` (**2.0**), and otherwise only when the cached
	// nearest node's `+0x94` equals `m_iRandomMoveHintNodeZone` (`+0x670c`).
	if (Hint.HintType == 0x3a9a || Hint.HintType == 0x3a9b || Hint.HintType == 0x3a99)
	{
		const float Elapsed = static_cast<float>(Now - WerewolfLastSeenTime);
		if (Elapsed < 0.f || Elapsed < GRandomMoveLastSeenWindow)
		{
			return true;
		}
		int32 Zone = 0;
		return CachedNearestNodeZone(Zone) && Zone == RandomMoveHintNodeZone;
	}
	// `103d7f4c`: `0x3aa7`, `0x3aa5`, `15000` and `0x3aa3` are ALWAYS false.
	if (Hint.HintType == 0x3aa7 || Hint.HintType == 0x3aa5 || Hint.HintType == 15000
		|| Hint.HintType == 0x3aa3)
	{
		return false;
	}
	// `103d7f8c`: every other type is true only when the per-NPC cooldown list `0x10366400` holds no
	// live entry — a call that also evicts the expired row by swapping in the last.
	FElysiumEntity* HintEntity = World != nullptr && Hint.HintOwner.IsSet()
		? World->Resolve(Hint.HintOwner) : nullptr;
	return !FUN_10366400(HintEntity);
}

bool FElysiumNpcWerewolf::IsValidMoveHint(const FHintWords& Hint, double Now)
{
	// `103d8113`: false on a null hint, on `0x102d14c0` reporting the hint taken, and on slot 566
	// `FValidateHintType` failing.
	if (!Hint.bValid)
	{
		return false;
	}
	const bool bOwnerAlive = Hint.HintOwner.IsSet() && World != nullptr
		&& World->Resolve(Hint.HintOwner) != nullptr;
	if (IsHintUnusable(Hint, Now, bOwnerAlive))
	{
		return false;
	}
	if (!ValidateHintTypeForWords(Hint))
	{
		return false;
	}
	// `103d8179`: types `0x3aa3` and `0x3aa9` are false. NOTE the difference from the random-move
	// twin, which refuses `0x3aa9` but treats `0x3aa3` through its always-false group.
	if (Hint.HintType == 0x3aa3 || Hint.HintType == 0x3aa9)
	{
		return false;
	}
	// `103d81a5`: `0x3aa8` needs `HasCondition(0x77)` SET **and** slot 617 to answer false.
	if (Hint.HintType == 0x3aa8)
	{
		if (!Cognition.Conditions.Has(static_cast<EElysiumNpcCond>(0x77)))
		{
			return false;
		}
		return !WerewolfHintTrace(GetHintEndpointUnits(Hint) * ElysiumMove::U);
	}
	// `103d820c`: `0x3aa5` needs `m_DoorState` (`+0x6680`) to be exactly `2`.
	if (Hint.HintType == 0x3aa5)
	{
		return WerewolfDoorState == 2;
	}
	// `103d8225`: every other type needs the hint's end entity to have a CLEAR byte at `+0xf4` and
	// the cooldown list to answer false. `0x100b5190`'s whole body is `return *(byte*)(ent + 0xf4)`;
	// **`+0xf4`'s retail name is UNRECOVERED** — it is `CBaseEntity`'s, and `CanHearSound`
	// (`0x1030f7b0`) reads it beside `m_bIsBCCTargetable` as a refusal. It answers 0 here, the
	// admitting value.
	const int32 EndNode = FindHintEndEntity(Hint);
	FHintWords End;
	if (EndNode != INDEX_NONE && HintWords(EndNode, End))
	{
		// The byte's seam: 0, so the arm never refuses on it.
	}
	FElysiumEntity* HintEntity = World != nullptr && Hint.HintOwner.IsSet()
		? World->Resolve(Hint.HintOwner) : nullptr;
	return !FUN_10366400(HintEntity);
}

namespace
{
	// `_DAT_10452dc4` (2.0), the up-probe's lift; `0x41200000` (10.0), the escalation's small-hull
	// maxs.z; `_DAT_104454c0` (1.0), the clear fraction. `_DAT_10449154` (0.45) is
	// `ElysiumNpcTunables::WerewolfStuckHullScale`.
	constexpr double GWerewolfStuckLiftUnits = 2.0;
	constexpr double GWerewolfStuckEscalationTopUnits = 10.0;
	constexpr float GWerewolfStuckClearFraction = 1.f;
	constexpr int32 GWerewolfStuckMask = 0x202400b;
}

void FElysiumNpcWerewolf::WerewolfCheckStuck(EStuckEscape Escape)
{
	// `CNPC_VWerewolf::CheckStuck` `0x103cb920`, from the listing. Story 5 step 4r re-port: the moved
	// body tested `fraction < 1` where retail tests `startsolid`, ran `SetHullSizeSmall(1)` on exits
	// retail leaves alone, repeated one probe three times and gated the teleport on COND 0x77 alone.
	//
	// `103cb994`: slot 163 (`+0x28c`), `CBaseEntity::IsViewable` (`0x100a9800`), over this runtime's
	// words: `m_fEffects` (`+0x19c`) bit `0x40` clear, and a model. `IsBSPModel` (`0x100b5110`) is
	// false for an NPC's bbox solid, so the model arm is slot 8's model pointer, which a spawned
	// NPC's `Model` stands for. The Werewolf does not override slot 163; the generated slot-163 row
	// stays another story's, and this is the one caller that needs its answer.
	constexpr uint32 GEffectsBit0x40 = 0x40;
	if ((EffectsWord & GEffectsBit0x40) != 0 || Model.IsEmpty())
	{
		return;
	}
	const double U = ElysiumMove::U;
	// `103cb9a7`..`103cba95`: probe 1, the engine hull trace (`0x1026e940` through the move probe's
	// filter, inside the "CAI_MoveProbe_TraceHull" profile scope) from `GetAbsOrigin()` to
	// `GetAbsOrigin() + (0, 0, 2.0)` on the FULL extents (`0x102d6100` / `0x102d6120`).
	const FVector OriginUnits = Origin / U;
	const FVector UpUnits = OriginUnits + FVector(0.0, 0.0, GWerewolfStuckLiftUnits);
	FKernelHullTrace Probe;
	KernelHullTrace(OriginUnits, UpUnits, HullMinsUnits(false), HullMaxsUnits(false), GWerewolfStuckMask,
		Probe);

	// `103cbaa9`: `trace.startsolid` (`+0x37`), NOT the fraction.
	if (!Probe.bStartSolid)
	{
		// `103cbddb`: the CLEAR start. Re-probe from `WorldSpaceCenter()` (slot 192) to
		// `GetAbsOrigin()` through `CAI_MoveProbe::TraceHull` (`0x102a99e0`), on the SMALL extents
		// while `m_fIsUsingSmallHull` (`+0x5f2d`) stands and the full ones otherwise, maxs.z scaled by
		// 0.45 (`103cbe9d`).
		const FVector CentreUnits = SpeciesWorldSpaceCenter() / U;
		const bool bSmall = bIsUsingSmallHull;
		FVector MaxsUnits = HullMaxsUnits(bSmall);
		MaxsUnits.Z = static_cast<float>(MaxsUnits.Z) * ElysiumNpcTunables::WerewolfStuckHullScale;
		FKernelHullTrace Reprobe;
		KernelHullTrace(CentreUnits, OriginUnits, HullMinsUnits(bSmall), MaxsUnits, GWerewolfStuckMask,
			Reprobe);
		// `103cbed7`: `fraction >= 1.0 && !allsolid && !startsolid` returns at once (`103cbefc` ->
		// `103cbf24`), with no hull change.
		if (Reprobe.Fraction >= GWerewolfStuckClearFraction && !Reprobe.bAllSolid && !Reprobe.bStartSolid)
		{
			return;
		}
		// `103cbefe`: "attempting alt unstuck...", `SetAbsOrigin(tr.endpos)` (slot 216), then
		// `SetHullSizeSmall(1)` (`103cbf1b`).
		UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s: 0x103cb920 attempting alt unstuck..."), *DebugString());
		Origin = Reprobe.EndPosUnits * U;
		SetHullSizeSmall(/*bForce=*/true);
		return;
	}

	// `103cbab8`: the SOLID start. Probe 2 is the same up-probe on the SMALL extents (`0x102d6140` /
	// `0x102d6160`), an engine `TraceRay` through `CTraceFilterSimple(this, 7)`.
	const FVector SmallMinsUnits = HullMinsUnits(true);
	FVector SmallMaxsUnits = HullMaxsUnits(true);
	FKernelHullTrace Second;
	KernelHullTrace(OriginUnits, UpUnits, SmallMinsUnits, SmallMaxsUnits, GWerewolfStuckMask, Second);
	if (!Second.bStartSolid)
	{
		// `103cbb95` -> `103cbf1b`: the small hull fits; shrink to it, no message.
		SetHullSizeSmall(/*bForce=*/true);
		return;
	}
	// `103cbb9b`: "Werewolf stuck?", and the small maxs.z becomes 10.0 (`103cbbb1`).
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s: 0x103cb920 Werewolf stuck?"), *DebugString());
	SmallMaxsUnits.Z = GWerewolfStuckEscalationTopUnits;
	// `103cbbbc`: probe 3, `WorldSpaceCenter()` (slot 192) to `GetOrigin()` (slot 220, the local
	// origin; an unparented NPC's is its absolute one).
	const FVector LocalOriginUnits = Origin / U;
	FKernelHullTrace EscapeTrace;
	KernelHullTrace(SpeciesWorldSpaceCenter() / U, LocalOriginUnits, SmallMinsUnits, SmallMaxsUnits,
		GWerewolfStuckMask, EscapeTrace);
	if (EscapeTrace.bStartSolid)
	{
		// `103cbc9d`: probe 4, `EyePosition()` (slot 193) to `GetOrigin()`.
		EscapeTrace = FKernelHullTrace();
		KernelHullTrace(EyePosition() / U, LocalOriginUnits, SmallMinsUnits, SmallMaxsUnits,
			GWerewolfStuckMask, EscapeTrace);
	}
	if (!EscapeTrace.bStartSolid)
	{
		// `103cbd71`: "Werewolf unstuck...", `SetAbsOrigin(tr.endpos)` (slot 216), `SetHullSizeSmall(1)`.
		UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s: 0x103cb920 Werewolf unstuck..."), *DebugString());
		Origin = EscapeTrace.EndPosUnits * U;
		SetHullSizeSmall(/*bForce=*/true);
		return;
	}
	// `103cbd8f`: the argument (`[ESP+0xdc]`, a bool) AND COND `0x77`. Neither stuck exit touches
	// the hull (`103cbdc7` / `103cbdd6` jump past `SetHullSizeSmall`).
	if (Escape == EStuckEscape::MayTeleport && Cognition.Conditions.Has(static_cast<EElysiumNpcCond>(0x77)))
	{
		UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s: 0x103cb920 Werewolf teleported out from stuck."),
			*DebugString());
		++WerewolfTeleportOutCalls;
		TeleportOut();                                   // `103cbdb3`, `0x103d4a60`
		// `103cbdc1`: slot 448 with the text fail code "Werewolf stuck". SEAM: this runtime's
		// `TaskFail` takes a numeric code and has no text form; the arm is unreachable in shipped
		// content (both callers pass 0), so the code is left at 0.
		TaskFail(0);
		return;
	}
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s: 0x103cb920 Werewolf STUCK!!!"), *DebugString());
}

// --- Moved from `ElysiumNpcSounds10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcSpecies2.cpp` (story 5 step 4) ---

int32 FElysiumNpcWerewolf::EngineFrameNumber() const
{
	// SEAM. See the declaration: `INDEX_NONE` is "no frame number", which the one caller reads as
	// "the cache is stale", so it recomputes on every call.
	return INDEX_NONE;
}

bool FElysiumNpcWerewolf::FUN_103d1e50() const
{
	// `0x103d1e50`:
	//
	//     if (m_DoorState (+0x6680) == 2) return true;
	//     if (m_DoorState == 0) return false;
	//     a = Resolve(m_hRotDoor1 (+0x6684));  if (!a) return false;
	//     b = Resolve(m_hRotDoor2 (+0x6688));  if (!b) return false;
	//     pa = a->WorldSpaceCenter();  pb = b->WorldSpaceCenter();   // vtable +0x300, slot 192
	//     dx = |pb.x - pa.x|;  dy = |pb.y - pa.y|;  dz = |pb.z - pa.z|;
	//     d  = <largest> + 0.25 * (<the other two, summed>);
	//     return !(d <= 135.0);
	//
	// The distance is Source's octagonal length approximation — the largest axis plus a quarter of
	// the sum of the other two (`_DAT_1044bef8` = **0.25**, read out of the image) — not a
	// Chebyshev distance as 29c's walk has it, and not a Euclidean one. The three-way `if` ladder in
	// the decompiled C is the max-of-three; it is written here as the same ladder rather than as a
	// sort, because the tie cases pick a different "other two" and the arithmetic differs by the
	// weighting.
	//
	// The threshold is **135.0** (`_DAT_104cf498`) and the answer is the NEGATION: true means the
	// two halves are further apart than 135 units, i.e. the door is OPEN. A door state of 2 short
	// circuits to true and a state of 0 to false, so the measurement only happens in between.
	//
	// `m_DoorState` (`+0x6680`) is family **Hints**' `WerewolfDoorState`, and the two halves are
	// family **Misc**'s `m_hRotDoor1` / `m_hRotDoor2` (`+0x6684` / `+0x6688`), which its
	// `0x103cade0` zone opener FILLS by the hardcoded map names `rotdoor1` and `rotdoor2`. This body
	// is the READER of that pair and goes through Misc's members; the two halves of one fact.
	if (WerewolfDoorState == WerewolfDoorStateSettled)
	{
		return true;
	}
	if (WerewolfDoorState == WerewolfDoorStateNone || World == nullptr)
	{
		return false;
	}
	const FElysiumEntity* A = World->Resolve(WerewolfRotDoor1);
	if (A == nullptr)
	{
		return false;
	}
	const FElysiumEntity* B = World->Resolve(WerewolfRotDoor2);
	if (B == nullptr)
	{
		return false;
	}
	const FVector Pa = A->Origin / ElysiumMove::U;
	const FVector Pb = B->Origin / ElysiumMove::U;
	const float Dx = static_cast<float>(FMath::Abs(Pb.X - Pa.X));
	const float Dy = static_cast<float>(FMath::Abs(Pb.Y - Pa.Y));
	const float Dz = static_cast<float>(FMath::Abs(Pb.Z - Pa.Z));
	// Retail's ladder verbatim: `if (dx <= dy) { if (dy <= dz) goto C; B; } else if (dx <= dz) C;
	// else A;` — three arms, each adding a quarter of the two it did not pick.
	float Distance;
	if (Dx <= Dy)
	{
		if (Dy <= Dz)
		{
			Distance = (Dy + Dx) * WerewolfDoorMinorAxisWeight + Dz;
		}
		else
		{
			Distance = (Dz + Dx) * WerewolfDoorMinorAxisWeight + Dy;
		}
	}
	else if (Dx <= Dz)
	{
		Distance = (Dy + Dx) * WerewolfDoorMinorAxisWeight + Dz;
	}
	else
	{
		Distance = (Dz + Dy) * WerewolfDoorMinorAxisWeight + Dx;
	}
	return !(Distance <= WerewolfDoorMaxDistanceUnits);
}

void FElysiumNpcWerewolf::FUN_103d9c90(FVector& OutPositionUnits)
{
	// `0x103d9c90`, the frame-memoised chase position:
	//
	//     if (field_0x6670 != gpGlobals->framecount) {         // DAT_1070b22c + 0x1e0
	//         enemy = GetEnemy();                              // vtable +0x29c, slot 167
	//         if (enemy) {
	//             tolerance = m_flGoalTolerance (+0x6320);
	//             cached    = enemy->GetAbsOrigin();           // vtable +0x364, slot 217
	//             field_0x6674/0x6678/0x667c = cached;
	//             ChaseLeadTolerance(this, enemy, cached, &cached, &tolerance);   // thunk 0x102c3b50
	//         }
	//         field_0x6670 = gpGlobals->framecount;
	//     }
	//     *param_1 = field_0x6674/0x6678/0x667c;
	//
	// Two retail details kept verbatim:
	//   * the frame stamp is written **whether or not there was an enemy**, so a werewolf with no
	//     enemy answers the previous frame's cached point for the rest of this frame rather than
	//     recomputing;
	//   * the goal tolerance is read into a local, handed to `0x102c3b50` and then **dropped** — the
	//     helper's only other output is the position, so the tolerance round-trip is dead.
	//
	// **Seam:** `EngineFrameNumber()` answers `INDEX_NONE`, which never equals the stored stamp, so
	// the recompute runs on every call. That is the named decision on the declaration: at retail's
	// one-call-per-frame rate it is retail's own behaviour, and the alternative — a constant stamp —
	// would freeze the cache after its first fill, which is the one thing retail never does.
	const int32 Frame = EngineFrameNumber();
	// `INDEX_NONE` is the seam's "there is no frame number", and it is the STORED value on a body
	// that has never been asked — so a bare `!=` would read "fresh" on the very first call and
	// answer a zero vector for ever. The staleness test is therefore explicit: no frame number
	// means always stale, which is the named decision on the declaration.
	const bool bStale = Frame == INDEX_NONE || WerewolfChaseFrame != Frame;
	if (bStale)
	{
		FElysiumEntity* Enemy = World != nullptr ? World->Resolve(Senses.Memory.Enemy) : nullptr;
		if (Enemy != nullptr)
		{
			float Tolerance = ScheduleHost.GoalToleranceCm;
			WerewolfChasePosUnits = Enemy->Origin / ElysiumMove::U;
			ChaseLeadTolerance(Enemy, WerewolfChasePosUnits * ElysiumMove::U, Tolerance);
			// The tolerance is dropped, exactly as retail drops it.
		}
		WerewolfChaseFrame = Frame;
	}
	OutPositionUnits = WerewolfChasePosUnits;
}

// --- Moved from `ElysiumNpcSpeciesLifecycle10.cpp` (story 5 step 4) ---

void FElysiumNpcWerewolf::DestroyWerewolf()
{
	// `CNPC_VWerewolf::~CNPC_VWerewolf` `0x103ca7c0`.

	// The one thing outside this object the body touches: the debug global, then the ConVar's
	// `SetValue(0)` (slot 4).
	NpcKernelSpeciesLifecycle10Shared::GWerewolfShowDebug = 0;
	ElysiumNpcTunables::SetConVar(ElysiumNpcTunables::EConVar::WerewolfShowDebug, 0.f);

	// The five outputs, in the listing's order: `m_OnTeleportIn`, `m_OnTeleportOut`,
	// `m_OnFinishCrushAnimation`, `m_OnBeginCrushAnimation`, `m_OnConditionDeathTriggered`.
	OutputListDestroys += 5;

	// The hint-data vector (`+0x6714`, count `+0x6720`, stride 0x48), walked BACKWARDS from
	// `count - 1` with `0x103dc5b0` on each record, then the count zeroed and `0x103dc220` run over
	// the vector. The backwards walk is recorded because it is the recovered order.
	WerewolfHintTeardownOrder.Reset();
	for (int32 Index = WerewolfHintGroundpoints.Num() - 1; Index >= 0; --Index)
	{
		WerewolfHintTeardownOrder.Add(Index);
	}
	WerewolfHintGroundpoints.Reset();   // `*(undefined4 *)&this->field_0x6720 = 0`

	// The three `CUtlMemory` teardowns under the "grow size is not -1" tests are allocator work with
	// nothing a program can observe, and `~CAI_BaseNPCTroika` is the world's reap here.
}

// --- Moved from `ElysiumNpcSpeciesMisc10_2.cpp` (story 5 step 4) ---

bool FElysiumNpcWerewolf::WerewolfHasPath(const FVector& StartUnits, const FVector& EndUnits) const
{
	// `103d0e2b`: the whole body is `0x102ee380(m_pNavigator, &start, &end)`. CORRECTION: the two
	// navigator-cache stamps the walk attributes to this body are inside `0x102ee380`, which writes
	// `nav+8` from the NPC's `+0x156c` hull and `nav+0xc` from the global frame word, repeats the
	// pair on the path object `0x102ecc00`, and only then forwards both Vectors to `0x102fdcc0`.
	//
	// SEAM: this substrate stands no `CAI_Path` object, so `0x102fdcc0` answers **false** — retail's
	// own answer for a navigator with no path — and the ask is recorded so a case can read that the
	// forward happened.
	HasPathQueries.Add(FHasPathQuery{ StartUnits, EndUnits });
	return false;
}

void FElysiumNpcWerewolf::WerewolfDrawDebugStatOverlays(TArray<FString>& OutLines) const
{
	// `103d51a1`: `Not Seen Time  : %3.1f` with `max(curtime - +0x66ec, 0.0)` — the clamp against
	// `_DAT_104454c4` (0.0) at `103d51aa` is folded away by the decompiler and is reproduced here.
	const double NotSeen = FMath::Max(NpcKernelSpeciesMisc10_2Shared::SpeciesMisc10_2Now(*this) - WerewolfLastSeenTime, 0.0);
	OutLines.Add(FString::Printf(TEXT("Not Seen Time  : %3.1f"), NotSeen));
	// `103d51d3`: `Player Distance: %3.1f` reads `+0x6264 m_flPlayerDist` — the CACHED distance, not
	// a computed range.
	OutLines.Add(FString::Printf(TEXT("Player Distance: %3.1f"),
		Senses.Memory.ClosestPlayerDistanceCm / ElysiumMove::U));
	// `103d51ee`: the tail into `CNPC_VMingXiao`'s arm `0x10366290` — chained, not replaced. The
	// port's slot-76 dispatcher owns that arm, so the caller runs it around this body.
	// `103d51f3`: the zone word, bit by bit, in retail's order.
	for (const FWerewolfZoneBit& Zone : GWerewolfZoneBits)
	{
		if ((WerewolfHintFlags & Zone.Bit) == Zone.Bit)
		{
			OutLines.Add(Zone.Name);
		}
	}
	// `103d52dd`: the five conditions, `0x7b` BEFORE `0x7a`.
	for (const FWerewolfCondLine& Line : GWerewolfCondLines)
	{
		if (Cognition.Conditions.Has(static_cast<EElysiumNpcCond>(Line.Cond)))
		{
			OutLines.Add(Line.Name);
		}
	}
	// `103d535f`: the door state. States 0..3 print; anything else prints NOTHING at all — retail
	// has no `default:`.
	switch (WerewolfDoorState)
	{
	case 0: OutLines.Add(FString::Printf(TEXT("door state: (%d)closed"), 0)); break;
	case 1: OutLines.Add(FString::Printf(TEXT("door state: (%d)closing"), 1)); break;
	case 2: OutLines.Add(FString::Printf(TEXT("door state: (%d)open"), 2)); break;
	case 3: OutLines.Add(FString::Printf(TEXT("door state: (%d)opening"), 3)); break;
	default: break;
	}
	// `103d539e`: the cvar-selected PLAYER hint dump — `UTIL_PlayerByIndex(1)` through
	// `0x10172710` and an RTTI cast to `CAI_Hint`. SEAM: no hint store, no cvar; the block is
	// recorded as unreachable rather than guessed at.
	// `103d53ea`: the NPC's own hint, `m_pMoveHint` (+0x66bc) FIRST, then `m_pTeleportHint`
	// (+0x66b0), then `m_pLastUsedTeleportHint` (+0x66b4) or `m_pLastUsedMoveHint` (+0x66c0) by two
	// more cvars. The OFFSETS the checklist's walk gives for the first two are swapped; the NAMES
	// are right.
	int32 DumpHint = MoveHintNode;
	if (DumpHint == INDEX_NONE)
	{
		DumpHint = TeleportHintNode;
	}
	if (DumpHint != INDEX_NONE)
	{
		OutLines.Add(FString::Printf(TEXT("hint %d"), DumpHint));
	}
	// `103d5450`: the LAST FIVE rows of the schedule stack (`+0x668c`, count `+0x6698`), a null row
	// printing `INVALID SCHEDULE`. The start index is `max(count - 5, 0)` — the decompiler's
	// `(count - 5) & ((count - 5 < 0) - 1)` is that clamp.
	const int32 Count = WerewolfScheduleStack.Num();
	const int32 First = FMath::Max(Count - 5, 0);
	const int32 Last = FMath::Min(First + 5, Count);
	for (int32 Index = First; Index < Last; ++Index)
	{
		const FString& Name = WerewolfScheduleStack[Index];
		OutLines.Add(Name.IsEmpty() ? FString(TEXT("INVALID SCHEDULE")) : Name);
	}
}

void FElysiumNpcWerewolf::SnapToAnimationPoint()
{
	// `103d9fdc`: `MatchOriginAnglesToAnimation("Bip01", 1, 1)` — origin AND angles onto the
	// animation's root bone. SEAM: recorded, because this substrate samples no bone at this tier.
	MatchOriginAnglesCalls.Add(FMatchOriginAnglesCall{ TEXT("Bip01"), true, true });
	// `103d9fe9`: `SetHullSizeSmall(force = 0)`, which therefore does NOTHING when `+0x5f2d` already
	// says small.
	SetHullSizeSmall(/*bForce=*/false);
	// `103d9ff7`: the cached fake-hull triple cleared from `vec3_origin` (family Geometry's word).
	WerewolfFakeHullPosUnits = FVector::ZeroVector;
	// `103da00e`: `+0x66a8 = 0`.
	WerewolfSnapWordA = 0;
	// `103da015`: both hint-node caches to -1 — the snap also drops whatever hint the Werewolf was
	// heading for.
	WerewolfHintNodeCacheA = INDEX_NONE;
	RandomMoveHintNodeZone = INDEX_NONE;
}

// --- Moved from `ElysiumNpcState19_2.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcTranslate19.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcGeometry.cpp` (story 5 step 4) ---

FVector FElysiumNpcWerewolf::NearestPointOnEntity(const FElysiumEntity* /*Entity*/,
	const FVector& PointCm) const
{
	// SEAM for `CollisionProperty::CalcNearestPoint` (`0x100dd000`): rotate the point into the
	// collideable's space, clamp it into `[m_vecMins, m_vecMaxs]` (`0x1013c8c0`), rotate back.
	++FakeHullSeams.NearestPointCalls;
	return PointCm;
}

int32 FElysiumNpcWerewolf::PushedEntityDirectionClass(FElysiumEntity* Pushed, const FVector& DeltaCm) const
{
	// Slot 323 (`0x10344dd0`), dispatched on the PUSHED entity, not on this one.
	++FakeHullSeams.DirectionClassCalls;
	if (FElysiumNpc* PushedNpc = Pushed ? Pushed->AsNpc() : nullptr)
	{
		return PushedNpc->Slot323(DeltaCm);
	}
	return 0;
}

// --- Moved from `ElysiumNpcMisc.cpp` (story 5 step 4) ---

void FElysiumNpcWerewolf::FireWerewolfZoneTrigger(FElysiumEntity& Zone)
{
	// SEAM for `zone->vtable[+0x3ec]()` — slot 251. On the `CAI_BaseNPC` line slot 251 is
	// `IsActivityFinished` (`0x10272900`), but a `trigger_werewolf_zone` is a different hierarchy
	// sharing the index and the census carries no table for it, so what this fires is
	// **unrecovered**. Counted; nothing is dispatched.
	(void)Zone;
	++WerewolfZoneTriggerFires;
}
