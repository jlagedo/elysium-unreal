# 0008 tuna-distraction — grab and throw tuna cans to draw the guard off the door: the physics hands and the physics world the tutorial touches

## Witness
`sp_tutorial_1`'s tuna-can beat: the player picks up a sardine can (`fish_can`-class prop, 0.30 kg)
with the physics hands, carries it, and throws it; the impact sound distracts the guard NPC
(consumes the awareness seam from 0005), and the player reaches the door while the guard is drawn
off. Proven from real input, not a mask read: office chair (`chairoffice`, 1.00 kg) picked up,
carried and dropped; the sardine can picked up and thrown; the stool (25 kg) and crate (100 kg)
behave as retail does; nothing is grabbable underwater. `sp_tutorial_1` and one Warrens map must
load with every physics classname resolved and no unhandled-class warning; a wired
`env_physexplosion` scatters props in its radius.

## Scope
- Roadmap rows absorbed: PHYS2 (The physics hands), PHYS3 (The rest of the physics world),
  4.11 (Trigger and `+use`-prop I/O gaps)
- Out of scope (belongs to another spec or is parked):
  - PHYS1, the ragdoll rig / PhysicsAsset construction and gameplay handoff — spec 0006.
  - `func_breakable` and the breakable damage-surface family — spec 0009.
  - `physics_prop_ragdoll`, `prop_ragdoll_attached`, `prop_ragdoll_special` — registered in VtMB,
    placed in no map; out of scope, a new task if a map ever places one.
  - Terminal interaction under 4.11's `CPropSwitch` reach — owned by 13.4 (computer terminals).
  - 4.12 (door faithfulness gaps) — separate row, not assigned here.
  - Doorknob lock authority, `prop_switch` interaction, lockables, `trigger_hurt` cadence — landed
    parts of 4.11, not carried forward (see Tasks).

## Requirements
1. Object handling is HL2's gravity-gun code (`weapon_physcannon`) wired to the **use key**,
   granted to the player as a hidden inventory item named *Hands*. The item record stays authored
   data; the grab state does not live on it. Retail: `docs/vtmb/physics-interaction.md`,
   `docs/vtmb/phy_vphysics.md`; reached from `CBasePlayer::PlayerUse` via `Inventory_Find`, ahead
   of `FindUseEntity`. `player_pickup`/`CPlayerPickupController` is proven dead (zero callers) —
   do not port it.
2. Grab candidate search runs **before** the ordinary focus query, reproducing retail's order: an
   open interactive-use session, then the grab candidate, then `FindUseEntity`.
3. Search geometry, in order: a ray of `physcannon_tracelength` (80 units = 203.2 cm); on a miss, a
   hull trace of ±4 units (±10.16 cm) along the same segment; then a cone at `physcannon_cone`
   0.97.
4. Eligibility (`CanPlayerCarry`): simulating body, not the player's ground entity; summed mass
   under the limit; every bounding-box axis under the size limit; player-side water gate
   (`m_nWaterLevel < 2`).
5. Two eligibility constants are open and not to be guessed: the mass and size limits are float
   arguments the decompiler dropped from the eligibility body (asm at `0x10411160`, per RE54).
   Until recovered, mass uses `physcannon_maxmass` (250 kg) and the size test fails loudly rather
   than admitting on an invented constant.
6. Carry rides the existing `+use` session: `CanPlayerFocus` admits, `BeginPlayerUse` returns
   `Started(WhileHeld)`, `EndPlayerUse(Released)` releases.
7. Throw force is `player_throwforce` 1000, converted through the Source impulse unit; the
   conversion is derived from the unit convention, not measured — a visibly wrong throw arc is the
   cheap test that it is right. RE54 open: the release/throw path and `player_throwforce`'s
   consumer, whether the light-object lob is real, whether a thrown object raises a sound NPCs
   hear (the tuna-can witness needs this to be true), the solid-transform frame and the constraint
   axis identity.
8. `use_icon` slot 9 is `PhysicsHand`, slot 1 is `CarryBody` (`hud/context_icons/` `T_` assets,
   R6.6); the hands publish the icon into `FElysiumInteractionView`, which today only entities
   produce.
9. `func_physbox` (179 placements, 17 maps) is `FElysiumPhysProp`'s brush twin: the entity's own
   `.ents` convex `hulls` as the body.
