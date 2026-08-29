"""High-level export workflows spanning offline decoders and Unreal asset baking."""

from __future__ import annotations

import ast
from collections import defaultdict
from collections.abc import Mapping, Sequence
from concurrent.futures import ProcessPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path
import threading
import time

from elysium_pipeline.clean import (
    INCOMPLETE_FILE,
    MANIFEST_FILE,
    adopt_export_root,
    clean_generated,
    mark_complete,
    validate_clean_targets,
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
from elysium_pipeline import shared_corpus, unreal, wield_corpus, workers


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
        "signs": (export_root / "signs" / "backgrounds.json",),
        "vdata": (export_root / "vdata",),
        "items": (
            export_root / "items" / "ground_models.json",
            wield_corpus.manifest_path(export_root),
        ),
        "cfg": (export_root / "cfg",),
        "scenes": (export_root / "scenes",),
        "ui": (export_root / "ui" / "strings.json",),
        "use-icons": (
            export_root / "hud" / "use_icons.json",
            export_root / "hud" / "use_icons.png",
        ),
        "npc": (export_root / "npc" / "npc_index.json",),
    }
    return mapping.get(bundle, ())


#: Bundles that read per-map export products under `$ELYSIUM_EXPORT_ROOT/<map>/` and therefore
#: wait on every map task: `audio` reads each map's `.ents` for its WAV and soundscheme
#: references, and `npc` seeds the cast from every exported map's `.ents`. Every other bundle
#: reads the install (or the pre-graph shared corpus) directly and starts immediately;
#: `scenes` only globs whatever `.ents` already exist for a report-only cross-check that its
#: own exporter declares independent of the map export.
MAP_DEPENDENT_BUNDLES = frozenset({"audio", "npc"})


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
    requested = set(bundles)
    for bundle in bundles:
        name = f"bundle:{bundle}"
        # Only the bundles that consume exported per-map products wait on the maps. Between
        # bundles there is exactly one edge: `npc` resolves the player bodies through the
        # exported `vdata/system/clandoc000.txt` mirror and skips them when it is absent. The
        # rest write disjoint directories from the install and run concurrently.
        dependencies = (
            all_map_dependencies if bundle in MAP_DEPENDENT_BUNDLES else ()
        )
        if bundle == "npc" and "vdata" in requested:
            dependencies += ("bundle:vdata",)

        def action(bundle_name=bundle) -> None:
            _raise_results(
                export_all.export_bundles(
                    [bundle_name],
                    maps=maps,
                    force=True,
                    inventory=True,
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
        targets = validate_clean_targets(
            repo_root=config.repo_root,
            game_root=config.game_root,
            work_root=config.work_root,
            export_root=config.export_root,
        )
        clean_generated(targets)
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
        # export resolves its materials and textures against it, and the `items` bundle joins
        # its manifests over the decoded props. It is decoded once, here, before anything
        # reads it.
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


WORLD_MATERIAL_GENERATOR = "make_world_materials.py"
CHARACTER_MATERIAL_GENERATORS = (
    "make_player_body_material.py",
    "make_eye_material.py",
)
WIELD_MATERIAL_GENERATOR = "make_wield_materials.py"


def _wield_material_fingerprint(config) -> str:
    scripts = _policy_script_paths(
        config, only_generators=(WIELD_MATERIAL_GENERATOR,), include_fonts=False)
    return fingerprint_content(scripts, extra=("policy-wield-materials-v1",))


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
    material_root = config.repo_root / "Content" / "ElysiumGenerated" / "Materials"
    return Task(
        "unreal:policy:character-materials",
        lambda: unreal.generate_policy_content(
            config, runner, CHARACTER_MATERIAL_GENERATORS, include_auxiliary=False),
        fingerprint=lambda: _character_material_fingerprint(config),
        outputs=(material_root / "M_PlayerBody.uasset", material_root / "M_Eyes.uasset"),
    )


def ensure_world_material_content(config, runner, *, force: bool = False) -> TaskResult:
    """Keep the material masters consumed by map packages current, without global policy work."""
    manifest = Manifest(config.export_root / MANIFEST_FILE)
    task = _world_material_task(config, runner)
    return TaskGraph([task]).run(force=force, manifest=manifest)[task.name]


def ensure_character_material_content(config, runner, *, force: bool = False,
                                      manifest: Manifest | None = None) -> TaskResult:
    """Keep the two material masters consumed by character/prop bakes current in isolation.

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
    outputs = (
        config.repo_root / "Content" / "ElysiumGenerated" / "Boot.umap",
        *(font_root / name for name in unreal.FONT_ASSETS),
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
    return (
        config.repo_root
        / "Plugins"
        / "ElysiumBaked"
        / "Content"
        / map_name
        / f"{map_name}.umap"
    )


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


def _ensure_wield_bake(config, runner, *, force: bool = False,
                       cache: ContentDigestCache | None = None) -> None:
    """Launch the wield-corpus bake only when its recorded recipe receipt is stale.

    The fingerprint covers the wield manifest, the wield `.eskm` containers and their texture
    closure, the driving scripts, and the wield-material policy the instances compile
    against. The launch itself takes no per-stem force: `bake_wield.py` decides per asset off
    its recipe stamps, so `--force` defeats only this gate.
    """
    def fingerprint() -> str:
        value = fingerprint_content(
            [
                wield_corpus.manifest_path(config.export_root),
                wield_corpus.wield_dir(config.export_root),
            ],
            extra=(
                "wield-bake-v1",
                _bake_script_fingerprint(config, ("bake_wield.py",), cache=cache),
                _wield_material_fingerprint(config),
            ),
            cache=cache,
        )
        if cache is not None:
            cache.write()
        return value

    task = Task(
        "unreal:bake:wield",
        lambda: unreal.bake_wield(config, runner, ()),
        fingerprint=fingerprint,
        outputs=(
            config.repo_root / "Plugins" / "ElysiumBaked" / "Content" / "Items"
            / "DA_WieldModels.uasset",
        ),
    )
    _run_launch_task(config, task, force=force)


def _maps_bake_fingerprint(config, maps: Sequence[str], *,
                           particles: bool = False,
                           cache: ContentDigestCache | None = None) -> str:
    """One recipe for the whole profile bake: every selected map's exported directory
    (geometry, entities and sidecars), the whole shared corpus (its tables, and the texture
    and mesh bytes the map-scoped material and level recipes hash), the particle sprites
    each map imports, the driving scripts, and the world-material policy."""
    inputs = [config.export_root / name for name in maps]
    inputs.append(config.export_root / "shared")
    if particles:
        # The sprites only reach a package when the Niagara pass runs, so they are an input
        # to the recipe only then; otherwise a re-decoded sprite would relaunch a bake that
        # cannot consume it.
        inputs.append(config.export_root / "particles")
    return fingerprint_content(
        inputs,
        extra=(
            "maps-bake-v2",
            "particles-on" if particles else "particles-off",
            _bake_script_fingerprint(config, ("bake_map.py",), cache=cache),
            _world_material_fingerprint(config),
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
    # The cast and its placed-prop scope BEFORE the map bakes: a map's level stage loads
    # `/ElysiumBaked/Props/<stem>/SK_*` and its rest clip for every animated GAME_LUMP
    # placement, so those assets must be on the mount first -- the same ordering the focused
    # map path already enforces through `export_placed_models`. Recipe stamps make the
    # repeat cost of a current cast one editor boot of "reused" decisions.
    export_characters(config, runner, None, force=force or clean, verify=verify,
                      jobs=jobs, cache=cache)
    _ensure_wield_bake(config, runner, force=force or clean, cache=cache)
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
            ensure_world_material_content(config, runner, force=force)
        except Exception as exc:
            raise ExportBakeFailure(str(exc)) from exc
        export_map_npcs(config, runner, names, force=force, index=index)
        # A focused map is a complete development unit: its non-character MDLs must have their
        # catalogue rows, native containers and independently baked prop scopes current before the
        # level is accepted. This path never invokes the global NPC exporter.
        export_placed_models(config, runner, names, force=force, index=index)
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
    _require_export_config(config)
    from elysium_pipeline.formats import install, mdl_gltf, mdl_skel

    index = install.build_index()
    normalized = model.replace("\\", "/")
    if not normalized.lower().endswith(".mdl"):
        normalized += ".mdl"
    if not integrate:
        destination = config.work_root / "scratch" / "models" / Path(normalized).stem
        destination.mkdir(parents=True, exist_ok=True)
        mdl_gltf.export(normalized, animation, destination, index=index)
        return destination

    adopt_export_root(config.export_root, config.work_root)
    from elysium_pipeline.exporters import npc_export

    npc_export.main(only=[normalized], index=index, integrate=True, strict=True)
    with (config.export_root / "npc" / "npc_manifest.json").open(encoding="utf-8") as handle:
        manifest = json.load(handle)
    normalized_key = normalized.lower().replace("\\", "/")
    stems = [stem for stem, row in manifest.get("npcs", {}).items()
             if str(row.get("model", "")).lower().replace("\\", "/") == normalized_key]
    if len(stems) != 1:
        raise OfflineExportFailure(
            f"integrated model {normalized} resolved {len(stems)} NPC catalogue rows"
        )
    # Character assets are global packages; changing one body does not dirty any map package.
    # Keep placed props out of this slice as well -- they own independent containers and scopes.
    export_characters(config, runner, stems, sweep=False, include_props=False)
    return config.export_root / "npc"


def export_character_glb(config, runner, model: str) -> Path:
    """Write one isolated full-slice Character GLB and validate the published file."""
    del runner  # This exporter is pure offline Python; kept for the public workflow signature.
    _require_export_config(config)
    from elysium_pipeline.exporters import character_glb
    from elysium_pipeline.formats import install, mdl_skel
    from elysium_pipeline.validation import character_glb as character_glb_validation

    normalized = model.replace("\\", "/")
    if not normalized.lower().endswith(".mdl"):
        normalized += ".mdl"
    index = install.build_index()
    output_root = config.export_root / "glb" / "characters"
    anorms = mdl_skel.load_anorms()
    try:
        destination = character_glb.export(
            index, normalized, output_root, anorms=anorms
        )
        summary = character_glb_validation.validate(destination)
    except Exception as exc:
        raise OfflineExportFailure(
            f"character GLB export failed for {normalized}: {exc}"
        ) from exc
    print(
        "character GLB: "
        f"{summary['asset']} -> {destination} "
        f"({summary['bones']} bones, {summary['lods']} LODs, "
        f"{summary['animations']} animations)"
    )
    return destination


def _character_glb_models(index: dict) -> list[str]:
    """Every character MDL with an admitted VTX topology companion."""
    models = []
    for path in index:
        if not path.startswith("models/character/") or not path.endswith(".mdl"):
            continue
        stem = path[:-4]
        if stem + ".dx80.vtx" in index or stem + ".dx7_2bone.vtx" in index:
            models.append(path)
    return sorted(models)


def _character_glb_one(index, model: str, output_root: Path, anorms) -> dict:
    from elysium_pipeline.exporters import character_glb
    from elysium_pipeline.validation import character_glb as character_glb_validation

    try:
        destination = character_glb.export(
            index, model, output_root, anorms=anorms
        )
        summary = character_glb_validation.validate(destination)
        return {
            "item": model,
            "destination": str(destination),
            "summary": summary,
            "error": "",
        }
    except Exception as exc:
        return {
            "item": model,
            "destination": "",
            "summary": None,
            "error": f"{type(exc).__name__}: {exc}",
        }


def _print_glb_row(label: str, ordinal: int, total: int, row: dict) -> None:
    item = row["item"]
    if row["error"]:
        print(f"! {label} [{ordinal}/{total}] failed for {item}: {row['error']}", flush=True)
        return
    summary = row["summary"]
    print(
        f"{label} [{ordinal}/{total}]: {summary['asset']} -> "
        f"{row['destination']} ({summary['accountedBytes']}/{summary['sourceBytes']} bytes)",
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


def export_all_character_glbs(config, runner, *, jobs=None) -> list[Path]:
    """Write every admitted character through the isolated schema-1.1 GLB pipeline."""
    del runner
    _require_export_config(config)
    from elysium_pipeline.formats import install, mdl_skel

    index = install.build_index()
    models = _character_glb_models(index)
    if not models:
        raise OfflineExportFailure("character GLB corpus has no admitted models")
    output_root = config.export_root / "glb" / "characters"
    jobs = max(1, default_jobs() if jobs is None else int(jobs))
    jobs = min(jobs, len(models))
    if jobs <= 1:
        anorms = mdl_skel.load_anorms()
        rows = []
        for ordinal, model in enumerate(models, 1):
            row = _character_glb_one(index, model, output_root, anorms)
            rows.append(row)
            _print_glb_row("character GLB", ordinal, len(models), row)
    else:
        rows = _run_glb_pool(
            "character GLB",
            workers.character_glb_worker,
            models,
            output_root,
            jobs,
        )
    return _finalize_glb_corpus("character GLB", "models", rows, output_root)


def export_texture_glb(config, runner, texture: str) -> Path:
    """Write and validate one isolated Texture GLB product."""
    del runner
    _require_export_config(config)
    from elysium_pipeline.exporters import texture_glb
    from elysium_pipeline.formats import install
    from elysium_pipeline.validation import texture_glb as texture_glb_validation

    index = install.build_index()
    output_root = config.export_root / "glb" / "textures"
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
    return destination


def _texture_glb_sources(index: dict) -> list[str]:
    prefix, suffix = "materials/", ".tth"
    return sorted(
        path[len(prefix):-len(suffix)]
        for path in index
        if path.startswith(prefix) and path.endswith(suffix)
    )


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
    _require_export_config(config)
    from elysium_pipeline.formats import install

    index = install.build_index()
    textures = _texture_glb_sources(index)
    if not textures:
        raise OfflineExportFailure("texture GLB corpus has no selected TTH members")
    output_root = config.export_root / "glb" / "textures"
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


def _placed_row_satisfies(row: dict, use) -> bool:
    """Whether an integrated catalogue row already covers one map-authored use."""
    if not row or row.get("model", "").lower().replace("\\", "/") != use.model:
        return False
    if row.get("static_stem") != use.static_stem or not row.get("eskm"):
        return False
    clips = {str(label).lower() for label in row.get("clips", {})}
    rest = {str(label).lower() for label in row.get("rest_candidates", ())}
    if not rest or not rest.issubset(clips):
        return False
    mode = str(row.get("clip_mode", "rest")).lower()
    if use.full_clips:
        return mode == "full"
    required = {label.lower() for label in use.required_clips}
    if required and (mode not in ("required", "full") or not required.issubset(clips)):
        return False
    return True


def export_map_npcs(config, runner, maps: Sequence[str], *, force: bool = False,
                    index: dict | None = None) -> list[str]:
    """Integrate and bake only NPC models newly required by the selected maps."""
    _require_export_config(config)
    from elysium_pipeline.exporters import npc_export
    from elysium_pipeline.formats import install
    from elysium_pipeline.placed_models import normalize_model_path

    models = set()
    for name in dict.fromkeys(maps):
        path = config.export_root / name / f"{name}.ents"
        if not path.is_file():
            raise ValueError(f"map entities are not exported: {path}")
        try:
            document = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError) as exc:
            raise OfflineExportFailure(f"cannot read NPC dependencies from {path}: {exc}") from exc
        for entity in document.get("entities", []):
            if not str(entity.get("classname", "")).lower().startswith("npc_"):
                continue
            model = normalize_model_path(entity.get("keys", {}).get("model", ""))
            if model.endswith(".mdl"):
                models.add(model)
    if not models:
        return []

    npc_dir = config.export_root / "npc"
    manifest_path = npc_dir / "npc_manifest.json"
    if not manifest_path.is_file():
        raise ValueError(
            f"{manifest_path} is missing; run: uv run elysium export bundle npc")
    with manifest_path.open(encoding="utf-8") as handle:
        manifest = json.load(handle)
    existing = {str(row.get("model", "")).lower().replace("\\", "/")
                for row in manifest.get("npcs", {}).values()}
    warned = {str(row.get("model", "")).lower().replace("\\", "/")
              for row in manifest.get("warnings", ())}
    dirty = sorted(models - warned if force else models - existing - warned)
    if not dirty:
        print(f"map NPCs: {len(models)} model reference(s) already catalogued")
        return []

    shared_index = index if index is not None else install.build_index()
    print("map NPCs: updating " + ", ".join(dirty))
    npc_export.main(only=dirty, index=shared_index, integrate=True, strict=True)
    with manifest_path.open(encoding="utf-8") as handle:
        manifest = json.load(handle)
    dirty_set = set(dirty)
    stems = sorted(stem for stem, row in manifest.get("npcs", {}).items()
                   if str(row.get("model", "")).lower().replace("\\", "/") in dirty_set)
    if stems:
        export_characters(
            config, runner, stems, sweep=False, include_props=False)
    return stems


def _preserve_placed_row_policy(use, row: dict):
    """Never let a focused map downgrade vocabulary another map already required."""
    from elysium_pipeline.placed_models import PlacedModelUse

    mode = str(row.get("clip_mode", "")).lower()
    if mode == "full":
        return PlacedModelUse(use.model, use.stem, use.static_stem, True, ())
    required = set(use.required_clips)
    if mode == "required":
        required.update(str(label) for label in row.get("clips", {}))
    return PlacedModelUse(
        use.model, use.stem, use.static_stem, use.full_clips, tuple(sorted(required)))


def export_placed_models(config, runner, maps: Sequence[str], models: Sequence[str] | None = None,
                         *, force: bool = False, index: dict | None = None,
                         verify: bool = False) -> list[str]:
    """Integrate and bake only placed models used by ``maps``.

    The global NPC manifest remains the release/reconstruct inventory. This workflow projects the
    selected maps against that inventory, upgrades only insufficient rows, writes only the named
    `.eskm` containers, and launches Unreal only for stale prop scopes.
    """
    _require_export_config(config)
    adopt_export_root(config.export_root, config.work_root)
    from elysium_pipeline import placed_models
    from elysium_pipeline.exporters import npc_export
    from elysium_pipeline.formats import install

    names = list(dict.fromkeys(str(name).strip() for name in maps if str(name).strip()))
    if not names:
        raise ValueError("placed-model export needs at least one map")
    missing_maps = [name for name in names
                    if not (config.export_root / name / f"{name}.ents").is_file()]
    if missing_maps:
        raise ValueError("map entities are not exported: " + ", ".join(missing_maps))

    npc_dir = config.export_root / "npc"
    manifest_path = npc_dir / "npc_manifest.json"
    if not manifest_path.is_file() or not (npc_dir / "npc_index.json").is_file():
        raise ValueError(
            f"{npc_dir} has no complete NPC catalogue; run: uv run elysium export bundle npc"
        )
    shared_index = index if index is not None else install.build_index()
    uses = placed_models.discover(str(config.export_root), shared_index, map_names=names)
    requested_models = {
        placed_models.normalize_model_path(model)
        for model in (models or ()) if str(model).strip()
    }
    if requested_models:
        by_model = {use.model: use for use in uses}
        unknown = sorted(requested_models - by_model.keys())
        if unknown:
            raise ValueError(
                "placed model(s) are not used by " + ", ".join(names) + ": " + ", ".join(unknown)
            )
        uses = [by_model[model] for model in sorted(requested_models)]
    if not uses:
        print("placed models: selected map scope is empty")
        return []

    with manifest_path.open(encoding="utf-8") as handle:
        source_manifest = json.load(handle)
    rows = source_manifest.get("placed_models", {})
    dirty_uses = []
    for use in uses:
        row = rows.get(use.stem, {})
        if force or not _placed_row_satisfies(row, use):
            dirty_uses.append(_preserve_placed_row_policy(use, row))
    if dirty_uses:
        print("placed models: updating " + ", ".join(use.stem for use in dirty_uses))
        npc_export.main(placed_uses=dirty_uses, index=shared_index, integrate=True, strict=True)

    stems = [use.stem for use in uses]
    receipt_store = Manifest(config.export_root / MANIFEST_FILE)
    write_placed_model_sources(
        config, npc_dir, stems, manifest=receipt_store, force=force)
    read_character_partition(npc_dir)

    # The prop mesh stores only a neutral material reference. The owning map supplies its exact
    # material instances at runtime, so policy changes do not invalidate this scope; only absence
    # of the neutral master is a prerequisite failure.
    body_master = config.repo_root / "Content" / "ElysiumGenerated" / "Materials" / "M_PlayerBody.uasset"
    if not body_master.is_file():
        ensure_character_material_content(config, runner, manifest=receipt_store)
    _run_character_bake(config, runner, (), props=stems, verify=verify, force=force,
                        manifest=receipt_store)
    return stems


FAMILIES_FILE = "families.json"
CHARACTER_TEXTURES_FILE = "textures.json"


def _character_bake_fingerprint(config, stems: Sequence[str], props: Sequence[str], *,
                                cache: ContentDigestCache | None = None) -> str:
    """The character launch's whole recipe: the complete `npc/` export tree (containers,
    partition, texture corpus and sidecars are all bake inputs), the driving scripts, the
    character-material policy the bodies instance from, and the requested slice."""
    return fingerprint_content(
        [config.export_root / "npc"],
        extra=(
            "character-bake-v1",
            _bake_script_fingerprint(config, ("bake_characters.py",), cache=cache),
            _character_material_fingerprint(config),
            "stems:" + ",".join(stems),
            "props:" + ",".join(props),
        ),
        cache=cache,
    )


def _character_bake_outputs(config, stems: Sequence[str],
                            props: Sequence[str]) -> tuple[Path, ...]:
    """The per-unit meshes the launch must leave on the mount, so a nuked or partially
    removed mount defeats the fingerprint skip."""
    mount = config.repo_root / "Plugins" / "ElysiumBaked" / "Content"
    return (
        *(mount / "Characters" / "Meshes" / f"SK_{stem}.uasset" for stem in stems),
        *(mount / "Props" / stem / f"SK_{stem}.uasset" for stem in props),
    )


def _run_character_bake(config, runner, stems: Sequence[str], *, props: Sequence[str] = (),
                        after_bake=None, verify: bool = False, force: bool = False,
                        manifest: Manifest | None = None,
                        cache: ContentDigestCache | None = None) -> None:
    """Author the stale character units and leave each one stamped with its recipe.

    The launch itself rides a manifest receipt over its complete input tree, so a current
    cast costs no editor boot at all; a stale receipt costs one boot whose per-unit reuse is
    still the commandlet's decision, read off each asset's recipe stamp. Acceptance is the
    editor's own exit: the commandlet stamps every unit it authored as it lands, so a run
    that dies keeps exactly what it saved, records no success, and the next run resumes off
    the stamps alone. `verify` adds the deep read-back on top, which
    `uv run elysium verify characters` also runs on its own.
    """
    stems = list(dict.fromkeys(stems))
    props = list(dict.fromkeys(props))
    if not stems and not props:
        raise ValueError("character bake needs at least one body or placed model")
    if cache is None:
        cache = ContentDigestCache(config.export_root / DIGEST_CACHE_FILE)

    def fingerprint() -> str:
        value = _character_bake_fingerprint(config, stems, props, cache=cache)
        # Persisted before the launch so the commandlet's own digest reads start warm.
        cache.write()
        return value

    def launch() -> None:
        # A bake that runs invalidates the sweep receipt first: a crash between the editor's
        # saves and the sweep must leave the next run scanning the mount, not trusting the
        # receipt of the partition that was swept before this one authored.
        (config.export_root / SWEEP_RECEIPT_FILE).unlink(missing_ok=True)
        unreal.bake_characters(config, runner, stems, props=props, force=force)

    task = Task(
        "unreal:bake:characters",
        launch,
        fingerprint=fingerprint,
        outputs=_character_bake_outputs(config, stems, props),
    )
    _run_launch_task(config, task, manifest=manifest, force=force)
    if after_bake is not None:
        after_bake()
    if verify:
        unreal.verify_characters(config, runner, stems, props=props)


def _character_code_inputs(config) -> tuple[Path, ...]:
    """The offline code a `.eskm` container's bytes depend on."""
    root = config.repo_root / "pipeline" / "src" / "elysium_pipeline"
    return (
        root / "exporters" / "UE_mdl_skeletal.py",
        root / "formats" / "mdl_skel.py",
        root / "formats" / "mdl_secondary_motion.py",
        root / "formats" / "mdl.py",
        root / "formats" / "mdl_gltf.py",
    )


def _character_code_fingerprint(config) -> str:
    """The exporter's identity, by content. A rebuilt or re-checked-out file whose bytes did not
    move must not rewrite 166 containers and re-bake the cast behind them."""
    return fingerprint_content(_character_code_inputs(config), extra=("eskm-exporter-v1",))


def _index_prefix_buckets(index: dict) -> dict[str, list[str]]:
    """{dot-terminated prefix: sorted index keys that start with it}, one pass.

    `_character_source_detail`'s needle is always a prefix ending at a ``.``, so bucketing
    every key under each of its dot prefixes answers the same `startswith` question with one
    lookup instead of a scan over the whole index per model. A key joins one bucket per dot
    it contains, which keeps the lookup exactly equal to the scan whatever dots a directory
    or stem carries.
    """
    buckets: dict[str, list[str]] = defaultdict(list)
    for key in index:
        at = key.find(".")
        while at >= 0:
            buckets[key[: at + 1]].append(key)
            at = key.find(".", at + 1)
    for entries in buckets.values():
        entries.sort()
    return dict(buckets)


def _character_source_detail(index: dict, model_rel: str,
                             buckets: Mapping[str, Sequence[str]] | None = None) -> str:
    """A stable identity for one model's own files inside the user's install.

    A model is more than its `.mdl` -- the exporter also reads the sibling `.vvd`, `.vtx` and
    `.ani` -- so every index entry sharing the model's directory and stem contributes. Its
    INCLUDED banks deliberately do not: a bank is written as its own container by its own task,
    and a body's container carries only the body's own clips.

    ``buckets`` is `_index_prefix_buckets(index)`; with it the sibling set is one lookup, and
    the answer is identical to the scan the bucketless call performs.
    """
    key = model_rel.replace("\\", "/").lower()
    prefix = key[: -len(".mdl")] + "." if key.endswith(".mdl") else key
    if buckets is not None and prefix.endswith("."):
        names = buckets.get(prefix, ())
    else:
        names = sorted(k for k in index if k.startswith(prefix))
    digest = hashlib.sha256()
    for name in names:
        kind, value = index[name]
        digest.update(name.encode("utf-8", errors="surrogateescape"))
        digest.update(b"\0")
        digest.update(str(kind).encode("ascii", errors="replace"))
        if kind == "loose":
            path = Path(value)
            try:
                stat = path.stat()
                detail = f"{stat.st_size}:{stat.st_mtime_ns}"
            except OSError:
                detail = "missing"
        else:
            detail = repr(value)
        digest.update(detail.encode("utf-8", errors="surrogateescape"))
        digest.update(b"\0")
    return digest.hexdigest()


def _load_npc_manifest(npc_dir: Path) -> dict:
    """One parse of the cast manifest, shared by every consumer a run passes it to."""
    with (npc_dir / "npc_manifest.json").open(encoding="utf-8-sig") as handle:
        return json.load(handle)


def character_source_plan(
    npc_dir: Path, manifest: dict | None = None,
) -> tuple[dict[str, str], dict[str, str], dict[str, str], dict[str, str]]:
    """({model stem}, {bank stem}, {cinematic stem}, {prop stem}) -> install path, whole cast.

    Deliberately not parameterised by a slice. The rig partition is a property of the entire
    corpus (`elysium_pipeline.character_partition`), and it was previously derived from whichever
    containers a slice happened to leave on disk -- so a workspace that had only ever baked two
    models partitioned the banks differently from one that had baked all of them. Writing every
    container on every run removes that filesystem side effect; making each write individually
    skippable is what keeps it affordable.

    ``manifest`` is the already-parsed `npc_manifest.json`; without it the file is read here.
    """
    if manifest is None:
        manifest = _load_npc_manifest(npc_dir)

    # A label names every bank that declares it, in include-tree order, so the owner set is the
    # flattened values rather than the values themselves.
    def _owners(record):
        for owners in record.get("clips", {}).values():
            yield from (owners if isinstance(owners, list) else [owners])

    models = {stem: record["model"] for stem, record in manifest["npcs"].items()}
    banks: dict[str, str] = {}
    for stem, record in manifest["npcs"].items():
        for owner in _owners(record):
            if owner == stem or owner in banks:
                continue
            bank = manifest["banks"].get(owner)
            if bank is None:
                raise ValueError(
                    f"{stem} names bank '{owner}', which the manifest does not carry"
                )
            banks[owner] = bank["model"]

    # A cinematic bank is reached by a choreographed SCENE, not by any body's clip map, so the
    # loop above -- which walks vocabularies -- never names one. Left out, its clips are absent
    # from the mount and `ResolveClipFromBank` answers them out of the glb instead, in
    # glTFRuntime's basis rather than this pipeline's (the character verifier).
    #
    # One model yields one bank PER ACTOR ROOT, so the writer is keyed by the model's own stem
    # and the per-root stems join `banks` for the partition and the paths that follow it.
    cinematics: dict[str, str] = {}
    for model, record in manifest.get("cinematics", {}).items():
        stem = record.get("stem")
        if not stem:
            continue
        cinematics[stem] = model
        for root in record.get("roots", []):
            bank = manifest["banks"].get(root.get("bank"))
            if bank is not None:
                banks[root["bank"]] = bank["model"]

    # A placed model is a skeletal model like any other, so it takes the same container. It is
    # NOT a cast member: it is absent from the declared partition and nothing declares
    # compatibility with its skeleton, because a crane and a wolf share no tree with each other or
    # with a biped.
    props = {stem: record["model"]
             for stem, record in manifest.get("placed_models", {}).items()
             if record.get("model")}
    return models, banks, cinematics, props


def placed_model_source_fingerprint(stem: str, record: dict, code_fingerprint: str,
                                    source_detail: str) -> str:
    """Fingerprint one placed-model container, including its rest/full clip policy."""
    clip_labels = "\n".join(record.get("clips", {}).keys())
    ensure_labels = "\n".join(record.get("rest_candidates", ()))
    return fingerprint_content(
        (), extra=("eskm-placed-model-v1", stem, record.get("clip_mode", "rest"),
                   clip_labels, ensure_labels, code_fingerprint, source_detail)
    )


def _inline_character_source(kind: str, stem: str, model_rel: str, out_dir: str,
                             clip_labels: Sequence[str] = (),
                             ensure_labels: Sequence[str] = ()) -> None:
    """Run one container write on the calling thread, through the same worker function the
    process pool uses, so both paths write identical containers."""
    text = workers.character_source_worker(
        kind, stem, model_rel, out_dir, clip_labels, ensure_labels)
    if text:
        print(text, end="")


def _placed_model_source_tasks(npc_dir: Path, source_manifest: dict,
                               props: dict[str, str], index: dict,
                               code_fingerprint: str, *,
                               buckets: Mapping[str, Sequence[str]] | None = None,
                               dispatch=None) -> list[Task]:
    """Build the independently cacheable `.eskm` tasks for exactly ``props``.

    Placed models own no shared skeleton or animation bank, so there is no correctness reason
    for a one-prop edit to enumerate the cast.  The whole-corpus character path calls this with
    every declared prop and its shared process-pool dispatch; the focused map path calls it
    with only the selected stems, written inline.
    """
    if dispatch is None:
        dispatch = _inline_character_source
    prop_dir = npc_dir / "placed_models"
    tasks: list[Task] = []
    for stem, model_rel in sorted(props.items()):
        record = source_manifest.get("placed_models", {}).get(stem, {})
        clip_labels = tuple(record.get("clips", {}).keys())
        ensure_labels = tuple(record.get("rest_candidates", ()))

        def prop_action(stem=stem, model_rel=model_rel, clip_labels=clip_labels,
                        ensure_labels=ensure_labels) -> None:
            dispatch("prop", stem, model_rel, str(prop_dir),
                     clip_labels, ensure_labels)

        def prop_fingerprint(model_rel=model_rel, stem=stem, record=record) -> str:
            return placed_model_source_fingerprint(
                stem, record, code_fingerprint,
                _character_source_detail(index, model_rel, buckets))

        tasks.append(Task(
            name=f"eskm:prop:{stem}",
            action=prop_action,
            fingerprint=prop_fingerprint,
            outputs=(prop_dir / f"{stem}.eskm",),
        ))
    return tasks


def write_placed_model_sources(config, npc_dir: Path, stems: Sequence[str], *,
                               manifest: Manifest, force: bool = False) -> list[str]:
    """Write only the named placed-model containers, preserving the global cast untouched."""
    from elysium_pipeline.formats import install

    source_manifest = _load_npc_manifest(npc_dir)
    declared = source_manifest.get("placed_models", {})
    requested = list(dict.fromkeys(str(stem).strip().lower() for stem in stems if str(stem).strip()))
    unknown = [stem for stem in requested if not declared.get(stem, {}).get("model")]
    if unknown:
        raise OfflineExportFailure(
            "placed model(s) absent from npc_manifest: " + ", ".join(sorted(unknown))
        )
    props = {stem: declared[stem]["model"] for stem in requested}
    index = install.build_index(verbose=False)
    code_fingerprint = _character_code_fingerprint(config)
    tasks = _placed_model_source_tasks(npc_dir, source_manifest, props, index, code_fingerprint)
    results = TaskGraph(tasks).run(force=force, manifest=manifest) if tasks else {}
    failures = [name for name, result in results.items() if result.status not in ("ok", "skipped")]
    if failures:
        raise OfflineExportFailure(
            "placed-model sources failed: " + ", ".join(sorted(failures))
        )
    return requested


def write_character_sources(
    config, npc_dir: Path, *, manifest: Manifest, force: bool = False,
    include_props: bool = True, body_stems: Sequence[str] | None = None,
    jobs: int | None = None, npc_manifest: dict | None = None
) -> tuple[list[str], list[str]]:
    """Write the requested `.eskm` closure, or the whole cast when no slice is named.

    This is the offline half of the character bake: the Python side decodes VtMB's own formats
    and writes one Unreal-native container per model and per bank, and the editor side reads
    nothing else. Returns (bodies, banks) as declared, whether or not each was rewritten.

    Each container is its own task, fingerprinted on that model's own files in the install plus
    the exporter code that turns them into a container. The default remains the complete corpus;
    a focused body slice writes only those bodies, body-owned clip donors they name, and the bank
    containers their clip maps reach. Existing containers for every other family remain the
    authoritative inputs when the global rig partition is recomputed.

    Container writing is GIL-bound struct parsing, so ``jobs`` scheduler threads dispatch each
    stale task's write to a shared process pool of the same width
    (`workers.character_source_worker`); the fingerprint and skip decisions stay on the
    scheduler threads, so a current container never pays a process round-trip, and a fully
    current run spawns no pool at all.
    """
    from elysium_pipeline.formats import install

    source_manifest = npc_manifest if npc_manifest is not None else _load_npc_manifest(npc_dir)
    models, banks, cinematics, props = character_source_plan(npc_dir, source_manifest)
    # Every per-root container of a cinematic model is produced by one `write_cinematic` call, so
    # its stems are not their own tasks -- they would each rewrite the whole performance.
    cinematic_stems = {stem for stem in banks if any(
        stem == prefix or stem.startswith(prefix + "__") for prefix in cinematics)}
    if body_stems is not None:
        requested = set(dict.fromkeys(
            str(stem).strip().lower() for stem in body_stems if str(stem).strip()))
        unknown = sorted(requested - models.keys())
        if unknown:
            raise OfflineExportFailure(
                "character source model(s) absent from npc_manifest: " + ", ".join(unknown))
        model_sources = set(requested)
        bank_sources = set()
        for stem in requested:
            for owners in source_manifest["npcs"][stem].get("clips", {}).values():
              for owner in (owners if isinstance(owners, list) else [owners]):
                if owner in models:
                    # Some dialogue/performance clips live in another body's own container.
                    model_sources.add(owner)
                elif owner in banks:
                    bank_sources.add(owner)
        models = {stem: models[stem] for stem in sorted(model_sources)}
        banks = {stem: banks[stem] for stem in sorted(bank_sources)}
        selected_cinematics = {
            prefix for prefix in cinematics
            if any(root == prefix or root.startswith(prefix + "__")
                   for root in bank_sources)
        }
        cinematics = {stem: cinematics[stem] for stem in sorted(selected_cinematics)}
        cinematic_stems &= bank_sources
    index = install.build_index(verbose=False)
    buckets = _index_prefix_buckets(index)
    code_fingerprint = _character_code_fingerprint(config)
    jobs = max(1, jobs if jobs is not None else default_jobs())

    # The pool exists only once a stale task actually dispatches, so a fully current run
    # costs no process spawn; `min` keeps a small slice from spawning idle workers.
    pool: list[ProcessPoolExecutor] = []
    pool_lock = threading.Lock()

    def dispatch(kind, stem, model_rel, out_dir, clip_labels=(), ensure_labels=()):
        if jobs <= 1:
            _inline_character_source(kind, stem, model_rel, out_dir,
                                     clip_labels, ensure_labels)
            return
        with pool_lock:
            if not pool:
                pool.append(ProcessPoolExecutor(max_workers=min(jobs, len(tasks))))
            executor = pool[0]
        text = executor.submit(
            workers.character_source_worker, kind, stem, model_rel, out_dir,
            clip_labels, ensure_labels,
        ).result()
        if text:
            print(text, end="")

    tasks: list[Task] = []
    for stem, model_rel in sorted(models.items()):
        def model_action(stem=stem, model_rel=model_rel) -> None:
            dispatch("model", stem, model_rel, str(npc_dir))

        def model_fingerprint(model_rel=model_rel, stem=stem) -> str:
            return fingerprint_content(
                (), extra=("eskm-model-v1", stem, code_fingerprint,
                           _character_source_detail(index, model_rel, buckets))
            )

        tasks.append(Task(
            name=f"eskm:{stem}",
            action=model_action,
            fingerprint=model_fingerprint,
            outputs=(npc_dir / f"{stem}.eskm",),
        ))

    for stem, model_rel in sorted(banks.items()):
        if stem in cinematic_stems:
            continue

        def bank_action(stem=stem, model_rel=model_rel) -> None:
            dispatch("bank", stem, model_rel, str(npc_dir))

        def bank_fingerprint(model_rel=model_rel, stem=stem) -> str:
            return fingerprint_content(
                (), extra=("eskm-bank-v1", stem, code_fingerprint,
                           _character_source_detail(index, model_rel, buckets))
            )

        tasks.append(Task(
            name=f"eskm:bank:{stem}",
            action=bank_action,
            fingerprint=bank_fingerprint,
            outputs=(npc_dir / "banks" / f"{stem}.eskm",),
        ))

    for stem, model_rel in sorted(cinematics.items()):
        # Every root of this performance at once, so the task's outputs are the whole set it
        # writes rather than the one its name carries.
        roots = sorted(bank for bank in cinematic_stems
                       if bank == stem or bank.startswith(stem + "__"))

        def cinematic_action(stem=stem, model_rel=model_rel) -> None:
            dispatch("cinematic", stem, model_rel, str(npc_dir))

        def cinematic_fingerprint(model_rel=model_rel, stem=stem) -> str:
            return fingerprint_content(
                (), extra=("eskm-cinematic-v1", stem, code_fingerprint,
                           _character_source_detail(index, model_rel, buckets))
            )

        tasks.append(Task(
            name=f"eskm:cinematic:{stem}",
            action=cinematic_action,
            fingerprint=cinematic_fingerprint,
            outputs=tuple(npc_dir / "banks" / f"{root}.eskm" for root in roots),
        ))

    # The same container a body takes, geometry and all -- an animated prop IS a skeletal model,
    # but owns no shared cast state. The focused path reuses this exact task factory for a slice.
    if include_props:
        tasks.extend(_placed_model_source_tasks(
            npc_dir, source_manifest, props, index, code_fingerprint,
            buckets=buckets, dispatch=dispatch))

    stamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
    log_root = getattr(config, "log_root", None) or npc_dir
    progress = TaskProgress(
        total=len(tasks),
        log_path=log_root / f"{stamp}-character-sources.log",
        per_task=False,
    )
    try:
        with progress:
            progress.note(
                f"character sources: {len(tasks)} container task(s), {jobs} job(s)")
            results = TaskGraph(tasks).run(
                jobs=jobs, force=force, manifest=manifest, progress=progress)
    finally:
        if pool:
            pool[0].shutdown(wait=True, cancel_futures=True)
    failures = [name for name, result in results.items() if result.status not in ("ok", "skipped")]
    if failures:
        raise OfflineExportFailure(
            "character sources failed: " + ", ".join(sorted(failures))
        )
    return sorted(models), sorted(banks)


def write_character_partition(npc_dir: Path, *, validate_props: bool = True,
                              npc_manifest: dict | None = None) -> dict:
    """Write `npc/families.json` and `npc/textures.json` over the whole corpus.

    Whole-corpus for two reasons. The BANK partition is greedy and order-dependent, so it is a
    property of the set it is given (`formats/eskm.rig_families`): recomputing it per slice renames
    bank families -- and can merge two the whole cast keeps apart -- while that name IS the path
    contract for the skeleton their sequences bind to. The MODEL half is one entry per stem, and
    the file is the one statement of which bodies are declared at all, which is what lets the bake,
    the sweep and the verifier judge a slice against the whole cast.

    `textures.json` rides along because the same pass already has every container's material
    table open. Without it the editor reopens 400 MB of containers just to recover which albedo
    each material wants.

    Only the SKEL and MATL sections are consumed, so each container is read through
    `eskm.read_sections`' seek path -- a few kilobytes per file instead of the clip payload
    that dominates the corpus.
    """
    from elysium_pipeline import asset_names, character_partition
    from elysium_pipeline.formats import eskm

    # Props are deliberately absent from the partition: a prop is baked out of its own container
    # into its own mount folder, and nothing declares compatibility with its skeleton, so no
    # reader joins it here. They are present in the texture table all the same -- one texture
    # package serves the whole mount, and a prop albedo missing from it is swept as an orphan the
    # moment anything sweeps.
    models, banks, _cinematics, props = character_source_plan(npc_dir, npc_manifest)
    model_paths = {stem: npc_dir / f"{stem}.eskm" for stem in models}
    bank_paths = {stem: npc_dir / "banks" / f"{stem}.eskm" for stem in banks}
    prop_paths = {stem: npc_dir / "placed_models" / f"{stem}.eskm" for stem in props}

    model_trees: dict[str, dict[str, str]] = {}
    bank_trees: dict[str, dict[str, str]] = {}
    textures: dict[str, dict] = {}
    bindings: dict[str, dict[str, str]] = {}
    corpus = hashlib.sha256()

    for kind, paths, trees in (
        ("model", model_paths, model_trees),
        ("bank", bank_paths, bank_trees),
    ):
        for stem, path in sorted(paths.items()):
            if not path.is_file():
                raise OfflineExportFailure(
                    f"{path} is missing; the character sources did not complete"
                )
            try:
                sections = eskm.read_sections(path, (b"SKEL", b"MATL"))
            except (OSError, ValueError) as exc:
                raise OfflineExportFailure(
                    f"{path} is not a readable .eskm container: {exc}"
                ) from exc
            trees[stem] = eskm.bone_parents_from_section(sections[b"SKEL"])
            corpus.update(f"{kind}:{stem}:".encode("utf-8"))
            corpus.update(character_partition.tree_fingerprint(trees[stem]).encode("ascii"))
            for material, uri in sorted(
                    eskm.materials_from_section(sections[b"MATL"]).items()):
                if not uri:
                    continue
                name = asset_names.texture_asset_name(uri)
                entry = textures.setdefault(name, {"uri": uri, "used_by": []})
                if stem not in entry["used_by"]:
                    entry["used_by"].append(stem)
                bindings.setdefault(stem, {})[material] = name

    # Placed-model materials are supplied by each map's already-baked static mesh at runtime.
    # Importing this complete corpus here would duplicate the same game-derived textures globally.
    if validate_props:
        for stem, path in sorted(prop_paths.items()):
            if not path.is_file():
                raise OfflineExportFailure(
                    f"{path} is missing; the character sources did not complete"
                )
            try:
                # Header and section directory only: validates magic, version and table
                # shape without loading the clip payload the partition never reads.
                eskm.read_sections(path, ())
            except (OSError, ValueError) as exc:
                raise OfflineExportFailure(
                    f"{path} is not a readable .eskm container: {exc}"
                ) from exc

    partition = character_partition.build_partition(
        model_trees, bank_trees, corpus_fingerprint=corpus.hexdigest()
    )
    _write_json(npc_dir / FAMILIES_FILE, partition)
    _write_json(npc_dir / CHARACTER_TEXTURES_FILE, {
        "schema": "elysium.character-textures",
        "version": 1,
        "textures": {name: {"uri": entry["uri"], "used_by": sorted(entry["used_by"])}
                     for name, entry in sorted(textures.items())},
        "bindings": {stem: dict(sorted(slots.items()))
                     for stem, slots in sorted(bindings.items())},
    })
    return partition


def _write_json(path: Path, payload: dict) -> None:
    """Write `payload` atomically, sorted and newline-terminated so a re-run diffs cleanly.

    A byte-identical rewrite is skipped, so an unchanged document keeps its mtime and every
    stat-keyed digest cache downstream of it stays warm instead of rehashing the corpus it
    describes.
    """
    text = json.dumps(payload, indent=1, sort_keys=True) + "\n"
    try:
        if path.read_text(encoding="utf-8") == text:
            return
    except (OSError, UnicodeError):
        pass
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(text, encoding="utf-8")
    temporary.replace(path)


def resolve_character_slice(partition: dict, selectors: Sequence[str] | None,
                            npc_dir: Path | None = None) -> list[str]:
    """Selectors -> the model stems to bake, resolved against the declared partition.

    A bare stem is that model. `family:<name>` is an alias for the bare stem, because a model's
    declared entry names only itself. `bank:<name>` is every model that plays a bank in that bank
    family, which is the slice to take when a bank container changed -- the partition names bank
    membership and the manifest names who plays them, so it is the one selector that reads both.

    No selectors is the whole cast, which is what the game needs and what a release must have.
    """
    from elysium_pipeline import character_partition

    if not selectors:
        return sorted(partition["model_family_of"])

    players: dict[str, set[str]] | None = None

    def bank_players() -> dict[str, set[str]]:
        """{bank owner: stems that play it}, read once and only if a bank selector asks."""
        nonlocal players
        if players is None:
            players = {}
            if npc_dir is not None and (npc_dir / "npc_manifest.json").is_file():
                with (npc_dir / "npc_manifest.json").open(encoding="utf-8-sig") as handle:
                    manifest = json.load(handle)
                for stem, record in manifest.get("npcs", {}).items():
                    for owners in record.get("clips", {}).values():
                      for owner in (owners if isinstance(owners, list) else [owners]):
                        if owner != stem:
                            players.setdefault(owner, set()).add(stem)
        return players

    stems: list[str] = []
    unknown: list[str] = []
    for raw in selectors:
        selector = raw.replace("\\", "/").strip()
        kind, _, value = selector.partition(":")
        if not value:
            kind, value = "", selector
        value = value.lower() if kind else Path(value).stem.lower()

        if kind == "family":
            members = character_partition.members_for(partition, "models", value)
            if members:
                stems.extend(members)
            else:
                unknown.append(raw)
        elif kind == "bank":
            banks = character_partition.members_for(partition, "banks", value)
            if not banks:
                unknown.append(raw)
                continue
            reached = bank_players()
            for bank in banks:
                stems.extend(sorted(reached.get(bank, ())))
        elif value in partition["model_family_of"]:
            stems.append(value)
        else:
            unknown.append(raw)

    if unknown:
        raise ValueError("not in the declared partition: " + ", ".join(sorted(unknown)))
    return sorted(dict.fromkeys(stem for stem in stems if stem))


#: The receipt `sweep_characters` keeps beside the export manifest: the content identity of
#: the two files the sweep's expected-asset set is a function of.
SWEEP_RECEIPT_FILE = ".elysium-character-sweep.json"


def _sweep_inputs_digest(npc_dir: Path) -> dict[str, str] | None:
    """Content identity of the sweep's inputs, or None when either file is absent."""
    out: dict[str, str] = {}
    for name in (FAMILIES_FILE, CHARACTER_TEXTURES_FILE):
        try:
            out[name] = hashlib.sha256((npc_dir / name).read_bytes()).hexdigest()
        except OSError:
            return None
    return out


def sweep_characters(config, partition: dict, *, apply: bool = True,
                     force: bool = False, skip_unchanged: bool = False,
                     npc_manifest: dict | None = None) -> dict:
    """Remove baked character assets the declared partition no longer produces.

    Safe on a slice, because the partition it checks against always covers the whole cast: a run
    that baked two models still knows what the other 164 own. That is the property that makes a
    global sweep possible at all -- the flat `Meshes/`, `Materials/` and `Skeletons/` folders give
    no per-body answer.

    ``skip_unchanged`` skips the whole mount scan when `families.json` and `textures.json` are
    byte-identical to the last applied sweep's receipt: the expected-asset set is a function of
    those two files, and a bake against an unchanged partition writes only expected assets, so
    no new orphan can have appeared. ``force`` defeats the receipt as well as the removal guard.
    """
    from elysium_pipeline import character_sweep

    mount = config.repo_root / "Plugins" / "ElysiumBaked" / "Content"
    npc_dir = config.export_root / "npc"
    receipt_path = config.export_root / SWEEP_RECEIPT_FILE
    digests = _sweep_inputs_digest(npc_dir)
    if skip_unchanged and apply and not force and digests is not None:
        try:
            recorded = json.loads(receipt_path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            recorded = None
        if recorded and recorded.get("inputs") == digests:
            print("character sweep: partition and texture table unchanged; "
                  "mount scan skipped")
            return {"orphan_assets": [], "orphan_dirs": [], "total": 0,
                    "removed": 0, "refused": "", "skipped": True}
    result = character_sweep.sweep(
        mount, npc_dir, partition, apply=apply, force=force, manifest=npc_manifest
    )
    if apply and not result["refused"] and digests is not None:
        _write_json(receipt_path, {"schema": "elysium.character-sweep-receipt",
                                   "version": 1, "inputs": digests})
    return result


def _stale_garments(config, stems: Sequence[str]) -> list[str]:
    """Named stems whose cloth asset is older than an input it was generated from.

    Two inputs, not one. The garment sidecar is the decoded payload, and the tracked authored
    tuning asset (`Content/ElysiumAuthored/Cloth/DA_ClothTuning.uasset`) is every material and
    solver value applied on top of it -- so a tuning edit has to invalidate the whole corpus the
    same way re-exporting one model invalidates that one. Without it the fast path reports the
    assets current and the edit silently does nothing.

    Most stems author no garment and are absent from both sides, which is not staleness.
    """
    garment_dir = config.export_root / "npc" / "garment"
    cloth_dir = config.repo_root / "Plugins" / "ElysiumBaked" / "Content" / "Characters" / "Cloth"
    tuning = (config.repo_root / "Content" / "ElysiumAuthored" / "Cloth"
              / "DA_ClothTuning.uasset")
    tuned_at = tuning.stat().st_mtime if tuning.is_file() else 0.0
    stale = []
    for stem in stems:
        sidecar = garment_dir / f"{stem}.json"
        if not sidecar.is_file():
            continue
        asset = cloth_dir / f"CLOTH_{stem}.uasset"
        if not asset.is_file() or asset.stat().st_mtime < max(sidecar.stat().st_mtime, tuned_at):
            stale.append(stem)
    return stale


def _cloth_bake_stems(config, stems: Sequence[str], *, force: bool = False) -> list[str]:
    """The stems worth a cloth editor launch: the stale garments, or under ``force`` every
    authored one. Empty when no named stem authors a garment at all, which skips the launch
    -- there is no work an editor boot could do."""
    if force:
        garment_dir = config.export_root / "npc" / "garment"
        return [stem for stem in stems if (garment_dir / f"{stem}.json").is_file()]
    return _stale_garments(config, stems)


def export_characters(
    config, runner, models: Sequence[str] | None = None, *, force: bool = False,
    sweep: bool = True, force_sweep: bool = False, include_props: bool = True,
    verify: bool = False, jobs: int | None = None,
    cache: ContentDigestCache | None = None
) -> list[str]:
    """Bake characters onto /ElysiumBaked/Characters (ANM1) -- the whole cast unless told otherwise.

    Three stages. The `.eskm` containers are written from the user's own install, the rig
    partition is derived from them and written down, then a headless editor turns the pair into a
    skeleton and a mesh per model, a shared skeleton per bank rig family, and a compressed
    sequence per clip. The manifest and the eye sidecars have to be on disk already, which
    `export bundle npc` or a complete profile writes. The policy content is a prerequisite too,
    because a body is built against the same master materials the runtime names.

    **The partition always covers the whole cast; a named slice writes only its source closure and
    bakes only those bodies.** The existing containers for every other body and bank remain inputs
    to the partition, so a slice cannot rename a family out from under meshes that already point at
    it. A complete export still writes and validates the entire corpus.

    **The cast is the bake's default because the mount is the only build of a character.** A stem
    the bake has not covered cannot stand at all -- there is no loader to fall back to -- so a
    partial bake is a broken game rather than a slower one. Naming models is for iterating on a few.
    """
    _require_export_config(config)
    npc_dir = config.export_root / "npc"
    index = npc_dir / "npc_index.json"
    if not index.is_file():
        raise ValueError(
            f"{index} is missing; run: uv run elysium export bundle npc"
        )
    # The 30 MB cast manifest, parsed once and handed to every stage of this run.
    npc_manifest = _load_npc_manifest(npc_dir)

    source_stems: list[str] | None = None
    if models:
        declared_models, _banks, _cinematics, _props = character_source_plan(
            npc_dir, npc_manifest)
        exact = []
        exact_only = True
        for selector in models:
            text = str(selector).replace("\\", "/").strip()
            if ":" in text:
                exact_only = False
                break
            stem = Path(text).stem.lower()
            if stem not in declared_models:
                exact_only = False
                break
            exact.append(stem)
        if exact_only:
            source_stems = sorted(dict.fromkeys(exact))
        else:
            # A selector that is not a bare stem resolves through the last complete partition.
            # The selected sources are then written before that partition is recomputed.
            source_stems = resolve_character_slice(
                read_character_partition(npc_dir), models, npc_dir)

    manifest = Manifest(config.export_root / MANIFEST_FILE)
    write_character_sources(
        config, npc_dir, manifest=manifest, force=force, include_props=include_props,
        body_stems=source_stems, jobs=jobs, npc_manifest=npc_manifest)
    partition = write_character_partition(npc_dir, validate_props=include_props,
                                          npc_manifest=npc_manifest)

    stems = resolve_character_slice(partition, models, npc_dir)
    if not stems:
        raise ValueError("no models named and the manifest lists no character")

    ensure_character_material_content(config, runner, manifest=manifest)

    def sweep_after_bake() -> None:
        # Once the editor has returned and before any receipt is promoted: an orphan from a
        # partition that has since moved must not survive on the mount, and a verify that is
        # opted into runs after this for the same reason.
        if not sweep:
            return
        result = sweep_characters(config, partition, force=force_sweep,
                                  skip_unchanged=not (force or force_sweep),
                                  npc_manifest=npc_manifest)
        if result["refused"]:
            raise ExportBakeFailure("character sweep refused: " + result["refused"])
        if result["removed"]:
            print(f"swept {result['removed']} orphaned character asset(s)")

    # One process for the whole cast. Banks are built once on their declared skeleton families and
    # reused by compatible body skeletons; package count must not scale with the number of bodies.
    # Whether the run authors anything at all is the commandlet's per-unit decision, read off
    # each asset's recipe stamp.
    _run_character_bake(config, runner, stems, after_bake=sweep_after_bake, verify=verify,
                        force=force, manifest=manifest, cache=cache)
    # The garments those bodies wear. After the bake has been accepted, because a cloth asset
    # resolves its bone names against the mesh's reference skeleton -- the mesh has to be on the
    # mount before a garment can bind to it. Most models author none, and a launch happens only
    # when a garment is stale (or, under --force, authored at all).
    cloth_stems = _cloth_bake_stems(config, stems, force=force)
    if cloth_stems:
        unreal.make_cloth_assets(config, runner, cloth_stems)
    else:
        print("cloth: no authored garment is stale; editor launch skipped")
    return stems


def read_character_partition(npc_dir: Path) -> dict:
    """The declared partition as the bake wrote it, refusing a schema this build cannot read."""
    from elysium_pipeline import character_partition

    path = npc_dir / FAMILIES_FILE
    if not path.is_file():
        raise ValueError(
            f"{path} is missing; run: uv run elysium export characters"
        )
    with path.open(encoding="utf-8-sig") as handle:
        partition = json.load(handle)
    character_partition.check(partition)
    return partition


def verify_characters(config, runner, models: Sequence[str] | None = None) -> list[str]:
    """Re-run the character verifier over a slice, baking nothing.

    A verify is not a cheap tail of the bake and cannot be folded into it: the verifier's whole
    premise is that only a fresh process can tell what is ON THE MOUNT from what a bake left
    resident in memory. So it is its own verb, and its own launch.

    Reads the declared partition rather than recomputing one -- a verify that partitioned the
    corpus its own way would be checking a different mount than the one the bake wrote.
    """
    _require_export_config(config)
    npc_dir = config.export_root / "npc"
    partition = read_character_partition(npc_dir)
    stems = resolve_character_slice(partition, models, npc_dir)
    if not stems:
        raise ValueError("no models named and the manifest lists no character")
    unreal.verify_characters(config, runner, stems)
    return stems


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
