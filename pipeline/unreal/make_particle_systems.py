"""Generate one Niagara system per VtMB emitter closure, onto a map's baked mount.

Reads the offline-compiled ``<map>.particles.json`` and authors
``/ElysiumBaked/<map>/Particles/NS_<root>`` for every emitter definition the map places.

VtMB's definitions are a spawn *graph*: a root names child particles by rate or burst, and those
children can name more.  Niagara has no equivalent of a particle spawning a particle outside event
handlers, so the graph is flattened here into one Niagara emitter ("layer") per drawing leaf, and
``UElysiumParticleAssetBuilder`` receives it already resolved.

Sprites instance ``M_Additive``, whose graph is already exactly a particle sprite: an ``Albedo``
texture driving emissive, with the texture's own alpha as opacity.
"""

from __future__ import annotations

import json
from pathlib import Path

import unreal

from pipeline.unreal import _bootstrap  # noqa: F401
from pipeline.unreal import bake_lib as bl


ADDITIVE_MASTER = "/Game/VtMB/Materials/M_Additive"

FOUNTAIN_TEMPLATE = "/Niagara/DefaultAssets/Templates/Emitters/Fountain.Fountain"

def _drain_compilation():
    """Finish every queued asset compilation before a package is force-deleted.

    Authoring or loading a Niagara system enqueues its scripts with the compiler, and
    force-deleting a package the compiler still owns crashes in CoreUObject. Every delete
    below is preceded by this drain, including the prunes, because the system built by the
    previous iteration -- or by the previous map in the same process -- is still in flight.
    """
    unreal.ElysiumParticleAssetBuilder.finish_asset_compilation()


def _fountain_template():
    """The stock Fountain emitter template, loaded fresh for each system authored: a wrapper
    held across the per-map garbage collection and the force-delete of a system being
    rebuilt is not a reference the engine honours."""
    template = unreal.load_asset(FOUNTAIN_TEMPLATE)
    if not template:
        raise SystemExit("[particles] missing the stock Fountain emitter template")
    return template

# VtMB paces `frames` at its own tick when a definition gives no explicit `fps`. The exported
# animation banks resolve at 30, so the same rate is assumed here; a definition carrying `fps`
# overrides it.
DEFAULT_FPS = 30.0


def _first(curve, default=0.0):
    """The first number of a compiled curve/range/scalar, or a default when absent."""
    if not curve:
        return default
    values = curve.get("values") or []
    return float(values[0]) if values else default


def _vec(entry, default=(0.0, 0.0, 0.0)):
    if not entry:
        return default
    return (
        _first(entry.get("x")), _first(entry.get("y")), _first(entry.get("z")),
    )


def _layer_from(name, definition, offset, spawn):
    """One Niagara emitter's parameters, in Unreal-native units."""

    frames = definition.get("frames") or definition.get("max_frames") or int(DEFAULT_FPS)
    fps = float(definition.get("fps") or DEFAULT_FPS)

    size = _first(definition.get("size_cm"), 1.0)
    height = _first(definition.get("height_cm"), size)

    # `mask` reads as the alpha ramp (255 -> 0 is the common fade); `red`/`green`/`blue` are the
    # 0-255 tint. `color` is a separate brightness channel and is deliberately not folded in here.
    alpha = _first(definition.get("mask"), 255.0) / 255.0

    velocity = list(_vec(definition.get("velocity_cm_per_second")))
    # The spherical frame's axial member maps onto +Z. `radius`, which expands each particle along
    # its own spawn direction, has no flat equivalent and is not applied — see the module note.
    radial = definition.get("radial_velocity_cm_per_second") or {}
    velocity[2] += _first(radial.get("elevation"))

    layer = unreal.ElysiumParticleLayer()
    layer.set_editor_property("name", name)
    layer.set_editor_property("sprite_texture", definition.get("sprite", ""))
    layer.set_editor_property("spawn_rate", _first((spawn or {}).get("rate")))
    layer.set_editor_property("burst", int(_first((spawn or {}).get("burst"))))
    layer.set_editor_property("lifetime_seconds", max(frames / fps, 0.01))
    layer.set_editor_property("sprite_size", unreal.Vector2D(size, height))
    layer.set_editor_property("color", unreal.LinearColor(
        _first(definition.get("red"), 255.0) / 255.0,
        _first(definition.get("green"), 255.0) / 255.0,
        _first(definition.get("blue"), 255.0) / 255.0,
        max(0.0, min(alpha, 1.0)),
    ))
    layer.set_editor_property("velocity", unreal.Vector(*velocity))
    layer.set_editor_property("offset", unreal.Vector(*offset))
    return layer


