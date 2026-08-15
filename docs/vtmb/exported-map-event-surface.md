# Exported-map event surface beyond `sp_tutorial_1`

## Scope and result

This is the cross-map continuation of `docs/vtmb/sp_tutorial_1-event-surface.md`. It surveys every
map currently present in `ELYSIUM_EXPORT_ROOT`, compares its authored entity, Python and referenced
VCD surfaces with `sp_tutorial_1`, and then follows the seven newly encountered trigger classes
into the retail server DLL. It is a current-export snapshot, not a claim about all 108 maps in the
patch-first install.

The snapshot contains **23 maps, 14,663 entities, 5,804 output rows, 1,683 Python-bearing rows and
30 referenced VCDs**. The tutorial baseline contributes 1,868 / 1,028 / 133 / 3 respectively. The
semantic delta, case-folded only for retail's case-insensitive lookup surfaces, is:

| Surface absent from `sp_tutorial_1` | Count |
| --- | ---: |
| Entity classnames | 96 |
| Trigger classnames | 7 |
| Authored producer `classname.output` pairs | 127 |
| Resolved receiver `classname.input` pairs | 207 |
| Static-unresolved authored `target.input` pairs | 113 |
| Python identifiers mentioned by field 5 | 209 |
| Special-target inputs | 2 |
| Referenced VCD event types | 2 |
| New VCD `firetrigger` values / Python payloads | 0 / 0 |

Static recovery of the seven new trigger leaves shows that they do **not** introduce a second
scheduler or different I/O resolver. They produce work at collision callbacks, explicit
inputs, or held-use transitions and then use the same output list and `CEventQueue` transaction
recovered in RE43. Only `trigger_push.SetSpeed` can install a leaf-specific think, and no current
export wires that input. Two rows that look like a new activity-trigger output are invalid retail
keyvalues and never enter an output list.

## Evidence snapshot and reproduction

The 23 entity files are `la_hub_1`; `sm_apartment_1`, `sm_asylum_1`, `sm_bailbonds_1`,
`sm_basement_1`, `sm_coffee_1`, `sm_diner_1`, `sm_gallery_1`, `sm_hub_1`, `sm_junkyard_1`,
`sm_medical_1`, `sm_oceanhouse_1`, `sm_pawnshop_1`, `sm_pawnshop_2`, `sm_pier_1`,
`sm_shreknet_1`, `sm_smoke_1`, `sm_tattoo`, `sm_vamparena`, `sm_warehouse_1`;
`sp_genesisdevice_1`, `sp_theatre` and `sp_tutorial_1`.

Run:

```powershell
uv run elysium research event_surface_survey --baseline sp_tutorial_1 --json "<work>/research/event-surface/exports-vs-sp_tutorial_1.json"
uv run elysium research exported-event-resolution research/cases/exported-event-resolution/specs/exported_event_resolution.json --binary "<VtMB>/Vampire/dlls/vampire.dll" --project-dir "<work>/research/ghidra/project_exported_events_<task>" --kinds funcs,xrefs,fields,vtables,grep
```

The survey JSON is the exhaustive machine-readable inventory. It includes the relative path,
byte length and SHA-256 of all 23 `.ents` and 30 consumed `.vcd` files. The combined manifest hash
for this snapshot is
`B1B986F7C67EDAAA0E9DDF6DA8FB5F79EF1F99BAFDE8591497E4F1368960C7EC`.

Retail code evidence is from `Vampire/dlls/vampire.dll`, size 7,860,281, SHA-256
`c546f4de2003624d72f54d03805e0dbe1d8157231adcc62368ff53fe6e48a76f`, image base
`0x10000000`. The tracked research specification and reproduction notes are under
`research/cases/exported-event-resolution/`.

## What an authored row proves

The survey deliberately records two different surfaces:

1. **authored producer rows** group every `On*` key by the classname stamped on the entity;
2. **semantic receivers** resolve targetnames and final-`*` prefixes case-insensitively, then group
   each found receiver classname and input.

Neither alone proves runtime behavior. On map load the entity's datamap chain decides whether a key
exists and whether it is an output. An unknown key is silently discarded. At dispatch, a missing
target or an input absent from the receiver's datamap is independently non-fatal. This creates four
useful categories:

| Authored surface | Retail result |
| --- | --- |
| Valid output and valid receiver input | Queued and delivered with the ordinary event rules. |
| Valid output, missing target | Output may fire, but name resolution finds nothing. |
| Valid output, receiver lacks input | Target resolves; `AcceptInput` refuses the name. |
| Producer classname lacks that output key | The row is dropped during entity keyvalue parsing and can never fire. |

`trigger_player_activity_level.OnTrigger` is the concrete fourth case in this corpus. Its two siren
rows in `sm_diner_1` are authored against a class whose datamap has no `OnTrigger`; there is no
output object or producer call in retail. They are inert authored data, not a missing reimplementation
feature.

### Static-unresolved does not always mean dangling

The survey resolves targets against the static entities in each `.ents` file and reports 113
new `target.input` pairs it cannot join. Retail instead resolves a named target when the queued
record is serviced. An entity created by an earlier maker, Python callback or input can therefore
make a statically absent name valid before delivery; a destroyed target can make a statically
present name disappear.

The dominant unresolved rows demonstrate both cases:

- `patrol_cop_*.ClearPatrolPath` and `patrol_cop_1/2.FollowPatrolPath` / `SetupPatrolType` address
  cops created by makers, so static absence is expected and runtime name resolution is required;
- `hunter_1/2/3.TeleportToEntity` addresses the `NPCTargetname`s of conditionally enabled hunter
  makers, so those names likewise exist only after child spawn;
- the repeated `streetlight_{red,green,yellow}_south` and `_east_west` `TurnOn`/`TurnOff` wires
  address no entity in their maps and belong to the already established dead streetlight graph;
- the remaining rows need producer-time lifetime analysis before being called either valid or
  dangling.

The current corpus carries 40 `TeleportToEntity` wires: 30 to those maker-created hunters, eight to
the static Jack in `sp_tutorial_1`, and two to static warehouse guards. The input's destination
parameter undergoes a second late lookup inside `AcceptInput`, after the receiver has resolved. All
40 destination names exist; `la_hub_1` has two `point_target` rows named `hunter_2_spot_1`, and
retail selects the first live entity-list match. The exact `FIELD_EHANDLE` conversion and native
placement order are recorded in `docs/vtmb/entity_io.md`.

A remake must not prebind every output to the load-time entity set. It preserves the authored name
and performs case-insensitive exact/final-`*` lookup at service time. Missing-at-service remains a
non-fatal drop and is not retried if a matching entity appears later.

## Trigger summary

| Class | Current occurrences | Activation and guards | Event/tick effect | Output/refire behavior |
| --- | ---: | --- | --- | --- |
| `trigger_teleport` | 3, `sm_medical_1` | Every `Touch`; enabled collision plus `PassesTriggerFilters`; target must resolve. | Immediate transform in the collision callback; no think. | No leaf output. May touch again only if the destination remains in/intersects the volume. |
| `trigger_push` | 2, `sm_junkyard_1`, `sm_medical_1` | Every accepted physics `Touch`; rejects null, non-solid/trigger-solid and an excluded collision/move case, then applies base trigger filters. | Reapplies base velocity/force while overlapping. `SetSpeed` alone can arm a game-clock think. | No leaf output. Spawnflag `0x80` is one-shot push-and-remove. |
| `trigger_player_activity_level` | 6 on 3 maps | Specialized `Touch` requires enabled state and a player-controller-bearing toucher; it does not call the base class filter. | Refreshes authored player activity levels on collision ticks; matching state may clear at `EndTouch`. No think. | Only inherited touch-edge outputs exist. Authored `OnTrigger` is invalid. |
| `trigger_discipline_context` | 7, `sm_warehouse_1` | `StartTouch`/`EndTouch`, enabled, non-negative context and a compatible toucher state object. | Sets/clears one context bit at contact edges; no per-tick work and no think. | Only inherited touch-edge outputs. Context mutation precedes the inherited filter/output call. |
| `trigger_checkvolume` | 1, `sm_warehouse_1` | Explicit `CheckNow`; refuses disabled state; each queried candidate passes `PassesTriggerFilters`. | One bounded broadphase occupancy query; no retained contacts, poll or think. | Fires `OnEntityInVolume` once per admitted result, every time `CheckNow` is invoked. |
| `trigger_bomb_site` | 1, `sm_warehouse_1` | Player must occupy this site, site enabled, no other live use owner. | Held-use session; completion mutates inventory/world and self-disables. No entity think. | `OnBombPlaced` once per successful completion; can refire only after an explicit re-enable and another completion. |
| `trigger_electric_bugaloo` | 1, `sm_asylum_1` | Player must occupy this site, site enabled, no other live use owner. | Held-use begin/end plus owner-only collision-tick check; no entity think. | Generic `OnUseBegin`/`OnUseEnd` refire on each session. A separate 300-second action is latched once per entity lifetime. |

