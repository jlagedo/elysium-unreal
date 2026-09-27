#pragma once

#include "Substrate/ElysiumNpcPlayerController.h"

// `CNPC_VFrenzyShadow` (primary vtable `0x104b2184`), built by `npc_VFrenzyShadow` factory
// `0x10375050` (story 5 fold A2).
//
// The player's frenzy body: `CBasePlayer::CheckForPlayerFrenzy` (`0x10161fc0`) fills the hostile
// scratch list (`0x10376d00`) and then creates one of these through `GetControllerNPC`, so it is a
// player controller — owned by the player, forwarding to it — that fights for the player. Its only
// creator has no port body, so no shipped path stands one today; the class keeps the retail contract
// and tests construct it by classname.
//
// Twenty-six own slots relative to the controller (the deleting destructor, slot 5 `0x10376f20`, is
// the C++ destructor's; slot 82 `0x10375280` is the datamap accessor, which the generated bindings
// stand). It puts BACK the base view and aim cones the controller answers false for (362-365), and
// inherits the controller's 72, 245, 246, 300, 404, 432, 437 and 488-497.
class FElysiumNpcFrenzyShadow : public FElysiumNpcPlayerController
{
public:
	// The retail class this C++ class is: `OwnRetailClass`'s row and `FElysiumNpc::AsSpecies`'s key.
	static constexpr const TCHAR* RetailClassName = TEXT("CNPC_VFrenzyShadow");

	virtual const FElysiumNpcClass* OwnRetailClass() const override;

	// Slot 103 `0x10375c50`.
	virtual void Spawn() override;
	// Slot 138 `0x10375d70` — `return 3;`.
	virtual int32 Classify() override;
	// Slot 142 `0x10376ae0`, slot 144 `0x10376b50`, slot 390 `0x10376b10`.
	virtual int32 OnTakeDamage(void* Info) override;
	virtual void Event_Killed(void* Info) override;
	virtual int32 OnTakeDamage_Alive(void* Info) override;
	// Slots 362-365 `0x10326a20` / `0x102b4540` / `0x10326bd0` / `0x10326ae0` — the base bodies,
	// restored.
	virtual bool FInViewCone(const FVector& PointCm) override;
	virtual bool FInViewCone(FElysiumEntity* Candidate) override;
	virtual bool FInAimCone(const FVector& TargetCm) override;
	virtual bool FInAimCone(FElysiumEntity* AimTarget) override;
	// Slot 420 `0x10375c80`.
	virtual void NPCInit() override;
	// Slot 431 `0x10375e50`.
	virtual void NPCThink() override;
	// Slot 433 `0x10375ed0`.
	virtual void GatherConditions() override;
	// Slot 438 `0x10375d90` — the species hook of the port's selector.
	virtual int32 SpeciesSelectSchedule() override;
	// Slot 440 `0x10375f20` — the raw-number body of the port's `TranslateSchedule`.
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	// Slot 442 `0x10375f50`. `Task` is retail's `Task_t*`, which the port stands as
	// `FElysiumScheduleStep` (`+0x00` task id, `+0x04` data).
	virtual int32 StartTaskSlot442(void* Task) override;
	// Slot 478 `0x103766d0`.
	virtual FElysiumEntity* BestEnemy() override;
	// Slot 546 `0x10375440` — the class's own squad-slot id space.
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
	// Slots 599-602 `0x10376b70` / `0x10376ba0` / `0x10376bd0` / `0x10376bf0`.
	virtual bool Slot599(int32 Arg) override;
	virtual bool Slot600(FElysiumEntity* Enemy) override;
	virtual void Slot601(FElysiumEntity* Enemy) override;
	virtual bool Slot602() override;

	// --- Own datamap words (`datamap_CNPC_VFrenzyShadow` `0x10637aa8`, both SAVE) ------------------

	int32 HostileEnemyCount = 0;   // +0x6664 m_iHostileEnemyCount
	bool bFailedGrapple = false;   // +0x6668 m_bFailedGrapple

	// --- The bodies and seams -------------------------------------------------------------------

