"""High-level export workflows spanning offline decoders and Unreal asset baking."""

from __future__ import annotations

import ast
from collections import defaultdict
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from concurrent.futures import ProcessPoolExecutor, as_completed
import hashlib
import importlib
import json
import os
from pathlib import Path
import threading
import time

from elysium_pipeline.clean import (
    INCOMPLETE_FILE,
    MANIFEST_FILE,
    adopt_export_root,
    clean_profile_outputs,
    validate_clean_targets,
    mark_complete,
)
from elysium_pipeline.tasking import (
    DIGEST_CACHE_FILE,
    ContentDigestCache,
    Manifest,
    Task,
    TaskFailure,
    TaskGraph,
    TaskProgress,
    TaskResult,
    fingerprint_content,
    fingerprint_paths,
)
from elysium_pipeline import shared_corpus, unreal, workers, native_model_pipeline


class OfflineExportFailure(RuntimeError):
    exit_code = 5


class ExportBakeFailure(RuntimeError):
    exit_code = 6


def default_jobs() -> int:
    """One worker per physical core; the policy lives with the pool that applies it."""
    return workers.default_jobs()


def _require_export_config(config) -> None:
    if config.game_root is None or config.work_root is None or config.export_root is None:
        raise ValueError("export requires configured game and work roots")


def _require_export_v2_config(config) -> None:
    if config.game_root is None or config.work_root is None or config.export_v2_root is None:
        raise ValueError("export_v2 requires configured game and work roots")


def _export_v2_root(config, seam: str) -> Path:
    """The publish root for one isolated GLB seam, under the export_v2 tree."""
    return config.export_v2_root / seam


def _source_index_fingerprint(index: dict) -> str:
    digest = hashlib.sha256()
    for key in sorted(index):
        kind, value = index[key]
        digest.update(key.encode("utf-8", errors="surrogateescape"))
        digest.update(b"\0")
        digest.update(str(kind).encode("ascii", errors="replace"))
        if kind == "loose":
            path = Path(value)
            try:
                stat = path.stat()
                detail = f"{path}:{stat.st_size}:{stat.st_mtime_ns}"
            except OSError:
                detail = f"{path}:missing"
        else:
            detail = repr(value)
        digest.update(detail.encode("utf-8", errors="surrogateescape"))
        digest.update(b"\0")
    return digest.hexdigest()


class _DecoderClosures:
    """Static import closures over `elysium_pipeline`, for scoped task fingerprints.

    One coarse fingerprint over every decoder module re-exports the whole profile for
    any one-line change, so each task instead hashes exactly the modules its entry
    function can reach. The entry set is the function's own `elysium_pipeline` imports
    -- the lazy-import convention the pipeline already follows makes those the honest
    dependency declaration -- expanded transitively through each module's parsed
    imports. Nothing is hand-listed, so a module cannot be left out by omission.
    """

    def __init__(self, package_root: Path):
        self.package_root = package_root
        self.modules: dict[str, Path] = {}
        for path in sorted(package_root.rglob("*.py")):
            parts = path.relative_to(package_root.parent).with_suffix("").parts
            name = ".".join(parts)
            if name.endswith(".__init__"):
                name = name[: -len(".__init__")]
            self.modules[name] = path
        self._imports: dict[str, frozenset[str]] = {}
        self._function_entries: dict[tuple[str, str], set[str]] = {}
        self._bundle_entries: dict[str, set[str]] = {}

    def _normalize(self, dotted: str, attribute: str | None = None) -> str | None:
        if attribute is not None:
            candidate = f"{dotted}.{attribute}"
            if candidate in self.modules:
                return candidate
        while dotted and dotted not in self.modules:
            dotted = dotted.rpartition(".")[0]
        return dotted or None

    def _collect(self, nodes) -> set[str]:
        found: set[str] = set()
        for node in nodes:
            for child in ast.walk(node):
                if isinstance(child, ast.Import):
                    for alias in child.names:
                        if alias.name.partition(".")[0] == "elysium_pipeline":
                            name = self._normalize(alias.name)
                            if name:
                                found.add(name)
                elif (isinstance(child, ast.ImportFrom) and child.level == 0
                      and child.module
                      and child.module.partition(".")[0] == "elysium_pipeline"):
                    for alias in child.names:
                        name = self._normalize(child.module, alias.name)
                        if name:
                            found.add(name)
        return found

    def imports_of(self, module: str) -> frozenset[str]:
        cached = self._imports.get(module)
        if cached is None:
            tree = ast.parse(self.modules[module].read_text(encoding="utf-8"))
            cached = frozenset(self._collect([tree]))
            self._imports[module] = cached
        return cached

    def function_entries(self, module: str, function: str) -> set[str]:
        """The `elysium_pipeline` modules one function imports (its whole body).
        Memoized like `imports_of`: the sources are fixed for the process lifetime."""
        cached = self._function_entries.get((module, function))
        if cached is not None:
            return cached
        tree = ast.parse(self.modules[module].read_text(encoding="utf-8"))
        for node in ast.walk(tree):
            if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))                     and node.name == function:
                entries = self._collect(node.body)
                if not entries:
                    raise ValueError(f"{module}.{function} imports no pipeline module")
                self._function_entries[(module, function)] = entries
                return entries
        raise ValueError(f"{module} has no function {function}")

    def bundle_entries(self, bundle: str) -> set[str]:
        """The modules `_run_bundle`'s branch for one bundle imports, read off its AST
        so the dispatch and the fingerprint cannot drift apart. Memoized per bundle."""
        cached = self._bundle_entries.get(bundle)
        if cached is not None:
            return cached
        module = "elysium_pipeline.exporters.export_all"
        tree = ast.parse(self.modules[module].read_text(encoding="utf-8"))
        for node in ast.walk(tree):
            if isinstance(node, ast.FunctionDef) and node.name == "_run_bundle":
                branch = node.body
                while branch:
                    test = branch[0]
                    if not isinstance(test, ast.If):
                        branch = branch[1:]
                        continue
                    literals = {c.value for c in ast.walk(test.test)
                                if isinstance(c, ast.Constant)}
                    if bundle in literals:
                        entries = self._collect(test.body)
                        if entries:
                            self._bundle_entries[bundle] = entries
                            return entries
                    branch = test.orelse
                raise ValueError(f"_run_bundle has no branch for bundle {bundle!r}")
        raise ValueError("export_all has no _run_bundle")

    def closure_files(self, entries: set[str]) -> list[Path]:
        seen: set[str] = set()
        frontier = sorted(entries)
        while frontier:
            name = frontier.pop()
            if name in seen or name not in self.modules:
                continue
            seen.add(name)
            frontier.extend(self.imports_of(name) - seen)
        return [self.modules[name] for name in sorted(seen)]


_DECODER_CLOSURES: dict[str, _DecoderClosures] = {}


def _decoder_closures(config) -> _DecoderClosures:
    root = config.repo_root / "pipeline" / "src" / "elysium_pipeline"
    key = str(root)
    if key not in _DECODER_CLOSURES:
        _DECODER_CLOSURES[key] = _DecoderClosures(root)
    return _DECODER_CLOSURES[key]


def _scoped_source_fingerprint(config, entries: set[str], *,
                               index_fingerprint: str = "",
                               cache: ContentDigestCache | None = None) -> str:
    """Content hash of one entry set's import closure, plus the install identity.

    A caller-supplied ``cache`` is shared and left for its owner to write; a run computing a
    dozen scoped fingerprints then parses and rewrites the digest store once rather than per
    call. Without one the call owns a private cache and writes it itself.
    """
    closures = _decoder_closures(config)
    owned = cache is None
    if owned:
        cache = ContentDigestCache(config.export_root / DIGEST_CACHE_FILE)
    try:
        return fingerprint_content(
            closures.closure_files(entries),
            extra=("decoder-v2", index_fingerprint, *sorted(entries)),
            cache=cache,
        )
    finally:
        if owned:
            cache.write()


def _raise_results(results) -> None:
    failures = [result for result in results if not result.ok]
    if failures:
        raise OfflineExportFailure(
            "; ".join(f"{item.name}: {item.error or item.status}" for item in failures)
        )


def _map_tasks(config, map_names: Sequence[str], index: dict, source_fingerprint: str) -> list[Task]:
    from elysium_pipeline.exporters import export_all
    from elysium_pipeline.formats import install

    tasks: list[Task] = []
    for map_name in map_names:
        bsp_path = Path(install.map_path(map_name))
        output_dir = config.export_root / map_name

        def action(name=map_name) -> None:
            _raise_results(
                export_all.export_maps(
                    [name],
                    out_root=config.export_root,
                    index=index,
                    continue_on_error=False,
                )
            )

        # The BSP is the only file input. Every format, exporter and package-root module a map
        # export reads -- `shared_corpus`, `placed_models`, `asset_names` included -- is already
        # hashed by content into `source_fingerprint`.
        def fingerprint(path=bsp_path, name=map_name) -> str:
            return fingerprint_paths([path], extra=("map", name, source_fingerprint))

        tasks.append(
            Task(
                name=f"map:{map_name}",
                action=action,
                fingerprint=fingerprint,
                outputs=(
                    output_dir / f"{map_name}.obj",
                    output_dir / f"{map_name}.ents",
                ) + ((output_dir / f"{map_name}.weather.json",)
                     if map_name == "sm_hub_1" else ()),
            )
        )
    return tasks


def _bundle_outputs(export_root: Path, bundle: str) -> tuple[Path, ...]:
    mapping = {
        "audio": (export_root / "audio" / "catalog.json",),
        "particles": (export_root / "particles" / "manifest.json",),
        "scripts": (export_root / "scripts", export_root / "dlg"),
        "signs": (export_root / "signs",),
        "vdata": (export_root / "vdata",),
        "cfg": (export_root / "cfg",),
        "scenes": (export_root / "scenes",),
        "ui": (export_root / "ui" / "strings.json",),
    }
    return mapping.get(bundle, ())


#: Bundles that read per-map export products under `$ELYSIUM_EXPORT_ROOT/<map>/` and therefore
#: wait on every map task: `audio` reads each map's `.ents` for its WAV and soundscheme
#: references. Every other bundle
#: reads the install (or the pre-graph shared corpus) directly and starts immediately;
#: `scenes` only globs whatever `.ents` already exist for a report-only cross-check that its
#: own exporter declares independent of the map export.
MAP_DEPENDENT_BUNDLES = frozenset({"audio"})


def _bundle_tasks(
    config,
    bundles: Sequence[str],
    maps: Sequence[str],
    index: dict,
    fingerprints: Mapping[str, str],
) -> list[Task]:
    from elysium_pipeline.exporters import export_all

    tasks: list[Task] = []
    all_map_dependencies = tuple(f"map:{name}" for name in maps)
    for bundle in bundles:
        name = f"bundle:{bundle}"
        # Only bundles consuming exported per-map products wait on the maps. The others
        # write disjoint directories from the install and run concurrently.
        dependencies = (
            all_map_dependencies if bundle in MAP_DEPENDENT_BUNDLES else ()
        )

        def action(bundle_name=bundle) -> None:
            _raise_results(
                export_all.export_bundles(
                    [bundle_name],
                    maps=maps,
                    force=True,
                    index=index,
                    continue_on_error=False,
                )
            )

        tasks.append(
            Task(
                name=name,
                action=action,
                dependencies=dependencies,
                # No file input of its own: every format, exporter and package-root module a
                # bundle reads -- `shared_corpus`, `placed_models`, `asset_names` included -- is
                # already hashed by content into `source_fingerprint`.
                fingerprint=lambda b=bundle: fingerprint_paths(
                    [], extra=("bundle", b, fingerprints[b], *maps)
                ),
                outputs=_bundle_outputs(config.export_root, bundle),
            )
        )
    return tasks


