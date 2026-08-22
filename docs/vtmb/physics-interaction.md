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
velocity zeroed and think cleared. Only the static-corpse branches (`No_Ragdoll_Death`, the player,
the burning path) call `SpawnStaticCorpse` and get a second entity.

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
| model carries no ragdoll collide | `BecomeClientRagdoll` returns false; the NPC animates `SCHED_DIE` instead |
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
