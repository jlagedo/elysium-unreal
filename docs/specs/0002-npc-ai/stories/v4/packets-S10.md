# Packets S10 — the grapple's activity commit and its end, settled (2026-10-04)

Settles packet S9 item 3's unknowns: what `SetGrappleActivity` writes per phase, what
`LeaveGrappleState` resets, what a grappled NPC's AI does, where the loop bit comes from. Read-only on
source; the only file written is this one.

Marks: **(L)** read off the listing / decompile in this sitting; **(D)** cited from an existing
`docs/vtmb` section, not re-read; **(I)** inferred; **(P)** the port's source, read.

Offsets (datamap, `CAI_BaseNPC`): `m_Activity +0xfec`, `m_IdealActivity +0xff0`,
`m_TranslatedActivity +0xff4`, `m_nSequence +0x6f0`, `m_flCycle +0x6f8`, `m_bSequenceLoops +0x65d`,
`m_bSequenceFinished +0x65c`; grapple block `+0x1538` partner, `+0x153c` role, `+0x1540` type,
`+0x1544` position.

## 1. `CBaseCombatCharacter::SetGrappleActivity` `0x1032a100` (thunk `0x10002626`), whole

One argument: the **base** grapple activity. It is not a vtable slot; every caller is listed below.

1. Resolve the pair from `this`: role 0 with a live partner → `this` is the attacker, partner the
   victim; else (role 1) the reverse; role `-1` → return, nothing written. Either pointer null →
   return (L).
2. `attAct = TranslateBaseGrappleActivity(attacker, base, -1, 0, -1)` (`0x1032a28e`), `vicAct =` the
   same on the victim (`0x1032a29e`) (L).
3. **Both** results go through slot 381 (`+0x5f4`, `Weapon_TranslateActivity 0x10327ec0`)
   **dispatched on the attacker** (`ECX = ESI` at `0x1032a2ac` and again at `0x1032a2bd`); the
   victim's own slot 381 is never called (L). The base body hands the activity to the active weapon's
   `+0x5a4` unless the weapon's `+0x19c & 0x40` is set; no weapon → unchanged (L).
4. `attSeq = SelectWeightedSequence(attacker, attAct', -1)` (`0x1032a2cc`), `vicSeq =
   SelectWeightedSequence(victim, vicAct', -1)` (`0x1032a2de`) (L). Selection is **by activity**,
   through the model's own activity → sequence table; no name is looked up.
5. Attacker, `attSeq >= 0` (`0x1032a2f2`..`0x1032a303`): `m_IdealActivity (+0xff0) = base`,
   `m_Activity (+0xfec) = attAct'`, `0x10260a50(attSeq)`, `m_flCycle = 0`. Miss: `Warning("Attacker
   could not find sequence for activity: %s (%s)")`, `EndGrapple(attacker)` (L).
6. Victim, `vicSeq >= 0` (`0x1032a355`..`0x1032a368`): `m_Activity = vicAct'`, `m_IdealActivity =
   base`, `0x10260a50(vicSeq)`, `m_flCycle = 0`. Miss: the "Victim ..." warning and
   `EndGrapple(attacker)` (L). The victim's arm runs even after the attacker's miss ended the pair.