The base brush rules in `docs/vtmb/entity_io.md` remain load-bearing: `Enable`/`Disable` adds or
removes trigger solidity, class bits and an optional filter decide `PassesTriggerFilters`, and
enabling around an already-contained player can create a fresh contact. The exceptions above are
intentional leaf behavior: activity-level `Touch`, discipline-context mutation and the two
use-volume relationship writes are not all gated at the same point as inherited edge outputs.

## `trigger_teleport`: landing and reconciliation

`CTriggerTeleport::Touch` (`0x101c92c0`, vtable `0x1047f694`) runs for each collision touch callback:

1. reject unless `PassesTriggerFilters` accepts the toucher;
2. resolve the `target` name stored by the trigger; a missing destination is a silent no-op;
3. optionally resolve `landmark`;
4. remove the toucher's on-ground flag;
5. write the destination transform immediately;
6. when a player controller exists, start its crossfade state from current game time using the
   authored `crossfade` duration.

Without a landmark, the destination's origin and angles are authoritative. For a player/collision
object, retail adds `-collisionMins.z` to destination Z: the target entity marks the **foot/ground
point**, not the collision origin. It passes a null velocity argument to the transform setter, so
the incoming velocity is preserved. It does not trace, search for ground, test headroom, depenetrate,
retry, defer, or reject a blocked destination.

With a landmark, retail preserves the toucher's offset relative to that landmark and rotates both
angles and velocity by the landmark-to-destination yaw delta. None of the three current exported
volumes authors a landmark, so they use the foot-point path. They are
`nurse_move_player → player_destination`, `player_teleport_cis → player_cis_target`, and
`player_teleport_cs → player_cs_target`; all three are initially disabled and author
`crossfade 0.5`.

Teleporting out normally ends the old overlap, and the new location may begin different overlaps.
The server leaf performs no explicit `EndTouch`/`StartTouch` walk; exact ordering of old-contact
release versus destination-contact creation is owned by retail `engine.dll` collision
reconciliation and remains unclosed. A destination inside the same or another enabled teleport can
therefore participate in another engine touch callback; the server leaf has no cooldown or
recursion guard of its own.

## `trigger_push`: continuous contact work versus its optional think

`CTriggerPush::Spawn` (`0x101c8aa0`) derives the push direction from `angles`, defaulting an all-zero
angle to `0 180 0`, initializes the brush trigger, and copies current `speed` into the target-speed
field. `Touch` (`0x101c8c40`) rejects a null toucher, non-solid and trigger-solid entities, and one
collision/move-type case before applying `PassesTriggerFilters`.

The ordinary actor path writes or updates base velocity in the authored direction at current
`speed`. An upward push clears on-ground state and gives a grounded actor a tiny positional lift.
VPhysics objects receive a force scaled by frame time. Spawnflag `0x80` instead adds
`speed * direction` to absolute velocity, clears ground for an upward component and removes the
trigger, making that leaf a one-shot impulse.

`accel` does not make the entity tick by itself. The two inputs are asymmetric:

- `SetAcceleration` (`0x101c8a60`) only changes the acceleration field;
- `SetSpeed` (`0x101c89c0`) records a new target speed and current game time, installs
  `CTriggerPushAccelThink` (`0x101c8b60`) and schedules it immediately.

