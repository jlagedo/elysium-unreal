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

**The data normally writes seven fields, but retail consumes exactly six.** 24,065 of 24,081
engine-loaded outputs write 7; 14 write 6 (one writes 5, one 8). Retail's parser
`FUN_100ccf90` reads target, input, parameter, delay, count and Python, then stops. A seventh
`extra` field and anything after it are inert authoring residue; accepting a wide row must not move
field 5 or turn the residue into behavior. (Retail corpus: 16,096 of 16,125 write 7, 29 write 6.)

Fields 0–4 are stock Source. The parser initializes `times` to `-1` and rewrites an authored `0` to
`-1`, so **both `0` and `-1` mean unlimited**; a positive value is the remaining-fire counter.
**Field 5 is a Python call string** (6,956 outputs carry one; this doc counts fields from zero —
prose elsewhere in the doc set counts from one and calls the same payload "field 6") which is
wrapped as `__main__.%s` —
see `docs/vtmb/python_bridge.md`. The wrap format string lives in **`vampire.dll`** (`0x1055e370`,
in the event-queue dispatch region), not `engine.dll`. A target of `!activator`/`!self` is a runtime
reference, not a `targetname`.

An output-looking key in the entity lump is not proof that an output exists. Ordinary keyvalue
parsing first resolves the external name through the entity's datamap/baseMap chain; only a record
typed as an output receives these parsed actions. An unknown key is silently dropped. This is
distinct from a valid output whose target later resolves to nothing or whose receiver refuses an
unknown input. The current exports contain the concrete trap
`trigger_player_activity_level.OnTrigger`: two rows are stamped in `sm_diner_1`, but that class has
no such datamap output and retail never stores or fires them.

### Output-list and queue order

Retail order is mechanical and differs from the visual/export order in a non-obvious place:

1. `FUN_100cd6d0` **prepends** each parsed action to the output object's linked list. The BSP
   entity lump and exporter preserve repeated-key authoring order, so repeated rows for one output
   fire in **reverse lump/export order**.
2. `CBaseEntityOutput::FireOutput` (`FUN_100cd300`) walks that list head to tail. For every action it
   first inserts one queue entry, then decrements a positive `times`; an action reaching zero is
   removed immediately without disturbing the remaining rows.
3. `CEventQueue::AddEvent` (`FUN_100ce210`) advances while existing `fireTime <= newFireTime`.
   Earlier deadlines therefore lead; equal-time entries are **FIFO in enqueue order**.
4. Service takes the due head repeatedly. If a handler enqueues another zero-delay event, it goes
   behind the equal-time cohort already pending: with due `A, B`, if `A` produces `C`, delivery is
   `A, B, C`, not depth-first `A, C, B`.

**A negative authored delay is admitted and is not the same as zero.** No clamp, `abs()` or sign
test exists anywhere between the keyvalue parser and the queue: the six-field parser (`FUN_100ccf90`)
reads the delay with a bare string-to-float straight into the action record, `FireOutput` computes
`fireTime = delay + curtime` unguarded, and `AddEvent` sorts on that value. `delay = -1.0` therefore
produces a `fireTime` a full second in the past — already overdue when queued — and because the
insert walk only advances past nodes with `fireTime <= new`, it splices in **ahead of** every
`curtime + 0` event queued in the same pass rather than tying with them. It is drained on the very
next service pass, with ordering priority over concurrent zero-delay outputs.
`sp_tutorial_1`'s `logic_shot_7 → sound_maul_wolves.PlaySound` authors exactly this.

One queued record may carry a name target, field-5 Python, and a direct `EHANDLE`. Service order
inside that record is: deliver the input to **every** matching name target in global entity-list
order; execute field-5 Python; then deliver to the direct handle if it is still valid. A missing name
or input is non-fatal. A stale activator/caller becomes null: name delivery and Python still run,
while an invalid direct target is skipped. Python errors print and continuation is not rolled back.
The frame boundary, unbounded retail drain and starvation consequence are owned by
`docs/vtmb/game_runtime.md`.

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

**A wire may name nothing.** The shipped maps carry outputs whose target no entity in that map
holds; the lookup finds nothing, the output is discarded, and no error is raised. `sp_theatre`'s
authored-dangling `controls` pair is one such wire (see `!playercontroller` below); its
`streetlight_red_south` / `_green_south` / `_yellow_south` trio is another, addressed
`TurnOn`/`TurnOff` by three `streetlight_state_*_south` relays that nothing triggers either, so
that traffic light is inert from both ends.

A target that resolves to nothing and an input the target's class does not implement are separate
conditions and worth counting separately: only the second is a gap in a reimplementation.

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

An explicit transform write to `!player` while this relationship is live also updates the
controller's pose anchor. Ordinary player movement does not: it samples the pawn into `!player`
without taking ownership of a scene-staged controller. This distinction is load-bearing in the
tutorial porch chain. `teleport_fade` moves the player while the controller exists, and the delayed
`RemoveControllerNPC` must preserve that destination rather than restoring the controller's
pre-fade porch transform.

Both inputs are thin: `CreateControllerNPC` (`0x10227280`) resolves the player and calls a
get-or-create keyed on `m_hControllerNPC` (player `+0x1db0`), which reuses a live controller of the
matching classname and otherwise spawns one and copies model, skin, pose fields, origin and angles
across. **Nothing in that chain touches the player pawn** — no hide, no freeze, no input suppression,
no solidity change; a scene wanting any of those authors them separately.
`RemoveControllerNPC` (`0x102272b0`) calls the destroy with both copy flags set, and the order inside
is state first: angles, then origin read from the controller's **live** absolute-origin field, then
the pose block, and only then is removal scheduled — via a think set to the next frame, not a
synchronous delete. The alias clears after that scheduling.

### `events_player.RemoveDisciplinesNow`

Three steps (`0x10227440`), and only the first is about disciplines. It calls the same
`DisciplineGlobalTeardown` (`0x10147a60`) that backs `ClearActiveDisciplines`/`vdiscipline_endall`
(`0x1033d8a0`), which clears the player's pending discipline events out of the global event queue and
then walks the active-discipline bitmask ending each effect ungracefully. Nothing is restored
afterwards.

It then force-destroys any live controller-NPC relationship **with** state hand-back — the same
transaction `RemoveControllerNPC` performs — and forces the player's model back to its default when
the current model differs, which is what undoes a form-changing discipline. A third step resets two
player fields, applies the clan/template record, and destroys a second template-linked relationship
without hand-back.

So the name understates it: this input cancels active disciplines *and* tears down the player's
controller and model state. `sp_tutorial_1` fires it at `trigger_4`, immediately before creating the
scene's controller.

The opening map also has one deliberately dangling authored wire:
`walk_out_cam_k.OnReachedKeyframe → controls.Deactivate`, paired with a `trigger_once` sending
`controls.Activate`. `sp_theatre` contains no target named `controls` and no `game_ui` entity. The
rebuild therefore leaves both as non-fatal missing-target diagnostics; it does not invent a receiver.

## `trigger_changelevel` (`CChangeLevel`)

The forced-transition input is named **`ChangeNow`**. The exported corpus carries 88 such wires
across 19 maps and zero wires named `ChangeLevel`; genesis's
`firetrans.OnStartTouch → boogieout,ChangeNow` is one of them. `ChangeLevel` is not a shipped map
input name and exists in the rebuild only as a compatibility alias for earlier internal callers.

`InputChangeNow` (`FUN_101c77d0`) **requires a player activator**: a non-player activator produces
`Warning("%s recieved ChangeNow input from non-player activator!")` and nothing else.

`CChangeLevel::Spawn` (`FUN_101c74c0`) errors out on an empty `map` or `landmark`, installs
`UseChangeLevel` (`FUN_101c7870`) when the entity has a `targetname` — so a named
`trigger_changelevel` is `+use`-able — and gates the **touch handler** on spawnflag bit `0x2`:

```c
CBaseTrigger::InitTrigger(this);
if ((*(byte *)(this + 0x204) & 2) == 0)
    m_pfnTouch /*+0x1ec*/ = TouchChangeLevel;      // FUN_101c7d90
```

Bit `0x2` is therefore stock `SF_CHANGELEVEL_NOTOUCH` on this class, **not** `CBaseTrigger`'s
`ALLOW_NPCS`. It is the dominant shipped value: 94 of the 104 exported `trigger_changelevel`
entities carry `2`, five carry `0`, two carry `1`, one carries `3`. Those 94 transition only when a
script fires `ChangeNow`.

`TouchChangeLevel` does not consult `PassesTriggerFilters` at all — the toucher merely has to be the
player, and a noclipping player is refused with `DevMsg("In level transition: %s %s")`:

```c
if (pOther[0x2a] /*+0xa8 player controller*/ != 0) {
    if (pOther->GetMoveType() == 9 /*MOVETYPE_NOCLIP*/) { DevMsg(...); return; }
    ChangeLevelNow(this);                          // FUN_101c7890
}
```

`ChangeLevelNow` latches one transition per frame (`if (curtime == m_flLastChange) return;`).
`InTransitionVolume` (`FUN_101c7fa0`) is **gutted — it returns `1` unconditionally**, so the
`"Player isn't in the transition volume"` branch is dead and `trigger_transition`
(`FUN_101c7060`) is inert.

`CChangeLevel` still inherits `CBaseTrigger::StartTouch`/`EndTouch`, so its `OnStartTouch` /
`OnEndTouch` outputs remain subject to the activator-class filter. No exported
`trigger_changelevel` wires either output.

## `trigger_hurt` (`CTriggerHurt`)

Datamap `0x1059d7f0`, records `0x1059d834`, 14, builder `FUN_101c59b0`.

| externalName | internal | offset | notes |
| --- | --- | --- | --- |
| `damage` | `m_flDamage` | `+0x598` | float |
| `SetDamage` | `m_flDamage` | `+0x598` | KEY + INPUT, null `inputFunc` — a direct write, like `skin` |
| `damagetype` | `m_bitsDamageInflict` | `+0x5a0` | bitfield; `& 0x8` selects a separate impact path |
| `damagevelocitymag` | `m_flDamageVelMag` | `+0x5a4` | force magnitude; `<= 0` disables the force entirely |
| `damagevelocitypos` | `m_vecDamageVelPos` | `+0x5a8` | vector |
| `damagevelocitydir` | `m_nDamageVelDir` | `+0x5c0` | **integer mode**, not a vector |
| — | `m_vecDamageVelVect` | `+0x5b4` | derived at `Spawn`, save-only |
| — | `m_flLastDmgTime` | `+0x59c` | save-only |
| — | `m_hurtEntities` | `+0x5f4` | `CUtlVector`, save-only |
| `HurtNow` | `InputHurtNow` | — | `0x10009557` → `FUN_101c6670` |
| `OnHurt` | `m_OnHurt` | `+0x5c4` | fires for a non-player victim |
| `OnHurtPlayer` | `m_OnHurtPlayer` | `+0x5dc` | fires for a player victim |

`HurtEntity` (`FUN_101c5f40`) selects between the two outputs on the victim's player-controller
pointer: `FireOutput(pOther[0xa8] == 0 ? m_OnHurt : m_OnHurtPlayer, pOther)`.

**Damage cadence.** Entry deals a half tick and the think deals a triple tick every three seconds,
so the sustained rate equals `damage` per second:

```
CTriggerHurt::StartTouch  FUN_101c6410 : m_flDamage * 0.5      (FMUL [0x10449270])
CTriggerHurt::HurtThink   FUN_101c6390 : HurtAllTouchers(3.0f)
                                         m_flNextThink = curtime + 3.0   ([0x10449258])
```

`HurtAllTouchers(t)` deals `m_flDamage * t` to every entity in the volume; a think that hurt nobody
does not re-arm. `InputHurtNow` (`FUN_101c6670`) hurts all touchers immediately when enabled;
against a disabled volume it temporarily calls `InputEnable`, then arms `HurtOnceThink`
(`FUN_101c63d0`, also `×3.0`) at `curtime + 0.1` and disarms after one pass.

**The damage-velocity trio.** `m_flDamageVelMag` gates the whole path; at or below zero the victim
takes a plain `CTakeDamageInfo` (`FUN_101c26d0`) with no force. `m_nDamageVelDir` then selects how
the direction is derived from the point `m_vecDamageVelPos`:

| `damagevelocitydir` | damage position | direction, normalized then scaled by `damagevelocitymag` |
| --- | --- | --- |
| `0` | the trigger's own origin | `m_vecDamageVelVect`, precomputed once in `Spawn` (`FUN_101c5e80`) as `normalize(damagevelocitypos − trigger origin) × mag` |
| `1` | `damagevelocitypos` | `victimOrigin − damagevelocitypos` — blow outward from the point |
| other | victim origin | `damagevelocitypos − victimOrigin` — pull toward the point |

The force is also applied to the victim's VPhysics object (`FUN_10344f80`, mode 2) when the
pushable test in `PassesTriggerFilters` holds.

## `Kill` removes; it never kills

`CBaseEntity::InputKill` (`0x100acef0`) is a one-line forward to the virtual `Kill` at vtable slot
119. `CBaseEntity::Kill` (`0x100acf90`) calls `UTIL_Remove` and nothing else, and
`CBaseCombatCharacter::Kill` (`0x1033cb90`) is a same-body wrapper differing only in its profiler
label — so an NPC, a `func_brush` and a `prop_dynamic` all answer the input identically. No class in
the character chain declares a `Kill` datamap record, so the name lookup walks
`CAI_BaseNPCTroika` → `CAI_BaseNPC` → `CBaseCombatCharacter` and lands on `CBaseEntity`'s own.

Nothing on that path touches health, life state, a death sequence or a ragdoll, and nothing fires an
output, drops inventory or plays a sound. Removal is **deferred**: `UTIL_Remove` sets a
not-yet-queued guard bit, releases the networkable interface, and appends the entity to a growable
global delete list (`FUN_100f6bb0`) drained later — the entity is not destroyed in the caller's
frame.

The practical consequence for authored scenes is that a cast removed with `Kill` **blinks out**. A
map that wants bodies to fall has to damage them instead; `sp_tutorial_1`'s alley scene does both,
killing the Sheriff, two Sabbat, a proxy and two wolves outright at the end while separately
`TakeDamage`-ing the third Sabbat so it dies on camera.

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

## `func_areaportalwindow`: Source visibility backing

An authored `func_areaportalwindow` uses `target` to name a black backing brush. Where
`BackgroundBModel` is present, it names the ordinary foreground window. VtMB hides the target as
an independent entity, adopts its model onto the controller, and changes that model's blend from
`TranslucencyLimit` at `FadeStartDist` to fully opaque at `FadeDist`. A named foreground model
remains fully drawn. The controller also opens or closes its area portal from viewer distance,
coupling the visual cover to Source's PVS mechanism.

**Deliberate divergence (owner-called):** Elysium does not reproduce this distance-faded PVS
cover. Unreal owns visibility and occlusion, while Lumen owns the associated lighting and
reflection response. The exporter therefore keeps the controller and target entity records and
their collision hulls, but omits the brush mesh of the entity named by
`func_areaportalwindow.target`. `BackgroundBModel` remains an ordinary renderable brush, so its
authored glass material is the visible window. `tools/black` remains renderable everywhere else.

