// Story 29c-1, family **Conditions** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcKernelConditions.cpp` and the tests in
// `Tests/ElysiumNpcKernelConditionsTests.cpp`. One file per family rather than 915 declarations
// appended to an already-oversized header: the family boundary is what this story ports by.
//
// The seven slots this family DEFINES (declared by the generator, defined in the `.cpp`):
//   459 `RemoveIgnoredConditions`   0x1026d7f0
//   463 `OnStateChange`             0x102ae140
//   477 `ClearSenseConditions`      0x1026e5c0
//   553 `RangeAttack1Conditions`    0x1026d890
//   554 `RangeAttack2Conditions`    0x1026d920
//   560 `ClearAttackConditions`     0x1026dc80
//   564 `FCanCheckAttacks`          0x102953a0

// --- Species words this family's bodies read ------------------------------------------------------
//
// 29b declared every word of the flattened `CAI_BaseNPCTroika` layout; the species words above
// `+0x665c` have no port member because one leaf carries every classname. These are the ones this
// family's bodies read, declared by retail name with the species class that owns the offset, the
// way families Hints and Squad declared theirs. `m_DoorState` (`+0x6680`) and the Werewolf zone word
// (`+0x66e8`) are declared by family **Hints** (`ElysiumNpcKernelHints.inl`) and are read here
// rather than duplicated.

bool bWasDisturbed = false;    // +0x6666 CNPC_VGhoulCroucher::m_bWasDisturbed (datamap)
bool bUnawareExited = false;   // +0x6667 CNPC_VGhoulCroucher::m_bUnawareExited (datamap)

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

/** `CCineAISchedule::RemoveIgnoredConditions` (`0x101a89a0`) — the SCRIPT entity's own slot-459
 *  body, which the NPC's slot 459 (`0x1026d7f0`) dispatches into. It clears fourteen conditions on
 *  the scene partner, in retail's order, and clears its `m_bCondTookDamage` byte between the two
 *  groups. Static and taking the partner because `this` is the cine entity in retail, not the NPC. */
static void ClearCineIgnoredConditions(FElysiumNpc& Partner);

/** SEAM for `CCineAISchedule::0x101a8930`, the "am I already in this state" predicate slot 459 tests
 *  first, and for the cine entity's `m_hTargetEnt` -> `+0x94` partner walk. This runtime's scripted
 *  scenes do not carry either, so the resolve answers null and slot 459 clears nothing. */
FElysiumNpc* CineIgnoredConditionsPartner() const;

// --- `OnStateChange`, slot 463 --------------------------------------------------------------------
//
// `FElysiumNpc::ApplyStateWeaponVisibility` (public, `ElysiumNpc.h`) already carries the SEVEN
// classes whose slot-463 body hides/unhides the weapon. What lands here is the rest of slot 463: the
// species pre-step table those seven are one row of, the Troika-line body every class ends in
// (`0x102ae140`) and the base body under it (`0x1026e3e0`, already `FElysiumNpcFlags::
// NpcStateFlagsForRetailState`).

enum class EStateChangeSpecies : uint8
{
	/** `CNPC_VGuard1::vfunc463` (0x1037d020), `CNPC_VHunter::vfunc463` (0x10388880) and
	 *  `CNPC_VGhoulCroucher::FUN_103871c0` — hide on IDLE, unhide on ALERT/COMBAT/11, then chain. */
	HolsterOnState,
	/** `CNPC_VTzimisce::vfunc463` (0x103ba2c0) — map the new state to a facial expression name and
	 *  blend to it over 1.0 s, then chain. */
	FacialExpression,
	/** `CNPC_VCamera::OnStateChange` (0x10368ea0) — an EMPTY body. It does not chain: a camera's
	 *  state change writes nothing at all, not even the base state-flag byte. */
	Suppressed,
	/** `CAI_BaseHumanoid::OnStateChange` (0x10260630) — a pre-step, then a chain that SKIPS the
	 *  Troika body and goes straight to `CAI_BaseNPC::OnStateChange`. No `npc_V*` classname reaches
	 *  it; the row is carried so the census can be checked against it. */
	HumanoidPreStep,
};

/** One row of retail's slot-463 species table: the census class, the retail address of the body that
 *  fills the slot for it, and which of the four shapes that body is. */
struct FStateChangeSpecies
{
	const TCHAR* RetailClass = nullptr;
	const TCHAR* Body = nullptr;
	EStateChangeSpecies Shape = EStateChangeSpecies::HolsterOnState;
};

/** The table. A class with no row runs `CAI_BaseNPCTroika::OnStateChange` (`0x102ae140`) directly,
 *  which is what 40-odd of the cast do. */
