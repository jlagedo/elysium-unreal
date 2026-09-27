// `CAI_BaseNPC`'s declarations of the `Conditions10` family (story 5 step 5),
// moved from `ElysiumNpcConditions10*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseConditions10.cpp`.

/** `CBaseCombatCharacter::CanBeFedUponBy` (`0x10339800`), 237 bytes — slot 342's base body, which
 *  the Troika override chains to. The FEEDER ARGUMENT IS NEVER READ: every one of its five terms is
 *  about the victim. In order: `CanBeFedUpon()` (`0x10339a90`), `NOT_FEEDABLE` (`m_bfAINPCFlags2 &
 *  0x8000000`) clear, no live grapple (`m_GrapplePartner` resolves AND `m_GrappleRole != -1`
 *  refuses), `IsAlive()` (slot 158, `vtable +0x278`) and `!IsUnconscious()`. */
// Declared by the generated slot surface (`ElysiumNpcBaseSlots.inl`, slot 342); defined by hand as
// `FElysiumCombatCharacter::CanBeFedUponBy`.

/** `0x102ee2e0` — `CAI_Navigator::IsGoalSet()`, `m_pPath(+0x30)->GoalType(+0x10) != 0`. Distinct
 *  from `0x102ee680` (`IsGoalActive`, the current-waypoint test) which family Motor already wires.
 *  **SEAM**: this runtime's mover carries ONE goal latch and no separate goal-type word, so this
 *  answers that latch — the admitting value, since `IsGoalActive` implies `IsGoalSet`. The one case
 *  it under-admits is a goal set with no current waypoint, which the mover cannot represent. */
bool NavIsGoalSet() const;
