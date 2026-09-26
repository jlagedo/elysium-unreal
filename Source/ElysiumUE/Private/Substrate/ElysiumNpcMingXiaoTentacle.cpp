#include "Substrate/ElysiumNpcMingXiaoTentacle.h"

#include "Substrate/ElysiumNpcMingXiao.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumStub.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcDialogue.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcLifecycle19Shared.h"
#include "Substrate/ElysiumNpcMotorShared.h"
#include "Substrate/ElysiumNpcPrecache10Shared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRetailHullTable.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// --- File-scope helpers moved with this class's bodies (story 5 step 4) ---

namespace
{
	FElysiumEntityHandle GLifecycle19MingXiaoTentacle;
	const TCHAR* const GTentacleFallbackModel =
		TEXT("models/character/monster/mingxiao/mingxiao_tentacle/mingxiao_tentacle.mdl");
	// `0x1064a2f0` is pushed TWICE — `m_iModeIndexTentacleToGrub` and `m_iModeIndexGrub` are two
	// indices of ONE model, which is retail's own duplicate call and is kept.
	const TCHAR* const GTentacleBabyModel =
		TEXT("models/character/monster/MingXiao/MingXiao_baby/MingXiao_baby.mdl");
	const TCHAR* const GTentacleTransformModel =
		TEXT("models/character/monster/MingXiao/MingXiao_transformation.mdl");
	const TCHAR* const GTentacleEmitters[] = {
		TEXT("Ming_xiao_tentacle_transform_emitter"),
		TEXT("Ming_xiao_baby_transform_emitter"),
		TEXT("Ming_xiao_baby_death_emitter"),
	};
	// `0x106477c8` then `0x106477cc`, both pinned by the listing's own annotation.
	const TCHAR* const GTentacleSounds[] = {
		TEXT("character/monster/ming xiao/tentacle_hit_ground.wav"),
		TEXT("character/monster/ming xiao/tentacle_flopping_loop.wav"),
	};
	// `CNPC_VMingXiaoTentacle`'s condition. `0x1039ef90` raises **0x78**, which is one past the
	// base registrar's dense `0x00..0x76` namespace and has NO NAME in `EElysiumNpcCond` — family
	// Conditions enumerated every identity it could name and this one is not among them. It is
	// pushed BY NUMBER with this note rather than left out, because a body that dropped a condition
	// would be a body silently losing an interrupt.
	constexpr EElysiumNpcCond TentacleCoordinateCondition = static_cast<EElysiumNpcCond>(0x78);
}

const FElysiumNpcClass* FElysiumNpcMingXiaoTentacle::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(RetailClassName);
	return Row;
}

// Slots 21-23: `0x1039e800` / `0x1039e830` / `0x1039e860`, each forwarding to the head.
void FElysiumNpcMingXiaoTentacle::Slot21(FElysiumEntity* Attacker)
{
	FUN_1039e800(Attacker);
}

void FElysiumNpcMingXiaoTentacle::Slot22(FElysiumEntity* Attacker)
{
	// `0x1039e830`, slot 22 — byte-identical to `0x1039e800`, verified against the decompiled C of
	// both. Three consecutive slots, one body written three times.
	FUN_1039e800(Attacker);
}

void FElysiumNpcMingXiaoTentacle::Slot23(FElysiumEntity* Attacker)
{
	// `0x1039e860`, slot 23 — the same body a third time.
	FUN_1039e800(Attacker);
}

// Slot 130: `0x1039f000`, which calls the Troika body first.
// `0x1039f000`
void FElysiumNpcMingXiaoTentacle::OnRestore(bool bFromLoad)
{
	TroikaOnRestore(bFromLoad);
	MingXiaoTentacleCache() = FElysiumEntityHandle::Invalid();            // DAT_1093bd34 = -1
}

