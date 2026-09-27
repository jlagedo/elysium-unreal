#include "Substrate/ElysiumNpcGhoulCroucher.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "ElysiumContentPaths.h"
#include "ElysiumDecalSubsystem.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumKeyValues.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumWorldServices.h"
#include "Misc/FileHelper.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcConditionsBodiesShared.h"
#include "Substrate/ElysiumNpcDamageShared.h"
#include "Substrate/ElysiumNpcLifecycle19_2Shared.h"
#include "Substrate/ElysiumNpcLifecycleShared.h"
#include "Substrate/ElysiumNpcPrecache10Shared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcSpeciesLifecycle10Shared.h"
#include "Substrate/ElysiumNpcSpeciesMisc10Shared.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumReactions.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// --- File-scope helpers moved with this class's bodies (story 5 step 4) ---

namespace
{
	// `CNPC_VGhoulCroucher::OnDisturbed`'s `AddEntityRelationship(player, D_HT, 10)`.
	constexpr int32 GCondDisturbedHatePriority = 10;
	const TCHAR* const GGhoulCroucherWeapon = TEXT("item_w_claws_ghoul");
	// `0x1063b088` then `0x1063b028`, preload 0 — the two models that make the male/female split in
	// its `SetModel` sibling (`0x1037b1f0`) reachable.
	const TCHAR* const GGhoulCroucherModels[] = {
		TEXT("models/character/npc/unique/Malkavian_mansion/Stalker/stalker.mdl"),
		TEXT("models/character/npc/unique/Malkavian_mansion/Stalker_Female/stalker_female.mdl"),
	};
	// `CBaseEntity::field_0x94`, the cached `CAI_BaseNPC*` — non-null on exactly an NPC. Family
	// Conditions reads it the same way for the comforter test (`ElysiumNpcConditions.cpp:683`).
	bool SpeciesLifecycle10IsNpc(const FElysiumEntity* Candidate)
	{
		return Candidate != nullptr && Candidate->AsNpcBase() != nullptr;   // `+0x94 m_pBaseNPC`
	}
	// `CNPC_VGhoulCroucher::BurnPlayer`'s per-hitbox pair (`1037c12a`) and the damage its one caller
	// hands it (`1037bf3c`, `0x41200000`).
	constexpr float GBurnHitboxSeconds = 5.f;
	constexpr float GBurnHitboxInterval = 0.5f;      // 0x3f000000
	// `CTakeDamageInfo`'s `bitsDamageType` at `1037c14b` — retail's DMG_BURN.
	constexpr int32 GBurnDamageBits = 8;
}

const FElysiumNpcClass* FElysiumNpcGhoulCroucher::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(RetailClassName);
	return Row;
}

// Slot 420: `0x1037b290`.
// `0x1037b290`
void FElysiumNpcGhoulCroucher::NPCInit()
{
	// Scope-trace name is `"CNPC_VWerewolf::NPCInit"` — retail copy-paste, reproduced as nothing
	// but the comment.
	HumanCombatantNPCInit();
	++InventoryDestroys;
	const TCHAR* Item = bGhoulSpawnDisturbed
		? TEXT("item_w_claws_ghoul")
		: TEXT("item_w_knife");
	GiveNamedFightingItem(Item);                                         // 1021fe50
	ElysiumMiscFlags::Set(MiscFlags, MiscFlagBaseFightingItems);
	if (bGhoulSpawnBurning)
	{
		// `thunk_FUN_100fbc90("la_malkavian_4_ghoul_body_fire_e", origin, angles)`, then the emitter's
		// `+0x3cc(this, 1, "")` attach and `m_hBurningParticle = *emitter->+0x4()`. The create and the
		// attach go through family Damage's emitter seam, which this runtime HAS; the handle store
		// does not, because `SpawnParticleRoot` answers an effect handle and not an entity — so
		// `ScriptUnhide`'s consumer still finds nothing. Stated, not hidden behind the counter.
		++GhoulBurningParticleCreates;
		CreateNamedEmitter(TEXT("la_malkavian_4_ghoul_body_fire_e"), Origin / ElysiumMove::U,
			/*AttachMode=*/1, Handle, TEXT(""));
	}
	NpcKernelLifecycle19_2Shared::Lifecycle19_2HatePlayerClass(*this);
}

