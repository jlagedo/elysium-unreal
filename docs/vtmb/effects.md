# Effects — every visual and impulse effect VtMB authors

VtMB does not ship Valve's later `.pcf` particle editor. Troika forked `engine.dll` and
authored a text-driven particle language under `particles/`, then placed it from map
entities, animation events, discipline records, weapon items, an impact table, and the
main menu. Separate Source leftovers (`env_steam`, `func_dustmotes`, `env_shake`,
`env_fade`) and two physics-impulse entities sit beside that system. This document is
the inventory of those families and the contracts a remaster has to cover.

Unreal reproduction — modern look, reproduced logic — lives in
`docs/architecture/effects-architecture.md`. Rain, wetness and the particle-definition
*grammar* live in `docs/vtmb/weather.md`. Collision models named `.phy` are not effects;
they live in `docs/vtmb/phy_vphysics.md`.

Status of any runtime or bake work lives only in `docs/project/roadmap.md`.

---

## 1. `.phy` is not an effect

A sibling `.phy` next to a `.mdl` is **VPhysics collision**: convex hulls plus an authored
mass, decoded to `props/<stem>.phys` and consumed by `prop_physics` / `prop_dynamic`.
Format, coordinates, and the missing-collision rule are `docs/vtmb/phy_vphysics.md`.

A burning barrel, a flying car, or a warehouse blast uses that collision body **and**
separate effect entities. The `.phy` never names a sprite, a particle definition, or a
render mode. Treating a `.phys` sidecar as a smoke or fire asset is a category error.

Two map classes *do* carry `phys` in the classname. They are impulse sources, not
collision files and not particle systems:

| Class | Count on the 23 exported maps | Input | What it does |
|---|---:|---|---|
| `env_physimpact` | 11 | `Impact` | applies a directional impulse to one named physics body |
| `env_physexplosion` | 8 | `Explode` | applies a radial impulse; `targetentityname` names the body |

Both cluster on `sm_junkyard_1` (cars, exploding barrels) and `sm_warehouse_1` (the
scripted blast). Keys: `magnitude`, plus `directionentityname` / `target_position`
(`env_physimpact`) or `radius` / `targetentityname` (`env_physexplosion`). They move
already-simulated Chaos/VPhysics bodies. They do not draw.

A typical junkyard barrel is therefore four authored pieces, not one:

1. `prop_physics` + the model's `.phy` — the body that can fly.
2. `env_particle` `barrelfireemitter` — the fire/smoke look.
3. `params_explosion` — flash light, damage, optional shake, a *different* particle
   (`Explosion2_emitter`), and a sound.
4. `env_physexplosion` — the kick that throws the barrel.

The remaster has to keep those four roles distinct. Combining them into "the fire
effect" loses either the look, the damage, or the physics.

---

## 2. The particle framework

### 2.1 It is Troika's, not Source 2007

`func_precipitation` is absent from every game DLL. The particle keywords
(`movealign`, `theta_speed`, `min_frames`, `"collide spawn for %s"`) live in
`Bin/engine.dll`. Valve's `.pcf` system arrives in Source 2007, three years later, and
is a different design. `particle_definition` always names a `particles/<name>.txt`
file — bare, with the directory, or with the `.txt` suffix; all three spellings resolve
to the same definition.

The public FGD (`@PointClass env_particle`, `@SolidClass func_particle`) agrees. There
is no `info_particle_system` and no `.pcf` in the install.

### 2.2 The shipped corpus (patch-first mirror)

`$ELYSIUM_EXPORT_ROOT/particles/` holds **1,698** definitions and **318** TGA sprites
(each also as a normalized PNG). Parser: `pipeline/src/elysium_pipeline/formats/particles.py`.
Mirror: `pipeline/src/elysium_pipeline/exporters/UE_extract_particles.py`.

Roles, distinguished by content rather than by a declared type:

| Role | Count | Shape |
|---|---:|---|
| Drawing particle | 1,054 | has `sprite`, no `spawn` |
| Emitter-only wrapper | 590 | has `spawn`, no `sprite` |
| Both | 32 | draws and spawns children |
| Neither | 22 | empty / template / broken |
| `collide { }` | 81 | impact child and/or decal |
| `precipitation "1"` | 17 | gated by `particles_enable_precipitation` |