// Slot 104: `0x1039c220`.
// 0x1039c220
void FElysiumNpcMingXiaoTentacle::Precache()
{
	// `CNPC_VMingXiaoTentacle::Precache` `0x1039c220` — the model fallback runs BEFORE the chain,
	// which no other arm does, and three `PrecacheModel` indices are STORED.
	if (Model.IsEmpty())
	{
		Model = GTentacleFallbackModel;   // slot 212, `0x1064a340`
	}
	TroikaPrecache();

	// The engine returns a model index from each call. This runtime has no model-index space, so the
	// three words take the index of the request in `PrecacheLog` — a stable, distinct number per
	// call with the same identity semantics the later mode-change bodies need ("is the current model
	// the grub or the proxy"), and NOT a claim about retail's numbering. The first two calls push
	// the SAME string (`0x1064a2f0`), so retail's own two indices are equal; that is reproduced by
	// recording the equality explicitly rather than by the log position.
	NpcKernelPrecache10Shared::Precache10Model(*this, GTentacleBabyModel, /*Preload=*/0);
	ModeIndexTentacleToGrub = PrecacheLog.Num() - 1;
	NpcKernelPrecache10Shared::Precache10Model(*this, GTentacleBabyModel, /*Preload=*/0);
	ModeIndexGrub = ModeIndexTentacleToGrub;   // retail: the same model, so the same index
	NpcKernelPrecache10Shared::Precache10Model(*this, GTentacleTransformModel, /*Preload=*/0);
	ModeIndexGrubToProxy = PrecacheLog.Num() - 1;

	for (const TCHAR* Emitter : GTentacleEmitters)
	{
		NpcKernelPrecache10Shared::Precache10Particle(*this, Emitter, /*Preload=*/0);
	}
	for (const TCHAR* Sound : GTentacleSounds)
	{
		NpcKernelPrecache10Shared::Precache10Sound(*this, Sound);
	}
}

// Slot 126: `0x1039ed50`.
/** `CNPC_VMingXiaoTentacle::Save` (`0x1039ed50`) — `m_flPhaseExpireTimer` (`+0x6674`) at mode
 *  **3** around the Troika body. */
int32 FElysiumNpcMingXiaoTentacle::Save(void* Archive)
{
	// `CNPC_VMingXiaoTentacle::Save` `0x1039ed50` — one field, `m_flPhaseExpireTimer` (`+0x6674`),
	// at mode 3 around the Troika body.
	SaveStampEncode(MingXiaoTentaclePhaseExpireTimer, ESaveStampMode::Zero);
	const int32 Result = TroikaSave(Archive);
	SaveStampDecode(MingXiaoTentaclePhaseExpireTimer, ESaveStampMode::Zero);
	return Result;
}

// Slot 127: `0x1039eda0`.
/** `CNPC_VMingXiaoTentacle::vfunc127` (`0x1039eda0`) — the Troika body, then `m_flPhaseExpireTimer`
 *  at mode **3**. */
int32 FElysiumNpcMingXiaoTentacle::Restore(void* Archive)
{
	// `CNPC_VMingXiaoTentacle::vfunc127` `0x1039eda0`.
	const int32 Result = TroikaRestore(Archive);
	SaveStampDecode(MingXiaoTentaclePhaseExpireTimer, ESaveStampMode::Zero);
	return Result;
}

// Slot 461: `0x1039e310`, a complete replacement: DEAD stays DEAD, else `GetEnemy() ? COMBAT : IDLE`.
int32 FElysiumNpcMingXiaoTentacle::SelectIdealStateRetail()
{
	// Here the dead arm is REAL: a tentacle whose CURRENT or IDEAL state is already 7 stays 7 and
	// never reaches the enemy test.
	if (Mind.State() == EElysiumNpcState::Dead || Mind.IdealState() == EElysiumNpcState::Dead)
	{
		Mind.WriteIdealStateRetail(7);
	}
	else
	{
		// `GetEnemy() ? 2 : 1`: COMBAT or IDLE.
		Mind.WriteIdealStateRetail(Senses.Memory.Enemy.IsSet() ? 2 : 1);
	}
	return IdealStateRetail();
}

// Slot 437: `0x1039de00`.
// Slot 437: `0x1039de00`, the body of its class's `PreSelectSchedule` override (story 5 step 3).
int32 FElysiumNpcMingXiaoTentacle::PreSelectSchedule()
{
	// CNPC_VMingXiaoTentacle: `field_0x1b2c = 0x1a; return 0;`
	RecordScheduleEvent(
		TEXT("PreSelectSchedule trace 0x1a (CNPC_VMingXiaoTentacle 0x1039de00) -> 0"));
	return 0;
}

