"""Bounded, sequential PHYS1 installed-source probe; never writes generated state.

The caller supplies explicit roots and owns the report destination. Binary GLBs are
seek-read only for individual hull accessors; animations and render buffers stay on disk.
"""

from __future__ import annotations

from collections import Counter
from contextlib import closing
import hashlib
import json
from pathlib import Path
import struct
import sqlite3
import re
import subprocess

import numpy as np

from elysium_pipeline.validation.physics_calibration import (
    PhysicsEvidenceError, bind_references, frame_candidates, rotation_error_degrees,
    score_axis_rates, score_solid_frames,
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_glb_json(path, maximum_json=32 * 1024 * 1024):
    with Path(path).open("rb") as stream:
        magic, version, size = struct.unpack("<4sII", stream.read(12))
        length, kind = struct.unpack("<I4s", stream.read(8))
        if magic != b"glTF" or version != 2 or kind != b"JSON" or length > maximum_json:
            raise PhysicsEvidenceError(f"invalid/beyond-bound GLB JSON: {path}")
        if size != Path(path).stat().st_size:
            raise PhysicsEvidenceError(f"GLB size mismatch: {path}")
        document = json.loads(stream.read(length))
        binary_size, binary_kind = struct.unpack("<I4s", stream.read(8))
        if binary_kind != b"BIN\0" or stream.tell() + binary_size != size:
            raise PhysicsEvidenceError(f"invalid GLB binary chunk: {path}")
        return document, stream.tell(), binary_size


def read_accessor(path, document, binary_offset, binary_size, index):
    accessor = document["accessors"][index]
    view = document["bufferViews"][accessor["bufferView"]]
    component = {5123: "u2", 5125: "u4", 5126: "f4"}[accessor["componentType"]]
    width = {"SCALAR": 1, "VEC3": 3}[accessor["type"]]
    if "sparse" in accessor or accessor.get("normalized") or view.get("buffer", 0) != 0:
        raise PhysicsEvidenceError("unsupported hull accessor; do not omit it")
    item_size = np.dtype(component).itemsize * width
    stride = view.get("byteStride", item_size)
    count = accessor["count"]
    relative = accessor.get("byteOffset", 0)
    length = (count - 1) * stride + item_size if count else 0
    start = view.get("byteOffset", 0) + relative
    if count < 0 or stride < item_size or relative < 0 or start < 0 or length > 8 * 1024 * 1024 or relative + length > view["byteLength"] or start + length > binary_size:
        raise PhysicsEvidenceError("out-of-bounds hull accessor")
    with Path(path).open("rb") as stream:
        stream.seek(binary_offset + start)
        data = stream.read(length)
    if len(data) != length:
        raise PhysicsEvidenceError("short hull accessor read")
    return np.ndarray((count, width), dtype="<" + component, buffer=data,
                      strides=(stride, np.dtype(component).itemsize)).copy()


def projection_errors(raw, projected, path, document, offset, size):
    """Audit every raw field plus all hull points/indices against the GLB projection."""
    errors, counts = [], Counter()

    def walk(left, right, key):
        if isinstance(left, dict):
            for name, value in left.items():
                if name == "textAnomalies":
                    continue  # Published at extension.anomalies; checked separately below.
                if name in ("vertices", "triangles") and ".hulls[" in key:
                    field = "positions" if name == "vertices" else "indices"
                    try:
                        actual = read_accessor(path, document, offset, size, right[field])
                        expected = np.asarray(value)
                        if name == "vertices":
                            # Decoder vertices already use the GLB axis-only frame.
                            counts["vertices"] += len(expected)
                        else:
                            expected = expected.reshape(-1, 1)
                            counts["triangles"] += len(expected) // 3
                        if actual.shape != expected.shape or not np.array_equal(actual, expected):
                            errors.append(key + "." + name)
                    except (KeyError, ValueError, PhysicsEvidenceError) as error:
                        errors.append(key + "." + name + ": " + str(error))
                elif name not in right:
                    errors.append(key + "." + name + ": missing")
                else:
                    walk(value, right[name], key + "." + name)
        elif isinstance(left, (list, tuple)):
            if len(left) != len(right):
                errors.append(key + ": count")
            for i, (a, b) in enumerate(zip(left, right)):
                walk(a, b, f"{key}[{i}]")
        elif left != right:
            errors.append(key)
    walk(raw, projected, "physics")
    return errors, dict(counts)


def collect(*, export_root, stage_root, maximum_phy=4096, maximum_canonical=None):
    """Index model paths once, inspect PHYs sequentially; score canonical 15/14 rigs.

    maximum_canonical is a pilot bound and explicitly leaves cohort acceptance open.
    There is no automatic claim that the historic count 289 is current coverage.
    """
    from elysium_pipeline.formats import install, mdl_skel, vpk
    from elysium_pipeline.formats.model_glb.physics import decode

    export_root, stage_root = Path(export_root), Path(stage_root)
    manifest_bytes = (stage_root / "manifest.json").read_bytes()
    manifest = json.loads(manifest_bytes)
    assets = {r["assetId"]: r for r in manifest["assets"]}
    inventory = {r["assetId"]: r for r in manifest["inventory"]}
    index = install.build_index(dirs=("models",), verbose=False)
    keys = sorted(k for k in index if k.startswith("models/") and k.endswith(".phy"))
    if len(keys) > maximum_phy:
        raise PhysicsEvidenceError(f"PHY file bound exceeded: {len(keys)} > {maximum_phy}")
    rows, canonical, totals, shapes, errors = [], [], Counter(), Counter(), []
    candidates = frame_candidates()
    try:
        for key in keys:
            data = install.read(index, key)
            if len(data) > 8 * 1024 * 1024:
                raise PhysicsEvidenceError(f"PHY byte bound exceeded: {key}")
            # Container size walk identifies the textual tail without decoding all hulls.
            header_size, _, count, _ = struct.unpack_from("<4i", data)
            position = header_size
            for _ in range(count):
                solid_size = struct.unpack_from("<i", data, position)[0]
                if solid_size < 48 or position + 4 + solid_size > len(data):
                    raise PhysicsEvidenceError(f"invalid PHY solid size: {key}")
                position += 4 + solid_size
            tail = data[position:]
            constraint_count = tail.lower().count(b"ragdollconstraint")
            totals["phyFiles"] += 1
            unit_id = "vtmb:model:" + key[7:-4]
            row = {"assetId": unit_id, "phyPath": key, "phySha256": digest(data),
                   "phyOrigin": list(index[key]), "solidCount": count,
                   "constraintTokenCount": constraint_count, "staged": unit_id in assets,
                   "inventory": inventory.get(unit_id), "scored": False}
            rows.append(row)
            if not constraint_count:
                continue
            raw = decode(data)
            shapes[f"{count}/{len(raw['constraints'])}"] += 1
            totals["rigs"] += 1
            row["constraintCount"] = len(raw["constraints"])
            row["solidNames"] = [s["properties"].get("name") for s in raw["solids"]]
            if count != 15 or len(raw["constraints"]) != 14:
                row["omission"] = "noncanonical rig; retained in coverage ledger"
                continue
            totals["canonicalRigs"] += 1
            if maximum_canonical is not None and len(canonical) >= maximum_canonical:
                row["omission"] = "explicit pilot bound"
                continue
            try:
                mdl = install.read(index, key[:-4] + ".mdl")
                if mdl is None:
                    raise PhysicsEvidenceError("missing installed MDL")
                inverse, hierarchy = bind_references(mdl_skel.read_bones(mdl))
                properties = [s["properties"] for s in raw["solids"]]
                row["mdlSha256"] = digest(mdl)
                row["mdlOrigin"] = list(index[key[:-4] + ".mdl"])
                row["referenceDisagreementMaxCm"] = max(float(np.linalg.norm(inverse[n][:3, 3]-hierarchy[n][:3, 3])*2.54) for n in inverse)
                row["referenceDisagreementMaxDegrees"] = max(rotation_error_degrees(inverse[n][:3, :3], hierarchy[n][:3, :3]) for n in inverse)
                row["scores"] = score_solid_frames(properties, inverse, candidates)
                # The hierarchy corroborates the winning *same* hypothesis independently.
                viable = [s for s in row["scores"] if not s["failures"]]
                if viable:
                    winner = min(viable, key=lambda s: (s["positionRmsCm"], s["rotationRmsDegrees"]))
                    row["hierarchicalWinnerCheck"] = score_solid_frames(properties, hierarchy,
                        [c for c in candidates if c.name == winner["candidate"]])[0]
                glb_path = export_root / ("models/" + key[7:-4] + ".glb")
                document, offset, size = read_glb_json(glb_path)
                extension = document["extensions"]["ELYSIUM_vtmb_model"]
                projected = extension["physics"]
                row["glbPhysicsSha256"] = digest(json.dumps(projected, sort_keys=True).encode())
                row["projectionErrors"], row["hullCoverage"] = projection_errors(raw, projected, glb_path, document, offset, size)
                row["textAnomalies"] = raw.get("textAnomalies", [])
                for anomaly in row["textAnomalies"]:
                    if anomaly not in extension.get("anomalies", []):
                        row["projectionErrors"].append("missing text anomaly")
                entry = assets.get(unit_id)
                if entry:
                    body_path = stage_root / entry["body"]
                    body_bytes = body_path.read_bytes()
                    body = json.loads(body_bytes)
                    row["stageBodySha256"] = digest(body_bytes)
                    row["stagePhysicsEqualGlb"] = body["sourceSemantics"]["physics"] == projected
                else:
                    row["stagePhysicsEqualGlb"] = None
                row["scored"] = True
                canonical.append(row)
            except (KeyError, ValueError, OSError) as error:
                row["error"] = str(error)
                errors.append({"assetId": unit_id, "error": str(error)})
    finally:
        vpk.close_handles()
    # A rig has equal weight; exact MDL/PHY duplicate sources are also reported so
    # a hundred copies of one compiler template cannot masquerade as independent evidence.
    aggregate = []
    for candidate in candidates:
        selected = [next(s for s in r["scores"] if s["candidate"] == candidate.name) for r in canonical]
        complete = [s for s in selected if not s["failures"]]
        aggregate.append({"candidate": candidate.name, "rigs": len(complete),
                          "incompleteRigs": len(selected)-len(complete),
                          "positionRmsCm": float(np.sqrt(np.mean([s["positionRmsCm"]**2 for s in complete]))) if complete else None,
                          "rotationRmsDegrees": float(np.sqrt(np.mean([s["rotationRmsDegrees"]**2 for s in complete]))) if complete else None,
                          "maxPositionCm": max((s["positionMaxCm"] for s in complete), default=None),
                          "maxRotationDegrees": max((s["rotationMaxDegrees"] for s in complete), default=None)})
    aggregate.sort(key=lambda s: (s["incompleteRigs"], s["positionRmsCm"] if s["positionRmsCm"] is not None else float("inf"), s["rotationRmsDegrees"] if s["rotationRmsDegrees"] is not None else float("inf"), s["candidate"]))
    totals["scoredCanonicalRigs"] = len(canonical)
    totals["uniqueScoredMdlPhyPairs"] = len({(r["mdlSha256"], r["phySha256"]) for r in canonical})
    return {"schemaVersion": 1, "accepted": False, "manifestSha256": digest(manifest_bytes),
            "historicCanonicalTarget": 289, "pilotBound": maximum_canonical,
            "totals": dict(totals), "rigShapes": dict(shapes), "errors": errors,
            "frameRanking": aggregate, "axisEvidence": score_axis_rates([]),
            "assets": rows, "omissions": ["noncanonical rigs have inventory only", "no live retail/UE pose or axis-rate pairs", "no simulation/physical-parameter calibration", "no native builder acceptance"]}


def summarize(report):
    """Compact reporting from saved evidence, without another install/corpus scan."""
    rows = [r for r in report["assets"] if r["scored"]]
    target = "+x,+y,+z/source_qangle/model/forward"
    identity = [{"assetId": r["assetId"], "phySha256": r["phySha256"],
                 "mdlSha256": r["mdlSha256"],
                 **next(s for s in r["scores"] if s["candidate"] == target)} for r in rows]
    complete = [r for r in identity if not r["failures"]]
    rankings = report["frameRanking"]
    position = [r for r in rankings if "/source_qangle/" in r["candidate"] and r["candidate"].endswith("/forward")]
    rotation = sorted(rankings, key=lambda r: (r["incompleteRigs"], r["rotationRmsDegrees"] if r["rotationRmsDegrees"] is not None else float("inf"), r["candidate"]))
    unique_phy = {}
    for r in complete:
        unique_phy.setdefault(r["phySha256"], []).append(r)
    return {"totals": report["totals"], "rigShapes": report["rigShapes"],
            "candidateCount": len(rankings), "canonicalSolidTargets": sum(s["expected"] for s in identity),
            "identityComparedSolids": sum(s["compared"] for s in identity),
            "preservedConstraints": sum(r["constraintCount"] for r in rows),
            "uniqueScoredPhyHashes": len(unique_phy),
            "projectionErrors": [{"assetId": r["assetId"], "errors": r["projectionErrors"]} for r in rows if r["projectionErrors"]],
            "stageMissing": [r["assetId"] for r in rows if r["stagePhysicsEqualGlb"] is None],
            "stageMismatches": [r["assetId"] for r in rows if r["stagePhysicsEqualGlb"] is False],
            "noncanonicalRigs": [r["assetId"] for r in report["assets"] if r.get("constraintCount") and r["solidCount"] != 15],
            "unstagedRigs": [r["assetId"] for r in report["assets"] if r.get("constraintCount") and not r["staged"]],
            "hullCoverage": dict(sum((Counter(r["hullCoverage"]) for r in rows), Counter())),
            "referenceDisagreementMaxCm": max((r["referenceDisagreementMaxCm"] for r in rows), default=None),
            "referenceDisagreementMaxDegrees": max((r["referenceDisagreementMaxDegrees"] for r in rows), default=None),
            "positionRanking": position[:6], "rotationRanking": rotation[:6],
            "positionRunnerUpMarginCm": position[1]["positionRmsCm"] - position[0]["positionRmsCm"] if len(complete) == len(rows) and complete else None,
            "rotationRunnerUpMarginDegrees": rotation[1]["rotationRmsDegrees"] - rotation[0]["rotationRmsDegrees"] if len(complete) == len(rows) and complete else None,
            "identityWorst": sorted(complete, key=lambda r: (-r["positionRmsCm"], -r["rotationRmsDegrees"]))[:12],
            "identityIncomplete": [r for r in identity if r["failures"]],
            "identityNumericalAgreement1e3": sum(r["positionMaxCm"] < 1e-3 and r["rotationMaxDegrees"] < 1e-3 for r in complete),
            "numericalAgreementNote": "1e-3 cm/degree is a diagnostic bucket, not a physical acceptance tolerance",
            "accepted": False, "axisEvidence": report["axisEvidence"], "errors": report["errors"]}


def retail_snapshot(database, install_root, reference_files):
    """Read-only corpus queries; verify captured binary hashes against this install."""
    queries = {"client.dll": ("10126d00", "10089660", "10108450", "10108a50", "10109420"),
               "vampire.dll": ("1019c330",)}
    binaries = {"client.dll": "Vampire/cl_dlls/client.dll", "vampire.dll": "Vampire/dlls/vampire.dll",
                "vphysics.dll": "bin/vphysics.dll"}
    result = {"binaryProvenance": [], "functions": [], "referenceSource": []}
    with closing(sqlite3.connect(Path(database).resolve().as_uri() + "?mode=ro", uri=True)) as db:
        db.row_factory = sqlite3.Row
        for module, relative in binaries.items():
            record = db.execute("SELECT * FROM meta WHERE module=?", (module,)).fetchone()
            actual = Path(install_root) / relative
            sha = digest(actual.read_bytes())
            result["binaryProvenance"].append({"module": module, "installedPath": str(actual),
                "installedSha256": sha, "corpus": dict(record) if record else None,
                "usedForFunctions": module in queries,
                "matchesCorpus": (record["sha256"] == sha) if record and record["sha256"] else None})
        for module, addresses in queries.items():
            for address in addresses:
                record = db.execute("SELECT module, addr, name, code, warn FROM functions WHERE module=? AND addr=?", (module, address)).fetchone()
                if record is None:
                    raise PhysicsEvidenceError(f"missing static function {module}:{address}")
                result["functions"].append(dict(record))
    for path in reference_files:
        path = Path(path)
        result["referenceSource"].append({"path": str(path), "sha256": digest(path.read_bytes()),
                                          "kind": "reference source, not matched retail implementation"})
    return result


class PE32Evidence:
    """Bounded static reads from PE32 bytes; never loads or executes the image."""

    def __init__(self, data):
        self.data = data
        if len(data) < 256 or len(data) > 16 * 1024 * 1024 or data[:2] != b"MZ":
            raise PhysicsEvidenceError("invalid or oversized DOS image")
        pe = struct.unpack_from("<I", data, 0x3c)[0]
        if pe + 24 > len(data) or data[pe:pe+4] != b"PE\0\0":
            raise PhysicsEvidenceError("invalid PE signature")
        machine, count, self.timestamp, _, _, optional_size, _ = struct.unpack_from("<HHIIIHH", data, pe+4)
        optional = pe + 24
        if machine != 0x14c or optional_size < 224 or optional + optional_size + count*40 > len(data) or struct.unpack_from("<H", data, optional)[0] != 0x10b:
            raise PhysicsEvidenceError("expected bounded x86 PE32 headers")
        self.image_base = struct.unpack_from("<I", data, optional+28)[0]
        self.image_size = struct.unpack_from("<I", data, optional+56)[0]
        self.directories = [struct.unpack_from("<II", data, optional+96+i*8) for i in range(16)]
        self.sections = []
        for i in range(count):
            at = optional + optional_size + i*40
            name = data[at:at+8].rstrip(b"\0").decode("ascii")
            virtual_size, rva, size, offset = struct.unpack_from("<IIII", data, at+8)
            flags = struct.unpack_from("<I", data, at+36)[0]
            if offset + size > len(data):
                raise PhysicsEvidenceError("section exceeds physical file")
            self.sections.append({"name": name, "rva": rva, "virtualSize": virtual_size,
                                  "rawOffset": offset, "rawSize": size, "flags": flags})

    def offset(self, va, size=1):
        for section in self.sections:
            local = va - self.image_base - section["rva"]
            if size >= 0 and 0 <= local and local + size <= section["rawSize"]:
                return section["rawOffset"] + local
        raise PhysicsEvidenceError(f"VA 0x{va:x}+{size} has no file-backed section")

    def read(self, va, size):
        offset = self.offset(va, size)
        return self.data[offset:offset+size]

    def cstring(self, va, maximum=256):
        offset = self.offset(va)
        end = self.data.find(b"\0", offset, offset+maximum)
        if end < 0:
            raise PhysicsEvidenceError("unterminated bounded PE string")
        self.offset(va, end-offset+1)
        return self.data[offset:end].decode("ascii")

    def occurrences(self, pattern, *, executable=None):
        """Byte-pattern hits only, not disassembly or proven instruction references."""
        if not pattern:
            raise PhysicsEvidenceError("empty byte pattern")
        result = []
        for section in self.sections:
            if executable is not None and bool(section["flags"] & 0x20000000) != executable:
                continue
            start = section["rawOffset"]
            blob = self.data[start:start+section["rawSize"]]
            at = blob.find(pattern)
            while at >= 0:
                result.append(self.image_base + section["rva"] + at)
                at = blob.find(pattern, at+1)
        return result

    def facts(self):
        debug = []
        rva, size = self.directories[6]
        for at in range(0, size, 28):
            if at+28 > size:
                raise PhysicsEvidenceError("partial PE debug record")
            fields = struct.unpack("<IIHHIIII", self.read(self.image_base+rva+at, 28))
            kind, length, _, pointer = fields[4:]
            if pointer+length > len(self.data):
                raise PhysicsEvidenceError("PE debug payload exceeds file")
            payload = self.data[pointer:pointer+length]
            record = {"type": kind, "rawHex": payload.hex()}
            if kind == 2 and payload[:4] == b"NB10" and len(payload) >= 17:
                record["codeView"] = {"format": "NB10", "signature": hex(struct.unpack_from("<I", payload, 8)[0]),
                                      "age": struct.unpack_from("<I", payload, 12)[0],
                                      "pdbPath": payload[16:].rstrip(b"\0").decode("ascii")}
            debug.append(record)
        return {"sha256": digest(self.data), "size": len(self.data), "imageBase": hex(self.image_base),
                "timeDateStamp": self.timestamp, "imageSize": self.image_size,
                "sections": self.sections, "debugDirectory": debug}

    def relative_branch_hits(self, target):
        """Candidate E8/E9 byte hits; callers must confirm instruction boundaries."""
        hits = []
        for opcode in (0xe8, 0xe9):
            for address in self.occurrences(bytes([opcode]), executable=True):
                try:
                    displacement = struct.unpack("<i", self.read(address+1, 4))[0]
                except PhysicsEvidenceError:
                    continue
                if (address+5+displacement) & 0xffffffff == target:
                    hits.append({"va": hex(address), "opcode": hex(opcode)})
        return hits


def installed_listing_rows(text, image):
    """Parse LLVM objdump rows and verify every emitted instruction byte against PE."""
    rows = []
    pattern = re.compile(r"^([0-9a-f]+):\s+((?:[0-9a-f]{2}\s+)+)\s*(\S.*)$")
    for line in text.splitlines():
        match = pattern.match(line)
        if not match:
            continue
        address = int(match[1], 16)
        data = bytes.fromhex(match[2])
        if image.read(address, len(data)) != data:
            raise PhysicsEvidenceError(f"disassembly bytes do not match PE at {address:x}")
        instruction = match[3].split(" <", 1)[0]
        rows.append({"va": hex(address), "bytes": data.hex(), "instruction": instruction})
    if not rows:
        raise PhysicsEvidenceError("no installed disassembly rows")
    return rows


# Explicit, bounded installed-code spans. The end is exclusive; the axis builder's
# jump table is read as DATA separately, not disassembled as fake instructions.
VPHYSICS_SPANS = {
    "physics_singleton": (0x26001ba0, 0x26001bc6),
    "physics_factory": (0x26001db0, 0x26001dd6),
    "create_environment": (0x26001bd0, 0x26001c7c),
    "environment_allocator": (0x260116b0, 0x260116c8),
    "environment_constructor_head": (0x2600f920, 0x2600f983),
    "collision_singleton": (0x26009850, 0x2600988a),
    "collision_factory": (0x260099d0, 0x260099f6),
    "parser_factory_wrapper": (0x2600ad90, 0x2600ada0),
    "parser_factory": (0x26024f90, 0x26024fc0),
    "parser_constructor": (0x26023800, 0x2602381b),
    "parse_solid": (0x260238f0, 0x26023bfa),
    "parse_ragdoll": (0x26023c40, 0x26023f0c),
    "create_ragdoll_wrapper": (0x260107a0, 0x260107c3),
    "create_ragdoll": (0x2600dd30, 0x2600dd70),
    "constraint_constructor": (0x2600b8a0, 0x2600b990),
    "init_ragdoll": (0x2600b990, 0x2600bc4c),
    "setup_axis": (0x2600dc10, 0x2600dc76),
    "matrix_source_to_ivp": (0x260010b0, 0x2600119d),
    "matrix_ivp_to_havana": (0x2600b380, 0x2600b408),
    "axis_builder": (0x2609d850, 0x2609e475),
    "object_constructor": (0x260168e0, 0x26016910),
    "object_factory_wrapper": (0x260104f0, 0x260105c1),
    "object_factory": (0x260189f0, 0x26018ba0),
    "object_mass": (0x260173f0, 0x260173fd),
    "object_set_position_matrix": (0x260182d0, 0x260183a3),
}


def _installed_span(binary, objdump, image, start, end):
    process = subprocess.run([str(objdump), "-d", "--x86-asm-syntax=intel",
        f"--start-address={start:#x}", f"--stop-address={end:#x}", str(binary)],
        capture_output=True, text=True, check=True, timeout=30)
    rows = installed_listing_rows(process.stdout, image)
    cursor = start
    for row in rows:
        if int(row["va"], 16) != cursor or "<unknown>" in row["instruction"]:
            raise PhysicsEvidenceError(f"noncontiguous/unknown disassembly at {cursor:x}")
        cursor += len(bytes.fromhex(row["bytes"]))
    if cursor != end:
        raise PhysicsEvidenceError(f"incomplete code span: {cursor:x} != {end:x}")
    return {"start": hex(start), "endExclusive": hex(end),
            "sha256": digest(image.read(start, end-start)), "rows": rows}


def vphysics_installed_receipt(binary, objdump, *, caller_binaries=None):
    """Re-disassemble only listed code spans from installed bytes; no corpus changes."""
    binary, objdump = Path(binary), Path(objdump)
    image = PE32Evidence(binary.read_bytes())
    result = {"installedBinary": str(binary), "pe": image.facts(),
              "objdump": str(objdump), "objdumpSha256": digest(objdump.read_bytes()),
              "spans": {}, "tables": {}, "constants": {}, "parserKeys": {}}
    for label, (start, end) in VPHYSICS_SPANS.items():
        span = result["spans"][label] = _installed_span(binary, objdump, image, start, end)
        if label.startswith("parse_"):
            keys = []
            for row in span["rows"]:
                match = re.fullmatch(r"push\s+(0x[0-9a-f]+)", row["instruction"])
                if match:
                    try:
                        name = image.cstring(int(match[1], 16))
                    except (PhysicsEvidenceError, UnicodeDecodeError):
                        continue
                    keys.append({"key": name, "instructionVa": row["va"], "stringVa": match[1]})
            result["parserKeys"][label] = keys
    for label, address, count in (
        ("physicsVtable", 0x260c91a8, 5), ("collisionVtable", 0x260c9550, 32),
        ("environmentVtable", 0x260c9998, 32), ("parserVtable", 0x260ca28c, 10),
        ("objectVtable", 0x260c9db0, 37), ("motorAxisMap", 0x260cfd7c, 3),
        ("limitAxisMap", 0x260cfd88, 3), ("axisBuilderSwitch", 0x2609e478, 4)):
        data = image.read(address, count*4)
        result["tables"][label] = {"va": hex(address), "rawHex": data.hex(),
                                  "values": list(struct.unpack(f"<{count}I", data))}
    for label, address in (("sourceInchToMetre", 0x260c918c), ("degreesToRadians", 0x260c92b0),
                            ("freedomEpsilon", 0x260cb0c8), ("half", 0x260c936c),
                            ("negativeHalf", 0x260c9390)):
        data = image.read(address, 4)
        result["constants"][label] = {"va": hex(address), "rawHex": data.hex(),
                                     "float32": struct.unpack("<f", data)[0]}
    result["callers"] = {}
    caller_starts = {"client.dll": 0x10126d00, "vampire.dll": 0x1019c330}
    for module, path in (caller_binaries or {}).items():
        start = caller_starts[module]
        caller = PE32Evidence(Path(path).read_bytes())
        result["callers"][module] = {"path": str(path), "sha256": digest(caller.data),
                                     "span": _installed_span(path, objdump, caller, start, start+756)}
        threshold_address = 0x101e34f0 if module == "client.dll" else 0x104454c4
        threshold = caller.read(threshold_address, 4)
        result["callers"][module]["frictionScaleThreshold"] = {
            "va": hex(threshold_address), "rawHex": threshold.hex(),
            "float32": struct.unpack("<f", threshold)[0]}
    result["claimsBoundary"] = "verified installed byte spans; missing historical corpus hash is not repaired; no live/Chaos acceptance"
    return result


def ragdoll_branch_census(saved_report, receipt):
    """Re-read only the 324 previously inventoried rig PHYs; no scoring/render load."""
    from elysium_pipeline.formats import install, vpk
    from elysium_pipeline.formats.model_glb.physics import decode
    report = json.loads(Path(saved_report).read_text(encoding="utf-8"))
    index = install.build_index(dirs=("models",), verbose=False)
    factor = receipt["constants"]["degreesToRadians"]["float32"]
    epsilon = receipt["constants"]["freedomEpsilon"]["float32"]
    result = {"rigs": 0, "sourceHashesChanged": [], "branchCounts": {},
              "canonicalBranchCounts": {}, "zeroFreedomJoints": [], "incompleteFields": []}
    counts, canonical_counts = Counter(), Counter()
    try:
        for source in report["assets"]:
            if not source.get("constraintCount"):
                continue
            data = install.read(index, source["phyPath"])
            if data is None or digest(data) != source["phySha256"]:
                result["sourceHashesChanged"].append(source["assetId"])
                continue
            raw = decode(data)
            result["rigs"] += 1
            for number, joint in enumerate(raw["constraints"]):
                missing = [axis+field for axis in "xyz" for field in ("min", "max", "friction") if axis+field not in joint]
                if missing:
                    result["incompleteFields"].append({"assetId": source["assetId"], "constraint": number, "missing": missing})
                    continue
                freedom = sum((joint[axis+"max"]-joint[axis+"min"])*factor > epsilon for axis in "xyz")
                counts[str(freedom)] += 1
                if source["solidCount"] == 15 and source["constraintCount"] == 14:
                    canonical_counts[str(freedom)] += 1
                if freedom == 0:
                    result["zeroFreedomJoints"].append({"assetId": source["assetId"], "constraint": joint})
    finally:
        vpk.close_handles()
    result["branchCounts"], result["canonicalBranchCounts"] = dict(counts), dict(canonical_counts)
    return result