The grammar — scalar / `a~b` range / `a,b(n)` keyframe, emitter vs particle keys — is
`docs/vtmb/weather.md` → "The particle-definition format". This document does not
repeat it.

Name-prefix census of the 1,698 files (a definition can match more than one row):

| Intent (from the filename) | Files |
|---|---:|
| Discipline (`d_*`, Animalism, Thaumaturgy, …) | 646 |
| Muzzle / combat / explosion / blast | 308 |
| Glow / aura / teleport / summon / charge | 305 |
| Fire / flame / ember / torch | 219 |
| Blood / feed / vomit | 210 |
| Smoke / steam / fog / cigar / ash | 143 |
| Named boss (Andrei, Chang, Ming, Sheriff, Tzimisce, Sabbat) | 121 |
| Body / bone / trail attach | 73 |
| Water / splash / drip / spray | 50 |
| Dust / debris / glass / break | 37 |
| Menu / widescreen / cursor | 33 |
| Lights / flare / citylight / airplane | 32 |
| Rain / weather | 32 |
| Unclassified by name | 254 |

The unclassified remainder is still the same language: moths, flies, frost, electrical
strikes, HUD shock, drag sparks, health orbs, cutscene panels, demo clouds. There is
no second particle format hiding in that tail.

### 2.3 Keys the current compiler rejects

`compile_definition` is strict: a live key outside the known contract is an export
error, and the map exporter records the root as `unresolved` rather than dropping the
whole map. Across the 23 exported maps, 21 roots fail for these reasons:

| Unsupported field | What it means | Typical consumer |
|---|---|---|
| `normal`, `refract` | a second texture used as a heat-haze / refraction card | `fire_heat` on every `barrelfireemitter` |
| `rotate` (particle body; spawn already allows it) | sprite spin spelling variant | debris (`debries`) |
| `timescale` (inside `spawn`) | moth path timing | `moth_emitter` |
| `frames` inside `spawn` | per-child lifetime override | gasoline-trail fire |
| `sortfront` enabled | translucent draw-order hint | blood-guardian, warehouse sparks |
| `lighting` | lit vs unlit sprite | Potence death-blow smoke |
| `radius` on a drawing particle | size-as-radius spelling | warehouse HUD explosion |
| malformed brace files | hand-edited / patch-broken text | `fire2_emitter`, Tourette suicide |

`refract` + `normal` is the one that takes out every barrel fire. The heat card is a
presentation layer on top of flames/smoke/embers; a remaster can drop the 2004
refraction trick and still keep the fire.

---

## 3. How the game places an effect

Five independent producers name into the same `particles/*.txt` pool. Covering the
maps' `env_particle` entities is not covering the game.

### 3.1 Map entities (the placed set)

Counts are over the 23 currently exported maps.

| Class | Count | Role |
|---|---:|---|
| `env_sprite` | 1,424 | additive billboards: coronas, volume-light shafts, candles, cop flashers, lightning, moon |
| `infodecal` | 879 | projected blood, bullet holes, graffiti, posters, stains |
| `env_particle` | 188 | point emitter; `particle_definition` names the root |
| `params_particle` | 49 | reusable named template, almost always dialog Dominate / Presence |
| `env_fade` | 37 | full-screen colour fade |
| `func_particle` | 24 | brush-volume emitter (rain boxes) |
| `env_physimpact` | 11 | directional physics impulse |
| `env_physexplosion` | 8 | radial physics impulse |
| `func_dustmotes` | 8 | Source dust volume (warehouse only) |
| `params_explosion` | 7 | explosion *recipe*: light + damage + shake + particle + sound |
| `env_shake` | 6 | camera shake |
| `env_shooter` | 6 | launches gib `.mdl`s (warehouse body-part fling) |
| `env_steam` | 5 | Valve leftover steam jet (clinic pipes) |
| `env_particle_hud` | 1 | HUD-space particle (warehouse explosion) |
| `point_explosion` | 1 | named consumer of a `params_explosion` |

`env_particle` I/O is recovered (`docs/vtmb/entity_io.md` → "`env_particle` attachment"):
`TurnOn` always restarts, `TurnOff` stops feeding and lets live particles finish,
`SetRateScale` / `SetRampTime` own the fade, `attach_type` 0/1/2/3 are
origin / tree / point / treecolor. Values 5, 9, 10, 11 appear and are not fully
decoded; 11 is the rain follow-emitter.

Placed `env_particle` roots on those maps, collapsed by intent:

