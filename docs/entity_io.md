# Entity I/O and the interaction layer

VtMB drives its world with Source's entity I/O: an entity fires an **output**
(`OnPressed`, `OnStartTouch`, …) which calls a named **input** (`Open`, `Trigger`,
`ScriptUnhide`, …) on a target entity. `tools/ent_survey.py` reports every table
below from the shipped maps.

Scale across the 101 maps: **63,861 entities**, **299 classnames**, **16,125
outputs**. `python tools/ent_survey.py [--map <name>]`. (Counts are one
`ent_survey.py` snapshot; `python_bridge.md`/`rebuild-strategy.md` cite **16,214
outputs / 1,621 Python calls** from a later run — a few dozen apart, re-run the survey
to reconcile.)

## Output format

Outputs are a comma-joined value on a key beginning `On`/`Out`:

```
target , input , param , delay , times , python , extra
   0       1       2       3       4       5        6
```

**Seven fields, not Source's five.** 16,096 of 16,125 outputs write 7; 29 write 6.
Fields 0-4 are stock Source (`times` = -1 means unlimited). **Field 5 is a Python
call string** (~1,591–1,621 outputs carry one across snapshots) which is wrapped as `__main__.%s` — see
`docs/python_bridge.md`. The wrap format string lives in **`vampire.dll`**
(`0x1055e370`, in the event-queue dispatch region), not `engine.dll`. A target of `!activator`/`!self` is a runtime
reference, not a `targetname`.

## Base inputs

`Kill`, `ScriptHide` and `ScriptUnhide` are received by nearly every classname.
They live on the base entity and reach subclasses through the datamap `baseMap`
chain (`docs/python_bridge.md`), so the port implements them once, not per class.

## Hidden state: StartHidden / ScriptHide / ScriptUnhide

**`ScriptHide` is not "invisible" — it is a whole-entity OFF switch.** It makes the
entity non-solid, non-thinking and undrawn; `ScriptUnhide` restores it. This is
VtMB's own system and has no equivalent in Source SDK 2013.

- `StartHidden` `1` — the entity spawns OFF. A keyvalue on any entity, not just
  brush entities (NPCs, props and `func_brush` all use it).
- `ScriptUnhide` (1,191 wires) / `ScriptHide` (536) — turn on / off at runtime.

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

**The exporter honours it.** Beyond filtering `tools/*` geometry by material name,
`bsp_to_scene.py` reads the entity keyvalues, collects the `StartHidden 1` brush
models into `hidden_models`, and drops their faces from the world/sky render (it
prints a `skipped StartHidden: N` count). So the two states that would otherwise leak
stay hidden until the game (once ported) reveals them:

- Six `func_button` volumes in `sp_tutorial_1` skinned `DEBUG/DEBUGEMPTY` (a 32×32
  half-red/half-blue placeholder) — no longer rendered as red/blue rectangles by the
  spawn door.
- `func_brush` `chopwndwbroken` (131 faces), the *broken* state of a window revealed by
  script when it breaks — no longer drawn on top of the intact one.

## The `<map>.ents` sidecar

`bsp_to_scene.write_entities` emits every entity to `<map>.ents` (JSON). It is
deliberately **unfiltered**: the render pass drops `tools/*` and `StartHidden`
faces, but those same entities carry the level's behaviour, so filtering the data
the way the render is filtered would throw the game away. In `sp_tutorial_1` alone
that would lose 110 of 147 brush entities, including 55 trigger volumes.

```json
{"map":"sp_tutorial_1","entities":[
  {"classname":"func_button","targetname":"","origin":[-3.02,1.78,9.45],
   "model":61,"hulls":[[x,y,z, ...]],"contents":1,"blocks_player":true,
   "start_hidden":true,
   "outputs":[{"name":"OnPressed","target":"pc_control_55","input":"Activate",
               "param":"","delay":0.0,"times":-1,"python":""}],
   "keys":{"spawnflags":"1057","use_icon":"12","soundgroup":"small_metal_switch"}}
]}
```

- `origin` — Unreal centimetres (`source_to_unreal`; the JSON above shows Godot-era
  metre values for illustration). For `func_door_rotating` it is also the hinge.
- `hulls` — convex hulls in **entity-local** Unreal centimetres, one flat `x y z ...` list
  each, built by walking the brush entity's own headnode (`dmodel_t` @36) through
  `_model_brushes`. World = `origin + hull`; `source_to_unreal` is linear, so
  converting each separately and adding is equivalent. Brush entities are authored
  about their origin, so a button's hull centre is (0,0,0).
