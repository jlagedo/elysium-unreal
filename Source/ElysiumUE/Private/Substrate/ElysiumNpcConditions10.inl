// Story 29d, family **Conditions10** — the declarations of this family's layer 10–18 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl` and story 29c-1's family `.inl`s. A body that fills a Troika-line
// vtable slot is NOT declared here: the generator already declares that virtual and this family only
// defines it (slots 35, 342, 404, 419, 532, 587). What lands here is the non-slot half — the
// base-tier bodies beside the Troika overrides, the per-species arms the one slot method dispatches
// to, the flag-word helpers, and the seams those arms read and write through.
//
// The definitions are in `Substrate/ElysiumNpcConditions10.cpp`. The tests are
// `Tests/ElysiumNpcKernelConditions10Tests.cpp` and the walked prose is
// `docs/vtmb/npc-ai/conditions-and-states.md` § "Conditions10 — the flag-word writers,
// `IRelationType` and `TaskFail`".
//
// --- What this family is ---------------------------------------------------------------------
//
// Twenty rows: slot 404 `IRelationType` (`0x10299da0`) and its four species arms, `CanBeFedUponBy`
// (slot 342) with the `CBaseCombatCharacter` body it chains, the seven `TaskFail` species arms in
// front of story 13's slot-448 body, the condition-debug string builder (dead, deleted 0019/6),
// `CanWitnessSupernatural` (slot 587), the door-failure slot 532, slot 35's gendered id,
// `UpdateBurstShootPause` (slot 419) and `ResetFakeReloadCount` (`0x102c54c0`).
//
// **The standing fact of this family.** A flag-word writer is observable by the program that reads
// the word, so the mask, the order of a set against a clear, and whether a bit is written before or
// after a dispatch are all reproduced from the LISTING rather than from the decompiler's folded
// constants. Every such mask below carries the instruction address that tests or writes it.

// --- Slot 404 `IRelationType` — one slot, five retail bodies -----------------------------------
//
// `FElysiumNpc::IRelationType()` (the generated virtual, defined by this family) is the Troika
// line's slot; every species body is its class's C++ override. Eight census rows fill the slot with
// FIVE distinct bodies, and two of the five chain nothing: `0x103a48b0` (`CNPC_VPlayerController`,
// inherited by `CNPC_VFrenzyShadow` and `CNPC_VWolfMorph`) is
// `FElysiumNpcPlayerController::IRelationType` (fold A2), and `0x103a01b0` (`CNPC_VNewscaster`) is
// an eight-byte `return 4;`.
//
// **Two kinds of chain, and that is the recovered shape.** All four species arms end
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

/** `CBaseCombatCharacter::IRelationType` — the relationship-table tail the Troika body ends at, and
 *  the same store family **Senses10**'s `IRelationTypeOf` reads. Kept as one named arm so both
 *  readers cannot drift. */
int32 BaseCombatCharacterIRelationType(const FElysiumEntity* Candidate) const;

// --- Slot 342 `CanBeFedUponBy` ------------------------------------------------------------------

// --- Slot 448 `TaskFail` — the seven species arms in front of story 13's body -------------------
//
// `FElysiumNpc::TaskFail` (`ElysiumNpc.cpp`) runs the whole `CAI_BaseNPCTroika::TaskFail`
// (`0x1029adb0`) chain. Every species body in the census runs its own arm and then calls
// `0x1029adb0` directly and UNCONDITIONALLY: each is its class's `TaskFail` override (story 5
// step 3), which runs the arm below and then `FElysiumNpc::TaskFail`.

// --- The species words those arms write ---------------------------------------------------------
//
// Every one of these is a SPECIES-ONLY datamap member with no row in this runtime's shape map,
// carried on the combined NPC until story 5 step 4 moves it to its species class. Each is the one
// word its species would have, named for the retail field, so the write is observable.


/** `0x10382970` (Hengeyokai, `m_BlacklistedEntities` at `+0x66a4`) and `0x103bf200` (Tzimisce,
 *  `m_FailedPickupTargets` at `+0x6690`) — byte-identical `CUtlVector<BlacklistedEntity_t>` appends
 *  of `(handle, curtime + 20.0)`, the 20.0 being `_DAT_1044eb0c`. **NOT a release**: the target is
 *  SHUNNED for twenty seconds, which is this family's second correction to the walk. */
static constexpr float SpeciesBlacklistSeconds = ElysiumNpcTunables::Twenty;   // _DAT_1044eb0c
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

/** `0x102bf7e0` — the door cleanup the navigator arm runs: `if (nav->IsGoalSet()) nav->StopMoving();
 *  m_bShouldMove = 1;`. The `IsGoalSet` test is made TWICE in retail (once by the caller at
 *  `102905da` and once here), and both are reproduced. */
void NavigatorDoorCleanup();

// --- Slot 419 `UpdateBurstShootPause` -----------------------------------------------------------

/** `0x102c5780` / `0x102c57c0` — the weapon-data words `m_flBurstShootPauseMin` and
 *  `m_flBurstShootPauseMax` take, `wpndata + 0x264` and `wpndata + 0x268`, each routed through
 *  `0x102c5570` with the weapon data resolved by `0x102517e0`: the item mode's
 *  `NpcAttackRateMin` / `NpcAttackRateMax` (spec 0002 V5a). False with no active weapon, where
 *  slot 419 takes retail's own UNARMED arm — the two literals 0.3 and 0.5. */
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

