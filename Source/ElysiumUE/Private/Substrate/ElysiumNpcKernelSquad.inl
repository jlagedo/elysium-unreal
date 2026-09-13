// Story 29c-1, family **Squad** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcKernelSquad.cpp` and the tests in
// `Tests/ElysiumNpcKernelSquadTests.cpp`. One file per family rather than 915 declarations
// appended to an already-oversized header: the family boundary is what this story ports by.

// --- The squad seam ------------------------------------------------------------------------------
//
// THERE IS NO SQUAD OBJECT IN THIS SUBSTRATE. `m_pSquad` (`+0x5da4`) is `ELYSIUM_NPC_WORD_ABSENT`
// in the shape map and `ConnectedSquad()` answers `nullptr` and says why (0002/17). Retail's
// `CAI_Squad` is decoded in `docs/vtmb/npc-ai/social.md` § "Squads, decoded (2026-09-08)" —
// 0x78 bytes, `+0x1c EHANDLE m_hMembers[16]`, `+0x5c m_nNumMembers`, `+0x64 m_squadSlotsUsed`.
//
// Every squad body this family ports asks through the accessors below. Each answers NOTHING and
// names the retail call it stands for, so a squad layer replaces the seam and not one of the
// recovered bodies. None of them invents a squad.

/** `CAI_BaseNPC::FindCreateSquad(this, name)` (`0x10315800`) — the find-or-create half, and
 *  `FindSquad(name)` (`0x10315790`) when `bFindOnly`. No squad store exists, so it never finds and
 *  never creates: it answers null and `m_pSquad` stays unwritten. */
void* FindOrCreateSquad(const FString& InSquadName, bool bFindOnly) const;

/** `CAI_Squad::RemoveFromSquad(squad, this)` (`0x103158f0`) — compacts the member array and calls
 *  slot 578 on each survivor. No squad object: it does nothing. */
void RemoveFromSquad(void* Squad);

/** `CAI_Squad::AddSelfToSquadMemory(squad, this)` (`0x10316720`) — `ReconnectToSquad`'s arm at a
 *  zero disconnect count. No squad object: it does nothing. */
void AddSelfToSquadMemory(void* Squad);

/** `CAI_Squad::NumMembers` (`0x103160a0`, reads `squad+0x5c`). Answers 0. */
int32 SquadMemberCount(const void* Squad) const;

/** `CAI_Squad::GetMember(i)` (`0x103160c0`, `squad+0x1c` handles, null for every index when member
 *  0 is disconnected). Answers null. */
FElysiumEntity* SquadMember(const void* Squad, int32 Index) const;

/** `m_squadSlotsUsed` (`CAI_Squad+0x64`; `VacateSquadSlot` indexes the `CVarBitVec`'s word array
 *  through `+0x6c`). Answers false / does nothing. */
bool IsSquadSlotOccupied(const void* Squad, int32 SquadSlot) const;
void ClearSquadSlotOccupied(void* Squad, int32 SquadSlot);

/** The private enemy memory `CAI_BaseNPCTroika::SetSquad` frees or re-points (`m_pEnemies`
 *  `+0x5d88`, `AI_Enemies` dtor `0x102e0730` + `operator delete`, fresh `AI_Enemies`
 *  `0x102e06f0`). The port carries one enemy memory per NPC and no squad memory to swap it for,
 *  so this seam records the retail ownership move and changes nothing. */
void RepointEnemyMemoryToSquad(void* Squad);

/** Retail's global `CAI_Hint` list (`DAT_10925450`, next link `+0x5d8`, `m_nHintType +0x5dc`),
 *  walked for the `Ordinal`-th node of `HintType`. This substrate has no hint-node store carrying
 *  hint types — `ScheduleHost.HintNode` is a bare index — so the walk answers null. */
FElysiumEntity* NthHintOfType(int32 HintType, int32 Ordinal) const;

// --- Species words this family's bodies read ------------------------------------------------------
//
// 29b declared every word of the flattened `CAI_BaseNPCTroika` layout; the 385 SPECIES words above
// `+0x665c` have no port member because one leaf carries every classname and the same offset means
// different things per species. These four are the ones this family's bodies read, declared by
// retail name with the species class that owns the offset.

int32 ChangType = 0;  // +0x66b8 CNPC_VChangBros::m_ChangType (datamap)
int32 CoordinateTentacleId = 0;  // +0x6740 CNPC_VMingXiao::m_iCoordinateTentacleID (datamap)
FElysiumEntityHandle Proxies[6];  // +0x668c CNPC_VMingXiao::m_rhProxies[6] (datamap)
FElysiumEntityHandle SeveredTentacles[6];  // +0x66a8 CNPC_VMingXiao::m_rhSeveredTentacles[6]

