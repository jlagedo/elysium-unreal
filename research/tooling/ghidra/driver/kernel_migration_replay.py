"""Exact-precondition story-5 source replay; recipes and patches remain outside the repo.

    uv run elysium research kernel_migration_replay --root <isolated-checkout> \
        --recipe <work-root recipe.json> --out <work-root packet-dir> [--apply]

Each operation consumes the preceding operation's actual output. Reapplying is a precondition
failure. This supports exact replacements and unambiguous definition moves, not arbitrary C++.
"""
from __future__ import annotations

import argparse
import difflib
import hashlib
import json
from pathlib import Path

from elysium_pipeline.paths import repo_root, research_root
from kernel_migration_inventory import definitions, sha


class PreconditionError(ValueError):
    pass


def checked_path(root: Path, relative: str) -> Path:
    path = (root / relative).resolve()
    if not path.is_relative_to(root.resolve()) or not relative.startswith('Source/'):
        raise PreconditionError('replay may only change Source files inside its isolated root')
    return path


def replay(root: Path, recipe: dict) -> tuple[dict, dict, list[dict]]:
    before, current, receipts = {}, {}, []

    def read(relative):
        if relative not in current:
            path = checked_path(root, relative)
            text = path.read_text(encoding='utf-8-sig') if path.exists() else None
            before[relative] = text
            current[relative] = text
        return current[relative]

    for index, op in enumerate(recipe['operations']):
        path = op['path']
        text = read(path)
        kind = op['op']
        if kind == 'create':
            if text is not None:
                raise PreconditionError(f'{path}: creation requires an absent file')
            current[path] = op['text']
        elif kind == 'replace':
            count = op.get('expected_count', 1)
            if not isinstance(count, int) or count < 1 or text is None or text.count(op['before']) != count:
                raise PreconditionError(f'{path}: replacement precondition must occur exactly once '
                                        f'or the explicitly recorded count ({count})')
            current[path] = text.replace(op['before'], op['after'], count)
        elif kind == 'move-definition':
            if text is None:
                raise PreconditionError(f'{path}: missing source')
            matches = [d for d in definitions(text, path) if d.symbol == op['symbol']
                       and (not op.get('signature') or d.signature == op['signature'])]
            if len(matches) != 1:
                raise PreconditionError(f'{path}: {op["symbol"]} has {len(matches)} matching definitions')
            definition = matches[0]
            body = text[definition.start:definition.end]
            if op.get('sha256') and sha(body) != op['sha256']:
                raise PreconditionError(f'{path}: definition hash changed')
            transformed = body.replace(op['symbol'], op['to_symbol'], 1)
            for old, new in op.get('replacements', []):
                if transformed.count(old) != 1:
                    raise PreconditionError(f'{path}: moved-body replacement is not unique: {old}')
                transformed = transformed.replace(old, new, 1)
            destination = op['destination']
            target = read(destination)
            if target is None:
                raise PreconditionError('create destination header/includes before moving a definition')
            current[path] = text[:definition.start] + op.get('replacement', '') + text[definition.end:]
            current[destination] = target + '\n' + transformed + '\n'
        else:
            raise PreconditionError(f'unsupported operation {kind!r}')
        receipts.append({'index': index, 'op': kind, 'path': path,
                         'before_sha256': sha(text) if text is not None else None,
                         'after_sha256': sha(current[path]),
                         'symbol': op.get('symbol'), 'destination': op.get('destination')})
    return before, current, receipts


def unified(before: dict, after: dict) -> str:
    result = []
    for path in sorted(after):
        old, new = before[path] or '', after[path]
        for line in difflib.unified_diff(old.splitlines(True), new.splitlines(True),
                                        fromfile='a/' + path if before[path] is not None else '/dev/null',
                                        tofile='b/' + path):
            result.append(line if line.endswith('\n') else line + '\n\\ No newline at end of file\n')
    return ''.join(result)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root', type=Path, required=True)
    ap.add_argument('--recipe', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--apply', action='store_true')
    args = ap.parse_args(argv)
    root, out = args.root.resolve(), args.out.resolve()
    allowed = research_root().resolve() / 'npc-kernel/story-5'
    if root == repo_root().resolve() or not root.is_relative_to(allowed) or not out.is_relative_to(allowed):
        raise SystemExit('source replay requires an isolated story-5 checkout and external report directory')
    recipe = json.loads(args.recipe.read_text(encoding='utf-8-sig'))
    before, after, receipts = replay(root, recipe)
    patch = unified(before, after)
    out.mkdir(parents=True, exist_ok=True)
    (out / 'change.patch').write_text(patch, encoding='utf-8', newline='\n')
    record = {'packet': recipe['packet'], 'applied': args.apply,
              'recipe_sha256': hashlib.sha256(args.recipe.read_bytes()).hexdigest(),
              'patch_sha256': sha(patch), 'operations': receipts,
              'before': {p: sha(t) if t is not None else None for p, t in before.items()},
              'after': {p: sha(t) for p, t in after.items()}}
    if args.apply:
        for relative, text in after.items():
            path = checked_path(root, relative)
            # Check all originals again before writing the first file.
            actual = path.read_text(encoding='utf-8-sig') if path.exists() else None
            if actual != before[relative]:
                raise PreconditionError(f'{relative}: source changed during preparation')
        for relative, text in after.items():
            path = checked_path(root, relative)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding='utf-8', newline='\n')
    (out / 'receipt.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print(patch)
    print(f'{recipe["packet"]}: {len(receipts)} operations; {len(after)} files; applied={args.apply}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
