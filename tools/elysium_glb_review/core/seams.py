"""Typed access to the ELYSIUM glTF extensions.

Each seam publishes one root extension object plus, for models, per-node and per-primitive
payloads. The readers here stay deliberately thin: they locate and shape data, they do not
reinterpret it. Anything a unit records about its own completeness -- coverage, omissions,
anomalies -- is passed through untouched so the reviewer sees the exporter's own account
rather than this tool's summary of it.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from . import ids

# -- root extensions: one per unit kind, `ELYSIUM_vtmb_<kind>` with the dashes underscored ----

MODEL_EXTENSION = "ELYSIUM_vtmb_model"
MATERIAL_EXTENSION = "ELYSIUM_vtmb_material"
TEXTURE_EXTENSION = "ELYSIUM_vtmb_texture"
SURFACE_PROPERTY_EXTENSION = "ELYSIUM_vtmb_surface_property"
IMAGE_EXTENSION = "ELYSIUM_vtmb_image"
SOUND_EXTENSION = "ELYSIUM_vtmb_sound"
EXPRESSION_TABLE_EXTENSION = "ELYSIUM_vtmb_expression_table"
SHADER_SOURCE_EXTENSION = "ELYSIUM_vtmb_shader_source"
SHADER_PROGRAM_EXTENSION = "ELYSIUM_vtmb_shader_program"
PARTICLE_EXTENSION = "ELYSIUM_vtmb_particle"
FONT_EXTENSION = "ELYSIUM_vtmb_font"
FONT_LIST_EXTENSION = "ELYSIUM_vtmb_font_list"
#: All five sound-script kinds -- game sound, manifest, soundscape, sentence, DSP preset --
#: share one exporter and one extension, and name their kind inside it.
SOUND_SCRIPT_EXTENSION = "ELYSIUM_vtmb_sound_script"
SOUND_SCHEME_EXTENSION = "ELYSIUM_vtmb_sound_scheme"
SCENE_EXTENSION = "ELYSIUM_vtmb_scene"
DIALOGUE_EXTENSION = "ELYSIUM_vtmb_dialogue"
VDATA_EXTENSION = "ELYSIUM_vtmb_vdata"
UI_RESOURCE_EXTENSION = "ELYSIUM_vtmb_ui_resource"
SCRIPT_EXTENSION = "ELYSIUM_vtmb_script"
MAP_EXTENSION = "ELYSIUM_vtmb_map"
MAP_ENTITIES_EXTENSION = "ELYSIUM_vtmb_map_entities"
MAP_LIGHTING_EXTENSION = "ELYSIUM_vtmb_map_lighting"
MAP_VISIBILITY_EXTENSION = "ELYSIUM_vtmb_map_visibility"
NAV_GRAPH_EXTENSION = "ELYSIUM_vtmb_nav_graph"
ENGINE_CONFIG_EXTENSION = "ELYSIUM_vtmb_engine_config"

#: The one product over other products: the corpus index, written directly at the export root.
CORPUS_INDEX_EXTENSION = "ELYSIUM_vtmb_corpus_index"

#: Every root extension, in the order `extension_of` looks for one. A unit carries exactly one.
ROOT_EXTENSIONS = (
    MODEL_EXTENSION,
    MATERIAL_EXTENSION,
    TEXTURE_EXTENSION,
    SURFACE_PROPERTY_EXTENSION,
    IMAGE_EXTENSION,
    SOUND_EXTENSION,
    EXPRESSION_TABLE_EXTENSION,
    SHADER_SOURCE_EXTENSION,
    SHADER_PROGRAM_EXTENSION,
    PARTICLE_EXTENSION,
    FONT_EXTENSION,
    FONT_LIST_EXTENSION,
    SOUND_SCRIPT_EXTENSION,
    SOUND_SCHEME_EXTENSION,
    SCENE_EXTENSION,
    DIALOGUE_EXTENSION,
    VDATA_EXTENSION,
    UI_RESOURCE_EXTENSION,
    SCRIPT_EXTENSION,
    MAP_EXTENSION,
    MAP_ENTITIES_EXTENSION,
    MAP_LIGHTING_EXTENSION,
    MAP_VISIBILITY_EXTENSION,
    NAV_GRAPH_EXTENSION,
    ENGINE_CONFIG_EXTENSION,
    CORPUS_INDEX_EXTENSION,
)

# -- cross-reference extensions: object-local bindings, never a unit's own root ---------------

MATERIAL_REFERENCE_EXTENSION = "ELYSIUM_material_reference"
MODEL_REFERENCE_EXTENSION = "ELYSIUM_model_reference"
TEXTURE_REFERENCE_EXTENSION = "ELYSIUM_texture_reference"
ASSET_REFERENCE_EXTENSION = "ELYSIUM_asset_reference"

REFERENCE_EXTENSIONS = (
    MATERIAL_REFERENCE_EXTENSION,
    MODEL_REFERENCE_EXTENSION,
    TEXTURE_REFERENCE_EXTENSION,
    ASSET_REFERENCE_EXTENSION,
)

#: Every extension this tool reads, root and cross-reference alike.
ALL_EXTENSIONS = ROOT_EXTENSIONS + REFERENCE_EXTENSIONS

#: The root extensions a corpus directory may hold, keyed on the corpus-relative directory.
#:
#: A key is a whole family directory, so a nested family (`shader-programs/source`,
#: `vdata/items`) is its own key and `expected_extensions` matches the longest one that is a
#: prefix of a unit's path. Two families hold more than one kind: `fonts/` publishes the faces
#: and the one registry unit beside them, and `maps/` publishes a BSP's root unit and its three
#: lump-family sub-units, which are told apart by the unit suffix in the file name rather than
#: by directory.
SEAM_EXTENSION = {
    "models": (MODEL_EXTENSION,),
    "materials": (MATERIAL_EXTENSION,),
    "textures": (TEXTURE_EXTENSION,),
    "surface-properties": (SURFACE_PROPERTY_EXTENSION,),
    "images": (IMAGE_EXTENSION,),
    "sounds": (SOUND_EXTENSION,),
    "expression-tables": (EXPRESSION_TABLE_EXTENSION,),
    "shader-programs/source": (SHADER_SOURCE_EXTENSION,),
    "shader-programs/psh": (SHADER_PROGRAM_EXTENSION,),
    "shader-programs/vsh": (SHADER_PROGRAM_EXTENSION,),
    "shader-programs/fxc": (SHADER_PROGRAM_EXTENSION,),
    "particles": (PARTICLE_EXTENSION,),
    "fonts": (FONT_EXTENSION, FONT_LIST_EXTENSION),
    "sound-scripts": (SOUND_SCRIPT_EXTENSION,),
    "soundscapes": (SOUND_SCRIPT_EXTENSION,),
    "sentences": (SOUND_SCRIPT_EXTENSION,),
    "dsp-presets": (SOUND_SCRIPT_EXTENSION,),
    "sound-schemes": (SOUND_SCHEME_EXTENSION,),
    "scenes": (SCENE_EXTENSION,),
    "dialogues": (DIALOGUE_EXTENSION,),
    "vdata/system": (VDATA_EXTENSION,),
    "vdata/items": (VDATA_EXTENSION,),
    "vdata/camerashots": (VDATA_EXTENSION,),
    "vdata/hackterminals": (VDATA_EXTENSION,),
    "vdata/signs": (VDATA_EXTENSION,),
    "vdata/precache": (VDATA_EXTENSION,),
    "ui-resources": (UI_RESOURCE_EXTENSION,),
    "scripts": (SCRIPT_EXTENSION,),
    "maps": (
        MAP_EXTENSION,
        MAP_ENTITIES_EXTENSION,
        MAP_LIGHTING_EXTENSION,
        MAP_VISIBILITY_EXTENSION,
    ),
    "nav-graphs": (NAV_GRAPH_EXTENSION,),
    "engine-config": (ENGINE_CONFIG_EXTENSION,),
    # The corpus index is the one unit that publishes at the export root itself, under no family
    # directory, so its key is the empty relative directory.
    "": (CORPUS_INDEX_EXTENSION,),
}


#: The three lump-family sub-units of a map, keyed on the suffix their file name carries before
#: `.glb`. A map file with no such suffix is the root unit.
MAP_UNIT_EXTENSION = {
    "entities": MAP_ENTITIES_EXTENSION,
    "lighting": MAP_LIGHTING_EXTENSION,
    "visibility": MAP_VISIBILITY_EXTENSION,
}


def expected_extensions(directory: str, filename: str | None = None) -> tuple[str, ...]:
    """The root extensions admitted below `directory`, which is corpus-relative.

    The longest declared family that is a prefix of the path wins, so a unit named by its whole
    relative path (`vdata/items/item_w_katana.glb`) answers as readily as one named by its
    family alone. An undeclared directory admits nothing, which reads as "no expectation"; the
    export root itself, which is the empty relative directory, admits the corpus index.

    Passing the unit's own file name narrows a family that holds more than one kind: the four
    map kinds share one directory and are told apart by the suffix, so `ch_cloud_1.entities.glb`
    admits only the entities extension and `ch_cloud_1.glb` only the root one.
    """
    path = str(directory).replace("\\", "/").strip("/").lower()
    if not path:
        return SEAM_EXTENSION.get("", ())
    while path:
        found = SEAM_EXTENSION.get(path)
        if found is not None:
            if path == "maps" and filename:
                return (_map_unit_extension(filename),)
            return found
        head, separator, _tail = path.rpartition("/")
        if not separator:
            break
        path = head
    return ()


def _map_unit_extension(filename: str) -> str:
    """The one map kind a file name declares through its unit suffix."""
    stem = str(filename).replace("\\", "/").rpartition("/")[2].lower()
    if stem.endswith(".glb"):
        stem = stem[: -len(".glb")]
    return MAP_UNIT_EXTENSION.get(stem.rpartition(".")[2], MAP_EXTENSION)


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
    """The seam extension a document carries, as a (name, payload) pair.

    Only a root extension answers: a cross-reference extension binds one glTF object and never
    stands for the unit, so it is not searched here.
    """
    for name in ROOT_EXTENSIONS:
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


def reference_identity(container: dict | None, name: str) -> str | None:
    """The identity a cross-reference extension binds to one glTF object.

    Every `ELYSIUM_*_reference` carries the referenced unit under the single key `asset`.
    `container` is the glTF object holding the extension, not the extension itself.
    """
    extension = ((container or {}).get("extensions") or {}).get(name) or {}
    return extension.get("asset") or None


def material_references(document: dict) -> list[Reference]:
    """Material identities named by a model's core materials."""
    references: list[Reference] = []
    for index, material in enumerate(document.get("materials") or []):
        identity = reference_identity(material, MATERIAL_REFERENCE_EXTENSION)
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
    if name == MODEL_EXTENSION:
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
