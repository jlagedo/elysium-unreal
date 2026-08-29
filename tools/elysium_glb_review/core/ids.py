"""Asset identities and how they resolve to files in the corpus.

Every seam names its neighbours with a `vtmb:<kind>:<path>` string and nothing else --
there is no index and no path baked into any GLB. Resolution is therefore pure string
work plus a corpus root, but three irregularities make a naive rule wrong:

* an animation bank lives under `characters/`, not an `animation-banks/` directory of
  its own, because the banks are exported through the character exporter;
* a material names its surface property with a bare name (`glass`) while a character
  names the same thing `vtmb:surface-property:glass`;
* some identities deliberately name nothing at all.

Lookups key on the full relative path from the identity. The corpus holds 1189
duplicated stems under `materials/` and 987 under `textures/` -- `black.glb` alone
exists nine times -- so a basename is not an identity.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

PREFIX = "vtmb:"

#: Identity kind -> the corpus directory its product lives in.
SEAM_DIRECTORY = {
    "material": "materials",
    "texture": "textures",
    "surface-property": "surface-properties",
    "character-body": "characters",
    "animation-bank": "characters",
}

#: Kinds that name a real thing but have no exported product to open.
UNRESOLVABLE_KINDS = frozenset({"missing-material", "sound", "sound-script", "effect"})

#: `asset.generator` is written by exactly one exporter per seam, so it identifies a
#: file's seam without guessing from its path.
GENERATOR_SEAM = {
    "Elysium Character GLB Exporter": "characters",
    "Elysium Material GLB Exporter": "materials",
    "Elysium Texture GLB Exporter": "textures",
    "Elysium Surface-property GLB Exporter": "surface-properties",
}


@dataclass(frozen=True)
class AssetId:
    """A parsed `vtmb:` identity."""

    kind: str
    path: str
    raw: str

    @property
    def resolvable(self) -> bool:
        """Whether an exported product is expected to exist for this identity.

        False is a statement about the seam's design, not about a broken export. A
        `vtmb:missing-material:` sentinel names a studio texture that resolves to no
        VMT; the engine draws its error checker and so should the reviewer.
        """
        return self.kind in SEAM_DIRECTORY

    @property
    def is_sentinel(self) -> bool:
        return self.kind == "missing-material"


def parse(identity: str) -> AssetId | None:
    """Parse a `vtmb:` identity, or return None if the string is not one.

    A sentinel carries an extra field (`vtmb:missing-material:<slot>:<name>`), so the
    split keeps everything after the kind as the path.
    """
    if not identity.startswith(PREFIX):
        return None
    remainder = identity[len(PREFIX) :]
    kind, separator, path = remainder.partition(":")
    if not separator:
        return None
    return AssetId(kind=kind, path=path, raw=identity)


def relative_path(asset: AssetId) -> Path | None:
    """The asset's file path relative to the corpus root."""
    directory = SEAM_DIRECTORY.get(asset.kind)
    if directory is None:
        return None
    return Path(directory, *asset.path.split("/")).with_suffix(".glb")


def resolve(identity: str | AssetId, root: str | Path) -> Path | None:
    """Locate an identity's product under `root`, or None if it names no product.

    Returns a path whether or not the file exists; a missing file is a corpus
    integrity finding, not a resolution failure, and the two are reported separately.
    """
    asset = parse(identity) if isinstance(identity, str) else identity
    if asset is None:
        return None
    relative = relative_path(asset)
    return None if relative is None else Path(root) / relative


def surface_property_id(name: str) -> str:
    """Build the identity a bare `$surfaceprop` value refers to.

    Materials write the bare name; characters write the full identity. Both mean the
    same unit under `surface-properties/`.
    """
    return f"{PREFIX}surface-property:{name.strip().lower()}"


def is_render_target(value: str) -> bool:
    """Whether a texture parameter names an engine-supplied render target.

    Render targets (`_rt_*`) never had a source file, so a binding that names one is
    complete rather than broken.
    """
    return value.strip().lower().lstrip("/").startswith("_rt_")


def seam_of(document: dict) -> str | None:
    """The corpus seam a loaded document belongs to, from its generator string."""
    generator = (document.get("asset") or {}).get("generator", "")
    return GENERATOR_SEAM.get(generator)


def corpus_root_for(path: str | Path) -> Path | None:
    """Infer the corpus root from a file inside it.

    A unit carries its own identity but not the corpus layout, so opening one file by
    hand would otherwise leave the tool unable to follow a single reference. The seam
    directory names are fixed, so the nearest ancestor named after one locates the root.
    """
    seams = set(SEAM_DIRECTORY.values())
    for parent in Path(path).resolve().parents:
        if parent.name in seams:
            return parent.parent
    return None
