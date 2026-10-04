# V4r — packet S2: weapons and attack data, settled before the work that depends on it

Reader S2, 2026-10-04, `spec-0002/step-2`. No code, no build, no run. **(V)** verified in the
listing or the decompilation this session; **(D)** taken from `docs/vtmb/` or packet R2, not
re-read; **(I)** inferred, with what it rests on. Doc written: `combat-and-damage.md`
§ "Weapon operator bodies, the shot's gates and the attack data (S2)" (new) and one correction in
§ "Contact is a per-frame swept walk…" (`ceil`, not `floor`).

## 1. Weapon operator bodies and the NPC fire paths (J6)

Slot 370 (`+0x5c8`) has **eight bodies in the whole image** (254 classes fill the slot; the
character classes hold an unrelated virtual there) (V):

| body | classes | what it takes |
|---|---|---|
| `0x1024f030` base | `CBaseCombatWeapon`, `CDisciplineWeapon`, `CWeaponUnarmed`, `CWeaponIThrown`, `CWeaponIWritten`, `CWeaponIArmor*`, `CWeaponHolyLight`, `CWeaponGravityGun`, `CWeaponPhysCannon`, `CWeaponThrown_Grenade_Frag`, `CWeaponThrown_Chang_Energy_Ball`, `CWeaponThrown_Chang_Ghost` | nothing: `DevWarning("Unhandled animation event…")` |
| `0x103f4470` | `CBaseMoneyObject` and every item / occult / book class, `CWeaponIGeneric`, `CWeaponLockpick`, `CWeaponWallet` | swallows 3014 and 3200; else the base |
| `0x10238160` | `CWeaponRanged` and **all 16 subclasses, the species ones included**: `…_FlameThrower`, `…_MingXiao_Spit`, `…_Tzimisce2Head`, both crossbows, pistols, rifles, SMGs | 4001 / 4002 bodygroup; 3030..3044 → `0x10238320` (mode slot 0), or `0x10238380` when the global mode slot `DAT_1088aee4 != 0` and the operator is a player (`+0xa8`) |
| `0x103ea5b0` | `CWeaponMelee` and 27 subclasses | 3001, 3030..3037, 3039..3044, 3047 → **if the operator is not a player** the weapon's slot 326 `PrimaryAttack`; 3003 swallowed; 4001 / 4002; 3038, 3045, 3046 → base |
| `0x103ec460`, `0x103eca20` | `CWeaponMelee_MingXiaoMelee`, `…_MingXiaoTentacle` | 3003 swallowed, else `0x103ea5b0` — the same behaviour as the melee body |
| `0x103e8be0` | `CWeaponMelee_TzimisceMelee` | 3003 swallowed; 3045 → `0x103e8c50`, 3046 → `0x103e8c90`, **NPC operator only**: weapon `+0x910 = 1` / `2`, slot 372 `RequestActivity(0x4b, 1, 0)`, `+0x910 = 0`; else `0x103ea5b0` |

- **No ranged species class has its own operator body or its own `Shot`** (V: `overridden_only`
  tables of `FlameThrower`, `MingXiao_Spit`, `Tzimisce2Head`, `Rifle_Crossbow`). The flamethrower
  overrides slots 326 `0x103e2ec0` and 372 `0x103e2f30` (the flame, ammo `+0x74c` decremented per
  call) — a start path, reached only from `PrimaryAttack`; its slot 373 is `CWeaponRanged::Shot`.
- **`0x103ed200` is not a species body**: it is the **player's grenade release** (`CWeaponThrown`,
  called from `0x103ee660`; first test `owner->+0xa8 != 0`, i.e. returns for an NPC) (V).
- **Whole-image NPC call sites of a weapon's fire virtuals** (V, grep of `+0x518` / `+0x51c` /
  `+0x5d0` / `+0x5d4`): slot 326/327 — `CAI_BaseNPCTroika::StartTask 0x102a1910` (the melee arm),
  `CNPC_VBach::StartTask 0x103645a0`, `CNPC_VManBat::RunTask 0x1038d130`, and the melee operator
  body above; slot 372 on a spawned projectile weapon — `CNPC_VChangBros::RunTask 0x1036bfc0` →
  `SpawnEnergyBall 0x1036dd20`, `CNPC_VFrenzyShadow::StartTask 0x10375f50`, `0x103f03e0`
  (`item_w_chang_ghost`); slot 373 — `ModeDispatch` only. `0x1029c4a0` is a false candidate (its
  receiver is not a weapon). `CWeaponRanged::Attack 0x10238580` (mode 0) does nothing for an NPC
  but stamp `m_flSoonestPrimaryAttack = curtime + Attack_Rate` and, on an empty clip, call slot 325.

