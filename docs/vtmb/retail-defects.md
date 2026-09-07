# VtMB retail defects — the Unofficial Patch loader's patch sites, resolved

Every retail bug and behaviour tweak the Unofficial Patch's **Game Loader** corrects at
runtime, resolved to a function in this project's Ghidra corpus and checked byte-for-byte
against the shipped DLLs. This is the catalogue of *things retail gets wrong*: for each
one, what the original code actually does, where it lives, and whether Elysium should
reproduce the behaviour, modernize past it, or ignore it.

Scope is the loader's `[Bugs]` and `[Tweaks]` INI sections. `[Modern OS]`, `[Mods]`,
`[WideScreen]`, `[UI Fixes]` and `[Localization]` are out of scope — see
[§5](#5-what-was-deliberately-left-out).

Source: `$ELYSIUM_WORK_ROOT/research/reference-source/exe-src/src_launcher/launcher_main/`
— `vtm_hooks.cpp` (the patch table, 5,743 lines) and `res/loader.ini` (the switch list).
Authors: Behar (original plugins), Psycho-A (adaptation and new fixes), with C6,
L@Zar0, int9, Drog Black Tooth, SomeCommentDoe, Frank7777 and AJ credited per fix.
The file is **CP1251** — 138 comment lines are Russian and render as mojibake in a UTF-8
reader.

Evidence tags: **[bin]** = byte-verified against the install's own DLL (raw PE read, this
document's default); **[corpus]** = resolved to a function or string in
`$ELYSIUM_WORK_ROOT/research/ghidra/corpus/`; **[loader]** = the patch author's stated
intent, not independently confirmed; **[inferred]** = reasoned from surrounding code.

## 0. How the loader's offsets map into the corpus

`vtm_hooks.cpp` addresses every patch as `base_<module> + 0x<offset>`, where `base_` is the
runtime `GetModuleHandle` result. **Corpus address = image base + loader offset**, with the
image bases below. The loader's own inline comments carry absolute addresses from the
author's machine (`200FA4A8`, `1F651FE3`) — those are that process's load bases and should
be ignored; only the offsets are portable.

| Module | Image base | In corpus? | Loader patch sites |
|---|---|---|---|
| `engine.dll` | `0x20000000` | yes, 7,430 fn | 41 |
| `client.dll` | `0x10000000` | yes, 17,045 fn | 613 |
| `vampire.dll` | `0x10000000` | yes, 38,562 fn | 12 |
| `GameUI.dll` | `0x10000000` | yes, 2,690 fn | 21 |
| `vguimatsurface.dll` | `0x10000000` | yes, 1,569 fn | 75 |
| `MaterialSystem.dll` | `0x10000000` | yes, 1,479 fn | 1 |
| `vampire_python21.dll` | `0x1E100000` | yes, 1,415 fn | 30 |
| `shaderapidx9.dll` | `0x10000000` [inferred] | **no** | 17 |
| `filesystem_stdio.dll` | `0x10000000` [inferred] | **no** | 7 |

Three independent confirmations of the mapping: `base_engine + 0xFA494` lands in the
function that references `'Available memory less than 15MB!!! %i\n'`; `base_client +
0x191FD5` lands in `CViewRender::Render`, which references `'r_anamorphic'`; and
`base_vampire + 0xE7EBC` sits inside `CDialog::fill_packet`, whose string
`'I do not have a valid reply.'` is at `1056368c` — exactly the `0x0056368C` the loader's
comment names. [corpus]

Of the 810 total offsets, **~470 (58%) are widescreen and HUD layout geometry** and are not
catalogued here: Elysium's menu and HUD are new assets, not VtMB reproductions.

## 1. Summary

Bearing: **reproduce** = retail behaviour Elysium should carry; **modernize** = a 2004
engine limitation whose Unreal-native expression replaces it; **n/a** = host-OS or
retail-UI only, no Elysium surface.

| INI key | Defect | Site (corpus) | Bearing |
|---|---|---|---|
| `Fix Claws Stats` | weapon stat panel tests `item_w_fists` only, so Protean claws show wrong stats | `client.dll` `FUN_10184bc0+0x7E8` | reproduce (§2.1) |
| `Floats For Female Fix` | NPC bark WAV path builder takes a branch that suppresses floats for a female PC | `vampire.dll` `FUN_101f4600+0x3C` | reproduce (§2.2) |
| `FOV Reset Fix` | zoom-out zeroes the same FOV field the `fov` cvar writes, clobbering the player's FOV | `vampire.dll` `FUN_10239d80+0x81` | reproduce (§2.3) |
| `Non-Solid Ragdolls` | ragdoll solidity gated on spawnflag bit 2; cutscene actors collide with corpses | `vampire.dll` `CRagdollProp::FUN_10154e40+0x50` | modernize (§2.4) |
| `Fix Model LODs` | `CModelRender::DrawModel` LOD branch compares against `1`, dropping detail early | `engine.dll` `CModelRender::DrawModel+0x151` | modernize (§2.5) |
| `Force Shadows Rebuild` | `CClientShadowMgr` early-outs on a stale cache and never invalidates | `client.dll` `CClientShadowMgr::vfunc6+0xA0` | modernize (§2.6) |
| `No Bullet Tracers` | not a defect — an opt-in cosmetic kill switch | `client.dll` `FUN_10040990+0xA` | reproduce (§2.7) |
| `Fix Histories` | two globals go stale across repeated history tests, then it faults | `client.dll` `FUN_10057dc0` | modernize (§2.8) |
| `Improve Save Speed` | save writer hardcodes compression level 9 at three call sites | `engine.dll` ×3 | modernize (§2.9) |
| `No Coronas Fix` / `Lights Through Walls Fix` | one 0xC8-byte engine routine, replaced wholesale | `engine.dll` `FUN_20065d50` | modernize (§2.10) |
| `Fix Python Exploit` | Python 2.1 honours machine-wide `PYTHONPATH`/`PYTHONHOME`/`Software\Python` | `vampire_python21.dll` ×14 | modernize (§2.11) |
| `Enable Console` | console availability gated behind a launch/registry check | `engine.dll` `FUN_2008ec20+0x59` | n/a (§3) |
| `Stop Trash Dumps` | five stray scratch files written to the mod directory | 5 string sites | n/a (§3) |
| `Developer Mode Fix` | a per-bone warning printf floods the log with injected animations | 2 string sites | n/a (§3) |
| `Dialog Box Width` | dialog box width is an `832.0` double constant | `client.dll` `.rdata 0x10228FA8` | n/a (retail UI) |
| `Correct Bubbles` | Character Editor bubble size is a `16.0` double constant | `client.dll` `.rdata 0x1022A388` | n/a (retail UI) |
| `Lists Height Scale` | list row height is a `20` imm32 in the list panel constructor | `GameUI.dll` `FUN_1002be20+0x17` | n/a (retail UI) |
| `Fix Refresh Rate` | display refresh clamped in the D3D9 shader API | `shaderapidx9.dll` | unresolved (§4) |
| `Fix Texture Memory` | texture budget capped below available VRAM | `shaderapidx9.dll` | unresolved (§4) |
| `Fix Paths` | path separator detection tests `\` but not `/` | `filesystem_stdio.dll` | unresolved (§4) |
| `Window Focus Fix` | — | host-side `SetWindowPos`, no in-DLL patch | n/a |
| `Desktop Resolution` | — | host-side registry seed, no in-DLL patch | n/a |
| `Default Clan Fix` | — | host-side `config.cfg` line removal | n/a |
| `Loading Delay Sec` | — | host-side `Sleep()` in the Python hook | n/a |
| `Fix Screenshots` † | screenshot writer overflows above 2048 px | `MaterialSystem.dll` `FUN_100168d0+0x1C6` | n/a |
| `Exit by Alt+F4` † | — | `engine.dll` `FUN_200a38a0+0x4E` key tracker | n/a |

† Read from `[Bugs]`/`[Tweaks]` by `ReadHooksINI()` but **absent from the shipped
`loader.ini`** — undocumented keys, on by default.

## 2. The defects, in detail

### 2.1 Claws report fists' stats — `client.dll` `FUN_10184bc0`

`FUN_10184bc0` (`0x10184bc0`, 0x1883 bytes) is the **item stat-string builder** — it
references `CostStrings`, `UIRequirementStrs`, `ItemRangeStrs`, `ItemFireRateStrs`,
`ItemReloadStrs` and `Item_Reload_Str_Mapping`. [corpus] At `+0x7D4`:

```
10185394  MOV  byte ptr [ESP + 0xa74], 0x0
1018539c  CALL dword ptr [EDX + 0x37c]     ; fetch wielded item classname
101853a2  PUSH 0x10275410                  ; "item_w_fists"
101853a7  PUSH EAX
101853a8  CALL 0x101e28e0                  ; strcmp
101853ad  MOV  EDX, dword ptr [0x105e8138]
101853b6  TEST EAX,EAX
101853b8  SETZ BL                          ; BL = "this is the unarmed weapon"
```

[bin] `68 10 54 27 10 50 E8 33 D5 05 00`

The unarmed-weapon test recognises **`item_w_fists` and nothing else**. Protean's
`item_w_claws` therefore falls through to the armed-weapon formatting path and the panel
prints stats that do not describe the claws. The loader redirects the `strcmp` at
`0x101853A8` to a two-way compare:

```c
int Fix_Weapon_Stats(const char* cmp_str, const char* cmp_a) {
    char* cmp_b = "item_w_claws";
    int result = strcmp(cmp_str, cmp_a);
    if (result) result = strcmp(cmp_str, cmp_b);
    return result;
}
```

**Bearing: reproduce the corrected behaviour.** Claws are an unarmed weapon; the retail
miscategorisation is a lookup bug with no design intent behind it. Credited to int9.
[loader] See [`disciplines.md`](disciplines.md) and
[`wielded_weapons.md`](wielded_weapons.md).

### 2.2 NPC floats suppressed for a female PC — `vampire.dll` `FUN_101f4600`

`FUN_101f4600` (`0x101f4600`, 0x291 bytes, `__thiscall`) builds bark/float voice paths — it
references `'%s\\%s_%d.wav'`, `'%s\\%s.wav'` and `'NULL.WAV'`. [corpus] At `+0x3A`:

```
101f4635  CALL 0x100092be
101f463a  TEST AL,AL
101f463c  JNZ  0x101f4655        ; 75 17  -- skip the variant lookup
101f463e  MOV  ECX,ESI
101f4640  CALL 0x1001320a        ; variant index
101f4645  CMP  EAX,-0x1
101f4648  JLE  0x101f4655
101f464a  MOV  ECX,ESI
101f464c  CALL 0x1001320a
101f4651  MOV  dword ptr [ESP + 0x14],EAX
101f4655  ...                    ; path assembly continues
```

[bin] `E8 84 4C E1 FF 84 C0 75 17`

The predicate at `0x100092BE` gates the `_%d` variant-index lookup. The loader patches
`75` → `EB`, making the skip **unconditional**: the variant lookup never runs and the plain
`'%s\\%s.wav'` form is always used. That the fix is "always take the branch retail only
sometimes takes" is the tell — retail's predicate returns the wrong answer for a female
player character, the bark resolves to a file that does not exist, and the float is
silently dropped. [inferred — the predicate itself is not decompiled here]

**Bearing: reproduce the corrected behaviour.** Barks should play for either PC sex.
Credited to AJ. [loader] See [`audio_pipeline.md`](audio_pipeline.md).

### 2.3 Zoom-out clobbers the player FOV — `vampire.dll` `FUN_100d2d80` / `FUN_10239d80`

Two functions write the same field, `+0x1E78` on the player/view object — only three
accesses to that offset exist in all of `vampire.dll`. [corpus]

The `fov` cvar setter (`FUN_100d2d80`, `0x100d2d80`, refs `'"fov" is "%d"\n'`) writes the
converted value:

```
100d2dba  CALL dword ptr [EDX + 0x150]
100d2dc1  CALL 0x10431447                    ; convert
100d2dc9  MOV  dword ptr [ESI + 0x1e78],EAX
```

[bin] `E8 81 E6 35 00 83 C4 04 89 86 78 1E 00 00`

The zoom-out path (`FUN_10239d80`, `0x10239d80`, `__fastcall`) writes **zero** into it:

```
10239dfb  CALL dword ptr [EDX + 0x534]
10239e01  MOV  dword ptr [EDI + 0x1e78],EBX  ; EBX = 0
```

[bin] `89 9F 78 1E 00 00`

So any zoom-out resets the field of view to the sentinel `0` instead of restoring the
configured value. The loader hooks both sites to cache and restore.

**Bearing: reproduce the corrected behaviour**, and record the RE fact: **`+0x1E78` is the
player FOV field**. This meets the camera contract in
[`../architecture/camera-architecture.md`](../architecture/camera-architecture.md), where
`config.cfg` and the Unofficial Patch's aliases keep governing FOV. Credited to C6. [loader]

### 2.4 Ragdoll solidity is spawnflag-gated — `vampire.dll` `CRagdollProp`

`CRagdollProp::FUN_10154e40` (`0x10154e40`, 0xE8 bytes) computes a solidity argument from
the entity's spawnflags and passes it into ragdoll setup:

```
10154e88  MOV  ECX, dword ptr [ESI + 0x204]   ; m_spawnflags
10154e8e  PUSH 0x1
10154e90  SHR  ECX, 0x2                       ; C1 E9 02
10154e93  AND  ECX, 0x1                       ; 83 E1 01   -> (spawnflags >> 2) & 1
10154e9a  PUSH ECX
```

[bin] `8B 8E 04 02 00 00 6A 01 C1 E9 02 83 E1 01`

The loader overwrites the six bytes of `SHR`/`AND` with `B9 01 00 00 00 90`
(`MOV ECX,1; NOP`), forcing the argument to `1` for **every** ragdoll regardless of
spawnflags. The decompile shows a matching tail that toggles flag bit 2 through
`CBaseEntity::AddFlag2`/`RemoveFlag2` on spawnflag `0x20`. [corpus]

**Bearing: modernize.** Retail leaves corpses solid unless a mapper set the flag, so
cutscene actors collide with bodies on the floor. In Unreal this is a collision
channel/profile decision on the ragdoll, not a per-entity spawnflag — record it as a named
modernization rather than reproducing the flag. Credited to C6. [loader] See
[`phy_vphysics.md`](phy_vphysics.md) and
[`physics-interaction.md`](physics-interaction.md).

### 2.5 LOD selection drops detail early — `engine.dll` `CModelRender::DrawModel`

`CModelRender::DrawModel` (`0x200a6640`, 0x334 bytes; named from the string
`"CModelRender::DrawModel"` at `201a624c`) [corpus]:

```
200a6789  MOV  EAX, dword ptr [EBX + 0xe0]
200a678f  CMP  EAX, 0x1                     ; 83 F8 01
200a6792  PUSH EBP
200a6793  JNZ  0x200a67f9                   ; skip the LOD block
```

[bin] `8B 83 E0 00 00 00 83 F8 01 55 75 64`

The loader writes `0x00` over the `0x01` immediate at `0x200a6791`, inverting which value
of `[EBX+0xE0]` enters the block. Reported as *"no-more-LOD fix for models"* — retail
selects a reduced-detail model in a case where it should not. [loader]

**Bearing: modernize.** Unreal's own LOD/Nanite selection replaces this entirely. The value
here is knowing that retail's model detail is *not* a faithful reference when comparing
screenshots. Credited to Drog Black Tooth. [loader] See
[`mdl-coverage-and-gaps.md`](mdl-coverage-and-gaps.md).

### 2.6 Shadows never invalidate — `client.dll` `CClientShadowMgr::vfunc6`

`CClientShadowMgr::vfunc6` (`0x100d7ec0`, 0xFB bytes, `__thiscall`, vtable slot 6) [corpus]
early-outs on a three-part cache comparison:

```
100d7f50  CMP  byte ptr [0x104cf008],BL
100d7f56  JNZ  0x100d7f68
100d7f58  CMP  dword ptr [0x104cf1b4],ESI
100d7f5e  JNZ  0x100d7f68
100d7f60  CMP  dword ptr [0x104cf004],EAX
100d7f66  JZ   0x100d7fb6                   ; all three match -> skip the rebuild
```

[bin] `39 05 04 F0 4C 10 74 4E`

When all three cached values match, the rebuild is skipped. Some state changes dirty none
of the three, so shadows stay stale indefinitely. The loader hooks the function entry and
forces a rebuild every *n* frames (`Force Shadows Rebuild = 180`) — a timer, not a fix for
the missing invalidation.

**Bearing: modernize.** Unreal's shadow invalidation is not this cache. Recorded because it
explains stale-shadow artefacts in retail reference captures. See
[`lighting.md`](lighting.md).

### 2.7 Bullet tracers — `client.dll` `FUN_10040990`

`FUN_10040990` (`0x10040990`, 0xC3 bytes) is the tracer emitter: it is reached through
`vfunc7`/`vfunc39` and ends in `FUN_100d9da0(param_3, param_4, 5000, 1)`. [corpus]

```
10040990  MOV  EAX,[0x104a0d50]
10040995  SUB  ESP,0x3c
10040998  TEST EAX,EAX
1004099a  JZ   0x10040a4f                   ; 0F 84 AF 00 00 00
```

The loader writes `E9 B0 00 00` at `0x1004099A`, turning the six-byte `JZ` into
`JMP 0x10040A4F` — the *same target*, now unconditional, skipping the whole emit. [bin]

**Bearing: reproduce the retail behaviour** (tracers on). This is not a defect but an opt-in
cosmetic kill switch, default `0` in `loader.ini`. Listed for completeness because it names
the tracer emitter. Credited to C6. [loader] See [`effects.md`](effects.md).

### 2.8 History-test crash — `client.dll` `FUN_10057dc0`

`FUN_10057dc0` (`0x10057dc0`, 0x44 bytes, reached through `vfunc21`) [corpus] reads the
client global at `0x104A0D50`, calls a pair of helpers, and dispatches to one of two
handlers. It never resets the two adjacent globals `0x104A0D5C` and `0x104A0D60`:

```
10057dc0  PUSH ESI
10057dc1  MOV  ESI, dword ptr [0x104a0d50]
10057dc7  TEST ESI,ESI
10057dc9  JZ   0x10057e00
...
10057df2  CALL 0x100a6740
10057dfb  CALL 0x100a6760
```

The loader replaces the entry (`RedirectJump` plus 0x3F NOPs) with a transcription that
zeroes both globals first, then runs the original body — i.e. retail carries state across
repeated history selections in the character creator until it faults. Neither global is
named in the corpus. [corpus]

**Bearing: modernize.** A state-reset bug with no design content; Elysium's character
creation does not share this structure. Recorded so the crash is not mistaken for a data
problem when comparing against retail.

### 2.9 Save compression level — `engine.dll`, three sites

Three call sites push a literal compression level of `9` (maximum) into the save writer.
All three are byte-identical `6A 09` (`PUSH 9`) and are patched to `6A 01` (`PUSH 1`). [bin]

| Corpus addr | Containing function |
|---|---|
| `0x200985FA` | **uncarved** — Ghidra left `0x20098523`–`0x2009866F` without a function |
| `0x2009896A` | `FUN_20098880`, which references `'Compression error writing to disk.'` |
| `0x200C53AF` | `FUN_200C5220`, called from `FUN_200C5610` |

**Bearing: modernize.** Level 9 on 2004 hardware is why saving stalls. Elysium's save
format is its own and disposable (see [`../../CLAUDE.md`](../../CLAUDE.md)), so nothing here
transfers except the explanation. Credited to C6. [loader] See
[`savegame_format.md`](savegame_format.md).

The `0x200985FA` gap is a **corpus coverage hole** worth noting: `FUN_20098520` is a 3-byte
thunk and the next carved function starts at `0x20098670`.

### 2.10 Coronas and lights-through-walls — `engine.dll` `FUN_20065d50`

`FUN_20065d50` (`0x20065d50`, 0xC8 bytes, single caller `FUN_20065af0`) [corpus] is replaced
wholesale: the loader `RedirectJump`s the entry, NOPs all 0xC8 bytes, and runs a
hand-transcribed rewrite. Two separate INI switches (`No Coronas Fix`,
`Lights Through Walls Fix`) share the one site, so the two reported symptoms — light coronas
vanishing, and light bleeding through world geometry — are **the same routine**. Credited to
C6 and SomeCommentDoe. [loader]

**Bearing: modernize.** Both symptoms are early-Source light-visibility artefacts that
Unreal's lighting does not have. See [`lighting.md`](lighting.md) and
[`light-attribution.md`](light-attribution.md).

### 2.11 Python reads machine-wide environment — `vampire_python21.dll`

Retail embeds an unmodified Python 2.1, so the interpreter honours the **host machine's**
`PYTHONPATH`, `PYTHONHOME`, `PATH` and `HKxx\Software\Python` — any Python installed on the
player's system can inject modules into the game. The loader renames every such lookup
string in place: `PYTHONPATH` → `PYVTMBPATH`, `PYTHONHOME` → `PYVTMBHOME`,
`Software\Python` → `Software\Troika`, `PATH` → `VDIR`, and overrides the default import
list with `./bin/python;./bin/python/lib/`. It also swaps two `\` for `/` in `lib\os.py`
and `<prefix>\lib`.

The loader branches on a DLL version check (`pythondll_version`). **The corpus holds the v1
"vanilla" DLL**: the v1 offset table lands on exact corpus strings — `0x1E189CEC` =
`'PYTHONPATH'`, `0x1E1952BC` = `'PYTHONHOME'`, `0x1E189CB4` =
`'.\DLLs;.\lib;.\lib\plat-win;.\lib\lib-tk'` — while **all sixteen v1.2-patch offsets miss**.
[corpus] The remaining v1 misses are mid-string offsets that patch a substring of a longer
literal.

**Bearing: modernize.** Elysium does not embed CPython 2.1; see
[`python_bridge.md`](python_bridge.md). Recorded because it explains a class of "works on my
machine" retail bug report, and because it pins the corpus DLL's provenance.

## 3. Host-side and log-hygiene entries

Not defects in game logic; listed so the catalogue is complete.

- **`Enable Console`** — `engine.dll` `FUN_2008ec20+0x59`. Retail computes an availability
  flag into the global at `0x20A3F438`:
  `TEST EAX,EAX / JNZ / MOV AL,0x1 / JMP / XOR AL,AL`, [bin] `85 C0 75 04 B0 01 EB 02 32 C0`.
  The loader zeroes the `0x01` immediate at `0x2008EC79`, forcing the flag off so the
  console is always permitted.
- **`Stop Trash Dumps`** — five `.rdata` filename strings overwritten with `"nul"`:
  `engine.dll 0x20198550` `'hl2.tmp'`, `vampire.dll 0x10571EE8` `'stats.txt'`,
  `GameUI.dll 0x10068290` `'unfound.txt'`, `client.dll 0x102D64C4` `'voice_ban.dt'`, and one
  in `shaderapidx9.dll`. [corpus]
- **`Developer Mode Fix`** — the same 63-character format string,
  `"chained model %s has a bone %s that isn't in chaining model %s\n"`, exists in both
  `engine.dll 0x201972A8` and `client.dll 0x102AD3B8` [corpus] and is blanked in both. Mods
  with injected animations trip it per bone per frame, and the logging dominates load time.
- **`Window Focus Fix`**, **`Desktop Resolution`**, **`Default Clan Fix`**,
  **`Loading Delay Sec`** — implemented entirely in the loader process: `SetWindowPos`, a
  registry seed under `Software\Troika\Vampire\Settings`, a `config.cfg` line removal, and a
  `Sleep()` inside the `PyRun_SimpleString` hook. No in-DLL patch site exists.

## 4. Unresolved — modules absent from the corpus

`shaderapidx9.dll` and `filesystem_stdio.dll` are **not dumped into the Ghidra corpus**, so
these three entries have offsets but no resolvable function:

| INI key | Site | What the loader does |
|---|---|---|
| `Fix Refresh Rate` | `shaderapidx9.dll + 0x16691`, `+0x166A1` (0x21 bytes), `+0x166B9` | replaces a branch and 0x21 bytes of body, or zeroes two comparisons (`XOR EAX,EAX` + NOP) |
| `Fix Texture Memory` | `shaderapidx9.dll + 0x262CC` | NOPs 2 bytes and sets the byte at `+0x8` to `0x7F` |
| `Fix Paths` | `filesystem_stdio.dll + 0x3F31`, `+0x3F3E` | sets both comparison immediates to `0x2F` (`'/'`) — retail's separator check tests `'\'` only |

Adding those two modules to the corpus dump would close the gap; both are small.

## 5. What was deliberately left out

- **`[Modern OS]`** (`15MB Fix`, `Low Textures Fix`, `DWM Fix`) — signed/unsigned comparison
  bugs that only surface on more than 2 GB of RAM and on Windows 7+, plus a DWM composition
  workaround. Host-OS compatibility, not game behaviour.
- **`[WideScreen]`, `[UI Fixes]`** — ~470 offsets of HUD and menu layout geometry. Elysium's
  menu and HUD are new assets, not VtMB reproductions.
- **`[Mods]`** — mod-directory path routing for the loader's own mod support.
- **`[Localization]`** — extended-ANSI support and translated-string plumbing.

## 6. Authoring defects in the shipped dialogue data *(appended 2026-09-06, dialogue plan D1/D2)*

Not loader patch sites — defects in the shipped `.dlg` corpus itself, found while reproducing
`CDialogDependency` (`docs/vtmb/game_runtime.md` §5). The port reproduces retail rather than
repairing them; each is recorded here so a later data-side decision has the evidence.

| Defect | Evidence | Port |
|---|---|---|
| **A col-4 with two Python halves can never pass.** `ParseDep` (`0x100e9290`) trims each Python half into the *same* buffer at `+0x1c`, so the second overwrites the first, and the compound then evaluates a simple dependency that was never claimed — `TestSimple`'s `Unhandled dialog dependency` arm, which is false. One shipped row does this: `IsClan(pc,"Ventrue") & G.Patch_Plus == 1`, a Ventrue Plus line that is dead in retail. **[corpus]** | 1 of 7,216 gated PC rows | reproduced (fails closed); `Elysium.Substrate.DlgDependency` pins it |
| **Bands authoring more than four responses.** `get_pc_responses` (`0x100e82d0`) stops at 4 because the wire packet holds four dependency slots; 699 bands author more than four rows. They are gate-exclusive by design, so an overflow is an authoring slip rather than an engine limit. **[corpus]** | 699 bands | M-CAP: the port shows every passing row and logs a band whose *enabled* count exceeds four |
| **Text-less `#` rows.** 635 NPC rows carry fewer than two characters of male text, so `read_line_data` (`0x100e61d0`) never stores them and any link to their id dangles. **[corpus]** | 635 of 50,393 rows | reproduced — the parser drops them, so link resolution matches retail |

## 7. Code defects in the scripted camera *(appended 2026-09-07, camera recovery)*

Not loader patch sites either — defects in the shipped `CBaseCineCam` / `C_BaseCineCamera` /
`CCameraAnimated` code, found while recovering the scripted-shot chain
(`docs/vtmb/camera-view-modes.md`). The first two are invisible in shipped content and the third is
load-bearing: it is why the dialogue camera re-resolves its anchors every tick. The last four were
added by the RG-A pass (`+0x564` triple, `m_iVFlags`, the unshipped `CamMode` arms); the
`point_player` row is the one of those that is reachable on every shipped shot.

| Defect | Evidence | Bearing |
|---|---|---|
| **`RemainingTime` NaNs its own pan.** `FUN_100010f0` (`client.dll` `0x100010f0`–`0x100011bb`), the remaining-translation-time solve `SyncRotateOnMove` divides the angular delta by, has two arithmetic faults: the trapezoid arm's acceleration distance is `(vmax − v)²/2a` instead of `(vmax² − v²)/2a`, correct only from `v = 0`; and the triangle arm's radicand `2a − 0.5(d − R2)` is an acceleration minus a distance and goes **negative** whenever `d − R2 > 4a`, so `sqrtf` returns NaN and the turn rate with it. At the shot-parse defaults (`MoveSpeed 150`, `MoveAccel 50`) the triangle arm is entered for `d ≤ 450` and the radicand is negative past `d − R2 > 200`, so a default-rate `SyncRotateOnMove` shot travelling 200–450 units freezes its pan. **[corpus]** | the band needs `MoveSpeed > 2·MoveAccel`; none of the 32 shipped `SyncRotateOnMove` shots has that (all author their own rates — `jack.txt` 500/250, `special-case.txt` 300/450, `andreibasement.txt` 80/40), so it is reachable only from the parse defaults, which no such shot uses | **split — reproduced, with the radicand clamped (M6, ruled and landed 2026-09-07, slice SC1).** `ElysiumCam::RemainingTranslationSeconds` is now retail's three arms verbatim, **trapezoid defect included**, because that defect is the timing all 33 shipped `SyncRotateOnMove` shots are tuned against — `jack.txt` closing 100 u from rest answers retail's 0.17 s again where the port's previous correct kinematic solve answered 1.27 s and panned ~7× slower. **Only the triangle arm's radicand is clamped at zero**, which removes the NaN state and nothing else: that state needs `MoveSpeed > 2·MoveAccel`, which no shipped shot has, so no shipped pan changes. Asserted by `Elysium.Substrate.CameraTracker` |
| **`Point2` without `Point1` aims at the origin.** The shot parser `FUN_100721e0` (`vampire.dll`) raises the target-point flag by *order of presence*, `flags \|= 1 << (count + 2)`, while writing the anchor into the fixed Point1/Point2 slot. A file authoring only `Point2` fills slot 3 but raises `0x04` (Point1), and the look-at solve `FUN_1006f670` then reads the empty slot 2 — a shot that looks at `(0,0,0)`. **[corpus]** | no shipped file authors `Point2` alone | **reproduced** (SC6, 2026-09-07). `FElysiumCameraShotDef` carries `TargetPointCount` and two presence flags raised by the same `1 << (count + 2)`, and the look-at reads the **flags**, not the slots. Unreachable in the corpus, and the client parser `client.dll FUN_10028d20` has the identical bug (RC11) |
| **The anchor cache reads anchor 0's flags for every anchor.** `FUN_1006f010` (`vampire.dll`) decides whether to return the shot-start cache or resolve live by testing `AttachType None` (`0x2000`) — but it passes the literal index `0` to the flag accessor, so every one of the four anchors is judged by the **Start** anchor's flags. A shot with no `Start` block reads a zeroed record, so `0x2000` is clear and all four anchors re-resolve live every tick. **[corpus]** | every shipped dialogue shot: `jack.txt` and `dialogdefault.txt` have no `Start` block, so retail re-resolves their `Bone: Bip01 Head` target every tick and the authored `AngularTolerance [10, 10, 10]` is what holds the camera still | **reproduced, bug included** (SC6, 2026-09-07). `ElysiumCameraShots::LatchesAnchors` is the same test — `Start` present **and** its `AttachType` is `None` — and `FElysiumCameraDirector::Tick` applies it per shot, reading back a per-anchor shot-start cache. The bug is not optional: the anchor-0 test is what decides which shots latch, so reproducing the latch without it would change 55 of the 66 shipped files. The shot is not hypothetical either — `special-case.txt`'s `Follow` authors `Start { AttachType None }` and is the one shipped shot that latches |
| **`point_player` is authored on the director and read on the camera.** `m_bForcePlayerLook` (`+0x5e8`, key `point_player`) is initialised to **1** by the `CBaseCineCam` constructor `FUN_1006d620` (`*(byte *)(this + 0x17a) = 1`). `FUN_10070780` copies **only** `m_bDrawPlayer` (`+0x640`) from the director onto the runtime camera, and `FUN_10070470` / `CBasePlayer::SetCamera` / `StartPlayerDialog` copy nothing at all; a map-placed director is never adopted, so its own `+0x5e8` is never read. The keyvalue is parsed into a field the shot never consults. **[corpus]** | 35 of 51 shipped `camera_cinematic` entities author it — **26 of them `0`** — and all 35 values are discarded. Only `CFuncMonitor::vfunc39` `0x10115260`, `CBasePlayer::HandleAnimEvent` 4050 `0x10178a10` and the `"Intrusion"` opener `FUN_10225070` clear it, each on the camera it just created | **reproduce** — the runtime camera defaults to *forcing* the subject's gaze at the look-at every tick (`FUN_1006f8f0` tail → `FUN_10178590` → `FUN_10178550`), the director's `point_player` is parsed and dead, and only those three openers opt out |
| **`m_iVFlags` bits `0x2` and `0x4` are read and cleared but never set.** `CBasePlayer::SetAnimation` (slot 417, `0x10164870`) selects activity `0xfa3` ("Grapple Release") on bit `0x2` and `0xfc9` ("Seductive Release") on bit `0x4`, then runs `RemoveVFlags(6)`. That `RemoveVFlags(6)` is the only writer of either bit anywhere in `vampire.dll`: `AddVFlags` `0x10181580` is called with `1` and `8` and nothing else, and `ToggleVFlags` `0x10181620` / `ClearVFlags` `0x10181600` have no callers. **[corpus]** | both release animations are unreachable in the shipped build | **reproduce the consumer, record the gap** — the port carries the two release requests with a comment that no shipped path raises them |
| **`CamMode 3` null-dereferences on anchor loss.** `FUN_1006fe00` (`vampire.dll`) opens with `if (!handleLive(+0x610)) this->+0x55c = 0;` — but the expiry gate in `FUN_1006f7d0` is `+0x55c > 0.0f && +0x55c < curtime`, which `0` fails, so the think does not end. It then resolves the same handle with a weaker test (index and serial only, no entry-pointer check), gets `NULL`, and calls `ent->WorldSpaceCenter()` (`vfunc 0x300`) on it. **[corpus]** | unreachable: the mode-3 factory `FUN_100705d0` has **zero callers** in the image, so no shipped path ever creates a `FollowEntity` camera | **reproduce the no-expiry, drop the crash** — the port keeps "a dead anchor does not end the shot" (the camera freezes on its last pose) and early-outs instead of faulting |
| **`CamMode 4`'s per-tick FOV publish is dead.** `FUN_1006f870` publishes `m_flFOV = shot->FieldOfView` every server tick, but `C_BaseCineCamera::Update` (`client.dll` `FUN_10001a20`) takes `if (m_CamMode == 4) { FUN_10002200(dt); return; }` and `FUN_10002200` is an empty `RET`; the copy-through that would move `m_flFOV` into `m_flCurFov` lives in the fall-through arm mode 4 never reaches, and shot start `FUN_10002210` never writes `m_flCurFov` either. So an `Animated` camera renders pose *and* FOV frozen at shot start. **[corpus]** | zero `camera_animated` entities ship, though `special-case.txt` authors the `Animated` shot for it with a "don't change this" comment | **reproduce** — the arm is ported as written, including the publish that nothing consumes |
| **Both crossfade pushers write the kind byte `0`, so the fold's target arm is unreachable.** `CBasePlayer::SetCameraViewEntity` `FUN_1017d280` at `0x1017d386` and the target setter `FUN_1017d460` at `0x1017d55f` are both `MOV byte ptr [ESI],0x0`, while `CHL2_Player::SetupVisibility` discriminates on that byte (`0x10352485 TEST DL,DL ; JNZ 0x1035259e`). A superseded **target** camera is therefore folded through the **view** arm: its `vfunc0xC8` viewpoint, `vfunc0xC0` roll and `vfunc0xC4` FOV are crossfaded into the published *view* override and it consumes `w[0]`, the view channel's coverage, while the target point itself simply snaps to the new target. The fade list's kind-1 branch (`0x1035259e`-`0x1035261f`) never executes in a shipped run. **[corpus]** | reachable wherever a map supersedes a camera target: `SetHeadAsCameraTarget` / `SetBodyAsCameraTarget` / `FadeHead…` / `FadeBody…` fire on every `CBaseCombatCharacter` (`sm_hub_1` fires `!playercontroller.SetBodyAsCameraTarget`), and because `CBaseCombatCharacter` overrides neither slot 48 nor slot 49 the drag is toward the `CBaseEntity` defaults — FOV **75.0** and roll **0.0** | **reproduce — ruled 2026-09-07, slice SC3.** `FElysiumCameraOverrideChannel::PushOutgoing` writes `EElysiumCameraOverrideKind::View` from both setters with both addresses named at the assignment, and the fold's kind-1 arm is kept as dead-but-present code so the shape stays retail's. Asserted by `Elysium.Substrate.CameraOverride` — a superseded target drags the published view origin and pulls the FOV toward 75 |
