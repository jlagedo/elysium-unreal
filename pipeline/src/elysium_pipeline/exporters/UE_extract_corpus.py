"""Decode the install's shared static corpus once: textures, materials and static models.

A texture, a material and a static model belong to the install, not to a map. `models/scenery/
structural/doorknoba/doorknoba.mdl` is one doorknob however many maps hang it on a door, and
`brick/brickwall001a` decodes to the same bytes whichever BSP named it. So this exporter walks
every map the engine could load, collects the source identities they reference, and decodes each
one exactly once into `$ELYSIUM_EXPORT_ROOT/shared/`.

Two things follow that a per-map decode could not give:

- **One definition.** Every copy of a decode is frozen at the vintage of the run that wrote it,
  so per-map copies drift apart with nothing to compare them against. One decode cannot disagree
  with itself.
- **A single-unit rebake.** With one asset per source, changing a prop is one decode and one
  baked mesh, and every map that places it is already pointing at the result.

Whether a base texture keeps its alpha channel is decided here, over the whole material set at
once (`keep_alpha`), because it is a property of every material that draws the texture rather than
of whichever one resolved first.

Unreal-native by construction: meshes go through `UE_bsp_to_scene.decode_prop_models`, the same
MDL -> OBJ writer both map prop paths share (cm, Z-up, left-handed, winding reversed). This module
states no coordinate rule of its own.

Usage:
  uv run elysium export bundle corpus                    # the whole corpus
  uv run elysium export prop <models/....mdl>            # one model and what it draws
  uv run elysium export texture <materials/path>         # one texture
"""
import json
import os
import re
import struct

from elysium_pipeline import shared_corpus as SC
from elysium_pipeline.formats import install, mdl as MDL
from elysium_pipeline.formats.bsp import read_lump, read_game_lump
from elysium_pipeline.paths import export_root

OUT = os.fspath(export_root())

#: A model whose classname starts with this belongs to the character track, which owns its own
#: corpus under `npc/` and its own bake scope.
CHARACTER_CLASS = "npc_"


# ------------------------------------------------------------------------- discovery

def _texdata_materials(data):
    """Every world material a map's texdata names, as base material keys.

    The texdata table is a superset of what the faces draw -- a compiled map lists a handful of
    materials no surviving face uses -- so the corpus decodes a few textures nothing references.
    That is the safe direction: the alternative is a face walk that has to reproduce the map
    exporter's own filtering, and a corpus that silently lacks a texture is a grey surface.
    """
    from elysium_pipeline.exporters.UE_bsp_to_scene import DRAWN_TOOL_MATERIALS

    out = set()
    for raw in read_lump(data, 43).split(b"\0"):
        if not raw:
            continue
        key = SC.base_material(raw.decode("ascii", "replace"))
        # `tools/` is the compile-tool namespace: nodraw, clip, the trigger and hint volumes. The
        # map exporter drops those faces, so their textures are never drawn.
        if key.startswith("tools/") and key not in DRAWN_TOOL_MATERIALS:
            continue
        out.add(key)
    return out


def _entity_blocks(data):
    return re.finditer(r"\{[^{}]*\}", read_lump(data, 0).decode("ascii", "replace"))


def _decal_materials(data):
    """Every `infodecal` material a map places. A decal's *instance* stays with its map -- it
    carries that map's fog -- but the material it instances is an ordinary install material."""
    out = set()
    for match in _entity_blocks(data):
        block = match.group(0)
        if '"infodecal"' not in block:
            continue
        texture = re.search(r'"texture"\s+"([^"]+)"', block)
        if texture:
            out.add(SC.base_material(texture.group(1)))
    return out


def _sprite_and_rope_materials(data):
    """`(env_sprite materials, rope materials)` -- the two entity-named material sets.

    A corona's sprite and a cable's material are ordinary install materials that happen to be
    named by an entity rather than by a face, so they belong to the corpus like any other. Both
    are drawn with their alpha (a corona is a cut-out, a chain is ~47% hole), so the caller keeps
    their base textures RGBA whatever their VMT says.
    """
    sprites, ropes = set(), set()
    for match in _entity_blocks(data):
        block = match.group(0)
        classname = re.search(r'"classname"\s+"([^"]+)"', block)
        name = classname.group(1).lower() if classname else ""
        if name == "env_sprite":
            model = re.search(r'"model"\s+"([^"]+)"', block, re.I)
            if model:
                sprites.add(SC.material_key(model.group(1)))
        elif name in ("keyframe_rope", "move_rope"):
            material = re.search(r'"RopeMaterial"\s+"([^"]+)"', block, re.I)
            if material:
                ropes.add(SC.material_key(material.group(1)))
    return sprites, ropes


