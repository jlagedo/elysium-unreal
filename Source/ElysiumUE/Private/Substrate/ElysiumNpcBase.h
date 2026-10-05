#pragma once

#include "CoreMinimal.h"

#include "ElysiumAnimationIntent.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcKernelTunables.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcNavigator.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcThinkCadence.h"   // `NpcInitThinkDelay` is `ElysiumNpcThink::InitThinkDelay`
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumScheduleCorpus.h"
#include "Substrate/ElysiumScriptedCharacter.h"

class FElysiumScriptedSequence;   // Substrate/ElysiumScriptedSequence.h — `CCineNPC`, the directors
class FElysiumPlaceSet;           // Substrate/ElysiumPlaceSet.h — the AI network (0018 story 4)

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
 FElysiumNpcBase(); // 0x1027c300 owner identity before any memory access
	// 0x10090180 model-interface slot 18: source rig, independent of the cooked solver asset.
	virtual bool HasClientRagdollRig() const;
	bool BecomeClientRagdoll(const FVector& Force, int32 Bone, bool bRetainEntity);
	virtual void BecomeClientRagdoll() override;
	// 0x101c2a30: damage packets have no hitbox input yet (0014); answers no hit.
	virtual int32 CorpseHitboxBone(const void* InInfo) const;
	virtual int32 CorpseForceBone(const void* InInfo) const; // 0x1032c226 LookupBone("Bip01 Spine2")
	int32 BaseRagdollRenderFxWord = 0; // m_nRenderFX +0x168 on base-only NPCs; Troika carries RenderFxWord
	// 0x10090950 native seed playback hook; serial base PlaySequenceClip forward is owed outside D3.
	bool PlayBaseClientRagdollSeed(int32 Sequence, float& OutSeconds, bool& bOutLoops);
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
	 * statics are one per class (`FElysiumNpcBase::IdSpace`).
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

	// `m_pSenses` (`+0x5cdc`), the `CAI_Senses` object. retail's NPC-base constructor order
	// builds it through slot 424 (`CreateComponents`) for EVERY NPC-base instance, the
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

	// +0x5d70 m_scriptState (datamap) -- the NPC's scripted-sequence state: 0 playing, 1 wait,
	// 2 post-idle, 3 cleanup, 4 walk-to-mark, 5 run-to-mark, 6 custom-move-to-mark. Writers:
	// `PossessEntity` `0x101a7880` (by `m_fMoveTo`), `StartTask` `0x102827f0` (`0x62` -> 0, `99`
	// -> 2), `ScriptEntityCancel` `0x101a7170` (-> 3), `CineCleanup` `0x1027d170` (-> 0). Readers:
	// `ShouldThinkFrequently` `0x102c2430`, `TaskMovementComplete` `0x10273f01`, the `0x60` / `0x66`
	// task arms. The NPC's own word since V3c (the cine's `NpcScriptState` deleted, M10).
	int32 ScriptState = 0;
	int32 GetScriptState() const { return ScriptState; }
	void SetScriptState(int32 State) { ScriptState = State; }

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

	// --- The sequence speed and event words (spec 0002 V4a seam) ---------------------------------
	//
	// Four `CBaseAnimating` words, kept on the NPC beside the sequence words this port already keeps
	// here (`ElysiumNpcBaseAnim.inl`: `+0x65c`, `+0x6f0`, `+0x6f8`). Written since spec 0002 V4a (the
	// dispatcher, `ElysiumNpcBaseAnimEvents.cpp`; the clock, `ElysiumNpcBaseAnim.cpp`).

	// +0x658 m_flLastEventCheck -- the cycle the event dispatcher last swept up to. Writers:
	// `CBaseAnimating::DispatchAnimEvents 0x10091880` (stores its look-ahead end, `m_flCycle + 0.1 x
	// cycle rate`), `ResetSequenceInfo 0x10090950` (zeroes it, `0x10090a3d`). Reader: `0x10091880`
	// (the window's start).
	float LastEventCheck = 0.f;

	// +0x560 m_flYawSpeed -- `GetSequenceYawSpeed(m_nSequence)` (`0x10091310`: the sequence's turn
	// yaw over its duration). Writers: `StudioFrameAdvance 0x1008f120` (every advance),
	// `ResetSequenceInfo 0x10090950`. Reader: slot 242 `GetIdealYawSpeed 0x100916a0`. 0 on shipped
	// data (every movement record's angle is 0.0).
	float YawSpeed = 0.f;

	// +0x654 m_flGroundSpeed -- `GetSequenceGroundSpeed(m_nSequence)` (`0x10091490`: move distance
	// over duration, pose-weighted over a blend fan). Writers: `StudioFrameAdvance 0x1008f120` (every
	// advance), `ResetSequenceInfo 0x10090950`, `MoveGroundExecute`'s re-write after slot 18
	// (`0x10264841`). Reader: slot 248 `GetIdealSpeed 0x10091740` (no playback-rate term). Carried
	// in CENTIMETRES per second, as `GroundSpeedCm()` answers it (retail's word is Source units per
	// second; the writer states the conversion).
	float GroundSpeed = 0.f;

	// +0x568 m_fSequencePastHalf -- the cycle is past 0.5. Writers: `StudioFrameAdvance 0x1008f120`
	// (from the real cycle, `0x1008f268..0x1008f280`), `DispatchAnimEvents 0x10091880` (from its
	// look-ahead end). Reader: `CNPC_VTzimisce`'s `0x103bcc00` (`Select19SequencePastHalf`).
	bool SequencePastHalf = false;

	// Slot 258 on the NPC chain: `CBaseAnimatingOverlay::DispatchAnimEvents 0x10098c80` (the base
	// `0x10091880`, then the four overlay layers through `0x10098cd0`), called by `PostRun
	// 0x1026c7c0` with `(interval, this)`. The body is in `ElysiumNpcBaseAnimEvents.cpp`.
	virtual void DispatchAnimEvents(float Interval, FElysiumEntity* Handler) override;

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
	#include "Substrate/ElysiumNpcKernelBaseHelpersBase.inl"
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

	// --- 0018/7: the door transaction's base-line bodies and the navigator words they read ------

	/** `CAI_BaseNPC::OnDoorFullyOpen` `0x1027dd10`, called by the door's `DoorHitTop 0x100f0860`
	 *  on its activator's `+0x94 m_pBaseNPC`: slot 158 `IsAlive`; a null door does nothing; slot
	 *  532(1) when the door is `m_hOpeningDoor`; the navigator un-paused ONLY if paused
	 *  (`0x102ee2e0` / `0x102ee2c0`); `m_bShouldMove = 1`; slot 528; and the Troika's
	 *  `m_eAlternateAI` 1 or 2 back to 0. */
	void OnDoorFullyOpen(FElysiumEntity* Door);

	/** `0x1027dfb0`, the hit-by-door notice (`IsCloseBlocked 0x100f0c00`, `StartBlocked 0x100f1340`):
	 *  `IsAlive`; `SetCondition(0x34)`; `m_hCondHitByDoor (+0x5d2c) = door`; then, when
	 *  `debug_hit_by_mode` is 0 (`HitByDoorGate`), the door being opened goes to `OnDoorBlocked`, any
	 *  other door is MAX-stamped `curtime + 5.0` and becomes `m_hBlockedDoor`; at the shipped 1, the
	 *  alternate arm: slot 532(4). */
	void HitByDoor(FElysiumEntity& Door);

	/** `0x1027dfb0`'s gate `IsCommand() || m_nValue == 0` on `DAT_1092038c`, the parent pointer of the
	 *  ConVar `debug_hit_by_mode` (object `0x10920388`, registrar `0x10264fc0`: name `0x105cb8dc`,
	 *  default `"1"` `0x10539978`, help "Set this to 1 to use alternate hit by door code.", read from
	 *  the image). A ConVar is no command, so the door-notice arm runs only at 0; the SHIPPED value 1
	 *  takes the alternate arm, slot 532(4). The port carries the shipped default (no console binds
	 *  this ConVar). The head call `(*DAT_10924a6c)->vtable+4()` is `ent_trace_conditions`
	 *  (`0x10924a68`, a debug read whose answer is discarded): not reproduced, no observable. */
	static constexpr int32 DebugHitByModeShipped = 1;
	bool HitByDoorGate() const { return DebugHitByModeShipped == 0; }

	/** `0x1027dc10` -- the base movement sink's slot 1 (`this = npc+0x19b0`): the move goal's
	 *  blocker (`goal+0x60`), its door (`+0xa4`), slot 531 `OnObstructingDoor(goal, door,
	 *  distClear, &result)`; true = handled, the result in `MoveSinkResult`. The Troika sink
	 *  `0x10298340` (`FElysiumNpc::MovementSinkObstructed`) tail-calls it. The port's goal is the
	 *  step's facts: `Blocker` stands for `goal+0x60` (a door the body's smart link is held at, or
	 *  an entity it names), `maxDist` is the leg's remaining distance and `distClear` the distance
	 *  to where the hold stands. */
	bool NavMoveSinkDoorStep(const FNavStepFacts& Step);

	/** The movement sink's `AIMoveResult_t* pResult` (slot 1's last argument): seeded `Ok` before
	 *  each dispatch, written by whichever sink arm handles the step; `NavMoveNormalPass` answers it. */
	ENavMoveResult MoveSinkResult = ENavMoveResult::Ok;

	/** The last move facts `NavSampleStep` read (the port's view of the path the look-ahead and the
	 *  move-step door arms read: the door link held at, the next door link ahead). */
	FElysiumNpcMoveFacts NavLastFacts;
	bool bNavLastFactsValid = false;

	/** `nav+0x38` -- `SimplifyPath`'s far-scan stamp (the constructor `0x102eca50` stores 0). */
	double NavSimplifyNextTime = 0.0;
	/** `nav+0x50` `m_fRememberStaleNodes` -- the stale mark's gate; the constructor `0x102eca50`
	 *  stores 1 and no other writer is recovered. */
	bool bNavRememberStaleNodes = true;
	/** The pathfinder's `+0x14`: the `curtime` its last stale-link re-probe ran at (`0x102fce80`
	 *  probes at most once per `curtime` per pathfinder). */
	double PathfinderLinkProbeTime = -1.0;

	/** `CAI_Navigator::SimplifyPath` `0x102f13d0`'s door half: nav type 0 or 2; when forced or
	 *  `nav+0x38 <= curtime`, stamp `curtime + 0.5` and run the far scan (`0x102f0e80` radius 384 /
	 *  `0x102f0fe0`); then ALWAYS the quick pass (`0x102f13a0` radius 144). Once a door has been
	 *  seen the rest of the tick's passes are skipped. The NAMED MODERNIZATION: the navmesh
	 *  follower shortcuts the path itself, so the only point each pass probes is the far end of the
	 *  next door smart link on the path, when it lies inside the pass's radius. True = a door
	 *  refused (`OnNavFailed(0x0e)` was raised inside). */
	bool NavSimplifyPath(bool bForce);

	/** `0x102f06e0` toward one path point: the ray from slot 193 (`WorldSpaceCenter`) under
	 *  `0x2400b` (`0x2600b` on a pedestrian path, `path+1`); clear -> no door; a hit door -> the lock
	 *  test `0x100eec70` (refused: dropped, nothing written) -> slot 531 with a zeroed goal whose
	 *  `maxDist = dist + 10.0` -> handled sets `bOutDoorSeen`; a non-zero result raises
	 *  `OnNavFailed(0x0e, 1)` then `OnDoorBlocked`. Answers true iff that failure was raised. */
	bool NavDoorProbe(const FVector& PointCm, bool& bOutDoorSeen);

	/** `0x102f1fa0(nav, Seconds, Blocker)`: gated on `nav+0x50`, a live path (`0x102ee6a0` inside),
	 *  and the link the NPC's current path segment stands on; marks that link stale. The port's
	 *  "current link" is the door smart link the body is held at, else the next one ahead on its
	 *  path (the link between the last node passed and the head's node); a segment that is no door
	 *  link has no link to mark. `NavMarkStaleLink` (the Move tail's 4.0 s) routes here. */
	void NavMarkLinkStale(double Seconds, const FElysiumEntity* Blocker);

	/** `0x102ff960`'s stale tail over `0x102fce80`, for the door smart link between `StartCm` and
	 *  `EndCm` (`AElysiumNavDoorLink::IsLinkPathfindingAllowed` asks it per route query): not stale
	 *  -> usable; `curtime > StaleUntil` (strict) -> clear, usable; else the FIRST ask this
	 *  `curtime` re-probes the segment (`0x10304a40`: this NPC's hull swept under `0x202400b`, the
	 *  world answer) and a clear probe clears the mark; still stale -> a live `StaleDoor` gets
	 *  `OnDoorBlocked` and the link is refused. */
	bool DoorLinkPathfindingAllowed(FElysiumEntity& Door, const FVector& StartCm, const FVector& EndCm);

	/** `CAI_Pathfinder::CheckStaleRoute` `0x10304a40`'s ground arm `0x103048d0` between a link's two
	 *  ends (world cm): the hull probe under `0x202400b`; blocked -> the local route `0x103059d0`
	 *  (SEAM, not found, counted in `NavStaleRouteLocalRouteAsks`); blocked by an NPC -> the probe
	 *  again under `0x2400b`. True = clear. */
	bool NavCheckStaleRoute(const FVector& StartCm, const FVector& EndCm);
	int32 NavStaleRouteLocalRouteAsks = 0;

	/** S1 `0x102eefb0` -- the obstructed step's pre-sink arms, in its order, before the movement sink
	 *  (slot 1) is asked. `MaxDistUnits` is the goal's `+0x28` (the leg's remaining distance) and
	 *  `DistClearUnits` the distance to the obstruction. True = the step is decided here, its status
	 *  in `OutResult`; false = hand it to the sink. */
	bool NavObstructionPreSink(const FNavStepFacts& Step, float DistClearUnits, ENavMoveResult& OutResult);

	/** The distance to the obstruction the step names, units: to where a door link holds the body,
	 *  else 0 (the body's own capsule met it). */
	float NavStepDistClearUnits(const FNavStepFacts& Step) const;

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

	// The `CAI_BaseNPC` half of the NPC record (`0x1027bc60` and its `Restore` twin), written ahead of the
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

	// `m_scriptState +0x5d70 in {4,5,6}` (walk / run / custom-move to the mark), read from the NPC
	// word. Both retail readers test the same set: `ShouldThinkFrequently 0x102c2430`'s second term
	// (`3 < s && s < 7`) and `TaskMovementComplete`'s `0x10273f01..0x10273f14` (`CMP 6 / 4 / 5`,
	// any hit skips the `SetIdealActivity` at `0x10273f20`), so one helper serves both.
	bool IsScriptDriven() const
	{
		return ScriptState == 4 || ScriptState == 5 || ScriptState == 6;
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

	// --- Retail's one-NPC trace (0018 story 8, brief S7) ---------------------------------------
	// Recovered in `docs/vtmb/npc-ai/schedule-kernel.md` § "The debug NPC's prints". Definitions in
	// `ElysiumNpcBaseTrace.cpp`.

	/** `m_debugOverlays` (`+0x224`) bit `0x8000000`, which `npc_task_text` (`0x10087b70`) toggles
	 *  and `SetSchedule` / `MaintainSchedule` / `IsScheduleValid` / `TaskFail` test before their
	 *  "Schedule:", "Task:", "Break condition ->" and "TaskFail ->" prints. */
	static constexpr int32 OverlayTaskTextBit = 0x08000000;
	/** `m_debugOverlays` bit `0x80000000`, which `ent_trace` (`0x100b0ad0`) toggles: "trace this
	 *  entity" for every `ent_trace_*` sub-switch. */
	static constexpr int32 OverlayEntTraceBit = static_cast<int32>(0x80000000u);

	/** `DAT_10925444`, the `ai_debug_npc` handle, resolved (serial match, live slot) to THIS NPC —
	 *  the test every hint validator's reason-string arm makes (`FElysiumNpc::IsHintDebugNpc`
	 *  forwards here) and the port's key for its one trace ring. */
	bool IsAiDebugNpc() const;

	/** `CAI_BaseNPCTroika` slot 18 (`0x1028de10`, formatter `0x1028d990`): one trace line,
	 *  `"%-20s  %6.2f : %*s %s\n%s%s %s%s %s\n\n"` — name, `curtime`, `Indent` spaces, `Message`,
	 *  then (only while `ent_trace_conditions > 0`) `"CONDS:"` + `" %s"` per held condition + `"\n"`,
	 *  the `m_afMemory` letters, the `m_bfAINPCFlags` letters and `"NAV CLIMB|JUMP"`. Ungated, as
	 *  retail's is: the caller holds the gate. Logged on `LogElysiumNpcTrace` and, for the
	 *  `AiDebugNpc`, appended to the world's trace ring. */
	void NpcTraceMessage(const FString& Message, int32 Indent = 0) const;

	/** The port's stand-in for the 129 compiled-out `ent_trace_conditions` gates retail keeps in
	 *  front of its `SetCondition` / `ClearCondition` call sites (`(**(*DAT_10924a6c + 4))()` with
	 *  the answer discarded): under `ent_trace` and `ent_trace_conditions > 0`, one trace line per
	 *  condition `GatherConditions` changed. NAMED MODERNIZATION (debug output only; retail's line
	 *  text did not survive the compile). */
	void TraceConditionDelta(const FElysiumNpcConditions& Before) const;

	/** `SetSchedule` `0x10280e50`'s tail print under `npc_task_text`: "Schedule: %s". */
	virtual void DebugScheduleInstalled(int32 GlobalScheduleId) override;

	/** `IsScheduleValid` `0x10280ff0`'s break print under `npc_task_text`: the LOWEST ordinal that
	 *  fired, `"   Break condition -> !%s"` when it is in the inverted set, else `"-> %s"`. Both
	 *  sets are GLOBAL ordinals. */
	virtual void DebugScheduleBreak(const FElysiumNpcConditions& Firing,
		const FElysiumNpcConditions& InvertedFiring) override;

	// --- The AI trace taps (spec 0002 step 1 T5, `stories/wave2/seam.md`) ---------------------------
	// Debug output only, each behind `FElysiumEntityWorld::HasAiTraceSink()`: they read, format and
	// emit, and never write a word a rule reads. Definitions in `ElysiumNpcBaseTrace.cpp`.

	/** A harness sink is installed on this NPC's world (`FElysiumEntityWorld::HasAiTraceSink`). */
	bool IsAiTraced() const;
	/** `FElysiumEntityWorld::EmitAiTrace` for this NPC; a no-op without a world or a sink. */
	void EmitAiTrace(FName Kind, FString Text) const;
	/** `taskdone`: the running step's task name (`CurrentRetailTaskNumber`), at every writer of
	 *  `TASKSTATUS_COMPLETE`. */
	void TraceTaskDone() const;
	/** `state`: `OLD -> NEW` in retail's `NPC_STATE` names (`0x1027e660`), when they differ. */
	void TraceStateChange(int32 OldRetail, int32 NewRetail) const;
	/** The name a `sequence` / `seqfinished` event prints for a sequence number: the sequence bridge's
	 *  clip label on the Troika line, else `seq <n>`. */
	FString TraceSequenceName(int32 Sequence) const;

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

	// The motor's own bookkeeping: a leg was issued to `IElysiumNpcMotor` and not since dropped (written
	// by `NavIssueLeg` and the route builders, read by the corner chain's re-issue dedupe). No retail
	// word: NOT to be read for a retail decision. Whether a route stands is `IsGoalActive 0x102ee6a0`
	// (`NavIsGoalActive()`, the head waypoint `path+0x24`).
	bool bMoveIssued = false;

	// Whether the death handoff has already run. Session state, not save state: it is derivable from
	// the mind's dead state, and a restored corpse re-runs the handoff on the body the load rebuilt.
	bool bDeathHandoffDone = false;
	USkeletalMeshComponent* DeathHandoffVisual = nullptr; // presentation identity only, never dereferenced
	int32 BaseClientRagdollSeed = INDEX_NONE; // native raw seed identity on the base-only line

	// `CreateCorpse` 0x1032c0e0 retains the ordinary NPC as the corpse. Port identity marker,
	// independent of source capability and physics admission; the corpse think owns removal.
	bool bDeathCommitted = false;

	// When the death clip `PlayDeathActivity` started runs out. `TASK_DIE`'s gate waits on it; zero
	// means nothing is playing, which is the ordinary case because base `DIE` names no activity task.
	double DeathPerformanceEndsAt = 0.0;

	// --- Moved from `FElysiumNpc`'s protected section (story 5 step 5) ---

	void SerializeExtendedHeader(FElysiumSaveArchive& Ar);

	// 0x10090180 presentation half only, once per visual. No picks, clocks or no-rig substitute.
	void CompleteDeathHandoff();

};