def run_offline_profile(
    config,
    profile: str,
    *,
    clean: bool,
    force: bool,
    jobs: int,
) -> tuple[list[str], dict[str, TaskResult]]:
    _require_export_config(config)
    if clean:
        # A profile clean empties only what the profile regenerates: the loose export root,
        # the policy packages and the corpus/map bakes. The GLB corpus and the canonical
        # native lanes it reads (`import textures/materials/models`) are prerequisites with
        # their own producer-owned prune, so they stay and are re-run with `--force`.
        _require_export_v2_config(config)
        native_model_pipeline.require_map_prerequisites(config)
        targets = validate_clean_targets(
            repo_root=config.repo_root,
            game_root=config.game_root,
            work_root=config.work_root,
            export_root=config.export_root,
            export_v2_root=config.export_v2_root,
        )
        clean_profile_outputs(targets)
        force = True
    else:
        adopt_export_root(config.export_root, config.work_root)
    config.export_root.mkdir(parents=True, exist_ok=True)

    # Lazy imports are load-bearing: config.apply_environment() must run first.
    from elysium_pipeline.exporters import export_all
    from elysium_pipeline.formats import install

    maps = export_all.maps_for_profile(profile)
    bundles = export_all.bundles_for_profile(profile)
    index = install.build_index()
    stamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
    progress = TaskProgress(
        total=len(maps) + len(bundles),
        log_path=config.log_root / f"{stamp}-export-tasks.log",
    )
    # One digest cache for every scoped fingerprint this run computes -- constructed after the
    # clean, parsed once, written once at the end, instead of a parse-and-rewrite per call.
    cache = ContentDigestCache(config.export_root / DIGEST_CACHE_FILE)
    with progress:
        progress.note(
            f"export {profile}: {len(maps)} map(s) + {len(bundles)} bundle(s), "
            f"{max(1, jobs)} job(s)"
        )
        # The shared corpus is a prerequisite of the graph rather than a node in it: every map
        # export resolves its materials and textures against it. It is decoded once here.
        with progress.stage("corpus"):
            ensure_corpus_export(config, force=force or clean, index=index, jobs=jobs,
                                 cache=cache)
        closures = _decoder_closures(config)
        index_fingerprint = _source_index_fingerprint(index)
        map_fingerprint = _scoped_source_fingerprint(
            config,
            closures.function_entries("elysium_pipeline.exporters.export_all", "export_maps"),
            index_fingerprint=index_fingerprint, cache=cache)
        bundle_fingerprints = {
            bundle: _scoped_source_fingerprint(
                config, closures.bundle_entries(bundle),
                index_fingerprint=index_fingerprint, cache=cache)
            for bundle in bundles
        }
        cache.write()
        manifest = Manifest(config.export_root / MANIFEST_FILE)
        dependency_fingerprint = fingerprint_paths(
            [config.repo_root / "dev" / "dependencies.lock.json"]
        )
        manifest.set_context(
            profile=profile,
            source_fingerprint=map_fingerprint,
            dependency_fingerprint=dependency_fingerprint,
            configuration={
                "game_root": str(config.game_root),
                "export_root": str(config.export_root),
                "jobs": max(1, jobs),
            },
            maps=list(maps),
            bundles=list(bundles),
        )
        graph = TaskGraph(
            [
                *_map_tasks(config, maps, index, map_fingerprint),
                *_bundle_tasks(config, bundles, maps, index, bundle_fingerprints),
            ]
        )
        results = graph.run(
            jobs=max(1, jobs),
            force=force,
            manifest=manifest,
            fail_fast=True,
            progress=progress,
        )
    return maps, results


def _policy_generator_names(config) -> list[str]:
    unreal_root = config.repo_root / "pipeline" / "unreal"
    build_content = unreal_root / "build_content.py"
    try:
        tree = ast.parse(build_content.read_text(encoding="utf-8"))
        for node in tree.body:
            if not isinstance(node, (ast.Assign, ast.AnnAssign)):
                continue
            targets = node.targets if isinstance(node, ast.Assign) else [node.target]
            if not any(
                isinstance(target, ast.Name) and target.id == "GENERATORS"
                for target in targets
            ):
                continue
            value = ast.literal_eval(node.value)
            return [str(item) for item in value]
    except (OSError, SyntaxError, ValueError, TypeError):
        return []
    return []


def _with_local_script_imports(unreal_root: Path, scripts: Sequence[Path]) -> list[Path]:
    """The given editor scripts plus their `from pipeline.unreal import ...` closure.

    Followed without importing the modules (they import ``unreal`` and are only executable
    inside an editor process).
    """
    pending = list(scripts)
    seen = set(scripts)
    while pending:
        script = pending.pop()
        try:
            tree = ast.parse(script.read_text(encoding="utf-8"))
        except (OSError, SyntaxError, UnicodeError):
            continue
        for node in ast.walk(tree):
            if not isinstance(node, ast.ImportFrom) or node.module != "pipeline.unreal":
                continue
            for alias in node.names:
                dependency = unreal_root / f"{alias.name}.py"
                if dependency.is_file() and dependency not in seen:
                    seen.add(dependency)
                    pending.append(dependency)
    return sorted(seen, key=lambda path: str(path).lower())


def _policy_script_paths(config, *, only_generators: Sequence[str] | None = None,
                         exclude_generators: Sequence[str] = (),
                         include_fonts: bool = True) -> list[Path]:
    unreal_root = config.repo_root / "pipeline" / "unreal"
    build_content = unreal_root / "build_content.py"
    generator_names = _policy_generator_names(config)
    if only_generators is not None:
        selected = set(only_generators)
        generator_names = [name for name in generator_names if name in selected]
    excluded = set(exclude_generators)
    generator_names = [name for name in generator_names if name not in excluded]
    scripts = [build_content, *(unreal_root / name for name in generator_names)]
    if include_fonts:
        scripts.append(unreal_root / "make_ui_fonts.py")
    return _with_local_script_imports(unreal_root, scripts)


def _bake_script_fingerprint(config, names: Sequence[str], *,
                             cache: ContentDigestCache | None = None) -> str:
    """Content identity of the editor scripts driving one bake launch.

    Covers the named `pipeline/unreal` scripts, their local-import closure, and the
    `elysium_pipeline` modules those scripts import, expanded through the same parsed-import
    closure the decoder fingerprints use -- so a helper the bake reaches cannot be left out
    by omission from a hand-kept list.
    """
    unreal_root = config.repo_root / "pipeline" / "unreal"
    scripts = _with_local_script_imports(
        unreal_root, [unreal_root / name for name in names])
    closures = _decoder_closures(config)
    entries: set[str] = set()
    for script in scripts:
        try:
            tree = ast.parse(script.read_text(encoding="utf-8"))
        except (OSError, SyntaxError, UnicodeError):
            continue
        entries |= closures._collect([tree])
    return fingerprint_content(
        [*scripts, *closures.closure_files(entries)],
        extra=("bake-scripts-v1", *names),
        cache=cache,
    )


def _policy_fingerprint(config, *, exclude_generators: Sequence[str] = (),
                        cache: ContentDigestCache | None = None) -> str:
    scripts = [
        *_policy_script_paths(config, exclude_generators=exclude_generators),
        config.repo_root / "Content" / "Fonts",
        # The Kenney PNGs are the source for generated CommonInput textures. Keep the source
        # directory beside Fonts in the policy fingerprint so replacing or extending a glyph
        # cannot leave a cached Unreal texture behind.
        config.repo_root / "Content" / "InputPrompts",
        # `make_input_assets.py` reads this CSV, and script discovery above only walks imports --
        # so without naming it here, editing the committed input table alone leaves the task
        # cached and the generated actions, mapping context and action set stale. The failure is
        # silent in the worst way: the export reports success and the button does nothing.
        config.repo_root / "Config" / "ElysiumInputActions.csv",
        # `make_player_anim_bp.py` reads the tracked graph text, and script discovery above only
        # walks imports -- so the text that IS the graph has to be named for the same reason the
        # CSV above does. A directory rather than the one file, so a second graph is covered the
        # day it is added rather than the day someone remembers this list.
        config.repo_root / "pipeline" / "unreal" / "graphs",
        # The generated Animation Blueprint is compiled against its native parent and binds to that
        # class's reflected properties by name. A `-game` run loads the serialized class rather than
        # recompiling it, so a property the graph binds to that the class no longer declares
        # resolves to nothing: transitions read false, the machine never leaves its entry state, and
        # the body poses one frame forever with no error anywhere. The class surface is a
        # generator input.
        config.repo_root / "Source" / "ElysiumUE" / "Private" / "Visual"
        / "ElysiumBipedAnimInstance.h",
        config.repo_root / "Source" / "ElysiumUE" / "Private" / "Visual"
        / "ElysiumBodyAnimInstance.h",
        config.repo_root / "Source" / "ElysiumUE" / "Public" / "ElysiumAnimGraphLibrary.h",
        config.repo_root / "Source" / "ElysiumUE" / "Private" / "Editor"
        / "ElysiumAnimGraphLibrary.cpp",
        # The graph places this project's own animation node, whose pin names ARE the wiring the
        # generator addresses by string. Rename a pin and every wire to it goes dead in a graph
        # that still compiles, so the node's declaration is a generator input like the two anim
        # instance headers above.
        config.repo_root / "Source" / "ElysiumUE" / "Public" / "ElysiumPostAdditiveNode.h",
        config.export_root / "particles" / "manifest.json",
        config.export_root / "particles" / "dropletfast.tga",
        config.export_root / "particles" / "fortituderings.tga",
        config.export_root / "particles" / "d_targetblob.tga",
        config.export_root / "particles" / "furball.tga",
        config.export_root / "particles" / "dropletfast.png",
        config.export_root / "particles" / "fortituderings.png",
        config.export_root / "particles" / "d_targetblob.png",
        config.export_root / "particles" / "furball.png",
    ]
    return fingerprint_content(scripts, extra=("policy-v2",), cache=cache)


#: One canonical package per umbrella generator. `build_content.py` authors its whole generator
#: set in a single launch, so one missing product is enough to make the launch gate rerun the
#: union; a sentinel each keeps every generator covered without restating the glyph and audio
#: corpora, whose members the generators derive from their own sources.
POLICY_GENERATOR_OUTPUTS = {
    "make_sky_material.py": ("Materials/M_Sky.uasset",),
    "make_gizmo_material.py": ("Materials/M_Gizmo.uasset", "Materials/M_Gizmo_XRay.uasset"),
    "make_audio_routing.py": ("Audio/SC_Master.uasset",),
    "make_input_glyphs.py": ("Input/Glyphs/Kenney/Keyboard/T_Kenney_keyboard_e.uasset",),
    "make_input_assets.py": (
        "Input/DA_ElysiumInputActions.uasset", "Input/IMC_Player_KBM.uasset",
        "Input/IMC_Player_Gamepad.uasset"),
    "make_dialogue_camera_set.py": ("Camera/DA_ElysiumDialogueCameraSet.uasset",),
    "make_boot_map.py": ("Boot.umap",),
    # `make_missing()`'s two products (docs/project/seam_migration.md R1.6 follow-up): every
    # `vtmb:missing-material:` sentinel slot in the model corpus binds `MI_V2_Missing`, so an
    # umbrella rerun that silently failed to author it must not report complete.
    "make_v2_materials.py": (
        "Materials/V2/MI_V2_Missing.uasset", "Materials/V2/T_V2_MissingChecker.uasset",
        "Materials/V2/M_V2_LitSkinned.uasset", "Materials/V2/M_V2_LitSkinnedTranslucent.uasset"),
}