def _sky_name(data):
    """`worldspawn`'s `skyname`, or "" when the map states none."""
    match = re.search(r'"skyname"\s+"([^"]+)"',
                      read_lump(data, 0).decode("ascii", "replace"), re.I)
    return match.group(1).lower() if match else ""


def _static_prop_models(data):
    """The GAME_LUMP `sprp` model dictionary."""
    out = set()
    lump = read_game_lump(data)
    if "sprp" not in lump:
        return out
    _version, payload = lump["sprp"]
    count = struct.unpack_from("<i", payload, 0)[0]
    for i in range(count):
        name = payload[4 + i * 128:4 + (i + 1) * 128].split(b"\0", 1)[0]
        out.add(name.decode("ascii", "replace").replace("\\", "/").lower())
    return out


def _entity_models(data):
    """`(every .mdl an entity names, those named by a prop_physics)`, minus the character track's.

    The physics set is collected across the whole install rather than per map because the corpus
    holds one mesh per model: a model any map simulates needs VtMB's own convex hulls cooked into
    it. `bake_lib.set_phy_collision` gives such a mesh `CTF_UseSimpleAndComplex`, so the same
    asset still serves the 103 models that are a physics prop in one map and static dressing in
    another.
    """
    out, physics = set(), set()
    for match in _entity_blocks(data):
        block = match.group(0)
        classname = re.search(r'"classname"\s+"([^"]+)"', block)
        name = classname.group(1).lower() if classname else ""
        if name.startswith(CHARACTER_CLASS):
            continue
        model = re.search(r'"model"\s+"([^"]+\.mdl)"', block, re.I)
        if model:
            key = model.group(1).replace("\\", "/").lower()
            out.add(key)
            if name == "prop_physics":
                physics.add(key)
    return out, physics


class Corpus(object):
    """What the install's maps reference, as source identities.

    `world` and `decals` are material keys, `models` are `.mdl` install paths, `physics` is the
    subset any map simulates, `skies` are `skyname` values, and `unreadable` names every map whose
    content is therefore absent -- never silently.
    """

    def __init__(self):
        self.world = set()
        self.decals = set()
        self.models = set()
        self.physics = set()
        self.sprites = set()
        self.ropes = set()
        self.skies = set()
        self.unreadable = []


def discover(idx, map_names=None):
    """Every source identity the install's maps reference.

    Returns a ``Corpus``. `map_names` restricts the walk; omitted, it is every map the engine
    could load.
    """
    names = list(map_names) if map_names else install.all_map_names()
    found = Corpus()
    # An item's world model is named by `vdata/items`, not by any map: a placed `item_*` states no
    # `model` key, and a scripted grant or a drop can put any definition in any map. They are the
    # same kind of thing as a prop -- one model, one mesh -- so they join the same corpus.
    if map_names is None:
        from elysium_pipeline.exporters import UE_extract_items

        found.models |= set(UE_extract_items.ground_models(idx))
    for name in names:
        try:
            with open(install.map_path(name), "rb") as handle:
                data = handle.read()
        except OSError as error:
            found.unreadable.append((name, str(error)))
            continue
        try:
            found.world |= _texdata_materials(data)
            found.decals |= _decal_materials(data)
            found.models |= _static_prop_models(data)
            entity_models, physics = _entity_models(data)
            found.models |= entity_models
            found.physics |= physics
            sprites, ropes = _sprite_and_rope_materials(data)
            found.sprites |= sprites
            found.ropes |= ropes
            sky = _sky_name(data)
            if sky:
                found.skies.add(sky)
        except (IndexError, KeyError, ValueError, struct.error) as error:
            found.unreadable.append((name, f"lump decode: {error}"))
    return found


# -------------------------------------------------------------------------- resolution

def _read(idx):
    return lambda key: install.read(idx, key)


