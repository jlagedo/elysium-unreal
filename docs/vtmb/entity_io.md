# Entity I/O and the interaction layer

VtMB drives its world with Source's entity I/O: an entity fires an **output**
(`OnPressed`, `OnStartTouch`, …) which calls a named **input** (`Open`, `Trigger`,
`ScriptUnhide`, …) on a target entity. `research/tooling/probes/ent_survey.py` reports every table
below from the shipped maps.

Scale across the **engine-loaded map set** — the 108 maps the runtime resolves
patch-first (`Unofficial_Patch/maps`; `uv run elysium research ent_survey --patch [--map <name>]`):
**71,096 entities**, **326 classnames**, **24,081 outputs**, **6,956 with a Python
payload**. Every table below is based on this set. The retail set (`Vampire/maps`, 101
maps, the tool's default) is **63,861 / 299 / 16,125 / 1,591** — carried through as a
labelled comparison, since the patch's maps carry ~50% more I/O.

Of the 6,956 Python-carrying outputs, **6,851 fire only Python** (no I/O target) and
**105 do both** (retail: 1,500 / 91).

> **Why the patch set is the baseline.** The runtime resolves every map patch-first
> (`install.map_path`), so the shipped-runtime shape is the patch's, not retail's. The
> patch's `maps/` is a strict superset of retail: all 101 retail names (shadowed by
> heavier patched versions) **plus 7 patch-only maps** (`hw_chateau_1`, `hw_warrens_2b`,
> `la_bradbury_1`, `la_library_1`, `la_malkavian_3b`, `sm_coffee_1`, `sm_smoke_1`), so
> globbing `Unofficial_Patch/maps` (108 maps) *is* the engine-loaded set. `ent_survey.py`
> defaults to the retail set (the labelled comparison); `--patch` surveys the
> engine-loaded set. (Resolving just the 101 retail *names* patch-first — excluding the 7
> new maps — gives **68,430 / 324 / 23,357 / 6,591**, a subset, not the full runtime set.)

## Output format

Outputs are a comma-joined value on a key beginning `On`/`Out`:

```
target , input , param , delay , times , python , extra
   0       1       2       3       4       5        6
```

**Seven fields, not Source's five.** 24,065 of 24,081 outputs write 7; 14 write 6
(one writes 5, one 8). (Retail: 16,096 of 16,125 write 7, 29 write 6.)
Fields 0-4 are stock Source (`times` = -1 means unlimited). **Field 5 is a Python
call string** (6,956 outputs carry one) which is wrapped as `__main__.%s` — see
`docs/vtmb/python_bridge.md`. The wrap format string lives in **`vampire.dll`**
(`0x1055e370`, in the event-queue dispatch region), not `engine.dll`. A target of `!activator`/`!self` is a runtime
reference, not a `targetname`.

## Base inputs

`Kill`, `ScriptHide` and `ScriptUnhide` are received by nearly every classname.
They live on the base entity and reach subclasses through the datamap `baseMap`
chain (`docs/vtmb/python_bridge.md`), so the port implements them once, not per class.

## Entity-name matching

`CGlobalEntityList::FindEntityByName` (`vampire.dll` `FUN_100f7770`) uses one small matching rule
for I/O targets and the Python `FindEntityByName`/`FindEntitiesByName` helpers:

- comparison is case-insensitive;
- an empty search matches nothing, and a nameless entity is never a candidate;
- a `*` is special only when it is the final character, where it makes the preceding text a prefix;
- a bare `*` matches every named entity; `*` anywhere else is a literal, and `?` has no special
  meaning.

The body tests the final character and calls `_strnicmp(targetname, pattern, len-1)` for the prefix
case, otherwise `_stricmp`. A leading `!` takes a separate single-result path for `!player`,
`!playercontroller`, `!pvsplayer`, `!activator`, `!picker`, and `!caller`. This rule is what makes
the patch's `FindEntitiesByName("plus_*")` / `FindEntitiesByName("basic_*")` switches operate over
the hundreds of hidden variant entities rather than silently finding none.

### `!playercontroller` — the cinematic relationship entity

`events_player.CreateControllerNPC` creates one map-epoch `npc_VPlayerController`, not a boolean
latch. Repeated creation returns the same entity. It copies the player's model, origin, angles, skin,
and available embodied character state, builds through the ordinary NPC skeletal path, and remains
non-AI and non-solid. `!playercontroller` resolves that relationship directly, which is why the
shipped choreographies and scripted sequences can bind it as an ordinary animating actor.

`events_player.RemoveControllerNPC` first transfers the controller's final model, transform, skin,
and applicable character state back to the player, then destroys it and clears the alias. The
relationship handle is map-snapshot state: restoring a scene in progress rebinds
`!playercontroller` to the restored runtime entity before event processing resumes.

The opening map also has one deliberately dangling authored wire:
`walk_out_cam_k.OnReachedKeyframe → controls.Deactivate`, paired with a `trigger_once` sending
`controls.Activate`. `sp_theatre` contains no target named `controls` and no `game_ui` entity. The
rebuild therefore leaves both as non-fatal missing-target diagnostics; it does not invent a receiver.

## `trigger_changelevel` forced input

The forced-transition input is named **`ChangeNow`**. The exported corpus carries 88 such wires
across 19 maps and zero wires named `ChangeLevel`; genesis's
`firetrans.OnStartTouch → boogieout,ChangeNow` is one of them. `ChangeLevel` is not a shipped map
input name and exists in the rebuild only as a compatibility alias for earlier internal callers.

## Hidden state: StartHidden / ScriptHide / ScriptUnhide

**`ScriptHide` is not "invisible" — it is a whole-entity OFF switch.** It makes the
entity non-solid, non-thinking and undrawn; `ScriptUnhide` restores it. This is
VtMB's own system and has no equivalent in Source SDK 2013.

