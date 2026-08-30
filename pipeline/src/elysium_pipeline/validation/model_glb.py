"""Independent structural reader for Model GLB products.

Nothing here imports the writer. The container, the ledger and the cross-unit rules are the unit
contract's; everything below them re-reads the published unit -- and, at export time, the source
members themselves -- and decides on its own whether the two agree.

Two of the export-time re-reads are written twice over, so a decoder fault cannot pass by
reproducing itself: the MDL vertex pool is dequantised here with this module's own reader, and the
VTX strip-group tree is walked here for the per-section triangle count. The byte ledger is not: the
walkers that classify six megabytes of retained compiler payload exist once, in
`formats/model_glb/coverage.py`, and calling them again proves the published table is the one those
walkers produce from these bytes -- not that their classification is right. What is checked
independently of them is arithmetic this module does itself: every table the MDL header declares,
and every vertex pool, mesh table and eyeball table a body-part model record declares, is owned in
the published range table by the owner the seam's owner table names for it; which LOD indices the
legacy VTX twin publishes is derived from the two variant files rather than read out of
`vtx.comparison`; and every `omitted-proven` range carries its recorded reason.
"""

from __future__ import annotations

import struct
from pathlib import Path
from typing import Any, Mapping, Sequence

from elysium_pipeline.formats.model_glb import coverage as model_coverage
from elysium_pipeline.formats.model_glb.model import (
    MODEL_EXTENSION,
    PARTICLE_EVENTS,
    SCHEMA_VERSION,
    SOUND_EVENTS,
    ROLES,
    SPLIT_ROTATION_FLAG,
    SHAPES,
    STATIC_PROP_FLAG,
    family_of,
    normalize_model_key,
)
from elysium_pipeline.formats.unit_contract import (
    MATERIAL_REFERENCE,
    MODEL_REFERENCE,
    UnitValidationError,
    completeness,
    generator,
    read_glb as _read_glb,
    reject_opaque_source,
    validate_accessors,
    validate_container,
    validate_extension_root,
    validate_ledgers,
    validate_sceneless,
)

#: The identity namespace every unit of this seam publishes in.
ASSET_PREFIX = "vtmb:model:"

#: `asset.generator` a unit of this seam declares.
GENERATOR = generator("Model")

#: The dependency roles this seam is allowed to name.
DEPENDENCY_ROLES = frozenset(
    {"material", "model", "expression-table", "surface-property", "sound", "particle"}
)

#: The extension keys the seam declares after the contract's five.
KIND_KEYS = (
    "coordinateTransform",
    "mdl",
    "vtx",
    "physics",
    "materialBindings",
    "facial",
    "procedural",
    "secondaryMotion",
    "cloth",
    "anomalies",
    "omissions",
)

_VERTEX_STRIDES = {0: 44, 1: 12, 2: 8}
_MODEL_STRIDE = 224
_MESH_STRIDE = 60
_EYEBALL_STRIDE = 140
_STRIPGROUP_STRIDE = 20
_SOURCE_TO_GLTF_SCALE = 0.0254


class ModelGlbValidationError(UnitValidationError):
    """A written Model GLB violates glTF or the Elysium model extension contract."""


def read_glb(path: Path) -> tuple[dict[str, Any], bytes]:
    """Parse a published unit; the container rules are the contract's."""

    try:
        return _read_glb(Path(path))
    except ValueError as error:
        raise ModelGlbValidationError(str(error)) from error


def _fail(message: str) -> None:
    raise ModelGlbValidationError(message)


def _i32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<i", data, offset)[0]


def _u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def _check_declarations(document: Mapping[str, Any], root: Mapping[str, Any]) -> None:
    if document.get("asset", {}).get("generator") != GENERATOR:
        _fail(f"asset.generator is {document.get('asset', {}).get('generator')!r}, not {GENERATOR!r}")
    used = list(document.get("extensionsUsed") or [])
    required = list(document.get("extensionsRequired") or [])
    if used != required:
        _fail("extensionsUsed and extensionsRequired disagree")
    for name, needed in (
        (MATERIAL_REFERENCE, bool(document.get("materials"))),
        (MODEL_REFERENCE, bool((root.get("mdl") or {}).get("includeModels"))),
    ):
        if needed and name not in used:
            _fail(f"{name} is bound by the unit but not declared")
        if not needed and name in used:
            _fail(f"{name} is declared and never used")
    tail = tuple(list(root)[5:])
    if tail != KIND_KEYS:
        _fail(f"{MODEL_EXTENSION} root carries {list(tail)} after the contract keys")


def _check_identity(root: Mapping[str, Any]) -> str:
    identity = root["identity"]
    asset = str(identity["asset"])
    key = asset[len(ASSET_PREFIX):]
    try:
        normalized = normalize_model_key(key)
    except ValueError as error:
        raise ModelGlbValidationError(str(error)) from error
    if normalized != key:
        _fail(f"identity {asset!r} is not the normalized model key {normalized!r}")
    model_path = str(identity.get("modelPath", ""))
    if model_path != f"models/{key}.mdl":
        _fail(f"identity.modelPath {model_path!r} does not select {asset!r}")
    if identity.get("family") != family_of(key):
        _fail(f"identity.family {identity.get('family')!r} is not the segment below models/")
    if identity.get("shape") not in SHAPES:
        _fail(f"identity.shape {identity.get('shape')!r} is not one of {list(SHAPES)}")
    # Empty as this seam exported it; the corpus index rewrites it in place with the roles other
    # units assign the model, and a published unit is read back in both states.
    roles = identity.get("roles")
    if not isinstance(roles, list) or any(role not in ROLES for role in roles):
        _fail(f"identity.roles is not a subset of the model role vocabulary {list(ROLES)}")
    if list(roles) != [role for role in ROLES if role in roles]:
        _fail("identity.roles is not in the vocabulary's order, or repeats a role")
    paths = identity.get("sourcePaths") or [identity.get("sourcePath")]
    members = [str(row.get("path")) for row in root["sourceResolution"]["members"]]
    if list(paths) != members:
        _fail("identity source paths disagree with the member table")
    return key