**The lists C1 asked for.**

- *`ContactEventCycle` has NO retail counterpart* for an NPC wielder of **every** class: the ranged
  body (all 17 classes) fires from the event and only the event; the base body (thrown, unarmed,
  discipline) takes no event and has no weapon-side timer; melee commits by the sweep (item 9).
- *A non-event path exists* (the species' own class code, never the weapon's clock):
  `CNPC_VChangBros` (energy ball, a task), `CNPC_VFrenzyShadow` (a task), `CNPC_VBach` (slot 326
  from `StartTask`; grenades from his camper pass `0x10365a90`, ported), `CNPC_VManBat` (slot 326
  from `RunTask`), the Chang ghost (`0x103f03e0`). These are task bodies in their class files, not
  estimates.
- **So there is no "unread" operator body left.** Not walked: each species class's
  `HandleAnimEvent` arm by arm (they are ported, `ElysiumNpcMisc2Species.cpp`), and
  `0x10239f30` (the type-6 throw's launch call; the decompilation is truncated).

## 2. `ModeDispatch(1)` and `Shot`'s gates (V, listing `0x102387b0`, `0x102383b0`)

`0x10238320`: `DAT_1088aee4 = 0`, `ModeDispatch(1)`. `ModeDispatch`: weapon `+0x848 =
weapon[+0x84c + slot×4]` (the current activate-mode tag); slot other than 0 / 1 → return; mode
record `0x102517e0` (the record whose `+0x104` equals `+0x848`, three records, stride `0xec84`; no
match → a static default record with type 0); then on the record's **type `+0x108`**:

| type | event arm (`param == 1`) |
|---|---|
| 1, 2 | slot 373 `Shot` |
| 3 | `0x10239d80` zoom step (player only), next-attack times `= curtime + GetFireRate` |
| 4 | slot 374 `0x10239270` fire-mode toggle unless `+0x898`; times as 3; mode slot reset to 0 |
| 6 | `0x10239e70`: activity `0xb9`, launch `0x10239f30(owner)`, clip `−1`, empty → drop |
| other | `+0x730 += GetFireRate`, `+0x734 =` the same; nothing fired |

`Shot`, for an NPC, in order:

1. Zoomed (`weapon data +0x4fe84 > 0 && +0x914 > 0`): the mode record is re-read as tag 2.
2. **Owner** `0x10252240` null → return. `AddMiscFlag(0x200000)` on the owner.
3. **Not a player and `owner->+0x94` (the NPC pointer) null → return.** NPC: slot 333
   `(6, 1, 0, 0, 0, 0)` (player: `(1, 1, …)`).
4. Weapon data `+0x50170` → owner `m_fEffects |= 2`.
5. Shoot position: owner slot 389 (`+0x614`); direction: the NPC's slot 574 (`+0x8f8`)
   `(&out, &shootPos, 1, 0)`.
6. **The cooldown is a count, not a refusal**: `bulletSetsToFire (+0x918) = 0`; `rate` = slot 332
   `0x10254410` (= record `+0x260` `Attack_Rate` through the owner's `0x1033d940`);
   `next = max(+0x730[slot], curtime − frametime)`; `while (next <= curtime) { next += rate;
   ++sets; }` (with `m_iAtkMode +0x86c != 0` the rate is re-read each step). **An event that
   arrives while `m_flNextPrimaryAttack > curtime` fires zero bullets** — and still plays the
   weapon activity and inserts the sound.
7. **The clip caps, never refuses outright**: `n = sets × Ammo_Cost (+0x110)`; if `n > 0` and
   `clip = m_iMagazineCurAmts[ammo index +0x10c] (+0x74c)` `< n` → `sets = clip / Ammo_Cost`. An
   empty clip gives zero sets. **The NPC's clip is not decremented** (the subtraction is inside
   the player-only block `0x10238a1d`).
8. **No line-of-fire gate**: the one trace (2 048-unit, mask `0x46004003`) feeds a debug overlay
   and a discarded `GetFlags`.
9. Writes: per set, owner slot 185 (`+0x2e4`) `FireBullets` (shots `Ammo_Fired +0x114`, the mode's
   `CVDmg_t`, ammo type, tracer data); `CSoundEnt::InsertSound(1, owner origin, [0x1072bc40], 0.2)`;
   slot 339 `Kick` when the owner's slot 220 answered non-null; `+0x730[slot]` advanced.

*Unrecovered:* who fills `m_iMagazineCurAmts` on an NPC-held weapon (the indexed store is not in a
decompiled assignment; reload slot 325 `0x10239570` is the candidate) — so whether an NPC's clip
can reach 0 is open; `0x1033d940`'s scaling; the mode-type key's name (string `0x105402f8`).

## 3. The attack-rate keys (V both parsers)

Server `WeaponModeDataLoader 0x10259230` and client `0x101a3eb0`, identical, per mode record:

| key | offset | default |
|---|---|---|
| `Attack_Rate` | `+0x260` | `1.0` (`0x102593cd`) |
| `NPC_Attack_Rate_Min` | `+0x264` | `2 × Attack_Rate` as just parsed (`0x102593e5`) |
| `NPC_Attack_Rate_Max` | `+0x268` | `3.0 × Attack_Rate` (`[0x10449258]`) |
| `NPC_Attack_Rate_Base_Range` | `+0x26c` | `120.0` |

Neighbours: `Ammo_Type` index `+0x10c`, `Ammo_Cost` `+0x110`, `Ammo_Fired` `+0x114` (default
`max(Ammo_Cost, 1)`), `Range` `+0x270`. README §1's mapping stands.

Readers: `0x102c5730(weapon)` — `v = RandomFloat(+0x264, +0x268)`, then `0x102c5570(rec, v)`; one
caller, `CAI_BaseNPCTroika::StartTask 0x102a1910` (the wait, N2). `0x102c5780` / `0x102c57c0` pass
`+0x264` / `+0x268` themselves to `0x102c5570` (the burst pause, V5). `0x102c5570` (listing):
`BaseRange <= 0` → `(v − Attack_Rate) × 1.0`; else distance to `m_hShootTargetOverride +0x5ba8`,
else to the enemy's last known position (`GetEnemies()->0x102dfed0`), else **no target →
`(v − Attack_Rate) × sqrt(1.0 / BaseRange)`**; `dist <= 0` → `(v − Attack_Rate) × dist`; else
`(v − Attack_Rate) × sqrt(dist / BaseRange)`.

Port: `ElysiumItemTable.cpp:192` parses `Attack_Rate` with default **0.0**, not retail's 1.0, and
none of the three keys. The staged corpus carries them: 11 item files under
`Content/ElysiumCorpus/vdata/items/` (60 lines). No pipeline change.

## 4. The melee band `0x103ea7e0` (V, listing)

`(weapon; activity, target, dot, dist)`, callers `0x103eac30` (activity `0x4b`) and `0x103eac60`
(`0x4e`). `ready` = `+0x730 < curtime && +0x734 < curtime && owner m_flNextAttack (+0x1564) <
curtime`, then (target is a combat character) the target's slot 327. `dot > [0x104492d0] && target
CC && ready` and owner slot 331 `(weapon, target, activity, &out)` true with `out >= 0` → **`0x51`**.
Then over `GetSequencesForActivity(owner, translated activity, …)`, each sequence counted when
`(target CC || seqdesc+0x10 > 0) && seqdesc+0x2c4 > 0`: `lo = min(+0x2cc)`, `hi = max(+0x2d0)`,
`mean` = average over the `+0x2bc` records at `+0x2c0` (24-byte stride) of `(rec[0] + rec[3]) ×
[0x104454d0]`. No record counted → 0. `mean` clamped into `[lo, hi]`. Then: `dist > max(hi ×
[0x1049ae90], [0x1044ddb0])` → **9**; `dist > hi` → **`0x60`**; `dot < [0x104492d0]` → **`0x61`**;
`dist < lo` → **`0x5f`**; target CC and `ready`: `dist < mean × [0x10449260]` → `0x5f`, else
`0x60`; else 0.

**Every input is the model's sequence descriptor, none is an item key, and the bake carries all
of them** (V, `importers/clip_data.py:16-25`, `formats/mdl_skel.py`): `+0x10` → `weight`,
`+0x2c4` → the `swings` count, `+0x2cc` → `low_reach_cm`, `+0x2d0` → `reach_cm`, `+0x2bc/+0x2c0`
→ `envelopes` (min corner + max corner); runtime `FElysiumNpcClip` (`LowReachCm`, `ReachCm`,
`Envelopes`). No pipeline change. *Unrecovered:* the five constants' values (not in
`kernel_tunables.tsv`); owner slot 331's body.

## 5. `GetBestMeleeWeapon 0x10336f20`, `ChooseBestMeleeWeapon 0x10337230` (V)

Walk the type list at `0x10619eb4` (terminated by −1); for a type whose `0x10619d28[type] >= 0`:
`first = 0x10937cd0[type]`, `count = owner slot 298 (+0x4a8)(0x10619d28[type])`; the first
`GetWeapon(i)` in `[first, first + count)` whose slot 360 (`+0x5a0`) `& 0x18000` is returned; none
→ 0. `Choose…`: a weapon → owner slot 388 (`+0x610`) `Weapon_Switch(weapon, 0)`, true; else false.
Callers: `Choose…` — `CAI_BaseNPC::StartTask 0x102827f0`, `CNPC_VSheriffMan::StartTask 0x103aec70`;
`GetBest…` — `Choose…` and `CNPC_VMingXiao::Spawn 0x103927a0`. *Missing:* the three tables' contents
(`0x10619eb4`, `0x10619d28` in `.rdata`; `0x10937cd0` filled at run time) — the corpus tools print
no data bytes.

## 6. `0x102a11d0` (V, listing) — "a non-hated NPC stands in the way"

`(this NPC; const Vector& point)`, callers the four `SelectScheduleMeleeCombat` bodies
(`CNPC_VHuman 0x10385e40`, `CNPC_VChangBros 0x1036d800`, `CNPC_VMingXiao 0x10396050`,
`CNPC_VTzimisceRunner 0x103c4430`). Hull `mins = (2·mins.x, 2·mins.y, −6)`, `maxs = (2·maxs.x,
2·maxs.y, +6)` of the NPC's collision box; a swept ray from `WorldSpaceCenter` (slot 192) to the
point, `Ray.Init(start, end, mins, maxs, 1, 0)`, filter `(this, group 0)`, mask `0x2000000`
(`CONTENTS_MONSTER`). **True** when the trace was blocked (`fraction < 1` or `allsolid` or
`startsolid`), hit an entity with an NPC pointer (`+0x94`), and `IRelationType(hit)` (slot 404,
`+0x650`) `!= 1` (`D_HT`). Else false. It is not a reach test: the port's name
`ScheduleMeleeReachGate` is wrong.

## 7. Attack extents (J2)

- Slot 247 `0x10090c80`, whole (V): `Flags2 & 4` and a seqdesc; `e[i] = max(|bbmin[i]|,
  bbmax[i])`; `e.x = e.y = sqrt(e.x² + e.y²)`; `e[i] = e[i] > collision maxs[i] ? e[i] − maxs[i] :
  0`; entity slot 15.
- `SetAttackExtents 0x1009af40` (V): `0x100dc220` → the engine's `SpatialPartition001` slot 0
  `0x20040fc0`, which stores the vector in the partition element at `+0x28` (stride `0x38`); and
  `m_vecAttackExtents +0x50`.
- **Readers of `+0x50` (slot 16 `0x1009b030`) — saves only** (V, every `+0x40` dispatch on an
  entity): `0x102a1910` twice (the sleep arms) and `0x102b7110` (the cover-hint acquire: saves to
  `+0x65d0`, then `SetAbsoluteAttackExtents`).
- **Readers of the engine copy** (V): `CEnumBox 0x200426e0` and `CEnumRay 0x20042b70` grow the
  element's box by it on every axis **only when the query carries Troika's flag** — the box
  query's sixth argument, the ray's byte `+0x32` (`Ray.Init 0x1006dec0`'s last argument).
  Flagged queries in `vampire.dll`: `0x1040f550` and `0x1040f080`, the player's **acquire cone**
  (`physics-interaction.md` § "The acquire chain") — two sites in all. No `Ray.Init` call passes
  the flag on a readable line (one, `0x102fb4e0`, wraps; not read), and no decompiled store sets
  `+0x32`.
- **So the bbox decides which entities the player's use / acquire cone can pick, plus three
  saves. Not melee reach (item 4 reads the seqdesc reach), not the melee sweep (I:
  `MeleeSwingStep 0x10343020` not re-read), not any NPC condition.**

## 8. `0x1044f020`, `StudioFrameAdvance`'s early-out, slot 273

- `0x1044f020` is the **double `0.001`** (D `kernel_tunables.tsv` `FrameAdvanceMinInterval`; V the
  compare is `FCOMP double ptr`).
- `StudioFrameAdvance 0x1008f120` (V): `m_flPrevAnimTime (+0x170) == 0` → both times `= curtime`;
  argument `== [0x1044fab0]` → `0.1`; `dt = arg + curtime − m_flAnimTime (+0x174)`; **`dt <=
  0.001` → return `0.0f`, nothing else written**.
- **Slot 273 `RestartGesture 0x10099570` has no caller that reaches an NPC** (V for the count, I
  for the receivers): its thunk `0x10008233` is uncalled; 23 functions (thunks counted) dispatch at
  `+0x444` — all weapon-class bodies on a weapon (`Deploy`, `Precache`, `0x10252ea0`,
  `0x10258440`; the weapon's slot 273 is `0x10251de0`) except the `DoorActivate` bodies (no
  arguments), `0x100dac20`, `0x100eef50` and `0x10189780` (cast receivers, 0–2 arguments;
  `RestartGesture` takes three). Dead on step-2 paths.

## 9. `MeleeSwingUpdate 0x10346cd0` (V) under `UpdateCharacter 0x103246d0` (V)

`UpdateCharacter(interval)`: discipline visuals, slot 313, `UpdateVampHeal_HOT`, expressions (an
NPC only with slot 513 `& 0x800000`), eye direction, slot 314, **slot 315 with no argument**, the
render-fx timers. `MeleeSwingUpdate`:

- **Window**: no seqdesc or `seqdesc+0x2c4 < 1` → `m_bMeleeSwingIsLive (+0xaa1) = 0`, return,
  nothing stamped. Not yet live → `MeleeSwingStep(pos, angles, 0, 0)`, `ForceMeleeReset`, stamp.
- Live: **`dt = curtime − m_flLastMeleeSwingUpdate (+0xaa4)` — its own stamp, not the caller's
  interval**; `dt <= 0` → return unstamped. `rate = GetSequenceCycleRate × m_flPlaybackRate`;
  `c1 = (curtime − m_flAnimTime + 0.1) × rate + m_flCycle`; `c0 = c1 − rate × dt`. NPC: `m_flCycle
  >= melee_swing_completion_percent` → NPC `+0x6064 = 0`. Once: `SendIncomingSwingNotice`
  (`0x10346ac0`); once: when `IsMeleeSwingActive(offset cvar)`, weapon slot 333 `(0x15, 1, …)`.
- **`N = ceil(dt × 100)`** (`_ceil`, `[0x10450564]`), for `i = 1..N` while the previous cycle `<
  1`: `c = lerp(c0, c1, i/N)` clamped to 1; position and angles lerped from the stored last pose;
  `MeleeSwingStep(pos, ang, prev, c)`. Epilogue stamps time, position, angles.
- Trace shape, hit decision, damage: `MeleeSwingStep 0x10343020` → the weapon's traced-impact
  virtual `+0x438` `0x102579f0` (D, not re-read).
- **Place** (D, R2): NPC — the Troika think tail `0x1029365b`, after `PostRun` and
  `PerformMovement`, on thinks where the update clock is due; the sweep covers the whole gap
  because `dt` is its own. Player — `PostThink 0x1016be10`: advance, slot 258, slot 312.

## Changes to the plan

- **J6 ruling / `v4/brief-C1-attack-producers.md` item 2(a).** The "unread operator body" seam is
  **dropped**: there is none. `ContactEventCycle` is removed for every NPC wielder — ranged body:
  the event; base body (thrown / unarmed): nothing weapon-side, the species task bodies are the
  retail path. `UsesUnreadOperatorEstimate()` is not written; the report's "silent-class list,
  first half" is empty. The player's estimate is still untouched.
- **C1 item 3.** (i) "The interval the sweep integrates is the update clock's" → **the sweep's own
  stamp `+0xaa4`**; slot 315 takes no argument. (ii) The melee operator's NPC trigger set is 3001,
  3030..3037, 3039..3044, 3047 → `PrimaryAttack` (today only 3047 is claimed, with no consumer):
  `IsMeleeSwingTrigger` takes the retail set and calls the swing start for a non-player operator.
  (iii) `CWeaponMelee_TzimisceMelee`'s 3045 / 3046 (`+0x910 = 1 / 2`, slot 372) is a named seam
  unless the Tzimisce weapon is in scope.
- **C1 item 4.** Port the body as item 7 states it. **J2's cost/benefit changes — a judge item**:
  nothing in NPC AI reads the extents; the consumer is the player's acquire cone and three saves.
  The pipeline half (`bboxMinCm` / `bboxMaxCm`, one body-data import) buys only that. The judge
  may keep it or move the data half to the player story.
- **`v4o/brief-O3-weapon-event-shot.md`.** Item 1's answer is §2 above; port exactly: owner, NPC
  pointer, the cooldown count, the clip cap, no decrement, no line-of-fire test, the sound and
  activity even at zero sets. **Test 4 changes**: "a second 3031 → a second shot" holds only once
  `m_flNextPrimaryAttack <= curtime`; a second 3031 inside the cooldown commits zero bullets. The
  mode type must be 1 or 2. J6's "key on the operator body" stays, now with one ranged body.
- **`v5a/brief-V5a-2.md`.** Mapping and both parsers confirmed; no stop. Added: the defaults
  chain (`Min = 2 × Attack_Rate`, `Max = 3 × Attack_Rate`, on a retail default `Attack_Rate` of
  1.0 while the port parses 0.0 — **the coder derives the two defaults from retail's 1.0 when the
  key is absent and leaves `AttackRate`'s own default alone**, naming the divergence for the
  integrator, since `ElysiumWeaponClasses.cpp` reads 0 as "absent"); the no-target arm of
  `0x102c5570` is `sqrt(1 / BaseRange)`, and `dist <= 0` multiplies by `dist` — check
  `ScaleWeaponBurstPause` against §3.
- **`v11/brief-V11-1.md`.** Item 11: the bodies are §5; the port can answer it only if its
  inventory has retail's per-type slot ranges — the three tables' contents are missing, so
  **the seam stays** unless the inventory already orders weapons that way; report which. "Not
  yours: `ScheduleMeleeReachGate 0x102a11d0` (R3 / R4)": the body is now §6 — whoever owns it
  ports a swept-slab NPC test and renames it; it needs an entity sweep against NPC bodies in the
  substrate (a judge item if absent).
- **`ElysiumNpcDamage3.cpp`'s seam for `seqdesc+0x2d0`** (no brief): the value is the baked
  `ReachCm`; a one-line fill for V5.
- **No pipeline change and no re-bake is required by S2.** The only pipeline work in the plan
  remains J2's bbox, now with a smaller benefit.

## Unrecovered, and what is missing

- The NPC weapon's clip fill (who writes `+0x74c[type]` at spawn / equip / reload).
- The three `GetBestMeleeWeapon` tables; the melee band's five constants; the mode-type key name;
  `0x10239f30`'s launch; `0x1033d940`; owner slot 331; `0x102fb4e0`'s `Ray.Init` flag.
- Queries over 10 s: none. Two lookups returned oversized replies (`vtmb_readers +0x264`,
  `vtmb_where HandleAnimEvent`): a field ledger on a small offset and a bare method name are not
  usable queries.