def resolve_world(idx, keys):
    """`{material key: channels}` for materials named by a full install path.

    A world or decal material's authored name *is* its path, so its one candidate is the
    materials root (`MDL.WORLD_SEARCH`) rather than a model header's search-path walk. A key the
    install does not carry resolves to nothing and is reported by the caller.
    """
    read_bytes = _read(idx)
    out = {}
    for key in sorted(keys):
        channels = MDL.material_channels(key, MDL.WORLD_SEARCH, read_bytes)
        if channels is not None:
            out[SC.material_key(channels["vmt"] or key)] = channels
    return out


def resolve_models(idx, model_paths):
    """`({stem: row}, {material key: channels}, missing)` for every model.

    A prop material's name is not its path: the model's own header search paths resolve it, which
    is why two models can both name `spike` and mean different files. The row records the join --
    authored slot name to resolved material key -- so the bake can bind a shared material to a
    mesh slot without resolving anything again.
    """
    read_bytes = _read(idx)
    rows, materials, missing = {}, {}, []
    for path in sorted(model_paths):
        data = install.read(idx, path)
        if not data:
            missing.append(path)
            continue
        try:
            search = MDL.search_paths(data)
            names = list(dict.fromkeys(MDL.materials(data)))
            for family in MDL.skin_families(data):
                for name in family:
                    if name not in names:
                        names.append(name)
        except (IndexError, ValueError, struct.error) as error:
            missing.append(f"{path} ({error})")
            continue
        slots = {}
        for name in names:
            channels = MDL.material_channels(name, search, read_bytes)
            if channels is None:
                continue
            key = SC.material_key(channels["vmt"])
            materials[key] = channels
            # Keyed by the mesh's material SLOT name -- `mdl.sanitize` of the authored name, which
            # is what the OBJ's `usemtl`, the `.skins` sidecar and the baked mesh's slot all carry.
            # The raw authored name would not join to any of them.
            slots[MDL.sanitize(name)] = key
        rows[SC.static_stem(path)] = {"model": path, "materials": slots}
    return rows, materials, missing


def alpha_keys(channel_sets):
    """The base textures that must keep their alpha, unioned over every material that draws them.

    One material asking is enough. Computed over the whole corpus so the answer is a property of
    the install rather than of decode order -- the thing a per-map cache could not give.
    """
    out = set()
    for channels in channel_sets:
        if channels.get("needs_alpha") and channels.get("albedo"):
            out.add(channels["albedo"])
    return out


def texture_demand(channels):
    """`{texture key: {suffix}}` -- every decoded product one material asks for."""
    out = {}

    def want(key, suffix):
        if key:
            out.setdefault(key, set()).add(suffix)

    albedo = channels.get("albedo")
    want(albedo, SC.ALBEDO)
    if channels.get("selfillum"):
        want(albedo, SC.EMISSIVE)
    if channels.get("envmap"):
        if channels.get("envmask"):
            want(channels["envmask"], SC.ENV_MASK)
        elif channels.get("envmask_from_alpha"):
            want(albedo, SC.ENV_MASK)
    if channels.get("bump"):
        want(channels["bump"], SC.NORMAL)
    elif channels.get("glass"):
        want(albedo, SC.GLASS_NORMAL)
    if channels.get("refract_map"):
        want(channels["refract_map"], SC.REFRACT_NORMAL)
    if channels.get("iris"):
        want(channels["iris"], SC.IRIS)
    if channels.get("water_normal"):
        want(channels["water_normal"], SC.NORMAL)
    return out


# ----------------------------------------------------------------------------- decode

def _decode_plain(idx, jobs, tex_out, *, label):
    """Decode textures that belong to no material's channel set.

    Two kinds need this: a sky face, which is an ordinary texture under `materials/skybox/` with
    no VMT at all, and the ``Water`` shader's normal map, which is the only thing a water material
    draws. Everything else arrives through `_resolve_material`.

    `jobs` is ``[(texture key, suffix)]``. Returns ``({key: {file: role}}, missing)``.
    """
    from elysium_pipeline.formats.tex_to_png import decode as decode_texture

    written, missing = {}, []
    for key, suffix in sorted(set(jobs)):
        name = SC.texture_file(key, suffix)
        target = os.path.join(tex_out, name)
        tth = install.read(idx, f"materials/{key}.tth")
        ttz = install.read(idx, f"materials/{key}.ttz")
        if not (tth and ttz):
            missing.append(key)
            continue
        try:
            decode_texture(tth, ttz).convert("RGB").save(target)
        except Exception as error:                           # noqa: BLE001 - decoder is broad
            print(f"  ! {label} decode failed {key}: {error}")
            missing.append(key)
            continue
        written.setdefault(key, {})[name] = SC.ROLES[suffix]
    return written, missing


