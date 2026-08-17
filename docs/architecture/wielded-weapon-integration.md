# Wielded weapon integration

How a third-person held weapon is bound to a character in Unreal, and what the bake must emit for
that binding to work. The VtMB behaviour this reproduces — item model roles, the equip transaction,
the follow-attach composition and the shipped corpus — is owned by `docs/vtmb/wielded_weapons.md`
and is not restated here.

First-person geometry is a different corpus with a different owner; see
`docs/vtmb/camera-view-modes.md` for the view-mode gate.

## The composition rule, as a transform

VtMB poses a wielded weapon by evaluating the weapon entity's own sequence and then overwriting
every bone whose **name matches** the wearer with the wearer's computed world matrix. For a vertex
skinned to bone `B`:

```
v_world = W_wield(B) · GB_wield(B)⁻¹ · v_bind
```

Two cases follow from whether the wearer declares `B`.

**A matched mount.** `W_wield(B) = W_wearer(B)` directly, so the mesh expressed in `B`'s bind-local
space rides the wearer's bone:

```
v_world = W_wearer(B) · [ GB_wield(B)⁻¹ · v_bind ]
```

**An unmatched mount.** The mount and everything under it run FK off the nearest matched ancestor —
in practice the hand. Writing `L(M)`, `L(S)` for the local transforms the weapon's own clip supplies:

```
W(Hand)·L(M)·L(S) · (GB(Hand)·LB(M)·LB(S))⁻¹
```

When the clip holds a **constant** pose this is one fixed transform on the hand for every vertex
below it, whatever the sub-rig's depth: a thirteen-bone pistol is rigid, because nothing varies
`L(·)` frame to frame.

**The pose the bake captures is the clip's, not the container's bind.** These are not the same
thing — 11 of the 61 collapsible models hold a constant pose that differs from their bind, by as
much as 12.43 in. `LB` cancels only when the clip happens to equal it. The engine evaluates the
clip, so the clip is what the bake reads; the bind pose is a fallback for a model that declares no
clip, and no shipped model needs it.

**Constancy is a measured property of the corpus, not a guarantee of the mechanism.** The manifest
records it per model rather than assuming it; nothing in the export or bake branches on it.

## The export and bake are uniform; binding is runtime metadata

Every one of the 67 real wield models exports and bakes through **one lane**: a skeletal `.eskm`
container whose **reference pose is the model's own clip at frame 0**, carrying its clips, baked as
a `USkeletalMesh` with a private skeleton. The bake does not branch on how a weapon will be bound;
the manifest carries the facts a runtime needs to choose — the mount bone, the hand bone, the
`on_body` scope, per-bone motion with each bone's skinned flag, and the classification — as
metadata, not as bake shape.

Two constraints on the bake lane, both learned from measurement:

- **A wield model never enters the character partition.** `rig_families` merges by shared bone
  names, and 65 of the 67 would join existing character families — one merge puts 25 bodies and 52
  weapons on a single 295-bone skeleton, with re-bake cascades keyed off the family fingerprint.
  Each weapon gets a private `SKEL_<stem>`, the shape the animated-prop bake already uses.
- **The frame-0 reference pose is load-bearing, not cosmetic.** The character bake's silent-track
  rule drops constant non-`Bip01` tracks, and an untracked bone resolves to the mesh's reference
  pose; leader-pose binding gives an unmatched follower bone a static offset from its reference
  pose; the Content Browser preview shows the reference pose. All three read the same stored
  answer, and frame 0 of the clip is the answer retail computes.

## Runtime binding

VtMB's rule — evaluate the weapon's own pose, then overwrite name-matched bones from the wearer —
is served for every *held* weapon by the mechanism this runtime already ships for garments:
`SetLeaderPoseComponent`, the recipe in `ElysiumNpcVisual::InstallGarment`. Leader pose matches
follower bones to the wearer **by name** with no shared-`USkeleton` requirement, installs the tick
prerequisite itself, and gives a bone the wearer lacks a cached rigid offset from its nearest
matched ancestor — which, with the frame-0 reference pose, is exactly retail's composition for a
clip-constant sub-rig. `FName` matching is case-insensitive, as is VtMB's `__strcmpi`.