WORLD_MATERIAL_GENERATOR = "make_world_materials.py"
CHARACTER_MATERIAL_GENERATORS = (
    "make_v2_materials.py",
)


def _world_material_fingerprint(config) -> str:
    scripts = _policy_script_paths(
        config, only_generators=(WORLD_MATERIAL_GENERATOR,), include_fonts=False)
    return fingerprint_content(scripts, extra=("policy-world-materials-v1",))


def _character_material_fingerprint(config) -> str:
    scripts = _policy_script_paths(
        config, only_generators=CHARACTER_MATERIAL_GENERATORS, include_fonts=False)
    return fingerprint_content(scripts, extra=("policy-character-materials-v1",))


def _world_material_task(config, runner) -> Task:
    material_root = config.repo_root / "Content" / "ElysiumGenerated" / "Materials"
    outputs = tuple(material_root / f"{name}.uasset" for name in (
        "M_World_Opaque", "M_World_Masked", "M_World_Translucent", "M_World_Glass",
        "M_Refract", "M_Additive"))
    return Task(
        "unreal:policy:world-materials",
        lambda: unreal.generate_policy_content(
            config, runner, (WORLD_MATERIAL_GENERATOR,), include_auxiliary=False),
        fingerprint=lambda: _world_material_fingerprint(config),
        outputs=outputs,
    )


def _character_material_task(config, runner) -> Task:
    material_root = config.repo_root / "Content" / "ElysiumGenerated" / "Materials" / "V2"
    return Task(
        "unreal:policy:character-materials",
        lambda: unreal.generate_policy_content(
            config, runner, CHARACTER_MATERIAL_GENERATORS, include_auxiliary=False),
        fingerprint=lambda: _character_material_fingerprint(config),
        outputs=tuple(material_root / name for name in (
            "M_V2_LitSkinned.uasset", "M_V2_LitSkinnedTranslucent.uasset", "M_V2_Eyes.uasset",
            "MI_V2_Missing.uasset", "T_V2_MissingChecker.uasset")),
    )


def ensure_world_material_content(config, runner, *, force: bool = False) -> TaskResult:
    """Keep the material masters consumed by map packages current, without global policy work."""
    manifest = Manifest(config.export_root / MANIFEST_FILE)
    task = _world_material_task(config, runner)
    return TaskGraph([task]).run(force=force, manifest=manifest)[task.name]


def ensure_character_material_content(config, runner, *, force: bool = False,
                                      manifest: Manifest | None = None) -> TaskResult:
    """Keep V2 character masters and shared missing-material products current in isolation.

    A caller holding a live ``manifest`` passes it in: two instances over the same file each
    write the whole document, so a receipt this task records through a private instance is
    reverted by the caller's next write."""
    store = manifest if manifest is not None else Manifest(config.export_root / MANIFEST_FILE)
    task = _character_material_task(config, runner)
    return TaskGraph([task]).run(force=force, manifest=store)[task.name]


def _run_launch_task(config, task: Task, *, manifest: Manifest | None = None,
                     force: bool = False) -> TaskResult:
    """Run one fingerprint-gated editor-launch task and report the skip.

    A launch that runs and fails is recorded failed by the graph, so a later run cannot read
    a half-authored mount as done; a skip is printed because an editor boot that never
    happens must be tellable from one that hung.
    """
    store = manifest if manifest is not None else Manifest(config.export_root / MANIFEST_FILE)
    try:
        result = TaskGraph([task]).run(force=force, manifest=store)[task.name]
    except TaskFailure as exc:
        raise ExportBakeFailure(str(exc)) from exc
    if result.status == "skipped":
        print(f"[bake] {task.name}: inputs unchanged; editor launch skipped")
    return result


def _ensure_particle_mirror(config, *, manifest: Manifest, force: bool = False,
                            cache: ContentDigestCache | None = None,
                            covered: bool = False) -> None:
    """Keep the raw particle-sprite mirror the generated rain material imports current.

    A complete profile runs the fingerprint-gated `bundle:particles` task before any policy
    work, so ``covered`` skips the duplicate; a focused policy run carries the same work as
    its own receipt-gated task rather than an unconditional call.
    """
    if covered:
        return
    from elysium_pipeline.exporters import UE_extract_particles
    from elysium_pipeline.formats import install

    def action() -> None:
        UE_extract_particles.main(index=install.build_index(dirs=("particles",)))

    def fingerprint() -> str:
        index = install.build_index(dirs=("particles",), verbose=False)
        return _scoped_source_fingerprint(
            config, {"elysium_pipeline.exporters.UE_extract_particles"},
            index_fingerprint=_source_index_fingerprint(index), cache=cache)

    task = Task(
        "export:particles-mirror",
        action,
        fingerprint=fingerprint,
        outputs=(config.export_root / "particles" / "manifest.json",),
    )
    try:
        TaskGraph([task]).run(force=force, manifest=manifest)
    except TaskFailure as exc:
        raise OfflineExportFailure("particle mirror failed: " + str(exc)) from exc


def ensure_policy_content(config, runner, *, force: bool = False,
                          cache: ContentDigestCache | None = None,
                          particles_covered: bool = False) -> TaskResult:
    """Keep every generated policy package current, in as few editor boots as possible.

    Three receipts, one commandlet: the world-material, character-material and remaining
    generator sets keep their separate fingerprints -- a narrow change must not invalidate
    every MIC shader map -- but every set stale in the same run rides one `build_content.py`
    launch carrying the union, in the generator list's own declared dependency order. The
    Slate font import and the animation-graph rebuild cannot ride it (`build_content.py`
    refuses names outside its registry, and the font import needs a Slate application), so
    they launch separately whenever the umbrella receipt is stale. A launch that fails
    records every stale receipt failed and satisfies none.
    """
    manifest = Manifest(config.export_root / MANIFEST_FILE)
    # The generated rain material imports normalized derivatives of the exact patch-first
    # source sprites, and the policy fingerprint below reads the mirror's outputs.
    _ensure_particle_mirror(config, manifest=manifest, force=force, cache=cache,
                            covered=particles_covered)
    font_root = config.repo_root / "Content" / "ElysiumGenerated" / "UI" / "Fonts"
    generator_names = _policy_generator_names(config)
    focused_generators = {WORLD_MATERIAL_GENERATOR, *CHARACTER_MATERIAL_GENERATORS}
    other_generators = [name for name in generator_names if name not in focused_generators]
    world_task = _world_material_task(config, runner)
    character_task = _character_material_task(config, runner)
    generated_root = config.repo_root / "Content" / "ElysiumGenerated"
    outputs = (
        # Every umbrella generator this task carries, plus the two the umbrella refuses and
        # `generate_auxiliary_policy_content` launches beside it: the Slate font import and the
        # animation-graph rebuild. An undeclared product is a generator the gate cannot see
        # missing, so a stale receipt would report complete having authored nothing.
        *(generated_root / relative
          for name in other_generators
          for relative in POLICY_GENERATOR_OUTPUTS.get(name, ())),
        *(font_root / name for name in unreal.FONT_ASSETS),
        generated_root / "Animation" / "ABP_ElysiumBiped.uasset",
    )
    policy_task = Task(
        "unreal:policy",
        lambda: unreal.generate_policy_content(config, runner, other_generators),
        dependencies=(world_task.name, character_task.name),
        fingerprint=lambda: _policy_fingerprint(
            config, exclude_generators=tuple(focused_generators), cache=cache),
        outputs=outputs,
    )

    batch = (
        (world_task, (WORLD_MATERIAL_GENERATOR,)),
        (character_task, tuple(CHARACTER_MATERIAL_GENERATORS)),
        (policy_task, tuple(other_generators)),
    )
    results: dict[str, TaskResult] = {}
    stale: list[tuple[Task, tuple[str, ...], str | None]] = []
    for task, generators in batch:
        started = time.monotonic()
        fingerprint = task.fingerprint() if task.fingerprint else None
        if not force and manifest.can_skip(task, fingerprint):
            results[task.name] = TaskResult(
                task.name, "skipped", time.monotonic() - started,
                fingerprint=fingerprint,
                outputs=[str(path) for path in task.outputs],
                dependencies=list(task.dependencies))
            manifest.record(results[task.name], flush=False)
        else:
            stale.append((task, generators, fingerprint))
    if cache is not None:
        cache.write()
    if not stale:
        manifest.write()
        print("[policy] content current; editor launches skipped")
        return results["unreal:policy"]

    union = [name for name in generator_names
             if any(name in generators for _, generators, _ in stale)]
    for _, generators, _ in stale:
        union.extend(name for name in generators if name not in union)
    policy_stale = any(task.name == policy_task.name for task, _, _ in stale)
    started = time.monotonic()
    try:
        unreal.generate_policy_content(config, runner, union, include_auxiliary=False)
        if policy_stale:
            unreal.generate_auxiliary_policy_content(config, runner)
        missing = [str(path) for task, _, _ in stale for path in task.outputs
                   if not path.exists()]
        if missing:
            raise ExportBakeFailure("policy content did not produce: " + ", ".join(missing))
    except BaseException as exc:
        elapsed = time.monotonic() - started
        for task, _, fingerprint in stale:
            manifest.record(TaskResult(
                task.name, "failed", elapsed, error=f"{type(exc).__name__}: {exc}",
                fingerprint=fingerprint,
                outputs=[str(path) for path in task.outputs],
                dependencies=list(task.dependencies)), flush=False)
        manifest.write()
        raise
    elapsed = time.monotonic() - started
    for task, _, fingerprint in stale:
        results[task.name] = TaskResult(
            task.name, "ok", elapsed, fingerprint=fingerprint,
            outputs=[str(path) for path in task.outputs],
            dependencies=list(task.dependencies))
        manifest.record(results[task.name], flush=False)
    manifest.write()
    return results["unreal:policy"]


def ensure_corpus_export(config, *, force: bool = False, index: dict | None = None,
                         jobs: int | None = None,
                         cache: ContentDigestCache | None = None) -> TaskResult:
    """Decode the shared static corpus, once, before anything that reads it.

    Every map export and every bake resolves its textures, materials and static models through
    `shared/manifest.json`, so this runs first and only once. Its fingerprint is the install's own
    map inventory plus the decoder code: a changed BSP can add a source identity the corpus does
    not yet hold, and a changed decoder changes what every identity decodes to. The decoder half
    is the same content fingerprint every map and bundle task carries, so a module the decode
    reaches cannot be left out of it by omission from a hand-kept list.
    """
    from elysium_pipeline.exporters import UE_extract_corpus
    from elysium_pipeline.formats import install

    _require_export_config(config)
    manifest = Manifest(config.export_root / MANIFEST_FILE)
    maps = [Path(install.map_path(name)) for name in install.all_map_names()]
    # The corpus decode also writes the offline enhancement track's reference set, which is the
    # one input outside the decoder closure that no other export product reads.
    enhancement = config.repo_root / "pipeline" / "src" / "elysium_pipeline" / "enhancement"
    task = Task(
        "export:corpus",
        lambda: UE_extract_corpus.main(
            index=index, force=force, jobs=jobs or default_jobs()),
        fingerprint=lambda: fingerprint_paths(
            maps,
            extra=(
                "corpus-v1",
                _scoped_source_fingerprint(
                    config,
                    _decoder_closures(config).function_entries(
                        "elysium_pipeline.export_manager", "ensure_corpus_export"),
                    cache=cache),
                fingerprint_paths([enhancement]),
            ),
        ),
        outputs=(
            shared_corpus.manifest_path(config.export_root),
            shared_corpus.materials_path(config.export_root),
        ),
    )
    return TaskGraph([task]).run(force=force, manifest=manifest)["export:corpus"]


