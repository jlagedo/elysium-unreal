// Story 0019/8 (29e under the strict verdict), family **Damage19** -- `CAI_BaseNPC`'s helper
// declarations.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Included inside `class FElysiumNpcBase` by `Substrate/ElysiumNpcBase.h`; the definitions are in
// `ElysiumNpcBaseDamage19.cpp`, or generated in the slot files for a slot body.
//
// Owns (Damage19's `rule` rows): 0x10265ed0 CAI_BaseNPC::OnTakeDamage_Alive, 0x10265e90
// CAI_BaseNPC::OnTakeDamage.

/** SEAM for the ANSWER of `CBaseCombatCharacter::OnTakeDamage_Alive` (`0x103302e0`), the call
 *  `CAI_BaseNPC::OnTakeDamage_Alive` makes at `0x10265ef5` and whose zero return aborts the whole
 *  body at `0x10265efc`. Retail's body has exactly one exit, `0x10330abe`, and it loads `1` on
 *  every path (the dead/invalid guards and the `damage <= 0.0` guard only SKIP the health commit),
 *  so its answer is always 1. The port's slot body is the generated 29e stub
 *  (`ElysiumCombatCharacterSlots.cpp`, `FireCombatCharacterSlot`, answers 0) and the health commit
 *  itself runs through `FElysiumCombatCharacter::TakeDamage`/`CommitDamage` instead. This calls
 *  that slot for its tally and answers retail's 1; when `0x103302e0` is ported the call site goes
 *  back to `FElysiumCombatCharacter::OnTakeDamage_Alive(Info)` directly and this is deleted. */
int32 CombatCharacterOnTakeDamageAlive(void* Info);

/** Recording surface for the motor pair `CAI_BaseNPC::OnTakeDamage_Alive` runs with a live enemy
 *  (`0x10266186` `0x102e0b40(m_pMotor)`, the yaw-speed hold, then `0x102661b9`
 *  `0x102e2020(m_pMotor, &lkp, 0)`, the set-ideal-yaw-to-target). Both are motor words this
 *  substrate's `IElysiumNpcMotor` does not carry (families Conditions, Hints and Lifecycle19 stand
 *  the same two calls as no-op seams); the face half goes through family Hints' `SetMotorHintYaw`
 *  seam, and these three words are what makes the arm measurable. */
int32 OnTakeDamageAliveMotorResets = 0;

int32 OnTakeDamageAliveMotorFaceCalls = 0;

FVector OnTakeDamageAliveMotorFaceTargetCm = FVector::ZeroVector;
