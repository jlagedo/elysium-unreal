// Story 29e, family **State19** — the declarations of this family's layer 19–29 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual and this family only defines it. What lands here is
// the non-slot half — `SetState`, the `CAI_BaseNPC` bodies beneath Troika overrides, the species
// arms the slot dispatchers run, and the seams that stand for retail inputs this substrate has no
// source for.
//
// The definitions are in `Substrate/ElysiumNpcState19.cpp` (the Troika line and `SetState`)
// and `ElysiumNpcState19_2.cpp` (the species line); the tests are
// `Tests/ElysiumNpcKernelState19Tests.cpp`. The walked prose is `docs/vtmb/npc-ai/conditions-and-states.md`
// § "Story 29e, family State19 — …".
//
// THREE STANDING FACTS OF THIS FAMILY, stated once here rather than at thirty call sites.
//
//   * **Troika `SelectIdealState` (`0x102ad660`) gates every arm on `HasInterruptCondition`
//     (`0x10269d30`), which answers 0 when `+0x5c38` holds no schedule.** The four flee arms
//     (`0x21` / `0x1f`, idle and alert) and case `0xe`'s three damage arms use the bare
//     `HasCondition`. A committed enemy with no program installed therefore answers nothing.
//   * **`SetState` (`0x1026e340`) writes BOTH `+0x5cc0` and `+0x5cc4`, and slot 463 is dispatched
//     with the value read at ENTRY** — the pre-write state is re-read after the `SetEnemy(NULL)`
//     strip to decide whether to dispatch, but the arguments are `(oldAtEntry, new)`.
//   * **`+0x1b38` is `SelectIdealStateSelector`.** The twelve fifteen-byte slot-461 species bodies
//     write a class tag there and chain; they are data rows, not no-ops. The port carries the
//     eight whose class has an instance; BatSwarm, Combatman, Moleman and SheriffSwarm have none.

// --- Raw NPC_STATE words --------------------------------------------------------------------------

/** `m_NPCState` (`+0x5cc0`) as retail's raw id. Maps the typed mind when `SetState` has not
 *  written an unmapped value (8 FLEE, 0xb HUNT, 0xe). */
int32 NpcStateRetail() const;
void WriteNpcStateRetail(int32 RetailId);
/** `m_IdealNPCState` (`+0x5cc4`) as retail's raw id. */
int32 IdealStateRetail() const;
void WriteIdealStateRetail(int32 RetailId);

/** `CAI_BaseNPC::SetState` (`0x1026e340`). Not a vtable slot. Writes both state words, strips
 *  the enemy on a transition to IDLE, and dispatches slot 463 with the entry-time old state. */
void SetState(int32 NewRetail);

/** Slot 461's raw answer, kept beside the typed virtual because this runtime's
 *  `EElysiumNpcState` has no member for retail 8 / 0xb / 0xe. */
int32 LastSelectIdealStateRetail = 0;
// Retail stamps `m_SelectIdealStateTrace`'s `__FILE__` (`+0x1b3c`) and `__LINE__` (`+0x1b40`) at
// every arm of every slot-461 body. The shape map calls that pair ABSENT; the mind's transition
// trace carries the same account, so no member stands for it. Only `+0x1b38`, the selector tag,
// is real here — 29d stood it as `SelectIdealStateSelector`.
/** Slot 463's arguments as retail ids. `SetState` writes these before the typed virtual so Bach
 *  and Cop can switch on 8 / 0xb / 0xe which `EElysiumNpcState` cannot spell. */
int32 LastOnStateChangeOldRetail = 0;
int32 LastOnStateChangeNewRetail = 0;

// --- Seams this family's bodies read --------------------------------------------------------------

/** How many times the base idle/alert hear arms reset the motor (`0x102e0b40`). */
int32 SelectIdealStateMotorResets = 0;
/** How many times `SquadNewEnemy` (`0x103161a0`) was reached. */
int32 SelectIdealStateSquadNewEnemyCalls = 0;
/** How many times case 4 ran `0x1027d0a0` (script-fail / cine release). */
int32 SelectIdealStateScriptExitCalls = 0;
/** How many times idle combat rolled below 0x14 and dispatched vtable `+0x7bc` — slot 495
 *  `SurprisedSound` (`0x10294660`), which family Sounds10 stands and this body CALLS. A count
 *  beside the real call, not instead of it. */