- `StartHidden` `1` — the entity spawns OFF. A keyvalue on any entity, not just
  brush entities (NPCs, props and `func_brush` all use it).
- `ScriptUnhide` (1,333 wires) / `ScriptHide` (626) — turn on / off at runtime.

Verified in `Vampire/dlls/vampire.dll` (image base `0x10000000`). The datamap is
built at runtime by `FUN_100a22f0`, which writes 44-byte `typedescription_t`
records into `.data`; both inputs resolve through incremental-link JMP thunks:

| | |
| --- | --- |
| `InputScriptHide` | datamap rec `0x10553d24`, `inputFunc` @+0x1C = `0x1001488f` → thunk → `0x100a8bf0` |
| `InputScriptUnhide` | datamap rec `0x10553d50`, `inputFunc` @+0x1C = `0x10008bde` → thunk → `0x100a8c90` |

Both are thin: they push a call-trace entry and make one virtual call.
`CBaseEntity`'s vftable is at `0x10450584` (via MSVC RTTI):

| vftable slot | → | implementation |
| --- | --- | --- |
| **+0x134** | `0x100011cc` → thunk | `CBaseEntity::ScriptHide` @ `0x100a8710` |
| **+0x138** | `0x1000468d` → thunk | `CBaseEntity::ScriptUnhide` @ `0x100a8990` |

`CBaseEntity::ScriptHide` (trace string `"CBaseEntity::ScriptHide"` @ `0x105567dc`):

1. early-outs if the hidden flag `byte [this+0xf4]` is already set;
2. saves prior state — `[this+0xe4] = [this+0x118]`, `[this+0xec] = [this+0x35c]`,
   `[this+0xf0] = [this+0x360]`, `[this+0xf8] = [this+0x19c]`, and
   **`word [this+0xf6]` = the current solid type** (from the vtable +0x34c getter);
3. sets `[this+0x17c] = 0x7f7fffff` (FLT_MAX) — next think never;
4. **`CBaseEntity::SetSolid(0)`** — `SOLID_NONE`;
5. **`CBaseEntity::SetSolidFlags(4)`** — `FSOLID_NOT_SOLID`.

`CBaseEntity::ScriptUnhide` is the exact inverse: it reads the saved solid type
back out of `word [this+0xf6]`, restores it through `SetSolid`/`SetSolidFlags`, and
clears the hidden flag with `byte [this+0xf4] = 0`. The collision object lives at
`this+0x270` (both setters are called with `ECX = this+0x270`).

**Consequences that matter for the port:**

- A `StartHidden` entity is **not solid**, so it does not block the player and the
  `+use` trace (which runs against `MASK_SOLID | CONTENTS_DEBRIS |
  CONTENTS_PLAYERCLIP`) cannot hit it. It is inert, not merely unseen.
- This diverges from Source SDK 2013, where `CBaseButton::Spawn()` is
  `SetSolid(SOLID_BSP)` with no non-solid path, and where visibility and solidity
  are orthogonal (`CFuncBrush::TurnOff` must set `FSOLID_NOT_SOLID` *and*
  `EF_NODRAW` separately). Do not reason about VtMB from SDK 2013 here.
- So a hidden `func_button` is a *disarmed* interaction point. The level script
  `ScriptUnhide`s it when the interaction becomes available — which is why
  `ScriptUnhide` is the third most-wired input in the game.
- `StartHidden` is not `rendermode`, not `renderamt`, and not a `SURF_NODRAW`
  surface flag. Hidden entities carry ordinary render values (`renderamt 255`,
  `rendermode 0`) and ordinary surface flags.

**The exporter and runtime honour it.** `UE_bsp_to_scene.py` removes every renderable
brush entity from the static world and writes it as a local-space `brush_mesh`, including
`StartHidden 1` models. The runtime attaches that mesh to the entity's collision body and
gates both through the same dormancy switch. Thus the hidden geometry exists for a later
`ScriptUnhide`, but cannot leak out of the static level:

- Six `func_button` volumes in `sp_tutorial_1` skinned `DEBUG/DEBUGEMPTY` (a 32×32
  half-red/half-blue placeholder) — no longer rendered as red/blue rectangles by the
  spawn door.
- `func_brush` `chopwndwbroken` (131 faces), the *broken* state of a window revealed by
  script when it breaks — no longer drawn on top of the intact one.

## The `<map>.ents` sidecar

The complete JSON contract is `docs/project/rebuild-strategy.md` → "Sidecar contracts." Entity I/O relies
on three properties: entities are exported unfiltered; `outputs[]` preserves all seven fields;
and brush `hulls` and `brush_mesh` vertices are entity-local Unreal-centimetre geometry whose
world transform is the entity origin. `elevator_floors` is the fixed eight-entry absolute-Z
table already converted to Unreal centimetres. `start_hidden` gates collision, visibility and
monitoring, while `keys` retains every raw keyvalue needed by a class handler. This doc owns
the behavior of those fields, not a second copy of their wire schema.

## The usable set

Only these classnames carry `use_icon`/`locked_icon` — i.e. only these are things
the player can look at and use:

| classname | wires | typical use_icon / locked_icon |
| --- | --- | --- |
| `func_door_rotating` | 2470 | 10 portal / 3 intrusion (also 59 door_transition, 73 valve) |
| `prop_doorknob` | 1434 | 10 portal / 3 intrusion |
| `func_door` | 428 | 10 portal / 3 intrusion (rare 6 lootable) |
| `prop_switch` | 334 | 12 switchable, 13 sewer / 12, 13 |
| `func_button` | 324 | 12 switchable, 3 intrusion / 12 |
| `prop_button` | 156 | 31 button_up, 32 button_down / 30 button_locked |
| `prop_doorknob_electronic` | 110 | 53 electroniclock / 54 electroniclocked |
| `prop_sign` | 100 | 18 note, 41 printedpapers, 56 bustopmap |
| `item_container_animated` | 78 | 6 lootable |
| `prop_doorknob-wesp` | 40 | 10 portal / 3 intrusion |
| `item_container_lock` | 24 | 3 intrusion / 54 electroniclocked |
| `prop_dynamic` | 6 | 10 portal / 3 intrusion |
| `prop_padlock` | 5 | 3 intrusion |
| `item_container_one_item_filtered` | 4 | 61 pedestal |
| `trigger_multiple` | 3 | 73 valve |
| `func_monitor` | 1 | 73 valve |
| `item_container` | 1 | 6 lootable |

