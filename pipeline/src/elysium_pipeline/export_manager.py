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
        # `make_input_assets.py` reads this CSV, and script discovery above only walks imports --
        # so without naming it here, editing the committed input table alone leaves the task
        # cached and the generated actions, mapping context and action set stale. The failure is
        # silent in the worst way: the export reports success and the button does nothing.
        config.repo_root / "Config" / "ElysiumInputActions.csv",
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
    from elysium_pipeline.exporters.export_all import bundles_for_profile

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
    # Only the domains this profile actually covers.  `grid` and `all` both run every bundle, so
    # this clears the corpus either way; a profile that dropped one would leave that one gated.
    mark_complete(config.export_root, ("maps", "policy", *bundles_for_profile(profile)))
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


FAMILIES_FILE = "families.json"
CHARACTER_TEXTURES_FILE = "textures.json"


def _character_code_inputs(config) -> tuple[Path, ...]:
    """The offline code a `.eskm` container's bytes depend on."""
    root = config.repo_root / "pipeline" / "src" / "elysium_pipeline"
    return (
        root / "exporters" / "UE_mdl_skeletal.py",
        root / "formats" / "mdl_skel.py",
        root / "formats" / "mdl.py",
        root / "formats" / "mdl_gltf.py",
    )


def _character_source_detail(index: dict, model_rel: str) -> str:
    """A stable identity for one model's own files inside the user's install.

    A model is more than its `.mdl` -- the exporter also reads the sibling `.vvd`, `.vtx` and
    `.ani` -- so every index entry sharing the model's directory and stem contributes. Its
    INCLUDED banks deliberately do not: a bank is written as its own container by its own task,
    and a body's container carries only the body's own clips.
    """
    key = model_rel.replace("\\", "/").lower()
    prefix = key[: -len(".mdl")] + "." if key.endswith(".mdl") else key
    digest = hashlib.sha256()
    for name in sorted(k for k in index if k.startswith(prefix)):
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


def character_source_plan(npc_dir: Path) -> tuple[dict[str, str], dict[str, str]]:
    """({model stem: install path}, {bank stem: install path}) for the WHOLE cast.

    Deliberately not parameterised by a slice. The rig partition is a property of the entire
    corpus (`elysium_pipeline.character_partition`), and it was previously derived from whichever
    containers a slice happened to leave on disk -- so a workspace that had only ever baked two
    models partitioned the banks differently from one that had baked all of them. Writing every
    container on every run removes that filesystem side effect; making each write individually
    skippable is what keeps it affordable.
    """
    with (npc_dir / "npc_manifest.json").open(encoding="utf-8") as handle:
        manifest = json.load(handle)

    models = {stem: record["model"] for stem, record in manifest["npcs"].items()}
    banks: dict[str, str] = {}
    for stem, record in manifest["npcs"].items():
        for owner in record.get("clips", {}).values():
            if owner == stem or owner in banks:
                continue
            bank = manifest["banks"].get(owner)
            if bank is None:
                raise ValueError(
                    f"{stem} names bank '{owner}', which the manifest does not carry"
                )
            banks[owner] = bank["model"]
    return models, banks