#: The export bundle name. Distinct from `shared_corpus.SCOPE`, which is the BAKE scope: the
#: bundle is what a user asks for, the scope is what the bake receipts are keyed on.
CORPUS_BUNDLE = "corpus"


def _baked_corpus_dir(config) -> Path:
    return config.repo_root / "Plugins" / "ElysiumBaked" / "Content" / "Shared" / "Meshes"


def export_corpus_unit(
    config,
    runner,
    *,
    models: Sequence[str] | None = None,
    materials: Sequence[str] | None = None,
    textures: Sequence[str] | None = None,
    force: bool = False,
) -> dict:
    """Re-decode and re-bake exactly one corpus unit -- a model, a material, or a texture.

    This is what one asset per source buys. The unit is decoded again from the install, and the
    corpus bake rebuilds only the assets whose recipe changed, because every other asset's receipt
    still matches. **No map is exported and no `.umap` is touched**: every map that draws the unit
    already points at the one package, so they all pick the change up with nothing to re-bake.

    A texture is addressed through the materials that draw it, because a texture is decoded as a
    material's channel rather than on its own.
    """
    from elysium_pipeline.exporters import UE_extract_corpus
    from elysium_pipeline.formats import install

    _require_export_config(config)
    adopt_export_root(config.export_root, config.work_root)
    index = install.build_index()

    selected = [shared_corpus.material_key(key) for key in (materials or ())]
    if textures:
        manifest_file = shared_corpus.manifest_path(config.export_root)
        if not manifest_file.is_file():
            raise ValueError(
                f"no shared corpus at {manifest_file}; run: uv run elysium export bundle corpus")
        with manifest_file.open(encoding="utf-8") as handle:
            manifest = shared_corpus.check_manifest(json.load(handle))
        wanted = {shared_corpus.texture_key(key) for key in textures}
        unknown = wanted - set(manifest["textures"])
        if unknown:
            raise ValueError("texture(s) are not in the shared corpus: " + ", ".join(sorted(unknown)))
        # Every material that draws one of these textures has to be resolved again: the decode is
        # a channel of a material, and which channel it is decides how it is written. A texture is
        # matched by every file it can decode to, not by its albedo alone -- the same install
        # texture reaches a material as a normal map, an env mask or a second blend layer.
        files = {shared_corpus.texture_rel(key, suffix)
                 for key in wanted for suffix in shared_corpus.ROLES}
        drawn = []
        for key, record in _corpus_materials(config).items():
            if record.get("albedo_key", "") in wanted:
                drawn.append(key)
                continue
            if any(record.get(channel) in files for channel in shared_corpus.CHANNEL_FIELDS):
                drawn.append(key)
        selected.extend(drawn)
        if not drawn:
            raise ValueError(
                "no corpus material draws " + ", ".join(sorted(wanted)) + "; nothing to re-decode")

    # A unit run decodes only its unit. `None` means "everything", so a material-only run states
    # an empty model list rather than leaving it open and re-decoding the whole corpus.
    UE_extract_corpus.main(
        index=index,
        models=list(models) if models is not None else ([] if selected else None),
        materials=sorted(dict.fromkeys(selected)) if selected else ([] if models else None),
    )
    ensure_corpus_bake(config, runner, force=force)
    return {"models": list(models or ()), "materials": sorted(dict.fromkeys(selected))}


def _corpus_materials(config) -> dict:
    path = shared_corpus.materials_path(config.export_root)
    if not path.is_file():
        raise ValueError(f"no shared corpus at {path}; run: uv run elysium export bundle corpus")
    with path.open(encoding="utf-8") as handle:
        return shared_corpus.check_materials(json.load(handle))["materials"]


def _corpus_bake_fingerprint(config, *, cache: ContentDigestCache | None = None) -> str:
    """The corpus launch's whole recipe: the decoded `shared/` export products it imports
    (manifest, materials, textures and prop meshes), the driving scripts, and the
    world-material policy the instanced MICs compile against."""
    return fingerprint_content(
        [config.export_root / "shared"],
        extra=(
            "corpus-bake-v1",
            _bake_script_fingerprint(config, ("bake_map.py",), cache=cache),
            _world_material_fingerprint(config),
        ),
        cache=cache,
    )


def ensure_corpus_bake(config, runner, *, force: bool = False,
                       cache: ContentDigestCache | None = None,
                       manifest: Manifest | None = None) -> None:
    """Launch the corpus bake only when its recorded recipe receipt is stale.

    The receipt's fingerprint covers the decoded `shared/` corpus, the bake scripts, and the
    world-material policy; `--force` defeats it, and a launch that fails records no success.
    Inside a launch the editor still decides per asset off each recipe stamp, so a stale
    receipt costs one boot of mostly "reused" decisions, and the stamps remain the second
    line of defense behind this gate.
    """
    if cache is None:
        cache = ContentDigestCache(config.export_root / DIGEST_CACHE_FILE)

    def fingerprint() -> str:
        value = _corpus_bake_fingerprint(config, cache=cache)
        # Persisted before the launch so the commandlet's own digest reads start warm.
        cache.write()
        return value

    task = Task(
        "unreal:bake:corpus",
        lambda: unreal.bake_corpus(config, runner, force=force),
        fingerprint=fingerprint,
        outputs=(config.repo_root / "Plugins" / "ElysiumBaked" / "Content" / "Shared",),
    )
    _run_launch_task(config, task, manifest=manifest, force=force)


def _baked_package(config, map_name: str) -> Path:
    from elysium_pipeline.asset_paths import map_package

    relative = map_package(map_name).removeprefix("/ElysiumBaked/")
    return config.repo_root / "Plugins" / "ElysiumBaked" / "Content" / relative / (relative.rsplit("/", 1)[-1] + ".umap")


def bake_and_verify(
    config,
    runner,
    maps: Sequence[str],
    *,
    force: bool = False,
    particles: bool = False,
    verify: bool = False,
) -> None:
    """Bake the named maps, and read the mount back only when explicitly asked to.

    The bake's exit code is the acceptance signal, and each asset's recipe stamp is the record
    of what was authored -- a crash loses only unsaved packages, and the next run resumes off
    the stamps alone. `verify` opts into the deep read-back, which `uv run elysium verify maps`
    also runs on its own whenever a mount has to be re-checked without authoring it again.
    """
    names = list(dict.fromkeys(maps))
    if not names:
        return
    unreal.bake_maps(config, runner, names, force=force, particles=particles,
                     batch_size=unreal.MAP_BAKE_BATCH)
    missing = [
        str(_baked_package(config, name))
        for name in names
        if not _baked_package(config, name).is_file()
    ]
    if missing:
        raise ExportBakeFailure("bake did not produce: " + ", ".join(missing))
    if verify:
        unreal.verify_bakes(config, runner, names)
    else:
        print("[bake] trusting the bake exit; pass --verify or run `elysium verify maps` "
              "to read the mount back")


def _maps_bake_fingerprint(config, maps: Sequence[str], *,
                           particles: bool = False,
                           cache: ContentDigestCache | None = None) -> str:
    """One recipe for the whole profile bake: every selected map's exported directory
    (geometry, entities and sidecars), the whole shared corpus (its tables, and the texture
    and mesh bytes the map-scoped material and level recipes hash), the particle sprites
    each map imports, the driving scripts, and the world-material policy."""
    inputs = [config.export_root / name for name in maps]
    inputs.append(config.export_root / "shared")
    # Native references embedded in a map change when the merged R8 catalogues change.
    inputs.extend(config.repo_root / "Plugins/ElysiumBaked/Content/Models/_Corpus" / name
                  for name in ("DA_PlacedModels.uasset", "DA_PropSkins.uasset"))
    if particles:
        # The sprites only reach a package when the Niagara pass runs, so they are an input
        # to the recipe only then; otherwise a re-decoded sprite would relaunch a bake that
        # cannot consume it.
        inputs.append(config.export_root / "particles")
    return fingerprint_content(
        inputs,
        extra=(
            "maps-bake-r8-catalogues",
            "particles-on" if particles else "particles-off",
            _bake_script_fingerprint(config, ("bake_map.py",), cache=cache),
            _world_material_fingerprint(config),
            *(str(_baked_package(config, name)) for name in maps),
            *maps,
        ),
        cache=cache,
    )


def _bake_profile_maps(config, runner, maps: Sequence[str], *, force: bool = False,
                       particles: bool = False, verify: bool = False,
                       cache: ContentDigestCache | None = None) -> None:
    """Bake the profile's maps behind one receipt; a stale receipt costs one launch whose
    per-map reuse is still the commandlet's own recipe-stamp decision. The declared outputs
    are every baked `.umap`, so a nuked or partial mount defeats the skip."""
    names = list(dict.fromkeys(maps))
    if not names:
        return

    def fingerprint() -> str:
        value = _maps_bake_fingerprint(config, names, particles=particles, cache=cache)
        if cache is not None:
            cache.write()
        return value

    task = Task(
        "unreal:bake:maps",
        lambda: unreal.bake_maps(config, runner, names, force=force, particles=particles,
                                 batch_size=unreal.MAP_BAKE_BATCH),
        fingerprint=fingerprint,
        outputs=tuple(_baked_package(config, name) for name in names),
    )
    result = _run_launch_task(config, task, force=force)
    if verify:
        unreal.verify_bakes(config, runner, names)
    elif result.status == "ok":
        print("[bake] trusting the bake exit; pass --verify or run `elysium verify maps` "
              "to read the mount back")


def export_profile(
    config,
    runner,
    profile: str,
    *,
    clean: bool = False,
    force: bool = False,
    jobs: int | None = None,
    particles: bool = False,
    verify: bool = False,
) -> list[str]:
    from elysium_pipeline.exporters.export_all import bundles_for_profile

    native_model_pipeline.require_map_prerequisites(config)
    jobs = jobs or default_jobs()
    bundles = bundles_for_profile(profile)
    maps, _results = run_offline_profile(
        config,
        profile,
        clean=clean,
        force=force,
        jobs=jobs,
    )
    # One digest cache for every launch-gate fingerprint below, constructed after the offline
    # phase (a --clean run wipes the store with the rest of the export root). Each gate
    # persists it before its editor boots, so the commandlet reads the same warm entries.
    cache = ContentDigestCache(config.export_root / DIGEST_CACHE_FILE)
    try:
        # The masters the corpus and every map instance from, then the shared assets a map bake
        # resolves. A map baked before the corpus binds nothing for every texture, material and
        # prop mesh the corpus owns, and its receipts would freeze that.
        ensure_policy_content(config, runner, force=force or clean, cache=cache,
                              particles_covered="particles" in bundles)
    except Exception as exc:
        raise ExportBakeFailure(str(exc)) from exc
    ensure_corpus_bake(config, runner, force=force or clean, cache=cache)
    # The shared R8 import owns every skeletal role before maps consume native references.
    # The R8 import lanes gate on their own recipes; a profile force or clean re-bakes what the
    # profile owns and leaves a lane's re-authoring to that lane's own `--force`.
    native_model_pipeline.import_map_dependencies(config, runner)
    _bake_profile_maps(config, runner, maps, force=force or clean, particles=particles,
                       verify=verify, cache=cache)
    # Only the domains this profile actually covers.  `grid` and `all` both run every bundle, so
    # this clears the corpus either way; a profile that dropped one would leave that one gated.
    mark_complete(config.export_root, ("maps", "policy", *bundles))
    cache.write()
    return maps


