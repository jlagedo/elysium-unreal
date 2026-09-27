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

	// --- The retail vtable surface, base layer (story 5 step 5) ---------------------------------
	//
	// One `virtual` per generated slot the `CAI_BaseNPC` table holds, with the body that table holds
	// (the base's own, or the entity chain's it inherits); `FElysiumNpc` overrides the ones Troika
	// fills with a body of its own (`ElysiumNpcSlots.inl`). Generated by `gen_kernel_shape`; the
	// bodies are in `ElysiumNpcBaseSlots.cpp`.
	#include "Substrate/ElysiumNpcBaseSlots.inl"

	// --- The base layer's family declarations (story 5 step 5) ---
	#include "Substrate/ElysiumNpcBaseAnim.inl"
	#include "Substrate/ElysiumNpcBaseAnim10.inl"
	#include "Substrate/ElysiumNpcBaseClosure.inl"
	#include "Substrate/ElysiumNpcBaseCombat10.inl"
	#include "Substrate/ElysiumNpcBaseConditions.inl"
	#include "Substrate/ElysiumNpcBaseConditions10.inl"
	#include "Substrate/ElysiumNpcBaseDamage.inl"
	#include "Substrate/ElysiumNpcBaseDebug.inl"
	#include "Substrate/ElysiumNpcBaseDebug10.inl"
	#include "Substrate/ElysiumNpcBaseDialogue.inl"
	#include "Substrate/ElysiumNpcBaseEntityChain.inl"
	#include "Substrate/ElysiumNpcBaseFacing.inl"
	#include "Substrate/ElysiumNpcBaseGeometry.inl"
	#include "Substrate/ElysiumNpcBaseHelpers.inl"
	#include "Substrate/ElysiumNpcBaseHints.inl"
	#include "Substrate/ElysiumNpcBaseLifecycle.inl"
	#include "Substrate/ElysiumNpcBaseLifecycle19.inl"
	#include "Substrate/ElysiumNpcBaseMaintain19.inl"
	#include "Substrate/ElysiumNpcBaseMisc.inl"
	#include "Substrate/ElysiumNpcBaseMotor.inl"
	#include "Substrate/ElysiumNpcBaseMotor10.inl"
	#include "Substrate/ElysiumNpcBasePositions.inl"
	#include "Substrate/ElysiumNpcBasePrecache10.inl"
	#include "Substrate/ElysiumNpcBaseSaveRestore10.inl"
	#include "Substrate/ElysiumNpcBaseSchedule.inl"
	#include "Substrate/ElysiumNpcBaseSenses.inl"
	#include "Substrate/ElysiumNpcBaseSenses10.inl"
	#include "Substrate/ElysiumNpcBaseSounds.inl"
	#include "Substrate/ElysiumNpcBaseSounds10.inl"
	#include "Substrate/ElysiumNpcBaseSpecies.inl"
	#include "Substrate/ElysiumNpcBaseSpeciesLifecycle10.inl"
	#include "Substrate/ElysiumNpcBaseSpeciesMisc10.inl"
	#include "Substrate/ElysiumNpcBaseSquad.inl"
	#include "Substrate/ElysiumNpcBaseState19.inl"
	#include "Substrate/ElysiumNpcBaseTranslate19.inl"

	// --- Moved from `FElysiumNpc` (story 5 step 5) ---------------------------------------------

	// One name out of a `FollowPatrolPath` list — retail's `0x102d2840`: the first hint, in hint-list
	// order, of type 10000 or 800 whose `Group` equals the token exactly (case-sensitive). A patrol
	// point is never addressed by targetname.
	const FElysiumEntity* FindPatrolPoint(const FString& Name) const;

	virtual void BeginDying() override;

	virtual bool IsDeathPerformanceFinished() const override;

	virtual void CommitDeath() override;

	virtual float RandomSeconds(float Max) override;

	/**
	 * `ClearSchedule` (`0x10280d30`) — the one door out of a running program.
	 *
	 * Retail has no raw clear: everything that drops a program calls this, and what it does is
	 * zero the six schedule words `+0x5c38..+0x5c4c`, clear `PRESERVE_PATH` and dispatch slot 435
	 * `OnScheduleChange` with `NULL`. `m_failSchedule` (`+0x5c54`) is deliberately not among them.
	 * The body is `ElysiumSchedule::ClearSchedule`; this is the name its callers see, because the
	 * retail call is on the NPC and a site that reached into the schedule record instead would
	 * skip the flag release the slot-435 dispatch performs.
	 *
	 * NOT a vtable slot: `0x10280d30` is a non-virtual the kernel calls directly.
	 */
	void ClearSchedule();

	virtual int32 ResolveScheduleId(int32 Id) const override;

	virtual EElysiumTaskResult BeginStopMovingTask() override;

	// `CAI_BaseNPC::TaskFail` `0x10273fc0` (slot 448's base body): `m_bShouldMove = 0`, the
	// failure code at `+0x5c50`, `SetCondition(0x5c TASK_FAILED)`. The Troika override
	// `0x1029adb0` does its own resets and then calls this directly.
	virtual void TaskFail(int32 Reason) override;

	// The `CAI_BaseNPC` half of the NPC record (`0x1027bc60` / `0x1027c160`), written ahead of the
	// Troika's: the extended header, the flag words, relationships, the base memory, the pending
	// sounds, the enemy memory, the base schedule host and `m_hTargetEnt`.
	virtual void Serialize(FElysiumSaveArchive& Ar) override;

	// `m_pSenses` (+0x5cdc), the `CAI_Senses` object. A base word, but the port's senses runner
	// reads Troika words throughout, so the object itself stays on `FElysiumNpc` (transitional,
	// fold 9) and the base reaches it through this accessor. Null on a base-only NPC: its
	// `CAI_Senses` is unported until then.
	virtual FElysiumNpcSenses* SensesObject() { return nullptr; }
	const FElysiumNpcSenses* SensesObject() const
	{
		return const_cast<FElysiumNpcBase*>(this)->SensesObject();
	}

	// The same write at an arbitrary stamp — `m_flNextThink := Stamp`, rounded so a float stamp
	// never lands above the double frame it names.
	void ArmThinkAt(double Stamp);

	// `m_scriptState in {4,5,6}`, the third term of `ShouldThinkFrequently()` (`0x102c2430`) --
	// the aiscripted states in which a beat is actively driving this body. Mapped rather than
	// transcribed: this runtime spells the same fact as a scripted owner holding the body or a
	// scripted move in flight.
	bool IsScriptDriven() const
	{
		return ScriptOwner.IsSet() || ScriptPhase != EScriptPhase::None;
	}

	// `m_bfNPCStateFlags`, the per-state capability byte `0x1026e3e0` writes on every state
	// change. A pure function of the state here rather than a second stored word: the byte has no
	// writer but the state change, so reading it off the state can never be stale.
	uint8 NpcStateFlags() const;

	void DisconnectFromSquad();

	void ReconnectToSquad();

	// `m_iSquadDisconnected < 1 ? m_pSquad : NULL` (`+0x5bb0`, `+0x5da4`), the squad an NPC answers
	// to while connected. This substrate has no squad object (0002/17), so `m_pSquad` is null on
	// every NPC and this answers nothing; a squad layer replaces the body, not the callers.
	const void* ConnectedSquad() const { return nullptr; }

	const FElysiumEntityHandle& GetTarget() const { return TargetEnt; }

	virtual void MakeOblivious(bool bOblivious) override;

	// Read-only, for the debug layer. The mind is private because every WRITE to it has to go
	// through `RequestState` / `Acquire` / `Release` so the admission and the body arbitration
	// cannot be sidestepped; reading its state, its ideal state, its owner and its transition trace
	// sidesteps nothing, and those four together are the only account of why a character is doing
	// what it is doing. Const on purpose — a panel that could call `RequestState` would be a second
	// producer of NPC state.
	const FElysiumNpcMind& GetMind() const { return Mind; }

	// --- Moved from `FElysiumNpc` (story 5 step 5) ---------------------------------------------

	// The resolved (bank, label) pair `TASK_SET_ACTIVITY` most recently made ideal. It is session
	// state: schedule restore restarts at task zero because neither the current body pose nor the
	// watchdog survives a load. The body phase, not this record, is the current sequence authority.
	FElysiumClipIdentity ScheduleIdealActivity;

	virtual bool IsIdealActivityCurrent() const override;

	virtual void RecordScheduleEvent(const FString& Row) override;

	virtual void StopMoving() override;

	/**
	 * `CBaseCombatCharacter::IsBusyWithDiscipline` (`0x1033e2b0`) — the sole reader of `D_IS_BUSY`,
	 * whose whole body is that one bit test.
	 *
	 * Named as its own predicate rather than left as a bit test at each site, because that is what
	 * its 17 retail callers see: the bit and the predicate are the same fact, and a caller that
	 * tested the bit directly would drift from them.
	 */
	virtual bool IsBusyWithDiscipline() const override
	{
		return NpcFlags.Has(EElysiumNpcFlag::D_IS_BUSY);
	}

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

	// --- Moved from `FElysiumNpc`'s protected section (story 5 step 5) ---

	void SerializeExtendedHeader(FElysiumSaveArchive& Ar);

	// The end of the death transaction, run once: hand the body to Unreal's physics, seeded from the
	// pose it is standing in. A body with no physics asset behind it holds that pose instead — the
	// shipped outcome, because the character bake writes none. The solid-body policy is deliberately
	// NOT here: it is re-asserted on every terminal dead think, because a corpse's body can be handed
	// back to it by something that took it before the kill.
	void CompleteDeathHandoff();

	// --- Moved from `FElysiumNpc`'s protected section (story 5 step 5) ---

	bool bWalkingAnimation = false;

};