The two thrown models (`changball`, `gio_spirit`) are free-standing actors playing their own clips,
not attached to a wearer.

There is no held-weapon case that *requires* more than leader pose in the shipped data:

- `w_{m,f}_claws` skin to fingertips 382 of 485 bodies declare; the remainder take the
  reference-pose offset.
- `w_{m,f}_handleclaws`' 30 skinned bones are fully declared by every body that ever equips it —
  the four Chang brothers (and Bach's two bodies, which never do) — so its own 30 clips are wholly
  overwritten on the shipped placement. They are baked anyway, and the transitive `$include` tree
  (1,829 clips of shared character banks) is deliberately **not** chased: the weapon's own container
  clips are the asset's completeness boundary.
- The only visible own-motion in the corpus is `w_m_lockpick`'s 6.357° pick wiggle
  (`w_m_flamethrower`'s movers are unskinned and move no vertices; every female variant and both
  `rifle_steyraug`s are at noise). Its clip is baked, so a runtime graph may restore the wiggle
  later without any pipeline change; until then the weapon holds frame 0.

`FAnimNode_CopyPoseFromMesh` is **not** the mechanism: it has no input pose link — it resets to
reference pose and overwrites matched bones — so it cannot compose over a sequence player.
Reproducing live own-clips under a wearer (the lockpick wiggle, or handleclaws on a hypothetical
non-Chang wearer) takes a small anim graph blending a sequence player against a copy-pose branch
per bone; that is a runtime enhancement consuming already-baked clips, never a bake change.

A melee weapon appears to swing because the prop bone belongs to the **character's** skeleton and
the character's own attack clip moves it; the motion arrives through the character's graph. A
static-mesh optimization for the clip-constant majority remains available later as a bake-side
change — the manifest already carries the mount bone and constancy facts it would key on — and is
taken up only if profiling demands it.

## Assets

Baked packages live under `/ElysiumBaked/Items/Wield/<stem>/SK_<stem>`, each model's animations
under its own package folder. The namespace is an immutable contract, sibling to
`/ElysiumBaked/Items/Props/` for ground models and opposite
`/ElysiumBaked/Characters/Viewmodels/` for first-person geometry.

The runtime resolves a row from `(classname, sex)` through `/ElysiumBaked/Items/DA_WieldModels`, a
`UDataAsset` holding soft references, the binding metadata and the mount bone name. The exporter
writes an engine-neutral JSON manifest; the editor bake consumes it and builds the data asset. A
missing or renamed package therefore fails at bake with a resolvable name rather than at load with
an empty attachment.

## Materials and textures

The wield corpus has its own texture and material wiring; neither pre-existing lane covers it. The
character texture table harvests bodies and banks only, and the animated-prop bake binds no
textures at all — it relies on a map placement copying an already-baked map material, which a
weapon in a hand does not have. So the export decodes the corpus's texture closure (89 distinct
keys across albedo, envmask and bump, skin-family overrides included, all present patch-first) into
the wield corpus's own tree, and the wield bake imports them role-typed (sRGB color / linear mask /
normal map) and binds material instances per slot.

The `.eskm` `MATL` section stays `{name, albedo}`; **render semantics ride the manifest**, the same
split the character pipeline already uses for bindings. Per material the manifest carries the
resolved albedo, envmask and bump keys and the VMT flags — `$envmap` on most of the corpus,
alphatest on the flamethrower grate and both handleclaws, translucent on the Steyr magazine,
translucent+additive on `gio_spirit` — plus any extra **skin family** as material-slot overrides
(`w_{m,f}_fire_axe` family 1, the ghost-form `Transparent` skin, is the one shipped case).

The character body master has no envmap, envmask, bump or translucency parameters, so weapons
instance from their own master family — `M_Wield` (opaque), `M_Wield_Masked`, `M_Wield_Translucent`
and `M_Wield_Additive` under `/Game/VtMB/Materials`, generator-built by
`pipeline/unreal/make_wield_materials.py`. Each carries `Albedo`/`Normal`/`EnvMask` texture
parameters and an `EnvStrength` scalar, flags `used_with_skeletal_mesh`, and the bake selects per
material row by flag precedence additive → translucent → alphatest → opaque (additive blending
subsumes a row that also declares translucent). The bake enumerates the selected master's
parameters before binding and logs any manifest demand the master cannot express, so a
master/manifest drift is loud.