- `contents` — the OR of the brushes' CONTENTS flags; `blocks_player` is
  `contents & BLOCK_MASK`. Solidity is per entity, gated at runtime by
  `start_hidden` (a hidden entity is `SOLID_NONE`).
- `keys` — every remaining keyvalue, verbatim.

`sp_tutorial_1`: 1226 entities, 147 brush, 300 hulls, 872 outputs, 533 KB. All 147
brush entities yield at least one hull.

**Only the debug layer consumes it so far** — the F1 debug-mode entity gizmos +
inspector read `.ents` read-only (`WorldLoader.BuildEntGizmos`/`PickEntity`/
`EntityDump` → `EntInspector`): MultiMesh boxes + labels at each origin, colour-keyed
by class, RMB-dumped. No *functional* runtime node is spawned yet. The runtime mapping
this is built for:

| Source | runtime node | fires on |
| --- | --- | --- |
| `trigger_multiple` / `trigger_once` | `Area3D` + `ConvexPolygonShape3D` | `body_entered`/`body_exited` = `OnStartTouch`/`OnEndTouch` |
| `func_button` | shape on a use-only collision layer | camera ray on use-press — **not** proximity |
| `StartHidden 1` | node built, monitoring + collision off | inert until `ScriptUnhide` |
| `func_door*` | `AnimatableBody3D` + hulls | blocks movement; `Open`/`Close`/`Lock` |

## The usable set

Only these classnames carry `use_icon`/`locked_icon` — i.e. only these are things
the player can look at and use:

| classname | typical use_icon / locked_icon |
| --- | --- |
| `func_door_rotating` | 10 portal / 3 intrusion |
| `prop_doorknob` | 10 portal / 3 intrusion |
| `func_door` | 10 portal / 3 intrusion |
| `prop_switch` | 12 switchable, 13 sewer / 12 |
| `func_button` | 12 switchable / 12 |
| `prop_button` | 31 button_up, 32 button_down, 30 button_locked |
| `prop_doorknob_electronic` | 53 electroniclock / 54 electroniclocked |
| `prop_sign` | 18 note, 56 bustopmap, 41 printedpapers |
| `item_container_animated` | 6 lootable |
| `item_container_lock` | 3 intrusion |
| `prop_padlock` | 3 intrusion |
| `item_container_one_item_filtered` | 61 pedestal |
| `item_container` | 6 lootable |

`use_icon` is the cursor when usable; `locked_icon` when locked. `3`/`intrusion` is
a padlock — it names the Intrusion (lockpicking) skill the player would need.

`func_button` emits three outputs: **`OnPressed`** (124), **`OnIn`** (86) and
**`OnOut`** (74) — `OnIn`/`OnOut` fire as the look-cursor enters and leaves the
volume, which is what arms the icon. `soundgroup` names the sound set
(`standard_door` 1028, `small_metal_switch` 141, `elevator_button` 100).

`func_button` spawnflags seen: 1056, 1057, 1025, 8193, 9217, 256, 1024, 33.
`m_spawnflags` is a `FIELD_INTEGER` at entity offset **`0x204`** (confirmed in the
`CBaseEntity` datamap builder `FUN_100a22f0`, `vampire.dll`). `CBaseButton::Spawn`
(`FUN_100c8d60`) reads it and wires up the button:

| Bit | Behaviour |
| --- | --- |
| `0x1` | **DONTMOVE** — pressed position is forced equal to the start position (matches stock `SF_BUTTON_DONTMOVE`) |
| `0x20` | **TOGGLE** — in the touch handler (`0x100c9250`) it gates the down-activation from the rest state; also read by the direction helper `0x100c93b0`. Touch toggles up↔down |
| `0x40` | spawn-time timed/animate setup (schedules a think at `spawn + Δ`) |
| `0x100` | assigns the **use/activate** handler (`m_pfn` at `+0x1ec` ← `0x100c9430`) + one vtable call (`+0x37c`) |
| `0x400` | assigns the **touch** handler (`m_pfn` at `+0x1f0` ← `0x100c9250`) — touch-activates |
| `0x800` | **starts locked** — sets the locked byte `+0x5c4` (the one `GetUseIcon` reads to pick `locked_icon`) |
| `0x1000` | sets `+0x5c5`, a secondary use-gate: the use handler (`0x100c9430`) only fires if the activator carries a matching flag (`activator[0x13]` bit 2) |

