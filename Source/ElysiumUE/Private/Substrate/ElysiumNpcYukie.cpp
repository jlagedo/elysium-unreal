#include "Substrate/ElysiumNpcYukie.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcConditions10Shared.h"
#include "Substrate/ElysiumNpcLifecycle19_2Shared.h"
#include "Substrate/ElysiumNpcSensesBodiesShared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// --- File-scope helpers moved with this class's bodies (story 5 step 4) ---

namespace
{
	// `CNPC_VYukie`'s two melee draws: `0x41b40000` / `0x42340000` at `0x103dd8b0` and
	// `0x40a00000` / `0x41200000` at `0x103dd9a0`.
	constexpr float GMiscYukieMustLeaveMin = 22.5f;
	constexpr float GMiscYukieMustLeaveMax = 45.f;
	constexpr float GMiscYukieCanEnterMin = 5.f;
	constexpr float GMiscYukieCanEnterMax = 10.f;
	// `_DAT_1044f02c` = 1.5f — Yukie's melee-range multiplier.
	constexpr float GYukieMeleeRangeScale = 1.5f;
	// The weapon capability bits slots 600 and `CNPC_VYukie`'s flee gate require.
	constexpr uint32 SpeciesMeleeWeaponCapabilityBits = 0x18000u;
	// `CNPC_VYukie`'s flee window, `curtime + RandomFloat(22.5, 45.0)`. The immediates are
	// `0x41b40000` and `0x42340000`; 29c's walk reads them as 22.0/45.0 and the first is **22.5**.
	constexpr float YukieFleeWindowMin = 22.5f;             // 0x41b40000
	constexpr float YukieFleeWindowMax = 45.0f;             // 0x42340000
}

// The melee quartet: 599 `0x103dd8b0` (no gates), 600 `0x103dd900` (the one-shot flee), 601
// `0x103dd9a0` (no coordinator release) and 602 `0x103dda10` (distance or clock). Story 5 step 3
// wires all four, which were ported but not dispatched (`21bb56cf`; `docs/vtmb/npc-ai/shape.md`
// § "The melee quartet's species replacements").
bool FElysiumNpcYukie::Slot599(int32 Arg)
{
	(void)Arg;
	return YukieEnterMelee();
}

/** `0x103dd900` — `CNPC_VYukie`'s slot 600. */
bool FElysiumNpcYukie::Slot600(FElysiumEntity* Enemy)
{
	// `0x103dd900`, `CNPC_VYukie`'s slot 600 — and it is not a melee-entry body at all. It reuses
	// the slot for a ONE-SHOT FLEE:
	//
	//     caps = GetActiveWeapon() ? weapon->slot360() : 0;        // +0x5a0
	//     if ((caps & 0x18000) != 0 && m_bInMelee == 0) {
	//         m_bInMelee = 1;
	//         m_flMeleeMustLeaveTimer = curtime + RandomFloat(22.5, 45.0);   // +0x6074
	//         (*DAT_10924edc)->vfunc1();
	//         return true;
	//     }
	//     return false;
	//
	// It keeps the Troika line's weapon-capability gate and its `m_bInMelee` latch and DROPS the
	// coordinator entirely, then arms `+0x6074` with a window an order of magnitude longer than the
	// line's `RandomFloat(7.5, 15.0)` — 22.5 to 45 seconds, off immediates `0x41b40000` and
	// `0x42340000`. 29c's walk reads the first as 22.0; the immediate says **22.5**.
	//
	// The latch is what makes it one-shot: `m_bInMelee` is never cleared by this body, so a second
	// call before something else clears it answers false and writes nothing.
	//
	// `ActiveWeaponCapabilityWord()` is family Motor's seam over the weapon's `+0x5a0` (slot 360)
	// and answers 0, so the gate is closed today and the whole body refuses. The refusal is the
	// recovered one — retail refuses for a weapon with neither capability bit — and the arm that
	// this substrate cannot yet reach is the accepting one.
	(void)Enemy;
	const FElysiumEntity* Weapon = (World != nullptr && Inventory.ActiveWeapon.IsSet())
		? World->Resolve(Inventory.ActiveWeapon)
		: nullptr;
	const uint32 Capability = Weapon != nullptr ? ActiveWeaponCapabilityWord() : 0u;
	if ((Capability & SpeciesMeleeWeaponCapabilityBits) != 0 && !bInMelee)
	{
		bInMelee = true;
		const double Now = World != nullptr ? World->NowSeconds() : 0.0;
		MeleeMustLeaveTimer = Now + static_cast<double>(
			ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
				.FRandRange(YukieFleeWindowMin, YukieFleeWindowMax));
		++MeleeEventFires;
		return true;
	}
	return false;
}

