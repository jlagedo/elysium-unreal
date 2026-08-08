# SCRATCH — the action→animation vocabulary

**Deliberately outside the project's documentation policy**, for the same reason
`SCRATCH-animation-spike.md` is: it carries open questions and pendencies, which are status, and
status lives only in the master roadmap and its two subtrackers. Companion to that file — the spike
owns *how a pose is built*, this owns *what asks for one*. Where it disagrees with `docs/vtmb/` or
`docs/architecture/`, those win, and every VtMB fact here is owed to
`docs/vtmb/animation_and_movers.md` once it settles.

Measured against the owner's own install and the current export, `sp_tutorial_1`, `sp_theatre` and
`sm_hub_1` plus their level scripts. Counts are over those three maps only unless stated.

**§1–§4 are the vocabulary and the demand. §5 is the pendency list — every open question, each with
the oracle that answers it.** A statement anywhere but §5 is a fact.

---

## 1. The six terms, and the one line that matters

`docs/architecture/animation-architecture.md` §3 already names the layers. This is that ladder with
the concrete identities filled in, because the abstraction is only useful once you can point at each
rung:

| Term | What it is | Player example | NPC example |
|---|---|---|---|
| **command** | one frame of requested input | `FElysiumUserCmd`: forward + `+speed` + `IN_JUMP` | — (NPCs have none) |
| **body sample** | what the body *did*, after collision | grounded, 214 cm/s, `move_yaw` −12°, not ducked | motor's post-tick velocity and facing |
| **action request** | a gameplay/script claim on a channel | jump pressed, weapon drawn, took a hit | `scripted_sequence` beat, interesting place, schedule task |
| **activity** | the stable VtMB key | `ACT_RUN` | `ACT_WALL_LEAN_IDLE` |
| **sequence** | the model's own label for that activity | `run` (weight 1, owner `…runotherspc_pcidles_allsequences`) | `wall_lean_idle_01` |
| **animation asset** | what Unreal evaluates | the baked `run` blend space, steered by `move_yaw` | one baked `UAnimSequence` |

**The load-bearing line: a key press never names an asset, and neither does a script.** The one
exception is deliberate and retail has it too — `player_sequence`, `SetAnimation()`,
`scripted_sequence`'s `m_iszPlay` and a choreographed scene's own events name an exact label and
bypass activity translation entirely (`docs/vtmb/animation_and_movers.md` A.3, the `0x10348560`
handler). Those are §3's *direct* column. Everything else goes through an activity.

That split is not stylistic. It is why §3's table has two halves that need two different runtime
doors, and why building only one of them leaves half the shipped content unreachable.

---

## 2. The player's own vocabulary — measured on `tremere_male_armor_0`

The parity-slice body, and the first slice's subject. Its clip set resolves **1,462 labels across 21
owner banks** and **1,106 distinct activity literals**. Everything retail's ordinary player selector
(`0x10164870`, `docs/vtmb/animation_and_movers.md` A.3) can reach, against what this body actually
carries:

