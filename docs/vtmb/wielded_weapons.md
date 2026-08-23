# Wielded weapons

The geometry a character holds, how an item names it, how equip binds it to a body, and how the
engine poses it. This document owns the wielded-weapon system end to end: the four item model
roles, the equip/detach transaction, the follow-attach composition, and the shipped corpus.

Neighbouring owners: the skeletal pose frame and animation-channel decode are
`docs/vtmb/animation_and_movers.md`; item records, acquisition and drop are
`docs/vtmb/inventory.md`; the first/third-person split is `docs/vtmb/camera-view-modes.md`; the
MDL container is `docs/vtmb/mdl_v2531.md`; NPC authored keyfields are
`docs/vtmb/npc-ai-reverse-engineering.md`.

Addresses are `vampire.dll` (server) and `client.dll` unless stated, both at image base
`0x10000000`.

## 1. An item names four model roles

`vdata/items/*.txt` carries four model keys, and they are not interchangeable. The install's
directory convention spells the roles apart: `view/v_`, `world/g_`, `wield/w_`, `info/i_`.

| Key | Role | Item-definition offset | Buffer |
|---|---|---|---|
| `viewmodel` | first-person geometry | — | — |
| `playermodel` | the loose ground model | — | — |
| `wieldmodel_f` | held geometry, female wielder | `+0x2288` | `char[0x80]` |
| `wieldmodel_m` | held geometry, male wielder | `+0x2308` | `char[0x80]` |
| `infomodel` | inventory/UI geometry | `+0x2388` | `char[0x80]` |
| `anim_prefix` | animation extension, unread at run time (§5) | `+0x2408` | `char[0x10]` |

`playermodel` is the loose ground model and is **not** what a character holds. For `item_w_katana`
the four are `weapons/w_null.mdl`, `weapons/katana/world/g_katana.mdl`,
`weapons/katana/wield/w_{m,f}_katana.mdl` and `weapons/katana/info/i_katana.mdl`.

`FUN_10259f80` is the single item-definition loader; each key is one `KeyValues::GetString`
followed by a `Q_strncpy` into the offset above. `FUN_1025b930` is the item-definition copy
constructor and walks the same offsets in declaration order, which independently corroborates the
layout. The struct spans to at least `0x5f320`.

### `w_null.mdl` is an authored value

`models/w_null.mdl` and `models/weapons/w_null.mdl` are real shipped, precached models with zero
bones and zero vertices. An item with no wielded geometry names one of them; it is not a missing
model and not a sentinel. Of 244 item definitions, 192 name a wield model for both sexes and 52
name neither — no definition authors one sex without the other. Of the resulting 384
`(classname, sex)` rows, **296 name a null model** (210 `models/w_null.mdl`, 86
`models/weapons/w_null.mdl`).

Because equip applies the named string unconditionally (§2), the null model is what makes the
system branchless: every key, every armour piece and every discipline runs the same code path as a
katana and receives geometry that draws nothing.

An **empty** string is a different value from `w_null.mdl`. `CWeaponMelee::Deploy` (`0x103ea510`,
§2) re-applies the gendered wield model on every deploy but guards that apply with
`if (*string != '\0')`, so an empty value there means "leave the current model alone". Equip
carries no such guard.

### Two models the install lacks

`models/error.mdl` (`weapon_pistol`, `weapon_pistol-null`) and
`models/weapons/throwing_star/ground/g_throwing_star.mdl` (`item_w_throwing_star`,
`item_w_throwing_star-null`) resolve to no file, loose or VPK. `models/error.mdl` is a path defect
rather than a content gap: `pack002.vpk` ships `models/error/error.mdl` one directory level away,
with the standard placeholder trio `error`/`missing`/`version`. The throwing star is unfinished
content by Troika's own note in the item's `description` field.

### The `-null` classname suffix is a duplicate registration

Eighteen classnames occur as a base/`-null` pair. Thirteen pairs are byte-identical; the remaining
five differ only cosmetically (flavour text and worth on `item_g_chewinggum`, a typo fix on
`item_g_ring_serial_killer_2`, `activation.criminallevel` on `item_w_throwing_star` and
`item_w_wolf_head`). **The model keys are identical between base and `-null` in every pair.** The
suffix does not mark a nulled variant. *Uncertain:* the engine's reason for maintaining two
registrations is unrecovered; the item-lookup path would show it.

### Repeated keys resolve last-wins

A key may occur twice in one definition. `anim_prefix` repeats on `item_g_chewinggum-null` and
`item_w_mingxiao_tentacle` (identical values); `is_wieldable` on `item_g_astrolite` and both
`item_g_ring_serial_killer_2` rows (identical); `activation` on 30 files (intentional multi-mode
weapons). Two repeat with **differing** values: `impact_snd_group` on `item_w_crossbow`,
`item_w_mingxiao_spit` and `item_w_tzimisce2_head` (`''` then `'bullet'`), and
`item_w_flamethrower`'s `Activation` block repeating `kickpitchmin`/`kickpitchmax`/`kickyawmin`/
`kickyawmax`/`kicktime`. Scalar leaf reads resolve to the **last** occurrence.

### `shows_view_model` selects the wield model over the world model