R6.4 (2026-09-02) gave `func_areaportalwindow` a class that carries `FadeStartDist`/`FadeDist`/
`TranslucencyLimit` and the two names, but writes no cull range: the only brush those distances
govern is the omitted backing above. Whether to re-mesh the backing with a minimum draw distance
(transparent near, black far — VtMB's two end states) is an owner call; `func_lod`'s
`DisappearDist` lands as `cull_max_cm`. `C_Func_LOD::ShouldDraw`
(`client.dll 0x100bb710`) is a hard draw/no-draw hysteresis cull on view distance, not an alpha
fade.

## The `<map>.ents` sidecar

Entity I/O relies on three properties: entities are exported unfiltered; `outputs[]` preserves all seven fields;
and brush `hulls` and `brush_mesh` vertices are entity-local Unreal-centimetre geometry whose
world transform is the entity origin. `elevator_floors` is the fixed eight-entry absolute-Z
table already converted to Unreal centimetres. `start_hidden` gates collision, visibility and
monitoring, while `keys` retains every raw keyvalue needed by a class handler. This doc owns
the behavior of those fields, not a second copy of their wire schema.

Keyvalue numbers read with C `atof` semantics — the longest numeric prefix, stopping at the
first character that cannot continue the number — and authored data relies on it:
`hw_jewelry_1` ships origins with comma decimals (`"-3496,92"`), which the engine, and
therefore the export (`UE_bsp_to_scene._atof`), reads as `-3496`.

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

### Modern targeting policy [owner-called Feel divergence]

The reconstruction reproduces the entity-side facts above — eligibility, hidden/locked state, icons,
logical owner, `Use`, and class outputs — but deliberately does not reproduce the retail client's
near-object/look-cursor search. Player `+use` selection is a modern camera-driven policy shared by
first and third person:

- the final `PlayerCameraManager` POV supplies the aim ray, while the player camera pivot supplies
  body reach and the second line-of-sight test;
- closest-bounds body reach is 225 cm; the camera ray extends by camera-to-body distance plus that
  reach and a 50 cm margin, capped at 1500 cm;
- an exact registered anchor hit wins absolutely. Only a miss admits an ephemeral 2.5-degree
  assistance query, whose radius is clamped to 4–10 cm at target depth;
- the current assisted focus receives 25 percent hysteresis. Remaining ties resolve by angular
  error, camera depth, then stable entity handle;
- camera-to-target and body-to-target line of sight must both pass. `elysium.UseAssist 0` disables
  the assistance tier for exact-only diagnosis.

This is a **Feel divergence**, explicitly owner-called: the original interaction facts remain the
content authority, while the selection feel is rebuilt for modern first-/third-person cameras.
There is no persistent nearby-candidate set and no trigger-volume ownership contest. Unreal
components register query anchors for their logical entities; knob-to-door and similar associations
remain class behavior, so focus can never bypass a knob's lock state or outputs. Hidden, dead,
disabled, 3D-sky, and unimplemented entities expose no active anchor.

Input is likewise separated from selection. `use` remains a press/release command bit in the
per-frame user command, and the entity world consumes its edges only after that frame's focus has
settled. Scripted `AcceptInput("Use")` remains direct entity I/O and is not spatially selected.
The foundation exposes anchors only for implemented doors, use-enabled `func_button`, and
`prop_button`; doorknobs, switches, signs, terminals, and containers join only with their specialized
interaction behavior.

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
entity may fire the trigger. It is called from `CBaseTrigger::StartTouch` (`FUN_101c5590`, fires
`m_OnStartTouch` `+0x568`), `CBaseTrigger::EndTouch` (`FUN_101c55d0`, `m_OnEndTouch` `+0x580`),
`CTriggerMultiple::MultiTouch` (`FUN_101c68b0`) and `CTriggerHurt` (`FUN_101c6410` /
`FUN_101c5f40`); a `false` return routes to the reject/cleanup path. It reads `m_spawnflags`
(`+0x204`):

| Bit | Meaning | Test |
| --- | --- | --- |
| `0x1` | ALLOW_CLIENTS | `GetFlags()` (`+0x434`) `& FL_CLIENT (0x80)` — the raw `TEST AL,AL / JS` on the low byte |
| `0x2` | ALLOW_NPCS | `GetFlags() & FL_NPC (0x2000)` |
| `0x4` | ALLOW_PUSHABLES | `FUN_101c53d0`: `[ent+0x4c] & 0x8`, else `vt+0x170 == 1 && GetMoveType() == 8` |
| `0x8` | ALLOW_PHYSICS | `FUN_101c5420`: `GetMoveType() == 7`, else `[ent+0x4c] & 0x2000` |

A toucher passes if **any** enabled class bit matches. **There is no allow-all fallback**: a trigger
carrying none of these four bits falls through to `XOR AL,AL` at `0x101c553b` and rejects every
toucher, so `spawnflags 0` admits nothing.

The engine names the two principal bits itself. `CBaseTrigger`'s datamap (`0x1059d570`, records
`0x1059d5b4`, 13, builder `FUN_101c49e0`) registers six inputs that mutate them at runtime —
`EnableFlagClient` / `DisableFlagClient` / `ToggleFlagClient` (`FUN_101c5840` / `101c58a0` /
`101c57e0`, bit `0x1`) and `EnableFlagNPC` / `DisableFlagNPC` / `ToggleFlagNPC` (`FUN_101c5870` /
`101c58d0` / `101c5810`, bit `0x2`). No exported map wires any of them. The same datamap carries
`filtername` → `m_iFilterName` (`FIELD_STRING`, `+0x560`), `StartDisabled` → `m_bDisabled`
(`+0x55c`), and the two touch outputs. `Enable` / `Disable` (`FUN_101c4bf0` / `FUN_101c4dd0`) work by
adding and removing solid flag `0x8` (`FSOLID_TRIGGER`), so a disabled trigger stops receiving
touches at the collision layer rather than filtering them. Adding `FSOLID_TRIGGER` rebuilds the
touch links: enabling a volume around an already-contained player can therefore produce a fresh
`StartTouch`. Removing it releases the existing touch pair before any late `EndTouch` callback can
be rejected on disabled or dead state.

The faithful runtime keeps the same physical invariant. Disabled, hidden, and dead state jointly
gate the brush body's collision. A begin is admitted by the class/filter test before it enters the
retained touch-pair set, so a rejected client cannot suppress a later valid edge. An end removes the
pair before liveness checks. This is also why `Enable` while overlapping produces one begin,
`Disable` clears it, and a later re-enable can produce another begin rather than being lost to
deduplication.

The remaining observed bits are **not activator classes**, and two of them are reinterpreted by a
single leaf:

| Bit | Owner | Meaning |
| --- | --- | --- |
| `0x2` | `CChangeLevel` | *also* `SF_CHANGELEVEL_NOTOUCH` — see "`trigger_changelevel` forced input" |
| `0x10` | `CBaseTrigger::InitTrigger` (`FUN_101c5080`, test at `0x101c52ef`) | keeps the brush drawn: without it, and with `showtriggers` at zero, `InitTrigger` sets `m_fEffects \|= EF_NODRAW (0x40)` |
| `0x20` | `CTriggerPlayerActivityLevel::EndTouch` (`FUN_102109b0`, test at `0x10210a0b`) | restore the player's activity and awareness levels on exit; without it the level set on entry sticks |
| `0x80` | `CTriggerLook::Touch` (`FUN_101c6cb0`, test at `0x101c6ee9`) | `SF_TRIGGERLOOK_FIREONCE` — after `OnTrigger`, `SetThink(SUB_Remove)` |

`trigger_once`'s self-removal is **not** a spawnflag. `CTriggerOnce::Spawn` (`FUN_101c6ad0`) sets
`m_flWait = -1.0f`, which routes `ActivateMultiTrigger` (`FUN_101c68e0`) into
`SetTouch(NULL); SetThink(SUB_Remove); m_flNextThink = curtime + 0.1`.
`CTriggerMultiple::MultiWaitOver` (`FUN_101c6a10`) is `SetThink(NULL)` and nothing else.

`sp_theatre`'s Embrace `trigger_once` (`spawnflags 17`) is the only trigger in the exported corpus
carrying `0x10`; since that bit only suppresses a debug-visibility `EF_NODRAW` on a brush with no
drawable surfaces, `spawnflags 17` behaves as `1`.

### The filter entity

`filtername` is resolved once in `CBaseTrigger::Activate` (`FUN_101c4d60`):

```c
if (m_iFilterName /*+0x560*/ != NULL) {
    ent = gEntList.FindEntityByName(NULL, m_iFilterName, NULL, NULL);   // FUN_100f7770
    if (ent) { m_hFilter /*+0x564*/ = ent->GetRefEHandle(); BaseClass::Activate(); return; }
    m_hFilter = INVALID_EHANDLE;
}
```

There is **no `dynamic_cast<CBaseFilter*>`** — whatever entity the name finds has its `vt+0x3c4`
called blind. The filter is consulted at the tail of `PassesTriggerFilters` (`0x101c54bf`), **only
after a class bit has already matched**, and its verdict is returned verbatim, so a filter can
reject but never admit. An unset or stale `m_hFilter` passes.

`CBaseFilter` (`filter_base`, ctor `FUN_10107670`, vftable `0x1045b1c4`, datamap `0x1056c1e8`) holds
`m_bNegated` `+0x450`, `m_OnPass` `+0x454`, `m_OnFail` `+0x46c`, and the input `TestActivator`
(`FUN_10107860`). The two filters the exported maps use:

- **`filter_activator_name`** (`CFilterName`, ctor `FUN_10107b90`, vftable `0x1045bae4`):
  `m_iFilterName` at `+0x484`, `PassesFilterImpl` = `FUN_10107c20` — pointer-identity fast path,
  then an empty filter matches only nameless entities, then a trailing `*` compares with
  `strnicmp(name, filter, len-1)`, else `stricmp`. The same rule as `FindEntityByName`
  ("Entity-name matching").
- **`filter_multi`** (`CFilterMultiple`, ctor `FUN_101078e0`, vftable `0x1045b654`): `FilterType`
  `+0x484` (`0` = AND, anything else = OR), five sub-filter names `+0x488`…`+0x498` resolved to
  EHANDLEs `+0x49c`…`+0x4ac` in `Activate` (`FUN_10107a30`); `PassesFilterImpl` = `FUN_10107aa0`.

Also present but unused by the exported maps: `filter_activator_class` (`FUN_10107d80`),
`filter_mass` (`FUN_10107ef0`), `filter_inventory` (`FUN_10108090`), `filter_feat` (`FUN_101082d0`).

### `trigger_multiple` / `trigger_once` edge and re-arm order

An accepted new contact enters `CBaseTrigger::StartTouch` first, which queues `OnStartTouch`, then
`CTriggerMultiple::MultiTouch` may activate the multiple and queue `OnTrigger`. Equal-time FIFO
therefore preserves producer order: `OnStartTouch` deliveries precede `OnTrigger` deliveries, while
the repeated rows *within* either output retain the reverse-row rule above. `EndTouch` releases the
retained pair before it queues `OnEndTouch`, so a kill, disable, or rejection cannot leave a stale
pair blocking the next valid begin.

`ActivateMultiTrigger` is a gate, not a backlog. With `wait > 0`, the first accepted activation
arms `MultiWaitOver` at `curtime + wait`; attempted **`OnTrigger` activations** during that window
are silently swallowed and are not retried when it re-arms. A genuinely new physical entry may
still have produced its edge-only `OnStartTouch` before reaching that wait gate. `wait == -1` (the
value forced by `trigger_once`) removes the touch
handler immediately and schedules removal at `curtime + 0.1`, so it can never **activate** again even
though its delayed output rows remain valid queue entries — but its edge outputs are unaffected for
the ~0.1 s it stays alive, which is the subject of the two subsections below. Enable/disable is
independent: disabling clears
contacts, and re-enabling while still geometrically contained can create a fresh begin.

### The touch dispatch path

The server-side touch dispatch is fully recovered in `vampire.dll`; only *pair selection* is
engine-owned. Two entry points hand pairs to the server: `CServerGameEnts::MarkEntitiesAsTouching`
(`FUN_1011be20`, reached only through the interface vtable slot at `0x1001017c` — it has no ordinary
call site in `vampire.dll`, which is exactly the `engine.dll` boundary), and
`CBaseEntity::PhysicsImpact` (`FUN_1003e6c0`, which rejects the pair when
`GetFlags(a) | GetFlags(b)` carries `0x4000000`). Both forward to
`CBaseEntity::PhysicsMarkEntitiesAsTouching` (`FUN_1003e2e0`), which calls
`CBaseEntity::PhysicsMarkEntityAsTouched` (`FUN_1003dc70`) once in each direction.
`CBaseEntity::PhysicsTouchTriggers` (`FUN_100a53d0`) only hands the collision object (`+0x2e0`) to
the engine partition interface at `vt+0x1e8`; which pairs come back, and in what order, stays below
that boundary.

`PhysicsMarkEntityAsTouched` is the entire begin decision:

- rejects self-touch, and either entity being the other's move parent (`+0x25c`);
- rejects when `GetFlags(a) | GetFlags(b)` carries `0x400000` (bit not otherwise named here);
- when **both** sides carry `FSOLID_TRIGGER` (`m_usSolidFlags` `+0x2b4` bit `0x8`), requires at least
  one of them to be solid (`m_nSolidType` `+0x2b0` non-zero and no `FSOLID_NOT_SOLID` bit `0x4`);
- if a touchlink for the pair already exists, it only refreshes the link stamp from `m_touchStamp`
  (`+0x1ac`) and calls `CBaseEntity::PhysicsTouch` (`FUN_1003daa0`) — an existing contact never
  produces a second begin;
- otherwise it allocates a link (pool ceiling `0x200`, `AllocTouchLink: MAX_TOUCHLINKS limit`) and,
  when the *toucher* is not itself a trigger, sets link flag bit `0x1` and calls
  `CBaseEntity::PhysicsStartTouch` (`FUN_1003db80`).

`PhysicsStartTouch` is two virtual calls behind one gate:

```c
if (other != NULL && !this->vt[0x1d0]() && !other->vt[0x1d0]()) {
    this->vt[0x2b8](other);   // StartTouch
    this->vt[0x2bc](other);   // Touch
}
```

`vt+0x1d0` (slot 116) is `FUN_10027490` = `m_iEFlags (+0x268) & EFL_KILLME (0x1)` —
`IsMarkedForDeletion`. `PhysicsTouch` applies the same two-sided gate and calls only `vt+0x2bc`.
For `CTriggerMultiple` (vftable `0x1047da24`) and `CTriggerOnce` (`0x1047dee4`) — both factories,
`FUN_101c66e0` and `FUN_101c6a30`, write the base vftable `0x1047d08c` first and then the leaf's —
those slots resolve to `CBaseTrigger::StartTouch` (`FUN_101c5590`), `CBaseEntity::Touch` (`FUN_100a4af0`) and
`CBaseTrigger::EndTouch` (`FUN_101c55d0`). **`CBaseEntity::Touch` is the only consumer of
`m_pfnTouch`** (`+0x1ec`): it calls the pointer when non-null, then forwards `Touch` to the move
parent (`+0x254`). `CTriggerMultiple::Spawn` (`FUN_101c6860`) installs `MultiTouch` there through the
thunk at `0x10015212` → `FUN_101c68b0`.

Ends run from `CBaseEntity::PhysicsCheckForEntityUntouch` (`FUN_1003d490`), which expires every link
whose stamp differs from the owner's `m_touchStamp`; a link stamped `0xffffffff` (written by
`PhysicsMarkEntitiesAsTouchingEventDriven`, `FUN_1003e4e0`) never expires and re-enters
`PhysicsTouch` instead. Expiry calls `PhysicsNotifyOtherOfUntouch` (`FUN_1003d640`) and then
`PhysicsRemoveToucher` (`FUN_1003d770`), and each fires `EndTouch` (`vt+0x2c0`) **only when the link
carries the `0x1` bit that `PhysicsMarkEntityAsTouched` set when it dispatched the begin** — ends are
paired to begins by construction, not by a liveness test. Neither function tests `EFL_KILLME`.

