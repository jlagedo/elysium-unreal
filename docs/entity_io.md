# Entity I/O and the interaction layer

VtMB drives its world with Source's entity I/O: an entity fires an **output**
(`OnPressed`, `OnStartTouch`, …) which calls a named **input** (`Open`, `Trigger`,
`ScriptUnhide`, …) on a target entity. `tools/ent_survey.py` reports every table
below from the shipped maps.

Scale across the **engine-loaded map set** — the 108 maps the runtime resolves
patch-first (`Unofficial_Patch/maps`; `python tools/ent_survey.py --patch [--map <name>]`):
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
that would lose most of the 185 brush entities, including all 97 trigger volumes.

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

`sp_tutorial_1`: 1868 entities, 185 brush, 466 hulls, 1028 outputs, 811 KB. All 185
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
i.e. +use switches. This corrects an earlier reading here that had `0x100`/`0x400` swapped; the
offsets/addresses above were right, only the use/touch labels were reversed. Matches
`animation_and_movers.md` B.5.

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

The runtime needs a name→delegate table per classname (`docs/python_bridge.md`:
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
