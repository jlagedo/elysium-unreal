# VtMB's rigid-body world — ragdolls, the physics hands, and the placed physics surface

The `.phy` container itself is `docs/vtmb/phy_vphysics.md`. This document owns the **behaviour**:
which models carry a ragdoll rig, where a ragdoll appears at all, how the player picks an object up
and throws it, and what the maps place. The death transaction that *produces* a corpse ragdoll —
the force envelope, the schedule fork, the solid-body policy — is
`docs/vtmb/combat-and-damage.md`'s and is only referenced here.

Three unrelated systems answer to the word "ragdoll" in VtMB, and they share only the `.phy` file:
a **client ragdoll** the server never simulates, a **server ragdoll prop** placed in maps, and the
**gib** pieces. The player's object handling is a fourth system that shares the same rigid bodies
and is described here for that reason.

---

## 1. Which models carry a ragdoll rig

A ragdoll rig is a `.phy` whose keyvalue tail carries `ragdollconstraint` blocks and whose solids
are named after bones. Over the patch-first install (2,929 `.phy` files) **324 carry one**; over the
retail VPKs alone (2,854 files) **289** do.

| Tree | Files |
|---|---:|
| `models/character/npc/**` | 238 |
| `models/character/pc/**` | 58 |
| `models/character/monster/**` | 19 |
| `models/character/gibs/**` | 6 |
| `models/cinematic/characters/e3/sewer_guard` | 1 |
| `models/scenery/structural/fishmarket/tuna.phy` | 1 |
| `models/weapons/severed_arm/wield/w_f_severed_arm.phy` | 1 |

**One rig dominates.** 289 of the 324 are the same humanoid shape — **15 solids, 14 constraints** —
covering every ordinary NPC and all 58 PC bodies (clan × sex × armour 0–3). The remainder are the
monsters and the odd cases: 18/17 on the security guard and its kin, 3/2 on the two-piece bodies,
27/26 at the top of the range, and 2/1 at the bottom.

`character/gibs` is the reason severed limbs behave like bodies: `left_arm`, `left_leg`,
`left_torso`, `right_arm`, `right_leg`, `right_torso` each ship their own rig, as do the
security-guard corpse pieces (`corpse2_leftarm` … `sg_headless`) and `undead_femalepart1..3`.

The nine bodies that carry **no** `ACT_DIERAGDOLL` at all are recorded in
`docs/vtmb/animation_and_movers.md`; that is a clip-corpus fact, independent of this one.

---

## 2. The client ragdoll — the death case

`CBaseAnimating::BecomeClientRagdoll` (`0x10090180`) is the only producer that a player normally
sees. The transaction that reaches it, its force envelope and its refusal cases are
`docs/vtmb/combat-and-damage.md` § "Corpse construction and the solid-body policy". Two facts about
the *result* are this document's:

**The corpse entity is the dying NPC, not a new one.** In the ragdoll branch `CreateCorpse`
(`0x1032c0e0`) returns `this` — the same `CAI_BaseNPC`, now `FSOLID_NOT_SOLID`, `MOVETYPE_NONE`,
velocity zeroed and think cleared. Only the static-corpse branches (`No_Ragdoll_Death`, the
player) call `SpawnStaticCorpse` and get a second entity. Burning is a later removal/look tail,
not a static-creation arm (`0x1032c32f`).