| Activity | Label | Owner bank | Shape | Frames@fps | loop | fade | authored ground speed |
|---|---|---|---|---|---|---|---|
| `ACT_IDLE` | `idle01` (w30) + `fidget01/02/03` (w1) | `~misc` | clip | 61@30 | yes | 0.3 | — |
| `ACT_WALK` | `walk` | `~move_and_ranged` | **9×1 grid** on `move_yaw` | 46@30 | yes | 0.2 | 60.7–136.7 cm/s per cell |
| `ACT_RUN` | `run` | `~runotherspc_pcidles_allsequences` | **9×1 grid** on `move_yaw` | 19@30 | yes | 0.2 | 457.8–528.3 cm/s per cell |
| `ACT_SNEAK` | `sneak` | `~move_and_ranged` | **9×1 grid** on `move_yaw` | 66@30 | yes | 0.2 | 69.7–79.3 cm/s per cell |
| `ACT_CROUCH` | `crouch` (w30) | `~move_and_ranged` | clip | 61@30 | **no** | 0.2 | — |
| `ACT_HOP` | `hop` | `~misc` | clip | **2**@30 | no | 0.2 | — |
| `ACT_HOP_UP` / `ACT_HOP_DOWN` | `hop_up` / `hop_down` | `~misc` | clip | 15@30 / 12@30 | no | 0.2 | — |
| `ACT_LEAP` | `leap` | `~misc` | clip | 46@30 | no | 0.2 | — |
| `ACT_LEAP_ASCEND` / `ACT_LEAP_DESCEND` | `leap_ascend` / `leap_descend` | `~misc` | clip | 31@30 | **yes** | **0.45** | — |
| `ACT_FALLING` | `falling` | `~misc` | clip | — | yes | 0.2 | — |
| `ACT_LAND` / `ACT_LAND_HARD` | `land` / `land_hard` | `~misc` | clip | 20@30 / 71@30 | no | 0.2 | — |
| `ACT_SWIM` / `ACT_TREADWATER` | `swim` / `treadwater` | `~misc` | clip | 39@30 | yes | 0.2 | — |
| `ACT_CLIMB_UP` / `ACT_CLIMB_DOWN` | `ladder_up` / `ladder_down` | `~misc` | clip | 29@30 | yes | 0.2 | — |
| `ACT_AIM_<weapon>` | `<weapon>_ready` | `~move_and_ranged` | **3×3 grid** on `aim_yaw`/`aim_pitch`, masked | 60–61@30 | yes | 0.2 | — |
| `ACT_WALK_RELAXED` / `ACT_RUN_RELAXED` | **absent unarmed** — only `<weapon>_relaxed_walk/run` | | | | | | |

Four things fall out that a graph has to be built around rather than patched for:

**The relaxed gaits do not exist unarmed, and retail says so.** `CBasePlayer::NPC_TranslateActivity`
(`0x101647a0`) maps `ACT_WALK_RELAXED → ACT_WALK` and `ACT_RUN_RELAXED → ACT_RUN` and leaves
everything else alone. The weapon-suffixed relaxed gaits are the only ones with clips, so the
translation is what makes the unarmed case resolve at all. That is one recovered table row, not a
fallback we invent.

**`ACT_RUN` is not owned by `move_and_ranged`.** It resolves through the include DAG to the
PC-only bank `character_shared_male_runotherspc_pcidles_allsequences`, whose `run` grid is a
different, faster set of cells than `move_and_ranged`'s (forward 478.7 cm/s against the NPC bank's
478.7 with a 313.2 backpedal against 522.0). **The player and the cast do not share a run.** A
resolver keyed on label alone rather than on the owner the DAG names would silently give the player
the NPC's gait.

**`ACT_CROUCH` does not loop.** `crouch` is a 61-frame one-shot with `flags == 0`. It is an
*into* pose; nothing in the model names a crouched idle for the unarmed player (only
`ACT_CROUCH_MELEESHARED_TWOHAND → crouch_idle` and the weapon `<weapon>_crouch` set). What holds a
held crouch is §5's PEND-4.

**`ACT_HOP` is two frames.** Not a jump; a stub. The airborne family that carries real animation is
`leap_ascend` / `leap_descend`, both looping and both asking for a 0.45 s fade — more than twice the
0.2 s the rest of the vocabulary asks for, which is the authors saying the jump transition is meant
to be soft.

### 2.1 The `move_yaw` fan, and the trap in its own names

Cell *k* of a 9×1 fan sits at `move_yaw = −180 + 45k`, and the clip's own numeric suffix **is that
angle taken mod 360**. Read off the `walk` grid:

| k | `move_yaw` | clip | cm/s |
|---|---|---|---|
| 0 | −180 | `walk` | 88.6 |
| 1 | −135 | `walk_225` | 113.9 |
| 2 | −90 | `walk_270` | 97.1 |
| 3 | −45 | `walk_315` | 88.1 |
| 4 | **0** | `walk_0` | **136.7** |
| 5 | +45 | `walk_45` | 88.1 |
| 6 | +90 | `walk_90` | 60.7 |
| 7 | +135 | `walk_135` | 113.9 |
| 8 | +180 | `walk` | 88.6 |

