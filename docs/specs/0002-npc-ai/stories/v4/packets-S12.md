# Packets S12 — the last unknowns before V4o, V4c and V4d (2026-10-04)

Settles what the remaining waves — **V4o** [O1, O2, O3] → **V4c** [C1, C2] → **V4d** — still
carried as unknown after V5a (`a6bd4add`), V4a (`c7a2645c`), V4b (`1442fdc2`) and V11 (in
integration). Read-only on source, `Arena/` and the tracker. Written beside this packet: the
briefs of the three waves and two paragraphs of `docs/vtmb/animation_events.md`.

Marks: **(L)** read off the listing / decompile this sitting, verified; **(B)** bytes read from
`vampire.dll`'s `.rdata` (image base `0x10000000`); **(S)** read from the staged clip sidecars
(`$ELYSIUM_WORK_ROOT/import/characters/character/shared/{male,female}/*.clips.json`, the row's
`flags` = `clips[label].slice.clips[label][0][3]`, written by
`pipeline/src/elysium_pipeline/importers/clip_data.py` `descriptor()`; there is no field named
`looping` — the bit is `flags & 1`, the snap bit `flags & 2`); **(P)** the port's source, read;
**(D)** cited from an existing doc or packet; **(I)** inferred, with the reason.
`vampire.dll` throughout. No query passed 10 s (the two sidecar lookups: 2.6 s and 0.1 s).

## a. The `*_attack_layer` clips' loop bit, and a layer's end

### a.1 The data (S)

Bank `move_and_ranged`, male and female alike (13 clips each):

| clips | `flags` | events |
|---|---|---|
| `anaconda_`, `crossbow_`, `enfield_`, `glock_`, `m37_`, `rem700_`, `smith_`, `steyr_`, `supershotgun_`, `submachinegun_`, `deserteagle_`, `throwing_star_attack_layer` (12) | **2** — `STUDIO_SNAP`, **not** `STUDIO_LOOPING` | 3031 at cycle 0.0 (then 5003, some 6002) |
| **`flamet_attack_layer`** | 2 | **none** — no 3031 |
| every `*_dryfire_layer` | 2 | none |
| every `*_reload_layer` | 0 | none below 5000 (6002 on two) |
| `lookback_left_layer` / `lookback_right_layer` (`ACT_LOOKBACK_LEFT` / `_RIGHT`, the `0x47` / `0x48` gestures), 0.6 s | 0 | none |
| `d_thaum_cast_charge_layer` | 0 | 3031 at 0.0 |

`smith_attack_layer`: 15 frames at 30 fps, 0.4667 s, cycle rate 2.143 /s.

