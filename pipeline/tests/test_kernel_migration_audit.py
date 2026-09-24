from pathlib import Path
import json
import sys
import subprocess
import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / 'research/tooling/ghidra/driver'))
from kernel_migration_audit import maker_shadows, refuse_shadowing
from kernel_migration_replay import PreconditionError, unified
import kernel_migration as km


def test_real_maker_duplicate_boundary_requires_all_seven_dispositions():
    shadows = maker_shadows(REPO)
    assert len(shadows) == 7 and 'bDisableAi' in shadows
    with pytest.raises(PreconditionError, match='bDisableAi'):
        refuse_shadowing(shadows, [s for s in shadows if s != 'bDisableAi'])
    refuse_shadowing(shadows, shadows)


def test_real_sibling_offsets_and_branch_signatures_remain_distinct():
    import csv
    layout = list(csv.DictReader((REPO/'docs/vtmb/npc-kernel/layout.tsv').open(encoding='utf-8'), delimiter='\t'))
    siblings = [r for r in layout if r['table'] in {'CNPC_VCop','CNPC_VHunter'}
                and int(r['offset'],16)==0x6664]
    assert len(siblings)==2
    assert len({(r['table'],r['offset'],r['member']) for r in siblings})==2
    signatures=list(csv.DictReader((REPO/'docs/vtmb/npc-kernel/signatures.tsv').open(encoding='utf-8'),delimiter='\t'))
    branch={r['class']:r for r in signatures if r['slot']=='617'}
    assert branch['CNPCMaker']['params']=='bool'
    assert branch['CNPC_VVampireBoss']['params']=='inputdata_t&'
    assert branch['CNPC_VBaseBoss']['params']=='Vector, bool, bool, Vector'


def test_unified_patch_round_trips_files_without_final_newline(tmp_path):
    before={'Source/a.cpp':'old', 'Source/b.cpp':None}
    after={'Source/a.cpp':'new', 'Source/b.cpp':'created'}
    (tmp_path/'Source').mkdir()
    (tmp_path/'Source/a.cpp').write_text('old',encoding='utf-8')
    patch=tmp_path/'change.patch'
    patch.write_text(unified(before,after),encoding='utf-8',newline='\n')
    result=subprocess.run(['git','apply',str(patch)],cwd=tmp_path,capture_output=True,text=True)
    assert result.returncode==0, result.stderr
    for name, expected in after.items():
        assert (tmp_path/name).read_text(encoding='utf-8')==expected


def test_reviewed_direct_edge_bypasses_troika_and_rat_edges_stay_virtual():
    decisions=json.loads((REPO/'docs/specs/0019-npc-kernel-rework/story-5/decisions.json').read_text())
    edge=next(e for e in decisions['call_edges'] if e['caller']=='103e1080')
    assert edge['callee']=='1027a530' and edge['kind']=='direct-tail'
    assert edge['required_call']=='FElysiumNpcBase::ShouldPlayFloatSound()'
    rat=[e for e in decisions['call_edges'] if e['caller'].startswith('103ad')]
    assert {e['slot'] for e in rat}=={368,369}
    assert all(e['kind']=='virtual' and e['receiver']=='this' for e in rat)


def test_packet_status_cannot_unlock_step0_without_evidence(tmp_path):
    story = REPO/'docs/specs/0019-npc-kernel-rework/story-5'
    for name in ('manifest.json','classes.tsv','factories.tsv'):
        (tmp_path/name).write_bytes((story/name).read_bytes())
    manifest=json.loads((tmp_path/'manifest.json').read_text(encoding='utf-8-sig'))
    manifest['packets']={name:'accepted' for name in manifest['packets']}
    (tmp_path/'manifest.json').write_text(json.dumps(manifest))
    with pytest.raises(FileNotFoundError):
        km.check_step0(tmp_path)
    (tmp_path/'acceptance.json').write_text(json.dumps({'scope':'step-0-only','production_migrations':[]}))
    with pytest.raises(km.InvalidManifest,match='pin the authored'):
        km.check_step0(tmp_path)
