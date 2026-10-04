# Packets S5 — the retail settling read, fifth sitting (2026-10-04)

Read-only on source and on `Arena/`. Every claim carries its address. **(L)** = read from the
listing or the decompilation this session, verified; **(B)** = bytes read from the install;
**(I)** = inferred, with the reason. `vampire.dll` throughout (`Vampire/dlls/vampire.dll`, image
base `0x10000000`; the constants below were read from its data sections by address).

Self-pointers used below, all on `CBaseEntity`: `+0x94` the `CAI_BaseNPC` self-cast, `+0x98` the
Troika NPC self-cast, `+0x9c` the combat-character self-cast, `+0xa8` the player self-cast (I, from
their uses: `+0x98` carries the move probe `+0x5d40` and slot 522; `+0xa8` gates the HUD line and
the player's hull trace; `+0x9c` is what `0x102579f0` takes as the victim character).

## 1. Slot 526 (`+0x838`) on the Troika line — settled (L)

**Slot 526 is `CAI_BaseNPC::OverrideMoveFacing 0x1027d9f0` on all 77 classes that fill it** — the
Troika base, every `CNPC_V*`, the cine and maker classes; no override anywhere (`vtmb_slot 526`).
Its body pushes and pops the scope-trace frame and **returns false**. It reads nothing and writes
nothing.

So `CAI_Motor::MoveFacing 0x102e19e0`'s first test never returns early on any shipped class. **For
B2:** no seam; the test is a constant false, cited `0x1027d9f0`. If the port holds an
`OverrideMoveFacing` answer that can be true, it is not retail's.

## 2. `0x102e0bd0`'s arguments, motor slot 10, and `0x10262ea0` — settled (L)

**`0x102e0bd0` is `CAI_Motor::MoveGroundStep`** (`RET 0x1c`, seven arguments):

```
char MoveGroundStep(this, const Vector& newPos, CBaseEntity* pMoveTarget, float newYaw,
                    char bAsFarAsCan, char bTestZ, AIMoveTrace_t* pTraceResult, char bNoTrace)
```

- Refreshes the motor's and the move probe's (`motor+0x68`) cached `owner+0x156c` and
  `gpGlobals+4` words (`102e0bf7..102e0c27`).
- `flags = bTestZ ? 1 : 5` (`102e0be5..102e0bf2`). `0x102e4f50` = `CAI_MoveProbe::TestGroundMove`
  (its VProf string): `(probe, slot 220 GetOrigin(), newPos, mask 0x202400b, 100.0, flags, &trace,
  bNoTrace, 0)`. In it: flag bit 1 → no stand-position check (`pct = 0`); **flag bit 4 → the final
  height test is skipped** (without it, `|end.z − newPos.z| > max(StepHeight × 0.5, hull height +
  0.1)` answers status −2 with the world as the blocker); **`bNoTrace != 0` → the trace's end is
  `newPos` untested and the probe answers true at once** (`0x102e4f50`, the `param_7` arm).
- `pTraceResult != 0` → the 14-word trace is copied out.
- `bHitTarget = trace.pObstruction (+0x1c) != 0 && pMoveTarget == trace.pObstruction`;
  `bBlocked = trace.fStatus (+0x00) < 0`. `bBlocked && !bAsFarAsCan && !bHitTarget` → **return 0**,
  nothing moved.
- Else: `0x101cf5c0(owner, trace.vEndPosition, 1)` (`UTIL_SetOrigin`, touching triggers; I, the
  SDK shape); `trace.flStepUpDistance (+0x34) > 0.1` (f32 `0x104491b4`) → `height =
  clamp(stepUp, 0, StepHeight 0x102e26f0)` and the physics object's (`owner+0x36c`) shadow
  controller (`+0xc0`) slot 3 `StepUp(height)`; `newYaw != −1.0` (f32 `0x104492dc`) →
  `SetLocalAngles` with `y = newYaw` (owner `+0x374` read, `+0x100` write).
- Return: `bHitTarget` → **4**; `!bBlocked` → **1**; `fStatus == −3` → **2**; else **3** (the
  status table of `navigation-jump-links.md` § "The motor status table").

**The three callers and what they pass** (L):

| caller | newPos | target | yaw | bAsFarAsCan | bTestZ | pTrace | bNoTrace |
|---|---|---|---|---|---|---|---|
| `CAI_HumanoidMotor` slot 19 `0x10264680` | origin + dir × dist | `move+0x34` | −1 | 1 | **1** | slot 19's 2nd argument | slot 19's 3rd argument |
| `CAI_Motor` walk `0x102e1560` | origin + dir × dist | `move+0x34` | −1 | 1 | "the step was clamped to `move.maxDist`" (1 when `dist > maxDist`, else 0) | its 4th argument | its 5th argument |
| `CAI_BaseNPC::AutoMovement 0x10280a50` | the interval's sequence movement | `0x102729d0(this)` | the movement's yaw | 0 | 0 | 0 | 0 |

**Motor slot 10 is `0x102e1440`** (the zero-answer path of both movers): `m_vecVelocity = 0` and
`0x102ddc40(owner+0x5d38)` (not walked). So a refused step zeroes the motor's velocity.

**`0x10262ea0(this NPC, i, t)` is the TURN script's insert** (`BuildInsertNode`; the array is
`+0x6038`, count `+0x6044`, capacity `+0x603c` — the turn script, not the velocity script
`+0x6024`). Entry = 14 floats, `+0x00` `flTime`, `+0x04` `flElapsed`, `+0x10` `flYaw`, `+0x2c`
`vecLocation`. From entry `k = i`, while `k < count`:
- `t <= flTime[k]`: `a = t / flTime[k]`; **`flTime[k] −= t`**; a new entry is inserted at `k+1`
  (the tail shifted up by one, the array grown by `0x10264370` when full) with **`flTime = t`**,
  `flElapsed = (1 − a)·flElapsed[k] + a·flElapsed[k+1]`, `vecLocation = (1 − a)·loc[k] +
  a·loc[k+1]`, every other word zero; **returns `k+1`**. The caller `0x10262c20` then writes the
  new entry's `flYaw`.