So: **no attack layer loops** (S8's guess is confirmed), and **every attack layer is a snap
clip**: `SetLayer 0x10099020` zeroes both blends for `seqdesc.flags & 2` (D, S8 item 3), so the
envelope of `0x10098830` never runs on it — the weight is 1.0 from its first advance to its end,
and **the zero-interval rule (S3 item 6.2) cannot free a fresh attack layer**; the rule bites only
blended layers (`lookback_*`, behind the closed `debug_allow_mf_turn` gate). **The flamethrower's
layer authors no 3031: an NPC running with a flamethrower pushes the layer and fires nothing from
it** (none of the nine tutorial run-and-gun rows holds one, S4 item d).

### a.2 What ends a non-looping layer (L: `0x10098830`, `0x10098bb0`, `0x10098cd0`)

1. `CAnimationLayer::StudioFrameAdvance 0x10098830`, on the advance that carries the cycle to or
   past 1.0: `m_fSequenceFinished (layer+4) = 1`; `GetSequenceFlags(seq) & 1` clear →
   `m_flCycle (layer+0xc) = 1.0` (the literal `0x3f800000`); then the weight: `layer+0x14 = 1.0`,
   and only when a blend is `< 0.95` (`0x1045001c`) the envelope — for a blended layer
   `(1 − 1.0) / blendOut = 0`, so its weight is 0 by the envelope alone.
2. Back in the owner `CBaseAnimatingOverlay::StudioFrameAdvance 0x10098bb0`, same call:
   `layer+4 != 0 && m_bAutoKillWhenFinished (layer+0x28)` → **`m_flWeight (layer+0x14) = 0`** and
   slot 112 `(i, m_nActivity)` (`+0x1c0`; empty on the NPC line, D). Nothing else is written: the
   sequence, the activity, the cycle 1.0 and the cursor stay. **"Freed" is the zero weight and
   nothing else** — `AllocateLayer 0x10099470` and `FindGestureLayer 0x100994c0` (slot 271, behind
   `HasLayer` slot 270 `0x10099540`: `slot 271 != -1`) both read only the weight.
3. `0x10098cd0`, later in the same think and in every think after: **it frees nothing and sets no
   weight.** It zeroes `layer+4` (so the finish flag of step 1 is gone by the end of the think
   that set it), stores `layer+0x2c = cycle + 0.1 × cycleRate × playbackRate`, and offers the
   records in `[previous +0x2c, new +0x2c)`. At rest the cycle is 1.0, so from the second resting
   think on the window is `[1.0 + 0.1r, 1.0 + 0.1r)` — empty — and the wrap clause is off
   (`seqdesc.flags & 1` clear). **A dead non-looping layer dispatches nothing, for ever**, until
   `SetLayer` re-seeds the slot (cycle 0, `+0x2c = 0`, `+4 = 0`).
4. A layer that is finished, non-looping, **not** autokill and snapped (weight 1.0 kept) is
   re-advanced every think: cycle 1.0, `layer+4 = 1` again each advance, zeroed again each
   dispatch. No step-2 pusher makes one (`AddGesture` from `0x102e8560` passes autokill 1).
5. A looping layer never rests at 1.0 (its cycle drops the integer part); freed by autokill it
   rests below 1 and S8's wrap-clause case needs `cycle + 0.1 × rate >= 1.0` at rest — a clip
   shorter than about 0.1 s. Retail's, reproduced by handing all four records every think; no
   shipped layer clip meets it.

`smith_attack_layer` at a 0.1 s think: pushed in `RunTask`, advanced in the same think's `PostRun`
to 0.214, its 3031 fired by that think's dispatch (window `[0, 0.429)`); cycle 1.0, finished and
freed in the fifth `PostRun` (0.5 s after the push). The .38's `Attack_Rate` is 0.8 s, so the
slot is free again before the next push.

## b. `StudioFrameAdvance 0x1008f120` on a one-shot resting at cycle 1.0 (L)

Per call (`0x1008f18c..0x1008f32c`):

- `interval` argument 0 → 0.1 (`0x3dcccccd`); `dt = interval + curtime − m_flAnimTime (+0x174)`;
  **`dt <= 0.001`** (double `0x1044f020` (B)) → **return 0.0, nothing written** (`0x1008f1fc`).
- Else: `m_flPrevAnimTime (+0x170) = m_flAnimTime`; `cycle = cycleRate × m_flPlaybackRate × dt +
  m_flCycle` = above 1.0; `m_flAnimTime += dt`; non-looping (`+0x65d` clear) → **`m_flCycle =
  1.0`** (0.0 only for a negative cycle); **`m_bSequenceFinished (+0x65c) = 1`**.
  **`m_fSequencePastHalf (+0x568)` is not written** on this arm (it is written only while the
  cycle is inside `[0, 1)`, against the double 0.5 at `0x10449270` (B)).
- **`m_flYawSpeed (+0x560)` and `m_flGroundSpeed (+0x654)` are re-written on every real advance**,
  resting or not, from the sequence (`0x1008f2e5`, `0x1008f2fa`): a one-shot that ended keeps
  answering its sequence's ground speed.
- `OnSequenceFinished` (`0x1008f316`, the empty `0x10091c80`) only on the rising edge. At rest
  there is none from this body: the dispatcher `0x10091880` clears `+0x65c` and sets it again in
  the same call (`flEnd = 1.0 + 0.1r >= 1.0`), so the word is 1 at every entry here.
- Returns `dt`.

**The one-shot idle after `RunAnimation 0x1026c540`'s re-pick** (S8's open (I), now (L)):
`RunAnimation` is slot 250 first, then the re-pick (`m_NPCState` not 4 / 7, `m_Activity == 1`,
slot 251) through `0x10260a50` = `m_nSequence = seq; ResetSequenceInfo` — **no cycle write**. So a
finished one-shot `ACT_IDLE` sequence is re-picked **every think** (`SelectHeaviestSequence` for a
non-looping one: the same sequence again), its cycle stays 1.0, `+0x658` is zeroed by the reset,
and the dispatch that follows fires the whole table `[0, 1.0)` and sets `+0x65c` — a rising edge
(the reset cleared it) — every think. The body holds the clip's last frame. Retail's; the port's
`StudioFrameAdvance` (`ElysiumNpcBaseAnim.cpp` :331-390) and re-pick (:320-327) are this, term for
term (P). **C2 must not zero the cycle at the re-pick.**

