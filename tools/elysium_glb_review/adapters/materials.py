"""Material reconstruction, faithful and labelled.

A character unit carries no textures. Its core materials are neutral placeholders whose
only real content is a `vtmb:material:` identity, and the material unit that identity
names holds a VMT transcription plus a deliberately thin PBR approximation. Rebuilding
appearance therefore means reading the extension, not the core material.

VtMB's shaders are fixed-function DirectX 8 programs with no metallic-roughness
parameterisation, so most of what a VMT says has no Principled equivalent at all. This
module wires only what maps one-to-one and puts everything else in a labelled frame
beside the shader, unconnected. A reviewer looking at a surface has to be able to tell
what came from the source and what Blender invented, and a plausible-looking
reconstruction that quietly drops `$envmap` is worse than an honest gap.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path

import bpy

from ..core import glb, ids, seams
from . import hooks, images

#: Custom properties left on a rebuilt material for the inspector.
SHADER_PROPERTY = "elysium_shader"
APPROXIMATION_PROPERTY = "elysium_approximations"
SENTINEL_PROPERTY = "elysium_sentinel"

#: Parameters this module reproduces. Everything else is reported, not wired.
BASE_COLOR_PARAMETER = "$basetexture"
NORMAL_PARAMETERS = ("$bumpmap", "$normalmap")

#: Colour of the frame holding what could not be reproduced.
APPROXIMATION_FRAME_COLOR = (0.35, 0.18, 0.18)

#: What the engine draws for a material that resolves to no VMT.
ERROR_CHECKER_COLORS = ((0.85, 0.0, 0.85, 1.0), (0.0, 0.0, 0.0, 1.0))


@dataclass
class Summary:
    """What one rebuild pass did."""

    rebuilt: int = 0
    approximated: int = 0
    sentinels: int = 0
    unresolved: int = 0
    missing: list = field(default_factory=list)


def _clear(material: bpy.types.Material) -> bpy.types.ShaderNodeTree:
    """Empty a material's node tree.

    `bpy.data.materials.new` already provides one; `use_nodes` is a deprecated no-op in
    5.2 and is removed in 6.0, so it is never touched here.
    """
    tree = material.node_tree
    tree.nodes.clear()
    return tree


def _principled(tree: bpy.types.ShaderNodeTree) -> bpy.types.Node:
    output = tree.nodes.new("ShaderNodeOutputMaterial")
    output.location = (320, 0)
    shader = tree.nodes.new("ShaderNodeBsdfPrincipled")
    shader.location = (0, 0)
    # VtMB has no metallic-roughness model; a flat dielectric is the neutral reading.
    shader.inputs["Metallic"].default_value = 0.0
    shader.inputs["Roughness"].default_value = 1.0
    tree.links.new(shader.outputs["BSDF"], output.inputs["Surface"])
    return shader


def _apply_core_material(material: bpy.types.Material, core: dict, shader: bpy.types.Node) -> None:
    """Apply the parts of the core glTF material that do carry meaning."""
    pbr = core.get("pbrMetallicRoughness") or {}
    factor = pbr.get("baseColorFactor")
    if factor:
        shader.inputs["Base Color"].default_value = tuple(factor)
        shader.inputs["Alpha"].default_value = factor[3]

    material.use_backface_culling = not core.get("doubleSided", True)
    if core.get("alphaMode") == "BLEND":
        material.surface_render_method = "BLENDED"


def _mask_alpha(tree, shader, texture_node, cutoff: float) -> None:
    """Turn a texture's alpha into a hard cutout at the authored threshold."""
    compare = tree.nodes.new("ShaderNodeMath")
    compare.operation = "GREATER_THAN"
    compare.location = (-220, -260)
    compare.inputs[1].default_value = cutoff
    tree.links.new(texture_node.outputs["Alpha"], compare.inputs[0])
    tree.links.new(compare.outputs["Value"], shader.inputs["Alpha"])


def _error_checker(material: bpy.types.Material) -> None:
    """Draw what the engine draws for a material that names no VMT."""
    tree = _clear(material)
    shader = _principled(tree)
    checker = tree.nodes.new("ShaderNodeTexChecker")
    checker.location = (-320, 0)
    checker.inputs["Scale"].default_value = 12.0
    checker.inputs["Color1"].default_value = ERROR_CHECKER_COLORS[0]
    checker.inputs["Color2"].default_value = ERROR_CHECKER_COLORS[1]
    tree.links.new(checker.outputs["Color"], shader.inputs["Base Color"])
    material[SENTINEL_PROPERTY] = True