def _check_shape(root: Mapping[str, Any]) -> None:
    """`identity.shape` is a statement about the bytes, so it is recomputed from them."""

    header = (root.get("mdl") or {}).get("header") or {}
    tables = header.get("tables") or {}
    body_parts = int((tables.get("bodyParts") or {}).get("count", 0))
    bones = len((root.get("mdl") or {}).get("bones") or [])
    animations = len((root.get("mdl") or {}).get("localAnimations") or [])
    flags = header.get("flags") or {}
    if not isinstance(flags, Mapping) or "value" not in flags:
        _fail("mdl.header.flags does not carry the stored value")
    if bool(flags.get("staticProp")) != bool(int(flags["value"]) & STATIC_PROP_FLAG):
        _fail("mdl.header.flags.staticProp disagrees with the stored flag word")
    if body_parts <= 0:
        expected = "bank"
    elif int(flags["value"]) & STATIC_PROP_FLAG or (bones == 1 and animations == 0):
        expected = "static"
    else:
        expected = "skeletal"
    if root["identity"]["shape"] != expected:
        _fail(
            f"identity.shape {root['identity']['shape']!r} contradicts the bytes "
            f"({expected!r})"
        )


def _check_split_rotation_bones(root: Mapping[str, Any]) -> None:
    mdl = root.get("mdl") or {}
    bones = mdl.get("bones")
    if not isinstance(bones, list):
        _fail("mdl.bones is missing")
    listed = mdl.get("splitRotationBones")
    if not isinstance(listed, list):
        _fail("mdl.splitRotationBones is missing")
    indexes = []
    for row in listed:
        if not isinstance(row, Mapping):
            _fail("a split-rotation row is not a record")
        index = row.get("bone")
        if not isinstance(index, int) or not 0 <= index < len(bones):
            _fail(f"a split-rotation row names bone {index!r}")
        if row.get("name") != bones[index].get("name"):
            _fail(
                f"split-rotation bone {index} is named {row.get('name')!r}; mdl.bones[{index}] "
                f"is {bones[index].get('name')!r}"
            )
        if row.get("rotation") != "model-space" or row.get("translation") != "parent-attached":
            _fail(f"split-rotation bone {index} does not state the split it stands for")
        indexes.append(index)
    flagged = {
        int(bone.get("index", position))
        for position, bone in enumerate(bones)
        if int(bone.get("flags", 0)) & SPLIT_ROTATION_FLAG
    }
    if len(set(indexes)) != len(indexes) or set(indexes) != flagged:
        _fail(
            f"mdl.splitRotationBones lists {sorted(indexes)}; bones flagged "
            f"0x{SPLIT_ROTATION_FLAG:x} are {sorted(flagged)}"
        )


def _reference_assets(root: Mapping[str, Any]) -> dict[str, set[str]]:
    """Every identity the unit binds, by role, so each can be held to a dependency row."""

    references: dict[str, set[str]] = {role: set() for role in DEPENDENCY_ROLES}
    for slot in (root.get("materialBindings") or {}).get("slots") or []:
        if slot.get("resolved"):
            references["material"].add(str(slot["material"]))
    for family in (root.get("materialBindings") or {}).get("skinFamilies") or []:
        for identity in family:
            if identity and not str(identity).startswith("vtmb:missing-"):
                references["material"].add(str(identity))
    for include in (root.get("mdl") or {}).get("includeModels") or []:
        # An include the install cannot reach is still declared: it keeps its real identity and a
        # `resolved: false` row, because a sentinel would claim the reference is unreachable by
        # the model kind's own rules rather than merely absent from this install.
        references["model"].add(str(include["asset"]))
    for row in (root.get("facial") or {}).get("selectedTables") or []:
        reachable = [row] if row.get("resolved") else list(row.get("fallbacks") or [])
        for entry in reachable:
            if entry.get("resolved"):
                references["expression-table"].add(str(entry["asset"]))
    # A surface name reaches the unit from four places, and the contract's rule is that every one
    # of them is declared once. The three the document states on its own are recomputed here; a
    # `$surfaceprop` a bound VMT states lives in the material unit, so it is not re-derivable
    # without the install and is not demanded.
    surfaces = {str((root.get("mdl") or {}).get("header", {}).get("surfaceProperty") or "")}
    surfaces.update(
        str(bone.get("surfaceProperty") or "") for bone in (root.get("mdl") or {}).get("bones") or []
    )
    for solid in (root.get("physics") or {}).get("solids") or []:
        surfaces.add(str((solid.get("properties") or {}).get("surfaceprop") or ""))
    for name in surfaces:
        cleaned = name.strip().lower()
        if cleaned:
            references["surface-property"].add("vtmb:surface-property:" + cleaned)
    return references