static const FStateChangeSpecies* StateChangeSpeciesRows(int32& OutCount);

/** The row for a retail class name, walking no base chain — the census's own `OverrideOf` is what
 *  walks it, and `StateChangeSpeciesOf` is what a test drives by name. */
static const FStateChangeSpecies* StateChangeSpeciesOf(const TCHAR* InRetailClass);

/** This NPC's row, resolved through the census (`ElysiumNpcKernelClass::OfClassname` + the base
 *  chain), or null for a class the table does not carry. */
const FStateChangeSpecies* StateChangeSpecies() const;

/** `CNPC_VTzimisce::vfunc463`'s expression map (`0x103ba2c0` + `0x103b9f50`): the retail state to
 *  one of `PTR_s_normal_10653120`'s four names. Null for a state the switch does not name, which is
 *  the arm that writes nothing. Pure, so the map is drivable with no NPC at all. */
static const TCHAR* StateChangeExpressionName(EElysiumNpcState NewState);

/** SEAM for `CBaseCombatCharacter::LookupExpressionIndex` + `0x103b9f90(this, index, 1.0)`. There is
 *  no `SetExpression` in this runtime (the script API lists it as a stub), so this records the NAME
 *  in `DefExpression` and blends nothing. `BlendSeconds` is retail's own `1.0`. */
void SetDefaultExpression(const TCHAR* ExpressionName, float BlendSeconds);

/** `CAI_BaseNPCTroika::OnStateChange` (`0x102ae140`) — the Troika-line body, reached by every class
 *  whose species row (if any) chains. Public so a fixture can state the transition without also
 *  driving a species pre-step. */
void OnStateChangeTroika(EElysiumNpcState OldState, EElysiumNpcState NewState);

// --- `FCanCheckAttacks`, slot 564 -----------------------------------------------------------------

/** `CAI_BaseNPC::FCanCheckAttacks` (`0x10270840`) — the BASE half, which the Troika body
 *  (`0x102953a0`, slot 564) tail-calls. Kept separate because the Troika body's own arm is a
 *  suppression in front of it and collapsing the two would lose the delegation. */
bool FCanCheckAttacksBase() const;

/** SEAM for `CAI_Navigator::GetNavType()` (`0x1027d990` = `m_pNavigator(+0x5d34)[0x18]`), whose
 *  `NAV_JUMP`(1) and `NAV_CLIMB`(3) values are the two `FCanCheckAttacks` refuses on. This
 *  substrate's motor has no nav-type word, so this answers `NAV_GROUND`(0) — the value that does NOT
 *  suppress, which is the arm every ground body takes in retail. */
int32 NavType() const;

// --- The alternate-AI door transaction ------------------------------------------------------------
//
// `RunAlternateAi` (public, `ElysiumNpc.h`) is `CAI_BaseNPCTroika::RunAlternateAI`'s DISPATCHER
// (`0x1028fd80`). The two bodies below are its mode-1 arm and the producer that enters mode 1.

/** `FUN_10298800` — enter alternate-AI mode 1: `m_bShouldMove = false`, stop the motor
 *  (`0x102ee2a0` on `m_pNavigator`), `m_eAlternateAI = 1`. */
void EnterAlternateAi();

/** `FUN_10290040` — `RunAlternateAI`'s **mode 1** arm, the door-opening transaction. NAMED for what
 *  it does rather than `RunAlternateAi`, which is the dispatcher above and is already ported.
 *  Returns retail's own byte: false only when the door handle went stale (which also resets the
 *  mode to 0) or when the door refused a facing point. */
bool RunAlternateAiOpeningDoor(double Now);

/** SEAM for the door's own vtable `+0x3d8` — "where should an NPC stand to open me", called with
 *  `m_bOpeningDoorWait`. This substrate's doors carry no NPC-open point, so it answers false, which
 *  is retail's `iStack_10 == -1` arm: the transaction returns without arming mode 2. */
bool OpeningDoorFacingPoint(const FElysiumEntity& Door, bool bWait, FVector& OutPointCm) const;

/** SEAM for `0x102e0b40` + `0x102e1c10(motor, yaw, -1.0)`, the reset-and-set-ideal-yaw pair the door
 *  transaction performs once the door has named a point. Kept separate from family Hints'
 *  `SetMotorHintYaw` because it is a different retail call with a different turn-rate argument. */
void SetAlternateAiIdealYaw(float YawDegrees);

