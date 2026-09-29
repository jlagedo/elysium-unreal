#include "Substrate/ElysiumNpcTzimisceHeadClaw.h"

#include "ElysiumAnimEvent.h"
#include "ElysiumAnimationIntent.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumStub.h"
#include "ElysiumWorldServices.h"
#include "Player/ElysiumCameraShots.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcDialogue.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcAnim10Shared.h"
#include "Substrate/ElysiumNpcLifecycle2_2Shared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcSpecies2Shared.h"
#include "Substrate/ElysiumNpcSpeciesMisc10_2Shared.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumReactions.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"
#include "Visual/ElysiumActionTables.h"

// --- File-scope helpers moved with this class's bodies (story 5 step 4) ---

namespace
{
	// The melee timers the species slot-599 bodies arm. `CNPC_VTzimisceHeadClaw` uses the SAME pair
	// as the Troika line (`0x40f00000` / `0x41700000`); `CNPC_VTzimisce`'s carry latch uses a
	// narrower one (`0x40f00000` / `0x41200000`) and `CNPC_VYukie`'s flee window a much wider one.
	constexpr float SpeciesMeleeMustLeaveMin = 7.5f;        // 0x40f00000
	constexpr float SpeciesMeleeMustLeaveMax = 15.0f;       // 0x41700000
	// `_DAT_104cd108` is a **DOUBLE** reading **240.0** (`103c20dc` is `FCOMP double ptr`). The 2-D
	// distance at or past which slot 332 raises condition `0x35`.
	constexpr double GHeadClawConditionDistanceUnits = ElysiumNpcTunables::HeadClawConditionDistance;
	// `_DAT_1044fab0`, the DOUBLE 0.0 sentinel, and `0x43fa0000` = 500.0. The ManBat half of this
	// family reads the same two cells and names them `GSlowExpireSentinel` /
	// `GSlowEntityMagnitude`; these carry the head claw's own spelling because an anonymous
	// namespace does NOT make a name file-local under a unity build — the two halves are
	// concatenated into one translation unit and two definitions of one name is a hard error. (It
	// only surfaced when story 29e's census grew and moved the unity chunk boundary, which is the
	// whole hazard: a latent collision compiles until an unrelated file changes size.)
	constexpr double GHeadClawSlowExpireSentinel = 0.0;
	constexpr float GHeadClawSlowEntityMagnitude = 500.f;
	// `103c1dd5`: `PUSH 8.0` then `PUSH 5.0` — `RandomFloat(5.0, 8.0)`.
	constexpr float GHeadClawSlowSecondsMin = 5.f;
	constexpr float GHeadClawSlowSecondsMax = 8.f;
	bool SpeciesMisc10_2IsPlayer(const FElysiumNpc& Npc, const FElysiumEntity* Candidate)
	{
		return Candidate != nullptr && Npc.World != nullptr
			&& Candidate->Handle == Npc.World->PlayerHandle();
	}
}

// `CNPC_VTzimisceHeadClaw`'s constructor `0x103c1220` writes both hull words at `0x103c127d`, after
// the `CAI_BaseNPC` constructor `0x1027c300` zeroed both; the port's constructor chain runs in the
// same order.
FElysiumNpcTzimisceHeadClaw::FElysiumNpcTzimisceHeadClaw()
{
	HullKind = 11;
	PathingHullKind = 11;
}

// The melee quartet, slots 599-602: `0x103c19e0`, `0x103c1a60`, `0x103c1ad0`, `0x103c1b10`. Every recovered dispatch
// site of 599 pushes `GetEnemy()` (family TroikaHelpers' `Slot599`), which the body is handed.
bool FElysiumNpcTzimisceHeadClaw::Slot599(int32 Arg)
{
	(void)Arg;
	return FUN_103c19e0(static_cast<const FElysiumNpc*>(this)->GetEnemy());
}