// Slot 438: `0x1039de20`, which replaces the whole selector (the Troika selector's species hook).
// Slot 438: `0x1039de20`, the body of its class's `SpeciesSelectSchedule` override (story 5 step 3).
int32 FElysiumNpcMingXiaoTentacle::SpeciesSelectSchedule()
{
	// CNPC_VMingXiaoTentacle. A four-phase state machine on `m_ePhase`, all of whose words —
	// `m_ePhase`, `m_flPhaseExpireTimer`, `m_flFailedEvadeTimer`, `m_flHideReadyTimer` — are
	// SPECIES words above `+0x665c` with no port member and no producer.
	RecordScheduleEvent(
		TEXT("SelectSchedule trace 0x1a (CNPC_VMingXiaoTentacle 0x1039de20)"));
	ElysiumStub::Fired(TEXT("species"), TEXT("CNPC_VMingXiaoTentacle::SelectSchedule 0x1039de20"),
		DebugString(), TEXT(""),
		TEXT("0002/29c-1: m_ePhase and the three phase timers have no port words"));
	// Phase 0 with an unexpired timer is the arm a freshly spawned tentacle takes, and it is the
	// only one reachable without the species words: `curtime < m_flPhaseExpireTimer` -> 0x156.
	return 0x156;
}

// Slot 440: `0x1039e2d0`.
	// `0x1039e2d0`
// `0x1039e2d0`, `CNPC_VMingXiaoTentacle::TranslateSchedule`, the body of `FElysiumNpcMingXiaoTentacle::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpcMingXiaoTentacle::TranslateScheduleRetail(int32 ScheduleNumber)
{
	if (ScheduleNumber == 0xc7) { return 0xc8; }
	if (ScheduleNumber == 0x147) { return 0x16d; }
	return TroikaTranslateScheduleRetail(ScheduleNumber);
}

// Slot 68: `0x1039eb50`. Ignore unconditionally unless the candidate is non-null AND (it is not a
// `CBaseCombatCharacter` (other+0x9c) OR `m_bIgnoreCollision` is clear); then a direct call into the
// Troika body `0x1029afc0`.
bool FElysiumNpcMingXiaoTentacle::ShouldIgnoreCollision(FElysiumEntity* Other)
{
	const bool bFallToBase = Other != nullptr
		&& (Other->AsCombatCharacter() == nullptr || !bIgnoreCollisionSpecies);
	if (!bFallToBase)
	{
		return true;
	}
	return FElysiumNpc::ShouldIgnoreCollision(Other);
}

// Slot 69: `0x1039eb90`, the same shape as its slot 68 falling to the NAV base `0x1029b180`.
bool FElysiumNpcMingXiaoTentacle::NavIgnoreCollision(FElysiumEntity* Other)
{
	const bool bFallToBase = Other != nullptr
		&& (Other->AsCombatCharacter() == nullptr || !bIgnoreCollisionSpecies);
	if (!bFallToBase)
	{
		return true;
	}
	return FElysiumNpc::NavIgnoreCollision(Other);
}

// Slot 166: `0x1039ebd0`. The tentacle's own companion is never standable; then a direct call into
// the family body `0x10026f80`.
bool FElysiumNpcMingXiaoTentacle::CanStandOn(FElysiumEntity* Other)
{
	if (Other == MingXiaoTentacleCompanion())
	{
		return false;
	}
	return FElysiumNpc::CanStandOn(Other);
}

// Slot 408: `0x1039ece0`, whose miss calls `CAI_BaseNPC::GetShortConditionName` (`0x1027ede0`) directly.
const TCHAR* FElysiumNpcMingXiaoTentacle::GetShortConditionName(int32 ConditionId)
{
	// The class's own block over ids 0x77..0x79, read from `.rdata` `0x1064a42c`, `0x1064a430`,
	// `0x1064a434`. Note the ADDRESSES ascend here where every other block descends.
	static const TCHAR* const Names[] = { TEXT("tfl"), TEXT("tsc"), TEXT("tpe") };
	const int32 Offset = ConditionId - 0x77;
	if (Offset >= 0 && Offset < UE_ARRAY_COUNT(Names))
	{
		return Names[Offset];
	}
	// The `default:` arm: `CAI_BaseNPC::GetShortConditionName` (`0x1027ede0`) directly.
	return FElysiumNpc::GetShortConditionName(ConditionId);
}

// Slot 337: `0x1039c480`.
int32 FElysiumNpcMingXiaoTentacle::GetUsedHullBits()
{
	// A bare `return 0x38000`: no call up the chain, so the Troika line's bit 0 is absent.
	return 0x38000;
}