def export_targeted_maps(
    config,
    runner,
    maps: Sequence[str],
    *,
    force: bool = False,
    intermediate_only: bool = False,
    particles: bool = False,
    verify: bool = False,
) -> list[str]:
    _require_export_config(config)
    if not intermediate_only:
        native_model_pipeline.require_map_prerequisites(config)
    adopt_export_root(config.export_root, config.work_root)
    from elysium_pipeline.exporters import export_all
    from elysium_pipeline.formats import install

    names = list(dict.fromkeys(maps))
    # One named map is one worker; naming several is a batch, and each map export is an
    # independent decode into its own directory.
    jobs = min(len(names), default_jobs())
    index = install.build_index()
    config.export_root.mkdir(parents=True, exist_ok=True)
    source_fingerprint = _scoped_source_fingerprint(
        config,
        _decoder_closures(config).function_entries(
            "elysium_pipeline.exporters.export_all", "export_maps"),
        index_fingerprint=_source_index_fingerprint(index))
    manifest = Manifest(config.export_root / MANIFEST_FILE)
    manifest.set_context(
        profile="targeted-map",
        source_fingerprint=source_fingerprint,
        dependency_fingerprint=fingerprint_paths(
            [config.repo_root / "dev" / "dependencies.lock.json"]
        ),
        configuration={
            "game_root": str(config.game_root),
            "export_root": str(config.export_root),
            "jobs": jobs,
        },
        maps=list(names),
        bundles=["audio"],
    )
    TaskGraph(_map_tasks(config, names, index, source_fingerprint)).run(
        jobs=jobs,
        force=force,
        manifest=manifest,
    )
    _raise_results(
        export_all.export_bundles(
            ["audio"],
            maps=names,
            force=force,
            index=index,
            continue_on_error=False,
        )
    )
    if not intermediate_only:
        try:
            # A targeted map owns only the world-material masters its map package consumes.
            # Rain, fonts, UI and other global policy content remain explicit bundle/profile work;
            # waking them here turns every map iteration into an unrelated whole-project rebuild.
            ensure_world_material_content(config, runner)
        except Exception as exc:
            raise ExportBakeFailure(str(exc)) from exc
        # One shared import includes NPCs, wield models, placed skeletal models and their
        # global catalogue dependencies. Do not split this into competing stage manifests.
        # `--force` here forces the map bake only: every import lane decides off its own
        # recipes, and forcing a lane is that lane's own `--force`.
        native_model_pipeline.import_map_dependencies(config, runner)
        bake_and_verify(config, runner, names, force=force, particles=particles,
                        verify=verify)
    return names


def export_bundle(config, runner, bundle: str, *, force: bool = False) -> None:
    _require_export_config(config)
    adopt_export_root(config.export_root, config.work_root)
    if bundle == "policy":
        ensure_policy_content(config, runner, force=force)
        mark_complete(config.export_root, ("policy",))
        return
    from elysium_pipeline.exporters import export_all
    from elysium_pipeline.formats import install

    index = install.build_index()
    _raise_results(
        export_all.export_bundles(
            [bundle],
            force=force,
            index=index,
            continue_on_error=False,
        )
    )
    if bundle == CORPUS_BUNDLE:
        # The decoded assets are only half of it: a map resolves against the baked packages, so
        # the focused command carries the scope's bake the way a focused map export does.
        try:
            ensure_corpus_bake(config, runner, force=force)
        except Exception as exc:
            raise ExportBakeFailure(str(exc)) from exc
    # This bundle's domain is now whole, whatever the rest of the corpus looks like.
    mark_complete(config.export_root, (bundle,))


def export_model(
    config,
    runner,
    model: str,
    *,
    animation: str | None = None,
    integrate: bool = False,
) -> Path:
    """One model, two modes. Without `--integrate`: a single-clip inspection glTF under
    `work/scratch/models/<stem>` decoded straight from the install (`formats/mdl_gltf.py`), a
    diagnostic that no bake or runtime reads and not the V2 unit. With `--integrate`: the R8
    native lane for that one body (expression tables, characters, catalogues) off the published
    V2 unit."""
    _require_export_config(config)
    from elysium_pipeline.formats import install, mdl_gltf

    index = install.build_index()
    normalized = model.replace("\\", "/")
    if not normalized.lower().endswith(".mdl"):
        normalized += ".mdl"
    if not integrate:
        destination = config.work_root / "scratch" / "models" / Path(normalized).stem
        destination.mkdir(parents=True, exist_ok=True)
        mdl_gltf.export(normalized, animation, destination, index=index)
        return destination

    from elysium_pipeline.formats.model_glb.model import asset_id

    identity = asset_id(normalized)
    native_model_pipeline.import_map_dependencies(config, runner, bodies=[identity])
    return config.work_root / "import" / "characters"

def _glb_source_bytes(summary: Mapping) -> str:
    """What a seam accounted for, in the terms its own validation summary reports.

    Every seam publishes a byte ledger, but not every summary lifts both totals out of it: some
    report the accounted and source counts, some only the coverage percent they computed from
    them, and a seam whose unit is cut from several members reports one percent per member. The
    progress line prints whichever the seam gives it rather than demanding one shape.
    """
    accounted, source = summary.get("accountedBytes"), summary.get("sourceBytes")
    if accounted is not None and source is not None:
        return f" ({accounted}/{source} bytes)"
    percent = summary.get("byteCoveragePercent")
    if isinstance(percent, (int, float)):
        return f" ({float(percent):.1f}% of source bytes)"
    if isinstance(percent, (list, tuple)) and percent:
        # One percent per member: the weakest is what a reader needs to see.
        return f" ({min(float(value) for value in percent):.1f}% of source bytes)"
    return ""


def _print_glb_row(label: str, ordinal: int, total: int, row: dict) -> None:
    item = row["item"]
    if row["error"]:
        print(f"! {label} [{ordinal}/{total}] failed for {item}: {row['error']}", flush=True)
        return
    summary = row["summary"] or {}
    print(
        f"{label} [{ordinal}/{total}]: {summary.get('asset', item)} -> "
        f"{row['destination']}{_glb_source_bytes(summary)}",
        flush=True,
    )
    for warning in row.get("warnings", ()):
        print(f"  warning: {item}: {warning}", flush=True)


def _finalize_glb_corpus(
    label: str, noun: str, rows: list[dict], output_root: Path
) -> list[Path]:
    destinations = [Path(row["destination"]) for row in rows if not row["error"]]
    failures = [(row["item"], row["error"]) for row in rows if row["error"]]
    total = len(rows)
    if failures:
        details = "; ".join(f"{item}: {error}" for item, error in failures[:12])
        if len(failures) > 12:
            details += f"; ... {len(failures) - 12} more"
        raise OfflineExportFailure(
            f"{label} corpus exported {len(destinations)}/{total}; "
            f"{len(failures)} failed: {details}"
        )
    # A unit that published with a warning is still a unit the corpus is weaker for; a run whose
    # only report is its exit code would hide that.
    warned = sorted(row["item"] for row in rows if row.get("warnings"))
    print(f"{label} corpus complete: {len(destinations)} {noun} -> {output_root}", flush=True)
    if warned:
        print(f"! {label} corpus: {len(warned)} {noun} published with warnings", flush=True)
        for item in warned[:12]:
            print(f"    {item}", flush=True)
        if len(warned) > 12:
            print(f"    ... {len(warned) - 12} more", flush=True)
    return destinations


def _print_pakfile_failures(label: str, index: dict) -> None:
    """Warn about every map whose PAKFILE this run's shared index reader could not parse.

    `pakfile_index.pakfile_members` skips an unparseable BSP silently by design (a map's own
    seam is where that failure belongs), so a plural export is the seam that has to print it --
    otherwise a whole map's PAKFILE-origin units go missing from the corpus with nothing in the
    run's own output saying why.
    """

    from elysium_pipeline.formats.map_glb.pakfile_index import pakfile_failures

    failures = pakfile_failures(index)
    if not failures:
        return
    print(
        f"! {label}: {len(failures)} map(s)' PAKFILE could not be parsed and contributed no "
        "PAKFILE-origin units:",
        flush=True,
    )
    for map_name in sorted(failures):
        print(f"    {map_name}: {failures[map_name]}", flush=True)


def _run_glb_pool(label: str, worker, items: list[str], output_root: Path, jobs: int) -> list[dict]:
    print(f"  {label}: {len(items)} unit(s) across {jobs} worker(s)", flush=True)
    rows = []
    with ProcessPoolExecutor(max_workers=jobs) as pool:
        futures = [pool.submit(worker, item, str(output_root)) for item in items]
        for ordinal, future in enumerate(as_completed(futures), 1):
            row = future.result()
            rows.append(row)
            _print_glb_row(label, ordinal, len(items), row)
    return rows


def export_texture_glb(config, runner, texture: str) -> Path:
    """Write and validate one isolated Texture GLB product."""
    del runner
    _require_export_v2_config(config)
    from elysium_pipeline.exporters import texture_glb
    from elysium_pipeline.formats import install
    from elysium_pipeline.validation import texture_glb as texture_glb_validation

    index = install.build_index()
    output_root = _export_v2_root(config, "textures")
    try:
        destination = texture_glb.export(index, texture, output_root)
        summary = texture_glb_validation.validate(destination)
    except Exception as exc:
        raise OfflineExportFailure(f"texture GLB export failed for {texture}: {exc}") from exc
    print(
        f"texture GLB: {summary['asset']} -> {destination} "
        f"({summary['width']}x{summary['height']}, {summary['mips']} mips, "
        f"{summary['accountedBytes']}/{summary['sourceBytes']} source bytes)"
    )
    for warning in texture_glb_validation.warnings_for(summary):
        print(f"  warning: {texture}: {warning}")
    refresh_corpus_index_row(config, destination)
    return destination


def _texture_glb_sources(index: dict) -> list[str]:
    """Every texture identity, from the seam's own selectors: the `materials/**.tth` corpus plus
    the PAKFILE probes (`source_keys`) and the particle sprites (`sprite_source_keys`, R7.3)."""
    from elysium_pipeline.formats import texture_glb

    return texture_glb.source_keys(index) + texture_glb.sprite_source_keys(index)


def _texture_glb_one(index, texture: str, output_root: Path) -> dict:
    from elysium_pipeline.exporters import texture_glb
    from elysium_pipeline.validation import texture_glb as texture_glb_validation

    try:
        destination = texture_glb.export(index, texture, output_root)
        summary = texture_glb_validation.validate(destination)
        return {
            "item": texture,
            "destination": str(destination),
            "summary": summary,
            "warnings": texture_glb_validation.warnings_for(summary),
            "error": "",
        }
    except Exception as exc:
        return {
            "item": texture,
            "destination": "",
            "summary": None,
            "warnings": [],
            "error": f"{type(exc).__name__}: {exc}",
        }


def export_all_texture_glbs(config, runner, *, jobs=None) -> list[Path]:
    """Write every selected TTH identity; any incomplete unit fails the corpus."""
    del runner
    _require_export_v2_config(config)
    from elysium_pipeline.formats import install

    index = install.build_index()
    textures = _texture_glb_sources(index)
    _print_pakfile_failures("texture GLB", index)
    if not textures:
        raise OfflineExportFailure("texture GLB corpus has no selected TTH members")
    output_root = _export_v2_root(config, "textures")
    jobs = max(1, default_jobs() if jobs is None else int(jobs))
    jobs = min(jobs, len(textures))
    if jobs <= 1:
        rows = []
        for ordinal, texture in enumerate(textures, 1):
            row = _texture_glb_one(index, texture, output_root)
            rows.append(row)
            _print_glb_row("texture GLB", ordinal, len(textures), row)
    else:
        rows = _run_glb_pool(
            "texture GLB",
            workers.texture_glb_worker,
            textures,
            output_root,
            jobs,
        )
    return _finalize_glb_corpus("texture GLB", "textures", rows, output_root)


