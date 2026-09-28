#pragma once

#include "CoreMinimal.h"

#include "ElysiumAnimationIntent.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcKernelTunables.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumScheduleCorpus.h"
#include "Substrate/ElysiumScriptedCharacter.h"

class FElysiumScriptedSequence;   // Substrate/ElysiumScriptedSequence.h — `CCineNPC`, the directors

// One class of the NPC tree (story 5), declared inside its class body: its retail name, its census
// row (`ElysiumNpcKernelShape::ClassNamed`, looked up once), the row as its own identity, and its
// link in the typed test `AsSpecies<T>()` walks -- its own row, then `BaseClass`'s answer. The row
// is identity only; nothing selects behaviour by it.
#define ELYSIUM_NPC_CLASS(RetailName, BaseClass) \
public: \
	static constexpr const TCHAR* RetailClassName = TEXT(RetailName); \
	static const FElysiumNpcClass* StaticRetailClass() \
	{ \
		static const FElysiumNpcClass* const Row = ElysiumNpcKernelShape::ClassNamed(RetailClassName); \
		return Row; \
	} \
	virtual const FElysiumNpcClass* OwnRetailClass() const override { return StaticRetailClass(); } \
	virtual bool IsNpcClass(const FElysiumNpcClass* Cls) const override \
	{ \
		return Cls != nullptr && (Cls == StaticRetailClass() || BaseClass::IsNpcClass(Cls)); \
	}

// `CAI_BaseNPC` — the AI base every NPC class derives from (story 5 step 5). `FElysiumNpc`, the
// `CAI_BaseNPCTroika` line every living `npc_*` classname builds, derives from it; so does
// `FElysiumNpcTestHull` (`CAI_TestHull`, story 5 fold A1), and the script directors
// `FElysiumScriptedSequence` (`CCineNPC`) with `FElysiumAiScriptedSequence` (`CCineAI`) and
// `FElysiumAiScriptedSchedule` (`CCineAISchedule`) beneath it (fold A3,
// `docs/vtmb/npc-kernel/classes.md`).
//
// It carries the base layer's own words (`+0x1a40..+0x5f40`, `docs/vtmb/npc-kernel/layout.tsv`)
// and bodies. A base body reaches Troika state only where retail does, through the entity's
// `+0x98 m_pBaseNPCTroika` (`AsNpc()`, null on a base-only NPC); `+0x94 m_pBaseNPC` is `AsNpcBase()`.

class FElysiumNpcBase : public FElysiumScriptedCharacter, public IElysiumScheduleRunner
{
public:
	// `+0x94 m_pBaseNPC`, set by the `CAI_BaseNPC` constructor `0x1027c300` (which also adds the NPC
	// to the AI list `DAT_1090fe10` and sets `FL_NPC`).
	virtual FElysiumNpcBase* AsNpcBase() override { return this; }

	// --- Which retail class this NPC IS ---------------------------------------------------------
	/**
	 * The census row of the retail class this NPC is (`ElysiumNpcKernelShape::Classes()`): the C++
	 * class's own answer (`OwnRetailClass`), which is the class retail's factory for the authored
	 * classname builds. Null on the two base lines, `FElysiumNpcBase` and `FElysiumNpc`, which no
	 * classname builds.
	 *
	 * IDENTITY, NEVER DISPATCH (story 5 commit B). Behaviour is reached through overrides and retail's
	 * `__RTDynamicCast` type tests through `AsSpecies<T>()`. What reads the row: the census and
	 * factory tests, logs and inspectors, and the schedule corpus, whose per-class id spaces and
	 * parse flags are keyed by the class's own name exactly as retail's `InitCustomSchedules`
	 * statics are one per class (`FElysiumNpcBase::IdSpace`, `FElysiumNpc::LoadedSchedules`).
	 */
	const FElysiumNpcClass* RetailClass() const { return OwnRetailClass(); }

	// Each class of the tree answers its own census row (`ELYSIUM_NPC_CLASS`); the two base lines null.
	virtual const FElysiumNpcClass* OwnRetailClass() const { return nullptr; }

