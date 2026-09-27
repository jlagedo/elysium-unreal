#include "Substrate/ElysiumNpcGargoyle.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumStub.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumDisciplines.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcConditions10Shared.h"
#include "Substrate/ElysiumNpcLifecycle19_2Shared.h"
#include "Substrate/ElysiumNpcMotorShared.h"
#include "Substrate/ElysiumNpcPrecache10Shared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcSpeciesLifecycle10Shared.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumRetailHullTable.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// --- File-scope helpers moved with this class's bodies (story 5 step 4) ---

namespace
{
	//
	// Nine gib models, precached in DESCENDING `.rdata` address order (`0x1063a808` down to
	// `0x1063a550`) — which is the order the body pushes them, not an artifact.
	const TCHAR* const GGargoyleGibModels[] = {
		TEXT("models/character/monster/gargoyle/gargoyle_gibbs/garg_gibbs.mdl"),
		TEXT("models/character/monster/gargoyle/gargoyle_gibbs/gargoyle_head.mdl"),
		TEXT("models/character/monster/gargoyle/gargoyle_gibbs/gargoyle_L_foot.mdl"),
		TEXT("models/character/monster/gargoyle/gargoyle_gibbs/gargoyle_L_hand.mdl"),
		TEXT("models/character/monster/gargoyle/gargoyle_gibbs/gargoyle_L_torso.mdl"),
		TEXT("models/character/monster/gargoyle/gargoyle_gibbs/gargoyle_pelvis.mdl"),
		TEXT("models/character/monster/gargoyle/gargoyle_gibbs/gargoyle_R_foot.mdl"),
		TEXT("models/character/monster/gargoyle/gargoyle_gibbs/gargoyle_R_hand.mdl"),
		TEXT("models/character/monster/gargoyle/gargoyle_gibbs/gargoyle_R_torso.mdl"),
	};
	const TCHAR* const GGargoyleStomps[] = {   // 0x10639480, to 0x10 — four
		TEXT("character/monster/gargoyle/stomp_1.wav"),
		TEXT("character/monster/gargoyle/stomp_2.wav"),
		TEXT("character/monster/gargoyle/stomp_3.wav"),
		TEXT("character/monster/gargoyle/stomp_4.wav"),
	};
	const TCHAR* const GGargoyleExerts[] = {   // 0x10639490, to 0xc — three
		TEXT("character/monster/gargoyle/exert_heavy_1.wav"),
		TEXT("character/monster/gargoyle/exert_heavy_2.wav"),
		TEXT("character/monster/gargoyle/exert_heavy_3.wav"),
	};
	const TCHAR* const GGargoyleRoar = TEXT("character/monster/gargoyle/roar2.wav");
	const TCHAR* const GGargoyleWeapon = TEXT("item_w_gargoyle_fist");
	// `1037a3c1 CALL dword ptr [EDX + 0x238]` — slot 142, `OnTakeDamage`, dispatched on the PILLAR.
	constexpr int32 GOnTakeDamageSlot = 142;
}

// `CNPC_VGargoyle`'s constructor `0x10377a60` writes both hull words at `0x10377a93`, after the
// `CAI_BaseNPC` constructor `0x1027c300` zeroed both; the port's constructor chain runs in the same
// order.
FElysiumNpcGargoyle::FElysiumNpcGargoyle()
{
	HullKind = 14;
	PathingHullKind = 14;
}

// Slots 599 / 600: `0x10379ef0` / `0x10379f20`, byte-identical to FrenzyShadow's unconditional
// melee entry.
bool FElysiumNpcGargoyle::Slot599(int32 Arg)
{
	(void)Arg;
	return FUN_10379ef0(static_cast<const FElysiumNpc*>(this)->GetEnemy());
}

bool FElysiumNpcGargoyle::Slot600(FElysiumEntity* Enemy)
{
	// `0x10379f20`, `CNPC_VGargoyle`'s slot 600 — byte-identical to `CNPC_VFrenzyShadow`'s
	// `0x10376ba0` (`FElysiumNpcFrenzyShadow::Slot600`, a sibling class, so the body is restated):
	//     m_bInMelee = 1; (*DAT_10924edc)->vfunc1(); return true;
	// Every gate is gone, the re-entry guard included.
	(void)Enemy;
	bInMelee = true;
	++MeleeEventFires;
	return true;
}

