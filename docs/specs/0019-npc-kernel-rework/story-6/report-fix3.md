# Report fix3 · the wave-3 review findings

(Saved by the orchestrator from the fixer's final answer. Nothing built or run; corpus server down.)

**1 (done).** A move takes its turn rate from the stored word `+0x38` (`MotorYawSpeedWord`), not a fresh `MaxYawSpeed()` per leg (`ElysiumNpcBaseMotor.cpp:958`). New `MotorThinkUpkeep` (`:975`) runs every think from `PerformMovement` (`ElysiumAnimatingSlotBodies.cpp:125`) and re-reads the word, so a mid-leg activity change reaches the turn. New seam `IElysiumNpcMotor::SetYawSpeed` (`ElysiumWorldServices.h:557`, `ElysiumNpcBody.cpp:968`, recording motor). Unrecovered: whether slot 18 `0x102e19e0` rewrites the word every step.

**2 (done except slot 7).** The kernel keeps retail's facing queue: entries with weight and expiry, blended as slot 15 `0x102e2180` blends them (`ElysiumNpcBaseFacing.cpp:36-176`); an entity entry follows the entity each blend; the body receives the blended point each think, only when it changes. The request's three floats were in the wrong order: now `(importance, duration, ramp)` per the SDK and `NPCThink`'s `(1.0, 0.8, 0)`; callers fixed. TroikaHelpers sent the boss position in Source units instead of cm; fixed. **Refused "cancel-current":** slot 7 `0x102e11f0` writes only `+0x30` and reads only `+0x04` (`functions.md`); it is the in-flight jump step `conditions-and-states.md:306` describes, not a facing-queue cancel; `ClearFacingTarget` (`:360`) leaves the queue alone.

**4 (done).** `RegisterMoveIgnores` re-asks slot 69 every think while a move is live, sends only what changed, still asks hidden entities; the comment names the per-contact versus per-think divergence; the set is cleared at all six `Motor->Stop()` sites (a new virtual on `FElysiumScriptedCharacter`) and when a request is refused.

**Tests:** new `Elysium.Substrate.NpcKernelFacing.FacingQueue` (two targets blended, expiry, a moving entity target, slot 7 leaves the queue, re-add); `NpcKernelMotor.RequestSeams` extended (the stored word, the mid-leg rate, ignores re-checked per think, a stop clears them); ThinkTests assertions updated for the argument order.

**Cells:** `InterestCubicThree` (`0x10449258`, 3.0; f32 in the image). **Docs owed:** `shape.md:443` should say slot 7 is the jump step, not a queue cancel; `shape.md:2860` should record the argument order.
