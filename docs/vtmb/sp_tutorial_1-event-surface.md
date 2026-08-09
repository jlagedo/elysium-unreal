# `sp_tutorial_1` event-surface research brief

## Authored triggers, interactive props, Python, dialogue, and planning closure

**Map:** `sp_tutorial_1`
**Baseline:** patch-first export resolved from the user's installed game
**Document role:** cross-system research brief; not a status tracker or a generic behavior owner
**Last reviewed:** 2026-08-08

The master sequence and completion state remain in `docs/project/roadmap.md`. Generic Source entity
I/O behavior belongs in `docs/vtmb/entity_io.md`; Python hosting belongs in
`docs/vtmb/python_bridge.md`; inventory behavior belongs in `docs/vtmb/inventory.md`; the
script-call inventory belongs in `docs/vtmb/script_api.md`; the tutorial beat machine belongs in
`docs/vtmb/game_runtime.md`. This brief keeps the map-specific join
between those systems so later plans can be derived from the authored demand rather than from a
hand-written feature wish list.

---

## Executive conclusion

`sp_tutorial_1` is not driven by Python alone. Its playable event graph has five cooperating layers:

1. **Ingress** — player touch, look, `+use`, damage, feeding, inventory changes, dialogue choices,
   NPC perception, combat, timers, and scene milestones originate events.
2. **Transport** — the entity output queue preserves target, input, parameter, delay, fire count,
   Python payload, caller, and activator.
3. **Receivers** — relays, movers, NPCs, props, UI, audio, particles, cameras, counters, and map
   travel perform the requested side effect.
4. **Python and dialogue** — the level module, `vamputil`, `logic_pythoncheck`, output field six,
   scheduled source, and `.dlg` expressions share the `__main__` namespace and the engine-owned
   `G` state bag.
5. **Persistence and policy** — player state, inventory, quest state, death state, discipline state,
   save/restore, and world policy carry consequences between beats.

The event queue can be operational while the map remains unplayable: every authored output also
needs a real producer. The first post-warp dependency is the blueblood encounter. A spawned NPC's
feeding lifecycle must reach the `npc_maker`-authored `OnFedUponBegin` and `OnFedUponEnd` wires;
`OnFedUponEnd` sets `G.Tutorial_Blueblood`, enables the outside-chopshop dialogue trigger, and lets
the Jack beat counter advance. Later beats add inventory, containers, specialized prop use,
combat, stealth, disciplines, dispositions, environmental audio, and presentation effects.

The current patch-first data also contains optional Patch Plus and hunter content. “Every event”
therefore means more than the shortest retail tutorial route. Plans should state whether their
acceptance target is:

- the ordered core tutorial route;
- every reachable event in the patch-first map under normal play;
- or every authored wire, including optional, late-game, and developer-only paths.

## 1. Evidence model and provenance

### 1.1 Confidence language

| Label | Meaning in this brief |
|---|---|
| **Observed** | Counted directly from the hash-pinned patch-first export or the referenced dialogue/script files. |
| **Recovered** | Established by binary inspection or corpus analysis in an owning `docs/vtmb` document. |
| **Source inspection** | Present-tense observation from the Unreal source tree; source remains authoritative and must be reread before implementation. |
| **Intent inference** | Meaning inferred from names, wiring, spatial role, and surrounding script. It requires retail observation if exact behavior matters. |
| **Planning requirement** | Capability or acceptance evidence needed to close an authored dependency; not project status. |

No live Unreal or retail-game acceptance run is evidence for this snapshot. Counts and authored
connections are static evidence. A phase closes only when its required event chain is observed at
runtime under the roadmap's acceptance conditions.

### 1.2 Survey inputs

Game-derived files remain below `$ELYSIUM_EXPORT_ROOT` and are not committed. Only their hashes,
counts, names, and derived behavioral inventory appear here.

| Input below `$ELYSIUM_EXPORT_ROOT` | Bytes | SHA-256 |
|---|---:|---|
| `sp_tutorial_1/sp_tutorial_1.ents` | 849,062 | `F54C3DF75977BDD1DDB464077C4A4D6AD979D240D6F8B576CE6774FC07352B6C` |
| `scripts/tutorial/tutorial.py` | 21,972 | `22BB91612F80123864FD4353E4C022639AF28B11FF71C43F4E155924E3CA6A01` |
| `scripts/vamputil.py` | 194,218 | `6FC4A026AFF8AA67AACEC00F39D8AE0A838EDB54F2F38CB9AF3DEAAD262C5D1D` |
| `dlg/Main Characters/jack_tutorial.dlg` | 100,434 | `077E993BE8CC3F040B559F1DCD516229A04CD5530CDAC82A731A607CA6B1CA57` |
| `dlg/downtown la/tutorial_security_guard.dlg` | 6,146 | `A8217D4BE12E548BA3C91E60DCC408A945BF93512735C70BA7A9FF17EB72031F` |
| `dlg/generic/hunter1.dlg` | 16,596 | `02EABAACB20283F3BA894E95A8F06309354D734005463D7E97CBADAFD2E7DDFA` |
| `dlg/generic/hunterv.dlg` | 2,475 | `520D6E805E6667D4D9F28EF6B53577EAC2B5BE9DB67929281C74AA421E17676C` |

Primary reproduction command:

```powershell
uv run elysium research ent_survey --patch --map sp_tutorial_1
```

The command reports the patch-first entity and I/O surface. The Python/dialogue closure in this
brief additionally joins field-six payloads, the level module, `vamputil`, and all dialogue files
named by this map. Re-run that join whenever any input hash changes.

## 2. Map-scale inventory

