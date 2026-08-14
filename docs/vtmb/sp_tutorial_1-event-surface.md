# `sp_tutorial_1` event-surface research brief

## Authored triggers, interactive props, Python, dialogue, and planning closure

**Map:** `sp_tutorial_1`
**Baseline:** patch-first export resolved from the user's installed game
**Document role:** cross-system research brief; not a status tracker or a generic behavior owner
**Last reviewed:** 2026-08-11

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
| `scenes/character/dlg/main characters/jack_tutorial/line1001_col_e.vcd` | 875 | `5A723836F5E1779D2289E03E5E691DDE77CBB618D5FAB1E10501F399E2F293EF` |
| `scenes/character/dlg/main characters/jack_tutorial/line1006_col_e.vcd` | 693 | `684D61C8C95AAF265E8EA4FD0E023E67BFBE16066E9AB962D51C728E4332A174` |
| `scenes/cinematic/tutorial/jack_vs_sabbat.vcd` | 1,128 | `B396680CDBACD528A045832F9B078778DC18A89122606EDA2E2AB23C5D35D19D` |

Native behavior is pinned separately to retail `Vampire/dlls/vampire.dll`: 7,860,281 bytes,
SHA-256 `c546f4de2003624d72f54d03805e0dbe1d8157231adcc62368ff53fe6e48a76f`, MD5
`1a10efcfe332036a9ee60bce223303db`. The reproducible address seeds and questions are tracked in
`research/cases/tutorial-event-resolution/`; generated decompilation remains below
`ELYSIUM_WORK_ROOT/research/ghidra/`.

Retail/UP character comparison and the native construct/Spawn/Activate/`NPCInitThink` join are
tracked separately in `research/cases/tutorial-npc-bootstrap/` (RE47). Its generated ledger remains
below `ELYSIUM_WORK_ROOT/research/tutorial-npc-bootstrap.json`.

Primary reproduction command:

```powershell
uv run elysium research ent_survey --patch --map sp_tutorial_1
uv run elysium research tutorial_npc_bootstrap --json E:\elysium-work\research\tutorial-npc-bootstrap.json
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

### 2.2 Retail, Unofficial Patch, and Plus character baselines

RE47 compares the original game files with the installed Unofficial Patch 11.5 replacement before
joining either map to native startup. This distinction is load-bearing: `Unofficial_Patch` replaces
the BSP and level script, while Basic and Plus are two runtime profiles over that one replacement
map. The installed `user.cfg` selects Plus with `alias patchtype "setPlus()"`; it is not a third BSP.

| Variant input | Bytes | SHA-256 |
|---|---:|---|
| Retail `Vampire/maps/sp_tutorial_1.bsp` | 5,123,508 | `026cd8ee971c64c5d595e72285351782a402d83f2a4c9fcc3a0d9dd5a4477770` |
| Retail `Vampire/python/tutorial/tutorial.py` | 19,570 | `e5f78fb50cbc0c592b5c875d7aecf56d2d5a902874d3a5ea8ae691df0e5676fe` |
| UP `Unofficial_Patch/maps/sp_tutorial_1.bsp` | 19,034,426 | `6cc1b53d0dd9b4b511107b72a9a3fb35b6a7b16f6a7ae1067f5b540fb00ac16c` |
| UP `Unofficial_Patch/python/tutorial/tutorial.py` | 21,972 | `22bb91612f80123864fd4353e4c022639af28b11ff71c43f4e155924e3ca6a01` |

The retail BSP contains 1,226 entities, 13 direct `npc_*` characters, 13 `npc_maker` templates and
one `logic_auto`. The UP BSP contains 1,868 entities, 20 direct characters, 14 maker templates and
five autos. The thirteen retail direct characters are Jack, two thugs, five Sabbat redshirts, the
Sheriff, three rats and `sabbat_redshirt_2_proxy`. UP retains that cohort, adds seven direct
hunter/Society actors in its remote Society section, and adds `talkguy_maker`. The maker templates
do **not** represent live children at map load: all fourteen UP makers start disabled, and their
children are allocated only when an authored `Spawn` is later accepted.

| Initial direct cohort | Original retail | UP replacement | Load-time state |
|---|---:|---:|---|
| Jack | 1 | 1 | Visible direct actor. |
| `thug_2`, `thug_3` | 2 | 2 | `StartHidden=1`. |
| `sabbat_redshirt_1` … `_5`, Sheriff | 6 | 6 | `StartHidden=1`. |
| Three `rat_2` rows | 3 | 3 | Two visible, one `StartHidden=1`; the repeated targetname is intentional data and not one actor overwritten three times. |
| `sabbat_redshirt_2_proxy` | 1 | 1 | Visible direct actor. |
| Hunter/Society cohort | 0 | 7 | Direct actors in the added remote Society section; `Hunter1`, `sentry2`, `mercenary_upstairs`, `monk_upstairs_podium`, `condotierre_upstairs`, `sentry3`, and `Hunterv` do not start hidden. |
| Maker children | 0 live / 13 templates | 0 live / 14 templates | Every maker starts disabled; explicit later `Spawn` owns allocation. |

UP changes ten shared named-character records. Most changes add explicit gender or equipment;
`thug_3` receives its alternate model; two late Sabbat actors move slightly. The opening-critical
difference is Jack:

| Field | Original retail BSP | UP replacement BSP |
|---|---|---|
| Entity index | 401 | 1363 |
| Source origin / angles | `-221 -258 -40` / `0 90 0` | `144 7352 -199` / `0 190 0` |
| Initial visibility | no `StartHidden` key | no `StartHidden` key |
| Equipment additions | none | `item_w_claws` |

Both variants otherwise author the same opening-relevant identity: `npc_VVampire`,
`Tutorial_Jack`, the Smiling Jack model, `spawnflags=4`, invincibility, `no_alert_state=1`,
`Neutral` / `D_NU 0`, `npc_perception=3`, `vision=0`, `hearing=0.10`,
`use_interesting=1`, interesting group 32, `default_camera=Jack`, and `jack_tutorial.dlg`.
UP therefore relocates and re-yaws Jack in authored map data before any dialogue exists.

Group 32 contains exactly three authored `intersting_place` rows in the UP map:
`ip_by_window` at `85 132 112` (`wall_lean`, enabled), `ip_0b2` at `-208 -16 -40`
(`wall_lean`, disabled), and `ip_lean_1` at `-221 -258 -32` (`wall_lean`, enabled). The last is
at original retail Jack's site; none is a porch node near UP Jack at `144 7352 -199`. The authored
`min_bounds`/`max_bounds` values remain occupancy geometry, not evidence for a global search radius.
A rebuild must neither manufacture a porch node nor reinterpret those bounds to keep Jack there.

The coincidence at `ip_lean_1` is exact rather than approximate: original retail Jack spawns at
`-221 -258 -40` with angles `0 90 0`, the node sits at `-221 -258 -32` with angles `0 90 0`, and it
carries `match_orientation 1`. Retail Jack is authored to stand on his own `wall_lean` in the pose
and facing the node names, which is what a rebuild has to account for before attributing his
pre-dialogue idle to a disposition stance or to a plain `ACT_IDLE`.

The eligibility gate is now recovered (`npc-ai-reverse-engineering.md` → "Interesting-place
eligibility"): a straight-line 10,000 units, with no pathfinding and no line-of-sight test in the
find stage. UP Jack sits 7,227 and 7,621 units from the two enabled group-32 nodes, so
`TASK_FIND_INTERESTING_PLACE` **succeeds** for him and hands back a goal across the map. He
demonstrably does not walk there, so the stop lies past the find stage — in
`SCHED_TROIKA_WALK_TO_INTERESTING_PLACE` failing to its `Idle_Stand` fail-schedule, in
`m_eInterestingPlaceMode` (`+0x6304`, an unsurveyed save field), or in a UP script mutation of
`use_interesting`. That is a named question for the controlled capture, not a licence to assume he
falls through to a stance idle.
The recovered `CNPC_VVampire` spawn chain caches the resulting live origin and angles and contains
no turn-to-player operation. `NPCInitThink` can repair ground placement but likewise contains no
player-facing write. Static evidence consequently does **not** justify rotating Jack at spawn or
dialogue acquisition. His first rendered sequence, the first selected AI schedule, and any later
motor/gaze turn before the porch trigger remain controlled-capture questions.

The player is not one of these BSP NPC rows. Map travel supplies the existing player and landmark
placement. `CreateControllerNPC` is also not load-time population: the UP porch trigger creates it
only when the player leaves the volume. A rebuild must not pre-spawn that stand-in or count it as
Jack's initial character state.

The first authored mutation also differs by map version. Original retail `trig_off_porch`
(BSP index 479) sends `Jack.WillTalk 1` at `t0` and `Jack.StartPlayerDialog 256` at `t+0.1`; it has
no load-time dialogue. UP's relocated `trig_off_porch` (index 1367) sends `WillTalk 1`,
`StartPlayerDialogRemote 256`, and `UseInteresting 0` at `t0` while separately spawning the
Blueblood and creating the controller stand-in. The UP main trigger is already the authored active
path; Basic explicitly enables `trig_off_porch_basic` after its fade. `setPlus()` does not rewrite
either trigger.

## 3. Load-bearing event API contract

Every system plan that consumes this brief needs to preserve this common contract.

| Contract surface | Required behavior | Map intent and acceptance observation |
|---|---|---|
| Level-script bootstrap | Load `tutorial/tutorial.py`, its imports, and public names before map activation; merge callable names into `__main__`. | A field-six payload such as `DialogPostProcess()` resolves both the level callback and engine globals not explicitly imported by that module. |
| Output parser | Consume exactly target, input, parameter, delay, fire count and Python from fields 0–5; ignore any trailing field without shifting field 5. Treat authored `times` 0 and -1 as unlimited. | Repeated rows for one output fire in reverse lump/export order because retail prepends parsed actions. |
| Event queue | Sort by deadline with equal-time FIFO; deliver all named targets, then field-5 Python, then a direct handle; recursively drain zero-delay work breadth-first in the same pass. | Delays follow game `curtime`. Retail has no gameplay budget, so a zero-delay cycle can starve/hang a frame; Elysium's 10,000-event cap is a visible safety divergence. |
| Name resolution | Case-insensitive exact match; final-character `*` prefix match; handle `!self`, `!caller`, `!activator`, `!player`, `!picker`, and `!playercontroller`. | Patch helpers resolve `plus_*`/`basic_*`; choreography resolves its controller relationship. |
| Input dispatch | Resolve through the entity class and base-map input surface from one `AcceptInput` seam. | `Kill`, `ScriptHide`, and `ScriptUnhide` work through the base entity instead of per-class copies. |
| Missing target policy | A wire naming no entity is a non-fatal no-op with a diagnostic distinct from “receiver lacks input.” | Plans do not invent objects merely to silence authored dangling wires. |
| Touch ingress | Produce begin/end/trigger events for the allowed activator classes, observe wait/one-shot state, filters, hidden/disabled state, per-class spawnflag meanings, and the movement-phase boundary after teleport. | A player, NPC, or physics object activates only the intended volume; rejected begins never poison edge deduplication. |
| `+use` ingress | Select the intended usable entity, expose its icon/locked state, preserve activator/caller, and dispatch the class-specific interaction. | Doors, elevator buttons, signs, switches, terminals, knobs, and containers do not collapse to decorative bodies. |
| Think/time | Drive timers, trigger wait gates, scripted sequences, delayed Python, fades, sounds, and autosave from the substrate clock. | Pausing/resuming dynamic resolution preserves rather than loses pending authored work. |
| Python exec/eval | Execute output payloads and scheduled statements; evaluate `logic_pythoncheck` and dialogue conditions in the shared namespace. Errors print and continue; failed checks resolve false. | One bad optional callback does not abort the map, and a failed expression does not become true. |
| State and persistence | Preserve `G`, morgue, player state, map-entity state, event queue state, controller relationships, inventory, and active tutorial systems across save/restore as required by their owning designs. | Restoring during a beat neither repeats consumed one-shots nor loses enabled follow-up triggers. |
| Map travel | Route `ChangeNow`/`ChangeMap` through the level-transition contract and landmark placement. | `LeaveTutorial()` reaches `sm_pawnshop_1` at landmark `newgame`. |

### 3.1 One host-frame event order

1. The current user command runs player movement first. Movement/contact processing can call trigger
   producers, but their outputs only enqueue actions.
2. `GameFrame` runs the entity-think pass. Timers, fades, movers, scripted sequences and VCD scenes
   can enqueue more actions. All entity thinks finish before any of those actions are delivered.
3. `CEventQueue::ServiceEvents` drains every entry due at current `curtime`, including zero-delay
   work produced by a receiver in the same pass. Python has no separate per-frame tick.
4. Host/render progression resumes only when the queue returns. A retail zero-delay cycle prevents
   that return; future deadlines cannot become due because game time is not advancing inside drain.

`GameFrame(simulating=false)` narrows the think pass but does not skip queue service. `host_timescale`
changes real-time duration through `curtime`; a global game-clock pause freezes deadlines. When an
enqueue observes `curtime` moving backward, retail shifts that new deadline forward by the rollback
amount plus 0.01 seconds.

A `point_teleport` delivered in step 3 changes the transform immediately but cannot retroactively
rerun step 1's touch reconciliation. Its destination contacts arrive at a later collision/movement
opportunity. Conversely, a reflected entity input called from Python is synchronous *inside* the
current step-3 Python handler; any output it fires rejoins the equal-time tail of the queue.

## 4. Trigger surface

| Class | Count | Outgoing wires | Authored role | Required semantics and planning consequence |
|---|---:|---:|---|---|
| `trigger_multiple` | 44 | 140 | Repeating proximity ingress for dialogue, popups, encounter gates, reset zones, and progression. | Start/end touch, `OnTrigger`, wait/re-arm, enabled/hidden state and activator flags affect correctness. Four begin hidden; all but one accept the player, while one uses the physics-only flag. This map authors no `filtername`. |
| `trigger_once` | 11 | 29 | One-way tutorial beats whose side effects must not repeat. | Same filtering contract as `trigger_multiple`, plus consumed-state persistence. |
| `trigger_look` | 4 | 8 | Teach looking/aiming by holding view on a target for `0.5` seconds. | Requires a player-view ray/angle test, uninterrupted dwell time, target identity, and enable/disable state. All four begin disabled. |
| `trigger_hurt` | 3 | 0 | Environmental damage volumes. | Two author `damage=13`, one `damage=8`; two use damage type `8`, one `0`. Recovered cadence is an entry half-tick followed by `damage * 3` every three seconds, not arbitrary per-frame or half-second damage. |
| `trigger_changelevel` | 14 | 0 | Script-only travel to `sm_pawnshop_1`, `sp_theatre`, and optional patch/hunter destinations. | All carry spawnflag `2` (`NOTOUCH` for this class) and are driven through `ChangeNow`; five begin hidden. The class-specific flag meaning must not be inherited from generic trigger flags. |
| `trigger_autosave` | 1 | 0 | Establish a recovery point without player UI. | Must join the save architecture and avoid repeated saves while continuously occupied. |
| `trigger_inventory_check` | 1 | 3 | Emit `OnPlayerHasItem` for an inventory-gated beat. | Recovered `StartTouch` accepts only a base-filtered player entry, then searches ordinary slots and keyring case-insensitively. It does not poll, test quantity, or disable itself; this map authors the one-shot outputs. |
| `trigger_environmental_audio` | 16 | 0 | Change acoustic room/reverb state while crossing tutorial spaces. | All begin disabled and author room types `123` (8), `12` (1), `5` (3), `104` (2), `108` (1), and `11` (1). Plans must preserve trigger, SoundScheme `RoomDSP`, interior/exterior, and scripted-override precedence until RE30/RE31 close it. |
| `trigger_stealth_mod` | 3 | 0 | Modify stealth detection/scoring inside authored regions. | Needs the faithful stealth observer and score contract; it is not equivalent to simply hiding the player. |

### 4.1 Trigger fidelity risks to keep explicit

- `filtername` is live generic data, but this hash-pinned tutorial authors **zero** `filtername`
  fields and contains **zero** `filter_*` entities. It cannot explain a tutorial-specific rejection.
- Spawnflag bits are class-specific. `trigger_changelevel` bit `2` means `NOTOUCH`; it must not be
  interpreted as the generic trigger `ALLOW_NPCS` bit.
- `trig_dialog_outside_chopshop` carries only `ALLOW_CLIENTS`, while Jack's `wall_lean` interesting
  place `ip_0b2` lies inside its brush. Enabling the brush may therefore present Jack as an existing
  overlap, but his NPC identity fails the client gate. The later player approach supplies the first
  accepted touch and opens dialogue; an Unreal `APawn` base class is not evidence of client identity.
- `ScriptHide` turns collision, drawing, and thinking off. A hidden trigger is not an enabled
  invisible volume.
- `StartDisabled`, `Enable`, `Disable`, and self-disable are physical brush state. Enabling while
  contained rebuilds touch links; disabling releases the retained pair, so re-enable can emit a
  new edge.
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
| `prop_sign` | 2 | Player use opens an authored document/sign window. | Retail grants one player an exclusive sign session; use begin/end fire `OnReadBegin`/`OnReadEnd`, and end closes and releases ownership. Neither tutorial sign authors an outgoing row. |
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

### 5.2 Specialized-prop event boundaries

- The unnamed `prop_switch` starts activated (`spawnflags 8192`). An unlocked use queues `OnUse`,
  flips its own state, then mirrors a linked switch (none is authored here). The tutorial's one row
  consequently queues `chop_light.Toggle` immediately; `OnActivate`/`OnDeactivate`, if authored,
  would occur only when the transition clip finishes. A locked use would fire only `OnLockedUse`.
- `office_knob.OnUseBegin` queues `logic_noblood.Disable`. On use end, reverse-row enumeration first
  encounters `counter_noblood.Add(0)` at +0.2 and then `logic_noblood.Enable` at zero; deadline sort
  makes the visible order **Enable now, Add after 0.2 game seconds**. The knob's lock check and
  begin/end session own these outputs; the attached door cannot replace them.
- An unlocked `prop_button` queues `OnPressed`, advances/wraps its state, then queues `OnSetStateN`.
  Here `elev_button_1_up` sends `tutelev.GotoFloor 1`; `elev_button_1` triggers `elev_down`; and
  `elev_button_2` has two equal-time rows whose retail order is
  `plus_elevarr.skin 1` then `elev_up.Trigger`. A locked button fires only `OnPressedLocked` and does
  not advance.
- `tuthack.OnTrigger0` resolves zero-time rows as `trig_popup_note.Disable` →
  `trig_popup_safe.Enable` → `tutsafelock.Unlock`, then hides the lock at +0.5. `OnTrigger1`
  unhides the lock now and locks it at +0.5. The terminal function enqueues `OnTriggerN`, then runs
  its `runscript` synchronously; target delivery follows the script in the queue pass.
- `tutsafe.OnItemRemove` enables `trig_jack_teleport_3` before the equal-time Python-only event
  writes `G.Tut_Key = 1`. `OnUseEnd` is Python-only `OnSafeEnd()`.
- The two `prop_sign`s own no map rows. The one-user session and `OnReadBegin`/`OnReadEnd`
  production are still required behavior, but there is no tutorial target delivery to order. The
  wider-export evidence for the begin output is in `docs/vtmb/exported-map-event-surface.md`.

### 5.3 Inventory chain closure

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
| `point_teleport` | 1 | 2 | Raw data places two `OnEnterMapHere` rows on `teleport_very_beginning`; the class has no such output, so both are dropped and never produced. |
| `prop_switch` | 1 | 1 | Use completion. |

For `npc_maker`, the ownership question is closed. The maker carries 24 `OnDeath`, 8
`OnFoundPlayer`, 5 `OnFedUponEnd`, 3 `OnDamaged`, 2 `OnFedUponBegin`, and individual
incapacitation/dialogue rows because `CNPCMaker` inherits the NPC datamap. On every `Spawn`, retail
copies the maker's raw keyvalue template through the new child's ordinary parser. The **child** thus
owns independent action lists and fire counters and emits those lifecycle outputs directly; the
maker separately emits `OnSpawnNPC`, `OnNPCDied` and `OnLastNPCDied`. This is cloning, not event
forwarding, and a replacement child receives fresh counters.

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

### 7.2 Jack's eight `TeleportToEntity` placements

All eight wires resolve the one live `npc_VVampire` named `Jack`; each parameter resolves to exactly
one `info_teleport_destination` entity. The destination string is not handled by Jack directly:
`AcceptInput` converts it to the first matching entity handle, then the Troika-NPC handler copies
that entity's absolute origin and all angles onto Jack.

| Producer and output | Destination | Delay |
|---|---|---:|
| `tutchopdoorc.OnOpen` | `teleport_2` | `0.2` |
| unnamed `trigger_once.OnTrigger` | `teleport_7` | `0` |
| `logic_jack_teleport_3.OnTrigger` | `teleport_3` | `0` |
| `trigger_6.OnStartTouch` | `teleport_3` | `0` |
| `tutwareportal05.OnOpen` | `teleport_jack_after_physics` | `0` |
| `logic_jack_from_elevator.OnTrigger` | `teleport_6` | `0` |
| `logic_jack_melee_room.OnTrigger` | `teleport_5` | `0` |
| `logic_jackflash_chopshop_door.OnTrigger` | `teleport_1` | `0` |

The chopshop log therefore represents a valid, progression-visible operation: queue service has
already resolved receiver `Jack`; `AcceptInput` resolves `teleport_1`; the handler snaps Jack's
absolute transform, makes his five think lanes due, and forces network transmission for one second.
Since the event queue runs after the frame's think phase, Jack resumes due AI work on the next server
frame. The input performs no safe-placement trace, velocity reset, schedule clear, or immediate touch
dispatch. The complete native contract and collision boundary are in `docs/vtmb/entity_io.md`; the
five AI deadlines are in `docs/vtmb/npc-ai-reverse-engineering.md`.

### 7.3 Missing-target diagnostics from the chopshop graph

The two other diagnostics in the observed batch are name-resolution outcomes, not unrecovered
receiver inputs:

- UP entity 485, `script_1b`, sends `OnBeginSequence -> trig_port_chopshop_door.Enable` at delay
  zero with unlimited refire, but the UP BSP contains no entity with that targetname. The original
  retail BSP has the corresponding sequence at entity 488 with a three-second delay and a disabled
  `trigger_multiple` named `trig_port_chopshop_door` at entity 1126; that trigger's `OnTrigger`
  executes `jackflashChopshopDoor()`. UP removed the receiver and changed the delay but retained the
  sender. The patch-first result is therefore a stale, non-fatal wire; the rebuild must not invent a
  trigger merely to silence it.
- `tutchopdoora.OnOpen` has one unlimited zero-delay row targeting `point_door.Kill`. Six
  `point_target` entities initially share that name. The first accepted open resolves and kills all
  six; each later `OnOpen` finds none and produces one non-fatal missing-target diagnostic. Repeated
  log lines are repeated deliveries after the cleanup, not evidence for a missing `Kill` handler.

Neither case needs more receiver-side RE. They are acceptance cases for distinguishing a missing
target from a resolved target whose input is pending or unknown.

## 8. Python execution surface

### 8.1 Five executable paths

| Path | Form | Required namespace behavior |
|---|---|---|
| Output field six | Statement/call string on an entity output | Execute through `__main__` after all name-target input deliveries in that queue record. Stale caller/activator handles become null; they do not cancel Python. |
| `logic_pythoncheck` | Expression | Evaluate in `__main__`; error or non-integer result is false; fire `OnTrue` or `OnFalse`. |
| Dialogue condition | `dlgexpr` condition with optional engine skill check | Resolve player fields, `G`, character methods, and script helpers without treating the whole grammar as unrestricted Python. |
| Dialogue action | Assignment/call statements | Mutate `G`, player state, quests, inventory, and map entities. |
| `ScheduleTask` | Delayed source string | Enqueue a Python-typed event at `curtime + delay` on the same queue, then evaluate later against the live `__main__` dictionary. |

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

The native and authored startup join is:

1. `CServerGameDLL::LevelInit` reads entity blocks in BSP order. For each block it resolves
   `classname`, constructs that registered class, passes the complete block through map-data /
   keyvalue parsing, and dispatches virtual `Spawn`. Parented entities are retained, sorted for
   parent-before-child setup, attached and then spawned; they are not activated early.
2. `CLogicAuto::Spawn` sets its first think deadline. Jack's concrete `CNPC_VVampire::Spawn`
   executes the Vampire → Human/Troika → `CAI_BaseNPC` chain, applies model/solid/capability,
   equipment, relationship and template-driven state, caches the live transform, and admits the
   NPC to native AI. It does not select a dialogue target or rotate toward the player.
3. After map entity creation/spawn completes, `ServerActivate` walks every surviving entity in the
   server entity list and invokes virtual `Activate`, then runs post-entity systems. Jack's Troika
   activation chains through base activation and performs its recovered class-specific setup; it
   still does not run a porch/dialogue transform transaction.
4. The entity-think pass reaches the autos and NPC initialization. Each auto enqueues its
   `OnMapLoad` rows. `NPCInitThink` resolves authored relationship overrides, performs the native
   ground/target/readiness work, installs the ordinary AI think, and applies the relevant
   spawnflag branch. Jack's concrete Troika override then resolves follower tuning and records the
   closest player handle, but performs no turn toward it; its base second virtual stage is empty.
   This recovered path contains no explicit `SetActivity(ACT_IDLE)`, no player-facing write, and no
   dialogue call.
5. The ordinary-vampire UP path calls `unhidePlus()` from the last auto. That helper schedules
   `c.patchtype=""` one game second later; the installed cfg alias invokes `setPlus()`. `setPlus`
   establishes `G.Patch_Plus=1`, unhides `plus_*`, hides `basic_*`, and invokes `IsIdling()`.
   Despite its name, `IsIdling()` is the Patch Plus **player** idle monitor; it does not initialize
   Jack or any NPC animation.
6. Only later movement across `trig_off_porch` sets Jack `WillTalk 1`, creates the Blueblood child
   and player controller stand-in, disables Jack's interesting-place use, and requests the first
   dialogue. None of those objects or dialogue mutations belongs to Jack's load-time state.

The original retail BSP has one auto. Its per-producer execution is the reverse of stored repeated
row order: `world.SetNoFrenzyArea 1`, `Jack.WillTalk 0`, then
`pc_0.MakePlayerUnkillable`. The UP replacement retains that core auto and adds four more:

| BSP index | Deadlines relative to the auto | Authored actions |
|---:|---|---|
| 667 | `t0` | Core world no-frenzy, Jack non-talking and player unkillable policy. |
| 1358 | `t+1.5` | Source-only `ccmd.wc_create`; Unreal supplies the look through the offline bake. |
| 1627 | `t+2`, `t+2.1` | Configure `sentry2`, then start its walking patrol. |
| 1832 | `t0`, `t+0.1` | Configure `monk_upstairs_podium`, then start its walking patrol. |
| 1862 | `t0`, `t+0.1` | At `t0`, call `unhidePlus()`, lock `frontdoor2`, lock `frontdoor1`, then refill the hunter container; test `G.Linux_Wine` at `t+0.1`. |

Reverse repeated-row order establishes each row above. The relative order between separate autos
whose thinks become due together is not claimed by this static case; all their queued side effects
still enter the recovered equal-time FIFO event service once each producer runs.

### 11.2 Core beat dependency chain

| Beat boundary | Event dependency | Required externally visible result |
|---|---|---|
| Tutorial placement | Landmark `tutorial` seats the player at the porch. | Player and Jack start in their authored placements without an invented arrival callback. |
| First beat | Leaving `trig_off_porch` fires Jack `WillTalk 1`, remote dialogue `256`, `blueblood_maker.Spawn`, and `events_player.CreateControllerNPC`. | Jack dialogue opens and the encounter actor/controller exist. |
| Dialogue progression | Jack `OnDialogEnd` calls `DialogPostProcess()`, which branches on `G.Tut_Jack`. | The next sequence/trigger/popup is enabled exactly once for that beat. |
| Second warp | `teleport_fade.OnBeginFade` teleports player and Jack to the alley. | Both actors arrive after the authored fade, with current progression state intact; any destination trigger begins from the following movement/touch phase, not inside `point_teleport`. |
| Feeding continuation | Feeding on the spawned blueblood reaches the child-owned clones of the maker-authored `OnFedUponBegin`/`OnFedUponEnd` rows. | `trig_dialog_outside_chopshop` enables before the equal-time Python event writes `G.Tutorial_Blueblood=1`. |
| Chopshop and office | Dialogue, door/knob use, safe/hacking, item and elevator events advance `G.Tut_Jack`. | Each world interaction unlocks only its authored next beat. |
| Combat/discipline lessons | Damage, death, incapacitation, break, stealth, rat-feeding, weapon, and discipline events update counters/flags. | Jack's dialogue and tutorial popups follow observed outcomes rather than scripted time alone. |
| Exit | `LeaveTutorial()` sets story/quest/killability state and calls `ChangeMap` through `trig_leave_tutorial`. | Travel reaches `sm_pawnshop_1` at landmark `newgame`. |

`teleport_very_beginning` carries `OnEnterMapHere` data, but the recovered owner of that output is
`info_landmark`, not `point_teleport`. The port retains the inert wire rather than broadening the
teleport API to make it fire.

### 11.3 Teleport, placement, and trigger order

Map travel and `point_teleport` are different transactions. Retail changelevel placement is
`destination landmark + (player - source landmark)`; it can carry a non-zero source-landmark
offset. The zero-offset reference at tutorial landmark `tutorial` is Source feet
`(-14, 7492, -156)`, yaw `270` (Unreal `(-35.56, -19029.68, -396.24)` cm). Initial containment is
reconciled as part of map activation. It is not a `point_teleport`, and the landmark's arrival does
not fire the inert `OnEnterMapHere` rows authored on `teleport_very_beginning`.

`point_teleport` instead caches its own origin/all angles at entity activation, resolves `target`,
and on input applies that exact cached transform immediately. No target deletes the point; a
parented target is refused. Spawnflag `1` replaces the point's authored transform with the target's
live activation transform, making a return point. There is no distance/activator/LOS check, hull
trace, ground search, collision clearance, nearest-safe fallback, or velocity reset. The player view
and controller relationship mirror the write. Several teleports before collision reconciliation
collapse to the final position.

Retail `vampire.dll` does **not** issue touch callbacks inside `InputTeleport`; the later
`PhysicsTouchTriggers` path delegates contact ordering through the engine collision-property
interface. The exact old-end/new-begin callback order below that interface remains unrecovered.
Elysium's deterministic post-movement rule—old ends, then new begins, entity-index order—is a port
rule, not retail evidence. Enabling a disabled trigger is separate: rebuilding its overlap links can
produce a fresh begin while a player is already contained.

### 11.4 Porch geometry and landing contacts

The tutorial has two mutually selected porch paths. Distances below use the Source player standing
hull, 16 units each side in X/Y and 72 units above the feet; multiply Source units by 2.54 for cm.

**Main/Patch Plus path.** `trig_off_porch` has world AABB X `[-67,45]`, Y `[7341,7445]`,
Z `[-204,-44]`. At the zero-offset landmark reference the player is not inside it: the feet centre
is 47 units north of its Y face and the hull has a **31-unit / 78.74 cm clear gap**. Facing yaw 270
toward negative Y, the first contact begins after 31 units plus overlap epsilon. `OnEndTouch`, which
owns the first beat, occurs only after the hull clears the south face: feet Y below 7325, about
**167 units / 424.18 cm** forward from the landmark. A carried landmark offset shifts both values.

The same zero-offset landing hull does overlap five other records:

- active `trigger_autosave trig_autosave`;
- disabled `trigger_once trig_popup_move`, which cannot begin until enabled;
- `trig_theater_to_tutorial`, `trig_leave_tutorial_short_1`, and
  `trig_leave_tutorial_short`, all `trigger_changelevel` records with spawnflag `2` (`NOTOUCH`).

Thus the only active collision ingress on that exact landing is `trig_autosave`; the first dialogue
trigger is reached by walking. The autosave has no ordinary outputs. Its native leaf initializes a
base trigger, but the actual engine/save transaction remains open and is not inferred from the
classname.

**Basic patch path.** When `G.Patch_Plus == 0`, `vamputil.basicInit` chooses
`TutorialUnderground`, starts `teleport_fade_basic`, enables `trig_off_porch_basic`, and schedules
popup 1 at +4 seconds. `teleport_player_basic` seats the player at Source feet `(-64,-368,0)`, yaw
90. The basic trigger AABB is X `[-120,-8]`, Y `[-380,-276]`, Z `[-80,80]`, so the standing hull
lands **already overlapping it by 4 units** at the south face. Facing positive Y, forward travel
must put feet Y above `-260`: **108 units / 274.32 cm** to fire the north-side `OnEndTouch`. Leaving
backward through the near south face takes only 28 units. The trigger is not start-disabled;
`Enable` is idempotent as logical state, while physical overlap-link rebuild timing remains the
collision boundary above.

The later main alley warp is different again. `teleport_player` seats the player at
`(-256,-176,-32)`, yaw 0, and no tutorial touch-trigger hull overlaps that destination. Its next
beat is therefore not an accidental destination `StartTouch`.

### 11.5 Exact porch and fade transactions

Repeated output rows fire in reverse export order, then the queue sorts by deadline and keeps FIFO
ties. `trig_off_porch.OnEndTouch` therefore resolves as:

| Relative game time | Actual action order |
|---:|---|
| `0` | `pc_0.RemoveDisciplinesNow` → `blueblood_maker.Spawn` → `pc_0.CreateControllerNPC` → `Jack.WillTalk 1` → `Jack.StartPlayerDialogRemote 256` → `Jack.UseInteresting 0` |
| `+0.5` | `trig_off_porch.Kill` |
| `+5.0` | `pc_0.RemoveControllerNPC` |

Killing the trigger does not retract the already queued +5 named delivery. Its caller handle may be
stale by then, but named target resolution still runs. The Basic trigger has the same zero-time six,
then `Kill` at +0.5, `Jack.UseInteresting 1` at +4, and controller removal at +5.

Fade output order is equally concrete:

| Fade | Actual queued/delivered order |
|---|---|
| `teleport_fade_basic.OnBeginFade` | `t0 Jack.WillTalk 0`; `t+1 teleport_jack.Teleport` then `teleport_player_basic.Teleport`; `t+3 Jack.WillTalk 1` |
| `teleport_fade.OnBeginFade` | `t0 Jack.WillTalk 0`; `t+1 teleport_jack.Teleport` then `teleport_player.Teleport`; `t+3 Jack.WillTalk 1`; `t+4 Jack.StartPlayerDialogRemote` then `Jack.UseInteresting 1` |

#### Jack dialogue-camera join

Jack's entity authors `default_camera=Jack`; the external `Jack` camera shot carries FOV 40,
`DialogTarget` follow/head anchors, `DialogPOV 1`, and `SyncRotateOnMove 1` **[data, RE46]**. Both
remote dialogue acquisitions above select that same definition-derived shot. The first conversation
must release its own camera ownership before `teleport_fade.OnBeginFade`; the +1-second Jack/player
teleports and the +4-second second acquisition retain the exact queue order in the table.

The `256` on the first `StartPlayerDialogRemote` is not a decoded camera or placement flag. The
hash-pinned server handler at `0x1029f060` never reads its input variant and writes no player, Jack,
or camera transform **[VtMB, RE46]**. `CreateControllerNPC` remains an earlier, independently authored
event in the same zero-time batch. Whether downstream client/body code changes visibility or body
ownership is an explicit capture question; no placement or forced-facing behavior follows from the
map row alone.

`teleport_very_beginning` is not an authored-position warp. Its spawnflag `1` changes the cached
destination to the player's activation-time transform, so its visible map origin
`(-14,7455,-164)` is discarded. It returns the player to that cached live position.

### 11.6 Blueblood maker admission

`blueblood_maker` is authored with `Flag_StartDisabled=1`, `Flag_InfChild=1`,
`MaxNPCCount=1`, `MaxLiveChildren=1`, and `SpawnFrequency=5`. The disabled latch suppresses only its
automatic `MakerThink`; it does not suppress an explicit `Spawn` input. The main and Basic porch
transactions can therefore both reach the maker, but they cannot create two simultaneous children:

1. The first accepted `Spawn` creates the Blueblood and increments the live-child count to one.
2. A later explicit `Spawn` enters the same native admission path and fails its first ordinary gate,
   `live children >= MaxLiveChildren`, before allocation or `OnSpawnNPC`.
3. `Flag_InfChild` preserves the maker for a replacement after the live child dies or is removed; it
   bypasses finite-total exhaustion, not the simultaneous-live ceiling.

The faithful static contract therefore has at most one live Blueblood from this maker even if both
porch trigger routes produce their authored `Spawn` input. Whether a specific retail playthrough
reaches both routes remains a live collision/route-capture question, but it cannot change that
maker-local invariant. The generic admission, timer, construction and death ordering is owned by
`docs/vtmb/entity_io.md`.

### 11.7 Feeding child order and VCD event actions

On each `blueblood_maker.Spawn`, the child receives cloned output action lists. Its two progression
edges have these exact effects:

- `OnFedUponEnd`: the named `trig_dialog_outside_chopshop.Enable` event is enqueued before the
  Python-only `G.Tutorial_Blueblood = 1` event. Equal-time FIFO therefore enables the trigger first,
  then writes `G`.
- `OnDeath`: the +1-second popup action is enqueued while parsing/firing the reversed list, but the
  zero-delay Python `G.Tutorial_Blueblood = 2` sorts ahead of it. State changes now; the popup opens
  one game second later.

Only one tutorial VCD carries map-event actions. `line1001_col_e.vcd` and `line1006_col_e.vcd`
contain speech/silence/loudness only: no `firetrigger`, no Python, and no wired scene-completion
output. `jack_vs_sabbat.vcd` carries four `firetrigger` events and no Python:

| VCD game time | VCD event | Actual scene-output action order |
|---:|---|---|
| `8.600` | `firetrigger 1` | `timer_muzzle_flash_2.Enable` → `Jack.SetScriptedDiscipline` (`potence 5`) |
| `8.967` | `firetrigger 3` | no authored `OnTrigger3` rows |
| `9.200` | `firetrigger 4` | no authored `OnTrigger4` rows |
| `10.033` | `firetrigger 2` | `Jack.SetScriptedDiscipline` (`potence 0`) → `potenced_emitter.TurnOn` → `timer_muzzle_flash_2.Disable` |
| scene end | `OnCompletion` | fight sound `StopSound` → `logic_alley_cleanup.Trigger` |

The scene thinks once per game frame using `curtime - startTime`; time scaling affects it and a
global clock pause freezes it. A hitch crosses missed events in VCD start-time order. Each
`firetrigger` merely queues `OnTriggerN` during scene think, so target delivery occurs later that
same frame after all entity thinks. A `Start` delivered in the queue arms the scene after that
frame's think; its first VCD processing is next frame. A same-frame queued `Cancel` cannot undo a
trigger already emitted by that think.

No `logic_relay` in the exported tutorial uses fast-retrigger spawnflag `2`. Ordinary relays lock
until their longest output delay plus 0.001 seconds; inputs during that lock, `OnTrigger` attempts
during trigger wait windows, disabled periods, and consumed once state are swallowed rather than
retried. A new contact can still emit its edge-only `OnStartTouch` before the wait check. Those blockers reduce
accidental recursion, but the underlying retail queue still has no starvation budget.

`trigger_environmental_audio` retains the same physical `StartDisabled` and client-admission gate
as the rest of the trigger family even while environmental room presentation is absent. Spawning
the blueblood inside disabled room brushes must not manufacture begin/end pairs.

### 11.8 Lockpick-to-Sheriff encounter transaction

The captured Unofficial Patch/Plus run after acquiring `tut_lockpicks` exposes the full transaction,
but the pickup entity does not directly start the Sheriff scene. The acquired item remains ordinary
inventory state; tutorial dialogue and `trigger_4` advance the authored graph. `trigger_4` enqueues
`sJack_waveover.BeginSequence`, calls `OnJackFloat()`, and removes disciplines at the touch time,
creates `!playercontroller` at +0.05 seconds, then starts `script_2a` at +0.2 seconds. Both
`script_2a` and the later `sPlayer_13` author `m_fMoveTo=1`, so the stand-in walks through the same
scripted-sequence motor seam as an NPC and its final transform transfers back to the real player on
`RemoveControllerNPC`; snapping is only the established no-body/no-path fallback.

Jack dialogue progresses through rows 261, 271, 281 and 291. Row 291 writes `G.Tut_Jack=3`; the
following `DialogPostProcess()` branch triggers `logic_scene_1`. That relay opens `frontgate`, runs
`logic_sabbat_setup`, and at +0.25 seconds starts `trackb00` as camera position and `focusb00` as
camera target. The paired authored tracks last 38.05 seconds and coordinate the Sheriff reveal,
gunfire, hand cast, wolves, mauling, the third Sabbat victim, the player-look window, controller
exit move, and cleanup. Track completion removes the controller at +1.5 seconds, starts Jack's next
dialogue at +1.0, and restores ordinary control through `logic_shot_end`.

Particle placement in this encounter uses three distinct recovered contracts:

- `attach_type=0` renders at the emitter's own authored origin.
- `attach_type=1` (`tree`) follows the named parent root while preserving the exported offset from
  that parent's initial placement. `pestilence` uses this on `sabbat_redshirt_3`; it is not a bone
  snap.
- `attach_type=2` (`point`) follows the named bone, falling back visibly and with a warning to the
  body root if that bone is absent. `plus_sheriff_hand_w` uses `Bip01 R Hand`, and muzzle effects use
  weapon `flash` points.

The UP muzzle closure also establishes two inert source spellings the strict particle compiler must
retain: spawn-only wrappers may author `frames 0`, and children may author `sortfront 0`. Enabled
`sortfront 1` remains unsupported rather than being accepted without a draw-order implementation.
Their nested spawn edge also retains `depth_offset` as an unresolved-unit scalar while the authored
`x`/`y`/`z` offset continues through the Unreal-native coordinate conversion.
`d_animalism_pestilence_cast_emitter` is referenced by the map but absent from the joined installed
particle corpus; runtime therefore emits one diagnosable missing-system warning and does not invent
a replacement. `d_animalism_pestilence_emitter` is a separate present definition and still plays.

The captured death/ragdoll response after `TakeDamage` is not implemented by this transaction.
`TakeDamage` input typing, death selection and ragdoll presentation remain the general combat/NPC
contract; map-special-casing the third Sabbat would turn a missing shared rule into tutorial script.

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
| NPC maker | Does each spawn clone the raw maker template through the child's parser and keep maker count outputs separate? | Feeding, damage, incapacitation, found-player, dialogue, and death fire from the child with independent counters and stable provenance. |
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
| Event transport | Parse and queue every wire; reverse repeated rows; resolve names/aliases; dispatch inherited and class inputs; execute field six after named targets. | Structured trace for all outputs from one representative relay transaction, including delay, caller, activator, Python and recursive enqueue order. |
| Trigger fidelity | Player/NPC/physics gates, `filtername`, once/wait state, look dwell, hurt cadence, autosave, inventory, stealth, environmental audio. | Positive and negative activation cases for every trigger class present in the map. |
| NPC lifecycle and feeding | Stable maker-child ownership; use/feed start/end; damage; incapacitation; death; dialog; perception. | Blueblood feeding causes the child-cloned `OnFedUponBegin` then `OnFedUponEnd`, enables the chopshop trigger before the equal-time state write, and never relies on debug firing. |
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

1. In retail `engine.dll`, after `PhysicsTouchTriggers` enters the collision-property interface,
   what exact order delivers old-contact ends and new-contact begins for a teleported player?
2. What save service and repeat/occupancy guard does the no-output `trigger_autosave` leaf invoke?
3. How do damage type, velocity mode, force, and victim classification affect the three tutorial
   `trigger_hurt` volumes beyond their recovered timing?
4. What is the precedence between `trigger_environmental_audio`, SoundScheme `RoomDSP`,
   interior/exterior state, and scripted audio overrides? RE30/RE31 own the general answer.
5. Are the five unresolved callback names version skew, missing patch imports, case drift, or
   deliberately shipped errors?
6. Which optional hunter/Patch Plus paths are reachable during the normal tutorial, and which
   require later story-state re-entry?
7. Which outputs rely on actual combat/AI perception, and which are script-forced during the
   tutorial to make the lesson deterministic?
8. What save/restore points were supported by the original tutorial, especially during feeding,
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
- Priority, task identifiers, and completion state remain only in `docs/project/roadmap.md`.

This brief remains the cross-system demand inventory. It should be regenerated or corrected when
the patch-first input hashes change, a retail trace contradicts an intent inference, or a new
reachable script/dialogue dependency is discovered.