**The label the sequence carries is the 180° cell, not the 0° one.** `walk` *is* `walk_180`. So
standing the bare label `walk` as a clip stands the cell at the wrap seam, and standing the grid at
a `move_yaw` nobody wrote stands it too. `FElysiumPoseParams::Neutral()` reads every parameter as
0, which lands on cell 4 — correct today by construction rather than by a rule anything states, and
the moment something writes `move_yaw` that construction is what moves.

Which physical direction 0 *is* is PEND-1. The fastest cell being 0 and the slowest strafe being 90
is consistent with 0 = forward, and it is not proof.

---

## 3. What the shipped content actually asks for

Two doors, and both have to exist. The census below is the three maps plus their level scripts.

### 3.1 The direct door — content that names an exact sequence label

| Producer | Key / call | Semantics | Count over the 3 maps |
|---|---|---|---|
| `scripted_sequence` | `m_iszIdle` | pre-action pose, looping, held until `BeginSequence` | 8 |
| | `m_iszPlay` | the action; **its length times `OnEndSequence`**, one-shot | 37 |
| | `m_iszPostIdle` | held after the action, looping | 20 |
| | `m_iszCustomMove` | the clip played *while walking to the mark* (`ACT_SCRIPT_CUSTOM_MOVE`, registry `0x18`) | 8 |
| `aiscripted_sequence` | same four | same, but the AI keeps its schedule (`m_iFinishSchedule`) | 4 entities |
| `prop_dynamic` | `LoopSequence` | looping prop clip | 38 (`idle` ×30, `palmtree_idle` ×6, `fly` ×2) |
| | `demo_sequence` | initial/authoring pose | 24 non-`None` |
| `item_container_animated` | `demo_sequence` | the container's open clip | 1 (`open`) |
| `prop_dynamic_ornament` | `demo_sequence` | a worn prop's own clip | 1 (`attack`) |
| `npc_V*` | `demo_sequence` | spawn pose override | 3 (`idle01`, `cower_idle`) |
| `logic_choreographed_scene` | `BaseAnim` / `MaleAnim` / `FemaleAnim` + `.vcd` `Sequence`/`Gesture` events | absolute-time cinematic playback out of a named cinematic bank | 28 scenes, 13 distinct `BaseAnim` |
| level script | `Ent.SetAnimation(label)` | direct label, no translation | 6 (all `santamonica.py`) |

**Every label the three maps name directly**, with the key that names it:

```
alert_r45_into / alert_r45_outof          hub    m_iszPlay / m_iszPostIdle
animalism_spectralwolves_idle             tut    m_iszPostIdle
attack                                    tut    ornament demo_sequence
claws_aggressive_run                      tut    m_iszCustomMove
Converse_Normal_Talk_A                    thea   m_iszPlay + m_iszPostIdle
cower_idle                                tut/hub m_iszPostIdle, npc demo_sequence
d_thaum_idle                              tut    m_iszPlay
doom_walk                                 hub    m_iszCustomMove
enter_apt                                 hub    m_iszPlay
feeding_attacker_shortvictim_front_{engage,bite,feed_loop,attack_release}   tut  m_iszPlay
feeding_victim_shortattacker_front_{engage,bite,feed_loop,attack_release}   tut  m_iszPlay
fly / idle / palmtree_idle / idle01       all    prop LoopSequence / demo_sequence
forgetit                                  tut    m_iszPlay
howl                                      tut    m_iszPlay
knockback_normal_high_back                tut    m_iszPlay
lockpick                                  hub    m_iszPlay
look_out_window                           tut    m_iszPostIdle
Low_CrouchFidget                          tut    m_iszPostIdle
open                                      tut    container demo_sequence
piss_outof                                hub    m_iszPlay
praying_begin / praying_idle              hub    m_iszIdle / m_iszPlay / m_iszPostIdle
rant_fist_down / rant_hand_up / rant_wave hub    m_iszPlay
sabbat_shooting_in_the_air                tut    m_iszPostIdle
sheriff_look_up / sheriff_spell_cast / sheriff_walk_in   tut  m_iszPlay
shovelhead_look_over_shoulder             tut    m_iszPlay
smith_fidget                              hub    m_iszPlay
sobbing / sobbing_idle / sobbing_into     hub    m_iszIdle / m_iszPlay / m_iszPostIdle
stance_flirt_idle_2                       hub    m_iszIdle / m_iszPlay / m_iszPostIdle
submachinegun_aggressive_walk             tut    m_iszCustomMove
submachinegun_ready / submachinegun_attack tut   m_iszPostIdle
submachinegun_sneak                       tut    m_iszCustomMove
vomit_getout                              hub    m_iszPlay
wall_lean_idle_01                         thea   m_iszPlay + m_iszPostIdle
waveover01                                tut    m_iszPlay
```

