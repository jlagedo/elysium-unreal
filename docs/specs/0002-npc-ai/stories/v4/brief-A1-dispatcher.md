# Brief A1 — V4a: slot 258 in `PostRun`, the world poll retired for NPCs (coder; no build)

Read `README.md` here (§1 "The frame", "The events"; §2 M1–M2; §4 V4a), `packets.md` § R2 items 1
and 4, `docs/vtmb/animation_events.md` :39-107. After the seam commit. Re-locate every site by Grep.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseAnimEvents.cpp` (the seam's override)
- `Source/ElysiumUE/Private/Substrate/ElysiumAnimEvents.{h,cpp}`
- `Source/ElysiumUE/Private/Substrate/ElysiumAnimatingImpl.cpp` (`FElysiumAnimating::AdvanceAnimEvents`,
  ~:310-396; the `animevent` tap ~:373-377)
- `Source/ElysiumUE/Private/Substrate/ElysiumEntityWorld.cpp` (`FElysiumEntityWorld::AdvanceAnimEvents`
  ~:2077-2099: the entity filter only)
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelAnimEventsTests.cpp` (new), and any existing test
  of `ElysiumAnimEvents::Advance` that names an NPC (Grep `Tests/`).

## The job

1. **The base dispatcher `0x10091880`** as a kernel function in `ElysiumAnimEvents` (a new entry
   beside today's `Advance`, which stays for the player, props and the camera, README §7 K3): inputs
   the kernel's words — the sequence's cycle rate × `m_flPlaybackRate`, `m_flCycle`, `LastEventCheck`,
   `SequenceLoops(m_nSequence)`, `SequenceEvents(m_nSequence)`; it writes `m_bSequenceFinished`,
   `SequencePastHalf`, `LastEventCheck` exactly as `animation_events.md` :43-63 (the 0.1 at
   `0x104491b4`, the look-ahead end stored, the wrap clause only with `STUDIO_LOOPING`, `>= 5000`
   skipped, strict top, non-looping clamp to 1.0) and calls `OnSequenceFinished` on the rising edge
   (the slot R2 names; if the port has no body for it, call the seam it has or declare none and say
   so in your report). Each fired event goes to slot 259 `HandleAnimEvent` on the handler, in table
   order, with the runtime copy's fields (`animation_events.md` :31-37; `eventtime` computed even
   though no handler reads it).
2. **The overlay wrapper `0x10098c80`** as the NPC's slot 258 in `ElysiumNpcBaseAnimEvents.cpp`: the
   base, then the four layers. The port has no kernel `CAnimationLayer`: the layer loop is a seam
   answering "no active layer", named for `CAnimationLayer` / `0x10098cd0`, with R2 item 4's answer
   in the comment.
3. **The tap**: the `animevent` trace (`<id> <options>`, `stories/wave2/seam.md` :46) is emitted
   from the kernel dispatcher for NPCs; the poll no longer emits it for an NPC.
4. **The poll retired for NPCs**: `FElysiumEntityWorld::AdvanceAnimEvents` skips every entity that
   is an `FElysiumNpcBase` (the player, props, camera unchanged). The census of unclaimed ids moves
   with the tap if it is cheap; otherwise report it.
5. **Tests** (`ElysiumNpcKernelAnimEventsTests.cpp`): `Elysium.Arm.NpcKernelAnimEvents.Window` and
   `.PostRunOrder` as README §6 lists, each assertion naming its address. Delete only tests that pin
   the poll for an NPC; list them in your report.

## Not yours

`StudioFrameAdvance` / `ResetSequenceInfo` and the row accessors (A2: you consume
`SequenceEvents` / `SequenceLoops`, filled by A2 — your tests use a fixture row), `PostRun` itself
(its call order is already retail; `Weapon_FrameUpdate` is V4c), the weapon's handler, the cone.

## Rules

README § "Rules for every agent of V4": no build, no editor, no suite; only your files; a line
another file needs goes in your report, exact. The query budget (10 s warns, 60 s stops). Text
through Grep / Read / Glob. Do not commit. Report ≤300 words: what you ported (addresses), the
tests added and deleted, the cross-lane lines, open questions.