| Surface | Observed count | Why it matters |
|---|---:|---|
| Entities | 1,868 | Complete patch-first entity population, including optional patch content. |
| Distinct classnames | 80 | Breadth of runtime class registration demanded by this map. |
| Output wires | 1,028 | Authored event edges that the runtime must resolve or diagnose. |
| Outputs carrying Python in field six | 133 | Edges that also execute source in `__main__`. |
| Seven-field output rows | 1,027 | Normal VtMB extended output format. |
| Six-field output rows | 1 | Legacy-width row that the parser must accept without shifting semantics. |
| Entities owning at least one output | 338 | Potential event producers, independent of receiver coverage. |
| Nonblank static `.props` placements | 809 | Baked/adopted scene objects; normally not per-instance I/O entities. |
| Entity placements carrying `model_mesh` | 160 | Addressable visual entities with runtime identity and keyvalues. |
| Exported prop OBJ/MTL pairs | 219 | Local geometry inputs used by the visual/physics construction paths. |
| Exported prop physics files | 18 | Model-derived collision inputs for applicable physics props. |
| Exported prop skin sidecars | 27 | Per-model material/skin variation inputs. |
| Tutorial Python module | 613 lines | Map-specific callback and state-machine body. |
| Referenced dialogue files | 4 | Jack, security guard, and optional hunter conversations. |
| Dialogue rows in those files | 1,420 | Conditions and actions that extend the map API beyond `.ents`. |
| Distinct `G` flags across map, module, and dialogues | 44 | Minimum tutorial-state namespace visible in the joined closure. |

### 2.1 Hidden-at-spawn population

`StartHidden` is an entity-wide off state, not merely render visibility. These objects require
`ScriptUnhide`, `Enable`, or another authored activation path before they participate.

| Class | Count |
|---|---:|
| `prop_dynamic` | 29 |
| `prop_physics` | 10 |
| `npc_VVampire` | 6 |
| `func_button` | 6 |
| `trigger_changelevel` | 5 |
| `trigger_multiple` | 4 |
| `prop_dynamic_ornament` | 3 |
| `npc_VHumanCombatant` | 2 |
| `func_brush` | 2 |
| `npc_maker` | 2 |
| `math_counter` | 2 |
| `npc_VRat` | 1 |
| `logic_relay` | 1 |

## 3. Load-bearing event API contract

Every system plan that consumes this brief needs to preserve this common contract.

| Contract surface | Required behavior | Map intent and acceptance observation |
|---|---|---|
| Level-script bootstrap | Load `tutorial/tutorial.py`, its imports, and public names before map activation; merge callable names into `__main__`. | A field-six payload such as `DialogPostProcess()` resolves both the level callback and engine globals not explicitly imported by that module. |
| Output parser | Preserve target, input, parameter, delay, fire count, Python payload, and extra field for both six- and seven-field rows. | Surveyed wires retain their exact targets and delays; no Python payload is shifted into the wrong column. |
| Event queue | Preserve ordering, delay, remaining fire count, caller, and activator; do not discard pending work when gameplay resolution is paused. | A traced relay chain fires once and in authored order after its delays. |
| Name resolution | Case-insensitive exact match; final-character `*` prefix match; handle `!self`, `!caller`, `!activator`, `!player`, `!picker`, and `!playercontroller`. | Patch helpers resolve `plus_*`/`basic_*`; choreography resolves its controller relationship. |
| Input dispatch | Resolve through the entity class and base-map input surface from one `AcceptInput` seam. | `Kill`, `ScriptHide`, and `ScriptUnhide` work through the base entity instead of per-class copies. |
| Missing target policy | A wire naming no entity is a non-fatal no-op with a diagnostic distinct from “receiver lacks input.” | Plans do not invent objects merely to silence authored dangling wires. |
| Touch ingress | Produce begin/end/trigger events for the allowed activator classes, observe wait/one-shot state, filters, hidden/disabled state, and per-class spawnflag meanings. | A player, NPC, or physics object activates only the intended volume. |
| `+use` ingress | Select the intended usable entity, expose its icon/locked state, preserve activator/caller, and dispatch the class-specific interaction. | Doors, elevator buttons, signs, switches, terminals, knobs, and containers do not collapse to decorative bodies. |
| Think/time | Drive timers, trigger wait gates, scripted sequences, delayed Python, fades, sounds, and autosave from the substrate clock. | Pausing/resuming dynamic resolution preserves rather than loses pending authored work. |
| Python exec/eval | Execute output payloads and scheduled statements; evaluate `logic_pythoncheck` and dialogue conditions in the shared namespace. Errors print and continue; failed checks resolve false. | One bad optional callback does not abort the map, and a failed expression does not become true. |
| State and persistence | Preserve `G`, morgue, player state, map-entity state, event queue state, controller relationships, inventory, and active tutorial systems across save/restore as required by their owning designs. | Restoring during a beat neither repeats consumed one-shots nor loses enabled follow-up triggers. |
| Map travel | Route `ChangeNow`/`ChangeMap` through the level-transition contract and landmark placement. | `LeaveTutorial()` reaches `sm_pawnshop_1` at landmark `newgame`. |

## 4. Trigger surface

| Class | Count | Outgoing wires | Authored role | Required semantics and planning consequence |
|---|---:|---:|---|---|
| `trigger_multiple` | 44 | 140 | Repeating proximity ingress for dialogue, popups, encounter gates, reset zones, and progression. | Start/end touch, `OnTrigger`, wait/re-arm, enabled/hidden state, activator flags, and `filtername` all affect correctness. Four begin hidden; all but one accept the player, while one uses the physics-only flag. |
| `trigger_once` | 11 | 29 | One-way tutorial beats whose side effects must not repeat. | Same filtering contract as `trigger_multiple`, plus consumed-state persistence. |
| `trigger_look` | 4 | 8 | Teach looking/aiming by holding view on a target for `0.5` seconds. | Requires a player-view ray/angle test, uninterrupted dwell time, target identity, and enable/disable state. All four begin disabled. |
| `trigger_hurt` | 3 | 0 | Environmental damage volumes. | Two author `damage=13`, one `damage=8`; two use damage type `8`, one `0`. Recovered cadence is an entry half-tick followed by `damage * 3` every three seconds, not arbitrary per-frame or half-second damage. |
| `trigger_changelevel` | 14 | 0 | Script-only travel to `sm_pawnshop_1`, `sp_theatre`, and optional patch/hunter destinations. | All carry spawnflag `2` (`NOTOUCH` for this class) and are driven through `ChangeNow`; five begin hidden. The class-specific flag meaning must not be inherited from generic trigger flags. |
| `trigger_autosave` | 1 | 0 | Establish a recovery point without player UI. | Must join the save architecture and avoid repeated saves while continuously occupied. |
| `trigger_inventory_check` | 1 | 3 | Emit `OnPlayerHasItem` for an inventory-gated beat. | Recovered `StartTouch` accepts only a base-filtered player entry, then searches ordinary slots and keyring case-insensitively. It does not poll, test quantity, or disable itself; this map authors the one-shot outputs. |
| `trigger_environmental_audio` | 16 | 0 | Change acoustic room/reverb state while crossing tutorial spaces. | All begin disabled and author room types `123` (8), `12` (1), `5` (3), `104` (2), `108` (1), and `11` (1). Plans must preserve trigger, SoundScheme `RoomDSP`, interior/exterior, and scripted-override precedence until RE30/RE31 close it. |
| `trigger_stealth_mod` | 3 | 0 | Modify stealth detection/scoring inside authored regions. | Needs the faithful stealth observer and score contract; it is not equivalent to simply hiding the player. |

