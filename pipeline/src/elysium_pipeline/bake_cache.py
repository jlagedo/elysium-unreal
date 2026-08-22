"""Content-addressed planning for the staged Unreal map bake."""

from __future__ import annotations

import ast
import hashlib
import json
from pathlib import Path
import tempfile
from typing import Iterable, Sequence
import uuid

from elysium_pipeline import shared_corpus
from elysium_pipeline.tasking import (
    ContentDigestCache,
    Manifest,
    Task,
    TaskResult,
    fingerprint_content,
)


#: A map's stages. Prop meshes and every surface texture and material belong to the shared
#: corpus scope, whose own stages are `shared_corpus.STAGES`.
STAGES = ("textures", "materials", "world", "sky", "particles", "level")
CACHE_REVISION = "elysium-bake-stage-v2"
DIGEST_CACHE_FILE = ".elysium-content-digests.json"
ASSET_RECEIPT_DIR = ".elysium-bake-assets"
ASSET_RUN_DIR = ".elysium-bake-runs"
ASSET_RECEIPT_SCHEMA = "elysium.bake-asset-receipts"
ASSET_RUN_SCHEMA = "elysium.bake-asset-run"
ASSET_SCHEMA_VERSION = 1

_BASE_METHODS = ("__init__", "load_masters", "load_sources", "flush")
_STAGE_METHODS = {
    "textures": ("stage_textures", "resolve_textures"),
    "materials": ("stage_materials", "resolve_textures", "resolve_materials"),
    "world": ("stage_world", "resolve_materials"),
    "sky": ("stage_sky", "resolve_materials"),
    "particles": (),
    "level": ("stage_level", "resolve_materials"),
}

#: `bake_corpus` drives `CorpusBake`, which overrides part of `Bake`, so a corpus stage's closure
#: is rooted across both classes. `checkpoint` joins the base set because every corpus stage ends
#: in one.
_CORPUS_CLASSES = ("Bake", "CorpusBake")
_CORPUS_BASE_METHODS = (
    "__init__", "load_masters", "load_sources", "flush", "checkpoint",
)
_CORPUS_STAGE_METHODS = {
    "textures": ("stage_textures", "resolve_textures", "_import_texture_jobs"),
    "materials": ("stage_materials", "resolve_textures", "resolve_materials", "_material_sets"),
    # A prop mesh's recipe names the material instances its slots bind, so what resolves those
    # instances decides the mesh as much as the geometry does.
    "props": ("stage_props", "_load_prop_sources", "_emit", "resolve_materials"),
}


def _hash_parts(parts: Iterable[str]) -> str:
    digest = hashlib.sha256()
    for part in parts:
        digest.update(part.encode("utf-8", errors="surrogateescape"))
        digest.update(b"\0")
    return digest.hexdigest()


def _atomic_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(payload, indent=2, sort_keys=True) + "\n"
    with tempfile.NamedTemporaryFile(
        "w", encoding="utf-8", newline="\n", delete=False, dir=path.parent
    ) as handle:
        handle.write(text)
        temporary = Path(handle.name)
    temporary.replace(path)


#: The character bake writes its run plan through the same atomic writer.
atomic_json = _atomic_json


def _canonical(value):
    """Return a JSON-safe, deterministic representation of an asset recipe."""

    if isinstance(value, Path):
        return value.as_posix()
    if isinstance(value, dict):
        return {str(key): _canonical(item) for key, item in sorted(value.items(), key=lambda p: str(p[0]))}
    if isinstance(value, (list, tuple)):
        return [_canonical(item) for item in value]
    if isinstance(value, set):
        return sorted((_canonical(item) for item in value), key=lambda item: repr(item))
    if isinstance(value, float):
        # JSON's shortest round-trip representation is deterministic for a fixed Python runtime.
        # Normalise signed zero because it has no authored meaning in any bake recipe.
        return 0.0 if value == 0.0 else value
    if value is None or isinstance(value, (str, int, bool)):
        return value
    raise TypeError(f"unsupported asset recipe value: {type(value).__name__}")