The think clamps elapsed game time, converges current speed toward target by `accel * dt`, and
re-arms until equality, then removes itself. The current 23 maps have no resolved wire to
`trigger_push.SetSpeed`, so their authored `accel 20` never arms this think. Their world-time cost is
only the physics touch callback while an admitted body overlaps; there is no event-queue traffic.

## `trigger_player_activity_level`: per-touch refresh, guarded release, invalid output

The leaf stores `supernatural_level`, `criminal_level` and `investigate_level` at `+0x598`,
`+0x59c` and `+0x5a0`; each defaults to `-1` (unset). `Touch` (`0x102108e0`) checks disabled state
and requires a toucher with the player/controller subobject. For every authored value at least zero,
it refreshes that level: supernatural and criminal pass duration `-1` to the player setters, which
convert it to a finite `max(previous retained level, pl_min_act_timer)` deadline; investigate uses
its direct setter. This specialized body does **not** call
`PassesTriggerFilters` and is collision-tick work while occupied, not an enter-only transaction.

`EndTouch` (`0x102109b0`) avoids clearing another source's replacement state. With spawnflag
`0x20`, supernatural and criminal return to zero only when the current level still equals this
volume's authored value. Investigate uses the same exact-match test and clears to zero without
requiring `0x20`. The inherited base `EndTouch` still handles any valid `OnEndTouch` row.

This is exact-match release, not a reference-counted or stacked ownership model. Repeated `Touch`
refresh can reassert a value while occupied; overlapping volumes can replace it, and an exiting
volume refuses to clear a different current value. The six current volumes set only criminal
levels: four enabled `restricted_section` brushes in `sm_medical_1` set 3, one disabled
`alley_criminal_trigger` brush in `sm_hub_1` sets 1, and one disabled `post_robbery_cop_call` in
`sm_diner_1` sets 4. Its two `OnTrigger → sirens` rows are invalid and do nothing in retail.

## `trigger_discipline_context`: edge-owned bit with no overlap count

`StartTouch` (`0x10210dc0`) checks disabled state, a non-negative `Discipline_Context`, a non-null
toucher and its context-state object. It sets `1 << (context & 31)` in that object **before** calling
the inherited `CBaseTrigger::StartTouch`. Consequently the context mutation itself precedes—and is
not gated by—the base class/filter admission that controls `OnStartTouch`. Current maps use
`spawnflags 1`, and the practical accepted actor is the player.

`EndTouch` (`0x10210e50`) clears the same bit and then calls the inherited end path. The leaf has an
empty `Touch`, so there is no per-tick refresh or think. It is also not reference-counted: two
overlapping volumes carrying the same context bit can clear one another early when either contact
ends. All seven current volumes are in `sm_warehouse_1`, carry context 4, and are enabled/disabled
by ordinary I/O around the upper jump/catwalk tutorial spaces.

## `trigger_checkvolume`: explicit, repeatable occupancy query

`CheckNow` (`0x101cb7d0`, vtable `0x10481c4c`) is an input, not a think. It rejects disabled state,
computes the brush's world AABB, asks the engine partition for at most `0x800` (2,048) candidate
entities, applies `PassesTriggerFilters` to each, and fires `OnEntityInVolume` once for each accepted
candidate. It retains no occupancy set and performs no deduplication, wait, one-shot removal or
polling. Calling `CheckNow` again repeats the query and may refire for the same entity. The query is
broadphase/AABB evidence; exact partition-side brush containment is an `engine.dll` boundary.

`sm_warehouse_1` has one player-only `explosion_playervolume`. `explosion_event` explicitly sends
`CheckNow`. Its three `OnEntityInVolume` rows are parsed by prepending, so the actual enqueue order
is:

1. `AttachToPlayerViewmodel` at delay 0;
2. `Fade` at delay 0;
3. `TurnOn` at delay 0.01.

The first two are equal-time FIFO in that enqueue order. They do not run recursively inside
`CheckNow`; the input queues them on the ordinary event queue.

## `trigger_bomb_site`: occupied-use transaction and one completion output

`StartTouch` (`0x102110e0`) first invokes the inherited base edge path, then, for a toucher carrying
a player controller, stores this site's handle in the controller. That relationship assignment is
after but not conditional on the base filter verdict. `EndTouch` (`0x10211160`) clears the handle
only if it still names this site and also runs the inherited end path.

