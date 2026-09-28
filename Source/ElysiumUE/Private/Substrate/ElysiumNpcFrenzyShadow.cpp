#include "Substrate/ElysiumNpcFrenzyShadow.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumScheduleText.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumWeaponClasses.h"

// `CNPC_VFrenzyShadow` — story 5 fold A2. Every body is the retail body at the address its comment
// names, read off the listing (`vtmb_asm`) and the decompiled C (`vtmb_code`); the evidence brief is
// `$ELYSIUM_WORK_ROOT/research/npc-kernel/story-5/briefs/A2-controller-line.md`.

namespace
{
	// The source file every trace in this class stamps (`0x10638f04`).
	const TCHAR* const GShadowFile = TEXT("NPC_VFrenzyShadow.cpp");

	// Retail `m_NPCState` 2, `NPC_STATE_COMBAT` (Bach's own state table, `ElysiumNpcBach.cpp`).
	constexpr int32 GShadowRetailStateCombat = 2;
	// Slot 404's `D_HT`.
	constexpr int32 GShadowD_HT = 1;

	// `Spawn`'s `CapabilitiesAdd(0x200000)` (`PUSH 0x200000`, `0x10375c51`) — the spawn-equip bit.
	constexpr int32 GShadowSpawnCapability = 0x200000;
	// `Classify` `0x10375d70`: `MOV EAX,3`. `CheckForPlayerFrenzy` warns "Non Troika Ent" when the
	// created entity does not answer 3 (`0x10162075`).
	constexpr int32 GShadowClass = 3;

	// `NPCInit` `0x10375c80`'s writes.
	const TCHAR* const GShadowFists = TEXT("item_w_fists");   // `0x10585f08`
	constexpr int32 GShadowRetailState = 0xb;                 // `+0x5cc4 = 0xb`, `SetState(0xb)`
	constexpr int32 GShadowNpcInitLine = 0xe4;                // `+0x1b3c`/`+0x1b40` trace
	constexpr uint32 GShadowFrenziedFlags = 0x5ddfu;          // `+0x5b84`
	constexpr float GShadowSpeedScale = 8.f;                  // `+0x1488 = 0x41000000`

	// `GatherConditions` `0x10375ed0`: local condition 121 (`0x79`),
	// `COND_VFRENZYSHADOW_TWO_HOSTILES` in the class's condition space (corpus unit
	// `cnpc_vfrenzyshadow`). The live condition word is class-local (`FElysiumNpcConditions`).
	constexpr EElysiumNpcCond GShadowCondTwoHostiles = static_cast<EElysiumNpcCond>(0x79);

	// `SelectSchedule` `0x10375d90`.
	constexpr int32 GShadowSelectTraceId = 0xf;               // `+0x1b2c = 0xf`
	constexpr int32 GShadowSelectLine = 0x133;
	constexpr int32 GShadowSchedFeed = 0x161;                 // 353 SCHED_VFRENZYSHADOW_FEED

	// `StartTask` `0x10375f50`'s task ids and constants.
	constexpr int32 GShadowTaskAttackA = 0x34;
	constexpr int32 GShadowTaskAttackB = 0x35;
	constexpr int32 GShadowTaskAttackC = 0x36;
	constexpr int32 GShadowTaskAttackD = 0x37;
	constexpr int32 GShadowTaskAttackE = 0x3e;
	constexpr int32 GShadowTaskAttackF = 0x3f;
	constexpr int32 GShadowTaskGoalTolA = 0x4e;
	constexpr int32 GShadowTaskGoalTolB = 0x9f;
	constexpr int32 GShadowTaskHuntPath = 0xae;
	constexpr int32 GShadowTaskHuntTarget = 0xaf;
	constexpr int32 GShadowTaskTurnA = 0x123;
	constexpr int32 GShadowTaskTurnB = 0x124;
	constexpr int32 GShadowTaskAttemptFeed = 0x14a;           // 330 TASK_VFRENZYSHADOW_ATTEMPT_FEED
	constexpr uint32 GShadowMeleeWeaponBits = 0x18000u;       // `(uVar3 & 0x18000) != 0`
	constexpr double GShadowGoalToleranceScale = 0.2;         // `_DAT_10449198` (a double: `FMUL double ptr`)
	constexpr int32 GShadowWeaponDispatchArg0 = 0xf18;        // `0x10376052 PUSH 0xf18`
	constexpr int32 GShadowActTurnPrimary = 0x13;             // the first activity the turn asks for
	constexpr int32 GShadowActTurnFallback = 9;
	// `TaskFail` codes the arms raise.
	constexpr int32 GShadowFailNoWeapon = 0x1f;
	constexpr int32 GShadowFailNoRoute = 0x20;
	constexpr int32 GShadowFailNoEnemy = 6;
	constexpr int32 GShadowFailNoActivity = 0x15;
	constexpr int32 GShadowFailNoClearance = 0xe;
	// The failure-trace lines (`+0x1b44` / `+0x1b48`).
	constexpr int32 GShadowLineNoWeapon = 0x1b5;
	constexpr int32 GShadowLineNoRoute = 0x1eb;
	constexpr int32 GShadowLineNoTarget = 0x206;
	constexpr int32 GShadowLineNoEnemy = 0x25c;
	constexpr int32 GShadowLineNoActivity = 0x26d;
	constexpr int32 GShadowLineNoClearance = 0x28b;
}