### `OnStartTouch` still fires inside the `wait == -1` removal window

`ActivateMultiTrigger` (`FUN_101c68e0`) writes exactly three things on the `wait == -1` branch:
`m_pfnTouch (+0x1ec) = NULL`, `m_flNextThink (+0x17c) = curtime + 0.1`, and the think pointer to
`SUB_Remove` (`FUN_101c0b10`). It does not change solidity, does not touch the touchlink list, and
does not set `m_iEFlags`. `EFL_KILLME` is set only inside `UTIL_Remove` (`FUN_101cd940` →
`0x101cd8c0`), which `SUB_Remove` reaches only when that think runs.

So for the ~0.1 s window the trigger is still a solid `FSOLID_TRIGGER` volume with a clean deletion
flag, still linked into the touch graph. A **new** contact therefore still allocates a link, still
passes the `IsMarkedForDeletion` gate in `PhysicsStartTouch`, and still reaches
`CBaseTrigger::StartTouch` — whose body is only `PassesTriggerFilters` (`FUN_101c5460`) and then
firing `m_OnStartTouch` (`+0x568`). **`CBaseTrigger::StartTouch` carries no wait, removal, enabled or
`m_pfnTouch` gate at all**; the activator-class/filter test is its sole admission check.
`CBaseTrigger::EndTouch` is the same two lines against `m_OnEndTouch` (`+0x580`).

What the null `m_pfnTouch` suppresses is the *second* call of the pair: `CBaseEntity::Touch` finds no
handler, so `CTriggerMultiple::MultiTouch` never runs and no `OnTrigger` is produced. `SetTouch(NULL)`
is an `OnTrigger` gate, not a touch gate. The faithful behaviour inside the window is therefore
`OnStartTouch` **yes**, `OnTrigger` **no**, `OnEndTouch` **yes** — ends behave exactly as on any other
frame, because nothing about the trigger's links or stamps changed, and because the untouch path has
no `EFL_KILLME` test even after the removal think has run.

The one asymmetry is destruction itself. `CBaseEntity::~CBaseEntity` (`FUN_1009df20`, trace string
`0x105554a0`) calls `CBaseEntity::PhysicsRemoveTouchedList` (`FUN_1003d8f0`), which calls
`PhysicsNotifyOtherOfUntouch` for every link — giving each *other* entity its `EndTouch` — and then
frees the link directly, **without** `PhysicsRemoveToucher`. The dying entity never receives its own
`EndTouch`, so a `trigger_once` that removes itself produces no final `OnEndTouch` for occupants
still standing inside it.

### New begins precede old ends within a frame

The `engine.dll` half is recovered. `PhysicsTouchTriggers` calls one `IVEngineServer` slot (122,
`+0x1e8`), whose wrapper enters the combined relink `FUN_20111f30` (`engine.dll`). That function first
arms the mover's untouch bookkeeping — `CBaseEntity::SetCheckUntouch(true)`, which bumps `m_touchStamp`
(`+0x1ac`) — then runs the spatial-partition enumeration and, for each returned pair, calls
`CServerGameEnts::MarkEntitiesAsTouching` **synchronously** before advancing to the next pair. Per pair
the two directions are marked `(element → mover)` first, then `(mover → element)`
(`PhysicsMarkEntitiesAsTouching` calls `PhysicsMarkEntityAsTouched(this, other)` then `(other, this)`).

So a **new** contact's `StartTouch` fires inside the move itself (`PhysicsMarkEntityAsTouched` →
`PhysicsStartTouch`), while a **stale** contact's `EndTouch` is **deferred**:
`PhysicsCheckForEntityUntouch` expires every link whose stamp no longer matches, and it is driven by an
`IGameSystem` `FrameUpdatePostEntityThink` hook that `CServerGameDLL::GameFrame` (`FUN_1011abc0`) runs
at **step 4** — after the entity-think pass, before `CEventQueue::ServiceEvents` at **step 6**. Both
edges queue their outputs before that same drain, so **within a frame the new `StartTouch` precedes the
old `EndTouch`**; with the queue's equal-time FIFO tie-break (see "Output-list and queue order") the end
edge is the last writer of any state the two handlers share.

The partition enumerates pairs in **spatial BSP-leaf order** with a per-query dedup token — not
entity-index, spawn, or name order. `CSpatialPartition::EnumerateElementsInBox` (`engine.dll
0x20042550`) delegates the leaf walk itself (vtable slot `+0x20`) to a handle-indexed hash/free-list
container built by `0x2000e690`, whose own slot `+0x20` in turn forwards to a caller-supplied
comparator object cached at construction (`+0x70`) rather than walking a literal BSP/kd-tree
in-place. The exact intra-leaf list order therefore depends on that comparator's own element
storage, two decompile layers past what this pass reached, and is not recovered; verifying it
needs three `trigger_multiple` volumes sharing one point, authored in different `.ents` orders across
map builds and walked into while capturing the `OnStartTouch` firing order.

**Deliberate divergence (owner-accepted):** Elysium reproduces this begin-before-end order and the
teleport deferral (see "`point_teleport`"), but within each phase it iterates by **ascending entity
index** as a deterministic substitute for retail's spatial enumeration. No authored map is known to
depend on the spatial order.

## Other brush-trigger leaves in the current exports

The whole-current-export delta from `sp_tutorial_1` adds seven trigger classnames. Their map-level
inventory and worked output chains are in `docs/vtmb/exported-map-event-surface.md`; the retail leaf
rules are recorded here.

### `trigger_teleport`

`CTriggerTeleport::Touch` (`FUN_101c92c0`, vftable `0x1047f694`) first applies
`PassesTriggerFilters`, then resolves its target. A missing target is a silent no-op. Without a
landmark it uses the target origin and angles; for a player/collision object it raises target Z by
`-collisionMins.z`, so the authored target denotes the foot point. It clears on-ground state and
passes no replacement velocity, preserving current velocity. A player controller also receives a
crossfade start time and the authored duration.

An optional landmark preserves the toucher's relative offset and rotates both angles and velocity
by the landmark-to-target yaw delta. Retail does no trace, clearance/headroom test, ground search,
depenetration, retry, delay or self-removal. The transform is immediate inside `Touch`; subsequent
old/new contact reconciliation is engine-owned. The three current instances author no landmark.

### `trigger_push`

`CTriggerPush::Spawn` (`FUN_101c8aa0`) derives direction from `angles` (all-zero defaults to
`0 180 0`), initializes the trigger and copies current `speed` to target speed. `Touch`
(`FUN_101c8c40`) rejects null, non-solid and trigger-solid touchers plus one excluded
collision/move-type path, then applies `PassesTriggerFilters`. Ordinary actors receive base
velocity in the push direction; an upward push clears on-ground and slightly lifts a grounded
actor. VPhysics bodies receive frame-time-scaled force. Spawnflag `0x80` adds one absolute-velocity
impulse and removes the trigger.

`accel` alone installs no think. `SetAcceleration` (`FUN_101c8a60`) only writes the field.
`SetSpeed` (`FUN_101c89c0`) writes a target speed and last game time, then arms
`CTriggerPushAccelThink` (`FUN_101c8b60`). The think clamps `dt`, converges current speed by
`accel * dt`, rethinks until exact and then clears itself. No current exported output resolves to
`trigger_push.SetSpeed`, so both current instances perform only collision-tick touch work.

### `trigger_player_activity_level`

The leaf's `supernatural_level`, `criminal_level` and `investigate_level` fields are `+0x598`,
`+0x59c`, `+0x5a0`, all default `-1`. `Touch` (`FUN_102108e0`) checks enabled state and a
player/controller-bearing toucher, but does **not** call `PassesTriggerFilters`. Every collision
touch refreshes each authored non-negative level. Supernatural and criminal pass duration `-1`;
their player setters derive a finite duration as `max(previous retained level, pl_min_act_timer)`,
raise but do not lower an existing non-zero level, and increment the channel's incident count.
Investigate uses its direct replacement setter. The detailed player-side timing belongs to
`player-entity.md`.

`EndTouch` (`FUN_102109b0`) uses exact-match release so it does not clear a value replaced by
another source. With spawnflag `0x20`, matching supernatural/criminal values return to zero;
matching investigate returns to zero regardless of that bit. The inherited edge path still owns
valid `OnStartTouch`/`OnEndTouch`. There is no leaf `OnTrigger`, output object, think, stack or
reference count; authored `OnTrigger` rows are discarded during keyvalue parsing.

### `trigger_discipline_context`

`StartTouch` (`FUN_10210dc0`) requires enabled state, a non-negative context, a non-null toucher and
its context-state object, then sets `1 << (context & 31)` **before** calling inherited
`CBaseTrigger::StartTouch`. The mutation is therefore not gated by the inherited class/filter test
that controls `OnStartTouch`. `EndTouch` (`FUN_10210e50`) clears the bit and then invokes the base
end path. Its `Touch` is empty: no per-tick refresh or think exists. The bit is not reference-counted,
so same-context overlapping volumes can clear one another early on exit.

### `trigger_checkvolume`

`CheckNow` (`FUN_101cb7d0`, vftable `0x10481c4c`) refuses disabled state, obtains the brush world
AABB, asks the engine partition for at most 2,048 candidates, applies `PassesTriggerFilters` to each
and fires `OnEntityInVolume` once per accepted candidate. It stores no occupancy set and performs
no deduplication, poll, wait or one-shot removal. Every explicit `CheckNow` repeats the query and can
refire for the same entity. The server evidence proves a broadphase AABB query; exact partition-side
containment remains an `engine.dll` boundary.

### `trigger_bomb_site`

`StartTouch` (`FUN_102110e0`) runs the inherited edge path, then stores this site on a toucher with
a player controller even if the inherited filter rejected its output edge. `EndTouch`
(`FUN_10211160`) clears the relationship only if it still names this site. Use query
(`FUN_10211280`) rejects disabled state, a missing/mismatched player relationship, or a different
live exclusive-use owner; no inventory guard is visible there.

Use begin (`FUN_102113c0`) claims the generic session, records `curtime` and marks the player busy.
The progress callback `FUN_10211480` completes at 100 percent or above. Completion
(`FUN_102116c0`) disables the trigger, releases owner/relationship state, requests removal of
`item_g_astrolite`, spawns/initializes an astrolite ahead of the player, then fires
`OnBombPlaced`. The output is completion-owned and normally one-shot through self-disable, but an
explicit re-enable permits another use/completion; there is no separate permanent latch. No entity
think is installed.

### `trigger_electric_bugaloo`

This occupied-use leaf stores itself on player-controller `+0x1cbc` after the inherited
`StartTouch`; the relationship write is not conditional on that inherited filter verdict.
`EndTouch` clears only a still-matching relationship. Use query (`FUN_10231520`) rejects disabled
state, a missing/mismatched relationship and a different live exclusive owner.

Use begin (`FUN_10231640`) fires generic `OnUseBegin`, claims ownership, marks the player busy and
records game time. Use end (`FUN_10231690`) clears busy/session state and fires generic
`OnUseEnd`; both outputs refire per accepted session. Owner-only collision `Touch`
(`FUN_102316d0`) also checks whether more than `300.0` game seconds have elapsed since begin. If so,
it calls one player routine and sets a per-entity byte that neither begin nor end resets. That
downstream action is once per entity lifetime and collision-touch-driven, not an entity think or
queued timer.

### `trigger_autosave` (`CTriggerSave`)

`LINK_ENTITY_TO_CLASS(trigger_autosave, CTriggerSave)` factory `0x101c9740`, vtable `0x1047fb54`,
base chain `CBaseToggle` → `CBaseTrigger` → `CTriggerSave`. The leaf overrides exactly `Spawn`
(`0x101c97d0`) and `Touch` (`0x101c9810`); it declares no datamap of its own and shares
`CBaseTrigger`'s `GetDataDescMap` byte-for-byte, so it is keyless and output-less — the 2013-SDK
`NewLevelUnit`/`MinimumHitPoints`/`DangerousTimer` fields and the "dangerous autosave" branch do not
exist in this build. `StartTouch`/`EndTouch` are inherited unchanged and unused; the autosave hangs
off `Touch`, which runs once per frame per persisting touch pair, not once per begin.

`Spawn` removes itself in deathmatch (`g_pGameRules->IsDeathmatch()`) and otherwise calls
`CBaseTrigger::InitTrigger`, which is also where the occupancy gate lives: a `m_bDisabled` trigger
never gets `FSOLID_TRIGGER`, so it never links and never touches. `Touch` is twelve instructions:
if the toucher's `CBaseEntity+0xA8` self-pointer is null (VtMB's replacement for a virtual
`IsPlayer()`, written only by `CBasePlayer::CBasePlayer`) it returns; otherwise it calls
`UTIL_Remove(this)` — Source's deferred, end-of-frame delete, not a synchronous unlink — then
`engine->ServerCommand("autosave\n")`. There is no `m_flWait`, rearm think, activator bookkeeping,
master or filter call, and no chain to `BaseClass::Touch`; a non-player toucher does nothing and
does not consume the trigger.

`IVEngineServer::ServerCommand` (`engine.dll` `CVEngineServer`, vtable slot 59) is stock Source:
it validates the trailing `\n`/`;` and calls `Cbuf_AddText`. **The save does not happen inline in
the touch** — `autosave\n` executes at the next `Cbuf_Execute` in the host frame loop, so the
trigger's observable effect is "a save happens later this frame or the next," not "during the
touch callback." The `autosave` `ConCommand` (`engine.dll 0x20099010`, `Host_AutoSave_f`) then
gates on, in order: `-buildpackfiles`/`-buildnodegraphs` on the command line; `autosave_on` (VtMB
cvar, default `1`); a minimum interval against engine realtime, `autosave_time_delay` (VtMB cvar,
default `300` seconds), skipped entirely for the session's first autosave; and
`CSaveRestore::IsValidSave()` — the game DLL's save-block reason code (below) is zero — together
with `gpGlobals`'s field at `+0x18`. **Neither original candidate for that field holds up under raw
disassembly**: it is not a copy of `maxClients` (`+0x14`, single writer in `SV_SpawnServer`, a
distinct field), and it is not a raw entity/edict-pool count either — `engine.dll`'s
`SV_SpawnServer` (`0x200f55f0`) computes it as `1 - (int)(-1.0 / x)` from a separate global float at
`0x20b42af4`, refreshed into `gpGlobals+0x18` every server frame (`0x200f7e40`) from a mirrored `sv`
field it also initializes. That arithmetic evaluates to `2` when `x == 1` and to `1` for any larger
integer `x`, so `gpGlobals+0x18 > 1` reduces to **"this session's underlying count is exactly one"**
— a single-player-shaped gate, not a pool-size or client-count comparison. The source global at
`0x20b42af4` sits immediately before the `mem_leakdelta` ConVar object in the engine's registration
table (best positional match: the tail field of `mp_logecho`'s object, though no write/registration
site was found to confirm that specific ConVar owns it) and carries no confirming symbol; naming it
precisely needs a debugger read of `dword [0x20b42af4]` in-map, not further static tracing.

On a D/E failure the request is **latched, not dropped**: `CSaveRestore::RequestAutosave()` sets
`m_bAutosavePending`, and `CSaveRestore::SetSaveBlocked(int n)` — the same call the game DLL makes
every player frame through `IVEngineServer` slot 5 — re-runs `Host_AutoSave_f` immediately the
instant a pending request's block clears to `0`. So a `trigger_autosave` whose save is refused
because the player is mid-something is not lost even though the trigger entity is already gone; the
retry lives entirely in the save service, driven by the unblock write itself rather than polling or
a timer.