### 4.1 Trigger fidelity risks to keep explicit

- `filtername` is data, not an editor hint. Ignoring it can activate unrelated actors or props.
- Spawnflag bits are class-specific. `trigger_changelevel` bit `2` means `NOTOUCH`; it must not be
  interpreted as the generic trigger `ALLOW_NPCS` bit.
- `ScriptHide` turns collision, drawing, and thinking off. A hidden trigger is not an enabled
  invisible volume.
- `trigger_hurt` entry, interval, force, direction, activator, and output behavior form one contract.
- A no-output trigger can still be load-bearing: environmental-audio and stealth triggers update
  system state rather than fire ordinary wires.

## 5. Prop and interactive-object surface

Static `.props` and entity props are different populations. The 809 static placement rows express
map look and collision; they do not automatically provide targetnames or per-instance I/O. The
addressable entity population carries the interaction contract.

| Group | Count | Authored role and API demand | Planning consequence |
|---|---:|---|---|
| Static `.props` placements | 809 | Baked/adopted scene dressing and collision. | Do not create an entity or event endpoint for every static placement unless authored entity data requires one. |
| `prop_dynamic` | 78 | Named animated/skinable/hideable objects; receives `SetAnimation`, `Skin`, `ScriptUnhide`, and `Kill`. | Preserve per-instance identity and animation completion/break outputs where authored. `solid` and shadow keyfields affect play and presentation. |
| `prop_dynamic_ornament` | 3 | Lightweight reveal/kill ornament used by state variants. | Base hidden-state semantics are load-bearing even without bespoke interaction. |
| `prop_physics` | 54 | Throwable/breakable objects and discipline demonstration targets. | Chaos body construction alone is insufficient: damage, `OnHealthChanged`, `OnBreak`, kill, and counter wiring must be observable. Ten begin hidden. |
| `phys_hinge` | 12 | Constrain physics props around authored pivots. | Requires stable entity binding and deterministic enough behavior for connected event tests. |
| `prop_button` | 3 | Elevator controls with `Lock`, `Unlock`, `SetState`, and pressed outputs. | Use focus, locked icon, button state, and mover command form one interaction. |
| `prop_switch` | 1 | `OnUse` toggles `chop_light`. | Requires a specialized use producer and a `light.Toggle` receiver; implementing only one end leaves the chain inert. |
| `prop_doorknob` | 11 | Lock state and begin/end use semantics; `office_knob` gates no-blood logic. | A mesh attached to a working door does not replace the knob's own inputs and outputs. |
| `prop_doorknob_electronic` | 2 | Electronic lock state and feedback. | Needs the same authoritative lock state exposed to use UI and scripting. |
| `prop_hacking` | 2 | Start/complete a terminal interaction; `tuthack` locks/unlocks the safe, changes visibility, and enables/disables follow-up triggers. | The generic terminal contract lives in `docs/vtmb/computer-terminals.md`; this map requires its `tuthack` worked chain. |
| `prop_sign` | 2 | Player use opens an authored document/sign window. | Needs a use producer and the same UI ownership path as `game_sign.OpenWindow`. |
| `item_container` | 1 | Receives `DeleteItems` and `SpawnItemInContainer`. | It owns the same real 224-slot item-entity inventory as a combat character. Here `DeleteItems` destroys all current contents and the delayed spawn creates/adds one new entity. |
| `item_container_animated` | 2 | Safe/container animation and use completion. | `tutsafe.OnUseEnd` calls `OnSafeEnd()`; item removal writes `G.Tut_Key` and enables Jack teleport logic. |
| `item_container_lock` | 1 | Lock/unlock/hide state around a container. | Lock state must govern both interaction and presentation. |
| Loose tutorial items | 2 | One lockpick and one Leopold interior key entity are explicit map items. | Need pickup, ownership, query, removal, and save/restore semantics. |
| `func_door` | 7 | Translating doors receive open/close and participate in gating. | Mover completion, obstruction, lock state, use state, and I/O must agree. |
| `func_door_rotating` | 29 | Hinged doors receive lock/unlock/open/close; many expose use/locked icons. | Door and doorknob state cannot diverge. |
| `func_elevator` | 1 | Receives `GotoFloor` from the three tutorial buttons. | Floor motion and button state need a traced completion boundary. |

### 5.1 Load-bearing prop chains

| Source | Authored chain | Intended result |
|---|---|---|
| `prop_switch` | `OnUse -> chop_light.Toggle` | Player demonstrates an ordinary environmental interaction. |
| `prop_hacking tuthack` | typed `Unlock`/`Lock` -> `OnTrigger0`/`OnTrigger1` -> safe lock, visibility and trigger state | The terminal transaction specified in `docs/vtmb/computer-terminals.md` opens the next inventory/container beat. |
| `item_container_animated tutsafe` | `OnUseEnd -> OnSafeEnd()`; item removal -> `G.Tut_Key=1` | Safe interaction advances the tutorial and exposes the acquired key. |
| `prop_doorknob office_knob` | use begin/end -> no-blood logic | Door interaction participates in feeding/blood tutorial state. |
| `prop_button` set | press/state -> `func_elevator.GotoFloor` and relays | Three controls move the tutorial elevator through its stages. |
| Breakable shell boxes | `OnBreak -> math_counter.Add` | Destruction count advances the relevant exercise. |
| Physics bottle | `OnHealthChanged -> hide/reveal clip and popup state` | Damage to a prop demonstrates combat/discipline effect and advances presentation. |

### 5.2 Inventory chain closure

The map-specific inventory graph is now joined to the recovered generic contract in
`docs/vtmb/inventory.md`:

| Tutorial object/branch | Exact authored and recovered behavior |
|---|---|
| `tut_lockpicks` | A loose `item_g_lockpick`; pickup retains the entity and changes owner/slot state. `spawnLockpicks()` first destroys a carried lockpick before spawning/relocating the loose entity. |
| `tutsafe` / `tutsafelock` | Safe `equip0` spawns a real chopshop-key entity owned by the safe. `Take` transfers it to the player and fires `OnItemRemove`, setting `G.Tut_Key=1` and enabling Jack's next teleport. The lock requires a key but authors `delete_key=0`. |
| `trig_popup_tireiron_check` | On a player entry with `item_w_tire_iron`, opens popup 35, disables itself immediately, and disables `trig_popup_tireiron` after 0.5 seconds. |
| `ammo_container` / `trig_more_ammo` | The container starts from five separately spawned `item_w_thirtyeight` entities. Touch destroys all remaining contents, then spawns exactly one new `.38` entity after 0.1 seconds. |
| Jack dialogue / `tutorial.py` | Dialogue grants/removes the `.38` and grants six reserve rounds when its loaded magazine count is zero; `tutorial.py` separately grants 12 reserve rounds. |
| `container_hunter` / hunter dialogue | The cache uses loot-mode take/give transfers for five authored weapons; `hunterv` uses `StartBarter(0,0)` vendor mode. |

## 6. Output-producer inventory

This table answers “what must originate events?” It is distinct from the receiver API below.

| Source class | Entities with outputs | Wires | Principal intent |
|---|---:|---:|---|
| `logic_relay` | 106 | 420 | Compose named tutorial transactions and fan out one beat into many side effects. |
| `trigger_multiple` | 43 | 140 | Repeating proximity ingress. |
| `game_sign` | 42 | 58 | Tutorial popup lifecycle and follow-up logic. |
| `func_door_rotating` | 18 | 47 | Door state/use completion and gate transitions. |
| `npc_maker` | 14 | 45 | Child found-player, damage, feeding, incapacitation, dialogue, and death lifecycle. |
| `events_player` | 2 | 36 | Player combat/dialogue, discipline, controller, frenzy, and morph lifecycle. |
| `func_door` | 5 | 33 | Translating-door completion/state events. |
| `trigger_once` | 11 | 29 | One-shot proximity ingress. |
| `events_world` | 1 | 25 | World policy, cops, masquerade, music, and global lifecycle. |
| `scripted_sequence` | 11 | 21 | Sequence begin/end milestones. |
| `func_elevator` | 1 | 17 | Floor movement and arrival transitions. |
| `env_fade` | 6 | 16 | Begin-fade milestones used to teleport actors. |
| `camera_keyframe` | 12 | 15 | Camera-track progress. |
| `prop_physics` | 6 | 14 | Health and break transitions. |
| `logic_auto` | 5 | 13 | Map-load initialization and patch gates. |
| `camera_track` | 4 | 12 | Camera playback completion/control restoration. |
| `logic_case_toggle` | 8 | 10 | Branch/cycle selection. |
| `logic_pythoncheck` | 5 | 8 | Python predicate true/false branches. |
| `logic_timer` | 6 | 8 | Repeating timed effects and control. |
| `trigger_look` | 4 | 8 | View-dwell completion. |
| `logic_choreographed_scene` | 1 | 7 | Scene completion and named VCD trigger points. |
| `math_counter` | 7 | 7 | Threshold events after kills, breaks, or actions. |
| `func_button` | 4 | 6 | Brush-button state/use milestones. |
| `prop_hacking` | 1 | 6 | Terminal interaction progress/completion. |
| `npc_VVampire` | 1 | 5 | Direct NPC dialogue/combat lifecycle. |
| `prop_button` | 3 | 4 | Elevator-button pressed events. |
| `item_container_animated` | 1 | 3 | Use/item-removal lifecycle. |
| `npc_VHumanCombatant` | 3 | 3 | Direct NPC lifecycle. |
| `prop_doorknob` | 1 | 3 | Use begin/end and lock-related logic. |
| `prop_dynamic` | 3 | 3 | Break/animation state. |
| `trigger_inventory_check` | 1 | 3 | Inventory predicate success. |
| `point_teleport` | 1 | 2 | Arrival-side effects. |
| `prop_switch` | 1 | 1 | Use completion. |

For `npc_maker`, “spawn a child” and “own the child's lifecycle outputs” are separate requirements.
The maker carries 24 `OnDeath`, 8 `OnFoundPlayer`, 5 `OnFedUponEnd`, 3 `OnDamaged`, 2
`OnFedUponBegin`, and individual incapacitation/dialogue outputs. A plan must define the stable
maker-child relationship and which object fires each output; copying only class/name/model into a
child does not establish that provenance.

## 7. Receiver API surface

Counts are authored calls in this map, not global popularity. Base inputs such as `Kill`,
`ScriptHide`, and `ScriptUnhide` should remain inherited inputs rather than per-class duplicates.