bool FElysiumNpcTzimisceHeadClaw::Slot600(FElysiumEntity* Enemy)
{
	// `0x103c1a60`, `CNPC_VTzimisceHeadClaw`'s slot 600:
	//
	//     (*DAT_10924edc)->vfunc1();                                 // FIRST, unconditionally
	//     if (m_bInMelee == 0) {
	//         if (RequestMeleeSlotNoForce(m_pAttackCoordinator, this, false)) {   // 0x1025dca0
	//             m_bInMelee = 1;
	//             return true;
	//         }
	//     }
	//     m_bInMelee = 0;
	//     return false;
	//
	// Three differences from the Troika line (`0x102b57c0`):
	//   * **the event fires before anything is decided**, not inside the accepting arm — so a head
	//     claw that is already in melee still fires it;
	//   * the weapon-capability gate (`weapon->slot360() & 0x18000`) is gone entirely;
	//   * `m_flMeleeMustLeaveTimer` is NOT re-armed on the accepting arm. 29c's walk calls that "a
	//     real divergence from the base body" and it is: the head claw enters melee with no leave
	//     deadline.
	//
	// The falling-out `m_bInMelee = 0` runs for an NPC that was ALREADY in melee too, because the
	// guard is on the way in and not around the write — so calling this on a body already in melee
	// takes it back out. That is retail's, and it is exactly the shape the Troika line avoids by
	// keeping its clear inside the capability arm.
	(void)Enemy;
	++MeleeEventFires;
	if (!bInMelee)
	{
		if (MeleeCoordinatorAdmits600())
		{
			bInMelee = true;
			return true;
		}
	}
	bInMelee = false;
	return false;
}

/** `0x103c1ad0` / `0x103c1b10` — `CNPC_VTzimisceHeadClaw`'s slots 601 and 602. */
void FElysiumNpcTzimisceHeadClaw::Slot601(FElysiumEntity* Enemy)
{
	// `0x103c1ad0`, `CNPC_VTzimisceHeadClaw`'s slot 601:
	//     (*DAT_10924edc)->vfunc1();
	//     m_bInMelee = 0;
	//     ReleaseMeleeSlot(m_pAttackCoordinator, this);      // 0x1025ddd0, UNGUARDED
	//
	// It drops the Troika line's `HasUsableRangedWeapon()` test and the `m_flMeleeCanEnterTimer`
	// re-arm that hangs off it, so a head claw that leaves melee may re-enter on the very next
	// pass. It also drops the `m_pAttackCoordinator != 0` guard the Troika line puts in front of the
	// release — the same guard family TroikaHelpers found the `CNPC_VAndreiBlood` line dropping,
	// and for the same reason: the release faults on a null coordinator, so a body that reaches it
	// already has one.
	(void)Enemy;
	++MeleeEventFires;
	bInMelee = false;
	++MeleeCoordinatorReleases;   // `thunk_FUN_1025ddd0(m_pAttackCoordinator, this)`
}

bool FElysiumNpcTzimisceHeadClaw::Slot602()
{
	return FUN_103c1b10();
}

// Slot 420: `0x103c1c80`.
// `0x103c1c80`
void FElysiumNpcTzimisceHeadClaw::NPCInit()
{
	TroikaNPCInit();
	HeadClawSlowedExpire = 0.0;
}

// Slot 104 `CNPC_VTzimisceHeadClaw::Precache`, slot 126 `Save` and slot 127
// `Restore` have no body here (0019 story 6, mechanism). The first only precached
// the four Tzim2 emitters, the TC_FatGuy tables and the two item classnames, which the bake
// resolves; the other two only swapped `m_flSlowedExpire` (+0x6678) through the FIELD_TIME mode-3
// codec around the Troika body, which the generated SAVE walk carries.

// Slot 310: `0x103c1cd0`, which calls the Troika body `0x10295750` directly.
/** `CNPC_VTzimisceHeadClaw::SetActivity` (`0x103c1cd0`). ONE arm in front of the Troika body:
 *  request 9 `ACT_WALK` with a non-null slot 167 `GetEnemy` becomes `0x1136 ACT_TZ_WALK2`. */
void FElysiumNpcTzimisceHeadClaw::SetActivity(int32 Activity)
{
	// `CNPC_VTzimisceHeadClaw::SetActivity` `0x103c1cd0`, 55 bytes. ONE rewrite in front of the
	// Troika body: request 9 `ACT_WALK` with a non-null slot 167 `GetEnemy` becomes `0x1136
	// ACT_TZ_WALK2`. Request 9 WITHOUT an enemy, and every other request, forwards unchanged.
	if (Activity == NpcKernelAnim10Shared::GAnim10ActWalk && GetEnemy() != nullptr)   // vtable +0x29c
	{
		TroikaSetActivity(NpcKernelAnim10Shared::GAnim10ActTzWalk2);   // the direct `thunk_FUN_10295750`
		return;
	}
	TroikaSetActivity(Activity);
}