`use_icon` is the cursor when usable; `locked_icon` when locked. `3`/`intrusion` is
a padlock — it names the Intrusion (lockpicking) skill the player would need.
`prop_doorknob-wesp` is a patch-only variant (the `-wesp` suffix is the Unofficial
Patch author); it behaves as `prop_doorknob`. `func_door_rotating`, `func_monitor` and
a few `trigger_multiple` in the patch pick up `73 valve`, absent from retail.

`func_button` emits three outputs: **`OnPressed`** (168), **`OnIn`** (167) and
**`OnOut`** (154) — `OnIn`/`OnOut` fire as the look-cursor enters and leaves the
volume, which is what arms the icon. `soundgroup` names the sound set — most
`func_button`s use `small_metal_switch` (127), the rest a mix (`tv`, `manhole_cover`,
`Wall Light Switch`, `standard_door`, `large_metal_lever`).

`func_button` spawnflags seen: 1056, 1057, 8193, 1025, 9217, 256, 1024, 3073, 1280,
8225, 9249, 33, 9729.
`m_spawnflags` is a `FIELD_INTEGER` at entity offset **`0x204`** (confirmed in the
`CBaseEntity` datamap builder `FUN_100a22f0`, `vampire.dll`). `CBaseButton::Spawn`
(`FUN_100c8d60`) reads it and wires up the button:

| Bit | Behaviour |
| --- | --- |
| `0x1` | **DONTMOVE** — pressed position is forced equal to the start position (matches stock `SF_BUTTON_DONTMOVE`) |
| `0x20` | **TOGGLE** — read by both activation handlers + the direction helper `0x100c93b0`; presses toggle up↔down and a re-use from the pressed state springs back |
| `0x40` | spawn-time timed/animate setup (schedules a think at `spawn + Δ` — the spark path) |
| `0x100` | **TOUCH_ACTIVATES** — assigns the touch handler (`m_pfn` at `+0x1ec` ← `0x100c9430`), the one that checks the toucher's caps + the `0x1000` gate (not the use filter) |
| `0x200` | **DAMAGE_ACTIVATES** — shootable; the damage handler `0x100c8b80` tests `0x200` and presses when the button takes damage |
| `0x400` | **USE_ACTIVATES** — assigns the +use handler (`m_pfn` at `+0x1f0` ← `0x100c9250`), the one that gates on `CBaseEntity::PassesUseFilter` (`0x100a7bd0`) — the +use path |
| `0x800` | **starts locked** — sets the locked byte `+0x5c4` (the one `GetUseIcon` reads to pick `locked_icon`) |
| `0x1000` | sets `+0x5c5`, a secondary use-gate: the touch handler (`0x100c9430`) only fires if the activator carries a matching flag (`activator[0x13]` bit 2) |

Bit `0x20`/`0x1000` are read in `CBaseButton::Spawn` (`FUN_100c8d60`) and the touch/use handlers
(thunks `0x10002cd4`/`0x100111da` → `0x100c9430`/`0x100c9250`). Bit **`0x2000`** (in 8193/9217)
is **not** tested anywhere in the button class — it is inert for `func_button` (a base-entity or
engine bit). **On buttons VtMB keeps the stock layout: `0x100`=touch, `0x400`=use** — verified by
decompile, not assumed: the `0x400`-armed handler (`0x100c9250`) gates on `PassesUseFilter` (the use
path), and all six `sp_tutorial_1` `func_button`s are `spawnflags 1057` (`0x400` + `use_icon 12`),
i.e. +use switches. Matches `docs/vtmb/animation_and_movers.md` B.5.

### `prop_button` [VtMB — decompiled]

`CPropButton` is a point prop rather than `CBaseButton` brush movement. Its datamap
(`vampire.dll` `0x105ac050`, 21 rows, builder `0x10214e30`) exposes `Use`, `Lock`,
`Unlock`, `ToggleLock` and `SetState`; fields are `locked` (`+0x738`),
`current_state` (`+0x73c`), `max_states` (`+0x740`), `use_icon` and `locked_icon`.
`Spawn` (`0x10215750`) clamps `max_states` to 0..7 and `current_state` to 0..max,
then sets the model skin to that zero-based state.

`Use` (`0x10215a30`) is ordered:

1. if locked, fire `OnPressedLocked` and stop;
2. otherwise fire `OnPressed`;
3. advance `current_state`, wrapping max back to zero;
4. fire `OnSetStateN`, then write the matching skin/state.

`SetState` takes a one-based external state and selects the corresponding zero-based
`OnSetState1..8`/skin row; its retail cap excludes the cycle's terminal max row.
Locking affects the use path and icon choice, not explicit state writes.

### `logic_case_toggle` [VtMB — decompiled]

`CLogicCaseToggle` adds two inputs to the stock `CLogicCase` datamap
(`vampire.dll` `0x10576f80`, builder `0x101345f0`): `InValue` is a string
variant and `InValueDelta` is an integer. They are deliberately different:

- `InValue` (`0x10134780`) case-insensitively matches the incoming value
  against `Case01..16`, records the matching index and fires `OnCaseNN`; a
  non-string variant is converted with retail's compact representation (`2.0`
  becomes `"2"`). A miss records `-1` and fires `OnDefault`.