def write_character_sources(
    config, npc_dir: Path, *, manifest: Manifest, force: bool = False
) -> tuple[list[str], list[str]]:
    """Write every `.eskm` container the cast needs, skipping the ones already current.

    This is the offline half of the character bake: the Python side decodes VtMB's own formats
    and writes one Unreal-native container per model and per bank, and the editor side reads
    nothing else. Returns (bodies, banks) as declared, whether or not each was rewritten.

    Each container is its own task, fingerprinted on that model's own files in the install plus
    the exporter code that turns them into a container. Rewriting all 233 costs ~90 s; confirming
    all 233 are current costs a few seconds, which is what makes writing the whole corpus the
    default rather than an expensive completeness gesture.
    """
    from elysium_pipeline.exporters import UE_mdl_skeletal
    from elysium_pipeline.formats import install, mdl_gltf

    models, banks = character_source_plan(npc_dir)
    index = install.build_index(verbose=False)
    code_fingerprint = fingerprint_paths(_character_code_inputs(config))
    # Read once, lazily: without the unit-vector table a compressed vertex-animation record has
    # directions but no magnitudes, so a body is written with no morph section rather than a
    # wrong one. A fully-current run never needs it.
    anorms: list = []

    def load_anorms():
        if not anorms:
            anorms.append(mdl_gltf.load_anorms())
        return anorms[0]

    tasks: list[Task] = []
    for stem, model_rel in sorted(models.items()):
        def model_action(stem=stem, model_rel=model_rel) -> None:
            UE_mdl_skeletal.write_model(
                index, model_rel, str(npc_dir), stem=stem, anorms=load_anorms()
            )

        def model_fingerprint(model_rel=model_rel, stem=stem) -> str:
            return fingerprint_content(
                (), extra=("eskm-model-v1", stem, code_fingerprint,
                           _character_source_detail(index, model_rel))
            )

        tasks.append(Task(
            name=f"eskm:{stem}",
            action=model_action,
            fingerprint=model_fingerprint,
            outputs=(npc_dir / f"{stem}.eskm",),
        ))

    for stem, model_rel in sorted(banks.items()):
        def bank_action(stem=stem, model_rel=model_rel) -> None:
            UE_mdl_skeletal.write_bank(index, model_rel, str(npc_dir), stem)

        def bank_fingerprint(model_rel=model_rel, stem=stem) -> str:
            return fingerprint_content(
                (), extra=("eskm-bank-v1", stem, code_fingerprint,
                           _character_source_detail(index, model_rel))
            )

        tasks.append(Task(
            name=f"eskm:bank:{stem}",
            action=bank_action,
            fingerprint=bank_fingerprint,
            outputs=(npc_dir / "banks" / f"{stem}.eskm",),
        ))

    results = TaskGraph(tasks).run(force=force, manifest=manifest)
    failures = [name for name, result in results.items() if result.status not in ("ok", "skipped")]
    if failures:
        raise OfflineExportFailure(
            "character sources failed: " + ", ".join(sorted(failures))
        )
    return sorted(models), sorted(banks)


def write_character_partition(npc_dir: Path) -> dict:
    """Write `npc/families.json` and `npc/textures.json` over the whole corpus.

    The rig partition is greedy and order-dependent, so it is a property of the set it is given
    (`formats/eskm.rig_families`). Recomputing it per slice therefore renames families -- and can
    merge two the whole cast keeps apart -- while the family name IS the path contract for every
    baked clip and skeleton. Computing it once here and writing it down is what lets the bake
    slice at all.

    `textures.json` rides along because the same pass already has every container's material
    table open. Without it the editor reopens 400 MB of containers just to recover which albedo
    each material wants.
    """
    from elysium_pipeline import asset_names, character_partition
    from elysium_pipeline.formats import eskm

    models, banks = character_source_plan(npc_dir)
    model_paths = {stem: npc_dir / f"{stem}.eskm" for stem in models}
    bank_paths = {stem: npc_dir / "banks" / f"{stem}.eskm" for stem in banks}

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
            blob = eskm.read(path)
            trees[stem] = eskm.bone_parents(blob)
            corpus.update(f"{kind}:{stem}:".encode("utf-8"))
            corpus.update(character_partition.tree_fingerprint(trees[stem]).encode("ascii"))
            for material, uri in sorted(eskm.materials(blob).items()):
                if not uri:
                    continue
                name = asset_names.texture_asset_name(uri)
                entry = textures.setdefault(name, {"uri": uri, "used_by": []})
                if stem not in entry["used_by"]:
                    entry["used_by"].append(stem)
                bindings.setdefault(stem, {})[material] = name

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
    """Write `payload` atomically, sorted and newline-terminated so a re-run diffs cleanly."""
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(payload, indent=1, sort_keys=True) + "\n", encoding="utf-8"
    )
    temporary.replace(path)


