"""Deploy whole live audio scripts in a lane separate from Python map scripts."""
from elysium_pipeline.exporters.sound_script_data_glb import (
    EXTENSION, FAMILY, SOURCE, source_path, manifest_children, wave_candidates, source_bindings,
)
from elysium_pipeline.importers import corpus_deploy
from elysium_pipeline.formats.unit_contract.origin import Origin
import json


def target_of(path: str) -> tuple[str, ...]:
    try:
        normalized = source_path(path)
    except ValueError as error:
        raise corpus_deploy.CorpusImportError(str(error)) from error
    if normalized != path:
        raise corpus_deploy.CorpusImportError(f"noncanonical sound script member {path!r}")
    return ("audio/" + path,)


def validate_closure(root, members):
    if SOURCE not in members:
        raise corpus_deploy.CorpusImportError("live sound manifest capsule is required")
    declarations = []
    expected = {SOURCE}
    for key, value in manifest_children(members[SOURCE]):
        accepted = key.lower() in ("precache_file", "declare_file")
        row = {"key": key, "path": value, "accepted": accepted,
               "precache": key.lower() == "precache_file"}
        if accepted:
            path = source_path(value)
            row.update(member=path, missing=path not in members)
            if path in members:
                expected.add(path)
        declarations.append(row)
    if root.get("declarations") != declarations:
        raise corpus_deploy.CorpusImportError("sound script declarations differ from raw first manifest root")
    if set(members) != expected:
        raise corpus_deploy.CorpusImportError("sound script capsules differ from manifest closure")
    if root.get('sourceBindings') != source_bindings(members):
        raise corpus_deploy.CorpusImportError('sound source bindings differ from raw capsules')
    if any(type(binding.get('byteLength')) is not int for binding in root['sourceBindings']):
        raise corpus_deploy.CorpusImportError('invalid sound source binding size type')
    coverage = root.get('availabilityCoverage')
    if (not isinstance(coverage, dict) or type(coverage.get('complete')) is not bool
            or not isinstance(coverage.get('issues'), list)
            or any(not isinstance(issue, str) for issue in coverage['issues'])
            or coverage['complete'] != (not coverage['issues'])
            or not isinstance(coverage.get('packs'), list) or not isinstance(coverage.get('looseRoots'), list)):
        raise corpus_deploy.CorpusImportError('invalid sound name coverage')
    rows = root.get('waveAvailability')
    candidates = wave_candidates(members)
    if not isinstance(rows, list) or len(rows) != len(candidates):
        raise corpus_deploy.CorpusImportError('sound wave candidate count differs from capsules')
    for row, candidate in zip(rows, candidates):
        if not isinstance(row, dict) or {key: row.get(key) for key in candidate} != candidate:
            raise corpus_deploy.CorpusImportError('sound wave candidate differs from capsules')
        if any(type(row.get(key)) is not int for key in ('entry', 'wave', 'gender')):
            raise corpus_deploy.CorpusImportError('invalid sound wave ordinal type')
        status = row.get('availability')
        if (status not in ('present', 'absent', 'unknown', 'nonfile')
                or (status == 'nonfile') != (candidate['filesystemPath'] is None)
                or (status == 'absent' and not coverage['complete'])):
            raise corpus_deploy.CorpusImportError('inconsistent sound wave availability')
        if status == 'present':
            origin = row.get('origin')
            if not isinstance(origin, dict) or origin.get('kind') not in ('loose', 'vpk'):
                raise corpus_deploy.CorpusImportError('invalid sound wave origin')
            try:
                captured = Origin(**origin)
                if captured.to_json() != origin:
                    raise ValueError('origin fields')
                if origin['kind'] == 'vpk' and any(type(origin[k]) is not int or origin[k] < 0 for k in ('offset', 'size')):
                    raise ValueError('origin extent')
            except (ValueError, TypeError) as error:
                raise corpus_deploy.CorpusImportError('invalid sound wave origin') from error
        elif 'origin' in row:
            raise corpus_deploy.CorpusImportError('non-present wave carries an origin')


def provenance(root):
    return {"audio/scripts/_source_provenance.json":
            (json.dumps({"sourceResolution": root["sourceResolution"],
                         "declarations": root["declarations"],
                         "sourceBindings": root["sourceBindings"],
                         "waveAvailability": root["waveAvailability"],
                         "availabilityCoverage": root["availabilityCoverage"]}, ensure_ascii=True) + "\n").encode()}


LANE_SPEC = corpus_deploy.Lane(
    name=FAMILY, recipe_version="elysium-sound-script-data-v2", families=((FAMILY, EXTENSION),),
    target_of=target_of, owned_directories=("audio/scripts",),
    empty_hint="run `uv run elysium export_v2 sound-script-data-glb` first",
    derived_of=provenance, capsule_guard=validate_closure,
)


def import_sound_script_data(export_v2_root, destination_root):
    return corpus_deploy.deploy_lane(export_v2_root, destination_root, LANE_SPEC)
