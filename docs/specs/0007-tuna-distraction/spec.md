# 0007 tuna-distraction — grab and throw tuna cans to draw the guard off the door: the physics hands and the physics world the tutorial touches

## Witness
`sp_tutorial_1`'s tuna-can beat: the player picks up a sardine can (`fish_can`-class prop,
0.30 kg) with the physics hands, carries it and throws it; the impact is a sound the guard NPC
hears and walks to, and the player reaches the door while the guard is drawn off. Played against
retail: the office chair (`chairoffice`, 1.00 kg) picked up, carried and dropped; the sardine can
picked up and thrown; the stool (25 kg) and crate (100 kg) behave as retail does; nothing is
grabbable underwater; `sp_tutorial_1` and one Warrens map load with every physics classname
resolved; a wired `env_physexplosion` scatters props in its radius.

## Scope
The physics hands (grab, carry, throw), the thrown object's sound, `func_physbox`, the `phys_*`
constraint family, the impulse entities, `CPropSwitch`'s residue. Owned elsewhere and consumed
here: the hearing that turns the impact into an investigation — **0002**; `func_breakable` —
**0008**; the ragdoll rig, `prop_ragdoll` — **0014**; terminal interaction under `CPropSwitch`'s
reach — 13.4 (unowned); doors — **0009**.

## Sources
- Oracle: `docs/vtmb/physics-interaction.md`, `docs/vtmb/phy_vphysics.md`,
  `docs/vtmb/entity_io.md` (`CPropSwitch`, `env_phys*`), `docs/vtmb/npc-ai-reverse-engineering.md`
  § "Hearing, walked" (`MakeAISound`, `sound_volume_table.txt`).
- Authored data, V2 seams under `$ELYSIUM_WORK_ROOT/exports_v2/`: `maps/sp_tutorial_1.entities.glb`
  (`fish_can`, `chairoffice`, the guard, the door trigger), `vdata/system/sound_volume_table.txt`,
  `models/` (prop mass and bounds).

## Witness data
- Object handling is HL2's gravity-gun code (`weapon_physcannon`) wired to the **use key**,
  granted as a hidden inventory item named *Hands*; the item record stays authored data, the grab
  state does not live on it. Reached from `CBasePlayer::PlayerUse` via `Inventory_Find`, ahead of
  `FindUseEntity`. `player_pickup` / `CPlayerPickupController` is dead (zero callers).
- Search order: an open interactive-use session, then the grab candidate, then `FindUseEntity`.
  Geometry: a ray of `physcannon_tracelength` (80 units = 203.2 cm); on a miss a hull trace of
  ±4 units along the same segment; then a cone at `physcannon_cone` 0.97.
- `CanPlayerCarry`: simulating body, not the player's ground entity; summed mass under the limit;
  every bounding-box axis under the size limit; `m_nWaterLevel < 2`. The mass and size limits
  are float arguments the decompiler dropped (asm at `0x10411160`, RE54): UNRECOVERED; until
  then mass uses `physcannon_maxmass` (250 kg) and the size test fails loudly.
- Throw force is `player_throwforce` 1000 through the Source impulse unit. RE54 open: the
  release/throw path and `player_throwforce`'s consumer; whether the light-object lob is real;
  whether a thrown object raises a sound NPCs hear — load-bearing for this witness.