- `InValueDelta` (`0x101348a0` through `0x101346e0`) advances the current
  pointer by that many configured slots, skipping empty slots and wrapping,
  then fires the selected `OnCaseNN`. Zero warns but still re-fires the current
  case.
- `Spawn` (`0x10134620`) validates `InitialCase` in 0..15, prepositions the
  pointer one slot behind it, then seeks backward to the preceding configured
  slot. This prepares the first positive delta; it does not change `InValue`
  into a delta input.

This distinction is load-bearing in the tutorial elevator. `counter_elev`
forwards its arrival reset value `0` to `case_elev.InValue`. Neither configured
case is `0`, so retail takes the unwired `OnDefault` path and the chain stops.
Treating `InValue` as a zero delta instead re-fires the current floor request
and creates an infinite arrival loop.

## Trigger activation filter (`trigger_multiple` / `trigger_once`)

`CBaseTrigger::PassesTriggerFilters` (`FUN_101c5460`, `vampire.dll`) decides whether a touching
entity may fire the trigger. Called from the StartTouch handler (`FUN_101c6410`); a `false`
return routes to the reject/cleanup path. It reads `m_spawnflags` (`+0x204`) and — unlike the
button — **matches stock Source** exactly:

| Bit | Meaning |
| --- | --- |
| `0x1` | ALLOW_CLIENTS — toucher's `GetFlags()` (`+0x434`) client bit |
| `0x2` | ALLOW_NPCS — `GetFlags() & 0x2000` |
| `0x4` | ALLOW_PUSHABLES — movetype/class check (`0x101c53d0`) |
| `0x8` | ALLOW_PHYSICS — `GetMoveType() == 7` (`MOVETYPE_VPHYSICS`) (`0x101c5420`) |

A toucher passes if **any** enabled class bit matches; then, if a **filter entity** is set
(`m_hFilter` at `+0x564`), its `PassesFilter` (vtable `+0x3c4`) is the final say. Separately,
spawnflag bit `0x80` (tested in the wait-over think as `(char)m_spawnflags < 0`) makes the trigger
**remove itself after firing** (the `trigger_once` behaviour; `trigger_once` is a distinct factory
`FUN_101c6a30`/vftable `0x1047dee4` over the same `CBaseTrigger` base `0x1047d08c`).

## Scripted sequences (`scripted_sequence` / `aiscripted_sequence`)

VtMB's cutscene beat: move a named NPC to a marker, play an animation on it, and fire an output on
either side. The class in `vampire.dll` is **`CCineNPC`** — the `aiscripted_sequence` factory
(`FUN_101a8fe0`) allocates `0x608c` bytes and installs vftable `10477d1c`, whose datamap
(`10593628`) names the class; the `scripted_sequence` factory is `FUN_101a6260` over the same
constructor. `CCineNPC` is an **HL1 `CCineMonster` derivative**, not HL2's `CAI_ScriptedSequence`,
which is what fixes the spawnflag lineage below.

The datamap is half-static: its 36 records live at `0x1059366c`, the leading keyfields and inputs
statically initialized and the nine trailing outputs written by the builder `FUN_101a5e10`; the base
map is `CAI_BaseNPC` (`105c9814`).

**Keyfields** (external name, then the internal name and offset where they differ). `m_iszIdle` and
`m_iszPreIdle` are **one record**, not two — external `m_iszIdle`, internal `m_iszPreIdle` — so a map
writing the literal key `m_iszPreIdle` is dropped by the keyvalue lookup:

| Field | Offset | Meaning |
| --- | --- | --- |
| `m_iszEntity` | `0x5f54` | the NPC's targetname. `!playercontroller` on 10 of the 108 exported sequences |
| `m_iszIdle` (`m_iszPreIdle`) | `0x5f44` | pre-action idle — the pose the NPC waits in from level start until the beat begins |
| `m_iszPlay` | `0x5f48` | the action animation; its length is the beat's duration |
| `m_iszPostIdle` | `0x5f4c` | the resting pose held after the action |
| `m_iszCustomMove` | `0x5f50` | the travel animation used when `m_fMoveTo` is 3 |
| `m_iszNextScript` | `0x5f58` | the sequence to begin when this one ends |
| `m_iszLinkedSequence` | `0x5f5c` | resolved to an entity by a single getter (`FUN_101a8130`); no exported map writes it |
| `m_fMoveTo` | `0x5f60` | 0 No / 1 Walk / 2 Run / 3 Custom movement / 4 Instantaneous / 5 No - Turn to Face |
| `m_iFinishSchedule` | `0x5f64` | which schedule the NPC is handed back on, in `FixScriptNPCSchedule` (`FUN_101a95d0`): `0` the default, `1` schedule `0x2a`, anything else `DevMsg("FixScriptNPCSchedule - no case!")` and then the default. All six exported `aiscripted_sequence`s write `0` |
| `m_flRadius` | `0x5f68` | **never read** — no site in the class range touches it |
| `m_flRepeat` | `0x5f6c` | **never read** |

The private half of the record set — `m_iDelay`, `m_startTime`, `m_saved_movetype`,
`m_saved_movecollide`, `m_saved_solid`, `m_saved_solidflags`, `m_saved_effects`,
`m_saved_troika_flags`, `m_interruptable` (`0x5f90`), `m_sequenceStarted`, `m_hNextCine` (`0x5f94`) —
records that a beat takes ownership of the NPC's movement and collision state for its duration and
gives it back afterwards.

**Inputs:** `BeginSequence`, `CancelSequence`, **`MoveToPosition`** (`inputFunc 0x1000d6c0`, whose
body is `FUN_101a72b0` — reached only through the datamap, so the analyzers leave it
undisassembled), plus the base `Kill`/`ScriptHide`/`ScriptUnhide`. `MoveToPosition` sends the NPC to
the mark without running the action: it returns unless the NPC's script state is `0` or `2`, picks
the move activity through vftable `+0x924`, validates it through `+0x788`, starts the move through
`+0x91c`, and re-arms the `Use` throttle. No exported map wires it. **Outputs:** `OnBeginSequence`,
`OnEndSequence`, and
`OnScriptEvent01..08` — the last driven by animation events embedded in the clip, so they need
decoded `.mdl` events to fire at all.

