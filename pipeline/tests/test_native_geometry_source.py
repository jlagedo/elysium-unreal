import hashlib
import json
import os
from pathlib import Path
import struct

import numpy as np
import pytest

from elysium_pipeline.validation.native_geometry_source import mdl_vertex_witness, vtx_triangle_witness, read
from elysium_pipeline.validation.native_geometry import accessor
from elysium_pipeline.validation.skeletal_diff import records, sections
from elysium_pipeline.formats.unit_contract.container import decode_glb
from elysium_pipeline.formats.unit_contract.precision import decode as precise


def source_fixture(boned=False):
    mdl, vtx = bytearray(920), bytearray(180)
    mdl[:4] = b"IDST"
    struct.pack_into("<iI", mdl, 4, 2531, 123)
    struct.pack_into("<2i", mdl, 320, 1, 400)
    struct.pack_into("<4i", mdl, 400, 0, 1, 0, 16)
    struct.pack_into("<6i", mdl, 416 + 136, 1, 224, 3, 304, 436, 0)
    struct.pack_into("<2i", mdl, 640 + 8, 3, 0)
    for i in range(3):
        struct.pack_into("<4B4h8f", mdl, 720 + i * 44, 255, 0, 0, 1, 0, 0, 0, 0,
                         float(i), 0., 0., 0., 0., 1., 0., -1.097e24 if i == 2 else .25)
    struct.pack_into("<i", vtx, 0, 107)
    struct.pack_into("<I", vtx, 16, 123)
    struct.pack_into("<2i", vtx, 28, 1, 36)
    struct.pack_into("<2i", vtx, 36, 1, 8)
    struct.pack_into("<2i", vtx, 44, 1, 8)
    struct.pack_into("<2i", vtx, 52, 1, 12)
    struct.pack_into("<H", vtx, 64, 1)
    struct.pack_into("<i", vtx, 68, 8)
    index_base = 128 if boned else 98
    struct.pack_into("<4H3i", vtx, 72, 3, 3, 1, 8 if boned else 16, 20, index_base - 72, index_base + 6 - 72)
    for i in range(3):
        struct.pack_into("<H", vtx, 92 + (12 * i + 10 if boned else 2 * i), i)
    struct.pack_into("<3H", vtx, index_base, 0, 1, 2)
    struct.pack_into("<2H", vtx, index_base + 6, 3, 0)
    return bytes(mdl), bytes(vtx)


@pytest.mark.parametrize("boned", [False, True])
def test_independent_raw_layout_and_vtx_used_vertex_witness(boned):
    mdl, vtx = source_fixture(boned)
    witness = mdl_vertex_witness(mdl, 0, 0, 0, 2)
    assert witness["vertexByteOffset"] == 808 and witness["uvByteOffset"] == 844
    assert witness["vertexStride"] == witness["poolStrideFromTangentBoundary"] == 44
    assert witness["uv"][1] == float(np.float32(-1.097e24))
    topology = vtx_triangle_witness(vtx, witness)
    assert topology["originalMeshVertex"] == 2 and topology["corner"] == 2
    assert topology["vertexTableStride"] == (12 if boned else 2)


def test_wrong_mesh_identity_and_vtx_index_refuse_instead_of_reading_plausible_bytes():
    mdl, vtx = source_fixture()
    with pytest.raises(ValueError, match="outside model/mesh"):
        mdl_vertex_witness(mdl, 0, 0, 0, 3)
    corrupted = bytearray(vtx)
    struct.pack_into("<H", corrupted, 98, 9)
    with pytest.raises(ValueError, match="outside its vertex"):
        vtx_triangle_witness(bytes(corrupted), mdl_vertex_witness(mdl, 0, 0, 0, 2))