def export_material_glb(config, runner, material: str) -> Path:
    """Write and validate one isolated Material GLB product."""
    del runner
    _require_export_v2_config(config)
    from elysium_pipeline.exporters import material_glb
    from elysium_pipeline.formats import install
    from elysium_pipeline.validation import material_glb as material_glb_validation

    index = install.build_index()
    output_root = _export_v2_root(config, "materials")
    try:
        destination = material_glb.export(index, material, output_root)
        summary = material_glb_validation.validate(destination)
    except Exception as exc:
        raise OfflineExportFailure(f"material GLB export failed for {material}: {exc}") from exc
    print(
        f"material GLB: {summary['asset']} -> {destination} "
        f"({summary['shader']}, {summary['parameters']} parameters, "
        f"{summary['proxies']} proxies, {summary['dependencies']} dependencies, "
        f"{summary['accountedBytes']}/{summary['sourceBytes']} source bytes)"
    )
    for warning in material_glb_validation.warnings_for(summary):
        print(f"  warning: {material}: {warning}")
    refresh_corpus_index_row(config, destination)
    return destination


def _material_glb_sources(index: dict) -> list[str]:
    """Every VMT identity the engine can address, from the seam's own selector."""
    from elysium_pipeline.formats import material_glb

    return material_glb.source_keys(index)


def _material_glb_one(index, material: str, output_root: Path) -> dict:
    from elysium_pipeline.exporters import material_glb
    from elysium_pipeline.validation import material_glb as material_glb_validation

    try:
        destination = material_glb.export(index, material, output_root)
        summary = material_glb_validation.validate(destination)
        return {
            "item": material,
            "destination": str(destination),
            "summary": summary,
            "warnings": material_glb_validation.warnings_for(summary),
            "error": "",
        }
    except Exception as exc:
        return {
            "item": material,
            "destination": "",
            "summary": None,
            "warnings": [],
            "error": f"{type(exc).__name__}: {exc}",
        }


def export_all_material_glbs(config, runner, *, jobs=None) -> list[Path]:
    """Write every addressable VMT identity; any incomplete unit fails the corpus."""
    del runner
    _require_export_v2_config(config)
    from elysium_pipeline.formats import install

    index = install.build_index()
    materials = _material_glb_sources(index)
    _print_pakfile_failures("material GLB", index)
    if not materials:
        raise OfflineExportFailure("material GLB corpus has no selected VMT members")
    output_root = _export_v2_root(config, "materials")
    jobs = max(1, default_jobs() if jobs is None else int(jobs))
    jobs = min(jobs, len(materials))
    if jobs <= 1:
        rows = []
        for ordinal, material in enumerate(materials, 1):
            row = _material_glb_one(index, material, output_root)
            rows.append(row)
            _print_glb_row("material GLB", ordinal, len(materials), row)
    else:
        rows = _run_glb_pool(
            "material GLB",
            workers.material_glb_worker,
            materials,
            output_root,
            jobs,
        )
    return _finalize_glb_corpus("material GLB", "materials", rows, output_root)


def export_surface_property_glb(config, runner, name: str) -> Path:
    """Write and validate one isolated Surface-property GLB product."""
    del runner
    _require_export_v2_config(config)
    from elysium_pipeline.exporters import surface_property_glb
    from elysium_pipeline.formats import install
    from elysium_pipeline.validation import surface_property_glb as surface_glb_validation

    index = install.build_index()
    output_root = _export_v2_root(config, "surface-properties")
    try:
        destination = surface_property_glb.export(index, name, output_root)
        summary = surface_glb_validation.validate(destination)
    except Exception as exc:
        raise OfflineExportFailure(
            f"surface-property GLB export failed for {name}: {exc}"
        ) from exc
    print(
        f"surface-property GLB: {summary['asset']} -> {destination} "
        f"({summary['parameters']} parameters, {summary['footsteps']} footsteps, "
        f"{summary['impacts']} impacts, {summary['dependencies']} dependencies, "
        f"{summary['accountedBytes']}/{summary['sourceBytes']} source bytes)"
    )
    for warning in surface_glb_validation.warnings_for(summary):
        print(f"  warning: {name}: {warning}")
    refresh_corpus_index_row(config, destination)
    return destination


def _surface_property_glb_one(index, name: str, output_root: Path, table, scripts) -> dict:
    from elysium_pipeline.exporters import surface_property_glb
    from elysium_pipeline.validation import surface_property_glb as surface_glb_validation

    try:
        destination = surface_property_glb.export(
            index, name, output_root, table=table, sound_scripts=scripts
        )
        summary = surface_glb_validation.validate(destination)
        return {
            "item": name,
            "destination": str(destination),
            "summary": summary,
            "warnings": surface_glb_validation.warnings_for(summary),
            "error": "",
        }
    except Exception as exc:
        return {
            "item": name,
            "destination": "",
            "summary": None,
            "warnings": [],
            "error": f"{type(exc).__name__}: {exc}",
        }


def export_all_surface_property_glbs(config, runner, *, jobs=None) -> list[Path]:
    """Write every named table entry; any incomplete unit fails the corpus."""
    del runner
    _require_export_v2_config(config)
    from elysium_pipeline.formats import install
    from elysium_pipeline.formats import surface_property_glb as surface_property_format

    index = install.build_index()
    try:
        table = surface_property_format.load_table(index)
        scripts = surface_property_format.load_sound_script_names(index)
    except surface_property_format.SurfacePropertySourceError as exc:
        raise OfflineExportFailure(f"surface-property GLB corpus has no table: {exc}") from exc
    names = list(table.names)
    output_root = _export_v2_root(config, "surface-properties")
    # The corpus is 63 entries cut from one 24 KB table, so a worker pool would spend more on
    # rebuilding the install index per process than the whole decode costs. It runs here.
    rows = []
    for ordinal, name in enumerate(names, 1):
        row = _surface_property_glb_one(index, name, output_root, table, scripts)
        rows.append(row)
        _print_glb_row("surface-property GLB", ordinal, len(names), row)
    return _finalize_glb_corpus("surface-property GLB", "surfaces", rows, output_root)


# --- the uniform isolated-GLB seam ------------------------------------------------------------
#
# `seam_map_unit_contract.md` gives every unit kind one shape: one identity is one file, the
# singular command writes one unit and the plural command writes every key the kind's own
# `source_keys(index)` resolves, and the seam's own validator reads each published file back.
# Only four facts differ per kind -- which module owns it, which family directory it publishes
# below, which exported function writes one unit and which one lists the keys -- so those are
# data here and the workflow is written once.


@dataclass(frozen=True, slots=True)
class GlbUnitSeam:
    """One `export_v2` unit kind: where its code lives and where it publishes."""

    #: The label every progress line carries, e.g. `vdata GLB`.
    label: str
    #: What a corpus line counts, e.g. `units`.
    noun: str
    #: The `exporters.<module>` / `validation.<module>` base name, e.g. `vdata_glb`.
    module: str
    #: The export_v2 subdirectory to publish below. Empty where the exporter's own
    #: `output_relative_path` already names the family directory.
    family: str = ""
    #: The exporter attribute that writes one unit.
    export_attr: str = "export"
    #: The exporter attribute that lists every key of this kind.
    keys_attr: str = "source_keys"
    #: True where the kind is one single unit the install either has or has not, so its writer
    #: takes no key: the font registry and the game-sound manifest.
    keyless: bool = False


def _glb_exporter(seam: GlbUnitSeam):
    """The seam's writer module, imported after `config.apply_environment()` has run."""
    return importlib.import_module("elysium_pipeline.exporters." + seam.module)


def _glb_validation(seam: GlbUnitSeam):
    """The seam's read-back validator, which shares no state with the writer."""
    return importlib.import_module("elysium_pipeline.validation." + seam.module)


def _glb_seam_root(config, seam: GlbUnitSeam) -> Path:
    return config.export_v2_root if not seam.family else _export_v2_root(config, seam.family)


def _glb_seam_keys(seam: GlbUnitSeam, index: dict) -> list[str]:
    return list(getattr(_glb_exporter(seam), seam.keys_attr)(index))


def _write_glb_unit(seam: GlbUnitSeam, index: dict, key: str, output_root: Path) -> Path:
    write = getattr(_glb_exporter(seam), seam.export_attr)
    if seam.keyless:
        return write(index, output_root)
    return write(index, key, output_root)


def _glb_unit_row(seam: GlbUnitSeam, index: dict, key: str, output_root: Path) -> dict:
    """Write and validate one unit. A failure is a row the corpus collects, not an exception."""
    validation = _glb_validation(seam)
    try:
        destination = _write_glb_unit(seam, index, key, output_root)
        summary = validation.validate(destination)
        return {
            "item": key,
            "destination": str(destination),
            "summary": summary,
            "warnings": validation.warnings_for(summary),
            "error": "",
        }
    except Exception as exc:
        return {
            "item": key,
            "destination": "",
            "summary": None,
            "warnings": [],
            "error": f"{type(exc).__name__}: {exc}",
        }


def refresh_corpus_index_row(config, destination: Path) -> None:
    """Bring the corpus index's `units[]` row for one just-published unit up to date.

    The corpus index is "rewritten by any single-unit command so that its `units[]` row for that
    unit is current" (`seam_map_corpus_index.md`, "Unit identity"), so every singular export ends
    here. Before the first `export-all` there is no index and this does nothing; a refresh that
    fails is reported and does not fail the export that already succeeded, because the unit on
    disk is what the command was asked for.
    """
    if config.export_v2_root is None:
        return
    from elysium_pipeline.exporters import corpus_index_glb

    try:
        corpus_index_glb.refresh_unit(config.export_v2_root, destination)
    except Exception as exc:                              # noqa: BLE001 - reported, not fatal
        print(f"  warning: corpus index not refreshed for {destination}: {exc}")


def export_glb_unit(config, runner, seam: GlbUnitSeam, key: str) -> Path:
    """Write and validate one unit of `seam`.

    The argument tolerates the kind's root prefix and its source extension because each seam's
    own key normalisation folds them; this passes the argument through to it unchanged.
    """
    del runner  # These exporters are pure offline Python; kept for the public workflow signature.
    _require_export_v2_config(config)
    from elysium_pipeline.formats import install

    index = install.build_index()
    output_root = _glb_seam_root(config, seam)
    validation = _glb_validation(seam)
    try:
        destination = _write_glb_unit(seam, index, key, output_root)
        summary = validation.validate(destination)
    except Exception as exc:
        raise OfflineExportFailure(f"{seam.label} export failed for {key}: {exc}") from exc
    print(
        f"{seam.label}: {summary.get('asset', key)} -> {destination}"
        f"{_glb_source_bytes(summary)}"
    )
    for warning in validation.warnings_for(summary):
        print(f"  warning: {key}: {warning}")
    refresh_corpus_index_row(config, destination)
    return destination


def export_all_glb_units(
    config, runner, seam: GlbUnitSeam, *, jobs=None, worker=None, index=None
) -> list[Path]:
    """Write every key of `seam`; any incomplete unit fails the corpus.

    `index` lets a seam that publishes several kinds out of one BSP or one table build the
    install index once for all of them.
    """
    del runner
    _require_export_v2_config(config)
    from elysium_pipeline.formats import install

    if index is None:
        index = install.build_index()
    try:
        items = _glb_seam_keys(seam, index)
    except Exception as exc:
        raise OfflineExportFailure(f"{seam.label} corpus has no source: {exc}") from exc
    if not items:
        raise OfflineExportFailure(f"{seam.label} corpus has no {seam.noun}")
    output_root = _glb_seam_root(config, seam)
    jobs = max(1, default_jobs() if jobs is None else int(jobs))
    jobs = min(jobs, len(items))
    if worker is None or jobs <= 1:
        rows = []
        for ordinal, key in enumerate(items, 1):
            row = _glb_unit_row(seam, index, key, output_root)
            rows.append(row)
            _print_glb_row(seam.label, ordinal, len(items), row)
    else:
        rows = _run_glb_pool(seam.label, worker, items, output_root, jobs)
    return _finalize_glb_corpus(seam.label, seam.noun, rows, output_root)