	// Whether this object's C++ class is the class whose census row is `Cls`, or derives from it.
	// Each class of the tree answers its own row and then asks its base (`ELYSIUM_NPC_CLASS`), so
	// the walk is the C++ inheritance chain -- the walk retail's RTTI class hierarchy descriptor
	// performs for `__RTDynamicCast`. The two base lines are no class of the tree and answer false.
	virtual bool IsNpcClass(const FElysiumNpcClass* Cls) const { return false; }

	// This NPC as class `T` of the tree (`Substrate/ElysiumNpc<X>.h`), or null: retail's
	// `dynamic_cast<T*>` (`__RTDynamicCast`), which the port without RTTI answers through
	// `IsNpcClass`. A non-null answer is always an object of class `T` or below -- a
	// `CNPC_VChangBrosBlade` IS a `CNPC_VChangBros` (`0x1036e2f0`).
	template <class T>
	T* AsSpecies()
	{
		return IsNpcClass(T::StaticRetailClass()) ? static_cast<T*>(this) : nullptr;
	}
	template <class T>
	const T* AsSpecies() const
	{
		return IsNpcClass(T::StaticRetailClass()) ? static_cast<const T*>(this) : nullptr;
	}

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

	// `m_pSenses` (`+0x5cdc`), the `CAI_Senses` object. `CAI_BaseNPC::PostConstructor` (`0x1027bb20`)
	// builds it through slot 424 (`CreateComponents`, `0x1027cae0`) for EVERY NPC-base instance, the
	// scripted directors included (story 5 fold A3), so it lives here. Only the Troika line runs a
	// sense pass over it; on a base-only NPC it stands idle, as retail's does (a director never runs
	// `PerformSensing`). The Troika's own memory words ride inside it (`Senses.Memory`).
	FElysiumNpcSenses Senses;

	// The base layer's two words of the sense pass, which the senses runner (`FElysiumNpc::Senses`,
	// the Troika's) fills through its `Npc` argument: `m_DelayedSoundConditionList` (`+0x1ae0`) — the
	// heard conditions waiting out their reaction delay; `+0x5ca8` — the conditions the last
	// `Listen` promoted, which the decision pass reads. (`m_bKeepSound` `+0x5cd8` is the Anim10
	// family's `bKeepSound`: the senses object's second copy of it merged there, story 5 step 5.)
	TArray<FElysiumNpcPendingSound> PendingSounds;
	FElysiumNpcConditions HeardConditions;

	// --- The retail words, declared and unwritten ------------------------------------------------
	//
	// Every word of `CAI_BaseNPC` (`+0x1a40..+0x5f40`) this class owns that no port system writes
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
	#include "Substrate/ElysiumNpcBaseLifecycle2.inl"
	#include "Substrate/ElysiumNpcBaseMaintain.inl"
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
	#include "Substrate/ElysiumNpcBaseState.inl"
	#include "Substrate/ElysiumNpcBaseTranslate.inl"
	#include "Substrate/ElysiumNpcBaseConditions2.inl"
	#include "Substrate/ElysiumNpcBaseRunAi.inl"
	#include "Substrate/ElysiumNpcBaseStartTask.inl"
	#include "Substrate/ElysiumNpcBaseRunTask.inl"
	#include "Substrate/ElysiumNpcBaseSelect.inl"
	#include "Substrate/ElysiumNpcBaseThink.inl"
	#include "Substrate/ElysiumNpcBaseSpawn.inl"
	#include "Substrate/ElysiumNpcBaseDamage2.inl"
	#include "Substrate/ElysiumNpcBaseScript.inl"
	#include "Substrate/ElysiumNpcBaseBoss2.inl"
	#include "Substrate/ElysiumNpcBaseWerewolf.inl"
	#include "Substrate/ElysiumNpcBaseMisc2.inl"
	#include "Substrate/ElysiumNpcBaseDamaged.inl"

	// --- Moved from `FElysiumNpc` (story 5 step 5) ---------------------------------------------