def _event_reference_candidates(root: Mapping[str, Any]) -> list[tuple[str, set[str]]]:
    """`(role, candidate identities)` for every sound or particle an animation event names.

    Which of a sound's two spellings the install holds is not knowable from the document, so the
    reference is satisfied by either; that a reference exists at all is not optional.
    """

    rows: list[tuple[str, set[str]]] = []
    seen: set[tuple[str, str]] = set()
    for sequence in (root.get("mdl") or {}).get("sequences") or []:
        for event in sequence.get("events") or []:
            options = str(event.get("options") or "").strip()
            code = event.get("event")
            if not options or not isinstance(code, int):
                continue
            stem = options.replace("\\", "/").strip().strip('"').lower().lstrip("/")
            if code in SOUND_EVENTS:
                role = "sound"
                base = stem[:-4] if stem.endswith((".wav", ".mp3")) else stem
                candidates = {f"vtmb:sound:{base}.wav", f"vtmb:sound:{base}.mp3"}
            elif code in PARTICLE_EVENTS:
                role = "particle"
                if stem.startswith("particles/"):
                    stem = stem[len("particles/"):]
                if stem.endswith(".txt"):
                    stem = stem[:-4]
                candidates = {f"vtmb:particle:{stem}"}
            else:
                continue
            key = (role, min(candidates))
            if key in seen:
                continue
            seen.add(key)
            rows.append((role, candidates))
    return rows


def _check_dependencies(root: Mapping[str, Any]) -> None:
    declared: dict[str, set[str]] = {}
    seen: set[tuple[str, str]] = set()
    for row in root["dependencies"]:
        role = str(row["role"])
        if role not in DEPENDENCY_ROLES:
            _fail(f"dependency role {role!r} is outside this seam's table")
        key = (role, str(row["asset"]))
        if key in seen:
            _fail(f"dependency {row['asset']!r} is declared twice under {role!r}")
        seen.add(key)
        if str(row["asset"]).startswith("vtmb:missing-"):
            _fail(f"a sentinel identity produces no dependency row: {row['asset']!r}")
        declared.setdefault(role, set()).add(str(row["asset"]))
    for role, assets in _reference_assets(root).items():
        missing = assets - declared.get(role, set())
        if missing:
            _fail(f"{role} reference(s) {sorted(missing)} carry no dependency row")
    for role, candidates in _event_reference_candidates(root):
        if not candidates & declared.get(role, set()):
            _fail(
                f"the animation event naming {sorted(candidates)[0]!r} carries no {role} "
                "dependency row"
            )


def _check_omitted_proven(root: Mapping[str, Any]) -> None:
    """Every `omitted-proven` range names the evidence that justified the omission.

    The unit contract puts that evidence in `omissions` or `coverage.omittedProven`; the reason is
    recomputed here from the seam's own classification table rather than trusted.
    """

    coverage = root.get("coverage") or {}
    declared: dict[tuple[str, str], str] = {}
    for row in coverage.get("omittedProven") or []:
        if isinstance(row, Mapping) and row.get("path"):
            declared[(str(row.get("sourcePath", "")), str(row["path"]))] = str(
                row.get("reason", "")
            )
    named = {
        str(row.get("path") or row.get("field") or row.get("row"))
        for row in root.get("omissions") or []
        if isinstance(row, Mapping)
    }
    for ledger in coverage.get("byteLedger") or []:
        source_path = str(ledger["sourcePath"])
        for entry in ledger["ranges"]:
            if entry["state"] != "omitted-proven":
                continue
            owner = str(entry["owner"])
            if owner in named:
                continue
            if (source_path, owner) not in declared:
                _fail(
                    f"{source_path}: the omitted-proven range at {entry['offset']} owned by "
                    f"{owner!r} carries no reason row"
                )
            try:
                expected = model_coverage.omission_reason(owner)
            except ValueError as error:
                raise ModelGlbValidationError(str(error)) from error
            if declared[(source_path, owner)] != expected:
                _fail(
                    f"{source_path}: {owner!r} is omitted for "
                    f"{declared[(source_path, owner)]!r}, not {expected!r}"
                )


def _offset_bearing_rows(root: Mapping[str, Any]) -> list[tuple[str, list[Any]]]:
    """`(path, rows)` for every table whose records were read from a file offset.

    A record that came from an offset keeps it, so the ledger range that paid for the record and
    the record itself name one place.
    """

    mdl = root.get("mdl") or {}
    facial = root.get("facial") or {}
    rows: list[tuple[str, list[Any]]] = [
        ("mdl.bones", list(mdl.get("bones") or [])),
        ("mdl.sequences", list(mdl.get("sequences") or [])),
        ("mdl.textures", list(mdl.get("textures") or [])),
        ("mdl.includeModels", list(mdl.get("includeModels") or [])),
        ("mdl.attachments", list(mdl.get("attachments") or [])),
        ("mdl.poseParameters", list(mdl.get("poseParameters") or [])),
        ("mdl.boneControllers", list(mdl.get("boneControllers") or [])),
        ("facial.controllers", list(facial.get("controllers") or [])),
        ("facial.rules", list(facial.get("rules") or [])),
        ("facial.mouths", list(facial.get("mouths") or [])),
        ("secondaryMotion", list(root.get("secondaryMotion") or [])),
        (
            "procedural.axisInterpolation",
            list((root.get("procedural") or {}).get("axisInterpolation") or []),
        ),
    ]
    for index, entry in enumerate(mdl.get("hitboxSets") or []):
        rows.append((f"mdl.hitboxSets[{index}].hitboxes", list(entry.get("hitboxes") or [])))
    for index, solid in enumerate((root.get("physics") or {}).get("solids") or []):
        rows.append((f"physics.solids[{index}]", [solid]))
        rows.append((f"physics.solids[{index}].hulls", list(solid.get("hulls") or [])))
    return rows