IMAGE_GLB = GlbUnitSeam("image GLB", "images", "image_glb", family="images")
SOUND_GLB = GlbUnitSeam("sound GLB", "sounds", "sound_glb", family="sounds")
EXPRESSION_TABLE_GLB = GlbUnitSeam("expression-table GLB", "tables", "expression_table_glb")
SHADER_SOURCE_GLB = GlbUnitSeam(
    "shader-source GLB",
    "sources",
    "shader_program_glb",
    family="shader-programs",
    export_attr="export_shader_source",
    keys_attr="shader_source_source_keys",
)
SHADER_PROGRAM_GLB = GlbUnitSeam(
    "shader-program GLB", "programs", "shader_program_glb", family="shader-programs"
)
PARTICLE_GLB = GlbUnitSeam("particle GLB", "particles", "particle_glb", family="particles")
FONT_GLB = GlbUnitSeam("font GLB", "fonts", "font_glb", family="fonts")
FONT_LIST_GLB = GlbUnitSeam(
    "font-list GLB",
    "registries",
    "font_glb",
    family="fonts",
    export_attr="export_font_list",
    keyless=True,
)
SOUND_SCRIPT_GLB = GlbUnitSeam(
    "sound-script GLB",
    "game sounds",
    "sound_script_glb",
    export_attr="export_game_sound",
    keys_attr="game_sound_keys",
)
SOUND_SCRIPT_MANIFEST_GLB = GlbUnitSeam(
    "sound-script-manifest GLB",
    "manifests",
    "sound_script_glb",
    export_attr="export_manifest",
    keys_attr="manifest_keys",
    keyless=True,
)
SOUNDSCAPE_GLB = GlbUnitSeam(
    "soundscape GLB",
    "soundscapes",
    "sound_script_glb",
    export_attr="export_soundscape",
    keys_attr="soundscape_keys",
)
SENTENCE_GLB = GlbUnitSeam(
    "sentence GLB",
    "sentences",
    "sound_script_glb",
    export_attr="export_sentence",
    keys_attr="sentence_keys",
)
DSP_PRESET_GLB = GlbUnitSeam(
    "dsp-preset GLB",
    "presets",
    "sound_script_glb",
    export_attr="export_dsp_preset",
    keys_attr="dsp_preset_keys",
)
SOUND_SCHEME_GLB = GlbUnitSeam(
    "sound-scheme GLB", "schemes", "sound_scheme_glb", family="sound-schemes"
)
SCENE_GLB = GlbUnitSeam("scene GLB", "scenes", "scene_glb", family="scenes")
MODEL_GLB = GlbUnitSeam("model GLB", "models", "model_glb", family="models")
DIALOGUE_GLB = GlbUnitSeam("dialogue GLB", "dialogues", "dialogue_glb", family="dialogues")
VDATA_GLB = GlbUnitSeam("vdata GLB", "units", "vdata_glb", family="vdata")
UI_RESOURCE_GLB = GlbUnitSeam("ui-resource GLB", "resources", "ui_resource_glb")
SCRIPT_GLB = GlbUnitSeam("script GLB", "scripts", "script_glb", family="scripts")
MAP_GLB = GlbUnitSeam("map GLB", "maps", "map_glb")
MAP_ENTITIES_GLB = GlbUnitSeam("map-entities GLB", "maps", "map_entities_glb")
MAP_LIGHTING_GLB = GlbUnitSeam("map-lighting GLB", "maps", "map_lighting_glb")
MAP_VISIBILITY_GLB = GlbUnitSeam("map-visibility GLB", "maps", "map_visibility_glb")
NAV_GRAPH_GLB = GlbUnitSeam("nav-graph GLB", "graphs", "nav_graph_glb", family="nav-graphs")
ENGINE_CONFIG_GLB = GlbUnitSeam(
    "engine-config GLB", "configs", "engine_config_glb", family="engine-config"
)


def export_image_glb(config, runner, key: str) -> Path:
    """Write and validate one isolated Image GLB product."""
    return export_glb_unit(config, runner, IMAGE_GLB, key)


def export_all_image_glbs(config, runner, *, jobs=None) -> list[Path]:
    """Write every `.tga`/`.bmp` identity the UP-first index resolves."""
    # 347 small images: a worker pool would spend more on rebuilding the install index per
    # process than the whole decode costs, so this runs in the caller.
    return export_all_glb_units(config, runner, IMAGE_GLB, jobs=jobs)


def export_sound_glb(config, runner, key: str) -> Path:
    """Write and validate one isolated Sound GLB product."""
    return export_glb_unit(config, runner, SOUND_GLB, key)


def export_all_sound_glbs(config, runner, *, jobs=None) -> list[Path]:
    """Write every `.wav` and `.mp3` identity below `sound/`."""
    return export_all_glb_units(
        config, runner, SOUND_GLB, jobs=jobs, worker=workers.sound_glb_worker
    )


def export_expression_table_glb(config, runner, key: str) -> Path:
    """Write and validate one isolated Expression-table GLB product."""
    return export_glb_unit(config, runner, EXPRESSION_TABLE_GLB, key)


def export_all_expression_table_glbs(config, runner, *, jobs=None) -> list[Path]:
    """Write every Faceposer expression table the install resolves below `expressions/`."""
    return export_all_glb_units(config, runner, EXPRESSION_TABLE_GLB, jobs=jobs)


def export_shader_source_glb(config, runner, key: str) -> Path:
    """Write and validate one isolated Shader-source GLB product."""
    return export_glb_unit(config, runner, SHADER_SOURCE_GLB, key)


def export_all_shader_source_glbs(config, runner, *, jobs=None) -> list[Path]:
    """Write every `materials/dxshaders/*.psh` identity."""
    return export_all_glb_units(
        config, runner, SHADER_SOURCE_GLB, jobs=jobs, worker=workers.shader_source_glb_worker
    )


def export_shader_program_glb(config, runner, key: str) -> Path:
    """Write and validate one isolated Shader-program GLB product."""
    return export_glb_unit(config, runner, SHADER_PROGRAM_GLB, key)


def export_all_shader_program_glbs(config, runner, *, jobs=None) -> list[Path]:
    """Write every `shaders/{psh,vsh,fxc}/*.vcs` identity."""
    return export_all_glb_units(
        config, runner, SHADER_PROGRAM_GLB, jobs=jobs, worker=workers.shader_program_glb_worker
    )


def export_particle_glb(config, runner, key: str) -> Path:
    """Write and validate one isolated Particle GLB product."""
    return export_glb_unit(config, runner, PARTICLE_GLB, key)


def export_all_particle_glbs(config, runner, *, jobs=None) -> list[Path]:
    """Write every particle identity below `particles/`."""
    return export_all_glb_units(config, runner, PARTICLE_GLB, jobs=jobs)


def export_font_glb(config, runner, key: str) -> Path:
    """Write and validate one isolated Font GLB product."""
    return export_glb_unit(config, runner, FONT_GLB, key)


def export_font_list_glb(config, runner) -> Path:
    """Write and validate the one `vtmb:font-list:fontlist` registry unit."""
    return export_glb_unit(config, runner, FONT_LIST_GLB, "fontlist")


def export_all_font_glbs(config, runner, *, jobs=None) -> list[Path]:
    """Write every `.fnt` identity plus the registry unit.

    `font_glb.source_keys` appends the registry's sentinel key and `font_glb.export` recognises
    it, so the registry publishes with the faces rather than needing a pass of its own.
    """
    return export_all_glb_units(
        config, runner, FONT_GLB, jobs=jobs, worker=workers.font_glb_worker
    )


def export_sound_script_glb(config, runner, key: str) -> Path:
    """Write and validate one named game-sound entry."""
    return export_glb_unit(config, runner, SOUND_SCRIPT_GLB, key)


def export_all_sound_script_glbs(config, runner, *, jobs=None) -> list[Path]:
    """Write every game sound, the game-sound manifest and every soundscape.

    `seam_map_sound_script.md` gives the manifest and the soundscapes no singular command of
    their own; `sound-scripts-glb` is where those two kinds publish.
    """
    from elysium_pipeline.formats import install

    # Before the index build, which costs a full install walk: an unconfigured root must fail
    # in the second it takes to notice, not after the walk.
    _require_export_v2_config(config)
    index = install.build_index()
    destinations = export_all_glb_units(
        config, runner, SOUND_SCRIPT_GLB, jobs=jobs, index=index
    )
    destinations += export_all_glb_units(
        config, runner, SOUND_SCRIPT_MANIFEST_GLB, jobs=jobs, index=index
    )
    destinations += export_all_glb_units(config, runner, SOUNDSCAPE_GLB, jobs=jobs, index=index)
    return destinations


def export_sentence_glb(config, runner, key: str) -> Path:
    """Write and validate one named sentence entry."""
    return export_glb_unit(config, runner, SENTENCE_GLB, key)


def export_all_sentence_glbs(config, runner, *, jobs=None) -> list[Path]:
    """Write every entry of `scripts/sentences.txt`."""
    return export_all_glb_units(config, runner, SENTENCE_GLB, jobs=jobs)


def export_dsp_preset_glb(config, runner, key: str) -> Path:
    """Write and validate one numbered DSP preset."""
    return export_glb_unit(config, runner, DSP_PRESET_GLB, key)


def export_all_dsp_preset_glbs(config, runner, *, jobs=None) -> list[Path]:
    """Write every entry of `scripts/dsp_presets.txt`."""
    return export_all_glb_units(config, runner, DSP_PRESET_GLB, jobs=jobs)


def export_sound_scheme_glb(config, runner, key: str) -> Path:
    """Write and validate one isolated Sound-scheme GLB product."""
    return export_glb_unit(config, runner, SOUND_SCHEME_GLB, key)


def export_all_sound_scheme_glbs(config, runner, *, jobs=None) -> list[Path]:
    """Write every `sound/schemes/*.txt` identity."""
    return export_all_glb_units(
        config, runner, SOUND_SCHEME_GLB, jobs=jobs, worker=workers.sound_scheme_glb_worker
    )


def export_scene_glb(config, runner, key: str) -> Path:
    """Write and validate one isolated Scene GLB product."""
    return export_glb_unit(config, runner, SCENE_GLB, key)


def export_all_scene_glbs(config, runner, *, jobs=None) -> list[Path]:
    """Write every `.vcd` choreography identity below `sound/`."""
    return export_all_glb_units(
        config, runner, SCENE_GLB, jobs=jobs, worker=workers.scene_glb_worker
    )


def export_model_glb(config, runner, key: str) -> Path:
    """Write and validate one isolated Model GLB product."""
    return export_glb_unit(config, runner, MODEL_GLB, key)


def export_all_model_glbs(config, runner, *, jobs=None) -> list[Path]:
    """Write every `models/**.mdl` identity the UP-first index resolves."""
    return export_all_glb_units(
        config, runner, MODEL_GLB, jobs=jobs, worker=workers.model_glb_worker
    )


def export_dialogue_glb(config, runner, key: str) -> Path:
    """Write and validate one isolated Dialogue GLB product."""
    return export_glb_unit(config, runner, DIALOGUE_GLB, key)