Equip tests item-definition `+0x2448` before selecting a wield model. When it is zero the gender
branch is skipped entirely and `GetWorldModel()` (`+0x55c`) supplies the model instead. The field is
`shows_view_model`, written by `FUN_10259f80` at `1025a890`:

```
PUSH 0x1                    ; default
PUSH 0x105c800c             ; "shows_view_model"
CALL [EAX + 0x28]           ; KeyValues::GetInt(name, default)
MOV  [EBX + 0x2448],EAX
```

**The default is `1`, supplied by the `GetInt` call itself.** The key is therefore a sparse opt-out
rather than a densely authored gate: 219 of 244 definitions never name it and inherit `1`.

| | names both wield models | names neither | total |
|---|---|---|---|
| gate on — unauthored, or explicit `1` | 170 | 52 | 222 |
| gate off — explicit `0` | 22 | 0 | 22 |
| | **192** | **52** | **244** |

The 22 opt-outs are the classes that render no held prop: the seven armour rows, the disciplines
`item_d_animalism`/`_dementation`/`_dominate` with their `-null` twins, `item_s_physicshand`,
`item_w_unarmed`, `weapon_physcannon` and `weapon_physgun`. Every one of them still names a wield
model, and every one names `models/weapons/w_null.mdl`, so both branches draw nothing — the gate
restates the authored intent rather than producing a distinct outcome. Three definitions author `1`
explicitly (`item_d_thaumaturgy` and its `-null`, `item_w_tzimisce2_head`), redundant with the
default.

`FUN_10256220` reads the same field independently of equip — `return *(int *)(defn + 0x2448) != 0` —
and is called throughout the ranged and melee reload state machine.

A third consumer is the first-person hands. `FUN_10182e00`, which resolves the hands viewmodel from
the wearer's clan and sex (`docs/vtmb/animation_and_movers.md`), tests `shows_view_model` and then
`hides_hands_model` before anything else: either a cleared `+0x2448` or a non-zero `+0x244c` on the
active weapon clears the hands entity's model outright and returns. `hides_hands_model` has no other
reader.

### The keys either side of the model block

`FUN_1025b930` walks the struct in declaration order and brackets the model keys:

| Offset | Type | Key |
|---|---|---|
| `+0x2408` | `char[0x10]` | `anim_prefix` (§5) |
| `+0x2418` | byte | `impact_snd_group`, resolved through a helper rather than stored raw |
| `+0x241c` | int | `bucket` |
| `+0x2420` | int | `bucket_position` |
| `+0x2424` | int | `weight` |
| `+0x2428` | int | `item_flags`, default 8 |
| `+0x242c` | bitmask | `BitFlag_CantBeLast` `0x1`, `BitFlag_Discipline_Tgt` `0x2`, `reload_single` `0x8` |
| `+0x2430`–`+0x243c` | float ×4 | `ZoomSwayDeltaMagnitudeMin`/`Max`, `ZoomSwayTimerMin`/`Max`, defaults 0.6, 3.0, 0.5, 2.0 |
| `+0x2440` | enum | `camera_class` |
| `+0x2444` | int | `is_visible_in_hud`, default 1 |
| `+0x2448` | int | `shows_view_model`, default 1 |
| `+0x244c` | int | `hides_hands_model`, default 0 |
| `+0x2450` | `char[0x84]` | `item_type` |

`+0x242c` is synthesized rather than parsed: the loader zero-initialises it and ORs one bit per key,
so no single `KeyValues` read owns the field. Bit `0x4` is never set. Authored counts are
`BitFlag_CantBeLast` 17, `BitFlag_Discipline_Tgt` 8, `reload_single` 2.

`camera_class` is a string compared into an enum — `ranged`→`0x2`, `thrown`→`0x4`,
`force_1st`→`0x8`, `melee`→`0x10`, `force_3rd`→`0x10`, anything else→`0x0`. **`melee` and
`force_3rd` resolve to the same value** and are indistinguishable downstream. Across 244
definitions: 121 `noswitch`, 70 unauthored, 21 `ranged`, 21 `melee`, 6 `force_1st`, 5 `thrown`;
`noswitch` and absence both fall through to `0x0`, and no definition ships `force_3rd`. Client
`FUN_1009c250` dispatches on `0x8` and `0x10`.

## 2. Equip and detach

### Precache resolves both sexes

`CBaseCombatWeapon::Precache` (`FUN_10250ed0`) reads the item definition's two wield-model strings
through per-class virtuals `+0x440` (`GetWieldModelF`) and `+0x444` (`GetWieldModelM`), precaches
each through the engine interface, and stores the resulting model indices on the weapon entity:

| Field | Server offset | Client offset |
|---|---|---|
| `m_iWieldModelFIndex` | `+0x76c` | `+0x7cc` |
| `m_iWieldModelMIndex` | `+0x770` | `+0x7d0` |
| `m_iTargetModelIndex` | `+0x774` | `+0x7d4` |
| `m_iViewModelIndex` | `+0x89c` | `+0x920` |
| `m_iWorldModelIndex` | `+0x8a0` | `+0x924` |
| `m_hOwner` | `+0x88c` | `+0x910` |
| `m_iState` | `+0x8b0` | `+0x934` |

