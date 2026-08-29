"""A browsable index of the corpus.

Built from filenames and sizes alone. The corpus holds 23,414 units and 1.9 GB of
extension JSON, so a browser that parsed every unit to list it would take forty seconds
to open; this takes about a second, and a unit's contents are read only when someone
looks at it.

Identity comes from the path rather than the file, which is sound because the export
writes each unit at the path its identity names. The one exception is a bank: it is
exported as a character body under `characters/`, so a file's identity says
`character-body` where a reference to it says `animation-bank`.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from . import glb, ids, seams

#: Identity kind published by each seam directory.
SEAM_KIND = {
    "characters": "character-body",
    "materials": "material",
    "textures": "texture",
    "surface-properties": "surface-property",
}


@dataclass(frozen=True)
class Unit:
    """One unit as the browser knows it, before anything is read."""

    seam: str
    #: Corpus-relative path, forward-slashed.
    relative: str
    identity: str
    byte_size: int

    @property
    def stem(self) -> str:
        """The identity path, which is what a reviewer recognises."""
        return self.identity.split(":", 2)[-1]

    @property
    def group(self) -> str:
        """The first path segment, for grouping a seam into its subtrees."""
        stem = self.stem
        return stem.split("/", 1)[0] if "/" in stem else ""


@dataclass(frozen=True)
class Details:
    """What one unit says about itself, read on demand."""

    identity: str
    seam: str
    #: Short lines a panel can show without knowing the seam's schema.
    rows: tuple[tuple[str, str], ...]
    #: Non-empty when the unit declares it failed to account for something.
    warnings: tuple[str, ...] = ()


def scan(root: str | Path, seam: str | None = None) -> list[Unit]:
    """List units under `root`, optionally narrowed to one seam."""
    root = Path(root)
    wanted = (seam,) if seam else tuple(SEAM_KIND)
    units: list[Unit] = []

    for name in wanted:
        directory = root / name
        if not directory.is_dir():
            continue
        kind = SEAM_KIND[name]
        prefix_length = len(directory.as_posix()) + 1
        for path in directory.rglob("*.glb"):
            relative = path.as_posix()
            stem = relative[prefix_length:-4]
            units.append(
                Unit(
                    seam=name,
                    relative=path.relative_to(root).as_posix(),
                    identity="%s%s:%s" % (ids.PREFIX, kind, stem),
                    byte_size=path.stat().st_size,
                )
            )

    units.sort(key=lambda unit: (unit.seam, unit.identity))
    return units


def groups(units: list[Unit]) -> list[str]:
    """The distinct first path segments present, for a subtree filter."""
    return sorted({unit.group for unit in units if unit.group})


def _character_rows(document: dict, payload: dict) -> tuple[list, list]:
    rows = [
        ("bones", str(len((payload.get("mdl") or {}).get("bones") or []))),
        ("clips", str(len(document.get("animations") or []))),
        ("materials", str(len(document.get("materials") or []))),
        ("LODs", str(len((payload.get("vtx") or {}).get("lods") or []))),
    ]
    primitives = ((document.get("meshes") or [{}])[0]).get("primitives") or [{}]
    rows.append(("morph targets", str(len(primitives[0].get("targets") or []))))

    banks = [
        reference.identity
        for reference in seams.dependencies(payload)
        if reference.role == "animation-bank"
    ]
    rows.append(("banks declared", str(len(banks))))

    sentinels = sum(
        1
        for material in document.get("materials") or []
        if str(
            ((material.get("extensions") or {}).get(seams.MATERIAL_REFERENCE_EXTENSION) or {})
            .get("material", "")
        ).startswith("vtmb:missing-material:")
    )
    warnings = []
    if sentinels:
        # Normal, not broken: these name studio textures that resolve to no VMT.
        rows.append(("slots with no VMT", str(sentinels)))
    if not document.get("animations") and banks:
        warnings.append("include stub: forwards to other banks, carries no clips")
    return rows, warnings


def _material_rows(payload: dict) -> tuple[list, list]:
    rows = [
        ("shader", str(payload.get("shader", "?"))),
        ("textures", str(len(payload.get("textureBindings") or []))),
        ("parameters", str(len(payload.get("parameters") or []))),
    ]
    surface = payload.get("surfaceProperty")
    if surface:
        rows.append(("surface property", str(surface)))

    warnings = []
    if not (payload.get("shaderResolution") or {}).get("resolved", True):
        warnings.append("shader family has no transcribed selector")
    for anomaly in payload.get("anomalies") or []:
        warnings.append("anomaly: %s" % anomaly.get("role", anomaly))
    return rows, warnings


def _texture_rows(payload: dict) -> tuple[list, list]:
    dimensions = payload.get("dimensions") or {}
    block = payload.get("payload") or {}
    rows = [
        ("size", "%sx%s" % (dimensions.get("width", "?"), dimensions.get("height", "?"))),
        ("format", str(block.get("vkFormat", "?"))),
        ("type", str(block.get("textureType", "?"))),
        ("mips", str(dimensions.get("mipCount", "?"))),
    ]
    if dimensions.get("frames", 1) > 1:
        rows.append(("frames", str(dimensions["frames"])))

    warnings = []
    source = payload.get("sourceFormat") or {}
    if (
        dimensions.get("width") != source.get("sourceWidth")
        or dimensions.get("height") != source.get("sourceHeight")
    ) and source.get("sourceWidth"):
        warnings.append(
            "largest complete level is below the declared %sx%s"
            % (source.get("sourceWidth"), source.get("sourceHeight"))
        )
    return rows, warnings


def _surface_property_rows(payload: dict) -> tuple[list, list]:
    physics = payload.get("physics") or {}
    rows = [("%s" % key, str(value)) for key, value in sorted(physics.items())]
    base = (payload.get("base") or {}).get("name")
    if base:
        rows.insert(0, ("base", str(base)))
    if not physics:
        rows.append(("physics", "declares none; inherited"))
    return rows, []


def details(unit: Unit, root: str | Path) -> Details | None:
    """Read one unit and summarise it.

    Cost is the unit's JSON chunk, which is a few milliseconds for most of the corpus
    and about a second for the two largest animation banks.
    """
    path = Path(root) / unit.relative
    if not path.is_file():
        return None

    document = glb.read_json(path)
    found = seams.extension_of(document)
    if found is None:
        return Details(unit.identity, unit.seam, (), ("no ELYSIUM extension",))
    name, payload = found

    if name == seams.CHARACTER_EXTENSION:
        rows, warnings = _character_rows(document, payload)
    elif name == seams.MATERIAL_EXTENSION:
        rows, warnings = _material_rows(payload)
    elif name == seams.TEXTURE_EXTENSION:
        rows, warnings = _texture_rows(payload)
    else:
        rows, warnings = _surface_property_rows(payload)

    coverage = seams.coverage(payload)
    if not coverage.clean:
        warnings.append(
            "coverage: %d unresolved, %d unsupported"
            % (len(coverage.unresolved), len(coverage.unsupported))
        )
    return Details(unit.identity, unit.seam, tuple(rows), tuple(warnings))