def _check_source_offsets(root: Mapping[str, Any]) -> None:
    ledgers = {str(row["sourcePath"]): int(row["byteLength"]) for row in
               root["coverage"]["byteLedger"]}
    model_path = str(root["identity"].get("modelPath", ""))
    physics_path = next((path for path in ledgers if path.endswith(".phy")), None)
    for path, rows in _offset_bearing_rows(root):
        limit = ledgers.get(physics_path if path.startswith("physics.") else model_path)
        for index, row in enumerate(rows):
            if not isinstance(row, Mapping):
                _fail(f"{path}[{index}] is not a record")
            offset = row.get("sourceOffset")
            if not isinstance(offset, int) or isinstance(offset, bool):
                _fail(f"{path}[{index}] does not keep the offset it was read from")
            if limit is not None and not 0 < offset < limit:
                _fail(f"{path}[{index}] was read from {offset}, outside its source member")


def _check_sentinels(root: Mapping[str, Any]) -> None:
    """Every sentinel identity is matched by an `omitted-proven` row that names it."""

    proven = {
        str(row.get("asset"))
        for row in (root.get("coverage") or {}).get("omittedProven") or []
        if isinstance(row, Mapping) and row.get("asset")
    }
    sentinels: set[str] = set()
    for slot in (root.get("materialBindings") or {}).get("slots") or []:
        if not slot.get("resolved"):
            sentinels.add(str(slot["material"]))
    for row in (root.get("facial") or {}).get("selectedTables") or []:
        if not row.get("resolved"):
            sentinels.add(str(row["asset"]))
    unproven = sorted(sentinels - proven)
    if unproven:
        _fail(f"sentinel identity {unproven} carries no omitted-proven evidence")


def _check_core(document: Mapping[str, Any], root: Mapping[str, Any]) -> None:
    """The core object graph the seam publishes: joints in bone order, then the rest."""

    nodes = list(document.get("nodes") or [])
    bones = list((root.get("mdl") or {}).get("bones") or [])
    lods = list((root.get("vtx") or {}).get("lods") or [])
    if not bones and not lods:
        # A model with no bone and no LOD holds nothing a general consumer can draw or play, so
        # the contract's scene-less rule applies and any core object here is a decode that leaked
        # out of the extension.
        validate_sceneless(document)
        return
    if not nodes:
        _fail("a model unit declares its joint and mesh nodes")
    for index, bone in enumerate(bones):
        if index >= len(nodes) or nodes[index].get("name") != bone.get("name"):
            _fail(f"node {index} is not the joint of mdl.bones[{index}]")
    skins = list(document.get("skins") or [])
    if bones:
        if len(skins) != 1:
            _fail(f"a model with {len(bones)} bones declares one skin, not {len(skins)}")
        if list(skins[0].get("joints") or []) != list(range(len(bones))):
            _fail("skins[0].joints is not the MDL bone order")
        parentless = [bone["index"] for bone in bones if int(bone.get("parent", -1)) < 0]
        if len(parentless) == 1:
            if skins[0].get("skeleton") != parentless[0]:
                _fail("skins[0].skeleton is not the one parentless bone")
        elif "skeleton" in skins[0]:
            _fail("skins[0].skeleton is declared for a model with several parentless bones")
    elif skins:
        _fail("a boneless model declares no skin")

    scene = document.get("scene")
    scenes = list(document.get("scenes") or [])
    if scene != 0 or len(scenes) != 1 or not scenes[0].get("nodes"):
        _fail("a model unit publishes one scene that instances its roots")

    accessors = list(document.get("accessors") or [])
    meshes = list(document.get("meshes") or [])
    if len(meshes) != len(lods):
        _fail(f"{len(meshes)} mesh(es) against {len(lods)} declared LOD(s)")
    for row in lods:
        mesh_index = row.get("mesh")
        if not isinstance(mesh_index, int) or not 0 <= mesh_index < len(meshes):
            _fail(f"vtx.lods[{row.get('index')}] names mesh {mesh_index!r}")
        expected_name = f"{root['identity']['asset'][len(ASSET_PREFIX):]}:lod{row['index']}"
        if meshes[mesh_index].get("name") != expected_name:
            _fail(f"mesh {mesh_index} is not named {expected_name!r}")
        if len(meshes[mesh_index].get("primitives") or []) != row.get("primitiveCount"):
            _fail(f"mesh {mesh_index} primitive count disagrees with vtx.lods")
    materials = list(document.get("materials") or [])
    for mesh_index, mesh in enumerate(meshes):
        for primitive_index, primitive in enumerate(mesh.get("primitives") or []):
            attributes = primitive.get("attributes") or {}
            required = {"POSITION", "NORMAL", "TEXCOORD_0"}
            if bones:
                required |= {"JOINTS_0", "WEIGHTS_0"}
            if not required <= set(attributes):
                _fail(
                    f"mesh {mesh_index} primitive {primitive_index} lacks {sorted(required - set(attributes))}"
                )
            counts = set()
            for name in attributes.values():
                if not isinstance(name, int) or not 0 <= name < len(accessors):
                    _fail(f"mesh {mesh_index} primitive {primitive_index} has an invalid attribute")
                counts.add(accessors[name]["count"])
            if len(counts) != 1:
                _fail(f"mesh {mesh_index} primitive {primitive_index} attribute counts differ")
            indices = primitive.get("indices")
            if not isinstance(indices, int) or not 0 <= indices < len(accessors):
                _fail(f"mesh {mesh_index} primitive {primitive_index} has invalid indices")
            if accessors[indices]["count"] % 3:
                _fail(f"mesh {mesh_index} primitive {primitive_index} is not triangles")
            material = primitive.get("material")
            if not isinstance(material, int) or not 0 <= material < len(materials):
                _fail(f"mesh {mesh_index} primitive {primitive_index} names no core material")
            bound = ((primitive.get("extensions") or {}).get(MATERIAL_REFERENCE) or {}).get("asset")
            declared = (
                (materials[material].get("extensions") or {}).get(MATERIAL_REFERENCE) or {}
            ).get("asset")
            if bound != declared:
                _fail(
                    f"mesh {mesh_index} primitive {primitive_index} binds {bound!r} through a "
                    f"core material that binds {declared!r}"
                )
    for index, animation in enumerate(document.get("animations") or []):
        if not animation.get("channels") or not animation.get("samplers"):
            _fail(f"animation {index} declares no channel")
        for channel in animation["channels"]:
            node = (channel.get("target") or {}).get("node")
            if not isinstance(node, int) or not 0 <= node < len(bones):
                _fail(f"animation {index} targets node {node!r}, which is not a joint")