| Definition (normalized) | Count | Intent |
|---|---:|---|
| `waterdrops_timer` | 46 | drip under overhangs |
| `dialog_domination_emitter` / `dialog_presence_emitter` | 23 + 23 | conversation-power FX, also the `params_particle` pair |
| `gasoline_fire_emitter` (+ start / wall / trail) | 22 | scripted fire (junkyard) |
| `barrelfireemitter` | 17 | barrel / drum fire |
| `steamrelease_constant_emitter` / `steamrelease_timer` | 13 | steam jets |
| `moth_emitter` | 9 | moths |
| `airplane_emitter` | 7 | distant airplane sprite |
| warehouse explosion family | 14 | scripted blast |
| `rain_follow_emitter` | 4 | viewer-tracking rain |
| `cigar_emitter` / cig-glow / cig-smoke | 8 | smoking NPCs |
| `fire1_emitter` / `fire2_emitter` / `stovefireemitter` | 5 | generic / stove fire |
| `starynight_emitter` / `glowywierd_emitter` | 2 | decorative night motes |
| discipline / cinematic one-offs | rest | wolf form, pestilence, obfuscate, potence, embrace bleed, prince decapitation, … |

`func_particle` is rain only: 14× `rain_box_noprecip_emitter`, 10× `rain_box_emitter`.

`params_particle` is almost a constant pair on every map — `dominate_particles` and
`presence_particles` at the origin, `attach_type` 2 — plus a few extras on the
junkyard. The conversation powers look up this named template rather than spawning a
fresh `env_particle`.

`env_steam` is **not** the Troika language. It is Valve's leftover jet:
`SpreadSpeed`, `Speed`, `StartSize`, `EndSize`, `Rate`, `JetLength`, `rendercolor`.
All five live on `sm_medical_1` pipe fittings. `env_particle` also receives a
`JetLength` input (2 wires), so some Troika emitters were driven with the Valve
length key; the particle files themselves do not declare it.

`func_dustmotes` is also Valve: a brush volume (`model *N`), `SpawnRate` 30,
`SpriteName materials/particle/sparkles.vmt`, pale colour, short life. Eight of them
fill `sm_warehouse_1`. They never name a `particles/*.txt`.

### 3.2 Animation events

Client `C_BaseAnimating::FireEvent` (`0x100935a0`) is the live effect bus for
anything attached to a playing sequence (`docs/vtmb/animation_and_movers.md`):

| Event | Role |
|---|---|
| 5001 / 5011 / 5021 / 5031 | player muzzle flashes |
| 5003 / 5013 / 5023 / 5033 | NPC muzzle flashes |
| 5002 | disabled spark warning |
| 5004 / 5005 | sound / `Disciplines/` sound |
| 5103 | effect teardown |
| 5111–5119 | attachment / origin emitter variants |
| 5120 | options effect from weapon attachment `slampoint` |
| 6001–6004 | shell ejection on attachment 1..4, repeated |
| 6011–6014 | clip ejection on attachment 1..4, once |

The corpus actually uses 5001, 5003, 5005, 5101–5102, 5105, 5112, 5115–5118, 5120,
6001, 6002, 6013. Feeding starts `force_feeding_emitter` from event 5116
(`docs/vtmb/feeding.md`). The two 60xx families are named by their own diagnostics —
`"weapon does not have attachment for shell ejection!"` and the clip-ejection twin — in both
`C_BaseViewModel::FireEvent` (`0x100ab530`) and the weapon hook (`0x1009c970`). The attachment
index is the event id less 6000 or 6010; 6001–6004 parse `"%d %d"` where the second integer is a
repeat count defaulting to 1, 6011–6014 parse a single integer and fire once. Both spawn a client
temp-entity at the attachment's origin and angles through the temp-entity manager, so a shell or
magazine is a dropped prop, not a particle handle. The two handlers split on camera mode: the
viewmodel's runs only when `ShouldDrawLocalPlayer` is false and the world weapon's only when it is
true, so exactly one spawns the effect. What the parsed integers select is not recovered.
**What would close it:** the manager's dispatch body, cross-referenced against the authored option
strings on `v_m37`'s `fire01`/`fireempty01`. The Unreal bake emits **no** AnimNotify; reproducing these
is runtime work over the decoded event list.

### 3.3 Discipline records

