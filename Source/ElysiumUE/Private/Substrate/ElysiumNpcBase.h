#pragma once

#include "CoreMinimal.h"

#include "ElysiumAnimationIntent.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcKernelTunables.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumScheduleCorpus.h"
#include "Substrate/ElysiumScriptedCharacter.h"

struct FElysiumNpcClass;   // Substrate/ElysiumNpcKernelShape.h — the census row for a retail class

// `CAI_BaseNPC` — the AI base every NPC class derives from (story 5 step 5). `FElysiumNpc`, the
// `CAI_BaseNPCTroika` line every living `npc_*` classname builds, derives from it; the script
// directors (`CCineNPC`, `CCineAI`, `CCineAISchedule`) and `CAI_TestHull` sit directly beneath it
// in retail and fold onto it in steps 9-10 (`docs/vtmb/npc-kernel/classes.md`).
//
// It carries the base layer's own words (`+0x1a40..+0x5f3c`, `docs/vtmb/npc-kernel/layout.tsv`)
// and bodies. A base body reaches Troika state only where retail does, through the entity's
// `+0x98 m_pBaseNPCTroika` (`AsNpc()`, null on a base-only NPC); `+0x94 m_pBaseNPC` is `AsNpcBase()`.

class FElysiumNpcBase : public FElysiumScriptedCharacter, public IElysiumScheduleRunner
{
public:
	// `+0x94 m_pBaseNPC`, set by the `CAI_BaseNPC` constructor `0x1027c300` (which also adds the NPC
	// to the AI list `DAT_1090fe10` and sets `FL_NPC`).
	virtual FElysiumNpcBase* AsNpcBase() override { return this; }

	// --- Which retail class this NPC IS (story 29c-1) ---------------------------------------------
	/**
	 * The census row for the retail class this NPC IS (`Substrate/ElysiumNpcKernelClassLookup.h`):
	 * the C++ class's own answer (`OwnRetailClass`), which is the class retail's factory for the
	 * authored classname builds (story 5 step 2). Null for a bare `FElysiumNpc`, the Troika line
	 * itself, which no classname builds.
	 *
	 * Species behaviour is reached through overrides (story 5 step 3) and lives on the species
	 * classes (step 4). What still reads this: retail's own `RTDynamicCast` type tests, the census
	 * lookups, and the class-keyed data queries `story-5/decisions-step3.json` lists.
	 */
	const FElysiumNpcClass* RetailClass() const;

	// Each species class answers its own census row; the base and Troika lines answer null.
	virtual const FElysiumNpcClass* OwnRetailClass() const;

	// `RetailClass()` is `CNPC_VVampireBoss` or below, `CNPC_VBaseBoss` or below, … The chain walk a
	// species body's "am I one of these" arm performs, so no body compares classnames by hand.
	bool IsRetailClass(const TCHAR* RetailClassName) const;

	// This NPC as species class `T` (`Substrate/ElysiumNpc<X>.h`), or null: the typed view a body
	// takes of ANOTHER instance whose species words it reads (a tentacle's head, a pickup helper's
	// ManBat). It tests the C++ class's own census row, never the test latch, so a non-null answer
	// is always an object of class `T`: the species tree mirrors retail's (story 5 step 2).
	template <class T>
	T* AsSpecies()
	{
		return OwnRetailClassDerivesFrom(T::RetailClassName) ? static_cast<T*>(this) : nullptr;
	}
	template <class T>
	const T* AsSpecies() const
	{
		return OwnRetailClassDerivesFrom(T::RetailClassName) ? static_cast<const T*>(this) : nullptr;
	}
	bool OwnRetailClassDerivesFrom(const TCHAR* RetailClassName) const;

	// --- Moved from `FElysiumNpc` (story 5 step 5) ---------------------------------------------

	FElysiumRelationships Relationships;

	// The running schedule and the variant token its activity picks ride on.
	FElysiumScheduleState Schedule;

	// `m_bfAINPCFlags` / `m_bfAINPCFlags2` and the obliviousness refcount. Written by
	// `TASK_SET_NPC_FLAG` / `TASK_MAKE_OBLIVIOUS` and released by every schedule install; saved,
	// because retail's are datamap members and an NPC left mesmerized across a save must not wake up
	// conversable.
	FElysiumNpcFlags NpcFlags;

	// `additionalequipment` (267 authored rows) and `alternateequipment` (184). The corpus authors
	// ONE classname per row, with the literal `0` as the "none" sentinel on 78 of them; the
	// resolution is `Substrate/ElysiumNpcLoadout.h`.
	FString AdditionalEquipment;

	FString AlternateEquipment;

	// The decision pass's gathered conditions and its once-latch diagnostics. Conditions are
	// session state by design (`ElysiumNpcConditions.h`); the memory they are derived from is what
	// a save carries.
	FElysiumNpcCognition Cognition;

	// The door-obstruction selector's own state. `m_hBlockedDoor` (+0x5d28) and `m_hCondHitByDoor`
	// (+0x5d2c) are the two obstruction sources this runtime can carry; `m_vSavePosition` (+0x5dd0)
	// is where the chosen one was standing when the schedules were picked.
	FElysiumEntityHandle BlockedDoor;

