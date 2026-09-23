// Story 29d, family **Conditions10** — the declarations of this family's layer 10–18 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl` and story 29c-1's family `.inl`s. A body that fills a Troika-line
// vtable slot is NOT declared here: the generator already declares that virtual and this family only
// defines it (slots 35, 342, 404, 419, 532, 587). What lands here is the non-slot half — the
// base-tier bodies beside the Troika overrides, the per-species arms the one slot method dispatches
// to, the flag-word helpers, and the seams those arms read and write through.
//
// The definitions are in `Substrate/ElysiumNpcKernelConditions10.cpp`. The tests are
// `Tests/ElysiumNpcKernelConditions10Tests.cpp` and the walked prose is
// `docs/vtmb/npc-ai/conditions-and-states.md` § "Conditions10 — the flag-word writers,
// `IRelationType` and `TaskFail`".
//
// --- What this family is ---------------------------------------------------------------------
//
// Twenty rows: slot 404 `IRelationType` (`0x10299da0`) and its four species arms, `CanBeFedUponBy`
// (slot 342) with the `CBaseCombatCharacter` body it chains, the seven `TaskFail` species arms in
// front of story 13's slot-448 body, the condition-debug string builder (`0x1028d990`),
// `CanWitnessSupernatural` (slot 587), the door-failure slot 532, slot 35's gendered id,
// `UpdateBurstShootPause` (slot 419) and `ResetFakeReloadCount` (`0x102c54c0`).
//
// **The standing fact of this family.** A flag-word writer is observable by the program that reads
// the word, so the mask, the order of a set against a clear, and whether a bit is written before or
// after a dispatch are all reproduced from the LISTING rather than from the decompiler's folded
// constants. Every such mask below carries the instruction address that tests or writes it.

// --- Slot 404 `IRelationType` — one slot, five retail bodies -----------------------------------
//
// `FElysiumNpc::IRelationType()` (the generated virtual, defined by this family) is the one port
// method. It resolves the census body for this NPC's retail class
// (`ElysiumNpcKernelClass::BodyOf(RetailClass(), 404)`) and runs that arm. Eight census rows fill
// the slot with FIVE distinct bodies, and two of the five are not this family's rows: `0x103a48b0`
// (`CNPC_VFrenzyShadow`, `CNPC_VPlayerController`, `CNPC_VWolfMorph`) is story 29c-1's
// `SpeciesIRelationType` in family Squad and is dispatched to, and `0x103a01b0`
// (`CNPC_VNewscaster`) is an eight-byte `return 4;` answered inline. Neither chains the Troika body.
//
// **No `FSpeciesDispatchScope` here, and that is the recovered shape.** All four species arms end
// in a DIRECT non-virtual thunk to `CAI_BaseNPCTroika::IRelationType` (`thunk_FUN_10299da0`), which
// `TroikaIRelationType` below is; but the Troika body's own three inner `vtable +0x650` calls are
// VIRTUAL and therefore re-enter the species arm. A latch that suppressed the species body for the
// whole call would change the answer a cop gives about its own closest player, so the two kinds of
// chain are spelled as what they are: a direct call to the named base arm, and a virtual call to
// `IRelationType`.

/** `CAI_BaseNPCTroika::IRelationType` (`0x10299da0`), 541 bytes — the Troika-line body proper, and
 *  the one every species arm chains to directly.
 *
 *  Three forwarding arms in front of `CBaseCombatCharacter::IRelationType`, in retail's order:
 *  the INSANE arm (the candidate's NPC carries `D_INSANE`, and my closest player is hated or is my
 *  enemy), the CANDIDATE'S BOSS arm, and MY OWN BOSS arm, which answers the boss's opinion — or,
 *  when the candidate is itself an NPC, the CANDIDATE'S opinion of the boss, because `EDI` is
 *  reassigned at `10299f84`. */
int32 TroikaIRelationType(FElysiumEntity* Candidate);

/** `CNPC_VCop::IRelationType` (`0x10372b70`), 170 bytes — three arms in front of the Troika body:
 *  a null candidate answers `D_ER` outright, the cop class's shared timed grudge answers `D_HT`,
 *  and a player candidate answers `D_HT` on `m_flHeightenedAlertExpireTimer` or a non-zero
 *  `m_iCopsInPursuitCount`. **UNREACHABLE TODAY**: `CNPC_VCop`'s census classname list is null, so
 *  a spawned `npc_VCop` has a null `RetailClass()` and correctly takes the Troika line (story
 *  29c-1's cleanup). The arm is ported and driven directly by its test. */