| Receiver class | Calls | Required inputs |
|---|---:|---|
| `logic_relay` | 137 | `Trigger` 96; `Disable` 24; `Enable` 14; `Kill` 3 |
| `trigger_multiple` | 79 | `Disable` 39; `Enable` 34; `ScriptHide` 4; `Kill` 2 |
| `ambient_generic` | 69 | `PlaySound` 33; `StopSound` 19; `Kill` 10; `Volume` 7 |
| `game_sign` | 55 | `OpenWindow` 51; `Kill` 4 |
| `npc_VVampire` | 53 | `StartPlayerDialog` 12; `TeleportToEntity` 8; `UseInteresting` 7; `WillTalk` 7; `ScriptUnhide` 6; `Kill` 4; `StartPlayerDialogRemote` 4; `TakeDamage` 3; `SetScriptedDiscipline` 2 |
| `scripted_sequence` | 43 | `BeginSequence` 33; `CancelSequence` 10 |
| `point_teleport` | 32 | `Teleport` 31; lowercase `teleport` 1 |
| `npc_maker` | 31 | `Spawn` 30; `Kill` 1 |
| `func_door_rotating` | 30 | `Lock` 15; `Close` 8; `Unlock` 4; `Open` 3 |
| `func_door` | 26 | `Open` 16; `Close` 6; `Kill` 4 |
| `env_fade` | 26 | `Fade` 26 |
| `env_particle` | 24 | `TurnOn` 14; `TurnOff` 9; `Kill` 1 |
| `math_counter` | 22 | `Add` 13; `SetValue` 9 |
| `prop_button` | 21 | `Unlock` 11; `Lock` 5; `SetState` 3; `Kill` 2 |
| `events_player` | 20 | `RemoveControllerNPC` 5; `CreateControllerNPC` 5; `RemoveDisciplinesNow` 4; `RemoveDisciplines` 3; `ClearDialogCombatTimers` 2; `MakePlayerUnkillable` 1 |
| `func_brush` | 14 | `ScriptHide` 11; `ScriptUnhide` 2; `Kill` 1 |
| `ambient_soundscheme` | 14 | `FadeIn` 7; `FadeOut` 6; `Disable` 1 |
| `trigger_changelevel` | 11 | `ChangeNow` 11 |
| `trigger_look` | 10 | `Enable` 5; `Disable` 5 |
| `prop_dynamic` | 10 | `SetAnimation` 4; `ScriptUnhide` 2; `Kill` 2; `Skin`/`skin` 2 |
| `logic_case_toggle` | 8 | `InValue` 7; `PickRandom` 1 |
| `camera_track` | 7 | `PlayAsCameraTarget` 3; `PlayAsCameraPosition` 3; `RestoreCameraToPlayerControl` 1 |
| `logic_timer` | 7 | `Disable` 4; `Enable` 3 |
| `item_container_lock` | 6 | `Lock` 2; `ScriptUnhide` 2; `Unlock` 1; `ScriptHide` 1 |
| `npc_VHumanCombatant` | 6 | `ScriptUnhide` 2; `FollowPatrolPath` 2; `SetupPatrolType` 2 |
| `trigger_once` | 5 | `Disable` 3; `Enable` 2 |
| `logic_pythoncheck` | 5 | `Test` 5 |
| `logic_choreographed_scene` | 5 | `Start` 3; `Cancel` 2 |
| `prop_doorknob` | 4 | `Lock` 3; `Unlock` 1 |
| `prop_dynamic_ornament` | 4 | `ScriptUnhide` 2; `Kill` 2 |
| `func_elevator` | 3 | `GotoFloor` 3 |
| `trigger_inventory_check` | 3 | `Disable` 2; `Enable` 1 |
| `prop_doorknob_electronic` | 2 | `Lock` 2 |
| `events_world` | 2 | `SetNoFrenzyArea` 2 |
| `camera_cinematic` | 2 | `StartShot` 1; `EndShot` 1 |
| `item_container` | 2 | `DeleteItems` 1; `SpawnItemInContainer` 1 |
| `trigger_hurt` | 2 | `Enable` 1; `Disable` 1 |
| `point_target` | 1 | `Kill` 1 |
| `point_explosion` | 1 | `Explode` 1 |
| `light` | 1 | `Toggle` 1 |
| `prop_physics` | 1 | `Kill` 1 |
| `env_shake` | 1 | `StartShake` 1 |

### 7.1 Receiver groups and intent

| Group | Receiver contract | Intent |
|---|---|---|
| Logic | Relay trigger/enable, timers, counters, cases, autos | Compose transaction-like beats without embedding all orchestration in Python. |
| Movers | Door open/close/lock, button state, elevator floor, brush visibility | Make world geometry and collision agree with tutorial progression. |
| NPC and scene | Talk, teleport, patrol, damage, discipline, sequence, choreography | Stage Jack and encounter actors while retaining character identity and lifecycle outputs. |
| Presentation | Sign windows, fade, particles, shake, explosion, camera, light | Communicate instructions and make scripted transitions legible. |
| Audio | Emitters and SoundScheme fades | Play lines/effects and move between authored ambient states. |
| Inventory | Container mutation and inventory predicates | Turn item ownership into progression state and dialogue availability. |
| World/player policy | Controller NPC, killability, disciplines, no-frenzy area | Change global rules for a bounded tutorial beat. |
| Travel | Teleport and changelevel | Move actors within the map, then leave for the real game. |

## 8. Python execution surface

### 8.1 Five executable paths

| Path | Form | Required namespace behavior |
|---|---|---|
| Output field six | Statement/call string on an entity output | Execute through `__main__` with the output's caller/activator context still valid. |
| `logic_pythoncheck` | Expression | Evaluate in `__main__`; error or non-integer result is false; fire `OnTrue` or `OnFalse`. |
| Dialogue condition | `dlgexpr` condition with optional engine skill check | Resolve player fields, `G`, character methods, and script helpers without treating the whole grammar as unrestricted Python. |
| Dialogue action | Assignment/call statements | Mutate `G`, player state, quests, inventory, and map entities. |
| `ScheduleTask` | Delayed source string | Evaluate later against the live `__main__` dictionary, not a captured module-local namespace. |

### 8.2 Native and reflected API groups demanded by the closure

| API group | Required surface | Map intent |
|---|---|---|
| Global lookup | `FindPlayer`, `FindEntityByName`, `FindEntitiesByName`, `FindEntitiesByClass` | Locate Jack, tutorial NPCs, relays, hidden variants, popups, and spawned entities. |
| Entity lifecycle | `CreateEntityNoSpawn`, `CallEntitySpawn`, `Kill` | Spawn lockpicks/cans and manage temporary actors. |
| Entity identity/transform | `GetOrigin`, `GetAngles`, `GetCenter`, `SetOrigin`, `SetAngles`, `SetModel`, `SetName` | Position and identify scripted objects without a second coordinate conversion. |
| Scheduling/travel | `ScheduleTask`, `ChangeMap` | Defer beat cleanup and leave the tutorial through the normal transition service. |
| Entity I/O reflection | `Trigger`, `Enable`, `Disable`, `ScriptHide`, `ScriptUnhide`, `BeginSequence`, `CancelSequence`, mover/audio/dialogue inputs | Let scripts use the same datamap/input bus as Hammer wires. |
| Player/character facts | `IsMale`, `CalcFeat`, clan field plus `IsClan` helper, money, humanity, blood, XP, quest inputs | Select tutorial variants and apply dialogue consequences. |
| Dialogue | `StartPlayerDialog`, `StartPlayerDialogRemote`, `PlayDialogFile` | Run Jack, guard, and hunter conversations and direct lines. |
| Inventory | `HasItem`, `GiveItem`, `RemoveItem`, `AmmoCount`, `GiveAmmo` | Gate and mutate lockpick, key, weapon, and ammunition state under the recovered `docs/vtmb/inventory.md` contract. |
| Containers | `SpawnItemInContainer`, `DeleteItems` | Refill and consume real item-entity contents; loot take/give uses authoritative server transfer. |
| NPC policy | `SetDisposition`, `TakeDamage`, patrol inputs, feeding/death/incapacitation events | Make dialogue and combat consequences affect later behavior. |
| Disciplines | `SetScriptedDiscipline`, `ClearActiveDisciplines`, player-event discipline inputs | Demonstrate, force, and reset tutorial powers. |
| Presentation | `SpawnTempParticle`, sign windows, fades, particles, audio, camera inputs | Support callbacks that communicate the state change to the player. |
| Console compatibility | `ccmd`, `cvar` alias tables | Run patch helpers such as `unhidePlus`; tolerate Source-only calls such as `wc_create` when the Unreal equivalent is offline. |
| Global state | `G` attribute/subscript reads and writes, `keys()`, default-on-miss `0`, `morgue` | Let map outputs, scripts, and dialogue share a persistent integer-valued state bag. |