def _decoded_files(tex_out):
    return {name for name in os.listdir(tex_out)} if os.path.isdir(tex_out) else set()


def _png_size(path):
    """A PNG's pixel dimensions from its IHDR alone. An `infodecal` sizes its projector from the
    texture's dimensions, so the corpus states them rather than making a reader decode the image."""
    try:
        with open(path, "rb") as handle:
            header = handle.read(24)
        if header[:8] != b"\x89PNG\r\n\x1a\n":
            return None
        return list(struct.unpack(">II", header[16:24]))
    except (OSError, struct.error):
        return None


def _reflectivity(idx, key):
    """`vtex`'s own average albedo for a texture, off the `.tth`'s embedded VTF header.

    VtMB's light cache multiplies every bounce ray by the reflectivity of the material it hit when
    it builds a model's ambient cube, so this is the input any reproduction of the bounce term
    needs. Nothing in the render path reads it -- it is carried because it is free.
    """
    from elysium_pipeline.formats.tex_to_png import reflectivity

    tth = install.read(idx, f"materials/{key}.tth")
    if not tth:
        return None
    try:
        value = reflectivity(tth)
    except Exception:                                        # noqa: BLE001 - header may be absent
        return None
    return [float(c) for c in value] if value else None


def _load_documents():
    """The corpus's two documents, or `(None, None)` when it has not been built."""
    manifest_file, materials_file = SC.manifest_path(OUT), SC.materials_path(OUT)
    if not (manifest_file.is_file() and materials_file.is_file()):
        return None, None
    with manifest_file.open(encoding="utf-8") as handle:
        manifest = SC.check_manifest(json.load(handle))
    with materials_file.open(encoding="utf-8") as handle:
        material_doc = SC.check_materials(json.load(handle))
    return manifest, material_doc


def build_units(idx, *, models=(), materials=(), verbose=True):
    """Re-decode named units and merge them into the corpus documents.

    This is the short path one asset per source buys. It resolves and decodes only what was named,
    and leaves every other row exactly as the last whole-corpus run wrote it -- so changing one
    prop costs one model's decode instead of the install's.

    The alpha decision stays corpus-wide: the manifest records, per texture, whether the whole
    material set needed its alpha, and a unit run unions its own demand onto that rather than
    deciding afresh from one material's point of view.
    """
    from elysium_pipeline.exporters.UE_bsp_to_scene import decode_prop_models

    manifest, material_doc = _load_documents()
    if manifest is None:
        raise SystemExit(
            "[corpus] no shared corpus to update; run: uv run elysium export bundle corpus")
    tex_out, prop_out = str(SC.tex_dir(OUT)), str(SC.props_dir(OUT))

    model_paths = [str(path).replace("\\", "/").lower() for path in models]
    unknown_models = [p for p in model_paths if SC.static_stem(p) not in manifest["models"]]
    unknown_materials = [k for k in materials if k not in material_doc["materials"]]
    if unknown_models or unknown_materials:
        raise SystemExit("[corpus] not in the shared corpus: "
                         + ", ".join(unknown_models + unknown_materials))

    world = resolve_world(idx, materials)
    rows, prop_materials, missing = resolve_models(idx, model_paths)
    for path in missing:
        print(f"  ! model not in the install: {path}")
    channels = dict(world)
    channels.update(prop_materials)

    keep = {key for key, record in manifest["textures"].items() if record.get("alpha")}
    keep |= alpha_keys(channels.values())

    tex_cache = {}
    read_bytes = _read(idx)
    for key in sorted(world):
        MDL._resolve_material(key, [], read_bytes, OUT, tex_cache,
                              keep_alpha=keep, tex_out=tex_out)
    resolved, ok, gone = decode_prop_models(
        idx, [row["model"] for row in rows.values()], prop_out, tex_cache, set(),
        keep_alpha=keep, tex_out=tex_out)
    if rows:
        from elysium_pipeline.formats import phy

        phy.write_physics_phys(idx, prop_out, {
            stem: row["model"] for stem, row in rows.items()
            if manifest["models"][stem].get("physics")})

    present = _decoded_files(tex_out)
    for key, unit in channels.items():
        for texture_key, suffixes in texture_demand(unit).items():
            record = manifest["textures"].setdefault(
                texture_key, {"files": {}, "alpha": texture_key in keep})
            record["alpha"] = texture_key in keep
            for suffix in suffixes:
                name = SC.texture_file(texture_key, suffix)
                if name in present:
                    record["files"][name] = SC.ROLES[suffix]
                    if suffix == SC.ALBEDO:
                        record["size"] = _png_size(os.path.join(tex_out, name))
                        record["reflectivity"] = _reflectivity(idx, texture_key)
        material_doc["materials"][key] = SC.material_record(
            unit, present,
            decal=bool(material_doc["materials"].get(key, {}).get("decal")))
    for stem, row in rows.items():
        row["physics"] = bool(manifest["models"][stem].get("physics"))
        manifest["models"][stem] = row

    if verbose:
        print(f"corpus unit: {ok} model(s) decoded ({gone} missing), "
              f"{len(world)} material(s) re-resolved -> {SC.corpus_dir(OUT)}")
    return manifest, material_doc


