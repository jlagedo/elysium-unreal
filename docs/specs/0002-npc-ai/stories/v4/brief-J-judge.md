# Brief J — the adversarial judge for V4 (one agent, no code, no build)

The owner's standing ruling (`spec.md` § The bug protocol, last paragraph): a bug that needs a later
phase, the pipeline or a re-bake goes to an adversarial judge, which **argues against doing it now**
and rules *implement now*, *leave a stub* or *file for later*, its reasons recorded in the triage.
Runs after R2 (and after R1 if Q2 below is raised), before V4a's seam.

Read `README.md` here (§2 M10, M12, M13; §7 K5; §8 Q2–Q4), `packets.md` (R1, R2),
`docs/specs/0014-ragdoll/spec.md`, `docs/vtmb/npc-ai/lifecycle.md` § "Nothing in base DIE plays a
death animation" and § "The ordered chain" (~:2705-2760), `stories/v1/triage.md` § "Judge's ruling,
V13" (the form of a ruling).

## The questions

1. **Q3, the corpse fall.** `damage_lethal_death` and `verbs_stealth_kill` assert a corpse on the
   floor; the port has no character `UPhysicsAsset` (0014/3, L), no calibrations (0014/1–2, M + M),
   no handoff (0014/5, XS). Options: (a) pull 0014/1–3 + 5 forward as V4d — pipeline physics-asset
   builder from the decoded `.phy`, the calibrations, a full character re-import (~1,434 s recorded,
   `$ELYSIUM_WORK_ROOT/logs/20260928T205853.840641Z-import-characters.json`), one build, the ragdoll's
   own collision at the handoff (the dead actor's collision is off today); (b) re-cut the two records
   to the death transaction step 2 owns (`death`, `OnDeath`, `corpse ragdoll`, the `ACT_DIERAGDOLL`
   seed `sequence`, nothing after death) and move `corpse_on_floor` to 0014's witness. Argue against
   each; rule.
2. **Q4, slot 247's bbox** — only if R2 item 5 shows `Flags2 & 4` set on an NPC class a step-2
   record or map reaches. Options: the bbox on the `UElysiumBodyData` row (re-authors `DA_` assets;
   whether the animation packages re-cook is unverified — check `pipeline/.../import_characters.py`
   `:170-245` for the fingerprint), in `clip_data.py` (changes `clipDataSha256`, a full re-import), or
   the named seam left. Rule.
3. **Q2, N13 in baked data** — only if R1 found the walk fan's cells or scale wrong. Options: fix
   the bake now (pipeline + a character re-import) or file. Rule.
4. **N19, sequence 0** (filed by V3c's closing integrator, 2026-10-04; `stories/v1/triage.md` N19).
   Retail: `StartSequence 0x101a82d0` on a `LookupSequence` miss warns and writes `m_nSequence := 0`
   (`0x101a833d`); `ResetSequenceInfo 0x10090950` plays the model's own sequence 0 at rate 1.0
   (`0x10090a23`) and `StudioFrameAdvance 0x1008f120` raises `m_bSequenceFinished` at its length. The
   port's bridge row 0 plays nothing (`ElysiumNpcAnim.cpp` `SequenceRowFor` / `PlaySequenceClip`,
   `ElysiumNpcBaseAnim.cpp` `ResetSequenceInfo` cycle rate 10.0), so a missed scene label ends in one
   think. Five shipped scene labels miss (`entity_io.md:2469-2476`); witness `script_walk_to_mark`
   (`sequence seq 0 rate=0`, `seqfinished seq 0` in one think; `expected-fail` on N19).
   **What the bake already carries** (read 2026-10-04): the body table's rows carry the GLOBAL flat
   sequence number, `rawIndex = sequenceBase + position in the owner's .mdl`
   (`pipeline/src/elysium_pipeline/importers/body_data.py:83-89`, `includeOwners` with
   `sequenceBase` `:118`), loaded as `FElysiumBodySequence::RawIndex` (`Public/ElysiumBodyData.h:32`,
   `:51-56`) and copied into the runtime vocabulary `FElysiumNpcClip::RawIndex`
   (`Private/ElysiumBodyData.cpp:14`). So sequence 0 is the clip whose `RawIndex == 0` — **no re-bake
   needed**, unless the export dropped it (`skeletal_stage/unit.py:226-229` drops empty-label,
   duplicate-label and invalid-base-cell descriptors; unverified per model). The comment at
   `Visual/ElysiumAnimationResolve.cpp:293-302` ("the character export writes no raw index") is stale.
   **What is missing**: no embodiment query answers "this stem's `RawIndex 0` clip" (a new
   `IElysiumEmbodiment` accessor through `AElysiumMapActor` → the bodies, the recording double's
   default answering none), the bridge's row 0 resolved from it with the clip's own `STUDIO_LOOPING`
   bit, and the trace's `seq 0` naming (`ElysiumNpcBaseTrace.cpp:119` prints a row's label). Why not
   done in V3c's close: a cross-layer interface change in V4a A2's files (`ResetSequenceInfo`, the
   row accessors) under a one-build cap with no second build to correct it. Options: (a) V4a A2 lands
   it with the row words (XS, no pipeline); (b) a pre-V4a XS lane; (c) file. Then
   `script_walk_to_mark`'s `plays` (`seq 0 rate=1`) and the timing after it re-measured. Rule.

## Output

Append a section "Judge's rulings, V4" to `docs/specs/0002-npc-ai/stories/v1/triage.md` (for,
against, the ruling, the cost) — that file only. Report ≤200 words: each ruling in one line. The
query budget (10 s warns, 60 s stops; never a file over ~200 KB whole); text through Grep / Read /
Glob; no sleep or polling loop.