// Slot 103: `0x10375c50`.
void FElysiumNpcFrenzyShadow::Spawn()
{
	// `CapabilitiesAdd(0x200000)` (`0x10012855`), then `JMP 0x10010a91` -> the controller's
	// `Spawn` `0x103a4510`, DIRECT.
	CapabilityWord |= GShadowSpawnCapability;
	FElysiumNpcPlayerController::Spawn();
}

// Slot 138: `0x10375d70`.
int32 FElysiumNpcFrenzyShadow::Classify()
{
	return GShadowClass;
}

// Slot 142: `0x10376ae0`.
int32 FElysiumNpcFrenzyShadow::OnTakeDamage(void* Info)
{
	// `owner = GetOwnerEntity()` (slot 97); when it resolves, `owner->OnTakeDamage(info)` (slot 142,
	// `+0x238`, VIRTUAL on the OWNER — the player takes the shadow's damage); then ALWAYS `XOR EAX,EAX`:
	// a frenzy shadow never reports damage taken, whatever the owner answered.
	if (FElysiumEntity* Owner = World != nullptr ? World->Resolve(GetOwnerEntity()) : nullptr)
	{
		Owner->OnTakeDamage(Info);
	}
	return 0;
}

// Slot 144: `0x10376b50`.
void FElysiumNpcFrenzyShadow::Event_Killed(void* Info)
{
	// Three bytes, `RET 4`: the whole death handling of `CAI_BaseNPC::Event_Killed` is suppressed —
	// no ideal-state change, no corpse, no outputs.
	(void)Info;
}

// Slot 390: `0x10376b10`.
int32 FElysiumNpcFrenzyShadow::OnTakeDamage_Alive(void* Info)
{
	// `owner = GetOwnerEntity()`; when it resolves, its `+0x9c` combat character's slot 390 (`+0x618`)
	// with the same info — one indirection deeper than slot 142's forward; then ALWAYS 0.
	FElysiumEntity* Owner = World != nullptr ? World->Resolve(GetOwnerEntity()) : nullptr;
	if (FElysiumCombatCharacter* OwnerCharacter = Owner != nullptr ? Owner->AsCombatCharacter() : nullptr)
	{
		OwnerCharacter->OnTakeDamage_Alive(Info);
	}
	return 0;
}

// Slot 362: `0x10326a20`, `CBaseCombatCharacter`'s body, restored.
bool FElysiumNpcFrenzyShadow::FInViewCone(const FVector& PointCm)
{
	return FElysiumCombatCharacter::FInViewCone(PointCm);
}

// Slot 363: `0x102b4540`, the Troika body, restored.
bool FElysiumNpcFrenzyShadow::FInViewCone(FElysiumEntity* Candidate)
{
	return FElysiumNpc::FInViewCone(Candidate);
}

// Slot 364: `0x10326bd0`, `CBaseCombatCharacter`'s body, restored.
bool FElysiumNpcFrenzyShadow::FInAimCone(const FVector& TargetCm)
{
	return FElysiumCombatCharacter::FInAimCone(TargetCm);
}

// Slot 365: `0x10326ae0`, `CBaseCombatCharacter`'s body, restored.
bool FElysiumNpcFrenzyShadow::FInAimCone(FElysiumEntity* AimTarget)
{
	return FElysiumCombatCharacter::FInAimCone(AimTarget);
}

FElysiumEntity* FElysiumNpcFrenzyShadow::WeaponCreateFists()
{
	// SEAM (header): the request is counted, nothing is created.
	++SpawnEquipRequests;
	return nullptr;
}