	FElysiumEntityHandle CondHitByDoor;

	FVector SavePosition = FVector::ZeroVector;

	// CAI_Memory is the observed-actor admission store. This stays distinct from
	// `BaseMemory.Enemy`, the committed sticky enemy selected from it.
	FElysiumNpcEnemyMemory EnemyMemory;

	// The base layer's words of the schedule host record (`Substrate/ElysiumNpcScheduleHost.h`): the
	// movement, hint, stored-path and schedule-failure words `CAI_BaseNPC` declares. The Troika's
	// (think clocks, move target, hint groups…) are `FElysiumNpc::ScheduleHost`.
	FElysiumNpcBaseScheduleHost BaseScheduleHost;

	// The base layer's memory words (`Substrate/ElysiumNpcSenses.h`): the enemy, the last-seen and
	// last-damage records and the occlusion edge `CAI_BaseNPC` declares. The Troika's (the sound
	// records, the player-LOS cache, the see-unknown clocks) are `FElysiumNpc::Senses.Memory`.
	FElysiumNpcBaseMemory BaseMemory;

	// The base layer's two words of the sense pass, which the senses runner (`FElysiumNpc::Senses`,
	// the Troika's) fills through its `Npc` argument: `m_DelayedSoundConditionList` (`+0x1ae0`) — the
	// heard conditions waiting out their reaction delay; `+0x5ca8` — the conditions the last
	// `Listen` promoted, which the decision pass reads. (`m_bKeepSound` `+0x5cd8` is the Anim10
	// family's `bKeepSound`: the senses object's second copy of it merged there, story 5 step 5.)
	TArray<FElysiumNpcPendingSound> PendingSounds;
	FElysiumNpcConditions HeardConditions;

	// --- The retail words, declared and unwritten ------------------------------------------------
	//
	// Every word of `CAI_BaseNPC` (`+0x1a40..+0x5f3c`) this class owns that no port system writes
	// yet (`docs/vtmb/npc-kernel/layout.md`), default-initialised, each carrying its offset, its
	// retail name and the tier that typed it. They are the shape 29b landed so a later story
	// fills a member instead of inventing one; `ElysiumNpcKernelShapeMap.cpp` binds every one of
	// them to its offset and the shape test fails if one goes missing.
	int32 CollisionMask = 0;  // +0x1a44 m_iCollisionMask (datamap)

	// +0x1a48 m_DeferredDeathInfo (doc) — CTakeDamageInfo is this port's typed damage packet
	FElysiumDmg DeferredDeathInfo;

	// +0x1b4d m_bUnknown1b4d (unsettled) — unsettled in 29b-0; carried by offset name with its
	// recorded type
	bool bUnknown1b4d = false;

	// +0x5b58 m_iUnknown5b58 (unsettled) — unsettled in 29b-0; carried by offset name with its
	// recorded type
	int32 Unknown5b58 = 0;

	// +0x5b5c m_flNPCInitTime (walked) — an absolute curtime stamp, carried as double like every
	// other stamp here
	double NpcInitTime = 0.0;

	// +0x5b60 m_flNextDoorUseTime (datamap) — an absolute curtime stamp, carried as double
	double NextDoorUseTime = 0.0;

	// +0x5b88 m_flWeaponBlockedByFriendTimer (datamap) — an absolute curtime deadline, carried as
	// double
	double WeaponBlockedByFriendTimer = 0.0;

	// +0x5b8c m_flExtendedBlockedByFriendTimer (datamap) — an absolute curtime deadline, carried
	// as double
	double ExtendedBlockedByFriendTimer = 0.0;

	int32 RelativeEyeTarget = 0;  // +0x5b94 m_RelativeEyeTarget (datamap)

	FElysiumEntityHandle ShootTargetOverride;  // +0x5ba8 m_hShootTargetOverride (datamap)

	float SpecialDistanceAccum = 0.f;  // +0x5bac m_flSpecialDistanceAccum (datamap)

	float BurstShootPauseMin = 0.f;  // +0x5bbc m_flBurstShootPauseMin (datamap)

	float BurstShootPauseMax = 0.f;  // +0x5bc0 m_flBurstShootPauseMax (datamap)

	bool bInChoreoScene = false;  // +0x5bc4 m_bInChoreoScene (datamap)

	// +0x5ccc m_nIdealSequence (datamap) — retail's resolved sequence index; this runtime's ideal
	// is the clip identity beside it
	int32 IdealSequence = 0;

	// +0x5cd0 m_IdealTranslatedActivity (datamap) — an activity enum with no port counterpart, so
	// the registered number
	int32 IdealTranslatedActivity = 0;

	// +0x5cd4 m_IdealWeaponActivity (datamap) — an activity enum with no port counterpart, so the
	// registered number
	int32 IdealWeaponActivity = 0;

	// +0x5cec m_afCapability (datamap) — the whole capability word; ElysiumNpcCond::ECapability is
	// only the two bits combat selection reads
	int32 CapabilityWord = 0;