Bit `0x20`/`0x1000` are read in `CBaseButton::Spawn` (`FUN_100c8d60`) and the use/touch handlers
(thunks `0x10002cd4`/`0x100111da` → `0x100c9430`/`0x100c9250`). Bit **`0x2000`** (in 8193/9217)
is **not** tested anywhere in the button class — it is inert for `func_button` (a base-entity or
engine bit). VtMB diverges from modern Source on buttons, so the values above are read from this
build, not assumed from stock `SF_BUTTON_*`.

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

`Trigger` 1444, `Kill` 1274, `ScriptUnhide` 1191, `Enable` 673, `Disable` 652,
`PlaySound` 635, `ScriptHide` 536, `BeginSequence` 461, `TurnOn` 442, `TurnOff`
427, `Skin` 289, `Unlock` 279, `ChangeNow` 275, `Open` 260, `Lock` 248,
`StopSound` 240, `TweakParam` 236, `HideSprite` 224, `Close` 190.

## Biggest output sources

`logic_relay` 5418 (`OnTrigger`), `trigger_multiple` 1811 (`OnStartTouch`,
`OnTrigger`, `OnEndTouch`), `trigger_once` 1055, `npc_VHumanCombatant` 780
(`OnDeath`, `OnFoundPlayer`), `func_door_rotating` 736, `scripted_sequence` 602,
`logic_auto` 414 (`OnMapLoad`), `prop_switch` 381, `logic_timer` 355,
`func_button` 284.

`logic_relay` is the indirection layer: a quarter of all wires pass through one,
so it is the highest-value class to implement early.

## Input surface per class

The runtime needs a name→delegate table per classname (`docs/python_bridge.md`:
`Entity.__getattr__` resolves Python attribute names against this same datamap, so
`npc.ScriptHide()` and the I/O wire `OnTrigger → ScriptHide` are one lookup).
Counts are wires observed across all maps.

| classname | inputs it receives |
| --- | --- |
| `logic_relay` | Trigger(1403) Disable(82) Enable(67) Kill(60) ScriptUnhide(11) ScriptHide(3) |
| `ambient_generic` | PlaySound(618) StopSound(230) Kill(70) Volume(33) FadeIn(2) FadeOut(2) |
| `npc_VHumanCombatant` | 28 inputs — ScriptUnhide(144) TweakParam(137) FollowPatrolPath(89) SetupPatrolType(89) SetRelationship(88) SetInvestigateMode*(130) StayEntrenched(53) WillTalk(24) StartPlayerDialog(18) … |
| `prop_dynamic` | Skin(268) ScriptUnhide(146) Kill(98) SetAnimation(74) Break(58) ScriptHide(46) |
| `trigger_multiple` | Disable(189) Enable(168) Kill(156) ScriptUnhide(62) ScriptHide(50) |
| `scripted_sequence` | BeginSequence(440) Kill(84) CancelSequence(70) ScriptHide(15) ScriptUnhide(14) |
| `env_sprite` | HideSprite(223) ShowSprite(111) TurnOn(92) TurnOff(53) Kill(13) ToggleSprite(2) |
| `func_door_rotating` | Unlock(86) Open(73) Lock(61) Close(50) ScriptUnhide(27) Toggle(4) |
| `func_door` | Open(128) Close(96) Unlock(15) Lock(12) Toggle(4) Kill(4) |
| `prop_button` | Lock(111) Unlock(98) SetState(49) Kill(2) |
| `trigger_changelevel` | ChangeNow(245) ScriptUnhide(15) ScriptHide(4) Kill(1) |
| `env_particle` | TurnOn(146) TurnOff(69) Kill(23) SetParent(4) SetRateScale(3) |
| `func_brush` | ScriptUnhide(99) ScriptHide(92) Kill(31) Enable(3) Disable(2) |
| `light` | TurnOff(71) TurnOn(40) Kill(23) SetPattern(20) FadeToPattern(7) Toggle(4) |
| `point_teleport` | Teleport(125) Kill(7) |
| `logic_timer` | Enable(71) Disable(70) Kill(12) FireTimer(2) |
| `camera_track` | PlayAsCameraPosition(76) PlayAsCameraTarget(70) RestoreCameraToPlayerControl(41) |
| `events_player` | CreateControllerNPC(67) RemoveControllerNPC(60) ImmobilizePlayer(13) MobilizePlayer(13) RemoveDisciplines*(23) AwardExp(2) |
| `ambient_soundscheme` | FadeIn(141) FadeOut(112) Kill(1) |
| `npc_maker` | Spawn(88) Enable(65) Disable(50) Kill(12) |

`events_player` is the player-side handle the maps address for camera, mobility,
disciplines and XP.

Input names are case-sensitive in the data with rare exceptions (`trigger`,
`teleport` appear once each lowercased), so lookup should fold case.
