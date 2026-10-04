# Packets S7 — the retail settling read, seventh sitting (2026-10-04)

Read-only on source and on `Arena/`. Four unknowns under `../v5a/brief-V5a-3-slot-562.md`. Every
claim carries its address. **(L)** = read from the listing this session, verified; **(P)** = read in
the port this session; **(doc)** = already in `docs/vtmb/`, cited; **(I)** = inferred, with the
reason. `vampire.dll` throughout.

## 1. Slot 389 `Weapon_ShootPosition 0x103338c0` — settled (L)

`(Vector* out, const Vector& origin)`, `RET 8`, one body for the whole Troika line except
`CNPC_VTzimisce 0x103bfd80`.

- **Attachment arm** (`0x1033392c..0x1033394d`): `GetActiveWeapon 0x10007e19`; a weapon →
  `weapon->GetAttachment01("muzzleflash" 0x105cba6c, &pos, &angles)` (`0x10007680`, by **name**, on
  the **weapon** entity, not on the owner). True → `out = pos` (`0x10333a33`); the angles are
  discarded. `origin` is not read on this arm.
- **Hacked-gun arm** (no weapon, or the model has no such attachment; `0x10333953..0x10333a2f`):
  `AngleVectors 0x10002310 (→ 0x10139610)(this->slot 221 (+0x374)(), &forward, &right, &up)`.
  Slot 221 is `CBaseEntity::GetAngles 0x100b3110` = `&m_angRotation (+0x428)`, the **local body
  angles — not the eye angles** (no eye-position fallback exists in this body). Then, per component,
  in this float order:

  `out = ((origin + forward · m_HackedGunPos.y) + right · m_HackedGunPos.x) + up · m_HackedGunPos.z`

  (`+0x157c` × forward `0x103339b9..d9`, `+0x1578` × right `0x10333999..b5`, `+0x1580` × up
  `0x10333972..95`; the sums `0x103339d9..0x10333a2f`). **The brief had forward and right swapped.**
  It is the SDK's line. `m_HackedGunPos` is `(0, 0, 55)` on the Troika line (`0x10298e0c..18`), so
  an upright armed human's point is `origin + (0, 0, 55)` units.
- **Which arm a shipped weapon takes** (doc, `wielded_weapons.md` § "The attachment tables"): only
  8 wield models declare `muzzleflash` (crossbow, deserteagle, rem700, steyraug); every other
  weapon — the record's `item_w_thirtyeight` included — takes the hacked-gun arm in retail.
- **The port (P).** `FElysiumCombatCharacter::Weapon_ShootPosition` is the generated stub (zero
  vector, `ElysiumCombatCharacterSlots.cpp` ~:1023). `HackedGunPosUnits` exists
  (`ElysiumNpcSpawn.inl` ~:40). Angles: `FElysiumNpcBase::RetailGetAnglesDegrees()` (slot 221's
  seam, Source degrees) and `AngleVectorsForward` (`ElysiumNpcBaseMotor10.inl` ~:164-169) — forward
  only; **no right / up helper exists**. Attachments: `IElysiumEmbodiment::GetBodyAttachment(Owner,
  FName, FTransform&)` (`ElysiumWorldServices.h` ~:1223) and `FElysiumEntity::GetBodyAttachmentPoint`
  (`ElysiumEntity.h` ~:404) exist, but the map actor resolves only bodies registered as use anchors
  (`ElysiumMapActorEmbodiment.cpp` ~:299, placed props); *(I)* a held wield model is not one — no
  registration site for a weapon was found, none was searched for exhaustively.

## 2. `GatherAttackConditions 0x1026dd10` — slot 197 and slot 562 (L)

