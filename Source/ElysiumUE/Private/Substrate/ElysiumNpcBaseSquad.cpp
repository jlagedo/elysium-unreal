// `CAI_BaseNPC`'s bodies of the `Squad` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseSquad.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

// --- File-scope helpers moved with the base bodies (story 5 step 5) ---

namespace
{
	// `bits_CAP_SQUAD`, the `CapabilitiesGet()` bit `InitSquad` gates on (`0x10273d30`).
	constexpr int32 GNpcKernelSquadCapSquad = 0x4000000;
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