Server and client classes are compiled separately and their offsets differ; the client values come
from the `DT_BaseCombatWeapon` receive-table builder `FUN_1007d1c0`.

**Both sexes are precached and networked, and neither index is read back.** A complete
field-reference sweep of `+0x76c` and `+0x770` across `vampire.dll` finds exactly one touch of
each — the Precache write. The indices exist so both models are resident and replicated; the model
actually applied is selected from the live strings instead.

### Equip selects by sex and applies a string

`FUN_10252ea0` (equip, weapon vtable slot `+0x468`) ends in the selection:

```
TEST AL,AL                    ; the +0x2448 gate (§1)
JZ   <world-model branch>
MOV  ECX,EDI                  ; EDI = the owner argument
CALL CBaseCombatCharacter::IsMale
TEST AL,AL
JNZ  <male>
CALL [vtable+0x440]           ; female: GetWieldModelF()
JMP  <apply>
CALL [vtable+0x444]           ; male:   GetWieldModelM()
JMP  <apply>
CALL [vtable+0x55c]           ; gate clear: GetWorldModel()
<apply>:
PUSH EAX                      ; the model string, unconditionally
CALL [vtable+0x1a4]           ; SetModel(string)
```

`IsMale` is called on the owner argument passed into equip, before that value is stored into
`m_hOwner`. `SetModel` receives the raw **string**, not a precached index, and is called
unconditionally — equip contains no empty-string guard.

### The binding is a movetype plus a parent

Equip and detach (`FUN_10252a90`, slot `+0x4ac`) are a symmetric pair on the same two virtuals:

| | Equip | Detach |
|---|---|---|
| `+0x174` — `(int, int)` setter, movetype-shaped | `(0xb, 0)` | `(6, 0)` |
| `+0x328` — `(entity*)` setter, parent-shaped | `(owner)` | `(NULL)` |
| `m_hOwner` | set from the owner handle | cleared to `0xffffffff` |

*Uncertain:* the literal enumerators for `0xb` and `6`. No string in `vampire.dll` or `engine.dll`
names them, and the reference SDK trees on hand postdate `MOVETYPE_FOLLOW`'s removal from
`MoveType_t` — one carries the comment *"This is needed due to the removal of `MOVETYPE_FOLLOW`"*
beside the `EF_BONEMERGE` replacement, confirming the enumerator existed without recording its
value. By call-site role `0xb` is the movetype used while wielded and `6` the movetype used while
detached. **What would close it:** a 2003–2004-era `movetype_t` header.

`CBaseEntity::FollowEntity` and `InputFollowEntity` have zero cross-references in `vampire.dll`,
and no receive table in `client.dll` carries a moveparent, aiment or follow property for a weapon
class — the only `"moveparent"` property in the binary belongs to `DT_Beam`. The binding is the
movetype and parent setters above, which is why those names are absent.

### There is no separate holster path

Detach fully releases: `m_hOwner` cleared, movetype changed, parent nulled. Every non-trivial
virtual in the seventeen slots between equip (`+0x468`) and detach (`+0x4ac`) is a trace helper, a
handle resolver, a jump-table forwarder or a no-op stub; none touches `m_hOwner`, `+0x174`,
`+0x328` or the wield-model accessors. No virtual implements "stay owned and parented, stop being
visually active".

An active-weapon switch does not detach the outgoing weapon. `CBaseCombatCharacter::Weapon_Switch`
(`0x1032dde0`) and `Weapon_Equip` (`0x1032d380`) both call the outgoing weapon's own `Holster`
(below); detach stays the release path, holster is the state change.

### Deploy and holster are weapon-side, and the busy timer is the character's

`Deploy` is vtable slot 315 (`+0x4ec`) and `Holster` slot 316 (`+0x4f0`) [static-verified].
`CBaseCombatWeapon::Deploy` (`0x10253c50`) commits directly. `CWeaponMelee::Deploy` (`0x103ea510`)
first **re-applies the gendered wield model** — the same `IsMale` → `+0x440`/`+0x444` →
`SetModel` sequence equip runs, guarded here by `if (*string != '\0')` (§1);
`CWeaponRanged::Deploy` (`0x102395c0`) runs that re-apply unguarded and picks its activity by
attack mode — `ACT_VM_DRAW2` (`0xe1`) when `m_iAtkMode` is non-zero, `ACT_VM_DRAW` (`0xb4`)
otherwise. All three commit through `0x10253b70`, which refuses only on an
ammunition/readiness test — there is no "already busy" gate — then stores the activity in
`m_iTakeOutActivity` (`+0x854`), where the weapon's idle think consumes it on the next tick
(`docs/vtmb/animation_and_movers.md`), and writes the item's `anim_prefix` into
`CBasePlayer::m_szAnimExtension` through a pointer at owner `+0xa8` (§5). That pointer's absence
is what selects the constant-timer fallback below; `m_iTakeOutActivity` is then `-1` and no
activity is committed at all.

