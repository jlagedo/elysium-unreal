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

// --- Seams this family's bodies read --------------------------------------------------------------

/** How many times idle combat rolled below 0x14 and dispatched vtable `+0x7bc` — slot 495
 *  `SurprisedSound` (`0x10294660`), which family Sounds10 stands and this body CALLS. A count
 *  beside the real call, not instead of it. */
int32 SelectIdealStateSlot495Calls = 0;

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

/** `CAI_BaseNPCTroika::SelectIdealState` (`0x102ad660`). */
int32 TroikaSelectIdealState();
/** Slot 461 in retail's own ordinals — the method species classes override (story 5 step 3); the
 *  typed `SelectIdealState()` records its answer and converts it. */
int32 SelectIdealStateRetail() override;
/** The Troika's typed slot-460 / 461 wrappers (`ElysiumNpcCombat10.cpp`, `ElysiumNpcState19.cpp`),
 *  declared here since 0019/8 mapped the slots to the `Retail` virtuals; bodies unchanged. */
EElysiumNpcState PreSelectIdealState() override;
EElysiumNpcState SelectIdealState() override;

int32 HumanSelectIdealState();
int32 AnimalSelectIdealState();

/** `FUN_103723f0` — `CNPC_VCop::vfunc461`'s idle/alert pre-pass, its only caller. Answers retail
 *  `2` when it takes the arm and `0` when it does not. */
int32 CopSelectIdealStatePrePass();

