// `CAI_BaseNPC`'s declarations of the `Lifecycle` family (story 5 step 5),
// moved from `ElysiumNpcLifecycle*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseLifecycle.cpp`.

/** `CAI_Hint::ScriptHide` (`0x102d0860`) — the base `CBaseEntity::ScriptHide` and then the hint's
 *  own `m_iDisabled` (`+0x05e8`) := 1. Pure over family **Hints**' `FHintWords`, because there is no
 *  `CAI_Hint` ENTITY in this substrate to run the base half on; the caller owns that half. */
static void HintScriptHide(FHintWords& Hint);

/** `CAI_Hint::ScriptUnhide` (`0x102d0890`) — the exact inverse, `m_iDisabled := 0`. */
static void HintScriptUnhide(FHintWords& Hint);

/** `CAI_Hint::Kill` (`0x102d08c0`), whose whole body is `JMP [[this]+0x134]` — vtable `+0x134` is
 *  slot 77, so a hint node's **Kill is its ScriptHide**, not the entity teardown every other class
 *  runs at slot 119. Verbatim: this forwards. */
static void HintKill(FHintWords& Hint);

/** `CAI_Hint::Spawn` (`0x102d0b60`) — a hint node's own spawn: fill the unset per-hint-type
 *  defaults, turn the angle range into its dot-product test, and fold the group id into a bit.
 *  Pure over family **Hints**' `FHintWords`, which is the hint's datamap. */
static void HintSpawn(FHintWords& Hint);

/** The `angle` arm's rewrite (`0x1009e430` @ `1009e6f6`): a NEGATIVE value takes a literal, and any
 *  other value becomes `"<current pitch> <value> <current roll>"` — the yaw only. Retail then
 *  re-enters the cascade as `angles`. */
static FString RewriteAngleKey(float AngleValue, const FVector& CurrentAngles);

/** `CAISound::OnRestore` (`0x100aa5a0`), slot 130 — the asm is
 *  `MOV [ESP+4], 0 / JMP [[this]+0x18]`: it overwrites its own argument with 0 and tail-jumps to
 *  slot 6 `SetCheckUntouch`, so **a restore always lands as `SetCheckUntouch(false)` whatever the
 *  caller passed**. Answers the value forwarded, which is always false. */
static bool OnRestoreForwardsCheckUntouch(bool bCallerValue);

/** `CAI_BaseNPC::UpdateOnRemove` (`0x1027ca30`), slot 180 for the NON-Troika branch — the squad
 *  unlink (`+0x5da4`), the hint release (`+0x5ddc`, a 0.0-delay release), the own vtable `+0x7fc`
 *  dispatch, then `CBaseCombatCharacter::UpdateOnRemove`. In that order. */
void BaseNpcUpdateOnRemove();

/** `CAISound::FUN_10026e70`, slot 153 for the five AI-helper classes — is `m_vecVelocity`
 *  (`+0x03d4`) different from the static default vector `DAT_1070d1b0/b4/b8`? That vector is the
 *  always-zero one `GetGroundVelocityToApply` (slot 210) answers, so this is "am I moving at all". */
bool HasNonDefaultVelocity() const;


/** `thunk_FUN_101618a0(player)` — the `!playercontroller` half of slot 559 `FindNamedEntity`
 *  (`0x10279090`): resolve the player's `m_hControllerNPC` (`+0x1db0`) with its serial check, or
 *  null. The port holds that handle on the world (`FElysiumEntityWorld::PlayerControllerHandle`,
 *  written by `CreatePlayerControllerEntity`, retail `GetControllerNPC` `0x10161a70`). */
FElysiumEntity* PlayerControllerOf(FElysiumEntity* Player) const;
