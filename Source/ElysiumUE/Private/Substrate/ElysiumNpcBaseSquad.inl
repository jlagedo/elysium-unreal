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

/** `CAI_BaseNPC::FindCreateSquad(this, name)` (`0x10315800`) — the find-or-create half, and
 *  `FindSquad(name)` (`0x10315790`) when `bFindOnly`. No squad store exists, so it never finds and
 *  never creates: it answers null and `m_pSquad` stays unwritten. */
void* FindOrCreateSquad(const FString& InSquadName, bool bFindOnly) const;

/** `CAI_Squad::RemoveFromSquad(squad, this)` (`0x103158f0`) — compacts the member array and calls
 *  slot 578 on each survivor. No squad object: it does nothing. */
void RemoveFromSquad(void* Squad);

/** The gates `0x10273d30` and `0x10369bd0` share, with the join arm each takes. */
bool InitSquadLine(bool bCameraArm);

/** The private enemy memory `CAI_BaseNPCTroika::SetSquad` frees or re-points (`m_pEnemies`
 *  `+0x5d88`, `AI_Enemies` dtor `0x102e0730` + `operator delete`, fresh `AI_Enemies`
 *  `0x102e06f0`). The port carries one enemy memory per NPC and no squad memory to swap it for,
 *  so this seam records the retail ownership move and changes nothing. */
void RepointEnemyMemoryToSquad(void* Squad);