// Slot 420: `0x103785f0`.
// `0x103785f0`
void FElysiumNpcGargoyle::NPCInit()
{
	TroikaNPCInit();
	GargoylePillarTarget = FElysiumEntityHandle::Invalid();
	GargoyleShunnedFindPillar = 0;
	GargoyleDoingGibDeath = 0;
	GargoyleCanKnockback = 0;
	NodeGraphHullIndex() = HullIndexGargoyle;
}

// Slot 104: `0x10378470`.
// 0x10378470
void FElysiumNpcGargoyle::Precache()
{
	// `CNPC_VGargoyle::Precache` `0x10378470` — the Troika body, nine gib models with preload 1 in
	// descending `.rdata` order, the 0x10-byte stomp table, the 0xc-byte exert table, one roar and
	// the fist.
	TroikaPrecache();
	for (const TCHAR* GibModel : GGargoyleGibModels)
	{
		NpcKernelPrecache10Shared::Precache10Model(*this, GibModel, /*Preload=*/1);
	}
	NpcKernelPrecache10Shared::Precache10SoundTable(*this, GGargoyleStomps, UE_ARRAY_COUNT(GGargoyleStomps));
	NpcKernelPrecache10Shared::Precache10SoundTable(*this, GGargoyleExerts, UE_ARRAY_COUNT(GGargoyleExerts));
	NpcKernelPrecache10Shared::Precache10Sound(*this, GGargoyleRoar);
	NpcKernelPrecache10Shared::Precache10Other(*this, GGargoyleWeapon);
}

// Slot 461: `0x10378b60`, the selector tag 0x11 and then a direct call into the human line's `0x103851e0`.
int32 FElysiumNpcGargoyle::SelectIdealStateRetail()
{
	SelectIdealStateSelector = 0x11;
	return HumanSelectIdealState();
}

// Slot 448: `0x10379060`, its own arm and then a direct call into the Troika body `0x1029adb0`.
/** `CNPC_VGargoyle::TaskFail` (`0x10379060`) — in combat with `TASK_FAILED` standing as an
 *  interrupt it clears the top bit of `m_afMemory`; then, when `FINDING_BODY` stands, it clears
 *  `FINDING_BODY`; then `m_iShunnedFindPillar` (`+0x6680`) = 0. */
void FElysiumNpcGargoyle::TaskFail(int32 Reason)
{
	// `CNPC_VGargoyle::TaskFail` (`0x10379060`), 77 bytes.
	(void)Reason;

	// `10379063`: in COMBAT with `TASK_FAILED` (0x5c) standing as an INTERRUPT condition
	// (`0x10269d30`, not the plain `HasCondition`), clear the top bit of `m_afMemory`.
	if (NpcKernelConditions10Shared::Cond10RetailNpcState(Mind.State()) == NpcKernelConditions10Shared::GCond10NpcStateCombat
		&& ElysiumSchedule::HasInterruptCondition(Schedule, *this, Cognition.Conditions,
			EElysiumNpcCond::TaskFailed))
	{
		BaseScheduleHost.MemoryBits &= ~NpcKernelConditions10Shared::GCond10MemoryTopBit;                 // 10379077
	}

	// **CORRECTION.** The checklist's walk records `1037908c CALL 0x10006613` as reached with a
	// `this` that "has no visible prior assignment in the decompile (likely a lost this alias rather
	// than a confirmed retail bug — needs an asm check before this arm is ported)". The asm check:
	// `0x10379040` is FOUR instructions (`MOV EAX,[ECX+0x14b8] / SHR EAX,4 / AND AL,1 / RET`) and
	// never writes `ECX`, so `ECX` still holds `this` from `10379081`. There is no bug. The two
	// bodies are plain `m_bfAINPCFlags` accessors: `0x10379040` reads bit `0x10` and `0x10379000`
	// writes it — `FINDING_BODY`.
	if (NpcFlags.Has(EElysiumNpcFlag::FINDING_BODY))                     // 10379083, 0x10379040
	{
		NpcFlags.Clear(EElysiumNpcFlag::FINDING_BODY);                   // 1037908e, 0x10379000(this, 0)
	}

	GargoyleShunnedFindPillar = 0;                                         // 1037909a, m_iShunnedFindPillar
	FElysiumNpc::TaskFail(Reason);
}

