// `CAI_BaseNPC`'s declarations of the `Anim10` family (story 5 step 5),
// moved from `ElysiumNpcAnim10*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseAnim10.cpp`.

/** `+0x5cd8 m_bKeepSound` — the byte the Troika `SetActivity` raises around its random pick so the
 *  activity-change listener `0x101f6010` is NOT told about the intermediate activity. It is the one
 *  member this family adds to the shape; `ElysiumNpcKernelShapeMap.cpp` binds `+0x5cd8` already. */
bool bKeepSound = false;

/** `CAI_BaseNPC::SetActivityAndSequence` (`0x10272490`) — the commit. 243 bytes, and every one of
 *  its six effects is ordered against the others:
 *
 *    1. `m_TranslatedActivity` (+0x0ff4) is written FIRST, before anything can read it.
 *    2. A NEGATIVE sequence takes `ResetSequence(0)` and SKIPS the cycle, the duration and the
 *       weapon activity entirely.
 *    3. Otherwise the cycle word `m_flCycle` (+0x06f8) is zeroed UNLESS the sequence equals
 *       `m_nSequence` (+0x06f0) with `m_bSequenceFinished` (+0x065d) set, OR the old activity
 *       (+0x0fec) and the new one are BOTH in `{9 ACT_WALK, 0x13 ACT_RUN}` — retail's walk/run cycle
 *       carry, which is what keeps a footfall in phase across a gait change.
 *    4. `ResetSequence(seq)`, `CBaseAnimating::SequenceDuration(seq)`, then
 *       `CBaseCombatCharacter::Weapon_SetActivity(weaponAct, duration)`.
 *    5. slot 533 `EyeOffset(activity, m_TranslatedActivity)` feeds `SetViewOffset` (`0x1009f380`) —
 *       for EVERY request, including the negative-sequence one.
 *    6. slot 465 `OnChangeActivity(activity)` fires only when `m_Activity` DIFFERS from the new
 *       activity; the activity-change listener `0x101f6010` fires only when `m_Activity` differs
 *       from `m_TranslatedActivity` AND `m_bKeepSound` is clear. **Two different comparisons**, and
 *       both read `m_Activity` BEFORE step 7 overwrites it.
 *    7. `m_Activity = activity`, then the navigator is notified (`0x102e1cf0`).
 *
 *  `+0x065d` is the byte AFTER family Anim's `bSequenceFinished` (+0x065c). The listing reads
 *  `+0x65d`, so it is carried as its own member rather than folded into the neighbouring one. */
bool bSequenceLoopedOnce = false;   // +0x065d

/** `CBaseCombatCharacter::Weapon_SetActivity(activity, duration)`. **SEAM**: the port's weapon
 *  activity lives on the item's own clip set and there is no per-weapon activity word on the NPC;
 *  the request is recorded with the duration `SequenceDuration` answered, which is what makes the
 *  step-4 ORDER assertable. */
struct FWeaponActivityRequest
{
	int32 Activity = 0;
	float Duration = 0.f;
};

TArray<FWeaponActivityRequest> WeaponActivityRequests;

/** `thunk_FUN_1009f380(this, &offset)` — `CBaseEntity::SetViewOffset`, which slot 533 `EyeOffset`
 *  feeds on EVERY commit, including the negative-sequence one.
 *  **SEAM**: this runtime has no writable `m_vecViewOffset` word — `FElysiumEntity::EyePosition()`
 *  derives the eye from the character's standing view height rather than from a stored offset — so
 *  the write is recorded. The recorded half is what matters here: that slot 533 is dispatched once
 *  per commit, with the NEW translated activity beside the requested one. */
TArray<FVector> ViewOffsetWrites;

/** `thunk_FUN_101f6010(&DAT_1073dd58, this, newTranslated, oldActivity)` — the global
 *  activity-change listener list. **SEAM**: no such registry exists here. The notice is RECORDED,
 *  because WHICH edge fires it (and that `m_bKeepSound` suppresses it) is the recovered rule. */
struct FActivityChangeNotice
{
	int32 NewTranslatedActivity = 0;
	int32 OldActivity = 0;
};

TArray<FActivityChangeNotice> ActivityChangeNotices;

/** `thunk_FUN_102e1cf0(m_pNavigator)` — `CAI_Navigator::OnNewActivity`. **SEAM**: story 29c-1's
 *  family Motor found no `CAI_Navigator` object in this substrate at all; the notice is counted so
 *  the tail of every commit is assertable. */
int32 NavigatorActivityNotices = 0;

void SetActivityAndSequence(int32 Activity, int32 Sequence, int32 TranslatedActivity,
	int32 WeaponActivity);

/** `AdvanceToIdealActivity` (`0x102726a0`), the non-virtual helper `MaintainActivity` ends in.
 *
 *  `FindTransitionSequence(m_nSequence, m_nIdealSequence)`; a `-NAN` answer (retail's own sentinel,
 *  which the listing produces by comparing the returned float against itself) dispatches slot 310
 *  with `m_IdealActivity` and stops. A transition sequence that is NOT the ideal one commits
 *  activity **2** with that sequence, re-resolving the translated/weapon pair from the transition's
 *  own activity when it has one. Only when the transition IS the ideal sequence does the ideal
 *  activity commit. */
void AdvanceToIdealActivity();

/** `CAI_BaseNPC::MaintainActivity` (`0x102727d0`). Gated on slot 466 `ShouldMaintainActivity`;
 *  inside, work happens only when `m_Activity` differs from `m_IdealActivity` OR `m_nSequence` from
 *  `m_nIdealSequence`. Activity 2 is the special arm — it waits for `m_bSequenceFinished` and then
 *  runs `AdvanceToIdealActivity` and NOTHING else; every other activity first re-resolves the ideal
 *  through `0x10272130` and then advances. The body writes no field of its own.
 *
 *  **Not a vtable slot** — `vtmb_func 0x102727d0` reports `__thiscall` with no dispatch site — so it
 *  takes its own name. Family **SaveRestore10** had already declared `MaintainActivity()` as a SEAM
 *  that counts the call and records the latch `m_bForceMaintainActivity` was holding; that seam is
 *  the entry point and keeps its bookkeeping, and it now runs THIS body after it instead of
 *  answering nothing. The name here is `BaseMaintainActivity` for the same reason
 *  `FElysiumNpcBase::SetActivity` is: it is `CAI_BaseNPC`'s own function, with no Troika override above it. */
void BaseMaintainActivity();

/** `CBaseAnimating::FindTransitionSequence(from, to, &dir)` — the studio transition graph.
 *  **SEAM**: family Anim recorded that this substrate stands no studio header and no sequence index.
 *  It answers **`To`**, which is retail's OWN no-transition answer — the SDK body returns the
 *  destination sequence unchanged when no transition clip stands between the two — and is the arm
 *  that commits the ideal through `SetActivityAndSequence` directly. See the definition for why the
 *  `-1` sentinel would have stranded a body in `ACT_TRANSITION`. */
int32 FindTransitionSequence(int32 From, int32 To) const;

void WeaponSetActivity(int32 Activity, float Duration);

void SetViewOffset(const FVector& OffsetUnits);
