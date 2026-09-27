// `CAI_BaseNPC`'s declarations of the `EntityChain` family (story 5 step 5),
// moved from `ElysiumNpcEntityChain*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseEntityChain.cpp`.

/** `0x101a6d00` — slot 580's BASE body (`CAI_BaseNPC`), `return &DAT_1090ff08`, as the port's typed
 *  slot-580 virtual; `FElysiumNpc` overrides it with the Troika's `0x101aa790` (story 5 step 5). The base answers a
 *  DIFFERENT `CAI_ClassScheduleIdSpace` from the Troika line's `&DAT_10924248`, and family
 *  Schedule's table has no row for it, so this is the row and the reading of it. */
virtual const FElysiumLocalIdSpace* ClassScheduleIdSpace() const;

/** This NPC's live `CAI_LocalIdSpace` for one category, out of the loaded corpus. Slot 580's own
 *  answer, and never null while a corpus is loaded. */
const FElysiumLocalIdSpace* IdSpace(EElysiumIdCategory Category) const;

/** `0x101a8930` — `CCineNPC::CanInterrupt`. `m_interruptable` (+0x5f90) set AND the resolved
 *  `m_hTargetEnt` (+0x5ce4) answering slot 158 `IsAlive`. A missing target is false, not true. */
bool CineCanInterrupt() const;

/** `CCineNPC::m_interruptable` (+0x5f90), read through the scripted-sequence entity that already
 *  stores the word. A missing or non-sequence owner answers false. */
bool CineIsInterruptable() const;

/** `0x102ea280` — the GLOBAL-to-LOCAL range translation slots 447 and 450 both forward into.
 *  A null space is retail's end-of-chain and answers -1. */
static int32 GlobalToLocalId(const FElysiumLocalIdSpace* Space, int32 GlobalId);