The reason code is `CBasePlayer::GetSaveBlockedReason` (`0x10174f80`, called from `CBasePlayer`
vtable slot 444 / `+0x6F0`, a per-frame player update), returning `0` for savable and a nonzero code
otherwise. Cross-referencing its tested byte offsets against the recovered `CBasePlayer` datamap
(`0x10580edc`, 157 records) and decompiling its four predicate callees narrows most codes past the
original shape-only guesses:

- **Codes 6 and 7 are the same relationship, split by state.** `+0x1db0` is an `EHANDLE` the
  `PostThink` common tail also touches (see "Recovered `PostThink` body" in `docs/vtmb/player-entity.md`);
  code **7**'s predicate (`0x10175180`) resolves it and returns true only when the target's own
  vtable slot `+0x228` getter equals exactly `3`, while code **6**'s `0x101618a0` resolves the same
  handle and returns true whenever it is merely valid, regardless of state — so **7 fires for that
  relationship in state 3 specifically, and 6 covers it in every other live state.** Code 6 also
  covers an active camera relationship directly: `+0x19C0`/`+0x19CC` are the datamap-confirmed
  `m_hCameraViewEntity`/`m_hCameraTargetEntity`, and its third predicate (`0x1017cf90`) requires a
  positive int at the adjacent `+0x1ec4` together with a valid `EHANDLE` at `+0x19b4` — a related
  camera-cluster field one datamap record short of a confirmed name.
- **Code 1**'s second predicate (`0x1023bd00`) simply returns a global pointer (`0x107532e8`) whose
  sole writer (`0x1023c020`) is a `worldspawn`-shaped initializer (creates the soundent, reads the
  `message`/`chaptertitle`/`startdark` keyfields at `param_1+0x114/0x115/0x11e`) — so the global is
  the world entity singleton, and code 1 also fires whenever that entity's `+0x4AC` field is
  nonzero; the field itself is not further named.
- **Codes 2, 3, 4, and code 1's `+0x1EB8` test have no datamap record anywhere in `CBasePlayer`'s
  base chain.** `+0xFE8`, `+0x153C`/`+0x1538`, `+0x1040`, and `+0x1EB8` were checked against every
  `typedescription_t` in the complete walked chain — `CBasePlayer` (157 records), `CBaseCombatCharacter`
  (305), `CBaseFlex` (3), `CBaseAnimatingOverlay` (70), `CBaseAnimating` (43), `CBaseToggle` (38),
  and the root `CBaseEntity` (110, `base=0`, chain terminates) — and none of the four offsets appear
  in any of them. This is a terminal result, not an unchased lead: datamap coverage is not
  universal, and these four are ordinary runtime `EHANDLE`/int fields `GetSaveBlockedReason` reads
  directly without ever exposing them to Hammer or save/restore. Naming what they represent
  semantically would need a logged walk of `0x10174f80`'s return across each candidate player state
  (idle, dialogue, terminal, feeding, climbing, cutscene, dead), same as the final `vtbl[+0x278]()`
  check (HYPOTHESIS `IsAlive()`, gating `0`/`5`), which this pass did not re-examine.

On success, `CSaveRestore::SaveGameSlot("autosave", <GetSaveComment>, "Autosave")` writes
`SAVE/autosave<NN>.sav` plus the map's `SAVE/*.HL1` state files; the comment is
`"<map display name> <elapsed h:mm>"`. `GameHeader.userName` is a VtMB addition absent from stock
Source's `GAME_HEADER` — the autosave writes the literal `"Autosave"`, which the load-game UI shows
as the slot label. `g_ClientDLL->+0xAC` receives ids `0x4F` before the write and `0x36` after a
successful one (hypothesised as the on-screen saving indicator's show/hide; `client.dll`'s
`IBaseClientDLL` vtable slot 43 would confirm statically, and was not decompiled in this pass).

Because `sp_tutorial_1`'s zero-offset landing spot puts the initial standing hull inside the map's
one `trigger_autosave` (`docs/vtmb/game_runtime.md` → "The tutorial"), and the session's first
autosave bypasses the minimum-interval guard, this trigger normally fires the very first autosave
of the game the moment the player is not in a save-blocked state.

## `logic_relay` spawnflags and refire

`CLogicRelay`'s datamap is at `0x10579058` (records `0x1057909c`, 8 fields, builder `FUN_10136330`);
the factory is `0x10136260` and the vftable `0x10469f94`. All the behaviour is in
`CLogicRelay::InputTrigger` (`FUN_101364e0`):

```c
if (m_bDisabled /*+0x450*/ == 0 && m_bWaitForRefire /*+0x451*/ == 0) {
    m_OnTrigger /*+0x454*/ .FireOutput(inputdata.pActivator, this, 0.0f);
    if (m_spawnflags & 1) { UTIL_Remove(this); return; }
    if (!(m_spawnflags & 2)) {
        m_bWaitForRefire = true;
        g_EventQueue.AddEvent(this, "EnableRefire",
                              m_OnTrigger.GetMaxDelay() + 0.001, this, this, 0);
    }
}
```

| Bit | Meaning |
| --- | --- |
| `0x1` | REMOVE_ON_FIRE — outputs fire, then `UTIL_Remove` (`0x101cd940` → `0x101cd8c0`: sets `EFL_KILLME`, stops thinking, queues destruction). The relay can never fire again. |
| `0x2` | ALLOW_FAST_RETRIGGER — skips the lockout below |
| `0x4` and above | **inert** |

Without `0x2` a fired relay sets `m_bWaitForRefire` and queues `EnableRefire` **at itself**, delayed
by its own longest authored `OnTrigger` delay plus `0.001` (`0x1044f020`), so it cannot re-enter until
its last delayed output has gone out. The guard order is `m_bDisabled` first, then `m_bWaitForRefire`;
either silently swallows the input. The activator is taken verbatim from `inputdata.pActivator` and
`pCaller` is the relay itself.

That lock is an intentional blocker, not a deferred retry. It also breaks an otherwise immediate
self-cycle unless spawnflag `0x2` is set. A fast-retrigger relay or an ordinary zero-delay cycle has
no retail event-budget protection and can monopolize the queue service pass; see
`docs/vtmb/game_runtime.md`.

Unlike `CBaseButton`, this class does **not** diverge from stock Source. The higher bits are a proven
negative, from three exhaustive checks: an operand scan over all 942,828 disassembled instructions
finds exactly one `+0x204` read anywhere in the class's code cluster (`0x10135e00`–`0x10136900`), the
one in `InputTrigger`; all 241 vftable slots resolved through their thunks yield only a debug
text-overlay printer that displays `m_spawnflags` without testing it; and all 15 inputs (5 own plus 10
inherited from `CBaseEntity`) lead back to `InputTrigger` as the sole reader.

Corpus values: `0` or absent (249), `1` (39), `8` (1), `49` (1). `8` therefore behaves exactly like
`0`, and `49` (`0x1|0x10|0x20`) exactly like `1`. `0x2` never occurs.

Other members: `m_bDisabled` `+0x450` is the `StartDisabled` keyfield, written directly at parse time —
there is no `Spawn`/`Activate` override, so it needs no spawn-time application. `m_bWaitForRefire`
`+0x451` is saved but not keyable.

## `prop_dynamic` animation (`CDynamicProp`)

The datamap is at `0x1058d4f8` — half-static, with `dataDesc` (`0x1058d53c`) and `numFields` (12)
written by the builder `FUN_101901ae`. Its own records:

| externalName | internal | offset | notes |
| --- | --- | --- | --- |
| `RandomAnimation` | `m_bRandomAnimator` | `+0x7c4` | |
| — | `m_flNextRandAnim` | `+0x7c8` | save-only |
| `MinAnimTime` | `m_flMinRandAnimTime` | `+0x7cc` | |
| `MaxAnimTime` | `m_flMaxRandAnimTime` | `+0x7d0` | |
| `LoopSequence` | `m_iszSequenceName` | `+0x7d4` | the authored resting animation |
| `SetAnimation` | `InputSetAnimation` | — | `0x100019ec` → `FUN_10190a00` |
| `SetSkin` | `InputSetSkin` | — | `0x10014646` → `0x10190810` |
| `OnAnimationBegun` | `m_pOutputAnimBegun` | `+0x77c` | |
| `OnAnimationDone` | `m_pOutputAnimOver` | `+0x794` | |
| `OnAnimationLoop` | `m_pOutputAnimLoop` | `+0x7ac` | |

**There is no `demo_sequence`.** The string appears nowhere in `vampire.dll` or `client.dll`
(case-insensitive), so it is a Hammer/FGD-only field the engine never reads; `LoopSequence` is the
only authored default, and it occurs exactly once in the image, in this datamap's string block at
`0x1054eadc`. `demo_sequence` is nonetheless stamped on map entities — every cinematic prop in
`sp_theatre` carries one. Across the current 22 maps it is present **1,619** times: 1,594 empty or
`None` sentinels and 25 apparent labels, all ignored. A separate literal `m_iszPreIdle` occurs once
on a `scripted_sequence`; that spelling is likewise ignored because the datamap exposes only
external `m_iszIdle` for its internal `m_iszPreIdle` member.

### The resting pose

A `prop_dynamic` at rest is a **held pose, not a playing clip**. `CBaseProp::Spawn`
(`FUN_1018df70`) leaves the cycle and the playback rate at zero and picks the sequence by activity:

```c
SetModel(szModel);  SetMoveType(0,0);  m_takedamage = 0;
m_flNextThink /*+0x17c*/ = 0;                       // "think never"
m_flPlaybackRate /*+0x6f4*/ = 0;  m_flCycle /*+0x6f8*/ = 0;
m_nSequence /*+0x6f0*/ = SelectWeightedSequence(ACT_IDLE /*1*/, -1);   // FUN_1008dc40
if (m_nSequence < 0) m_nSequence = 0;               // fall back to the first sequence
```

`ACT_IDLE` is activity `1`, from the registration table at `0x104126e3` (`ACT_WALK` is 9, `ACT_RUN`
`0x13`). `ResetSequenceInfo` (`FUN_10090950`) is the only thing that ever raises
`m_flPlaybackRate` to `1.0`, and nothing calls it for a prop with neither a `LoopSequence` nor a
scripted `SetAnimation` — so such a prop stands frozen on frame 0 of its idle sequence for the
whole map. `ResetSequenceInfo` also contains the only literal sequence-0 default,
`if (m_nSequence == -1) m_nSequence = 0;`.

Most cinematic props miss the activity lookup and land on the fallback: they tag their idle
`ACT_VM_IDLE` rather than `ACT_IDLE`, and `cin_wineglass` carries no idle at all, so its rest pose
is frame 0 of `wineglass_1` — the first frame of the clip a script later plays.

### `Activate` — where `LoopSequence` starts

`CDynamicProp::Spawn` (`FUN_101905e0`) only writes `m_iGoalSequence /*+0x7d8*/ = -1` and, **when
`m_bRandomAnimator` is set**, arms `CDynamicPropAnimThink` at
`m_flNextRandAnim = curtime + RandomFloat(min, max)`, `m_flNextThink = m_flNextRandAnim + 0.1`. It
never reads `m_iszSequenceName`. Its tail is a virtual call to `CDynamicProp::CreateVPhysics`
(vtable `+0x37c` → `FUN_101907e0`), which is how a prop gets collision — see
`phy_vphysics.md` → "Which entities get a collision model".

The authored loop is resolved one phase later, in **`CDynamicProp::Activate` (`FUN_101906c0`)**:

```c
BaseClass::Activate();
m_iGoalSequence /*+0x7d8*/ = LookupSequence(m_iszSequenceName /*+0x7d4*/);   // FUN_1008f7b0
if (m_iGoalSequence >= 0) {
    SetThink(FUN_10190750);
    m_flNextThink = curtime + RandomFloat(0.1f, 0.99f);   // per-prop stagger
}
```

**The comparison is against −1, not zero** — `ACTIVITY_NOT_AVAILABLE`, so a clip at sequence
index 0 does arm:

```
101906da  CALL 0x1000e057               ; CBaseAnimating::LookupSequence
101906df  CMP  EAX,-0x1
101906e2  MOV  dword ptr [ESI + 0x7d8],EAX
101906e8  JLE  0x1019072a               ; skip only when the result is <= -1
```

That distinction decides whether the commonest animated props move at all. `bats_smaller`'s
authored `fly` and the static `stage_light`'s `idle` are sequence 0 and therefore do arm. The six
`palmtree` entities are different: their `LoopSequence` says **`palmtree_idle`**, while the model's
only 61-frame sequence is **`idle`**. `LookupSequence` returns −1, so `Activate` never arms their
think; they remain on the frame-0 pose selected during spawn. `InputSetAnimation` carries the same
`CMP EAX,-0x1` / `JLE` pair at `0x10190a2f`.

and that one-shot think (`FUN_10190750`) starts it:

```c
if (m_iGoalSequence >= 0) {
    m_nSequence = m_iGoalSequence;  ResetSequenceInfo();  ResetClientsideFrame();
    m_pOutputAnimBegun /*+0x77c*/ .FireOutput();
    SetThink(CDynamicPropAnimThink);  m_flNextThink = curtime + 0.1;
}
```

So `OnAnimationBegun` fires for the resting loop as well as for `SetAnimation`, and the loop starts
0.1–0.99 s after level load, staggered per prop, then ticks at 10 Hz for the rest of the map.

### Who advances the cycle

The **server** does, in `CBaseAnimating::StudioFrameAdvance` (`FUN_1008f120`), called from the anim
think through vtable `+0x3e8`. It advances `m_flCycle` by
`GetSequenceCycleRate(m_nSequence) * m_flPlaybackRate * dt`, clamps a non-looping sequence at `1.0`
and wraps a looping one, and sets `m_bSequenceFinished` in **both** branches.

`m_flCycle` (`+0x6f8`), `m_flPlaybackRate` (`+0x6f4`) and `m_nSequence` (`+0x6f0`) are
`DT_BaseAnimating` SendProps, received client-side at `+0x648`, `+0x640` and `+0x63c` and
interpolated. Client-side *advance* is a separate mechanism gated on `m_bClientSideAnimation`
(server `+0x70c`, client `+0x6a8`): `C_BaseAnimating::OnDataChanged` (`FUN_10094b20`) only calls
`SetNextClientThink(CLIENT_THINK_ALWAYS)` when that flag is set, and no code in the prop cluster
`0x1018d000`–`0x10191000` ever sets it. **A prop whose server think is disarmed does not animate.**

`CDynamicPropAnimThink` (`FUN_10190850`, think record `0x1058d6f4` → `LAB_1000d2ce`):

```c
if (!m_bRandomAnimator || curtime <= m_flNextRandAnim) {
    // a finished one-shot returns to the authored loop
    if (loopIdx >= 0 && m_bSequenceFinished /*+0x65c*/ && !m_bSequenceLoops /*+0x65d*/) {
        m_nSequence = loopIdx; ResetSequenceInfo();
    }
} else {
    m_nSequence = SelectWeightedSequence(ACT_IDLE, -1);   // FUN_1008dc40, same call as spawn
    ResetSequenceInfo();  ResetClientsideFrame();
    m_pOutputAnimBegun.FireOutput();
    m_flNextRandAnim = curtime + RandomFloat(m_flMinRandAnimTime, m_flMaxRandAnimTime);
}
StudioFrameAdvance();
if (m_bSequenceFinished) {
    if (!m_bSequenceLoops) {
        m_pOutputAnimOver.FireOutput();
        if (!m_bRandomAnimator) return;            // leaves the think disarmed
        m_flNextThink = m_flNextRandAnim + 0.1; return;
    }
    m_pOutputAnimLoop.FireOutput();
}
m_flNextThink = curtime + 0.1;                     // 0.1 is `_DAT_104493d0`
```