// --- Slot 546 `SquadSlotName`: one port method and a species table --------------------------------
//
// 57 retail bodies fill slot 546 — the Troika line's own (`0x101a6c00`) and 56 species overrides
// across 60 census classes — and all 57 are the SAME two-step: translate the squad-slot id through
// this class's `CAI_ClassScheduleIdSpace` (`0x102ea2d0 SquadSlotLocalToGlobal`), then look the
// global id up in the one shared squad-slot namespace `DAT_10936c74`
// (`0x102ea020 CAI_GlobalNamespace::IdToSymbol`). The Troika line skips the translation and looks
// `slotEN` up directly. So this is one method plus a data table keyed on the retail class name,
// not 57 methods; `ElysiumNpcKernelClass::OverrideOf(RetailClass(), 546)` picks the row.

/** One row of retail's slot-546 species table: the class, the body that fills the slot for it, and
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

/** The table: 61 rows — 60 census classes that override slot 546, plus the Troika line itself. */
static const FSquadSlotSpecies* SquadSlotSpeciesRows(int32& OutCount);

/** The row for a retail class name, or null when no row carries it. */
static const FSquadSlotSpecies* SquadSlotSpeciesOf(const TCHAR* InRetailClass);

/** `CAI_ClassScheduleIdSpace::SquadSlotLocalToGlobal` (`0x102ea2d0`): walk the id-space chain from
 *  `Species` upward and translate, or -1. A null `Species` is the Troika line, which does not
 *  translate and answers `LocalId` unchanged. */
static int32 SquadSlotLocalToGlobal(const FSquadSlotSpecies* Species, int32 LocalId);

/** `CAI_GlobalNamespace::IdToSymbol` over the one squad-slot namespace `DAT_10936c74`
 *  (`0x102ea020`), which `0x10316e80` seeds with exactly two symbols. `"<<null>>"` for -1, null for
 *  an id the namespace does not carry. */
static const TCHAR* GlobalSquadSlotName(int32 GlobalId);

// --- The bodies -----------------------------------------------------------------------------------

/** `CAI_BaseNPCTroika::SetSquad` (`0x1029a930`) — the `SQUAD` tweak param's squad move. */
void SetSquad(const FString& NewSquadName);

/** `0x102781a0` — do this NPC and `Other` answer to the same `CAI_Squad`? Retail name unrecovered. */
bool SharesSquadWith(const FElysiumNpc* Other) const;

/** `0x1028ae60` — release `m_iMySquadSlot` (`+0x5dac`) in the squad's slot bitmap. Retail name
 *  unrecovered; 29c named the port method. */
void VacateSquadSlot();

/** `CNPC_VChangBros::GetOtherBrother` (`0x1036e2f0`) — the paired brother, found by walking my
 *  squad for another `CNPC_VChangBros`. */
FElysiumNpc* GetOtherBrother() const;

/** `CNPC_VChangBros::ReadyForUnited` (`0x1036e820`) — am I running schedule `0x15a` or `0x15b`? */
bool ReadyForUnited() const;

/** `CNPC_VChangBros::SelectUnitedNode` (`0x1036d100`) — the hint node the twins meet at. */
FElysiumEntity* SelectUnitedNode() const;

/** `CNPC_VMingXiao::CoordinateTroops` (`0x10399610`) — one severed tentacle and one proxy per
 *  call, round-robin over six. */
void CoordinateTroops();

/** `0x102bf5d0` — tell one nearby ally about `Attacker`. 29c named the port method; the body is
 *  the detected-attack notice (`m_hDetectedAttacker +0x65c0`). */
void AlertNearbyAlly(FElysiumEntity* Attacker);

/** `0x103a48b0` — slot 404 `IRelationType`'s SPECIES body, filling `CNPC_VFrenzyShadow#404`,
 *  `CNPC_VPlayerController#404` and `CNPC_VWolfMorph#404`. Not the slot itself: slot 404's
 *  Troika-line body (`0x10299da0`, layer 14) is story 29d's and is still a generated stub, so this
 *  species arm lands under its own name until 29d's body can route to it.
 *  Returns retail `Disposition_t`: 0 `D_ER`, 1 `D_HT`, 2 `D_FR`, 3 `D_LI`, 4 `D_NU`. */
int32 SpeciesIRelationType(const FElysiumEntity* Candidate) const;

/** `0x102c4470` — adopt `Boss` as the follower boss by name (`!player` for the player, else its
 *  targetname) and store the key in `m_sFollowerBoss` (`+0x6478`).
 *  NAMED `SetFollowerBossName`, not 29c's `FollowerBossName`: that name is already the `+0x6478`
 *  data member 29b declared. */
void SetFollowerBossName(const FElysiumEntity* Boss);

/** `SetFollowerType` (`0x102c4640`) and the distance resolve/clamp it calls (`0x102c4680`) —
 *  `Rules.txt` `Npc_Follower_Info`, then `walkTo >= backAway + 10`, `runTo >= walkTo + 10`. */
void SetFollowerType(const FString& NewFollowerType);

/** `0x101a8130` — the named-master lookup. Largely UNRECOVERED; see the definition. */
FElysiumEntity* ResolveNamedMaster() const;