plus, from `santamonica.py`: `claws_attack_low`, `fists_attack_shortright`,
`claws_attack_medcombo`, `fists_dodge`, `katana_attack_sweepkick`, `katana_attack_flipkick` — six
labels a Python script names on an NPC to stage the alley fight.

**`m_iszCustomMove` is the only one of these that is not a pose.** It names the locomotion clip an
actor plays *while travelling*, which means the scripted-sequence path needs a moving body and a
travel clip, not just a one-shot slot: `claws_aggressive_run`, `doom_walk`,
`submachinegun_aggressive_walk`, `submachinegun_sneak`. `m_fMoveTo` says how it gets there —
over the three maps `{0: 35, 1: 31, 2: 13, 3: 8, 4: 3, 5: 2}`, so **57 of 92 beats travel**.

### 3.2 The activity door — content that names a behaviour and lets the model choose

| Producer | How it names an activity | Over the 3 maps |
|---|---|---|
| `intersting_place` + `interestingplacetypelist.txt` | `type` → a weighted `ACT_*` set | 116 places, 14 distinct types |
| `dispositiontable.txt` via `default_disposition` / `.dlg` `SetDisposition` | disposition → `Stance_<Name>_Idle_*` / `ACT_DISPOSITION_*` | every NPC; 8 `ACT_DISPOSITION_*` on the PC body |
| NPC schedules and tasks | the desired activity | **not decoded** — PEND-7 |
| the player classifier | the compact code → base activity | **not decoded** — PEND-2 |
| weapon `acttable_t` | base activity → weapon activity | **not extracted** — PEND-3 |
| `npc_maker` | inherits the spawned class's own selection | 62 makers |
| `combat_start_activity` | an explicit override on spawn | present but **`-1`/`ACT_INVALID` on every entity in all three maps** |

**The ambient activity set these three maps demand**, resolved through the vdata table:

```
ACT_IDLE                     (Idle ×69, Citizen_Idle ×12, Doorknock, bum_rest, bum_idle, conversation_normal)
ACT_ARMSCROSSED_IDLE / _FIDGET                      (arms_crossed ×3)
ACT_CELLPHONE_INTO / _IDLE / _OUTOF                 (cellphone ×1)
ACT_CIGARETTE_INTO / _IDLE                          (cigarette ×2)
ACT_CONVERSE_NORMAL_TALK / _LISTEN                  (conversation_normal ×2)
ACT_DOORKNOCK                                       (Doorknock ×2)
ACT_DRINK_INTO / _IDLE / _TAKE / _LOOKAROUND / _OUTOF  (can_drink ×1)
ACT_HUDDLE, ACT_SLEEP                               (bum_rest ×9)
ACT_LIKE_A_BUM                                      (bum_idle ×4)
ACT_PAY_PHONE_PICKUP / _IDLE / _HANGUP              (payphone ×2)
ACT_PISS_INTO / _IDLE / _OUTOF                      (piss ×2)
ACT_TOOL_HACKPANEL                                  (operate_panel ×2)
ACT_WALL_LEAN_INTO / _IDLE / _OUTOF                 (wall_lean ×5)
```