int32 SelectIdealStateSlot495Calls = 0;
/** Crash-guard count: `0x1026f590` ORs `0x80002000` into a non-NPC move parent. Retail faults. */
int32 SelectIdealStateNonNpcParentFlagWrites = 0;

/** SEAM for `thunk_FUN_101e3d70(&DAT_10739a4c, this)`, the discipline manager's break-on-notice
 *  sweep at the end of `CAI_BaseNPC::SelectIdealState` case 3's hear arm (`1026f91b`). It walks
 *  `this +0xf34`'s discipline bitmask and `RemoveEffect`s (`0x101e3af0`) every discipline whose
 *  record carries a non-zero byte at `+0x32`. `FElysiumDisciplines` has no "strip the effects that
 *  break on notice" accessor — the break flag itself is unrecovered — so the sweep is counted and
 *  strips nothing. Its only other retail caller is `SetEnemy` (`0x10279a50`), which the port's
 *  `ElysiumNpcEnemy::SetEnemy` does not carry either. */
int32 SelectIdealStateDisciplineStripCalls = 0;

/** `DAT_1092447c` as `CNPC_VHuman::SelectIdealState` reads it: not a `GetBool` — `IsCommand() ==
 *  false` AND the raw float word at `+0x2c` non-zero. Default refuses the hunt arm. */
bool HuntConVarIsCommand = false;
float HuntConVarRawWord = 0.f;

// `CNPC_VCop`'s census byte `+0x6672` is NOT declared here: family SaveRestore10 already stands
// it as `bCopCountedSecond`, and `CNPC_VCop::OnStateChange` (`0x10371c20`) gates its decrement of
// `DAT_1093acb0` on that same word.
/** Process-wide cop census `DAT_1093acb0`. Last writer wins; never restored. */
static int32 CopCensusCount();
static void SetCopCensusCount(int32 Value);

/** SEAM for the crime level of the player's active weapon — `0x102517e0` resolves the weapon's
 *  record and `CNPC_VPedestrian::vfunc461` (`0x103a2e30`) reads it at `weaponData +0x3c4`. This
 *  runtime's weapon record carries no crime-level column, so the reader answers `-1` ("the player
 *  is holding nothing this pedestrian would report") and the witness half of the arm is skipped,
 *  exactly as retail skips it for a player with no active weapon. The flee promote and slot 596
 *  are outside the guard in retail too, so the state machine is whole without it. */
int32 PedestrianWeaponCrimeLevel = -1;
/** How many times the pedestrian's two gunshot arms ran `RecordCriminalWitness` (`0x1028ea60`)
 *  and `PlayerCriminalIncident` (`0x1017f2a0`). */
int32 PedestrianCrimeReports = 0;

// --- Non-slot / base bodies -----------------------------------------------------------------------

/** `CAI_BaseNPC::PreSelectIdealState` (`0x1026f590`), slot 460's BASE body. Always returns 0. */
int32 BasePreSelectIdealState();
/** `CAI_BaseNPC::SelectIdealState` (`0x1026f660`), slot 461's BASE body. */
int32 BaseSelectIdealState();
/** `CAI_BaseNPCTroika::SelectIdealState` (`0x102ad660`). */
int32 TroikaSelectIdealState();
/** Slot 461 in retail's own ordinals — the method species classes override (story 5 step 3); the
 *  typed `SelectIdealState()` records its answer and converts it. */
virtual int32 SelectIdealStateRetail();

int32 HumanSelectIdealState();
int32 AnimalSelectIdealState();

/** `FUN_103723f0` — `CNPC_VCop::vfunc461`'s idle/alert pre-pass, its only caller. Answers retail
 *  `2` when it takes the arm and `0` when it does not. */
int32 CopSelectIdealStatePrePass();

