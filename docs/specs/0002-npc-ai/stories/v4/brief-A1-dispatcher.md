# Brief A1 — V4a: the dispatcher, slot 258 in `PostRun`, the world poll deleted (coder; no build)

**Final (amended after V4r, 2026-10-04 — J3, J4, J5).** Read `README.md` here (§1 "The frame",
"The events" with its two amended paragraphs — they are the body you port; §2 M1–M2, M15; §4 V4a;
§7 K3; § "Shared names"), `packets-R2.md` items 1 and 4 and extension 1,
`docs/vtmb/animation_events.md` § "Who calls the dispatcher, and where against the frame advance"
and § "Overlay layers…". After the seam commit. Re-locate every site by Grep.

**You own the dispatcher function; lane A4 only calls it** (for the player and the camera). Its
signature is fixed in README § "Shared names" and declared by the seam: do not rename a field or a
parameter. If you must add a field, add it last with a default and say so in your report.

## Files (only these)

`Source/ElysiumUE/Private/Substrate/` unless a path says otherwise:

- `ElysiumAnimEvents.{h,cpp}` (the dispatcher bodies; the poll's rule `Advance` deleted)
- `ElysiumNpcBaseAnimEvents.cpp` (the seam's override: the NPC's slot 258)
- `ElysiumAnimatingImpl.cpp` (`FElysiumAnimating::AdvanceAnimEvents` ~:310-396, deleted whole with
  what only it used — `GPolledEventChannels` ~:41-42, the tap ~:373-377)
- `ElysiumEntityWorld.cpp` (`FElysiumEntityWorld::AdvanceAnimEvents` ~:2077-2100 deleted; its call
  site ~:1938 and the `TickStealthKill()` call after it ~:1940 — nothing else in the tick)
- the three declarations: `Source/ElysiumUE/Public/ElysiumAnimating.h` (~:105),
  `Source/ElysiumUE/Public/ElysiumEntity.h` (`AdvanceAnimEvents` ~:412 only — `HandleAnimEvent`
  ~:422 stays), `Source/ElysiumUE/Public/ElysiumEntityWorld.h` (~:995)
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelAnimEventsTests.cpp` (new), and any existing
  test whose only subject is `ElysiumAnimEvents::Advance` (Grep `Tests/`)

## The job

1. **`ElysiumAnimEvents::DispatchBase` = `0x10091880`**, over `FElysiumSequenceWords`, exactly as
   README §1's amended paragraph (R2 item 1): finished cleared at entry; `flEnd = Cycle + 0.1
   (0x104491b4) × CycleRate`; non-looping with a descriptor: `flEnd >= 1 || flEnd < 0` → finished
   = 1 and `flEnd = 1.0`, else past-half = `flEnd > 0.5`; looping: the same without the clamp,
   plus the start wrapped into `[0,1)`; `LastEventCheck = flEnd`; events with id `< 5000` in
   `[start, flEnd)` or, with `bDescriptorLoops` and `flEnd >= 1`, `< flEnd − 1`; no descriptor on
   a non-looping sequence: neither the finish nor past-half written. Each fired event goes to
   `Handler.HandleAnimEvent` (slot 259) in table order, with `eventtime = (cycle − Cycle) /
   CycleRate + AnimTime` computed even though no handler reads it. The interval is not an input
   (retail's argument is unused). Return true on the finish flag's rising edge — the caller makes
   its own `OnSequenceFinished` call (`0x10091b9a → 0x10091c80` is a direct call, not a slot).
2. **`ElysiumAnimEvents::DispatchLayer` = `0x10098cd0`**, one overlay layer: the finish word
   zeroed and never set; no past-half; `LastEventCheck` = the layer's look-ahead end; no clamp; no
   "in use" or weight test; `eventtime` from the owner's `AnimTime` (the caller fills it).
3. **The `animevent` tap** (`<id> <options>`, `stories/wave2/seam.md` :46) and the census of
   unclaimed ids live inside the two bodies, emitted for `Source` — so the NPC, the player and the
   camera are tapped at one place. If the census cannot move cheaply, report it.
4. **The NPC's slot 258 = `0x10098c80`** in `ElysiumNpcBaseAnimEvents.cpp`: build the words from
   the kernel (`m_nSequence`, `m_flCycle`, the sequence's cycle rate × `m_flPlaybackRate`,
   `m_flAnimTime`, `SequenceLoops(m_nSequence)`, `LastEventCheck`), events from
   `SequenceEvents(m_nSequence)`; call `DispatchBase`; copy back `LastEventCheck`,
   `m_bSequenceFinished`, `SequencePastHalf`; on a true return call the port's
   `OnSequenceFinished` (if the port has no body for `0x10091c80`, call the seam it has or declare
   none and say so). Then the four layers: **a seam answering "no layer"**, named in its comment
   for `CAnimationLayer` (`+0x734`, stride `0x30`), for `0x10098cd0`, and for **`0x102e8560` (the
   move-and-shoot overlay) as its only step-2 pusher** (R2 item 4; J5: it stays the counter in
   `ElysiumNpcBaseMaintain.cpp`, the stack is 0015's, the wire 0002 R3's; the red record is
   `cover_move_shoot`). The seam is correct only while that overlay stays a counter — say so at
   the line.
5. **The world poll deleted whole** (J3, J4): `FElysiumEntityWorld::AdvanceAnimEvents`,
   `FElysiumAnimating::AdvanceAnimEvents`, the virtual on `FElysiumEntity`, and
   `ElysiumAnimEvents::Advance`. **Props get nothing in its place and no seam**: retail's
   `CDynamicProp` think `0x10190850` advances and never dispatches, and slot 258 has four call
   sites in the image, none a prop — one comment at the deleted call says so. At the same place
   in the tick leave **one call**: `if (FElysiumPlayer* PlayerEnt = FindPlayer())
   PlayerEnt->PostThinkAnimation();` replacing both the `AdvanceAnimEvents()` call and the
   `TickStealthKill()` call after it (A4's body runs the dispatch, then `TickStealthKill()`:
   retail's `PostThink` order, `0x1016be10`). You write the call site only; the body is A4's.
   The camera's dispatch is inside its own think (A4).
   What the deletion strands outside your files — R2 lists the dependents
   (`ElysiumCameraAnimated.cpp:251-253`, `ElysiumFeed.cpp:14, 1158-1164`,
   `ElysiumWeaponClasses.cpp:1259-1282`, `Visual/ElysiumEntityBodies.cpp:141`,
   `Tests/ElysiumNpcKernelPlayerControllerTests.cpp:874, 881`): the camera, the feed and the
   player test are A4's; for the other two, report the exact stale line.
6. **Tests** (`ElysiumNpcKernelAnimEventsTests.cpp`): `Elysium.Arm.NpcKernelAnimEvents.Window`
   (`0x10091880`: closed-bottom open-top, look-ahead 0.1 × rate, the wrap once with the descriptor
   flag and lost without it, ≥ 5000 skipped, 1.0 never fires on a non-looping clip, a zeroed
   `LastEventCheck` restarts at 0, finish and past-half written, no descriptor → neither, the
   rising-edge return once), `.Layer` (`0x10098cd0`: no clamp, finish zeroed, no past-half),
   `.PostRunOrder` (`0x1026c7c0`: `RunAnimation`, slot 258, `Weapon_FrameUpdate`), each assertion
   naming its address. Delete only tests that pin the poll; list them in your report.

## Not yours

`StudioFrameAdvance` / `ResetSequenceInfo` and the row accessors (A2: you consume `SequenceEvents`
/ `SequenceLoops`, filled by A2 — your tests use a fixture row), `PostRun` itself (its call order
is already retail; `Weapon_FrameUpdate` is V4c), the weapon's handler, the cone (A3), the player's
and the camera's words and call (A4), the overlay stack (0015).

## Rules

README § "Rules for every agent of V4": no build, no editor, no suite; only your files; a line
another file needs goes in your report, exact. The query budget (10 s warns, 60 s stops; never a
file over ~200 KB whole). Text through Grep / Read / Glob. Do not commit. Report ≤300 words: what
you ported (addresses), the tests added and deleted, the cross-lane lines, open questions.