def build(idx, *, map_names=None, verbose=True):
    """Decode the whole corpus and return `(manifest, materials document)`.

    `map_names` restricts the census, which only a focused diagnostic wants; a unit rebake goes
    through `build_units` so it costs one decode instead of the install's.
    """
    from elysium_pipeline.exporters.UE_bsp_to_scene import decode_prop_models

    tex_out = str(SC.tex_dir(OUT))
    prop_out = str(SC.props_dir(OUT))
    os.makedirs(tex_out, exist_ok=True)
    os.makedirs(prop_out, exist_ok=True)

    found = discover(idx, map_names)
    for name, error in found.unreadable:
        print(f"  ! map unreadable, its content is absent from the corpus: {name}: {error}")

    world = resolve_world(idx, found.world | found.decals | found.sprites | found.ropes)
    rows, prop_materials, missing_models = resolve_models(idx, found.models)
    for path in missing_models:
        print(f"  ! model not in the install: {path}")
    all_channels = dict(world)
    all_channels.update(prop_materials)
    keep = alpha_keys(all_channels.values())
    # A corona and a cable draw their own cut-out, so their base textures keep alpha whatever the
    # VMT declares -- a chain flattened to RGB renders as a solid tube with a chain painted on it.
    for key in found.sprites | found.ropes:
        channels = world.get(key)
        if channels and channels.get("albedo"):
            keep.add(channels["albedo"])
    if verbose:
        print(f"corpus: {len(all_channels)} materials, {len(rows)} models, "
              f"{len(found.skies)} skies, {len(keep)} textures keeping alpha")

    # World and decal materials: decode their textures into the one corpus directory. The `.mtl`
    # these would have written is `materials.json` instead, so no per-material file is produced.
    tex_cache = {}
    read_bytes = _read(idx)
    for key, channels in sorted(world.items()):
        MDL._resolve_material(key, [], read_bytes, OUT, tex_cache,
                              keep_alpha=keep, tex_out=tex_out)

    # Models: one OBJ + MTL + skins + phys each, with every texture landing in the same corpus
    # directory the world materials used.
    wanted = [row["model"] for _stem, row in sorted(rows.items())]
    resolved, ok, missing = decode_prop_models(
        idx, wanted, prop_out, tex_cache, set(),
        keep_alpha=keep, tex_out=tex_out)
    if verbose:
        print(f"corpus models: {ok} decoded, {missing} missing -> {prop_out}")

    # VtMB's own convex collision, for every model any map simulates. `write_physics_phys` skips a
    # model with no `.phy`, which is meaningful rather than a gap: CPhysicsProp::CreateVPhysics
    # demotes such a prop to SOLID_NONE and leaves it visible but inert.
    from elysium_pipeline.formats import phy

    phy.write_physics_phys(idx, prop_out, {
        SC.static_stem(path): path for path in sorted(found.physics)})

    sky_files, missing_sky = _decode_plain(
        idx, [(SC.sky_texture_key(sky, face), SC.ALBEDO)
              for sky in found.skies for face in SC.SKY_FACES],
        tex_out, label="sky face")
    for key in missing_sky:
        print(f"  ! sky face not in the install: {key}")
    water_files, missing_water = _decode_plain(
        idx, [(channels["water_normal"], SC.NORMAL) for channels in all_channels.values()
              if channels.get("water_normal")],
        tex_out, label="water normal")
    for key in missing_water:
        print(f"  ! water normal not in the install: {key}")

    # What actually landed, joined back onto the demand, so the manifest names files rather than
    # intentions -- a decode that failed leaves its material without that channel.
    present = _decoded_files(tex_out)
    textures = {}
    for channels in all_channels.values():
        for key, suffixes in texture_demand(channels).items():
            record = textures.setdefault(key, {"files": {}, "alpha": key in keep})
            for suffix in suffixes:
                name = SC.texture_file(key, suffix)
                if name in present:
                    record["files"][name] = SC.ROLES[suffix]
                    if suffix == SC.ALBEDO:
                        record["size"] = _png_size(os.path.join(tex_out, name))
                        record["reflectivity"] = _reflectivity(idx, key)
    for extra in (sky_files, water_files):
        for key, files in extra.items():
            textures.setdefault(key, {"files": {}, "alpha": False})["files"].update(files)
    textures = {key: record for key, record in textures.items() if record["files"]}

    decal_keys = {SC.material_key(key) for key in found.decals}
    material_rows = {}
    material_records = {}
    for key, channels in all_channels.items():
        decal = key in decal_keys
        material_rows[key] = {
            "map_scoped": SC.is_map_scoped_material(key, decal=decal,
                                                    wetness_driven=channels.get(
                                                        "globalwetness") is not None)}
        material_records[key] = SC.material_record(channels, present, decal=decal)

    # Only claim a model the corpus actually holds. A selective run decodes one unit, so a row for
    # a model with no OBJ on disk would read as coverage the corpus does not have.
    physics_stems = {SC.static_stem(path) for path in found.physics}
    rows = {stem: dict(row, physics=stem in physics_stems) for stem, row in rows.items()
            if os.path.isfile(os.path.join(prop_out, stem + ".obj"))}

    fingerprint = SC.corpus_fingerprint(textures, all_channels, found.models)
    manifest = SC.build_manifest(
        textures=textures,
        materials=material_rows,
        models=rows,
        missing={
            "models": sorted(missing_models),
            "sky": sorted(missing_sky),
            "maps": [name for name, _ in found.unreadable],
        },
        fingerprint=fingerprint,
    )
    return manifest, SC.build_materials(material_records, fingerprint=fingerprint)


