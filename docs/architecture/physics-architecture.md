# Physics — Unreal reproduction

VtMB's rigid-body facts are `docs/vtmb/phy_vphysics.md` (the `.phy` container and the ragdoll rig)
and `docs/vtmb/physics-interaction.md` (what simulates, the physics hands, where ragdolls appear).
This document is the Unreal side: one owner for everything that touches a simulated body, so the
prop, the corpse, the carried chair and the explosion kick are one system rather than four.

---

## 1. Owner call

**VtMB owns the rules; Chaos owns the solve.**

Every number a player can observe is authored VtMB data or a recovered VtMB constant: a prop's
`totalmass`, a joint's per-axis limit, the grab's reach and cone, the throw force, the death
impulse. Nothing of Source's machinery is reproduced — not IVP, not the shadow controller, not
`CGrabController`'s convergence terms, not `CWeaponPhysCannon`. Porting any of those is the
mechanism port the Ownership test in `docs/project/remaster-direction.md` forbids; reproducing
their *numbers* is faithful work.

Two corollaries that decide most arguments:

- **A rule the file already states is a bake input, not a runtime dependency.** A ragdoll's joint
  limits, bone parenting and per-body mass are all in the `.phy`; they are resolved once, offline,
  into a `UPhysicsAsset`. This is the same contract as `CLAUDE.md` → "Poses are baked native".
- **No A/B toggle.** `elysium.PhysicsProps` already exists and stays; nothing here adds another.

---

## 2. What already exists — do not rebuild

| Piece | Where | What it does |
|---|---|---|
| `.phy` decoder | `pipeline/src/elysium_pipeline/formats/phy.py` | ledge tree → convex hulls (Euler-checked) + authored `totalmass` |
| `props/<stem>.phys` | `UE_extract_corpus.py` | one sidecar per `prop_physics` model |
| Prop collision bake | `bake_map.py` | one `FKConvexElem` per ledge, `MaxConvexHullsPerMesh = 1`, simplification off — the hull is reproduced exactly |
| `FElysiumPhysProp` | `Substrate/ElysiumPhysProp.{h,cpp}` | the `prop_physics` leaf: Chaos rigid body, authored mass re-applied to the component, dormancy gating, the RE'd `Wake`/`Break`/skin input surface |
| `phys_hinge` | `.ents` `hinge_axis` + `UPhysicsConstraintComponent` | the one constraint family that lands today |
| `StartBodyRagdoll` | `IElysiumEmbodiment`, `UElysiumEntityBodies` | hands a killed body to Chaos from its current pose; returns false and warns once because no baked mesh carries a `UPhysicsAsset` |
| `HoldBodyFinalPose` | same | the recorded stand-in the death handoff falls back to |
| The `+use` seam | `ElysiumInteraction.h`, `FElysiumEntityWorld::UpdatePlayerInteraction`, `IElysiumEmbodiment::QueryPlayerUse` | candidate query, exact/assisted tiers, `EElysiumUseSessionKind::WhileHeld`, `BeginPlayerUse`/`EndPlayerUse` |
| The use icons | `ElysiumUseIconName` + `ElysiumUI::UseIconArt` → the `hud/context_icons/` `T_` assets (R6.6) | the 72-entry enum — **slot 9 is `PhysicsHand`** and slot 1 is `CarryBody` |
| Impulse targets | `FBodyInstance::AddImpulse` / `AddRadialImpulse` | named by `docs/architecture/effects-architecture.md` for `env_physimpact` / `env_physexplosion`, which are stubs today |
| Surface materials | `docs/vtmb/surface_properties.md` | `surfaceprop` → physical material, the ragdoll solids' `flesh` included |

The gaps are narrow and named in §7.

---

## 3. The four layers