	FElysiumEntityHandle OpeningDoor;  // +0x5d24 m_hOpeningDoor (datamap)

	bool bOpeningDoorWait = false;  // +0x5d30 m_bOpeningDoorWait (datamap)

	// +0x5d5c m_flCheckOnGroundTime (walked) — an absolute curtime deadline, carried as double
	double CheckOnGroundTime = 0.0;

	// +0x5d7c m_ScriptArrivalActivity (sdk-order) — an activity enum with no port counterpart, so
	// the registered number
	int32 ScriptArrivalActivity = 0;

	FString ScriptArrivalSequence;  // +0x5d80 m_strScriptArrivalSequence (sdk-order)

	// +0x5d9c m_flLastAttackTime (datamap) — an absolute curtime stamp, carried as double
	double LastAttackTime = 0.0;

	// +0x5da0 m_flNextWeaponSearchTime (datamap) — an absolute curtime stamp, carried as double
	double NextWeaponSearchTime = 0.0;

	FString SquadName;  // +0x5da8 m_SquadName (datamap)

	int32 MySquadSlot = 0;  // +0x5dac m_iMySquadSlot (datamap)

	FVector LastPosition = FVector::ZeroVector;  // +0x5db8 m_vecLastPosition (datamap)

	// +0x5dc4 m_qaLastFacing (datamap) — retail types it Vector though it holds angles, as the
	// chain stores Angles as FVector
	FVector LastFacing = FVector::ZeroVector;

	float DistTooFar = 0.f;  // +0x5de4 m_flDistTooFar (datamap)

	bool bNoDamageDecal = false;  // +0x5df0 m_fNoDamageDecal (sdk-order)

	bool bWantsLargeHull = false;  // +0x5f2c m_bWantsLargeHull (datamap)

	bool bIsUsingSmallHull = false;  // +0x5f2d m_fIsUsingSmallHull (sdk-order)

	// --- The base layer's family declarations (story 5 step 5) ---
	#include "Substrate/ElysiumNpcBaseAnim.inl"
	#include "Substrate/ElysiumNpcBaseAnim10.inl"
	#include "Substrate/ElysiumNpcBaseCombat10.inl"
	#include "Substrate/ElysiumNpcBaseDamage.inl"
	#include "Substrate/ElysiumNpcBaseDialogue.inl"
	#include "Substrate/ElysiumNpcBaseFacing.inl"
	#include "Substrate/ElysiumNpcBaseHelpers.inl"
	#include "Substrate/ElysiumNpcBaseHints.inl"
	#include "Substrate/ElysiumNpcBaseLifecycle.inl"
	#include "Substrate/ElysiumNpcBaseLifecycle19.inl"
	#include "Substrate/ElysiumNpcBaseMotor.inl"
	#include "Substrate/ElysiumNpcBaseMotor10.inl"
	#include "Substrate/ElysiumNpcBasePositions.inl"
	#include "Substrate/ElysiumNpcBaseSaveRestore10.inl"
	#include "Substrate/ElysiumNpcBaseSenses.inl"
	#include "Substrate/ElysiumNpcBaseSenses10.inl"
	#include "Substrate/ElysiumNpcBaseSpeciesLifecycle10.inl"
	#include "Substrate/ElysiumNpcBaseState19.inl"
	#include "Substrate/ElysiumNpcBaseTranslate19.inl"

protected:
	// `FElysiumNpc::SetRetailClassForTests`'s latch (test builds only write it). Kept only for the
	// enumerated deferred-class cases of story 5 step 2; removed at step 11.
	const FElysiumNpcClass* RetailClassForTests = nullptr;
	bool bRetailClassForTests = false;

	// --- Moved from `FElysiumNpc`'s protected section (story 5 step 5) ---

	// The edge tracker `PumpStateChange` keeps (see the pump's own comment, in the public section).
	EElysiumNpcState LastStateChange = EElysiumNpcState::Idle;

	bool bStateChangeSeen = false;

	FElysiumNpcMind Mind;

	// `m_hTargetEnt` (`+0x5ce4`); see `SetTarget`.
	FElysiumEntityHandle TargetEnt;

	bool bMoveIssued = false;

	// Whether the death handoff has already run. Session state, not save state: it is derivable from
	// the mind's dead state, and a restored corpse re-runs the handoff on the body the load rebuilt.
	bool bDeathHandoffDone = false;

	// `TASK_DIE`'s commit has run. Retail has no such flag: there, the commit re-enters `Event_Killed`
	// and `CreateCorpse` (`0x1032c0e0`) takes the entity out of the world, so the parked task simply
	// stops existing along with the NPC. This runtime has no corpse entity and no removal, so the
	// flag is what stands in for "the body this program was running on is gone".
	bool bDeathCommitted = false;

	// When the death clip `PlayDeathActivity` started runs out. `TASK_DIE`'s gate waits on it; zero
	// means nothing is playing, which is the ordinary case because base `DIE` names no activity task.
	double DeathPerformanceEndsAt = 0.0;

};
