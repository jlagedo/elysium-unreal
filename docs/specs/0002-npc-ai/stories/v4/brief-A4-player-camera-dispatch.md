# Brief A4 — V4a: the player's and the camera's anim-event dispatch (coder; no build)

**New after V4r (2026-10-04), by the judge's rulings J3, J4 and J8.** Read `README.md` here (§1
"The events" with its two amended paragraphs; §2 M2; §4 V4a; §7 K3; §8 Q5, Q10; § "Shared
names"), `packets-R2.md` item 1, item 6 (f) and extension 1, the rulings J3, J4, J8
(`stories/v1/triage.md` § "Judge's rulings, V4"; Grep, read only that section),
`docs/vtmb/animation_events.md` § "Who calls the dispatcher, and where against the frame advance",
`docs/vtmb/player-entity.md` § "Recovered `PostThink` body". After the seam commit. Re-locate
every site by Grep.

**How you share the dispatcher with A1.** `ElysiumAnimEvents::DispatchBase` / `DispatchLayer` over
`FElysiumSequenceWords` are declared by the seam in `ElysiumAnimEvents.h`; **A1 owns and fills
them, you only call them**, by the signature in README § "Shared names". You do not edit
`ElysiumAnimEvents.{h,cpp}`: a field or parameter you find missing goes in your report, exact.
A1 also deletes the world poll and writes the one call of `FElysiumPlayer::PostThinkAnimation()`
in the world tick; you write that function's body. A1 is deleting
`FElysiumAnimating::AdvanceAnimEvents` (`ElysiumAnimatingImpl.cpp` ~:310-396) while you work: read
it **as the seam commit has it** (`git show HEAD:<path>` if the working file has changed) — it is
your reference for which accessor gives each channel's timeline and phase today.

## Files (only these)

`Source/ElysiumUE/Private/Substrate/` unless a path says otherwise:

- `ElysiumPlayerEntity.cpp` (`FElysiumPlayer::PostThinkAnimation`'s body; `FElysiumPlayer::
  HandleAnimEvent` ~:593, its gate only; `FElysiumPlayer::Spawn` ~:120, the FOV line only)
- `Source/ElysiumUE/Public/ElysiumPlayer.h` (the `FElysiumPlayer` class: the dispatch words per
  channel, nothing else)
- `ElysiumGrapple.cpp` (`FElysiumPlayer::TickStealthKill` ~:167: its entry only, if being called
  from `PostThinkAnimation` needs a change; otherwise untouched)
- `ElysiumCameraAnimated.{h,cpp}` (`FElysiumCameraAnimated::Think` ~:244, the camera's words)
- `ElysiumFeed.cpp` (the feed timeline's hand-off to the dispatch ~:1158-1164 and the comments at
  ~:14 and there that name `AdvanceAnimEvents`; nothing else)
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelPlayerControllerTests.cpp` (its two
  `AdvanceAnimEvents()` calls ~:874, ~:881 only), new
  `Source/ElysiumUE/Private/Tests/ElysiumPlayerPostThinkTests.cpp`, new
  `Source/ElysiumUE/Private/Tests/ElysiumCameraAnimatedThinkTests.cpp`
- `docs/vtmb/player-entity.md` (§ "Recovered `PostThink` body" only)

## The job

1. **Fix the doc first** (J8): `docs/vtmb/player-entity.md` § "Recovered `PostThink` body" says
   `-> Weapon_FrameUpdate` (~:450). The call is **slot 312 `UpdateCharacter`**;
   `Weapon_FrameUpdate 0x1032aa40` has one caller, `PostRun`, and the player never calls it.
   Correct the line and add retail's order as R2 read it — `CBasePlayer::PostThink 0x1016be10`:
   slot 250 `(0)` (the advance) → `0x101600a0` → slot 258 `(interval, this)` → slot 312 — with
   the date and "packet R2, V4r". Change nothing else in that document on one read.
2. **The player's dispatch site and order: `FElysiumPlayer::PostThinkAnimation()`.** It is called
   once per world tick at the point that stands for `PostThink` — after `SyncFromBody` /
   `TickStepClock`, before `RunThinks` (A1 writes the call) — and runs, in retail's order
   `0x1016be10` (**advance → slot 258 → slot 312**):
   - **the advance: a named seam.** The player has no kernel sequence words (no `m_nSequence`,
     `m_flCycle`, `StudioFrameAdvance`): its clock is the pose layer's phase. So the words' `Cycle`
     for each channel is **read from the pose layer's phase** (the accessor the poll used,
     `GetBodyClipPhase` on the embodiment), and the line carries the comment: "stands for
     `m_flCycle` until the player's `StudioFrameAdvance` is ported — the player's kernel sequence
     clock is filed to 0015, layer 0 (J3)". This is the existing K2-shaped residue, named at its
     line; write no sequence clock for the player here (`SetAnimation 0x10164240` and the action
     classifier are the player story's).
   - **slot 258 — the overlay body, so base then layers.** Keep one `FElysiumSequenceWords` per
     channel on the player — the channels the poll walked, Base and UpperBody
     (`GPolledEventChannels`) — holding its `LastEventCheck` between ticks; fill `Sequence`,
     `Cycle` (the phase), `CycleRate` (the clip's cycle rate × its play rate), `AnimTime`,
     `bLoops`, `bDescriptorLoops`, `bHasDescriptor` from the clip the channel plays, and the
     events from the same timeline the poll read (including the feed timeline's hand-off,
     `ElysiumFeed.cpp`). Base: `DispatchBase`; the UpperBody layer: `DispatchLayer` (retail's
     `0x10098cd0`: the firearm's 3031 is authored on the `*_attack_layer` clip in an overlay
     layer). A channel whose clip changed since the last tick starts with `LastEventCheck = 0`
     (retail: `ResetSequenceInfo` zeroes it). `Source` and `Handler` are both the player. This
     gives the player retail's window and its 0.1 s look-ahead (`+0x658`).
   - **the handler gate** of `CBasePlayer::HandleAnimEvent 0x10178a10`: `!IsObserver && source ==
     this`; 4050, 4051 and 2060 handled itself; **2050..2053 swallowed** (the footstep sound stays
     the step clock's); the rest to `CBaseCombatCharacter::HandleAnimEvent 0x1032e330` (the weapon
     band, 4006/4007, 4020, 4100..4102). Check `FElysiumPlayer::HandleAnimEvent` against these
     arms and fix a difference at its line; the port has no observer and the source is the player
     by construction — say so in a comment rather than inventing a parameter.
   - **slot 312's stand-in**, last: `TickStealthKill()`. The player's melee sweep is the other
     half of slot 312; it is called today from the map actor's tick
     (`EntityWorld->AdvanceMeleeSwings`, `Private/Map/ElysiumMapActor.cpp` ~:2525), a file that is
     not yours — leave it, name it in a comment in `PostThinkAnimation` as "slot 312's sweep,
     still at the map actor's tick", and report the line.
3. **What moves, and is not loosened** (README §8 Q10): with the look-ahead every player shot's
   commit arrives up to 0.1 s of clip time earlier. `CommitArrivesFromAnimEvent`
   (`ElysiumWeaponClasses.cpp` ~:1259-1282) and the feed timeline (`ElysiumFeed.cpp`
   ~:1158-1164) depend on the poll's cursor: read both; fix the feed's hand-off in your file; for
   the weapon file (not yours) report the exact line and what it must read instead. The player's
   `ContactEventCycle` estimate is **not** removed here (J6: it goes with the player's weapon
   story).
4. **The player's FOV** (R2 item 6 (f); yours because you own this file in the wave): in
   `FElysiumPlayer::Spawn`, `FieldOfView = 0.5f;` with the comment "`CBasePlayer::Spawn
   0x1016d260` writes `m_flFieldOfView +0x1574 = 0.5`". Lane A3 ports the cone that reads it.
5. **The camera's dispatch** in `FElysiumCameraAnimated::Think` (J4), retail `0x10071840`, 10 Hz:
   **advance → dispatch → the finish test on `m_bSequenceFinished`**. Give the camera its words
   (`FElysiumSequenceWords` on the class; the cycle from the sequence's start time and length);
   call `DispatchBase` only — **no layers** — with the camera as source and handler; the handler
   is `CBaseAnimating::HandleAnimEvent 0x10091da0` (2070, 2071, 4005, anything else a
   `DevWarning`): if the port's camera handler has other arms, report them, do not delete on a
   guess. **The finish comes from the dispatcher's flag, replacing today's time compare**
   (~:256) — so the camera ends on the look-ahead finish, 0.1 s of clip time earlier than today,
   and `OnCameraComplete` moves with it on every cutscene: that is retail's. Remove the comment
   that says the advance and dispatch "are the world's own animation pass" (~:251-253).
6. **Tests**, each assertion naming its address:
   - `Elysium.Arm.Player.PostThinkOrder` (`0x1016be10`): the dispatch runs before
     `TickStealthKill`; a base-channel 2050 reaches the handler and is swallowed; a layer's 3031
     reaches the weapon band through `0x1032e330`; a clip change restarts the window at 0.
   - `Elysium.Arm.CameraAnimated.ThinkOrder` (`0x10071840`): advance, then dispatch, then the
     finish from the flag; a non-looping sequence finishes one look-ahead before its end; 2070
     reaches the handler.
   - `ElysiumNpcKernelPlayerControllerTests.cpp`: its two `F.Player->AdvanceAnimEvents()` calls
     become `F.Player->PostThinkAnimation()`; an assertion that only pinned the poll's cursor is
     deleted and listed.

## The records that prove it (the seam wrote them; you edit none)

`anim_player_footsteps` (2050 then 2051 `who: player` walking, none standing);
`anim_player_weapon_event` firearm (3031 before its `damage`) and melee (`never animevent` in
3000..0xfa2, the `damage` still landing). The camera has no step-2 arena record; the A integrator
names one if it exists.

## Not yours

The dispatcher bodies and the poll's deletion (A1); the row data and the clock (A2); the cone
(A3); props (nothing replaces their poll: retail never dispatches for a prop); the player's kernel
sequence clock (0015, layer 0); the player's `ContactEventCycle` estimate; the move-and-shoot
overlay.

## Rules

README § "Rules for every agent of V4": no build, no editor, no suite; only your files; a line
another file needs goes in your report, exact, with its place. Follow the listing and cite the
address at every line you port; a retail input with no source yet is a seam answering "nothing",
named for the retail field; a new divergence is recorded in your report, not adopted. No new test
of a port mechanism. The query budget (10 s warns, 60 s stops; never a file over ~200 KB whole).
Text through Grep / Read / Glob. Do not commit. Report ≤300 words: what you ported (addresses),
the seam's exact line, the tests added, changed and deleted, the cross-lane lines (the weapon
file, the map actor's sweep, any dispatcher field you lacked), open questions.
