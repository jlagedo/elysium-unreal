// `CAI_BaseNPC`'s bodies of the `Precache10` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBasePrecache10.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityDefs.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcPrecache10Shared.h"

// --- Moved from `ElysiumNpcPrecache10.cpp` (story 5 step 5) ---

// -------------------------------------------------------------------------------------------------
// `CAI_BaseNPC::Precache` — `0x1027bb50`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpcBase::Precache()
{
	// Step 1. `m_spawnEquipment` (`+0x5dec`, this runtime's `AdditionalEquipment`) is precached when
	// the pointer is non-null AND the string is not the two-byte literal `"0"` (`DAT_105399a0`). A
	// null pointer reads as the empty string (`DAT_106b8540`) and an empty string is NOT `"0"`, so
	// retail's outer null test is what stops an unset keyfield — and this runtime, which carries the
	// keyfield as an `FString`, spells the same pair of tests as "non-empty and not the sentinel".
	if (!AdditionalEquipment.IsEmpty() && AdditionalEquipment != NpcKernelPrecache10Shared::GNoneSentinel)
	{
		NpcKernelPrecache10Shared::Precache10Other(*this, AdditionalEquipment);
	}

	// Step 2. Slot 452 `LoadedSchedules` (vtable `+0x710`).
	if (!LoadedSchedules())
	{
		// `DevMsg` then `UTIL_Remove(this)` (`thunk_FUN_101cd940`) — and RETURN, so the base
		// `CBaseCombatCharacter::Precache` never runs. That is the whole point of the arm: an NPC
		// whose schedule text failed to parse is removed rather than half-precached.
		//
		// UNREACHABLE IN THIS RUNTIME TODAY: `FElysiumNpcBase::LoadedSchedules` answers true for every
		// class by design (`ElysiumNpcSchedule.cpp:297` — nothing here parses schedule text,
		// so no class flag can be cleared). Ported anyway, and exercised through the same slot.
		//
		// The format is `s_ERROR__Rejecting_spawn_of__s_as_e_105cd21c` verbatim, including the
		// apostrophe and the trailing period the checklist's walk drops.
		UE_LOG(LogElysiumNpcEnt, Error,
			TEXT("ERROR: Rejecting spawn of %s as error in NPC's schedules."), *DebugString());
		Kill();
		return;
	}

	// Step 3. `CBaseCombatCharacter::Precache` (`0x10011324` -> `CBaseCombatCharacter::PrecacheOnce`
	// `0x1033f750`). SEAM, and deliberately not a row of this family: that body is the once-guarded
	// GLOBAL block every character shares — the discipline emitters (`D_Potence_Emitter`,
	// `D_AuspexCast_Emitter`, `D_ObfuscateIn_Emitter`, …), the damage-effect emitters
	// (`DMGFX_body_fire_emitter`, `DMGFX_hud_shock_emitter`, …) and `HUD_targeting_emitter`. It is
	// `CBaseCombatCharacter`'s story, not the NPC kernel's, it is precached once per map rather than
	// per NPC, and this runtime's discipline and damage visuals are Unreal assets the map epoch
	// already holds. Nothing is recorded for it, because recording a per-map block on a per-NPC log
	// would misstate what retail does.
}

// --- Moved from `ElysiumNpcPrecache10.cpp` (story 5 step 5) ---

void FElysiumNpcBase::IssuePrecache(const FPrecacheOp& Op)
{
	// The record IS the recovered half; the acquisition is the seam. See the `.inl`.
	PrecacheLog.Add(Op);
}