One retail defect is reproduced, not repaired: the handleclaws material named `null` has an empty
20-byte texture payload in the install; its slot records the failure and binds no texture.

## What the character bake must preserve

Two character-side properties are load-bearing, and both are the kind an import optimizer removes:

- **The seven prop bones survive with their exact names.** They carry zero skin weight, so nothing
  about the geometry states they are needed. Stripped, every melee weapon still renders but never
  swings, because its socket no longer exists as an animated bone.
- **Every clip keeps its prop-bone channels, moving or constant.** The bake drops a bank clip's
  appendix tracks when no clip in the container animates them — a generic `BoneNN` hair name denotes
  a different chain on different bodies, so binding one by name delivers a foreign rest pose — and
  the seven mount names are exempt from that rule outright, because each of them denotes the same
  chain on every body that declares it.

  A constant channel is not a redundant one. An untracked bone resolves to the playing mesh's own
  bind, and a bank's stated mount value is frequently not that bind: `character_shared_female_baseball`
  states `handle` 123.5° and 7.5 cm from the bind of every body that plays it, and
  `character_shared_female_pc_g2` states `bush hook` 176.6° and 9.7 cm from `heather`'s. Retail poses
  the mount from the clip, so a dropped constant is a weapon mounted at the wrong fixed offset while
  the body animates. Leaving the decision to a motion threshold also answers by sex: it is what made
  the male locomotion bank carry `Bat` and the female one carry none of the seven.

The character bake verifier asserts both halves for every family and every bank — the built mesh
carries every mount its container declares, and a baked base clip carries every mount channel the
container wrote for that clip. Expectations are read per clip from the container, so a masked
overlay that legitimately owns no mount is not a failure.

Placement accuracy depends on the wield model's bind agreeing with the wearer's, per sex. The bake
carries each model's own bind through unmodified, so agreement and disagreement both reproduce.

## Player and NPC

One path serves both. Equip is the same transaction for a player and an NPC, differing only in the
owner, and sex selects the manifest row. The player additionally suppresses the wielded model in
first person and shows first-person geometry instead — a visibility decision driven by camera mode,
not a second binding system.

## Faithfulness

Binds are emitted as authored. `w_f_bushhook.mdl`'s degenerate identity bind is carried through
rather than corrected, so its placement reproduces retail's; the manifest records the anomaly on
that row so it reads as known rather than as ordinary data. That bind is internally *self-consistent*
— its stored inverse agrees with its FK composition — so the file is not corrupt and the weapon
renders at the hand origin, which is what retail shows.

Every clip is baked whether or not the default binding plays it, so nothing a runtime later needs
is lost at export time. The one behaviour the default binding does not reproduce — the lockpick's
own 6.357° wiggle — is a runtime-graph enhancement away, not a divergence baked into the assets.

## Verification

- Compare each model's stored `pose_to_bone` against its FK-composed inverse bind. Composing the
  full chain against the collapsed transform is an algebraic identity and cannot fail; what can fail
  is a file disagreeing with itself, and that is the check with teeth.
- Record whether every skinned bone lies inside one collapse subtree, and whether the model's own
  clip is **frame-constant** below the mount — measured frame to frame, not against the bind pose,
  which it is free to differ from. Both are manifest facts a later static optimization would key
  on. Note the measurement's shape: most of these clips run 60–75 frames, so counting sequences or
  frames proves nothing, and `Seq.base` is an animdesc byte offset, not an index into `LocalAnims`
  — passing it to an index-taking accessor yields `None` and measures nothing while reporting
  success.
- Assert that the emitted `.eskm` reference pose equals the model's clip at frame 0 on every bone.
- Assert that every wield texture key in the manifest resolved to a decoded albedo, with the
  handleclaws `null` slot as the one recorded exception.
- Resolve every manifest row through the asset registry, reject duplicate package paths, and sweep
  stale packages below the wield root before saving.