- else `t −= flTime[k]`, `k += 1`. Ran off the end → **returns 0**.

Two things a coder must keep: the two durations are **swapped against the SDK** (the old entry
keeps `flTime − t`, the new one gets `t`), and the lerp reads entry `k+1` **before** the shift, so
on the last entry it reads one slot past the count. As read; reproduce the first, guard the second
and say so at the line.

**For B1:** nothing in item 2 changes — under K1 the body's own movement stands for
`MoveGroundStep`; the facts a later lane needs are the return codes, "a refused step zeroes the
velocity", and `bNoTrace`.

## 3. The per-step contact: `MeleeSwingStep 0x10343020` and `0x102579f0` — settled (L)

### `MeleeSwingStep(this, const Vector& pos, const QAngle& ang, float prevCycle, float cycle)`

The decompilation is offset by `__alloca_probe`; the entity filter and the hit arms were read from
the listing (`0x1034394d..0x10343f96`).

1. `GetModelPtr`, `GetSeqDesc(m_nSequence)`; either null → return. `count = min(seqdesc+0x2c4,
   20)`. `CalcPose` at **`cycle`** with the live pose parameters. `weapon = GetActiveWeapon()`.
2. **Per record `r`** (`seqdesc + seqdesc[+0x2c8] + r × 0xbc`: `+0x00` start, `+0x04` end, `+0x08`
   bone, `+0x0c` point A, `+0x18` point B, both bone-local):
   - bone index `0x100c77d0(model, sequence, rec+8) < 0` → the record is skipped whole (nothing
     stored, nothing cleared).
   - The bone's matrix is built from `pos` / `ang` (`0x100c3600`, once per distinct bone); `A`, `B`
     = `VectorTransform` of the two points.
   - **`n = ceil(|B − A| × 0.1666667)`** (f32 `0x10488874` = 1/6: one sample per 6 units of
     segment). `n < 2` → `n = 1` and **`A = B =` the midpoint**.
   - **The window**: open iff `m_bMeleeSwingIsLive (+0xaa1)` and `start <= cycle` and `end >=
     prevCycle` (both inclusive; `10343…` `start > cycle` or `end < prevCycle` closes). Closed →
     this record's hit list count (`+0xcac + r × 0x14`) `= 0`. Either way the record's last
     endpoints (`+0xac0 + r × 0x18`: last A, last B) are stored at the end of the pass.
   - Open: the AABB over `{A, B, lastA, lastB}`; `0x101cca80(list, 100, &mins, &maxs, 0x22102080,
     1)` — the entities in that box, at most 100 (the helper is not walked).
3. **Per entity `e` in the box, in this order** (`victimCC = e+0x9c`):
   1. **The NPC attacker's relation filter** (`1034394d..103439a7`): when `this+0x94 != 0` and
      `debug_allow_melee_ff` (`0x10936ee0`) is 0 and `victimCC != 0` and `victimCC != this+0x94`:
      slot 404 `IRelationType(victimCC)` must be **1 or 2** (hate, fear) **and**
      `0x10323930(npc, victimCC)` must be false (true when both carry the same 16-bit word at
      `+0x10b0` and neither is `0xffff` — the team id; I, it sits beside `m_sTeamName +0x10ac`).
      Else the entity is skipped. **A player attacker has no relation filter.**
   2. `e` solid: `m_nSolidType (+0x2b0) != 0` and `!(+0x2b4 & 4)`; else skip.
   3. The attacker itself not `FSOLID_NOT_SOLID` (`this+0x2b4 & 4` clear); else skip.
   4. `e+0xf4` (byte, `0x100b5190`; unnamed) zero; else skip.
   5. `victimCC == 0` or `m_bIsBCCTargetable (+0x1480) != 0`; else skip.
   6. Already in this record's hit list (searched from the tail) → skip.
   7. `0x1012c9c0(e)` (the root of `e`'s `+0x25c` owner chain) `== this` → `e` is added to **this
      record's** list only; no hit (the attacker's own weapon and attachments).
   8. **`victimCC == 0` or the victim's slot 329 (`+0x524`) true** (`10343b00..10343b16` →
      `10343eb0`) — **the box overlap is the hit, no ray**: a trace record with start = the
      attacker's `GetAbsOrigin`, end = `e`'s `GetAbsOrigin`, `m_pEnt = e`; `e` is added to the hit
      list of **every** record of the sequence (no window test); `weapon` slot 270 (`+0x438`)
      `(&trace, record)`. Slot 329's base is `0x1014f850` (`XOR AL,AL` — false);
      `CNPC_VTzimisceRunner` overrides it (`0x103c30c0`). So **every non-character solid entity is
      hit by box overlap**, and a character only through the ray below.
   9. **Else (a character, slot 329 false) — the ray**: for `i = 0 .. n−1`: `f = n > 1 ? 1 −
      i/(n−1) : 0`; `P = A + (B − A)·f`; `Q = lastA + (lastB − lastA)·f`; a zero-extent ray
      **from `Q` to `P`** (the same point of the segment, last pass → this pass);
      `0x101d2530(&ray, 0x200400b, e, &tr)` (clip the ray to that one entity). `fraction >= 1` and
      not `allsolid` and not `startsolid` → next `i`. **First hit**: `e` is added to the list of
      every record `k` whose window overlaps this one's (`start_r <= end_k && start_k <= end_r`,
      `10343e37..10343e84`); `weapon` slot 270 `(&tr, record)`; done with `e`.
   No world or occlusion trace stands between the limb and an entity.
