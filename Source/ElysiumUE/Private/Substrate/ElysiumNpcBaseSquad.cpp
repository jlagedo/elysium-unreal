// `CAI_BaseNPC`'s bodies of the `Squad` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseSquad.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSquadShared.h"

// --- File-scope helpers moved with the base bodies (story 5 step 5) ---

namespace
{
	// `bits_CAP_SQUAD`, the `CapabilitiesGet()` bit `InitSquad` gates on (`0x10273d30`).
	constexpr int32 GNpcKernelSquadCapSquad = 0x4000000;
	// The two symbols `0x10316e80` registers into the one squad-slot namespace `DAT_10936c74`, with
	// the ids it registers them under. They are the ONLY squad-slot names in `vampire.dll`.
	constexpr int32 GNpcKernelSquadSlotAttack1 = 1000000000;  // 0x3b9aca00
	constexpr int32 GNpcKernelSquadSlotAttack2 = 1000000001;  // 0x3b9aca01
	// `CAI_LocalIdSpace`'s "this space holds no ids" sentinel, tested by name in `0x102ea2d0`.
	constexpr int32 GNpcKernelSquadEmptyIdSpace = 9999;
	// The root `CAI_ClassScheduleIdSpace` (`DAT_10920484`), the only one in the image constructed
	// with `isRoot = true` (`staticinit_10265660` → `0x102ea090('\x01')`): global base 0, local base
	// 0, local top -1. A top of -1 matches no id, so the chain walk falls off the end and answers
	// -1 for every species. Declared as a row so the walk below is the retail walk and not a
	// hard-coded refusal.
	constexpr FElysiumNpcBase::FSquadSlotSpecies GNpcKernelSquadRootIdSpace = {
		TEXT("CAI_BaseNPC"), TEXT(""), TEXT("0x10920484"), 0, 0, INDEX_NONE };
}

// --- Moved from `ElysiumNpcSquad.cpp` (story 5 step 5) ---

void FElysiumNpcBase::AddSelfToSquadMemory(void* Squad)
{
	// SEAM for `CAI_Squad::AddSelfToSquadMemory` (`0x10316720`), the arm `ReconnectToSquad`
	// (`0x1026d0c0`) takes when the disconnect count reaches zero.
	(void)Squad;
}

bool FElysiumNpcBase::IsSquadSlotOccupied(const void* Squad, int32 SquadSlot) const
{
	// SEAM for `m_squadSlotsUsed` (`CAI_Squad+0x64`, the `CVarBitVec` whose word array hangs off
	// `+0x6c`). `docs/vtmb/npc-ai/social.md`: "Strategy slots ship dead" — retail itself has no
	// `OccupyStrategySlot`, so nothing ever sets a bit.
	(void)Squad;
	(void)SquadSlot;
	return false;
}

void FElysiumNpcBase::ClearSquadSlotOccupied(void* Squad, int32 SquadSlot)
{
	// SEAM: the write half of the bitmap above.
	(void)Squad;
	(void)SquadSlot;
}

// slot 546 0x101a6c00 `const char* SquadSlotName(int)`. The species classes override it with their
// own id-space row (story 5 step 4; the controller line's `CNPC_VFrenzyShadow` `0x10375440` and
// `CNPC_VWolfMorph` `0x103dc950` since fold A2, `CNPC_VPlayerController` inheriting
// `CNPC_VVampire`'s `0x103c4a80`); the rest inherit one of those. The table arm below answers the
// Troika line alone.
const TCHAR* FElysiumNpcBase::SquadSlotName(int32 SlotEn)
{
	const FElysiumNpcClassSlot* Override = ElysiumNpcKernelClass::OverrideOf(RetailClass(), 546);
	const FSquadSlotSpecies* Species =
		Override != nullptr ? SquadSlotSpeciesOf(Override->Class) : nullptr;
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(Species, SlotEn));
}

// slot 545 0x10273d30 `bool InitSquad()`. `CNPC_VCamera` (`0x10369bd0`, inherited by
// `CNPC_VCameraSecurity`) overrides it on its C++ class (story 5 step 3); the two bodies share every
// gate and differ only in the join arm, so both run `InitSquadLine`.
bool FElysiumNpcBase::InitSquad()
{
	return InitSquadLine(/*bCameraArm*/ false);
}