def resolve_character_slice(partition: dict, selectors: Sequence[str] | None,
                            npc_dir: Path | None = None) -> list[str]:
    """Selectors -> the model stems to bake, resolved against the declared partition.

    A bare stem is that model. `family:<name>` is every member of a model rig family.
    `bank:<name>` is every model that plays a bank in that bank family, which is the slice to take
    when a bank container changed -- the partition names bank membership and the manifest names
    who plays them, so it is the one selector that reads both.

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
                    for owner in record.get("clips", {}).values():
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


def sweep_characters(config, partition: dict, *, apply: bool = True,
                     force: bool = False) -> dict:
    """Remove baked character assets the declared partition no longer produces.

    Safe on a slice, because the partition it checks against always covers the whole cast: a run
    that baked two models still knows what the other 164 own. That is the property that makes a
    global sweep possible at all -- the flat `Meshes/`, `Materials/` and `Skeletons/` folders give
    no per-family answer.
    """
    from elysium_pipeline import character_sweep

    mount = config.repo_root / "Plugins" / "ElysiumBaked" / "Content"
    return character_sweep.sweep(
        mount, config.export_root / "npc", partition, apply=apply, force=force
    )


def _stale_garments(config, stems: Sequence[str]) -> list[str]:
    """Named stems whose cloth asset is older than an input it was generated from.

    Two inputs, not one. The garment sidecar is the authored payload, and `cloth_tuning.json` is
    every material and solver value applied on top of it -- so a tuning edit has to invalidate the
    whole corpus the same way re-exporting one model invalidates that one. Without it the fast
    path reports the assets current and the edit silently does nothing.

    Most stems author no garment and are absent from both sides, which is not staleness.
    """
    garment_dir = config.export_root / "npc" / "garment"
    cloth_dir = config.repo_root / "Content" / "VtMB" / "Cloth"
    tuning = config.repo_root / "pipeline" / "unreal" / "cloth_tuning.json"
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


def export_characters(
    config, runner, models: Sequence[str] | None = None, *, force: bool = False,
    sweep: bool = True, force_sweep: bool = False
) -> list[str]:
    """Bake characters onto /ElysiumBaked/Characters (ANM1) -- the whole cast unless told otherwise.

    Three stages. The `.eskm` containers are written from the user's own install, the rig
    partition is derived from them and written down, then a headless editor turns the pair into a
    shared skeleton per rig family, a mesh per model and a compressed sequence per clip. The
    manifest and the eye sidecars have to be on disk already, which `export bundle npc` or a
    complete profile writes. The policy content is a prerequisite too, because a body is built
    against the same master materials the runtime names.

    **The containers and the partition always cover the whole cast; only the BAKE is sliced.**
    Naming models bakes those models against the partition every other body already shares, so a
    slice can no longer rename a family out from under the meshes that point at it. Each container
    is individually skippable, so covering the cast costs seconds once it is current.

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

    manifest = Manifest(config.export_root / MANIFEST_FILE)
    write_character_sources(config, npc_dir, manifest=manifest, force=force)
    partition = write_character_partition(npc_dir)

    stems = resolve_character_slice(partition, models, npc_dir)
    if not stems:
        raise ValueError("no models named and the manifest lists no character")

    from elysium_pipeline import character_cache

    with (npc_dir / "npc_manifest.json").open(encoding="utf-8-sig") as handle:
        npc_manifest = json.load(handle)
    stale = character_cache.plan_stages(
        config, manifest, npc_dir, npc_manifest, partition, stems, force=force
    )
    todo = {scope: stages for scope, stages in stale.items() if stages}
    if not todo:
        # Nothing to author, so nothing to launch. This is the case the receipts exist for: the
        # editor costs 20-40s of process lifetime before it does any work at all.
        print(f"characters: {len(stems)} model(s) already current")
        # Garments are not covered by those receipts -- they are generated from the sidecar, not
        # from the bake's own inputs -- so a cast that is current can still be undressed. Compared
        # by file rather than by receipt so the fast path stays free when it is not.
        stale_garments = _stale_garments(config, stems)
        if stale_garments:
            unreal.make_cloth_assets(config, runner, stale_garments)
        return stems

    plan_path = npc_dir / ".elysium-character-plan.json"
    _write_json(plan_path, {"schema": "elysium.character-bake-plan", "version": 1,
                            "force": bool(force), "scopes": todo})
    print("characters: " + ", ".join(
        f"{scope}[{'+'.join(stages)}]" for scope, stages in sorted(todo.items())))

    ensure_policy_content(config, runner)

    # One process for the whole cast. What used to exhaust its address space was the DUPLICATION --
    # a bank rebuilt against every rig family that included it -- and the bank pass removes that at
    # the source: 7,871 distinct clips instead of ~90,000 assets.
    unreal.bake_characters(config, runner, stems, plan=plan_path)
    # After the bake, before the verify: the verifier walks the mount, and an orphan from a
    # partition that has since moved is exactly the thing it should not find.
    if sweep:
        result = sweep_characters(config, partition, force=force_sweep)
        if result["refused"]:
            raise ExportBakeFailure("character sweep refused: " + result["refused"])
        if result["removed"]:
            print(f"swept {result['removed']} orphaned character asset(s)")
    unreal.verify_characters(config, runner, stems)
    # The garments those bodies wear. After the verify, because a cloth asset resolves its bone
    # names against the mesh's reference skeleton -- the mesh has to be on the mount and sound
    # before a garment can bind to it. Most models author none and cost nothing here.
    unreal.make_cloth_assets(config, runner, stems)
    # Promoted only now: a commandlet can save part of a scope and then fail, and a receipt
    # written before the verifier agreed would make that partial scope look current forever.
    character_cache.record(manifest, config, npc_dir, npc_manifest, partition, todo)
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
