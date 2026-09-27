#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"

// Story 29c-1, family **Species** — the per-species bodies of layers 0–9. This file carries slot
// 323, the species slot table and its dispatchers, the two `CUtlVector<{EHANDLE, expiry}>` stores
// (`CNPC_VBaseBoss`'s and `CNPC_VTzimisce`'s) and the melee quartet's species replacements (slots
// 599, 600, 601, 602) plus slots 606 and 609. Everything else — `CNPC_VNewscaster`,
// `CNPC_VTzimisce`'s carry chain, `CNPC_VWerewolf`, `CNPC_VZombie`, `CNPC_VMingXiaoTentacle`,
// `CNPC_VCamera` and `CNPC_VAndreiBlood` — is
// `Substrate/ElysiumNpcSpecies2.cpp`. The declarations and this family's three standing facts
// are `Substrate/ElysiumNpcSpecies.inl`; the walked prose is `docs/vtmb/npc-ai/shape.md`.
//
// **Every `.rdata` constant in this family was READ**, not inferred: the pinned retail
// `vampire.dll`'s `.rdata` was addressed directly (image base `0x10000000`, section VA
// `0x10445000`, raw `0x445000`) and each cell's little-endian float is quoted beside its address
// below. Where a datum lives in `.data` and is filled at runtime it is a seam and says so. Nothing
// here is a guess dressed as a number.

namespace
{
	// --- Retail `.rdata`, one line per cell, each value read out of the pinned image ---------------

	constexpr float SpeciesOne = ElysiumNpcTunables::One;

}

// -------------------------------------------------------------------------------------------------
// The species slot table — this family's whole dispatch surface.
// -------------------------------------------------------------------------------------------------

const FElysiumNpc::FSpeciesSlotRow* FElysiumNpc::SpeciesSlotRows(int32& OutCount)
{
	// One row per (class, slot) this family ports, each carrying the RETAIL ADDRESS of the body so
	// the table is checkable against `docs/vtmb/npc-kernel/slots.md` by eye. Ordered by slot then
	// class; every row is exercised by name in `Elysium.Substrate.NpcKernelSpecies.SlotTable`.
	static constexpr FSpeciesSlotRow Rows[] =
	{
		// Story 5 step 3: every introduced class overrides its slots on its own C++ class
		// (`story-5/overrides-step3.tsv`). What remains are the deferred classes' rows, reached by a
		// test-latched Troika-line instance or read as census until their folds
		// (`decisions-step3.json` `surviving_sites`).
		// Slot 139 `DeathNotice` — `CNPCMaker_Fleshpile`'s, which lands on `FElysiumNpcMaker` (step 8).
		{ TEXT("CNPCMaker_Fleshpile"), 139, TEXT("0x1034c8e0") },
		// Slots 599 / 600 — `CNPC_VFrenzyShadow`'s melee entry, until the controller fold (step 7).
		{ TEXT("CNPC_VFrenzyShadow"), 599, TEXT("0x10376b70") },
		{ TEXT("CNPC_VFrenzyShadow"), 600, TEXT("0x10376ba0") },
		// Slot 617 `MakeNPC` — `CNPCMaker_Fleshpile`'s, which lands on `FElysiumNpcMaker` (step 8).
		{ TEXT("CNPCMaker_Fleshpile"), 617, TEXT("0x1034c2d0") },
	};
	OutCount = UE_ARRAY_COUNT(Rows);
	return Rows;
}

const FElysiumNpc::FSpeciesSlotRow* FElysiumNpc::SpeciesSlotRowOf(const TCHAR* InRetailClass,
	int32 Slot)
{
	if (InRetailClass == nullptr)
	{
		return nullptr;
	}
	int32 Count = 0;
	const FSpeciesSlotRow* Rows = SpeciesSlotRows(Count);
	// The exact class: the deferred classes left here have no census subclass that inherits a row.
	for (int32 i = 0; i < Count; ++i)
	{
		if (Rows[i].Slot == Slot && FCString::Strcmp(Rows[i].RetailClass, InRetailClass) == 0)
		{
			return &Rows[i];
		}
	}
	return nullptr;
}