- `use_icon` slot 9 is `PhysicsHand`, slot 1 `CarryBody` (`hud/context_icons/` `T_` assets).
- `func_physbox` (179 placements, 17 maps) is `FElysiumPhysProp`'s brush twin: the entity's own
  `.ents` convex `hulls` as the body. `phys_ballsocket` (84), `phys_constraint` (78),
  `phys_convert` (56, a transaction on its target's leaf), `phys_thruster` (3),
  `phys_constraintsystem` (1), `phys_animlink` (1); `phys_hinge` (43) landed. `env_physimpact`
  (130) and `env_physexplosion` (61) are the first non-player consumers of `AddBodyImpulse` /
  `AddRadialImpulse`.
- `CPropSwitch` is a sequence player whose `OnActivate` fires on clip end (landed); its
  `soundgroup` on/off events and the `reset_state` runtime reset hook are open.
- Port shape: `FElysiumPhysicsHands`, one per player, owned by `FElysiumEntityWorld`, saved with
  the player; `TryAcquire` inside `UpdatePlayerInteraction` before the focus query;
  `QueryPhysicsGrab` a separate embodiment query (a different retail shape and mask from
  `QueryPlayerUse`); carry through `UPhysicsHandleComponent` on the pawn, the level Source's
  shadow controller is reproduced at; mass and angular-damping overrides restored on release.

## Stories
In build order. A story is done when every behaviour it lists is in the substrate and its
recovery is written in the oracle section it names. Numbers are stable ids cited by other
documents; a split keeps the number and adds a letter. Each open story carries the retail
contract the code must match, the job, what it consumes or provides, and a size (XS–XL) with the
model / effort tier recommended for it.

- [ ] **1. The hands and the search** (was PHYS2).
  Retail: the *Hands* item, `TryAcquire` ahead of the focus query, the ray → hull → cone search.
  Job: `FElysiumPhysicsHands` and `QueryPhysicsGrab`.
  Oracle: `physics-interaction.md` § "The physics hands".
  Size: M. Effort: Opus / high.
- [ ] **2. Eligibility.**
  Retail: `CanPlayerCarry` as above; the two dropped constants at `0x10411160`.
  Job: `FElysiumPhysProp::CanPlayerCarry`; the constants recovered from the asm, or the size test
  failing loudly and mass on `physcannon_maxmass` until they are.
  Oracle: `physics-interaction.md` § "Eligibility".
  Size: S. Effort: Sonnet / high; asm pass on `0x10411160` first.
- [ ] **3. Carry.**
  Retail: the carry rides the `+use` session (`CanPlayerFocus` admits, `BeginPlayerUse` returns
  `Started(WhileHeld)`, `EndPlayerUse(Released)` releases); mass/angular-damping overrides while
  held, restored on release.
  Job: `BeginBodyCarry` / `UpdateBodyCarry` / `EndBodyCarry` on the embodiment over
  `UPhysicsHandleComponent`; the overrides.
  Oracle: `physics-interaction.md` § "Carry".
  Size: M. Effort: Sonnet / high.
- [ ] **4. Throw.**
  Retail: `player_throwforce` 1000 through the Source impulse unit; the release path and the
  light-object lob are RE54's open items.
  Job: the release/throw path recovered and ported; the conversion derived from the unit
  convention.
  Oracle: `physics-interaction.md` § "Throw".
  Size: S. Effort: Sonnet / high; corpus pass on the release path first.
- [ ] **5. The impact is a sound.**
  Retail: whether a thrown object's impact raises an AI sound (`MakeAISound`, radius and type
  from `sound_volume_table.txt`) is UNRECOVERED (RE54) and load-bearing for the witness.
  Job: the producer recovered — the physics impact callback that raises the sound, its type and
  radius — and wired into 0002's hearing as a sound record; if retail raises none, the witness
  is re-read against retail play before anything is invented.
  Consumes: 0002/6a (hearing), 0002/10a (the sound sweep).
  Oracle: `npc-ai-reverse-engineering.md` § "Hearing, walked" (physics impacts, new).
  Size: S. Effort: Opus / high; corpus pass first.
- [ ] **6. The icons.**
  Job: `PhysicsHand` / `CarryBody` published into `FElysiumInteractionView`.
  Size: XS. Effort: Sonnet / low.
- [ ] **7. `func_physbox`** (was PHYS3).
  Job: `FElysiumPhysProp`'s brush twin over the `.ents` convex hulls.
  Provides: the brush body 0008's `func_breakable` surface binds to.
  Oracle: `phy_vphysics.md`.
  Size: S. Effort: Sonnet / medium.
- [ ] **8. The constraint family.**
  Job: `phys_ballsocket`, `phys_constraint`, `phys_convert`, `phys_thruster`,
  `phys_constraintsystem`, `phys_animlink` over `UPhysicsConstraintComponent` between two
  entities' `GetAttachBody()`.
  Oracle: `phy_vphysics.md` § "Constraints".
  Size: M. Effort: Sonnet / high.
- [ ] **9. The impulse entities.**
  Job: `env_physimpact` / `env_physexplosion` on `AddBodyImpulse` / `AddRadialImpulse`.
  Oracle: `entity_io.md` (`env_phys*`).
  Size: S. Effort: Sonnet / medium.
- [x] **10. `CPropSwitch` plays its sequence** (was 4.11); `OnActivate` on clip end. Oracle:
  `entity_io.md`.
- [ ] **11. `CPropSwitch` residue.**
  Job: the `soundgroup` on/off events (through 0011's typed event); the runtime reset hook
  applying `reset_state`.
  Oracle: `entity_io.md` (`CPropSwitch`).
  Size: S. Effort: Sonnet / medium.

## Seams
- Provides: the grab/carry/throw session and `QueryPhysicsGrab`; `CanPlayerCarry`; the `phys_*`
  family and the impulse consumers to 0008 and 0014; the impact sound to 0002's hearing.
- Consumes: 0002's hearing and sound sweep (5); the `+use` foundation (4.4, landed); 0011's typed
  events (11).
- Open recoveries: the two eligibility constants (2); the release path and the lob (4); the
  impact sound (5); the solid-transform frame and constraint axis identity for carried objects
  (shared with 0014).