// Slot 453: `0x103c16f0`, a direct call into the Troika body `0x102ad140` first, then its own bits.
// Slot 453: `0x103c16f0`'s own bits, the body of its class's `BuildScheduleTestBits` override (story 5 step 3).

void FElysiumNpcTzimisceHeadClaw::BuildScheduleTestBits(FElysiumNpcConditions& InOutMask)
{
	FElysiumNpc::BuildScheduleTestBits(InOutMask);
	InOutMask.Set(EElysiumNpcCond::ShouldCharge);
}

// Slot 440: `0x103c1720`.
// `0x103c1720`
// `0x103c1720`, `CNPC_VTzimisceHeadClaw::TranslateSchedule`, the body of `FElysiumNpcTzimisceHeadClaw::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpcTzimisceHeadClaw::TranslateScheduleRetail(int32 ScheduleNumber)
{
	if (ScheduleNumber == 0xbf) { return 0x157; }
	if (ScheduleNumber == 0xc7) { return 0xc8; }
	if (ScheduleNumber == 0xe7) { return 0x156; }
	if (ScheduleNumber == 0xed || ScheduleNumber == 0xef) { return 0xf0; }
	return TroikaTranslateScheduleRetail(ScheduleNumber);
}

// Slot 337: `0x103c1cb0`.
int32 FElysiumNpcTzimisceHeadClaw::GetUsedHullBits()
{
	// The Troika body `0x1029a050` called directly, its 1 ORed with this class's bit.
	// `OR AH,0x8` in the listing; the decompiled C drops the bit.
	return FElysiumNpc::GetUsedHullBits() | 0x0800;
}

// Slot 332: `0x103c1d80`, a replacement that does not chain.
/** `CNPC_VTzimisceHeadClaw::vfunc332` (`0x103c1d80`), 956 bytes; slot 332's base (`0x1014f890`) is
 *  `return;`. Guarded on a target that is the PLAYER (`+0xa8`) and has a combat view (`+0x9c`).
 *  Seven steps, in order: `BeginSlowEntity(victim, 500.0)` only when the expiry still equals the
 *  0.0 sentinel; `m_flSlowedExpire = curtime + RandomFloat(5.0, 8.0)` (`103c1dd5`: `PUSH 8.0` then
 *  `PUSH 5.0`); latch the victim's handle; spawn `Tzim2_player_emitter` at the victim's origin and
 *  attach it at `Bip01 Spine` mode 1 when the handle is dead; emit
 *  `Character/Monster/TC_FatGuy/Sluge_Hit.wav` on channel **4** and `…/Sluge_Affected.wav` on
 *  channel **3**, both volume 1.0 attenuation 0.8 pitch 100, from the victim's slot-222 origin;
 *  spawn `HUD_Tzim2_emitter` on the player's inventory slot 0 at mode `0xe` with an empty bone;
 *  finally raise condition `0x35` when the 2-D distance reaches `_DAT_104cd108`, a **DOUBLE**
 *  reading **240.0** (`103c20dc` is `FCOMP double ptr`). */
