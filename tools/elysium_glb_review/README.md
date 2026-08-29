# Elysium GLB Review

A Blender add-on for reviewing the `export_v2` GLB corpus: 23,414 units across four
seams (characters, materials, textures, surface-properties).

Every unit lists its `ELYSIUM_*` extensions in `extensionsRequired`, so Blender's stock
glTF importer refuses all of them:

```
RuntimeError: Error: Extension ELYSIUM_material_reference is not available on this addon version
```

This add-on is what makes the corpus openable. It is not optional.

## Requirements

- **Blender 5.2 LTS.** The add-on uses the slotted-Action API and the current node
  idioms; it declares `blender_version_min = "5.2.0"` and is not tested below it.
- Nothing else. The code uses only Python's standard library and the numpy Blender
  bundles. It never imports `elysium_pipeline`, which targets a different Python.

Blender is found through `ELYSIUM_BLENDER`, then `PATH`, then the usual install
locations. Set `ELYSIUM_BLENDER` to the full path to `blender.exe` if it is elsewhere.

## Setup

### Packaged install

```
uv run elysium blender install
```

This validates the manifest, builds the extension archive into
`$ELYSIUM_WORK_ROOT/glb_review/dist/`, registers an extension repository named
`elysium`, and installs the add-on enabled. It prints the module name Blender will use:

```
installed and enabled as bl_ext.elysium.elysium_glb_review
```

Re-run it after any change to pick up the new code.

### Editing in place

For a change-and-reload loop, symlink the source into a local extension repository
instead. Create the repository from **Preferences ▸ Get Extensions ▸ Repositories ▸ +
▸ Add Local Repository**, outside any Blender-managed directory, then:

```
mklink /d "C:\blender-elysium-repo\elysium_glb_review" "E:\dev\elysium-unreal\tools\elysium_glb_review"
```

After editing, use **Blender menu ▸ System ▸ Reload Scripts**. The add-on carries the
`_needs_reload` idiom so submodule edits are picked up.

> **Uninstalling a symlinked extension from Preferences can delete the symlink target on
> Windows — that is the git working tree.** Remove the repository entry or the symlink by
> hand instead of using the Remove button.

## Corpus root

The add-on needs to know where the corpus is: a unit carries its own identity but records
nothing about the corpus layout, so without a root it can open one file and follow none
of its references.

It is resolved in this order:

1. The **Corpus Root** field in **Preferences ▸ Add-ons ▸ Elysium GLB Review**. The
   preferences panel lists which seam directories it can see.
2. Failing that, the directory is inferred from the file being imported, by walking up to
   the nearest ancestor named after a seam. Opening any unit from inside a corpus works
   without configuring anything.

The field is pre-filled from `ELYSIUM_EXPORT_V2_ROOT`, or `$ELYSIUM_WORK_ROOT/exports_v2`.
Blender launched from a shortcut inherits no shell variables, so treat that as a
convenience and set the preference.

## Usage

### Browsing

**Elysium ▸ Browse** in the 3D viewport sidebar (`N`) lists the corpus without opening
anything. Pick a seam, press refresh, and filter the result three ways: the **Subtree**
menu narrows to a first-level directory (`monster`, `npc`, `pc`, `shared`, `gibs` for
characters), the list's own search box matches on the identity path, and the column
sorts alphabetically.

The list is built from filenames and sizes alone — 0.1 s for the 484 characters, about a
second for the 11,624 materials — because parsing every unit to list it would cost forty
seconds. Highlighting a unit reads that one file and shows what it contains:

```
npc/common/blood_doll/blood_doll
  bones 88 | clips 1 | materials 13 | LODs 7 | morph targets 53
  banks declared 1 | slots with no VMT 2
```

Materials report their shader family, texture count and surface property; textures report
size, format, type and mip count; surface properties report their physics or say that
they declare none and inherit. Anything a unit says about its own gaps — an untranscribed
shader family, a parse anomaly, an include-stub bank with no clips, a texture below its
declared size — appears as a warning on the row.

The **Open Selected** button does whatever the seam allows: a character imports, a texture
decodes into an image (shown in an Image Editor if one is open), a material is built onto
a plane so its shader can be looked at. A surface property has nothing to instantiate, so
the detail box is all there is.

Reading one unit takes 0–110 ms, so browsing stays responsive; the two largest animation
banks are about a second.

There is no thumbnail grid. Blender's Asset Browser needs assets marked inside `.blend`
libraries with generated previews, which for 484 characters is a batch job in its own
right rather than something the browser can do live.

### Importing

**File ▸ Import ▸ Elysium GLB (.glb)**, or `uv run elysium blender review <path>`, or the
Browse panel above. Multi-select works in the file dialog.

Two options:

- **Animations** — `No clips` (default) or `All clips`. A shared bank declares up to 722
  clips and a body's full closure runs past 1,700, so nothing is imported unless asked
  for. Filtering `andrei.glb` takes 1.5 s against 9.7 s unfiltered.
- **Rebuild Materials** — resolve each material identity against the corpus, decode its
  textures and build a shader. On by default.

A character unit carries no textures at all: its core materials are neutral placeholders
whose only real content is a `vtmb:material:` identity. Appearance comes from resolving
that identity against `materials/`, and from there against `textures/`. With no corpus
root the import still succeeds and the materials stay grey.

