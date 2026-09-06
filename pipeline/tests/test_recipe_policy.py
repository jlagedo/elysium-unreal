"""Recipes are data content plus a producer version. Code is never hashed.

`docs/architecture/seam_map_unit_contract.md` -> "Baked assets" -> "Recipes". Hashing a script,
a module or the editor DLL into a recipe re-authored whole lanes for a comment edit or an
unrelated C++ change; the version string is the code half, bumped on purpose.
"""
from pathlib import Path
import re

PIPELINE = Path(__file__).resolve().parents[1]
ROOTS = (PIPELINE / "unreal", PIPELINE / "src" / "elysium_pipeline")
FORBIDDEN = (
    re.compile(r"__file__\)\.read_bytes\("),
    re.compile(r"__file__\)\.read_text\("),
    re.compile(r"\.__file__\)\.read_(?:bytes|text)\("),
    re.compile(r"UnrealEditor-ElysiumUE\.dll"),
    re.compile(r"native_source_files|_source_hash\("),
)
# The task graph of the loose export lane still fingerprints its decoder closure by content;
# it retires with that lane in R9.2 and is the one allowed exception until then.
ALLOWED = {PIPELINE / "src" / "elysium_pipeline" / "export_manager.py",
           PIPELINE / "src" / "elysium_pipeline" / "tasking.py"}


def test_no_producer_hashes_its_own_code():
    offenders = []
    for root in ROOTS:
        for path in sorted(root.rglob("*.py")):
            if path in ALLOWED or "__pycache__" in path.parts:
                continue
            for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                if any(pattern.search(line) for pattern in FORBIDDEN):
                    offenders.append(f"{path.relative_to(PIPELINE)}:{number}: {line.strip()}")
    assert not offenders, "code hashed into a recipe:\n" + "\n".join(offenders)


def test_every_native_lane_declares_its_producer_version():
    expected = {
        "unreal/import_characters.py": "PRODUCER_VERSION",
        "unreal/import_cook_roots.py": "PRODUCER_VERSION",
        "unreal/import_expression_tables.py": "PRODUCER_VERSION",
        "unreal/import_model_catalogues.py": "PRODUCER_VERSION",
        "unreal/import_physics_data.py": "PRODUCER_VERSION",
        "unreal/make_v2_materials.py": "GRAPH_VERSION",
        "src/elysium_pipeline/importers/characters.py": "SETTINGS_VERSION",
        "src/elysium_pipeline/importers/model_catalogues.py": "RULES_VERSION",
        "src/elysium_pipeline/importers/models.py": "SETTINGS_VERSION",
        "src/elysium_pipeline/importers/materials.py": "SETTINGS_VERSION",
        "src/elysium_pipeline/importers/textures.py": "SETTINGS_VERSION",
        "src/elysium_pipeline/importers/surface_properties.py": "SETTINGS_VERSION",
    }
    for relative, name in expected.items():
        text = (PIPELINE / relative).read_text(encoding="utf-8")
        assert re.search(r"^%s\s*=\s*[\"\d]" % name, text, re.M), f"{relative} lacks {name}"