4. **The wall contact** (`10343f96`): only when the attacker's slot 328 (`+0x520`) is true —
   **`CBasePlayer 0x1015dca0` and `CNPC_VWerewolf 0x103ca730` return 1; the base `0x1014f830`
   returns 0, so no other NPC ever takes it**. Per sample, the same `Q → P` ray through the engine
   trace with a simple filter on the attacker (`0x101d3190(this, 0)`); on a hit whose plane normal
   is non-zero with `|normal.z| < 0.3` (f32 `0x10451ab8`): forward = `AngleVectors(GetAbsAngles)`
   flattened; when its 2-D length² `> 1e-12` (`0x1049e038²`) and `|dot(normal, forward)| >
   0.7071` (`0x1049e03c`) and a length `< 20.0` (`0x1049e040`; its operand is lost to the x87
   stack — unread) → the attacker's slot 319 (`+0x4fc`, the blocked reaction) and the sample loop
   breaks. Otherwise, once per swing (`+0xaa0` clear): `0x101cfef0(&tr, 0x80, 1, weapon)` (the
   impact effect) and `+0xaa0 = 1`.
5. After the record loop, records `count..19` have their hit-list counts zeroed.

### `0x102579f0` — the weapon's slot 270 (`+0x438`), `(this weapon, trace*, record)`

1. `owner = 0x10252240(this)` null → return. `ent = trace+0x4c`; `victimCC = ent ? ent+0x9c : 0`.
2. Weapon slot 339 (`+0x54c`) with no argument; `CSoundEnt::InsertSound(0x10, &trace.endpos,
   [0x1072bc5c], 0.2, [0x1072bcb7], owner)` — **before** the null-entity test. `ent == 0` → return.
3. `dir = normalize(trace.endpos − trace.startpos)`. Player owner with a victim character and the
   mode record's `+0x3c4 > 0` → the HUD line `"MeleeHit %s with %s"`.
4. The packet: the mode's `CVDmg_t` (weapon `+0x7a0 + mode × 0x44`, 17 words) copied;
   `SetSrc(owner)`; the damage info seeded from it (`0x101c2770`), its inflictor `= this`;
   `0x101c29e0(info, trace)`.
5. `blocked = WasMeleeBlocked(owner, victimCC)`; `rolls = GetMeleeDiceRolls(owner, victimCC)`.
   - **`rolls == 0` → `blocked = 0`, successes 1, and the damage goes on.**
   - else `through = DamageWentThrough(rolls)`; `successes =` an integer conversion of a value
     from the roll (the operand is lost to the x87 stack — unread); `dmg+0x38 = rolls[3]`.
     `blocked` → the debug overlays, then the victim's slot 318 (`+0x4f8`) and the owner's slot
     319 (`+0x4fc`, `(victim, rolls, &dir, …)`) — the two blocked reactions.
     **`!through && blocked` → the tail (step 8): no damage, no reaction.** `successes < 1 → 1`.
6. `m_iToHitSuccesses = successes`; `AddFlags(8)`; **`DispatchTraceAttack(ent, info, dir,
   trace)`**; `modifier = 0x10204900(dmg.m_vtModifierDependency, owner)`; `inflicted =` the
   accumulator's (`DAT_1072cb10` ? `GetDmg` : `DAT_1072cb40`); Potence: the owner's type-3 stat
   list `GetValue(9) > inflicted` → `inflicted =` it; `mult = 0x101c2a70(info)`; **`total =
   (modifier + m_iDiceAmt) × mult × inflicted`**; owner misc flag `0x100000` → `total ×=
   0x101e8f20(&0x10739d08, owner) × 0.01` (f32 `0x10450aa4`) and the flag is removed;
   `0x103455a0(owner, victimCC, record, &force)`; the accumulator's force, damage and target
   words written; `0x101c2a10(&DAT_1072cb10, −1)` (the apply); `0x101c2c60()`; weapon slot 269
   (`+0x434`) `(ent, dir.x, dir.y)`.
