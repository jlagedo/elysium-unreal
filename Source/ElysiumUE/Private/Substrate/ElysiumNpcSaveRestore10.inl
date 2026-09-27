// Story 29d, family **SaveRestore10 + Lifecycle10** — the declarations of slots 126 `Save`,
// 127 `Restore`, 180 `UpdateOnRemove` and 106 `PostConstructor`, plus the non-slot bodies beside
// them.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual and this family only defines it. The four virtuals
// this family fills are `Save`, `Restore`, `UpdateOnRemove` and `PostConstructor`; everything below
// is the half the generator does not declare — the `CAI_BaseNPC` base bodies beside them, the
// species arms, the sentinel codec all three share, and the seams they go through.
//
// The definitions are in `Substrate/ElysiumNpcSaveRestore10.cpp` and the tests in
// `Tests/ElysiumNpcKernelSaveRestore10Tests.cpp`. The walked prose is
// `docs/vtmb/npc-ai/lifecycle.md` § "Story 29d, family SaveRestore10", and the non-slot half's is
// § "Story 29d, family Lifecycle10".
//
// --- What this family is --------------------------------------------------------------------------
//
// **Save is a sentinel codec, and that is the whole shape of the family.** Twelve of the sixteen
// `SaveRestore10` rows are one pair of retail functions applied to a list of `FIELD_TIME` stamps:
// `0x101cf250` rewrites a stamp to `1e+11` when it matches its mode's "unset" value, and
// `0x101cf2f0` reverses it. Everything around them — which field, which mode, and that the encode
// runs BEFORE the archive call and the decode AFTER it — is retail's rule, which is why the three
// species bodies a batch verdicted `mechanism` were re-verdicted `rule` by review: a mechanism
// routed through a service seam is not what they are, they are a retail-chosen sentinel applied to
// named offsets in a recovered order.
//
// **Why a disposable save file does not make this moot.** `CLAUDE.md` says save games are
// disposable and no migration is wanted. What is reproduced here is not a file format: it is the
// ORDER in which a program observes its own fields change and the VALUES they take while the
// archive runs. A retail body that read `m_flEyeFidgetTime` between the encode and the decode would
// see `1e+11`, and any body that runs while `Save` is on the stack is that body.
//
// --- The three corrections this family's reading made to the checklist's walks --------------------
//
//   * **`0x1027bc60`'s "4 timers and 3 timers" is a misread MODE.** `thunk_FUN_101cf250` takes
//     `(float*, mode)`, never `(first, count)`. The listing (`1027bc6c` `PUSH 0x4` / `1027bc80`
//     `PUSH 0x3`) shows exactly TWO encode calls: `m_flExtendedBlockedByFriendTimer` (`+0x5b8c`)
//     with mode **4** and `m_flWaitFinished` (`+0x5db4`) with mode **3**. `0x1027c160`
//     (`CAI_BaseNPC::Restore`, band 5–9, ported by story 29c-1 as `RestoreExtendedHeader`) carries
//     the same two calls and was read the same wrong way; both are corrected in this story, and the
//     `RebaseRestoredStamp` helper 29c-1 coined for a re-base that retail does not perform is gone.
//   * **`0x1023f040`/`0x1023f0c0`/`0x1023f060` are not a bit-vector copy: they are CRC32.**
//     `0x1023f040` writes `0xffffffff`, `0x1023f0c0` is an unrolled table-driven CRC32 over
//     `DAT_10496f58`, and `0x1023f060` complements. So `AIExtendedSaveHeader_t`'s last word is a
//     CHECKSUM of the running schedule's task array (`schedule+0x20`, `schedule+0x24 << 3` bytes —
//     an 8-byte `Task_t` per task), not a copy of its interrupt bits.
//   * **`0x102993c0`'s decode pass is NOT shifted where it matters.** The decompiled C renders the
//     eleven decode pointers one slot out (EBX/stack aliasing) and the nine sound pointers two
//     slots out, but the MODE arguments are in the listing in their own right and read
//     `3,2,2,3,3,3,3,2,2,4,4` — identical to the encode list. The decode therefore walks the same
//     eleven fields in the same order with the same modes; nothing is shifted in the ported body.
//
// --- The `.rdata` cells, read out of the pinned image ---------------------------------------------
//
// `_DAT_104454c4` = **0.0** (already settled by story 29c-1, family Geometry) and
// `_DAT_10482fac` = **1e+10**, read at file offset `0x482fac` of the pinned `vampire.dll`
// (base `0x10000000`, `.rdata` VA `0x10445000` at file offset `0x445000`). The encode writes
// `1e+11` and the decode catches anything at or above `1e+10`, so the sentinel has a decade of
// headroom over any stamp a running game could hold.

// --- The sentinel codec ---------------------------------------------------------------------------