bool FElysiumNpcBase::SharesSquadWith(const FElysiumNpc* Other) const
{
	// 0x102781a0: null other -> false; my squad null -> false (even when the other's is also
	// null); else `m_pSquad == other->m_pSquad`.
	if (Other == nullptr)
	{
		return false;
	}
	const void* MySquad = ConnectedSquad();
	if (MySquad == nullptr)
	{
		return false;
	}
	return MySquad == Other->ConnectedSquad();
}

void FElysiumNpcBase::VacateSquadSlot()
{
	// 0x1028ae60. Gates: `m_iMySquadSlot != -1`, `m_iSquadDisconnected < 1`, `m_pSquad != 0`.
	// Then it reads the squad's `m_squadSlotsUsed` word for the slot and DevMsgs
	// `"ERROR: Vacating an empty slot!"` when the bit is already clear, re-reads the squad under
	// the same disconnect test (retail tests it twice; the second read yields NULL when
	// disconnected, which would fault — it cannot be reached because the first gate already
	// required `< 1`), clears the bit and writes `m_iMySquadSlot = -1`.
	//
	// NOTE: this runtime declares `MySquadSlot = 0`, not retail's -1 sentinel. Nothing writes the
	// field — `docs/vtmb/npc-ai/social.md`: "zero code readers, save-only" — and the squad gate
	// below refuses first, so the difference is unobservable today. Reported so 29b's default can
	// be corrected with the shape map.
	if (MySquadSlot == INDEX_NONE || BaseScheduleHost.SquadDisconnected >= 1)
	{
		return;
	}
	void* Squad = const_cast<void*>(ConnectedSquad());
	if (Squad == nullptr)
	{
		return;
	}
	if (!IsSquadSlotOccupied(Squad, MySquadSlot))
	{
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("ERROR: Vacating an empty slot!"));
	}
	ClearSquadSlotOccupied(Squad, MySquadSlot);
	MySquadSlot = INDEX_NONE;
}

// --- Moved from `ElysiumNpcSquad.cpp` (story 5 step 5) ---

void* FElysiumNpcBase::FindOrCreateSquad(const FString& InSquadName, bool bFindOnly) const
{
	// `FindCreateSquad` `0x10315800` / `FindSquad` `0x10315790`: a `strcmpi` walk of the global
	// squad list `g_pSquadList` (`0x10936c68`), then `new CAI_Squad(name)` on a miss, with a 17th
	// recruit DevMsg'd (`"Error!! Squad %s is too big!!!"`) and overwriting member 16.
	//
	// SEAM: no squad list, no `CAI_Squad`. Answers nothing for retail's `m_pSquad` (`+0x5da4`).
	(void)InSquadName;
	(void)bFindOnly;
	return nullptr;
}

void FElysiumNpcBase::RemoveFromSquad(void* Squad)
{
	// SEAM for `CAI_Squad::RemoveFromSquad` (`0x103158f0`): compacts `m_hMembers` and calls
	// slot 578 (an empty virtual) on each survivor.
	(void)Squad;
}

const FElysiumNpcBase::FSquadSlotSpecies* FElysiumNpcBase::SquadSlotSpeciesOf(const TCHAR* InRetailClass)
{
	if (InRetailClass == nullptr)
	{
		return nullptr;
	}
	for (const FSquadSlotSpecies& Row : NpcKernelSquadShared::GNpcKernelSquadSlotSpecies)
	{
		if (FCString::Strcmp(Row.RetailClass, InRetailClass) == 0)
		{
			return &Row;
		}
	}
	return nullptr;
}