def asset_recipe_fingerprint(
    stage: str,
    object_path: str,
    policy_fingerprint: str,
    recipe,
) -> str:
    """Hash the semantic recipe for one generated Unreal package."""

    payload = {
        "stage": stage,
        "object_path": object_path,
        "policy": policy_fingerprint,
        "recipe": _canonical(recipe),
    }
    encoded = json.dumps(
        payload, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8", errors="surrogateescape")
    return hashlib.sha256(encoded).hexdigest()


def level_sidecar_recipe(map_root: Path, map_name: str) -> dict:
    """Parse only the values that affect authored level actors.

    Formatting, comments, and ignored Source fields do not dirty the level package.  Ordered
    rows stay ordered because decal sort order and light source indices are authored output.
    """

    def lines(suffix: str):
        path = map_root / f"{map_name}.{suffix}"
        if not path.is_file():
            return []
        return [line.split() for line in path.read_text(
            encoding="utf-8", errors="replace"
        ).splitlines()]

    def number(value: str):
        try:
            return float(value)
        except ValueError:
            return value

    props = []
    for tokens in lines("props"):
        if len(tokens) < 9:
            continue
        props.append({
            "stem": tokens[0],
            "position": [float(value) for value in tokens[1:4]],
            "rotation": [float(value) for value in tokens[4:8]],
            "solid": int(tokens[8]),
            "skin": int(tokens[9]) if len(tokens) >= 10 else 0,
            "sky": int(tokens[10]) if len(tokens) >= 11 else 0,
        })

    decals = []
    for tokens in lines("decals"):
        if len(tokens) == 15:
            decals.append({
                "material": tokens[0],
                "values": [float(value) for value in tokens[1:]],
            })

    lights = []
    for index, tokens in enumerate(lines("lights")):
        if len(tokens) < 15:
            continue
        kind = int(tokens[0])
        rgb = [float(value) for value in tokens[7:10]]
        if max(rgb) <= 0.0 or kind not in (0, 1, 2, 3, 5):
            continue
        row = {"index": index, "kind": kind, "rgb": rgb}
        if kind != 5:
            row.update({
                "position": [float(value) for value in tokens[1:4]],
                "direction": [float(value) for value in tokens[4:7]],
                "radius": float(tokens[10]),
                "stopdot": float(tokens[11]),
                "stopdot2": float(tokens[12]),
                "sky": int(tokens[15]) if len(tokens) >= 16 else 0,
            })
        lights.append(row)

    env = {}
    relevant_env = {
        "fog", "fogcolor", "fogstart", "fogend",
        "skyfog", "skyfogcolor", "skyfogstart", "skyfogend",
    }
    for tokens in lines("env"):
        if len(tokens) >= 2 and tokens[0] in relevant_env:
            if tokens[0] in {"fog", "skyfog"}:
                env[tokens[0]] = tokens[1] == "1"
            else:
                env[tokens[0]] = [number(value) for value in tokens[1:]]

    sky = {"origin": [0.0, 0.0, 0.0], "scale": 16.0}
    for tokens in lines("sky"):
        if len(tokens) == 4 and tokens[0] == "origin":
            sky["origin"] = [float(value) for value in tokens[1:4]]
        elif len(tokens) == 2 and tokens[0] == "scale":
            sky["scale"] = float(tokens[1])

    spawn = {"origin": None, "yaw": 0.0}
    for tokens in lines("spawn"):
        if len(tokens) >= 4 and tokens[0] == "origin":
            spawn["origin"] = [float(value) for value in tokens[1:4]]
        elif len(tokens) >= 2 and tokens[0] == "yaw":
            spawn["yaw"] = float(tokens[1])

    return {
        "props": props,
        "decals": decals,
        "lights": lights,
        "environment": env,
        "sky": sky,
        "spawn": spawn,
    }


def unreal_output_path(repo_root: Path, object_path: str, stage: str | None = None) -> Path:
    """Map an owned virtual package path to the package file written by this project."""

    package = object_path.split(".", 1)[0].rstrip("/")
    if package.startswith("/ElysiumBaked/"):
        relative = package.removeprefix("/ElysiumBaked/")
        suffix = ".umap" if stage == "level" else ".uasset"
        return repo_root / "Plugins" / "ElysiumBaked" / "Content" / (relative + suffix)
    if package.startswith("/Game/"):
        relative = package.removeprefix("/Game/")
        return repo_root / "Content" / (relative + ".uasset")
    raise ValueError(f"asset receipt is outside an owned mount: {object_path}")


class AssetReceiptStore:
    """Verified per-map asset recipes, separate from coarse task receipts.

    One file per map keeps a grid export from turning the root task manifest into a very large
    document that must be rewritten for every promoted map.
    """

    def __init__(self, export_root: Path, map_name: str):
        self.path = export_root / ASSET_RECEIPT_DIR / f"{map_name}.json"
        self.map_name = map_name
        self.data = self._empty()
        if self.path.is_file():
            try:
                loaded = json.loads(self.path.read_text(encoding="utf-8"))
                if (
                    loaded.get("schema") == ASSET_RECEIPT_SCHEMA
                    and loaded.get("version") == ASSET_SCHEMA_VERSION
                    and loaded.get("map") == map_name
                    and isinstance(loaded.get("stages"), dict)
                ):
                    self.data = loaded
            except (OSError, ValueError):
                pass

    def _empty(self) -> dict:
        return {
            "schema": ASSET_RECEIPT_SCHEMA,
            "version": ASSET_SCHEMA_VERSION,
            "map": self.map_name,
            "stages": {},
        }

    def stage_assets(self, stage: str) -> dict[str, dict]:
        stage_data = self.data.get("stages", {}).get(stage, {})
        assets = stage_data.get("assets", {}) if isinstance(stage_data, dict) else {}
        return assets if isinstance(assets, dict) else {}

    def matches(self, stage: str, object_path: str, fingerprint: str) -> bool:
        saved = self.stage_assets(stage).get(object_path, {})
        return saved.get("fingerprint") == fingerprint

    def has_stage(self, stage: str, policy: str) -> bool:
        saved = self.data.get("stages", {}).get(stage)
        return (
            isinstance(saved, dict)
            and saved.get("policy") == policy
            and isinstance(saved.get("assets"), dict)
        )

    def replace_stages(self, replacements: dict[str, dict]) -> None:
        stages = dict(self.data.get("stages", {}))
        for stage, value in replacements.items():
            stages[stage] = value
        self.data = {**self._empty(), "stages": stages}
        _atomic_json(self.path, self.data)


def checkpoint_receipts(export_root: Path, map_name: str, stages: dict) -> Path:
    """Persist, from inside the bake, the receipts of the stages it has already flushed.

    `promote_asset_run` writes this same document once the orchestrator has validated a run report,
    and that stays the checked path. This is the floor beneath it: a bake that saved its packages
    and then died with its whole process tree still leaves the next run everything that landed.

    Only flushed work belongs here, so the caller checkpoints after saving. A stage that has
    registered no asset has not started, and states nothing rather than erasing what an earlier run
    left; a stage still in progress states the assets it has registered so far, and an asset the
    document does not name is simply rebuilt.
    """

    payload = {
        stage: {"policy": str(data["policy"]), "assets": dict(data["assets"])}
        for stage, data in stages.items()
        if data.get("assets")
    }
    store = AssetReceiptStore(export_root, map_name)
    if payload:
        store.replace_stages(payload)
    return store.path


def stage_code_fingerprint(
    config,
    stage: str,
    *,
    cache: ContentDigestCache | None = None,
) -> str:
    bake_map = config.repo_root / "pipeline" / "unreal" / "bake_map.py"
    driver = config.repo_root / "pipeline" / "src" / "elysium_pipeline" / "unreal.py"
    cache_module = config.repo_root / "pipeline" / "src" / "elysium_pipeline" / "bake_cache.py"
    cache_symbols = ["_canonical", "asset_recipe_fingerprint"]
    if stage == "level":
        cache_symbols.append("level_sidecar_recipe")
    return fingerprint_content(
        _stage_code_paths(config, stage),
        extra=(
            _bake_map_source_fingerprint(bake_map, stage, cache),
            _python_symbols_fingerprint(cache_module, cache_symbols, cache),
            _python_symbols_fingerprint(
                driver, ("_run", "editor_executable", "bake_maps"), cache
            ),
        ),
        cache=cache,
    )


def _node_source(lines: list[str], node: ast.AST) -> str:
    start = max(0, getattr(node, "lineno", 1) - 1)
    end = getattr(node, "end_lineno", start + 1)
    return "".join(lines[start:end])


def _bake_source_closure_fingerprint(
    path: Path,
    stage: str,
    class_names: Sequence[str],
    roots: Iterable[str],
    label: str,
    cache: ContentDigestCache | None = None,
) -> str:
    """Hash shared driver code plus the bake-class method closure one stage reaches.

    ``bake_map.py`` deliberately remains one Unreal entrypoint.  Treating the whole file as one
    input would make a particle-stage edit invalidate world and prop meshes, so its class methods
    are fingerprinted by the ``self.method()`` closure rooted at each stage instead.  Where a
    subclass overrides a method, both bodies join the closure and both are hashed, because either
    may be the one that runs.
    """

    try:
        source = path.read_text(encoding="utf-8")
        tree = ast.parse(source)
    except (OSError, SyntaxError, UnicodeError):
        return fingerprint_content(
            [path], extra=(f"{label}-fallback", stage), cache=cache
        )

    lines = source.splitlines(keepends=True)
    wanted_classes = set(class_names)
    classes = [
        node
        for node in tree.body
        if isinstance(node, ast.ClassDef) and node.name in wanted_classes
    ]
    if {node.name for node in classes} != wanted_classes:
        return fingerprint_content(
            [path], extra=(f"{label}-no-class", stage), cache=cache
        )

    methods: dict[str, list[ast.AST]] = {}
    for bake_class in classes:
        for node in bake_class.body:
            if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
                methods.setdefault(node.name, []).append(node)

    selected = set(roots)
    pending = list(selected)
    while pending:
        for node in methods.get(pending.pop(), ()):
            for child in ast.walk(node):
                if (
                    isinstance(child, ast.Attribute)
                    and isinstance(child.value, ast.Name)
                    and child.value.id == "self"
                    and child.attr in methods
                    and child.attr not in selected
                ):
                    selected.add(child.attr)
                    pending.append(child.attr)

    # Imports, constants, top-level helpers, argument parsing, and the driver are shared.  Class
    # method bodies are the only portion split by stage.
    shared = [
        node
        for node in tree.body
        if not (isinstance(node, ast.ClassDef) and node.name in wanted_classes)
    ]
    stage_nodes = [node for name in selected for node in methods.get(name, ())]
    ordered = sorted([*shared, *stage_nodes], key=lambda node: getattr(node, "lineno", 0))
    return _hash_parts([stage, *(_node_source(lines, node) for node in ordered)])


def _bake_map_source_fingerprint(
    path: Path,
    stage: str,
    cache: ContentDigestCache | None = None,
) -> str:
    return _bake_source_closure_fingerprint(
        path,
        stage,
        ("Bake",),
        set(_BASE_METHODS) | set(_STAGE_METHODS[stage]),
        "bake-map",
        cache,
    )


def _corpus_bake_source_fingerprint(
    path: Path,
    stage: str,
    cache: ContentDigestCache | None = None,
) -> str:
    return _bake_source_closure_fingerprint(
        path,
        stage,
        _CORPUS_CLASSES,
        set(_CORPUS_BASE_METHODS) | set(_CORPUS_STAGE_METHODS[stage]),
        "corpus-bake",
        cache,
    )


def module_closure_fingerprint(
    path: Path,
    key: str,
    roots: Iterable[str],
    *,
    shared_names: Iterable[str] = (),
    cache: ContentDigestCache | None = None,
) -> str:
    """Hash a module's shared nodes plus the call closure of the named top-level functions.

    The function-level twin of `_bake_source_closure_fingerprint`, for an Unreal entrypoint whose
    stages are module-level functions rather than methods of a bake class. Imports, constants and
    every node that is not a top-level function are shared and always hashed, as are the functions
    named in `shared_names` -- a driver that calls every stage is hashed verbatim rather than
    expanded, because expanding it would make every stage reach every other one.
    """

    try:
        source = path.read_text(encoding="utf-8")
        tree = ast.parse(source)
    except (OSError, SyntaxError, UnicodeError):
        return fingerprint_content([path], extra=(f"{key}-fallback",), cache=cache)

    lines = source.splitlines(keepends=True)
    functions: dict[str, ast.AST] = {}
    for node in tree.body:
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            functions[node.name] = node

    shared_set = set(shared_names)
    missing = (set(roots) | shared_set) - functions.keys()
    if missing:
        return fingerprint_content(
            [path], extra=(f"{key}-missing", *sorted(missing)), cache=cache
        )

    selected = set(roots)
    pending = list(selected)
    while pending:
        node = functions[pending.pop()]
        for child in ast.walk(node):
            if (
                isinstance(child, ast.Name)
                and child.id in functions
                and child.id not in selected
                and child.id not in shared_set
            ):
                selected.add(child.id)
                pending.append(child.id)

    shared = [
        node
        for node in tree.body
        if not isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        or node.name in shared_set
    ]
    stage_nodes = [functions[name] for name in selected]
    ordered = sorted([*shared, *stage_nodes], key=lambda node: getattr(node, "lineno", 0))
    return _hash_parts([key, *(_node_source(lines, node) for node in ordered)])


def _python_symbols_fingerprint(
    path: Path,
    names: Sequence[str],
    cache: ContentDigestCache | None = None,
) -> str:
    try:
        source = path.read_text(encoding="utf-8")
        tree = ast.parse(source)
    except (OSError, SyntaxError, UnicodeError):
        return fingerprint_content(
            [path], extra=("python-symbol-fallback", *names), cache=cache
        )
    lines = source.splitlines(keepends=True)
    wanted = [
        node
        for node in tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef))
        and node.name in names
    ]
    if len(wanted) != len(set(names)):
        return fingerprint_content(
            [path], extra=("python-symbol-missing", *names), cache=cache
        )
    ordered = sorted(wanted, key=lambda node: node.lineno)
    return _hash_parts(_node_source(lines, node) for node in ordered)


