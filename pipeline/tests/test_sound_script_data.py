"""Whole selected source capsules and the independent audio script deploy lane."""
import hashlib
import pytest

from elysium_pipeline.exporters import sound_script_data_glb as exporter
from elysium_pipeline.importers import sound_script_data as importer
from elysium_pipeline.formats.unit_contract import read_glb, write_glb, source_capsules, verify_ledger_row


def publish(tmp_path, manifest, tables):
    data = {exporter.SOURCE: manifest, **tables}
    index = {key: ("loose", "C:/game/Unofficial_Patch/" + key) for key in data}
    return exporter.export_table(tmp_path / "units", index=index,
                                 read_bytes=lambda index, key: data.get(key),
                                 name_coverage=({}, {'complete': True, 'issues': [], 'packs': [], 'looseRoots': []}))


def test_whole_sources_duplicate_order_missing_and_readback(tmp_path):
    body = b'// raw\r\nA { volume "0.6,0.4" wave first wave second }\r\na { wave rejected }\r\n'
    other = b'A { wave acrossfile } Other { wave third }\n'
    manifest = (b'manifest { PreCache_File scripts/a.txt declare_file scripts/b.txt '
                b'precache_file scripts/a.txt precache_file scripts/missing.txt unknown ignored }\r\n')
    path = publish(tmp_path, manifest, {"scripts/a.txt": body, "scripts/b.txt": other})
    doc, payload = read_glb(path)
    root = doc["extensions"][exporter.EXTENSION]
    assert source_capsules(doc, payload, root) == {
        exporter.SOURCE: manifest, "scripts/a.txt": body, "scripts/b.txt": other}
    assert [d["precache"] for d in root["declarations"]] == [True, False, True, True, False]
    assert root["declarations"][3]["missing"]
    for member, ledger in zip(root["sourceResolution"]["members"], root["coverage"]["byteLedger"]):
        raw = source_capsules(doc, payload, root)[member["path"]]
        assert member["sha256"] == hashlib.sha256(raw).hexdigest()
        assert "Unofficial_Patch" in str(member["origin"])
        verify_ledger_row(ledger, raw)
    corpus = tmp_path / "corpus"
    (corpus / "scripts").mkdir(parents=True)
    (corpus / "scripts/python.py").write_bytes(b"python owned")
    result = importer.import_sound_script_data(tmp_path / "units", corpus)
    assert not result.failures
    assert (corpus / "audio/scripts/a.txt").read_bytes() == body
    assert (corpus / "audio/scripts/b.txt").read_bytes() == other
    assert (corpus / "scripts/python.py").read_bytes() == b"python owned"
    assert importer.import_sound_script_data(tmp_path / "units", corpus).units_current == 1
    path.write_bytes(b"corrupt glb")
    failed = importer.import_sound_script_data(tmp_path / "units", corpus)
    assert failed.failures
    assert (corpus / "audio/scripts/a.txt").read_bytes() == body


def test_required_manifest_and_source_paths(tmp_path):
    with pytest.raises(ValueError, match="missing required"):
        exporter.export_table(tmp_path, index={}, read_bytes=lambda i, k: None)
    for path in ("scripts/../bad", "/scripts/a", "scripts/C:/bad", "scripts//a", "scripts/./a", "other/a"):
        with pytest.raises(ValueError):
            exporter.source_path(path)
    with pytest.raises(ValueError):
        publish(tmp_path, b'manifest { precache_file "scripts/../bad" }', {})


def test_lane_prunes_only_its_owned_audio_scripts(tmp_path):
    publish(tmp_path, b'm { declare_file scripts/a.txt }', {"scripts/a.txt": b'A { wave a }'})
    corpus = tmp_path / "corpus"
    importer.import_sound_script_data(tmp_path / "units", corpus)
    (corpus / "audio/other").mkdir()
    (corpus / "audio/other/preserved").write_bytes(b"other lane")
    publish(tmp_path, b'm { declare_file scripts/b.txt }', {"scripts/b.txt": b'B { wave b }'})
    assert not importer.import_sound_script_data(tmp_path / "units", corpus).failures
    assert not (corpus / "audio/scripts/a.txt").exists()
    assert (corpus / "audio/other/preserved").exists()


