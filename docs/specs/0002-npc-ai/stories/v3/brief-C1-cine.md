# Brief C1 — V3c: the cine writes the NPC's words and claims nothing; the beat stand-in deleted

Read `README.md` here (§1 "how a scene holds an NPC", §2 M9–M10, §4 V3c, §7 K1/K2),
`docs/specs/0003-scripted-sequence/spec.md` § Witness data, `docs/vtmb/entity_io.md` § "Scripted
sequences" and `docs/vtmb/npc-ai/authored-control.md` § `0x101a7880`, § `0x101a8640`. Runs after
V3b. You never build.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumScriptedSequence.h`, `ElysiumScriptedSequence.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumAiScriptedSequence.h`, `ElysiumAiScriptedSequence.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumAiScriptedSchedule.h`, `.cpp` — only where they reach
  the beat or `NpcScriptState`

## The job

1. **`m_scriptState` is the NPC's word** (M10). Delete `NpcScriptState`, `ScriptStateOf`,
   `SetScriptStateOf` (`ElysiumScriptedSequence.h:129-132, 180-186`; `.cpp:482-488, 761-768`). Every
   write becomes `Npc.SetScriptState(n)` at the retail site: `PossessEntity`'s `m_fMoveTo` switch
   (`.cpp:400-423`, `0x101a7d26..0x101a7e7a`), the `CCineAI` twin (`ElysiumAiScriptedSequence.cpp`,
   `0x101a9080`), `ScriptEntityCancel` (→ 3, `0x101a7170`), `CineCleanup` (→ 0, `0x1027d170`). C3
   moves the task arms' writes (`0x62` → 0, `99` → 2).
2. **The beat stand-in goes** (M9; divergence 18 closed): `EBeatPhase`, `Phase`, `BeatThinkAt`,
   `BeatClipEndsAt`, `bTravelled`, `bResumeTravel`, `bPreIdleStarted`, `bRestartBeat`,
   `RestoredNpcIndex` if only the beat reads it, `BeginBeat`, `EndBeat`, `TickBeat`, `StartTravel`,
   `PlaceOnMark`, `Arrive`, `PlayBeatClip`, `BeatNpc`, `GBeatTickSeconds` and the beat's constants
   (`.h:24-31, 210-229, 262-276`; `.cpp:1263-1512`). `PossessEntity` ends at `RequestIdealStateRetail`
   (`.cpp:424`): retail's last statement. `ThinkAt` (`.cpp:552`) runs only the installed think
   (`CineThink 0x101a8070`, `SUB_Remove`). Rewrite the header's class comment: the NPC's own
   scripted schedules run the scene; no stand-in.
3. **`ClaimBody`** keeps only the collision view of `NAV_IGNORE_NPC 0x40` (spawnflag `0x1000`,
   `IsIgnoreCollisionEntity`), which `CineCleanup`'s restore of `+0x5f8c` undoes; inline it at the
   possession site. No `ClaimScriptBody` call: C2 deletes it.
4. **`StartSequence`** (slot 584, `0x101a82d0`, `.cpp:495-518`) does retail's three statements on the
   NPC: `m_nSequence = LookupSequence(name)` (−1 → the warning and sequence 0), `m_flCycle = 0`,
   `ResetSequenceInfo()` — through the NPC base's own words and `CommitForcedSequence`'s door
   (`ElysiumNpcBaseAnim.cpp:110-117`), with the cycle reset. `LookupSequenceByName` is C3's to fill.
   No montage segment, no length stand-in: `TASK_PLAY_SCRIPT` reads the kernel's
   `m_bSequenceFinished`. `bCompleteOnEmpty` stays.
5. **`CineCleanup`** (`.cpp:821-893`): drop the `EndScriptMove()` / `ReleaseScriptBody()` calls
   (`857-858`, C2 deletes both); the state write is the NPC word. Check the bone placement (spawnflag
   `0x2000` → `MoveToBoneOrigin("Bip01")`, else bone 0, `+1` Z, `FL_ONGROUND`, the floor probe; bit
   `0x80` skips): port it if the doc's walk and the port's accessors allow; else keep today's seam and
   name it.
6. **Save** (K1, owner-pending): `SaveBlockReason` (`.cpp:1516-1522`) refuses while this cine
   possesses an NPC (its target's `m_hCine == this`), with a comment naming K1 and V6 as its end.
   `Serialize` / `OnPostRestore` (`.cpp:1524-1582`) drop the beat's fields; keep the datamap words and
   the `m_hCine` re-stamp.
7. Check `Activate` / `PreloadForActivation` play nothing (`0003` spec: "`Spawn` and `Activate`
   resolve names and play nothing"); a preload is fine.

## Not yours

`ElysiumNpc.*`, `ElysiumNpcBase.*`, `ElysiumScriptedCharacter.*`, `Public/ElysiumEntity.h` (C2);
`ElysiumNpcBaseAnim.*`, `ElysiumNpcAnim.cpp`, the task arms, every test (C3). The choreographed
scene (`ElysiumChoreoScene.cpp`, 0010's) is untouched.

## Rules

README § "Rules for every agent of V3". The query budget: 10 s warns, 60 s stops; never read a file
over ~200 KB whole. Text through Grep / Read / Glob. No sleep, no polling loop. Do not build, do not
commit.

## Report (≤300 words)

Per change, the retail address; what `CineCleanup`'s bone placement now does; every call another
lane must provide (`SetScriptState`, `LookupSequenceByName`'s answer, …) and every deleted symbol
another file still names.