const FElysiumNpc::FSpeciesSlotRow* FElysiumNpc::SpeciesSlotRow(int32 Slot) const
{
	// `RetailClass()` is NULL only on the bare Troika line; the fall-through to "no species body,
	// run the one you have" is its answer.
	const FElysiumNpcClass* Cls = RetailClass();
	return SpeciesSlotRowOf(Cls != nullptr ? Cls->Name : nullptr, Slot);
}

#if WITH_DEV_AUTOMATION_TESTS
void FElysiumNpc::SetRetailClassForTests(const TCHAR* RetailClassName)
{
	// Story 5 step 2: every introduced class is spawned by its own factory, so this latch stands
	// only a deferred class (steps 7-10) on a bare Troika-line instance -- never on a species class,
	// whose C++ type already answers.
	check(OwnRetailClass() == nullptr);
	bRetailClassForTests = true;
	RetailClassForTests = ElysiumNpcKernelClass::Find(RetailClassName);
}
#endif

// -------------------------------------------------------------------------------------------------
// Slot 599 — the four species replacements of `CAI_BaseNPCTroika::FUN_102b5650`.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::FUN_10376b70(FElysiumEntity* Enemy)
{
	// `0x10376b70`, `CNPC_VFrenzyShadow`'s slot 599, the WHOLE body:
	//     (*DAT_10924edc)->vfunc1();      // the global melee-entered event, FIRST
	//     m_bInMelee = 1;
	//     return <whatever was in EAX>;
	//
	// Twenty-six bytes. It drops EVERY gate the Troika line has — the frenzied bits, the melee-enter
	// timer, the range and height terms and the attack coordinator — so a frenzy shadow enters melee
	// unconditionally and can never be refused. The declared return is `void`; the caller reads the
	// register the event call left behind, which is not a decision this body makes. Slot 599's port
	// signature answers `bool`, and **true** is the only honest reading: `m_bInMelee` was set, which
	// is what "entered melee" means to every caller of slot 599.
	//
	// `Enemy` is read by nothing in the body — retail takes it and ignores it, exactly as the Troika
	// line does.
	(void)Enemy;
	++MeleeEventFires;   // `(*DAT_10924edc)->vfunc1()`, family Bosses' counter for this global
	bInMelee = true;
	return true;
}

bool FElysiumNpc::SpeciesSlot599(FElysiumEntity* Enemy, bool& OutAnswer)
{
	// Story 5 step 3: the introduced classes (`CNPC_VGargoyle`, `CNPC_VTzimisceHeadClaw`,
	// `CNPC_VTzimisceRunner`) override slot 599 on their C++ classes. What is left is the deferred
	// `CNPC_VFrenzyShadow` row, reached only by a test-latched Troika-line instance until the
	// controller line folds (step 7, `decisions-step3.json` `surviving_sites`).
	const FSpeciesSlotRow* Row = SpeciesSlotRow(599);
	if (Row == nullptr || FCString::Strcmp(Row->Address, TEXT("0x10376b70")) != 0)
	{
		return false;   // no species body: run the Troika line
	}
	OutAnswer = FUN_10376b70(Enemy);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 600 — the five species replacements of `FUN_102b57c0`.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::FUN_10376ba0(FElysiumEntity* Enemy)
{
	// `0x10376ba0`, `CNPC_VFrenzyShadow`'s slot 600, twenty-three bytes:
	//     m_bInMelee = 1;
	//     (*DAT_10924edc)->vfunc1();
	//     return true;
	//
	// **The write comes FIRST here and SECOND in slot 599** (`0x10376b70`). Nothing observes the
	// order — the event reaches no NPC word — but it is the bodies' own order and is kept.
	//
	// As with 599, every gate is gone: no weapon capability test, no `m_bInMelee` re-entry guard,
	// no coordinator. A frenzy shadow always accepts.
	(void)Enemy;
	bInMelee = true;
	++MeleeEventFires;
	return true;
}

bool FElysiumNpc::SpeciesSlot600(FElysiumEntity* Enemy, bool& OutAnswer)
{
	// The deferred `CNPC_VFrenzyShadow` row only, as slot 599 above.
	const FSpeciesSlotRow* Row = SpeciesSlotRow(600);
	if (Row == nullptr || FCString::Strcmp(Row->Address, TEXT("0x10376ba0")) != 0)
	{
		return false;
	}
	OutAnswer = FUN_10376ba0(Enemy);
	return true;
}