def export_all_dialogue_glbs(config, runner, *, jobs=None) -> list[Path]:
    """Write every `dlg/**.dlg` identity."""
    return export_all_glb_units(
        config, runner, DIALOGUE_GLB, jobs=jobs, worker=workers.dialogue_glb_worker
    )


def export_vdata_glb(config, runner, key: str) -> Path:
    """Write and validate one isolated Vdata GLB product."""
    return export_glb_unit(config, runner, VDATA_GLB, key)


def export_all_vdata_glbs(config, runner, *, jobs=None) -> list[Path]:
    """Write every `vdata/<subtree>/<name>.txt` identity."""
    return export_all_glb_units(
        config, runner, VDATA_GLB, jobs=jobs, worker=workers.vdata_glb_worker
    )


def export_ui_resource_glb(config, runner, key: str) -> Path:
    """Write and validate one isolated UI-resource GLB product."""
    return export_glb_unit(config, runner, UI_RESOURCE_GLB, key)


def export_all_ui_resource_glbs(config, runner, *, jobs=None) -> list[Path]:
    """Write every UI-resource identity the seam admits."""
    return export_all_glb_units(
        config, runner, UI_RESOURCE_GLB, jobs=jobs, worker=workers.ui_resource_glb_worker
    )


def export_script_glb(config, runner, key: str) -> Path:
    """Write and validate one isolated Script GLB product."""
    return export_glb_unit(config, runner, SCRIPT_GLB, key)


def export_all_script_glbs(config, runner, *, jobs=None) -> list[Path]:
    """Write every `python/**.py` and `.pyc` identity."""
    return export_all_glb_units(config, runner, SCRIPT_GLB, jobs=jobs)


#: The four units one BSP is cut into: the root and its three lump-family sub-units. They share
#: one member and one partition proof, so `map-glb <map>` publishes all four.
MAP_GLB_UNITS = (MAP_GLB, MAP_ENTITIES_GLB, MAP_LIGHTING_GLB, MAP_VISIBILITY_GLB)

#: One worker per map unit, in `MAP_GLB_UNITS` order. All four re-read the same multi-megabyte
#: BSP, so all four are worth a process.
MAP_GLB_WORKERS = (
    workers.map_glb_worker,
    workers.map_entities_glb_worker,
    workers.map_lighting_glb_worker,
    workers.map_visibility_glb_worker,
)


def export_map_glb(config, runner, key: str) -> list[Path]:
    """Publish all four units of one BSP: the root plus entities, lighting and visibility."""
    return [export_glb_unit(config, runner, seam, key) for seam in MAP_GLB_UNITS]


def export_all_map_glbs(config, runner, *, jobs=None) -> list[Path]:
    """Publish all four units of every map the UP-first index resolves."""
    from elysium_pipeline.formats import install

    # Before the index build, for the same reason `export_all_sound_script_glbs` does it.
    _require_export_v2_config(config)
    index = install.build_index()
    destinations: list[Path] = []
    for seam, worker in zip(MAP_GLB_UNITS, MAP_GLB_WORKERS):
        destinations += export_all_glb_units(
            config, runner, seam, jobs=jobs, worker=worker, index=index
        )
    return destinations


def export_map_entities_glb(config, runner, key: str) -> Path:
    """Write and validate one map's entity-lump unit."""
    return export_glb_unit(config, runner, MAP_ENTITIES_GLB, key)


def export_all_map_entities_glbs(config, runner, *, jobs=None) -> list[Path]:
    """Write every map's entity-lump unit."""
    return export_all_glb_units(
        config, runner, MAP_ENTITIES_GLB, jobs=jobs, worker=workers.map_entities_glb_worker
    )


def export_map_lighting_glb(config, runner, key: str) -> Path:
    """Write and validate one map's lighting unit."""
    return export_glb_unit(config, runner, MAP_LIGHTING_GLB, key)


def export_all_map_lighting_glbs(config, runner, *, jobs=None) -> list[Path]:
    """Write every map's lighting unit."""
    return export_all_glb_units(
        config, runner, MAP_LIGHTING_GLB, jobs=jobs, worker=workers.map_lighting_glb_worker
    )


def export_map_visibility_glb(config, runner, key: str) -> Path:
    """Write and validate one map's visibility unit."""
    return export_glb_unit(config, runner, MAP_VISIBILITY_GLB, key)


def export_all_map_visibility_glbs(config, runner, *, jobs=None) -> list[Path]:
    """Write every map's visibility unit."""
    return export_all_glb_units(
        config, runner, MAP_VISIBILITY_GLB, jobs=jobs, worker=workers.map_visibility_glb_worker
    )


def export_nav_graph_glb(config, runner, key: str) -> Path:
    """Write and validate one isolated Nav-graph GLB product."""
    return export_glb_unit(config, runner, NAV_GRAPH_GLB, key)


def export_all_nav_graph_glbs(config, runner, *, jobs=None) -> list[Path]:
    """Write every `maps/graphs/*.ain` identity."""
    return export_all_glb_units(
        config, runner, NAV_GRAPH_GLB, jobs=jobs, worker=workers.nav_graph_glb_worker
    )


def export_engine_config_glb(config, runner, key: str) -> Path:
    """Write and validate one isolated Engine-config GLB product."""
    return export_glb_unit(config, runner, ENGINE_CONFIG_GLB, key)


def export_all_engine_config_glbs(config, runner, *, jobs=None) -> list[Path]:
    """Write every engine-configuration identity the seam admits."""
    return export_all_glb_units(
        config, runner, ENGINE_CONFIG_GLB, jobs=jobs, worker=workers.engine_config_glb_worker
    )


def export_corpus_index_glb(config, runner) -> Path:
    """Write and validate the one corpus index of the export root.

    The index is a product over other products: it walks the whole merged install, states which
    unit owns every member or why nothing does, and carries the cross-unit reference graph over
    every unit already published below `$ELYSIUM_EXPORT_V2_ROOT`. It fails while any member is
    unclaimed or any cross-unit check fails, which is the mechanical form of "no data left
    undecoded" (`seam_map_corpus_index.md`, "The guarantee").
    """
    del runner
    _require_export_v2_config(config)
    from elysium_pipeline.exporters import corpus_index_glb
    from elysium_pipeline.formats.corpus_index_glb import walk as corpus_walk
    from elysium_pipeline.validation import corpus_index_glb as corpus_index_validation

    output_root = config.export_v2_root
    try:
        walk = corpus_walk.collect()
        destination = corpus_index_glb.export(walk, output_root)
        summary = corpus_index_validation.validate(destination)
    except Exception as exc:
        raise OfflineExportFailure(f"corpus-index GLB export failed: {exc}") from exc
    if summary["failedChecks"]:
        raise OfflineExportFailure(
            "corpus-index GLB export failed: cross-unit check(s) "
            + ", ".join(summary["failedChecks"])
        )
    counts = summary["byDisposition"]
    print(
        f"corpus-index GLB: {summary['asset']} -> {destination} "
        f"({summary['members']} members, {counts['unit']} unit, {counts['companion']} companion, "
        f"{counts['residue']} residue, {counts['unclaimed']} unclaimed; "
        f"{summary['units']} units, {summary['references']} references)"
    )
    for warning in corpus_index_validation.warnings_for(summary):
        print(f"  warning: corpus-index: {warning}")
    return destination


def export_all_corpus_index_glbs(config, runner, *, jobs=None) -> list[Path]:
    """The corpus-index seam as `export-all` runs it: one unit, so no pool and no jobs."""
    del jobs
    return [export_corpus_index_glb(config, runner)]


def _glb_seam_of(*corpora):
    """One `GLB_SEAMS` entry that runs several plural commands as one seam.

    A kind whose command surface is split across more than one plural -- the sound-script tables,
    the two shader kinds -- is still one seam of the corpus, and `export-all` runs it as one.
    """

    def run(config, runner, *, jobs=None) -> list[Path]:
        destinations: list[Path] = []
        for corpus in corpora:
            destinations += corpus(config, runner, jobs=jobs)
        return destinations

    return run


#: Every isolated GLB seam, in the order `export_v2 export-all` runs them. The order is a
#: convenience, not a guarantee: the reference graph has edges in both directions (a surface
#: property names sound scripts, a script names maps), so no run order makes every reference
#: already published when it is written. What the order does guarantee is that corpus-index runs
#: last, over the whole published corpus. Each seam resolves its own references against the
#: UP-first install index rather than against what earlier seams wrote, so each is independent.
#: Each seam owns its own source selection and publishes under its own directory of the export_v2
#: root.
GLB_SEAMS = (
    ("texture", export_all_texture_glbs),
    ("surface-property", export_all_surface_property_glbs),
    ("material", export_all_material_glbs),
    ("image", export_all_image_glbs),
    ("sound", export_all_sound_glbs),
    ("expression-table", export_all_expression_table_glbs),
    ("shader-program", _glb_seam_of(
        export_all_shader_source_glbs, export_all_shader_program_glbs)),
    ("particle", export_all_particle_glbs),
    ("font", export_all_font_glbs),
    ("sound-script", _glb_seam_of(
        export_all_sound_script_glbs, export_all_sentence_glbs, export_all_dsp_preset_glbs)),
    ("sound-scheme", export_all_sound_scheme_glbs),
    ("scene", export_all_scene_glbs),
    ("model", export_all_model_glbs),
    ("dialogue", export_all_dialogue_glbs),
    ("vdata", export_all_vdata_glbs),
    ("ui-resource", export_all_ui_resource_glbs),
    ("script", export_all_script_glbs),
    ("map", export_all_map_glbs),
    ("nav-graph", export_all_nav_graph_glbs),
    ("engine-config", export_all_engine_config_glbs),
    # Last, and last for a reason: the corpus index is written over the published corpus, so
    # every seam above has to have run before it can name what it indexes
    # (`seam_map_corpus_index.md`, "Unit identity").
    ("corpus-index", export_all_corpus_index_glbs),
)


def export_all_glb_seams(config, runner, *, jobs=None) -> dict[str, list[Path]]:
    """Run every isolated GLB seam and report each one's outcome."""
    _require_export_v2_config(config)
    published: dict[str, list[Path]] = {}
    failures: list[tuple[str, str]] = []
    for seam, export_corpus in GLB_SEAMS:
        print(f"== {seam} GLB seam", flush=True)
        try:
            published[seam] = export_corpus(config, runner, jobs=jobs)
        except OfflineExportFailure as exc:
            # A seam that fails must not discard the seams that already succeeded, nor stop the
            # ones still to run: each is an independent corpus and each costs its own hours.
            published[seam] = []
            failures.append((seam, str(exc)))
            print(f"! {seam} GLB seam failed: {exc}", flush=True)
    for seam, destinations in published.items():
        print(f"{seam} GLB seam: {len(destinations)} unit(s)", flush=True)
    if failures:
        raise OfflineExportFailure(
            "export_v2 export-all failed for "
            + "; ".join(f"{seam}: {error}" for seam, error in failures)
        )
    return published


def verify_maps(config, runner, maps: Sequence[str] | None = None) -> list[str]:
    """Re-run the map bake verifier, baking nothing. Every baked map unless told otherwise."""
    _require_export_config(config)
    names = list(dict.fromkeys(maps or ()))
    if not names:
        mount = config.repo_root / "Plugins" / "ElysiumBaked" / "Content"
        names = sorted(
            child.name
            for child in (mount.iterdir() if mount.is_dir() else ())
            if child.is_dir() and (child / f"{child.name}.uasset").is_file()
        )
    if not names:
        raise ValueError("no maps named and /ElysiumBaked carries no baked level")
    unreal.verify_bakes(config, runner, names)
    return names


def export_is_incomplete(export_root: Path) -> bool:
    return (export_root / INCOMPLETE_FILE).is_file()
