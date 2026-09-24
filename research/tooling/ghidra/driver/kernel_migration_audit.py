"""Validate the bounded step-0 inventory, storage hazards and composed rehearsal receipts."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re

import kernel_migration as km
from kernel_migration_inventory import sha
from kernel_migration_replay import replay, unified, PreconditionError
from elysium_pipeline.paths import repo_root, research_root


def file_sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def maker_shadows(root: Path) -> list[str]:
    parent = (root / 'Source/ElysiumUE/Private/Substrate/ElysiumNpc.h').read_text(encoding='utf-8')
    for path in sorted((root / 'Source/ElysiumUE/Private/Substrate').glob('ElysiumNpcKernel*.inl')):
        parent += '\n' + path.read_text(encoding='utf-8')
    child = (root / 'Source/ElysiumUE/Private/Substrate/ElysiumNpcMaker.h').read_text(encoding='utf-8')
    fields = {'RetailSolidType', 'PlInvestigate', 'PlCriminalFlee', 'PlCriminalAttack',
              'PlSupernaturalFlee', 'PlSupernaturalAttack', 'bDisableAi'}
    def declares(text, name):
        return bool(re.search(r'(?m)^\s*(?:int32|bool)\s+' + name + r'\s*=', text))
    return sorted(name for name in fields if declares(parent, name) and declares(child, name))


def refuse_shadowing(shadows: list[str], retired: list[str]) -> None:
    left = set(shadows) - set(retired)
    if left:
        raise PreconditionError('maker fold would retain duplicate inherited state: ' + ', '.join(sorted(left)))


def check_inventory(report: dict, root: Path) -> dict:
    for relative, digest in report['source_hashes'].items():
        if sha((root / relative).read_text(encoding='utf-8-sig')) != digest:
            raise km.InvalidManifest(f'inventory source changed: {relative}')
    identities = set()
    for row in report['fields']:
        key = ('vampire.dll', row['declaring_class'], int(row['offset'], 16), row['member'])
        if key in identities or not row['phase0_disposition'] or not row['binding_policy']:
            raise km.InvalidManifest(f'duplicate/undisposed field: {key}')
        identities.add(key)
    tests = set()
    for row in report['fixtures']:
        if row['test'] in tests or not row['kinds'] or not row['disposition']:
            raise km.InvalidManifest('duplicate/unclassified fixture')
        tests.add(row['test'])
    sites = set()
    for row in report['sites']:
        if row['id'] in sites or not row['consumer'] or not row['disposition']:
            raise km.InvalidManifest('duplicate/ownerless compatibility site')
        sites.add(row['id'])
    for row in report['bodies']:
        if not row['phase0_disposition']:
            raise km.InvalidManifest('body without a phase disposition')
        for receiver in row['slot_receivers']:
            if not receiver['signature'] or not receiver['family']:
                raise km.InvalidManifest(f'lost slot contract: {row["address"]} {receiver}')
    for candidate in report['deletion_candidates']:
        if candidate['disposition'].startswith('dead-only') and (
                candidate['shared_live_blockers'] or candidate['caller_blocks'] or candidate['identity_blocks']):
            raise km.InvalidManifest('shared/live/unsettled caller authorized for deletion')
    rules = [(r['module'], r['address'], r['receiver'], r['slot'], r['family'])
             for r in report['live_rule_inventory']]
    if len(rules) != len(set(rules)):
        raise km.InvalidManifest('duplicate rule identity')
    return {'fields': len(identities), 'fixtures': len(tests), 'sites': len(sites),
            'bodies': len(report['bodies']), 'rule_contracts': len(rules),
            'rule_identity_sha256': sha(json.dumps(sorted(rules), separators=(',', ':')))}


def verify_series(source: Path, series: list[dict], output: Path) -> dict:
    """Replay every packet's real predecessor, prove patch identity and refusal on reapplication."""
    source_only = output / 'source-replay'
    if source_only.exists():
        raise PreconditionError('verification output already exists; use a fresh report directory')
    paths = set()
    for stage in series:
        recipe = json.loads(Path(stage['recipe']).read_text(encoding='utf-8-sig'))
        for op in recipe['operations']:
            paths.add(op['path'])
            if op.get('destination'):
                paths.add(op['destination'])
    for relative in paths:
        src, dest = source / relative, source_only / relative
        if src.is_file():
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(src.read_bytes())
    results = []
    for stage in series:
        recipe_path = Path(stage['recipe'])
        recipe = json.loads(recipe_path.read_text(encoding='utf-8-sig'))
        receipt_path = Path(stage['receipt'])
        receipt = json.loads(receipt_path.read_text(encoding='utf-8'))
        before, after, operations = replay(source_only, recipe)
        if {p: sha(t) if t is not None else None for p, t in before.items()} != receipt['before']:
            raise PreconditionError(f'{recipe["packet"]}: predecessor differs from applied source')
        if {p: sha(t) for p, t in after.items()} != receipt['after']:
            raise PreconditionError(f'{recipe["packet"]}: replay result differs')
        patch = unified(before, after)
        # Refresh only patch formatting (EOF-newline markers); source hashes must be identical.
        receipt_path.with_name('change.patch').write_text(patch, encoding='utf-8', newline='\n')
        receipt['patch_sha256'] = sha(patch)
        receipt_path.write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
        for relative, text in after.items():
            dest = source_only / relative
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_text(text, encoding='utf-8', newline='\n')
        refused = False
        try:
            replay(source_only, recipe)
        except PreconditionError:
            refused = True
        if not refused:
            raise PreconditionError(f'{recipe["packet"]}: completed packet silently reapplies')
        results.append({'packet': recipe['packet'], 'operations': len(operations),
                        'recipe_sha256': file_sha(recipe_path), 'receipt_sha256': file_sha(receipt_path),
                        'patch_sha256': sha(patch), 'reapplication_refused': refused})
    return {'stages': results, 'files': {p: sha((source_only / p).read_text(encoding='utf-8'))
                                        for p in paths if (source_only / p).is_file()}}