**The shape of that set is the design brief.** Nearly every one is an `INTO → IDLE → OUTOF` triple.
That is a montage with three segments and a hold, not a state-machine state, and it is the same
shape a `scripted_sequence`'s `m_iszIdle` → `m_iszPlay` → `m_iszPostIdle` has. **One mechanism
serves both**, and building the ambient behaviour on top of the scripted-sequence beat rather than
beside it is what stops there being two.

---

## 4. What the first slice needs, and what it does not

The slice is **`tremere_male_armor_0` in the green room, responding to movement and jump**. Against
§2 and §3 that is a tiny, sharply bounded subset:

**In the slice** — six activities, three of them grids:

```
ACT_IDLE     -> idle01                 clip,  looping
ACT_WALK     -> walk                   9x1 grid on move_yaw
ACT_RUN      -> run                    9x1 grid on move_yaw   (PC bank, not move_and_ranged)
ACT_SNEAK    -> sneak                  9x1 grid on move_yaw
ACT_CROUCH   -> crouch                 clip,  one-shot
ACT_LEAP_ASCEND / _DESCEND / ACT_LAND  clip,  the jump chain
```

**Not in the slice, and deliberately:** every weapon activity, every `_layer` and `_delta`, the aim
grids, the whole ambient/interesting-place set, the scripted-sequence and choreographed paths, and
the `ACT_WALK_RELAXED`/`ACT_RUN_RELAXED` translation (nothing selects a weapon yet, so nothing
reaches the table that needs it).

**What that buys.** None of the six is masked and none is additive, so **none of them needs the
derived `<label>@<host>` naming** the bake writes for the layer families — those grids build once as
a plain label. The slice therefore does **not** depend on the layer-addressing work, the accumulation
order, the layer weight or the aim-offset node. That is the whole reason to cut it here: it is the
largest piece of the graph that can be built with nothing else finished.

---

## 5. Pendencies

Each is a question with an oracle. Nothing here is settled, and nothing else in this file is open.

### Player movement

**PEND-1 — the sign and reference of `move_yaw`.**
Is it `yaw(velocity) − yaw(facing)` or the reverse, and is `move_yaw = 0` forward? The cell
placement is measured (§2.1) and the fastest cell is 0, which is *consistent* with forward but does
not establish it — a mirrored convention reproduces the same cell speeds. Getting it backwards
gives a body that strafes when it walks and nothing logs.
*Oracle:* `0x10164870` writes `move_yaw`, `aim_yaw` and `aim_pitch` before choosing a base activity
(`docs/vtmb/animation_and_movers.md` A.3). Read the two yaws it differences. A live A/B in the
green room is a check, not a proof.

**PEND-2 — the player's compact action-code vocabulary.**
`0x1016bb50` returns `−1` or a compact integer from post-move state; `0x10164240` routes it through
the animation mode. Neither the code set nor the mode set has a symbolic name. Every branch of the
locomotion state machine is one of these codes, so the machine's *states* are currently inferred
from the clip vocabulary rather than recovered.
*Oracle:* ANM4a's `ELGACT1` instrument on the `sm_hub_1` recipe — the controlled trace has not been
run. Static decompile alone cannot prove which codes gameplay reaches.

**PEND-3 — the third land activity.**
Retail's selector reaches three land activities at registry `0x30`–`0x32`; the body carries two
(`ACT_LAND`, `ACT_LAND_HARD`). The third's name is in the registry dump and its clip, if any, is not
on this model.
*Oracle:* the recovered 3,045-name activity table, then a vocabulary sweep across the 56 player
bodies.

**PEND-4 — what holds a held crouch.**
`crouch` is a 61-frame non-looping clip and no unarmed crouched idle exists in the vocabulary. Does
retail hold the last frame, loop the clip, or select something else once ducked?
*Oracle:* the controlled trace with `IN_DUCK` held — the emitted activity is what answers it, and it
is the same run PEND-2 needs.