// Slot 420: `0x10375c80`.
void FElysiumNpcFrenzyShadow::NPCInit()
{
	// `CNPC_VFrenzyShadow::NPCInit`, in retail's order:
	//   1. `w = Weapon_Create("item_w_fists")`.
	FElysiumEntity* Fists = WeaponCreateFists();
	//   2. `if (w)`: `Inventory_Can_Insert(w)` (`0x100112d9`) ? slot 383 `Weapon_Equip(w, 0)`
	//      (`+0x5fc`) : `0x1024f7b0(w)`. Unreachable while the create seam answers null; the
	//      `Inventory_Can_Insert` predicate has no port body, so the reachable-day arm is the equip.
	if (Fists != nullptr)
	{
		Weapon_Equip(Fists, false);
	}
	//   3. `w->m_fEffects |= 0x40` (EF_NODRAW) — UNCONDITIONAL, with no null test: a failed create
	//      faults here in retail. The port counts the fault and carries on (named crash guard). The
	//      port's weapon draw state is the holster visual (`FElysiumWeapon::Hide`).
	if (Fists != nullptr)
	{
		FElysiumItem* Item = Fists->AsItem();
		if (FElysiumWeapon* Weapon = Item != nullptr ? Item->AsWeapon() : nullptr)
		{
			Weapon->Hide();
		}
		++FistsNoDrawWrites;
	}
	else
	{
		++FistsNullWeaponFaults;
	}
	//   4. the controller's `NPCInit` `0x103a4580`, DIRECT.
	FElysiumNpcPlayerController::NPCInit();
	//   5. trace `NPC_VFrenzyShadow.cpp:0xe4` into `+0x1b3c` / `+0x1b40`.
	RecordScheduleEvent(FString::Printf(TEXT("NPCInit trace %s:%d"), GShadowFile, GShadowNpcInitLine));
	//   6. `+0x5cc4 = 0xb` (the ideal state), then `SetState(0xb)` (`0x10002554` -> `0x1026e340`).
	WriteIdealStateRetail(GShadowRetailState);
	SetState(GShadowRetailState);
	//   7. `+0x5b84 = 0x5ddf` — the frenzied word.
	SetFrenziedWord(GShadowFrenziedFlags);
	//   8. `m_pSenses+0x80 = 1`.
	Senses.bCanPerformSenses = true;
	//   9. `+0x6664 = 0`, `+0x6668 = 0`.
	HostileEnemyCount = 0;
	bFailedGrapple = false;
	//  10. `+0x1488 = 8.0f` (`m_flSpeedScale`), `+0x65f7 = 1`.
	NpcSpeedScale = GShadowSpeedScale;
	bNavIgnorePhysicsProps = true;
	//  11. tail `JMP 0x1000b74e` -> `0x10376c10`, the hostile recount.
	HostileRecount();
}

TConstArrayView<FElysiumEntity*> FElysiumNpcFrenzyShadow::HostileScratchList()
{
	// SEAM for `DAT_1093ada8[0 .. DAT_1093ae9c)` (header): its producer `0x10376d00` has no port
	// caller, so the list is empty.
	return TConstArrayView<FElysiumEntity*>();
}

int32 FElysiumNpcFrenzyShadow::HostileRecount()
{
	return HostileRecountOver(HostileScratchList());
}

int32 FElysiumNpcFrenzyShadow::HostileRecountOver(TConstArrayView<FElysiumEntity*> Scratch)
{
	// `0x10376c10`:
	//
	//   m_iHostileEnemyCount = 0;                                  // 10376c17 +0x6664
	//   friend = m_hFriendPlayer (+0x60ac) resolved ? its +0xa8 : NULL;
	//   for (i = 0; i < DAT_1093ae9c; ++i) {
	//       cc = DAT_1093ada8[i] ? DAT_1093ada8[i]->+0x9c : NULL;
	//       if (cc && cc->IRelationType(friend) == D_HT)       // slot 404, THE ENTRY's table
	//           ++m_iHostileEnemyCount;
	//       UpdateEnemyMemory(cc, cc->GetAbsOrigin(), ...);    // slot 544, VIRTUAL
	//   }
	//
	// The `UpdateEnemyMemory` call is OUTSIDE the null test and dereferences `cc`; the producer
	// admits an entry only when both pointers are non-null, so the port skips a null entry rather
	// than faulting (named crash guard, unreachable from shipped data).
	HostileEnemyCount = 0;
	FElysiumEntity* FriendRecord = World != nullptr && FriendPlayer.IsSet() ? World->Resolve(FriendPlayer) : nullptr;
	const bool bFriendIsPlayer = FriendRecord != nullptr && World != nullptr
		&& FriendRecord->Handle == World->PlayerHandle();
	const FElysiumEntity* const FriendArgument = bFriendIsPlayer ? FriendRecord : nullptr;
	for (FElysiumEntity* Entry : Scratch)
	{
		FElysiumCombatCharacter* Character = Entry != nullptr ? Entry->AsCombatCharacter() : nullptr;
		if (Character == nullptr)
		{
			continue;
		}
		const FElysiumNpc* EntryNpc = Entry->AsNpc();
		if (EntryNpc != nullptr && EntryNpc->IRelationTypeOf(FriendArgument) == GShadowD_HT)
		{
			++HostileEnemyCount;
		}
		UpdateEnemyMemory(Entry, Entry->Origin, nullptr);
	}
	return HostileEnemyCount;
}