	// One name out of a `FollowPatrolPath` list — retail's `0x102d2840`: the first hint, in hint-list
	// order, of type 10000 or 800 whose `Group` equals the token exactly (case-sensitive). A patrol
	// point is never addressed by targetname.
	const FElysiumEntity* FindPatrolPoint(const FString& Name) const;

	virtual float RandomSeconds(float Max);

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

	// `CAI_BaseNPC::TaskFail` `0x10273fc0` (slot 448's base body): `m_bShouldMove = 0`, the
	// failure code at `+0x5c50`, `SetCondition(0x5c TASK_FAILED)`. The Troika override
	// `0x1029adb0` does its own resets and then calls this directly.
	virtual void TaskFail(int32 Reason) override;

	// The `CAI_BaseNPC` half of the NPC record (`0x1027bc60` / `0x1027c160`), written ahead of the
	// Troika's: the extended header, the flag words, relationships, the base memory, the pending
	// sounds, the enemy memory, the base schedule host and `m_hTargetEnt`.
	virtual void Serialize(FElysiumSaveArchive& Ar) override;

	// `m_pSenses` (+0x5cdc), the `CAI_Senses` object (`Senses` above). Never null: every NPC-base
	// instance carries one (story 5 fold A3 ended the Troika-only transitional home).
	FElysiumNpcSenses* SensesObject() { return &Senses; }
	const FElysiumNpcSenses* SensesObject() const { return &Senses; }

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

	// `CAI_BaseNPC::SetTarget` (`0x10279cc0`): `m_hTargetEnt` (`+0x5ce4`) := the handle. Written by the
	// comfort sweep, by a director's `FindEntity` wrapper (`0x101a7760`), by `PossessEntity`
	// (`SetTarget(npc, cine)`) and by `CineCleanup` (`SetTarget(npc, NULL)`); read by the gaze
	// cascade's target arm (`GazeTargetEntity`).
	void SetTarget(const FElysiumEntityHandle& NewTarget) { TargetEnt = NewTarget; }

	// `m_hCine` (`+0x5d74`, `FElysiumEntity::ScriptOwner`) resolved to the director that owns this NPC,
	// or null: the dead-handle test every retail reader of the word performs, plus the type the
	// word always holds in retail (only `CCineNPC::PossessEntity` and its two twins write it).
	FElysiumScriptedSequence* ResolveCine() const;

	// `m_IdealNPCState := RetailId` with the selector trace retail stamps beside every writer
	// (`+0x1b3c`/`+0x1b40`, recorded by the mind's transition trace): the public face of the mind's
	// own request, for a director writing ANOTHER NPC's ideal state (`PossessEntity`,
	// `FixScriptNPCSchedule`).
	void RequestIdealStateRetail(int32 RetailId, int32 SourceLine);

	// --- `m_iIsOblivious` (`+0x5bb4`) -------------------------------------------------------------
	// A `CAI_BaseNPC` word. Its bookkeeping bit `MADE_OBLIVIOUS` is in the combat character's
	// `m_bfAINPCFlags2` (`NpcFlags`), and the methods below write the pair the way retail's do.

	/**
	 * `m_iIsOblivious > 0`.
	 *
	 * A REFCOUNT, not a flag, and that is load-bearing: retail nests the sources (a scripted scene,
	 * a grapple, being fed upon, and `TASK_MAKE_OBLIVIOUS` all increment it), so a body that is
	 * oblivious for two reasons stays oblivious when one of them ends. Its four recovered consumers
	 * are the sense pass (`CAI_BaseNPC::PerformSensing` `0x1026e4f0` skips sensing entirely), the
	 * weapon-aim pose (slot 314, `0x102bf070`, stops aiming), a reaction predicate (slot 587,
	 * `0x1028ef20`) and `CStealthKillRules::FindVictim` (`0x101be1f0`, which makes an oblivious body
	 * backstabbable from any angle).
	 */
	bool IsOblivious() const { return ObliviousCount > 0; }
	// Grapples own a raw nesting reference, not `TASK_MAKE_OBLIVIOUS`'s bookkeeping bit.
	void AddGrappleOblivious() { ++ObliviousCount; }
	void RemoveGrappleOblivious() { ObliviousCount = FMath::Max(0, ObliviousCount - 1); }
	// `0x1026d130` / `0x1026d160`, the refcount halves of `TASK_MAKE_OBLIVIOUS`: the counter and the
	// bookkeeping bit, the pair the schedule-change clear keeps consistent. `MakeOblivious` owns the
	// rest (the enemy, the squad, the outputs).
	void AddOblivious();
	void RemoveOblivious();
	// `m_iIsOblivious`, saved through its generated binding.
	int32 ObliviousCount = 0;