int32 CopIRelationType(FElysiumEntity* Candidate);

/** `CNPC_VHunter::IRelationType` (`0x10388bb0`), 109 bytes — the cop arm minus both player-side
 *  tests: a null candidate answers `D_ER`, the hunter class's own static grudge answers `D_HT`, and
 *  everything else defers. A hunter's extra hostility comes only from that one shared timer. */
int32 HunterIRelationType(FElysiumEntity* Candidate);

/** `CNPC_VPedestrian::IRelationType` (`0x103a2930`), 58 bytes — a null candidate answers `D_ER`,
 *  a candidate whose NPC carries `D_INSANE` (`m_bfAINPCFlags2 & 0x20000`) answers `D_FR` WITHOUT
 *  consulting the relationship table at all, and everything else defers. Pedestrians fear the
 *  insane, and dialogue and the flee schedules read that through slot 404. */
int32 PedestrianIRelationType(FElysiumEntity* Candidate);

/** `CNPC_VYukie::IRelationType` (`0x103dd880`), 20 bytes, read off the LISTING because the C hides
 *  the return: the candidate is loaded into `EAX`, a null one `RET`s with `EAX` still holding that
 *  null (so `D_ER`), and anything else tail-jumps to the Troika body unchanged. The whole species
 *  override is the null guard. */
int32 YukieIRelationType(FElysiumEntity* Candidate);

/** `CBaseCombatCharacter::IRelationType` — the relationship-table tail the Troika body ends at, and
 *  the same store family **Senses10**'s `IRelationTypeOf` reads. Kept as one named arm so both
 *  readers cannot drift. */
int32 BaseCombatCharacterIRelationType(const FElysiumEntity* Candidate) const;

// --- Slot 342 `CanBeFedUponBy` ------------------------------------------------------------------

/** `CBaseCombatCharacter::CanBeFedUponBy` (`0x10339800`), 237 bytes — the base body the Troika
 *  override chains to. The FEEDER ARGUMENT IS NEVER READ: every one of its five terms is about the
 *  victim. In order: `CanBeFedUpon()` (`0x10339a90`), `NOT_FEEDABLE` (`m_bfAINPCFlags2 &
 *  0x8000000`) clear, no live grapple (`m_GrapplePartner` resolves AND `m_GrappleRole != -1`
 *  refuses), `IsAlive()` (slot 158, `vtable +0x278`) and `!IsUnconscious()`. */
bool BaseCanBeFedUponBy(FElysiumEntity* Feeder);

/** `CBaseCombatCharacter::CanBeFedUpon` (`0x10339a90`) — `GetCharTemplate(this)->+0x95 == 0`.
 *  **SEAM**: `+0x95` has no recovered column name and this runtime's `FElysiumClanTemplate` exposes
 *  none, so this answers TRUE, which is retail's own answer for a template whose byte is zero — the
 *  admitting arm, so nothing is silently refused. */
bool CanBeFedUponTemplate() const;

/** `CBaseCombatCharacter::IsUnconscious` (`0x10341aa0`) — `(m_iMiscFlags & 1) != 0`, bit 0 of the
 *  name table being `Unconscious` (`Substrate/ElysiumMiscFlags.h`). */
bool IsUnconsciousMiscFlag() const;

// --- Slot 448 `TaskFail` — the seven species arms in front of story 13's body -------------------
//
// `FElysiumNpc::TaskFail` (`ElysiumNpc.cpp`) is slot 448's one port method and already runs the
// whole `CAI_BaseNPCTroika::TaskFail` (`0x1029adb0`) chain. Every species body in the census runs
// its own arm and then chains `0x1029adb0` UNCONDITIONALLY, so the arms are a PROLOGUE: `TaskFail`
// calls `SpeciesTaskFail` on its first line and the Troika body follows, which is retail's order.

/** The slot-448 species dispatcher, called from the first line of `FElysiumNpc::TaskFail`. */
void SpeciesTaskFail(int32 Reason);

/** `CNPC_VAsianVampire::TaskFail` (`0x10362390`) — on failure codes 12..15 (`0xb < code && code <
 *  0x10`) it sets `m_bPathBlocked` (`+0x66d4`) and nothing else. */
void AsianVampireTaskFail(int32 Reason);

