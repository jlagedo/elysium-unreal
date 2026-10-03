// Story 29c-1, family **Squad** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcSquad.cpp` and the tests in
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

/** `CAI_Squad::NumMembers` (`0x103160a0`, reads `squad+0x5c`). Answers 0. */
int32 SquadMemberCount(const void* Squad) const;

/** `CAI_Squad::GetMember(i)` (`0x103160c0`, `squad+0x1c` handles, null for every index when member
 *  0 is disconnected). Answers null. */
FElysiumEntity* SquadMember(const void* Squad, int32 Index) const;

/** Retail's global `CAI_Hint` list (`DAT_10925450`, next link `+0x5d8`, `m_nHintType +0x5dc`),
 *  walked head first (`FElysiumEntityWorld::HintList`) for the `Ordinal`-th hint of `HintType`
 *  (0-based); the type is the only gate (`0x1036d100` / `0x1036e400`). Answers the hint entity, or
 *  null off the end. Not one of the kernel "answers nothing" seams above: it is live. */
FElysiumEntity* NthHintOfType(int32 HintType, int32 Ordinal) const;

// --- Species words this family's bodies read ------------------------------------------------------
//
// 29b declared every word of the flattened `CAI_BaseNPCTroika` layout; the 385 SPECIES words above
// `+0x665c` have no port member because one leaf carries every classname and the same offset means
// different things per species. These four are the ones this family's bodies read, declared by
// retail name with the species class that owns the offset.

// --- Slot 546 `SquadSlotName` ------------------------------------------------------------------
//
// 57 retail bodies fill slot 546 — the Troika line's own (`0x101a6c00`) and 56 species overrides
// across 60 census classes — and all 57 are the SAME two-step: translate the squad-slot id through
// this class's `CAI_ClassScheduleIdSpace` (`0x102ea2d0 SquadSlotLocalToGlobal`), then look the
// global id up in the one shared squad-slot namespace `DAT_10936c74`
// (`0x102ea020 CAI_GlobalNamespace::IdToSymbol`). The Troika line skips the translation and looks
// `slotEN` up directly. Every one of the 57 bodies is a closed `dead` row (0019/6): the name
// only reaches a debug print, so `FElysiumNpcBase::SquadSlotName` answers the default and the
// species overrides are gone.

// --- The bodies -----------------------------------------------------------------------------------

/** `CAI_BaseNPCTroika::SetSquad` (`0x1029a930`) — the `SQUAD` tweak param's squad move. */
void SetSquad(const FString& NewSquadName);

/** `0x102bf5d0` — tell one nearby ally about `Attacker`. 29c named the port method; the body is
 *  the detected-attack notice (`m_hDetectedAttacker +0x65c0`). */
void AlertNearbyAlly(FElysiumEntity* Attacker);


/** `0x102c4470` — adopt `Boss` as the follower boss by name (`!player` for the player, else its
 *  targetname) and store the key in `m_sFollowerBoss` (`+0x6478`).
 *  NAMED `SetFollowerBossName`, not 29c's `FollowerBossName`: that name is already the `+0x6478`
 *  data member 29b declared. */
void SetFollowerBossName(const FElysiumEntity* Boss);

/** `SetFollowerType` (`0x102c4640`) and the distance resolve/clamp it calls (`0x102c4680`) —
 *  `Rules.txt` `Npc_Follower_Info`, then `walkTo >= backAway + 10`, `runTo >= walkTo + 10`. */
void SetFollowerType(const FString& NewFollowerType);