- **Slot 197** (`0x1026de18..0x1026de30`), **every pass, unconditionally**, after the top clear
  (`0x1026de02`) and slot 513 (`0x1026de0c`), before the capability test `0x1026de3a`:
  `enemy->slot 197 (+0x314)(&target [ESP+0x28], owner->slot 217 (+0x364)(), 1, 0)` — `posSrc` = the
  owner's `GetAbsOrigin`, `bNoisy = 1`, fourth argument 0. Being noisy it **draws random numbers
  every gather**: an NPC enemy (`0x102789c0`) two `RandomFloat(0, 0.5)`; a **player** enemy
  (`CBasePlayer 0x10174e60`) one `RandomFloat(0.5, 1.0)` `r`, answering
  `GetOrigin (slot 220) + m_vecViewOffset × r` (all three components by the same `r`).
- **Slot 562**, only when the ranged answer `== 0x4f` (`0x1026ded9`):
  1. `0x1026dee4..0x1026def2`: `this->slot 562 (+0x8c8)(this->slot 217(), &target, 1)`; true →
     `SetCondition(0x4f)` (`0x1026df52..64`);
  2. false → slot 560 (`0x1026df00`, clears what the first test raised); `enemy->slot 193
     (+0x304)(&tmp)` (`0x1026df0f`; the player's is `0x100b7f70` → `EyePosition 0x100b4b40`) copied
     to `[ESP+0x28]`; `slot 562(slot 217(), &target, 1)` (`0x1026df3d`); true → `0x4f`; false →
     nothing set (`0x1026df45 JZ 0x1026df69`).
  The gather sets or clears nothing else from the answer; `0x63` / `0x66` / `0x42` / `0x64` are the
  slot's own raises, and the friend timers read `0x63` afterwards (V5a-1, landed).
- **The port (P).** The gather never calls slot 197. `FElysiumEntity::BodyTarget` is a generated
  stub (zero vector and a fire line, `ElysiumEntitySlots.cpp` ~:1421); `FElysiumNpcBase` overrides
  it; **the player does not** (`Public/ElysiumPlayer.h` overrides `EyePosition` only). The gather's
  comment (~:1276) calls slot 193 "BodyTarget": slot 193 is `EyePosition`, slot 197 `BodyTarget`.

## 3. `0x1024f330` → `0x1024f3d0` — settled (L); the brief's walk stands

- `0x1024f330` (`RET 0xc`): `m_hOwner (+0x88c)` resolved, no null guard; `owner = entity+0x94`;
  `owner->slot 389 (+0x614)(&shootPos, ownerPos)`; `weapon->(+0x470)(owner, owner, &shootPos,
  target, bSet)`.
- `0x1024f3d0` (`RET 0x14`: `owner, ignore, start*, end*, bSet`): `Ray_t::Init(start, end)`
  (`0x10015929`) — a **line**, no extents; `CTraceFilterSimple(ignore, 0)` (`0x1000bd7f`);
  `enginetrace->TraceRay(ray, 0x46004003, &filter, &tr)` (`0x1024f424..2a`). Then: fraction `== 1.0`
  → true (`0x1024f46e`); `tr.m_pEnt == owner->slot 167()` → true (`0x1024f4ae`); `m_pEnt+0x9c != 0`
  → `owner->slot 404 (+0x650)(that +0x9c pointer) == 1` → true (`0x1024f4d4`; the body returns
  `AL` of the relation, `MOV AL,AL` at `0x1024f4dc`), else `0x63` under `bSet`, false; no `+0x9c`:
  `m_CollisionGroup (+0x368) == 4 && fraction > 0` → itself again `(owner, ignore = hit entity,
  &(start + (end − start)·fraction), end, bSet)` (`0x1024f58c..9e`), else `0x66` under `bSet`,
  false. A null `m_pEnt` reaches `0x1024f509` and faults (as the brief says).
- **Who counts as a friend: every hit combat character the owner does not hate.** Relation walk
  for a Troika owner (`CAI_BaseNPCTroika::IRelationType 0x10299da0`, doc
  `conditions-and-states.md` § that address): candidate insane / candidate's boss / own boss arms,
  then `CBaseCombatCharacter::IRelationType 0x10333340` (L): for a targetable, unhidden NPC
  candidate, the owner's `m_bfAINPCFlags2` bits `0x20000` / `0x10000` / `0x40000` and its frenzy
  count answer first (1 or 3); otherwise the owner's `m_Relationship` rows — entity match, then
  class match on the candidate's `Classify` (slot 138) — and **with no row, 4 (`D_NU`)**.
  There is no team word and no static default table in this path.
- **Two `npc_VHumanCombatant`s**: `Classify 0x103871a0` returns 4; none of
  `AddClassRelationship`'s 14 callers (`0x10013cf5`) is on the human line, so a spawned combatant
  holds no class row; `player_reaction "D_HT 5"` is an **entity** row for the player
  (doc, `population.md` ~:262). So shooter → friend is `D_NU`: not `D_HT` → **`0x63`**. Verified
  for the base table; *(I)* that no spawn key of the staging adds a row.

## 4. The record `combat/ranged_friend_in_line_of_fire`

- **Times and conditions: unchanged** by items 1–3. Geometry made exact: the shooter's line starts
  at `origin + (0, 0, 55)` units; the first target is the player's origin + view offset × `r`,
  `r ∈ [0.5, 1.0]`; the second the player's eye. The friend must stand across the whole fan.
- **Relation setup: none.** Two `npc_VHumanCombatant`s with no relationship input are `D_NU` to
  each other, which blocks. No same-team key exists. Retail precedent for the default: the 422
  `npc_vhumancombatant` rows carry `player_reaction` only (doc, `population.md` ~:1546); a relation
  between NPCs is authored only by the `SetRelationship` input (`InputSetRelationship 0x10273790`,
  107 uses on the class, doc `entity_io.md` ~:2754) — no single map row was opened this sitting.
  The friend must not be `D_HT` to the shooter by any row, must be `m_bIsBCCTargetable` and not
  script-hidden (else the trace filter drops it), and the shooter must carry none of flags2
  `0x10000` / `0x20000` / `0x40000`.
- **A dependency the record has**: the first test's target is the **player's** slot 197. With the
  port's stub it is the world origin, so the first test can pass and raise no `0x63`. The record
  needs `0x10174e60` ported (item 2; cross-lane, `Public/ElysiumPlayer.h`).

## Not answered

*(The first two are settled in "S7 addendum" below.)*

- `AngleVectors 0x10139610`'s right / up rows were not read (the SDK's are assumed; with
  `m_HackedGunPos = (0, 0, 55)` only `up` is used).
