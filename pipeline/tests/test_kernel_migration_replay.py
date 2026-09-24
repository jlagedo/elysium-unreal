from pathlib import Path
import sys
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'research/tooling/ghidra/driver'))
from kernel_migration_inventory import definitions
from kernel_migration_replay import replay, PreconditionError


def test_definition_scanner_masks_literals_and_distinguishes_overloads():
    text = '// void FElysiumNpc::Fake() {}\n' + '''
void FElysiumNpc::Call(int X) { const char* S = "}"; if (X) { Other(); } }
void FElysiumNpc::Call(float X) { /* } */ Other(); }
'''
    defs = definitions(text, 'Source/test.cpp')
    assert len(defs) == 2
    assert defs[0].signature != defs[1].signature
    assert text[defs[0].start:defs[0].end].endswith('} }')


def test_replay_composes_and_refuses_reapplication(tmp_path):
    source = tmp_path / 'Source/a.cpp'
    source.parent.mkdir()
    source.write_text('void FElysiumNpc::Call(int X) { Use(X); }\n')
    recipe = {'operations': [
        {'op': 'replace', 'path': 'Source/a.cpp', 'before': 'Use(X)', 'after': 'Use(X + 1)'},
        {'op': 'create', 'path': 'Source/b.cpp', 'text': '// destination\n'},
        {'op': 'move-definition', 'path': 'Source/a.cpp', 'symbol': 'FElysiumNpc::Call',
         'signature': 'void FElysiumNpc::Call(int X)', 'destination': 'Source/b.cpp',
         'to_symbol': 'FElysiumNpcCop::Call'}]}
    before, after, receipts = replay(tmp_path, recipe)
    assert 'Use(X + 1)' in after['Source/b.cpp']
    assert 'FElysiumNpcCop::Call' in after['Source/b.cpp']
    assert 'Call' not in after['Source/a.cpp']
    assert len(receipts) == 3
    for name, text in after.items():
        (tmp_path / name).write_text(text)
    with pytest.raises(PreconditionError):
        replay(tmp_path, recipe)


def test_overloads_and_duplicate_preconditions_fail_closed(tmp_path):
    source = tmp_path / 'Source/a.cpp'
    source.parent.mkdir()
    source.write_text('void FElysiumNpc::F(int X) {}\nvoid FElysiumNpc::F(float X) {}\n')
    with pytest.raises(PreconditionError, match='2 matching'):
        replay(tmp_path, {'operations': [{'op': 'move-definition', 'path': 'Source/a.cpp',
            'symbol': 'FElysiumNpc::F', 'destination': 'Source/b.cpp', 'to_symbol': 'Other::F'}]})
    with pytest.raises(PreconditionError, match='exactly once'):
        replay(tmp_path, {'operations': [{'op': 'replace', 'path': 'Source/a.cpp',
                                         'before': '{}', 'after': '{ Fail(); }'}]})
