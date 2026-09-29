// Story 29d, family **SaveRestore10 + Lifecycle10** — the declarations beside slot 180
// `UpdateOnRemove` (slot 106 `PostConstructor` closed at UE component construction, 0019/6), plus the non-slot bodies beside them.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual and this family only defines it. The virtuals this
// family fill is `UpdateOnRemove`; everything below is the half the
// generator does not declare — the bodies beside them and the seams they go through.
//
// The definitions are in `Substrate/ElysiumNpcSaveRestore10.cpp` and the tests in
// `Tests/ElysiumNpcKernelSaveRestore10Tests.cpp`. The walked prose is
// `docs/vtmb/npc-ai/lifecycle.md` § "Story 29d, family SaveRestore10", and the non-slot half's is
// § "Story 29d, family Lifecycle10".
//
// --- What is gone ---------------------------------------------------------------------------------
//
// Slot 126 `Save` (`CAI_BaseNPCTroika::Save`), slot 127 `Restore`, the
// `CSound` stamp wrappers (`0x101b9840` / `0x101b9860`), the pedestrian-link bool/int archive calls
// and every species `Save` / `Restore` twin were deleted in 0019/6 (verdict `mechanism`, service:
// the generated SAVE walk of 0019/2, `FElysiumSaveArchive`). Nothing in this runtime ran them:
// persistence is `Freeze` / `ApplySnapshot`, and the load-side logic a species twin carried runs
// from that class's `OnPostRestore` (retail slot 130), in retail's order. The base layer's
// `Restore` and its sentinel codec went the same way; `Save` `0x1027bc60` forwards to
// the NPC record's `AIExtendedSaveHeader_t` block (`FElysiumNpcBase::SerializeExtendedHeader`).

// --- Slot 180 `UpdateOnRemove` ---------------------------------------------------------------------

/** `CAI_BaseNPCTroika::UpdateOnRemove` (`0x1028d6e0`) — slot 180's own body, read off the listing.
 *  Six steps in order:
 *    1. `ClearHintNode(this, 5.0)` — a **5.0-second** reuse delay, where the base `0x1027ca30`
 *       (story 29c-1's `BaseNpcUpdateOnRemove`) passes 0.0. That difference is the whole reason the
 *       two bodies are distinct.
 *    2. slot `0x964` (**601**) dispatched with `this`, only when `m_pAttackCoordinator` (`+0x65e8`)
 *       is non-zero — the melee-coordinator release.
 *    3. `LeaveInterestingPlace(0, "Leaving interesting place (UpdateOnRemove)")` — the string is
 *       `0x105d87d4`, read off the listing at `1028d702`, and it settles what `0x102b53d0` is.
 *    4. `if (IsInDialog()) StopDialog()` — `0x102c1170` then `0x102c0bb0`.
 *    5. free `m_sppPatrolPath` (`+0x658c`) and then `m_sppPatrolPathHunt` (`+0x6594`), in that
 *       order, through `0x1029f5d0`.
 *    6. tail-jump to `CAI_BaseNPC::UpdateOnRemove`. */
void TroikaUpdateOnRemove();

/** `FUN_102b53d0` (`0x102b53d0`) — the interesting-place release, which the checklist's walk called
 *  "the grapple release". It is not: `+0x62ec` is `m_pInterestingPlace`, `+0x62e8`
 *  `m_bInterestingPlaceArrived` and `+0x6304` `m_eInterestingPlaceMode`
 *  (`ElysiumNpcKernelShape.cpp`), and the listing's own string argument is
 *  `"Leaving interesting place (UpdateOnRemove)"`.
 *
 *  Retail: with a place held, re-check it (`0x10299a80`), emit two sounds through a
 *  `CPASAttenuationFilter` (channel 4, pitch `0x24`, volume 100), detach through `0x102da600` with
 *  a flag computed from `m_bInterestingPlaceArrived` and `0x100cd660`, clear the place and the
 *  mode, strip `m_bfAINPCFlags` bit `0x20000000` and `m_bfAINPCFlags2` bits `0x08000008`, call
 *  `0x102ae310`; and ALWAYS, place or no place, clear `m_bInterestingPlaceArrived` last.
 *
 *  This runtime already owns that transaction as `FinishAmbientUse(bFireLeft, bStopMovement)`, so
 *  this is the call site and not a second copy of the body — `bAmbientArrived` is
 *  `m_bInterestingPlaceArrived` and it is the `bFireLeft` argument, which is retail's own
 *  `0x100cd660` flag. */
void LeaveInterestingPlaceOnRemove();

/** How many times the release above ran. The call is unconditional in retail — the place check is
 *  INSIDE `0x102b53d0`, not at its call site — so this counts the Troika body's passes as well as
 *  the releases, which is what a species arm's "did it chain?" case reads. */
int32 InterestingPlaceReleases = 0;

