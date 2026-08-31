"""The colour/data role a texture asset imports under, derived from its material bindings.

A texture unit does not own its meaning; the material binding does (`seam_map_material.md`), and
Unreal decides sRGB per asset. So the lane reads every material unit's `dependencies[]` once,
collects the set of parameters that bind each texture, and applies the one rule
`seam_map_texture.md` → "Import" → "Role, sRGB and compression" states:

- any colour binding, or no binding at all -> `colour`, sRGB on;
- only normal-class bindings -> `data-normal`, sRGB off;
- only mask-class bindings, or any other data-only set -> `data-mask`, sRGB off;
- a colour binding beside a data binding is a *conflict*: the asset is `colour` and a `_linear`
  twin carries the data role, because Source filtered the raw bytes and only an sRGB-off asset
  filters the same way.

The rule lives here alone so the material lane, which must pick sampler types that agree with
the asset, reads the same tables rather than restating them.
"""

from __future__ import annotations

from collections import defaultdict
from pathlib import Path

from elysium_pipeline.formats.unit_contract.container import GlbContainerError, read_glb

MATERIAL_EXTENSION = "ELYSIUM_vtmb_material"

COLOUR_PARAMETERS = frozenset({
    "$basetexture", "$basetexture2", "$detail", "$detail2", "$iris", "$selfillumtexture",
    "$texture2", "$envmap", "$lightwarptexture", "%tooltexture",
})
NORMAL_PARAMETERS = frozenset({
    "$bumpmap", "$bumpmap2", "$normalmap", "$dudvmap", "$dudvtexture",
})
MASK_PARAMETERS = frozenset({
    "$envmapmask", "$masktexture", "$basealphaenvmapmask", "$spotlightmask",
    "$cloudalphatexture", "$blendmodulatetexture",
})

ROLE_COLOUR = "colour"
ROLE_NORMAL = "data-normal"
ROLE_MASK = "data-mask"


def data_role(parameters: set[str]) -> str:
    """The data role a binding set resolves to, colour bindings set aside.

    `data-normal` only when every data binding is normal-class; a mixed data set (`$bumpmap` in
    one material, `$envmapmask` in another) is `data-mask`, as is any other data-only set.
    """

    data = {parameter.lower() for parameter in parameters} - COLOUR_PARAMETERS
    return ROLE_NORMAL if data and data <= NORMAL_PARAMETERS else ROLE_MASK


def role_for(parameters: set[str]) -> tuple[str, bool, bool]:
    """`(role, srgb, conflict)` for one texture's binding parameters (lower-cased)."""

    parameters = {parameter.lower() for parameter in parameters}
    colour = bool(parameters & COLOUR_PARAMETERS)
    data = bool(parameters - COLOUR_PARAMETERS)
    if not parameters or (colour and not data):
        return ROLE_COLOUR, True, False
    if colour and data:
        return ROLE_COLOUR, True, True
    return data_role(parameters), False, False


def material_units(export_v2_root: Path) -> list[Path]:
    root = Path(export_v2_root) / "materials"
    return sorted(root.rglob("*.glb")) if root.is_dir() else []


def scan_material_bindings(export_v2_root: Path) -> tuple[dict[str, set[str]], list[tuple[str, str]]]:
    """`{texture asset id: {parameters}}` over every material unit, plus the units that failed.

    Only resolved `texture` dependency rows count: an unresolved row names a texture the install
    does not have, so it binds no asset this lane creates.
    """

    bindings: dict[str, set[str]] = defaultdict(set)
    failures: list[tuple[str, str]] = []
    for unit in material_units(export_v2_root):
        try:
            document, _binary = read_glb(unit)
        except (GlbContainerError, OSError, ValueError) as error:
            failures.append((unit.as_posix(), str(error)))
            continue
        root = (document.get("extensions") or {}).get(MATERIAL_EXTENSION)
        if not isinstance(root, dict):
            failures.append((unit.as_posix(), f"not a {MATERIAL_EXTENSION} unit"))
            continue
        for row in root.get("dependencies") or ():
            if not isinstance(row, dict) or row.get("role") != "texture" or not row.get("resolved"):
                continue
            asset = row.get("asset")
            parameter = row.get("parameter")
            if isinstance(asset, str) and isinstance(parameter, str) and parameter:
                bindings[asset].add(parameter.lower())
    return dict(bindings), failures