// Slot 431: `0x10375e50`.
void FElysiumNpcFrenzyShadow::NPCThink()
{
	// `CALL 0x10012c6f` -> the controller's `NPCThink` `0x103a4700`, DIRECT (Troika think + slot 614).
	FElysiumNpcPlayerController::NPCThink();
	// Then: `enemy = GetEnemy()` (slot 167, `+0x29c`); when `m_NPCState == 2` AND there is an enemy
	// AND NOT `HasCondition(0x46 COND_SEE_ENEMY)`, slot 544 `UpdateEnemyMemory(enemy,
	// enemy->GetAbsOrigin(), &enemy->field_0x3d4)` — VIRTUAL. The third argument is an ADDRESS inside
	// the enemy (`LEA ECX,[EDI+0x3d4]`, `0x10375e83`) whose meaning is unrecovered; the port passes
	// no informer, which the base body does not read.
	FElysiumEntity* Enemy = static_cast<const FElysiumNpc*>(this)->GetEnemy();
	if (NpcStateRetail() != GShadowRetailStateCombat || Enemy == nullptr)
	{
		return;
	}
	if (Cognition.Conditions.Has(EElysiumNpcCond::SeeEnemy))
	{
		return;
	}
	UpdateEnemyMemory(Enemy, Enemy->Origin, nullptr);
}

// Slot 433: `0x10375ed0`.
void FElysiumNpcFrenzyShadow::GatherConditions()
{
	// `CALL 0x10013f2f` (10375ed3) -> `CAI_BaseNPCTroika::GatherConditions` `0x102b27f0`, DIRECT. Then
	// `CMP [+0x6664],1 / JLE` (signed, `0x10375ed8` / `0x10375edf`):
	//   `m_iHostileEnemyCount > 1` -> the `ent_trace_conditions` read `(*DAT_10924a6c)->vfunc1()` (10375ee9)
	//                                 (answer discarded) and `SetCondition(0x79)` (`0x10375ef0`);
	//   otherwise                   -> `ClearCondition(0x79)` (`0x10375efb`, `0x10015839` -> `0x10269b50`).
	// Story 8 lane L07 re-read the listing: the body agrees arm for arm (Conditions19).
	FElysiumNpc::GatherConditions();
	if (HostileEnemyCount > 1)
	{
		++TwoHostilesEventFires;
		Cognition.Conditions.Set(GShadowCondTwoHostiles);
		return;
	}
	Cognition.Conditions.Clear(GShadowCondTwoHostiles);
}

// Slot 438: `0x10375d90`.
int32 FElysiumNpcFrenzyShadow::SpeciesSelectSchedule()
{
	// `+0x1b2c = 0xf` on every arm (`0x10375d99`). Then, only in `m_NPCState == 2` (COMBAT):
	//   1. `m_bCondTookDamage (+0x5b80) = 0`;
	//   2. `m_iHostileEnemyCount <= 1` AND `GetEnemy()` (slot 167) AND the enemy `IsAlive()` (slot 158,
	//      `+0x278` on the enemy) AND NOT `HasCondition(0x59 COND_ENEMY_UNREACHABLE)` AND NOT
	//      `m_bFailedGrapple` -> trace `:0x133` and answer `0x161` (353 SCHED_VFRENZYSHADOW_FEED);
	// every other arm tail-jumps `0x10375e13 JMP 0x10015ad2` -> `CNPC_VHuman::SelectSchedule`
	// `0x10384ee0` (story 8 Select19).
	SelectScheduleSelector = GShadowSelectTraceId;                 // 0x10375d99
	if (NpcStateRetail() != GShadowRetailStateCombat)             // 0x10375da3 / 0x10375da6
	{
		return FElysiumNpcHuman::SpeciesSelectSchedule();          // 0x10375e13
	}
	Cognition.bCondTookDamage = false;                             // 0x10375dae
	if (HostileEnemyCount > 1)                                     // 0x10375db5 / 0x10375db8 JG
	{
		return FElysiumNpcHuman::SpeciesSelectSchedule();
	}
	FElysiumEntity* Enemy = static_cast<const FElysiumNpc*>(this)->GetEnemy();   // 0x10375dbc slot 167 (again 0x10375dca)
	if (Enemy == nullptr || !Enemy->IsAlive())                     // 0x10375dc4 / 0x10375dd4 slot 158 / 0x10375ddc JZ
	{
		return FElysiumNpcHuman::SpeciesSelectSchedule();
	}
	if (Cognition.Conditions.Has(EElysiumNpcCond::EnemyUnreachable) || bFailedGrapple)   // 0x10375de2 / 0x10375de9 JNZ / 0x10375deb / 0x10375df3 JNZ
	{
		return FElysiumNpcHuman::SpeciesSelectSchedule();
	}
	RecordScheduleEvent(FString::Printf(TEXT("SelectSchedule trace %s:%d -> 0x%x"), GShadowFile,
		GShadowSelectLine, GShadowSchedFeed));
	return GShadowSchedFeed;                                       // 0x10375e09
}