void FElysiumNpcTzimisceHeadClaw::Slot332(FElysiumEntity* SlowTarget)
{
	// `103c1d8a`..`103c1daa`: a null target, a target with no `+0xa8` player record, or one with no
	// `+0x9c` combat view does nothing at all. So slot 332 is a PLAYER-ONLY body.
	if (SlowTarget == nullptr || !SpeciesMisc10_2IsPlayer(*this, SlowTarget))
	{
		return;
	}
	FElysiumCombatCharacter* Victim = SlowTarget->AsCombatCharacter();
	if (Victim == nullptr)
	{
		return;
	}
	// 1. `103c1db0`: `BeginSlowEntity(victim, 500.0)` only while `m_flSlowedExpire` (**+0x6678**,
	//    not the `+0x6684` the walk names) still equals the 0.0 sentinel.
	if (HeadClawSlowedExpire == GHeadClawSlowExpireSentinel)
	{
		BeginSlowEntity(SlowTarget->Handle, GHeadClawSlowEntityMagnitude);
	}
	// 2. `103c1dd5`: `m_flSlowedExpire = RandomFloat(5.0, 8.0) + curtime`, unconditionally.
	HeadClawSlowedExpire = NpcKernelSpeciesMisc10_2Shared::SpeciesMisc10_2Now(*this)
		+ ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(GHeadClawSlowSecondsMin,
			GHeadClawSlowSecondsMax);
	// 3. `103c1e02`: `m_hSlowedEntity` (**+0x6674**) = the victim's handle.
	HeadClawSlowedEntity = SlowTarget->Handle;
	// 4. `103c1e08`: with the particle handle (**+0x667c**) dead, spawn `Tzim2_player_emitter` at the
	//    VICTIM's slot-217 origin, store it, attach at `Bip01 Spine` mode 1, start it.
	if (World == nullptr || World->Resolve(HeadClawPlayerEmitter) == nullptr)
	{
		const int32 Index = CreateNamedEmitter(TEXT("Tzim2_player_emitter"),
			SlowTarget->Origin / ElysiumMove::U, /*AttachMode*/ 1, SlowTarget->Handle, TEXT("Bip01 Spine"));
		StartNamedEmitter(Index);
	}
	// 5. `103c1f13` / `103c1f5c`: the two Slug sounds, from a `CPASAttenuationFilter` built on the
	//    VICTIM's slot-222 emission origin at attenuation 0.8, volume 1.0 and pitch 100 — the HIT on
	//    channel **4** and the AFFECTED on channel **3**, in that order.
	EmitNamedWav(SlowTarget, /*Channel*/ 4, TEXT("Character/Monster/TC_FatGuy/Sluge_Hit.wav"),
		1.f, 0.8f, 100);
	EmitNamedWav(SlowTarget, /*Channel*/ 3, TEXT("Character/Monster/TC_FatGuy/Sluge_Affected.wav"),
		1.f, 0.8f, 100);
	// 6. `103c1f87`: with the player's inventory slot 0 holding an entity and the HUD handle
	//    (**+0x6680**) dead, spawn `HUD_Tzim2_emitter` at `vec3_origin`, attach it to THAT ITEM with
	//    mode `0xe` and an EMPTY bone name, and start it.
	const FElysiumEntity* Item = PlayerInventorySlot0();
	if (Item != nullptr && (World == nullptr || World->Resolve(HeadClawHudEmitter) == nullptr))
	{
		const int32 Index = CreateNamedEmitter(TEXT("HUD_Tzim2_emitter"), FVector::ZeroVector,
			/*AttachMode*/ 0xe, Item->Handle, TEXT(""));
		StartNamedEmitter(Index);
	}
	// 7. `103c20ba`: the **2-D** distance (`sqrt(dx*dx + dy*dy)`, no Z) between MY slot-217 origin
	//    and the victim's; at or past `_DAT_104cd108` — a DOUBLE reading **240.0** — the cvar is
	//    touched and `SetCondition(0x35)` is raised.
	const FVector Delta = (SlowTarget->Origin - Origin) / ElysiumMove::U;
	const double Flat = FMath::Sqrt(Delta.X * Delta.X + Delta.Y * Delta.Y);
	if (GHeadClawConditionDistanceUnits <= Flat)
	{
		Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(0x35));
	}
}

// --- Moved from `ElysiumNpcAnim10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcLifecycle19_2.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcPrecache10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcSaveRestore10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcSchedule.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcSpecies.cpp` (story 5 step 4) ---

bool FElysiumNpcTzimisceHeadClaw::FUN_103c19e0(FElysiumEntity* Enemy)
{
	// `0x103c19e0`, `CNPC_VTzimisceHeadClaw`'s slot 599:
	//
	//     if (RequestMeleeSlot(m_pAttackCoordinator, this)) {     // 0x1025db70
	//         (*DAT_10924edc)->vfunc1();
	//         m_bInMelee = 1;
	//         m_flMeleeMustLeaveTimer = curtime + RandomFloat(7.5, 15.0);
	//         return true;
	//     }
	//     m_bInMelee = 0;
	//     return false;
	//
	// It replaces the Troika line's five-term ladder with the COORDINATOR ALONE: no frenzied bits,
	// no follower boss, no melee-enter timer, no range or height test. The timer it arms afterwards
	// is the same `RandomFloat(7.5, 15.0)` (`0x40f00000` / `0x41700000`) the Troika line uses, and
	// the event fires BEFORE the two writes, which is the Troika line's order too.
	//
	// `MeleeCoordinatorAdmits599()` is family TroikaHelpers' seam over `0x1025db70` and answers
	// false with no coordinator object here — so this body takes its refusal arm, clears
	// `m_bInMelee` and answers false. That IS retail's behaviour for a coordinator with no free
	// slot; what this substrate cannot yet produce is the accepting arm.
	(void)Enemy;
	if (MeleeCoordinatorAdmits599())
	{
		++MeleeEventFires;
		bInMelee = true;
		const double Now = World != nullptr ? World->NowSeconds() : 0.0;
		MeleeMustLeaveTimer = Now + static_cast<double>(
			ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
				.FRandRange(SpeciesMeleeMustLeaveMin, SpeciesMeleeMustLeaveMax));
		return true;
	}
	bInMelee = false;
	return false;
}

