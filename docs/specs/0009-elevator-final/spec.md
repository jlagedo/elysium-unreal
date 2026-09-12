# 0009 elevator-final — the elevator, the final fight, the last Jack conversation and the transition to sm_hub_1

## Witness
`sp_tutorial_1`'s closing beat, played start to finish: the player rides the tutorial elevator
(`func_elevator` + `prop_button`), fights through the encounter on the behaviour 0002–0008
ported, has the final conversation with Jack, and walks through the exit trigger into `sm_hub_1`
with quest, inventory and save state carried over. The map load is the landed
`trigger_changelevel` + landmark travel (4.6).

## Scope
The mover family's remainder (`func_rotating`, `func_movelinear`, keyframed movers, the push
observable), the door faithfulness gaps (`CBaseDoor`), the level-script delegated fills, the
transition. Owned elsewhere and consumed here: the fight — **0002**, **0005**, **0006**,
**0007**, **0008**; the conversation — **0004**; the gamepad and rebinding remainder — **0016**;
the mover sound's typed event — **0011** (the emission point is decided here); async travel
(10.4) and the packaged content path (10.5) — unowned.

## Sources
- Oracle: `docs/vtmb/animation_and_movers.md` B, B.4, B.4.3, B.4.4, `docs/vtmb/entity_io.md`,
  `docs/vtmb/level_transitions.md`.
- Authored data, V2 seams under `$ELYSIUM_WORK_ROOT/exports_v2/`: `maps/sp_tutorial_1.entities.glb`
  (the elevator, every `func_door*`, the exit trigger), `scripts/` (`sp_tutorial_1`'s level
  script).

## Witness data
- Movers are `MOVETYPE_PUSH` in retail and displace what they touch from their own side;
  `FElysiumMoverBase` sweeps instead, and Chaos already shoves the pawn clear of a closing
  door's arc; `dmg` is not dealt and `OnBlockedClosing` does not fire unless the sweep is fully
  blocked. The retail observable is reproduced through the engine's sweep/displacement response,
  never a ported `PhysicsPushEntity` (S12).
- Doors, landed: the doorknob lock authority (a knob owns its lock, the door consults its nearest
  one via `IsUseRefused`), silent `Open`, the unconditional `+use`
  `ResolveToggleStateFromTransform` resync, `OpenAwayFromEntity` / `bResolveSwing` /
  `SF_DOOR_ONEWAY`; the mid-motion self-heal reissues `DoorGoDown(1)` / `DoorGoUp(1,1)`
  (`0x100efdc0` / `0x100efddf`), both propagating to the linked leaf.
- `CBaseDoor::Use`'s ordered chain: `noopenwanted` refusal, the mid-motion self-heal, the
  `{AT_BOTTOM, AT_TOP} ∪ NO_AUTO_RETURN` admission set (a `GOING_*` non-return door is a silent
  no-op). Retail dispatches arrival on a move-start callback (`MoveDone`), not the mutable
  `ToggleState`. Input level (`FUN_100f0170` / `FUN_100f00a0` / `FUN_100f0210`): `OnOpen` fires
  at the `Open` input and again in `DoorGoUp`; `Open` / `Toggle` gate on `IsUseRefused`; `Close`
  carries no lock test; admission is `!= AT_TOP` / `!= AT_BOTTOM`; `Toggle` reaches the motion
  helpers directly, so only `Open` / `Close` double-fire.
- Sound: retail emits `close` at arrival (`DoorHitBottom`), starts no loop and stops none;
  `swing` is one event; any looping is the soundgroup's. The port plays `close` at motion start
  and owns a loop in code.
- Blocked: `Blocked` damages in both directions, self-reverses only behind `CRotDoor`'s
  re-entrancy byte, synchronises its targetname group to its own direction; `StartBlocked` /
  `EndBlocked` carry `OnBlocked*` / `OnUnblocked*` with different activators; `CRotDoor::Blocked`
  adds a stuck detector. A pure rotation is never swept (`MoveComponentImpl` skips the sweep on
  zero translation), so `OnMoveBlocked` is unreachable for a rotating leaf until a
  rotating-sweep mechanism exists. The swing blocked-latch flip and its arming predicate are
  recovered; the blocker's `+0x98` identity is not statically recoverable (zero-initialised in
  `CBaseEntity`'s constructor, no setter in the image; `+0x94` beside it is the `CAI_BaseNPC`
  pointer).
- A live session showed a baked `func_door_rotating` firing `OnOpen` and the swing sound while
  its body did not visibly rotate; state and collision were correct.
- Level script: core landed (B2, 9.3b, 9.3c); the character-method fill is delegated — inventory
  natives are 9.8's, feats/stats landed (9.4), disposition/camera/barter arrive with their
  domains; `SquadSeesPlayer` stays a stub (no shipped caller).