`vdata/system/disciplinetgt_000.txt` … `_004.txt` carry `Particle_FirstPerson`,
`Particle_ThirdPerson` and (where present) `Particle_Hit`. The first-person / third-person
mode tag is recovered (`docs/vtmb/camera-view-modes.md`); whether a live emitter is
stopped, restarted or faded on a camera swap is not.

`vdata/system/stats.txt` (and the vampire / hunter variants) name the renewable-discipline
body and HUD emitters: Fortitude aura 1–5, Potence 1–5 blood-point hand/flash, Celerity
view, Bloodbuff / Blood Healing view, Protean. These are not map entities. They start
when the discipline commits and die when the active slot clears
(`docs/vtmb/disciplines.md`).

Named families in those tables: Animalism (bats, beetles, ravens, wolves, pestilence,
brood, bloodsuckers), Dementation, Dominate (including possession and suicide-pact),
Presence, Thaumaturgy (blood strike / shot / rip / boil / shield / theft / cauldron).
That is most of the 646 `d_*` files.

### 3.4 Weapon items

Ranged `vdata/items/item_w_*.txt` name a per-gun emitter (`w_thirtyeight_emitter`,
`w_ithaca_m_37_emitter`, …) in four muzzle keys, plus `projectile_particles` for the tracer
(default `BulletTrail_Emitter`). The flaming crossbow and flamethrower name fire emitters;
Ming Xiao spit and the Tzimisce head name vomit / launch emitters. These fire from the
animation-event path above, not from a map entity.

#### A muzzle flash is two named emitters plus a one-frame dynamic light [static-verified]

There is no muzzle sprite and no temp entity. A muzzle flash is **two mechanisms with
separate triggers**, and reproducing only one of them loses half the effect:

1. a *flash* emitter and a *smoke* emitter, started by name on a numbered attachment from a
   client-only animation event (below);
2. a **dynamic light**, set as an entity effect bit by the server and consumed once by the
   client's per-frame animating body (§ *The light*).

The particle half is the one the item record names. The light half carries no authored
parameters at all — its colour, radius and lifetime are constants in `client.dll`.

The event ID encodes the attachment slot and the shooter:

| Event | `GetAttachment` index | Shooter |
|---|---:|---|
| 5001 / 5011 / 5021 / 5031 | 1 / 2 / 3 / 4 | player |
| 5003 / 5013 / 5023 / 5033 | 1 / 2 / 3 / 4 | NPC |

The handler derives 0–3 from the ID and asks for index + 1; attachments are 1-based, and the
request is skipped when the model declares fewer. Resolved origin and angles go to
`CTempEnts` slot 6 (`client.dll 0x1003cc90`), which forks on a first-person flag into
`0x1003d950` or `0x1003d9c0`. Both then do the same thing twice — flash, then smoke —
through the named-emitter spawn `0x100b47b0(name, origin, angles, entity, mode 6,
attachmentIndex, 1,1,1,1)`, and force one immediate simulate tick so the burst lands on the
frame that fired it. A name the particle registry cannot resolve produces
`could not create particle of type %s` and no effect.

The two names come from the parsed item record. All four keys are 128-byte strings and each
carries a default, so a gun that names none still flashes:

| Key | Record offset | Default |
|---|---|---|
| `muzzleflash_particle` | `+0x50170` | `MuzzleFlash_Emitter` |
| `muzzlesmoke_particle` | `+0x501f0` | `MuzzleSmoke_Emitter` |
| `viewmuzzleflash_particle` | `+0x50270` | `MuzzleFlash_Emitter` |
| `viewmuzzlesmoke_particle` | `+0x502f0` | `MuzzleSmoke_Emitter` |

**Almost no gun sets them.** Of the 16 `camera_class ranged` records, four name an emitter, two
blank the keys, and the other ten omit them and take the parser default. The whole of Troika's
per-weapon muzzle art is three statements:

| Profile | Weapons | `muzzleflash_particle` | `muzzlesmoke_particle` | first-person variant |
|---|---|---|---|---|
| revolver — flash **and** smoke | `thirtyeight`, `colt_anaconda` | `W_ThirtyEight_Emitter` | `W_ThirtyEight_Emitter-Smoke` | same as world |
| shotgun — own flash, **no** smoke | `ithaca_m_37` | `W_ithaca_m_37_Emitter` | `Blank_emitter` | `W_ithaca_m_37_View_Emitter` |
| | `supershotgun` | `W_SuperShotgun_Emitter` | `Blank_emitter` | `W_SuperShotgun_View_Emitter` |
| none at all | `crossbow`, `crossbow_flaming` | `""` | `""` | `""` |
| generic default | `deserteagle`, `glock_17c`, `mac_10`, `uzi`, `steyr_aug`, `remington_m_700`, `rem_m_700_bach`, `flamethrower`, `mingxiao_spit`, `tzimisce2_head` | *(absent)* → `MuzzleFlash_Emitter` | *(absent)* → `MuzzleSmoke_Emitter` | *(absent)* → `MuzzleFlash_Emitter` |

The shotguns are the only records whose first-person emitter differs from their world one, so the
`view*` keys buy VtMB exactly two distinct assets across the whole arsenal. The crossbows' blank
is load-bearing in both halves: an empty `muzzleflash_particle` also fails the server gate in
*The light* below, so a crossbow throws neither particles nor a dynamic light.

Which pair is read, and when the emitters start, depends on who is shooting:

| Case | Body | Timing | Pair |
|---|---|---|---|
| local player, first person | `C_BaseViewModel::DrawModel` `0x100aba00` | latched at `+0x78c`, emitted on the next draw | `view*` |
| local player third person, and the world weapon model | `C_BaseCombatWeapon::DrawModel` `0x1009c070` | latched at `+0x8cf`, emitted on the next draw | non-`view` |
| NPC | `C_BaseCombatCharacter::FireEvent` `0x10099c00` | immediate | `view*` |

The latch is `{bool pending, Vector origin, QAngle angles, float time, byte type, int
attachment}`, written by the event and consumed by the next `DrawModel`. The weapon's event
hook (`0x1009c970`) declines — returning false so the viewmodel handles it — when the owner
is the local player and the camera is not third-person; that is what routes first person to
the viewmodel and everything else to the world model. The NPC body prefers an attachment on
the *held weapon* and falls back to one on the character.

Two Troika behaviours to reproduce rather than tidy:

- The NPC path reads the **`view*` pair** (`0x10099cbe` / `0x10099ccf`), not the world pair.
  It is only observable on a gun whose `view*` keys differ from its world keys.
- The flash is one frame late in first and third person, because it is latched at event time
  and started from `DrawModel`. Only the NPC path starts on the event itself.

#### The light [static-verified]

The light does not ride the animation event, the item record, or the particle system. It is
Source's entity-effect-bit path, and it is the only part of a muzzle flash the *server*
triggers.

`CWeaponRanged::Shot` (`vampire.dll 0x102387b0`), in the same body that spends ammunition
and builds the fire packet, reads the weapon's item record and sets bit `0x2` on the
**shooter's** `m_fEffects` (server `+0x19c`) — `GetOwner()`, not the weapon:

```
10238881  CALL 0x10010ea6                   ; weapon->GetItemRecord()
10238886  MOV  CL, byte ptr [EAX + 0x50170] ; muzzleflash_particle[0]
1023888c  TEST CL, CL
1023888e  JZ   0x1023889e                   ; empty -> no bit, no light
10238890  MOV  EAX, dword ptr [ESI + 0x19c] ; ESI = the shooter
10238896  OR   AL, 0x2
10238898  MOV  dword ptr [ESI + 0x19c], EAX
```

That is the **only** site in either DLL that sets bit `0x2`, and it is gated on the weapon
naming a non-empty `muzzleflash_particle` — a gun with no muzzle particle also gets no
light.

The bit networks to the client, where `C_BaseAnimating`'s per-frame body (slot 17,
`client.dll 0x10093370`) consumes it:

| Property | Value |
|---|---|
| position | the entity's **attachment 1**, always — not the 1..4 the event selected |
| colour | 255 / 192 / 64, exponent 10 |
| radius | 100 Source units (254 cm) |
| decay | 2000 |
| life | `curtime` + a shared `.rdata` constant; Valve's published `MuzzleFlashCallback` uses the same constant at 0.05 s, and the byte value is not read out of the corpus |
| repeat | none — the body clears bit `0x2` after allocating, so one bit set is one light |

`decay` is Source's radius shrink rate in units per second, so the radius ramps linearly to
zero and 2000 × 0.05 s = 100 — exactly the initial radius. The light dies at the same
instant it reaches zero size, which is independent evidence that the unread life constant is
0.05 s.

