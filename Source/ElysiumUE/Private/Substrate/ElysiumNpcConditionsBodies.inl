// Story 29c-1, family **Conditions** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcConditionsBodies.cpp` and the tests in
// `Tests/ElysiumNpcKernelConditionsTests.cpp`. One file per family rather than 915 declarations
// appended to an already-oversized header: the family boundary is what this story ports by.
//
// The slots this family DEFINES (declared by the generator, defined in the `.cpp`):
//   459 `RemoveIgnoredConditions`   0x1026d7f0
//   463 `OnStateChange`             0x102ae140
//   477 `ClearSenseConditions`      0x1026e5c0
//   553 `RangeAttack1Conditions`    0x1026d890
//   560 `ClearAttackConditions`     0x1026dc80
//   564 `FCanCheckAttacks`          0x102953a0

// --- Species words this family's bodies read ------------------------------------------------------
//
// 29b declared every word of the flattened `CAI_BaseNPCTroika` layout; the species words above
// `+0x665c` have no port member because one leaf carries every classname. These are the ones this
// family's bodies read, declared by retail name with the species class that owns the offset, the
// way families Hints and Squad declared theirs. `m_DoorState` (`+0x6680`) and the Werewolf zone word
// (`+0x66e8`) are declared by family **Hints** (`ElysiumNpcHints.inl`) and are read here
// rather than duplicated.

// +0x1564 CBaseCombatCharacter::m_flNextAttack (datamap) — an absolute curtime deadline, carried as
// double. It sits on the CHAIN, not on `CAI_BaseNPCTroika`'s own 388 words, which is why 29b's shape
// map has no row for it; `RefreshCombatConditions` (`0x102b2570`) and `GatherAttackConditions`
// (`0x1026dd10`) are its two readers in this band and nothing in this runtime writes it yet.
double NextAttackTime = 0.0;

// +0x10b4 CAI_BaseNPCTroika::m_idxDefExpression (datamap). Retail stores the resolved index from
// `CBaseCombatCharacter::LookupExpressionIndex`; this runtime NAMES expressions (see
// `NoDeformExpression`, +0x64d0, which 29b declared the same way), so the NAME is what is stored and
// the index has no counterpart.
FString DefExpression;

// --- `RemoveIgnoredConditions`, slot 459 ----------------------------------------------------------

// --- `OnStateChange`, slot 463 --------------------------------------------------------------------
//
// The species bodies are overrides on their C++ classes (story 5 step 3). What lands here is the
// Troika-line body every chaining class ends in (`0x102ae140`) and the base body under it
// (`0x1026e3e0`, already `FElysiumNpcFlags::NpcStateFlagsForRetailState`).

/** `CAI_BaseNPCTroika::OnStateChange` (`0x102ae140`) — the Troika-line body, reached by every class
 *  whose species row (if any) chains. Public so a fixture can state the transition without also
 *  driving a species pre-step. */
void OnStateChangeTroika(EElysiumNpcState OldState, EElysiumNpcState NewState);

// --- `FCanCheckAttacks`, slot 564 -----------------------------------------------------------------

// --- The alternate-AI door transaction ------------------------------------------------------------
//
// `RunAlternateAI` (`ElysiumNpcRunAi.inl`, `0x1028fd80`) is the DISPATCHER
// (`0x1028fd80`). The two bodies below are its mode-1 arm and the producer that enters mode 1.

/** `FUN_10298800` — enter alternate-AI mode 1: `m_bShouldMove = false`, PAUSE the path
 *  (`0x102ee2a0` on `m_pNavigator` -> `path+0x10 = 1`; the move step parks the body), `m_eAlternateAI
 *  = 1`. Reached from `AdvancePath 0x102f0400`'s door arm (`FElysiumNpc::NavAdvanceDoorWaypoint`). */
void EnterAlternateAi();

/** `FUN_10290040` — `RunAlternateAI`'s **mode 1** arm, the door-opening transaction. NAMED for what
 *  it does rather than `RunAlternateAI`, which is the dispatcher above.
 *  Returns retail's own byte: false only when the door handle went stale (which also resets the
 *  mode to 0) or when the door refused a facing point. */
bool RunAlternateAiOpeningDoor(double Now);

/** The door's own vtable `+0x3d8` (`FElysiumDoorBase::GetNPCOpenData`), called with
 *  `m_bOpeningDoorWait`, and the ONE field mode 1 reads: `FaceDir` (`+0xc`). `OutPointCm` carries
 *  that DIRECTION (port axes), not a point -- the name predates the struct (0018/7). False is the
 *  door's -1 (`iStack_10 == -1`: a sliding door, the wrong side of a swinging one, a non-door): the
 *  transaction returns without arming mode 2. */
bool OpeningDoorFacingPoint(const FElysiumEntity& Door, bool bWait, FVector& OutPointCm) const;