**For a layer** (`0x10098830`, called only while `weight != 0`, with the base's returned `dt`,
which is 0.0 on the early-out): cycle `+= rate × layerRate × dt`; at or above 1.0 →
`layer+4 = 1`, non-looping → 1.0; weight per a.2. A zero `dt` moves no cycle and still recomputes
the weight (S3 item 6.2, D). The speed words and `OnSequenceFinished` are the base's only.

**Model-less entity**: `0x1008f120` has no `GetModelPtr` test; `GetSequenceCycleRate` answers 10.0
(D, S8 item 4). No NPC, player or camera on a step-2 path is model-less: no lane.

## c. Every "unrecovered" / "not read" sentence left in the remaining briefs

| brief | sentence | answer |
|---|---|---|
| V4o README :351, O3 | "which status [doubles the rate] is unrecovered" | settled by S5 item 5 (D): Presence, discipline id 10; O3's brief already says so. README text is stale, the brief wins |
| C1 item 2, C-integrator step 7 | "each species class's own `HandleAnimEvent` arm by arm — not walked" | **walked and ported** (P): `FElysiumNpcDog`, `Gargoyle`, `Hengeyokai`, `ManBat`, `MingXiao`, `SabbatLeader`, `Tzimisce`, `TzimisceHeadClaw`, `Werewolf` (`ElysiumNpcMisc2Species.cpp` :192-496), `TzimisceRunner` (`ElysiumNpcTzimisceRunner.cpp` :243), `FElysiumNpcCamera` (:390), each from its listing. Not a reading owed; C1's Warning stays the tripwire for a ranged clip with no fire event |
| C2 item 6 | "which port stream stands for retail's one engine stream at the non-NPC pick sites is not ruled" | not a corpus question: retail has **one** stream (`*0x1070b244` slot 2) for every draw; the port split it by subsystem. The listing cannot name a port stream. Kept as the brief has it (the site's existing stream; a hash site proposes one behind a single named function) — an owner's one-line ruling at V4c's close, blocking nothing |
| C2 "Not yours" | "what `0x102b5bb0` is reached from" | **nothing** (L): 17 bytes, `BecomeClientRagdoll(this, vec3_origin 0x1070d1b0, −1, 1)`; no direct caller, in no vtable, no data reference to its address (`vtmb_callers`, `vtmb_globals`). Dead code in the image. The state-7 fork `0x1028a8ec` stays the only NPC reacher of bone −1 |
| D Step 1 item 3 | "whether a ragdoll collides with the player is not recovered" | (L + I) server side it cannot: `BecomeClientRagdoll 0x10090180` sets `FSOLID_NOT_SOLID` on the corpse (ported in V4b: `RetailSolidFlags \|= 4`), the player's movement traces are the server's, and the ragdoll is a client-only physics object, not a server entity. Whether the client pushes its ragdoll with the local player's hull is the client's solver — the half the ruling gives to Unreal. `client.dll` holds no collision-group string to read it from. **Not colliding with pawns is the contract**, no longer "inferred" |
| D "Not in V4d" | "whether any shipped damage source sets bit `0x2000`" | **cannot be answered by a bounded listing query**: the bit is a run-time word built at every damage-packet site and from weapon data. What is read (L): the gib body slot 402 `0x102658f0` (slot 394 `CorpseGib 0x10327a90` false → slot 395, return false; else slot 398, `UTIL_Remove` or the blood particles) is dispatched from `CAI_BaseNPCTroika::RunTask 0x102aacf0` and the discipline hit applier `0x101de660`, and the `& 0x2000` tests sit in `CAI_BaseNPC::Event_Killed` and `CBaseCombatCharacter::OnTakeDamage`. A gibbed NPC is removed, not ragdolled: no V4d record reaches it (the arena's packets are scalar `TakeDamage`, bullets and `DMG_CLUB`). Owner: 0014 |

## d. What the landed waves and packets S8–S11 left open

### d.1 Settled here, because a remaining lane depends on it

1. **`0x10450564`** (B): bytes `00 00 c8 42` — the **float 100.0**. `MeleeSwingUpdate 0x10346cd0`'s
   `N = ceil(dt × 100)` stands as C1 has it.
2. **`CBaseCombatCharacter::UpdateCharacter 0x103246d0`** (L, whole; slot 312 on `CAI_BaseNPC`,
   `CBaseCombatCharacter` **and `CBasePlayer`** — the player has no override; Troika's
   `0x10298070` ends in it): `UpdateDisciplineVisuals`; slot 313 (`+0x4e4`); `UpdateVampHeal_HOT`;
   `UpdateExpressions(interval)` (a player, or an NPC whose slot 513 word has `0x800000`);
   `+0xe68 == 0` → slot 333 (`+0x534`) else `MaintainScriptedEyeDirection(interval)`; a debug
   overlay; slot 314 (`+0x4e8`); **slot 315 (`+0x4ec`) `MeleeSwingUpdate`, no argument**; then the
   two render-fx expiries (`m_nRenderFX 0x1a` / `0x25` after 0.2 s, double `0x10449198`). **The
   player's sweep is therefore in `PostThink 0x1016be10`'s slot 312 call (`0x1016c316`), after
   slot 258 and behind the four gates (S8 item 3), not in a world tick.** The port's base body is
   the counting seam `Think19CombatCharacterUpdateCharacter` (`ElysiumNpcThink.cpp` :122); the
   player's stand-in is the tail of `PostThinkAnimation` (`ElysiumPlayerEntity.cpp` :1344-1348).
3. **`Weapon_FrameUpdate 0x1032aa40`** (L): a valid `m_hActiveWeapon` → the weapon's slot 369
   (`+0x5c4`) `(wielder)`; nothing else. **`CBaseCombatWeapon 0x1024efa0`** (L): the weapon's slot
   250 `(0)`; `m_bSequenceFinished && m_bSequenceLoops` → `SelectWeightedSequence(m_Activity)`,
   not −1 → `m_nSequence`, `ResetSequenceInfo`; then the weapon's slot 258 `(dt, wielder)`. As C1
   item 1 states it.
4. **Slot 389's two arms** (L). NPC / base `0x103338c0`: an active weapon whose
   `GetAttachment01("muzzleflash" 0x105cba6c, &pos, &ang)` answers true → `out = pos`, the
   `origin` argument unread; else `out = origin + forward × gun.y + right × gun.x + up × gun.z`
   over `AngleVectors(slot 221)` and `m_HackedGunPos`. Player `0x10162260`: `out = origin +
   m_vecViewOffset`, nothing else. The listing is whole; what is open is the **port's** accessor
   (no attachment read on a held wield model — the seam at `ElysiumCombatCharacterSlotBodies.cpp`
   :217-221 answers "no attachment") and the player body (unported). O3 calls slot 389 as landed;
   the muzzle seam moves the ray's origin by centimetres on the wield models that author the
   attachment and changes no gate. Owner: 0015 (the held model's attachments).
5. **`MoveNormal 0x102efaa0`'s restore arm** (L, `0x102efc03..0x102efc9c`): after the commit
   `SetActivity(GetMovementActivity())` and the step (navigator slot 15, `+0x3c`), **only when the
   step's result is 0**: if the owner's slot 248 `GetIdealSpeed` **sampled before the commit**
   (`0x102efb44`) is `< 0.01` (double `0x1044e658` (B)) and the body's origin moved less than
   0.01 units from the origin saved before the commit → `m_nSequence (+0x6f0) = the saved
   sequence` (a bare store, **no `ResetSequenceInfo`**) and slot 310 `SetActivity(the saved
   m_Activity)`. Then, result still 0 and `nav+0x51` clear → navigator slot 6 (`+0x18`). So the
   arm undoes the movement-activity commit only for a body that was standing on a zero-speed
   sequence and did not move — never for a body already running. **O2's aim-twin swap
   (`0x102e84a0`, `9 → 0x11`, `0x13 → 0x15`) reaches the kernel through this commit on the next
   move step and is not undone by the arm.** The port names the arm as unported
   (`ElysiumNpcBaseMotor.cpp`, `NavMoveNormalPass`); owner: V13's motor pass, not a V4o lane.
6. **The feed cells answer by activity** (S; S10's open D5 and "which feed clips loop"): every clip
   of `forced_feed` (66), `seductive_feed` (17), `animal_feed` (12) and `zombie_feed` (66) authors
   its own cell activity (`ACT_FEEDING_ENGAGE_ATTACKER_SHORTVICTIM_FRONT`, …), **one sequence per
   cell**; `STUDIO_LOOPING` is set on the `_idle` and `_feed_loop` / `_loop` cells only (engage,
   bite, release and attack-release are one-shots). C2's weighted pick can therefore select a
   grapple cell by number with nothing to draw among, and `SelectGrappleSequence`'s label fallback
   (`ElysiumFeed.cpp` :547-562) can go once the pick walks the body's table.
7. **`LeaveGrappleState`** — settled by S10 item 2 (D): no body writes an animation word. C2's
   `SetDisposition` loses its `IsFeedBusy()` term without consequence there.

### d.2 Open, with the owner — no remaining lane depends on them

| item | from | owner |
|---|---|---|
| the turn script's arrival direction `path+0x64` | V4b's message | V13 (the motor's arrival facing); no V4o / V4c / V4d read of it |
| the crowd corridor's corners | V4b's message | port-side, K1; V13 |
| which wield models author `muzzleflash`; the held-model attachment accessor | d.1 item 4 | 0015 |
| the player's slot 389 body `0x10162260` | V4b's body comment | the player's weapon story (0015); read above, two lines |
| the 18 + 18 physics-chain records' models (2070 / 2071); 4005 | S8 item 2 | 0015 backlog; visual-only |
| a choreographed scene pushing a gesture on the player | S8 item 3 | 0015; no step-2 scene |
| retail's player tick rate (the per-frame burst) | S9 item 1 | the judge; debug count only |
| the port's slow think intervals against retail's ladder | S9 item 2 | the efficiency ladder's story |
| what moved `script_walk_to_mark`'s two timings | S9 item 4 | re-measure after V4b; no lane |
| slots `+0x6a8` / `+0x6ac` (the flyback predicate), S10's D7 / D8 | S10 | the feed story (0006 line) |
| `EndGrapple` callers `0x100db5c0`, `0x10170090`; `OnTakeDamage`'s arm | S10 | the feed story |
| the weapon's `+0x5a4` translate over a feed activity; mode 8's leaf with the player as role 1 | S10 | the feed story |
| which arm raised `0xc` at 80.2 cm; the seat's floor; the draw that moved the first wait; the 0.0625-unit floor swap | S11 items 1.4, 1.5, 2.2, 3.3 | the record's lane / the judge |
| the rest of `0x103246d0` (slots 313, 314, 333, `UpdateVampHeal_HOT`, the expressions, the render-fx expiries) behind the port's counting seam | d.1 item 2 | 0006 (disciplines) and 0015; C1 ports the slot-315 call only and names the rest at the line |

## Changes to the plan

- **O1**: all four records every think; the attack layers are snap clips (weight 1.0, no
  self-free); the dead layer rests at cycle 1.0 with weight 0 and an empty window; the landed
  dispatcher's names. `SequenceSnaps` reads `FElysiumNpcClip::IsSnap()` (`flags & 2`).
- **O2**: `ActiveWeaponBurstPauseWords` is **no longer a seam** (V5a landed it): bursts pause.
  The flamethrower's layer fires nothing — a fact, not a defect.
- **O3**: the stamp per S11 item 2.3; the test assertion to delete; the record's bound 1.64 s.
- **O integrator**: the relative `never` window, written before the one build.
- **C1**: its two slot files (`ElysiumCombatCharacterSlots.cpp`, `ElysiumAnimatingSlots.cpp`) are
  **generated** (P: "Do not hand-edit"): slots 315 and 247 are written in
  `ElysiumCombatCharacterSlotBodies.cpp` / `ElysiumAnimatingSlotBodies.cpp` and the C integrator
  retargets rows `10346cd0` / `10090c80` to `hand:` and regenerates before the build.
  Otherwise unchanged in size; `MeleeSwingUpdate`'s stub going live also wakes the three species
  call sites already in the tree (`ElysiumNpcRunTaskSpecies.cpp` :2117, :2128, :2152).
- **C2**: gains `ElysiumPlayerEntity.cpp` (`PostThinkAnimation` only): the alive gate (S8) and the
  player's slot-315 call at its tail (d.1 item 2). C1 stops the world-tick sweep for every
  wielder; the two lanes stay disjoint by file.
- **D**: no change; one sentence loses its "inferred".
- **Order and size**: unchanged — V4o → V4c → V4d.

## Unrecovered after this sitting

Nothing a remaining lane waits on. Listed with owners in d.2; the two that no listing query can
close are the gib bit's producers (c, last row: an unbounded sweep of damage-packet sites and
weapon data) and the port stream for the non-NPC picks (c: a ruling, not a recovery).
