"""Typed access to the four ELYSIUM glTF extensions.

Each seam publishes one root extension object plus, for characters, per-node and
per-primitive payloads. The readers here stay deliberately thin: they locate and shape
data, they do not reinterpret it. Anything a unit records about its own completeness --
coverage, omissions, anomalies -- is passed through untouched so the reviewer sees the
exporter's own account rather than this tool's summary of it.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from . import ids

CHARACTER_EXTENSION = "ELYSIUM_vtmb_character"
MATERIAL_REFERENCE_EXTENSION = "ELYSIUM_material_reference"
MATERIAL_EXTENSION = "ELYSIUM_vtmb_material"
TEXTURE_EXTENSION = "ELYSIUM_vtmb_texture"
SURFACE_PROPERTY_EXTENSION = "ELYSIUM_vtmb_surface_property"

ALL_EXTENSIONS = (
    CHARACTER_EXTENSION,
    MATERIAL_REFERENCE_EXTENSION,
    MATERIAL_EXTENSION,
    TEXTURE_EXTENSION,
    SURFACE_PROPERTY_EXTENSION,
)

#: Root extension name per seam directory.
SEAM_EXTENSION = {
    "characters": CHARACTER_EXTENSION,
    "materials": MATERIAL_EXTENSION,
    "textures": TEXTURE_EXTENSION,
    "surface-properties": SURFACE_PROPERTY_EXTENSION,
}

#: Texture parameters whose image carries data rather than colour. Colour space is a
#: property of the binding: a texture unit deliberately records no role of its own.
NON_COLOR_PARAMETERS = frozenset(
    {
        "$bumpmap",
        "$normalmap",
        "$dudvmap",
        "$dudvtexture",
        "$envmapmask",
        "$selfillummask",
        "$masktexture",
        "$spotlightmask",
        "$lightwarptexture",
        "$refracttexture",
    }
)


def root_extension(document: dict, name: str) -> dict | None:
    return (document.get("extensions") or {}).get(name)


def extension_of(document: dict) -> tuple[str, dict] | None:
    """The seam extension a document carries, as a (name, payload) pair."""
    for name in (
        CHARACTER_EXTENSION,
        MATERIAL_EXTENSION,
        TEXTURE_EXTENSION,
        SURFACE_PROPERTY_EXTENSION,
    ):
        payload = root_extension(document, name)
        if payload is not None:
            return name, payload
    return None


def asset_id(document: dict) -> str | None:
    found = extension_of(document)
    if found is None:
        return None
    return ((found[1].get("identity") or {}).get("asset")) or None


@dataclass(frozen=True)
class Reference:
    """One outgoing edge from a unit to another unit."""

    role: str
    identity: str
    #: Where the reference was found, for reporting.
    origin: str
    #: The parameter that carried it, when the seam records one.
    parameter: str | None = None
    #: The exporter's own verdict on whether the target exists in the install.
    resolved: bool = True


def material_references(document: dict) -> list[Reference]:
    """Material identities named by a character's core materials."""
    references: list[Reference] = []
    for index, material in enumerate(document.get("materials") or []):
        extension = (material.get("extensions") or {}).get(MATERIAL_REFERENCE_EXTENSION)
        if not extension:
            continue
        identity = extension.get("material")
        if identity:
            references.append(
                Reference(
                    role="material",
                    identity=identity,
                    origin="materials[%d]" % index,
                )
            )
    return references


def dependencies(payload: dict) -> list[Reference]:
    """The dependencies rows of any seam's root extension."""
    rows = []
    for index, entry in enumerate(payload.get("dependencies") or []):
        identity = entry.get("asset")
        if not identity:
            continue
        rows.append(
            Reference(
                role=entry.get("role", "unknown"),
                identity=identity,
                origin="dependencies[%d]" % index,
            )
        )
    return rows


def texture_bindings(payload: dict) -> list[Reference]:
    """A material's texture bindings, minus engine-supplied render targets.

    A binding with no asset names a render target the engine creates at runtime; it
    never had a source file, so it is complete rather than broken.
    """
    rows = []
    for index, entry in enumerate(payload.get("textureBindings") or []):
        identity = entry.get("asset")
        if not identity:
            continue
        rows.append(
            Reference(
                role="texture",
                identity=identity,
                origin="textureBindings[%d]" % index,
                parameter=entry.get("parameter"),
                resolved=bool(entry.get("resolved", True)),
            )
        )
    return rows


def surface_property_reference(payload: dict) -> Reference | None:
    """A material's surface property, promoted from a bare name to an identity."""
    name = payload.get("surfaceProperty")
    if not name:
        return None
    return Reference(
        role="surface-property",
        identity=ids.surface_property_id(name),
        origin="surfaceProperty",
    )


def outgoing_references(document: dict) -> list[Reference]:
    """Every identity a document names, whatever seam it belongs to."""
    found = extension_of(document)
    if found is None:
        return []
    name, payload = found

    references = list(dependencies(payload))
    if name == CHARACTER_EXTENSION:
        references.extend(material_references(document))
    elif name == MATERIAL_EXTENSION:
        references.extend(texture_bindings(payload))
        reference = surface_property_reference(payload)
        if reference is not None:
            references.append(reference)
    return references


def is_non_color(parameter: str | None) -> bool:
    return bool(parameter) and parameter.strip().lower() in NON_COLOR_PARAMETERS


@dataclass
class Coverage:
    """A unit's own account of what it did and did not carry."""

    mapped: list = field(default_factory=list)
    unresolved: list = field(default_factory=list)
    unsupported: list = field(default_factory=list)
    typed_unidentified: list = field(default_factory=list)
    omitted_proven: list = field(default_factory=list)

    @property
    def clean(self) -> bool:
        """Whether the exporter accounted for everything it read."""
        return not self.unresolved and not self.unsupported


def coverage(payload: dict) -> Coverage:
    block = payload.get("coverage") or {}
    return Coverage(
        mapped=block.get("mapped") or [],
        unresolved=block.get("unresolved") or [],
        unsupported=block.get("unsupported") or [],
        typed_unidentified=block.get("typedUnidentified") or [],
        omitted_proven=block.get("omittedProven") or [],
    )