// Slot 440: `0x10378a30`.
// `0x10378a30`
// `0x10378a30`, `CNPC_VGargoyle::TranslateSchedule`, the body of `FElysiumNpcGargoyle::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpcGargoyle::TranslateScheduleRetail(int32 ScheduleNumber)
{
	if (ScheduleNumber == 0x5b) { return 0x15d; }
	if (ScheduleNumber == 0xdc) { return 0x15f; }
	if (ScheduleNumber == 0xdd) { return 0x160; }
	if (ScheduleNumber == 0xea) { return 0x15e; }
	return TroikaTranslateScheduleRetail(ScheduleNumber);
}

// Slot 69: `0x10379490`. The `0x16` derived-type gate, then the three `FClassnameIs` compares
// (`prop_dynamic`, `func_brush`, `func_door_rotating`); otherwise a direct call into the Troika body
// `0x1029b180`.
bool FElysiumNpcGargoyle::NavIgnoreCollision(FElysiumEntity* Other)
{
	if (Other != nullptr)
	{
		if ((RetailDerivedType(*Other) & 0x16) != 0)
		{
			return true;
		}
		if (GargoyleIgnoresClassname(Other->Def != nullptr ? Other->Def->Classname : FString()))
		{
			return true;
		}
	}
	return FElysiumNpc::NavIgnoreCollision(Other);
}

// Slot 337: `0x10378680`.
int32 FElysiumNpcGargoyle::GetUsedHullBits()
{
	// A bare `return 0x4000`: no call up the chain, so the Troika line's bit 0 is absent.
	return 0x4000;
}

// Slot 24: `0x1037a450`, a replacement that never calls the Troika body.
//
// The Troika line (`0x1029f8d0`) is `OnVictimHitByMe` itself. `CNPC_VGargoyle`, `CNPC_VSabbatLeader`,
// `CNPC_VZombie` and `CNPC_VGhoulCroucher` override it on their C++ classes (story 5 step 3) with these
// bodies; only the Zombie and the GhoulCroucher call the Troika body (directly).
// `0x1037a450`
void FElysiumNpcGargoyle::OnVictimHitByMe(FElysiumEntity* Victim)
{
	// `CNPC_VGargoyle::OnVictimHitByMe` `0x1037a450`. The control flow inverts twice, so read the
	// listing's jump targets rather than the nesting: the two early `JZ 0x1037a547` on a
	// pointer-identity hit and the two `SETZ` tails all land on the DISPATCH, and the only path that
	// reaches `0x1037a552` (the return) is "neither name matched". So:
	//
	//     if (classname is "pillar" or "central_pillar") victim->vtable[+0x428](this);
	//     // and NOTHING otherwise — not even the base body.
	//
	// **29c's walk has this inverted** ("skips the base hit reaction when the victim's classname is
	// pillar ... otherwise calls it"). Corrected here and in the walked paragraph.
	//
	// Note what this override does NOT do: it never calls the Troika line, so a Gargoyle's melee move
	// records are never cleared. That is retail's, not an omission here.
	const FString Classname = Victim != nullptr && Victim->Def != nullptr
		? Victim->Def->Classname : FString();
	if (GargoyleHitsPillar(Classname))
	{
		DispatchVictimHitReaction(Victim);
	}
}

