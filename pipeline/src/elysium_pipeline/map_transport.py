"""The per-map cutover flags, read offline -- the Python twin of `ElysiumMapTransport`.

`Config/DefaultElysium.ini` carries `UElysiumMapTransportSettings` (R4.6), the tracked, reviewable
list that answers "is this map on the new lane" on purpose rather than by accident of whichever
producer last ran. The C++ side is `Source/ElysiumUE/Public/ElysiumMapTransportSettings.h`; this
module is what the bake reads, so one edit to one file moves both halves of a map.

Two lists live in that section, and they are deliberately not the same list:

* `MapsOnNewTransport` (R4.6) -- the map's **entity / collision / environment** resolvers read the
  baked `DA_<map>_*` assets instead of the `.ents`/`.hulls`/`.env` sidecars.
* `MapsOnV2Models` (R5.1) -- the map's **geometry and props** come from the V2 lanes: the map bake
  authors world/sky/brush meshes and prop placements from the published map root unit, and the
  runtime resolves prop meshes and the skin table under `/ElysiumBaked/Meshes` (R1) instead of
  `/ElysiumBaked/Shared/Meshes`. `sp_theatre` is exactly why the two lists are separate: it is on
  the R4.6 transport (its entity/environment assets exist) but its 3,661-unit model corpus has not
  been imported, so pointing its prop resolver at the V2 root would resolve nothing.

**The `+Key=Value` array syntax is parsed here, not by `configparser`.** Unreal writes one `+Key=`
line per array element, which `configparser` rejects as a duplicate option
(`DuplicateOptionError`) -- a reader that wraps it in `except configparser.Error` silently falls
back to defaults instead of reading the file. This module reads the ini line by line, which is what
the format actually is.
"""

from __future__ import annotations

from pathlib import Path
from typing import Iterable

from elysium_pipeline import paths

#: The settings object's ini section, matching `UCLASS(Config = Elysium)` on the C++ side.
SECTION = "/Script/ElysiumUE.ElysiumMapTransportSettings"
#: R4.6: entity / collision / environment assets instead of sidecars.
NEW_TRANSPORT_KEY = "MapsOnNewTransport"
#: R5.1: geometry and props from the V2 lanes.
V2_MODELS_KEY = "MapsOnV2Models"

CONFIG_RELATIVE = Path("Config") / "DefaultElysium.ini"


def config_path(root: Path | None = None) -> Path:
    return (Path(root) if root is not None else paths.repo_root()) / CONFIG_RELATIVE


def read_array(key: str, root: Path | None = None) -> list[str]:
    """Every value of one `+Key=` array in the map-transport section, in file order.

    A missing file or section is an empty list, never a raise: an unlisted map is the safe default
    everywhere both flags are consulted.
    """

    path = config_path(root)
    try:
        text = path.read_text(encoding="utf-8-sig")
    except OSError:
        return []
    return list(_array_values(text.splitlines(), key))


def _array_values(lines: Iterable[str], key: str) -> Iterable[str]:
    wanted = key.casefold()
    in_section = False
    for raw in lines:
        line = raw.strip()
        if not line or line.startswith((";", "#")):
            continue
        if line.startswith("[") and line.endswith("]"):
            in_section = line[1:-1].strip().casefold() == SECTION.casefold()
            continue
        if not in_section or "=" not in line:
            continue
        name, _, value = line.partition("=")
        # Unreal's array operators: `+` appends, `-` removes, a bare name assigns. Only `+` and the
        # bare form put a value in the list this project ever writes; `-` is honoured so a future
        # removal line reads the same here as it does in the engine.
        operator = name[:1] if name[:1] in "+-." else ""
        if name[len(operator):].strip().casefold() != wanted:
            continue
        stripped = value.strip().strip('"')
        if operator == "-":
            continue
        yield stripped


def is_listed(key: str, map_name: str, root: Path | None = None) -> bool:
    """Case-insensitive membership, matching `ElysiumMapTransport::IsMapOnNewTransport`."""

    folded = (map_name or "").casefold()
    return any(value.casefold() == folded for value in read_array(key, root))


def is_map_on_new_transport(map_name: str, root: Path | None = None) -> bool:
    return is_listed(NEW_TRANSPORT_KEY, map_name, root)


def is_map_on_v2_models(map_name: str, root: Path | None = None) -> bool:
    return is_listed(V2_MODELS_KEY, map_name, root)