So the two words differ on each body: **`m_IdealActivity` is the untranslated base, the same number
on both bodies; `m_Activity` is the role/size/side cell (after the attacker's weapon translate)**.
`m_TranslatedActivity`, `m_nIdealSequence`, the NPC state and the schedule are not written (L).

`0x10260a50` (L): `m_nSequence = seq`, then `ResetSequenceInfo 0x10090950`. No argument but the
sequence.

### `TranslateBaseGrappleActivity` `0x10328380` (L)

`role = -1` → own `+0x153c`; `partner = 0` → own `+0x1538`; `position = -1` → own `+0x1544`. No live
partner, no role, or position `-1` → the base is returned untranslated. Otherwise

```
translated = base + (GetGrappleSize(partner) ? 2 : 1) + (role == 1 ? 2 : 0) + (position == 1 ? 4 : 0)
```

`GetGrappleSize 0x103282e0` is `IsMale(partner)` and nothing else (L): "tall" means the **partner is
male**; no height is measured.

Each family is therefore nine consecutive numbers, the base and eight cells:

| offset | cell |
|---|---|
| `+0` | the base (what `m_IdealActivity` holds) |
| `+1` | `_ATTACKER_SHORTVICTIM_FRONT` (name verified at 3932, port table) |
| `+2` | `_ATTACKER_TALLVICTIM_FRONT` (I, by the formula) |
| `+3` | `_VICTIM_SHORTATTACKER_FRONT` (I) |
| `+4` | `_VICTIM_TALLATTACKER_FRONT` (I) |
| `+5`..`+8` | the same four, `_BACK` (`+8` = `_VICTIM_TALLATTACKER_BACK` verified at 3939) |

### The bases, and who advances them

Base names from the registration table the port carries (`ElysiumRetailActivities.cpp` :3890-4139);
initial activity per mode from `animation_and_movers.md` :1341-1351 (D).

Callers of `SetGrappleActivity` (L, the whole set): `StartGrappleAttack 0x10328df0` once, with the
mode's initial base, after both `EnterGrappleState` calls succeed; and the player's four mode leaves
below. No NPC code calls it. The leaves run from `CBasePlayer::SetAnimation 0x10164240` **only for
role 0** (role 1 skips to the tail), switch on `m_GrappleType`, and a leaf answering true makes
`SetAnimation` call `EndGrapple(this)`, `AddFlag(0x40000000)`, slot `+0x704(anim, 0)` (L). Modes 4
and 7 (and any type off the table) end on the bare `m_bSequenceFinished`.

Every leaf first clears the continuation latch `+0x14a8` (and `m_bCantBreakGrapple`,
`m_flBreakGrappleTimer`) when the partner is gone or its slot `+0x278` answers false, then does
nothing until the **attacker's** `m_bSequenceFinished`; it then switches on the attacker's
`m_IdealActivity` and commits the next base only when it differs from the current one (L).

**Modes 0 / 1 — `0x101655b0` (slot `+0x68c`)**

| current base | next |
|---|---|
| `0xf5b` `ACT_FEEDING_ENGAGE`, `0xf64` `ACT_FEEDING_IDLE` | latch set → `0xf76` `ACT_FEEDING_BITE`; else slot `+0x6a8` ? `0xf9a` `ACT_FEEDING_RELEASE_PC_FLYBACK` : `0xf91` `ACT_FEEDING_RELEASE` |
| `0xf76` `ACT_FEEDING_BITE`, `0xf7f` `ACT_FEEDING_FEED_LOOP` | latch set and `CanFeed` → `0xf7f` (and `+0x1b6c = 1`); else slot `+0x6a8` ? `0xf9a` : `0xf88` `ACT_FEEDING_FEED_RELEASE` |
| anything else (`0xf88`, `0xf91`, `0xf9a`) | answer true → `EndGrapple` |

`0xf6d` `ACT_FEEDING_ATTACK_RELEASE` is registered and has no producer in this leaf; `0xf64` is a case
label nothing here commits (L).

**Mode 2 — `0x10165ae0` (slot `+0x690`)**: `0xfa5` `ACT_SEDUCTIVE_ENGAGE` → latch ? `0xfae`
`ACT_SEDUCTIVE_LOOP` : R; `0xfae` → latch and `CanFeed` ? stay : R; else true. R = the victim's
type-0 stat list `GetValue(0xc) > 0` ? `0xfc0` `ACT_SEDUCTIVE_RELEASE_TO_MEZ` : `0xfb7`
`ACT_SEDUCTIVE_RELEASE` (L).

**Mode 6 — `0x10165330` (slot `+0x698`)**: `0x1027` `ACT_RAT_FEED_ENGAGE` → latch ? `0x1030`
`ACT_RAT_FEED_LOOP` : `0x1039` `ACT_RAT_FEED_RELEASE`; `0x1030` → latch and `CanFeed` ? stay :
`0x1039`; else true (L).

**Mode 8 — `0x10165f20` (slot `+0x69c`)**: `0xfca` `ACT_ZOMBIE_FEEDING_ENGAGE` / `0xfd3` `_IDLE` →
latch ? `0xfdc` `_BITE` : slot `+0x6ac` ? `0x1009` `_RELEASE_PC_FLYBACK` : `0x1000` `_RELEASE`;
`0xfdc` / `0xfe5` `_FEED_LOOP` → latch and `CanFeed` ? `0xfe5` : slot `+0x6ac` ? `0x1009` : `0xff7`
`_FEED_RELEASE`; else true (L). The player is the victim's partner here but the leaf still runs on
the player only when the player is role 0; for `BeFedOnByZombie` the player is role 1, so this leaf's
reach in the shipped game is unrecovered.

**Mode 3, the stealth kill — `0x10165d90` (slot `+0x694`)**: never calls `SetGrappleActivity`. The
one commit is `StartGrappleAttack`'s, with `0x1015` `ACT_SNEAKATTACK_SUCCESS` (D); it reaches the
same `0x1032a100`, so the victim gets `m_IdealActivity = 0x1015`, `m_Activity = 0x1015 + cell` after
the **attacker's weapon** translate (the `_KNIFE`, `_FISTS`, ... rows of the registration table are
that translate's range, I), and the clip as `m_nSequence`. On the attacker's `m_bSequenceFinished`
the leaf deals the kill packet, `SetBaseToStatValue(0xf, 0x11)`, victim slots 144 and 403, and
answers true (L).

**Mode 5, payphone — `0x101654e0` (slot `+0x688`)**: `0xf40` → `0xf49` on finish; `0xf49` with the
dialog partner gone → `0xf52`; `0xf52` finished → true (L).

Unrecovered: slots `+0x6a8` / `+0x6ac` (the flyback predicate); the weapon's `+0x5a4` over a feed
activity when the attacker still holds a weapon handle.

## 2. `LeaveGrappleState`, whole, and who ends a grapple

**Base `0x10329a70` (slot 380) (L).** `+0xa9c = -1.0f`; for role 0 and type 1 or 4, read the partner's
saved origin `+0x1548`/`+0x154c`; set `+0x1558`, `+0x1538`, `+0x153c`, `+0x1540`, `+0x1544` to `-1`;
when `+0x1554` (holstered on enter) and a weapon is active, the weapon's slot `+0x10c`; when `+0xa8`
is non-null (the player arm) the exit placement (hull traces, `Bip01` bone, slots `+0xf8`/`+0x104`);
last, slot `+0x178()` `== 0` → slot `+0x174()` (the move-type restore, D `stealth.md` :547).

**`CAI_BaseNPC::LeaveGrappleState 0x1026ce30` (L):** `m_OnGrappleEnd` with the partner as activator,
the base body, slot 416 `(0)`, `0x10007ea0` (`--m_iIsOblivious`, squad reconnect, D).
**Troika `0x102b5d90`:** that, then a tail jump to slot 614 (`+0x998`, the think timers := now) (L).

**None of the three bodies writes `m_Activity`, `m_IdealActivity`, `m_nSequence`, `m_flCycle`, the
NPC state or the schedule (L).** The released NPC stands on the release clip's sequence with
`m_Activity` = the release cell and `m_IdealActivity` = the release base until its next `RunAI`
changes them. For a surviving feed victim that is the schedule `FeedInterrupt 0x1033a9e0` installed
(`SCHED_TROIKA_MESMERIZED 0xfb`, D `feeding.md` :457-463): its `TASK_SET_ACTIVITY` writes the ideal
and `MaintainActivity` commits it. Nothing in the end path plays an idle.

What entry did to the kernel, so what the end finds (D, `conditions-and-states.md` :3386, :3447):
`SetEnemy(NULL)`, squad disconnect, `++m_iIsOblivious`, `m_OnGrappleBegin`; Troika adds dialogue
exit, `CancelScript` for a live `m_hCine` (+ `SetState(ideal)` when they differ) and
**`ClearSchedule`**. So during the grapple the victim has no schedule; the state word is untouched.

**Who calls it.** Slot 380 is dispatched by `EndGrapple 0x10329560` (partner first, then `this`; for
type 8 with a role also `0x10175400(player, 0)`) and by `StartGrappleAttack`'s rollback of the
attacker when the victim refuses (L). `EndGrapple`'s direct callers, the whole set (L, caller list):

| caller | when |
|---|---|
| `CBasePlayer::SetAnimation 0x10164240` | the normal end: the mode leaf answered true (release clip finished; stealth kill dealt) |
| `SetGrappleActivity 0x1032a100` ×2 | a sequence miss on either body |
| `CanStartGrappleAttack 0x103285a0` | an attacker already grappling starts another |
| `CBasePlayer::OnTakeDamage 0x10163020` | the player is damaged (the arm's conditions not read) |
| `CBasePlayer` `0x10164870` | a live `m_hPlaceholderNPC` when the normal-animation path runs |
| `CPointTeleport::InputTeleport 0x1018dc00` | teleport |
| `0x100db5c0`, `0x10170090` | not read |

No NPC death function is in the set: a victim dying does not itself end the pair (I, from the caller
list). The attacker's leaf sees the partner's slot `+0x278` false, clears the latch, and the release
family plays out to its end; the stealth kill kills the victim and ends the pair in the same leaf call.

## 3. What a grappled NPC victim's AI does

- `NPCThink` runs. `CAI_BaseNPCTroika::NPCThink 0x10292de0` step 6 calls `RunAlternateAI(!bAI)` and
  `RunAI` only when it answers false; then `PostRun`, `PerformMovement`, the think-time tail (D,
  `schedule-kernel.md` :4906-4917). The base `0x1026ca80` has the same skip inline (D, :4933).
- `RunAlternateAI 0x1028fd80` head arm: live partner and role 1 → for `m_IdealActivity` in the nine
  release bases (`0xf88 0xf91 0xf9a 0xfb7 0xfc0 0xff7 0x1000 0x1009 0x1039`) call `AutoMovement
  0x10280a50`; answer true either way (D, :4576-4581). **It reads the base word**, which is why
  `m_IdealActivity` must be the base and must change per phase.
- So: no `RunAI`, no condition gather, no `MaintainSchedule`, no schedule (cleared on entry), **no
  `MaintainActivity`**. The differing `m_Activity` / `m_IdealActivity` are never reconciled while
  grappled; nothing reads the mismatch. After the leave, the first `RunAI`'s `MaintainActivity
  0x102727d0` sees `m_Activity != m_IdealActivity`, re-resolves the ideal through `0x10272130` and
  advances (D, `shape.md` :5226) — by then the new schedule's task has usually replaced the ideal.
- `PostRun` → `RunAnimation 0x1026c540` still runs (L): slot 250 frame advance, then the idle re-pick
  gated on `m_NPCState` not 4 / 7, **`m_Activity (+0xfec) == 1`** and slot 251; it selects by
  `m_TranslatedActivity` and commits through `0x10260a50`. With `m_Activity` a grapple cell the
  re-pick is off for the whole grapple, and stays off after the leave until the kernel commits
  another activity. Slot 258 dispatches the clip's records from `PerformMovement` (D).
- The attacker role on an NPC (mode 8) does not take the head arm (role 0): its `RunAI` runs. Not
  walked further.

## 4. The loop bit

From the descriptor. `ResetSequenceInfo 0x10090950` writes `m_bSequenceLoops (+0x65d) =
GetSequenceFlags(m_nSequence) & 1`, `m_bSequenceFinished = 0`, playback rate 1.0 (L).
`SetGrappleActivity` and `0x10260a50` pass no loop argument and no phase reaches the bit (L). Which
feed clips author `STUDIO_LOOPING` is the baked flag's answer; it was not read here.

## 5. The port against this (P)

`Source/ElysiumUE/Private/Substrate/ElysiumFeed.cpp` unless said.

| # | divergence | where | fix |
|---|---|---|---|
| D1 | Both activity words are `0xf5b` on every phase. Retail: ideal = the phase's base, activity = the cell. | `ElysiumFeedGrappleCommit` :461-469, :482-483 | Pass the base in. `IdealActivityNumber = Base`; `ActivityNumber = Base + (partner male ? 2 : 1) + (role == victim ? 2 : 0) + (position == 1 ? 4 : 0)`, then the **attacker's** `Weapon_TranslateActivity`. Delete `GrappleActivityStandIn`. |
| D2 | Consequence of D1: `RunAi19GrappleVictimAutoMoves(IdealActivityNumber)` never matches, so the victim's `AutoMovement` during the release clip never runs. | `ElysiumNpcRunAi.cpp` :87 | none there; D1 feeds it `0xf88`. |
| D3 | The phase has no base. Port phases map: `Engage` `0xf5b`, `Bite` `0xf76`, `Loop` `0xf7f`, `Release` `0xf88` (its clip suffix is `feed_release`). | `PlayFeedPhaseClips` :496, `PhaseSuffix` :84-94 | a `PhaseBaseActivity(Phase)` beside `PhaseSuffix`, mode 0 only; pass it to `CommitNpcHalf`. |
| D4 | The size cell is the rendered mesh height. Retail: `IsMale(partner)`. | `FeedVictimHeightCell` :447-457, `RenderedFeedHeightCm` :206 | `Taller` when the victim's `Sheet.IsMale()` (as `ElysiumGrapple.cpp` :45-46 already does); the victim's own clip cell takes the **attacker's** sex, so `ResolveClipPair` needs both sexes, not one height. |
| D5 | The sequence is chosen by clip label. Retail: `SelectWeightedSequence(cell)`. | `CommitNpcHalf` :477 | `Npc.SequenceForActivity(cell)` (`ElysiumNpc.h` :101). If the resolver cannot answer a cell number for the shared feed banks, keep the label lookup as the named modernization and state it in the comment; the two activity words are written from numbers either way. |
| D6 | The end plays an idle on the victim's body and writes `NextThink`. Retail's leave writes no animation word; slot 614 owns the timers. | `EndFeedVictimRole` :434-443 | for `AsNpc()`: no `ResetAnimToIdle()`, no `NextThink` write (`FElysiumNpc::LeaveGrappleState`, `ElysiumNpc.cpp` :555-566, already resets the timers). The player half keeps both. |
| D7 | The player half's loop is forced by the phase (`bLoop = Phase == Loop`). Retail: the descriptor's bit. | `PlayFeedPhaseClips` :548, :568, :584 | not this lane (the player has no kernel sequence words); record it. The NPC half already takes the row's bit. |
| D8 | The phase machine has one release (`feed_release`) and always goes Engage → Bite. Retail: a cleared latch at engage's end goes to `0xf91` / `0xf9a`; a loop release picks `0xf88` or `0xf9a`. | `AdvanceFeedPhase` :1274-1304, `EnterFeedRelease` :1206 | not this lane; needs slot `+0x6a8` recovered first. |
| D9 | The stealth kill plays both clips directly (`PlayAnimSegment`) and slaves the victim's cycle to the attacker's; the NPC victim's kernel words are untouched. Retail: the same `0x1032a100` commit with base `0x1015`, each body on its own clock. | `ElysiumGrapple.cpp` :139-160, :189-196 | second commit of the same lane: the victim half through `CommitNpcHalf` (base `0x1015`, cell from `Pair.Position` and the attacker's sex, the attacker's weapon translate); drop `SyncGrappleClip` for an NPC victim. |

Matches retail already: attacker first then victim, the miss warnings and `EndGrapple` (:557-592);
`m_flCycle = 0`; the victim's `RunAI` skip (`ElysiumNpcRunAi.cpp` :83-92); the NPC leave bodies.

## Fix lane

One coder. Files: `Source/ElysiumUE/Private/Substrate/ElysiumFeed.cpp`, `ElysiumFeed.h`,
`Source/ElysiumUE/Private/Substrate/ElysiumGrapple.cpp` (D9 only),
`Source/ElysiumUE/Private/Tests/ElysiumFeedingTests.cpp`, `Arena/scenarios/combat/`,
`docs/vtmb/feeding.md` (§ "Feed modes" :171-182: replace the stand-in sentence with section 1's
tables).

1. D3: add the phase → base table (`0xf5b`, `0xf76`, `0xf7f`, `0xf88`).
2. D1: `CommitNpcHalf` takes the base, the NPC's role, the partner's sex, the position and the
   attacker; writes the two words as in section 1 steps 5-6. Remove the stand-in constant and its
   comment.
3. D4: the size cell from `IsMale`, per body.
4. D5: select through `SequenceForActivity(cell)`; label fallback only as a named modernization.
5. D6: `EndFeedVictimRole` leaves an NPC's animation and think words alone.
6. D9, separate commit: the stealth kill's NPC victim through the same commit.
7. Not in the lane: D7, D8.

Unit test (`Elysium.Arm.` feed family): after the engage commit on an NPC victim with a male
attacker, front, `IdealActivityNumber == 0xf5b` and `ActivityNumber == 0xf5f`; after release
`0xf88` / `0xf8c`; after `EndFeedGrapple` both words and `SequenceNumber` unchanged.

Arena. `verbs_feed_victim_dispatch.json` is specified in packet S9 but **was not on disk** at this
reading (only `verbs_feed_trance.json` is); create it as S9 says, plus:

- expect, after `arena_vessel output OnGrappleEnd`: `arena_vessel schedule SCHED_TROIKA_MESMERIZED
  (0xfb)` within 1.0 (the first think after the leave runs the installed schedule);
- never: `arena_vessel sequence` matching `idle|Idle` between `OnGrappleBegin` and the
  `SCHED_TROIKA_MESMERIZED` line (retail's leave commits no idle; the next sequence is the trance
  task's);
- never: `arena_vessel schedule` of any kind between `OnGrappleBegin` and `OnGrappleEnd` (the entry
  cleared it and `RunAI` is skipped).

`verbs_feed_trance.json`: add the same "no idle sequence before `entranced`" never-line; its existing
expectations stand.

**Unrecovered after this sitting:** slots `+0x6a8` / `+0x6ac`; `EndGrapple` callers `0x100db5c0`,
`0x10170090` and `OnTakeDamage`'s arm conditions; the weapon `+0x5a4` translate over feed
activities; which feed clips carry `STUDIO_LOOPING`; mode 8's leaf reach with the player as role 1;
whether the port's resolver answers the cell numbers (D5).