```
L0  authored data        .phy  ──► props/<stem>.phys        (exists)
    (offline)                  └─► .eskm  RAGD chunk        (new)

L1  baked assets         .phys ──► UStaticMesh simple collision + mass   (exists)
    (offline)            RAGD  ──► UPhysicsAsset on the baked USkeletalMesh  (new)

L2  runtime bodies       FElysiumPhysProp          prop_physics       (exists)
    (substrate)          FElysiumPhysBox           func_physbox       (new)
                         the phys_* constraint family                 (partial)
                         the character ragdoll                        (starved)

L3  producers            the physics hands  ·  the death impulse
    (who pushes)         env_physimpact / env_physexplosion
                         bullet and explosion pushes  ·  NPC throwers
```

L0 and L1 are the "resolve it offline" half; L2 is one leaf per VtMB class; L3 is a **single
vocabulary** on the embodiment seam that every producer speaks. The reason to draw it this way is
that the four discoveries this design comes from — the ragdoll rig, the corpse handoff, the grab,
and the placed physics surface — all bottom out in the same two things: a `.phy` and a Chaos body.

---

## 4. L0 — the export contract

`phy.py` gains a second product from the same parse. It does **not** grow a second decoder.

**Characters get a `RAGD` chunk in their `.eskm`**, not a loose sidecar. The rig is per-model
skeletal data addressed by bone name, and the container already carries exactly that kind of
payload (`SKEL`, `ATCH`, `DYNM`, `BDYN`, `MASK`, `MORF`). A loose file would have to be re-joined to
the skeleton the bake is already holding.

```
RAGD
  u32 solidCount, u32 constraintCount
  per solid:       bone name, parent bone name (empty = root),
                   origin (Unreal cm), rotation (quaternion, Unreal space),
                   mass kg, damping, rotdamping, inertia, massbias,
                   surfaceprop, hull index range into the solid's convex hulls
  per constraint:  parentSolid, childSolid,
                   axis[3] { min, max, friction }   degrees, authored order preserved
  hulls:           the same convex hulls props already get, per solid
```

Three rules the writer owes, all of them the `UE_` convention (`pipeline/CLAUDE.md`):

- Bone names go through the **same folding the bank baseline uses**, so a `RAGD` name matches the
  baked skeleton's bone exactly. A name that does not resolve is a hard export error, never a
  dropped body — the client's own failure mode is a named warning, and ours must be louder.
- Hull vertices use the settled `(x, -z, -y) × 100` mapping (`docs/vtmb/phy_vphysics.md`).
- **The solid transform frame is an open calibration.** `origin`/`angles` are Source units and Euler
  degrees, a different frame from the hulls'. Settle it the way `(x, -z, -y)` was settled: score the
  candidate permutations against each model's own bind-pose bone transforms across all 289 canonical
  rigs and take the winner by margin. Until it is settled, the export fails rather than guesses.

Props keep `props/<stem>.phys` unchanged.

---

## 5. L1 — the bake

`bake_characters.py` gains one stage, after the mesh and before the clips: build a `UPhysicsAsset`
from `RAGD` and assign it to the baked `USkeletalMesh`.

**Bodies.** One `USkeletalBodySetup` per solid:

| From | To |
|---|---|
| bone name | `BoneName` |
| the solid's convex hulls | `AggGeom.ConvexElems`, one element per ledge — the same exactness rule the prop bake proved |
| `mass` | `DefaultInstance.bOverrideMass` + `MassInKg` |
| `damping` / `rotdamping` | `DefaultInstance.LinearDamping` / `AngularDamping` |
| `surfaceprop` | `PhysMaterialOverride` through the existing surface-property table |
| — | `PhysicsType = PhysType_Default`, `CollisionReponse` default |

`massbias` has no direct Unreal counterpart and is sparse in the corpus; carry it in the chunk and
leave it unconsumed until a body demonstrably needs it, rather than inventing a mapping.

**Constraints.** One `UPhysicsConstraintTemplate` per `ragdollconstraint`, linear motion locked on
all three axes, angular limited. The mapping has two real problems and both are decided here:

- **Asymmetry.** VtMB authors `min`/`max` independently (`-25 … +20`); Unreal's swing and twist
  limits are symmetric about the constraint frame. The resolution is standard and exact: set the
  limit to **half the range** and rotate the child frame by the **midpoint**, so
  `xmin -25 / xmax 20` becomes a ±22.5° limit on a frame biased −2.5°. Doing anything else — taking
  the max, or the larger magnitude — changes reachable poses.
- **Axis identity.** Which of VtMB's x/y/z is Unreal's twist versus swing1 versus swing2 is not
  stated by the data and is the second calibration. Score it the same way, against the same 289
  rigs, by comparing settled poses under gravity; a wrong assignment shows up as knees that bend
  sideways, which is cheap to detect and cheap to score.

A joint whose six limits are all `0` with zero friction is a **weld** — lock all angular axes rather
than authoring a zero-width limit.

`friction` becomes constraint angular damping. It is uniform on the humanoid rigs, so one tuned
value reproduces the whole canonical set; record the tuning beside the number it approximates.

Finish with `UpdateBodySetupIndexMap()` + `UpdateBoundsBodiesArray()` and
`SkeletalMesh->SetPhysicsAsset()`, and let the existing bake cache key the asset off the `RAGD`
bytes like every other product.

---

## 6. L2 — the runtime bodies

Four leaves, one shape: the substrate decides, the embodiment builds.

**`FElysiumPhysProp`** is unchanged and is the template the others follow.

**`FElysiumPhysBox` (`func_physbox`, 179 placements)** is `FElysiumPhysProp`'s brush twin: the
entity's own `.ents` convex `hulls` become the body instead of a model's `.phys`, and the
break/damage surface is `func_breakable`'s. It is a new leaf, not a flag on the prop.

**The `phys_*` constraint family** (`phys_ballsocket` 84, `phys_constraint` 78, `phys_convert` 56,
`phys_hinge` 43, `phys_thruster` 3, `phys_constraintsystem` 1, `phys_animlink` 1) all resolve to
`UPhysicsConstraintComponent` between two named entities' `GetAttachBody()` — the accessor
`FElysiumPhysProp` already exposes for exactly this. `phys_convert` is the odd one: it turns a
non-physics entity into a simulating one on an input, so it is a transaction on the target's leaf
rather than a constraint.

**The character ragdoll** needs no new runtime code. `StartBodyRagdoll` already sets the `Ragdoll`
collision profile, calls `SetSimulatePhysics` from the current pose and reports refusal; once §5
ships an asset it simply stops returning false, and `HoldBodyFinalPose` becomes what it was always
described as — the fallback for a body with no rig.

Two behavioural calls fall out of `docs/vtmb/physics-interaction.md` §2 and belong to the substrate,
not the embodiment:

- **The impulse.** Retail seeds the ragdoll with the killing blow's force envelope, fully recovered
  in `docs/vtmb/combat-and-damage.md`. The handoff applies it at the hit bone (or `Bip01 Spine2`
  when the hit bone is unknown), converted per §8. This retires the "no impulse" stand-in recorded
  in `plans/animation.md`.
- **The interaction volume.** Retail leaves the corpse's use/feed/loot volume at the **death
  origin** while the drawn body slides away. That is a reproducible fact, and reproducing it means
  the use anchor stays where it was rather than following the pelvis. Following the ragdoll is a
  Feel divergence and needs an explicit owner call before it is written.

---

## 7. L3 — one seam for everything that pushes

`IElysiumEmbodiment` (`ElysiumWorldServices.h`) gains a small, closed physics vocabulary. Geometry
and solving only — every eligibility question stays in the substrate, exactly as
`QueryPlayerUse` and the B6 feed query already do.

