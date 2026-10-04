# Packets S9 — the four problems V4a's integrator left, and the camera's slot 259 (2026-10-04)

Read-only on source and `Arena/`. HEAD `182c7864`; three lanes (B1, B2, V5a-3) had uncommitted
edits in the tree while this was read, and one of them changes item 4 (said there). Every claim
carries its address. **(L)** = read from the listing or decompile this session; **(P)** = read in
the port this session; **(T)** = read in an arena trace under
`$ELYSIUM_WORK_ROOT/reports/arena/`; **(doc)** = already in `docs/vtmb/` or an earlier packet;
**(I)** = inferred, with the reason. `vampire.dll` throughout. No query passed 10 s.

Traces used: `20261004T164900.842062Z` (before V4a's dispatcher) and `20261004T173659.780098Z`
(after), `arena/sense_cone_enter`, `script_walk_to_mark`, `verbs_feed_trance`,
`anim_player_footsteps`.

## 1. The player's footstep burst — the port matches `0x10091880`; the burst is retail's and is swallowed

### The wrap arm, instruction by instruction (L)

| address | what it does |
|---|---|
| `0x100918f5` | `cVar2 = m_bSequenceFinished (+0x65c)`, kept at `[ESP+0x13]` |
| `0x1009190c..0x1009194f` | `rate = GetSequenceCycleRate(m_nSequence) × m_flPlaybackRate (+0x6f4)`; `flStart = +0x658`; `+0x65c = 0`; `flEnd = rate × 0.1 (0x104491b4) + m_flCycle (+0x6f8)` |
| `0x1009195a` | `m_bSequenceLoops (+0x65d)` selects the arm |
| `0x10091968..0x100919b1` | looping: `flEnd >= 1.0` or `flEnd < 0.0` → `+0x65c = 1`; else past-half. **`flEnd` is not clamped and not reduced** |
| `0x100919b8..0x100919f4` | looping only: `flStart >= 1.0` → `flStart −= 1.0`; `flStart < 0.0` → `flStart += 1.0` (one step each) |
| `0x10091a66..0x10091a6c` | **`+0x658 = flEnd`, the raw end** (e.g. 1.05). There is no "end minus 1" store; the reduction is the *next* call's start wrap |
| `0x10091a96` | `event >= 5000` → skip |
| `0x10091aa3..0x10091abd` | fire when `cycle >= flStart` and `cycle < flEnd` |
| `0x10091abf..0x10091af3` | else, with `seqdesc.flags & 1` and `flEnd >= 1.0`: fire when `cycle < flEnd − 1.0` (strict) |
| `0x10091bac` | `RET 8`: the interval argument (`[ESP+0x80]`) is never read; only the handler (`[ESP+0x84]`) is |

`ElysiumAnimEvents::DispatchBase` / `FireWindow` (`ElysiumAnimEvents.cpp` :93-149, :38-39) (P) are
the same term for term: the raw end stored (:135), the start wrap (:126-133), `>=` / `<` on the
first clause, `End >= 1` and strict `< End − 1` on the second. **None of the four candidate terms is
wrong in the port.** The look-ahead is applied once.

### Why a caller faster than the look-ahead bursts (L, arithmetic on the rows above)

Take a looping clip and a caller dispatching every frame. While `m_flCycle` is in the last
`0.1 × rate` of the lap, every dispatch has `flEnd >= 1.0`:

- first such frame: `flStart` = the previous end (< 1). Fires `[flStart, 1.0]` and, by the wrap
  clause, `[0, flEnd − 1)`. Stores e.g. `1.02`.
- every following frame until the cycle itself wraps: `flStart = 1.02 − 1 = 0.02`, `flEnd` is again
  `>= 1.0`, so **the first clause is `0.02 <= cycle < 1.0x`: the whole table from `0.02` up**, plus
  the wrap clause below `flEnd − 1`.
- first frame after the cycle wraps: `flStart = prevEnd − 1`, `flEnd < 1`: a normal window.

So the table re-fires once per dispatch for 0.1 s of real time per lap (about 5 extra passes at
60 fps). A caller at exactly 0.1 s (an NPC's think) has `flStart == m_flCycle` each think and sees
exactly one `flEnd >= 1` dispatch per lap: no burst. The burst is a property of the listing for any
per-frame caller.

The trace agrees (T): `sense_cone_enter` after V4a has a ~0.6 s lap; both 2052 and 2053 fire on six
consecutive frames (2.550..2.633 = 0.1 s), then one normal pair, each lap. Before V4a: one each per
lap. The pattern is the arithmetic above, not a re-arm: a channel reset each lap would open at the
anchor and fire nothing extra. `PlayId` does not change per lap (`RefreshLocomotionArm` keeps it for
the same clip, `ElysiumBipedAnimInstance.cpp` :2030-2050) (P).

### Why retail does not sound a burst (L)

`CBasePlayer::HandleAnimEvent 0x10178a10`: ids `0x802..0x805` (2050..2053) fall out of the switch
with no call at all (`< 0x802` → `0x1032e330`; `> 0x805` → the 2060 arm or `0x1032e330`). The
player's footstep sounds come from the step clock, not from these records (doc). The port does the
same: `ElysiumFootsteps::PlayerSwallows` (`ElysiumPlayerEntity.cpp` :628) (P). **The count 5 → 23 is
the `animevent` debug tap, which sits before the handler exactly where retail's `DisplayAnimEvent`
does (`0x10091b34..0x10091b4d`). No state changes.**

(I) That retail's player dispatches every frame rests on `PostThink 0x1016be10` running per user
command; the tick rate was not measured. If it were coarser than 0.1 s there would be no burst in
retail and the port's per-frame call would be the divergence; nothing read here suggests that.

Caution for later lanes: any id below 5000 that is *not* swallowed and sits on a looping player base
clip fires up to `0.1 s × fps` times per lap, in retail too.

### The anchor — the exact rule

Retail (S8 item 6, L): every sequence set runs `ResetSequenceInfo 0x10090950`, which zeroes `+0x658`
and never touches `m_flCycle`. The player's commit `0x101644f0` zeroes the cycle except for the five
locomotion activities, where it is carried; the first window is then `[0, cycle + 0.1 × rate)`.
Retail has **no resume**: when something displaces a clip, coming back is a new commit (cycle 0, or
carried for locomotion).

The port at HEAD opens every re-armed channel at `Phase.AnchorCycle` (`ElysiumPlayerEntity.cpp`
:1205-1212) (P). That is 0 for a clip a play seam started (right), the found fraction for a
locomotion clip (`ElysiumBipedAnimInstance.cpp` :2044; not retail: retail's window is 0 with the
cycle carried), and the frozen cursor for a displaced clip that returns (a case retail does not have).

Rule for the seam:

1. **A play the channel has not dispatched before** (a new `PlayId`, or another clip) stands for
   `0x101644f0` + `ResetSequenceInfo`: `LastEventCheck = 0`, `bSequenceFinished = false`, `Cycle` =
   the phase. For a locomotion clip found running, the catch-up `[0, phase + 0.1 × rate)` is
   retail's carried-cycle window.
2. **A play that was displaced and comes back** (same `PlayId` and identity as the play this channel
   was dispatching before the current one) has no retail counterpart: the pose layer resumed it
   mid-clip where retail would have restarted it at 0. It keeps the cursor it had when displaced.
   Opening it at 0 would fire everything behind the playhead in one dispatch, which retail never does.
3. `Phase.AnchorCycle` is not read by the player.

Under rule 2 a locomotion clip that returns after an overlay keeps its old cursor, where retail
opens at 0. The only records affected on the player are 2050..2053 (swallowed), so it is a debug
count only. Making it exact needs `FElysiumClipPhase` to say the arm is the locomotion stack (a
Visual file); not required.

**Fix** (`ElysiumPlayerEntity.cpp` `PostThinkAnimation` :1197-1213; `Public/ElysiumPlayer.h`
`FPostThinkChannel` ~:2141): add to the channel the displaced play (`PlayId`, owner stem, owner
root, label, `LastEventCheck`, `bSequenceFinished`). In the `!bSameSequence` block: save the current
play as the displaced one; if the phase names the previously displaced play, restore its two words;
otherwise `LastEventCheck = 0` (`0x10090a3d`). Replace the comment at :1205-1210 with rules 1-2 and
the addresses `0x10090950`, `0x101644f0`. `DispatchBase` is not touched. `sense_cone_enter` pins no
count and stays green.

## 2. NPC footsteps thin out on slow-thinking NPCs — the port matches; the loss is retail's

- `CAI_BaseNPC::PostRun 0x1026c7c0` (L): `dt = RunAnimation 0x1026c540()`; slot 258 `(dt, this)`;
  `Weapon_FrameUpdate(dt)`. `RunAnimation` returns slot 250 `StudioFrameAdvance(0)`'s answer.
- `0x10091880` never reads the interval (item 1, `RET 8` with `[ESP+0x80]` unread). The window is
  `[+0x658, m_flCycle + 0.1 × rate)`: it sweeps from the stored cursor, so **without a lap wrap
  nothing is lost at any think interval**.
- **At a lap wrap it loses events when the think is slower than the look-ahead.** The wrap clause
  needs `flEnd >= 1.0`, i.e. the dispatch must land while the cycle is within `0.1 × rate` of the
  lap end. A think that jumps from cycle 0.7 to 0.3 has `flStart = 0.8`, `flEnd = 0.4`: the first
  clause is empty and the wrap clause is off, so the records in `[0.8, 1.0)` and `[0, 0.4)` never
  fire. A think slower than a whole lap loses most of the table.
- Port (P): `FElysiumNpcBase::PostRun` (`ElysiumNpcBaseMotor.cpp` :587-599), `DispatchAnimEvents`
  (`ElysiumNpcBaseAnimEvents.cpp` :36-105, interval ignored), `StudioFrameAdvance`
  (`ElysiumNpcBaseAnim.cpp` :331-390) are the same shape. **No fix.**
- Not re-read: whether the port's slow think intervals are retail's (the efficiency ladder). If a
  far NPC thinks slower in the port than in retail, that is the ladder's defect, not the
  dispatcher's.

## 3. The feed victim's 4007 — retail plays the victim's clip through `m_nSequence`; the port does not

- **Retail (L).** `CBaseCombatCharacter::SetGrappleActivity 0x1032a100`, for the attacker and the
  victim alike: `TranslateBaseGrappleActivity`, slot 381 (`+0x5f4`) translate,
  `SelectWeightedSequence`; then `m_IdealActivity = the base activity`, `m_Activity = the
  translated one`, `0x10260a50(seq)` (`m_nSequence = seq`, `ResetSequenceInfo`), `m_flCycle = 0`. A
  miss warns and ends the grapple. So the victim's paired clip **is** its kernel sequence, and its
  own slot 258 dispatches its records.
- The victim's 4007 / 4006 reach `0x1032e330` and are **refused**: the guard needs the paired role
  `+0x153c == 0`, and the victim is role 1 (doc, `feeding.md` :177-182; the port's guard at
  `ElysiumCombatCharacter.cpp` :2138-2159). The attacker's 4007 opens the transaction. So the lost
  line changes no feed state; what the port loses is every *other* record on the victim's paired
  clips, and the kernel's activity words.
- **Port (P).** `PlayFeedPhaseClips` (`ElysiumFeed.cpp` :507-508) calls `PlayAnimClip` on both
  bodies (`ElysiumAnimatingImpl.cpp` :121 → `PlayNpcClip`). An NPC's `SequenceNumber`,
  `ActivityNumber`, `SequenceCycle` and `LastEventCheck` are untouched. Two consequences:
  1. slot 258 walks the idle row, so nothing on the feed clip fires (T: `arena_vessel animevent
     4007` at 2.550 before V4a, absent after);
  2. `m_Activity` is still `ACT_IDLE`, so `RunAnimation`'s idle re-pick can commit an idle row over
     the feed clip. The pre-V4a trace shows it doing so every 0.1 s for the whole grapple (T).
     Retail's `m_Activity` is the grapple activity, so the re-pick is off.
- **Fix** (`ElysiumFeed.cpp` `PlayFeedPhaseClips` :504-516): for each half that is an NPC
  (`AsNpc()`; the victim in modes 0 / 2, the attacker in mode 8), replace the direct `PlayAnimClip`
  with the kernel commit of `0x1032a100`: `Seq = LookupSequenceByName(label)`
  (`ElysiumNpcBaseAnim.cpp` :39); a miss is retail's warning + `EndFeedGrapple`;
  `IdealActivityNumber = the phase's base grapple activity`; `ActivityNumber = its translation`;
  `CommitForcedSequence(Seq)` (:153, = `0x10260a50`); `SequenceCycle = 0`. The clip's length comes
  from the row (`SequenceDurationSeconds`). The player half keeps `PlayAnimClip`.
  Unrecovered for the port: the feed resolves labels, not activity numbers
  (`ElysiumFeed::ResolveClipPair`), so the two activity words need the phase → activity table
  (`feeding.md` § "Feed modes": `0xf5b`, `0xfa5`, `0x1027`, `0xfca` and their continuations). Until
  it exists, write a named non-idle value so the re-pick stays off, with a comment naming
  `0x1032a100`. The same direct-clip pattern in `ElysiumGrapple.cpp` / the stealth kill was not read.
- **Arena.** `verbs_feed_trance` is the only feed record; it expects nothing of the victim's events.
  New record `Arena/scenarios/combat/verbs_feed_victim_dispatch.json`: staging copied from
  `verbs_feed_trance` (same cast, same four console lines, duration 8). Expect, in order:
  `arena_vessel output OnGrappleBegin` by 2.5; `arena_vessel sequence` not matching `idle|Idle`
  within 0.2 (the engage row committed through the kernel); `arena_vessel animevent ^4007( |$)`
  within 1.0; `arena_vessel output OnFedUponBegin` within 0.5. Never: `arena_vessel sequence`
  matching `idle|Idle` between `OnGrappleBegin` and `OnGrappleEnd`; `arena_vessel death`.

## 4. The scripted walk holds the idle row — `MoveNormal` commits through slot 310, not the ideal

- **Retail (L).** `TASK_WALK_TO_TARGET`'s start arm puts `ACT_WALK` in the goal's word [5];
  `SetGoal 0x102ecd20` → `SetMovementActivity 0x102ee250` (`path+0x2c`) (doc). Each move step,
  `CAI_Navigator::Move 0x102eff40` dispatches navigator slot 12, `MoveNormal 0x102efaa0` (the
  humanoid navigator's `0x10264470` is the same shape): after the gate `0x102efd50` and slot 16 it
  saves owner slot 248, `m_Activity (+0xfec)`, `m_nSequence (+0x6f0)` and the origin, then calls
  **owner slot 310 `SetActivity(GetMovementActivity 0x102ee3f0)`** (`0x10264450` is a tail jump to
  the same slot). Slot 310 → `0x10295750` → `0x102725d0` → `0x10272130` + `SetActivityAndSequence
  0x10272490` → `0x10260a50` → `ResetSequenceInfo`. The walk row is the kernel's sequence from the
  first move step. If the step did not move, `m_nSequence` and the saved activity are written back.
- **Why the ideal route cannot work in a scene (L).** `MaintainActivity 0x102727d0` is gated by
  slot 466; `0x102bf510` → `0x10272790` answers false in `NPC_STATE_SCRIPT` (4) unless
  `m_Activity == 2`. An ideal activity set during a scripted walk is never committed.
- **Port at HEAD (P, `git show HEAD`).** `NavMoveNormalPass` wrote
  `SetIdealActivity(ResolveLinkActivity())` (`ElysiumNpcBaseMotor.cpp` ~:1559). In the Script state
  `ShouldMaintainActivity` (`ElysiumNpcAnim.cpp` :613-629) refuses, so `m_nSequence` stays the idle
  row for the whole walk (T: no `sequence` line between 2.033 and 7.267, no `animevent`).
- **The fix is already in B2's working tree (P, uncommitted):** `ElysiumNpcBaseMotor.cpp`
  :1555-1572 now reads `SetActivity(Navigator.GetMovementActivity())` with the
  `GetIdealSpeed() <= 0 && m_Activity == 2` early return (`0x102efb80`, `0x102efb88`,
  `0x102efb93..0x102efbb0`), and names the restore arm (`0x102efc11`) as not ported. That is the
  right line; the fix lane does not touch it. After arrival the walk row may stay committed until
  the scene's `StartSequence`, because `TaskMovementComplete`'s `SetIdealActivity` is also refused
  in state 4: retail's, not to be "fixed".
- **Record.** Add to `script_walk_to_mark` after `walks`: `AsianVamp sequence` matching the walk row
  within 0.3, and `AsianVamp animevent ^205[0-3]( |$)` within 1.5.
- **The two timing changes — not recovered.** Arrival (7.167 → 6.733, 6.700 in the latest run) and
  `task_face_script` (0.53 → 0.33) are both the body's: the crowd follower's travel and arrival
  creep (doc, `packets-R1.md`), and the turn at `motor+0x38`, which `SetActivityAndSequence` stores
  from `MaxYawSpeed 0x10297ce0`'s ladder on `m_Activity` (20 idle, 25 walk outside combat) (P). (I)
  V4a changed the row the kernel holds going into the scene (`katana_idle` re-picked at 1.983
  before, `Stance_Neutral_Idle_3` at 1.950 after) and began writing `+0x560` / `+0x654` from that
  row; which of those moved each number was not isolated. Both will move again when B1 (speed from
  `+0x654`) and B2 (the walk commit, so `motor+0x38` is the walk's) land. Re-measure then; do not
  pin either value.

## 5. The animated camera's slot 259 — `0x10071900` (L)

```
10071907  CMP [event],0x3eb          ; 1003
1007190d  JZ  ...                    ; else: CALL 0x10003f99 (-> 0x10091da0), return
1007191f  CALL atoi(options)
10071927  DEC EAX ; JS out           ; n - 1 < 0  -> nothing
1007192a  CMP EAX,8 ; JGE out        ; n - 1 >= 8 -> nothing
10071936  LEA ECX,[ESI + (n-1)*0x18 + 0x730]
10071932  PUSH 0 ; PUSH ESI ; PUSH ESI   ; delay 0, caller this, activator this
1007193d  CALL 0x10010794            ; FireOutput
```

Outputs: `OnScriptEvent01` .. `OnScriptEvent08` (`+0x730`, stride `0x18`; the same eight names
`ElysiumScriptedSequence.cpp` :946-947 uses).

**Add to `ElysiumCameraAnimated.h`** beside `Think()` (:88):
`virtual bool HandleAnimEvent(const FElysiumAnimEvent& Event) override;`

**Add to `ElysiumCameraAnimated.cpp`:**

```cpp
bool FElysiumCameraAnimated::HandleAnimEvent(const FElysiumAnimEvent& Event)
{
	// `CCameraAnimated::HandleAnimEvent` `0x10071900`, slot 259.
	if (Event.Event != 1003)                                    // 0x10071907 CMP [event],0x3eb
	{
		return FElysiumAnimating::HandleAnimEvent(Event);       // 0x10071912 -> 0x10091da0
	}
	const int32 Index = FCString::Atoi(*Event.Options) - 1;     // 0x1007191f atoi / 0x10071927 DEC
	if (Index >= 0 && Index < 8)                                // 0x10071928 JS / 0x1007192d JGE
	{
		// `FireOutput(this + 0x730 + Index * 0x18, activator this, caller this, delay 0)` 0x1007193d
		FireOutput(FName(*FString::Printf(TEXT("OnScriptEvent%02d"), Index + 1)), Handle);
	}
	return true;   // claimed: an out-of-range 1003 fires nothing and does not reach the base
}
```

The activator is the camera's own handle (retail pushes `this` twice), unlike `OnCameraBegin`'s
invalid one. Correct the comment at `ElysiumCameraAnimated.cpp` :315-316: slot 259 is `0x10071900`,
which falls to `0x10091da0` for every id but 1003. Update `docs/vtmb/entity_io.md` :2457
("`OnScriptEvent01..08` do not fire") for the camera. Test in
`Tests/ElysiumCameraAnimatedThinkTests.cpp`: options `"3"` fires `OnScriptEvent03` once with the
camera as activator; `"0"`, `"9"`, `""` fire nothing; id 1004 reaches the base.

## Fix lane

One coder, items 1, 3, 5 (item 2 has no fix; item 4's code is B2's and already written).

| file | item | collides with |
|---|---|---|
| `Source/ElysiumUE/Private/Substrate/ElysiumPlayerEntity.cpp` (`PostThinkAnimation` :1197-1213) | 1 | **V5a-3** (same file, another function: the slot-197 site) |
| `Source/ElysiumUE/Public/ElysiumPlayer.h` (`FPostThinkChannel` ~:2141) | 1 | **V5a-3** (same file, another member) |
| `Source/ElysiumUE/Private/Tests/ElysiumPlayerPostThinkTests.cpp` | 1 | none |
| `Source/ElysiumUE/Private/Substrate/ElysiumFeed.cpp` (`PlayFeedPhaseClips` :504-516) | 3 | none |
| `Arena/scenarios/combat/verbs_feed_victim_dispatch.json` (new) | 3 | none |
| `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseMotor.cpp` (:1555-1572) | 4 | **B2 owns it; already fixed in B2's tree. Do not edit** |
| `Arena/scenarios/world/script_walk_to_mark.json` (two expectations) | 4 | none; land after B2 |
| `Source/ElysiumUE/Private/Substrate/ElysiumCameraAnimated.{h,cpp}` | 5 | none |
| `Source/ElysiumUE/Private/Tests/ElysiumCameraAnimatedThinkTests.cpp` | 5 | none |
| `docs/vtmb/animation_events.md`, `feeding.md`, `entity_io.md` :2457, `player-entity.md` | 1, 3, 5 | none |

No file of B1's (`Visual/ElysiumNpcBody.cpp`, `Visual/ElysiumAnimationDriver.cpp`,
`Visual/ElysiumLocomotionSample.cpp`, `Public/ElysiumWorldServices.h`) is touched.
`Visual/ElysiumBipedAnimInstance.cpp` is not edited: its `AnchorCycle` simply stops being read by
the player.

**Unrecovered after this sitting:** retail's player tick rate (item 1's (I)); the port's slow think
intervals against retail's (item 2); the feed phase → activity table and the grapple / stealth-kill
clip sites (item 3); `MoveNormal`'s restore arm conditions beyond B2's read, and what moved the two
`script_walk_to_mark` timings (item 4).
