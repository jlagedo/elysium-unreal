#pragma once

#include "Substrate/ElysiumNpcAnimal.h"

// `CNPC_VDog` (primary vtable `0x104b15bc`), built by `npc_VDog` factory `0x10373480`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcDog : public FElysiumNpcAnimal
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VDog", FElysiumNpcAnimal)

	virtual int32 NPC_EarlyTranslateActivity(int32 Activity) override;
	virtual int32 SelectIdealStateRetail() override;
	virtual int32 PreSelectIdealStateRetail() override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual float MaxYawSpeed() override;
	virtual bool FValidateHintType(void* Hint) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcState19.inl`.
	/** `CNPC_VDog`'s snarl-arm pair, `0x103743c0` at `103746b5`: `+0x6680 = +0x667c` then
	 *  `+0x6678 = gpGlobals->curtime`. SEAM: `CNPC_VDog` carries no datamap in the corpus and no
	 *  reader of either offset is recovered, so the source word `+0x667c` answers 0 and the snapshot
	 *  records only that the arm ran. The stamp itself is real. */
	float DogSnarlSourceWord = 0.f;    // `+0x667c`, unrecovered
	float DogSnarlPrevValue = 0.f;     // `+0x6680`
	double DogSnarlTime = 0.0;         // `+0x6678`
	void DogCombatShortCircuit();   // `0x10374e50`

	// --- 0019/8 Spawn19 (lane L08): words and helpers the family's bodies need (three searches each
	// in the L08 report) ---
	/** `+0x6688` and `+0x6674`, the two words `CNPC_VDog::Spawn` (`0x10374000`) seeds. `CNPC_VDog`
	 *  carries no datamap in the corpus and no reader of either is recovered; declared by offset. */
	int32 DogWord6688 = 0;                   // +0x6688 (walked)
	double DogStamp6674 = 0.0;               // +0x6674 (walked), curtime + RandomFloat(0, 1)

	// --- 0019/8 shape: forwarding overrides (replace the body, keep the declaration) ---
	virtual void Spawn() override;
	virtual bool HandleAnimEvent(const FElysiumAnimEvent& Event) override;
	virtual void RunAI(bool Arg0) override;
	virtual void GatherConditions() override;
	virtual int32 SpeciesSelectSchedule() override;
	virtual int32 StartTaskSlot442(void* Arg0) override;
	virtual int32 RunTaskSlot444(void* Arg0) override;
};