It is additionally gated on the model declaring at least one attachment and on the global
no-dynamic-lights ConVar at `0x105fb298`, which gates every `CL_AllocDlight` site in the
client (explosions, gunshot decals, temp entities) rather than muzzle flashes specifically.

Both halves are therefore faithful behaviour to reproduce, but they are *not* one effect:
the particles follow the event's attachment 1..4 and the item record's per-gun emitter, and
the light is a fixed-colour constant at attachment 1 on the shooter.

Source's own muzzle *sprite* presentation ships in `client.dll` and is unreachable. The
sprite temp entity (`C_TEMuzzleFlash`, `effects/muzzleflash1`…`4`,
`MuzzleFlash_Pistol_Player` at `0x1003d770`) and the `MuzzleFlash` client effect at
`0x10044870` — a smoke puff plus a dlight with the same colour and radius constants, gated
on the registered `muzzleflash_light` ConVar (`0x100446f0`) — are reached only from
`CTempEnts` slots 22 and 24 and `CEffectsClient` slot 5, which VtMB's weapon code never
calls. The `muzzleflash_light` ConVar therefore does **not** gate the light VtMB actually
shows; the effect-bit path at `0x10093370` never reads it.

Separately, the flamethrower resolves an attachment named literally `muzzle` and drives
`Flamethrower_Muzzle_Active_emitter` / `Flamethrower_Muzzle_Inactive_emitter`
(`0x100280a0`). `ent_muzzle` is a server console command that draws the muzzleflash
attachment point.

### 3.5 Impact table

`vdata/system/particleimpacttable.txt` is a **surface × weapon** matrix. Each
`Material` block has a `Default_Effect` and optional per-weapon overrides. Surfaces
with their own default:

| Surface | Default emitter |
|---|---|
| concrete | `Impact_Concrete_Emitter` |
| metal / vent / grate / computer / gargoyle_soak | `Impact_Metal_Emitter` |
| wood | `Impact_Wood_Emitter` |
| dirt | `Impact_dirt_Emitter` |
| glass | `Impact_Glass_Emitter` |
| flesh | `Impact_Flesh_Emitter` |
| flesh_soak | `Impact_Soak_Emitter` |
| gargoyle | `Impact_Gargoyle_Emitter` |
| fortitude_soak | `Impact_Fortitude_Soak_Emitter` |
| bloodshield_soak / special_dmg_soak | `Impact_Bloodshield_Soak_Emitter` |
| ming_xiao | `Ming_Xiao_Hit_Emmitter` |
| ming_xiao_tentacle | `Ming_Xiao_Baby_Hit_Emmitter` |

Weapon overrides that matter: shotgun / MAC-10 variants per surface, flaming-crossbow
→ `Impact_FX_Explosion`, flamethrower / torch on flesh →
`flamethrower_hit_flame-emitter`, claws → `ImpactClaws_flesh_Emitter`, melee →
`ImpactFX_Melee_Generic`, most firearms on flesh → `ImpactFX_Ranged_Generic`.

Audio for the same hit is a different table (`docs/vtmb/surface_properties.md`). The
impact particle and the impact sound are sibling lookups, not one asset.

### 3.6 Main menu

`resource/mainmenuparticles.txt` is a 3D scene in the same language: camera, skybox,
three emitter groups (clouds + fire, clan seals, blood cels). Reconstruction:
`docs/vtmb/m0_menu_build.md` §9.

---

## 4. Effect families the remaster has to handle

This is the closed list of *kinds*. Every shipped effect is one of these, or a bundle
of them (the barrel in §1).

### 4.1 Sprite billboards (`env_sprite`)

Camera-facing additive quads. Material selects the family
(`docs/vtmb/entity_visuals.md` §4):

| Material | Count | Intent |
|---|---:|---|
| `sprites/glowa` | 969 | lamp / bulb corona |
| `sprites/volumelighta` / `volumelightb` (+ `_proxyfade`) | 186 | light shaft, elongated |
| `sprites/streetlight3_proxyfade` | 74 | street-lamp halo |
| `sprites/glowb` | 58 | second corona |
| `sprites/candle` | 44 | flame card, `framerate` animates |
| `sprites/coplights` / `coplightsb` | 50 | cop-car flasher |
| `sprites/lightning` / `lightningglow` | 42 | lightning flash (parented to `lightningrotator`) |
| `sprites/moonhalola` | 1 | moon |

