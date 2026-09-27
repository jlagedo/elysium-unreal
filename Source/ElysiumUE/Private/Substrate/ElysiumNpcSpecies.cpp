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
		// (`story-5/overrides-step3.tsv`); fold A2 moved `CNPC_VFrenzyShadow`'s 599/600 onto
		// `FElysiumNpcFrenzyShadow`. What remains is the Fleshpile maker's pair, read as census until
		// the maker fold (A4).
		// Slot 139 `DeathNotice` — `CNPCMaker_Fleshpile`'s, which lands on `FElysiumNpcMaker` (step 8).
		{ TEXT("CNPCMaker_Fleshpile"), 139, TEXT("0x1034c8e0") },
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

