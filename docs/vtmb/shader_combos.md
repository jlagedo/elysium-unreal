# Shader combos — which program a material's parameters select

A VMT names a shader *family*. The engine draws with one of that family's shipped *combos*, chosen
from the material's parameter and flag state. This document owns the recovered selection rules and
the evidence for them.

What the programs compute stays with the topic that owns the channel: the `$envmap` composite is
`docs/vtmb/reflections.md`, the overbright factor is `docs/vtmb/color_gamma.md`, water is
`docs/vtmb/water.md`, the Eyes shader is `docs/vtmb/facial_animation.md`.

## Why the name is not the program

The family name identifies a behaviour class, not a file. Over the install's 11,624 addressable
VMTs there are 52 distinct family names and 112 shipped `.psh` programs, and **only 11 family names
match a `.psh` stem exactly**. `LightmappedGeneric` alone has 27 shipped combos and
`VertexLitGeneric` has 39. A name-to-file mapping is therefore wrong far more often than right, and
the difference is not cosmetic: the combos differ in whether they sample a mask, fold in self
illumination, or multiply by the lightmap.

## Where the rule lives

The selection is a short branch cascade in `stdshader_dx8.dll`, ending either in a returned string
constant or in a pointer table indexed by material flag bits. The corpus decompiles the module
completely (1,294/1,294 functions), so the cascade is readable.

The **table contents are the part no naming convention states**, and Ghidra prints only the table's
symbol. `research/tooling/probes/shader_combo_tables.py` reads them out of the shipped binary:

```text
uv run python research/tooling/probes/shader_combo_tables.py 10021620 8 6,20,99
```