The use-query method (`0x10211280`) rejects a disabled site, a missing player controller, a
controller whose current bomb-site handle is not this entity, or an exclusive use owner that is a
different live actor. It accepts no owner or the same player. No inventory-presence test is visible
in that eligibility body. Use begin (`0x102113c0`) claims the generic exclusive session, records
current game time and sets the player's busy state. The duration callback returns zero; the progress
callback stores the reported percentage and completes at 100 or above.

Completion (`0x102116c0`, reached through `0x10211480`) is ordered:

1. disable the brush trigger;
2. release the exclusive owner and controller relationship;
3. request removal of `item_g_astrolite` from the player;
4. spawn an astrolite ahead of the player and initialize/parent it to the site;
5. fire `OnBombPlaced` with the player activator.

The output is therefore completion-owned, not entry-owned. Self-disable prevents another ordinary
completion. An explicit `Enable` can make the brush usable again; retail has no separate permanent
one-shot latch, so another completed use can refire the output.

The eight `sm_warehouse_1` rows enqueue in reverse authored order. The actual zero-delay cohort is
`Block_Exit.ScriptHide`, `beckett_trigger.Enable`, `explosion_timer.RestartTimer`,
`explosion_timer.Show`, `turn on beckett.ScriptUnhide`, `Exit_Block.ScriptHide`. At `t + 2`,
`Relay_Trigger_Bonus.Trigger` precedes `Relay_Trigger_Sabbat.Trigger`. Zero-delay receivers can
enqueue more work, but equal-time FIFO keeps it behind the cohort already pending.

## `trigger_electric_bugaloo`: reusable use edges and one 300-second latch

This is another occupied-use volume. `StartTouch` (`0x10231370`) runs the inherited edge path and
then stores this site at player-controller `+0x1cbc`; as on the bomb site, the relationship write is
not conditional on the inherited filter verdict. `EndTouch` (`0x102313f0`) clears it only if still
current. Use query (`0x10231520`) rejects disabled state, a missing/mismatched controller relation,
or another live exclusive use owner.

Use begin (`0x10231640`) fires the generic `OnUseBegin`, claims the owner, marks the player busy and
records game `curtime` at `+0x598`. Use end (`0x10231690`) clears busy/session state and fires the
generic `OnUseEnd`. Those two outputs have no leaf cooldown or one-shot counter and refire on every
accepted session.

While the player is the current owner, each collision `Touch` calls one leaf helper. If the
per-entity byte at `+0x59c` is still false and current game time is more than **300.0 seconds** after
use begin, retail calls a player routine once and latches that byte true. Neither use begin nor use
end resets it. This long-delay action is driven opportunistically by collision touches, not by an
entity think or event queue; it can occur only once in the entity lifetime. Its downstream player
mode is outside this event-layer pass.

The single `sm_asylum_1` `player_dance_trigger` uses the generic outputs. Actual begin enqueue order
is `dance_spot.Disable`, `dance_spot_player.Enable`, then Python `G.No_Idle = 1`. Actual end order is
`dance_spot_player.Disable`, `dance_spot.Enable`, then Python `G.No_Idle = 0`. Name-target delivery
precedes field-5 Python within each individual queue record, as usual.

## A non-trigger event exposed by the wider corpus: `prop_sign.OnReadBegin`

Fifteen rows on seven maps use `prop_sign.OnReadBegin`. The output object is at `+0x734`, and
`CPropSign::UseBegin` (`0x10211db0`) explicitly fires it after the inherited use-begin output and
before opening the document/session UI. `OnReadEnd` remains at `+0x74c` and fires from use end.
The `use_icon` value is consumed separately from `+0x76c`; it is not the `+0x734` field.

The two tutorial signs author neither begin nor end rows, which is why RE43 could close their use
lifecycle without observing the begin producer in that map. The wider rows prove both the datamap
name and real call site. They use the ordinary event queue and may refire on every accepted sign
session; exclusive ownership prevents a second user from stealing a live session.

## Other producer families absent from the tutorial baseline

The 127-pair delta is not seven new trigger types. Most rows are additional content demand on
already shared runtime families. The survey JSON retains every pair and example; the high-signal
groups are:

| Family | Examples in the current delta | Classification |
| --- | --- | --- |
| Reused interaction/damage outputs | `prop_switch.OnActivate` 117; `func_button.OnIn` 11 / `OnOut` 10; `func_breakable.OnBreak` 12; `trigger_hurt.OnHurtPlayer` 11 | Existing generic/I/O or owning-class behavior; no new dispatcher. |
| Travel, spawn, camera and movers | `trigger_changelevel.OnChangeLevel` 103; `npc_maker.OnSpawnNPC` 62 / `OnNPCDied` 6; `camera_track.OnLeavingKeyframe` 1; `prop_mover.OnLinearMoveDone` 4 | Already recovered in `entity_io.md`, `camera-view-modes.md`, and `animation_and_movers.md`. |
| NPC perception/lifecycle | `npc_VHumanCombatant.OnFoundPlayer` 113, `OnDamaged` 23, `OnHearCombat` 14; dialogue, death, feeding, incapacity and enemy events across NPC subclasses | Valid authored demand; exact producer guards belong to NPC AI/combat/dialogue leaf RE, not this trigger pass. |
| Landmark and interesting-place state | cop-arrival/pursuit and map-entry outputs on `info_landmark`; `intersting_place.OnNPCArrived` 27 / `OnNPCLeft` 28 | Valid authored demand; exact producer timing remains separate leaf RE. |
| One-off world leaves | `logic_visibility_test.OnCanSeeTarget` 2; security-camera player discovery; `hud_timer.OnTimerComplete` 1; `prop_slashable.OnSlashed` 4; item `OnPlayerPickup` | Discovered and inventoried; their exact producer sites/guards are not established by this trigger pass. |
| Invalid producer key | `trigger_player_activity_level.OnTrigger` 2 | Silently discarded at load; no retail output. |

The two new special-target calls are `!playercontroller.LookAtEntityEye` in `sp_theatre` and
`!playercontroller.SetBodyAsCameraTarget` in `sm_hub_1`. They use the existing special-target
resolver and ordinary reflected input dispatch. Their camera/body side effects are separate from
the event transport itself.

This classification matters for scheduling: discovering a new output name does not imply a new
world tick. A perception callback may be produced by AI sensing, a timer by its own think, a mover
by movement completion, and an item by pickup. Once produced, all rows still join the same queue;
the producer's owning subsystem determines *when* the output object is fired.

## Python and VCD delta

The 209 new field-5 identifiers are content vocabulary—level callbacks and `G` keys—not new bridge
entry modes. Every current map row still uses the six-field output parser and queues Python in the
same record after named-target delivery. No second Python tick, scheduler or rollback path appears.
The exhaustive names and examples remain in the survey JSON rather than being duplicated here.

The 30 referenced scenes add only two event tokens absent from the tutorial's three scenes:
`expression` (66 events, `sm_medical_1` and `sp_theatre`) and `gesture` (9 events,
`sm_diner_1`). Both are actor-local facial/overlay requests in the choreography dispatcher. The
delta adds **no additional** VCD `firetrigger` value and **no** VCD Python event. It therefore adds
no entity-I/O producer from VCD; scene-entity outputs such as `OnCompletion` still belong to the
ordinary entity output surface.

## Evidence limits and open questions

- The survey is exhaustive for this manifest only. The patch-first 108-map corpus remains broader.
- The seven novel trigger leaves and `prop_sign.OnReadBegin` are static `vampire.dll` findings. The
  unresolved one-off world/NPC producer families listed above are demand discoveries, not
  leaf-level producer proofs.
- Exact collision callback order after a transform, exact partition containment behind
  `trigger_checkvolume`, and contact behavior during enable/disable cross the closed server leaf
  into retail `engine.dll`.
- No controlled retail run has yet tested blocked teleport destinations, overlapping activity or
  discipline volumes, repeated checkvolume inputs, bomb-site re-enable, or the 300-second dance
  latch. Unreal parity acceptance is likewise open.
- Retail's shared queue has no gameplay budget. The new leaves do not add a starvation guard:
  their outputs can still enter a zero-delay recursive cycle and monopolize the same-frame service
  pass exactly as described in `docs/vtmb/game_runtime.md`.