_CORPUS_DOCUMENT_CACHE: dict[Path, tuple[tuple[int, int], dict]] = {}

#: The map stages whose products depend on shared-corpus entries.
CORPUS_SLICE_STAGES = ("materials", "world", "sky", "level")


def _corpus_document(path: Path) -> dict | None:
    """Parse one shared-corpus document, memoized on the file's stat identity."""
    try:
        stat = path.stat()
    except OSError:
        return None
    identity = (stat.st_size, stat.st_mtime_ns)
    cached = _CORPUS_DOCUMENT_CACHE.get(path)
    if cached is not None and cached[0] == identity:
        return cached[1]
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    _CORPUS_DOCUMENT_CACHE[path] = (identity, document)
    return document


def _map_material_keys(map_root: Path, map_name: str) -> set[str]:
    """The corpus material keys the map's `.mtl` binds to its surfaces."""
    keys: set[str] = set()
    path = map_root / f"{map_name}.mtl"
    if not path.is_file():
        return keys
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        tokens = line.split()
        if len(tokens) >= 2 and tokens[0] == "mat":
            keys.add(tokens[1])
    return keys


def _map_placed_stems(map_root: Path, map_name: str) -> set[str]:
    """The corpus model stems the map places, from its `.props` and `.ents` sidecars."""
    stems: set[str] = set()
    props = map_root / f"{map_name}.props"
    if props.is_file():
        for line in props.read_text(encoding="utf-8", errors="replace").splitlines():
            tokens = line.split()
            if tokens:
                stems.add(tokens[0])
    ents = map_root / f"{map_name}.ents"
    if ents.is_file():
        try:
            entities = json.loads(ents.read_text(encoding="utf-8")).get("entities", [])
        except (OSError, ValueError):
            entities = []
        for entity in entities:
            stem = entity.get("model_mesh", "") if isinstance(entity, dict) else ""
            if stem:
                stems.add(stem)
    return stems