**`m_fMoveTo` selects the NPC's script state** (`FUN_101a9080`, NPC `+0x5d70`); an unrecognised value
raises `DevWarning("aiscript: invalid Move To Positi[on]")`:

| `m_fMoveTo` | Script state | Meaning |
| --- | --- | --- |
| 0 No, 5 No - Turn to Face | 1 | `SCRIPT_WAIT` |
| 1 Walk | 4 | `SCRIPT_WALK_TO_MARK` |
| 2 Run | 5 | `SCRIPT_RUN_TO_MARK` |
| 3 Custom movement | 6 | `SCRIPT_CUSTOM_MOVE` |
| 4 Instantaneous | 1 | placed inline, then `SCRIPT_WAIT` |

Instantaneous placement does not write angles directly: it sets the origin, drives the yaw through
the NPC's yaw controller, zeroes angular velocity, and raises the entity's effect bit `0x10`
(`docs/vtmb/choreographed_scenes.md` → *The effect bit a scripted jump raises*).

The yaw controller is `FUN_102e0a80`: it wraps `target - current` (`m_pAnim+0x18`) into ±180°,
clamps it to ±the per-step yaw limit at `m_pAnim+0x1c`, re-adds the current yaw and re-wraps into
`[0, 360)`. The placement flips the target 180° first when `m_pAnim+0x28` is set, and writes the
ideal yaw at `m_pAnim+0x34` **directly** when the yaw limit is exactly `180.0` — that value is the
"no limit, snap" sentinel. Any other limit makes the turn a rate-limited approach.

*Divergence, by owner call:* this runtime writes the mark's angles directly instead of driving them
through a rate-limited yaw controller. No `sp_theatre` sequence uses `m_fMoveTo` 4 or 5, so the
difference is confined to `sp_tutorial_1` and `sm_warehouse_1`.

**Spawnflags** live at `CBaseEntity+0x204` and follow the HL1 `CCineMonster` set for bits 1–128; the
VtMB additions are decoded from the bit tests in the class range `0x101a5000–0x101a9600`:

| Bit | Meaning | Site |
| --- | --- | --- |
| `1` | WAITTILLSEEN | — |
| `2` | EXITAGITATED | — |
| `4` | REPEATABLE — when clear, the beat schedules its own removal | `FUN_101a8640` |
| `8` | LEAVECORPSE | — |
| `16` | START_ON_SPAWN — **set on none of the exported sequences** | — |
| `32` | NOINTERRUPT — gates `m_interruptable` | `FUN_101a8890` |
| `64` | OVERRIDESTATE | — |
| `128` | NOSCRIPTMOVEMENT — do not move the NPC to the mark | — |
| `256` | **Hold the post-idle.** With `m_iszPostIdle` set and no live `m_hNextCine`, the sequence-done path logs `Post Idle %s finished`, sets the NPC's script state to 2, replays the post-idle, and returns before cleanup — so the beat never completes and `OnEndSequence` never fires | `FUN_101a8640` |
| `512` | **Priority script.** Tested on the contending cine; when set the challenger is refused with `%s is a priority script and cannot be kicked out of the queue` | `FUN_101a8ac0` |
| `1024` | caches the resolved NPC pointer into the cine at `+0x5f98`; no exported map sets it | `FUN_101a7760` |
| `2048` | suppresses the `Found %s, but can't play` console warning; no exported map sets it | `FUN_101a7600` |
| `4096` | **Pass through characters.** Saves the NPC's troika flags into `m_saved_troika_flags` and ORs bit `0x40` into them for the beat's duration, restoring on cleanup. Bit `0x40` is read by the NPC's `CBaseAnimating::IsIgnoreCollisionEntity` override (`FUN_1029afc0`, `FUN_1029b180`), which with it set answers true for any entity carrying an AI object (`+0x94`) or a player controller (`+0xa8`) — every NPC and the player. World collision is untouched | `FUN_101a7880`, `FUN_101a9080` |
| `8192` | **Never read.** No instruction in `.text` tests spawnflags bit `0x2000`, in either the dword encoding (`+0x204` with immediate `0x2000`) or the byte one (`+0x205` with `0x20`), across all 391 sites that read the field — though four sequences author it | — |

Bit usage across the 22 exported maps: `4` on 49, `32` on 53, `64` on 55, `128` on 1, `256` on 21,
`512` on 31, `4096` on 6, `8192` on 4; bits `1`, `2`, `8`, `16`, `1024` and `2048` on none.

**The queue is a real thing with its own diagnostics.** `BeginSequence` (`FUN_101a7390`) opens with
a re-trigger throttle — a call arriving within 0.05 s of the last is dropped and the gate pushed
further out, with `"*** WARNING *** Called BeginSequence…"`, `"Still another %f seconds before…"`
and `"Try delaying your BeginSequence call…"`. Past it, taking an NPC that another script holds logs
`script "%s" kicking script "%s" out of the queue`; the two refusals above are what stop that
happening. `CCineNPC::Activate` (`FUN_101a8de0`) reports its own resolution failures —
`Could not find NPC %s in CCineNPC::Activate for %s` and `NPC %s has no model in CCineNPC::Activate
for %s`.

That no sequence carries bit 16 is load-bearing: every beat is entered by an explicit
`BeginSequence` — an I/O wire, a `m_iszNextScript` chain, or a level-script call — and none starts
itself at map load.

`sp_theatre`'s courtroom walk-out is the worked example of the two additions that matter: the five
NPCs that walk carry `0x1260` (NOINTERRUPT + OVERRIDESTATE + priority + troika), and the two that
were teleported to the far end and only play looping idles carry `0x360` — the same word with
post-idle hold in place of the troika bit.