The random animator re-picks by activity, not arbitrarily — it is the same
`SelectWeightedSequence(ACT_IDLE, -1)` the spawn path uses.

**The revert branch is unreachable in shipped data, and the rebuild reproduces that.**
`CBaseEntity::PhysicsRunSpecificThink`
(`FUN_10033de0`) zeroes `m_flNextThink` before every dispatch, so a think that returns without
rewriting it is disarmed permanently. With `m_bRandomAnimator == 0` — true for all 749 entities that
carry the key — the think returns at `0x10190935` on the frame the one-shot finishes, so it is never
called again and the top-of-think revert never runs. A `SetAnimation` one-shot therefore **holds its
final frame**: for a prop with no `LoopSequence` there is no fallback to a rest sequence, and for a
prop with one, the `LoopSequence` only ever plays *before* the first `SetAnimation`.

`InputSetAnimation` (`FUN_10190a00`) resolves the clip name, sets `m_nSequence`, zeroes
`m_flCycle`, calls `ResetSequenceInfo` and `ResetClientsideFrame`, fires `m_pOutputAnimBegun`, and
re-arms the think at `curtime + 0.1`. It does **not** force non-looping: `ResetSequenceInfo` derives
`m_bSequenceLoops /*+0x65d*/ = GetSequenceFlags(m_nSequence) & 1`, so playback honours the model's
own `STUDIO_LOOPING` bit. On a miss it logs `"Dynamic prop no sequence named:%s\n"` and sets
`m_nSequence = 0` — but calls no `ResetSequenceInfo`, does not zero the cycle and does not arm the
think, leaving `m_bSequenceLoops`, `m_flPlaybackRate` and `m_flCycle` stale from whatever was
playing.

Member offsets on the server `CBaseAnimating`: `m_nSequence` `+0x6f0`, `m_flPlaybackRate` `+0x6f4`,
`m_flCycle` `+0x6f8`, `m_bSequenceFinished` `+0x65c`, `m_bSequenceLoops` `+0x65d`,
`m_bClientSideAnimation` `+0x70c`, `m_flNextThink` `+0x17c`.

Corpus: `RandomAnimation` is `0` on all 749 entities that carry it, so the random animator never
engages in shipped data. `LoopSequence` is authored on 64 (`idle` ×54, `palmtree_idle` ×6, `fly`
×2, `only_sequence`, `running`). The character manifest resolves 55 directly; three more `idle`
requests are the deliberately unbaked single-frame `stage_light`, `lampfloor` and
`junkyardcraneb`; the six `palmtree_idle` requests miss as described above. All **18** current-map
`SetAnimation` wires resolve to an exact target-model clip. `OnAnimationBegun` /
`OnAnimationDone` / `OnAnimationLoop` are wired **zero** times.

### Class chain and the complete I/O surface

`CDynamicProp` `0x1058d4f8` → `CBreakableProp` `0x1058d1c8` → `CBaseAnimating` `0x1054cd70` →
**`CBaseToggle` `0x1059b6f0`** → `CBaseEntity` `0x10552e18`. Record arrays and counts:
`0x1058d53c`/12, `0x1058d20c`/17, `0x1054cdb4`/43, `0x1059b734`/38, `0x10552e5c`/110.
`CPhysicsProp` (`0x1058d864`) and `CBreakable` (`0x1056e590`) are **siblings**, not ancestors — an
input declared only there does not reach a `prop_dynamic`.

`CBaseToggle` in the chain means **every animating entity is a mover**; the inherited keyfields and
move inputs are documented in `animation_and_movers.md` → Part B.

Outputs reaching `prop_dynamic` (9):

| output | member | offset | declared by |
| --- | --- | --- | --- |
| `OnAnimationBegun` | `m_pOutputAnimBegun` | `+0x77c` | `CDynamicProp` |
| `OnAnimationDone` | `m_pOutputAnimOver` | `+0x794` | `CDynamicProp` |
| `OnAnimationLoop` | `m_pOutputAnimLoop` | `+0x7ac` | `CDynamicProp` |
| `OnBreak` | `m_OnBreak` | `+0x730` | `CBreakableProp` |
| `OnHealthChanged` | `m_OnHealthChanged` | `+0x748` | `CBreakableProp` |
| `OnLinearMoveDone` | `m_OnLinearMoveDone` | `+0x4a4` | `CBaseToggle` |
| `OnAngularMoveDone` | `m_OnAngularMoveDone` | `+0x4bc` | `CBaseToggle` |
| `OnUseBegin` | `m_OnUseBegin` | `+0x5c` | `CBaseEntity` |
| `OnUseEnd` | `m_OnUseEnd` | `+0x74` | `CBaseEntity` |

`CBaseAnimating` contributes no outputs. Inputs reaching `prop_dynamic` (25): `SetAnimation`,
`SetSkin` (`CDynamicProp`); `Break`, `SetHealth`, `AddHealth`, `RemoveHealth`, `FadeOutKill`,
`SetDebris`, `physdamagescale` (`CBreakableProp`); `SetBodygroup`, `SetSkinFadeTime`, `FadeToSkin`,
`SpawnTempParticle`, `skin` (`CBaseAnimating`); `MoveToDest`, `MoveToHome`, `RotateToDest`,
`RotateToHome` (`CBaseToggle`); `Kill`, `Use`, `Alpha`, `Color`, `SetParent`, `ClearParent`,
`ScriptHide`, `ScriptUnhide`, `SetSoundOverrideEnt`, `SetFakeSilence` (`CBaseEntity`).

`OnHealthChanged` fires from `CBreakableProp::OnTakeDamage` (`FUN_1018f400`) on **every** accepted
damage call — there is no threshold — and from `InputSetHealth` / `InputAddHealth` /
`InputRemoveHealth` (`FUN_1018f960` / `FUN_1018f750` / `FUN_1018f7f0`), which fall through to
`Break()` at zero. The payload is variant type 4, `FIELD_INTEGER`: the raw health value, **not**
stock Source's 0..1 ratio.

`OnTrigger0`..`OnTrigger7` are **not** in this chain. They belong to `CPropHacking`
(`docs/vtmb/computer-terminals.md`), and
`CSceneEntity` separately declares `OnTrigger1`..`OnTrigger4`; a wire naming one on a `prop_dynamic`
cannot connect and is dead in retail. `SetCausesImpactDamage` is likewise a `CPhysicsProp` input
(record `0x1058d8d8` → `FUN_10191720`, which sets spawnflag bit `0x2` on `m_spawnflags` rather than
a dedicated member), so it too is inert on a `prop_dynamic`.

## `prop_switch` (`CPropSwitch`)

Datamap `0x105aa864`, records `0x105aa8ac`, 16, builder `FUN_1020d1b0`; base map `CBaseAnimating`.
Factory `FUN_1020d0e0`, ctor `FUN_1020d5f0`, vftable `0x104862fc`, instance size `0x7b8`.

| externalName | internal | offset | notes |
| --- | --- | --- | --- |
| — | `m_bLocked` | `+0x7a4` | save-only |
| — | `m_bActivated` | `+0x7a5` | save-only |
| `linkedswitch` | `m_sLinkedSwitch` | `+0x730` | targetname of a second `prop_switch` mirrored on every state change; never authored in the exported corpus |
| `use_icon` | `m_UseIcon` | `+0x734` | |
| `locked_icon` | `m_LockedIcon` | `+0x738` | |
| `reset_state` | `m_nResetState` | `+0x73c` | `0` keep, `1` force deactivated, `2` force activated, applied by the reset hook (vtable `+0x208`, `FUN_1020e100`) |
| `OnActivate` | `m_OnActivate` | `+0x744` | fires when the `activate` **sequence finishes** |
| `OnDeactivate` | `m_OnDeactivate` | `+0x75c` | fires when the `deactivate` sequence finishes |
| `OnUse` | `m_OnUse` | `+0x774` | fires immediately on an unlocked `+use` |
| `OnLockedUse` | `m_OnLockedUse` | `+0x78c` | fires instead of everything else when locked |
| `Toggle` | `InputToggle` | — | `0x1020db00` → `Use(activator, caller, USE_TOGGLE, 0)` |
| `Lock` | `InputLock` | — | `0x1020e060` |
| `Unlock` | `InputUnlock` | — | `0x1020e080` |
| `Activate` | `InputActivate` | — | `0x1020e0a0`, no-op when already activated |
| `Deactivate` | `InputDeactivate` | — | `0x1020e0d0`, no-op when already deactivated |
| — | `CPropSwitchSwitchThink` | — | think func, body `0x1020dd70` |

**A switch is a sequence player, not a mover.** `CPropSwitch::Activate` (`FUN_1020d9c0`) resolves the
linked switch — which must also be classname `prop_switch`, checked by `_stricmp` plus an RTTI cast —
then looks up four sequences by name: **`activate`, `deactivate`, `idle_on`, `idle_off`**, holding the
matching idle at spawn (resolved indices at `+0x7a8`/`+0x7ac`/`+0x7b0`/`+0x7b4`). `SetState`
(`FUN_1020dce0`) plays the transition clip and emits the `soundgroup` event `on` or `off`; the think
(`FUN_1020dd70`) waits for that clip to end, writes `m_nSkin` (`+0x670`) to `1`/`0`, fires
`OnActivate`/`OnDeactivate`, then loops `idle_on`/`idle_off`. Nothing in the class touches the
inherited `CBaseToggle` mover — a `prop_switch` animates only through its own `.mdl` clips.

`Use` (`FUN_1020db30`, vtable `+0x2b4`): locked fires `OnLockedUse` and stops; otherwise `OnUse`,
then its own state flips, then `linkedswitch` is set to the same value. `GetUseIcon`
(`FUN_1020dfc0`) returns `locked_icon` while locked. Spawn (`FUN_1020d850`) spawnflags: `0x2000`
start activated (skin 1), `0x4000` start locked, `0x8000` skip the `SOLID_BBOX` model-bbox
collision setup.

## `func_door` / `func_door_rotating` locked I/O

`func_door` is `CBaseDoor` and `func_door_rotating` is `CRotDoor` (which does not override `Use`, so a
rotating door runs `CBaseDoor::Use`). The door **motion** cycle and spawnflags are owned by
`docs/vtmb/animation_and_movers.md`; the I/O surface under lock is:

- **`CBaseDoor` has no `OnLockedUse` output.** That output belongs only to `prop_switch` (and
  `prop_button` carries the analogous `OnPressedLocked`). A locked door fires **no** output in response
  to a refused use — there is no locked-use output to wire.
- A direct **`Open`** input on a locked door refuses **silently**: no sound, no output, no motion.
  (`InputOpen` gates on `!IsDoorLocked(activator)`; that test is doorknob- and NPC-aware, not the raw
  `m_bLocked` byte. An already-open door refuses `Open` silently too.)
- A **`+use`** on a locked door plays **only** the locked sound (`CBaseDoor::Use` →
  `PlayDoorSound("locked")` plus the locked noise); no output, no motion.
- **`Toggle`** on a locked door does **nothing** (`InputToggle` also gates on `!IsDoorLocked`).
- **`Close`** has **no lock check**: a direct `Close` input closes a locked door. `InputClose` only
  refuses when the door is already `TS_AT_BOTTOM`.

`Lock` / `Unlock` write `m_bLocked` with no state guard and fire no output. `Open` and `Close` each
fire their `OnOpen` / `OnClose` twice on an admitted edge — once at the input handler, once again inside
the motion start.

### The lock authority is the knob's, not the door's [VtMB — decompiled]

`IsDoorLocked` is `CBaseDoor::IsUseRefused` (`FUN_100eec70`), and the door's own `m_bLocked` byte is
only the **last** of its four answers:

```c
if (NoOpenWantedRefusal(this))                        return true;   // FUN_100eef10
if ((m_spawnflags & 0x200) && pUser && pUser->+0x94)  return true;   // NONPCS: the user is an NPC
h = GetNearestDoorknob(this, pUser);                                 // FUN_100ee950
if (h) return IsKnobLocked(h);          // knob->+0x780 < 3 — the KNOB decides
return m_bLocked;                       // the door's byte only when there is NO knob
```

So a `func_door` whose spawnflags carry no `LOCKED` bit is still refused when its knob is locked, and
a door that *does* carry `LOCKED` is still admitted when its knob has been picked or keyed open. The
two states are independent and are never reconciled: `Lock`/`Unlock` on the door write only the door,
`Lock`/`Unlock` on the knob write only the knob (`FUN_10224190` / `FUN_10224950`). Map data is authored
for this — `sm_warehouse_1`'s `train_door1` carries `LOCKED` *and* a padlock, and wires
`train_padlock.OnSkillSuccess → train_door1.Unlock()` so that picking the padlock clears the door byte
before the padlock deletes itself.

**`GetNearestDoorknob` (`FUN_100ee950`)** picks between the two knob handles `+0x628` / `+0x62c`:

- distance is **Manhattan** — the per-axis absolute differences of `WorldSpaceCenter()` (vtable
  `+0x304`) summed, not a Euclidean length;
- the comparison is `<=`, so an exact draw goes to the **second** handle (`+0x62c`);
- with only one valid handle, that one wins by default;
- **a null user returns null**, before the handles are read at all. This is load-bearing: an I/O- or
  script-fired `Open`/`Toggle` carries no user, so it never consults a knob and falls straight through
  to `m_bLocked`. The "user" is not the activator entity either — the input handlers pass
  `activator->+0x9c` and `DoorknobUse` passes `+0xa8`, both null for anything that is not a character,
  so a relay or trigger propagating itself as activator is also "no user".