def rulebook_fact(root: Path) -> dict:
    from elysium_pipeline.formats import install
    key = 'vdata/system/rules_tables.txt'
    index = install.build_index(dirs=('vdata',), verbose=False)
    data = install.read(index, key)
    deployed = root / 'Content/ElysiumCorpus' / key
    if data is None or data != deployed.read_bytes():
        raise km.InvalidManifest('deployed rulebook does not match the install winner')
    text = data.decode('utf-8-sig')
    block = re.search(r'"InternalName"\s+"Float_Sound_Info"(.*?)\n\s*}', text, re.S)
    if not block or not re.search(r'"3"\s+"250\.0"', block[1]):
        raise km.InvalidManifest('pinned Zombie distance row changed')
    return {'path': key, 'sha256': hashlib.sha256(data).hexdigest(),
            'origin': repr(index[key]), 'table': 'Float_Sound_Info', 'row': 3, 'value': 250.0,
            'evidence': ['vampire.dll:103e11b8 PUSH 3', 'vampire.dll:1006caa0 table float accessor'],
            'deployed_matches_install': True}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--inventory', type=Path, required=True)
    ap.add_argument('--series', type=Path)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args(argv)
    root, out = repo_root(), args.out.resolve()
    if not out.is_relative_to((research_root() / 'npc-kernel/story-5').resolve()):
        raise SystemExit('audit output must remain in the work root')
    report = json.loads(args.inventory.read_text(encoding='utf-8'))
    result = {'inventory': check_inventory(report, root), 'rulebook': rulebook_fact(root),
              'maker_shadow_fields': maker_shadows(root)}
    if len(result['maker_shadow_fields']) != 7:
        raise km.InvalidManifest('maker duplicate-state boundary differs from reviewed seven fields')
    try:
        refuse_shadowing(result['maker_shadow_fields'], [])
    except PreconditionError as exc:
        result['maker_naive_fold_refused'] = str(exc)
    else:
        raise km.InvalidManifest('maker shadow hazard was not refused')
    if args.series:
        result['replay'] = verify_series(root, json.loads(args.series.read_text(encoding='utf-8-sig')), out)
    out.mkdir(parents=True, exist_ok=True)
    (out / 'audit.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: v for k, v in result.items() if k != 'replay'}, indent=2))
    if 'replay' in result:
        print(f"{len(result['replay']['stages'])} composed stages verified; repeated application refused")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