### Where the rebuild diverges

Each is a deliberate call, recorded beside the behaviour it departs from:

- **Travel can fail here; in retail it cannot.** Retail's mover always reaches its mark, so a beat
  has no failure branch. This runtime walks a real navigation graph, so travel is bounded by a
  no-progress window and an absolute cap, and a body that runs out is placed on the mark so the beat
  still ends — a beat that never ends stalls the map's whole script flow. The cap has to sit under
  the cleanup timers a map hangs off its own camera track: `sp_theatre` kills the walk-out beats
  twenty seconds into the shot, having authored them against a walk of about half that.
- **Gait speed comes from `speed_walk`/`speed_runbase`, not from the cycle's own displacement**,
  which is not decoded, so a travelling NPC can foot-slide.
- **Spawnflag 256's condition is approximated.** The engine holds the post-idle when there is no
  live `m_hNextCine`; the nearest thing here is an authored `m_iszNextScript`, so an empty one
  stands in for it.
- **Queue ownership is one bit, not a re-read of the owner.** The engine decides a refusal by
  inspecting the cine that holds the NPC; this runtime stamps "this owner refuses handover" onto the
  NPC when the claim is made, and distinguishes the two refusal messages by re-reading only the
  owner's spawnflags. A claim whose owner has been destroyed is cleared rather than honoured.
- **`OnScriptEvent01..08` do not fire** — they need decoded `.mdl` animation events.

**Demand across the 10 exported maps:** 104 `scripted_sequence` + 4 `aiscripted_sequence`;
68 `BeginSequence` and 16 `CancelSequence` I/O wires, plus 68 and 8 receiver-qualified script calls
(`script.BeginSequence()`) that reach the same registered input through the datamap lookup
(`docs/vtmb/python_bridge.md`). 88 wires leave these entities — `OnEndSequence` 48, `OnBeginSequence` 35,
`OnScriptEvent01/02/03` 5 — and they unlock doors, restore cameras, and open conversations, so a
beat that never ends stalls the map's flow. 94 animation references across the set, of which the
4 that name no NPC skeleton all belong to `!playercontroller`.

## `point_teleport`

`CPointTeleport` — factory `FUN_1018d940` (allocates `0x468`, vftable `0x10472e94`), datamap
`0x1058cbf8` (3 records, builder `FUN_1018da10`). It moves the entity named by `target` to a
destination **captured at `Activate`**, not read at input time.

`Activate` (`FUN_1018da40`) caches the teleporter's own origin and angles, then resolves `target`:

- no target and no `m_target` → `Warning("ERROR: %s given no target. Deleted")` and the entity
  removes itself;
- a target that has a parent → `Warning("ERROR: %s can't teleport object (%s) which has a parent")`
  and the destination is left alone;
- **spawnflag `1`** → the cached destination is replaced by the *target's* current origin and
  angles, so the input later returns the target to where it spawned. Two exported entities set it,
  both naming `!player`.

`InputTeleport` (`FUN_1018dc00`) re-checks the parent (same warning, three arguments this time),
then applies the cached transform with `SetAbsOrigin` / `SetAbsAngles` — all three angles, not yaw
alone. When the target carries a player controller it additionally snaps the player's view angles to
the destination's and stamps a teleport time on the player.

Every teleport ends with `FUN_101cf600` on the target, which is **`CBaseEntity::Relink`** (trace
string `0x10558f08`) and whose body is a profiler scope push and pop with no work between them. It
adds nothing: re-establishing the entity's spatial links falls out of `SetAbsOrigin` itself, so
there is no separate touch re-test in the teleport path.

*Divergence, by owner call:* this runtime reads the destination from the entity's own origin at
input time rather than caching it at `Activate`, ignores spawnflag `1`, and moves a parented target
instead of refusing it. None of `sp_theatre`'s nine teleports sets the flag, carries a parent, or
moves after spawn, so the three are confined to `sp_tutorial_1`'s two `!player` teleports. The view
snap *is* reproduced — a teleported player's control rotation follows the destination angles.

## Skin families (`skin` / `SetSkin` / `FadeToSkin`)

The datamap records in `vampire.dll` that drive a model's alternate skin family (record layout per
`docs/vtmb/python_bridge.md` §252: `flags` is the u16 at `+0x12`, bit `0x8` = INPUT):

| externalName | flags | internal | inputFunc | behaviour |
|---|---|---|---|---|
| `skin` | `0xe` SAVE\|KEY\|INPUT | `m_nSkin` @0x670 | **null** | Source `DEFINE_INPUT` — firing it writes the field directly. **Snaps.** |
| `crossfade_skin_time` | `0x6` SAVE\|KEY | `m_flSkinCrossfadeTime` @0x678 | null | keyfield only — the `skin` input never reads it |
| `damaged_skin` | `0x6` SAVE\|KEY | `m_nDamagedSkin` @0x730 | null | |
| `FadeToSkin` | `0x8` INPUT | — | `1008d6d0` | `if (m_nSkin != new) { m_nSkinCrossfade = m_nSkin; m_nSkin = new; }` + `DevMsg("Fading to skin %d over %.2f")` |
| `SetSkinFadeTime` | `0x8` INPUT | — | `1008d5f0` | `m_flSkinCrossfadeTime = max(t, floor)` |
| `SetSkin` | `0x8` INPUT | — | `10014646` → `10190810` | CBreakableProp's; vtable-only-reached, **not disassembled** |

**There is no input named `Skin`.** Input matching is case-insensitive (`Q_stricmp`), so a map's
`Skin` wire binds to the keyfield-input `skin` — a direct `m_nSkin` write. Across the 16 exported
maps that is the *only* skin input any map fires (18 wires); `FadeToSkin` / `SetSkin` /
`SetSkinFadeTime` are wired **zero** times.