`CBaseDoor::DoorknobUse` (`FUN_100eef50`, the datamap's `CBaseDoorDoorknobUse`) is the `+use` half:
it requires `activator->+0xa8` or `DevMsg`s *"Non player entity %s trying to use…"*, then opens when
the nearest knob is unlocked, and otherwise plays the knob's refuse pose (`+0x444`), the `locked`
sound, and a `CSoundEnt::InsertSound(type 4, …)` stimulus — louder and longer when the user is
player-controlled.

## The lockable family (`CBaseLockableEnt` / `CBaseVampireSkillEntity`)

`prop_doorknob`, `prop_doorknob_electronic`, `prop_padlock` and `item_container_lock` are four
factories over **one** datamap pair. There is no `CPropDoorknob` datamap.

`CBaseVampireSkillEntity` — datamap `0x105aa150`, records `0x105aa194`, 12, builder `FUN_1020a7d0`,
vftable `0x104858cc`:

| externalName | internal | offset | notes |
| --- | --- | --- | --- |
| `difficulty` | `m_nSkillDifficulty` | `+0x77c` | read through `GetDifficulty` (vtable `+0x430`, `FUN_1020b1c0`); the deterministic feat threshold |
| `skilltype` | `m_vSkillType` | `+0x784` | selects the skill registry entry |
| — | `m_LastRoll` | `+0x780` | `FIELD_EMBEDDED` (td `0x105a1668`); its first int is the result tier **and** the lock state |
| — | `m_flLastAttempt` | `+0x788` | `FIELD_TIME` |
| — | `m_nSkillAttempts` | `+0x78c` | increments on every resolved outcome; the HUD uses zero/non-zero to distinguish not-attempted from failed |
| — | `m_nLastSkillLevel` | `+0x790` | rating recorded at use exit; a later higher rating resets the attempt counter on approach |
| `ResetDifficulty` | `InputResetDifficulty` | — | `0x1020abc0`, clamps the payload to 0..10 then calls the reset virtual `+0x440` |
| `OnSkillSuccess` | `m_OnSkillSuccess` | `+0x794` | |
| `OnSkillFail` | `m_OnSkillFail` | `+0x7ac` | |
| `OnSkillBotch` | `m_OnSkillBotch` | `+0x7c4` | |
| `OnSkillAttemptBegin` | `m_OnSkillAttemptBegin` | `+0x7dc` | |
| `OnSkillAttemptCycle` | `m_OnSkillAttemptCycle` | `+0x7f4` | |

Despite the field and helper names, the normal skill-entity path is **not a dice roll**.
`FUN_1020b090` calls the shared Intrusion/Hacking check helper with its `doRoll` argument hard-coded
to zero. That branch compares the current feat rating to the entity's authored `difficulty` and
returns the generic result tier `3` for pass or `1` for fail:

```c
m_flLastAttempt = curtime;
if (m_vSkillType == 1)      rating = FeatValue(Intrusion); // registry id 0
else if (m_vSkillType == 2) rating = FeatValue(Hacking);   // registry id 2
if (m_vSkillType == 1 || m_vSkillType == 2)
    m_LastRoll = (rating >= difficulty) ? 3 : 1;
// another skilltype leaves m_LastRoll unchanged
FireOutput(m_OnSkillAttemptCycle, player);
if (m_LastRoll > 2)       vt[0x438](player); // m_nSkillAttempts++, OnSkillSuccess
else if (m_LastRoll == 0) vt[0x434](player); // generic botch branch
else                      vt[0x43c](player); // m_nSkillAttempts++, OnSkillFail
```

The retained generic thresholds explain the `OnSkillBotch` surface, but this caller can emit only
`1` or `3`; botch is unreachable for a normal `skilltype` 1/2 attempt. No RNG table, botch table or
authored `diceroll` value participates. `IsLocked()` (`FUN_10224100`) is literally
`m_LastRoll < 3`; `Lock` writes `1` and `Unlock` writes `3`.

The cycle duration is exact: `(5.0 - rating * 0.25) / player[+0x1488]` seconds
(`FUN_1020aea0`). The think computes `remaining = max(duration + m_flLastAttempt - curtime, 0)` and
publishes `clamp(100 - remaining / duration * 100, 0, 100)` as an integer progress byte. A use
session resolves **one** attempt when remaining reaches zero. Failure ends the current use session;
retry requires another accepted `+use` and another full timed cycle. `ResetDifficulty` resets
`m_nSkillAttempts`, and approach also resets it when the current rating is greater than
`m_nLastSkillLevel`; use exit records the current rating.

`CBaseLockableEnt` — datamap `0x105b21e8`, records `0x105b222c`, 9, builder `FUN_102240d0`:

| externalName | internal | offset | notes |
| --- | --- | --- | --- |
| `key_name` | `m_sKeyName` | `+0x810` | inventory item that opens it |
| `delete_key` | `m_bDeleteKey` | `+0x814` | consume the key on use |
| `requires_key` | `m_bRequiresKey` | `+0x815` | blocks the lockpick attempt entirely |
| `use_icon` | `m_UseIcon` | `+0x818` | |
| `locked_icon` | `m_LockedIcon` | `+0x81c` | |
| `key_icon` | `m_KeyIcon` | `+0x820` | shown when the player holds the key |
| `Lock` | `InputLock` | — | `0x10224150` → `FUN_10224190` |
| `Unlock` | `InputUnlock` | — | `0x10224170` → `FUN_102241b0` (fires `OnUnlocked`, then writes `3`) |
| `Use` | `InputUse` | — | `0x102241f0`, forwards to the attached door or container |

`parentname` is the **attachment**, not a transform parent: `CPropDoorknob::Activate`
(`FUN_10225af0` → `FUN_102256f0`) resolves it with `FindEntityByName`, RTTI-casts to `CBaseDoor` and
calls `CBaseDoor::AddDoorknob(this)`. A missing target produces
`DevWarning("%s attached to non-existent door: %s")` followed by `UTIL_Remove`, and a door accepts at
most two (`"Door %s already has 2 doorknobs!"`). `item_container_lock` does the same against a
container (`FUN_10226520`).

Lockpick admission and use order:

1. `CanAttempt` (`FUN_10224ae0`) requires the lock to be locked, refuses a different current user,
   observes the attached-door block, and accepts an authored key immediately. Without that key,
   `requires_key` rejects the interaction; otherwise the player must carry `item_g_lockpick`.
2. `UnlockWithKey` (`FUN_10224950`) writes result tier `3`, optionally consumes the key when
   `delete_key` is set, and takes the unlocked/attachment path without entering the timed attempt.
3. Lockpick placement (`FUN_10224440`) requires model attachments named `camera_position` and
   `camera_target`. It derives a horizontal stance 31 units along the attachment direction, traces
   1,024 units down, and rejects solid obstruction or missing ground before aligning the player and
   beginning use.
4. `StartAttempt` (`FUN_10225070`) initializes the timing state, creates the `Intrusion`
   `camera_cinematic`, assigns it to the player, enters the player use mode, then fires
   `OnSkillAttemptBegin`.
5. Per-tick `FUN_102252f0` moves the player to the recovered stance and sends the four-byte
   `ProgBar` message: flags, progress percentage, attempt count and current feat rating. The client
   shows the lockpick bar with `Intrusion <rating>` while active. On exit `FUN_10225140` clears the
   cinematic camera/use mode and sends a terminal success, failed or not-attempted state.
6. At expiry `FUN_10224fa0` resolves the deterministic threshold, passes tier `1` or `3` to
   `CWeaponLockpick`, and follows the locked/unlocked branch. Tier assignment precedes
   `OnSkillAttemptCycle`; the selected result handler then increments `m_nSkillAttempts` before
   firing `OnSkillSuccess` or `OnSkillFail`. Success continues to `OnUnlocked` and the attached
   object's use path; failure ends the current use session immediately.

`CWeaponLockpick` is usable only while its owner has a current-use entity RTTI-castable to
`CBaseLockableEnt` (`FUN_103f4de0`). Starting the attempt plays weapon sequence `1` and gates both
attack timers to its duration (`FUN_103f4f40`); result tiers select weapon-animation code `2` for
failure, `7` for success and `13` for botch (`FUN_103f4fe0`). The normal deterministic path
therefore never selects code `13`. Player body routing maps `ACT_TOOL_LOCKPICK` to the `LockPick`
action.

`GetUseIcon` (`FUN_10224d40`) returns icon 58 or 59 for special door states, else `key_icon` when
the player has the key, else `locked_icon`, else `use_icon`.

Ctor defaults, which is where the corpus's "typical" icon values come from:

| class | factory / ctor / vftable | `use_icon` | `locked_icon` | `key_icon` | notes |
| --- | --- | --- | --- | --- | --- |
| `prop_doorknob` | `0x10225830` / `0x102258a0` / `0x1048c364` | 10 | 3 | 4 | |
| `prop_padlock` | `0x10225c00` / `0x10225c70` / `0x1048c89c` | 10 | 3 | 4 | on unlock it detaches and deletes itself (`FUN_10225f60`) |
| `prop_doorknob_electronic` | `0x10226190` / `0x10226200` / `0x1048cdd4` | 53 | 54 | 5 | Spawn forces `requires_key = 1`, so it cannot be picked; `Lock`/`Unlock` also set skin 0/1 (`FUN_10226360` / `FUN_10226380`) |
| `item_container_lock` | `0x102263b0` / `0x10226420` / `0x1048d30c` | 10 | 3 | — | attaches to a container |

### The doorknob handle sequence

`CPropDoorknob` overrides vtable slot `+0x444` with `FUN_10225b40`:

```c
const char *seq = IsLocked() ? "handle_locked" : "handle_unlocked";   // 0x105b2690 / 0x105b267c
int idx = LookupSequence(seq);
if (idx < 0)                 { m_nSequence = 0; }                    // falls back to `idle`
else if (m_nSequence != idx) { m_flCycle = 0; m_nSequence = idx;
                               ResetSequenceInfo(); StudioFrameAdvance(); }
```

It is **not** called from `Lock`/`Unlock`. `CBaseDoor`/`CRotDoor` re-pose their registered doorknobs
from the door's own use/activate path — the call sites are `0x100eef88` (inside `DoorknobUse`, on the
nearest knob only, on the refused branch), `0x100f0485`/`0x100f0524` (`CBaseDoor::DoorActivate`, both
knobs, each preceded by vtable `+0x450`) and `0x100f2877`/`0x100f2916` (`CRotDoor::DoorActivate`).
`CPropPadlock` overrides the same slot with an empty stub (`FUN_10225ce0`).

**What travels down that slot is the handle pose, not the lock.** `+0x444` reads the knob's *own*
`IsLocked()` to pick the sequence; no door path anywhere writes a knob's `m_LastRoll`. The authority
runs the other way — see *The lock authority is the knob's* below. Reading these call sites as a
lock-state push is the exact mistake that opens every key-gated door whose door entity happens to
carry no `LOCKED` spawnflag.

`CPropDoorknob::Spawn` (`FUN_102259e0`) seeds the state from the keyfield: `difficulty != 0` spawns
locked (`m_LastRoll = 1`) with bodygroup 1; `difficulty == 0` spawns unlocked (`m_LastRoll = 3`) with
bodygroup 0. Every doorknob in `sp_theatre` carries `difficulty 0`, so they rest on
`handle_unlocked`.

## `item_container` / `item_container_animated` / `item_container_lock`

Datamap `0x105a93d0`, records `0x105a9414`, 24, builder `FUN_10207f80`; base map
**`CBaseCombatCharacter`** — a container owns a real inventory. Factory `FUN_10207eb0`, instance
size `0x1a38`.

| externalName | internal | offset | notes |
| --- | --- | --- | --- |
| `Use` | `InputUse` | — | `0x10208c90` |
| `AddEntityToContainer` | `InputAddEntityToContainer` | — | `0x10208e50`, `FIELD_STRING` payload |
| `SpawnItemInContainer` | `InputSpawnItemInContainer` | — | `0x10208f30`, `FIELD_STRING` payload |
| `DeleteItems` | `InputDeleteItems` | — | `0x10208fa0` |
| `OnItemRemove` | `m_OnItemRemove` | `+0x19b0` | |
| `OnItemInsert` | `m_OnItemInsert` | `+0x19c8` | |
| `OnBreak` | `m_OnBreak` | `+0x19e0` | |
| `dmgmodel` | `m_sDmgModel` | `+0x1a30` | model swapped in when broken |
| `health` | `m_iHealth` | `+0x210` | |
| `use_icon` | `m_UseIcon` | `+0x1a34` | |
| `equip0` … `equip11` | `m_sEquip0` … `m_sEquip11` | `+0x19f8` … `+0x1a24` | the twelve authored contents slots; the corpus uses `equip0`–`equip4` |
| — | `m_BCCUser` | `+0x1a28` | EHANDLE, save |
| — | `m_hLockEnt` | `+0x1a2c` | EHANDLE of the attached `item_container_lock`, save |

`equip0`…`equip11` are spawn seeds, not a twelve-item storage cap. `CItemContainer::Spawn`
materializes every non-empty seed as a real item entity in the inherited 224-slot combat-character
inventory. `SpawnItemInContainer` does the same for one classname; `AddEntityToContainer` resolves
all matching targetnames and accepts only items with no current combat-character owner.
`DeleteItems` is destructive: it removes every contained entity and clears the slots and
active/last handles.

`Use` holds one exclusive player handle. Opening selects the lower barter service's loot mode and
shows the loot panel; closing hides it and releases the user. The server-side `vbarter` path moves
or splits the actual item entities and fires `OnItemRemove` / `OnItemInsert` for unpriced take/give
transfers. The shared ownership, stack, keyring, drop, ammo and barter contract is owned by
`docs/vtmb/inventory.md`.

`Lock` / `Unlock` wires aimed at a container are handled by that attached lock entity, not by the
container. The lid is the inherited `CBaseToggle` mover, selected by `use_pref`:

```c
CItemContainer::OpenMover()  // FUN_10208c30
    UseType == 1 -> StartMover(1);   // linear:  move_dest at move_speed
    UseType == 2 -> StartMover(3);   // angular: rot_dist degrees about rot_axis at rot_speed
CItemContainer::CloseMover() // FUN_10208c60 -> StartMover(0) / StartMover(2)
```

`CItemContainer::Spawn` (`FUN_10209350`) is the only class in this family that calls the mover
initialiser `FUN_101c1cb0`. `item_container_animated` (factory `0x10209cd0`, ctor `0x10209d40`,
vftable `0x104844cc`) additionally plays sequences named `open` / `close` (`FUN_10209df0` /
`FUN_10209e10` → `FUN_10209e30`; a missing clip logs `"%s no sequence named %s"` and falls back to
sequence 0) and emits the `soundgroup` events `open`, `close`, `swing`, `locked` (`FUN_10209790`).
`item_container_one_item_filtered` (factory `0x10209ef0`, vftable `0x10484c84`) presets a filter
slot to `5`.

## `npc_maker`

`CNPCMaker` (constructor `FUN_1034ad80`) descends through the AI NPC chain rather than from a
logic-only base. Its own datamap adds `OnSpawnNPC` (`+0x6668`), `OnNPCDied` (`+0x6680`) and
`OnLastNPCDied` (`+0x6698`), while the inherited NPC datamaps make keys such as
`OnFedUponBegin`, `OnFedUponEnd`, `OnDamaged`, `OnIncapacitated`, `OnFoundPlayer`, `OnDialogEnd`
and `OnDeath` syntactically valid on the maker record.

### State and admission

The maker keeps two independent quotas. `MaxNPCCount` (`+0x6660`) is the mutable number of finite
children still available to create; `MaxLiveChildren` (`+0x66b4`) is a simultaneous-live ceiling,
compared with `m_cLiveChildren` (`+0x66b0`). `Flag_InfChild` (`+0x66c3`) disables only finite-total
exhaustion and forces `Flag_Fade` (`+0x66c2`) on. It does **not** bypass the live ceiling.

Both `InputSpawnNPC` (`FUN_1034b500`) and `MakerThink` (`FUN_1034bbf0`) invoke the ordinary spawn
path with its internal bypass argument clear. Before allocation, that path caches ground height by
tracing 2,048 units down from the maker, then `CanMakeNPC` (`FUN_1034b580`) applies these gates in
order:

1. A true internal bypass argument accepts immediately. Neither the public `Spawn` input nor the
   timer supplies it.
2. If `MaxLiveChildren > 0` and `m_cLiveChildren >= MaxLiveChildren`, reject.
3. If choreographed-scene actor capture/hiding is active, reject.
4. Resolve the local player when one exists. With `Flag_NPCClip` (`+0x66c1`), reject when a
   player-view trace can see the maker; with `Flag_ViewCone` (`+0x66c5`), reject when the maker is
   inside the player's view cone.
5. With positive `MinPCDistance` (`+0x66c8`), truncate the 3D player-to-maker distance to an integer
   and reject when it is strictly less than the authored threshold. Equality is admitted.
6. Query a 68-by-68-unit square at cached ground height, X/Y `origin +/- 34`, using native filter
   value `0x2080`; admit only when it finds no blocking entity.

A rejected attempt allocates no entity, changes neither quota, and fires no maker output.
`Flag_StartDisabled` (`m_bDisabled`, `+0x66c0`) controls only automatic thinking: it does not block
an explicit `Spawn` input. `Spawn` (`FUN_1034afe0`) initializes the live count to zero, installs
`MakerThink` at `curtime + SpawnFrequency` when enabled, installs no think when disabled, and clears
the cached ground height. `Enable` refuses a finite depleted maker; otherwise it clears the disabled
latch and schedules an immediate think at current time. `Disable` sets the latch and clears the
think; `Toggle` selects between those two operations.