// Slot 104: `0x1037b1a0`.
// 0x1037b1a0
void FElysiumNpcGhoulCroucher::Precache()
{
	// `CNPC_VGhoulCroucher::Precache` `0x1037b1a0` — 55 bytes, the shortest arm in the band: the
	// Troika body, the claws, and the two Malkavian-mansion stalker models with preload 0. Those two
	// models are what makes the male/female split in its `SetModel` sibling (`0x1037b1f0`)
	// reachable.
	TroikaPrecache();
	NpcKernelPrecache10Shared::Precache10Other(*this, GGhoulCroucherWeapon);
	for (const TCHAR* StalkerModel : GGhoulCroucherModels)
	{
		NpcKernelPrecache10Shared::Precache10Model(*this, StalkerModel, /*Preload=*/0);
	}
}

// Slot 105: `0x1037b1f0`, the vocalization-group body shared with the zombie; it calls the Troika
// body `0x10298ce0` directly.
void FElysiumNpcGhoulCroucher::SetModel(TCHAR* ModelName)
{
	ZombieLineSetModel(ModelName, TEXT("0x1037b1f0"));
}

// Slot 24: `0x1037be80`, the burn, then the Troika body `0x1029f8d0` directly.
// `0x1037be80`
void FElysiumNpcGhoulCroucher::OnVictimHitByMe(FElysiumEntity* Victim)
{
	// `CNPC_VGhoulCroucher::OnVictimHitByMe` `0x1037be80`, story 29d family SpeciesMisc10:
	//     player = param_1 ? param_1->+0xa8 : 0;                  // the PLAYER downcast cache
	//     if (m_bSpawnBurning (+0x6665) && player) BurnPlayer(player, 10.0);
	//     CAI_BaseNPCTroika::OnVictimHitByMe(this, param_1);      // ALWAYS, unlike the Gargoyle
	//                                                             // and SabbatLeader arms
	// `1037bf3c` pushes `0x41200000` = **10.0** as the burn damage.
	if (bGhoulSpawnBurning && Victim != nullptr && World != nullptr
		&& Victim->Handle == World->PlayerHandle())
	{
		BurnPlayer(Victim, 10.f);
	}
	FElysiumNpc::OnVictimHitByMe(Victim);   // `0x1029f8d0`, direct
}

// Slot 546: `0x1037a950`, the class's own schedule id space.
const TCHAR* FElysiumNpcGhoulCroucher::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1093b0e0`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VGhoulCroucher"), TEXT("0x1037a950"), TEXT("0x1093b0e0") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}

// Slot 615: `0x1037c420`, whose miss calls the Troika body `0x102ad0c0` directly.
/** `CNPC_VGhoulCroucher::CanBeSetOnFire` (`0x1037c420`) — the body of the class's override. */
bool FElysiumNpcGhoulCroucher::CanBeSetOnFire()
{
	// `0x1037c420`, the body of `FElysiumNpcGhoulCroucher::CanBeSetOnFire`: it refuses outright while
	// the authored `on_fire` keyfield (`m_bSpawnBurning`, +0x6665) is set — a croucher that spawned
	// burning cannot be set on fire again. Anything else is a direct call into `thunk_FUN_102ad0c0`.
	if (bGhoulSpawnBurning)
	{
		return false;
	}
	return FElysiumNpc::CanBeSetOnFire();
}

// Slot 174: `0x1037bf60`.
/** `CNPC_VGhoulCroucher::StartTouch` (`0x1037bf60`), `CNPC_VGhoulCroucher#174`. Retail, in order:
 *
 *    1. `CBaseEntity::StartTouch(other)` — the base body FIRST.
 *    2. `if (other && other->m_pPlayer (+0xa8)) OnDisturbed(other);`
 *       `else if (other->field_0x94) OnDisturbed(other);`
 *       i.e. **the toucher is the player, or the toucher is an NPC**. `+0x94` is `CBaseEntity`'s
 *       cached `CAI_BaseNPC*` (families Conditions and Damage both already read it that way).
 *       **Retail's bug, and this port's one crash guard:** the `else` branch is entered with
 *       `other == NULL` too (`1037bfd9 XOR EBX,EBX / JMP 0x1037bfe7`, and `1037bfe7 MOV EAX,[EDI +
 *       0x94]` dereferences the null `EDI`). A null toucher faults in retail. This port refuses the
 *       arm instead — no `FElysiumEntityWorld` path can produce a null toucher, so the divergence is
 *       unreachable, and it is named rather than reproduced.
 *    3. `if (m_bSpawnBurning (+0x6665) && the toucher IS the player && m_flNextTouchBurnTime
 *       (+0x666c) < curtime)`: restamp `m_flNextTouchBurnTime = curtime + _DAT_1044ffd0` (**5.0 s**)
 *       and `BurnPlayer(player, 5.0f)`.
 *       The compare is `FLD curtime / FCOMP [+0x666c] / AND EAX,0x4100 / JNZ skip`, so the arm runs
 *       only when curtime is STRICTLY greater. Note the player term reuses the `+0xa8` pointer
 *       resolved in step 2 — a toucher that entered step 2 through the `+0x94` (NPC) arm carries a
 *       null one and cannot burn.
 *
 *  The 5.0 here is against the **10.0** of this class's own `OnVictimHitByMe` (`0x1037be80`
 *  `PUSH 0x41200000`): standing in the ghoul's fire hurts half as much as being hit by it. */