// Slot 440: `0x10375f20`.
int32 FElysiumNpcFrenzyShadow::TranslateScheduleRetail(int32 ScheduleNumber)
{
	// `0x102b11c0(id)` — the frenzied pre-table, UNGATED (the Troika body gates it on a frenzied
	// bit; this one does not); a zero answer falls to the Troika body `0x102b12f0`, DIRECT.
	if (const int32 Frenzied = FrenziedTranslateSchedule(ScheduleNumber); Frenzied != 0)
	{
		return Frenzied;
	}
	return TroikaTranslateScheduleRetail(ScheduleNumber);
}

bool FElysiumNpcFrenzyShadow::OwnerFrenzyHunger(const FElysiumEntity& Owner)
{
	// SEAM (header): the owner's `+0x1474` has no port member.
	(void)Owner;
	return false;
}

bool FElysiumNpcFrenzyShadow::OwnerReplenish(FElysiumPlayer& Owner)
{
	// SEAM (header): `CBasePlayer::Replenish(1)` answers false.
	(void)Owner;
	return false;
}

bool FElysiumNpcFrenzyShadow::BuildHuntRoute(const FVector* GoalUnits)
{
	// SEAM (header): no pathfinder on the kernel.
	(void)GoalUnits;
	return false;
}

FElysiumEntity* FElysiumNpcFrenzyShadow::NearestHuntEntity() const
{
	// SEAM (header): `0x100f8580(origin, 256.0)` answers null.
	return nullptr;
}

int32 FElysiumNpcFrenzyShadow::WeaponAttackDispatch(int32 Arg0, int32 Arg1, int32 Arg2)
{
	// SEAM (header).
	(void)Arg0;
	(void)Arg1;
	(void)Arg2;
	return 0;
}

int32 FElysiumNpcFrenzyShadow::SequenceForActivity(int32 RetailActivity) const
{
	// SEAM (header).
	(void)RetailActivity;
	return INDEX_NONE;
}