/** `CNPC_VChangBros::TaskFail` (`0x1036d1d0`), shared by `CNPC_VChangBrosBlade` and
 *  `CNPC_VChangBrosClaw` — the same 12..15 gate, but the write is `m_failSchedule` (`+0x5c54`) =
 *  `0x15d`. */
void ChangBrosTaskFail(int32 Reason);

/** `CNPC_VGargoyle::TaskFail` (`0x10379060`) — in combat with `TASK_FAILED` standing as an
 *  interrupt it clears the top bit of `m_afMemory`; then, when `FINDING_BODY` stands, it clears
 *  `FINDING_BODY`; then `m_iShunnedFindPillar` (`+0x6680`) = 0. */
void GargoyleTaskFail(int32 Reason);

/** `CNPC_VHengeyokai::TaskFail` (`0x10380510`) — the memory-bit clear, then the blacklist-and-drop
 *  pair on `FINDING_BODY`, then the ignore-collision re-arm on `!CARRYING_BODY`, then
 *  `m_iShunnedFindFish` (`+0x6678`) = 0. */
void HengeyokaiTaskFail(int32 Reason);

/** `CNPC_VMingXiao::TaskFail` (`0x10394090`) — switches on `m_eThrowableObjectMode` (`+0x673c`):
 *  modes 3 and 4 reset the motor's steering to 180.0 and touch nothing else; every other mode sets
 *  the mode to 0 and releases `m_hThrowObject` (`+0x6718`). */
void MingXiaoTaskFail(int32 Reason);

/** `CNPC_VSheriffMan::TaskFail` (`0x103b0290`) — apart from the scope-trace bookkeeping the whole
 *  body is the chain to the base. The recovered fact is the ABSENCE of an arm, and it is ported so
 *  a test can prove the sheriff adds nothing. */
void SheriffManTaskFail(int32 Reason);

/** `CNPC_VTzimisce::TaskFail` (`0x103ba350`) — the Hengeyokai's arm with its own words:
 *  `m_iShunnedFindBody` (`+0x66b8`) and its own `m_FailedPickupTargets` blacklist. */
void TzimisceTaskFail(int32 Reason);

// --- The species words those arms write ---------------------------------------------------------
//
// Every one of these is a SPECIES-ONLY datamap member with no row in this runtime's shape map,
// because this port stands ONE leaf for every classname. Each is carried here as the one word its
// species would have, named for the retail field, so the write is observable rather than dropped.

/** `CNPC_VAsianVampire::m_bPathBlocked` (`+0x66d4`). Written by `0x10362390` on failure codes
 *  12..15; its readers are the AsianVampire's own schedule selector, which this band does not own,
 *  so nothing reads it here yet. */
bool bSpeciesPathBlocked = false;

/** The three `m_iShunnedFind*` counters — `CNPC_VGargoyle::m_iShunnedFindPillar` (`+0x6680`),
 *  `CNPC_VHengeyokai::m_iShunnedFindFish` (`+0x6678`) and `CNPC_VTzimisce::m_iShunnedFindBody`
 *  (`+0x66b8`). Three retail fields, ONE word here: a spawned NPC is exactly one of those three
 *  species, so the three can never be live at once and a per-species word would be three names for
 *  the same storage. */
int32 SpeciesShunnedFindCount = 0;

/** `CNPC_VHengeyokai::m_hPickupTarget` (`+0x6664`) and `CNPC_VTzimisce::m_hPickupTarget`
 *  (`+0x6670`) — the same field under two offsets, for the same reason. */
FElysiumEntityHandle SpeciesPickupTarget;

/** `CNPC_VMingXiao::m_hThrowObject` (`+0x6718`) and `m_eThrowableObjectMode` (`+0x673c`).
 *  `0x10398d90` is `m_eThrowableObjectMode = arg` and nothing else — it is the MODE setter, not a
 *  prop release, which is this family's correction to the checklist's walk. */
FElysiumEntityHandle SpeciesThrowObject;
int32 SpeciesThrowableObjectMode = 0;

/** `0x10382970` (Hengeyokai, `m_BlacklistedEntities` at `+0x66a4`) and `0x103bf200` (Tzimisce,
 *  `m_FailedPickupTargets` at `+0x6690`) — byte-identical `CUtlVector<BlacklistedEntity_t>` appends
 *  of `(handle, curtime + 20.0)`, the 20.0 being `_DAT_1044eb0c`. **NOT a release**: the target is
 *  SHUNNED for twenty seconds, which is this family's second correction to the walk. */