void FElysiumNpcGhoulCroucher::StartTouchSpecies(FElysiumEntity* Other)
{
	// `CNPC_VGhoulCroucher::StartTouch` `0x1037bf60`.

	// 1. `1037bfd0 CALL 0x10009caf` — `CBaseEntity::StartTouch(other)`, FIRST.
	BaseEntityStartTouch(Other);

	// 2. `1037bfd5`..`1037bff4`. The player pointer resolved here is reused by step 3, which is why
	//    it is a local and not re-derived.
	//
	//    DIVERGENCE, named: retail reads `[EDI + 0x94]` with `EDI == 0` when the toucher is null
	//    (`1037bfd9 XOR EBX,EBX / JMP 0x1037bfe7`) and faults. This port refuses the arm. No
	//    `FElysiumEntityWorld` touch path can hand a null toucher in, so the guard is unreachable.
	const bool bTouchIsPlayer = NpcKernelSpeciesLifecycle10Shared::SpeciesLifecycle10IsPlayer(*this, Other);
	if (Other != nullptr && (bTouchIsPlayer || SpeciesLifecycle10IsNpc(Other)))
	{
		OnDisturbed(Other);
	}

	// 3. `1037bff9`..`1037c036`. `m_bSpawnBurning` (+0x6665), the toucher being the PLAYER (the
	//    `+0xa8` pointer from step 2 — an NPC toucher carries a null one and cannot burn), and
	//    `m_flNextTouchBurnTime` (+0x666c) STRICTLY behind curtime.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (bGhoulSpawnBurning && bTouchIsPlayer && GhoulNextTouchBurnTime < Now)
	{
		// `1037c022 FADD double ptr [0x1044ffd0]` — 5.0 s, and the restamp happens BEFORE the burn
		// (`1037c030 FSTP` precedes `1037c036 CALL`). `BurnPlayer` is family SpeciesMisc10's body
		// for `0x1037c090`; this row supplies the 5.0 against `OnVictimHitByMe`'s 10.0.
		GhoulNextTouchBurnTime = Now + GhoulTouchBurnIntervalSeconds;
		BurnPlayer(Other, GhoulTouchBurnDamage);
	}
}

// --- Moved from `ElysiumNpcConditionsBodies.cpp` (story 5 step 4) ---

bool FElysiumNpcGhoulCroucher::IsDisturbed() const
{
	// `CNPC_VGhoulCroucher::IsDisturbed` (`0x1037bb20`). The 121 bytes are an AI scope-trace push and
	// pop around one read; the body IS `return m_bWasDisturbed`.
	return bWasDisturbed;
}

void FElysiumNpcGhoulCroucher::OnDisturbed(FElysiumEntity* Disturber)
{
	// `CNPC_VGhoulCroucher::OnDisturbed` (`0x1037b6e0`). The whole body is under one latch: a second
	// disturbance writes nothing and fires nothing.
	if (bWasDisturbed)
	{
		return;
	}
	bWasDisturbed = true;     // +0x6666
	bUnawareExited = false;   // +0x6667

	// The split is on the disturber's cached `CBasePlayer*` (`+0xa8`) — "was it a player", the same
	// read the see-unknown sweep makes. This runtime asks the world for its player and compares, the
	// way `ShouldInvestigate` does.
	const FElysiumPlayer* Player = World != nullptr ? World->FindPlayer() : nullptr;
	const bool bDisturberIsPlayer = Disturber != nullptr && Player != nullptr
		&& static_cast<const FElysiumEntity*>(Player) == Disturber;
	FName Output;
	if (!bDisturberIsPlayer)
	{
		// `0x10293a80 SetClosestPlayer` — the generic handler, which rewrites `m_hClosestPlayer`
		// (+0x628c) from the nearest live player within 20000 units, or clears it when there is none.
		Senses.SetClosestPlayer(*this, World != nullptr ? World->NowSeconds() : 0.0);
		Output = FName(TEXT("OnDisturbed"));          // +0x6674
	}
	else
	{
		// The player arm does NOT run the sweep: it writes the DISTURBER's handle straight into
		// `m_hClosestPlayer` and fires the other output.
		Senses.Memory.ClosestPlayer = Player->Handle;
		Output = FName(TEXT("OnDisturbedByPlayer"));  // +0x668c
	}
	FireOutput(Output, Disturber != nullptr ? Disturber->Handle : FElysiumEntityHandle::Invalid());

	// Then, and only when `m_hClosestPlayer` now resolves to a live entity,
	// `AddEntityRelationship(player, D_HT, 10)`. Note the handle is re-read: on the non-player arm
	// this is whatever the sweep just found, which may be a player the disturbance had nothing to do
	// with. Reproduced.
	if (World == nullptr || !Senses.Memory.ClosestPlayer.IsSet())
	{
		return;
	}
	const FElysiumEntity* Closest = World->Resolve(Senses.Memory.ClosestPlayer);
	if (Closest == nullptr)
	{
		return;
	}
	Relationships.SetEntity(Senses.Memory.ClosestPlayer, EElysiumRelationship::Hate,
		GCondDisturbedHatePriority);
}