void FElysiumNpcYukie::Slot601(FElysiumEntity* Enemy)
{
	(void)Enemy;
	YukieLeaveMelee();
}

/** `0x103dda10`, `CNPC_VYukie#602` — Yukie's replacement for the melee-leave decision the Troika
 *  line answers at `FElysiumNpc::Slot602`. Two arms on slot 308 `HasUsableRangedWeapon()` (vtable
 *  `+0x4d0`): with no ranged weapon, leave when `2 * meleeRange * 1.5` (`_DAT_1044f02c`) is
 *  **`<=`** `m_flEnemyDist`; with one, leave when `m_flMeleeMustLeaveTimer` has expired. No
 *  frenzy gate, no follower-boss gate and no attack coordinator — the four terms the Troika body
 *  spends its first half on are simply gone. */
bool FElysiumNpcYukie::Slot602()
{
	// `0x103dda10`, 103 bytes:
	//
	//     if (!HasUsableRangedWeapon())                             // slot 308, vtable +0x4d0
	//         return (2 * meleeRange) * 1.5 <= m_flEnemyDist;       // _DAT_1044f02c, +0x6268
	//     return m_flMeleeMustLeaveTimer <= curtime;                // +0x6074
	//
	// Retail spells the first compare as `(a < b) != (a == b)`, which is the FPU flag pair for
	// `a <= b`; the second is `!(curtime < timer)`, the same relation the other way round. Both are
	// `<=`, not `<`.
	//
	// The melee range is `DAT_10924a1c` read as `IsCommand() ? 0.0 : +0x28` — family
	// **TroikaHelpers**' `MeleeRangeUnits`, `debug_melee_advance_combatmove_dist` "100".
	//
	// Against the Troika line (`FElysiumNpc::Slot602`) this body drops FOUR terms: the
	// `m_bfNPCFrenziedFlags & 2` gate, the follower-boss gate, the attack-coordinator null test and
	// the coordinator's own `holds-me` tail. Yukie leaves melee on distance or on the clock alone.
	//
	// Dispatched by `FElysiumNpcYukie::Slot602` since story 5 step 3.
	if (!HasUsableRangedWeapon())
	{
		const float RangeUnits = 2.0f * MeleeRangeUnits() * GYukieMeleeRangeScale;
		return RangeUnits <= ScheduleHost.EnemyDistUnits;
	}
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	return MeleeMustLeaveTimer <= Now;
}

// Slot 420: `0x103dd800`.
// `0x103dd800`
void FElysiumNpcYukie::NPCInit()
{
	HumanCombatantNPCInit();
	HideActiveWeaponIfAny();
}

// Slot 461: `0x103dd780`, the selector tag 0x2a and then a direct call into the combatant's `0x10387380`.
int32 FElysiumNpcYukie::SelectIdealStateRetail()
{
	SelectIdealStateSelector = 0x2a;
	return HumanCombatPatrolSelectIdealState();
}

// Slot 201: `0x103ddaf0`
bool FElysiumNpcYukie::FVisible(FElysiumEntity* SeenTarget, int32 Mask, FElysiumEntity* Blocker, int32 Arg4)
{
	// `CNPC_VYukie#201`, story 29c-1's `YukieFVisible`, which chains slot 594 below.
	FElysiumEntityHandle Unused;
	return YukieFVisible(SeenTarget, &Unused);
}

// Slot 404: `0x103dd880`.
/** `CNPC_VYukie::IRelationType` (`0x103dd880`), 20 bytes, read off the LISTING because the C hides
 *  the return: the candidate is loaded into `EAX`, a null one `RET`s with `EAX` still holding that
 *  null (so `D_ER`), and anything else tail-jumps to the Troika body unchanged. The whole species
 *  override is the null guard. */
int32 FElysiumNpcYukie::IRelationType(FElysiumEntity* Candidate)
{
	// `CNPC_VYukie::IRelationType` (`0x103dd880`), 20 bytes, read off the LISTING: `MOV EAX,[ESP+4]
	// / TEST EAX,EAX / JNZ` then `RET 0x4` with `EAX` still holding the null — so a null candidate
	// answers 0, `D_ER`. Anything else is `JMP 0x10001adc`, a tail jump to the Troika body.
	if (Candidate == nullptr)
	{
		return NpcKernelConditions10Shared::GCond10_D_ER;
	}
	return TroikaIRelationType(Candidate);
}