`m_nSkin` / `m_nSkinCrossfade` / `m_flSkinCrossfadeTime` (0x670/0x674/0x678) are all networked
SendProps and `FadeToSkin` writes no start time, so the crossfade is rendered **client-side**.
`crossfade_skin_time` carries no authored signal — it is `2.0` on all 723 entities that have it,
including `npc_maker` and `npc_VRat`, i.e. an FGD default stamped on everything that animates.

## Screen fade (`env_fade` spawnflags)

`CEnvFade`'s datamap (`vampire.dll` `0x10568c10` → dataDesc `0x10568c54`, base map
`0x10552e18`) is four records and nothing else: `duration` (`m_Duration`, `+0x450`), `holdtime`
(`m_HoldTime`, `+0x454`), the input **`Fade`** (`0x10100F90`) and the output **`OnBeginFade`**
(`+0x458`). There is no `OnEndFade` and no `ReverseFade` — neither string exists in the binary.
Colour and opacity come from the base map's `rendercolor`/`renderamt`.

`InputFade` starts the fade, then fires `OnBeginFade` at delay 0 with the incoming activator and
itself as caller. It has no think, so every downstream timing in a map is an authored output
delay measured from the moment `Fade` is accepted.

It translates spawnflags into the **client's** fade flags, which are not the same numbers:

| Spawnflag | Client flag | Effect |
| --- | --- | --- |
| `0x1` SF_FADE_IN | **`0`** — clears every bit | flat, no ramp (see below) |
| `0x2` SF_FADE_MODULATE | `0x4` | modulate instead of blend |
| `0x4` SF_FADE_ONLYONE | — | send to the activator alone, when it is a client |
| `0x8` SF_FADE_STAYOUT | `0x20` | uncover again once the hold expires |
| *(none of the above)* | `0x2` FFADE_OUT | cover over `duration` |

The curve itself lives in the client. `CViewEffects::Fade` (`client.dll` `0x10196FE0`) reads
`duration`/`holdtime` as 12-bit fixed point (`×1/4096`) and, for `FFADE_OUT`, sets
`speed = -alpha/duration`, `FadeEnd = now + duration`, `FadeReset = FadeEnd + holdtime`.
`CViewEffects::FadeCalculate` (`0x10197190`) then runs each frame:

- **alpha** — `(FadeEnd - now) * speed`, plus `alpha` when `FFADE_OUT`, clamped to `[0, alpha]`;
  fades whose flags carry neither ramp bit (`0x1`/`0x2`) sit flat at full `alpha` instead.
- **expiry** — once `now` passes *both* `FadeEnd` and `FadeReset` the fade is normally deleted.
  With `0x20` it is instead flipped to a fade-in (`flags &= ~0x22`, `|= 0x1`), its speed negated
  and `FadeEnd` pushed to `now + alpha/speed` — so it takes one more `duration` to uncover, then
  expires. **`SF_FADE_STAYOUT` is therefore what brings the screen back**, not what holds it out.
- The genuinely-permanent flag is `0x8`, which re-pushes `FadeReset` to `curtime + 0.1` every
  frame; `env_fade` never sets it. `0x10` purges the other fades; `0x40` runs `disconnect` when
  the fade ends. A map that wants to stay covered uses a huge `holdtime` instead (the tutorial's
  `end_fade` holds 5000 s).

`SF_FADE_IN` maps to flags `0`, which fails `FadeCalculate`'s `flags & 0x3` ramp test — so it
holds the colour flat at full alpha for `holdtime + duration` and then snaps clear, with no ramp
in either direction. Fades are kept in a list whose colours **sum** and whose alphas **max**.

## use_icon enum

`use_icon`/`locked_icon` index a 72-entry table of `hud/Context_Icons/<name>`
materials. The pointer array is at file offset **`0x2700b4`** in
`Vampire/cl_dlls/client.dll` (image base `0x10000000`; file offset = VA − base),
duplicated at `0x270d54`.

**`use_icon N` selects entry `N-1`; `use_icon 0` means no icon.** Confirmed against
map data and by decoding the art: entry 10 (`portal`) is a picture of a door,
entry 3 (`intrusion`) a padlock, 12 (`switchable`) a light switch, 6 (`lootable`)
an open chest, 18 (`note`) a handwritten note.

| N | icon | N | icon | N | icon | N | icon |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | CarryBody | 19 | stealth_succeed | 37 | reeltoreel | 55 | breakable |
| 2 | Hacking | 20 | stealth_chance | 38 | key | 56 | bustopmap |
| 3 | Intrusion | 21 | Button_1 | 39 | clipboard | 57 | sewerlines |
| 4 | Key | 22 | Button_2 | 40 | printedpapers | 58 | door_playerwanted |
| 5 | accesscard | 23 | Button_3 | 41 | spotlight | 59 | door_transition |
| 6 | Lootable | 24 | Button_4 | 42 | phonograph | 60 | sewer_transition |
| 7 | Monitor | 25 | Button_5 | 43 | phonograph | 61 | pedestal |
| 8 | Phone | 26 | Button_6 | 44 | cashregister | 62 | dance_male |
| 9 | PhysicsHand | 27 | Button_7 | 45 | giovanbook | 63 | dance_female |
| 10 | Portal | 28 | Button_8 | 46 | payphone | 64 | switch |
| 11 | Stakeable | 29 | Button_G | 47 | webcam | 65 | valve |
| 12 | Switchable | 30 | Button_Locked | 48 | button_penthouse | 66 | malkchaos |
| 13 | Sewer | 31 | Button_Up | 49 | bustopmap | 67 | malkkey |
| 14 | Talk_Female | 32 | Button_Down | 50 | sewermap | 68 | malkmind |
| 15 | Talk_Male | 33 | stop | 51 | deadbody | 69 | malkorder |
| 16 | Nosferatu_Warning | 34 | drop | 52 | electroniclock | 70 | malksight |
| 17 | Use_Bomb | 35 | arrowright | 53 | electroniclocked | 71 | malktime |
| 18 | Note | 36 | arrowleft | 54 | breakable | 72 | push |