/** `FUN_102c0bb0` (`0x102c0bb0`), the dialogue stop `UpdateOnRemove` runs when `IsInDialog()` says
 *  yes: stop sound channel 5 on this entity, dispatch slot `0x44c` (**275**), then
 *  `CBaseCombatCharacter::FadeoutExpressions`, `CAI_BaseNPCTroika::FinishTalking`, and — when slot
 *  `0x994` (**613**) allows and the running schedule is not already `0xf1` — install schedule
 *  `0xf1` with a fresh think.
 *
 *  **SEAM**: this runtime's dialogue teardown is `FElysiumNpcDialogue`'s session end and its
 *  `TalkingUntil` stamp, not a schedule install; schedule `0xf1` has no registered id here. What
 *  the body does that this substrate CAN state is end the talking window, which is what it does —
 *  the schedule install is counted and named. */
void StopDialogOnRemove();
int32 DialogStopScheduleRequests = 0;

// --- `FUN_10290350` — `RunAlternateAI` mode 4, the door-blocked transaction ------------------------

/** `FUN_10290350` (`0x10290350`), the fourth arm of the door transaction family Conditions carries
 *  `EnterAlternateAi` and `RunAlternateAiOpeningDoor` (mode 1) for. Retail, in order:
 *
 *    1. `m_bForceMaintainActivity` (`+0x65fa`) := 1 across `CAI_BaseNPC::MaintainActivity`
 *       (`0x102727d0`), then := 0. The latch spans exactly that one call.
 *    2. When `m_hOpeningDoor` (`+0x5d24`) still resolves AND that door's `m_toggle_state`
 *       (`+0x4f8`) is 0 (fully closed): stop the motor (`0x102bf7e0`, family Motor's
 *       `ResumeScheduledMove`) and `m_eAlternateAI` (`+0x644c`) := 0.
 *    3. A hull trace from slot 217 `GetAbsOrigin()` to that origin plus `m_vecForward`
 *       (`+0x6290`) scaled by `_DAT_10451acc` — **64.0**, read out of the pinned image — with mask
 *       `0x202400b` and radius `100.0`, through the filter at `+0x5d40`. On a HIT: stop the motor,
 *       `m_hOpeningDoor` := -1, `m_bOpeningDoorWait` (`+0x5d30`) := false, `m_eAlternateAI` := 0.
 *    4. Once `curtime` has reached `m_flAlternateAIExpireTimer` (`+0x6450`) — the test is
 *       `curtime < timer` and the ELSE arm fires, so an equal stamp expires — dispatch slot `0x700`
 *       (**448**) `TaskFail` with `0xe` and clear the same three fields.
 *
 *  Always answers **true** (`CONCAT31(..., 1)`), so the transaction keeps the body. */
bool RunAlternateAiDoorMode4(double Now);

/** SEAM for `CAI_BaseNPC::MaintainActivity` (`0x102727d0`), the one call `m_bForceMaintainActivity`
 *  brackets. Retail's body re-publishes the ideal activity onto the body when the current sequence
 *  has finished (or when the force latch is up, which is what step 1 raises it for).
 *
 *  **Not a vtable slot** (`vtmb_func 0x102727d0`: `__thiscall`, no dispatch site), so it takes its
 *  own name here. This runtime publishes locomotion and reaction bands from the animation layer
 *  rather than from an NPC-side maintain pass, so there is no ideal activity to push and this
 *  answers nothing. It is COUNTED, because the observable half at this call site is that the latch
 *  was up across exactly one call and down on either side of it. */
void MaintainActivity();
int32 MaintainActivityCalls = 0;
/** The value `m_bForceMaintainActivity` held while `MaintainActivity` last ran — the whole reason
 *  step 1 exists, and what a case asserts instead of a body this substrate does not have. */
bool bForceMaintainActivitySeenByLastMaintain = false;

/** SEAM for the hull trace at step 3 — `thunk_FUN_102e6d70(filter, 0, start, end, 0x202400b, 0,
 *  100.0, 0, &trace, 0, 0)`. This runtime's move solver has no radius-hull sweep against an
 *  arbitrary content mask at kernel level, so this answers **false** (no hit), which is retail's
 *  own "the way ahead is clear" arm and leaves the expiry arm as the one that ends the
 *  transaction. Counted, so a case can state that the trace was asked for. */
bool AlternateAiDoorSweepHit(const FVector& StartCm, const FVector& EndCm);
int32 AlternateAiDoorSweeps = 0;

/** `m_toggle_state` (`CBaseDoor +0x4f8`), the one word step 2 reads off the door it is holding.
 *  **SEAM**: this runtime's doors are `FElysiumMover` and carry their own phase rather than
 *  Source's four-state toggle; `0` is `TS_AT_TOP`… retail's own numbering makes `0` the CLOSED
 *  rest state for a door that opens upward, which is the value this answers for a mover at rest.
 *  Named so the day the mover's phase is mapped the read moves with it. */
int32 OpeningDoorToggleState() const;

// --- The species words this family's bodies read ---------------------------------------------------
//
// Each carries its offset and the retail class that owns it, the convention family Lifecycle set:
// one offset means a different thing per class, and `+0x6674` alone is five different fields.