### Panels

The **Elysium** tab in the 3D viewport sidebar (`N`):

- **Corpus** — the active root, the import operator, and the integrity report.
- **Browse** — the corpus listing described above.
- **Animation Banks** — the bank closure of the selected body, and clip loading.
- **Character** — the selected body's identity, its coverage block, and its full
  extension payload as a collapsible tree.
- **Material** — the identity the primitive named, the VMT shader family, and an explicit
  list of what was **not** reproduced.

### Animation banks

A body carries only its own clips — usually one, a ragdoll pose — and names its banks by
identity. Those names are not the whole story: banks include other banks, and several of
the ones a body names directly are include stubs holding nothing. `blood_doll` declares
one bank; the clips actually reachable from it live across **35 files and 1,722 clips**.

Select an imported body and open **Elysium ▸ Animation Banks**, then press Scan. The list
shows every reachable bank with its clip count, marking include stubs and any bank that
was named but never exported. Highlighting one lists its clips; **Load All** takes the
bank, **Load Highlighted** takes a single clip.

Loading a single clip out of a 674-clip bank takes about three seconds, almost all of it
parsing that bank's JSON. Loading the whole closure is not offered: at this median it
would be minutes of import and a `bpy.data.actions` nobody can navigate.

A bank is a character body in its own right, so importing it brings a proxy mesh and a
second armature. Both are discarded; only the Actions are kept, and each is bound to the
body's armature explicitly. Assigning an Action to a second armature does **not** bind a
slot on its own, and an unbound Action animates nothing while reporting no error.

Clips address bones by name, which is usually every bone the body has. Not always:
`shared/female/move_and_ranged` animates a `bush hook` bone a blood doll does not carry,
so those channels land nowhere. The load says how many bones went unmatched rather than
leaving a clip that silently half-plays.

### Integrity report

```
uv run elysium blender report          # summary plus JSON in the work root
uv run elysium blender report --json   # the report on stdout
```

This needs no Blender. The `core` package imports no `bpy` precisely so a full sweep runs
in the pipeline's own interpreter; a pass over all 23,414 units takes about 40 seconds.
The same sweep is available in Blender from the Corpus panel.

The report separates breakage from facts. A sentinel identity
(`vtmb:missing-material:…`) names a studio texture that resolves to no VMT, and a
render-target parameter names an image the engine makes at runtime; both are complete
statements about the source and are counted rather than reported as problems.

## What is and is not reproduced

VtMB's shaders are fixed-function DirectX 8 programs with no metallic-roughness
parameterisation, so most of what a VMT says has no Principled BSDF equivalent. The
add-on wires only what maps one-to-one and puts everything else in a red frame beside the
shader, unconnected, carrying the source values.

Wired: `$basetexture` to Base Color and Alpha, `$bumpmap`/`$normalmap` through a Normal
Map node, and the core material's alpha mode, cutoff and backface culling.

Framed and labelled: `$envmap` and its masks and tints, `$detail`, `$selfillum`,
`$refract`/`$dudvmap`, `$basetexture2`, water and eye specifics, proxies, and every other
parameter the VMT declares. A material that reproduces everything says so; one that does
not lists what it dropped.

Materials whose shader family has no transcribed selector are marked as such. A sentinel
material draws the engine's error checker, which is what the game draws too.

Textures decode from the KTX2 payload each unit carries. Block-compressed formats go
through a DDS envelope that Blender's own decoder reads; uncompressed formats are
uploaded as pixels. Cubemaps become six images named by face. Array textures are
animation frames; frame 0 is decoded.

Blender logs two warnings per texture the first time it uploads one
(`DDS image '' failed to load data from file`, `falling back to uncompressed`). The
fallback is the intended path and the result is correct.

## Layout

```
core/       No bpy. GLB, identities, KTX2, DDS, seam readers, the browsable index,
            bank closure, surface-property inheritance, the corpus sweep.
adapters/   The glTF import hook, texture decoding, material reconstruction,
            bank clip loading.
ui/         Operators and panels.
tests/      Contract tests for core; tests/blender/ runs inside Blender.
```

The `core` and `adapters` split is load-bearing: it is what lets most of the logic be
tested without launching Blender.

## Tests

Contract tests, no Blender needed:

```
uv run python -m unittest discover -s tools/elysium_glb_review -t tools/elysium_glb_review -p "test_core_*.py"
```

Acceptance inside Blender, against the real corpus:

```
uv run elysium blender install
blender --background --python-exit-code 1 \
  --python tools/elysium_glb_review/tests/blender/test_import.py -- \
  --module bl_ext.elysium.elysium_glb_review --corpus %ELYSIUM_EXPORT_V2_ROOT%
```

Every check there is an assertion because the glTF importer swallows exceptions raised
inside a user extension. Without them a broken add-on produces a silent no-op that reads
exactly like a clean run.

Do not pass `--factory-startup` to either the install steps or the test run: it discards
the extension repository the add-on lives in. Do not use `--addons` either; it enables a
module without adding it to `preferences.addons`, which is the collection the glTF
importer scans.

To keep a test run away from your own Blender configuration, point
`BLENDER_USER_RESOURCES` at a scratch directory before installing.