// Slot 546: `0x10377cd0`, the class's own schedule id space.
const TCHAR* FElysiumNpcGargoyle::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1093b038`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VGargoyle"), TEXT("0x10377cd0"), TEXT("0x1093b038") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}

// Slot 292: `0x10378cb0` — no flinch from gunfire or a zero-magnitude hit; otherwise the base
// `CBaseCombatCharacter::DamageFlinch`. The port's flinch runs through `StartDamageFlinch`, whose
// species hook this is.
bool FElysiumNpcGargoyle::SuppressesDamageFlinch(const FElysiumDmg& Dmg) const
{
	// `0x10378cb0`, byte-identical with its sibling's: the port's commit path hands the flinch a
	// resolved descriptor rather than a packet, so the combined bits are the descriptor's own and the
	// magnitude is the committed damage — which is what `CVDmg_t::GetDmg` answers once `Apply` has
	// run. `0x4000002` is `DMG_BULLET | DMG_BUCKSHOT`, exactly `ElysiumDamage::FirearmMask`.
	return DamageFlinchSuppressed(Dmg.DmgMask, static_cast<float>(Dmg.GetDmg()),
		ElysiumDamage::FirearmMask);
}

// Slot 435: `0x10378fc0`, the Troika body `0x102a0940` directly, then the class's own tail.
/** Slot 435's species bodies, each its class's `OnScheduleChange` override's body (story 5 step 3). */
// `0x10378fc0`
void FElysiumNpcGargoyle::OnScheduleChange(int32 NewSchedule)
{
	// `CNPC_VGargoyle::OnScheduleChange` `0x10378fc0`: the Troika body `0x102a0940` directly, first.
	FElysiumNpc::OnScheduleChange(NewSchedule);
	if (!NpcFlags.Has(EElysiumNpcFlag::PRESERVE_PATH)
		&& GargoyleShunnedFindPillar > 0) // 0x10378fd5, 0x10378fe0
	{
		--GargoyleShunnedFindPillar; // 0x10378fe4
	}
}

// Slot 175: `0x1037a270`.
/** `CNPC_VGargoyle::Touch` (`0x1037a270`), `CNPC_VGargoyle#175` — the gargoyle's pillar damage.
 *
 *  Retail, in order:
 *    1. `FClassnameIs(other, "pillar")`, else `FClassnameIs(other, "central_pillar")`. Both are the
 *       case-insensitive whole-name compare (`__strcmpi`), because neither literal carries the
 *       trailing `*` the compare would honour as a prefix. This is the SAME predicate this class's
 *       slot-24 body uses, and family Misc's `GargoyleHitsPillar` already carries it — it is called
 *       here, not restated.
 *    2. On a match: a `CVDmg_t` with `SetSrc(this)`, `Set(1, 0x80, 10)` — family **Lethal**,
 *       `m_bdmgTypes` **`DMG_CLUB`**, `m_iDiceAmt` **10** — and `m_iToHitSuccesses` (`+0x0c`) forced
 *       to **1**; wrapped by `0x101c26d0` into a `CTakeDamageInfo` with **inflictor = the pillar**,
 *       **attacker = the gargoyle**, damage 1.0 and ammo type -1; then slot **142** `OnTakeDamage`
 *       dispatched **ON THE PILLAR** (`1037a3c1 CALL dword ptr [EDX + 0x238]`, `ECX = ESI`).
 *    3. BOTH paths end with `CBaseEntity::Touch(other)`.
 *
 *  The inflictor being the pillar itself rather than the gargoyle is retail's, and is reproduced. */