// `FacingIdeal` (`0x10278c80`), the gate this transaction's advance arms sit behind, is family
// **Facing**'s body (`ElysiumNpcKernelFacing.inl`) and is called rather than restated:
// `|CAI_Motor::DeltaIdealYaw()| <= 0.006` degrees with the equal bit carried. The motor seam under
// it stands at retail's "already facing the ideal" answer of 0, so the gate is OPEN on an untouched
// body — it is the DOOR query above, which runs first, that stops the transaction.

/** SEAM for `FUN_10298840`, the "start the open" follow-up mode 1 runs when `m_bOpeningDoorWait` is
 *  clear: it asks the door for its point again, `RestartIdealActivity`s onto it, and either pushes
 *  the door open or fires its use handler. Answers false. */
bool StartOpeningDoor(FElysiumEntity& Door);

/** SEAM for `CAI_Motor::StopMoving`-side `0x102ee2a0`, the motor-chain stop `EnterAlternateAi`
 *  performs on `m_pNavigator` before it arms the mode. The port's own `StopMoving()` is the
 *  body-facing half and is called; this names the retail call it stands for. */

// --- The disturbed latch --------------------------------------------------------------------------

/** `CNPC_VGhoulCroucher::IsDisturbed` (`0x1037bb20`) — `return m_bWasDisturbed`, and nothing else.
 *  The producer `ElysiumNpc.cpp`'s stealth-kill gate names as absent. */
bool IsDisturbed() const;

/** `CNPC_VGhoulCroucher::OnDisturbed` (`0x1037b6e0`) — the once-latch: on the FIRST disturbance it
 *  latches `m_bWasDisturbed`, clears `m_bUnawareExited`, and then splits on whether the disturber is
 *  a player. `Disturber` is retail's `param_1`, the entity that did it; null is allowed and takes
 *  the non-player arm. */
void OnDisturbed(FElysiumEntity* Disturber);

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

// --- `SelectIdealState`, slot 461: the species half -----------------------------------------------
//
// THE SLOT ITSELF IS NOT THIS STORY'S: slot 461's Troika-line body (`0x102ad660`, layer 22) is story
// 29e's and the generator still emits its stub, so `FElysiumNpc::SelectIdealState()` is taken. What
// 29c-1 owns is the three SPECIES overrides, which are complete replacements — none of them chains.

enum class EIdealStateSpecies : uint8
{
	/** `CNPC_VCamera::FUN_10369060` (0x10369060), shared with `CNPC_VCameraSecurity`: the ideal state
	 *  is HARDCODED to retail 3 (ALERT) with no test at all. */
	AlwaysAlert,
	/** `CNPC_VMingXiao::vfunc461` (0x103945a0): `IsAlive() && GetState() != 7` skips a (dead) write
	 *  of 7, then `GetEnemy() ? 2 : 1` unconditionally. */
	MingXiao,
	/** `CNPC_VMingXiaoTentacle::vfunc461` (0x1039e310): already dead — current OR ideal state 7 —
	 *  stays 7; otherwise `GetEnemy() ? 2 : 1`. */
	MingXiaoTentacle,
};

/** One row of retail's slot-461 species table. */
struct FIdealStateSpecies
{
	const TCHAR* RetailClass = nullptr;
	const TCHAR* Body = nullptr;
	EIdealStateSpecies Rule = EIdealStateSpecies::AlwaysAlert;
};

static const FIdealStateSpecies* IdealStateSpeciesRows(int32& OutCount);
static const FIdealStateSpecies* IdealStateSpeciesOf(const TCHAR* InRetailClass);

/** The rule applied. Pure over the three inputs the three bodies read, so a test drives it with no
 *  NPC: `bAlive` is slot 158, `Current` is `m_NPCState`, `Ideal` is `m_IdealNPCState` and
 *  `bHasEnemy` is `GetEnemy() != NULL`. */
static EElysiumNpcState SelectIdealStateSpecies(EIdealStateSpecies Rule, bool bAlive,
	EElysiumNpcState Current, EElysiumNpcState Ideal, bool bHasEnemy);

/** This NPC's species answer. False when no row carries this class, which is every registered
 *  classname today and is the arm that lets the two-layer rule run. */
bool SelectIdealStateForSpecies(EElysiumNpcState& OutIdeal) const;

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

// --- The Werewolf death-triggered latch -----------------------------------------------------------

/** `CNPC_VWerewolf::UpdateConditionDeathTriggered` (`0x103cc890`). 29c's row targets
 *  `FElysiumNpcConditions::UpdateConditionDeathTriggered`; that type is a bare 256-bit set with no
 *  NPC, no activity and no outputs, so the body lands here instead.
 *
 *  Condition `0x7a` belongs to a Werewolf-line registrar the census has not decoded, so it is spelled
 *  as the raw number. */
void UpdateConditionDeathTriggered();