// --- Moved from `ElysiumNpcDamage.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcLifecycle.cpp` (story 5 step 4) ---

void FElysiumNpcGhoulCroucher::GhoulCroucherScriptUnhideTail()
{
	// 0x1037c2f0 — the base, then `m_hBurningParticle` (`+0x6670`) resolved through the handle table
	// and its vtable `+0x138` (slot 78) dispatched when it resolves to a live entity. Retail does
	// NOT clear the handle, so neither does this.
	if (World == nullptr || !BurningParticle.IsSet())
	{
		return;
	}
	if (FElysiumEntity* Particle = World->Resolve(BurningParticle))
	{
		Particle->ScriptUnhide();
	}
}

int32 FElysiumNpcGhoulCroucher::UnawareTableEntry(const TCHAR* RetailTable, int32 Index)
{
	// SEAM for `DAT_1063abcc` and `DAT_1063abdc`. **Unrecovered:** neither table's contents nor its
	// purpose (message, sound or activity selection) is settled anywhere in the corpus. The indexing
	// is the recovered body and is above; this is the row it would read.
	(void)RetailTable;
	(void)Index;
	return 0;
}

int32 FElysiumNpcGhoulCroucher::UnawareTableA() const
{
	// `FUN_1037b870` — `*(undefined4 *)(&DAT_1063abcc + *(int *)(this + 0x6668) * 4)`.
	return UnawareTableEntry(TEXT("DAT_1063abcc"), UnawareType);
}

int32 FElysiumNpcGhoulCroucher::UnawareTableB() const
{
	// `FUN_1037b890` — the same shape over `DAT_1063abdc`, same index.
	return UnawareTableEntry(TEXT("DAT_1063abdc"), UnawareType);
}

// --- Moved from `ElysiumNpcLifecycle19_2.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcMisc.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcPrecache10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcSpeciesLifecycle10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcSpeciesMisc10.cpp` (story 5 step 4) ---

void FElysiumNpcGhoulCroucher::BurnPlayer(FElysiumEntity* BurnTarget, float Damage)
{
	// `1037c0f8`: a null target does nothing at all — not even the damage.
	if (BurnTarget == nullptr)
	{
		return;
	}
	// `1037c105`: `GetModelPtr()` then `*(modelPtr + *(modelPtr + 0x104) + 4)` — the hitbox COUNT of
	// the model's hitbox set — and `BurnHitbox(target, i, 5.0, 0.5)` for each.
	//
	// SEAM: family Damage's `HitboxSetCount` answers 0 here, so the loop makes no passes. That IS
	// retail's arm for a model with no hitboxes, and the record below is what says the body ran.
	const int32 HitboxCount = HitboxSetCount();
	for (int32 Index = 0; Index < HitboxCount; ++Index)
	{
		BurnHitboxCalls.Add(FBurnHitboxCall{ BurnTarget->Handle, Index, GBurnHitboxSeconds,
			GBurnHitboxInterval });
	}
	// `1037c13d`: `CTakeDamageInfo(inflictor = GetActiveWeapon(), attacker = this, damage, bits = 8)`
	// built by `thunk_FUN_101c26d0` with the trailing `0, 0, -1`, then `TakeDamage(target, info)`.
	// The damage is the CALLER's — `0x1037bf3c` pushes `0x41200000` (10.0) — and `8` is the
	// `bitsDamageType`, not the amount, which the checklist's walk left unsaid.
	FElysiumDmg Dmg;
	Dmg.DmgMask = static_cast<uint32>(GBurnDamageBits);
	Dmg.BaseDamage = static_cast<int32>(Damage);
	Dmg.Source = Handle;
	Dmg.Inflictor = Inventory.ActiveWeapon;
	if (FElysiumCombatCharacter* Victim = BurnTarget->AsCombatCharacter())
	{
		Victim->TakeDamage(Dmg, this);
	}
}