The arguments are the table's virtual address, its entry count, and the material-flag bit tested at
each index position (`99` marks a condition that is not a flag, such as "this parameter is bound to
a texture").

## Reading the decompiled selectors

| Construct | Meaning |
|---|---|
| vtable `+0x5c` on a material var returning `3` | `IMaterialVar::GetType() == MATERIAL_VAR_TYPE_TEXTURE` — the parameter resolves to a real texture |
| vtable `+0x60` returning a byte | `IMaterialVar::IsDefined()` — the parameter is named at all |
| `(**(code **)(*(int *)*param_1 + 0x14))()` | the material flag word |
| `param_1[N]` | material parameter slot `N`; `param_1[6]` is `$basetexture` |

A material flag is set by the `$`-parameter of the same name being present and non-zero. The flags
the recovered selectors read, with their bit positions:

| Bit | Flag | Parameter | Materials |
|---|---|---|---|
| 22 | `NORMALMAPALPHAENVMAPMASK` | `$normalmapalphaenvmapmask` | 146 |
| 4 | `VERTEXCOLOR` | `$vertexcolor` | 1,475 |
| 6 | `SELFILLUM` | `$selfillum` | 1,240 |
| 17 | `ENVMAPSPHERE` | `$envmapsphere` | 11 |
| 19 | `ENVMAPCAMERASPACE` | `$envmapcameraspace` | 1 |
| 20 | `BASEALPHAENVMAPMASK` | `$basealphaenvmapmask` | 14 |

`$envmapsphere`'s 11 corpus users break down as 9 `shadertest/`/`dev/` debug materials and one
shipped break-glass pair (`envmap/gioint`, `skybox/hav_env`), one of which has no `$envmap` bound.

## LightmappedGeneric

3,540 materials. Draw routine `FUN_10006b10`; pixel selector `FUN_10006f50`, transcribed from its
decompilation and confirmed against its disassembly.

### Vertex program

Table `0x100214f0`, indexed
`VERTEXCOLOR | ENVMAPSPHERE<<1 | ENVMAPCAMERASPACE<<2 | ($envmap defined)<<3`:

| Index | State | Program |
|---|---|---|
| 0 | — | `LightmappedGeneric` |
| 1 | `VERTEXCOLOR` | `LightmappedGeneric_VertexColor` |
| 2, 4, 6 | sphere and/or camera-space, no `$envmap` | `LightmappedGeneric` |
| 8 | `$envmap` | `LightmappedGeneric_EnvMap` |
| 9 | `$envmap`, `VERTEXCOLOR` | `LightmappedGeneric_EnvMapVertexColor` |
| 10, 14 | `$envmap`, `ENVMAPSPHERE` | `LightmappedGeneric_EnvMapSphere` |
| 12 | `$envmap`, `ENVMAPCAMERASPACE` | `LightmappedGeneric_EnvMapCameraSpace` |

Two rules the table states and the filenames do not:

- **`ENVMAPSPHERE` and `ENVMAPCAMERASPACE` do nothing without `$envmap`.** Indexes 2, 4 and 6 all
  fall back to the plain program.
- **`ENVMAPSPHERE` overrides `ENVMAPCAMERASPACE`.** Indexes 14 and 15 pick the sphere program, so
  a material setting both never reaches the camera-space one.

### Pixel program

```text
if $basetexture is a bound texture:
    if $envmap is a bound texture:
        index = SELFILLUM | BASEALPHAENVMAPMASK<<1 | ($envmapmask bound)<<2
        return table_0x10021620[index]
    return SELFILLUM ? LightmappedGeneric_SelfIlluminated : LightmappedGeneric
if $envmap is a bound texture:
    return $envmapmask.IsDefined() ? LightmappedGeneric_MaskedEnvmapNoTexture
                                   : LightmappedGeneric_EnvmapNoTexture
return LightmappedGeneric_NoTexture
```

Table `0x10021620`:

| Index | State | Program |
|---|---|---|
| 0 | — | `LightmappedGeneric_EnvMapV2` |
| 1 | `SELFILLUM` | `LightmappedGeneric_SelfIlluminatedEnvMapV2` |
| 2 | `BASEALPHAENVMAPMASK` | `LightmappedGeneric_BaseAlphaMaskedEnvMapV2` |
| 3 | `SELFILLUM+BASEALPHAENVMAPMASK` | `LightmappedGeneric_SelfIlluminatedEnvMapV2` |
| 4 | `$envmapmask` | `LightmappedGeneric_MaskedEnvMapV2` |
| 5 | `SELFILLUM+$envmapmask` | `LightmappedGeneric_SelfIlluminatedMaskedEnvMapV2` |
| 6 | `BASEALPHAENVMAPMASK+$envmapmask` | `LightmappedGeneric_MaskedEnvMapV2` |
| 7 | all three | `LightmappedGeneric_SelfIlluminatedMaskedEnvMapV2` |

- **A bound `$envmapmask` texture overrides `$basealphaenvmapmask`** (indexes 6 and 7), so
  `_BaseAlphaMaskedEnvMapV2` is unreachable whenever both are authored.
- **Self-illumination absorbs the base-alpha mask** (index 3 equals index 1): the base texture's
  alpha cannot be both the emission mask and the envmap mask, and emission wins.
- **`$envmapmask` is tested two ways.** With a base texture the selector requires a *bound texture*
  (`GetType`); without one, merely *defined* (`IsDefined`) is enough.

The selector's second argument gates both envmap branches. Its only caller, `FUN_10006b10`, zeroes
it before the call, so the envmap combos are always reachable in shipped code.

## Eyes

406 materials — every character eyeball. Selector `FUN_100053f0`. A flat 2×2, no pointer table.

The vertex program is always `Eyes`. The pixel program is chosen by `$vampire` and by the
overbright render config:

| `$vampire` | `overbright == 2` | Pixel program |
|---|---|---|
| off | no | `Eyes` |
| off | yes | `Eyes_Overbright2` |
| on | no | `Eyes_Vampire` |
| on | yes | `Eyes_Vampire_Overbright2` |

`$vampire` is a VtMB-only `SHADER_PARAM` registered by `staticinit_10005210`, help text *"Turn on
to get whatever vampire-eye effect we use"*, type bool, default 0. It is set on **12** materials.

`$iris` never selects a program — it supplies the texture bound at stage 1 in every branch, and
`$glint` is bound at stage 2. Neither `$glint` nor `$irisframe` is set by any shipped material.
`docs/vtmb/facial_animation.md` owns what the programs then compute; its selection table agrees
with the binary.

Only `eyes.psh` and `eyes_overbright2.psh` ship as readable ps.1.1 source; both vampire variants
ship compiled-only, as `.vcs`.

`FUN_1000f6c0` is the family's `GetFallbackShader` and names `Eyes_dx6`, but **no `eyes_dx6` member
of any kind ships**, so that path has no program behind it in retail content.

## Teeth

8 materials. Selector `FUN_1000f7d0`.

**No material parameter selects the program**; only the overbright config does. The vertex program
is always `Teeth`, and the family ships **no pixel program of its own** — it draws with the generic
pair:

| `overbright == 2` | Pixel program |
|---|---|
| no | `VertexLitTexture` |
| yes | `VertexLitTexture_Overbright2` |

This is why a resolved pixel program cannot be required to carry its family's name.

Two custom parameters are registered but do not affect selection: `$forward` (vec3, default
`[1 0 0]`, *"Forward direction vector for teeth lighting"*) is written to vertex constant c42, and
`$illumfactor` (float, *"Amount to darken or brighten the teeth"*).

### Open questions

- `$illumfactor` is registered but no read of its slot was found in the selector, so its runtime
  effect is unconfirmed. Decompiling the constant-write callees would settle it.
- `FUN_1000f350`, the family's `GetFallbackShader`, returns `Teeth_DX8` unconditionally, and
  `Teeth_DX8` is a separately registered class — but no `teeth_dx8` member ships. Whether the
  fallback supersedes `FUN_1000f7d0` at registration, or is gated by a capability check upstream,
  is not established. It does not change the rule above: `FUN_1000f7d0` is the only Teeth function
  that issues shader-set calls, so it governs all 8 materials either way.
- Both selectors OR bit 2 into the material flag word. That bit is not one of the flags any
  recovered selector reads, and its meaning is unidentified.

## VertexLitGeneric

5,477 materials, the largest family. Draw at `0x10011cf0`; vertex selector `0x10011ea0`, pixel
selector `0x10011f00`. The VMT name `VertexLitGeneric` always resolves to the registered class
`VertexLitGeneric_DX8` inside this module.

### Preconditions that delete the envmap

Three rules run at load, before any selection, and each can remove `$envmap` entirely:

- **`$envmapoptional` non-zero** calls `SetUndefined()` on `$envmap` unconditionally. The DLL's
  help text says *"Make the envmap only apply to dx9 and higher hardware"*, but this install's
  `stdshader_dx9.dll` carries no VertexLitGeneric at all, so in VtMB it is simply an envmap kill.
- **`NORMALMAPALPHAENVMAPMASK` without a bound `$bumpmap`** clears the flag and deletes `$envmap`.
- **A base texture whose image carries no alpha** clears `SELFILLUM` and `BASEALPHAENVMAPMASK`.
  This one is not modelled offline: the evidence is in the texture, not the VMT, so a material
  relying on it resolves here to its pre-clear program.

### Two passes when bump mapping is active

When the bump-mapping video toggle is on *and* `$bumpmap` is bound, the draw splits. Pass 0 draws
the lit base with the envmap **suppressed**; pass 1 adds the envmap back:

| Pass | Condition | Vertex | Pixel |
|---|---|---|---|
| 0 | `bumpmapping` | from the table below, envmap bit clear | as below, envmap branch not taken |
| 1 | `bumpmapping` | `VertexLitGeneric_EnvmappedBumpmap_NoLighting` | `VertexLitGeneric_EnvmappedBumpmapV2`, or `_MultByAlpha` under `NORMALMAPALPHAENVMAPMASK` |
| 1 | `bumpmapping+ps14` | the `_ps14` spelling of the same pair | the `_ps14` spelling |

So an envmap is never folded into the base combo while bump mapping is active. The `_ps14` pair
ships compiled-only, as `.vcs` under `shaders/fxc/`; there is no readable `.psh` for it.

The `V2` suffix elsewhere in this family is **not** a hardware axis — it is simply the shipped
spelling.

### Vertex program

Table `0x10023488`, indexed `($envmap live) | ENVMAPSPHERE<<1 | ENVMAPCAMERASPACE<<2`. The
programs are named for `VertexLitTexture`, not for the family:

| Index | State | Program |
|---|---|---|
| 0, 2, 4, 6 | no live `$envmap` | `VertexLitTexture` |
| 1 | `$envmap` | `VertexLitEnvMappedTexture` |
| 3, 7 | `$envmap`, `ENVMAPSPHERE` | `VertexLitEnvMappedTexture_Spheremap` |
| 5 | `$envmap`, `ENVMAPCAMERASPACE` | `VertexLitEnvMappedTexture_CameraSpace` |

### Pixel program

The cascade matches LightmappedGeneric's, with one difference: in the **no-base-texture** branch
the mask test is `GetType() == TEXTURE`, where LightmappedGeneric used `IsDefined()`.

Table `0x10023590`, indexed `SELFILLUM | BASEALPHAENVMAPMASK<<1 | ($envmapmask bound)<<2`:

| Index | State | Program |
|---|---|---|
| 0 | — | `VertexLitGeneric_EnvmapV2` |
| 1, 3 | `SELFILLUM` | `VertexLitGeneric_SelfIlluminatedEnvmapV2` |
| 2 | `BASEALPHAENVMAPMASK` | `VertexLitGeneric_BaseAlphaMaskedEnvmapV2` |
| 4, 6 | `$envmapmask` | `VertexLitGeneric_MaskedEnvmapV2` |
| 5, 7 | `SELFILLUM+$envmapmask` | `VertexLitGeneric_SelfIlluminatedMaskedEnvmapV2` |

The same two collapses as LightmappedGeneric: a bound `$envmapmask` overrides
`$basealphaenvmapmask`, and self-illumination absorbs it. `_BaseAlphaMaskedEnvmapV2` is reachable
only at index 2 — base-alpha mask, no mask texture, no self-illumination.

### Unreachable combos

39 `vertexlitgeneric*.psh` ship; the selector can name only 12. The other 27 — the 13 `_detail*`
combos, the 5 pre-`V2` envmap spellings, the 8 `_diffbump*` combos and `_selfillumonly` — appear
as no string anywhere in `stdshader_dx8.dll`. **This build's VertexLitGeneric registers no
`$detail` parameter at all**; all 28 `$detail` authors in the install corpus are `shadertest/*` or
a `shatteredglass` crack-material path.

## UnlitGeneric

1,867 materials. Draw at `0x10001e60`; vertex selector `0x10001dd0`, pixel selector `0x10001e10`.

### Pixel program

The base-alpha mask is a branch *ahead* of the table rather than a bit inside it, and that branch
requires the mask texture to be absent — so a bound `$envmapmask` still wins:

```text
if $envmap bound and no $envmapmask and $basetexture bound and BASEALPHAENVMAPMASK:
    return UnlitGeneric_BaseAlphaMaskedEnvMap
return table_0x10020588[($basetexture bound) | ($envmap bound)<<1 | ($envmapmask bound)<<2]
```

| Index | State | Program |
|---|---|---|
| 0 | — | `UnlitGeneric_NoTexture` |
| 1 | `$basetexture` | `UnlitGeneric` |
| 2 | `$envmap` | `UnlitGeneric_EnvMapNoTexture` |
| 3 | `$basetexture`, `$envmap` | `UnlitGeneric_EnvMap` |
| 6 | `$envmap`, `$envmapmask` | `UnlitGeneric_EnvMapMaskNoTexture` |
| 7 | all three | `UnlitGeneric_EnvMapMask` |

Indexes 4 and 5 are unreachable: the mask bit cannot be set while the envmap bit is clear.

### Vertex program

Table `0x10020548`, indexed
`VERTEXCOLOR | ($envmap bound)<<1 | ENVMAPSPHERE<<2 | ENVMAPCAMERASPACE<<3`. Every index without
the envmap bit falls back to the plain pair whatever the two projection flags say, and
**`ENVMAPSPHERE` overrides `ENVMAPCAMERASPACE`** at indexes 14 and 15.

`UnlitGeneric` has a whole-class fallback to `UnlitGeneric_DX6` — a separate fixed-function shader
with no combos — rather than a per-material hardware axis.

## Sprite

62 materials. Selector `0x1000eca0`. A `switch` on `$spriterendermode`, not a table. The pixel
programs are Sprite's own; the vertex programs are UnlitGeneric's.

| `$spriterendermode` | Vertex | Pixel |
|---|---|---|
| 0 | `unlitgeneric` | `SpriteRenderNormal` |
| 1, 2, 3, 4, 8, 9 | `unlitgeneric_vertexcolor` | `SpriteRenderTransColor` |
| 5 | `unlitgeneric` if `$ignorevertexcolors`, else `unlitgeneric_vertexcolor` | `SpriteRenderTransAdd` |
| 7 | `unlitgeneric_vertexcolor` | `SpriteRenderTransAdd` |
| 6 | — | none; the shader warns *"Unknown sprite rendermode"* |

Two rules worth stating:

- **`$ignorevertexcolors` changes the program in mode 5 only.** Mode 7, the other additive mode,
  ignores it and always takes the vertex-colour program.
- **Mode 6 binds nothing at all.** The modes line up with Source's `RenderMode_t`, where 6 is
  `kRenderEnvironmental` — the one value this shader does not implement. That enum mapping is
  inferred from the well-known Source enum, not from a string in the binary.

The blend factors differ between modes that share a program pair (1/2 against 3/9 against 8), so
the pair alone does not distinguish them.

## UnlitTwoTexture

5 materials. Selector `0x10010940`. **No branching at all** — the pair is always
`UnlitTwoTexture` / `UnlitTwoTexture`. Only a numeric static-combo index varies with the `MODEL`
and `VERTEXCOLOR` flags, and `$texture2` changes the blend mode and a shader boolean rather than
the program name.

A whole-class `UnlitTwoTexture_DX6` fallback exists, selected the same way as UnlitGeneric's.

Not to be confused with `MonitorScreen`, a separate registered class that reuses this vertex
program with its own pixel program.

## Water

24 materials. Draw at `0x100138a0` (refract), `0x10013b30` (reflect), `0x10013d30` (cheap).
Literal string selection, no pointer table. `docs/vtmb/water.md` owns what the programs compute.

Two hardware-capability queries choose between the ps.1.1 pair and the "ps2.0 old" pair. Ahead of
that, `$forcecheap` short-circuits to the cheap pair and skips the refract and reflect passes:

| Condition | Pass | Vertex | Pixel |
|---|---|---|---|
| cheap | 0 | `WaterCheap_vs11` | `WaterCheap_ps11` |
| cheap, ps2.0 | 0 | `WaterCheap_vs20_old` | `WaterCheap_ps20_old` |
| otherwise | 0 | `WaterWarp_old` | `WaterRefract_old` |
| otherwise, ps2.0 | 0 | `Water_vs20_old` | `WaterRefract_ps20_old` |
| `$reflecttexture` | 1 | `WaterWarp_old` | `WaterReflect_old` |
| `$reflecttexture`, ps2.0 | 1 | `Water_vs20_old` | `WaterReflect_ps20_old` |

One qualifier `docs/vtmb/water.md` did not have: **the instant-cheap branch requires `$envmap` to
be defined as well as `$forcecheap` non-zero**, not `$forcecheap` alone.

The `$cheapwaterstartdistance` / `$cheapwaterenddistance` blend is *not* a branch — no code path
conditions on it. It reaches the pixel shader as a constant for a GPU-side lerp, which is what the
water document already concluded. `$bumpmap`'s registration comment is literally *"dudv bump map"*
and `$normalmap`'s is *"normal map"*, confirming that document's DUDV-versus-tangent split.

## DecalModulate is not in the shipped DX8 module

38 materials name `DecalModulate`, and **the string does not occur anywhere in
`stdshader_dx8.dll`** — confirmed by a raw byte scan of the shipped binary as well as by the
decompilation corpus. It exists only as a DX6 fixed-function shader in the SDK tree, never ported
to the module VtMB actually runs.

`MaterialSystem.dll`'s shader resolution (`FUN_10002180`) warns *"Material '%s' uses unknown shader
'%s'"* and retries `FindShader("wireframe")`. So those 38 materials do not draw the shader they
name. This is a property of the material system rather than a combo rule, so the seam records them
unresolved rather than inventing a program for them.

## Families whose selector could not be transcribed

Two families' `DrawElements` bodies are **absent from the decompilation corpus** — Ghidra created
no function over the address ranges that hold them, so the combo cascade cannot be read:

| Family | Materials | Range holding the undecompiled body |
|---|---|---|
| `WorldVertexTransition` | 89 | `0x10015aa6`–`0x10015ff0` |
| `Refract` | 9 | `~0x1000b1e0`–`0x1000b8f0` |

What *is* readable for them is preconditional rather than selective:

- **WorldVertexTransition** (`InitShaderParams` `0x10015960`) rejects two authored combinations
  outright: `$envmap` without `$bumpmap` warns and deletes *both*, and `$envmapsphere` warns and
  deletes `$envmap` — the shader does not support sphere mapping at all. Bump-map loading is gated
  on a config byte rather than a hardware query.
- **Refract** (`SHADER_FALLBACK` `0x1000b130`) falls back off the shader entirely below DX8, and on
  hardware a second capability query flags, unless `$forcerefract` overrides it — matching that
  parameter's own help text, *"Forces refraction on boards that have poor performance."*

Recovering these two needs a Ghidra pass that forces function creation over those ranges. Until
then their materials publish unresolved.

## Remaining families

44 of the 52 family names, covering 232 of 11,624 materials. A material in one of these publishes
its shader *name* and records the programs as unresolved rather than guessing. The largest are
`worldvertextransition` (89), `decalmodulate` (38), `wireframe` (18),
`desaturatespotlight` (11), `cable` (9) and `refract` (9); the rest are single-digit or
single-material debug and effect shaders.

A Sprite material whose `$spriterendermode` the shader does not implement also resolves
unresolved, because the engine binds no program for it either.

## Verification

The transcribed rules resolve **11,392 of 11,624 materials (98.0%)**, and **every program they
name -- pixel and vertex, across every condition and both draw passes -- is a member the install
actually ships**, as a readable `.psh` or a compiled `.vcs`. Nothing resolves to a program that
does not exist.

The rules also show which combos the shipped corpus never reaches. LightmappedGeneric selects 10
distinct pixel programs out of the family's 27 shipped combos:

| Selected pixel program | Materials |
|---|---|
| `LightmappedGeneric` | 2,309 |
| `LightmappedGeneric_MaskedEnvMapV2` | 695 |
| `LightmappedGeneric_SelfIlluminatedMaskedEnvMapV2` | 355 |
| `LightmappedGeneric_SelfIlluminated` | 108 |
| `LightmappedGeneric_EnvMapV2` | 53 |
| `LightmappedGeneric_SelfIlluminatedEnvMapV2` | 10 |
| `LightmappedGeneric_BaseAlphaMaskedEnvMapV2` | 9 |
| `LightmappedGeneric_NoTexture` | 1 |
| `LightmappedGeneric_EnvmapNoTexture` | 1 |
| `LightmappedGeneric_MaskedEnvmapNoTexture` | 1 |

## How to re-measure

Everything above comes from the install and the decompilation corpus:

- the cascade: `vtmb_code` / `vtmb_asm` on the selector named per family above;
- the tables: `research/tooling/probes/shader_combo_tables.py <va> <count> <bits>`;
- the shipped combo inventory: `install.build_index(dirs=("materials",))` over
  `materials/dxshaders/*.psh`;
- the per-material resolution and its `.psh` cross-check:
  `elysium_pipeline.formats.material_glb.shaders.resolve`, whose tables are transcribed from the
  addresses named here.

The resolution each material selects is published in its material GLB as
`ELYSIUM_vtmb_material.shaderResolution`.

## Recovered from architecture notes (2026-09-07)

Source's `$alphatest` clips at `AlphaFunc GEQUAL 0.5`; `$alphatestreference` is a registered VMT
parameter no corpus material authors.

Source samples frame 0 of a multi-frame texture when no `animatedtexture` proxy animates it.

`$halflambert` occurs on 0 of 11,624 material units in the install corpus — it is a fixed
StudioRender lighting term VtMB never actually authored against.