```cpp
// --- Impulse -------------------------------------------------------------------------------
// Chaos owns the solve. Magnitudes arrive already converted to Unreal units (§8).
virtual bool  AddBodyImpulse(const FElysiumEntityHandle& Body,
                             const FVector& Impulse, const FVector& AtLocation,
                             FName BoneName = NAME_None) { return false; }
virtual int32 AddRadialImpulse(const FVector& Origin, float RadiusCm,
                               float Strength, bool bVelocityChange) { return 0; }

// --- Carry ---------------------------------------------------------------------------------
// A separate query from QueryPlayerUse, for the same reason the feed search is separate: retail
// runs its own shape (a short ray, then a hull, then a cone) with its own mask, and consults it
// BEFORE the ordinary use trace. Geometry only.
virtual FElysiumUseQueryResult QueryPhysicsGrab(float ReachCm, float ConeCos,
                                                float HullExtentCm) const { return {}; }
virtual bool  BeginBodyCarry(const FElysiumEntityHandle& Body,
                             const FElysiumCarryParams& Params) { return false; }
virtual void  UpdateBodyCarry(const FVector& TargetLocation, const FRotator& TargetRotation) {}
virtual void  EndBodyCarry(const FVector& ReleaseImpulse) {}
```

`BeginBodyCarry`/`UpdateBodyCarry`/`EndBodyCarry` are implemented with a
**`UPhysicsHandleComponent`** on the pawn. That component is Unreal's own PD constraint to a target
transform — the same idea as Source's shadow controller and the right level to reproduce at.
`FElysiumCarryParams` carries what the substrate decided: hold distance, preferred carry angles,
and the mass / angular-damping overrides to apply while held and restore on release (retail saves
both in `m_savedMass[32]` / `m_savedRotDamping[32]`).

Consumers, all through this one seam:

| Producer | Call |
|---|---|
| the physics hands | `QueryPhysicsGrab` → `BeginBodyCarry` → `EndBodyCarry(throw)` |
| the death handoff | `StartBodyRagdoll` then `AddBodyImpulse` at the hit bone |
| `env_physimpact` | `AddBodyImpulse` |
| `env_physexplosion` | `AddRadialImpulse` |
| bullet / explosion pushes on corpses | `AddRadialImpulse` |
| NPC throwers (Ming Xiao, Tzimisce, Hengeyokai) | their own schedules; the release is `AddBodyImpulse` |

`docs/architecture/effects-architecture.md` keeps the *presentation* of an explosion and points
here for the impulse.

---

## 8. The physics hands, concretely

**One player-side service, not a weapon entity.**

`weapon_physcannon` stays what the data says it is: an authored `vdata/items` record named *Hands*,
hidden, weightless, undroppable, drawing `w_null.mdl`. Inventory and the criminal-law check read it
by name (`docs/vtmb/physics-interaction.md` §4), so the record must exist and must be granted. The
**grab state does not live on it** — that would be reproducing Source's need to hang a controller
off a `CBaseCombatWeapon`.

```
FElysiumPhysicsHands            Player/ElysiumPhysicsHands.{h,cpp}
    owned by FElysiumEntityWorld, one per player, saved with the player
    ├─ TryAcquire(Now)          runs inside UpdatePlayerInteraction, BEFORE the ordinary
    │                           focus query — retail's order is: open session, then the
    │                           grab candidate, then FindUseEntity
    ├─ Tick(Now)                UpdateBodyCarry toward eye + hold distance
    └─ Release(EReleaseKind)    Drop | Throw
```

**Eligibility stays in the substrate**, as a predicate on the leaf:

```cpp
bool FElysiumPhysProp::CanPlayerCarry(const FElysiumCarryQuery& Q) const;
```

reproducing the recovered rules — the body simulates, it is not the player's ground entity, its
summed mass is under the limit, every bounding-box axis is under the size limit — plus the player-
side water gate (`m_nWaterLevel < 2`). The two limit constants are open
(`docs/vtmb/physics-interaction.md` §7); until they are read, use `physcannon_maxmass` for mass and
**fail loudly rather than guessing a size limit** — a guessed constant that silently admits a wrong
object is exactly the quiet-default the runtime-failure rule forbids.