### 8.3 Field-six composition

The 133 field-six payloads divide into:

| Payload form | Count | Examples of intent |
|---|---:|---|
| Direct `G` assignment/update | 49 | Advance tutorial counters, mark kills/items/actions, select popup and patch state. |
| Function or global API call | 83 | Spawn helpers, reset blood/state, process dialogue, handle deaths, leave the map. |
| Console compatibility call | 1 | `ccmd.wc_create`; runtime cubemap creation is replaced by the offline bake. |

Map-used callbacks defined by `tutorial.py` include:

`ResetDiscBlood`, `ResetBlood`, `OnKillDisc1`, `OnKillDisc2`, `OnKillDisc3`,
`jackflashChopshopOffice`, `spawnLockpicks`, `spawnKeycard`, `OnSafeEnd`, `spawnCans`,
`OnJackFloat`, `SetClanPopups`, `OnDiscGuys`, `OnOpenDoorDisc2`, `OnOpenDoorDisc3`,
`OnBluebloodDeath`, `OnMasqueradeEnd`, `OnPopupFrenzy`, `OnBumDeath`, `LeaveTutorial`, and
`DialogPostProcess`.

The closure also calls shared or patch helpers including `testGuard`, `civilianDeath`,
`RemoveMac10`, `hunterQuests`, `masterRefill`, `unhidePlus`, `IsIdling`, player-event callbacks,
and world-event callbacks. Their availability depends on importing the shared script surface into
the same namespace as the level module.

### 8.4 `logic_pythoncheck` predicates

| Entity | Predicate | Branch intent |
|---|---|---|
| `distract_check` | `G.Sabbat_Incapacitated == 1` | Skip or begin a sequence based on incapacitation. |
| `dialogue_check` | `G.Patch_Plus == 0` | Show a baseline-only popup. |
| `kill_check` | `G.Tut_Jack == 9` | Enable the bathroom dialogue at the correct Jack beat. |
| `plus_check` | `G.Patch_Plus == 1` | Reveal Patch Plus wolf-halo content. |
| `linux_check` | `G.Linux_Wine == 1` | Select the compatibility popup branch. |

### 8.5 Tutorial state closure

The map directly writes 17 distinct flags:

`Sabbat_Incapacitated`, `Sabbat_Kills`, `Tut_Aggfeed`, `Tut_Ashot`, `Tut_Chopshop`,
`Tut_Dogdoor`, `Tut_Elev`, `Tut_Jack`, `Tut_Key`, `Tut_Melee`, `Tut_Officedoor`, `Tut_Ratfeed`,
`Tut_Stealthkill`, `Tut_Throw`, `Tutorial_Blueblood`, `Tutorial_Bum`, and
`Tutorial_Discflags`.

The level module additionally writes 11 flags:

`Jack_Ammo`, `Player_Cheated`, `Popup_Inventory`, `Popup_Masquerade`, `Popup_Quest`,
`Popup_Unarmed`, `Popup_Use`, `Story_State`, `Tut_Gun`, `Tut_Patch`, and `Tutorial_Feeding`.

The four dialogues add overlapping inventory, hunter, quest, and optional-patch flags. The union is
44 keys, so a fixed C++ tutorial struct is not a faithful substitute for the engine-owned open
mapping. `saveState()` and `state()` also depend on `G.keys()` plus subscript access to snapshot and
restore arbitrary keys.

## 9. Dialogue API demand

The four map-referenced dialogue files contain 1,420 rows. These are the script/global calls in
their conditions and actions that materially extend the `.ents` surface.

| API/helper | Occurrences | Intent |
|---|---:|---|
| `SetQuest` | 27 | Advance or complete the tutorial and optional quests. |
| `IsClan` | 19 | Select clan-specific instruction and discipline branches. |
| `SetDisposition` | 14 | Change NPC attitude after dialogue choices or player actions. |
| `HasItem` | 11 | Gate dialogue on keys, weapons, or other inventory. |
| `LeaveTutorialShort` | 4 | Skip/short-circuit the remaining tutorial. |
| `MoneyAdd` | 4 | Apply dialogue economy effects. |
| `AmmoCount` | 2 | Branch on available ammunition. |
| `RemoveItem` | 2 | Consume an owned item. |
| `Bloodloss` | 2 | Apply blood cost/consequence. |
| `dialogParticles` | 2 | Spawn temporary dialogue presentation effects. |
| `StartBarter` | 2 | Enter optional hunter/barter flow. |
| `GiveItem` | 1 | Award an item. |
| `GiveAmmo` | 1 | Award ammunition. |
| `AwardExperience` | 1 | Apply an experience reward. |

Dialogue is therefore not “UI after the game logic.” Its conditions and actions own required
inventory, quest, economy, disposition, particle, and travel calls. A plan that validates only map
outputs misses these branches.

## 10. Patch-first dangling Python calls

Five field-six expressions in the hash-pinned map do not resolve to a definition in any exported
script in the joined closure.