`rendermode` 3 is occlusion-faded glow; 5 is plain additive. StartOff sprites stay
dark until I/O. This is a billboard problem, not a particle-graph problem.

### 4.2 Fire and embers

Emitter wrappers spawn the same four drawing leaves: a flame card (`Flamemass` /
`FirePlace_Flames`), a glow, `Smoke1`, `FlameEmbers1`. Optional fifth: `Fire_Heat`
(refraction). Placed as barrel / stove / gasoline / generic `fire1`. Also body-fire
(`body_fire_emitter`), crossbow flame, flamethrower, Molotov, torch, and several
discipline / boss casts.

Intent: a local rising, flickering volume with sparks and a smoke plume. Not a
fluid-dynamics authoring. The heat card is a 2004 refraction sprite.

### 4.3 Smoke, steam, fog, ash, cigars

Continuous, slow, often additive grey sprites (`furball`, `cloud`) with
`radius_speed` + `elevation_speed`. Steam is a directional cone (`phi`/`theta` ±10°,
rate 50). Cigar smoke is the same language attached to a bone (`attach_type` 2).
Ash and rainfog are large, slow, camera-facing cards. `blowsmoke_*` is an animated
exhale.

`env_steam` on the clinic is the Valve jet with the same intent and none of the
Troika keys.

### 4.4 Rain, drips, lightning, wetness

Owned by `docs/vtmb/weather.md`. Follow emitter + box volumes + `WaterDrops_Timer` +
sprite lightning + `FadeGlobalWetness`. Largest coherent consumer of the particle
language, not the only one.

### 4.5 Dust motes and hanging particulates

`func_dustmotes` on the warehouse, plus `starynight_emitter` / `glowywierd_emitter`
and moth / fly emitters. Intent: sparse, slow, local motes that sell volume.

### 4.6 Blood, feeding, vomit

Sprays, drips, trails, explosions, splash decals. Feeding uses
`force_feeding_emitter`. Embrace uses neck-bleed emitters. Thaumaturgy is the large
authored set (blood strike / shot / boil / shield). Impact on flesh is
`Impact_Flesh_Emitter` and friends. Several collide-blocks lay a blood decal.

### 4.7 Muzzle flash, tracers, shells

Per-gun first- and third-person bursts from animation events + item records; the chain,
the four record keys and the absent dynamic light are §3.4. A muzzle is a 3-burst flash
card with a smoke child, not a continuous emitter, and the smoke is a second named
emitter rather than a child of the flash. Tracers are `bullettrail_emitter` (distort +
ring) from `projectile_particles`. Shell ejection, when present, is a mesh or sprite
burst from the same event bus.

### 4.8 Impact hits

The `particleimpacttable.txt` matrix. Concrete dust, metal sparks, wood chips, dirt
puff, glass shards, flesh spray, plus soak / Fortitude / Bloodshield specials and
Ming Xiao. A hit is a short burst at a contact point, sometimes with a decal.

### 4.9 Explosions (the bundle)

`params_explosion` is the recipe, `point_explosion` (and scripted I/O) is the trigger:

| Key group | What it authors |
|---|---|
| `dl_color` / `dl_radius` / `dl_time` / `dl_decay` | a point light that dies |
| `dmg_amount` / `dmg_radius` / `damage_players|npcs|breakables` | damage |
| `shk_amp` / `shk_freq` / `shk_dur` / `shk_radius` / `shk_inair` | camera shake |
| `particle` | a Troika emitter (`Explosion2_emitter`, `LightBulb_Pop_emitter`, …) |
| `snd_name` / `snd_dist` / `snd_pitch_*` | sound |

`env_physexplosion` / `env_physimpact` are the physics half of the same beat.
`env_shake` is the standalone shake (elevator, bobcat, warehouse blast).

### 4.10 Debris, gibs, breakables

`env_shooter` launches one or more `.mdl`s (`models/gibs/hgibs*`) with velocity,
variance, lifetime. Warehouse scripted dismemberment. `func_breakable` is the brush
that breaks; its *look* is a model swap plus whatever particle the mapper wired.
Particle-side debris (`debries`, column-break, barrel explosion FX) is sprites, not
gibs.

### 4.11 Discipline and conversation presentation