def _frame_unreproduced(
    tree: bpy.types.ShaderNodeTree,
    payload: dict,
    wired: set[str],
    cache: images.TextureCache,
) -> list[str]:
    """Park everything with no Principled equivalent in a labelled frame.

    The nodes are real and carry the source values, so the data is inspectable in the
    node editor, but nothing is connected: the viewport must not imply that VtMB's
    envmap, detail or refraction passes are being reproduced.
    """
    unreproduced: list[str] = []
    bindings = [
        binding
        for binding in (payload.get("textureBindings") or [])
        if binding.get("parameter") not in wired
    ]
    scalars = [
        parameter
        for parameter in (payload.get("parameters") or [])
        if parameter.get("key") not in wired
        and not any(
            binding.get("parameter") == parameter.get("key")
            for binding in (payload.get("textureBindings") or [])
        )
    ]
    if not bindings and not scalars:
        return unreproduced

    frame = tree.nodes.new("NodeFrame")
    frame.label = "VtMB %s: not reproduced" % payload.get("shader", "?")
    frame.use_custom_color = True
    frame.color = APPROXIMATION_FRAME_COLOR

    y = 400
    for binding in bindings:
        parameter = binding.get("parameter", "?")
        unreproduced.append(parameter)
        node = tree.nodes.new("ShaderNodeTexImage")
        node.parent = frame
        node.location = (-900, y)
        node.label = parameter
        node.mute = True
        y -= 300
        identity = binding.get("asset")
        if identity:
            decoded = cache.get(identity, non_color=seams.is_non_color(parameter))
            if decoded is not None and decoded.image is not None:
                node.image = decoded.image

    for parameter in scalars:
        key = parameter.get("key", "?")
        unreproduced.append(key)
        node = tree.nodes.new("ShaderNodeValue")
        node.parent = frame
        node.location = (-900, y)
        node.label = "%s = %s" % (key, parameter.get("value"))
        node.mute = True
        y -= 120

    return unreproduced


def rebuild(material: bpy.types.Material, root: Path, cache: images.TextureCache) -> str:
    """Rebuild one material from the unit its identity names.

    Returns a short status: `rebuilt`, `sentinel`, `missing` or `skipped`.
    """
    identity = material.get(hooks.MATERIAL_REFERENCE_PROPERTY)
    if not identity:
        return "skipped"

    asset = ids.parse(identity)
    if asset is not None and asset.is_sentinel:
        # By design this names no VMT, so there is nothing to resolve and nothing wrong.
        _error_checker(material)
        return "sentinel"

    path = ids.resolve(identity, root)
    if path is None or not path.is_file():
        return "missing"

    document = glb.read_json(path)
    payload = seams.root_extension(document, seams.MATERIAL_EXTENSION)
    if payload is None:
        return "missing"

    core = (document.get("materials") or [{}])[0]
    tree = _clear(material)
    shader = _principled(tree)
    _apply_core_material(material, core, shader)

    wired: set[str] = set()
    bindings = {
        binding.get("parameter"): binding
        for binding in (payload.get("textureBindings") or [])
    }

    base = bindings.get(BASE_COLOR_PARAMETER)
    if base and base.get("asset"):
        decoded = cache.get(base["asset"], non_color=False)
        if decoded is not None and decoded.image is not None:
            node = tree.nodes.new("ShaderNodeTexImage")
            node.location = (-560, 120)
            node.label = BASE_COLOR_PARAMETER
            node.image = decoded.image
            tree.links.new(node.outputs["Color"], shader.inputs["Base Color"])
            mode = core.get("alphaMode")
            if mode == "BLEND":
                tree.links.new(node.outputs["Alpha"], shader.inputs["Alpha"])
            elif mode == "MASK":
                _mask_alpha(tree, shader, node, float(core.get("alphaCutoff", 0.5)))
            wired.add(BASE_COLOR_PARAMETER)

    for parameter in NORMAL_PARAMETERS:
        binding = bindings.get(parameter)
        if not binding or not binding.get("asset"):
            continue
        decoded = cache.get(binding["asset"], non_color=True)
        if decoded is None or decoded.image is None:
            continue
        node = tree.nodes.new("ShaderNodeTexImage")
        node.location = (-560, -220)
        node.label = parameter
        node.image = decoded.image
        normal = tree.nodes.new("ShaderNodeNormalMap")
        normal.location = (-260, -220)
        tree.links.new(node.outputs["Color"], normal.inputs["Color"])
        tree.links.new(normal.outputs["Normal"], shader.inputs["Normal"])
        wired.add(parameter)
        break

    unreproduced = _frame_unreproduced(tree, payload, wired, cache)

    material[SHADER_PROPERTY] = payload.get("shader", "")
    material[APPROXIMATION_PROPERTY] = json.dumps(sorted(set(unreproduced)))
    hooks.stash(material, "elysium_vtmb_material", payload)
    return "rebuilt"


def rebuild_all(materials, root: Path, cache: images.TextureCache | None = None) -> Summary:
    """Rebuild every material that names a corpus identity."""
    cache = cache or images.TextureCache(root)
    summary = Summary()
    for material in materials:
        status = rebuild(material, root, cache)
        if status == "rebuilt":
            summary.rebuilt += 1
            if json.loads(material.get(APPROXIMATION_PROPERTY, "[]")):
                summary.approximated += 1
        elif status == "sentinel":
            summary.sentinels += 1
        elif status == "missing":
            summary.unresolved += 1
            summary.missing.append(material.get(hooks.MATERIAL_REFERENCE_PROPERTY, material.name))
    return summary