| Source output | Missing name | Intent inference | Required treatment |
|---|---|---|---|
| `trig_popup_obfuscate.OnStartTouch` | `OnApproachThug1()` | Prepare the first Obfuscate/thug lesson. | Confirm against the matching patch version; restore/transform the missing helper or retain the retail error deliberately. |
| `trig_popup_melee.OnStartTouch` | `OnPreMelee()` | Prepare the melee lesson. | Same evidence gate. |
| `trig_popup_rats.OnStartTouch` | `OnPreRats()` | Prepare the rat-feeding lesson. | Same evidence gate. |
| `trig_popup_physicsdisc.OnStartTouch` | `OnPostPhysics()` | Transition after the physics-discipline lesson. | Same evidence gate. |
| `popup_7.OnUseEnd` | `resetState()` | Restore a prior tutorial snapshot after popup use. | Determine whether case/name drift or a missing import caused the authored error. |

These are data/script-closure defects, not native API gaps. VtMB's Python error policy prints and
continues, so the literal retail/patch behavior may be a non-fatal missed side effect. Do not invent
the function body from its name without matching source or a controlled original-runtime trace.

One additional patch-first call resolves as Python but targets the wrong native receiver:
`spawnKeycard()` finds `tutsafelock` and calls `SpawnItemInContainer` on it. That input belongs to
the container, not `item_container_lock`; no forwarding path was found. Preserve it as a likely
non-fatal missed side effect unless matching evidence proves otherwise, rather than silently
retargeting it to `tutsafe`.

## 11. Authored tutorial orchestration

### 11.1 Map-load autos

Five `logic_auto` entities establish the starting policy and optional patch state:

| Initialization group | Authored actions and intent |
|---|---|
| Core player/world policy | Make the player unkillable, set Jack `WillTalk 0`, and mark the tutorial as a no-frenzy area. |
| Source cubemap compatibility | Schedule `ccmd.wc_create`; Unreal supplies the map look through the offline bake instead. |
| Patch patrol setup | Configure/unhide optional patrol NPCs. |
| Patch content setup | `masterRefill('container_hunter 5')`, lock front doors, call `unhidePlus()`, and test `G.Linux_Wine`. |

### 11.2 Core beat dependency chain

| Beat boundary | Event dependency | Required externally visible result |
|---|---|---|
| Tutorial placement | Landmark `tutorial` seats the player at the porch. | Player and Jack start in their authored placements without an invented arrival callback. |
| First beat | Leaving `trig_off_porch` fires Jack `WillTalk 1`, remote dialogue `256`, `blueblood_maker.Spawn`, and `events_player.CreateControllerNPC`. | Jack dialogue opens and the encounter actor/controller exist. |
| Dialogue progression | Jack `OnDialogEnd` calls `DialogPostProcess()`, which branches on `G.Tut_Jack`. | The next sequence/trigger/popup is enabled exactly once for that beat. |
| Second warp | `teleport_fade.OnBeginFade` teleports player and Jack to the alley. | Both actors arrive after the authored fade, with current progression state intact. |
| Feeding continuation | Feeding on the spawned blueblood reaches maker-owned `OnFedUponBegin`/`OnFedUponEnd`. | `G.Tutorial_Blueblood=1`; `trig_dialog_outside_chopshop` becomes enabled. |
| Chopshop and office | Dialogue, door/knob use, safe/hacking, item and elevator events advance `G.Tut_Jack`. | Each world interaction unlocks only its authored next beat. |
| Combat/discipline lessons | Damage, death, incapacitation, break, stealth, rat-feeding, weapon, and discipline events update counters/flags. | Jack's dialogue and tutorial popups follow observed outcomes rather than scripted time alone. |
| Exit | `LeaveTutorial()` sets story/quest/killability state and calls `ChangeMap` through `trig_leave_tutorial`. | Travel reaches `sm_pawnshop_1` at landmark `newgame`. |

`teleport_very_beginning` carries `OnEnterMapHere` data, but the recovered owner of that output is
`info_landmark`, not `point_teleport`. The port retains the inert wire rather than broadening the
teleport API to make it fire.

## 12. Implementation inspection anchors

The source is the as-built record, so a phase plan rereads these seams instead of treating this
brief as an implementation-status snapshot. The questions below keep that inspection focused.

| Inspection area | Question for the phase plan | Invariant to preserve |
|---|---|---|
| Event spine | Where do touch, use, think, queue delivery, Python execution, and `AcceptInput` meet? | The tutorial uses the shared entity-world and event-queue seams, never a parallel map-specific dispatcher. |
| Repeating/one-shot triggers | Where are activator flags, `filtername`, wait, hidden/disabled, and consumed state enforced? | One trigger filter contract serves every map while respecting class-specific spawnflags. |
| Hurt trigger | Where are entry damage, periodic damage, victim outputs, force, and direction computed? | The implementation matches the recovered half-tick/three-second contract before tutorial damage is acceptance evidence. |
| Dynamic props | Where are `solid`, shadow keyfields, animation, health, break, and physics behavior adopted? | Gameplay collision and output production are tested separately from visual/shadow acceptance. |
| Specialized props | Which classes own switch, sign, hacking, doorknob, button, and container use/lock state? | Each class implements its general input/output contract rather than a targetname-based tutorial special case. |
| NPC maker | How is a spawned child associated with the maker that owns lifecycle outputs? | Feeding, damage, incapacitation, found-player, dialogue, and death events retain stable caller/activator provenance. |
| Dialogue | Where do parse, condition/action execution, session closure, UI, line audio, lips, and facial state meet? | Logic acceptance stays distinguishable from final presentation acceptance. |
| Inventory | Does the current source route every item query and mutation through one service matching `docs/vtmb/inventory.md`? | Python, dialogue, triggers, pickups, containers and save/restore observe the same entity, stack, keyring, equipment and ammo state. |
| Player/world event buses | What real domain transition produces each discipline, frenzy, morph, cop, masquerade, and dialogue/combat output? | Acceptance fires outputs from their real producers, not a debug shortcut. |
| Scene/presentation receivers | Which services own camera, fade, particles, sound, shake, explosion, light, and environmental-room state? | Every logical event can be joined to a visible/audible result without merging ownership. |

Primary inspection anchors:

- `Source/ElysiumUE/Private/Substrate/ElysiumEntityWorld.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumStarterClasses.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumPropClasses.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcClasses.cpp`
- `Source/ElysiumUE/Private/Scripting/ElysiumScriptNatives.cpp`
- `Source/ElysiumUE/Private/Audio/ElysiumSoundScheme.cpp`