// `FacingIdeal` (`0x10278c80`), the gate this transaction's advance arms sit behind, is family
// **Facing**'s body (`ElysiumNpcFacing.inl`) and is called rather than restated:
// `|CAI_Motor::DeltaIdealYaw()| <= 0.006` degrees with the equal bit carried. The motor seam under
// it stands at retail's "already facing the ideal" answer of 0, so the gate is OPEN on an untouched
// body — it is the DOOR query above, which runs first, that stops the transaction.

// `0x102ee2a0` is the navigator's PAUSE (`0x1030be80`, `path+0x10 = 1`), which `EnterAlternateAi`
// writes directly (`Navigator.bPaused`); it is not a motor stop (0018/7 correction).

// --- The cop / hunter pursuit counters ------------------------------------------------------------
//
// RECEIVER CORRECTION: 29c's rows name `FElysiumNpc::On*Pursuit*`, but the recovered receiver is the
// PLAYER — `CNPC_VCop::vfunc597` (`0x10372cc0`) calls `0x1017f650(player + 0xa8)`, and `+0x1d10` /
// `+0x1d14` are `FElysiumPoliceState::CopsInPursuit` / `HuntersInPursuit`. They are therefore static
// here and take the player, so the row's name is honoured and the state stays where it belongs.
// The counters and their four outputs are already carried by `ElysiumLaw`; these three bodies are the
// increment/decrement wrappers retail calls.

/** `FUN_1017f650` `"CSActs: %6.1f - OnCopPursuitStart - %d in pursuit"` — on the 0 -> 1 edge run the
 *  two start hooks, THEN increment. The test is against the PRE-increment count. */
static void OnCopPursuitStart(FElysiumPlayer& Player);

/** `FUN_1017f7b0` — the same shape for hunters, with one start hook (`0x1017fa40`). */
static void OnHunterPursuitStart(FElysiumPlayer& Player);

/** `FUN_1017f830` — DECREMENT FIRST, then run the stop hook (`0x1017fa70`) when the count reached
 *  zero. The asymmetry with the two start bodies is retail's and is reproduced. */
static void OnHunterPursuitStop(FElysiumPlayer& Player);

/** SEAM for `FUN_103705e0`, the first of `OnCopPursuitStart`'s two hooks: it walks the global NPC
 *  list and puts every body whose `GetState()` is **14** into state 2. This runtime's
 *  `EElysiumNpcState` has no member for retail state 14 (the state `UpdateIdealState` names as "the
 *  criminal window"), so nothing can be in it and the sweep visits nobody. */
static void PromoteCriminalWindowNpcs(FElysiumEntityWorld& World);

// --- The combat-condition refreshers --------------------------------------------------------------

/** `FUN_102b2570` — the melee dodge/block refresh, run inside the condition pass. It clears
 *  `SHOULD_STEPBACK` (0x0e) and `SHOULD_KICK` (0x0f) unconditionally and re-raises each from its own
 *  timer plus a random draw, and it is the one producer of `TOO_FAR_FOR_MELEE` (0x09). */
void RefreshCombatConditions();

/** SEAM for the active weapon's flag word (`weapon vtable +0x5a0`) bit 30, the last term of
 *  `RefreshCombatConditions`' `SHOULD_KICK` arm. This runtime's `FElysiumWeapon` carries no retail
 *  flag word — the capability answer is two named bits and deliberately not a register — so it
 *  answers false and the kick condition is never raised. UNRECOVERED: the bit's name. */
bool WeaponFlagBlocksAttack() const;

/** `FUN_1028e700` — the occlusion debounce, run per condition. `InOutStamp` is the caller's own
 *  sentinel float; retail's caller passes `&m_flOccludedTimer`-shaped storage and the sentinel is
 *  `_DAT_104454c4` = `0.0f`. The condition is SUPPRESSED until `m_flOccludedDelay` (`+0x62c8`) has
 *  elapsed since it first stood, and the sentinel is reset the moment it drops. */
void RefreshOccludedCondition(EElysiumNpcCond Cond, double& InOutStamp, double Now);

// --- `RequestDesiredState`, the two flee arms -----------------------------------------------------

/** `FUN_102ad260` (gate `SUPERNATURAL_FLEE_LEVEL` 0x21, retail source line 0x4468) and `FUN_102ad2d0`
 *  (gate `CRIMINAL_FLEE_LEVEL` 0x1f, line 0x447d) — the two identical bodies `PreSelectIdealState`
 *  (slot 460, story 29d) calls. Both force retail ideal state **8** (FLEE) and return it.
 *
 *  The gate is `HasInterruptCondition` (`0x10269d30`), NOT `HasCondition`: the running program has to
 *  list the law condition for the flee to be requested at all.
 *
 *  Returns retail's own answer — 8 when the gate stood, 0 when it did not. */
int32 RequestFleeDesiredState(EElysiumNpcCond GateCondition, int32 RetailSourceLine);
