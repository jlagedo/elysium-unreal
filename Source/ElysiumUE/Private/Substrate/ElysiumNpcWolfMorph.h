#pragma once

#include "Substrate/ElysiumNpcPlayerController.h"

// `CNPC_VWolfMorph` (primary vtable `0x104d05a4`), built by `npc_VWolfMorph` factory `0x103dc6c0`
// (story 5 fold A2).
//
// The player's wolf-form body: a player controller that plays `ACT_WOLF_MORPH` whatever it is asked
// to play. Its creators are the trait apply/remove pair `0x101f8620` / `0x101f8f30`, whose tails the
// port runs (`ElysiumCombatCharacter.cpp`) without the spawn, so no shipped path stands one today;
// the class keeps the retail contract and tests construct it by classname.
//
// Eleven own slots (the deleting destructor, slot 5 `0x103dcf20`, is the C++ destructor's; slot 82
// is the datamap accessor the generated bindings stand; slots 451, 452 and 546 are
// the dead class-name, loaded-flag and squad-slot-id rows (0019/6); slot 580
// `0x103dc750` returns the class's own schedule space `0x109402a8`, which the corpus loads as unit
// `cnpc_vwolfmorph` keyed on this class). It inherits the controller's 72, 103, 245, 246, 300,
// 362-365 (all false), 404, 431, 437 and 488-497.
class FElysiumNpcWolfMorph : public FElysiumNpcPlayerController
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VWolfMorph", FElysiumNpcPlayerController)


	// Slot 138 `0x103dce50` — `return 2;`.
	virtual int32 Classify() override;
	// Slot 375 `0x103dcdc0` — every activity becomes `ACT_WOLF_MORPH` (`0x1145`).
	virtual int32 NPC_EarlyTranslateActivity(int32 Activity) override;
	// Slot 420 `0x103dce00`.
	virtual void NPCInit() override;
	// Slot 438 `0x103dceb0` — the species hook of the port's selector.
	virtual int32 SpeciesSelectSchedule() override;
	// Slot 588 `0x103dcf00` — a bare `RET`.
	virtual void Slot588() override;

	/** `ACT_WOLF_MORPH`, registered id `0x1145` (4421). */
	static constexpr int32 ActWolfMorph = 0x1145;

	// --- Own datamap words (`datamap_CNPC_VWolfMorph` `0x10663b38`, both SAVE) ---------------------
	//
	// No `CNPC_VWolfMorph` body reads or writes either word (every own body above was walked;
	// `+0x6664` / `+0x6668` are other species' words at the same offsets elsewhere).
	//
	// `+0x6668 m_flNextFleeSoundTime` has NO port member: its SAVE row carries the same retail name
	// as the Troika's `m_flNextFleeSoundTime` (`+0x641c`, `FElysiumNpcMemory::NextFleeSoundTime`), and
	// the registry's save walk keeps one row per name, most-derived first — binding it here would
	// silently stop the Troika word being saved on a wolf morph (`gen_kernel_bindings`
	// `check_species_save_names`). With no reader the gap is inert; the species shape map records it
	// ABSENT with that reason.

	bool bFirstThink = false;          // +0x6664 m_bFirstThink
};