**PEND-5 — which jump family the player uses, and when.**
The registry exposes `ACT_HOP`/`_UP`/`_DOWN` (`0x28`–`0x2a`) and `ACT_LEAP`/`_ASCEND`/`_DESCEND`
(`0x2c`–`0x2e`) plus `ACT_FALLING`. `hop` is two frames; `leap_ascend`/`leap_descend` are 31-frame
loops asking for a 0.45 s fade. Which the classifier emits on a press, at apex, and on a fall that
was not a jump is unknown. VtMB's jump is a *held* push (`docs/vtmb/source_movement.md`), so the
ascend phase has a variable length no clip can assume.
*Oracle:* PEND-2's trace, driven through a tap-jump, a held jump, a fall off a ledge and a
crouch-jump.

**PEND-6 — the speed coupling, and whether we close it.**
`CHL2_Player::PreThink` (`0x10350830`) sets `m_flMaxSpeed` **from the current sequence's root
motion**, scaled by `sv_walkscale`/`sv_runscale`/`sv_sneakscale` (1.0 / 1.0 / 2.3). The animation
therefore drives the movement, not the other way round. Our mover uses
`ElysiumMove::WalkSpeed`/`RunSpeed` (100 / 225 u/s — Troika's *stated* intent, and dead ConVars in
the retail build) and the code says so at the seam.
The authored numbers do not agree with the ConVars: `walk_0` is 136.7 cm/s = **53.8 u/s** against
`speed_walk` 100, and the PC bank's `run_0` is 478.7 cm/s = **188.5 u/s** against `speed_runbase`
225. Two unknowns sit under that gap — which cell the retail read samples (the neutral one, or the
one the live `move_yaw` selects) and what `sv_sneakscale` 2.3 divides or multiplies.
This is not only an RE question. **Closing the loop makes movement speed a function of the
animation blend**, which is a real design decision with a feel cost: it is also the only way the
sideways-run and forward-run speed difference stops being foot-sliding. `docs/project/animation-roadmap.md`
ANM5 already carries the per-cell half of it.
*Oracle:* the decompile of `0x10350830` for the read, and `uv run elysium debug move` for the
resulting course times against a retail capture.

**PEND-7 — the NPC schedule→activity call graph.**
Open and named as open in `docs/vtmb/animation_and_movers.md` A.3. Out of the slice, but it is the
other producer feeding the same resolver, and building the seam without it is what keeps it honest.

**PEND-8 — the autolayer weight.**
The four-byte record carries no weight, ramp or flags. Unchanged from `SCRATCH-animation-spike.md`;
restated here only because it is the one pendency the *action* layer will hit as soon as a weapon
selects its own layers.

### Content commands

**PEND-9 — `m_iszCustomMove`'s combine.**
The clip is played while the actor travels to its mark. Whether it replaces the locomotion selection
outright or rides over it, and what happens at the mark, is not established. 8 uses over the three
maps.
*Oracle:* the `scripted_sequence` decompile beside the existing `CCineMonster` work, or a capture of
the tutorial's Sabbat approach.

**PEND-10 — `OnBeginSequence`'s firing point.**
Already recorded as not-RE'd in `ElysiumScriptedSequence.cpp`: at the input, or on arrival at the
mark. Restated here because it is the timing the travel clip in PEND-9 is bounded by.

**PEND-11 — how an interesting place sequences its `INTO → IDLE → OUTOF` triple.**
The vdata gives weighted activities with the comment *"play that activity until the animation
finishes and then randomly choose another one"*, which cannot be the whole rule — a weighted pick
over `{INTO, IDLE, OUTOF}` would play them out of order. Something orders them.
*Oracle:* the NPC behaviour decompile (same target as PEND-7), or a hub capture with an ambient
NPC in frame.

---

## 6. Divergences this layer will have to record

Not open questions — choices already implied by the design, to be written beside the faithful
behaviour in the owning document once each is made.

- **The player's gait speed is a constant, not the clip's root motion** (PEND-6). Standing today,
  marked at `UElysiumMovementComponent::GetMaxSpeed`.
- **Blend-space interpolation between authored cells is Unreal's**, already recorded in
  `docs/project/animation-roadmap.md`.
- **A held crouch's hold rule** (PEND-4) will be ours if the trace cannot answer it.
- **Jump transition timing** — `leap_ascend`'s authored 0.45 s fade is longer than a tap jump's
  entire ascent, so something has to give, and whichever way it gives is a choice.