7. `!blocked && victimCC`: `(record == 0 || record == −0x28 || !victim slot 326 (+0x518)(record))
   && !victim slot 400 (+0x640)()` → victim slot 321 (`+0x504`), the plain hit reaction; else
   `GetKnockbackActivity(victim, trace, owner, record)`: −1 → a Warning, else victim slot 320
   (`+0x500`) `(owner)`.
8. Tail: `0x101cfef0(trace, 0x80, 1, this)` (the impact effect), `ent` slot 21 (`+0x54`), owner
   slot 24 (`+0x60`) `(ent)`; the packet destroyed.

### The port's contact against it (`ElysiumWeaponClasses.cpp` `AdvanceSwingContact` :2503, `MeleeContact` :2842; `AElysiumMapActor::QuerySwingContacts` :1650)

C1 keeps the contact and changes only who calls it (its brief). These are the differences, for the
judge to assign:

| # | retail | port |
|---|---|---|
| D1 | **An NPC attacker hits only characters it hates or fears and that are not on its team** (`debug_allow_melee_ff` off). | No relation filter in the walk (`:2811-2816` asks only "a living combat character"). Two squad-mates swinging side by side hit each other. |
| D2 | `rolls == 0` → unblocked, successes 1, damage dispatched. | `:2859-2867`: no staged roll → **no contact**. |
| D3 | Damage is dispatched unless `blocked && !DamageWentThrough`; successes floored to 1. | `:3005` `if (Margin > 0)` — an unblocked non-positive margin commits nothing. |
| D4 | The hit test is `n` rays (one per 6 units of segment) from each sample's last position to its present one, clipped to the candidate entity itself, mask `0x200400b`; `n < 2` is the midpoint alone. No occlusion trace. | The candidate's box against four edges of the swept patch (the segment before, after, and each endpoint's path), plus an occlusion trace on the use channel that ignores the attacker (`:1663-1700`). |
| D5 | Candidates: every solid entity in the box. A non-character is hit on box overlap and reaches slot 270 (props, breakables). | Combat characters only; containers and reported-dead bodies excluded (`:1707-1719`). |
| D6 | `m_bIsBCCTargetable (+0x1480)` and the entity byte `+0xf4` gate a candidate; a non-solid attacker hits nothing. | Not asked in the walk (the candidate's not-solid test is: `ElysiumIsRetailNotSolid`). |
| D7 | Victim slot 329 true (the Tzimisce Runner): box overlap is the hit and marks **every** record. | No such arm. |
| D8 | Slot 328 true (player, Werewolf): the wall contact — the attacker's blocked reaction off a wall, or one impact effect per swing. | No world contact in the walk. |
| D9 | The record's endpoints are posed at each sub-step's own cycle (`CalcPose` at `cycle`). | The segment is the live bone's, lerped linearly between two batches in the attacker's frame (`:2786-2795`). |
| D10 | Slot 270 inserts a combat sound (`InsertSound(0x10, end, …, 0.2)`), calls weapon slot 339, the impact effect, `ent` slot 21 and the owner's slot 24. | Not in `MeleeContact` as read; if another file does them it was not searched. |
| D11 | Plain hit vs knockback is the victim's slot 326(record) / slot 400, asked after the damage. | Armed from the margin class `HitKnockback` (`:2972`), spent after the commit — the order is right; the selector differs unless `KnockbackContact` asks those slots (not read). |
| same | Hit-once spread over records with overlapping windows; a closed record clears its list; the roll is consumed, not rolled, at contact; blocked reactions before the damage, knockback after. | The same. |

`ElysiumSwing::WindowOverlaps` was not read: retail's test is inclusive at both ends (`start <=
cycle && end >= prevCycle`).

## 4. Slot 331 `ChooseMeleeAttackSequence 0x10347180` — settled (L)

`bool (this, CBaseCombatWeapon* weapon, CBaseEntity* enemy, int activity, int* outSequence)`. The
bit meanings `animation_rig_resolution.md` left open are settled here.

1. `*out = −1`. No model → false. `npc = this+0x98`.
2. **NPC with an enemy — the line gate** (`1034723d..103472fb`): `0x102e37b0(npc move probe
   +0x5d40, this slot 192 (+0x300), enemy slot 192, 0x202400b, 1, &tr)`. Blocked (`fraction < 1`
   or `allsolid` or `startsolid`) by an entity that is not the enemy, not the entity of the
   enemy's handle `+0x1538`, and whose `+0x4c` bit 10 is clear → **`npc SetCondition(0x3a)`,
   return false, `*out = −1`**.
3. **Enemy geometry** (`10347300`): the enemy's collision bounds (`+0x270` slot 15), `centre`,
   half-extents `h`; `rel = centre − this.GetAbsOrigin()`; **`dist2D = sqrt(rel.x² + rel.y²)`**;
   the enemy's box **in the attacker's line frame**: `mins = (dist2D − h.x, −h.y, rel.z − h.z)`,
   `maxs = (dist2D + h.x, +h.y, rel.z + h.z)`.
4. `step = npc ? npc slot 522 (+0x828) : 4.0`.
5. `weapon == 0` → false. Weapon slot 360 (`+0x5a0`) `& 0x18000 == 0` → false.
6. `count = GetSequencesForActivity(this, activity, seqs[], weights[], 0x10204900(mode's CVDmg_t
   weapon+0x7a0 + mode×0x44, this))`. NPC with an enemy: `dir = normalize(enemy origin − this
   origin)` (3-D), `right = cross(dir, (0,0,1))` (`0x1011e010`).
7. **Per candidate** (`seqdesc`): `flags = 0`.
   - **bit 8** — enemy and `seq+0x2cc <= dist2D <= seq+0x2d0` (the sequence's authored range).
   - Always: `weapon+0x8b8 = min(weapon+0x8b8, seq+0x2cc)`, `weapon+0x8c0 = max(weapon+0x8c0,
     seq+0x2d0)`.
   - **`flags |= 4`.** `0x100c6020(model, seq, 1.0, pose parameters, &delta, &angles)` (the
     sequence's whole movement) false → **`flags |= 0x10`**, no sweep. Else:
     - `end` = origin + `dir·delta.x + right·delta.y` (NPC with an enemy — the movement is laid
       along the line to the enemy), else `delta` through the body's own coordinate frame. Both
       ends raised by `step`.
     - NPC (`this+0xa8 == 0`): `0x102e3450(move probe, start, end, 0x202400b, &tr, 0)` with the
       NPC's collision mins and maxs; **then `seq+0x2d4 >= 0` → `flags = 0`** (`1034795d..10347967`
       — bit 8 and bit 4 both dropped for a sequence carrying that word; as read). Player: a hull
       trace.
     - Not blocked → keep. Blocked: `hit = tr.m_pEnt`; the entity of the enemy's `+0x1538` handle
       counts as the enemy.
       - NPC, `hit == 0 || hit == enemy`, not `startsolid`, and **`(1 − fraction) × |end − start|
         <= debug_melee_npc_range`** (the ConVar's float, default **`"128"`**, strings
         `0x10623f88` / `0x10623fa4`; 0 when its `+4` virtual answers true) → keep (bit 4 stays).
       - Player: not `startsolid` and (`fraction >= 0.9` (f32 `0x10450a9c`) or `(1 − fraction) ×
         length <= 8.0` (f32 `0x1045597c`)) → keep.
       - Otherwise: `hit == enemy && this.m_bAllowsInterpenetratingAttacks (+0xfe0)` → **`flags
         |= 1`**; else **`flags &= ~4`**.
   - **bit 2** — enemy, and any of the sequence's boxes (`seq+0x2bc` count, `seq+0x2c0` offset,
     24 bytes: mins, maxs) overlaps the enemy's line-frame box of step 3 (strict on all six sides).
8. **The pick** — `0x10348100(count, seqs, weights, flags, mask)` (`ChooseSequenceFromList`):
   the candidates whose `(flags & mask) == mask`; none → −1; one → it; total weight `< 1` → a
   uniform `RandomInt`; else a weighted `RandomInt` walk.
   - **No enemy**: mask `0x10`, then mask `0`; `*out =` the pick; **returns false**.
   - **Enemy**: two passes; in each, masks in the order **`7, 5, 6, 3, 1, 2, 4, 0`**, the first
     pass with **`| 8`**. The first non-negative pick is written to `*out`; **returns true only
     when the mask has a bit of `3`, and bit 4, and bit 8** — i.e. the first pass's `7`, `5` or
     `6`. Nothing picked → `*out = −1`, false.

**The bits**: `1` = the movement ran into the enemy (interpenetration allowed); `2` = a swing box
reaches the enemy from where the attacker stands; `4` = the movement is not blocked; `8` = the
enemy is inside the sequence's range; `0x10` = the sequence does not move.

**So "true with `out >= 0`" — the `0x51` arm of `0x103ea7e0` — means: a candidate in range
(`+0x2cc..+0x2d0` against `dist2D`), whose movement is clear, and that either reaches the enemy
with a swing box or closes onto it.**

**For V5a-1 and V11-1:** the port has no body for `0x10347180` (`research where`: only
`FElysiumNpcMingXiao::ChooseMeleeAttackSequenceSeam` and comments). Its inputs: the sequence's
range pair `+0x2cc` / `+0x2d0`, its box list `+0x2bc` / `+0x2c0`, the word `+0x2d4`, the sequence
movement, the move probe's sweep, the enemy's bounds. Whether the baked sequence table carries
`+0x2bc..+0x2d4` was not checked (source is not this reader's to read beyond the contact).

## 5. The status behind `0x1033d940`, and the flamethrower — settled (L; the name I)

- `0x1033d940(owner, rate)` (listing, 11 instructions): `0x101e3f50(&DAT_10739a4c, owner)` true →
  `rate + rate`; else `rate`.
- `0x101e3f50(manager, cc)`: lazily caches `mask = 0x101e1610(manager, 10)` (the OR of `record+0xc`
  over every record whose `record+0x10 == 10`) and `first = 0x101e1520(manager, 10, 0)` (the first
  such record's `+0xc`); true iff `cc.m_iDisciplineFlags2 (+0xeb4) & mask` and one of the five
  bits `first << 0..4` is set in it.
- **Discipline id 10 is Presence**: the `v_discipline_*` enum is the datamap's array order at
  `CBaseCombatCharacter +0x12c4` — animalism 0, auspex 1, blood_healing 2, celerity 3,
  corpus_vampirus 4, dementation 5, dominate 6, fortitude 7, obfuscate 8, potence 9, **presence
  10**, protean 11, thaumaturgy 12 (L, `vtmb_fields`). Corroborated twice: `0x101e3ff0` (the same
  id-10 masks) is followed by the zeroing of `m_iFriendPresenceEffect` / `m_iEnemyPresenceEffect`
  in species slot 313, and `0x102579f0` reads Potence as value index 9.
- **So: a character carrying any of the five Presence-level bits in `m_iDisciplineFlags2` has its
  attack rate (the interval) doubled** — it fires half as often. Who sets those bits on an NPC
  (the aura's application) is not read; the bit values are the run-time table's `record+0xc`.
- The same test is the "discipline gate" of `SelectScheduleRangedCombat 0x102b7fc0` and its
  human twin, and of `0x102b7f40`: its identity is settled with it.

**`CWeaponRanged_FlameThrower::Attack 0x103e2f30` has no NPC gate** (L): after
`CWeaponRanged::Attack`, `+0x89a = 0`; the owner's combat-character pointer (`0x102521f0`: handle
`+0x88c` → `+0x9c`) null → return; `!+0x1519 && +0x1518 && +0x151c < curtime` → `+0x1518 = 0`,
return; **`clip[0] (+0x74c) −= 1` for any owner**; `< 1` → `+0x1518 = 0`, return; else the flame is
lit. So an NPC's flamethrower **does** spend its clip — the one exception to "firing never lowers
an NPC's clip" (S4 item a).

**Mode type 6, the throw `0x10239e70`, and its "launch" `0x10239f30`** (L): `0x10239f30` is 40
bytes, not truncated: the owner's slot 220 and slot 368 `BodyDirection2D(&out)`, **both results
discarded** — it launches nothing. `0x10239e70`: weapon slot 307 (`+0x4cc`) `(0xb9)`; that call;
unless the ConVar at `DAT_1088aef4` is set to 2 or more, `clip[ammo index] −= 1`; `clip < 1` →
owner slot 385 (`+0x604`) `(weapon, 0, 0)` and `0x101cd940(weapon)` (the weapon leaves the owner
and is removed). No projectile is created in either body.

## 6. The inventory-section order — settled (B + L)

The list `0x1073a2e0` is filled by `0x101ead30` from **`vdata\system\items.txt`**, block
`InventorySections`, one record per `InventorySection` child **in file order**, numbered from 0
(`record[0]` = the index, `record[1]` = `InternalName`, flag bit 1 = `IsDisplayed`).
`CacheInventorySections 0x10340180` stores `0x10619d28[t] = index − 1` and `0x10937cd0[t] = (index
− 1) × 32` when positive, else 0.

The file (the pack copy, `Vampire/pack101.vpk`, 28,293 bytes, and the deployed
`Content/ElysiumCorpus/vdata/system/items.txt` agree on this block): **`None`, `Weapon_Melee`,
`Weapon_Ranged`, `Weapon_Thrown`, `Armor`, `Generic`, `Powerups`, `Hidden`** — the same order as
the DLL's own name table, with the file's comment "The `None` section must exist, and must be the
first one". So:

| type `t` | section | `0x10619d28[t]` | first slot `0x10937cd0[t]` |
|---|---|---|---|
| 1 | Weapon_Melee | 0 | 0 |
| 2 | Weapon_Ranged | 1 | 32 |
| 3 | Weapon_Thrown | 2 | 64 |
| 7 | Hidden | 6 | 192 |

**`GetBestMeleeWeapon 0x10336f20` iterates** the type list `0x10619eb4 = {1, 2, 3, 7, −1}` (B):
weapon slots `[0, n₀)`, then `[32, 32 + n₁)`, then `[64, 64 + n₂)`, then `[192, 192 + n₆)`, where
`n` is owner slot 298 (`+0x4a8`) of the section number in column 3; the first `GetWeapon(i)` whose
slot 360 `& 0x18000` wins.

*Found on the way:* the deployed `Content/ElysiumCorpus/vdata/system/items.txt` (33,109 bytes) is
byte-identical to `Unofficial_Patch/vdata/system/items.txt`, not to the pack copy. Not this
reader's to fix; named for the owner.

## 7. `0x102beda0`, `0x10374e50`, and the Werewolf's slot 600 — settled (L)

- **`CAI_BaseNPCTroika::OnTakeDamage 0x102beda0`** (slot 390), in order: the damage info is copied
  to `this+0x660c..` (the 0x4b-byte record); `r = CAI_BaseNPC::OnTakeDamage_Alive(info)`; `r == 0`
  → return 0. `dmg = info.vdmg ? GetDmg : info+0x30`.
  - `dmg <= 0`: `SetCondition(0x4c)`; `m_hLastDamageEnt =` the attacker's handle (`info+0x2c`) or
    −1; `m_bCondTookDamage = 1`.
  - `dmg > 0`: `AddExpressionForEvent(0)`; `m_bfAINPCFlags & 0x40000000` → slot 144 (`+0x240`)
    `(info)`, return `r`. `+0x62ec != 0 && 0x102daf50(it)`: when `m_iInterestingDeathActivity ==
    −1`: the activity of the name `0x102daf20(it)`; −1 → `DevWarning`, slot 144 `(info)`, return
    `r`; else `0x102ae750(this, 0x104, 0)` (line `0x6121`); **return `r` either way**.
  - Then: attacker non-null → `0x1028e8b0(this, attacker, 5.0)`; **slot 600 `(slot 167
    GetEnemy())`**; return `r`.
  The same shape as the combatant's `0x10385a50`; the dispatch is slot 600 of whichever line the
  class is on.
- **`0x10374e50` is `CNPC_VDog`'s** (called only from `CNPC_VDog` slot 460 `0x10374d80`, when
  condition `0x78` or `0x7e` is set): slot 596 (`+0x950`) `(the entity of handle +0x628c)`;
  `+0x5cc4 = 2` (line `0x52a`); **slot 600 `(GetEnemy())`**; then, `+0x5bb0 < 1` and a squad
  (`+0x5da4`) and an enemy → `SquadNewEnemy(squad, enemy)`. The Dog's slot 600 is the Troika
  body `0x102b57c0`.
- **The Werewolf's slot 600 is `0x103cb7f0`: `m_bInMelee (+0x6078) = 1; return true`** — no
  capability test, no coordinator call. Its dispatch sites: `CNPC_VWerewolf::NPCThink 0x103cb590`
  (when the think is not paused by `DAT_1092053c & 1` and `GetEnemy()` is live — the listing has a
  five-way jump table on a tick counter there, not walked) and `CNPC_VWerewolf::StartTask
  0x103ccda0`. **The Werewolf never takes a coordinator slot through slot 600.** The other
  non-coordinator bodies of the same shape (`+0x6078 = 1`): `0x10376ba0` FrenzyShadow,
  `0x10379f20` Gargoyle, `0x10381780` Hengeyokai, `0x10395df0` MingXiao, `0x1039eab0` the
  tentacle; `0x103c1a60` HeadClaw and `0x103c39e0` Runner are their own; `0x103dd900` Yukie (not
  read).

## 8. The female `walk_0`'s `.mdl` fps bytes — settled (B)

`models/character/shared/female/move_and_ranged.mdl` (version 2531, 5,370,952 bytes) read from the
retail pack **`Vampire/pack001.vpk`** through the pipeline's own `vpk.extract` and
`mdl_skel.find_anim`: animdesc `walk_0` at file offset **`0x2f44`**; `fps` at `+4` = bytes **`AE 47
07 42`** = float32 **33.82** (33.81999969482422); `numframes` at `+12` = **37**. The male bank
(`pack001.vpk`, 6,248,704 bytes): `walk_0` at `0x2ffc`, `00 00 F0 41` = **30.0**, 37 frames. The
loose `Unofficial_Patch` copies carry the same bytes at the same offsets. So the sidecar's 33.82 is
the file's, and the file is retail's. `36 / 33.82 = 1.064459 s`; 101.278 cm/s stands.

## 9. The other "unrecovered" lines in the briefs

| line | answer |
|---|---|
| `brief-A2` :48 — `0x1044f020`'s value | **f64 0.001** (B). The early-out's return was not read (R2's own open item; A2 does not need it unless it ports the guard's return). |
| `brief-C2` :116 — slot 611 `0x102c12a0` | **Walked** (L): `rec = 0x100ecee0(&DAT_10924980, this, &minTime, &changeChance, &altChance)` (the disposition table's row for `+0x64d4` × `+0x64e8`; the three numbers come from its `+0x108 / +0x10c` pair when `+0x64c0` is set, else `+0x110 / +0x114`; `altChance` from `+0x118`). `+0x64c0` set → `seq = rec[+8 + stance×4]`, `+0x64e0 = 0`, `+0x64e1 = 0`. Else, `+0x64e0` or `+0x64e1` set → the same. Else, when `rec[+8+…] != rec[+0x14+…]` and `RandomInt(1,100) < altChance` → `seq = rec[+0x14 + stance×4]`, `+0x64e0 = 1`, `+0x64e1 = 0`. Else, `minTime < curtime − +0x64e4` and `RandomInt(1,100) < changeChance` → `0x102c1230` (a new stance `RandomInt(0,2)` redrawn until it differs from `+0x64c8`; `0x100ecfc0(table, this, old, new)` answers the sequence; `+0x64c8 = new`; `+0x64e4 = curtime`), `+0x64e0 = 0`, `+0x64e1 = 1`. Else `seq = rec[+8 + stance×4]`, both bytes 0. `seq == −1` → `m_nSequence (+0x6f0)`. `stance` = `+0x64c8`. Draw order as written. |
| `brief-C2` :278 — what reaches `0x102b5bb0` | The corpus cannot answer: no static caller and no vtable holds it (S4). |
| `brief-C1` :88 — `0x10239f30` | Settled, item 5: it launches nothing. |
| `brief-D` :153 — whether any shipped damage sets bit `0x2000` | A data question over the damage records and maps, not an address; not read; no lane depends on it. |
| `brief-B2` :108 — the 13.4 s | Reader R1b's (a measurement). |

## Budget

No query over 10 s (the longest: the pack index, 0.5 s). Two corpus replies ran to the size cap
(`vtmb_slot 328` and `329` asked without a small `limit`); nothing was retried.

## Changes to the plan

- **B2** — smaller: item 2's "the coder reads slot 526 first" is gone; the owner test is a
  constant false (`0x1027d9f0`), no seam.
- **B1** — none. (`MoveGroundStep` and the insert are recorded; item 2's port is unchanged.)
- **C1** — its own size is unchanged (it keeps the contact). **New, for the judge:** the contact
  differs from retail in D1–D11. D1 (no relation filter: squad-mates hit each other) bears on any
  record with two melee NPCs side by side — V11's `melee_ally_in_the_way` among them; D2 and D3
  change whether an NPC's swing damages. They need an owner and a place in the order — a contact
  lane after C1, or a ruling that they wait. C1's type-6 line: no launch exists to port.
- **V5a-1** — **larger, or a new lane before it**: the port has no body for slot 331, and the
  `0x51` arm is that body. It needs the per-sequence range pair, box list and movement
  (`seq+0x2bc..+0x2d4`); whether the bake carries them must be checked before the lane is sized.
  Until then the brief's seam stands, now with its exact meaning.
- **V11-1** — item 11 is unblocked (the order is settled); it ports the walk if the port's
  inventory has sections. Slot 600: the Werewolf and four other species never reach the
  coordinator (no change to V11's files).
- **O3** — the rate seam is named (Presence, `m_iDisciplineFlags2`); it stays answering false
  until the discipline table's bits exist in the port. Its "two other decrements" note changes:
  the flamethrower's applies to an NPC owner; type 6 creates no projectile.
- **A0** — none (the record text's 33.82 fps is the retail file's bytes).
- **Corpus hygiene, no lane yet:** the deployed `items.txt` is the Unofficial Patch's copy.

## Placement of items 3 and 4 (planner, 2026-10-04)

**Item 4's open check — does the bake carry `seq+0x2bc..+0x2d4`? Yes, all of it, and the
movement; no pipeline change, no judge item.**

| retail | pipeline | runtime |
|---|---|---|
| `+0x2cc` | `formats/mdl_skel.py:273`, `importers/clip_data.py:19` | `FElysiumNpcClip::LowReachCm`, `Public/Visual/ElysiumNpcClips.h:98` (row col 11, `Visual/ElysiumNpcClips.cpp:444`) |
| `+0x2d0` | `mdl_skel.py:262`, `clip_data.py:18` | `ReachCm`, `ElysiumNpcClips.h:80` |
| `+0x2bc` / `+0x2c0` | `mdl_skel.py:290-292`, `clip_data.py:22` | `Envelopes`, `ElysiumNpcClips.h:104` (col 12, `ElysiumNpcClips.cpp:448`) |
| `+0x2c4` / `+0x2c8` | `mdl_skel.py:304-305`, `clip_data.py:21` | `Swings`, `ElysiumNpcClips.h:111` |
| `+0x2d4` | `mdl_skel.py:346, 752` (`combo.mask`), `clip_data.py:23` | `Combo.Mask`; `>= 0` is `HasStateMask()`, `Public/ElysiumComboChain.h:192, 225` (col 10, `ElysiumNpcClips.cpp:430`) |
| the movement `0x100c6020` reads | `mdl_skel.py:1704`, `clip_data.py:50-61` | `FElysiumClipMovementPath`, `ElysiumClipMovement::SampleDelta`, `Public/ElysiumClipMovement.h:76, 253` |

Caveats for the porter: the bake states an unset edge as absent (`ReachCm` 0, `LowReachCm` −1),
retail as `FLT_MAX` / `FLT_MIN` (`mdl_skel.py:267, 281`) — restore the marker at the line; a
block with `+0x2d4 == −1` and nothing else authored loads as "no combo", which is the same
answer to `>= 0`. Not in the bake and not needed from it: the move probe's two traces
(`0x102e37b0`, `0x102e3450` — no port line cites either) and `debug_melee_npc_range`.

**Lanes.** Slot 331 → **V11-3** (`../v11/brief-V11-3-slot-331.md`): its stub is in
`ElysiumCombatCharacterSlots.cpp`, A3's file in V5a's wave, and its only records are V11's.
V5a-1 calls the virtual and keeps no stand-in. D1–D8, D10, D11 → **V11-2**
(`../v11/brief-V11-2-melee-contact.md`); **D9 → C1** (the sub-step's cycle is
`MeleeSwingUpdate`'s, which C1 ports).

**Waves** (cap 3): `[V5a-1, V5a-2, A3]` → `[A1, A2, A4]` → `[B1, B2]` → `[V11-1, V11-2, V11-3]`
→ V4o → `[C1, C2]`. V11 leaves V4b's wave: four lanes would pass the cap, and V11-2 holds B1's
`ElysiumWorldServices.h`, `ElysiumMapActor.h`, `ElysiumTestServices.h` for the swing query.
`spec.md`'s sequence line (`[V4b + V11]`) and V4b's three briefs are not amended here.
