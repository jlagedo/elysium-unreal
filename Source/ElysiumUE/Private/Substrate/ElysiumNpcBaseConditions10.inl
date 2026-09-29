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

/** `0x102ee2e0` — the navigator's PAUSED byte, `path+0x10` `m_bPaused` (`Navigator.IsPaused()`), which
 *  `0x102bf7e0` and the `TASK_WAIT_FOR_MOVEMENT` arms read as `if (path+0x10)` before clearing it
 *  (`0x102ee2c0` -> `0x1030bea0`). The name is the port's old one, from a misreading of `0x102ee2e0`
 *  as `IsGoalSet`; the goal-type test is `NavigatorIsGoalSet` (`0x102ee680`). Kept for the callers
 *  outside the navigator's files; it answers exactly `NavigatorIsPaused`. Nothing writes the byte
 *  yet (the setter `0x102ee2a0`'s callers are not recovered), so it answers false. */
bool NavIsGoalSet() const;