// --- Moved from `ElysiumNpcSpecies2.cpp` (story 5 step 4) ---

bool FElysiumNpcTzimisceHeadClaw::FUN_103c24a0() const
{
	// `0x103c24a0`, thirty bytes: `return 0.0 < m_flSlowedExpire (+0x6678)` — `_DAT_1044fab0` read
	// out of `.rdata` as **0.0f**.
	//
	// STRICTLY greater, and against zero rather than against `curtime`: the word is a `FIELD_TIME`
	// on `CNPC_VTzimisceHeadClaw`'s datamap but this predicate only asks whether it was ever ARMED,
	// not whether it has expired. A head claw that was slowed once and whose window has long since
	// run out still answers true here until something writes the word back to zero.
	return NpcKernelSpecies2Shared::Species2Zero < static_cast<float>(HeadClawSlowedExpire);
}

// --- Moved from `ElysiumNpcSpeciesMisc10_2.cpp` (story 5 step 4) ---

bool FElysiumNpcTzimisceHeadClaw::HeadClawSlowRunning() const
{
	// `0x103c24a0`: `m_flSlowedExpire` (+0x6678) strictly above 0.0.
	return HeadClawSlowedExpire > GHeadClawSlowExpireSentinel;
}

void FElysiumNpcTzimisceHeadClaw::TzimisceHeadClawEndSlow(bool bForce)
{
	// `103c223c`: the gate, then forced-or-lapsed.
	if (!HeadClawSlowRunning())
	{
		return;
	}
	if (!bForce && !(HeadClawSlowedExpire <= NpcKernelSpeciesMisc10_2Shared::SpeciesMisc10_2Now(*this)))
	{
		return;
	}
	// `103c2265`: zero the expiry first.
	HeadClawSlowedExpire = 0.0;
	// `103c2277`: ONLY while the slowed entity resolves AND carries a combat view — the handle
	// clear, the `EndSlowEntity` and the sound are ALL inside that guard, so a stale handle leaves
	// `m_hSlowedEntity` standing.
	FElysiumEntity* Victim = World != nullptr ? World->Resolve(HeadClawSlowedEntity) : nullptr;
	if (Victim != nullptr && Victim->AsCombatCharacter() != nullptr)
	{
		EndSlowEntity(HeadClawSlowedEntity, GHeadClawSlowEntityMagnitude);
		HeadClawSlowedEntity = FElysiumEntityHandle::Invalid();
		// `103c2319`: `…/Sluge_Affected.wav` on channel 3 from the VICTIM's slot-222 origin at
		// attenuation 0.8 — the same wav slot 332's second emit uses.
		EmitNamedWav(Victim, /*Channel*/ 3,
			TEXT("Character/Monster/TC_FatGuy/Sluge_Affected.wav"), 1.f, 0.8f, 100);
	}
	// `103c2360` / `103c23e0`: `UTIL_Remove` the two owned effects at **+0x667c** and **+0x6680**,
	// each only while its handle's serial matches its slot, and set both to -1 either way.
	if (World != nullptr && World->Resolve(HeadClawPlayerEmitter) != nullptr)
	{
		RemoveNamedEntity(HeadClawPlayerEmitter);
		HeadClawPlayerEmitter = FElysiumEntityHandle::Invalid();
	}
	if (World != nullptr && World->Resolve(HeadClawHudEmitter) != nullptr)
	{
		RemoveNamedEntity(HeadClawHudEmitter);
		HeadClawHudEmitter = FElysiumEntityHandle::Invalid();
	}
}

// --- Moved from `ElysiumNpcTranslate.cpp` (story 5 step 4) ---

