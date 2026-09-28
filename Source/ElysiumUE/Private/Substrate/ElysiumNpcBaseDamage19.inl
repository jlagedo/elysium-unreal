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


/** Recording surface for the motor pair `CAI_BaseNPC::OnTakeDamage_Alive` runs with a live enemy
 *  (`0x10266186` `0x102e0b40(m_pMotor)`, the yaw-speed hold, then `0x102661b9`
 *  `0x102e2020(m_pMotor, &lkp, 0)`, the set-ideal-yaw-to-target). Both are motor words this
 *  substrate's `IElysiumNpcMotor` does not carry (families Conditions, Hints and Lifecycle19 stand
 *  the same two calls as no-op seams); the face half goes through family Hints' `SetMotorHintYaw`
 *  seam, and these three words are what makes the arm measurable. */
int32 OnTakeDamageAliveMotorResets = 0;

int32 OnTakeDamageAliveMotorFaceCalls = 0;

FVector OnTakeDamageAliveMotorFaceTargetCm = FVector::ZeroVector;