10. The `phys_*` constraint family over `UPhysicsConstraintComponent` between two entities'
    `GetAttachBody()`: `phys_ballsocket` (84), `phys_constraint` (78), `phys_convert` (56,
    transaction on its target's leaf rather than a constraint), `phys_thruster` (3),
    `phys_constraintsystem` (1), `phys_animlink` (1). `phys_hinge` (43) is already landed.
11. `env_physimpact` (130) and `env_physexplosion` (61) stop being stubs and become the first
    non-player consumers of `AddBodyImpulse` / `AddRadialImpulse`; presentation stays owned by
12. `prop_ragdoll` (52 placements, 14 maps — Warrens corpses, Malkavian mansion stalkers, Ventrue
    Tower aftermath): the one placed server ragdoll, seeded from its `demo_sequence` pose onto the
    PHYS1 physics asset.
13. `CPropSwitch` (4.11) is a sequence player whose `OnActivate` fires on clip end; landed. Open:
    its `soundgroup` on/off events, and a runtime reset hook that can apply `reset_state`.
    → `docs/vtmb/entity_io.md`.

## Design
`FElysiumPhysicsHands`, one per player, owned by `FElysiumEntityWorld` and saved with the player.
`TryAcquire` runs inside `UpdatePlayerInteraction` before the ordinary focus query. The search is a
new embodiment query, `QueryPhysicsGrab`, separate from `QueryPlayerUse` for the same reason the B6
feed search is separate — a different retail shape with a different mask; geometry lives here,
eligibility stays on the leaf as `FElysiumPhysProp::CanPlayerCarry`.
`BeginBodyCarry`/`UpdateBodyCarry`/`EndBodyCarry` on the embodiment are a `UPhysicsHandleComponent`
on the pawn — Unreal's own PD constraint to a target transform, the level Source's shadow
controller should be reproduced at. Mass and angular-damping overrides applied while held are
restored on release, as retail's saved arrays do.

## Seams
- Consumes: PHYS1's impulse seam (`AddBodyImpulse`/`AddRadialImpulse` on the physics leaf,
  spec 0006); the awareness/hearing seam from 0005 (a thrown object's impact sound must be a source
  the NPC's hearing perceives); 4.4 (`+use` foundation); 4.1 and 8.3 for 4.11's remaining hooks.
- Provides: the physics-hands grab/carry/throw session and `QueryPhysicsGrab`, for any later spec
  needing player object manipulation; `FElysiumPhysProp::CanPlayerCarry` eligibility; the
  `phys_*` constraint family and `env_physimpact`/`env_physexplosion` impulse consumers, for the
  breakables spec (0009) and any prop-physics consumer; `prop_ragdoll` placement, seeded onto
  PHYS1's physics asset.

## Tasks
- [ ] PHYS2: `FElysiumPhysicsHands` service, `TryAcquire` ordering ahead of the ordinary focus
  query.
- [ ] PHYS2: `QueryPhysicsGrab` — ray → hull trace → cone search.
- [ ] PHYS2: `FElysiumPhysProp::CanPlayerCarry` eligibility (mass, size, water gate); size test
  fails loudly pending the two constants at `0x10411160`.
- [ ] PHYS2: carry over the existing `+use` session (`WhileHeld`/`Released`); `UPhysicsHandleComponent`
  carry/update/end; mass/angular-damping override + restore.
- [ ] PHYS2: throw via `player_throwforce`, Source impulse-unit conversion.
- [ ] PHYS2: publish `PhysicsHand`/`CarryBody` icons into `FElysiumInteractionView`.
- [ ] PHYS3: `func_physbox` as `FElysiumPhysProp`'s brush twin (`.ents` convex hulls as body).
- [ ] PHYS3: `phys_ballsocket`, `phys_constraint`, `phys_convert`, `phys_thruster`,
  `phys_constraintsystem`, `phys_animlink` over `UPhysicsConstraintComponent`.
- [ ] PHYS3: `env_physimpact` / `env_physexplosion` wired to `AddBodyImpulse`/`AddRadialImpulse`.
- [ ] PHYS3: `prop_ragdoll` placement seeded from `demo_sequence` pose onto the PHYS1 physics asset.
- [~] 4.11: `CPropSwitch` sequence player, `OnActivate` on clip end — landed.
- [ ] 4.11: `CPropSwitch` `soundgroup` on/off events.
- [ ] 4.11: runtime reset hook applying `reset_state`.

## Open questions
- The two `CanPlayerCarry` eligibility constants (mass limit, size limit) — decompiler dropped
  float args at `0x10411160`; not yet recovered.
- Whether the light-object lob (thrown-object trajectory shape) is real in retail.
- Whether a thrown object raises a sound NPCs hear — load-bearing for this spec's witness beat;
  unresolved in RE54.
- The solid-transform frame and constraint axis identity for carried/thrown objects.
- `CPropSwitch`'s `soundgroup` on/off event shape and the `reset_state` runtime reset hook —
  unspecified beyond "remains open" in the plan.