@pytest.mark.skipif(os.environ.get("ELYSIUM_R8_SOURCE_UV") != "1", reason="explicit installed four-model UV byte audit")
def test_winning_installed_uv_bytes():
    from elysium_pipeline.formats import install, vpk
    from elysium_pipeline.formats.model_glb.source import load_source_closure
    stage, export = Path("E:/elysium-work/import/characters"), Path("E:/elysium-work/exports_v2")
    manifest_bytes = (stage / "manifest.json").read_bytes()
    manifest = json.loads(manifest_bytes)
    census = json.loads(Path("E:/elysium-work/_r8_explore/agents/geometry/stage_uv_precision_census.json").read_bytes())
    wanted = {r["assetId"] for r in census["products"]}
    index = install.build_index(dirs=("models",), verbose=False)
    results, failures = [], []
    try:
        for entry in manifest["assets"]:
            if entry["assetId"] not in wanted:
                continue
            try:
                blob = (stage / entry["payload"]).read_bytes()
                body_data = (stage / entry["body"]).read_bytes()
                glb_data = (export / entry["unitGlb"]).read_bytes()
                for data, field in ((blob, "payloadSha256"), (body_data, "bodySha256"), (glb_data, "unitSha256")):
                    assert hashlib.sha256(data).hexdigest() == entry["recipe"][field], field
                body = json.loads(body_data)
                document, binary = decode_glb(glb_data)
                root = document["extensions"]["ELYSIUM_vtmb_model"]
                closure = load_source_closure(index, entry["assetId"].removeprefix("vtmb:model:"))
                members = {m["role"]: m for m in root["sourceResolution"]["members"]}
                for member in closure.members():
                    assert member.sha256 == members[member.role]["sha256"]
                    assert member.origin.to_json() == members[member.role]["origin"]
                assert closure.primary_vtx is not None
                mdl, vtx_data = closure.mdl.data, closure.primary_vtx.data
                mdl_checksum, vtx_checksum = read(mdl, 8, "I"), read(vtx_data, 16, "I")
                lod = next(r for r in root["vtx"]["lods"] if r["index"] == 0)
                primitives = {tuple(p["extensions"]["ELYSIUM_vtmb_model"][k] for k in ("bodyPart", "model", "mesh")): p
                              for p in document["meshes"][lod["mesh"]]["primitives"]}
                joins = {staged: (join, original) for join in body["renderVertexMap"] for original, staged in join["vertices"]}
                uv = {k: v for k, v, _ in records("MESH", sections(blob)["MESH"])}["uv"]
                witnesses = []
                for staged, component in np.argwhere(np.abs(uv) > 65504):
                    staged, component = int(staged), int(component)
                    join, original = joins[staged]
                    key = tuple(join[k] for k in ("bodyPart", "model", "mesh"))
                    native = mdl_vertex_witness(mdl, *key, original)
                    assert native["modelByteOffset"] == root["mdl"]["bodyParts"][key[0]]["models"][key[1]]["sourceOffset"]
                    topology = vtx_triangle_witness(vtx_data, native)
                    primitive = primitives[key]
                    extension = primitive["extensions"]["ELYSIUM_vtmb_model"]
                    local = extension["sourceVertices"].index(original)
                    core = accessor(document, binary, primitive["attributes"]["TEXCOORD_0"])[local]
                    assert np.array_equal(np.asarray(native["uv"], dtype=np.float32), core)
                    assert np.array_equal(core, uv[staged])
                    count = len(extension["sourceVertices"])
                    if native["vertexListType"] == 0:
                        for label in ("position", "normal"):
                            precise_values = np.asarray(precise(document, binary, extension["source" + label.title() + "s"], (count, 3))).reshape(count, 3)
                            assert np.array_equal(precise_values[local], native[label])
                    witnesses.append({"stagedVertex": staged, "component": "UV"[component], "primitiveVertex": local,
                                      "stagedValue": float(uv[staged, component]), "mdl": native, "vtx": topology})
                results.append({"assetId": entry["assetId"], "passed": True, "unitSha256": entry["recipe"]["unitSha256"],
                                "payloadSha256": entry["recipe"]["payloadSha256"], "bodySha256": entry["recipe"]["bodySha256"],
                                "sources": [m.to_json() for m in closure.members()], "primaryVtxRole": closure.primary_vtx.role,
                                "mdlChecksum": mdl_checksum, "vtxChecksum": vtx_checksum, "checksumMatches": mdl_checksum == vtx_checksum,
                                "overflowComponents": len(witnesses), "witnesses": witnesses})
            except (ValueError, KeyError, IndexError, AssertionError) as exc:
                failures.append({"assetId": entry["assetId"], "reason": str(exc)})
    finally:
        vpk.close_handles()
    report = {"scope": "winning-install-MDL/VTX-to-GLB-to-stage-byte-witnesses", "passed": not failures and len(results) == 4,
              "manifestSha256": hashlib.sha256(manifest_bytes).hexdigest(), "failures": failures, "products": results}
    Path("E:/elysium-work/_r8_explore/agents/geometry/installed_uv_byte_evidence.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    assert report["passed"], failures