`Holster` (`0x10253ca0`, shared unmodified by melee; `CWeaponRanged`'s `0x10239d10` cancels zoom
first) clears `m_bInReload`, cancels the weapon's pending `Think`, unwields and hides the world
model. **It plays `ACT_VM_LOWER` (`0xde`) only when its caller passes a non-null second argument**:
`Weapon_Equip` passes `0`, so a forced replace holsters silently, while `Weapon_Switch` passes the
incoming weapon, so the lower animation is specifically the player-driven weapon-to-weapon switch.

Both ends write the character-level `m_flNextAttack` from `SequenceDuration` of the clip actually
selected, falling back to a constant only on the absence branch above — the timing is the
sequence's, not an authored item field. `CBasePlayer::ItemPostFrame` (`0x10174ce0`) routes to the
weapon's `ItemBusyFrame` (slot 320) while that timer is live, and `ItemBusyFrame` reads no fire or
reload input at all, so attack and reload are unreachable for the duration of a draw or holster.

A switch issued mid-draw or mid-reload aborts rather than queues: nothing in the select path
(`0x101772b0`, gated on `m_bCanSwitchWeapons`) or in `Weapon_CanSwitchTo` (`0x1032db60`) consults
`m_flNextAttack` or `m_iTakeOutActivity`, and `Holster` then zeroes `m_bInReload`, kills the pending
reload-completion `Think` and overwrites the timer. [inferred] **What would close it:** the
key-bound console-command layer feeding `0x101772b0`, checked for a further gate.

## 3. Composition — a per-bone name-matched copy

The wield model is its own entity with its own studio model. It stays with the wielder because
`C_BaseAnimating::BuildTransformations` (`FUN_1008fd00`) copies bone transforms out of the
follow-parent by name.

`FUN_100921d0` resolves the follow relationship: it reads a handle at `this+0x188`, requires the
resolved parent's model type to equal `3` (`mod_studio`), and returns the parent's model context.
Its two diagnostics — `"mod_studio: MOVETYPE_FOLLOW with no model.\n"` and
`"Attached %s (mod_studio) to %s (%d)\n"` — are the engine's own vocabulary for the mechanism.

Once a parent resolves, the per-bone loop is:

```
for each bone of this model:
    for each bone of the parent's StudioBone[] (count @0xf0, index @0xf4, stride 160):
        if __strcmpi(childBoneName, parentBoneName) == 0:
            copy the parent's 3x4 world matrix (FUN_10108430, 12 dwords)
            skip this bone's own FK
            break
    otherwise: boneToWorld[i] = boneToWorld[parent[i]] * localPose[i]
```

`localPose` is **this entity's own decoded pose**, not its bind pose: a follow-attached weapon still
evaluates its own `m_nSequence`/`m_flCycle` through the ordinary `SetupBones` path, and only the
matched bones are taken away from it.

`DrawModel` (`FUN_10092540`) carries the matching draw-time gate through the same resolver.

Three consequences follow, and they explain the whole corpus:

- **A matched bone is driven entirely by the wearer.** The wield model's own bind for that bone
  never reaches the frame; only its inverse bind does, through skinning.
- **An unmatched bone is FK off its nearest matched ancestor, driven by the weapon's own clip.**
  A weapon bone parented under `Bip01 R Hand` rides the hand whether or not the wearer declares it,
  but its local transform each frame comes from the weapon entity's own evaluated sequence. The
  weapon is only rigid where that sequence holds still.
- **The shipped sequences are near-static, but they are not the bind pose.** Every wield model
  except the two `handleclaws` files carries exactly one local sequence and no `$include` — `idle01`
  with activity `ACT_VM_IDLE` for the ordinary ones, `lockpick` for the two lockpick files,
  `useless` for the two `holylight` files. Most run 60–75 frames. Measured per frame on the bones
  below the mount, of the 61 models whose skin lies below a single matched bone: **46 hold the bind
  pose, 11 hold a constant pose that differs from bind, and 4 vary across frames.** The offsets in
  the middle group are not small — `Bone13` on `w_m_crossbow` sits 12.43 in from its bind,
  `w_f_flamethrower` 30.5°, both `tireiron` files 17.8°. **The clip, not the container's bind pose,
  is what the frame path uses**, because the previous bullet's local FK reads the evaluated
  sequence.

  The four that vary do so marginally, and only on the **male** files — every female counterpart
  measures 0.0000 in / ~0.000°: `w_m_flamethrower`'s `trigger` 0.3203 in / 1.253°,
  `w_m_lockpick`'s `lockpick` 0.0313 in / 6.357°, `w_m_rifle_steyraug`'s `Object01` 0.0508 in /
  0.446°, and `w_f_rifle_steyraug`'s `body` 0.0039 in / 0.002° at the quantisation floor.
  `w_m_flamethrower`'s movers (`trigger`, `switch`) carry **zero skin weight**, so their motion
  moves no vertices; the corpus's only *visible* own-motion is `w_m_lockpick`'s pick wiggling
  6.357° through its `lockpick` sequence.

