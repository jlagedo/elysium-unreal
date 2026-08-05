"""High-level export workflows spanning offline decoders and Unreal asset baking."""

from __future__ import annotations

import ast
from collections import defaultdict
from collections.abc import Sequence
import hashlib
import json
from pathlib import Path

from elysium_pipeline.clean import (
    INCOMPLETE_FILE,
    MANIFEST_FILE,
    adopt_export_root,
    clean_generated,
    mark_complete,
    validate_clean_targets,
)
from elysium_pipeline.tasking import (
    Manifest,
    Task,
    TaskGraph,
    TaskResult,
    fingerprint_content,
    fingerprint_paths,
)
from elysium_pipeline import bake_cache, unreal


class OfflineExportFailure(RuntimeError):
    exit_code = 5


class ExportBakeFailure(RuntimeError):
    exit_code = 6


def default_jobs() -> int:
    try:
        import psutil

        physical = psutil.cpu_count(logical=False) or 1
    except Exception:
        physical = 1
    return min(4, max(1, physical // 2))


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


def _raise_results(results) -> None:
    failures = [result for result in results if not result.ok]
    if failures:
        raise OfflineExportFailure(
            "; ".join(f"{item.name}: {item.error or item.status}" for item in failures)
        )


def _map_tasks(config, map_names: Sequence[str], index: dict, source_fingerprint: str) -> list[Task]:
    from elysium_pipeline.exporters import export_all
    from elysium_pipeline.formats import install

    code_inputs = (
        config.repo_root
        / "pipeline"
        / "src"
        / "elysium_pipeline"
        / "exporters"
        / "UE_bsp_to_scene.py",
        config.repo_root / "pipeline" / "src" / "elysium_pipeline" / "formats" / "bsp.py",
        config.repo_root / "pipeline" / "src" / "elysium_pipeline" / "formats" / "weather.py",
        config.repo_root / "pipeline" / "src" / "elysium_pipeline" / "formats" / "particles.py",
    )
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

        def fingerprint(path=bsp_path, name=map_name) -> str:
            return fingerprint_paths(
                [path, *code_inputs],
                extra=("map", name, source_fingerprint),
            )

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


def _bundle_tasks(
    config,
    bundles: Sequence[str],
    maps: Sequence[str],
    index: dict,
    source_fingerprint: str,
) -> list[Task]:
    from elysium_pipeline.exporters import export_all

    tasks: list[Task] = []
    prior: tuple[str, ...] = ()
    all_map_dependencies = tuple(f"map:{name}" for name in maps)
    for bundle in bundles:
        name = f"bundle:{bundle}"
        dependencies = all_map_dependencies
        if prior:
            dependencies += prior

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

        code = (
            config.repo_root
            / "pipeline"
            / "src"
            / "elysium_pipeline"
            / "exporters"
            / (
                "npc_export.py"
                if bundle == "npc"
                else "UE_use_icons.py"
                if bundle == "use-icons"
                else "UE_extract_particles.py"
                if bundle == "particles"
                else "export_all.py"
            )
        )
        tasks.append(
            Task(
                name=name,
                action=action,
                dependencies=dependencies,
                fingerprint=lambda b=bundle, p=code: fingerprint_paths(
                    [p], extra=("bundle", b, source_fingerprint, *maps)
                ),
                outputs=_bundle_outputs(config.export_root, bundle),
            )
        )
        # Global mirrors intentionally run once and serially. This also keeps vdata
        # before NPC and ensures scenes sees every completed map.
        prior = (name,)
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
    source_fingerprint = fingerprint_paths(
        [
            config.repo_root / "pipeline" / "src" / "elysium_pipeline" / "formats",
            config.repo_root / "pipeline" / "src" / "elysium_pipeline" / "exporters",
        ],
        extra=(_source_index_fingerprint(index),),
    )
    manifest = Manifest(config.export_root / MANIFEST_FILE)
    dependency_fingerprint = fingerprint_paths(
        [config.repo_root / "dev" / "dependencies.lock.json"]
    )
    manifest.set_context(
        profile=profile,
        source_fingerprint=source_fingerprint,
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
            *_map_tasks(config, maps, index, source_fingerprint),
            *_bundle_tasks(config, bundles, maps, index, source_fingerprint),
        ]
    )
    results = graph.run(
        jobs=max(1, jobs),
        force=force,
        manifest=manifest,
        fail_fast=True,
    )
    return maps, results


def _policy_script_paths(config) -> list[Path]:
    unreal_root = config.repo_root / "pipeline" / "unreal"
    build_content = unreal_root / "build_content.py"
    scripts = [build_content, unreal_root / "make_ui_fonts.py"]
    try:
        tree = ast.parse(build_content.read_text(encoding="utf-8"))
        generator_names: list[str] = []
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
            generator_names = [str(item) for item in value]
            break
        scripts.extend(unreal_root / name for name in generator_names)
    except (OSError, SyntaxError, ValueError, TypeError):
        # The umbrella itself still invalidates the policy task.  A malformed file will then fail
        # in Unreal with its real diagnostic instead of being hidden by fingerprint discovery.
        pass

    # Follow local helper imports without importing the modules (they import ``unreal`` and are
    # only executable inside an editor process).
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


def _policy_fingerprint(config) -> str:
    scripts = [
        *_policy_script_paths(config),
        config.repo_root / "Content" / "Fonts",
        config.export_root / "particles" / "manifest.json",
        config.export_root / "particles" / "dropletfast.tga",
        config.export_root / "particles" / "fortituderings.tga",
        config.export_root / "particles" / "d_targetblob.tga",
        config.export_root / "particles" / "furball.tga",
        config.export_root / "particles" / "dropletfast.png",
        config.export_root / "particles" / "fortituderings.png",
        config.export_root / "particles" / "d_targetblob.png",
        config.export_root / "particles" / "furball.png",
        config.repo_root / "Source" / "ElysiumUE" / "Public" / "ElysiumRainAssetBuilder.h",
        config.repo_root
        / "Source"
        / "ElysiumUE"
        / "Private"
        / "Editor"
        / "ElysiumRainAssetBuilder.cpp",
    ]
    return fingerprint_content(scripts, extra=("policy-v2",))


def ensure_policy_content(config, runner, *, force: bool = False) -> TaskResult:
    # The generated rain material imports normalized derivatives of the exact patch-first source
    # sprites. Keep the raw full mirror and its four-item closure current even for a focused
    # map/policy command, not only complete profiles.
    from elysium_pipeline.exporters import UE_extract_particles
    from elysium_pipeline.formats import install

    UE_extract_particles.main(index=install.build_index(dirs=("particles",)))
    manifest = Manifest(config.export_root / MANIFEST_FILE)
    font_root = config.repo_root / "Content" / "VtMB" / "UI" / "Fonts"
    outputs = (
        config.repo_root / "Content" / "Elysium.umap",
        config.repo_root / "Content" / "VtMB" / "Materials" / "M_World_Opaque.uasset",
        *(font_root / name for name in unreal.FONT_ASSETS),
        config.repo_root / "Content" / "VtMB" / "Particles" / "M_ElysiumRain.uasset",
        config.repo_root / "Content" / "VtMB" / "Particles" / "NS_ElysiumRain.uasset",
    )
    task = Task(
        "unreal:policy",
        lambda: unreal.generate_policy_content(config, runner),
        fingerprint=lambda: _policy_fingerprint(config),
        outputs=outputs,
    )
    return TaskGraph([task]).run(force=force, manifest=manifest)["unreal:policy"]


def _baked_package(config, map_name: str) -> Path:
    return (
        config.repo_root
        / "Plugins"
        / "ElysiumBaked"
        / "Content"
        / map_name
        / f"{map_name}.umap"
    )


def _verification_task(config, map_name: str) -> Task:
    baked_root = _baked_package(config, map_name).parent
    fingerprint = fingerprint_paths(
        [
            baked_root,
            config.repo_root / "pipeline" / "unreal" / "bake_verify.py",
            config.repo_root / "pipeline" / "unreal" / "bake_lib.py",
            config.repo_root
            / "pipeline"
            / "src"
            / "elysium_pipeline"
            / "validation"
            / "png_alpha.py",
        ],
        extra=("verify-bake-v2", map_name),
    )
    output = _baked_package(config, map_name)
    return Task(
        name=f"verify:{map_name}",
        action=lambda: None,
        fingerprint=lambda value=fingerprint: value,
        outputs=(output,) if output.is_file() else (),
    )


def bake_and_verify(
    config,
    runner,
    maps: Sequence[str],
    *,
    force: bool = False,
) -> None:
    manifest = Manifest(config.export_root / MANIFEST_FILE)
    names = list(dict.fromkeys(maps))
    plan = bake_cache.plan_stages(manifest, config, names, force=force)
    asset_plan_path = None
    asset_plan = None
    if plan:
        asset_plan_path, asset_plan = bake_cache.create_asset_run_plan(
            config, plan, force=force
        )

    try:
        if plan:
            grouped: dict[tuple[str, ...], list[str]] = defaultdict(list)
            for name, stages in plan.items():
                grouped[stages].append(name)
            for stages, stage_maps in grouped.items():
                unreal.bake_maps(
                    config,
                    runner,
                    stage_maps,
                    stages=",".join(stages),
                    asset_plan=asset_plan_path,
                )

        missing = [
            str(_baked_package(config, name))
            for name in plan
            if not _baked_package(config, name).is_file()
        ]
        if missing:
            raise ExportBakeFailure("bake did not produce: " + ", ".join(missing))

        reports = (
            bake_cache.load_asset_run_reports(config, asset_plan)
            if asset_plan is not None
            else {}
        )
        if asset_plan is not None:
            bake_cache.assert_asset_run_inputs_current(config, asset_plan)
        changed = bake_cache.mutated_maps(reports)
        verification = []
        for name in names:
            task = _verification_task(config, name)
            fingerprint = task.fingerprint() if task.fingerprint else None
            if name in changed or force or not manifest.can_skip(task, fingerprint):
                verification.append(name)
        if not plan and not verification:
            return
        if verification:
            unreal.verify_bakes(config, runner, verification)
        if asset_plan is not None:
            bake_cache.assert_asset_run_inputs_current(config, asset_plan)
    except ExportBakeFailure:
        raise
    except Exception as exc:
        raise ExportBakeFailure(str(exc)) from exc

    # A commandlet can save partial packages before failing.  Advance neither the stage receipts
    # nor the verification receipt until every selected map has passed independent verification.
    if asset_plan is not None:
        bake_cache.promote_asset_run(config, asset_plan, reports)
    bake_cache.record_stages(
        manifest,
        config,
        plan,
        frozen_fingerprints={
            name: value["fingerprints"]
            for name, value in (asset_plan or {}).get("maps", {}).items()
        },
    )
    for name in verification:
        task = _verification_task(config, name)
        manifest.record(
            TaskResult(
                name=task.name,
                status="ok",
                duration_seconds=0.0,
                fingerprint=task.fingerprint() if task.fingerprint else None,
                outputs=[str(path) for path in task.outputs],
            )
        )


def export_profile(
    config,
    runner,
    profile: str,
    *,
    clean: bool = False,
    force: bool = False,
    jobs: int | None = None,
) -> list[str]:
    maps, _results = run_offline_profile(
        config,
        profile,
        clean=clean,
        force=force,
        jobs=jobs or default_jobs(),
    )
    try:
        ensure_policy_content(config, runner, force=force or clean)
    except Exception as exc:
        raise ExportBakeFailure(str(exc)) from exc
    bake_and_verify(config, runner, maps, force=force or clean)
    mark_complete(config.export_root)
    return maps


def export_targeted_maps(
    config,
    runner,
    maps: Sequence[str],
    *,
    force: bool = False,
    intermediate_only: bool = False,
) -> list[str]:
    _require_export_config(config)
    adopt_export_root(config.export_root, config.work_root)
    from elysium_pipeline.exporters import export_all
    from elysium_pipeline.formats import install

    names = list(dict.fromkeys(maps))
    index = install.build_index()
    source_fingerprint = fingerprint_paths(
        [
            config.repo_root / "pipeline" / "src" / "elysium_pipeline" / "formats",
            config.repo_root / "pipeline" / "src" / "elysium_pipeline" / "exporters",
        ],
        extra=(_source_index_fingerprint(index),),
    )
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
            "jobs": 1,
        },
        maps=list(names),
        bundles=["audio"],
    )
    TaskGraph(_map_tasks(config, names, index, source_fingerprint)).run(
        jobs=1,
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
            ensure_policy_content(config, runner, force=force)
        except Exception as exc:
            raise ExportBakeFailure(str(exc)) from exc
        bake_and_verify(config, runner, names, force=force)
    return names


def export_bundle(config, runner, bundle: str, *, force: bool = False) -> None:
    _require_export_config(config)
    adopt_export_root(config.export_root, config.work_root)
    if bundle == "policy":
        ensure_policy_content(config, runner, force=force)
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


def export_model(
    config,
    runner,
    model: str,
    *,
    animation: str | None = None,
    integrate: bool = False,
) -> Path:
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

    adopt_export_root(config.export_root, config.work_root)
    from elysium_pipeline.exporters import npc_export

    npc_export.main(only=[normalized], index=index, integrate=True, strict=True)
    dependent_maps: list[str] = []
    needle = normalized.lower().removesuffix(".mdl")
    for ents in config.export_root.glob("*/*.ents"):
        try:
            text = ents.read_text(encoding="utf-8").lower()
        except OSError:
            continue
        if needle in text:
            dependent_maps.append(ents.parent.name)
    if dependent_maps:
        ensure_policy_content(config, runner)
        bake_and_verify(
            config,
            runner,
            dependent_maps,
            force=True,
        )
    return config.export_root / "npc"


def write_character_sources(npc_dir: Path, stems: Sequence[str]) -> tuple[list[str], list[str]]:
    """Write the `.eskm` container for each named body and for every bank those bodies play.

    This is the offline half of the character bake: the Python side decodes VtMB's own formats
    and writes one Unreal-native container per model, and the editor side reads nothing else.
    Returns (bodies, banks) as written.

    A bank is written whole. It is shared by the whole cast, it is the natural unit the manifest
    already resolves ownership against, and the rollout to the rest of the corpus needs it
    anyway.
    """
    from elysium_pipeline.exporters import UE_mdl_skeletal
    from elysium_pipeline.formats import install, mdl_gltf

    with (npc_dir / "npc_manifest.json").open(encoding="utf-8") as handle:
        manifest = json.load(handle)

    banks: dict[str, str] = {}
    for stem in stems:
        record = manifest["npcs"].get(stem)
        if record is None:
            raise ValueError(f"{stem} is not in the NPC manifest")
        for owner in record.get("clips", {}).values():
            if owner == stem or owner in banks:
                continue
            bank = manifest["banks"].get(owner)
            if bank is None:
                raise ValueError(
                    f"{stem} names bank '{owner}', which the manifest does not carry"
                )
            banks[owner] = bank["model"]

    index = install.build_index(verbose=False)
    # Without the unit-vector table a compressed vertex-animation record has directions but no
    # magnitudes, so a body is written with no morph section rather than a wrong one.
    anorms = mdl_gltf.load_anorms()

    for stem in stems:
        UE_mdl_skeletal.write_model(
            index, manifest["npcs"][stem]["model"], str(npc_dir), stem=stem, anorms=anorms
        )
    for stem in sorted(banks):
        UE_mdl_skeletal.write_bank(index, banks[stem], str(npc_dir), stem)
    return list(stems), sorted(banks)


def export_characters(config, runner, models: Sequence[str]) -> list[str]:
    """Bake the named models onto /ElysiumBaked/Characters (ANM1).

    Two stages. First the `.eskm` containers are written from the user's own install, then a
    headless editor turns them into a shared skeleton per rig family, a mesh per model and a
    compressed sequence per clip. The manifest and the eye sidecars have to be on disk already,
    which `export bundle npc` or a complete profile writes. The policy content is a prerequisite
    too, because a body is built against the same master materials the runtime names.

    Unlike the map bake this runs wholesale for the models it is given. There is no per-model
    receipt yet, so re-running re-bakes; the map bake's `bake_cache` store is keyed and staged by
    map and does not carry over unchanged.
    """
    _require_export_config(config)
    stems = [Path(model.replace("\\", "/")).stem.lower() for model in models]
    stems = [stem for stem in dict.fromkeys(stems) if stem]
    if not stems:
        raise ValueError("no models named")

    npc_dir = config.export_root / "npc"
    index = npc_dir / "npc_index.json"
    if not index.is_file():
        raise ValueError(
            f"{index} is missing; run: uv run elysium export bundle npc"
        )

    write_character_sources(npc_dir, stems)
    ensure_policy_content(config, runner)
    unreal.bake_characters(config, runner, stems)
    unreal.verify_characters(config, runner, stems)
    return stems


def export_is_incomplete(export_root: Path) -> bool:
    return (export_root / INCOMPLETE_FILE).is_file()
