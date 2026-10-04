# Brief V11-3 — slot 331 `ChooseMeleeAttackSequence 0x10347180`, whole (coder; no build)

Planner, 2026-10-04, after `../v4/packets-S5.md` item 4. The `0x51` arm of the melee band
(`0x103ea7e0`, ported by V5a-1) asks owner slot 331; the port's slot 331 is a counting stub that
answers false (`ElysiumCombatCharacterSlots.cpp` ~:624-634), so no weapon-armed NPC can ever raise
`CAN_MELEE_ATTACK1`. It runs in **V11's wave** ([V11-1, V11-2, V11-3], after V5a, V4a and V4b)
because its only records are V11's (`melee_swing`, `chase_melee`) and its stub sits in a file that
is A3's in V5a's wave.

Read `../v4/packets-S5.md` item 4 (the body, arm by arm — the authority beside the listing
`vtmb_asm 10347180`), `README.md` here §1, `CLAUDE.md`, `spec.md` § Standing rules,
`../v4/README.md` § "Rules for every agent of V4", `uv run elysium research section 0x10347180`
(`animation_rig_resolution.md` § "The NPC melee selector"; where it and S5 disagree, S5 wins and
you write the doc's corrected line in your report). **Re-locate every site by Grep.**

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumMeleeSequenceChoice.h`, `.cpp` (new: the body and the
  picker, plain C++, no reflection)
- `Source/ElysiumUE/Private/Substrate/ElysiumCombatCharacterSlots.cpp` (slot 331's stub body
  ~:624-634 and its table row ~:1466-1469, `EElysiumNpcSlotBody::Stub` → the ported value; these
  only — the virtual's signature in `Public/ElysiumCombatCharacterSlots.inl` ~:257 does not change:
  the `void*` is retail's `int* outSequence`)
- `Source/ElysiumUE/Private/Tests/ElysiumMeleeSequenceChoiceTests.cpp` (new)

## What the bake carries (checked 2026-10-04; no pipeline change, no re-bake)

| retail (`mstudioseqdesc_t`) | pipeline | runtime |
|---|---|---|
| `+0x2cc` near edge | `formats/mdl_skel.py:273` `_SEQ_LOW_REACH`; `importers/clip_data.py:19` `low_reach_cm` | `FElysiumNpcClip::LowReachCm` (`Public/Visual/ElysiumNpcClips.h:98`; −1 = unstated, retail's `FLT_MIN`) |
| `+0x2d0` far edge | `mdl_skel.py:262` `_SEQ_REACH`; `clip_data.py:18` `reach_cm` | `ReachCm` (`:80`; 0 = unstated, retail's `FLT_MAX`) |
| `+0x2bc` / `+0x2c0` boxes | `mdl_skel.py:290-292`; `clip_data.py:22` `envelopes` | `Envelopes` (`:104`, `FElysiumMeleeEnvelope`, `Public/ElysiumMeleeEnvelope.h`) |
| `+0x2d4` button mask | `mdl_skel.py:346, 752` (`combo.mask`; −1 unset); `clip_data.py:23` | `Combo.Mask`; `seq+0x2d4 >= 0` is `Combo.HasStateMask()` (`Public/ElysiumComboChain.h:225`) |
| the sequence's movement (`0x100c6020`) | `mdl_skel.py:1704` `movement_table`; `clip_data.py:50-61` | `FElysiumClipMovementPath` + `ElysiumClipMovement::SampleDelta` (`Public/ElysiumClipMovement.h:76, 253`); `FElysiumBlendGrids::FindMovement` (`Public/Visual/ElysiumBlendGrids.h:234`) |
| `+0x10` weight | `clip_data.py:16` | `Weight` (`:49`) |

All centimetres, Unreal axes (the movement's Y is already reflected: do not negate it again);
`dist2D`, `debug_melee_npc_range` and the listing's constants are Source units — convert at the
line and say which side. Unstated edges: use retail's own markers so the arithmetic is retail's
(`+0x2cc` unstated = `FLT_MIN`, `+0x2d0` unstated = `FLT_MAX`).

## The job, in retail's order (S5 item 4's step numbers)

1. `*out = −1`; no model → false (step 1).
2. **The line gate** (`0x1034723d..0x103472fb`, NPC with an enemy): `0x102e37b0(move probe, my
   slot 192 point, the enemy's, 0x202400b, 1, &tr)`; blocked by an entity that is not the enemy,
   not the entity of the enemy's handle `+0x1538`, and whose `+0x4c` bit 10 is clear →
   `SetCondition(0x3a)`, false. Grep the port's move probe for a line trace
   (`0x102e37b0`, `0x102e30d0`; none cites them today): if none exists, call
   `IElysiumEmbodiment::TraceRetail` (read, not edited) and say so at the line.
3. **Enemy geometry** (`0x10347300`): collision-bounds centre and half-extents, `dist2D`, the
   enemy's box in the attacker's line frame (step 3).
4. `step` = NPC slot 522 (`+0x828`), else 4.0. `weapon == 0` → false; weapon slot 360 `& 0x18000
   == 0` → false (steps 4–5).
5. `GetSequencesForActivity(this, activity, …)` with the mode's modifier (`0x10204900`); `dir`,
   `right` (step 6). Use the accessor V5a-1's wave added for the band; candidates in ascending
   `RawIndex`.
6. **Per candidate, the flag word** (step 7), each bit citing its address:
   bit **8** (`+0x2cc <= dist2D <= +0x2d0`); the weapon's running `+0x8b8` min / `+0x8c0` max
   (their home is `ElysiumWeaponClasses.h`, V11-2's file: Grep for the words — `.h` ~:353 names
   them; if absent, the exact declaration goes in your report); bit **4**; the sequence's whole
   movement (`0x100c6020`, cycle 0 → 1) absent → bit **0x10**, no sweep; else the hull sweep
   (`0x102e3450`, mask `0x202400b`, both ends raised by `step`, the NPC's collision hull; the
   player: a hull trace) and its four outcomes — **`seq+0x2d4 >= 0` → `flags = 0`**
   (`0x1034795d..0x10347967`, NPC arm), not blocked → keep, blocked within
   `debug_melee_npc_range` (default 128; player: `fraction >= 0.9` or the remainder `<= 8.0`) →
   keep, else `hit == enemy && m_bAllowsInterpenetratingAttacks (+0xfe0)` → bit **1**, else bit 4
   cleared; bit **2** (any envelope strictly overlaps the enemy's line-frame box).
7. **The picker `0x10348100`** (`ChooseSequenceFromList`): candidates with `(flags & mask) ==
   mask`; none → −1; one → it; total weight `< 1` → uniform `RandomInt`; else the weighted
   `RandomInt` walk. Name the stream the port's other weighted sequence picks draw from and use it.
8. **The passes** (step 8): no enemy — mask `0x10`, then `0`, **returns false**; enemy — two
   passes over `7, 5, 6, 3, 1, 2, 4, 0`, the first with `| 8`; true only for the first pass's
   `7`, `5`, `6`.
9. **The slot** calls the body; delete the stub and its `FireCombatCharacterSlot` line.
10. **Check, do not edit**: `FElysiumNpcMingXiao::ChooseMeleeAttackSequenceSeam`
    (`ElysiumNpcMingXiao.cpp` ~:924, V11-1's file) stands for this slot and answers false — write
    in your report the exact replacement line (the call of slot 331 with `0x10398030`'s
    arguments) for the integrator. And any test that pins slot 331 as a stub (Grep `0x10347180`,
    `ChooseMeleeAttackSequence` under `Tests/`): list it with its line.
11. **Tests** `Elysium.Arm.MeleeSequenceChoice.*`, each assertion naming its address: `.Flags`
    (each bit alone: in range / out of range; movement clear / blocked beyond 128 / blocked by
    the enemy with and without `+0xfe0`; no movement → `0x10`; an envelope reaching / not
    reaching; a stated `+0x2d4` zeroes the word), `.LineGate` (a third entity on the line →
    `0x3a` and false; the enemy itself → no gate), `.Picker` (`0x10348100`: none, one, weights
    under 1, the weighted walk on a seeded stream), `.Passes` (mask order; true only on 7 / 5 / 6
    with bit 8; no enemy → false with a pick), `.Unstated` (edges at retail's markers).

## Not yours

The band `0x103ea7e0` and `GatherAttackConditions` (V5a-1, landed: it calls this slot), the swing
start (`RequestActivity 0x103e9e00`) and the contact (`ElysiumWeaponClasses.*`: V11-2), the
coordinator (V11-1), the player's selector (`PlayerSelectMeleeSequence`), `ElysiumNpcAnim.cpp`,
`Public/ElysiumWorldServices.h`, any pipeline file, any record under `Arena/`.

Wave check ([V11-1, V11-2, V11-3]): your three files are in neither list (V11-1: `README.md` §4;
V11-2: `brief-V11-2-melee-contact.md`). `ElysiumCombatCharacterSlots.cpp` is yours for slot 331
only; a line V11-2 needs there (slots 318–329) reaches the integrator.

## Rules

Coders never build, never launch the editor, never run the arena or a suite. Retail first: follow
the listing, cite the address at every line. A divergence is recorded in your report, not adopted.
Query budget: 10 s warns (log to `$ELYSIUM_WORK_ROOT/logs/slow-queries.tsv`), 60 s stops — never
retried as-is or widened. Never read a file over ~200 KB whole. Text through Grep / Read / Glob.
`research where <addr>` before searching `docs/`. Do not commit. Report ≤300 words: what you
ported (addresses), which probe stood for `0x102e37b0` / `0x102e3450`, the picker's stream, the
MingXiao line, tests added and pinned-stub tests found, cross-lane lines, what stays unrecovered.