static constexpr float SpeciesBlacklistSeconds = 20.f;   // _DAT_1044eb0c
struct FSpeciesBlacklistEntry
{
	FElysiumEntityHandle Entity;
	double ExpiresAt = 0.0;
};
TArray<FSpeciesBlacklistEntry> SpeciesBlacklistedEntities;
void BlacklistPickupTarget(const FElysiumEntityHandle& BlacklistTarget);

/** `0x102c43b0(this, 0.75)` — `if (GetIgnoreCollisionEntity()) { m_flIgnoreCollisionTimer
 *  (+0x6458) = curtime + delay; 0x102c43f0(this); }`, and `0x102c43f0` clears the ignored entity
 *  and parks the timer at `FLT_MAX` once the stamp has passed. **SEAM** for
 *  `GetIgnoreCollisionEntity()`: this runtime's `IgnoreCollisionUntil` is the timer and no ignored
 *  ENTITY is carried, so the gate is "the timer is armed", which is the same question for every
 *  body that ever armed it. */
void SetIgnoreCollisionExpiry(float DelaySeconds);

// --- Slot 587 `CanWitnessSupernatural` — the frenzied bit it reads -------------------------------
//
// `0x1028ef20`'s fourth refusal is `m_bfNPCFrenziedFlags & 0x10`, the "does not witness" bit
// `ElysiumNpcFlags.h` already names in prose. The constant lands beside its two siblings there.

// --- Slot 532 — the door-failure cleanup --------------------------------------------------------

/** `CAI_BaseNPC::vfunc532` (`0x1027e0f0`) — the base body every arm of `0x10290570` chains to
 *  unconditionally: `m_hOpeningDoor = -1; m_bOpeningDoorWait = 0; return 1;`. */
void BaseSlot532(int32 FailureBits);

/** `0x102bf7e0` — the door cleanup the navigator arm runs: `if (nav->IsGoalSet()) nav->StopMoving();
 *  m_bShouldMove = 1;`. The `IsGoalSet` test is made TWICE in retail (once by the caller at
 *  `102905da` and once here), and both are reproduced. */
void NavigatorDoorCleanup();

/** `0x102ee2e0` — `CAI_Navigator::IsGoalSet()`, `m_pPath(+0x30)->GoalType(+0x10) != 0`. Distinct
 *  from `0x102ee680` (`IsGoalActive`, the current-waypoint test) which family Motor already wires.
 *  **SEAM**: this runtime's mover carries ONE goal latch and no separate goal-type word, so this
 *  answers that latch — the admitting value, since `IsGoalActive` implies `IsGoalSet`. The one case
 *  it under-admits is a goal set with no current waypoint, which the mover cannot represent. */
bool NavIsGoalSet() const;

// --- Slot 419 `UpdateBurstShootPause` -----------------------------------------------------------

/** `0x102c5780` / `0x102c57c0` — the weapon-data words `m_flBurstShootPauseMin` and
 *  `m_flBurstShootPauseMax` take, `wpndata + 0x264` and `wpndata + 0x268`, each routed through
 *  `0x102c5570` with the weapon data resolved by `0x102517e0`. **SEAM**: story 29c-1's
 *  `ActiveWeaponEntity()` answers null and no weapon-data record is stood, so this answers false
 *  and slot 419 takes retail's own UNARMED arm — the two literals 0.3 and 0.5. */
bool ActiveWeaponBurstPauseWords(float& OutMin, float& OutMax) const;

/** `0x102c5570`'s arithmetic as a pure function, so the recovered rule is exercised without a
 *  weapon record. `Value` is `wpndata+0x264` or `+0x268`, `Base` is `wpndata+0x260`, `Range` is
 *  `wpndata+0x26c` and `DistanceUnits` is the distance to `m_hShootTargetOverride` (`+0x5ba8`) or,
 *  failing that, to `GetEnemy()`'s body target; `bHasTarget` false is retail's no-enemy arm.
 *
 *  The body, from the listing: the scale starts at 1.0; when `Range > K` (`_DAT_104454c4`) the
 *  distance is measured and, when THAT exceeds `K` too, the scale becomes `sqrt(distance / Range)`;
 *  with no enemy at all the scale is `sqrt(1.0 / Range)`. The answer is `scale * (Value - Base)`. */
static float ScaleWeaponBurstPause(float Value, float Base, float Range, float DistanceUnits,
	bool bHasTarget);

