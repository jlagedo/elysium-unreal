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
	// The retail class this C++ class is: `OwnRetailClass`'s row and `FElysiumNpc::AsSpecies`'s key.
	static constexpr const TCHAR* RetailClassName = TEXT("CNPC_VDog");

	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual int32 NPC_EarlyTranslateActivity(int32 Activity) override;
	virtual int32 SelectIdealStateRetail() override;
	virtual int32 PreSelectIdealStateRetail() override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual float MaxYawSpeed() override;
	virtual bool FValidateHintType(void* Hint) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcKernelState19.inl`.
	/** `CNPC_VDog`'s snarl-arm pair, `0x103743c0` at `103746b5`: `+0x6680 = +0x667c` then
	 *  `+0x6678 = gpGlobals->curtime`. SEAM: `CNPC_VDog` carries no datamap in the corpus and no
	 *  reader of either offset is recovered, so the source word `+0x667c` answers 0 and the snapshot
	 *  records only that the arm ran. The stamp itself is real. */
	float DogSnarlSourceWord = 0.f;    // `+0x667c`, unrecovered
	float DogSnarlPrevValue = 0.f;     // `+0x6680`
	double DogSnarlTime = 0.0;         // `+0x6678`
	void DogCombatShortCircuit();   // `0x10374e50`

};