After a successful timed attempt, or a failure caused by the live ceiling, `MakerThink` retries at
`curtime + SpawnFrequency`. Other admission failures retry after a random 1-to-2-second delay. This
keeps transient visibility, distance, scene and occupancy failures responsive without polling them
at the full authored frequency.

### Child construction and output ownership

On an admitted attempt, `FUN_1034b7b0` creates the class named by `NPCType`, rejects a null or
non-Troika NPC, copies the maker's raw keyvalue template through the child's ordinary parser, runs
the class initialization, and copies the maker's NPC type and model. It then fires `OnSpawnNPC`
with the maker as both activator and caller **before** applying child spawn flags and relationship
template data, dispatching the child spawn, associating the maker as owner, assigning
`NPCTargetname`, or incrementing either quota. `Flag_Fade` adds child spawnflag `0x200` to the base
value `4`.

Only after successful dispatch does the maker increment `m_cLiveChildren`. A finite maker then
decrements `MaxNPCCount`; reaching zero clears `MakerThink`. Infinite mode leaves the remaining
total untouched. `Flag_NoDrop` (`+0x66c4`) is declared on this datamap but is not consumed by this
base `CNPCMaker` child-construction body; a nearby specialized maker path references it, so its exact
leaf-specific effect remains outside this base-class result.

The inherited NPC rows are **child templates, not maker-forwarded notifications**. Each child owns
a newly parsed copy of those action lists and its own positive `times` counters. Feeding, damage,
perception, dialogue and `OnDeath` fire from that child with the child as caller; no proxy hop
through the maker changes their activator/caller provenance.

### Child death and removal

The separate child notification (`FUN_1034bc90`) has a per-child once guard. If the notified child
is still alive, removal refunds one finite remaining-total slot and does not fire `OnNPCDied`. A
genuinely dead child fires `OnNPCDied` without a refund. The maker then tests finite-total
depletion and fires `OnLastNPCDied` when depleted, before decrementing `m_cLiveChildren` and
clamping it to zero. The code does not include a second `m_cLiveChildren == 1` test at that output,
and infinite makers are never depleted through this path.

The child still fires its own inherited `OnDeath`. Killing and respawning a child therefore does
not restore consumed counters on the old child; an admitted replacement receives fresh counters
from the maker template.

## `prop_sign`

`CPropSign` — factory `FUN_102118a0`, constructor `FUN_10211a50`, datamap builder
`FUN_10211970` — stores `definition_file` at `+0x730`, `OnReadBegin` at `+0x734` and
`OnReadEnd` at `+0x74c`. The UI consumes `use_icon` separately from `+0x76c`; it is not the
`+0x734` output field.

Use begin (`FUN_10211db0`) runs the inherited use-begin path, fires `OnReadBegin` with the player
activator, then loads the sign definition and acquires one exclusive player/sign session. The sign
data's first dependency that evaluates true selects any redirected document. A second user cannot
steal the live session. Use end (`FUN_10211e70`) runs the base use-end path, queues `OnReadEnd`,
closes the sign view and releases the owner. Both outputs can refire on later accepted sessions.
This is a held interaction lifecycle, not an `OnPressed`-style one-shot.

Fifteen current-export rows across seven maps author `OnReadBegin`. The two `sp_tutorial_1` signs
author no outgoing rows: `sign_chopshop_upstairs` selects
`tutorial_note.txt` with icon 18, and the unnamed bus-stop sign uses icon 56. They still require the
same ownership and open/close behavior even though `OnReadEnd` has nothing to deliver in this map.

## `prop_hacking` (`CBaseTerminal` / `CPropHacking`)

The terminal-specific class surface, content grammar, skill attempts, `OnUseBegin`/`OnUseEnd`
(fired from `FUN_100a4fe0` / `FUN_100a5030` inside skill-entity slots 39/42), `OnSkill*` and
`OnTrigger0`…`OnTrigger7` production, email state, teardown through `FUN_10167fd0`, and the
tutorial worked chain are owned by `docs/vtmb/computer-terminals.md`. Once a terminal output
fires, the generic wire format, name resolution and event-queue delivery specified here apply
unchanged.

## Proven-dead Hammer/FGD keys

These keys are stamped on shipped entities and read by no engine code. A case-insensitive scan of
the whole `vampire.dll` image returns zero occurrences of each; `demo_sequence` and `npc_opaque` are
additionally absent from `client.dll`.

| key | authored | stamped on |
| --- | --- | --- |
| `demo_sequence` | 1,619 | `prop_dynamic`, `prop_physics`, NPC classes |
| `climbable` | 366 | `func_door_rotating`, `func_brush`, `prop_switch` |
| `locksnd` | 174 | the lockable family, `prop_switch` |
| `npc_opaque` | 130 | `prop_dynamic`, `item_container_animated` |
| `diceroll` | 86 | `prop_doorknob`, `prop_hacking` |
| `actsnd` / `deactsnd` | 12 each | `prop_switch` |

Sound selection runs entirely through `soundgroup` — a `CBaseEntity` keyfield, `m_iszVSoundGroup`
`+0xc0` (record `0x10553f34`, builder `FUN_100a22f0`), resolved to a group handle at `+0xb4` — with
each class playing named events inside the group, as listed per class above.

## `env_particle` attachment (`CEnvParticle`)

The datamap is at `0x105669d8` (`dataDesc` `0x10566a1c`, builder write at `0x100fad2a`).

| externalName | internal | offset |
| --- | --- | --- |
| `particle_definition` | `m_sParticleDefinition` | `+0x450` |
| `attach_type` | `m_nAttachType` | `+0x458` |
| `bone` | `m_sAttachName` | `+0x45c` |
| `attach_point` | `m_nAttachPoint` | `+0x460` |
| *(no key)* | `m_fRedScale` / `m_fGreenScale` / `m_fBlueScale` / `m_fMaskScale` / `m_fSizeScale` | `+0x464`…`+0x474` |
| `active` | `m_bActive` | `+0x484` |
| `ramp_scale` | `m_fRateScaleTarget` | `+0x48c` |
| `ramp_time` | `m_fRampTime` | `+0x490` |
| `spawnbounds` | `m_fSpawnBounds` | `+0x49c` |
| `hud_draw_third` | `m_bHUDDrawThird` | `+0x4a0` |