Entries 42/43 both name `phonograph` and 49/56 both `bustopmap` — the table has
genuine duplicate slots. `context_icon_ring` and `context_icon_back` are the frame
drawn around the icon, not selectable values; the ring alone is the idle reticle.

The icon materials are `UnlitGeneric` + `$translucent` + `$ignorez` +
`$vertexcolor`/`$vertexalpha`, 128×128, under `materials/hud/context_icons/`.

## Most-driven inputs

`Trigger` 1757, `Kill` 1647, `ScriptUnhide` 1333, `PlaySound` 920, `Disable` 710,
`Enable` 692, `ScriptHide` 626, `BeginSequence` 533, `TurnOn` 520, `TurnOff` 490,
`Unlock` 365, `Skin` 354, `ChangeNow` 308, `Open` 299, `Lock` 292, `StopSound` 257,
`TweakParam` 246, `FadeIn` 221, `HideSprite` 219, `SetRelationship` 200.

## Biggest output sources

`logic_relay` 5957 (`OnTrigger`), `events_world` 2551, `events_player` 2290,
`trigger_multiple` 1958 (`OnStartTouch`, `OnTrigger`, `OnEndTouch`), `trigger_once`
1185, `npc_VHumanCombatant` 1051 (`OnDeath`, `OnFoundPlayer`), `func_door_rotating`
833, `scripted_sequence` 738, `logic_auto` 678 (`OnMapLoad`), `prop_switch` 544,
`logic_timer` 500, `func_button` 489.

`logic_relay` is the indirection layer: a quarter of all wires pass through one,
so it is the highest-value class to implement early. The patch's added scripting also
pushes `events_world` (2,551) and `events_player` (2,290) — the world/player event
buses (`SetSafeArea`, `CreateControllerNPC`, discipline/mobility control) — high up the
list, where retail barely used them; both are load-bearing for the shipped runtime.

## Input surface per class

The runtime needs a name→delegate table per classname (`docs/vtmb/python_bridge.md`:
`Entity.__getattr__` resolves Python attribute names against this same datamap, so
`npc.ScriptHide()` and the I/O wire `OnTrigger → ScriptHide` are one lookup).
Counts are wires observed across all maps.

| classname | inputs it receives |
| --- | --- |
| `logic_relay` | Trigger(1704) Disable(84) Kill(74) Enable(66) ScriptUnhide(12) ScriptHide(3) |
| `ambient_generic` | PlaySound(903) Kill(254) StopSound(248) Volume(36) FadeIn(2) FadeOut(2) ScriptUnhide(2) |
| `npc_VHumanCombatant` | 29 inputs — ScriptUnhide(175) TweakParam(140) SetRelationship(107) FollowPatrolPath(95) SetupPatrolType(95) SetInvestigateMode*(130) StayEntrenched(65) WillTalk(36) ScriptHide(32) Kill(25) StartPlayerDialog(19) … |
| `prop_dynamic` | Skin(332) ScriptUnhide(176) Kill(94) SetAnimation(81) ScriptHide(67) Break(65) |
| `scripted_sequence` | BeginSequence(515) CancelSequence(93) Kill(92) ScriptHide(15) ScriptUnhide(14) |
| `trigger_multiple` | Disable(197) Enable(171) Kill(170) ScriptUnhide(60) ScriptHide(45) |
| `env_sprite` | HideSprite(218) ShowSprite(108) TurnOn(96) TurnOff(62) ScriptUnhide(11) Kill(11) ScriptHide(10) |
| `func_door_rotating` | Unlock(102) Open(82) Lock(69) Close(58) ScriptUnhide(31) ScriptHide(7) Toggle(6) Kill(4) |
| `prop_button` | Lock(137) Unlock(105) SetState(69) Kill(2) |
| `env_particle` | TurnOn(180) TurnOff(85) Kill(27) SetRateScale(5) JetLength(2) ScriptUnhide(2) SetParent(1) |
| `trigger_changelevel` | ChangeNow(273) ScriptUnhide(18) ScriptHide(6) Enable(1) Kill(1) |
| `func_door` | Open(157) Close(93) Unlock(15) Lock(12) Toggle(4) Kill(4) ScriptHide(2) |
| `func_brush` | ScriptHide(104) ScriptUnhide(101) Kill(30) Disable(3) Enable(3) |
| `ambient_soundscheme` | FadeIn(205) FadeOut(179) Kill(1) Disable(1) |
| `light` | TurnOff(81) TurnOn(46) Kill(25) SetPattern(20) Toggle(7) FadeToPattern(7) ScriptUnhide(2) |
| `point_teleport` | Teleport(140) Kill(9) |
| `light_spot` | TurnOff(63) TurnOn(58) FadeToPattern(30) SetPattern(7) ScriptHide(5) ScriptUnhide(5) Toggle(1) |
| `logic_timer` | Disable(79) Enable(75) Kill(12) FireTimer(6) RefireTimer(5) ScriptUnhide(2) ScriptHide(2) |
| `camera_track` | PlayAsCameraPosition(87) PlayAsCameraTarget(80) RestoreCameraToPlayerControl(46) Kill(2) |
| `events_player` | CreateControllerNPC(77) RemoveControllerNPC(68) RemoveDisciplines*(29) ImmobilizePlayer(14) MobilizePlayer(13) ClearDialogCombatTimers(8) AwardExp(4) |
| `npc_maker` | Spawn(91) Enable(69) Disable(53) Kill(14) ScriptUnhide(4) TweakParam(3) |

`events_player` is the player-side handle the maps address for camera, mobility,
disciplines and XP.

Input names are case-sensitive in the data with rare exceptions (`trigger`,
`teleport` appear once each lowercased), so lookup should fold case.