def _source_vertex_positions(mdl_data: bytes, model_record: int, source_vertices: Sequence[int]):
    """Read the MDL vertex pool directly, without the decoder the writer used."""

    vertex_offset = _i32(mdl_data, model_record + 148)
    vertex_type = _i32(mdl_data, model_record + 156)
    stride = _VERTEX_STRIDES.get(vertex_type)
    if stride is None:
        _fail(f"model record {model_record} declares vertex list {vertex_type}")
    quantised_offset = struct.unpack_from("<3f", mdl_data, model_record + 160)
    quantised_scale = struct.unpack_from("<3f", mdl_data, model_record + 172)
    positions = []
    for source in source_vertices:
        vertex = model_record + vertex_offset + int(source) * stride
        if vertex_type == 0:                                    # StudioVertex, 44 bytes
            x, y, z = struct.unpack_from("<3f", mdl_data, vertex + 12)
        elif vertex_type == 1:                                  # StudioVertex2, 12 bytes
            packed = struct.unpack_from("<3H", mdl_data, vertex)
            x, y, z = (
                quantised_offset[axis] + packed[axis] * quantised_scale[axis]
                for axis in range(3)
            )
        else:                                                   # StudioVertex3, 8 bytes
            packed = mdl_data[vertex:vertex + 3]
            x, y, z = (
                quantised_offset[axis] + (packed[axis] / 255.0) * quantised_scale[axis]
                for axis in range(3)
            )
        positions.append(
            (
                x * _SOURCE_TO_GLTF_SCALE,
                z * _SOURCE_TO_GLTF_SCALE,
                -y * _SOURCE_TO_GLTF_SCALE,
            )
        )
    return positions


def _vtx_sections(vtx_data: bytes) -> dict[tuple[int, int, int, int], int]:
    """`(bodyPart, model, lod, mesh) -> triangle count`, walked straight from the VTX."""

    sections: dict[tuple[int, int, int, int], int] = {}
    bodypart_count, bodypart_base = _i32(vtx_data, 28), _i32(vtx_data, 32)
    for bodypart_index in range(bodypart_count):
        bodypart = bodypart_base + bodypart_index * 8
        model_count, model_relative = struct.unpack_from("<2i", vtx_data, bodypart)
        for model_index in range(model_count):
            model = bodypart + model_relative + model_index * 8
            lod_count, lod_relative = struct.unpack_from("<2i", vtx_data, model)
            for lod_index in range(lod_count):
                lod = model + lod_relative + lod_index * 12
                mesh_count, mesh_relative = struct.unpack_from("<2i", vtx_data, lod)
                for mesh_index in range(mesh_count):
                    mesh = lod + mesh_relative + mesh_index * 8
                    group_count = _u16(vtx_data, mesh)
                    group_base = mesh + _i32(vtx_data, mesh + 4)
                    indices = 0
                    for group_index in range(group_count):
                        group = group_base + group_index * _STRIPGROUP_STRIDE
                        strip_count = _u16(vtx_data, group + 4)
                        strip_base = group + _i32(vtx_data, group + 16)
                        for strip_index in range(strip_count):
                            indices += _u16(vtx_data, strip_base + strip_index * 16)
                    if indices:
                        sections[(bodypart_index, model_index, lod_index, mesh_index)] = (
                            indices // 3
                        )
    return sections