// Slot 442: `0x10375f50`, 1,234 bytes.
int32 FElysiumNpcFrenzyShadow::StartTaskSlot442(void* Task)
{
	const FElysiumScheduleStep* Step = static_cast<const FElysiumScheduleStep*>(Task);
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	auto Fail = [this](int32 Line, int32 Reason)
	{
		// `+0x1b44 = file, +0x1b48 = line`, then `TaskFail(reason)` (slot 448, `+0x700`).
		RecordScheduleEvent(FString::Printf(TEXT("StartTask fail trace %s:%d"), GShadowFile, Line));
		TaskFail(Reason);
	};

	// The prologue, every task: `owner = GetOwnerEntity()`; when it resolves and its `+0xa8` is set,
	// copy the owner's `m_bFrenzyHunger` (`+0x1474`) into this body's own.
	if (World != nullptr && OwnerIsThePlayer())
	{
		if (const FElysiumEntity* Owner = World->Resolve(GetOwnerEntity()))
		{
			bFrenzyHunger = OwnerFrenzyHunger(*Owner);
		}
	}
	if (Step == nullptr)
	{
		return FElysiumNpcHuman::StartTaskSlot442(Task);
	}

	// Retail switches on the class-LOCAL task id (`[EDI]`, `0x10375f7f`); the port's step carries the
	// GLOBAL id, so it goes back through slot 450 first (0019/8 L04: the landed body compared raw
	// global ids, which no retail task ever equals once a schedule is installed).
	const int32 TaskLocal = GetLocalTaskId(Step->TaskId);             // 0x10375f7f MOV EAX,[EDI]
	switch (TaskLocal)
	{
	case GShadowTaskAttackA:
	case GShadowTaskAttackB:
	case GShadowTaskAttackC:
	case GShadowTaskAttackD:
	case GShadowTaskAttackE:
	case GShadowTaskAttackF:
	{
		// The attack tasks, only while NOT frenzy-hungry (a hungry shadow falls to the default):
		// a weapon whose `+0x5a0` capability word carries `0x18000` stamps `m_flLastAttackTime` and
		// answers the weapon's own `+0x5d0` dispatch; anything else fails with `0x1f`. The three
		// arguments `(0xf18, 1, 1)` are pushed BEFORE the task-0x37 compare (`0x1037604b`..`0x10376052`,
		// then `0x1037605f CMP EAX,0x37`), so both call sites make the same call (0019/8 pass R
		// correction; the landed body passed a task-0x37 flag instead).
		if (bFrenzyHunger)
		{
			break;
		}
		const FElysiumEntity* Weapon = ActiveWeaponEntity();
		if (Weapon != nullptr && (ActiveWeaponCapabilityWord() & GShadowMeleeWeaponBits) != 0)
		{
			LastAttackTime = Now;                                         // 0x10376057 +0x5d9c
			return WeaponAttackDispatch(GShadowWeaponDispatchArg0, 1, 1);  // 0x10376068 / 0x1037607a +0x5d0
		}
		Fail(GShadowLineNoWeapon, GShadowFailNoWeapon);
		return 0;
	}
	case GShadowTaskGoalTolA:
	case GShadowTaskGoalTolB:
	{
		// `m_flGoalTolerance = thunk_FUN_102d61b0(0) * 0.2 + ResolveTaskDistance(data)` (slot 418,
		// `+0x688`), written to the navigator twice, then `TaskComplete(false)`.
		// `0x102d61b0` is `NAI_Hull::Width`: `row[+0x18] - row[+0xc]` of the table at `0x1060a750`,
		// whose rows are `{bits, name, mins, maxs, ...}` (`staticinit_102d4440`), so maxs.y - mins.y
		// of hull 0 (`HUMAN_HULL`, 26.0). The landed body read it as 0.0; the replayed table
		// (`RetailHullExtents`) carries it (0019/8 L04). The product is x87 double then stored.
		FVector HullMins = FVector::ZeroVector;
		FVector HullMaxs = FVector::ZeroVector;
		RetailHullExtents(0, EElysiumHullExtents::Full, HullMins, HullMaxs);   // 0x10375faf 0x10008de6(0)
		const double HullTerm = HullMaxs.Y - HullMins.Y;
		const float Scaled = static_cast<float>(HullTerm * GShadowGoalToleranceScale);   // 0x10375fb4 FMUL double
		ScheduleHost.GoalToleranceCm = Scaled * ElysiumMove::U;           // 0x10375fbf FSTP +0x6320 (the interim store)
		const float Tolerance = ResolveTaskDistance(Step->Data) + Scaled;              // 0x10375fcb slot 418; 0x10375fd1 FADD
		SetGoalTolerance(Tolerance);                                      // 0x10375fe5 FSTP +0x6320
		// The same word into the navigator's path, twice: `0x10375fec` `0x102ee1c0` (path `+0x28`) and
		// `0x10375ffe` `0x102f2fe0` (path `+0x20`).
		StartTask19SetNavTolerances(Tolerance, Tolerance);
		TaskComplete(false);                                              // 0x10376007
		return 0;
	}
	case GShadowTaskHuntPath:
	{
		// The hunt route: the enemy (slot 168, `+0x2a0`) or else the nearest entity within 256 of this
		// body's slot 220 `GetOrigin` (`+0x370`) names the goal, at the goal's own `GetOrigin`; the
		// route starts at this body's `GetOrigin`. With neither the route is asked with NO goal. The
		// port has no local/abs split, so `GetOrigin` is `Origin`. A refused route fails with `0x20`
		// and STILL falls into `CNPC_VHuman::StartTask`; a built one completes and ALSO falls into it.
		FElysiumEntity* HuntGoal = GetEnemy();
		if (HuntGoal == nullptr)
		{
			HuntGoal = NearestHuntEntity();
		}
		FVector GoalUnits = FVector::ZeroVector;
		if (HuntGoal != nullptr)
		{
			GoalUnits = HuntGoal->Origin / ElysiumMove::U;
		}
		if (!BuildHuntRoute(HuntGoal != nullptr ? &GoalUnits : nullptr))
		{
			Fail(GShadowLineNoRoute, GShadowFailNoRoute);
			return FElysiumNpcHuman::StartTaskSlot442(Task);
		}
		TaskComplete(false);
		return FElysiumNpcHuman::StartTaskSlot442(Task);
	}
	case GShadowTaskHuntTarget:
	{
		// `m_vecHuntPatrolTarget (+0x645c)` from the enemy, else the nearest entity within 256; with
		// neither, fail with `0x20` and fall into `CNPC_VHuman::StartTask`. Otherwise complete and
		// fall into it too. The word holds the target's slot 220 `GetOrigin` (`+0x370`, not
		// `GetAbsOrigin`; the port has no local/abs split), in Source units.
		FElysiumEntity* HuntGoal = GetEnemy();
		if (HuntGoal == nullptr)
		{
			HuntGoal = NearestHuntEntity();
			if (HuntGoal == nullptr)
			{
				Fail(GShadowLineNoTarget, GShadowFailNoRoute);
				return FElysiumNpcHuman::StartTaskSlot442(Task);
			}
		}
		HuntPatrolTarget = HuntGoal->Origin / ElysiumMove::U;
		TaskComplete(false);
		return FElysiumNpcHuman::StartTaskSlot442(Task);
	}
	case GShadowTaskTurnA:
	case GShadowTaskTurnB:
	{
		// The turn tasks: resolve the data word (slot 418, discarded), then with no enemy fail with 6
		// and remember it (`iVar11 = 1000`); with one, the distance is computed and discarded. The
		// activity is `0x13`, else `9`; with neither the task fails with `0x15`. With an enemy the
		// clearance sweep `0x102a1650(act, 0.0, m_flWaitFinishedDelta)` decides: clear -> zero
		// `m_flDesiredMoveYaw`, `m_flWaitFinished = curtime + delta`, `RestartIdealActivity(act)`;
		// blocked -> fail with `0xe`. The no-enemy arm returns after the activity lookup.
		(void)ResolveTaskDistance(Step->Data);
		const bool bNoEnemy = GetEnemy() == nullptr;
		if (bNoEnemy)
		{
			Fail(GShadowLineNoEnemy, GShadowFailNoEnemy);
		}
		int32 Activity = GShadowActTurnPrimary;
		if (SequenceForActivity(Activity) == INDEX_NONE)
		{
			Activity = GShadowActTurnFallback;
			if (SequenceForActivity(Activity) == INDEX_NONE)
			{
				Fail(GShadowLineNoActivity, GShadowFailNoActivity);
				return 0;
			}
		}
		if (bNoEnemy)
		{
			return 0;
		}
		if (TraceMoveClearanceAtYaw(Activity, 0.f, ScheduleHost.WaitFinishedDelta))
		{
			ScheduleHost.DesiredMoveYaw = 0.f;
			BaseScheduleHost.WaitFinished = Now + ScheduleHost.WaitFinishedDelta;
			RestartIdealActivityId(Activity);
			return 0;
		}
		Fail(GShadowLineNoClearance, GShadowFailNoClearance);
		return 0;
	}
	case GShadowTaskAttemptFeed:
	{
		// Task 330, `TASK_VFRENZYSHADOW_ATTEMPT_FEED`: `owner->+0xa8` slot 424 `Replenish(1)`
		// (`+0x6a0`). Accepted -> `player+0x1476 = 1`, `0x1033f6d0(player)`, `m_bFailedGrapple = 0`;
		// refused -> `m_bFailedGrapple = 1`. With no owner player neither word moves. Every arm
		// completes the task.
		FElysiumPlayer* Player = (World != nullptr && OwnerIsThePlayer()) ? World->FindPlayer() : nullptr;
		if (Player != nullptr)
		{
			if (OwnerReplenish(*Player))
			{
				++OwnerFeedAcceptances;
				bFailedGrapple = false;
				TaskComplete(false);
				return 0;
			}
			bFailedGrapple = true;
		}
		TaskComplete(false);
		return 0;
	}
	default:
		break;
	}
	// `CNPC_VHuman::StartTask` `0x103847f0`, DIRECT. It has no port body (the census's unported
	// `CNPC_VHuman#442`); the qualified call runs what the port runs for every VHuman.
	return FElysiumNpcHuman::StartTaskSlot442(Task);
}