/** `FUN_101b9840` (`0x101b9840`) and `FUN_101b9860` (`0x101b9860`) — the nine-times-repeated
 *  wrappers whose whole body is the codec at mode **2** over a `CSound`'s `+0x10 m_flExpireTime`
 *  (`FIELD_TIME`, `vtmb_fields CSound`). This runtime's `CSound` is `FElysiumGameSoundEvent` and
 *  its `+0x10` is `ExpireTime`. */
static void SaveSoundStampEncode(struct FElysiumGameSoundEvent& Sound);
static void SaveSoundStampDecode(struct FElysiumGameSoundEvent& Sound);

// --- The CRC32 `AIExtendedSaveHeader_t`'s last word carries ---------------------------------------

// --- The archive seam ------------------------------------------------------------------------------
//
// Retail's slot 126 takes an `ISave&` and slot 127 an `IRestore&`; the generated signature spells
// both `void*` because `signatures.tsv` has no port type to name. This runtime's archive is
// `FElysiumSaveArchive` (`ElysiumSaveArchive.h`) and that is what the `void*` IS — every write
// below goes through it when one is handed in, and is recorded either way.
//
// **NOTHING IN THIS RUNTIME CALLS `Save()` OR `Restore()` YET, and that is deliberate.** This
// port's persistence is `FElysiumNpc::Serialize` (`ElysiumNpc.cpp`), a block-per-subsystem record
// written by the snapshot applier; wiring slot 126 into it would ADD the sentinel encode/decode
// events to a path retail does not have here and would change a payload format for no observable
// gain. The bodies are ported whole and driven by `Elysium.Substrate.NpcKernelSaveRestore10.*`, the
// same posture family Precache10 took for slot 104 and for the same reason.

/** `ISave` vtable `+0x30` / `+0x28` — the single `bool` and the two `int`s the Troika body writes
 *  between the encode and the decode. */
void SaveWriteBool(void* Archive, const TCHAR* Field, bool bValue);
void SaveWriteInt(void* Archive, const TCHAR* Field, int32 Value);

/** `IRestore` vtable `+0x44` / `+0x3c` — the mirror pair slot 127 reads. `RestoreReadBool` answers
 *  the value read; with no archive it answers **false**, which is the arm that skips the two
 *  `ReadInt`s, and is what a fresh runtime's record holds. */
bool RestoreReadBool(void* Archive, const TCHAR* Field);
void RestoreReadInt(void* Archive, const TCHAR* Field, int32& Value);

// --- Slot 126 `Save` -------------------------------------------------------------------------------

/** `CAI_BaseNPCTroika::Save` (`0x102993c0`) — slot 126's own body, without the species prologue.
 *  `Save()` is the slot; this is what it runs on the Troika line, and what a species
 *  class's override calls directly.
 *
 *  Eleven stamps encoded in retail's order and modes, then nine `CSound` expiry stamps at mode 2,
 *  then `FElysiumNpcBase::Save`, then one `bool` and (when it is true) two `int`s through the archive, then the
 *  same eleven and the same nine decoded in the same order. The return is `FElysiumNpcBase::Save`'s. */
int32 TroikaSave(void* Archive);

// --- Slot 127 `Restore` ----------------------------------------------------------------------------

/** `CAI_BaseNPCTroika::Restore` (`0x10299700`) — slot 127's own body. `CAI_BaseNPC::Restore`
 *  (`0x1027c160`, story 29c-1's `RestoreExtendedHeader`) runs FIRST and ITS answer is what this
 *  returns, unchanged; then one `ReadBool` whose truth gates two `ReadInt`s into
 *  `m_iRestorePedLinkNode` (`+0x6310`) and `m_iRestorePedLinkDestNode` (`+0x6314`); then the same
 *  eleven stamps and nine sounds `TroikaSave` encodes, decoded in the same order with the same
 *  modes. */
int32 TroikaRestore(void* Archive);

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

// --- Slot 106 `PostConstructor` --------------------------------------------------------------------
//
// `CAI_BaseNPC::PostConstructor` (`0x1027bb20`) is 27 bytes whose entire content is an ORDER:
// `CBaseCombatCharacter::PostConstructor(name)` runs to completion FIRST, and only then does this
// object dispatch its own slot `0x6a0` — `0x6a0 / 4` = slot **424**, `CreateComponents`. So the
// NPC-side post-construct pass observes everything the base pass built and nothing it has not, and
// no state is written here directly. Slot 424 is family Lifecycle's, already ported, and is
// DISPATCHED here rather than re-recovered.
//
// `PostConstructor` is a Troika-line slot, so its body is the generated virtual and is defined —
// not declared — in this family's `.cpp`. The base half is named here because it is a distinct
// retail function: `CBaseCombatCharacter::PostConstructor` (`0x100035e4` -> the chain's) sets the
// entity's classname and registers it, which this runtime's `FElysiumEntity::Construct` has already
// done by the time any NPC stands.

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