## 13. Planning slices derived from the authored graph

These are dependency slices, not roadmap rows or completion claims. The master roadmap decides
their sequencing and status.

| Slice | Required contract | Map-local acceptance evidence |
|---|---|---|
| Event transport | Parse and queue every wire; resolve names/aliases; dispatch inherited and class inputs; execute field six. | Structured trace for all outputs from one representative relay transaction, including delay, caller, activator, and Python. |
| Trigger fidelity | Player/NPC/physics gates, `filtername`, once/wait state, look dwell, hurt cadence, autosave, inventory, stealth, environmental audio. | Positive and negative activation cases for every trigger class present in the map. |
| NPC lifecycle and feeding | Stable maker-child ownership; use/feed start/end; damage; incapacitation; death; dialog; perception. | Blueblood feeding causes maker `OnFedUponBegin` then `OnFedUponEnd`, updates state, and enables the chopshop dialogue trigger without debug firing. |
| Inventory and containers | Implement the closed `docs/vtmb/inventory.md` entity/stack/keyring/ammo/transfer contract across pickup, scripts, dialogue, triggers, containers and save/restore. | Lockpick, key, safe, weapon, ammo, and dialogue branches all agree on the same inventory state. |
| Specialized interactions | Switch, sign, doorknob, hacking, button, container, door/elevator coupling. | Each `+use` chain produces its authored output and visible/physical side effect once. |
| Combat and disciplines | Player/NPC/prop damage, weapons, discipline forcing/reset, stealth, frenzy, dispositions, break/death outputs. | Later tutorial counters and Jack branches advance only from actual player actions. |
| Dialogue and quests | Condition/action grammar, line playback, choice UI, quest/economy/reward calls, `OnDialogEnd`. | All four referenced dialogue files can traverse their reachable tutorial/optional paths with correct state changes. |
| Choreography and cameras | Scripted sequence, choreographed-scene trigger points, controller NPC, camera tracks/cinematic shots, restoration. | Jack-versus-Sabbat scene fires VCD milestones, effects, audio, cleanup, and returns camera control. |
| Audio and presentation | Emitters, SoundSchemes, environmental rooms, direct dialogue audio, signs, particles, fade, shake, explosion, light toggle. | Each event has both a traceable logical transition and the intended audible/visible result. |
| Persistence and travel | Save/restore of consumed triggers, queued work, `G`, inventory, entity state, player policy; tutorial exit. | Save/reload at representative beats neither skips nor repeats progress; final exit seats the player at the pawnshop landmark. |
| Patch-data closure | Version-matched callback resolution and optional content classification. | No reachable field-six call raises unexpectedly; intentionally broken authored calls are documented as such. |

## 14. Acceptance trace schema

A reusable play-path trace should record enough information to distinguish “output never produced,”
“target not found,” “input absent,” “receiver refused,” and “side effect invisible.” At minimum:

| Field group | Required fields |
|---|---|
| Map/run identity | map epoch, export hash, build identity, patch/retail baseline, save origin |
| Event identity | substrate time, source entity id/name/class, output name, ordinal |
| Delivery | target expression, resolved target ids, input, parameter, delay, remaining fire count |
| Context | caller id, activator id/class, player/NPC/physics classification |
| Python | execution path, source string or stable hash, result/error, affected `G` keys |
| Receiver | receiver class, input resolution result, enabled/hidden/consumed state before and after |
| Domain state | inventory delta, quest delta, health/blood delta, disposition/discipline state, maker-child relationship |
| Presentation | dialogue line/session, audio handle, camera mode, particle/effect handle, mover state |
| Travel/save | destination/landmark, save transaction, restored entity/event relationship ids |

The acceptance report should join this trace to the static 1,028-wire inventory and classify every
wire in the chosen scope as observed, conditionally unreachable, intentionally dangling, blocked by
an unmet prerequisite, or still unexplained. “No error in the log” is not closure evidence.

## 15. Open research questions

1. What exact retail object owns and fires an `npc_maker` child's feeding, damage,
   incapacitation, found-player, dialogue, and death outputs?
2. What are the complete datamaps and use-state transitions for `prop_switch`, `prop_doorknob`,
   and `prop_sign`? `docs/vtmb/computer-terminals.md` owns the corresponding terminal questions;
   this brief needs only the subset exercised by `tuthack`.
3. Which tutorial `trigger_multiple` volumes use `filtername`, and what entities pass each filter
   in retail?
4. How do damage type, velocity mode, force, and victim classification affect the three tutorial
   `trigger_hurt` volumes beyond their recovered timing?
5. What is the precedence between `trigger_environmental_audio`, SoundScheme `RoomDSP`,
   interior/exterior state, and scripted audio overrides? RE30/RE31 own the general answer.
6. Are the five unresolved callback names version skew, missing patch imports, case drift, or
   deliberately shipped errors?
7. Which optional hunter/Patch Plus paths are reachable during the normal tutorial, and which
   require later story-state re-entry?
8. Which outputs rely on actual combat/AI perception, and which are script-forced during the
   tutorial to make the lesson deterministic?
9. What save/restore points were supported by the original tutorial, especially during feeding,
   dialogue, choreography, and elevator movement?

## 16. Ownership and consumption

- Confirmed generic I/O, trigger, hidden-state, name-matching, maker, or prop behavior is written
  into `docs/vtmb/entity_io.md` or another owning VtMB topic.
- Confirmed Python-host mechanics are written into `docs/vtmb/python_bridge.md`.
- Confirmed function signatures and call counts are written into `docs/vtmb/script_api.md`.
- Confirmed item, keyring, ammo, pickup/drop, container and barter behavior is written into
  `docs/vtmb/inventory.md`.
- Confirmed tutorial progression and RPG semantics are written into
  `docs/vtmb/game_runtime.md`.
- Confirmed audio behavior is written into `docs/vtmb/audio_pipeline.md`; Unreal integration stays
  in `docs/architecture/audio-architecture.md`.
- Unreal system seams remain in the appropriate `docs/architecture/` document.
- Priority, task identifiers, and completion state remain only in `docs/project/roadmap.md` and its
  declared scoped subtrackers.

This brief remains the cross-system demand inventory. It should be regenerated or corrected when
the patch-first input hashes change, a retail trace contradicts an intent inference, or a new
reachable script/dialogue dependency is discovered.