def test_manifest_first_root_and_settled_permissive_grammar(tmp_path):
    manifest = (b'/* comment */ first "{prefix" PreCache_File scripts/a.txt '
                b'"}" later { precache_file scripts/ignored.txt }')
    path = publish(tmp_path, manifest, {"scripts/a.txt": b'A { wave a }',
                                        "scripts/ignored.txt": b'Ignored { wave never }'})
    doc, payload = read_glb(path)
    root = doc["extensions"][exporter.EXTENSION]
    assert [row["path"] for row in root["declarations"]] == ["scripts/a.txt"]
    assert set(source_capsules(doc, payload, root)) == {exporter.SOURCE, "scripts/a.txt"}
    for text in (b'', b'// comment', b'first dropped later { precache_file scripts/a.txt }'):
        assert exporter.manifest_children(text) == []
        path = publish(tmp_path, text, {"scripts/a.txt": b'A {}'})
        doc, payload = read_glb(path)
        root = doc["extensions"][exporter.EXTENSION]
        assert root["declarations"] == []
        assert source_capsules(doc, payload, root) == {exporter.SOURCE: text}
    assert exporter.manifest_children(b'm { unknown name//literal precache_file "scripts/a.txt"') == [
        ("unknown", "name//literal"), ("precache_file", "scripts/a.txt")]
    assert exporter.manifest_children(b'm { unknown "{" nested value } precache_file scripts/a.txt }') == [
        ("unknown", ""), ("precache_file", "scripts/a.txt")]
    assert exporter.manifest_children(b'm { key value:next other }') == [
        ("key", "value"), (":", "next"), ("other", "}")]


@pytest.mark.parametrize("mutation", ["precache", "missing", "unknown", "second_root", "absent_manifest"])
def test_import_rederives_manifest_selection_before_writing(tmp_path, mutation):
    manifest = (b'first { precache_file scripts/a.txt declare_file scripts/missing.txt unknown ignored } '
                b'later { precache_file scripts/unselected.txt }')
    path = publish(tmp_path, manifest, {"scripts/a.txt": b'A { wave original }'})
    corpus = tmp_path / "corpus"
    assert not importer.import_sound_script_data(tmp_path / "units", corpus).failures
    doc, payload = read_glb(path)
    root = doc["extensions"][exporter.EXTENSION]
    if mutation == "precache":
        root["declarations"][0]["precache"] = False
    elif mutation == "missing":
        root["declarations"][1]["missing"] = False
    elif mutation == "unknown":
        root["declarations"][2]["key"] = "invented"
    elif mutation == "second_root":
        root["declarations"].append({"key": "precache_file", "path": "scripts/unselected.txt",
                                     "accepted": True, "precache": True,
                                     "member": "scripts/unselected.txt", "missing": True})
    else:
        root["sourceResolution"]["members"] = [
            row for row in root["sourceResolution"]["members"] if row["path"] != exporter.SOURCE]
    write_glb(doc, payload, path)
    failed = importer.import_sound_script_data(tmp_path / "units", corpus)
    assert len(failed.failures) == 1
    assert failed.written == 0
    assert (corpus / "audio/scripts/a.txt").read_bytes() == b'A { wave original }'