def _map_corpus_slice(
    export_root: Path, map_name: str
) -> tuple[dict, dict, set[str], dict[str, dict | None]] | None:
    """(manifest, materials document, drawn material keys, drawn model rows), or `None`.

    A map's own sidecars name exactly what it draws -- the `.mtl`'s `mat` keys plus the model rows
    (and their material keys) for its placed stems.
    """
    manifest = _corpus_document(shared_corpus.manifest_path(export_root))
    materials = _corpus_document(shared_corpus.materials_path(export_root))
    if manifest is None or materials is None:
        return None
    model_rows = manifest.get("models", {})
    map_root = export_root / map_name
    keys = _map_material_keys(map_root, map_name)
    models: dict[str, dict | None] = {}
    for stem in sorted(_map_placed_stems(map_root, map_name)):
        row = model_rows.get(stem)
        models[stem] = row
        if isinstance(row, dict):
            keys.update(row.get("materials", {}).values())
    return manifest, materials, keys, models


def _digest_json(payload) -> str:
    encoded = json.dumps(
        payload, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8", errors="surrogateescape")
    return hashlib.sha256(encoded).hexdigest()


def corpus_slice_fingerprint(export_root: Path, map_name: str) -> str:
    """Digest only the shared-corpus entries this map draws.

    The corpus documents describe the whole install, so taking them as stage inputs marks every
    map stale whenever any one entry changes. The slice a map draws is the fingerprint instead, so
    an entry no map draws invalidates no map.
    """
    sliced = _map_corpus_slice(export_root, map_name)
    if sliced is None:
        return "corpus-missing"
    manifest, materials, keys, models = sliced
    material_records = materials.get("materials", {})
    return _digest_json({
        "revision": manifest.get("revision"),
        "materials": {key: material_records.get(key) for key in sorted(keys)},
        "models": models,
    })


def _slice_env_mask_paths(export_root: Path, map_name: str) -> list[Path]:
    """The env-mask PNGs the map's drawn materials name, as material-stage file inputs.

    A reflective material instance stamps `EnvMaskCoarseMip` from the mask PNG's own header, so
    the mask's bytes decide a material parameter and not only the texture asset it binds.
    """
    sliced = _map_corpus_slice(export_root, map_name)
    if sliced is None:
        return []
    _manifest, materials, keys, _models = sliced
    records = materials.get("materials", {})
    corpus = shared_corpus.corpus_dir(export_root)
    paths = set()
    for key in keys:
        record = records.get(key)
        if not isinstance(record, dict):
            continue
        relative = str(record.get("env_mask") or "")
        if not relative:
            continue
        path = corpus / relative
        if path.is_file():
            paths.add(path)
    return sorted(paths, key=lambda path: str(path).lower())


def _map_catalogue_stems(map_root: Path, map_name: str) -> set[str]:
    """The placed-model catalogue keys the map's `.props` rows join to.

    A `.props` row carries two identities: field 0 is the corpus static-mesh stem, and field 11 is
    the normalized model path whose `placed_models.model_stem` keys `npc/npc_index.json`. A row
    without field 11 predates the catalogue and joins on its static stem.
    """
    from elysium_pipeline import placed_models

    stems: set[str] = set()
    path = map_root / f"{map_name}.props"
    if not path.is_file():
        return stems
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        tokens = line.split()
        if len(tokens) < 9:
            continue
        stems.add(placed_models.model_stem(tokens[11]) if len(tokens) >= 12 else tokens[0])
    return stems


def _placed_index_slice_fingerprint(export_root: Path, map_name: str) -> str:
    """Digest only the `npc/npc_index.json` rows this map's placed props join to.

    The level stage reads each row to decide static or skeletal placement and, for a skeletal one,
    which rest clip the placement poses in. The document covers the whole catalogue, so the slice
    is the rows the map's own `.props` stems reach; the manifest version joins them because it is
    what gates the join at all.
    """
    document = _corpus_document(export_root / "npc" / "npc_index.json")
    if not isinstance(document, dict):
        return "npc-index-missing"
    rows = document.get("placed_models", {})
    if not isinstance(rows, dict):
        return "npc-index-missing"
    return _digest_json({
        "manifest_version": document.get("manifest_version"),
        "placed_models": {
            stem: rows.get(stem)
            for stem in sorted(_map_catalogue_stems(export_root / map_name, map_name))
        },
    })


def _placed_skins_paths(export_root: Path, map_name: str) -> list[Path]:
    """The `.skins` sidecars of the map's placed models: the level stage bakes their alternate
    skins in as material overrides, so they are that stage's inputs."""
    props_dir = shared_corpus.props_dir(export_root)
    return [
        path
        for stem in sorted(_map_placed_stems(export_root / map_name, map_name))
        if (path := props_dir / f"{stem}.skins").is_file()
    ]


def _matching_files(root: Path, patterns: Iterable[str]) -> list[Path]:
    return sorted(
        {path for pattern in patterns for path in root.glob(pattern) if path.is_file()},
        key=lambda path: str(path).lower(),
    )


def _particle_inputs(export_root: Path, map_root: Path, map_name: str) -> list[Path]:
    document = map_root / f"{map_name}.particles.json"
    paths = [document]
    try:
        payload = json.loads(document.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return paths
    for relative in payload.get("particles", {}).get("sprites", []):
        paths.append(export_root / "particles" / (Path(relative).stem + ".png"))
    return paths


def stage_input_paths(
    export_root: Path,
    map_name: str,
    stage: str,
) -> tuple[Path, ...]:
    """What one map stage reads.

    Textures, materials and prop meshes are the shared corpus's, but a map depends only on the
    corpus entries it draws -- `corpus_slice_fingerprint` states that slice inside
    `stage_fingerprint`, so the whole-install documents are not file inputs here and a re-decoded
    material invalidates only the maps that draw it.
    """
    if stage not in STAGES:
        raise ValueError(f"unknown bake stage: {stage}")
    root = export_root / map_name
    world_mtl = root / f"{map_name}.mtl"
    local_materials = root / f"{map_name}.materials.json"

    if stage == "textures":
        # A map imports only its own baked env cubemaps and its rain height field.
        paths = [
            world_mtl,
            root / "tex" / "cube",
            root / f"{map_name}.weather.json",
            root / "weather",
        ]
    elif stage == "materials":
        paths = [
            world_mtl,
            local_materials,
            root / f"{map_name}.env",
            root / f"{map_name}.weather.json",
            *_slice_env_mask_paths(export_root, map_name),
        ]
    elif stage == "world":
        paths = [
            root / f"{map_name}.obj",
            root / f"{map_name}.blend",
            world_mtl,
            local_materials,
            *_matching_files(root, ("brushes/*.obj", "brushes/*.blend")),
        ]
    elif stage == "sky":
        paths = [root / f"{map_name}_sky.obj", world_mtl, local_materials]
    elif stage == "particles":
        paths = _particle_inputs(export_root, root, map_name)
    else:
        paths = [
            root / f"{map_name}.props",
            root / f"{map_name}.decals",
            root / f"{map_name}.lights",
            root / f"{map_name}.env",
            root / f"{map_name}.sky",
            root / f"{map_name}.spawn",
            root / f"{map_name}.ents",
            world_mtl,
            local_materials,
            *_placed_skins_paths(export_root, map_name),
        ]
    return tuple(dict.fromkeys(Path(path) for path in paths))


def _stage_code_paths(config, stage: str) -> tuple[Path, ...]:
    unreal_root = config.repo_root / "pipeline" / "unreal"
    source_root = config.repo_root / "Source" / "ElysiumUE"
    package_root = config.repo_root / "pipeline" / "src" / "elysium_pipeline"
    # Every stage reads the corpus keys, record shape and baked asset names through these two.
    paths = [
        unreal_root / "bake_lib.py",
        package_root / "shared_corpus.py",
        package_root / "asset_names.py",
    ]
    if stage == "materials":
        paths.extend(
            (
                unreal_root / "make_world_materials.py",
                unreal_root / "make_decal_material.py",
                unreal_root / "mat_fog.py",
            )
        )
    elif stage == "level":
        paths.extend(
            (
                source_root / "Public" / "ElysiumPropSkins.h",
                source_root / "Private" / "Visual" / "ElysiumPropSkins.cpp",
                # The stage joins each placed prop to the catalogue through `model_stem` and poses
                # it with the clip `select_rest_label` picks.
                package_root / "placed_models.py",
            )
        )
    elif stage == "particles":
        paths.extend(
            (
                unreal_root / "make_particle_systems.py",
                source_root / "Public" / "ElysiumParticleAssetBuilder.h",
                source_root / "Private" / "Editor" / "ElysiumParticleAssetBuilder.cpp",
            )
        )
    return tuple(paths)


def stage_fingerprint(
    config,
    map_name: str,
    stage: str,
    *,
    cache: ContentDigestCache | None = None,
) -> str:
    code_fingerprint = stage_code_fingerprint(config, stage, cache=cache)
    extra = [CACHE_REVISION, map_name, stage, code_fingerprint]
    if stage in CORPUS_SLICE_STAGES:
        extra.append(corpus_slice_fingerprint(config.export_root, map_name))
    if stage == "level":
        extra.append(_placed_index_slice_fingerprint(config.export_root, map_name))
    return fingerprint_content(
        stage_input_paths(config.export_root, map_name, stage),
        extra=tuple(extra),
        cache=cache,
    )


def _files(root: Path, pattern: str) -> list[Path]:
    return [path for path in root.glob(pattern) if path.is_file()]


def stage_output_paths(config, map_name: str, stage: str) -> tuple[Path, ...]:
    root = (
        config.repo_root
        / "Plugins"
        / "ElysiumBaked"
        / "Content"
        / map_name
    )
    if stage == "textures":
        # A map's own textures: the baked env cubemaps and the rain height field. Surface
        # textures are the corpus's package.
        paths = [
            *_files(root, "Textures/**/*.uasset"),
            *_files(root, "Weather/T_RainHeight.uasset"),
        ]
    elif stage == "materials":
        paths = [
            *_files(root, "Materials/**/*.uasset"),
            *_files(root, "Weather/MI_ElysiumRain.uasset"),
        ]
    elif stage == "world":
        paths = [
            *_files(root, "Meshes/SM_World_*.uasset"),
            *_files(root, "Brushes/**/*.uasset"),
        ]
    elif stage == "sky":
        paths = _files(root, "Meshes/SM_Sky_*.uasset")
    elif stage == "particles":
        paths = _files(root, "Particles/**/*.uasset")
    elif stage == "level":
        level = root / f"{map_name}.umap"
        paths = [level] if level.is_file() else []
    else:
        raise ValueError(f"unknown bake stage: {stage}")
    return tuple(sorted(set(paths), key=lambda path: str(path).lower()))


def stage_task(
    config,
    map_name: str,
    stage: str,
    *,
    cache: ContentDigestCache | None = None,
) -> Task:
    fingerprint = stage_fingerprint(config, map_name, stage, cache=cache)
    return Task(
        name=f"bake:{map_name}:{stage}",
        action=lambda: None,
        fingerprint=lambda value=fingerprint: value,
        outputs=stage_output_paths(config, map_name, stage),
    )


def plan_stages(
    manifest: Manifest,
    config,
    maps: Sequence[str],
    *,
    force: bool = False,
) -> dict[str, tuple[str, ...]]:
    plan: dict[str, tuple[str, ...]] = {}
    cache = ContentDigestCache(config.export_root / DIGEST_CACHE_FILE)
    try:
        for map_name in dict.fromkeys(maps):
            stale = set()
            asset_receipts = AssetReceiptStore(config.export_root, map_name)
            for stage in STAGES:
                task = stage_task(config, map_name, stage, cache=cache)
                fingerprint = task.fingerprint() if task.fingerprint else None
                policy = stage_code_fingerprint(config, stage, cache=cache)
                if (
                    force
                    or not asset_receipts.has_stage(stage, policy)
                    or not manifest.can_skip(task, fingerprint)
                ):
                    stale.add(stage)
            # Mesh package membership is discovered when the level is authored.  Rebuilding the
            # level whenever a mesh family changes safely handles new and pruned chunks while
            # still leaving texture- and particle-only work isolated.
            if stale.intersection({"world", "sky"}):
                stale.add("level")
            if stale:
                plan[map_name] = tuple(stage for stage in STAGES if stage in stale)
    finally:
        cache.write()
    return plan


def record_stages(
    manifest: Manifest,
    config,
    plan: dict[str, tuple[str, ...]],
    *,
    frozen_fingerprints: dict[str, dict[str, str]] | None = None,
) -> None:
    cache = ContentDigestCache(config.export_root / DIGEST_CACHE_FILE)
    try:
        for map_name, stages in plan.items():
            for stage in stages:
                task = stage_task(config, map_name, stage, cache=cache)
                fingerprint = (
                    frozen_fingerprints.get(map_name, {}).get(stage)
                    if frozen_fingerprints is not None
                    else None
                )
                manifest.record(
                    TaskResult(
                        name=task.name,
                        status="ok",
                        duration_seconds=0.0,
                        fingerprint=fingerprint or (
                            task.fingerprint() if task.fingerprint else None
                        ),
                        outputs=[str(path) for path in task.outputs],
                    )
                )
    finally:
        cache.write()


def create_asset_run_plan(
    config,
    plan: dict[str, tuple[str, ...]],
    *,
    force: bool,
) -> tuple[Path, dict]:
    """Freeze stage inputs and authoring policies for one Unreal invocation group."""

    run_id = uuid.uuid4().hex
    cache = ContentDigestCache(config.export_root / DIGEST_CACHE_FILE)
    try:
        maps = {}
        for map_name, stages in plan.items():
            maps[map_name] = {
                "stages": list(stages),
                "fingerprints": {
                    stage: stage_fingerprint(config, map_name, stage, cache=cache)
                    for stage in stages
                },
                "policies": {
                    stage: stage_code_fingerprint(config, stage, cache=cache)
                    for stage in stages
                },
            }
    finally:
        cache.write()
    document = {
        "schema": ASSET_RUN_SCHEMA,
        "version": ASSET_SCHEMA_VERSION,
        "run_id": run_id,
        "force": bool(force),
        "maps": maps,
    }
    path = config.export_root / ASSET_RUN_DIR / run_id / "plan.json"
    _atomic_json(path, document)
    return path, document


def corpus_stage_policy(
    config,
    stage: str,
    *,
    cache: ContentDigestCache | None = None,
) -> str:
    """The authoring-code identity of one shared-corpus stage.

    Split by the `self.method()` closure `bake_corpus` reaches for that stage across `Bake` and
    `CorpusBake`, exactly as a map stage is: the corpus is one commandlet that authors every
    texture, material and model in the install, so taking the whole entrypoint as the policy would
    make an edit to a method no corpus stage calls invalidate every receipt it holds.
    """

    unreal_root = config.repo_root / "pipeline" / "unreal"
    package_root = config.repo_root / "pipeline" / "src" / "elysium_pipeline"
    paths = [
        unreal_root / "bake_lib.py",
        # The corpus keys, record shape and baked asset names every stage authors through.
        package_root / "shared_corpus.py",
        package_root / "asset_names.py",
    ]
    if stage == "materials":
        paths.extend(
            (
                unreal_root / "make_world_materials.py",
                unreal_root / "make_decal_material.py",
                unreal_root / "mat_fog.py",
            )
        )
    return fingerprint_content(
        paths,
        extra=(
            "corpus-policy",
            stage,
            _corpus_bake_source_fingerprint(unreal_root / "bake_map.py", stage, cache),
        ),
        cache=cache,
    )


def corpus_stage_input_fingerprint(
    config,
    stage: str,
    *,
    cache: ContentDigestCache | None = None,
) -> str:
    """What one shared-corpus stage reads, together with the code that authors it."""

    export_root = config.export_root
    manifest = shared_corpus.manifest_path(export_root)
    if stage == "textures":
        paths = [manifest, shared_corpus.tex_dir(export_root)]
    elif stage == "materials":
        paths = [manifest, shared_corpus.materials_path(export_root)]
    elif stage == "props":
        paths = [manifest, shared_corpus.props_dir(export_root)]
    else:
        raise ValueError(f"unknown shared corpus bake stage: {stage}")
    return fingerprint_content(
        paths,
        extra=(
            CACHE_REVISION,
            shared_corpus.SCOPE,
            stage,
            corpus_stage_policy(config, stage, cache=cache),
        ),
        cache=cache,
    )


def create_corpus_run_plan(config, *, force: bool) -> tuple[Path, dict]:
    """Freeze the shared-corpus stage inputs and authoring policies for one Unreal invocation.

    The same document a map group's plan uses, holding the one scope the corpus has, so the
    commandlet's per-asset receipts are decided and promoted exactly as a map's are.
    """

    run_id = uuid.uuid4().hex
    stages = list(shared_corpus.STAGES)
    cache = ContentDigestCache(config.export_root / DIGEST_CACHE_FILE)
    try:
        maps = {
            shared_corpus.SCOPE: {
                "stages": stages,
                "fingerprints": {
                    stage: corpus_stage_input_fingerprint(config, stage, cache=cache)
                    for stage in stages
                },
                "policies": {
                    stage: corpus_stage_policy(config, stage, cache=cache)
                    for stage in stages
                },
            }
        }
    finally:
        cache.write()
    document = {
        "schema": ASSET_RUN_SCHEMA,
        "version": ASSET_SCHEMA_VERSION,
        "run_id": run_id,
        "force": bool(force),
        "maps": maps,
    }
    path = config.export_root / ASSET_RUN_DIR / run_id / "plan.json"
    _atomic_json(path, document)
    return path, document


def asset_run_report_path(export_root: Path, run_id: str, map_name: str) -> Path:
    return export_root / ASSET_RUN_DIR / run_id / f"{map_name}.json"


def write_asset_run_report(export_root: Path, report: dict) -> Path:
    path = asset_run_report_path(export_root, str(report["run_id"]), str(report["map"]))
    _atomic_json(path, report)
    return path


def load_asset_run_reports(config, document: dict) -> dict[str, dict]:
    reports = {}
    run_id = str(document["run_id"])
    for map_name, map_plan in document.get("maps", {}).items():
        path = asset_run_report_path(config.export_root, run_id, map_name)
        try:
            report = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError) as exc:
            raise RuntimeError(f"asset bake report missing or invalid for {map_name}: {exc}") from exc
        if (
            report.get("schema") != ASSET_RUN_SCHEMA
            or report.get("version") != ASSET_SCHEMA_VERSION
            or report.get("run_id") != run_id
            or report.get("map") != map_name
        ):
            raise RuntimeError(f"asset bake report identity mismatch for {map_name}")
        expected = set(map_plan.get("stages", []))
        actual = set(report.get("stages", {}))
        if actual != expected:
            raise RuntimeError(
                f"asset bake report stages mismatch for {map_name}: "
                f"expected {sorted(expected)}, got {sorted(actual)}"
            )
        for stage, stage_report in report["stages"].items():
            if stage_report.get("policy") != map_plan["policies"][stage]:
                raise RuntimeError(f"asset bake policy mismatch for {map_name}:{stage}")
            assets = stage_report.get("assets")
            if not isinstance(assets, dict):
                raise RuntimeError(f"asset bake report has no inventory for {map_name}:{stage}")
            counts = {}
            for name in ("built", "reused", "pruned"):
                value = stage_report.get(name)
                if not isinstance(value, int) or isinstance(value, bool) or value < 0:
                    raise RuntimeError(
                        f"asset bake report has invalid {name} count for {map_name}:{stage}"
                    )
                counts[name] = value
            if counts["built"] + counts["reused"] != len(assets):
                raise RuntimeError(
                    f"asset bake report count mismatch for {map_name}:{stage}"
                )
            for object_path, receipt in assets.items():
                if receipt.get("object_path") != object_path:
                    raise RuntimeError(f"asset receipt identity mismatch: {object_path}")
                fingerprint = receipt.get("fingerprint", "")
                if len(fingerprint) != 64:
                    raise RuntimeError(f"asset receipt has invalid fingerprint: {object_path}")
                expected_output = unreal_output_path(config.repo_root, object_path, stage)
                if Path(receipt.get("output", "")).resolve() != expected_output.resolve():
                    raise RuntimeError(f"asset receipt output mismatch: {object_path}")
                if not expected_output.is_file():
                    raise RuntimeError(f"asset receipt output is missing: {expected_output}")
        reports[map_name] = report
    return reports


def mutated_maps(reports: dict[str, dict]) -> set[str]:
    changed = set()
    for map_name, report in reports.items():
        for stage in report.get("stages", {}).values():
            if int(stage.get("built", 0)) or int(stage.get("pruned", 0)):
                changed.add(map_name)
                break
    return changed


def assert_asset_run_inputs_current(config, document: dict) -> None:
    cache = ContentDigestCache(config.export_root / DIGEST_CACHE_FILE)
    try:
        for map_name, map_plan in document.get("maps", {}).items():
            for stage, frozen in map_plan.get("fingerprints", {}).items():
                current = stage_fingerprint(config, map_name, stage, cache=cache)
                if current != frozen:
                    raise RuntimeError(
                        f"bake inputs changed during the run: {map_name}:{stage}"
                    )
    finally:
        cache.write()


def promote_asset_run(config, document: dict, reports: dict[str, dict]) -> None:
    for map_name, report in reports.items():
        replacements = {}
        for stage, stage_report in report.get("stages", {}).items():
            replacements[stage] = {
                "policy": stage_report["policy"],
                "assets": stage_report["assets"],
            }
        AssetReceiptStore(config.export_root, map_name).replace_stages(replacements)