All are plain keyfields with a null `inputFunc`; the five scale fields have **no external name** —
a map cannot author them, code writes them (the impact spawn's constant `0.8`), and they default to
1. **The FGD's `bounds` is not in the datamap**: 1,110 corpus rows author it and nothing reads it;
the extent key is `spawnbounds` (default 512, 228 rows author it, all 512). No spawnflag is read
(`m_spawnflags` is the base field; the class never tests it). `attach_type` is a `FIELD_INTEGER`,
so a map's numeric value lands directly; the same field is also settable **by name** from the
particle definition, and that parser (`FUN_100fb620`) gives the low enum its names — a `__strcmpi`
chain over `"origin"`, `"tree"`, `"point"`, `"treecolor"` writing `0`, `1`, `2`, `3` in that order.
The whole enum has **19 values**, dumped at `0x105a7000`, and the animation-event spawn `mode`
argument lands in the same field and is the same enum (verified):

| Value | Name | Value | Name |
| --- | --- | --- | --- |
| `0` | `FollowOrigin` (`origin`) | `10` | `PlayerBox` — the `spawnbounds` cube round the viewer, wrapping |
| `1` | `BoneTree` (`tree`) | `11` | `PlayerSky` — as 10, spawns above the viewer (the rain follow) |
| `2` | `BoneSinglePoint` (`point`) | `12` / `13` | `PlayerSphereEdge` / `FollowPlayerSphereEdge` |
| `3` | `BoneTreeWithColors` (`treecolor`) | `14` | `ScreenCenter` |
| `4` | `BoneHitboxVolumes` | `15` | `BrushEmitter` — forced by `func_particle` |
| `5` | `ScreenBorder` | `16` | `ScreenRandom` |
| `6` | `ModelAttachment` — origin and basis each tick | `17` | `ModelAttachmentNoFollow` |
| `7` | `ScreenBottomAndSides` | `18` | random point on the parent's visible skin |
| `8` | `EntitySimulatedPoint` | `9` | `EntityBox` — random point in the parent's render OBB |

`2` = `point` is the attachment-point/bone follow, and the corpus agrees: the `attach_type 2`
emitters carry **both** `parentname` and `bone`. `attach_type 0` is the entity's own origin.
Corpus usage over 1,304 rows: `0` 1,085 · `1` 111 · `2` 45 · `-1` 29 (the switch default; origin,
INFERRED) · `11` 14 · `6` 11 · `17` 5 · `10` 2 · `9` 1 · `5` 1. Modes 4, 7, 8, 12–14, 16 and 18 are
placed nowhere and reachable only from code that passes 1, 2 or 6. `Activate` removes the entity
when the definition does not resolve.

### `TurnOn` restarts; `TurnOff` only stops feeding

Both inputs forward to adjacent virtual slots (`vtable + 0x3c4` / `+0x3c8`) on `CEnvParticle`'s own
vtable (`0x10455f1c`). `TurnOn` (`0x100fb7a0`; the `0x100fc570` an earlier pass cited is
`CEnvParticleHUD`'s override) carries **no already-active guard**: every call re-issues the
spawn/attach call and re-stamps the activation time (`m_bActive` `+0x484`, start time `+0x488`),
and the client rebuilds the emitter whenever that timestamp changes — that is the restart. This
is what lets a scene address two distinct emitters sharing one targetname with a single wire —
each restarts its own. `TurnOff` (`0x100fb7d0`) is a bare `m_bActive = 0`: idempotent, and it does
**not** kill live particles, which finish on their own timeline (the emitter's stop clears `loop`
and stops feeding). Live particles **do** follow a moving parent — `parent_speed` (default 1) is
the fraction of the parent's movement they inherit (`docs/vtmb/effects.md` §2.4).

`SetRateScale` / `SetRampTime` write a target the client approaches **linearly** into the one
per-emitter rate float that multiplies both `rate` and `burst`. **`JetLength` is not an input on
this class**: the two map wires that send it (`env_particle`'s input surface below) are dead.

`active` seeds spawn-time state through the plain field, and the constructor (`0x100fad50`) defaults
it to `1`, so an emitter authoring no `active` key starts on. `ramp_scale` (`+0x48c`) and `ramp_time`
(`+0x490`) are **not** consulted by `TurnOn` — they take effect only through the explicit
`SetRateScale` / `SetRampTime` inputs, or through the ramp-down think that fades the rate to zero
over `ramp_time` and then chains into a delayed `TurnOff`. A graceful fade is that sequence, never
the plain input.

## `logic_timer` (`CTimerEntity`)

Factory `0x10131390` (vtable `0x10465fb4`), datamap `0x10575e50`, 14 records. Keyfields are
`StartDisabled` (`m_iDisabled` `+0x498`), `RefireTime` (`+0x49c`), `UseRandomTime` (`+0x4a4`) and its
`LowerRandomBound` / `UpperRandomBound` pair (`+0x4a8` / `+0x4ac`). Inputs are `Enable`, `Disable`,
`Toggle`, `RefireTime` and `FireTimer`; `OnTimer` (`+0x450`) is the only live output.

One "arm" routine (`0x101316e0`) governs the cadence:

```c
if (m_iDisabled == 0) {
    if (m_iUseRandomTime) m_flRefireTime = RandomFloat(LowerRandomBound, UpperRandomBound);
    NextThink = curtime + m_flRefireTime;      // always a full interval from now
}
```

Four consequences follow, and all four are observable:

- **`Enable` does not fire immediately.** The first `OnTimer` lands one full `RefireTime` after the
  input, because arming only ever writes `curtime + RefireTime`.
- **`Disable` cancels the pending think outright** (`NextThink = 0`); it is not an output gate.
- **Re-`Enable` restarts the interval.** The arm never reads the old deadline, so no phase survives a
  disable/enable pair.
- **`UseRandomTime` re-rolls on every arm**, not once at spawn — each interval is independently drawn.

`RefireTime` is clamped to a `0.01` floor. The `RefireTime` input (`0x101318e0`) applies the same
clamp, no-ops when the value is unchanged, and otherwise re-arms — so a live change reschedules from
now, and does nothing at all while the timer is disabled. `FireTimer` (`0x101317d0`) is the fire
routine itself: it returns early when disabled, fires `OnTimer`, then re-arms.

`StartDisabled` is applied at **Spawn** (`0x10131600`), not Activate, and the same pass forces the
disabled state when `RefireTime` is invalid and no random range is authored — so a timer with a
nonsensical interval never runs even if it was authored enabled.

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

**What claim and release actually move.** The claim captures, at `CCineNPC + 0x5f78` upward,
the NPC's movetype, movecollide, solid, solidflags and effects, plus a flags word taken from the
NPC's **active weapon**, not the NPC (`m_saved_troika_flags`, `+0x5f8c`). Release (`0x1027d170`,
also reached from the `ScriptHide` cancel path) re-applies movetype+movecollide, solidflags, effects
and the weapon word, detaches `m_hCine`, and sets the ideal state to `IDLE`. Two facts follow:
`m_saved_solid` (`+0x5f80`) is captured and **never read back** — the only `SetSolid` call on this
path is the no-valid-cine fallback, which hard-codes `SOLID_BBOX`; and **neither claim nor release
touches a schedule, a goal entity, or any navigation field.** The physics hand-back and the schedule
hand-back are separate mechanisms.

**`FixScriptNPCSchedule`'s "default" is `ClearSchedule`, not a schedule.** Case `1` calls
`0x10280de0`, which resolves the local ID through the class schedule table (vtable `+0x910`) and
installs it directly — a real `SetSchedule(0x2a)`. Case `0` and the fall-through call a *different*
function, `0x10280d30`, which zeroes six consecutive fields from `+0x5c38` (schedule ID, task index
and timers), clears a bit on the task owner, and installs nothing. The NPC therefore falls through
to ordinary schedule selection on its next maintain. Since every exported sequence writes
`m_iFinishSchedule = 0`, this is the path that always runs, and where it lands is decided by the
idle selector in `npc-ai/README.md`.

**Inputs:** `BeginSequence`, `CancelSequence`, **`MoveToPosition`** (`inputFunc 0x1000d6c0`, whose
body is `FUN_101a72b0` — reached only through the datamap, so the analyzers leave it
undisassembled), plus the base `Kill`/`ScriptHide`/`ScriptUnhide`. `MoveToPosition` sends the NPC to
the mark without running the action: it returns unless the NPC's script state is `0` or `2`, picks
the move activity through vftable `+0x924`, validates it through `+0x788`, starts the move through
`+0x91c`, and re-arms the `Use` throttle. No exported map wires it. **Outputs:** `OnBeginSequence`,
`OnEndSequence`, and `OnScriptEvent01..08`. A 76-byte `.mdl` sequence event with ID **1003** and
numeric options **1..8** reaches `CCameraAnimated::HandleAnimEvent` and fires the corresponding
output; the record and dispatch are decoded in `docs/vtmb/animation_and_movers.md`. The rebuild
still needs that decoded timeline exported and played before the outputs can fire.

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
through a rate-limited yaw controller. No `sp_theatre` sequence uses `m_fMoveTo` 4 or 5, but **27 of
the install's 108 maps do** — 81 authorings, 62 at value 4 and 19 at value 5 — so the difference
reaches a quarter of the game rather than a couple of maps. The heaviest carriers are `ch_temple_2`
(10), `la_ventruetower_1` and `la_bradbury_2` (9 each), `hw_cemetery_1` (5) and `sm_warehouse_1` (4);
`sp_tutorial_1` carries 3. Over all 108 maps `m_fMoveTo` is authored 702 times across 68 maps,
distributed `{0: 165, 1: 198, 2: 206, 3: 52, 4: 62, 5: 19}`.

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

A scripted Walk resolves `ACT_WALK` through the character vocabulary and the owning bank's neutral
blend-grid cell before requesting travel. That same deterministic selection plays the visible
in-place clip and supplies its decoded average ground speed to the existing route motor. The
skeleton therefore animates without contributing a second actor translation; an old sidecar with
no motion summary retains the `speed_walk` fallback. Playback re-enters the global resolver with
the vocabulary label (`walk`), not the concrete bank animation (`walk_0`), because the label is
what retains the shared-bank owner needed to load and retarget the clip.

### Where the rebuild diverges

Each is a deliberate call, recorded beside the behaviour it departs from:

- **Travel can fail here; in retail it cannot.** Retail's mover always reaches its mark, so a beat
  has no failure branch. This runtime walks a real navigation graph, so travel is bounded by a
  no-progress window and an absolute cap, and a body that runs out is placed on the mark so the beat
  still ends — a beat that never ends stalls the map's whole script flow. The cap has to sit under
  the cleanup timers a map hangs off its own camera track: `sp_theatre` kills the walk-out beats
  twenty seconds into the shot, having authored them against a walk of about half that.
- **Spawnflag 256's condition is approximated.** The engine holds the post-idle when there is no
  live `m_hNextCine`; the nearest thing here is an authored `m_iszNextScript`, so an empty one
  stands in for it.
- **Queue ownership is one bit, not a re-read of the owner.** The engine decides a refusal by
  inspecting the cine that holds the NPC; this runtime stamps "this owner refuses handover" onto the
  NPC when the claim is made, and distinguishes the two refusal messages by re-reading only the
  owner's spawnflags. A claim whose owner has been destroyed is cleared rather than honoured.
- **`OnScriptEvent01..08` do not fire** — the `.mdl` event record and ID-1003 dispatch are decoded,
  but the current character export/bake does not yet carry the event timeline into playback.

**Demand across the 22 exported maps:** 182 `scripted_sequence` + 6 `aiscripted_sequence`;
146 `BeginSequence` and 22 `CancelSequence` I/O wires. The exported Python/dialogue corpus adds
70 `BeginSequence` and 8 `CancelSequence` calls through the same datamap lookup
(`docs/vtmb/python_bridge.md`). **180** wires leave the sequence entities — `OnEndSequence` 109,
`OnBeginSequence` 66 and `OnScriptEvent01/02/03` 5 — and they unlock doors, restore cameras, and
open conversations, so a beat that never ends stalls the map's flow. The entities author **134**
exact animation references: `m_iszPlay` 76, post-idle 31, pre-idle 18 and custom move 9; seven
belong to `!playercontroller`.

The action-demand join resolves **129 of those 134** exact references against their target body's
complete include-model vocabulary. The five authored misses are content, not decoder gaps:
`ACT_COWER` on three `sm_diner_1` actors, `pre_fight_bow` on AsianVamp (the model spells the label
`prefight_bow`), and `ACT_DOORKNOCK` on `!playercontroller` (absent from all 56 exported player
bodies). `FUN_101a82d0` (`scripted_sequence`) and `FUN_101a9510`
(`aiscripted_sequence`) both handle a missing exact label by warning, assigning sequence **0**,
zeroing the cycle and calling `ResetSequenceInfo`; they do not reinterpret an `ACT_*` spelling as
an activity. One of the 42 `m_iszNextScript` links, `forky_thug_charge → cower3_outof` in
`sm_warehouse_1`, names no exported sequence entity. The reproducible ledger is produced by
`uv run elysium research action_animation_survey`.

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
the destination's and stamps a teleport time on the player. The controller relationship mirrors the
explicit transform write, so later controller removal transfers the teleported pose rather than its
pre-teleport pose.

There is **no destination trace, hull-clearance test, ground search, nearest-safe-point search, or
velocity reset** in the recovered path. The cached transform is authoritative even if it intersects
world or entity collision; ordinary movement/physics resolves the consequence later. The only
pre-write rejection is the target's transform parent. The input also does not test distance from the
teleporter, activator identity, line of sight, or destination trigger state.

Every teleport ends with `FUN_101cf600` on the target, which is **`CBaseEntity::Relink`** (trace
string `0x10558f08`) and whose body is a profiler scope push and pop with no work between them. It
adds nothing: re-establishing the entity's spatial links falls out of `SetAbsOrigin` itself, so
there is no separate touch re-test in the teleport path.

Touch delivery therefore does not occur inside `InputTeleport`'s event-queue delivery. Retail's
`Teleport` and `teleport_player` move the entity through `UTIL_SetOrigin` with `bFireTriggers = false`;
`point_teleport` writes the transform directly and its trailing relink hook (`FUN_101cf600`,
`CBaseEntity::Relink`) is empty. Either way `PhysicsTouchTriggers` never runs at teleport time, so the
teleported entity fires **no** touch callbacks and arms **no** untouch. There is no solid-flags window —
retail never clears `FSOLID_NOT_SOLID` around the move (that SDK-2013 mechanism is not in the 2004
binary). Both the entity's stale `EndTouch` and its new `StartTouch` wait for its **next**
`PhysicsTouchTriggers` — for a player, its next movement/physics step — where the begin-before-end order
recovered under "The touch dispatch path" then applies unchanged. Several teleports before that
reconciliation collapse observationally to the final containment. A trigger explicitly enabled by a
later setup event is a different operation: rebuilding its physical touch links can create a fresh
`StartTouch` after the enabling input.

The faithful runtime caches live origin and all Source angles in the late entity-activation pass,
after the frozen player placement has been synchronized into `!player`. It preserves that cache in
map snapshots, resolves `!activator` only when the input arrives, refuses parented targets, and
applies position plus angles as one body update. The logical player's `origin` is Source feet and
its `angles` are the complete Source view; the Unreal body owns the one feet-to-centre conversion,
body yaw, and full controller view. Its post-movement containment diff reproduces retail's recovered
begin-before-end order (see "The touch dispatch path") and then orders by stable entity index — a
deterministic substitute for retail's spatial (BSP-leaf) enumeration, marked as an owner-accepted
divergence. Initial map containment remains an activation transaction:
place and freeze the pawn, synchronize and activate entities, open the entity world, reconcile
containment immediately, then run the frozen think/event pass.

## NPC `TeleportToEntity` (`CAI_BaseNPCTroika`)

This is a different input from `point_teleport.Teleport`. It is declared by
`CAI_BaseNPCTroika`, not `CAI_BaseNPC`: datamap `0x105ce470`, record array `0x105ce4b4`, record
`0x105d0a2c`. The record is `FIELD_EHANDLE` (VtMB field type 12), external name
`TeleportToEntity`, internal name `InputTeleportToEntity`, with handler thunk `0x100028dd` jumping
to `FUN_102c24a0`. The base `CAI_BaseNPC` map at `0x105c9814` has no such record.

Hammer outputs author the destination as a string. `CBaseEntity::AcceptInput` first converts that
string to the record's `FIELD_EHANDLE`: `variant_t::Convert` (`FUN_100d05d0`) calls the global
entity list's `FindEntityByName` (`FUN_100f7770`) with a null starting entity. Lookup walks entity-list
order and returns the first case-insensitive exact match; a final `*` changes the comparison to a
case-insensitive prefix match. A missing or empty name becomes `INVALID_EHANDLE`, but conversion
still reports success, the handler consumes the input and returns without moving the NPC. There is
no retry if a destination with that name appears later.

For a valid handle the native handler performs, in order:

1. read the destination's absolute origin and call `SetAbsOrigin` on the NPC;
2. resolve the same handle again, read the destination's absolute angles and call `SetAbsAngles`
   on the NPC — all three angles, not yaw alone;
3. call the empty `CBaseEntity::Relink` profiler hook;
4. invoke virtual slot `+0x998` on the NPC;
5. call `CBaseEntity::ForceTransmit`, setting the receiver's transmit deadline to
   `curtime + 1.0` seconds.

For all three concrete receiver families in the current corpus, slot `+0x998` resolves through
thunk `0x10010f0f` to `FUN_102c23f0`. It writes `curtime` to the general `m_flNextThink` (`+0x17c`)
and to the four Troika deadlines
`m_flNextUpdateThink`, `m_flNextNormalThink`, `m_flNextMoveThink`, and `m_flNextAIThink`
(`+0x6244` through `+0x6250`). It does not clear an active schedule, route, movement goal, enemy,
NPC state, animation, or condition memory. The separately authored `teleport_move_timer` field at
`+0x65dc` is not read by this input.

There is also no parent rejection, destination trace, hull-clearance test, ground or nearest-safe
search, velocity or angular-velocity reset, or direct touch dispatch. The transform is authoritative;
spatial relinking follows `SetAbsOrigin`, and touch consequences cross the same later engine-owned
collision boundary as `point_teleport`. Because retail `GameFrame` runs entity thinks before it
services the event queue, an authored output delivered through that queue makes the five deadlines
due only after the think pass has ended; Jack consumes them on the next server frame, not recursively
inside `TeleportToEntity`.

The current 23-map export corpus authors 40 such wires: eight to Jack in `sp_tutorial_1`, two to
static `npc_VHumanCombatant` guards in `sm_warehouse_1`, and thirty to `npc_VHunter` children named
by makers in `la_hub_1` and `sm_hub_1`. Every destination name exists. `la_hub_1` deliberately has
two `point_target` rows named `hunter_2_spot_1`; first-match lookup selects the earlier live
entity-list entry rather than rejecting the ambiguity or choosing the nearest point. This pass pins
the shared transform/lookup handler and all three concrete post-teleport hooks used by these wires.
Jack's `CNPC_VVampire`, `CNPC_VHumanCombatant` vtable `0x104b7ff4`, and `CNPC_VHunter` vtable
`0x104b9784` all resolve slot `+0x998` (index 614) through thunk `0x10010f0f` to
`FUN_102c23f0`, so the same five due-think writes apply to every current corpus receiver family.

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
SendProps (`DT_BaseAnimating` builder `0x1008aaf0`; 10 bits, 10 bits, and an unscaled float) and
`FadeToSkin` writes no start time, so the crossfade would have to be rendered client-side.

**The client crossfade is dead code, and every skin change snaps.** The client receives the three
props at `+0x410` (`m_nSkinCrossfade`), `+0x418` (`m_flSkinCrossfadeTime`) and `+0x55c` (`m_nSkin`),
and keeps a client-only start time at `+0x41c`. `C_BaseAnimating`'s draw path (`0x10092970`) really
does implement the blend, by drawing the model twice — once with `m_nSkinCrossfade` at a reduced
`modelrender->SetBlend`, then once with `m_nSkin` — gated on `IsSkinCrossfading` (`0x10093e40`):

```c
if (m_nSkinCrossfade /*+0x410*/ == -1) return false;
elapsed = curtime - m_flCrossfadeStart /*+0x41c*/;
if (m_flSkinCrossfadeTime /*+0x418*/ < elapsed) { m_nSkinCrossfade = -1; return false; }
return true;
```

The only writer of `+0x41c` anywhere in `client.dll` is the `C_BaseAnimating` constructor
(`0x1008f3c0`), which stores `-FLT_MAX`; `m_nSkinCrossfade` is registered with RecvProxy `0` and no
`OnDataChanged` latch touches it. An operand scan for that displacement across the whole image (106
hits) finds no other write. So `elapsed` always exceeds the fade time, the first evaluation resets
`m_nSkinCrossfade` to `-1`, and the second draw pass never runs.

`crossfade_skin_time` is therefore inert on all 1,785 entities that carry it, and it carries no
authored signal in any case — `2.0` on nearly all of them, i.e. an FGD default stamped on everything
that animates.

Server-side the two fade inputs are real but write only fields: `InputFadeToSkin` (`0x1008d520` →
body `0x1008d6d0`) and `InputSetSkinFadeTime` (`0x1008d450` → body `0x1008d5f0`).

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
| `ambient_generic` | PlaySound(903) Kill(254) StopSound(248) Volume(36) FadeIn(2) FadeOut(2) ScriptUnhide(2) — **FadeIn/FadeOut have no datamap handler**; those two+two wires are `AcceptInput` refusals. Real fade keys are KeyValue `fadein`/`fadeout` LFO into `m_dpv` (`docs/vtmb/audio_pipeline.md` §7) |
| `npc_VHumanCombatant` | 29 inputs — ScriptUnhide(175) TweakParam(140) SetRelationship(107) FollowPatrolPath(95) SetupPatrolType(95) SetInvestigateMode*(130) StayEntrenched(65) WillTalk(36) ScriptHide(32) Kill(25) StartPlayerDialog(19) … |
| `prop_dynamic` | Skin(332) ScriptUnhide(176) Kill(94) SetAnimation(81) ScriptHide(67) Break(65) |
| `scripted_sequence` | BeginSequence(515) CancelSequence(93) Kill(92) ScriptHide(15) ScriptUnhide(14) |
| `trigger_multiple` | Disable(197) Enable(171) Kill(170) ScriptUnhide(60) ScriptHide(45) |
| `env_sprite` | HideSprite(218) ShowSprite(108) TurnOn(96) TurnOff(62) ScriptUnhide(11) Kill(11) ScriptHide(10) |
| `func_door_rotating` | Unlock(102) Open(82) Lock(69) Close(58) ScriptUnhide(31) ScriptHide(7) Toggle(6) Kill(4) |
| `prop_button` | Lock(137) Unlock(105) SetState(69) Kill(2) |
| `env_particle` | TurnOn(180) TurnOff(85) Kill(27) SetRateScale(5) JetLength(2 — not an input on the class; dead wires) ScriptUnhide(2) SetParent(1) |
| `trigger_changelevel` | ChangeNow(273) ScriptUnhide(18) ScriptHide(6) Enable(1) Kill(1) |
| `func_door` | Open(157) Close(93) Unlock(15) Lock(12) Toggle(4) Kill(4) ScriptHide(2) |
| `func_brush` | ScriptHide(104) ScriptUnhide(101) Kill(30) Disable(3) Enable(3) |
| `ambient_soundscheme` | FadeIn(205) FadeOut(179) Kill(1) Disable(1) — **Disable has no handler**; Kill does not stop the manager stems (`docs/vtmb/audio_pipeline.md` §5) |
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

### The declared surface, from the binary [decompile-verified]

The table above is the surface the shipped maps actually *wire*. The surface each class
**declares** is larger, and it is enumerable: every `DEFINE_INPUTFUNC` record binds a class,
an input name and a handler address in one 44-byte structure. **`vampire.dll` declares 402
input handlers across its datamaps**, `client.dll` and `engine.dll` none — inputs are a
server-side concept.

The recovered names follow `<Class>::Input<ExternalName>` and are what the handler bodies now
carry, so an input is traceable from the map wire to its implementation without a fresh
disassembly. Examples from `CAI_BaseNPCTroika`, the class the NPC wires address:

| input | handler |
| --- | --- |
| `StartPlayerDialog` | `0x1029ef80` |
| `StartPlayerDialogRemote` | `0x1029f060` |
| `StartPlayerDialogUnforced` | `0x1029f120` |
| `FollowPatrolPath` | `0x1029ed90` |
| `SetupPatrolType` | `0x1029eb30` |
| `ClearPatrolPath` | `0x1029ef60` |
| `WalkToNode` | `0x1029e840` |
| `TweakParam` | `0x1029ea40` |
| `FleeAndDie` | `0x1029f210` |
| `Faint` | `0x1029f250` |
| `DisableThink` | `0x1029f2a0` |
| `TeleportToEntity` | `0x102c24a0` |

A declared input that no map wires is still real: `FadeToSkin` and `SetSkinFadeTime` are
declared by the skin family above and wired zero times.