def test_availability_projection_accepted_order_gender_prefix_and_numeric(tmp_path):
    manifest = b'first { precache_file scripts/a.txt declare_file scripts/b.txt precache_file scripts/a.txt } later { precache_file scripts/no.txt }'
    tables = {'scripts/a.txt': b'Empty {} A { wave "*?$GeNdEr/x/$gender.wav" rndwave { any "one" other { wave ignored } z 1.5 } } a { wave duplicate }',
              'scripts/b.txt': b'A { wave duplicate2 } B { wave "!sentence" wave "" wave "***x" }'}
    path = publish(tmp_path, manifest, tables)
    doc, payload = read_glb(path)
    root = doc['extensions'][exporter.EXTENSION]
    rows = root['waveAvailability']
    assert [(r['entry'], r['name'], r['wave'], r['value'], r['gender']) for r in rows] == [
        (0, 'A', 0, '*?male/x/$gender.wav', 1), (0, 'A', 1, '*?female/x/$gender.wav', 2),
        (0, 'A', 2, 'one', 0), (0, 'A', 3, '', 0), (0, 'A', 4, '1.500000', 0),
        (1, 'B', 0, '!sentence', 0), (1, 'B', 1, '', 0), (1, 'B', 2, '***x', 0)]
    assert [r['filesystemPath'] for r in rows] == ['sound/male/x/$gender.wav', 'sound/female/x/$gender.wav',
        'sound/one', None, 'sound/1.500000', None, None, 'sound/*x']
    assert [r['availability'] for r in rows] == ['absent', 'absent', 'absent', 'nonfile', 'absent', 'nonfile', 'nonfile', 'absent']
    assert root['sourceBindings'] == exporter.source_bindings(source_capsules(doc, payload, root))
    assert not importer.import_sound_script_data(tmp_path / 'units', tmp_path / 'corpus').failures


def test_availability_requires_explicit_coverage_and_preserves_known_present(tmp_path):
    data = {exporter.SOURCE: b'm { precache_file scripts/a.txt }', 'scripts/a.txt': b'A { wave present wave unknown }'}
    index = {key: ('loose', 'C:/game/Vampire/' + key) for key in data}
    with pytest.raises(ValueError, match='explicit sound name coverage'):
        exporter.export_table(tmp_path, index=index, read_bytes=lambda i, k: data.get(k))
    path = exporter.export_table(tmp_path, index=index, read_bytes=lambda i, k: data.get(k),
        name_coverage=({'sound/present': ('vpk', ('pack010.vpk', 0, 0))},
                       {'complete': False, 'issues': ['unreadable source'], 'packs': [], 'looseRoots': []}))
    doc, _ = read_glb(path)
    rows = doc['extensions'][exporter.EXTENSION]['waveAvailability']
    assert [r['availability'] for r in rows] == ['present', 'unknown']
    assert rows[0]['origin'] == {'kind': 'vpk', 'container': 'pack010.vpk', 'offset': 0, 'size': 0}


@pytest.mark.parametrize('mutation', ['order', 'path', 'identity', 'gender', 'source', 'binding', 'status', 'origin', 'coverage'])
def test_availability_structural_forgery_isolated_before_writes(tmp_path, mutation):
    path = publish(tmp_path, b'm { precache_file scripts/a.txt }', {'scripts/a.txt': b'A { wave a wave b }'})
    corpus = tmp_path / 'corpus'
    assert not importer.import_sound_script_data(tmp_path / 'units', corpus).failures
    doc, payload = read_glb(path)
    root = doc['extensions'][exporter.EXTENSION]
    row = root['waveAvailability'][0]
    if mutation == 'order': root['waveAvailability'].reverse()
    elif mutation == 'path': row['filesystemPath'] = 'sound/forged'
    elif mutation == 'identity': row['entry'] = 4
    elif mutation == 'gender': row['gender'] = 2
    elif mutation == 'source': row['sourceSha256'] = '0' * 64
    elif mutation == 'binding': root['sourceBindings'][0]['bytesBase64'] = 'YQ=='
    elif mutation == 'status': row['availability'] = False
    elif mutation == 'origin': row['origin'] = {'kind': 'loose', 'root': 'Vampire'}
    else:
        root['availabilityCoverage'].update(complete=False, issues=['incomplete'])
    write_glb(doc, payload, path)
    failed = importer.import_sound_script_data(tmp_path / 'units', corpus)
    assert len(failed.failures) == 1 and failed.written == 0
    assert (corpus / 'audio/scripts/a.txt').read_bytes() == b'A { wave a wave b }'