**Nothing on the server simulates.** The server sets `m_nRenderFX = 0x17` (`kRenderFxRagdoll`) and
stops; the client builds a `CRagdoll` from the model's vcollide and simulates it locally
(`cl_ragdoll_collide`, and a client-physics timescale ConVar described as "Sets the scale of time
for client-side physics (ragdolls)"). The consequence is observable: the server entity — the thing a
`+use`, a feed or a loot reaches — **stays at the death origin** while the drawn body slides,
falls downstairs or comes to rest somewhere else entirely.

Client-side pushes exist and are the client's alone: `CRagdollBulletEnumerator`, reached from
`C_TEGunshotDecal`, and `CRagdollExplosionEnumerator`. Both shove nearby client ragdolls; neither
touches server state.

### Suppressors

| Condition | Result |
|---|---|
| the player dies | never ragdolls — static corpse, all 18 body-fire emitters stopped |
| MiscFlag `No_Ragdoll_Death` (bit 19, `0x80000`) | static corpse; `SCHED_TROIKA_D_VISION_OF_DEATH` is the authored producer |
| model carries no ragdoll collide | `BecomeClientRagdoll` only zeroes bounds and returns false; ordinary corpse think is replaced anyway. Only the separate state-7 selector fork chooses `SCHED_DIE`. |
| `CNPC_VZombie` with `should_ragdoll` unset | gates two VZombie paths (`vfunc142`, `vfunc301`) |

`should_ragdoll` is authored **162 times across 7 maps and is `1` in every one** — `hw_cemetery_1`
70, `la_crackhouse_1` 39, `sp_giovanni_4` 31, `sp_giovanni_3` 11, `sp_giovanni_2a`/`2b` 4 each,
`la_hospital_1` 3. The unset case is therefore the class default and is never authored; whether it
selects `SCHED_VZOMBIE_ANIMATED_DEATH` is not established here.

---

## 3. The server ragdoll props

`CRagdollProp` is a real server-simulated ragdoll: up to **24 bodies** (`m_ragdoll.list[0..23]`,
each with `pObject`, `pConstraint`, `parentIndex` and `originParentSpace`), a 30-entry bone index,
30 networked positions and angles (`m_ragPos`, `m_ragAngles`), `m_allAsleep`, `m_ragdollMins`/`Maxs`,
`m_flBlendWeight`, `m_nOverlaySequence` and `m_hUnragdoll`. The client mirror is `C_ServerRagdoll`.
Its `.phy` is parsed on the client (`"CRagdollProp::CreateObjects: Couldn't Lookup Bone %s"`), so
the constraint blocks are consumed by name against the model's bones.

Four classnames register; one is placed.

| classname | class | placements |
|---|---|---:|
| `prop_ragdoll` | `CRagdollProp` | **52** across 14 maps |
| `physics_prop_ragdoll` | `CRagdollProp` | 0 |
| `prop_ragdoll_attached` | `CRagdollPropAttached` (`m_ragdollAttachedObjectIndex`, `m_attachmentPointRagdollSpace`) | 0 |
| `prop_ragdoll_special` | `CRagdollPropSpecial` (adds `damage`, `explosion_params`, `m_hBreaker`, `CRagdollPropSpecialBreakThink`) | 0 |

The 52 are set dressing — corpses, hanging bodies and aftermath:

| Map | Count | What |
|---|---:|---|
| `hw_warrens_3` | 13 | security-guard corpses and pieces, sewer worker, `undead_femalepart1..3` |
| `la_ventruetower_1` | 9 | `attack_props_1..9` — the Sabbat attack's dead guards and SWAT |
| `hw_warrens_2b` | 7 | skeletons, a dead bum, `sg_headless`, the three undead pieces |
| `la_malkavian_2` / `_3` | 5 / 5 | `corpse3` variants and the Stalkers |
| `la_malkavian_4` | 3 | corpses and a stalker |
| `hw_netcafe_1`, `la_malkavian_3b` | 2 each | |
| `hw_609_1`, `hw_warrens_1`, `la_abandoned_building_1`, `la_parkinggarage_1`, `la_plaguebearer_sewer_1`, `la_skyline_1` | 1 each | |

Keys authored on them: `model`, `angles`, `origin`, `skin`, `crossfade_skin_time`,
`npc_transparent`, `physdamagescale`, `disableshadows` on all or nearly all; `demo_sequence` on 37,
`StartHidden` on 30, `targetname` on 31, `spawnflags` on 24 (`0`, `4`, `32`, `8196`), plus
`renderamt`/`rendercolor` on 15 and `renderfx`/`rendermode` on 2.

**Retail's own Python never spawns a ragdoll.** Every scripted `prop_ragdoll` is an Unofficial Patch
addition: `killWerewolf` and the nine-body Ventrue Tower `collateral()` in `vamputil.py` (which also
`Kill()`s them all again on `Clean_Rubble` or during credits), plus `ashFakeBody` and the dead Muddy
in `hollywood/hollywood.py`.

---

## 4. The physics hands — how the player picks things up

VtMB kept HL2's gravity-gun code and wired it to the **use key**. The weapon is invisible and is
called *Hands*.

### The item

`weapon_physcannon` is an ordinary `vdata/items` record, granted to the player at game start. It
names `models/weapons/w_null.mdl` for both wield models (`docs/vtmb/wielded_weapons.md` § the
render gate lists it among the 22 explicit opt-outs) and declares itself invisible:

```
"item_type"          "hidden"      "bucket"           "0"
"is_visible_in_hud"  "0"           "bucket_position"  "0"
"shows_view_model"   "0"           "camera_class"     "force_1st"
"weight"             "0"           "equip_mask"       "Normal"
```

It also carries a `Primary` activation with `Dmg "Strength 0 Bashing Close_Combat_Brawl"`, the
fists' swing/botch sounds and the fists' inventory sprites — so the record is the unarmed hand as
well as the grab.

Retail ships two more of the same shape, unplaced and ungranted: `item_s_physicshand` and
`weapon_physgun`. Retail also ships the HUD art the grab uses —
`materials/hud/physicshand_open.vmt`, `physicshand_closed.vmt` and
`materials/hud/context_icons/physicshand.vmt`.

**How it is granted.** Retail lists `weapon_physcannon` in every `StartingEquip` table in
`vdata/system/items.txt`, and all six playable clans in `clandoc000.txt` resolve
`Starting_Equipment` to `Player_Kindred`, which has it. The Unofficial Patch **moves** the grant
rather than removing it: it comments the item out of `Player_Kindred` and both of `vamputil.py`'s
setup paths (Basic and Plus) instead run

```python
if not (pc.HasItem("weapon_physcannon")):
    ... pc.GiveItem("weapon_physcannon")
```

so an existing save that lacks it gets it too. The patch also adds `printname "Hands"`,
`description "Your hands."`, `is_droppable "0"` and `permanent_inventory "1"` to the record, and
renames the two unused siblings to `-null`. Its readme states the constraint directly:
`weapon_physcannon (manipulation) -> hand won't work if changed`.

`vamputil.py` reads the item back in the criminal-law check: swinging anything *other* than
`item_w_unarmed`, `item_w_fists`, `weapon_physcannon`, the tire iron, the bat or the torch in a
public area sets criminal level 1.

### The acquire chain

```
CBasePlayer::PlayerUse                 0x10167850   IN_USE (0x20)
  ├─ m_hInteractiveUseTarget                        an open session wins outright
  ├─ 0x104115b0                                     gate: m_nWaterLevel < 2
  │    └─ 0x104115f0   Inventory_Find(player, CPhysicsProp::vfunc36())
  │                                                 vfunc36 returns "weapon_physcannon"
  │         └─ 0x10411630   holding? return the held object : find one
  │              └─ 0x104116d0   the search
  └─ FindUseEntity                                  only if the grab found nothing
```

The grab candidate is consulted **before** the ordinary use entity, and only after an already-open
interactive-use session. `CPhysicsProp` is the only class whose slot 36 answers
`weapon_physcannon`, so `prop_physics` is the grabbable class.

**The search** (`0x104116d0`), in order, with the result cached for the frame in two slots
(`+0x87c` value, `+0x884` frame stamp):

1. a ray from the eye of `physcannon_tracelength`, trace mask `0x46004003`, filter
   `CTraceFilterNoOwnerTest`;
2. on a miss (or a hit the follow-up test rejects), a **hull trace with ±4-unit extents** along the
   same segment;
3. then a cone search (`0x1040f550`) at `physcannon_cone`.

**Eligibility** (`0x10411160` → `0x1016a660`, HL2's `CanPickupObject` shape):

- entity flags2 bit `0x2` set;
- **`MOVETYPE_VPHYSICS` (7)**;
- not the player's own ground entity;
- the summed mass of up to 32 `VPhysicsGetObjectList` objects below a mass limit;
- every axis of `(maxs - mins)` below a size limit.

The two limit arguments are lost in the decompiled body (both are float stack arguments the
decompiler dropped); `physcannon_maxmass` is the obvious candidate for the first, and that is an
inference, not a reading. Recovering them means reading the asm at `0x10411160`.

### Tuning

Defaults as registered in `vampire.dll`. Distances are Source units (1 unit = 2.54 cm); masses are
kilograms; forces are Source's `kg·in/s` impulse units.

| ConVar | Default | | ConVar | Default |
|---|---:|---|---|---:|
| `physcannon_tracelength` | **80** | | `physcannon_minforce` | 700 |
| `physcannon_mega_tracelength` | 750 | | `physcannon_maxforce` | 1500 |
| `physcannon_maxmass` | 250 | | `physcannon_pullforce` | 4000 |
| `physcannon_cone` | 0.97 | | `physcannon_mega_pullforce` | 8000 |
| `physcannon_ball_cone` | 0.997 | | `physcannon_chargetime` | 2 |
| `physcannon_lob_maxweight` | 5 | | `physcannon_lob_maxforce` | 650 |
| `physcannon_launch_multiplier` | .01 | | `player_throwforce` | 1000 |

`g_debug_physcannon` is registered alongside them.

**Two values are VtMB's own.** HL2 ships `physcannon_tracelength 250` and a launch multiplier near
`1.8`; VtMB ships **80** and **.01**. Reading that pair as "keep the carry, delete the punt" fits
every other fact here — no gravity-gun viewmodel, no HUD slot, an item named *Hands* — but it is an
interpretation of two numbers, not a traced behaviour.

`physcannon_lob_maxweight 5` / `lob_maxforce 650` plausibly describe the light-object lob (the
sardine at 0.30 kg and both tutorial chairs at 1.00 and 5.00 kg fall under it; the 25 kg stool and
100 kg crate do not). The call that reads those two ConVars has not been traced, so this is a
hypothesis.

### The carry

`CGrabController` is the holder, a member of `CWeaponPhysCannon` (`m_grabController`) and of
`CPlayerPickupController`. Its recovered fields:

| Field | Meaning |
|---|---|
| `m_shadow` | the Source shadow-controller state that drives the body toward a target transform |
| `m_timeToArrive`, `m_errorTime`, `m_error`, `m_angleAlignment` | the controller's convergence terms |
| `m_savedMass[32]`, `m_savedRotDamping[32]` | the carried objects' originals, restored on release |
| `m_flLoadWeight` | the carried weight the player is charged with |
| `m_attachedEntity`, `m_attachedAnglesPlayerSpace`, `m_attachedPositionObjectSpace` | the hold pose |
| `m_vecPreferredCarryAngles`, `m_bHasPreferredCarryAngles` | per-object authored carry orientation |
| `m_bCarriedEntityBlocksLOS`, `m_bIgnoreRelativePitch` | the two behaviour switches |

### What is dead code

HL2's *other* grab — `player_pickup` / `CPlayerPickupController`, the controller HL2 uses when the
player has no gravity gun — is fully present and **never created**: `PlayerPickupObject`
(`0x1040d4b0`) has zero callers, direct or virtual. `CHL2_Player` is present and unreached for the
same reason. Neither is part of VtMB's behaviour.

---

## 5. What the maps place

Classname counts over all 108 engine-resolvable maps, patch-first.

| classname | Placements | Maps |
|---|---:|---:|
| `prop_physics` | 1,517 | 72 |
| `func_physbox` | 179 | 17 |
| `env_physimpact` | 130 | 15 |
| `env_physexplosion` | 61 | 15 |
| `phys_ballsocket` | 84 | 14 |
| `phys_constraint` | 78 | 10 |
| `phys_convert` | 56 | 15 |
| `phys_hinge` | 43 | 5 |
| `prop_ragdoll` | 52 | 14 |
| `phys_thruster` | 3 | 2 |
| `prop_physics_contested` | 3 | 2 |
| `phys_constraintsystem`, `phys_animlink` | 1 each | 1 each |

Every `prop_physics` in the exported maps carries `override_mass "-1"`, so the authored `.phy`
`totalmass` is the only mass the game uses for them (`docs/vtmb/phy_vphysics.md`).

`sp_tutorial_1`'s set is the worked example — the office chairs and the sardine cans are the two the
tutorial teaches:

| Model | `.phy` mass | Count |
|---|---:|---:|
| `scenery/structural/society/shutters.mdl` | 1.00 | 12 |
| `scenery/structural/society/stool.mdl` | 25.00 | 11 |
| `scenery/misc/bottles/glassa.mdl` | 1.46 | 6 |
| `scenery/PHYSICS/sardine/sardine.mdl` | 0.30 | 4 |
| `scenery/PHYSICS/bottle/bottle.mdl` | 1.00 | 4 |
| `scenery/trash/barrels/barrel{a,b,c,d}.mdl` | 7.00 | 4 |
| `scenery/misc/trashgarage/trashgarage.mdl` | 3.00 | 3 |
| `scenery/structural/warehouse/break_crate.mdl` | 100.00 | 3 |
| `scenery/furniture/retro_chair/retro_chair.mdl` | 5.00 | 1 |
| `scenery/misc/chairoffice/chairoffice.mdl` | 1.00 | 1 |
| `scenery/structural/society/barrel.mdl` | 5.00 | 1 |
| `scenery/misc/plates/{plate4,plate5,plate7,crapy}.mdl` | 0.05 | 4 |

---

## 6. Other producers of impulse

- `env_physimpact` and `env_physexplosion` are the authored impulse entities. Their I/O surface is
  `docs/vtmb/entity_io.md`'s; their effect presentation is `docs/vtmb/effects.md`'s.
- The death force envelope, clamped to `50000.0`, is `docs/vtmb/combat-and-damage.md`'s.
- NPCs that lift and throw are their own schedules, not this system:
  `TASK_VMING_XIAO_PICKUP_THROWABLE` with the `ming_xiao_pickup` ConVar ("Set this to 1 to allow
  ming xiao to pickup and throw bodies"), the Tzimisce `ACT_THROW_BODY` / `ACT_PICKUP_BODY_*` family,
  the Hengeyokai fish pickup, and `TASK_MANBAT_GRAB_COP`. The activity vocabulary they use
  (`ACT_PICKUP_LIGHT` `0x127`, `ACT_PICKUP_LIGHTIDLE` `0x128`, `ACT_PICKUP_LIGHTCARRY` `0x129`,
  `ACT_PICKUP_LIGHTTHROW` `0x126`) is `docs/vtmb/animation_and_movers.md`'s.

---

## 7. Open questions

| Question | What would close it |
|---|---|
| The mass and size limits passed into the eligibility test | read the asm at `0x10411160`; both floats are lost in the decompiled body |
| Which input releases versus throws, and where `player_throwforce` is consumed | trace the release path out of `CWeaponPhysCannon`; the acquire path is closed above |
| Whether the light-object lob is real, and its trigger | locate the reader of `physcannon_lob_maxweight` / `lob_maxforce` |
| Whether a carried or thrown object makes a sound NPCs hear | look for a `CSoundEnt::InsertSound` on the physics impact path |
| Per-prop `m_vecPreferredCarryAngles` authoring | find the producer that sets it; no map keyfield is known to |
| Whether `should_ragdoll` unset selects `SCHED_VZOMBIE_ANIMATED_DEATH` | read `CNPC_VZombie::vfunc142` / `vfunc301` |
| `m_hUnragdoll`'s consumer on `CRagdollProp` | no placed map data is known to reach it |

## Provenance

Retail server `vampire.dll`, SHA-256
`c546f4de2003624d72f54d03805e0dbe1d8157231adcc62368ff53fe6e48a76f`, image base `0x10000000`;
retail `client.dll`, SHA-256
`e88beae0dd03af06493c71c5e8d87a6993b54e590cb6ad37cd3513c588582870`. Corpus counts are over the
engine-resolved patch-first install and the retail VPK set separately, both stated where they
differ.


## V4d D3 — cooked rig admission and retained visual lifetime (2026-10-05)

`0x10090180` model-interface slot 18 is represented by the cooked skeletal mesh's
`PhysicsSourceData->Data.bHasPhysics`. PhysicsAsset absence/empty bodies/simulation refusal
is a bake/attachment/handoff failure, never evidence of source riglessness. Native Spine2 is
looked up on the actual mesh using typed source/native bone names (`0x1032c226`); missing data
is reported, never replaced by the state-7 bone -1 seed. Every source-rig transaction makes
its weighted pick, but only explicit -1 resets sequence/cycle. The ordinary real-bone body
starts from its current pose.

StartBodyRagdoll retains the spike-proven Ragdoll profile -> simulate -> verify -> wake order.
Admission logs component, real asset path, body count and simulation result. Terminal release
logs the same component's pre-release simulation/awake state, then releases claims, simulation,
collision and physics state, presentation-owned children and the mesh. ReleaseNpcVisual takes
an explicit visual pointer: a simulated component can detach from its motor. Pointer clearing
is idempotent, and the map retirement check precedes UObject access. Children belonging to
other entities survive detached; no shared-owner stale-component sweep is used.

The entity/use/feed/loot anchor remains at the death spot. Ordinary mortal corpses remain while
seen, burning ones release at +10 regardless of sight, ordinary pedestrians retain indefinitely,
and fade children release only on actual removal (about +13.8 from alpha255). Lethal death and
ScriptHide retain the body; terminal Kill, including already hidden removal, and destruction
release it. Chaos solving/collision tuning is the named presentation modernization; impulse,
hitbox producer completion, prop_ragdoll/friction and BurnModel visuals remain 0014; save
semantics remain V6. Coder arms record lifetime requests, not proof of a real Chaos solve.

## L0 object and shadow service [decompiled/PE asm, 2026-10-10]

This targeted recovery supersedes the earlier planning claim that the VPhysics implementation
was outside the corpus. `vphysics.dll` is now available. It does not confer closure on the
earlier Chaos presentation claim or the generic L0 service. The complete address/argument walk,
port comparison and repaired dependencies are in
`docs/specs/layers/L0-entity/walks/L0-physics-producer.md` (address map, not independently confirmed as a whole; selected caller details below are newly confirmed).

`CPhysicsHook::Init vampire.dll 0x1002d7d0` requests `VPhysics030`, `VPhysicsCollision007` and
`VPhysicsSurfaceProps001`, at `DAT_10723934/3c/44`. VPhysics factory `vphysics.dll 0x26001dd0`
returns singleton `0x260e4d1c`, whose constructor installs table `0x260c91a8`. Slot 0
`0x26001bd0 -> 0x260116b0 -> 0x2600f920` constructs an owned environment, table `0x260c9998`.
The environment constructs IVP state and retains object, active and deferred-delete collections.
`LevelInitPreEntity 0x1002d930` publishes the environment, configures timestep/gravity and event
handlers, builds world physics and constructs the serial-handle shadow registry.

Entity setters `VPhysicsSetObject 0x100a6490` and `VPhysicsSwapObject 0x100a68f0` only assign
the reference with warning/null-swap registry semantics. The body producer is
`VPhysicsInitShadow 0x100a6150`: require edict through `0x100a5ad0`, destroy/relink existing body,
reject solid-none, read absolute origin/angles, construct a bbox for solid 2 or a model body,
assign, configure shadow `(10000,10000,allowMove,allowRotate)`, submit `(origin,angles,false,0)`,
then register the entity. Bbox uses `0x100a5d80 -> 0x1002dfe0 -> Collision007 slot 19`;
model uses `0x1002e3a0`, engine model-collision slot 58 and parsed object/surface parameters.
Environment creation `0x260104f0 -> 0x260189f0` creates shape/IVP body and object table
`0x260c9db0`. An arbitrary existing actor is not this construction contract.

| Object operation | Table slot / body | State and timing |
|---|---|---|
| SetShadow | 45 / `0x26018520` | Create controller or change existing limits; adjust gravity/drag and core ownership |
| UpdateShadow | 46 / `0x260185b0` | Store bool/gravity mode and call controller target writer; no immediate pose copy |
| GetShadowPosition | 47 / `0x26018640` | Read live body pose at environment simulation time |
| GetShadowController | 48 / `0x260186d0` | Separate controller reference at object+0xc |
| RemoveShadowController | 51 / `0x26018600` | Environment destroys controller before body destruction |
| IsAsleep | 4 / `0x26016cf0` | Object inactivity byte +0x35 is nonzero; distinct from deletion +0x3d bit4 |

Environment slot 35 `0x26011360` allocates through `0x2601b0e0 -> 0x2601a670` and returns a
public interface at controller base+4 (table `0x260ca098`), separate from IVP controller table
`0x260ca0c8` at base. Interface slot 1 `0x2601aad0` writes target `(X,-Z,Y)*0.0254`, quaternion,
nonnegative arrival interval and active bit, and wakes a changed target. Slot 2 `0x2601ac40`
combines requested speed limits with actual core velocity. Slot 4 `0x2601aa40` stores teleport
distance in metres. NPC `0x10272f40` supplies `(size.x+size.y)*0.25`, not damping or squared
three-axis size; the current `BuildVPhysicsShadow` metadata does not implement that setting.

IVP callback `0x2601aa50` calls `0x2601a250` on simulation-event dt, counts arrival time down,
reads actual pose/velocity, applies teleport-threshold/collision arms and linear/angular velocity
correction via `0x26019600`. Core attachment/restoration `0x2601a7e0/0x2601a920` is part of
lifetime. Scheduler `0x2602d090` dispatches controller records backwards after priority sorting
(`0x2602ccd0`, shadow priority 500 at `0x2601a6f0`). Target storage alone is not this service.

`CPhysicsHook::FrameUpdatePostEntityThink 0x1002dd70 -> PhysFrame 0x1002ea40` owns publication:
validate/cap frametime, apply the physics-scale cvar, simulate environment slot 26, then visit
active bodies forwards (game data -> entity slot 226), shadow handles forwards (nonnull awake
object -> slot 229), and collision FrameUpdate after clearing the in-simulation flag. Environment
`0x26010900` owns its IVP clock and activity/deletion work. Generic `VPhysicsUpdate 0x100b4f30`
publishes actual position for movetype 7 and sends movetypes 1/8 to pusher feedback
`0x100a55b0`, which can rewind logical pose/localtime from solved pose/contact and report Blocked.
NPC slot 229 is inherited empty; NPC pose reconciliation occurs before STEP in `0x1003a610`,
with trace/think phases and target submission after logical movement. Current pre-think actor
sampling is not this consumer order.

`VPhysicsDestroyObject 0x100b5040` removes the shadow handle, forgets the saved body recipe through
`0x10158520`, asks environment slot 20 `0x26010f80` to destroy/queue the body, then clears the
entity reference. The owned-list entry disappears immediately; environment flags +0x61/+0x62
defer final deletion to `0x26011240`. The active list is repaired before the deleting destructor.
Object destructor `0x26016c10` destroys controller through environment slot 36 before the IVP
body; controller destruction restores core parameters and detaches scheduler state. Shutdown
`0x1002dc50` clears events/collision state, destroys environment and frees the shadow registry.

Collision007 factory `0x260099f0`/ctor `0x26009850` establishes singleton `0x260f0698`, table
`0x260c9550`. Slot 18 `0x2600aad0` adjusts this by +4 and enters `CollideGetAABB 0x26021db0`
(transformed support/ledge query and bounds expansion); slot 19 `0x2600a310` creates/caches bbox
collision. The body is available; complete geometry and object-parameter conversion are still
open. Exact clock limits/contact producers and damaged quaternion/parameter aliases also remain
to be walked before an engine adapter is a complete service. Unreal may replace solver algorithms
under AGENTS.md only with the retail contract/state/event sequence preserved. No such replacement
acceptance or object/controller runtime implementation is claimed by this recovery.

### Actual entity body identity and saved-recipe removal [decompiled/PE, 2026-10-11]

Vampire.dll100a6490 SetObject warns on nonnull-over-nonnull, even identical pointer, then assigns
on every arm. It performs no old-body destruction or shadow registration. Base233100a6ad0 tests
nonnull stored body and signed capacity >0; it writes only output[0] and returns1. Other arms
return0 without output writes. The real selected PhysProp producer now supplies the actual live
Chaos actor pointer rather than component/FBodyInstance storage, with reference retirement on
every existing native lifecycle path. Tests use imported real bodies and actual registered base
and prop operations. No fabricated presence word, target or historical address-uniqueness rule.

Fresh PE read1053c5e0 yields106bda64. Ctor10043a00 installs saved-physics manager table10449420
there. Helper10158520 calls manager#3 thunk1000ed68->10045110 before environment#20. It removes
the first matching body in its model-recipe map, returning immediately, otherwise searches/removes
the bbox-recipe map. Model create1002e3a0's success calls manager#2 thunk10012f12->10044db0 with
body/model, inserting the recipe association. This corrects the older claim that helper10158520
forgot collision-pair state. The observable is saved-body geometry reconstruction/association
lifetime; Source red-black trees and free lists are mechanics, not required substrate layout.
This packet implements neither recipe registration/save consumer nor generic destruction225.

Null Swap100a68f0 removes shadow membership first, warns when old body absent, then assigns;
generic225 removes shadow membership, saved recipe, requests deletion and clears reference.
Those complete chains remain open. Actual selected model props have no shadow membership, so
their native retirement does not invent registry state. Chaos owns native contact/body removal.
Solved226 remains open through absolute setters, trigger touch, child relink and pusher feedback;
no component-to-Origin shortcut or transform-cache candidate is accepted. Original runs remain23/88.

### Historical BBoxToCollide geometry construction [PE/execution, 2026-10-10; runtime removed]

`Collision007#19 0x2600a310` was implemented independently by
`FElysiumPhysicsCollision::BBoxToCollide`. Its real inputs were the two Source-space float
endpoints. It returns allocated, immutable compact collision geometry, with the full ledge,
packed triangle/edge features, point array, surface header and single-leaf bounds tree.
No caller-created geometry record, body pointer, solver object or fixture supplies these inputs.
The later entity wrapper/body creation remained absent; no production caller consumed the
compact representation. The owner then applied the vision ownership test: Unreal owns collision
geometry, broadphase/contact solving and rigid mechanics. A fresh reference inventory at c7035f35
confirmed only the reconstruction header/implementation/tests and their box golden depended on
it; those four files were removed. The facts/probe below remain retail oracle history. Packed
layout, feature IDs, support trees and IVP internals are not runtime prerequisites without a
shipped observer. No gameplay divergence, body adapter or query closure follows from removal.

The operation rejects **only** three equal endpoint components, looks backwards through the
exact-component cache (`0x2600a480`), bootstraps its seed on the first miss (`0x2600a1a0`),
clones its ledge and rewrites eight point XYZs (`0x2600a050`), builds the surface through slot4
`0x26009fd0 -> 0x2606d290`, then appends the borrowed pointer (`0x2600a3e0`). Signed zero
matches; NaN never matches. Plane and line boxes survive. Reversed endpoints are not sorted.
Even a first request equal to the seed endpoints creates the internal seed and a separate
returned clone, with the newest record winning the next lookup.

Seed endpoints are `(-16,-16,0)/(16,16,72)`. Corner `0x2600a140` ordinal bits1/2/4 choose
maxX/maxY/maxZ. The Qhull producer `0x26009b10 -> 0x2606c640 -> 0x2606bec0` uses
`qhull Qs Pp C-0 W1e-14 E1.0e-18` (retry QJ) before ledge builder `0x2606ba80`.
Read-only retail execution recovered the exact resulting permutation **7,2,6,3,5,4,0,1** and
all 12 packed triangle records, including opposite-edge offsets and triangle feature IDs.
The native constructor retains those bytes rather than introducing a six-face feature numbering.
XYZ becomes `(X,-Z,Y)*0.0254f`, each product stored as float; W remains zero.

The returned surface is 412 bytes: header48, ledge48, triangles64 (12*16), points256 (8*16),
leaf384 (28). Ledge point offset208, packed flags/size `0x1504`, triangle count12; header
size is high24 bits of +28, root offset384, zero dummies and magic `0x53505649` at +44.
The leaf has right offset0, ledge offset-336, float center/radius and three quantized extents.
All these bytes, including feature words and padding, are checked against the retrieved outputs.

Header construction preserves the retail mass/inertia algorithm, not textbook box inertia:
`0x2607fd80` accumulates signed volume and float first moments; its Z first addition remains
double through the second before rounding for the third. The apparent +2c alias in the decompile
is **not** a bug: `pop edi` at `0x2607fe76` shifts the following read to saved NormalX.
`0x260800f0` uses the positive-volume/area gate with promoted float `1e-9`; negative, tiny-gate,
degenerate and unordered cases take bbox-center/half-diagonal fallback. Otherwise directed-edge
integration `0x2607f9f0` supplies second moments; the per-axis low-volume arm substitutes1,
then the final inertia components are norms of pairs of second moments. Thus the tested tiny
nondegenerate box has inertia `sqrt(2)`, while the reversed odd-axis box takes bbox fallback.
`0x26031e60` ignores unordered point comparisons when accumulating bounds.

Radius/deviation `0x260320f0` and leaf construction `0x2606d810` feed byte quantization.
`0x2606f92a` keeps radius*.004 and division in x87 precision until `__ftol 0x260b4bc4`,
which truncates via **fistp qword**, then stores its low byte. A native rounded double would
turn 1/.004 into250 and change planar deviation/line extents. The implementation recovers the
product/division residual before truncation, and cancels the sole nonzero axis-aligned box
normal component before rounding tangent length. This is a native arithmetic implementation
of the retrieved box contract. It preserves the observed byte boundary; no tolerance is used
for the checked goldens. It is not a generic convex constructor or a claim of all-input x87 parity.

Ownership deliberately retains the retail defect. `DestroyCollide#7 0x2600a520` frees geometry
through aligned allocator `0x2602bf10` **without** removing its cache record. Service destructor
`0x26009930` frees only cache storage; shapes, including the bootstrap seed, remain allocated
until explicitly destroyed. A later equal bbox lookup can therefore return a dangling identity.
The native service has the same borrowed raw-pointer lifetime; tests compare stale identity
without dereferencing it. No safe-cache or automatic-shape-deletion modernization was added.

Provenance: installed `Bin/vphysics.dll` SHA256
`b8bbbf306b939055ce49548ace707cabc719783026d4044950b204de73d3ae10`;
the durable x86 probe is `research/tooling/vphysics_box_probe.cpp`, SHA256
`18db88f5fd9b105346ab5ca5db0928fab4fb53d5d8215dd47b6f1ba330e4c561`.
It loads the installed DLL read-only, calls `CreateInterface("VPhysicsCollision007")`, invokes
slot19 using the actual x86 `__thiscall` signature, and dumps `header[+28]>>8` bytes.
The public capture command is `uv run elysium research vphysics_box`, or
`uv run elysium research vphysics_box --coords -3 2 7 19 34 55` for the asymmetric case.
The wrapper loads configured roots, finds VS x86 tools, compiles with `/EHsc /W4`, then supplies
`ELYSIUM_VTMB_ROOT/Bin` and a fresh scratch output to the instrument. `--out` must stay under
`ELYSIUM_WORK_ROOT`; no outputs overwrite the corpus. No coordinates means seed endpoints.
The public wrapper's asymmetric capture independently reproduced SHA256
`95190e2b82033e775eccb846d7874c6c8228cb0968f852c8b82366b58f34f630` at
`E:/elysium-work/research/vphysics-box/20261010T232238.982357Z/box.bin`.
The original exploratory probe hardcoded only the DLL path; its preserved scratch source hash
is `a0d82711d1c487622de43b3b672d84045cead535965a1f1ff955c9589bd3db63`.
Captured seed SHA256
`8f58c8627e5802b3542da9bc69044ea1a98adc7a55c67b549dd94726db1f835a`.

Scratch evidence lives at `E:/elysium-work/codex/l0-body-shape-20261010/`.
Seven primary captures are seed, asymmetric `(-3,2,7)/(19,34,55)`, plane and line with equalX
and equalXY, reversedX, tiny `(0,0,0)/(1e-7,2e-7,3e-7)`, and minX NaN (x86 atof bits
`0x7fffffff`). Full-byte native comparisons also cover20 deterministic shifted nonuniform boxes
(`random.Random(26000310)`, each min uniform[-500,500], each extent uniform[.1,150]) and
plane/line/reversed arms for each axis of `(-3.25,2.5,7.75)/(19.125,34.625,55.875)`.
All **36/36** reconstructed 412-byte outputs matched the retail probe after the documented
arithmetic corrections. This is bounded constructor evidence, not downstream body/query parity.
The 16 primary/axis outputs were tracked full-byte goldens in
`Private/Tests/ElysiumPhysicsBoxGolden.inl`; that game-derived file and its construction/lifetime
tests were removed with the reconstruction. Captures remain in ignored scratch; the read-only
probe remains available for research, not a runtime dependency.

The recovered retail construction chain is environment `0x260104f0 -> 0x260189f0`: template `0x26018ba0`
uses real object parameters/surface material, shape `0x26009a00`, IVP construction
`0x260397c0 -> 0x2605ad10 -> 0x2602dcf0` (core `0x26030f20`) and wrapper
`0x260168e0 -> 0x26016930`. Broadphase/recheck `object#25 0x26016da0 -> 0x2602e320 ->
0x26048b00` refreshes actual candidates and core lookahead, filters static/same-core/disabled
arms, removes obsolete pairs in reverse, then inserts pairs and invokes listeners in reverse
candidate/listener order. Its live broadphase, owned pair hash and deletion/insertion callbacks
are oracle evidence for observable mutations and ordering. Chaos supplies backend geometry,
candidate/contact mechanics and solving; its adapter must expose the required body identity,
lifecycle/activity, authored parameters and ordered game callbacks. Reproducing the IVP core,
tree, packed IDs or scheduler is not required. An ignored-pair fixture cannot supply missing
production body/contact outputs. Next work follows the targeted L0 producer walk: body inputs/
lifecycle, query/contact rules, shadow target/solved feedback, postthink/NPC order, then bounds/cache.

### Independent trace selectors and actual single-player collision rules (2026-10-10)

All `0x100...` / `0x101...` / `0x103...` / `0x104...` addresses here are vampire.dll;
`0x260...` are vphysics.dll. Bodies were read through the corpus; missing tiny Ghidra boundaries
were read from the installed PE without writing the corpus or shared project.

`Physics_TraceHull` `0x10035030` calls **slot 189 GetDamageType** at vtable offset `0x2f4`.
The base body `0x100ad850` does only diagnostic scope push/pop and returns zero. It reads no
entity field. The port now provides that complete constant-zero body and hand-slot metadata;
this replaces a tallying generated stub, not a collision algorithm. A nonzero override selects
game-rules slot 76 at offset `0x130`, with start/end/mins/maxs/mask, entity, its collision group,
entity again, and trace output. It is not an entity custom-collision predicate or callback.
Single-player rules inherit slot 76 `0x10128fa0`: an ordinary centered Ray_t, Simple filter with
the passed entity/group, engine TraceRay, optional debug trace, then return 1. It does not set
the cylindrical flag. Neither that engine-backed function nor nonzero damage-type overrides
are completed by this packet.

The zero arm reads `m_CollisionGroup +0x368`, then slot 211 `GetSolidFlags`, initializes the
centered ray and tests bit `0x40`. Extents are `(maxs-mins)*0.5`, center is `(maxs+mins)*0.5`,
start is start+center, start-offset is -center. IsRay is `extent.LengthSquared() < 1e-6`
(`0x104492e0` is an f64 literal); IsSwept is `delta.LengthSquared() != 0`. Flags start clear;
the cylinder arm requires bit `0x40`, a non-ray, and `sqrt(extent.x²+extent.y²) > 1e-6` before
setting the cylinder byte. Thus the squared-length and radius comparisons have different
dimensions in retail. Current UE box traces do not implement that cylinder query contract.
`SetSolidFlags 0x100dc580` has partition/body tails, so a fixture setting `0x40` does not
establish its complete same-L0 production. Full trace/geometry/filter remains open.

The actual single-player rules object is established by `CWorld::Precache 0x1023c020`, which
assigns `DAT_1070ba0c = InstallGameRules 0x10352f70`. On the single-player byte arm it calls
ctor `0x10355740`, which installs **CHalfLife2 table `0x104a308c`**. Slot 77 is
`0x10356230`, whose asm tail-dispatches getter `0x1042c0a0`, then singleton `0x109eed4c`
(initialized by `0x1042c080` with CHLSharedGameRules table `0x10516c58`), then slot 0
`0x1042c0c0`. This differs from base CGameRules slot 77 `0x10128f80`, whose getter
`0x1042df50` selects separate singleton `0x109eff34` / table `0x10516ce4` / `0x1042df70`.
The HL function's extra arms therefore apply to the shipped single-player trace.

`CHLSharedGameRules::vfunc0 0x1042c0c0` remaps each group 6 to 3 **before signed sorting**.
In order it rejects 15/15, 13/13, larger-group 17 with smaller 3/16/17, then 18/18. It then
runs the inlined shared rule `0x1042df70`, which signed-sorts again and rejects either group
8, smaller group 1, then pair 4/4; otherwise it accepts. No rules-object field is read or
written by either predicate. Negative/unrecognized values are not clamped: 0/1 and -1/1
accept, 1/2 rejects. The complete stateless predicates are ported in `ElysiumCollisionRules`;
the existing `FElysiumNpcBase::RetailGameRulesShouldCollide` caller now invokes the actual SP
function instead of always accepting. No nonzero entity group producer was fabricated.

#### Smallest remaining filter producer frontier

`CBaseEntity::SetCollisionGroup 0x100aaa40` returns unchanged on an equal group; otherwise it
stores the group first and, only with a real `m_pPhysicsObject +0x36c`, calls object slot 25.
The object table `0x260c9db0` has slot 25 `0x26016da0`, whose PE body replaces this with
the real IVP object at wrapper+8 and tail-calls `0x2602e320`. With an IVP collision record at
object+0x9c it invokes `0x26048b00`: live broad-phase candidates, filter dispatch, existing
collision-pair comparison, obsolete-contact deletion and insertion callbacks. A group word
setter with a bool body-presence/recheck counter omits these effects and is not this producer.
SetCollisionGroup remains open until a real Chaos body/contact adapter propagates those
observable filter mutations and callbacks in order. The IVP broadphase itself is oracle
mechanism evidence, not a required implementation.

`CTraceFilterSimple::ShouldHitEntity 0x101d31c0` executes StandardFilterRules `0x101d3080`,
pass rules `0x101d2fc0`, **candidate slot 91**, candidate BCC targetable/script-hidden gate,
actual rules slot 77, then **candidate slot 68 before pass slot 68**, with short-circuiting.
Mask `0x46004003` skips only the last bilateral stage. Pass rules reject identical entity
pointers, candidate's owner equal to pass, and pass's owner equal to candidate; this is owner
slot 97, not move-parent/root resolution. The current shared character helper's tester-first
order belongs to the nav filter and remains a known divergence for Simple; base-line/player
slot-68 overrides and lower collision-pair state are unfinished, so the whole filter cannot
be claimed reproduced except for one line.

That lower state is concrete: `CBaseAnimating::ShouldIgnoreCollision 0x1008bfe0` returns
`0x10158b90`, which calls `DAT_10723948` slot 3. LevelInitPreEntity `0x1002d930` obtains
that service through VPhysics030 slot 3 `0x26001d00 -> 0x260116d0`: it allocates a 0x48-byte
pair-hash service, initializes tables, creates a 0x400-capacity hash through `0x26040340`,
then publishes table `0x260c9b7c`. Slot 3 `0x26011b70` asks embedded pair-hash table
`0x260c9b9c` slot 0 `0x2603b190` and inverts AL; that query canonicalizes the pair through
`0x2603b0e0` and queries `0x2603aff0`. Pair add/remove entry points `0x10158b40` /
`0x10158af0` and service slots 1/2 `0x26011780` / `0x26011920` also call `0x10158ab0`:
for each physics object whose slot 1 predicate is clear, invoke the same real object slot 25
recheck. Slot 1 `0x26016d60` tests bits `0x0c` on the word reached through wrapper+8 then
IVP object+0xa4; this is distinct from the inactivity predicate at object slot 4 `0x26016cf0`.
Identity lifetime, complete ignored-pair mutations and their body/contact notifications remain open;
Unreal containers/Chaos replace internal hash allocation and mechanical pair maintenance.
An always-empty ignored-pair set would stub same-L0 state, not provide an upward hook.

`Elysium.Substrate.CollisionRulesProduction` covers each predicate rejection/neighbor and
reversed arguments, 6 remapping, signed edge values, the existing wrapper, registered base/NPC/
player construction and virtual slot 189. Its real registered NPC/player call with special
mask `0x46004003` reaches the rules consumer before bypassing the unfinished bilateral tail.
`l0_physics_damage_type` calls the actual slot 189 on three live registered receivers; it
does not call an internal filter helper or invent a hull hit. No body, controller, partition,
cache, query or complete filter closure follows; no modernization was accepted.

## Independent physics surface material producer (2026-10-10)

Actual body construction `260104f0 ->260189f0` calls template initializer `26018ba0`.
Its material input is SurfaceProps001, not a UPhysicalMaterial: negative requested index first
looks up `default` through interface#3, then #10 returns the material row; the pointer is stored
at template+0x18. `26014be0` returns process singleton260f0f40, ctor26014c10 installs
table260c9c30. Interface#1 parser26015170 is outside a Ghidra function boundary; the read-only
PE disassembly, small lookup bodies and executable capture establish the following contract.

Parser filename registration26014df0 happens before reading any text; exact repeated filename
returns0 without further token/symbol writes. Filenames, row names and sound values share the
same symbol table. It initially interns the empty string as symbol0. Ctor260228e0 selects
case-sensitive byte comparator260227b0; insertion26022b40/260229d0 uses stable16-bit IDs.
Pair reader26024fd0 calls tokenizer26001850 and lowercases both key and value (C locale
__strlwr260c6620). Parsed entry names therefore become lowercase, while ordinary lookup#3
26014ed0 is case-sensitive. Unknown ordinary names return-1. Only reserved
`$MATERIAL_INDEX_SHADOW` uses strcmpi260150b0 and returns0xf000.

Tokenizer26001850 reads selector260e4d11 at26001873. It is zero-initialized BSS with one
reader and no writer, so tableB260e4c10 is live. Initializer26001970 builds tableA from
260cf158 (`{}()'`) and B from260cf150 (`{}()':`), verified directly in the PE. Signed-byte
whitespace, // and /* */ comments, raw quoted contents/escaped quote and delimiter order match
the already recovered `ElysiumKeyValues::NextToken` byte grammar; only the pair wrapper differs.
`}` consumes one token and clears the value. Both tokens otherwise consume in source order.
The same bounded end-of-buffer representation as that lexer applies to malformed unterminated
quotes; retail reading beyond its allocation is not treated as a valid source contract.

Each entry zeros its116-byte payload, interns its name, then copies the currently stored
default payload if present. Every key is applied immediately. `base`26015282 ->26015140 copies
the entire116-byte payload from an already stored row at that token position; invalid/forward
base has no effect. Later base overwrites earlier fields, including sound symbols. Every repeated
scalar/sound assignment overwrites its slot. A duplicate entry parses/interns its whole block,
but the first stored row wins26015917. Missing closing brace never inserts the unfinished row.
Unknown keys diagnose `Bad surfaceprop key %s (%s)\n` (260d021c), then continue.

First parser call, even empty input, appends the special shadow row once2601598c: copy current
default, override friction=.8/elasticity=0, retain uppercase interned constant name, save index
at service+168 and gate at+164. Later parser calls never refresh/reinsert it. Successful new
filename parse returns total stored count (#2 26014de0); repeated filename returns0 instead.

The physical row is0x84bytes: material prefix0x10 then surfacedata payload0x74. The row's
identity is the IVP material identity. Payload-relative fields (all stores include padding):

| Offset | Fields/type |
|---|---|
| 0,4,8,12,16 | float friction,elasticity,density,thickness,dampening |
| 20,24,28,32,36,40 | float audioreflectivity,audiohardnessfactor,audioroughnessfactor,scraperoughthreshold,impacthardthreshold,audiohardminvelocity |
| 44,46,48,50,52,54 | uint16 stepleft,stepright,impact,impactsoft,impacthard,scrape |
| 56 | untouched zero padding/slot |
| 58,60,62,64 | uint16 scrapesmooth,scraperough,bulletimpact,rolling |
| 66..101 | 18 uint16 sound symbols: metal,wood,blade,fist,bullet,claw × norm,soak,crit |
| 102..103 | zero padding |
| 104,108 | float maxspeedfactor,jumpfactor |
| 112,114,115 | uint16 gamematerial,uint8 climbable,zero padding |

Float conversion `_atof260b55f7` skips all six C whitespace bytes then __fltin260b99b4 ->
___strgtold12 260c0525. This is VC6 decimal/D-or-E exponent grammar, with no hex/inf/nan state;
the result is stored asfloat without clamping. `_atoi260b5532 ->_atol260b54a7` accepts signed
decimal and wraps its32-bit accumulator rather than strtol's overflow clamp. Climbable truncates
to byte; gamematerial is one non-digit character uppercased, otherwise atoi truncated to ushort.
The compiler's FSTP occurs before the caller removes the atof argument; payload offsets above
are derived from that actual stack position, not the apparent decompiler locals.

Lookup#10 260150e0 ->26014f60 preserves signed negatives as invalid/null. Any index>127 remaps
to row0 except0xf000, which selects the saved shadow index; normal table bounds then apply.
There is no auto-default for an unknown ordinary lookup. Name#7 26014f30 resolves the selected
row name, string#6 26015060 ->26022d10 resolves symbol0xffff to empty. Material table260c9d04
#1 26015b20 and#3 26015b30 read row+0x10/+0x14 raw friction/elasticity; #4 26015b60 reads
row+0x20 dampening; #2/#5 26015b40/26015b50 return constantdoublezero260c92d0.

`FElysiumPhysicsSurfaceProps` implements the authored parser/lookup input as typed float fields,
sound symbols, names and game flags. Retail packed rows and internal material-interface constants
are research facts, not runtime requirements under the owner-approved Unreal/Chaos boundary.
`FElysiumEntityWorld` loads the real deployed whole source through `LoadDeployed` before entities,
using the singleton exposed by `PhysicsSurfaces()`. Source bytes are copied to Latin-1-valued
characters only for tokenizer replay; names/values/symbols/rows are owned after input release.
Rows borrow the service vector and may relocate on a later parse; service teardown destroys its
rows/strings, while the process service survives world/level teardown. No external install/export
path is opened by the game. The dedicated GLB/import input and packaging path are documented in
`docs/contracts/seam_physics_surfaces.md`. Existing flattened material/audio pools, recursive
explicit-base resolution and Chaos clamps remain a documented separate consumer divergence.

Read-only executable research `uv run elysium research vphysics_surfaces --source <file>` loads
the retail DLL, calls actual factory/parser/#3/#10/#7, captures every116 payload byte and actual
material vtable friction/elasticity, then calls real template ctor26041d30/init26018ba0/dtor26041da0
and verifies template+0x18 is that same row. All64 shipped rows and12 bounded parser rows returned
identity1. Shipped sourceSHA2563219d190cfb3eadb65159a4f48373c5ac3bd5f999cb960262abc87a6e09c4570;
DLLSHA256b8bbbf306b939055ce49548ace707cabc719783026d4044950b204de73d3ae10.
Captures: `E:/elysium-work/research/vphysics-surfaces/20261010T234033.156388Z/surfaces.tsv`
(SHAf82172fbd53cd9314f4207ca947432f700419f099a80e6a9c2862dfb6d756de7) and
`E:/elysium-work/research/vphysics-surfaces/20261010T234436.375345Z/surfaces.tsv`
(SHAc9c69e4a168255e345c5dfcaf989067daa22b5458ccec3f5024d7f47412cf56d).

Current compiled public production tests `Elysium.Substrate.PhysicsSurface.Deployed/Parser` compare
13 float fields,28 sound names, gamematerial and climbable for64 deployed plus12 authored rows.
Shipped expected semantics are generated into ignored deployed artifacts by the public import;
tracked tests retain only project-authored boundary cases. Cases cover default timing, forward/invalid/late base,
duplicate rows/sound writes and symbol ordering, all sound matrix slots, signed/reserved lookups,
filename case/dedupe, empty first parse, shadow once-only lifetime, numeric grammar/wrap,
colon selector/comments/escaped quote and world recreation. Both passed in
`E:/elysium-work/reports/tests/20261010T235831.476345Z-elysium-substrate-physicssurface/index.json`
(2/2 Success, no errors/warnings). The earlier234509 full-byte gate preceded the owner correction
and is historical research evidence only.

Fresh native named semantic captures are
`E:/elysium-work/research/vphysics-surfaces/20261010T235435.508436Z/semantics.json` (12 authored)
and `E:/elysium-work/research/vphysics-surfaces/20261010T235546.180950Z/semantics.json` (64 shipped).
The independent pipeline decoder matches every semantic value/name in both:
`E:/elysium-work/research/vphysics-surfaces/semantic-comparison-20261010.json`.

Only the authored surface parser input is complete. Chaos material wiring and existing audible
consumers remain unwired/divergent. The revised frontier is entity callbacks and authored body
geometry/pose/mass/damping/inertia/material inputs -> Chaos identity/create/destroy/activity and
ordered physical timing, followed by contact filtering/notifications and shadow feedback.
Internal IVP shape/core/scheduler/packed topology is not a requirement absent an authored observer.
The known named observers and staged seams are in the producer walk. No original run or broad
entity/body/trace row closes, and the saved r022a frame/cache candidate stays deferred.

## Selected model body authored input adapter (2026-10-11)

This is a narrow production prop creation prerequisite, not closure of the environment,
controller, contact or publication chains. Vampire.dll1002e3a0 selects binary vcollide solid0;
1002e210 independently parses the first case-insensitive authored `solid` block, without using
its `index` to select geometry. Vphysics.dll260238f0 applies pairs in source order (duplicates
replace earlier values), including mass/inertia/damping/rotdamping/volume/drag/rollingdrag,
surfaceprop and optional authored masscenter. Callback10158260 copies1046ecb4 defaults:
mass1, inertia1, damping0.1, rotdamping0.1, rotinertialimit0.5, volume0, drag1 and no authored
masscenter. This masscenter is explicit authored data, not a requirement to reconstruct IVP's
computed surface center. Unrecognized keys are retained as evidence, not invented mechanics.

The actual model importer now cooks a separate selected first-solid UBodySetup into typed
UElysiumModelPhysicsInputs asset userdata. The shared static mesh still cooks all solids for
its other consumers. PhysProp's real prepared-model factory selects the dedicated body setup;
Chaos computes geometry and base mass properties. The adapter transfers authored mass,
linear/angular damping, inertia multiplier and optional COM, then obtains the process surface
service's typed friction/elasticity/density/thickness into the established physical material
class. Unknown or empty surface names use the surface service default. This does not replace
older flattened audible/static material consumers or claim pair/contact parity.

26018ba0 stores the authored inertia as isotropic moment multipliers (negative becomes1).
2602ea60's computed-inertia arm multiplies the backend moments by those parameters and mass.
Unreal's geometric InertiaTensorScale squares an isotropic scale at fixed mass, so the adapter
uses sqrt(authored multiplier). Actual native tensor tests verify a multiplier2 produces
ratio2 against the same Chaos base body. Neither IVP's internal base tensor nor its default
rotinertialimit operation is reconstructed. Native COM uses the authored converted location
and component scale; full recreation reapplies it because native mass updates can forget COM.

10191510 supplies spawnflags bit1 as start-asleep; missing source body writes SOLID_NONE and
MOVETYPE_NONE. Successful100a5ea0 writes solid6/flags0 before model creation, then movetype7
after body creation. Positive massbias multiplies source mass and clamps to[0.5,1000000];
positive override_mass wins last. These are real CPhysicsProp key/save fields, not inferred
model properties. The production adapter distinguishes retail source absence from missing
userdata/stale cook or unsupported Chaos input (diagnostic reimport/failure).

Real public-import witnesses are scenery/street/trashcan1/trashcan1_base (.phy, mass6kg in this
corpus) and scenery/structural/society/chair (no .phy, visual fallback geometry). Tracked tests
compare the actual cooked input rather than embedding shipped expected payloads. The no-body
production caller has no dynamic identity, solid0/movetype0, NoCollision and a real world trace
passes through its independently positioned visual. Native actor disposal is an implementation
check, not a claimed retail observer. Model replacement through SetModel, Kill, world teardown,
failed recreation and activity queries retire/observe native identity.

Remaining gaps: ordered SetObject/relink/touch/publication callbacks, simulation clock,
environment active enumeration/deferred removal, game-data/body flags and damageType==1,
bbox/dedicated static body callers, shadow controllers and r022 cache/query consumers.
Contact dampening is retained by the process surface service but has no Chaos response adapter.
Drag/rollingdrag/volume and rotinertialimit are retained, not mechanically implemented.
Configured damping/material values do not establish retail motion/contact feel equivalence;
restitution range, pair combine rules and contact dampening remain explicit adapter gaps.

### Saved body recipe observer and selected Chaos adapter [decompiled, 2026-10-11]

The observer of manager associations is Physics saving/reconstruction, not body allocation.
Model creator1002e3a0 admits only a nonnull environment#7 result, then manager#2
thunk10012f12->10044db0 associates body/model. Bbox1002dfe0 obtains collision#19
BBoxToCollide(min,max), and manager#0->10044c20 records bounds for a successful collide.
Custom creator1002e7e0 uses object parameters then manager#1->10044f60 associates successful
body/collide. Its absent-parameter default record is not inferred by the new native service.
Removal10158520 calls manager#3 thunk1000ed68->10045110 before environment#20: first matching
model body is removed and returns; otherwise first matching bbox body is removed. The collide
bounds map is not removed in this function. Semantic owned associations suffice; no Source
tree/hash/allocator layout is part of the port contract.

Physics handler vtable1044943c reaches PreSave10043c40, Save10043c70, header10043f70,
Restore100440c0 and PostRestore100447f0. Save writes each valid owner PhysObjectHeader and a
block for each saved object, passing the actual object to environment saving; registered lists
are cleared after each. Restore validates owner/header, calls ShouldSavePhysics, destroys the
current object when eligibility is false or count<1, and dispatches the one-object type1 arm
to100443e0. That arm rejects an existing object, rebuilds bbox using saved min/max or model
using saved model name/solid, and re-registers successful reconstruction. Missing geometry or
failed environment restore reports DevMsg. Other object types and multiobject environment
restoration remain outside this adapter. Base100aa680 returns true; false constant overrides
include Door100ee3e0, FuncBrush10114d90, FuncRotating100bf900, Button100c8740 and
Player1017be30. NPC101a6520 already returns false. FuncMoveLinear10115be0 has no concrete
port leaf; its independent caller remains open.

The named modernization is a typed Unreal archive in the existing Physics block instead of
the opaque vphysics blob. Actual prop_physics Spawn creates/registers its first-solid native
recipe only after successful Chaos output. Physics Save captures real native pose, velocity,
angular velocity in radians, activity/gravity and current mass/damping, with owned creation
inputs. Restore reconstructs the actual selected Chaos body, then preserves its saved native
state across final entity reconnect. Sleeping is not removal. All replacement/failure/removal
paths retire recipe before native release, including component destruction. Eligible source aliases are saved as independent fields and reconstructed independently, as
the type1 environment allocation requires. Missing supported recipes explicitly fence persistence.

Finite bbox native-service evidence uses explicit authored min/max/parameters and actual Chaos
box output, saved reconstruction and retirement. It is not a retail Spawn producer. The
remaining chain is VPhysicsInitShadow100a6150 -> InitSetup/225, pose217 and params219 ->
box helper100a5d80 model type/surface/default selection ->1002dfe0/1002e7e0 -> SetObject,
shadow caps/permissions/update registration. Static/shadow/default caller selection is open.
No visual CalcBounds input substitutes for saved authored bounds. Generic Swap/225, solved226
pose/touch/relink ordering, contact/controller/multiobject and fluid enter/exit/cache save state
remain open. Current damping capture covers the real water setter, not complete fluid restore.

Type1 aliases do not coalesce native bodies in this reconstruction arm. Environment table
260c9998#50=26025400 dispatches type1 through260d2ca0=26019450. Its read-only PE body
26019483-260194bc unconditionally allocates0x5c, calls ctor260168e0, writes the new pointer to
the destination field and restores26019070. Only afterward does26025400 register old/new through
26025280. Environment#49=26025240 initializes that reference map. This is downstream reference
fixup machinery, not a pre-allocation body reuse lookup. Thus the finite port saves every eligible
recipe-backed owner field, including aliases, and reconstructs independent actual bodies. Base
leaves own invisible service components for these outputs, with native collision profile/mask
preserved. SetObject clear/overwrite remain reference-only; base225/Kill/destructor retire only
the owned reconstructed producer, while original source aliases do not own their producer.
Full downstream pointer/controller fixups remain open. If any native reconstruction fails, earlier
successful fields in this selected Physics packet are retired and application fails; malformed or
truncated rows fail decode and propagate outer archive failure. Unsupported native recipes fence
saving explicitly. This does not close generic225 shadow/ownership semantics.

Saved absence uses100440c0's count<1 observer invoking owner225. Eligible null fields retain
a semantic count0 row; restore destroys baseline Spawn's body and skips reconstruction. Prop225
sets runtime bSimulating=false, so final GateBody does not resurrect it. Actual Destroy ->Freeze
->archive ->ApplySnapshot observes null field/no native actor after reconnect. Generic shadow225
remains unfinished.


## Reached static bbox and current-object lifetime [PE asm/decompiled, 2026-10-11]

`prop_dynamic::Spawn101905e0` tail10190680 dispatches223 `CreateVPhysics101907e0`;
solid nonzero calls `VPhysicsInitStatic100a5bb0`. Setup100a5ad0 first tests edict+2e0,
then225 at100a5b56, thenCALL1001514a at100a5b5e. This thunk resolves to
Relink100b3b00: its111 bytes only push/pop a scope trace. It has no bounds dirty,
touch, spatial query, gameplay callback or entity-state write. There is no omitted
partition subsystem dependency at this reached call.

Static100a5bb0 tests the **resolved moveparent handle**, not authored parentname presence.
A resolved parent delegates InitShadow(false,false,null); solid0 returns null;
unparented solid2 reads collision-property mins/maxs and absolute origin, calls
PhysModelCreateBox100a5d80 with static=true, thenSetObject even on null output.
Other solids call static model1002e580; that arm remains outside this packet.
Map parser resolves parents before DispatchSpawn (10136650 ->SetParent100a0670),
so an unresolved authored parent name legitimately reaches the unparented bbox arm.

Bounds producer is not visual CalcBounds or first-solid .phy geometry. Engine studio
loader200a3c90 copies MDL header dwords0x2d..0x32 (bytes180..203, hullMin/hullMax,
not view bounds204/216) into model_t+90..a4. CModelInfoServer#3=200b5920 copies those
six floats. UTIL_SetModel101cf4a0 ->UTIL_SetSize101cf3c0 ->SetCollisionBounds and213
produces live collision/size fields. Source inches become cm; Y reflection swaps min/max.
`CBaseProp::Spawn1018df70` dispatches105SetModel before the dynamic leaf's223InitStatic;
these size/bounds writes precede InitSetup225/Relink and the resolved-parent shadow branch.
Box100a5d80 starts surface=`flesh`; when model exists and modeltype3, studio accessor
10428eb0 supplies the raw string at MDL+392, including an empty string. 1002dfe0
registers explicit bounds; 1002e7e0 receives absolute origin and zero angles1070d9d0.

1002e7e0 asm distinguishes static argument6 from the surface argument despite folded
C aliasing. Null params copy48 bytes1046ecb4, override mass85 (42aa0000) and inertia
1e24 (6753c21c). Template semantic inputs: masscenter=null, linear/angular damping=.1,
rotinertialimit=.5, volume=0, drag=1, rollingdrag=0, collisions=true. Static mass/inertia
are retained creation inputs, not claimed native dynamic mass/tensor observations.
Raw surface lookup precedes default fallback26018ba0; native material receives typed
friction/elasticity/density. Full retained drag/rotinertialimit response remains open.

Swap100a68f0 removes shadow membership only when incoming=null, warns only when OLD
object=null, then assigns; it never destroys either producer. Generic225100b5040 null
exits, otherwise removes membership, calls10158520 (recipe10045110 before native deletion),
then clears the field. The pointed producer is deleted even when borrowed from another
entity. Bare SetObject clear/overwrite does not retire its previous producer. Registered
native model/bbox incarnations retire all live references and recipes before native release;
selected leaf225 must not also delete an unrelated former Visual producer.

Current narrow port reaches actual unparented prop_dynamic solid2 Spawn with authored MDL
inputs and a genuine Chaos static identity. Native shape filter updates preserve incarnation
through hide/unhide; logical visual rotation does not rotate the bbox. Persisted Physics
recipes carry static mode, actual native pose, response container, mask/profile, shape query
enabled state, gravity and damping. Reconnect preserves native pose/filter state differing
from logical Origin/shown state and eligible count0 absence. New saved response count/byte
and shape-enabled enum validation refuses malformed data before native setters.

This does not complete VPhysicsInitStatic's parent/static-model arms, full box helper's
nonstudio/default arms, explicit-params1002e7e0, game-data flags, pair/contact filtering,
shadow controller/submission, frame enumeration/deferred deletion, solved226/touch/child
publication, fluid/controller restore graphs or broad model migration. A reached resolved
parent currently reports the existing L0 shadow planning gap and creates no substitute static
body; this is an unclosed arm, not a named gameplay modernization. Semantic shadow handle
add/remove exists as a lifetime prerequisite, but no full InitShadow producer consumes add
and no fake membership setter witness claims closure. Chaos owns solving and native geometry;
configuration and query/lifetime witnesses do not establish retail dynamic equivalence.

Unreal5.8 EngineTypes.h1745/1762 defines64 response bytes and an unchecked channel lookup.
ECC_MAX also includes a deprecated pseudochannel with no container entry. Persistence uses
the actual container extent, never the enum's end; native sleeping-body evidence caught and
corrected an initial out-of-bounds capture. Swap's reference-only arms are covered, but its
membership removal has no complete production shadow witness and the whole function stays partial.

### Recovered shadow/frame prerequisite chain (2026-10-11; implementation open)

The first reached observer is a parented `prop_dynamic` solid2: InitStatic100a5bb0
delegates InitShadow100a6150(false,false,null). It requires a dynamic bbox body, not
a static replacement. InitShadow runs Setup/225, obtains absolute pose and collision
bounds, creates the body, assigns it, installs SetShadow10000/10000 permissions,
submits UpdateShadow(false,0), then registers serial shadow membership1002edd0.
232100a69e0 ignores movetype7/null body; otherwise it reads absolute angles then
origin and submits the supplied interval. Neither submission teleports native pose.

SetShadow26018520 creates a controller only once; later calls change speed limits
without changing its permissions. Movement forbidden installs mass1e6 and disables
gravity; rotation forbidden zeroes rotational response. Controller teardown restores
the saved body mass/inertia/gravity. UpdateShadow260185b0 changes gravity mode before
target submission; forbidden movement retains disabled gravity. Target2601aad0 stores
position/quaternion, clamps arrival at zero, activates the controller and wakes only
when target changes beyond its small position/quaternion comparisons.

Callback2601aa50 invokes2601a250 with actual native simulation delta. Arrival fraction
is one for nonpositive remaining, otherwise min(dt/remaining,1); remaining counts down
to zero. Linear26019600 removes velocity below squared1e-6, adds displacement times
fraction/dt, and damps the existing velocity with factor1, subject to separately stored
positive speed caps. SetMaxSpeed2601ac40 retains max(current speed,requested speed)
and requested-minus-that-maximum. Threshold2601aa40 converts inches to meters;
2601a250 compares strict distance-squared against threshold-squared, using target
distance before a predicted pose exists and prediction error afterward. Teleport
temporarily disables collision, restores it, and preserves the preteleport delta for
the velocity calculation. GetShadowPosition26018640 samples native pose at environment
simulation time; it never returns the submitted target. NPC10272f40 supplies
(sizeX+sizeY)*.25 inches as threshold, not a damping parameter.

PhysFrame1002ea40 rejects dt outside[0,1], caps .1, multiplies phys_timescale (default1,
flags0), caps .1 again, then simulates after entity thinks. Environment26010900 advances
only for dt strictly above .0001 and below1. Active objects receive226 in native order;
registered live shadows receive229 afterward, skipping deleted objects. Base229 is
empty, so this parented bbox observer does not require generic226 solved publication.
Collision frame callbacks follow both loops. Unreal's native scene owns geometry,
contacts and solving; its gameplay-before-StartPhysics and PostPhysics ticks supply
the production submit/solve/sample ordering, rather than a second IVP scheduler.

Required parent producer remains open: PhysicsParent10039ac0 runs think0 and interval
clipping, then linear10038710 and/or rotation transactions. FinishPushers10036b10 runs
in reverse pusher order: TouchTriggers(saved origin), then232(0). FinishPush10036d40
does that before reverse pushed-entity touch/submission/impact work. RelinkChildren
100342c0 processes each live child's solid/trigger touch,232(frame interval) unless
movetype7, descendants, then rereads MovePeer after callbacks. TouchTriggers100a53d0
hands engine slot122 the edict and optional old origin; its recovered native-query
consumer and begin-before-deferred-end contract are in entity_io.md. Setter submission
would bypass this observable callback order. Existing absolute/native/logical poses
must remain distinct through this chain, deletion and saved Physics reconstruction.

This is recovery evidence and dependency order, not a port-status closure. Explicit
parameter/nonbbox creator arms, NPC movement, other movetypes, generic226, collision
frame notification/filtering and fluid/controller pointer graphs remain open.

The reached child has MOVETYPE_NONE. PhysicsSimulate10040390 dispatches it to
PhysicsNone, so the successful parent transaction has only FinishPushers' reverse
captured hierarchy TouchTriggers(old origin),232(0). RelinkChildren100342c0 belongs
to generic226's other arms; appending it here would replace the observable zero
arrival with a second frame interval. A refused native movement transaction restores
the parent pose and publishes neither child touch nor a target. Full speculative
pushed-object rollback/contact and the broad parent dispatcher remain unfinished.

SetMaxSpeed2601ac40 receives the secondary controller interface at allocation+4;
callback2601aa50 passes allocation+8 as state. Consequently the retained maximum
is the damping cap, while requested-minus-maximum is the desired cap. Nonpositive
desired caps do not clamp. GetShadowPosition26018640 literally returns1 after optional
native-pose reads. Teleport prediction2601a3f8..2601a43a uses the retained preteleport
current position plus the computed velocity times native dt.

Postthink order is explicit, not inferred from auto-registration: server initializer
1011a0c0 obtains PhysicsHook through1002d750 (106b88ec) and appends it via1042c310
before1042c4e0 appends the auto-system list. EntityListSystem100f8f80 prepends itself
to that auto-list with1042c260. Forward step4 dispatch1042c700/slot7 therefore runs
PhysFrame before EntityListSystem's deferred untouch; EventQueue follows both.

Native touch discovery is not trigger activation. PhysicsMarkEntityAsTouched1003dc70
admits self/parent/flag/solid gates and creates physical links before StartTouch.
Trigger101c5460/101c53d0/101c5420 reads +4c m_edtDerivedType: pushables bit8 or
SOLID_BSP with movetype8; physics movetype7 or bit0x2000. InitShadow writes neither
derived bit. The selected prop_dynamic movetype0 bbox is therefore a genuine native
geometric candidate with physical links, but cannot activate ALLOW_PHYSICS. EndTouch
101c55d0 repeats PassesTriggerFilters independently; no per-pair activation latch exists.

Implementation checkpoint: first arm build failed on the component-query constructor
(20261011T023126.845373Z-build.json); the second found a local shadowing error and
test-only solver template/origin lvalue errors (20261011T024024.690394Z-build.json).
The next arm build passed (20261011T024112.908599Z-build.json). The first actual map
frame test verified native scaled/zero/negative time and positive arrival displacement,
then failed its saved-controller assertion (20261011T024153.786488Z-test.json).
At that checkpoint the packet remained under implementation and verification; these were not
closure gates.

Implementation checkpoint after recovery: the resolved-parent studio bbox observer now reaches
actual native creation, controller installation, owner membership and target submission. Its
successful existing native mover transaction uses FinishPushers' reverse captured child touch/
232(0), then actual production postthink scene solve and native result; generic RelinkChildren
is not additionally reached for childmove0. Refusal publishes no child target. Collision-frame
notification, broad active226, full pusher/contact and other creator/NPC arms remain open.

Native controller tests distinguish linear velocity from actor-origin displacement under an
angular target. Interim030049/030213 threshold assertion
had nativevelocity999, predictionX3043.062, targetX3133.062, solvedX3139.453 and Y/Z motion;
it selected teleport but combined angular motion with a linear displacement assertion.
Offset-hull rotation about Chaos's center of mass is an inference from these native outputs,
not a recovered retail fact or a proven gameplay divergence.
The final witness holds current native rotation for the threshold arm, observes target+10 and
retained preteleport prediction, and separately verifies full Source angular submission/solve.
Actual native scene dt, restored collision state, saved target/countdown/original mechanics and
first scheduled restore tick pass in final030355. See L0-entity/progress.md for authoritative JSON.
These narrow production witnesses do not close full retail functions or the L0 closing gate.

### Native dynamic publication recovery (2026-10-11)

Fresh `PhysFrame1002ea40` and vphysics.dll walks establish creation-owned game data, a frozen
native object snapshot and live per-entry game-data resolution for slot226. Object4 `26016cf0`
is inactivity, not deletion. Wake `2600fbb0` appends an absent nonstatic body, or cancels pending
sleep while retaining its position. Sleep `2600fc70` marks it pending. Simulation `26010900`
removes pending entries backwards before its `.0001 < dt < 1` solve gate; removal `26010c20`
replaces the removed slot with the last entry. A final native sleeping output therefore receives
226 once, then disappears on the next simulation. Awake226 still runs for zero/rejected dt.
The following fresh shadow roster resolves each current object and admits229 only when object4
is false. The subsequent collision FrameUpdate remains outside this packet's closure.

Generic226 `100b4f30` move7 reads object37 `26017eb0`, then216 origin,218 angles, physical touch
and child relink `100342c0`. Move0 is empty; moves1/8 pusher feedback `100a55b0` remains partial.
The x/y/x angle finite diagnostic warns and continues. Object37 uses `26001310`, whose double
`.001` threshold and atan2+180 roll differ from entity MatrixAngles `10137ed0` (degenerate roll0).
Independent IVP-to-Source bridge `260012a0` maps rows `(r0,r2,-r1)`, `(r8,r10,-r9)`,
`(-r4,-r6,r5)`; native reflected basis conversion retains these branches without UE Euler
canonicalization. Raw stored parent frames from `100b2150` compose through `100b1ac0`; absolute
writers216/218 publish raw fields before inverse-parent local writes. Actual native creation
enables separate local/absolute cache words so later host prethink cannot teleport that body.

The native adapter uses Chaos EndPhysics dirty output and native wake/sleep events for activity,
with semantic wake append/pending cancellation/backwards last-entry removal and actor incarnation
validation. Creation ownership survives current-field null/aliases. Physical query `100a53d0`
uses actual edict geometry; trigger `101c5460/101c5420` admits reached movetype7 under spawnflag8
after pair links. Child232 `100a69e0` reads angles before origin and submits object46(false,dt).
Deferred untouch and EventQueue follow existing postphysics sequencing. Detailed recovery,
native witnesses and explicit unrecovered frontier are in
`docs/specs/layers/L0-entity/walks/L0-native-publication.md`. Original23/88 remains open.