#: `(count field, offset field, stride, ledger owner)` for every table the v2531 header declares.
#: The seam's owner table names who pays for each, and this is where that naming is held to the
#: header's own arithmetic rather than to the walker that produced the range table.
_DECLARED_TABLES = (
    (240, 244, 160, "mdl.bones["),
    (248, 252, 24, "mdl.boneControllers"),
    (256, 260, 12, "mdl.hitboxSets"),
    (264, 268, 72, "mdl.localAnimations"),
    (272, 276, 764, "mdl.sequences["),
    (284, 288, 16, "mdl.sequenceGroups"),
    (292, 296, 20, "mdl.textures"),
    (300, 304, 4, "mdl.searchPaths"),
    (320, 324, 16, "mdl.bodyParts"),
    (328, 332, 60, "mdl.attachments"),
    (344, 348, 4, "facial.flexDescriptions"),
    (352, 356, 20, "facial.controllers"),
    (360, 364, 12, "facial.rules"),
    (368, 372, 16, "mdl.ikChains"),
    (376, 380, 20, "facial.mouths"),
    (384, 388, 20, "mdl.poseParameters"),
    (396, 400, 28, "mdl.secondaryMotion"),
    (404, 408, 116, "mdl.includeModels"),
)


def _require_owner(
    ranges: Sequence[tuple[int, int, str]], base: int, end: int, owner: str, label: str
) -> None:
    """`[base, end)` is owned, byte for byte, by ranges whose owner starts with `owner`."""

    cursor = base
    for start, length, name in ranges:
        if start + length <= cursor:
            continue
        if start > cursor:
            break
        if not name.startswith(owner):
            _fail(f"{label} at {base} is owned at {start} by {name!r}, not by {owner!r}")
        cursor = start + length
        if cursor >= end:
            break
    if cursor < end:
        _fail(f"the ledger leaves {label} at {base}+{end - base} unowned")


def _ledger_owners(row: Mapping[str, Any]) -> list[tuple[int, int, str]]:
    return sorted(
        ((int(entry["offset"]), int(entry["length"]), str(entry["owner"])) for entry in row["ranges"]),
        key=lambda entry: entry[0],
    )


def _check_declared_table_owners(row: Mapping[str, Any], mdl_data: bytes) -> None:
    """Every header-declared table is owned, byte for byte, by the owner the seam names for it."""

    ranges = _ledger_owners(row)
    for count_at, offset_at, stride, owner in _DECLARED_TABLES:
        count, base = _i32(mdl_data, count_at), _i32(mdl_data, offset_at)
        if count <= 0 or base <= 0:
            continue
        end = base + count * stride
        if end > len(mdl_data):
            _fail(f"the header declares {count} record(s) at {base} past the end of the MDL")
        _require_owner(ranges, base, end, owner, "the table the header declares")


def _check_body_part_owners(row: Mapping[str, Any], mdl_data: bytes) -> None:
    """The payloads a body-part model record declares are owned by the owners the seam names.

    The header only reaches the body-part array; the model records below it declare the vertex
    pool, the mesh table and the eyeball table that make up the bulk of a drawable model, so their
    extents are computed here from those records and held to the published range table. The
    tangent pool is deliberately not checked: a cloth model's tangents are truncated where the
    secondary-motion table starts, which is a fact about the file the record's own arithmetic
    cannot state.
    """

    ranges = _ledger_owners(row)
    count, base = _i32(mdl_data, 320), _i32(mdl_data, 324)
    if count <= 0 or base <= 0:
        return
    for bodypart_index in range(count):
        bodypart = base + bodypart_index * 16
        model_count = _i32(mdl_data, bodypart + 4)
        model_relative = _i32(mdl_data, bodypart + 12)
        if model_count <= 0 or model_relative <= 0:
            continue
        model_base = bodypart + model_relative
        tag = f"mdl.bodyParts[{bodypart_index}]"
        _require_owner(
            ranges, model_base, model_base + model_count * _MODEL_STRIDE,
            f"{tag}.models", f"{tag}.models",
        )
        for model_index in range(model_count):
            record = model_base + model_index * _MODEL_STRIDE
            model_tag = f"{tag}.models[{model_index}]"
            vertex_count = _i32(mdl_data, record + 144)
            vertex_relative = _i32(mdl_data, record + 148)
            stride = _VERTEX_STRIDES.get(_i32(mdl_data, record + 156))
            if stride is None:
                _fail(f"{model_tag} declares a vertex list this seam does not read")
            if vertex_count > 0 and vertex_relative > 0:
                pool = record + vertex_relative
                _require_owner(
                    ranges, pool, pool + vertex_count * stride,
                    f"{model_tag}.vertices", f"{model_tag}.vertices",
                )
            for offset_field, count_field, item_stride, name in (
                (140, 136, _MESH_STRIDE, "meshes"),
                (196, 192, _EYEBALL_STRIDE, "eyeballs"),
            ):
                items = _i32(mdl_data, record + count_field)
                relative = _i32(mdl_data, record + offset_field)
                if items <= 0 or relative <= 0:
                    continue
                table = record + relative
                _require_owner(
                    ranges, table, table + items * item_stride,
                    f"{model_tag}.{name}", f"{model_tag}.{name}",
                )


def _variant_lod_indices(vtx_data: bytes) -> set[int]:
    """Every LOD index one VTX variant carries, walked straight from the file."""

    indices: set[int] = set()
    bodypart_count, bodypart_base = _i32(vtx_data, 28), _i32(vtx_data, 32)
    for bodypart_index in range(bodypart_count):
        bodypart = bodypart_base + bodypart_index * 8
        model_count, model_relative = struct.unpack_from("<2i", vtx_data, bodypart)
        for model_index in range(model_count):
            model = bodypart + model_relative + model_index * 8
            indices.update(range(_i32(vtx_data, model)))
    return indices