// Slot 546: `0x1039b230`, the class's own schedule id space.
const TCHAR* FElysiumNpcMingXiaoTentacle::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1093bd80`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VMingXiaoTentacle"), TEXT("0x1039b230"), TEXT("0x1093bd80") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}

// --- Moved from `ElysiumNpcGeometry.cpp` (story 5 step 4) ---

void FElysiumNpcMingXiaoTentacle::NotifyOwnerOfMyMove()
{
	// `FUN_1039ef60`, 22 bytes: resolve `m_hMingXiao` (`+0x665c`) and, when it is live, run the walk
	// above ON THE OWNER. This is how the body is entered; nothing calls `0x10397e00` directly.
	FElysiumEntity* Owner = (TentacleMingXiao.IsSet() && World)
		? World->Resolve(TentacleMingXiao) : nullptr;
	FElysiumNpc* OwnerNpc = Owner ? Owner->AsNpc() : nullptr;
	if (FElysiumNpcMingXiao* Head = OwnerNpc ? OwnerNpc->AsSpecies<FElysiumNpcMingXiao>() : nullptr)
	{
		Head->NotifyOwnedCopiesOfOwnerMove(this);
	}
}

// --- Moved from `ElysiumNpcLifecycle19.cpp` (story 5 step 4) ---

FElysiumEntityHandle& FElysiumNpcMingXiaoTentacle::MingXiaoTentacleCache()
{
	return GLifecycle19MingXiaoTentacle;
}

// --- Moved from `ElysiumNpcMotor.cpp` (story 5 step 4) ---

FElysiumEntity* FElysiumNpcMingXiaoTentacle::MingXiaoTentacleCompanion() const
{
	// `thunk_FUN_1039ede0(this)`. **SEAM**: the tentacle proxy chain is the Squad family's
	// `Proxies[6]` and nothing links a head to it yet.
	return nullptr;
}

// --- Moved from `ElysiumNpcSpecies2.cpp` (story 5 step 4) ---

FElysiumEntity* FElysiumNpcMingXiaoTentacle::MingXiaoTentacleHead() const
{
	// SEAM for `thunk_FUN_1039ede0(this)`. Family **Motor** stands the same retail call over the
	// same absent proxy chain (`ElysiumNpcMotor.cpp:543`). Answers null — retail's
	// "no companion" arm, which forwards nothing.
	return nullptr;
}

void FElysiumNpcMingXiaoTentacle::FUN_1039ef90(const FVector& PositionUnits)
{
	// `0x1039ef90`, fifty-seven bytes, in retail's order:
	//     (*DAT_10924a6c)->vfunc1();                 // result DISCARDED
	//     SetCondition(this, 0x78);                  // thunk 0x10269a20
	//     field_0x668c = param_1[0];
	//     field_0x6690 = param_1[1];
	//     field_0x6694 = param_1[2];
	//
	// The caller is `CNPC_VMingXiao::CoordinateTroops`'s severed-tentacle arm (`0x103998d0`), which
	// family **Squad** already ports as far as this call and names it as unported. This is that row.
	//
	// `DAT_10924a6c` is the same discarded `ConVar` read families Hints, Schedule and Positions each
	// found in front of a `SetCondition` — a folded log gate whose result nothing uses. Its name and
	// default are **UNRECOVERED** and it is not asked here, because nothing downstream can observe
	// it either.
	//
	// Condition **0x78** has no name in `EElysiumNpcCond`: the base registrar's namespace ends at
	// 0x76 and family Conditions named every identity it could. It is set BY NUMBER, with the
	// constant above carrying the note, rather than dropped.
	Cognition.Conditions.Set(TentacleCoordinateCondition);
	TentacleCoordinatePosUnits = PositionUnits;
}

void FElysiumNpcMingXiaoTentacle::FUN_1039e800(FElysiumEntity* Arg)
{
	// `0x1039e800`, `CNPC_VMingXiaoTentacle`'s slot 21, twenty-nine bytes:
	//     head = GetHead(this);                        // thunk 0x1039ede0
	//     if (head) TellHead(head, this, param_1);      // thunk 0x10397dd0
	//
	// Slot 21's base across the family is `CAISound::FUN_10026530`, a no-op, and
	// `CAI_BaseNPCTroika` overrides it generically. This class overrides it AGAIN to hand the call
	// to its head — so a tentacle answers nothing itself and the head decides.
	//
	// `MingXiaoTentacleHead()` is this family's seam and answers null, so the forward does not
	// happen; `TentacleHeadForwards` counts the asks so a test can read that the arm was taken.
	++TentacleHeadForwards;
	FElysiumEntity* Head = MingXiaoTentacleHead();
	if (Head != nullptr)
	{
		// `thunk_FUN_10397dd0(head, this, param_1)` — family **Damage** declared the body this
		// reaches (`CNPC_VMingXiao`'s per-tentacle notice). Unreachable while the seam answers null.
		(void)Arg;
	}
}