**The session is the existing `+use` one.** `CanPlayerFocus` admits the prop when the hands would
take it; `BeginPlayerUse` returns `FElysiumUseBeginResult::Started(EElysiumUseSessionKind::WhileHeld)`;
`EndPlayerUse(..., Released)` releases or throws. Nothing new is invented for hold-to-carry, because
`WhileHeld` was built for it.

**The cursor is already imported.** `use_icon` slot **9** is `PhysicsHand` and slot **1** is
`CarryBody` (`hud/context_icons/physicshand` and `carrybody`, the texture lane's `T_` assets). The hands publish the icon into `FElysiumInteractionView` when a candidate is eligible,
so the HUD needs no new art path — only a producer for an icon that today only entities publish.

---

## 9. Units

VtMB is Source: **1 unit = 2.54 cm** (`bsp.INCH_TO_CM`). Convert once, at the boundary, and never
in the frame path.

| Quantity | VtMB | Unreal |
|---|---|---|
| `physcannon_tracelength` 80 | units | **203.2 cm** |
| the hull-trace extent ±4 | units | **±10.16 cm** |
| `physcannon_maxmass` 250 | kg | kg — no conversion |
| `physcannon_cone` 0.97 | cos of the half-angle | identical — dimensionless |
| joint limits | degrees | degrees |
| forces (`player_throwforce` 1000, `physcannon_*force`, the 50000 death clamp) | Source impulse, `kg·in/s` | `× 2.54` → `kg·cm/s` for `AddImpulse` |

The force row is **derived from the unit convention, not measured.** It is the one number in this
document that a live comparison should confirm before it is treated as settled; a thrown can that
travels visibly wrong is the cheap test.

---

## 10. Deliberately not reproduced

| VtMB thing | Why |
|---|---|
| `CGrabController`'s shadow controller and its convergence terms | mechanism; `UPhysicsHandleComponent` is Unreal's equivalent |
| `CWeaponPhysCannon` as a weapon class | the grab is not a weapon; only the item record is data |
| `player_pickup` / `CPlayerPickupController` | dead code in VtMB — `PlayerPickupObject` has zero callers |
| `CHL2_Player` | unreached HL2 leftover |
| client-versus-server ragdoll split | a networking artefact of Source; we simulate once |
| the physcannon's mega/ball/charge path | HL2 gravity-gun behaviour VtMB grants no way to reach |
| IVP ledge trees at runtime | the hulls are baked; nothing parses a ledge tree in the game |

`physics_prop_ragdoll`, `prop_ragdoll_attached` and `prop_ragdoll_special` register in VtMB and are
placed **nowhere**. They are not implemented; if a map is ever found that places one, that is a new
task, not a gap in this one.

---

## 11. Landing order

Three slices, each independently provable, in dependency order.

1. **The ragdoll rig** — `RAGD` export (with both calibrations settled), the `UPhysicsAsset` bake,
   the death impulse. `StartBodyRagdoll` stops returning false and the LIFE5 stand-in retires.
2. **The hands** — `QueryPhysicsGrab`, the carry seam, `FElysiumPhysicsHands`, the `PhysicsHand`
   cursor. Acceptance is the tutorial: pick up an office chair, carry it, throw a sardine can.
3. **The rest of the physics world** — `func_physbox`, the `phys_*` constraint family, and the
   `env_physimpact` / `env_physexplosion` impulses through the L3 seam.

Slice 1 is the only one with an unresolved RE dependency (the two frame calibrations). Slice 2's
open constants are named and each has a stated failure mode rather than a guess. Slice 3 is
mechanical once the seam from slice 2 exists.

Status and sequencing live in `docs/project/roadmap.md`; the open specifications live in
`docs/project/plans/gameplay.md`.