Cast bursts, target auras, body smoke, HUD overlays, first-person view emitters,
resist flashes, form transitions (wolf, obfuscate in/out). Dialog Dominate / Presence
use the per-map `params_particle` pair. Celerity's `Fx_Motion_Trail` and Obfuscate
translucency are trait flags that a renderer consumes; they are not particle files
(`docs/vtmb/disciplines.md`, `docs/vtmb/game_runtime.md`).

Auspex aura-sight and Protean heat vision are vision modes (post-process / material),
not emitters, even though Auspex also ships aura sprites.

### 4.12 Boss and cinematic one-shots

Andrei, Chang, Ming Xiao, Sheriff, Lasombra, Tzimisce, prince decapitation, Tourette
suicide, warehouse HUD explosion, cutscene widescreen panels. Same language, unique
roots, often `attach_type` point/tree on a named parent.

### 4.13 Screen and HUD

`env_fade` (already a real class) is a colour fade with duration / hold.
`env_particle_hud` and the `d_*_hud_*` / `hud_*` definitions draw in view space.
Menu particles are a dedicated scene.

### 4.14 Projected decals

`infodecal` is the authored layer (blood, holes, graffiti) and is already a bake
product (`UDecalComponent`). Particle `collide { decal { particle … } }` is a
*runtime* decal spawned at an impact. Same Unreal primitive, different lifetime.

### 4.15 Water surface

Map water is a material / Single Layer Water problem (`docs/project/plans/world.md`
7.3), not a particle. Splashes, bubbles and rain-on-water are particles that land on
it.

### 4.16 Fog and atmosphere

Height fog, sky ambient and the 3D skybox are `docs/vtmb/sky-ambience.md`. Particle
fog (`rainfog`, `rainmist`, Animalism ground fog) is a local card emitter, not the
map fog.

### 4.17 Lights that pretend to be effects

Switchable `light` / `light_spot` (8 named lights in the whole exported set), fire
point-lights inside `params_explosion`, candle flicker via `env_sprite` frame
animation. The look of a fire is particles; the illumination is a light.

---

## 5. What is already decoded vs what is still open

Decoded and in the export:

- the particle language and the patch-first mirror;
- per-map `<map>.particles.json` (placed `env_particle` + the
  `force_feeding_emitter` gameplay root + unresolved list);
- sprite TGA → PNG;
- `env_sprite` `.sprites` sidecar;
- `infodecal` `.decals` sidecar;
- `.phy` → `.phys` for physics props;
- rain / wetness contract for `sm_hub_1` (`docs/vtmb/weather.md`).

Open, and they matter for a remaster even if the *look* is modernized:

- `attach_type` values above 3 (5, 9, 10, 11) — 11 is the rain-follow hypothesis. The
  runtime enum is wider than the four values maps use: the emitter's mode field drives a
  19-branch switch in `0x100af150`, and the muzzle-flash spawn passes **6**, whose branch
  reads a matrix off the attachment. Whether the `env_particle` keyfield and that spawn
  argument are the same enum is inferred from both landing in the same emitter field, not
  captured;
- `func_particle` volume sampling;
- `frames` / `fps` / `v(n)` keyframe units (data-supported, not retail-captured);
- sprite blend vs `mask` (additive vs translucent);
- first-person / third-person particle lifetime across a camera swap;
- `env_steam` / `func_dustmotes` as Valve classes (keys are public, runtime not RE'd
  in this repo);
- exact `env_physimpact` spawnflag bits.

None of those block listing the families or choosing an Unreal stand-in. They
constrain how faithfully a Niagara system is *driven*, not whether fire is fire.

---

## 6. Related docs

- `docs/architecture/effects-architecture.md` — Unreal mapping and what to open in
  the 5.8 install.
- `docs/vtmb/weather.md` — grammar, rain, wetness.
- `docs/vtmb/phy_vphysics.md` — `.phy` collision.
- `docs/vtmb/entity_visuals.md` — sprites, ropes, render keys.
- `docs/vtmb/entity_io.md` — `env_particle` attach / TurnOn / TurnOff.
- `docs/vtmb/disciplines.md` — when discipline FX start and stop.
- `docs/vtmb/animation_and_movers.md` — animation-event IDs.
- `docs/vtmb/surface_properties.md` — impact *audio* matrix.
- `docs/vtmb/camera-view-modes.md` — first / third person particle tags.
- `docs/vtmb/m0_menu_build.md` — menu particle scene.
- `docs/vtmb/feeding.md` — `force_feeding_emitter`.