- Whether the killed friend stops blocking at once (when a dying NPC leaves the `0x46004003`
  trace) — outside items 1–4; it bounds `unblocked`, not `blocked`.
- Whether the port registers a held weapon's body anywhere an attachment lookup can reach — not
  swept beyond the two accessors above.

## S7 addendum — the two unknowns above, settled (coder V5a-3, 2026-10-04) (L)

**A. `AngleVectors 0x10139610`'s right / up rows — read; they are the SDK's.** With
`a[0]` pitch, `a[1]` yaw, `a[2]` roll, each × `_DAT_1044eb08` (0.017453292, read from the image),
and `_DAT_104492dc` = **−1.0** (read from the image):

- forward = `(cp·cy, cp·sy, −sp)`
- right = `(cr·sy − sr·sp·cy, −(cr·cy + sr·sp·sy), −(sr·cp))`
- up = `(sr·sy + cr·sp·cy, cr·sp·sy − sr·cy, cr·cp)`

Each out-pointer is null-tested. Slot 389's order stands (item 1): forward·`gun.y`, right·`gun.x`,
up·`gun.z`. The port writes the three rows beside slot 389's body with Y reflected into its
position space (as `StartTaskAngleVectors` does).

**B. A killed friend leaves the `0x46004003` trace on the frame it is killed.** The filter
(`CTraceFilterSimple::ShouldHitEntity 0x101d31c0` → `StandardFilterRules 0x101d3080`,
`PassServerEntityFilter 0x101d2fc0`, the candidate's slot 91 `ShouldCollide 0x100b4de0`,
`m_bIsBCCTargetable +0x1480`, script-hidden `0x100b5190`, the game rules' group pair) reads **no
life state**: nothing in it or in `0x1024f3d0` / `0x10333340` drops a dying character. What drops
it is solidity. The kill is synchronous: `CAI_BaseNPC::Event_Killed 0x10265ad0` →
`CBaseCombatCharacter::Event_Killed 0x1032b9b0` (`m_lifeState = 1`) → slot 301 `CreateCorpse
0x1032c0e0` → `BecomeClientRagdoll 0x10090180(force, bone, 0)` (`0x1032c298 PUSH 0` … `0x1032c29c`),
whose third argument 0 runs, for a model with a ragdoll rig (`modelinfo` slot 18 non-zero — the
human line): `AddSolidFlags(m_usSolidFlags | 4)` (`FSOLID_NOT_SOLID`), `SetMoveType(0, 0)`,
`UTIL_SetSize(vec3_origin, vec3_origin)`, `ThinkSet(NULL)`. A not-solid entity is not handed to
the filter *(I: the engine's side, `engine.dll`, not in this read; the SDK's `IsSolid` cull)*; a
rig-less model only collapses its bounds to a point (`0x101cf390`), which a ray cannot meet. So
the shooter's first gather after the kill traces clear.

**What that does to the record.** Nothing moves `blocked` or the `never` rows. `unblocked`
(`cond-` 0x63) is not the kill: the tail re-raises `0x63` while `curtime < +0x5b88`
(`0x1026e068..0x1026e087`), and `+0x5b88` = last trace-raise + 1.5. With the kill at 0.8 the last
raise is at or before 0.8, so `unblocked` lands at the first gather at or after `last + 1.5`:
**2.2–2.3 s plus one think**, and `fires` after it. `0x2e` stays unraised: the first gather past
the hold resets `+0x5b8c` to `FLT_MAX` (`0x1026e02c`) before `first + 2.5`.

**The port (P).** `FElysiumNpc::BecomeClientRagdoll` (`ElysiumNpc.cpp`) freezes the body
(`SetBodyFrozen`) and does **not** write `RetailSolidFlags |= 4`; whether a frozen capsule still
answers `TraceRetail`'s character list is the embodiment's (not read). V5a-3's ray drops a
character that `IsRetailNotSolid()`, so the one line owed is the flag write in
`BecomeClientRagdoll` (`0x10090180`'s `AddSolidFlags(w | 4)`).

**C. Slot 389 on the player.** `CBasePlayer` fills slot 389 with `0x10162260`, not `0x103338c0`
(`vtmb_slot 389`); unported and unread — no NPC path of this lane reaches it.

## Budget

No query over 10 s.

## Changes to the plan

1. **V5a-3 item 4**: basis order corrected (forward·y, right·x, up·z, added in that order to
   `origin`); angles are slot 221 through `RetailGetAnglesDegrees()`; the muzzle attachment is a
   seam answering "no attachment", named `GetAttachment01("muzzleflash")` on the active weapon.
2. **V5a-3 item 1**: add the unconditional slot 197 call at `0x1026de30` (`BodyTarget(Origin, true,
   false)` on the enemy) — one line above the `0x4f` block, the lane's by this packet.
3. **Integrator (cross-lane)**: the player's `BodyTarget` override, `0x10174e60` — noisy:
   `Origin + (EyePosition() − Origin) × RandomFloat(0.5, 1.0)`, one draw; not noisy and the third
   bool set: `EyePosition()`; neither: slot 192 `WorldSpaceCenter()`.
4. **Record**: no relation input; `about` cites `0x10333340`'s `D_NU` default and
   `Classify 0x103871a0`.
