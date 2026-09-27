#include "Substrate/ElysiumNpcWolfMorph.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumSchedule.h"

// `CNPC_VWolfMorph` — story 5 fold A2. Every body is the retail body at the address its comment
// names, read off the listing (`vtmb_asm`).

namespace
{
	// `SelectSchedule` `0x103dceb0`: the name it compares the running schedule against
	// (`0x10663d18`) and the schedule it answers otherwise, `0x158` — local 344
	// `SCHED_VWOLFMORPH_MORPH` in the class's own space (`0x109402a8`, corpus unit
	// `cnpc_vwolfmorph`). Local 344 names a DIFFERENT schedule in `CNPC_VFrenzyShadow`'s space.
	const TCHAR* const GWolfMorphSchedMorphName = TEXT("SCHED_VWOLFMORPH_MORPH");
	constexpr int32 GWolfMorphSchedMorph = 0x158;
	// `NPCInit` `0x103dce00`: `+0x5cc4 = 0xc`, the `NPC_VWolfMorph.cpp:0x67` trace.
	constexpr int32 GWolfMorphRetailState = 0xc;
	constexpr int32 GWolfMorphNpcInitLine = 0x67;
}

const FElysiumNpcClass* FElysiumNpcWolfMorph::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(RetailClassName);
	return Row;
}

// Slot 138: `0x103dce50`.
int32 FElysiumNpcWolfMorph::Classify()
{
	return 2;
}

// Slot 375: `0x103dcdc0`.
int32 FElysiumNpcWolfMorph::NPC_EarlyTranslateActivity(int32 Activity)
{
	// `MOV EAX,0x1145; RET 4` — the request is not read. The visual half is the generated
	// `PreTranslate_WolfMorph` table (`ElysiumNpcActivityTables.cpp`), which cites the same address
	// and rewrites every request to `ACT_WOLF_MORPH`; the class row `npc_VWolfMorph` resolves to it.
	(void)Activity;
	return ActWolfMorph;
}

// Slot 420: `0x103dce00`.
void FElysiumNpcWolfMorph::NPCInit()
{
	// In retail's order:
	//   1. `CALL 0x100155aa` -> the controller's `NPCInit` `0x103a4580`, DIRECT;
	FElysiumNpcPlayerController::NPCInit();
	//   2. trace `NPC_VWolfMorph.cpp:0x67` into `+0x1b3c` / `+0x1b40`;
	RecordScheduleEvent(FString::Printf(TEXT("NPCInit trace NPC_VWolfMorph.cpp:%d"), GWolfMorphNpcInitLine));
	//   3. `+0x5cc4 = 0xc` — the ideal state, written RAW (retail state 0xc has no typed name);
	WriteIdealStateRetail(GWolfMorphRetailState);
	//   4. slot 310 `SetActivity(0x1145)` through `vt+0x4d8` — VIRTUAL.
	SetActivity(ActWolfMorph);
}

// Slot 438: `0x103dceb0`.
int32 FElysiumNpcWolfMorph::SpeciesSelectSchedule()
{
	// `m_pSchedule (+0x5c38)` non-null AND `stricmp(m_pSchedule->m_pszName (+0x40),
	// "SCHED_VWOLFMORPH_MORPH") == 0` -> tail `JMP` into `CNPC_VHuman::SelectSchedule` `0x10384ee0`;
	// otherwise answer `0x158`. So the wolf morphs first, and only a body already running the morph
	// selects normally. `0x10384ee0` has no port body (the census's unported `CNPC_VHuman#438`): the
	// port's selector runs its base branch for every VHuman, which answering 0 hands back to.
	const TCHAR* const Running =
		Schedule.Current != ElysiumScheduleId::None ? ElysiumScheduleName(Schedule.Current) : nullptr;
	if (Running != nullptr && FCString::Stricmp(Running, GWolfMorphSchedMorphName) == 0)
	{
		return 0;
	}
	return GWolfMorphSchedMorph;
}

// Slot 546: `0x103dc950`, the class's own squad-slot id space.
const TCHAR* FElysiumNpcWolfMorph::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1094028c`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VWolfMorph"), TEXT("0x103dc950"), TEXT("0x1094028c") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}

// Slot 588: `0x103dcf00`.
void FElysiumNpcWolfMorph::Slot588()
{
	// A bare `RET`: it suppresses the Troika body `0x10293e50`'s `ACT_DISPOSITION` restart, so the
	// wolf keeps playing the morph.
}