	/** `0x10376c10`, `NPCInit`'s tail `JMP`: zero `m_iHostileEnemyCount`, resolve the friend player's
	 *  `+0xa8` record, then walk the static scratch list `DAT_1093ada8[0 .. DAT_1093ae9c)` — for each
	 *  entry's combat character, count it when ITS `IRelationType(friend)` is `D_HT`, and hand it to
	 *  slot 544 `UpdateEnemyMemory` at its origin. Runs over `HostileScratchList()`. */
	int32 HostileRecount();
	/** The loop of `0x10376c10` over an explicit list, so the arms are reachable from a test. */
	int32 HostileRecountOver(TConstArrayView<FElysiumEntity*> Scratch);
	/** SEAM for the static scratch list `DAT_1093ada8` (32 entries) and its count `DAT_1093ae9c`.
	 *  Its one producer is `0x10376d00` — a ±1024/±1024/±128 `EntitiesInBox` sweep around the player
	 *  filtered on the ground flag, `+0x340`, slot 158 and `0x100b5190` — which
	 *  `CBasePlayer::CheckForPlayerFrenzy` (`0x10161fc0`, the thunk call at `0x1016201a`) runs just
	 *  before it creates the shadow. The frenzy check has no port body, so nothing fills the list:
	 *  it answers EMPTY, and the recount's only effect is the count reset. */
	static TConstArrayView<FElysiumEntity*> HostileScratchList();

	/** The copy `StartTask`'s prologue takes of the OWNER's `m_bFrenzyHunger` (`+0x1474`, a
	 *  `CBaseCombatCharacter` word) into this body's own `+0x1474`. The port's combat character
	 *  carries no such word, so the copy lives on the one class that reads it. */
	bool bFrenzyHunger = false;
	/** SEAM for the owner player's `m_bFrenzyHunger` (`+0x1474`): no port member stands for it, so
	 *  it answers false — the not-hungry arm. */
	static bool OwnerFrenzyHunger(const FElysiumEntity& Owner);

	/** SEAM for `Weapon_Create("item_w_fists")` (`0x1000b0b4` at `0x10375c89`). Creating an entity
	 *  from `NPCInit` — which the port runs from `Activate` — would grow the entity list the spawn
	 *  pass is walking (the reason `SpawnEquipLoadout` defers its items), so it answers null and
	 *  counts the request on `SpawnEquipRequests`. */
	FElysiumEntity* WeaponCreateFists();
	/** The unconditional `OR [EDI+0x19c],0x40` on what `Weapon_Create` answered (`0x10375cb6`), and
	 *  the crash-guarded fault when it answered null: retail dereferences the null pointer. */
	int32 FistsNoDrawWrites = 0;
	int32 FistsNullWeaponFaults = 0;

	/** SEAM for `(*DAT_10924a6c)->vfunc1()` — the global "two hostiles" event `GatherConditions`
	 *  fires before setting local condition 121. The event object is unrecovered; counted. */
	int32 TwoHostilesEventFires = 0;

	/** SEAM for the owner player's slot 424, `CBasePlayer::Replenish(1)` (`0x10168320`, reached through
	 *  `vt+0x6a0` in task 330's arm). The port's feed entry is `FElysiumPlayer::AttemptFeed(victim)`,
	 *  which takes a victim this call does not name; the argument-1 form is unrecovered, so it answers
	 *  false — the arm that sets `m_bFailedGrapple`. */
	static bool OwnerReplenish(FElysiumPlayer& Owner);
	/** The success arm's two player writes: `player+0x1476 = 1` and `0x1033f6d0(player)`. Counted;
	 *  neither is recovered. */
	int32 OwnerFeedAcceptances = 0;

	/** SEAM for `CAI_Pathfinder` `0x10306700(this, myOrigin, goal, 256.0)` — the hunt route task
	 *  `0xae` builds. No pathfinder stands on the kernel; answers false, the arm that fails the task
	 *  with `0x20`. */
	bool BuildHuntRoute(const FVector* GoalUnits);
	/** SEAM for `0x100f8580(origin, 256.0)` — the nearest entity within 256 units the hunt tasks fall
	 *  back to when there is no enemy. Unrecovered filter; answers null. */
	FElysiumEntity* NearestHuntEntity() const;
	/** SEAM for the active weapon's `+0x5d0` dispatch the attack tasks answer with (task `0x37`
	 *  passes `(0xf18, 1, 1)`). Unreachable while `ActiveWeaponCapabilityWord` answers 0; answers 0. */
	int32 WeaponAttackDispatch(bool bTask37);
	/** SEAM for `0x10295460(this, act, false)` — the activity's sequence, `-1` when the model has none.
	 *  The port's activity vocabulary is names, not retail ids (`RestartIdealActivityId`), so it
	 *  answers -1. */
	int32 SequenceForActivity(int32 RetailActivity) const;
};