## Stories
In build order. A story is done when every behaviour it lists is in the substrate and its
recovery is written in the oracle section it names. Numbers are stable ids cited by other
documents; a split keeps the number and adds a letter. Each open story carries the retail
contract the code must match, the job, what it consumes or provides, and a size (XS–XL) with the
model / effort tier recommended for it.

- [x] **1. `func_elevator` and `prop_button`** (was 4.8). Oracle: `animation_and_movers.md` B.
- [x] **2. The door's lock authority and swing gates** (was 4.12): the doorknob authority,
  silent `Open`, the `+use` resync, `OpenAwayFromEntity`, the self-heal reissue arguments.
  Oracle: `animation_and_movers.md` B.4.
- [x] **3. The level script's core** (was 9.3: B2, 9.3b, 9.3c).
- [ ] **4. `CBaseDoor::Use`'s guard chain.**
  Retail: `noopenwanted`, the self-heal step, the admission set (Witness data).
  Job: `+use` through the ordered chain instead of `InputToggle`.
  Oracle: `animation_and_movers.md` B.4.
  Size: S. Effort: Sonnet / high.
- [ ] **5. `MoveDone`'s arrival binding.**
  Retail: arrival dispatched on the move-start callback, independent of the live `ToggleState`.
  Job: the binding, so a locked door caught mid-close by a resync cannot drop `OnFullyClosed`.
  Oracle: `animation_and_movers.md` B.4.
  Size: S. Effort: Sonnet / medium.
- [ ] **6. Input-level outputs and admission.**
  Retail: `FUN_100f0170` / `FUN_100f00a0` / `FUN_100f0210` as above.
  Job: the double-fire, the gates and the admission per input.
  Oracle: `animation_and_movers.md` B.4.
  Size: S. Effort: Sonnet / high.
- [ ] **7. The mover sound's emission point.**
  Retail: `close` at arrival, no code-owned loop.
  Job: the emission moved to arrival and the loop retired, through 0011's typed `Openable` event
  when it lands and the soundgroup call until then.
  Consumes: 0011/4.
  Oracle: `animation_and_movers.md` B.4.3.
  Size: XS. Effort: Sonnet / low.
- [ ] **8. The blocked family.**
  Retail: bidirectional damage, `CRotDoor`'s re-entrancy self-reverse, group sync, the
  `StartBlocked` / `EndBlocked` edges and activators, the stuck detector; the any-entity blocker
  filter waits on 0002's door-obstruction reaction.
  Job: the family on `FElysiumMoverBase`; a rotating-sweep mechanism so a rotating leaf can be
  blocked at all.
  Oracle: `animation_and_movers.md` B.4.4.
  Size: M. Effort: Opus / high.
- [ ] **9. The mover-push observable.**
  Retail: `dmg` on a blocked close, `OnBlockedClosing`, own-side displacement.
  Job: reproduced through the engine's sweep/displacement response (S12).
  Oracle: `animation_and_movers.md` B.
  Size: M. Effort: Opus / medium.
- [ ] **10. `func_rotating`, `func_movelinear`, keyframed movers.**
  Retail: spin-up/down and hurt-touch on `func_rotating`; the linear and keyframed families.
  Job: the three families on `FElysiumMoverBase`.
  Oracle: `animation_and_movers.md` B.
  Size: M. Effort: Sonnet / high.
- [ ] **11. The swing blocked-latch inversion.**
  Retail: the flip and its arming predicate; the blocker's `+0x98` identity needs a live-retail
  capture of which blocker classes set it.
  Job: the capture, then the port.
  Oracle: `animation_and_movers.md` B.4.4.
  Size: S. Effort: Sonnet / high; live capture first.
- [ ] **12. Runtime rotation of the baked `func_door_rotating`.**
  Job: confirm 4.3's brush-travel holds for a rotating leaf at runtime; fix the visual body's
  follow of the mover transform if it does not.
  Oracle: `animation_and_movers.md` B.
  Size: S. Effort: Sonnet / medium.
- [ ] **13. The level script's delegated fills.**
  Job: inventory natives once 9.8's remainder exists; disposition, camera and barter with their
  domains; `SquadSeesPlayer` stays a stub.
  Consumes: 0004 (disposition), 0012 (camera).
  Size: S. Effort: Sonnet / medium.
- [ ] **14. The transition to `sm_hub_1`.**
  Retail: `trigger_changelevel` + landmark travel with the carried set (`level_transitions.md`).
  Job: the played hand-off with quest, inventory and save state carried; the carried-entity set
  is 0017's to complete.
  Oracle: `level_transitions.md`.
  Size: S. Effort: Sonnet / medium.

## Seams
- Provides: the door and mover faithfulness the tutorial's doors exercise; the proven
  `sp_tutorial_1` → `sm_hub_1` hand-off for later specs.
- Consumes: 0002–0008 for the fight; 0004 for the conversation; 0011's typed event (7); 0016 for
  pad play of the same beat.
- Open recoveries: the blocker `+0x98` identity (11); the rotating leaf's runtime body (12).