`w_{m,f}_handleclaws.mdl` is the exception: 30 of its own `*_CHANG_CLAW` sequences plus `$include`s
on `models/character/shared/male/pcidles_allsequences.mdl` and `frenzy.mdl`. Only 6 of 485
character models declare all 30 of its skinned bone names — the four Chang brothers
(`chang1`–`chang4.mdl`) and Grünfeld Bach's two bodies (`bach.mdl`, `buch.mdl`) — so on any other
wearer most of its skeleton fails the match and runs on those clips. Its one shipped wearer,
`chang2.mdl`, declares all 30, so in the shipped game every skinned bone is overwritten by the
wearer. It is nonetheless an ordinarily equipped weapon (§7), not a body.

## 4. First person suppresses submission only

The camera gate on a held weapon's draw submission is owned by
`docs/vtmb/camera-view-modes.md` → "World weapon / attachments" (`FUN_100a7a50`, `FUN_100aef40`,
resolving `CAM_IsThirdPerson` through `CInput` vtable slot `+0x74`).

Two consequences for this system. The weapon is **not** hidden, detached or swapped to a null model
when the camera goes first-person — only its submission is suppressed, so equip state and the
follow binding are untouched by view mode. And an NPC-owned weapon is never camera-gated: its
visibility mirrors the owner's own flag byte, so a cast member's drawn weapon is unaffected by where
the player's camera is.

## 5. `anim_prefix` is the animation extension, and it is dead

`GetAnimPrefix()` (`FUN_10251e20`) returns item-definition `+0x2408` and occupies weapon vtable
slot `+0x560`, unoverridden across all 169 subclasses. Its only caller is `FUN_10253b70`, the
shared deploy commit (§2), which every `Deploy` funnels into. That function's shape is Source's
`DefaultDeploy(char *szViewModel, char *szWeaponModel, int iActivity, char *szAnimExt)` and the
prefix is `szAnimExt`:

```c
prefix = vtable[0x560]();      // GetAnimPrefix()          — no arguments
world  = vtable[0x55c]();      // GetWorldModel()          — no arguments
view   = vtable[0x558](0);     // GetViewModel(index)      — one argument
         FUN_10253b70(this, view, world, iActivity, prefix);   // RET 0x10 — four arguments
```

`FUN_10253b70` stores `iActivity` at `this+0x854` (`m_iTakeOutActivity`) and hands `szAnimExt` to
`FUN_10170e80`, which is `Q_strncpy(owner + 0x20a9, szAnimExt, 32)` — `CBasePlayer::m_szAnimExtension`.