// Slot 478: `0x103766d0`, 793 bytes.
FElysiumEntity* FElysiumNpcFrenzyShadow::BestEnemy()
{
	if (World == nullptr)
	{
		return nullptr;
	}
	// `103766d8`: the friend player's `+0xa8` record, resolved ONCE and handed to every relation
	// query below — so a frenzy shadow asks what its candidates think of its FRIEND, not of itself.
	FElysiumEntity* FriendRecord = FriendPlayer.IsSet() ? World->Resolve(FriendPlayer) : nullptr;
	const bool bFriendIsPlayer = FriendRecord != nullptr
		&& FriendRecord->Handle == World->PlayerHandle();
	FElysiumEntity* const FriendArgument = bFriendIsPlayer ? FriendRecord : nullptr;

	auto CandidateHatesFriend = [FriendArgument](FElysiumEntity* Candidate)
	{
		// `10376769` / `10376885`: `cand->IRelationType(friendRecord)`. The CANDIDATE's table is
		// asked, not this NPC's, which is why it goes through the candidate's own NPC leaf when it
		// has one and answers D_ER otherwise — retail's null-`this` arm.
		const FElysiumNpc* CandidateNpc = Candidate != nullptr ? Candidate->AsNpc() : nullptr;
		return CandidateNpc != nullptr && CandidateNpc->IRelationTypeOf(FriendArgument) == GShadowD_HT;
	};
	auto PassesFilter = [](FElysiumEntity* Candidate)
	{
		// `1037673d`..`1037675c` and `1037681d`..`10376846`, the filter both halves share:
		// a live `+0x9c`, `GetFlags` bit `0x8000` clear, `m_bIsBCCTargetable` set and slot 158 alive.
		return Candidate != nullptr && Candidate->AsCombatCharacter() != nullptr
			&& !HasNoTargetFlag(*Candidate) && IsBccTargetable(*Candidate) && !Candidate->IsInert();
	};

	// `1037671a`: the STICKY arm, and it runs only while more than ONE hostile was counted last
	// pass.
	if (HostileEnemyCount > 1)
	{
		FElysiumEntity* Current = GetEnemy();
		if (PassesFilter(Current) && CandidateHatesFriend(Current)
			&& !EnemyMemory.IsEluded(Current->Handle)      // 10376777 IsEluded
			&& !IsUnreachable(Current))                    // 1037678f slot 530
		{
			return Current;                                // 1037679a — unchanged
		}
	}

	// `103767a2`: the full rescan. NOTE the seeds — best null, bestDist `0x10000000`, bestScore
	// **0** (not `-1000`, which is the base body's priority seed) — and the hostile count is zeroed
	// here, so it is rebuilt by this pass.
	FElysiumEntity* Best = nullptr;
	int32 BestDistance = 0x10000000;
	int32 BestScore = 0;
	HostileEnemyCount = 0;

	for (const FElysiumNpcEnemyMemoryRecord& Record : EnemyMemory.Records())
	{
		FElysiumEntity* Candidate = World->Resolve(Record.Handle);
		if (!PassesFilter(Candidate))
		{
			continue;
		}
		if (EnemyMemory.IsEluded(Candidate->Handle))       // 10376859
		{
			continue;
		}
		// `1037686d`: the SCORE, built from four independent terms rather than compared as a
		// lexicographic key — which is the whole difference from the base body.
		int32 Score = 0;
		if (!IsUnreachable(Candidate))                      // slot 530
		{
			Score += 0x40000000;
		}
		if (CandidateHatesFriend(Candidate))                // 10376885
		{
			Score += 0x20000000;
			++HostileEnemyCount;                            // 1037689d
		}
		if (BestEnemyCandidateVisible(Candidate))           // 103768a3 DidSeeEntity || FVisible
		{
			Score += 0x10000000;
		}
		Score += IRelationPriorityOf(Candidate) * 0x1000000;   // 103768d6, slot 405 shifted 24

		if (Score > BestScore)
		{
			// `103768e9`: slot 479 `IsValidEnemy` before the win.
			if (!IsValidEnemy(Candidate))
			{
				continue;
			}
			BestDistance = BestEnemyDistanceKey(*Candidate);
			BestScore = Score;
			Best = Candidate;
			continue;
		}
		if (Score != BestScore)
		{
			continue;                                        // 10376944 JNZ — only EQUAL carries on
		}
		const int32 Distance = BestEnemyDistanceKey(*Candidate);
		// `10376992`: STRICTLY closer, and then `IsValidEnemy`.
		if (Distance < BestDistance && IsValidEnemy(Candidate))
		{
			BestDistance = Distance;
			BestScore = Score;
			Best = Candidate;
		}
	}
	// `103769cb`: on exit, a winner that differs from the current `GetEnemy()` clears
	// `m_bFailedGrapple`. Retail compares the entity against the `+0x9c` it kept, which on this leaf
	// is the same pointer, so the compare is a plain entity compare.
	if (Best != GetEnemy())
	{
		bFailedGrapple = false;
	}
	return Best;
}