// Slot 546: `0x103dd1f0`, the class's own schedule id space.
const TCHAR* FElysiumNpcYukie::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x10940314`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VYukie"), TEXT("0x103dd1f0"), TEXT("0x10940314") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}

// --- Moved from `ElysiumNpcConditions10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcLifecycle19_2.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcMisc.cpp` (story 5 step 4) ---

bool FElysiumNpcYukie::YukieEnterMelee()
{
	// `CNPC_VYukie::vfunc599` `0x103dd8b0`, the whole body:
	//     (*DAT_10924edc)->vfunc1();                                    // the global melee event
	//     m_bInMelee = 1;                                               // +0x6078
	//     m_flMeleeMustLeaveTimer = RandomFloat(22.5f, 45.0f) + curtime; // +0x6074
	//     return true;
	//
	// Against family **TroikaHelpers**' `Slot599` (the Troika line, `0x102b5650`) this is the whole
	// species difference: **Yukie has no gates at all**. No frenzy bit, no follower boss, no
	// can-enter timer, no range term, no height term, no attack coordinator — she always enters
	// melee, and her must-leave window (22.5–45 s) is three times the Troika line's (7.5–15 s).
	//
	// `(*DAT_10924edc)->vfunc1()` is the same global event object families Bosses and TroikaHelpers
	// already count through `MeleeEventFires`; the same counter is incremented rather than a second
	// one stood beside it.
	++MeleeEventFires;
	bInMelee = true;
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	MeleeMustLeaveTimer = Now + static_cast<double>(
		ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
			.FRandRange(GMiscYukieMustLeaveMin, GMiscYukieMustLeaveMax));
	return true;
}

void FElysiumNpcYukie::YukieLeaveMelee()
{
	// `0x103dd9a0`, `CNPC_VYukie#601`, the whole body:
	//     (*DAT_10924edc)->vfunc1();                                     // the global melee event
	//     m_bInMelee = 0;                                                // +0x6078
	//     if (HasUsableRangedWeapon())                                   // slot 308 (+0x4d0)
	//         m_flMeleeCanEnterTimer = RandomFloat(5.0f, 10.0f) + curtime;  // +0x6070
	//
	// The Troika line's `0x102b5880` with its LAST line dropped: there is no attack-coordinator
	// release. Everything before it — the event, the clear, the gated re-arm and both draw bounds —
	// is identical. So Yukie enters melee unconditionally and, on the way out, never gives a
	// coordinator slot back, because she never took one.
	//
	// Slot 308 `HasUsableRangedWeapon` (`0x10336d70`) is still a generated stub answering false, so
	// the timer arm is not reached today; it is wired, not inlined.
	++MeleeEventFires;
	bInMelee = false;
	if (HasUsableRangedWeapon())
	{
		const double Now = World != nullptr ? World->NowSeconds() : 0.0;
		MeleeCanEnterTimer = Now + static_cast<double>(
			ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
				.FRandRange(GMiscYukieCanEnterMin, GMiscYukieCanEnterMax));
	}
}

// --- Moved from `ElysiumNpcSensesBodies.cpp` (story 5 step 4) ---

bool FElysiumNpcYukie::YukieFInViewCone(const FElysiumEntity* Candidate) const
{
	// `0x103ddaa0`, `CNPC_VYukie#363`, 58 bytes: the gate and a literal `1`. Yukie has no view
	// cone at all — the base body it replaces (`0x102b4540`) is the follower/cone chain.
	return SpeciesStealthSenseGate(Candidate);
}

bool FElysiumNpcYukie::YukieFVisible(const FElysiumEntity* Candidate, FElysiumEntityHandle* OutBlocker)
{
	// `0x103ddaf0`, `CNPC_VYukie#201`, 109 bytes. The same gate as the werewolf, but the success
	// arm CHAINS rather than answering true:
	//
	//     return slot594(candidate, param_2, NULL, 0) != 0;     // vtable +0x948, 0x102b4760
	//
	// and the blocker is zeroed on BOTH veto arms (`npc_ignore_senses` and `npc_ignore_player`),
	// never on a null candidate. Slot 594 is story 29d's row and is a declared stub here, so the
	// chain answers what the stub answers and the gate above it is the recovered half.
	if (Candidate == nullptr)
	{
		return false;
	}
	if (SpeciesStealthSenseGate(Candidate))
	{
		return Slot594(const_cast<FElysiumEntity*>(Candidate), 0, nullptr, 0);
	}
	if (OutBlocker != nullptr)
	{
		*OutBlocker = FElysiumEntityHandle();
	}
	return false;
}

// --- Moved from `ElysiumNpcSpecies.cpp` (story 5 step 4) ---

