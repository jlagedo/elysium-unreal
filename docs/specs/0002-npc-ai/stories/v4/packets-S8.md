# Packets S8 — the retail settling read, eighth sitting (2026-10-04)

Read-only on source, `Arena/`, briefs and existing docs. Six unknowns the V4a coders left. Every
claim carries its address. **(L)** = read from the listing this session, verified; **(B)** = bytes
read from `vampire.dll`'s `.rdata` this session (image base `0x10000000`); **(P)** = read in the
port this session; **(doc)** = already in `docs/vtmb/`, cited; **(I)** = inferred, with the reason.
`vampire.dll` throughout. No query in this sitting passed 10 s.

## 1. `OnSequenceFinished` — settled (L): one body, empty, no override anywhere

- **The body** `0x10091c80` (111 bytes; thunk `0x10012bf2`): pushes the scope-trace record
  `"CBaseAnimating::OnSequenceFinish…"` (`0x1054f8a4`), pops it, returns. No field read but
  `m_iName` for the trace, no write, no call.
- **It is not a virtual.** The function is in no class vtable (`vtmb_callers`: "in no class
  vtable"); its single call site is the direct `CALL 0x10012bf2` at `0x10091b9a` inside
  `CBaseAnimating::DispatchAnimEvents 0x10091880`, on the rising edge (`0x10091b85..0x10091b96`:
  `+0x65c` now set, the entry copy at `[ESP+0xf]` clear). So there is **no override on the NPC chain
  (`CAI_BaseNPC`, Troika, any species) and none on the player**: every class runs the same empty
  body. The per-layer body `0x10098cd0` never calls it.
- **What the finish edge does in retail is therefore nothing at the edge.** Every consumer reads
  the byte `m_bSequenceFinished +0x65c` later: an NPC task through slot 251 (doc, `shape.md` § "Which
  finish value a task reads"), `RunAnimation 0x1026c540`'s idle re-pick, the player's commit
  `0x101644f0` (item 6) and `SetAnimation 0x10164240`'s paired-action default arm
  (`cVar2 = m_bSequenceFinished`), the camera's think `0x10071840` (doc).
- **Port (P): already as ported.** No body is the faithful port. `ElysiumNpcBaseAnimEvents.cpp`
  :82-89, `ElysiumPlayerEntity.cpp` :1243-1245 and `ElysiumCameraAnimated.cpp` :316-317 all say so;
  the AI trace's `seqfinished` on the NPC edge is debug output only.

## 2. `CBaseAnimating::HandleAnimEvent 0x10091da0` — settled (L)

Four arms, on `event` (`param_1[0]`), `options` = `param_1[1]`:

| id | arm |
|---|---|
| `0x816` **2070** | `TurnOffPhysicsChain(LookupPhysicsChain(options))`: `m_nPhysicsChainDisableMask (+0x72c) \|= 1 << (index & 0x1f)` (`0x100978c0`) |
| `0x817` **2071** | `TurnOnPhysicsChain(…)`: `+0x72c &= ~(1 << (index & 0x1f))` (`0x100977f0`) |
| `0xfa5` **4005** | `m_nSequence (+0x6f0) = SelectWeightedSequence(atoi(options), -1)`; `ResetSequenceInfo 0x10090950`. **`m_flCycle` is not written** (the SDK's `SetSequence` + reset; item 6's rule applies) |
| anything else | `DevWarning(2, "Unhandled animation event %d for %s\n" 0x1054f8d0, event, GetDebugName())` |

- `LookupPhysicsChain 0x10097980`: `LookupBone(options)`; below 0 → `DevWarning("physics chain at
  bone …not f…" 0x10550690)` and `-1`; else the index of the model's chain record whose first word
  is that bone (`studiohdr +0x18c` count, `+0x190` offset, 28-byte stride), or `-1`. A `-1` index
  still reaches the mask write as `1 << 31` (the `& 0x1f`) — retail's, not guarded.
- **No arm claims by return value**: the body is `void`. "Claimed" is the port's census notion.
- **Who reaches the body.** Slot 259 holds `0x10091da0` itself on 51 classes (`vtmb_slot 259`):
  `CBaseAnimating`, `CBaseAnimatingOverlay`, `CBaseFlex`, `CBaseViewModel`, every prop
  (`CBaseProp`, `CDynamicProp`, `CPhysicsProp`, `CBreakableProp`, `CProp*`, `CRagdollProp*`,
  `CActivityCopyProp`, `COrnamentProp`, `CWindowPane`), `CCorpse`, `CGib`, `CSecCamera`,
  `CBaseTerminal`, the projectiles. **None of those is ever dispatched to**: slot 258 has five call
  sites (doc, `animation_events.md` § "Who calls the dispatcher") — NPC, player ×2, the NPC's weapon
  (handler = the wielder), the animated camera — and no prop think calls it. The body is reached
  only by **fall-through**:
  - `CBaseCombatCharacter::HandleAnimEvent 0x1032e330`'s `default:` (L) — every NPC and the player,
    for an id that is not 3000..3999, not from another source, and not 4006 / 4007 / 4020 /
    4100..4102. So an NPC's or the player's own 2070 / 2071 / 4005 lands here.
  - `CCameraAnimated::HandleAnimEvent 0x10071900` (L) for any id but 1003.
- **Correction — the animated camera has its own slot 259.** `0x10071900`: `event == 0x3eb`
  (**1003**) → `n = atoi(options)`; `1 <= n <= 8` → `FireOutput(this + 0x730 + (n-1)*0x18,
  activator this, caller this, delay 0)` (`0x10010794`), the eight `OnScriptEvent01..08` outputs;
  out of range → nothing; every other id → `0x10091da0`. (doc, `animation_and_movers.md` :2218 has
  the row; the port comment at `ElysiumCameraAnimated.cpp` :315-316 names the base body instead.)
- **Shipped data** (doc, `animation_events.md` :328, :372): 2070 / 2071 have 18 records each
  (options are bone names); **4005 has none**. Which models carry the 18 was not read this sitting.
- **Port (P).** No arm of `0x10091da0` exists (`Grep` 2070 / 2071 / 4005 / `PhysicsChain` in
  `Source/ElysiumUE`: comments and the datamap name only); the three ids fall to the census. The
  camera has no `HandleAnimEvent` override, so a camera clip's 1003 reaches `FElysiumEntity::
  HandleAnimEvent` (false) and no `OnScriptEventNN` fires from the dispatcher.

## 3. `CBasePlayer::PostThink 0x1016be10` — gates and the player's layers (L)

**Order, whole, around the animation step:**

1. Unconditional head: `m_flHolyLightEndTime` expiry; `CalcAbsoluteVelocity` when `m_iEFlags` bit
   12; `m_vecSmoothedVelocity = 0.9 × old + 0.1 × velocity` (`0x10450a9c` = 0.9, `0x104491b4` = 0.1).
2. **The four gates, in this order, each jumping to the tail `LAB_1016c41d`** (`0x1016bede..
   0x1016bf17`):
   1. the byte `DAT_1070ba2c != 0` (`g_fGameOver` (I): written by `CMultiplayRules` slots 79 / 80,
      cleared by `CWorld::Precache`);
   2. `m_iPlayerLocked (+0x2304) != 0` — **no writer sets it** in the image (doc,
      `camera-view-modes.md` :3123);
   3. slot 158 `IsAlive` (`0x100b4dc0`: `m_lifeState == 0`) **false**;
   4. slot 406 (`0x1015ee60`: the byte `+0x19f6`, the doc's `m_bIsObserver`) **true** — the field
      ledger has no typed writer.
   **There is no frozen gate and no cinematic gate**: `FL_FROZEN`, `FL_ATCONTROLS`,
   `m_bIsImmobilized (+0x19f7)`, a cine camera and a controller NPC are not tested. The only flags
   read in the live body are `FL_DUCKING` (2, the hull height) and `FL_ONGROUND` (1, the fall
   reset). In shipped play the one gate that fires is **not alive**.
3. Live body: the wolf-form arm (`m_Local.m_bWolf +0x1edc`; its beast-form swap runs its own
   slot 250 → slot 258 → slot 312 → `0x1016b480` and **returns** — no `0x101600a0`, no tail);
   `SetCollisionBounds`; slot 458 `ItemPostFrame` (`0x1016c211`); the on-ground fall reset;
   **`IsAlive` again** (`0x1016c24d`) gating only the melee freeze (`m_IdealActivity +0xff0 ==
   0x4b`, slot 412 false, `m_nButtons & 0x79a == 0` → both velocities zeroed), the classifier
   `0x1016bb50` and slot 449 `SetAnimation(code, 0)` (`0x1016c2a5`).
4. **The animation step, ungated inside the live body** (`0x1016c2ab..0x1016c31e`):
   `m_nSequence == -1 → 0`; slot 250 `StudioFrameAdvance(0)` (`0x1016c2bf`); `0x101600a0(interval ×
   m_flSpeedScale +0x1488)`; **slot 258 `(interval, this)`** (`0x1016c2e5`); `+0x1db4 == 0xb` →
   `0x1017df50(this, 3, -1.0, "Jump_LandedExtremelyHard")`; **slot 312 `UpdateCharacter(interval)`**
   (`0x1016c316`); `UpdatePlayerSound` (`0x10010578`).
5. Tail (always): simulated entities, the keyring countdown, hunger, the status reaction, **the
   controller copy** (`m_hControllerNPC +0x1db0`: `m_nSequence`, `m_flAnimTime`, `m_flCycle`,
   `m_flPlaybackRate`, all four layers and `m_Flinch[]` copied from the stand-in, **with no
   `ResetSequenceInfo`** — `m_flLastEventCheck` is not touched), the delayed callback, the
   on-head condition.

`CBasePlayer::HandleAnimEvent 0x10178a10` (L) repeats gate 4 and adds the source test: `slot 406
false && pSource == this`, else the record is dropped whole.

**Who pushes a layer on the player.** Slot 268 `SetLayer 0x10099020` (L) writes one record: sequence
`+8`, cycle `+0xc = 0`, playback rate `+0x10 = 1.0`, weight `+0x14 = 0.1`, `+0x18 = 1.0`, blend in /
out `+0x1c` / `+0x20 = 0.2` (both 0 for a `seqdesc.flags & 2` clip), activity `+0x24`, autokill
`+0x28`, **`m_fSequenceFinished +4 = 0` and `m_flLastEventCheck +0x2c = 0`**. On a player `this` it
is called with a **literal index 0** and nowhere else:

- `0x1015fbb0(activity, force)` — the weapon-activity commit (doc, `player-entity.md` § "The weapon
  activity queue"): `SetLayer(0, activity, SelectWeightedSequence(activity), autokill 1)`, then
  `layer0.m_flPlaybackRate (+0x744) = m_flSpeedScale`. Callers (L): the player commit `0x101644f0`'s
  second argument (`0` → slot 269 clears layer 0; `-1` → untouched; else translate and commit),
  `0x103ee870`, and `CWeaponThrown_Grenade_Frag` slot 265 `0x103ee990`.
- `0x1015fd80(seq)` — `SetLayer(0, -1, seq, autokill 1)` when layer 0's sequence differs.

**Layers 1..3 of the player have no pusher on step-2 paths.** `AddGesture 0x100991b0` /
`AllocateLayer` (slot 272) is the only route to a non-zero index, and its callers are the NPC ones
(doc: `0x102e8560`, TASK `0xe4`, slot 273, `0x10397930`, the discipline applier) plus the scene
entity's `AddGestureSequence`. *(I)* a choreographed scene with the player as an actor could reach
slot 272 on the player; no step-2 record runs one, and that path was not walked. The other route is
the tail's controller copy, which overwrites all four records wholesale during a controller scene
(off step 2). So the firearm's 3031 on `*_attack_layer` is on **layer 0**, started at cycle 0 with
its window at 0 by `SetLayer`: the shot leaves in the same `PostThink` that committed it (slot 449
precedes slot 258), 3031 being authored at cycle 0.0 (doc).

**Port (P).** `PostThinkAnimation` (`ElysiumPlayerEntity.cpp` :1151) carries **no gate** and says
so (:1158-1160); it dispatches Base + UpperBody (= layer 0); layers 1..3 are a seam answering "no
layer" (`ElysiumPlayer.h` :2145-2148) — correct for step 2 by the paragraph above.

## 4. The null arms of `0x10091880` / `0x10098c80` — settled (L)

- **No model** (`GetModelPtr(-1) 0x10013d8b == 0`, `0x100918ff..0x10091906` → `0x10091b9f`): the
  base body returns having written **nothing** — `m_bSequenceFinished` is not cleared,
  `m_flLastEventCheck` not moved, past-half untouched, no event, no `OnSequenceFinished`. The entry
  read of `+0x65c` is discarded.
- **A model but no sequence descriptor** (`GetSeqDesc 0x1000b4f6 == 0`: the sequence is outside both
  the model's and its include's tables): `+0x65c` is cleared; `flEnd = cycleRate × playbackRate ×
  0.1 + m_flCycle`, where `SequenceDuration 0x10091080` answers **0.1** for an out-of-range sequence
  (`FLD [0x104491b4]` at `0x10091196`, after a `DevWarning` when the entity has a model name) so
  `GetSequenceCycleRate 0x10091230` answers `1 / 0.1 = 10` (its `<= 0` arm loads `0x1044e664` =
  **10.0** (B)) and the look-ahead is one whole cycle. Then by `m_bSequenceLoops +0x65d`:
  - **clear** (`0x100919fa..0x10091a0c`): the `seqdesc != 0` test skips the finish and past-half
    block whole — the entity is **never finished by the dispatcher**, `flEnd` is not clamped;
  - **set** (`0x10091968..0x100919f8`): finish / past-half and the start wrap run as usual (no
    descriptor test on this arm).
  `m_flLastEventCheck = flEnd` is stored either way (`0x10091a6c`); the event loop is skipped
  (`0x10091a72`); the rising-edge test still runs (`0x10091b85`), so the looping arm can still call
  the (empty) `OnSequenceFinished`.
- **The layer body `0x10098cd0`** has **no `GetModelPtr` test** (`0x10098cd0..0x10098d25`): for each
  of the four records it always zeroes `layer+4`, always stores `layer+0x2c = layer cycle + 0.1 ×
  GetSequenceCycleRate(owner, layer seq) × layer rate`, and only then tests the descriptor (null →
  no events). So `0x10098c80` on a model-less overlay entity leaves the base words alone and still
  rewrites the four layer cursors (with the 10.0 rate).
- **A dead layer keeps dispatching (I, from the listing's wrap clause).** A freed slot's cycle no
  longer moves, so its window is `[end, end)` — empty — **unless** its descriptor has
  `flags & 1` and `end >= 1.0`: the wrap clause `cycle < end − 1.0` then fires every record in
  `[0, 0.1 × rate)` on **every** dispatch. A finished autokill layer rests at cycle 1.0. Shipped
  `*_attack_layer` clips author 3031 at 0.0, so they must be non-looping or retail would fire each
  think; the loop bit of those clips was not read this sitting.
- **Port (P).** `DispatchBase` (`ElysiumAnimEvents.cpp` :98-146) reproduces the descriptor arm
  exactly (`bHasDescriptor`); "no model" is the caller's precondition (:91-92). NPC: taken as always
  true (`ElysiumNpcBaseAnimEvents.cpp` :45-47). Player: no body → nothing dispatched and the words
  kept (:1163-1165), matching; a channel with **no phase** resets its words (:1183-1186) — a seam
  rule with no retail counterpart (retail's player always has a sequence: `-1 → 0`). Camera: a body
  publishing no phase still advances the window and writes the finish (:298-300), which is the
  descriptor-less looping arm's shape, not the no-model arm's; harmless on shipped cameras.

## 5. The four constants — read (B), with their use sites in `0x10091880` (L)

| address | bytes | type at the site | value | use |
|---|---|---|---|---|
| `0x104454c0` | `00 00 80 3f` | `float` | **1.0** | finish test `flEnd >= 1.0` (`0x1009196c`, `0x10091a12`); the wrap clause `flEnd >= 1.0` and `flEnd − 1.0` (`0x10091acd`, `0x10091ae4`) |
| `0x1044fab0` | `00 × 8` | `double` | **0.0** | finish test `flEnd < 0.0` (`0x1009197f`, `0x10091a25`); the start wrap `flStart < 0.0` (`0x100919dd`) |
| `0x104454d0` | `00 00 00 3f` | `float` | **0.5** | past-half: `flEnd <= 0.5 → +0x568 = 0`, else `1` (`0x10091990`, `0x10091a36`) |
| `0x10449280` | `00 00 00 00 00 00 f0 3f` | `double` | **1.0** | the looping arm's start wrap: `flStart >= 1.0 → −= 1.0` (`0x100919bc`, `0x100919cf`), `flStart < 0.0 → += 1.0` (`0x100919ee`) |

Also (B): `0x104491b4` = `float` **0.1** (the look-ahead, `0x10091943`; the same constant in
`0x10098cd0` at `0x10098d06`). The non-looping finish writes the literal `0x3f800000` (1.0) into
`flEnd` (`0x10091a5e`). All four agree with what A1 took; `DispatchBase`'s `1.f` / `0.f` / `0.5f`
and `GEventLookAheadSeconds = 0.1f` are right, and the comparisons' strictness matches
(`>=`, `<`, `<=`).

## 6. A sequence change with a non-zero cycle — settled (L): the window opens at 0, always

- **`ResetSequenceInfo 0x10090950` never reads or writes `m_flCycle (+0x6f8)`.** It writes: `-1 → 0`
  on `m_nSequence`; the byte `+0x5ac = 0`; yaw speed `+0x560`; ground speed `+0x654`;
  `m_bSequenceLoops +0x65d = seqdesc.flags & 1`; `m_flPlaybackRate +0x6f4 = 1.0`;
  `m_bSequenceFinished +0x65c = 0`; **`m_flLastEventCheck +0x658 = 0`**; `+0x19c |= +0x5b0 | 0x300`,
  `+0x5b0 = 0`; `ResetClientsideFrame` when `+0x70c`; `0x101cd940(handle +0x5a8)`; and, when
  `+0x650 != m_nSequence`, slot 247 `(m_nSequence)` then `+0x650 = m_nSequence`.
- **The dispatcher has no notion of where the cycle started.** The first window after a reset is
  `[0, m_flCycle + 0.1 × rate)` (non-looping: capped at 1.0), so **every record between 0 and the
  current cycle fires on the first dispatch after the change** — in table order, in one call. There
  is no "anchor" at the entry cycle anywhere in `0x10091880`. Then the window chains normally.
- **Who keeps a non-zero cycle across a reset** (so the catch-up really happens):
  - **The player's commit `0x101644f0`** (`0x101644f9..0x10164517`, `0x10164603..0x10164628`): on a
    sequence change, or the same sequence with `m_bSequenceFinished` set, it writes `m_nSequence`,
    zeroes `m_flCycle` **unless the requested activity is one of `9`, `0x12`, `0x13`, `0x16`, `0x17`**
    (`ACT_WALK`, 18, `ACT_RUN`, `ACT_WALK_RELAXED`, `ACT_RUN_RELAXED`; 18's name not looked up),
    calls `ResetSequenceInfo`, then `m_flPlaybackRate = m_flSpeedScale`. For those five the cycle is
    **carried** and the window still restarts at 0: a walk → run change at cycle 0.6 fires the run
    clip's records in `[0, 0.6 + adv + 0.1 × rate)` in that same `PostThink` (slot 449 → slot 250 →
    slot 258). On the player these are 2050..2053, which `0x10178a10` swallows (doc), so nothing is
    observable; any other record authored on a locomotion clip would fire.
  - **NPC `SetActivityAndSequence 0x10272490`**: cycle zeroed unless the same looping sequence, or
    old and new activity both in `{9, 0x13}` (doc, `shape.md` :5187; re-read (L)). Same catch-up; an
    NPC's 2050..2053 are **not** swallowed, so a walk ↔ run change fires the new clip's earlier
    footfalls at once.
  - **The bare commit `0x10260a50`** (`m_nSequence = seq; ResetSequenceInfo`, no cycle write):
    `RunAnimation 0x1026c540`'s idle re-pick, `SetModel`, `SetGrappleActivity 0x1032a100`,
    `CAI_BaseNPC::HandleAnimEvent 0x10274e30`, prop inputs. A **one-shot idle that ended sits at
    cycle 1.0**; the re-pick keeps it, so the next dispatch's window is `[0, 1.0)` — the whole new
    clip's records at once — and `+0x65c` is set again in that same dispatch (I: unless the next
    `StudioFrameAdvance` moved the cycle first; `0x1008f120` clamps a non-looping cycle at 1.0 and
    was not re-read here).
  - `0x10091da0`'s 4005 (item 2; no shipped record); `CPayphone::NPCThink` (cycle = the partner's,
    every pass; doc).
  - **Zeroed, so no catch-up:** both `ForcePreTranslatedSequenceAndActivity` bodies (`0x103250d0`:
    commit, `m_flCycle = 0`, a second `ResetSequenceInfo`, rate = speed scale; `0x10272400`: commit,
    `m_flCycle = 0`, `m_flPrevAnimTime = 0`), the non-locomotion player commit, the camera
    (`0x10071770`, doc), the scripted sequences (doc).
  - **No reset at all:** the controller copy (item 3) and `0x101618e0` write sequence and cycle and
    leave `+0x658` stale; the next live dispatch runs the new sequence from the old cursor.
- **Port (P).** NPC: `ResetSequenceInfo` zeroes `LastEventCheck` (`ElysiumNpcBaseAnim.cpp` :118) and
  the carry rule is ported (`ElysiumNpcBaseAnim10.cpp` :90-100) — already as retail. Player:
  `PostThinkAnimation` re-arms the channel on a changed clip or play id with `LastEventCheck = 0`
  and takes `Cycle` from the pose phase (:1192-1214), so a clip first seen mid-cycle dispatches
  `[0, phase + 0.1 × rate)` — **retail's rule, as long as no anchor is added**.

## Changes to the plan

1. **`OnSequenceFinished`** — **none; already as ported.** No lane writes a body. Any brief line
   that reads "the NPC's / the player's `OnSequenceFinished` override" is void: there is none.
2. **`HandleAnimEvent 0x10091da0`** —
   - **V4b integrator** (the camera file is A4's, closed with V4a): add
     `FElysiumCameraAnimated::HandleAnimEvent` = `0x10071900`: id 1003, `n = atoi(options)`,
     `1..8` → fire `OnScriptEvent0<n>` with activator and caller the camera, delay 0, and answer
     claimed; out of range → claimed, nothing fired; any other id → the base. Correct the comment at
     `ElysiumCameraAnimated.cpp` :315-316 (slot 259 is `0x10071900`, not `0x10091da0`).
   - **V4o O1**: none. 2070 / 2071 write only `m_nPhysicsChainDisableMask +0x72c` (a rendering
     word; visual-only half) and 4005 has no shipped record. File both in 0015's backlog; no step-2
     lane implements them. **V4c C1**: do not use 4005 as an attack producer.
3. **`PostThink` gates / player layers** —
   - **V4c C2** (it owns the death transaction): gate `PostThinkAnimation`'s advance + slot 258 +
     slot 312 stand-in on the player being alive (`m_lifeState == 0`, retail gate 3,
     `0x1016befc`), ahead of the body test; a dying or dead player dispatches nothing and keeps its
     words. Leave game-over, `m_iPlayerLocked` and `+0x19f6` as named seams answering "open" (no
     writer in the image). Add **no** frozen / immobilized / cinematic gate.
   - **V4o O1**: the player's layers 1..3 stay "no layer"; the attack layer is layer 0, started by
     `SetLayer` at cycle 0 and window 0. **V4o O3**: the player's 3031 leaves in the `PostThink`
     that commits the layer; keep the UpperBody channel re-arming on a new play id.
4. **Null arms** —
   - **V4o O1**: `OverlayLayerWords` must hand `DispatchLayer` **all four** records every think,
     in-use or not (no weight, no "in use", no model test in `0x10098cd0`), each with its own
     descriptor loop bit from the bridge row — do not assume an attack layer is non-looping, and do
     not skip a freed slot (its wrap clause is retail's).
   - Everyone else: **none**. `DispatchBase` already matches; the player's "no phase" reset and the
     camera's phase-less advance are host seams, to stay as they are.
5. **Constants** — **none; already as ported** (1.0, 0.0, 0.5, 1.0; look-ahead 0.1). A1's values
   stand as re-read.
6. **Sequence change at a non-zero cycle** —
   - **V4a integrator / V4b integrator**: add **no** anchor to the player seam; keep
     `LastEventCheck = 0` on a re-armed channel with `Cycle` = the pose phase. State in the comment
     that the catch-up window `[0, phase + 0.1 × rate)` is retail's for the five locomotion
     activities (`0x101644f0`) and that every other player commit starts at cycle 0.
   - **V4b B1 / B2**: a gait change through `0x10272490` carries the cycle and re-fires the new
     clip's earlier 2050..2053 at once; a record that counts footfalls or hearing stimuli across a
     walk ↔ run change must expect them. Do not "fix" it.
   - **V11-1 / V11-2 / V11-3, V4o O2, V4c C1, V4d, V5a-3**: none.

**Unrecovered after this sitting:** the loop bit of the shipped `*_attack_layer` clips and which
models carry the 18 + 18 physics-chain records (data, not listing); `StudioFrameAdvance 0x1008f120`
on a model-less entity and on a one-shot idle resting at 1.0 after the bare re-pick; whether a
choreographed scene ever pushes a gesture on the player.