void FElysiumNpcGargoyle::TouchSpecies(FElysiumEntity* Other)
{
	// `CNPC_VGargoyle::Touch` `0x1037a270`.
	const FString Classname = Other != nullptr && Other->Def != nullptr
		? Other->Def->Classname : FString();

	// 1. The two `FClassnameIs` compares, in retail's order. Family Misc already carries them for
	//    this class's slot-24 body; called, not restated.
	if (GargoyleHitsPillar(Classname))
	{
		// 2. `CVDmg_t`: `SetSrc(this)`, `Set(1, 0x80, 10)`, `m_iToHitSuccesses = 1`.
		FGargoylePillarHit Hit;
		Hit.Pillar = Other->Handle;                              // retail's inflictor, and the victim
		Hit.Family = GargoylePillarDamageFamily;                 // 1 = EElysiumDmgFamily::Lethal
		Hit.DiceAmount = GargoylePillarDiceAmount;               // m_iDiceAmt = 10
		Hit.ToHitSuccesses = GargoylePillarToHitSuccesses;       // m_iToHitSuccesses = 1
		Hit.DamageTypes = GargoylePillarDamageTypes;             // m_bdmgTypes = DMG_CLUB
		Hit.Damage = GargoylePillarDamageScale;                  // the packet's scalar, 1.0

		FElysiumDmg Dmg;
		Dmg.Family = static_cast<EElysiumDmgFamily>(GargoylePillarDamageFamily);
		Dmg.BaseDamage = GargoylePillarDiceAmount;               // word 1, `m_iDiceAmt`
		Dmg.ExtraInput = GargoylePillarToHitSuccesses;           // word 3, `m_iToHitSuccesses`
		Dmg.DmgMask = GargoylePillarDamageTypes;                 // word 4, `m_bdmgTypes`
		Dmg.Source = Handle;                                     // `SetSrc(this)`, word 13

		FElysiumTakeDamageInfo Info;
		Info.Dmg = &Dmg;
		// `1037a3a5 PUSH EBP` — the ATTACKER is the gargoyle. `1037a3a7 PUSH ESI`, pushed first and
		// therefore argument 0, makes the PILLAR the inflictor; this runtime's packet carries no
		// inflictor word, so it is recorded on the hit above instead of being dropped.
		Info.Attacker = Handle;
		Info.Damage = GargoylePillarDamageScale;
		Info.DamageBits = 0;                                     // `1037a39c PUSH 0x0`
		Info.AmmoType = INDEX_NONE;                              // `1037a399 PUSH -0x1`

		// 3. Slot 142 `OnTakeDamage` dispatched ON THE PILLAR.
		if (FElysiumNpc* PillarNpc = Other->AsNpc())
		{
			(void)GOnTakeDamageSlot;
			PillarNpc->OnTakeDamage(&Info);
			Hit.bDispatched = true;
		}
		GargoylePillarHits.Add(MoveTemp(Hit));
	}

	// 4. `1037a3d0` — BOTH paths end in `CBaseEntity::Touch(other)`.
	BaseEntityTouch(Other);
}

// --- Moved from `ElysiumNpcConditions10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcLifecycle19_2.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcMaintain19.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcMisc.cpp` (story 5 step 4) ---

bool FElysiumNpcGargoyle::GargoyleHitsPillar(const FString& Classname)
{
	// The two `FClassnameIs` compares of `0x1037a450`, which are `__strcmpi` (case-insensitive) and
	// accept a trailing `*` as a prefix match — neither `"pillar"` nor `"central_pillar"` carries
	// one, so both are whole-name compares.
	return Classname.Equals(TEXT("pillar"), ESearchCase::IgnoreCase)
		|| Classname.Equals(TEXT("central_pillar"), ESearchCase::IgnoreCase);
}

// --- Moved from `ElysiumNpcMotor.cpp` (story 5 step 4) ---

bool FElysiumNpcGargoyle::GargoyleIgnoresClassname(const FString& Classname)
{
	// `CNPC_VGargoyle::NavIgnoreCollision` `0x10379490`'s three `FClassnameIs` compares, which are
	// case-insensitive (`__strcmpi`) and accept a trailing `*` as a prefix match — none of these
	// three carries one, so all three are whole-name compares.
	return Classname.Equals(TEXT("prop_dynamic"), ESearchCase::IgnoreCase)
		|| Classname.Equals(TEXT("func_brush"), ESearchCase::IgnoreCase)
		|| Classname.Equals(TEXT("func_door_rotating"), ESearchCase::IgnoreCase);
}

// --- Moved from `ElysiumNpcPrecache10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcSpecies.cpp` (story 5 step 4) ---

bool FElysiumNpcGargoyle::FUN_10379ef0(FElysiumEntity* Enemy)
{
	// `0x10379ef0`, `CNPC_VGargoyle`'s slot 599 — byte-identical to `CNPC_VFrenzyShadow`'s
	// `0x10376b70`, verified against the decompiled C of both. Since story 5 fold A2 that body is
	// `FElysiumNpcFrenzyShadow::Slot599`, on a sibling class, so it is restated here:
	//     (*DAT_10924edc)->vfunc1();      // the global melee-entered event, FIRST
	//     m_bInMelee = 1;
	//     return <EAX>;                   // true: m_bInMelee was set
	// Every gate the Troika line has is dropped; `Enemy` is read by nothing.
	(void)Enemy;
	++MeleeEventFires;
	bInMelee = true;
	return true;
}

// --- Moved from `ElysiumNpcSpeciesLifecycle10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcTranslate19.cpp` (story 5 step 4) ---