def _write(path, document):
    text = json.dumps(document, separators=(",", ":"), sort_keys=True)
    # Skip a byte-identical rewrite: an unchanged document keeps its mtime, so the downstream
    # stat-keyed digest cache and fingerprint stay warm instead of forcing a gigabyte-scale rehash.
    try:
        with open(path, "r", encoding="utf-8") as handle:
            if handle.read() == text:
                return
    except OSError:
        pass
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)


def main(index=None, *, map_names=None, models=None, materials=None, force=False):
    """Decode the corpus and write its two documents. Returns the manifest."""
    idx = index if index is not None else install.build_index()
    if force:
        for directory in (SC.tex_dir(OUT), SC.props_dir(OUT)):
            if directory.is_dir():
                for entry in directory.iterdir():
                    if entry.is_file():
                        entry.unlink()
    if models or materials:
        manifest, material_doc = build_units(
            idx, models=models or (), materials=materials or ())
    else:
        manifest, material_doc = build(idx, map_names=map_names)
    _write(str(SC.manifest_path(OUT)), manifest)
    _write(str(SC.materials_path(OUT)), material_doc)

    # DXT siblings for every albedo the runtime prefers over its PNG. One corpus directory now,
    # rather than a world `tex/` and a prop `props/tex/` per map.
    from elysium_pipeline.enhancement import retex_dds

    flat, dropped = retex_dds.flat_index(idx)
    retex_dds.emit_refs(
        idx, flat, dropped,
        sorted(name for name in SC.texture_files(manifest) if name.endswith(".png")),
        str(SC.tex_dir(OUT)))

    print(f"corpus: {len(manifest['textures'])} textures / "
          f"{len(manifest['materials'])} materials / {len(manifest['models'])} models "
          f"-> {SC.corpus_dir(OUT)}")
    return manifest


if __name__ == "__main__":
    main()