def flatten(definitions, root):
    """Depth-first walk of a root's spawn graph into one layer per drawing leaf."""

    layers = []
    seen_sprites = []

    def walk(name, offset, spawn, chain):
        if name in chain:          # the closure is cycle-safe, but a diamond can still revisit
            return
        definition = definitions.get(name)
        if definition is None:
            return
        if definition.get("sprite"):
            layers.append(_layer_from(name, definition, offset, spawn))
            seen_sprites.append(definition["sprite"])
        for child in definition.get("spawns", []):
            child_offset = tuple(
                a + b for a, b in zip(offset, _vec(child.get("offset_cm")))
            )
            walk(child["particle"], child_offset, child, chain | {name})

    root_definition = definitions.get(root)
    if root_definition is None:
        return [], []
    if root_definition.get("sprite"):
        layers.append(_layer_from(root, root_definition, (0.0, 0.0, 0.0), None))
        seen_sprites.append(root_definition["sprite"])
    for child in root_definition.get("spawns", []):
        walk(child["particle"], _vec(child.get("offset_cm")), child, {root})
    return layers, seen_sprites


def _import_sprites(sprites, package, source_root, tracker):
    """Import each closure sprite's normalized PNG and instance M_Additive over it."""

    materials = {}
    master = unreal.load_asset(ADDITIVE_MASTER)
    if not master:
        raise SystemExit("[particles] missing %s; run the policy content step first" % ADDITIVE_MASTER)
    wanted_textures = set()
    wanted_materials = set()
    md5_missing = 0
    md5_mismatch = 0
    for relative in sorted(set(sprites)):
        stem = Path(relative).stem
        source = source_root / "particles" / (stem + ".png")
        if not source.is_file():
            raise SystemExit("[particles] no normalized sprite for %s" % stem)
        texture_asset = "%s/T_%s" % (package, stem)
        wanted_textures.add("T_" + stem)
        md5 = bl.file_md5(str(source))
        exists = unreal.EditorAssetLibrary.does_asset_exist(texture_asset)
        if exists:
            unreal_md5 = bl.texture_source_md5(texture_asset)
            if not unreal_md5:
                md5_missing += 1
            elif unreal_md5 != md5:
                md5_mismatch += 1
        texture_dirty = tracker.register(
            "particles", texture_asset,
            {
                "source": source.name,
                "sha256": tracker.file_sha256(source),
                "role": "particle-sprite",
                "settings": "srgb-no-mips-v1",
            },
            expected_class="Texture2D")
        if texture_dirty:
            task = unreal.AssetImportTask()
            task.set_editor_property("filename", str(source))
            task.set_editor_property("destination_path", package)
            task.set_editor_property("destination_name", "T_" + stem)
            task.set_editor_property("automated", True)
            task.set_editor_property("replace_existing", True)
            task.set_editor_property("save", False)
            unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        texture = unreal.load_asset(texture_asset)
        if not texture:
            raise SystemExit("[particles] sprite import failed: %s" % texture_asset)
        if texture_dirty:
            texture.set_editor_property("srgb", True)
            texture.set_editor_property(
                "mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
            if not unreal.EditorAssetLibrary.save_asset(texture_asset, only_if_is_dirty=True):
                raise SystemExit("[particles] could not save %s" % texture_asset)
            tracker.built("particles")

        material_path = "%s/MI_P_%s" % (package, stem)
        wanted_materials.add("MI_P_" + stem)
        if tracker.register(
                "particles", material_path,
                {"master": ADDITIVE_MASTER, "texture": texture_asset},
                expected_class="MaterialInstanceConstant"):
            material = bl.make_material_instance("MI_P_" + stem, package, master)
            unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(
                material, "Albedo", texture)
            if not unreal.EditorAssetLibrary.save_asset(material_path, only_if_is_dirty=True):
                raise SystemExit("[particles] could not save %s" % material_path)
            tracker.built("particles")
        else:
            material = unreal.load_asset(material_path)
            if not material:
                raise SystemExit("[particles] cached material missing: %s" % material_path)
        materials[relative] = material
    if md5_missing or md5_mismatch:
        print("[particles] Unreal SourceFile MD5 diagnostic: %d missing / %d mismatch" % (
            md5_missing, md5_mismatch))
    _drain_compilation()
    tracker.pruned("particles", bl.prune_package_prefix(package, "T_", wanted_textures))
    tracker.pruned("particles", bl.prune_package_prefix(package, "MI_P_", wanted_materials))
    return materials


def _root_recipe(definitions, root, material_paths):
    pending = [root]
    seen = set()
    closure = {}
    while pending:
        name = pending.pop()
        if name in seen:
            continue
        seen.add(name)
        definition = definitions.get(name)
        if definition is None:
            continue
        closure[name] = definition
        pending.extend(
            child.get("particle") for child in definition.get("spawns", [])
            if child.get("particle")
        )
    return {
        "root": root,
        "definitions": closure,
        "materials": sorted(material_paths),
        "template": "/Niagara/DefaultAssets/Templates/Emitters/Fountain",
        "default_fps": DEFAULT_FPS,
    }


def build(map_name: str, export_root: Path, package_root: str, tracker=None) -> int:
    """Author every system for one map. Returns the number of systems written."""

    document_path = Path(export_root) / map_name / ("%s.particles.json" % map_name)
    package = "%s/Particles" % package_root
    if not document_path.is_file():
        if tracker:
            _drain_compilation()
            for prefix in ("T_", "MI_P_", "NS_"):
                tracker.pruned(
                    "particles", bl.prune_package_prefix(package, prefix, set()))
        return 0
    document = json.loads(document_path.read_text(encoding="utf-8"))
    closure = document.get("particles", {})
    definitions = closure.get("definitions", {})
    roots = closure.get("roots", [])
    if not roots:
        if tracker:
            _drain_compilation()
            for prefix in ("T_", "MI_P_", "NS_"):
                tracker.pruned(
                    "particles", bl.prune_package_prefix(package, prefix, set()))
        return 0

    materials = _import_sprites(
        closure.get("sprites", []), package, Path(export_root), tracker)

    # Loading an existing Niagara system can enqueue compilation. Force-deleting that package
    # while the compiler still owns its scripts crashes in CoreUObject; a full bake used to hide
    # the race behind minutes of mesh work, while an isolated particle stage reached it instantly.
    # Preload the complete replacement set here, so the deletes below run against objects that
    # are already resident rather than loading each one an instant before destroying it.
    existing = []
    work = []
    wanted_systems = set()
    for root in roots:
        layers, sprites = flatten(definitions, root)
        if not layers:
            unreal.log_warning("[particles] %s flattened to no drawing layer; skipped" % root)
            continue
        asset_name = "NS_" + root
        asset = "%s/%s" % (package, asset_name)
        wanted_systems.add(asset_name)
        material_paths = [
            materials[sprite].get_path_name().split(".", 1)[0]
            for sprite in sprites if sprite in materials
        ]
        dirty = tracker.register(
            "particles", asset,
            _root_recipe(definitions, root, material_paths),
            expected_class="NiagaraSystem")
        work.append((root, layers, sprites, asset_name, asset, dirty))
        if dirty and unreal.EditorAssetLibrary.does_asset_exist(asset):
            loaded = unreal.load_asset(asset)
            if loaded:
                existing.append(loaded)

    written = 0
    for root, layers, sprites, asset_name, asset, dirty in work:
        if not dirty:
            continue
        if unreal.EditorAssetLibrary.does_asset_exist(asset):
            _drain_compilation()
            bl.delete_owned_asset(asset)
        # Loaded after the delete: that delete collects garbage, and the template must be a
        # live object when the builder reads it.
        system = unreal.ElysiumParticleAssetBuilder.build_particle_system(
            asset_name, package, _fountain_template(), layers)
        if not system:
            raise SystemExit("[particles] could not author %s" % asset_name)
        # By index: a definition a spawn graph reaches twice yields two layers with one name, and
        # the engine uniquifies the second emitter's. Binding by the name we asked for wrote the
        # first emitter twice and left the second on the Fountain template's own material.
        for index, sprite in enumerate(sprites):
            material = materials.get(sprite)
            if material and not unreal.ElysiumParticleAssetBuilder.bind_layer_material(
                system, index, material
            ):
                unreal.log_warning("[particles] %s: could not bind %s" % (asset_name, sprite))
        problems = unreal.ElysiumParticleAssetBuilder.validate_particle_system(system, len(layers))
        if problems:
            raise SystemExit("[particles] %s failed validation: %s" % (asset_name, problems))
        if not unreal.EditorAssetLibrary.save_asset(asset, only_if_is_dirty=True):
            raise SystemExit("[particles] could not save %s" % asset)
        tracker.built("particles")
        written += 1

    _drain_compilation()
    tracker.pruned(
        "particles", bl.prune_package_prefix(package, "NS_", wanted_systems))

    unreal.log("[particles] %s: %d system(s) built, %d sprite material(s); %s"
               % (map_name, written, len(materials), tracker.summary("particles")))
    return written
