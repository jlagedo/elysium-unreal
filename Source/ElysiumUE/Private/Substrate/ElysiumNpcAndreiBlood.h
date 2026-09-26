#pragma once

#include "Substrate/ElysiumNpcVampireBoss.h"

// `CNPC_VAndreiBlood` (primary vtable `0x104a6fc4`), built by `npc_VAndreiBlood` factory
// `0x1035c080`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcAndreiBlood : public FElysiumNpcVampireBoss
{
public:
	// The retail class this C++ class is: `OwnRetailClass`'s row and `FElysiumNpc::AsSpecies`'s key.
	static constexpr const TCHAR* RetailClassName = TEXT("CNPC_VAndreiBlood");

	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual void NPCInit() override;
	virtual void Precache() override;
	virtual int32 Restore(void* Archive) override;
	virtual int32 SelectIdealStateRetail() override;
	virtual int32 SpeciesSelectSchedule() override;
	virtual bool FValidateHintType(void* Hint) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcDamage.inl`.
	// `CNPC_VAndreiBlood`'s two emitter handles. Both are WALKED words past the datamap's last row
	// (`m_iHitMax` +0x66dc): `0x1035e1a0` owns `+0x66e0` and `0x1035e3c0` owns `+0x66e4`, read off the
	// listing (`MOV EAX, dword ptr [ESI + 0x66e0]`). 29b declared neither.
	FElysiumEntityHandle AndreiBloodEmitter;     // +0x66e0 (walked)
	FElysiumEntityHandle AndreiSummonEmitter;    // +0x66e4 (walked)
	bool bAndreiActivated = false;               // +0x66cc m_bActivated (datamap)
	/** `0x1035d150` — `CNPC_VAndreiBlood`'s slot-461 `SelectIdealState`. Writes the trace selector tag
	 *  4 (the base writes 1, `CNPC_VAnimal` 5, `CNPC_VHengeyokai` 0x13, `CNPC_VHunter` 0x17) and
	 *  answers `NPC_STATE_ALERT` (2) when `m_bActivated` is set, else `NPC_STATE_IDLE` (1). Slot 461's
	 *  Troika body is story 29e's, so this species arm lands under its retail name. */
	EElysiumNpcState CNPC_VAndreiBlood_vfunc461();
	/** `0x1035e1a0` — `CNPC_VAndreiBlood::StartBloodEmitter(string_t name)` and `0x1035e3c0` —
	 *  `StartSummonEmitter`. ONE body written twice: refuse a null name; release the previously cached
	 *  handle (`thunk_FUN_101cd940`) and set it to -1 if it still resolves; create the named emitter at
	 *  this NPC's origin; store the new handle (or -1 on failure); then attach and start it. The whole
	 *  of the difference is the word the handle lives in and the attach: the BLOOD arm parents the
	 *  emitter to this entity (`thunk_FUN_100faf60`) and the SUMMON arm attaches it at the bone
	 *  `Bip01_R_Hand` (`+0x3cc(this, 2, name)`). Both then call `+0x3c4`. */
	void StartBloodEmitter(const FString& Name);
	void StartSummonEmitter(const FString& Name);
	/** The bone the summon emitter attaches at, `s_Bip01_R_Hand_1053ec20`. */
	static const TCHAR* SummonEmitterBoneName();

	// From `ElysiumNpcFacing.inl`.
	// `CNPC_VAndreiBlood::FacePlayerAdvance` `0x1035e5f0`.
	void FacePlayerAdvance();

	// From `ElysiumNpcPositions.inl`.
	// `CNPC_VAndreiBlood`'s teleport position. There is NO time stamp beside it: `SelectTeleportNode`
	// (`0x1035ddd0`) caches the position and stamps no clock, unlike the Sheriff's and the Changs'.
	FVector AndreiLastTeleportPosition = FVector::ZeroVector;   // +0x66c0 m_vLastTeleportPosition
	/** `CNPC_VAndreiBlood::SelectTeleportNode` `0x1035ddd0` — types 17000 / `0x4269`, the clearance gate
	 *  at `DAT_104a6f7c`, and nearest-or-FARTHEST by a coin flip taken ONCE before the walk. */
	static int32 SelectTeleportNodeAndreiRule(TArrayView<const FHintWords> Nodes, const FVector& PlayerCm,
		bool bPickFarthest, TFunctionRef<bool(const FVector&, float)> Clear);
	int32 SelectTeleportNodeAndrei();
	/** `CNPC_VAndreiBlood::PositionClearForTeleport` `0x1035e030`. */
	bool PositionClearForTeleportAndrei(const FVector& PositionCm, float ClearanceCm) const;

	/** SEAM for `thunk_FUN_1035e920` (`CNPC_VAndreiBlood::SelectSchedule`'s 0x15d/0x15c split, which
	 *  reads `+0x66b8`) and for `CNPC_VManBat`'s navigator-state probe `0x1027d990`. Both are species
	 *  state this substrate does not carry; each answers its refusing value and says so at the call
	 *  site in `AndreiBloodSelectSchedule` / `ManBatSelectSchedule`. */
	bool AndreiBloodSelectGate() const;

	// From `ElysiumNpcSpecies.inl`.
	int32 AndreiHitMax = 0;        // +0x66dc CNPC_VAndreiBlood::m_iHitMax (datamap)
	/** `0x1035e920` — `CNPC_VAndreiBlood`: may another runner be made? */
	bool FUN_1035e920() const;
	/** `0x1035e950` — `CNPC_VAndreiBlood`: roll `m_iHitMax`. */
	void FUN_1035e950();

	/** `CNPC_VAndreiBlood::Destructor` (`0x1035cd00`). Retail, in order:
	 *    1. Restore its own vftable and the secondary one at `+0x19b0` (a C++ artifact; nothing a
	 *       program can observe, and nothing this port has).
	 *    2. Under a `"CNPC_VAndreiBlood::Destructor"` scope frame: `UTIL_Remove` (`0x101cd940`) the
	 *       blood emitter at **`+0x66e0`** and then the summon emitter at **`+0x66e4`**, each only when
	 *       the handle resolves live, and write `0xffffffff` back to each after its removal.
	 *       **Retail writes the invalid handle only on the arm it removed on**, not unconditionally.
	 *    3. Destroy `m_OnTransformComplete` (`+0x6664`).
	 *    4. Tail-jump to `~CAI_BaseNPCTroika` (`0x1028d610`).
	 *  Step 2 is the whole observable body: Andrei's blood pieces do not outlive him. */
	void DestroyAndreiBlood();

};