// --- `0x102c54c0 ResetFakeReloadCount` ----------------------------------------------------------

/** `0x102c54c0` — `m_iFakeReloadCount (+0x65f0) = RandomInt(template[0x34], template[0x38])`, the
 *  template resolved by `GetCharTemplate` (`0x10207c40`) through the template manager
 *  (`0x101d5e80` over `DAT_10738d10`). No caller in the corpus reaches it virtually; its two direct
 *  callers are the AsianVampire's reload arms. */
void ResetFakeReloadCount();

/** The two template columns that roll it, `+0x34` and `+0x38`. **SEAM**: `FElysiumClanTemplate`
 *  exposes no such pair, so this answers false with both ends zero — and the ROLL STILL HAPPENS,
 *  because retail's body has no arm that skips the write and a body that silently declined its only
 *  write would be a refusal this row does not have. `RandomInt(0, 0)` is 0. */
bool CharTemplateFakeReloadRange(int32& OutMin, int32& OutMax) const;

// --- `0x1028d990` — the condition/flag debug string -------------------------------------------

/** `0x1028d990`, 906 bytes — the formatter slots 17 and 18 push every trace message through, and
 *  family **Debug10**'s `TraceMessageFormat` seam's real body.
 *
 *  Retail writes into a caller-supplied `(char* out, int size)` pair and answers nothing; this
 *  returns the string, because the size-clamp is `Q_snprintf`'s and the port's `FString` carries no
 *  fixed buffer. The `param_3 == NULL || param_4 <= 0` guard is therefore spelled as an explicit
 *  argument so retail's "write nothing at all" arm stays reachable and testable.
 *
 *  Three format strings, selected by the two debug BYTES `DAT_10920534` and `DAT_10920535`:
 *    both set        -> `"%6.2f : %*s %s\n%s%s %s%s %s\n\n"`            (`0x105d8868`)
 *    534 set only    -> `"%6.2f : %*s %s\n"`                            (`0x105d8854`)
 *    534 clear       -> `"%-20s  %6.2f : %*s %s\n%s%s %s%s %s\n\n"`     (`0x105d8828`)
 *  and the four optional blocks are built when `534 == 0 || 535 != 0`, which is exactly the two
 *  arms that consume them. */
FString BuildConditionDebugString(const TCHAR* Message, int32 IndentLevel, int32 BufferSize) const;

/** The `CONDS:` list — `"CONDS:"` (`0x105d8908`) then `" %s"` (`0x105a3060`) per condition whose
 *  `HasCondition` stands, walking `[0, GetLastSharedCondition())` (slot 409, `vtable +0x664`, which
 *  is RE-READ every iteration) and naming each through `GetShortConditionName` (slot 408,
 *  `vtable +0x660`), then a trailing `"\n"` (`0x10547e40`). Gated by the schedule-debug ConVar
 *  `DAT_10924a6c`, whose `vtable[4]()` must answer 0 and whose `+0x2c` int must be `> 0`. */
FString ConditionDebugList() const;

/** One bitmask-to-glyph ladder: a set bit takes the template's character at that index, a clear bit
 *  takes `'.'` (`0x2e`), and the result is NUL-terminated at `Count`. Retail builds two — 32 glyphs
 *  of `"PIS__PF_T_L__TTEPLM________ICCCC"` (`0x105d88e0`) over `m_afMemory` (`+0x5d8c`) and 30 of
 *  `"RSCPFCNFIPCDHVAEFSBDSLIAMFDPOIO_"` (`0x105d88b8`) over `m_bfAINPCFlags` (`+0x14b8`). */
static FString DebugMaskLadder(const TCHAR* Template, uint32 Mask, int32 Count);

/** The `"NAV %s %s"` pair (`0x105d888c`): built only when `GetNavType()` is 3 or 1, with the first
 *  `%s` `"CLIMB"` (`0x105d88a0`) on nav type 3 and five spaces (`0x105d8898`) otherwise, and the
 *  second `"JUMP"` (`0x105d88b0`) on nav type 1 and four spaces (`0x105d88a8`) otherwise. Retail
 *  calls `GetNavType()` FIVE times building it. */
FString NavDebugPair() const;

/** `DAT_10924a6c` — `ent_trace_conditions`, the ConVar the `CONDS:` block gates on (`> 0`). It ships
 *  "1" (`ElysiumNpcTunables::EConVar::EntTraceConditions`). */
bool ScheduleDebugConditionsEnabled() const;