**Nothing reads it.** A full field-reference sweep of `+0x20a9` finds exactly two touches beyond
that write, both `CBasePlayer::vfunc31` — the datamap debug dump. The field is an HL2 vestige: in
that lineage the extension is pasted into a sequence name, and VtMB replaced the mechanism with the
compiled per-weapon activity tables (`docs/vtmb/animation_and_movers.md` → "Activity translation is
a second compiled-data layer") while leaving the string plumbing in place. `anim_prefix` selects
nothing at run time.

That is why the shipped values tolerate being wrong. Across 244 definitions there are 35 distinct
values; 157 author a single space `" "` and 12 the empty string, and several of the rest name a
family that is not the item: `item_w_fireaxe`→`sledgehammer`, `item_w_torch`→`tireiron`,
`item_w_severed_arm`→`baseballbat`, `item_w_flamethrower`→`anaconda`,
`item_w_crossbow_flaming`→`m37`, `item_g_wallet` and `item_m_wallet`→`none`, the three disciplines
`item_d_animalism`/`_dementation`/`_dominate`→`uzi`, and `item_i_written` — a readable note —
→`pistol`. No guard precedes the call, so empty and single-space flow through identically.

The weapon-family key that *does* drive third-person animation is the per-class activity table at
weapon virtuals `+0x5a8`/`+0x5ac`, consumed by `CBaseCombatWeapon::ActivityOverride` (`+0x5a4`) and
reached from `CBaseCombatCharacter::Weapon_TranslateActivity` — owned by
`docs/vtmb/animation_and_movers.md`. It is compiled data keyed by C++ class, not by this string.

## 6. The shipped corpus

384 `(classname, sex)` rows resolve to 71 distinct model paths: 2 null models, 2 absent paths, and
**67 real models** carrying geometry over 80 rows.

### Every wield model is an arm chain plus a weapon sub-rig

The shared prefix is `Bip01 → Bip01 Pelvis → Bip01 Spine → Bip01 Spine1 → Bip01 Spine2 →
Bip01 Neck → Bip01 <L|R> Clavicle → UpperArm → Forearm → <L|R> Hand`, and the weapon's own bones
hang from a single child of that hand. Melee is the degenerate case where the sub-rig is one leaf;
firearms carry 2–13 sub-rig bones (`body`/`slide`/`mag`, `stock`/`bolt`,
`shotgun`/`pump`/`reload`/`clip`/`trigger`/`flash`/`shelleject`, the crossbow's two limb chains).

**The mount bone is the unique skinned root under a hand**, and it is read from the model rather
than assumed: `w_m_m37` and `w_m_submachine_mac10` park unskinned authoring leftovers beside the
real mount, and `w_f_m37` and `w_f_dragonbreath` hang the same leftovers from a second root outside
the Biped chain entirely.

### A weapon's moving part exists on both models and animates on only one

The wield model keeps the viewmodel's articulation and cannot use it. `w_m_m37.mdl` is the clearest
case, and the shotgun's two-piece split is visible in the skin weights on both sides:

| | `v_m37.mdl` (first person) | `w_m_m37.mdl` (held) |
|---|---|---|
| bones | 13 | 14 |
| sequences | **13** — `idle01`, `fire01`, `dryfire01`, `reload_begin`/`reload`/`reload_complete`, `draw01`, `lower01`, `fidget01`, … | **1** — `idle01`, `ACT_VM_IDLE`, 10 fps, 60 frames |
| receiver | `Dummy01`, 762 influences | `Box02`, 832 influences |
| **pump** | `Stock01`, **117** | `Box01`, **117**, child of `Box02` |
| shells | `Dummy11` 66, `Cylinder01` 66, `Box01` 70 | absent |

The 117-influence piece is the same authored mesh on both files. In first person its bone is driven
by the ten firing/reloading clips; in the wield model the only clip that exists is `idle01`, and
nothing ever sends the wield entity a different sequence — `ACT_VM_*` goes to the viewmodel, and the
weapon-suffixed body clips the wearer resolves (`m37_attack`, `m37_reload` and the rest, owned by
`docs/vtmb/animation_and_movers.md`) move only the wearer's arms.
**The held M37 is two rigid segments riding `Bip01 R Hand`; its pump never cycles in third person.**
The unskinned `stock` → `pump handle` leftovers are the viewmodel rig's names surviving in the wield
file, carrying no geometry.

The mount bones are `Box02` (whole gun) and its child `Box01`, and **no character model declares
either under a hand**: across all 485 character models `Box01` occurs only on `swat.mdl`/`swat2`/
`swat3` — parented to `Bip01 Pelvis` — and `Box02` only on `bomb_guy.mdl`, under `Bip01 R Finger1`
(§"`Box01` and `Box02` are vestigial"). So on every real wearer both fail the name match and FK off
the hand, which is what makes the gun rigid; on a `swat` body the pump would instead be yanked to
the pelvis, and no placement pairs those bodies with this weapon.

Six models have no single skinned root under one hand: `w_{m,f}_claws.mdl` (44 bones, skinned to ten
fingertips across both hands), `w_{m,f}_handleclaws.mdl` (40 bones, 30 skinned),
`models/scenery/misc/changball/changball.mdl` (2 bones, no Biped) and
`models/scenery/misc/gio_spirit/gio_spirit.mdl` (46-bone rig, 43 skinned). The first four are still
ordinarily equipped weapons; the last two are thrown.

Grip side is derivable from which hand the skinned chain descends from: `bushhook`, `sledgehammer`
and `fire_axe` are left-handed, `lockpick` is left-handed, every other conforming model is
right-handed.

### The corpus divides into three binding cases

Measured over all 67 real models — skin weights, bone parentage, and per-frame local motion of every
bone below the mount:

| Case | Models | What the wearer supplies | What the model supplies |
|---|---|---|---|
| all skin at or below one bone the wearer declares | **61** | the entire visible transform | a constant pose — 57 of them; 4 vary marginally |
| skinned to bones nearly every wearer declares | **2** — `w_{m,f}_claws` | almost every bone | a constant pose |
| skinned to bones most wearers lack | **2** — `w_{m,f}_handleclaws` | the matched bones only | 30 live clips on the remainder |
| not held; thrown | **2** — `changball`, `gio_spirit` | — | its own clip, free of any wearer |

Of the 61, **41 mount a bone no character declares** and **20 mount a prop bone**. Either way every
skinned bone lies at or below a single bone the wearer does declare, so the whole mesh resolves to
one transform on that bone — constant for 57 of them, and the pose it takes is the clip's rather
than the container's bind. Multi-influence vertices exist — 132 on each crossbow, 64 on each
`severed_arm` — but their influences are all inside that same subtree, so they blend between bones
that share a transform.

`w_{m,f}_claws.mdl` skins to ten fingertips, and 382 of 485 character models declare every one of
them. `w_{m,f}_handleclaws.mdl` skins to 30 bones and only 6 do.

`item_w_chang_energy_ball` and `item_w_chang_ghost` name `changball` and `gio_spirit`, and both items
are the stock frag-grenade template with the art swapped: `camera_class "thrown"`, `throwing_weapon`,
and a `Magazine { Type "FragGrenade" }` block, with the original pineapple lines still present as
comments in `item_w_chang_energy_ball`. `changball`'s one skinned bone `polySurface50` matches no
character skeleton. `gio_spirit.mdl` is separately placed as ten `prop_haunted` entities
(`spirit01`–`spirit10`) beside the Chang coffins in `sp_giovanni_5` — art reuse, not a second
binding path.

### The wearer declares seven prop bones

A character body declares `Bat`, `bush hook`, `handle`, `gerber`, `Sledgehammer`, `Cylinder01` and
`tire iron` under its hands with zero skin weight — 209–337 of the 485 character models carry them,
and **no character skeleton declares any other wield-model mount bone**. A firearm's mount
(`body`, `stock`, `shotgun`, `frame`, `uzi`, `polySurf01`, `Boom`, `flamethrower`,
`SeveredArmBone01`, `baton`, `Fire_axe`, `lockpick`, `Sheriff Sword`) matches nothing anywhere and
therefore rides the hand by FK (§3).

The prop bones exist because the shared banks animate them. Idle, aim, walk, run and the bobble and
relaxed-move layers never move a prop bone; **melee attack clips do** — `baseballbat_attack_*`
(every direction, combo, block and alert variant), `bushhook_attack_*`, `knife_attack_*`,
`sledgehammer_attack_*` and `stake_attack_*`, reaching 179.99° on `gerber` and `Cylinder01` and
21.4 in translation on `bush hook`. `stealth_success_victim_*` and `stealth_failure_victim_*` carry
prop bones through whole-body reactions at up to 214 in.

### `Box01` and `Box02` are vestigial

`Box01` occurs on `swat.mdl`, `swat2.mdl` and `swat3.mdl`; `Box02` on `bomb_guy.mdl`. On those
bodies they hang from `Bip01 Pelvis` and `Bip01 R Finger1` respectively, not from a hand, and no
sequence any of those NPCs can play animates either bone. `item_w_colt_anaconda` and
`item_d_holy_light` mount `Box01`; `item_w_crossbow_flaming` and `item_w_ithaca_m_37` mount
`Box02` — and no placement authors any of those four items on any of those four bodies. Every swat
and bomb_guy placement authors `item_w_steyr_aug`.

`VentrueSecurity` — the stat template `swat.mdl` uses — carries
`"NpcFakeReloadCountMin" "4.0" // anaconda`, naming the revolver the body is still rigged for.

### Bind agreement is per-sex and exact

For the melee families the mount bone's parent-relative bind splits cleanly by sex: the male file
matches 184 of 288 mesh bodies to ≤1e-3 in, the female file matches the complementary 104, and the
two clusters differ by a fixed per-family offset of 1.05–1.6 in and 4.9–17.8°. Each file is
calibrated to its own sex.

Composed against a standard male body, the geometry lands where a held object should:

| Family | Mount bone | Centroid from hand | Extent |
|---|---|---|---|
| katana | `handle` (R) | 4.29 in | 3.7–27.2 in |
| knife | `gerber` (R) | 3.76 in | 3.0–12.5 in |
| tireiron | `tire iron` (R) | 10.70 in | 3.8–18.8 in |
| baseball_bat | `Bat` (R) | 12.26 in | 4.0–35.0 in |
| torch | `Bat` (R) | 17.74 in | 3.5–24.6 in |
| bushhook | `bush hook` (L) | 28.70 in | 2.5–46.2 in |
| sledgehammer | `Sledgehammer` (L) | 38.87 in | 5.4–46.1 in |

Because a matched bone's world matrix is copied from the wearer, a wield model's own bind for that
bone reaches the frame only through the inverse bind folded into skinning. Bind differences that
the mesh compensates for are therefore invisible: `w_m_tireiron.mdl`'s prop bone is calibrated to
the wrong sex and `w_{m,f}_torch.mdl` disagrees with every body's `Bat` convention, and both render
identically to their counterparts.

### `w_f_bushhook.mdl` carries a degenerate bind

`w_f_bushhook.mdl`'s `bush hook` bone has a literal identity local bind — position norm `1e-6`,
rotation distance `0.0` from identity. Every other terminal bind in the corpus has a position norm
of 2.27–10.80 in and a rotation distance of 0.017–1.41; nothing else is within three orders of
magnitude, and the pattern does not cluster by sex, grip side or two-handedness.
`w_f_sledgehammer.mdl` — the other two-handed left-hand weapon — is entirely ordinary.

In mount-bone-local space the male and female meshes disagree by 36.23 in and ~82°, with the long
axis on local-Y in the male file and local-Z in the female. The difference is not compensated, so
the two sexes do not render alike. Both files resolve from `pack001.vpk` (`.mdl`) and `pack008.vpk`
(`.vtx`) and exist in exactly one version each; the Unofficial Patch does not replace either.

### Materials

The 67 models' materials resolve to 89 distinct texture keys, all present patch-first. Nearly every
weapon material carries `$envmap`; alphatest appears on the flamethrower `grate` and both
`handleclaws` materials, translucency on the Steyr AUG magazine, translucent+additive on
`gio_spirit`. Several models resolve materials through a second header search path
(`models/scenery/pipes/valve_wheel/`, `models/weapons/ammo/`, `models/hands/`, or the bare
materials root), so resolution must honour the header's path order.

`w_{m,f}_fire_axe.mdl` declares a second **skin family** whose sole material, `Transparent`
(basetexture under the Santa Monica ghost's folder), is the ghost-form invisible-weapon skin — the
only extra family in the corpus. One retail defect: the handleclaws material literally named
`null` resolves to a texture whose `.ttz` payload is a 20-byte empty file, so the slot decodes no
image; retail ships it that way.

### Per-sex model differences

The sexed files are distinct assets, not one mesh with two names. Topology differs on
`pistol_glock` (13 bones/2200 verts male, 12/918 female), `m37` (14/15, female carrying an extra
`hands box` root), `flamethrower` (15/16, female-only `knob`, `switch`, `trigger`),
`severed_arm` (13/16) and `submachine_mac10` (14/13, male-only unskinned `mac10`). Mesh density
alone differs on `baseball_bat`, `grenade_frag`, `holylight`, `rifle_rem700_bach` and
`sledgehammer`. The crossbow pair differs in bone-name case (`cBone03`↔`CBone03`), and
`Bone13`(male)/`cBone13`(female) differ by more than case, so a name join must be confirmed by
parent and index.

`models/weapons/sheriff_sword/wield/sheriff_sword.mdl`, `changball.mdl` and `gio_spirit.mdl` serve
both sexes from one file.

## 7. Who carries what

### NPCs

`additionalequipment` names the weapon an NPC spawns wielding and `alternateequipment` a second
choice. Across the 108 installed maps, 2,060 placed `npc_*` entities carry these keys: 1,100
author a non-empty `additionalequipment` and 307 a non-empty `alternateequipment` (316 of the
former are `npc_maker`/`npc_maker_zombie` spawner templates rather than live placements). The
per-map survey counts in `docs/vtmb/npc-ai-reverse-engineering.md` cover a narrower scope.

Most-authored values, `additionalequipment`/`alternateequipment`: `item_w_glock_17c` 203/19,
`item_w_fists` 107/7, `item_w_mac_10` 95/1, `item_w_thirtyeight` 86/9, `item_w_knife` 69/91,
`item_w_ithaca_m_37` 68/1, `item_w_fists_zombie` 58/0, `item_w_avamp_blade` 51/10,
`item_w_steyr_aug` 51/0, `item_w_unarmed` 51/47, `item_w_crossbow` 35/5, `item_w_katana` 31/16,
`item_w_colt_anaconda` 30/0, `item_w_baton` 12/68, `item_w_tire_iron` 8/14.

Two authored classnames have **no `vdata/items` definition**: `weapon_smg1` (13 placements) and
`item_w_sw_m64` (1, the Night Watchman in `sm_junkyard_1`). Those NPCs resolve no item.

### The starting-equipment tables grant no firearm

`npctemplate*.txt` names a `Starting_Equipment` package that resolves against the
`Starting_Equipment_Tables` block of `vdata/system/items.txt`. The block holds three regions:
player chargen kits by background; a clan-named region delimited by
`// new equipment added for multiplayer by wesp`; and the NPC packages.

`NPCGeneric` (63 templates, including `SWAT`, `SuperSWAT`, `VentrueSecurity`) and `Civilian` (41
templates, including `Bomberman`) are **empty**. The only non-empty NPC packages are special-form
melee kits: `Gargoyle`→`item_w_gargoyle_fist`, `Hengeyokai`→`item_w_hengeyokai_fist`,
`ManBat`→`item_w_manbat_claw`, `ManBatMinion`→`item_w_claws`,
`MingXiao`→`item_w_mingxiao_melee` + `item_w_mingxiao_spit`,
`MingXiaoTentacle`→`item_w_mingxiao_tentacle`, the three `TzimisceCreation*` claw packages, and
`Yukie`→`item_w_katana`. `ChunkGuard`'s single anaconda line is commented out.

**No firearm is granted through a starting-equipment package.** Every NPC firearm comes from a
per-placement keyfield.

### Coverage

Every wield-model-bearing weapon is reachable except as noted. `item_w_sledgehammer`,
`item_w_bush_hook`, `item_w_occultblade` and `item_w_grenade_frag` are player-only;
`item_w_grenade_frag` is a single patch-added copy obtained during the endgame. `item_w_claws` and
its ghoul and Protean variants are discipline-granted rather than picked up.

`item_w_throwing_star` is reachable as neither: absent from both reference weapon catalogues, no
separate object definition exists, `is_wieldable` is absent, and its model is missing. It survives
only as a mail-quest object. `item_d_holy_light` appears in no weapon catalogue.

The Chang encounter uses no bespoke path. `npc_VChangBrosClaw` in `sp_giovanni_5` places its body
with an ordinary `model` keyvalue (`.../Chang_Brothers/chang2.mdl`) and receives its weapon through
`additionalequipment` → `item_w_chang_claw`, the same keyfield every other armed NPC uses;
`item_w_chang_claw` is `item_type "weapon_melee"` with a single `Activation { Type Attack }` block
and equips through the path in §2. `item_w_chang_energy_ball` and `item_w_chang_ghost` are its
thrown counterparts (§6).

*Player reachability rests on the community walkthrough and mod-developer guide under
`$ELYSIUM_WORK_ROOT/research/reference-source/`, and is secondary evidence. NPC carriage is
measured from authored map entities and `vdata`.*

---

*Provenance: item and NPC data read from the patch-first corpus, placements from the maps' own
entity lumps; model claims decoded from the pinned install's VPK-resident `.mdl`/`.dx80.vtx` bytes
(bone tables, `StudioVertex` skin weights, attachment records, sequence tables, and per-frame
animation channels composed to measure local motion); binary claims decompiled and disassembled from the pinned
`vampire.dll`, `client.dll` and `engine.dll`. Generated decompilation, probe scripts and evidence
remain outside the checkout under `ELYSIUM_WORK_ROOT`.*