// Slot 546: `0x10375440`, the class's own squad-slot id space.
const TCHAR* FElysiumNpcFrenzyShadow::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1093ae28`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VFrenzyShadow"), TEXT("0x10375440"), TEXT("0x1093ae28") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}

// Slot 599: `0x10376b70`.
bool FElysiumNpcFrenzyShadow::Slot599(int32 Arg)
{
	// The WHOLE body, twenty-six bytes:
	//     (*DAT_10924edc)->vfunc1();      // the global melee-entered event, FIRST
	//     m_bInMelee = 1;
	//     return <whatever was in EAX>;
	// It drops EVERY gate the Troika line has — the frenzied bits, the melee-enter timer, the range
	// and height terms and the attack coordinator — so a frenzy shadow enters melee unconditionally.
	// The declared return is `void`; the caller reads the register the event call left behind. Slot
	// 599's port signature answers `bool`, and **true** is the only honest reading: `m_bInMelee` was
	// set, which is what "entered melee" means to every caller of slot 599.
	(void)Arg;
	++MeleeEventFires;   // `(*DAT_10924edc)->vfunc1()`, family Bosses' counter for this global
	bInMelee = true;
	return true;
}

// Slot 600: `0x10376ba0`.
bool FElysiumNpcFrenzyShadow::Slot600(FElysiumEntity* Enemy)
{
	// Twenty-three bytes: `m_bInMelee = 1; (*DAT_10924edc)->vfunc1(); return true;` — the write
	// FIRST here and SECOND in slot 599. Every gate is gone, the re-entry guard included.
	(void)Enemy;
	bInMelee = true;
	++MeleeEventFires;
	return true;
}

// Slot 601: `0x10376bd0`.
void FElysiumNpcFrenzyShadow::Slot601(FElysiumEntity* Enemy)
{
	// `(*DAT_10924edc)->vfunc1(); RET 4` — the event ALONE: no `m_bInMelee` clear, no re-enter timer,
	// no coordinator release (the VHuman line's `0x10385cf0` does all three).
	(void)Enemy;
	++MeleeEventFires;
}

// Slot 602: `0x10376bf0`.
bool FElysiumNpcFrenzyShadow::Slot602()
{
	// `XOR AL,AL; RET` — a frenzy shadow never asks to leave melee.
	return false;
}
