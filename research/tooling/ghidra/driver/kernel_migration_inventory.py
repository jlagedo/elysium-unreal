"""Story 5 inventory joined to the existing ledger; generated reports stay in the work root.

This is a source inventory, not a general C++ rewriter. Exact-span transformations use the
recorded signature, text and source hashes and refuse ambiguity.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from dataclasses import asdict, dataclass
import csv
import hashlib
import json
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_kernel_shape as gs
import kernel_ledger as kl
import kernel_migration as km
from elysium_pipeline.paths import repo_root, research_root


def sha(text: str) -> str:
    return hashlib.sha256(text.encode()).hexdigest()


def mask_cpp(text: str) -> str:
    """Keep offsets/newlines, mask comments and literals. Raw strings fail instead of guessing."""
    if re.search(r'\bR"', text):
        # Raw strings occur in test inputs, not method declarations; support their delimiters.
        pattern = r'R"([^ ()\\\t\r\n]{0,16})\(.*?\)\1"|//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    else:
        pattern = r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    return re.sub(pattern, lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]), text, flags=re.S)


def end_pair(mask: str, start: int, opening: str = "{", closing: str = "}") -> int:
    depth = 0
    for at in range(start, len(mask)):
        if mask[at] == opening:
            depth += 1
        elif mask[at] == closing:
            depth -= 1
            if depth == 0:
                return at + 1
    raise ValueError(f"unbalanced {opening} at {start}")


DEFINITION = re.compile(r"(?m)^[ \t]*(?P<ret>[A-Za-z_][\w:<>,*& \t]*?)\s+"
                        r"(?P<owner>FElysium\w+)::(?P<name>\w+)\s*\(")


@dataclass
class Definition:
    symbol: str
    signature: str
    path: str
    start: int
    end: int
    line: int
    text_sha256: str
    calls: list[str]
    includes: list[str]
    identifiers: list[str]
    leading_address: str
    cited_addresses: list[str]


# The generated slot surface: one file before story 5 step 5, one per layer after it.
GENERATED_SLOT_CPP = ('ElysiumNpcKernelSlots.cpp', 'ElysiumNpcBaseSlots.cpp', 'ElysiumNpcSlots.cpp',
                      'ElysiumEntitySlots.cpp', 'ElysiumAnimatingSlots.cpp', 'ElysiumAnimatingOverlaySlots.cpp',
                      'ElysiumFlexSlots.cpp', 'ElysiumCombatCharacterSlots.cpp')


def definitions(text: str, path: str) -> list[Definition]:
    mask = mask_cpp(text)
    includes = re.findall(r'^#include\s+"([^"]+)"', text, re.M)
    result = []
    for match in DEFINITION.finditer(mask):
        end_params = end_pair(mask, match.end() - 1, "(", ")")
        tail = re.match(r"\s*(?:const\s*)?(?:noexcept\s*)?\{", mask[end_params:])
        if not tail:
            continue
        brace = end_params + tail.end() - 1
        end = end_pair(mask, brace)
        signature = re.sub(r"\s+", " ", text[match.start():brace].strip())
        body = mask[brace:end]
        first_code = re.search(r'\S', mask[brace + 1:end - 1])
        lead_end = brace + 1 + first_code.start() if first_code else end
        lead = re.search(r'(?<![\w])(?:0x)?(10[0-9a-f]{6})(?![\w])', text[brace + 1:lead_end])
        if lead is None:
            prefix = []
            for line in reversed(text[:match.start()].splitlines()):
                stripped = line.strip()
                if not stripped or stripped.startswith(('//', '/*', '*')):
                    prefix.append(line)
                else:
                    break
            prefix_text = '\n'.join(reversed(prefix))
            if path.endswith(GENERATED_SLOT_CPP):
                slot_headers = list(re.finditer(r'// slot \d+ 0x(10[0-9a-f]{6})', prefix_text))
                lead = slot_headers[-1] if slot_headers else None
            else:
                lead = re.search(r'(?<![\w])(?:0x)?(10[0-9a-f]{6})(?![\w])', prefix_text)
        calls = sorted(set(re.findall(r"\b([A-Za-z_]\w*(?:::\w+)*)\s*\(", body)) -
                       {"if", "while", "for", "switch", "return", "sizeof", "TEXT"})
        result.append(Definition(match['owner'] + '::' + match['name'], signature, path,
                                 match.start(), end, text.count('\n', 0, match.start()) + 1,
                                 sha(text[match.start():end]), calls, includes,
                                 sorted(set(re.findall(r"\b[A-Za-z_]\w*\b", body))),
                                 lead[1] if lead else '',
                                 sorted(set(re.findall(r'0x(10[0-9a-f]{6})', text[match.start():end])))))
    return result


def source_inventory(root: Path) -> tuple[list[Definition], list[dict], list[dict], dict]:
    defs, sites, fixtures, sources = [], [], [], {}
    pattern = re.compile(r"SetRetailClassForTests|\b(?:RetailClass|IsRetailClass|SpeciesDispatchRow|"
                         r"SpeciesSlotRows|IsSlotDispatching|Lookup\w*Override|OfClassname|OfClass|"
                         r"FindClass|IsA|FSpeciesDispatchScope)\s*\(|&FElysiumNpc::FUN_[0-9a-f]{8}|"
                         r"ElysiumNpcKernelClass::\w+\s*\(|IsSpeciesDispatching\s*\(|AsNpc\s*\(")
    for path in sorted((root / 'Source/ElysiumUE').rglob('*')):
        if path.suffix not in {'.h', '.cpp', '.inl'}:
            continue
        text = path.read_text(encoding='utf-8-sig')
        rel = path.relative_to(root).as_posix()
        if 'Npc' not in path.name and not re.search(r'FElysiumNpc|RetailClass|SetRetailClassForTests|AsNpc\s*\(', text):
            continue
        sources[rel] = sha(text)
        local = definitions(text, rel)
        defs.extend(local)
        test_names = list(re.finditer(r'IMPLEMENT_(?:SIMPLE|COMPLEX)_AUTOMATION_TEST\(\s*(\w+)\s*,\s*"([^"]+)"', text))
        for idx, test in enumerate(test_names):
            stop = test_names[idx + 1].start() if idx + 1 < len(test_names) else len(text)
            fragment = text[test.end():stop]
            classes = sorted(set(re.findall(r'TEXT\("((?:CNPC|CAI|CCine|CGeneric|CScripted|CPayphone)[^" ]*)"\)', fragment)))
            reclasses = re.findall(r'SetRetailClassForTests\(.*?TEXT\("([^"]+)"\)', fragment)
            reclass_sites = len(re.findall(r'SetRetailClassForTests\s*\(', fragment))
            loop_reclass = reclass_sites > 0 and bool(re.search(r'\b(?:for|while)\s*\(', mask_cpp(fragment)))
            kinds = []
            if re.search(r'FUN_[0-9a-f]{8}\s*\(|BaseShouldPlay|Base\w+\(', fragment):
                kinds.append('direct-body')
            if re.search(r'SetRetailClassForTests|->Slot\d+|\.Slot\d+|RetailClass|Lookup.*Override', fragment):
                kinds.append('virtual-dispatch')
            if re.search(r'Spawn|Create|Restore|Serialize|Think\(|Tick\(|DeliverInput|Dispatch', fragment):
                kinds.append('lifecycle/integration')
            if 'CAI_TestHull' in fragment:
                kinds.append('internal-construction')
            if not kinds:
                kinds = ['direct-body']
            fixtures.append({'test': test[2], 'symbol': test[1], 'path': rel,
                             'line': text.count('\n', 0, test.start()) + 1,
                             'sha256': sha(text[test.start():stop]), 'kinds': kinds,
                             'retail_classes': classes, 'reclass_calls': reclasses,
                             'reclass_sites': reclass_sites, 'dynamic_reclass': reclass_sites > len(reclasses),
                             'loop_reclass_candidate': loop_reclass,
                             'multiple_reclass': len(reclasses) > 1 or reclass_sites > 1 or loop_reclass,
                             'slots': sorted(set(re.findall(r'\bSlot(\d+)', fragment)), key=int),
                             'disposition': 'retain; migrate fixture in owner packet; never auto-delete'})
        mask = mask_cpp(text)
        for match in pattern.finditer(mask):
            line = text.count('\n', 0, match.start()) + 1
            source_line = text.splitlines()[line - 1].strip()
            enclosing = next((d for d in local if d.start <= match.start() < d.end), None)
            owner = enclosing.symbol if enclosing else '<table/header>'
            if owner == '<table/header>':
                active_test = next((test[2] for i, test in enumerate(test_names)
                                    if test.start() <= match.start() < (test_names[i+1].start()
                                        if i+1 < len(test_names) else len(text))), None)
                owner = active_test or f'declaration/table in {rel}:{line}'
            token = match[0].strip()
            kind = ('participation' if token.startswith('AsNpc') else
                    'fixture-reclass' if 'SetRetailClassForTests' in token else
                    'type-test' if token.startswith(('IsA(', 'IsRetailClass')) else
                    'dispatch' if re.search(r'Dispatch|Override|BodyOf|&FElysiumNpc', token) else 'data-query')
            sites.append({'id': f'{rel}:{line}:{match.start()}', 'path': rel, 'line': line,
                          'consumer': owner, 'kind': kind, 'source': source_line, 'sha256': sha(source_line),
                          'mentioned_classes': sorted(set(re.findall(r'\b(?:CNPC\w+|CAI_\w+|CCine\w+)\b',
                              text[enclosing.start:enclosing.end] if enclosing else source_line))),
                          'removal_step': 11, 'disposition': 'retain until the owning class/fixture folds; replace dispatch with overrides, preserve genuine queries'})
    return defs, sites, fixtures, sources


def build(root: Path) -> dict:
    manifest, classes, factories = km.load(root / km.STORY)
    clsmap = {row['retail_class']: row for row in classes}
    ledger = kl.Ledger(kl.MODULE, kl.DEFAULT_DEPTH, root)
    ledger.load()
    print('ledger loaded; scanning port definitions', flush=True)
    defs, sites, fixtures, sources = source_inventory(root)
    for site in sites:
        deferred = [name for name in site['mentioned_classes'] if name in clsmap
                    and clsmap[name]['step'] in {'7', '8', '9', '10'}]
        site['deferred_consumers'] = deferred
        site['removal_step'] = max((int(clsmap[name]['step']) for name in deferred), default=11)
        if site['kind'] == 'participation':
            site['disposition'] = 'preserve type-admission consumer; audit new participants at folds 7/8/9/10'
        elif site['kind'] == 'data-query':
            site['disposition'] = 'retain metadata query until documented final non-dispatch consumer review'
    print(f'source scan: {len(defs)} definitions, {len(sites)} sites, {len(fixtures)} fixtures', flush=True)
    by_symbol = defaultdict(list)
    by_lead = defaultdict(list)
    for definition in defs:
        if '/Tests/' in definition.path:
            continue  # a test citing a retail body is coverage, never its implementation
        by_symbol[definition.symbol].append(definition)
        if definition.leading_address:
            kind, located = ledger.locate(definition.leading_address)
            if kind in {'function', 'inside'}:
                by_lead[ledger.resolve(located)].append(definition)
    signatures = list(csv.DictReader((root / 'docs/vtmb/npc-kernel/signatures.tsv').open(encoding='utf-8'), delimiter='\t'))
    sigs = {(int(row['slot']), row.get('class', row.get('cls', ''))): row for row in signatures}
    datamaps = json.loads((research_root() / 'ghidra/types/datamap_records-vampire.dll.json').read_text(encoding='utf-8'))
    bodies = []
    addresses = set(ledger.verdicts) | set(by_lead)
    identities = defaultdict(list)
    for slot, receivers in ledger.slot_bodies.items():
        for cls, address in receivers.items():
            if cls in clsmap:
                identities[address].append((cls, slot))
    decisions = json.loads((root / km.STORY / 'decisions.json').read_text(encoding='utf-8'))
    resolutions = {addr: row['current_symbol'] for addr, row in decisions['body_resolutions'].items()}
    def slot_contract(cls, slot):
        owner, seen = cls, set()
        while owner and owner not in seen:
            seen.add(owner)
            if (slot, owner) in sigs:
                signature = sigs[slot, owner]
                return {'family': owner, 'signature': signature}
            owner = ledger.bases.get(owner, '')
        signature = sigs.get((slot, ''))
        return {'family': 'CAI_BaseNPC' if slot < 583 else 'CAI_BaseNPCTroika',
                'signature': signature, 'note': 'common NPC-line contract; inherited entity owner retained in ledger'}
    for address in sorted(addresses | set(identities)):
        fn = ledger.functions.get(address)
        verdict = ledger.verdicts.get(address)
        target = verdict.target if verdict else '-'
        symbol = resolutions.get(address, target.removeprefix('hand:'))
        found = by_symbol.get(symbol, [])
        addressed = by_symbol.get('FElysiumNpc::FUN_' + address, [])
        resolution = 'overlay-surface; not proof of implemented retail body'
        if by_lead.get(address):
            found = by_lead[address]
            symbol = ' | '.join(d.symbol for d in found)
            resolution = 'leading body citation; review required before moving'
        if addressed:
            symbol, found = 'FElysiumNpc::FUN_' + address, addressed
            resolution = 'address-named definition'
        if address in resolutions:
            symbol = resolutions[address]
            found = by_symbol[symbol]
            resolution = 'reviewed identity; see authored decisions'
        receivers = identities[address]
        live = sorted({cls for cls, _ in receivers if clsmap[cls]['liveness'] == 'live'})
        owner = fn.ns if fn else ''
        candidate = bool(verdict and verdict.verdict == 'dead' and found and not live)
        step1_scope = bool(verdict and 'dead=class ' in verdict.evidence and 'no instance' in verdict.evidence)
        bodies.append({'module': kl.MODULE, 'address': address, 'retail_owner': owner,
                       'verdict': verdict.verdict if verdict else 'unsettled', 'overlay_target': target,
                       'evidence': verdict.evidence if verdict else 'unsettled ledger row',
                       'step1_scope': step1_scope,
                       'actual_symbol': symbol, 'definitions': [asdict(d) for d in found],
                       'resolution': resolution,
                       'slot_receivers': [{'class': cls, 'slot': slot, **slot_contract(cls, slot)} for cls, slot in sorted(receivers)],
                       'live_receivers': live, 'final_owner': clsmap.get(owner, {}).get('port_class', ''),
                       'phase0_disposition': 'retain for scoped step-1 deletion review' if candidate and step1_scope else 'retain current definition/registry/seam; not authorized for step-1 deletion',
                       'dead_candidate': candidate})
    # All identities targeting the same actual port body must agree before a deletion is proposed.
    target_rows = defaultdict(list)
    for row in bodies:
        for definition in row['definitions']:
            target_rows[(definition['path'], definition['start'])].append(row)
    deletions = []
    incoming = defaultdict(set)
    for caller, edges in ledger.edges.items():
        for callee, kind in edges:
            if kind == 'direct':
                incoming[ledger.resolve(callee)].add(ledger.resolve(caller))
    dead_factories = {r['factory'] for r in factories if clsmap[r['retail_class']]['liveness'] != 'live'}
    for key, rows in target_rows.items():
        if not any(r['dead_candidate'] for r in rows):
            continue
        definition = next(d for d in rows[0]['definitions'] if (d['path'], d['start']) == key)
        blocked = [r['address'] for r in rows if r['verdict'] != 'dead' or r['live_receivers']]
        caller_rows = []
        for row in rows:
            for caller in sorted(incoming[row['address']]):
                cv = ledger.verdicts.get(caller)
                caller_rows.append({'callee': row['address'], 'caller': caller,
                                    'verdict': cv.verdict if cv else 'unsettled',
                                    'dead_factory': caller in dead_factories})
        caller_blocks = [r for r in caller_rows if r['verdict'] != 'dead' and not r['dead_factory']]
        deletions.append({'definition': definition, 'addresses': [r['address'] for r in rows],
                          'step1_scope': any(r['step1_scope'] for r in rows),
                          'shared_live_blockers': blocked,
                          'direct_callers': caller_rows, 'caller_blocks': caller_blocks,
                          'identity_blocks': [r['address'] for r in rows if len(r['definitions']) != 1
                                              or r['resolution'].startswith('overlay-surface')],
                          'disposition': 'keep-outside-step1' if not any(r['step1_scope'] for r in rows)
                          else 'keep-shared-live' if blocked else 'keep-unsettled-caller' if caller_blocks
                          else 'keep-ambiguous-identity' if any(len(r['definitions']) != 1 or
                               r['resolution'].startswith('overlay-surface') for r in rows)
                          else 'dead-only-definition; exact fixture assertions still require consuming-packet review'})
    layout = list(csv.DictReader((root / 'docs/vtmb/npc-kernel/layout.tsv').open(encoding='utf-8'), delimiter='\t'))
    fields = []
    shape_map = (root / 'Source/ElysiumUE/Private/Substrate/ElysiumNpcKernelShapeMap.cpp').read_text(encoding='utf-8')
    bindings = {int(m[2], 16): {'form': m[1] or 'WORD', 'arguments': m[3].strip()}
                for m in re.finditer(r'ELYSIUM_NPC_WORD(_NOTED|_PRIVATE|_CHAIN|_IMPLICIT|_ABSENT)?\(\s*(0x[0-9a-f]+)\s*,\s*([^\n]+)', shape_map)}
    for row in layout:
        if any(c in row['member'] for c in '.[') or '+' in row['member']:
            continue
        owner = row['layer'] if row['table'] == 'CAI_BaseNPCTroika' else row['table']
        if owner not in clsmap:
            continue
        records = [record for record in datamaps.get(owner, {}).get('records', [])
                   if record.get('offset') == int(row['offset'], 16) and record.get('name') == row['member']]
        raw_flags = records[0]['flags'] if len(records) == 1 else None
        flag_names = records[0].get('flagNames', []) if len(records) == 1 else []
        binding = bindings.get(int(row['offset'], 16)) if row['table'] == 'CAI_BaseNPCTroika' else None
        final_owner = clsmap[owner]['port_class']
        current_path = binding['arguments'] if binding else 'unresolved current storage; no move authorized'
        final_path = (final_owner + '::' + row['member']) if final_owner != '-' else 'dead census only; no final port storage'
        if binding and binding['arguments'].startswith('FElysiumNpcScheduleHost,'):
            component = 'BaseScheduleHost' if owner == 'CAI_BaseNPC' else 'ScheduleHost'
            member = binding['arguments'].split(',')[1].strip().rstrip('),')
            final_path = final_owner + '::' + component + '.' + member
        fields.append({**row, 'declaring_class': owner, 'final_owner': final_owner,
                       'current_path': current_path, 'planned_path': final_path,
                       'path_status': 'reviewed aggregate partition' if 'ScheduleHost.' in final_path else
                           'owner decision; exact member move requires source precondition',
                       'binding': binding,
                       'raw_flags': raw_flags, 'flag_names': flag_names,
                       'phase0_disposition': 'retain current aggregate/seam; class-qualified identity required before move',
                       'binding_policy': 'inherited lookup; no sibling offset merge'})
    live_rules = [{'module': r['module'], 'address': r['address'], 'receiver': receiver['class'],
                   'slot': receiver['slot'], 'family': receiver['family'],
                   'implementation_evidence': r['resolution']}
                  for r in bodies if r['verdict'] == 'rule'
                  for receiver in r['slot_receivers'] if receiver['class'] in r['live_receivers']]
    # Helpers and unresolved rule owners must not disappear just because there is no live slot
    # receiver to attach. Retain every rule address as well as receiver-specific contracts.
    virtual_rule_addresses = {r['address'] for r in live_rules}
    live_rules += [{'module': r['module'], 'address': r['address'],
                    'receiver': r['retail_owner'] or 'unresolved-helper', 'slot': -1,
                    'family': 'helper-or-unresolved-rule', 'implementation_evidence': r['resolution']}
                   for r in bodies if r['verdict'] == 'rule' and r['address'] not in virtual_rule_addresses]
    return {'schema_version': 1, 'phase': 0, 'source_hashes': sources, 'bodies': bodies,
            'fields': fields, 'sites': sites, 'fixtures': fixtures, 'deletion_candidates': deletions,
            'signatures': signatures, 'live_rule_inventory': live_rules,
            'outside_census_layout': [r for r in layout if r['table'] == 'CAI_BaseActor'
                                     and not any(c in r['member'] for c in '.[') and '+' not in r['member']],
            'summary': {'definitions': len(defs), 'bodies': len(bodies), 'fields': len(fields),
                        'generated_slot_definitions': sum(d.path.endswith(GENERATED_SLOT_CPP) for d in defs),
                        'generated_slot_stubs': sum(d.path.endswith(GENERATED_SLOT_CPP)
                                                    and {'FireKernelSlot', 'FireKernelBaseSlot'} & set(d.calls)
                                                    for d in defs),
                        'base_fields': sum(f['declaring_class'] == 'CAI_BaseNPC' for f in fields),
                        'troika_fields': sum(f['declaring_class'] == 'CAI_BaseNPCTroika' for f in fields),
                        'live_species_fields': sum(f['declaring_class'] not in {'CAI_BaseNPC','CAI_BaseNPCTroika'} and clsmap[f['declaring_class']]['liveness']=='live' for f in fields),
                        'sites': len(sites), 'fixtures': len(fixtures), 'deletion_candidates': len(deletions),
                        'shared_live_deletion_blocks': sum(bool(r['shared_live_blockers']) for r in deletions),
                        'unsettled_caller_deletion_blocks': sum(bool(r['caller_blocks']) for r in deletions),
                        'step1_retail_rows': sum(r['step1_scope'] for r in bodies),
                        'outside_census_ancestor_words': 12,
                        'live_rule_contracts_preserved': len(live_rules),
                        'site_kinds': dict(Counter(s['kind'] for s in sites)),
                        'unresolved_target_definitions': sum('::' in r['actual_symbol'] and not r['definitions'] for r in bodies)}}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args(argv)
    out = args.out.resolve()
    if not out.is_relative_to(research_root().resolve()):
        raise SystemExit('reports must stay in work research root')
    report = build(repo_root())
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report['summary'], indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