int32 FElysiumNpcBase::SquadSlotLocalToGlobal(const FSquadSlotSpecies* Species, int32 LocalId)
{
	// 0x102ea2d0 `CAI_ClassScheduleIdSpace::SquadSlotLocalToGlobal`, arm for arm:
	//
	//   if (id == -1) return -1;
	//   do {
	//     if (localBase != 9999 && localBase <= id && id <= localTop)
	//       return (globalBase - localBase) + id;
	//     space = space->parent;
	//   } while (space);
	//   return -1;
	//
	// A null `Species` is the Troika line (`0x101a6c00`), which performs no translation at all and
	// hands `slotEN` straight to `IdToSymbol`.
	if (Species == nullptr || Species->IdSpace == nullptr || *Species->IdSpace == TEXT('\0'))
	{
		return LocalId;
	}
	if (LocalId == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	// The chain: this class's space, then its parent's, up to the root. Retail's parent link is
	// per-class (`Init`'s third argument), and every level of every chain carries the same empty
	// range, so the walk is modelled as species → root.
	const FSquadSlotSpecies* Chain[2] = { Species, &GNpcKernelSquadRootIdSpace };
	for (const FSquadSlotSpecies* Space : Chain)
	{
		if (Space->LocalBase != GNpcKernelSquadEmptyIdSpace && Space->LocalBase <= LocalId
			&& LocalId <= Space->LocalTop)
		{
			return (Space->GlobalBase - Space->LocalBase) + LocalId;
		}
	}
	return INDEX_NONE;
}

const TCHAR* FElysiumNpcBase::GlobalSquadSlotName(int32 GlobalId)
{
	// 0x102ea020 `CAI_GlobalNamespace::IdToSymbol`: -1 answers the literal `"<<null>>"`, anything
	// else is looked up in the symbol table and answers NULL when it is not there
	// (`0x10249c70`). `0x10316e80` puts exactly two symbols in this table.
	if (GlobalId == INDEX_NONE)
	{
		return TEXT("<<null>>");
	}
	if (GlobalId == GNpcKernelSquadSlotAttack1)
	{
		return TEXT("SQUAD_SLOT_ATTACK1");
	}
	if (GlobalId == GNpcKernelSquadSlotAttack2)
	{
		return TEXT("SQUAD_SLOT_ATTACK2");
	}
	return nullptr;
}

bool FElysiumNpcBase::InitSquadLine(bool bCameraArm)
{
	if (ConnectedSquad() == nullptr)
	{
		// `CapabilitiesGet()` is slot 513 (`0x1026db30`) and is still a generated stub, so
		// `bits_CAP_SQUAD` never reads set and this body stops here. That refusal IS the seam:
		// asking the capability is retail's first gate and the port asks it.
		if ((CapabilitiesGet() & GNpcKernelSquadCapSquad) != 0)
		{
			if (SquadName.IsEmpty())
			{
				UE_LOG(LogElysiumNpcEnt, Warning,
					TEXT("WARNING: Found %s that isn't in a squad but not supposed to be solo"),
					*DebugString());
				return ConnectedSquad() != nullptr;
			}
			// The one difference between the two bodies: `CNPC_VCamera`'s `0x10369bd0` against the
			// Troika line's `0x10273d30`.
			if (bCameraArm)
			{
				// 0x10369bd0: join an EXISTING squad by name; on a miss create one and then
				// immediately remove yourself from it — the camera wants the squad object for the
				// shared memory, not membership. Verbatim, odd as it reads.
				void* Squad = FindOrCreateSquad(SquadName, /*bFindOnly=*/true);
				if (Squad == nullptr)
				{
					Squad = FindOrCreateSquad(SquadName, /*bFindOnly=*/false);
					if (Squad != nullptr)
					{
						RemoveFromSquad(Squad);
					}
				}
			}
			else
			{
				// 0x10273d30: find-or-create and join.
				FindOrCreateSquad(SquadName, /*bFindOnly=*/false);
			}
			// Slot 542 `SetSquadEnemies` (`0x10273dd0`) — delete the private memory and point
			// `m_pEnemies` at the squad's. Called on both arms.
			Slot542();
		}
	}
	return ConnectedSquad() != nullptr;
}

// --- Moved from `ElysiumNpcSquad.cpp` (story 5 step 5) ---

void FElysiumNpcBase::RepointEnemyMemoryToSquad(void* Squad)
{
	// SEAM for the ownership move `SetSquad` (`0x1029a930`) and `SetSquadEnemies` (slot 542,
	// `0x10273dd0`) perform: delete the private `AI_Enemies` and point `m_pEnemies` (`+0x5d88`) at
	// `squad+8`, the squad's embedded memory. This runtime carries one enemy memory per NPC
	// (`FElysiumNpcBase::EnemyMemory`) and has no squad memory to point it at, so the ownership move is
	// recorded here and changes nothing.
	(void)Squad;
}
