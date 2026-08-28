# Animation-bank GLB seam

This document defines one binary glTF 2.0 unit for one VtMB character animation-bank MDL.
Format facts remain owned by `docs/vtmb/mdl_v2531.md`,
`docs/vtmb/animation_and_movers.md`, and `docs/vtmb/animation_rig_resolution.md`.

## Unit identity

```text
<VTMB>/Vampire/pack*.vpk -> models/character/<bank>.mdl
  -> vtmb:animation-bank:<bank>
  -> animation-banks/<bank>.glb
```

The member resolves UP-first. One bank MDL path produces one animation-bank GLB.

The unit carries its declared skeleton, local animation/sequence data, pose parameters, masks,
blend grids, autolayers, events, movement records, include-model edges, and semantic coverage.

## GLB structure

```text
animation-bank.glb
|- JSON chunk
|  |- skeleton nodes and core animations
|  `- ELYSIUM_vtmb_animation_bank
`- BIN chunk
   |- inverse-bind accessors
   `- animation input/output accessors
```

```json
{
  "extensionsUsed": [
    "ELYSIUM_vtmb_animation_bank"
  ],
  "extensions": {
    "ELYSIUM_vtmb_animation_bank": {
      "schemaVersion": "1.0.0",
      "identity": {},
      "sourceResolution": {},
      "skeleton": {},
      "animations": [],
      "sequences": [],
      "poseParameters": [],
      "dependencies": [],
      "coverage": {}
    }
  }
}
```

## Mapping

| Bank datum | glTF core | Extension |
|---|---|---|
| Bones and bind locals | joint `nodes` | original bone indexes, flags and source identity |
| Animation tracks | `animations`, samplers and channels | animation index, mask and decoded-channel semantics |
| Sequence descriptors | referenced core animations | label, activity, weight, flags, bounds, fade and additive base |
| Blend grids and layers | animation/accessor references | axes, cells, masks, autolayer order and composition semantics |
| Events and movement | extension records | event payloads, displacement, reach, swings, envelopes and combos |
| Include-model graph | dependency asset IDs | original include order and remap identity |

## Dependencies

Each included bank becomes one dependency row:

```json
{
  "role": "animation-bank",
  "sourcePath": "models/character/shared/male/move_and_ranged.mdl",
  "asset": "vtmb:animation-bank:shared/male/move_and_ranged"
}
```

## LaCroix reference

```text
vtmb:character-body:npc/unique/downtown/lacroix/lacroix
  -> vtmb:animation-bank:shared/male/npc_allsequences

<VTMB>/Vampire/pack001.vpk
  -> models/character/shared/male/npc_allsequences.mdl
```

## Coverage

A complete bank GLB has zero `unresolved` and zero `unsupported` records. Validation compares every
bone, local animation, sequence, frame/bone pose, mask, grid, layer, event, movement row, and include
edge with the decoded bank model.