def _published_lods(
    root: Mapping[str, Any], members: Mapping[str, Any]
) -> dict[str, frozenset[int]]:
    """Which LOD indices each VTX member's topology reached the product through.

    The primary variant publishes every LOD it carries; the legacy twin publishes only the LODs
    the primary lacks, and the rest of its topology is `omitted-proven`. Both sets are derived
    here from the two files rather than read out of `vtx.comparison`, and the published comparison
    is held to them.
    """

    primary = members.get("vtx-dx80") or members.get("vtx-dx7-2bone")
    alternate = members.get("vtx-dx7-2bone") if "vtx-dx80" in members else None
    if primary is None or alternate is None:
        return {}
    comparison = (root.get("vtx") or {}).get("comparison") or {}
    if comparison.get("primary") != primary.role or comparison.get("alternate") != alternate.role:
        _fail("vtx.comparison names variants the source closure does not hold")
    expected = _variant_lod_indices(alternate.data) - _variant_lod_indices(primary.data)
    stated = {int(value) for value in comparison.get("alternateOnlyLods") or ()}
    if stated != expected:
        _fail(
            f"vtx.comparison.alternateOnlyLods says {sorted(stated)}; the two variants say "
            f"{sorted(expected)}"
        )
    return {alternate.path: frozenset(expected)}


def _recheck_sources(
    document: Mapping[str, Any],
    root: Mapping[str, Any],
    binary: bytes,
    source_members: Sequence[Any],
) -> None:
    """Re-decode the members and hold the published unit to what they say."""

    members = {member.role: member for member in source_members}
    mdl = members.get("mdl")
    if mdl is None:
        _fail("prepublication source members omit the MDL")
    published_lods = _published_lods(root, members)
    for role, member in members.items():
        row = next(
            (entry for entry in root["coverage"]["byteLedger"] if entry["sourcePath"] == member.path),
            None,
        )
        if row is None:
            _fail(f"{member.path}: no byte ledger row for the {role} member")
        # The published range table is held to the header's own arithmetic first, because that
        # check owes nothing to the walkers; the digest comparison after it proves the table is
        # the one those walkers produce from these bytes.
        if member.role == "mdl":
            _check_declared_table_owners(row, member.data)
            _check_body_part_owners(row, member.data)
        rebuilt = model_coverage.cover_member(
            member, published_lods=published_lods.get(member.path)
        )
        if rebuilt["rangesSha256"] != row["rangesSha256"]:
            _fail(f"{member.path}: a second ledger walk disagrees with the published one")

    header = root["mdl"]["header"]
    if struct.unpack_from("<I", mdl.data, 8)[0] != header["checksum"]:
        _fail("mdl.header.checksum disagrees with the source")
    if _i32(mdl.data, 4) != header["version"] or _i32(mdl.data, 140) != header["length"]:
        _fail("mdl.header version or length disagrees with the source")
    if _i32(mdl.data, 228) != int(header["flags"]["value"]):
        _fail("mdl.header.flags disagrees with the source")
    if _i32(mdl.data, 240) != len(root["mdl"]["bones"]):
        _fail("mdl.bones does not hold every declared bone")
    disagreeing = {
        str(row.get("path"))
        for row in root.get("anomalies") or []
        if row.get("row") == "vtx-variant-disagreement"
    }
    for member in members.values():
        if not member.role.startswith("vtx-"):
            continue
        if struct.unpack_from("<I", member.data, 16)[0] == header["checksum"]:
            continue
        if member.path not in disagreeing:
            _fail(f"{member.path}: the VTX checksum disagrees with the MDL and is not an anomaly")

    comparison = root["vtx"].get("comparison") or {}
    primary = (
        members.get(str(comparison.get("primary")))
        or members.get("vtx-dx80")
        or members.get("vtx-dx7-2bone")
    )
    if primary is None:
        return
    sections = _vtx_sections(primary.data)
    # A LOD the primary variant does not carry is published from the legacy twin, so the section
    # it is held to is that twin's. Only the LODs `vtx.comparison` names are taken from it, so an
    # overlapping LOD is still checked against the variant that published it.
    alternate = members.get(str(comparison.get("alternate")))
    alternate_only = {int(index) for index in comparison.get("alternateOnlyLods") or ()}
    if alternate is not None and alternate_only:
        for section_key, triangles in _vtx_sections(alternate.data).items():
            if section_key[2] in alternate_only:
                sections[section_key] = triangles
    # Triangles the source vertex block cannot supply are dropped by the decode and named in
    # `anomalies[]`; the index accessor is held to what the VTX declares minus exactly those.
    dropped: dict[tuple[int, int, int, int], int] = {}
    for row in root.get("anomalies") or []:
        if row.get("row") != "vtx-vertex-outside-model":
            continue
        dropped[
            (int(row["bodyPart"]), int(row["model"]), int(row["lod"]), int(row["mesh"]))
        ] = int(row["droppedTriangles"])
    accessors = list(document.get("accessors") or [])
    views = list(document.get("bufferViews") or [])
    models: dict[tuple[int, int], int] = {}
    for bodypart in root["mdl"]["bodyParts"]:
        for body_model in bodypart["models"]:
            models[(bodypart["index"], body_model["index"])] = body_model["sourceOffset"]
    # An anomaly that licenses a dropped triangle is itself held to the MDL: the model it names
    # declares the vertex count it claims, and every vertex it calls stale is past that count.
    for row in root.get("anomalies") or []:
        if row.get("row") != "vtx-vertex-outside-model":
            continue
        record = models.get((int(row["bodyPart"]), int(row["model"])))
        if record is None:
            _fail(f"{row['row']} names a body-part model the extension does not declare")
        declared = _i32(mdl.data, record + 144)
        if int(row["vertexCount"]) != declared:
            _fail(f"{row['row']} says {row['vertexCount']} vertices; the MDL says {declared}")
        if any(int(vertex) < declared for vertex in row["staleVertices"]):
            _fail(f"{row['row']} calls a vertex the MDL does hold stale")
    for row in root["vtx"]["lods"]:
        mesh = document["meshes"][row["mesh"]]
        for primitive in mesh["primitives"]:
            identity = primitive["extensions"][MODEL_EXTENSION]
            key = (identity["bodyPart"], identity["model"], row["index"], identity["mesh"])
            expected = sections.get(key)
            if expected is None:
                _fail(f"the VTX declares no section for {key}")
            published = expected - dropped.get(key, 0)
            if accessors[primitive["indices"]]["count"] != published * 3:
                _fail(
                    f"section {key} carries {published} triangles the index accessor "
                    "disagrees with"
                )
            source_vertices = identity["sourceVertices"]
            position = accessors[primitive["attributes"]["POSITION"]]
            if position["count"] != len(source_vertices):
                _fail(f"section {key} maps {len(source_vertices)} source vertices to "
                      f"{position['count']} core vertices")
            record = models.get((identity["bodyPart"], identity["model"]))
            if record is None:
                _fail(f"section {key} names a body-part model the extension does not declare")
            expected_positions = _source_vertex_positions(mdl.data, record, source_vertices)
            view = views[position["bufferView"]]
            start = int(view["byteOffset"]) + int(position.get("byteOffset", 0))
            written = struct.unpack_from(
                f"<{len(source_vertices) * 3}f", binary, start
            )
            for vertex, values in enumerate(expected_positions):
                for axis in range(3):
                    if abs(written[vertex * 3 + axis] - values[axis]) > 1e-4:
                        _fail(
                            f"section {key} vertex {vertex} axis {axis} is "
                            f"{written[vertex * 3 + axis]}, the MDL says {values[axis]}"
                        )