	// --- `m_bfNPCFrenziedFlags` (`+0x5b84`) ---------------------------------------------------------
	// A `CAI_BaseNPC` word no task addresses, so it carries no name table. 16c's two discipline arms
	// write it whole -- `0x3b1c` for `DoPossession` (`0x102c51a0`), `0x9fbd` for `DoFrenzy`
	// (`0x102c5310`). Bit meanings come from their readers: `0x8` always-PVS/LOS
	// (`CalcNextNormalThink`, `CalcNextAIThink`, `SetPlayerLOS`), `0x10` "does not witness", `0x800`
	// the frenzy friend, `0x8000` `NPCThink`'s 1% death-scream roll.
	static constexpr uint32 FrenziedAlwaysInPlayerView = 0x00000008;
	// `0x10`, "does not witness". Its one reader is slot 587 `CanWitnessSupernatural`
	// (`0x1028ef20`, `1028ef53 TEST byte ptr [ESI+0x14c8],0x10`), whose fourth refusal it is: a
	// frenzied body cannot witness a supernatural act at all. Story 29d, Conditions10.
	static constexpr uint32 FrenziedDoesNotWitness = 0x00000010;
	// `0x800`, the frenzy friend. Its readers are slot 467 `QueryHearSound` (`0x102b35b0`,
	// `102b3621`) and slot 468 `QuerySeeEntity` (`0x102b38b0`, `102b38f1`), which both refuse the
	// entity `m_hFriendPlayer` (`+0x60ac`) resolves to while the bit stands. Story 29d, Senses10.
	static constexpr uint32 FrenziedFriendPlayer = 0x00000800;
	bool HasFrenzied(uint32 Mask) const { return (FrenziedWord & Mask) != 0; }
	// 16c's writer. Retail assigns the whole word rather than OR-ing, and so does this.
	void SetFrenziedWord(uint32 Value) { FrenziedWord = Value; }
	// `m_bfNPCFrenziedFlags`, saved through its generated binding.
	uint32 FrenziedWord = 0;

	// The flag words by name plus the refcount: the trace row `TASK_MAKE_OBLIVIOUS` and
	// `TASK_SET_NPC_FLAG` record.
	FString DescribeNpcFlags() const;

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

	virtual bool IsIdealActivityCurrent() const;

	virtual void RecordScheduleEvent(const FString& Row) override;

	virtual void StopMoving();

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
	// --- Moved from `FElysiumNpc`'s protected section (story 5 step 5) ---

	FElysiumNpcMind Mind;

	// `m_hTargetEnt` (`+0x5ce4`); see `SetTarget`.
	FElysiumEntityHandle TargetEnt;

	bool bMoveIssued = false;

	// Whether the death handoff has already run. Session state, not save state: it is derivable from
	// the mind's dead state, and a restored corpse re-runs the handoff on the body the load rebuilt.
	bool bDeathHandoffDone = false;

	// `BecomeClientRagdoll` (`0x10090180`, the ragdoll arm of `CreateCorpse` `0x1032c0e0`) has run:
	// the body went to physics. Retail has no such flag -- its client ragdoll is a separate entity --
	// so this marks the port's own body as the corpse (`IsCorpse`), which the think's
	// `SUB_PVSRemove` later removes.
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
