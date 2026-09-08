"""The baked-unit address contract, shared with FElysiumContentPaths::BakedUnit.

Only identities enter this resolver. Source spellings stay in provenance; path collisions must
be checked over the complete producer manifest before publishing any output.
"""
from __future__ import annotations

from pathlib import Path

from elysium_pipeline.asset_names import safe_name, sound_safe_name

BAKED_MOUNT = "/ElysiumBaked"
KIND_ROOTS = {
    "texture": "Textures", "material": "Materials", "model": "Models",
    "surface-property": "SurfaceProperties", "map": "Maps",
    "expression-table": "ExpressionTables", "sound": "Sounds",
    "particle": "Particles", "scene": "Scenes",
}
LEGACY_ROOTS = {
    "Shared": "R9.2", "Characters": "R8.2", "Props": "R8.4", "Items": "R8.3",
}
PREFIXES = frozenset({"T", "TC", "TA", "MI", "SM", "SK", "SKEL", "A", "BS",
                      "CLOTH", "PHYS", "DYN", "PM", "DA", "NS", "SW"})


class AssetPathError(ValueError):
    """An identity cannot be addressed without violating the baked-asset contract."""


#: The one kind whose segments go through :func:`asset_names.sound_safe_name` instead of
#: :func:`asset_names.safe_name`. Sound keys are the only family that is not injective under the
#: shared fold; that function states why, and no other kind's names change spelling.
SOUND_FOLD_KINDS = frozenset({"sound"})


def _segment(value: str, *, kind: str | None = None, stem: bool = False) -> str:
    """One source segment as a legal name.

    `stem` marks the last segment of a key, which becomes the object name after its class prefix
    and so may keep a leading underscore where the kind's fold allows it; a directory never may.
    """
    if not value or value in (".", "..") or any(c in value for c in "/\\:"):
        raise AssetPathError(f"invalid or reserved source segment: {value!r}")
    if kind in SOUND_FOLD_KINDS:
        if value.startswith("_") and not stem:
            raise AssetPathError(f"invalid or reserved source segment: {value!r}")
        return sound_safe_name(value, stem=stem)
    if value.startswith("_"):
        raise AssetPathError(f"invalid or reserved source segment: {value!r}")
    return safe_name(value)


def _product(prefix: str, name: str, *, folded: bool = False) -> str:
    if prefix not in PREFIXES:
        raise AssetPathError(f"unknown baked class prefix: {prefix!r}")
    return f"{prefix}_{name if folded else _segment(name)}"


def baked_path(kind: str, key: str, prefix: str, role: str | None = None,
               label: str | None = None) -> str:
    """Return a package path from a unit kind/key; labels retain their authored case."""
    if kind not in KIND_ROOTS:
        raise AssetPathError(f"unknown baked kind: {kind!r}")
    raw = key.split("/")
    directory = [_segment(part, kind=kind) for part in raw[:-1]]
    base = _segment(raw[-1], kind=kind, stem=True)
    root = "/".join([BAKED_MOUNT, KIND_ROOTS[kind], *directory])
    if not prefix:
        if kind != "map" or label is not None or role is not None:
            raise AssetPathError("only a map level may omit its class prefix")
        return f"{root}/{base}/{base}"
    if label is not None:
        root += "/" + base
    # `base` is folded already; re-folding it would strip the leading underscore a sound stem is
    # allowed to keep. A label is authored text and folds here for the first time.
    name = (_product(prefix, label) if label is not None
            else _product(prefix, base, folded=True))
    if role is not None:
        name += "_" + _segment(role)
    return root + "/" + name


def baked_unit(asset_id: str, prefix: str, role: str | None = None,
               label: str | None = None) -> str:
    fields = asset_id.split(":", 2)
    if len(fields) != 3 or fields[0] != "vtmb":
        raise AssetPathError(f"expected vtmb:<kind>:<key>, got {asset_id!r}")
    return baked_path(fields[1], fields[2], prefix, role, label)


def corpus_path(kind: str, prefix: str, name: str) -> str:
    if kind not in KIND_ROOTS:
        raise AssetPathError(f"unknown baked kind: {kind!r}")
    return f"{BAKED_MOUNT}/{KIND_ROOTS[kind]}/_Corpus/{_product(prefix, name)}"


def map_package(map_name: str) -> str:
    """Existing map bundle directory; the whole-map namespace move belongs to R9.

    Validate through the shared resolver, but keep the deployed maps addressable until their
    packages and references move together. Models already use the canonical kind namespace.
    """
    canonical = baked_unit("vtmb:map:" + map_name, "").rsplit("/", 1)[0]
    return BAKED_MOUNT + canonical.removeprefix(BAKED_MOUNT + "/Maps")


def validate_landing(package: str, content_root: Path, *, extension: str = ".uasset") -> Path:
    """Bound the actual filesystem path, including this machine's content-root length."""
    prefix = BAKED_MOUNT + "/"
    if not package.startswith(prefix):
        raise AssetPathError(f"package is outside {BAKED_MOUNT}: {package}")
    segments = package[len(prefix):].split("/")
    if any(not part or part in (".", "..") or "\\" in part or ":" in part for part in segments):
        raise AssetPathError(f"invalid package path: {package}")
    path = Path(content_root).resolve().joinpath(*segments).with_suffix(extension)
    if len(str(path)) > 240:
        raise AssetPathError(f"baked filename exceeds 240 characters ({len(str(path))}): {path}")
    return path


def assert_unique_paths(products) -> None:
    """Reject folded collisions, including unit/composite and per-label collisions.

    Each row is (unfolded product identity, package path). Repeated identical declarations are
    harmless; different identities may never alias, even on a case-insensitive filesystem.
    """
    owners = {}
    for identity, package in products:
        key = package.casefold()
        prior = owners.setdefault(key, identity)
        if prior != identity:
            raise AssetPathError(f"baked path collision at {package}: {prior!r} and {identity!r}")
