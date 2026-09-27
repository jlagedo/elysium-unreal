// `CAI_BaseNPC`'s declarations of the `Squad` family (story 5 step 5),
// moved from `ElysiumNpcSquad*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseSquad.cpp`.

/** `CAI_Squad::AddSelfToSquadMemory(squad, this)` (`0x10316720`) — `ReconnectToSquad`'s arm at a
 *  zero disconnect count. No squad object: it does nothing. */
void AddSelfToSquadMemory(void* Squad);

/** `m_squadSlotsUsed` (`CAI_Squad+0x64`; `VacateSquadSlot` indexes the `CVarBitVec`'s word array
 *  through `+0x6c`). Answers false / does nothing. */
bool IsSquadSlotOccupied(const void* Squad, int32 SquadSlot) const;

void ClearSquadSlotOccupied(void* Squad, int32 SquadSlot);

/** `0x102781a0` — do this NPC and `Other` answer to the same `CAI_Squad`? Retail name unrecovered. */
bool SharesSquadWith(const FElysiumNpc* Other) const;

/** `0x1028ae60` — release `m_iMySquadSlot` (`+0x5dac`) in the squad's slot bitmap. Retail name
 *  unrecovered; 29c named the port method. */
void VacateSquadSlot();

/** One slot-546 id-space row: the class, the body that fills the slot for it, and
 *  the `CAI_ClassScheduleIdSpace` that body translates through. The id-space fields are the state
 *  the static constructor left (`0x102ea090`) — every one of the 56 species spaces is constructed
 *  with `isRoot = false` and no class in the image ever registers a squad slot, so `LocalBase`
 *  keeps the 9999 "empty" sentinel and the translation answers -1. */
struct FSquadSlotSpecies
{
	// The census class this row came from, `CNPC_VSabbatLeader` (`docs/vtmb/npc-kernel/slots.md`).
	const TCHAR* RetailClass = nullptr;
	// The retail body that fills slot 546 for it, `0x10……`; checkable against `slots.md`.
	const TCHAR* Body = nullptr;
	// The species `CAI_ClassScheduleIdSpace` global the body translates through, `0x10……`. Empty
	// for the Troika line, which performs no translation at all.
	const TCHAR* IdSpace = nullptr;
	// `CAI_LocalIdSpace` `+0x00 m_globalBase`, `+0x04 m_localBase`, `+0x08 m_localTop`. 9999 in
	// `LocalBase` is retail's "this space holds no ids" sentinel (`0x102ea2d0` tests it by name).
	int32 GlobalBase = INDEX_NONE;
	int32 LocalBase = 9999;
	int32 LocalTop = INDEX_NONE;
};

/** `CAI_BaseNPC::FindCreateSquad(this, name)` (`0x10315800`) — the find-or-create half, and
 *  `FindSquad(name)` (`0x10315790`) when `bFindOnly`. No squad store exists, so it never finds and
 *  never creates: it answers null and `m_pSquad` stays unwritten. */
void* FindOrCreateSquad(const FString& InSquadName, bool bFindOnly) const;

/** `CAI_Squad::RemoveFromSquad(squad, this)` (`0x103158f0`) — compacts the member array and calls
 *  slot 578 on each survivor. No squad object: it does nothing. */
void RemoveFromSquad(void* Squad);

/** The row for a retail class name, or null when no row carries it. */
static const FSquadSlotSpecies* SquadSlotSpeciesOf(const TCHAR* InRetailClass);

/** The gates `0x10273d30` and `0x10369bd0` share, with the join arm each takes. */
bool InitSquadLine(bool bCameraArm);

/** `CAI_ClassScheduleIdSpace::SquadSlotLocalToGlobal` (`0x102ea2d0`): walk the id-space chain from
 *  `Species` upward and translate, or -1. A null `Species` is the Troika line, which does not
 *  translate and answers `LocalId` unchanged. */
static int32 SquadSlotLocalToGlobal(const FSquadSlotSpecies* Species, int32 LocalId);

/** `CAI_GlobalNamespace::IdToSymbol` over the one squad-slot namespace `DAT_10936c74`
 *  (`0x102ea020`), which `0x10316e80` seeds with exactly two symbols. `"<<null>>"` for -1, null for
 *  an id the namespace does not carry. */
static const TCHAR* GlobalSquadSlotName(int32 GlobalId);

/** The private enemy memory `CAI_BaseNPCTroika::SetSquad` frees or re-points (`m_pEnemies`
 *  `+0x5d88`, `AI_Enemies` dtor `0x102e0730` + `operator delete`, fresh `AI_Enemies`
 *  `0x102e06f0`). The port carries one enemy memory per NPC and no squad memory to swap it for,
 *  so this seam records the retail ownership move and changes nothing. */
void RepointEnemyMemoryToSquad(void* Squad);