def validate_document(
    document: dict[str, Any],
    binary: bytes,
    *,
    source_members: Sequence[Any] | None = None,
) -> dict[str, Any]:
    """Every rule a published model unit answers to, with the install present or absent."""

    try:
        root = validate_extension_root(
            document,
            MODEL_EXTENSION,
            asset_prefix=ASSET_PREFIX,
            schema_version=SCHEMA_VERSION,
        )
        _check_declarations(document, root)
        validate_container(document, binary)
        validate_accessors(document, binary)
        validate_ledgers(root, source_members)
        reject_opaque_source(document, binary, source_members)
        key = _check_identity(root)
        _check_shape(root)
        _check_split_rotation_bones(root)
        _check_dependencies(root)
        _check_sentinels(root)
        _check_omitted_proven(root)
        _check_source_offsets(root)
        _check_core(document, root)
        if source_members is not None:
            _recheck_sources(document, root, binary, source_members)
    except UnitValidationError as error:
        raise ModelGlbValidationError(str(error)) from error

    ledgers = root["coverage"]["byteLedger"]
    counts = completeness(root)
    return {
        "asset": root["identity"]["asset"],
        "key": key,
        "family": root["identity"]["family"],
        "shape": root["identity"]["shape"],
        "sources": len(root["sourceResolution"]["members"]),
        "sourceBytes": sum(int(row["byteLength"]) for row in ledgers),
        "accountedBytes": sum(int(row["accountedBytes"]) for row in ledgers),
        "byteCoveragePercent": (
            min(float(row["coveragePercent"]) for row in ledgers) if ledgers else 0.0
        ),
        "bones": len(root["mdl"]["bones"]),
        "lods": len(root["vtx"]["lods"]),
        "meshes": len(document.get("meshes") or []),
        "animations": len(document.get("animations") or []),
        "dependencies": len(root["dependencies"]),
        "unresolvedDependencies": [
            row["asset"] for row in root["dependencies"] if not row["resolved"]
        ],
        "unresolved": counts["unresolved"],
        "unsupported": counts["unsupported"],
        "typedUnidentified": counts["typedUnidentified"],
        "anomalies": [str(row.get("row", "")) for row in root.get("anomalies") or []],
        "omissions": [str(row.get("row", "")) for row in root.get("omissions") or []],
    }


def validate(path: Path) -> dict[str, Any]:
    document, binary = read_glb(path)
    return validate_document(document, binary)


def warnings_for(summary: Mapping[str, Any]) -> list[str]:
    """What a published unit could not account for, phrased for the operator."""

    warnings: list[str] = []
    for count, label in (
        (summary.get("unresolved", 0), "unresolved coverage row"),
        (summary.get("unsupported", 0), "unsupported coverage row"),
        (summary.get("typedUnidentified", 0), "typed but unidentified value"),
    ):
        if count:
            warnings.append(f"{summary['asset']}: {count} {label}(s)")
    for asset in summary.get("unresolvedDependencies") or []:
        warnings.append(f"{summary['asset']}: dependency {asset} does not resolve")
    for row in summary.get("anomalies") or []:
        warnings.append(f"{summary['asset']}: anomaly {row}")
    return warnings
