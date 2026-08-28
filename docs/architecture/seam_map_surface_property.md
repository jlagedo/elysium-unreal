# Surface-property GLB seam

This document defines one binary glTF 2.0 unit for one named VtMB surface-property record. Format
facts remain owned by `docs/vtmb/surface_properties.md`.

## Unit identity

```text
<VTMB>/Vampire/pack*.vpk -> scripts/surfaceproperties.txt#<name>
  -> vtmb:surface-property:<name>
  -> surface-properties/<name>.glb
```

The table member resolves UP-first. One named entry produces one surface-property GLB.

## GLB structure

A surface-property GLB is a scene-less glTF asset whose semantic value lives in one extension:

```json
{
  "extensionsUsed": [
    "ELYSIUM_vtmb_surface_property"
  ],
  "extensions": {
    "ELYSIUM_vtmb_surface_property": {
      "schemaVersion": "1.0.0",
      "identity": {},
      "sourceResolution": {},
      "base": null,
      "physics": {},
      "movement": {},
      "footsteps": {},
      "impacts": {},
      "sounds": {},
      "gameMaterial": "",
      "dependencies": [],
      "coverage": {}
    }
  }
}
```

## Mapping

| Surface datum | Extension field |
|---|---|
| Inheritance | `base` surface-property asset ID |
| Density, elasticity, friction and thickness | `physics` |
| Speed, jump and climb values | `movement` |
| Left/right footsteps | `footsteps` audio asset IDs |
| Weapon/outcome impact matrix | `impacts` audio asset IDs |
| Physics impact and scrape scripts | `sounds` dependency asset IDs |
| Compact material class | `gameMaterial` |

## LaCroix reference

```text
<VTMB>/Vampire/pack001.vpk
  -> scripts/surfaceproperties.txt#flesh
  -> vtmb:surface-property:flesh
  -> surface-properties/flesh.glb
```

LaCroix's PHY solids reference `vtmb:surface-property:flesh`.

## Coverage

A complete surface-property GLB has zero `unresolved` and zero `unsupported` keys. Validation
resolves the inheritance chain and compares the effective physics, movement, footstep, impact,
sound, and game-material values with the decoded source entry.
