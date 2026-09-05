"""Preserve authored tuning values while replacing lookup aliases with stable model IDs."""
import json
from pathlib import Path
import shutil

import unreal
from pipeline.unreal import bake_lib as bl
from elysium_pipeline.importers.tuning_keys import model_key_plan

TABLES = (("/Game/ElysiumAuthored/Hair/DA_HairDynamics", "stems", "ElysiumAuthored/Hair/DA_HairDynamics.uasset"),
          ("/Game/ElysiumAuthored/Cloth/DA_ClothTuning", "garments", "ElysiumAuthored/Cloth/DA_ClothTuning.uasset"))


def migrate(cast, output_root):
    """Preflight both tables before mutating either, then verify value text after assignment."""
    plans = []
    for path, field, relative in TABLES:
        asset = unreal.load_asset(path)
        if asset is None:
            raise RuntimeError("authored tuning asset is absent: " + path)
        original = dict(asset.get_editor_property(field))
        plan = model_key_plan(original, cast)
        values = {str(key): value.export_text() for key, value in original.items()}
        plans.append((path, field, relative, asset, original, plan, values))
    report = []
    backup = Path(output_root) / "authored_tuning_before_ids"
    backup.mkdir(parents=True, exist_ok=True)
    expected_values = {path: {plan[key]: value for key, value in values.items()}
                       for path, _, _, _, _, plan, values in plans}
    receipt = Path(output_root) / "tuning_verification.json"
    temporary = receipt.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(expected_values, indent=2), encoding="utf-8")
    temporary.replace(receipt)
    for path, field, relative, asset, original, plan, values in plans:
        changed = any(key != id for key, id in plan.items())
        if changed:
            source = Path(unreal.Paths.project_content_dir()) / relative
            saved = backup / source.name
            if not saved.exists():
                shutil.copy2(source, saved)
                saved.with_suffix(".json").write_text(json.dumps({"asset": path, "field": field, "values": values, "keys": plan}, indent=2), encoding="utf-8")
            migrated = {unreal.Name(plan[str(key)]): value for key, value in original.items()}
            asset.set_editor_property(field, migrated)
            after = {str(key): value.export_text() for key, value in asset.get_editor_property(field).items()}
            expected = {plan[key]: value for key, value in values.items()}
            if after != expected:
                asset.set_editor_property(field, original)
                raise RuntimeError("tuning value changed while replacing keys: " + path)
            if not bl.save(path):
                asset.set_editor_property(field, original)
                raise RuntimeError("could not save migrated tuning: " + path)
        report.append({"asset": path, "field": field, "entries": len(plan), "changed": changed, "keys": plan})
    return report


def verify(cast, output_root):
    report = []
    receipt = Path(output_root) / "tuning_verification.json"
    if not receipt.is_file():
        raise RuntimeError("tuning import has no value-preservation receipt")
    expected_values = json.loads(receipt.read_text(encoding="utf-8"))
    for path, field, relative in TABLES:
        asset = unreal.load_asset(path)
        if asset is None:
            raise RuntimeError("authored tuning asset is absent: " + path)
        values = {str(key): value.export_text() for key, value in asset.get_editor_property(field).items()}
        plan = model_key_plan(values, cast)
        if any(key != id for key, id in plan.items()):
            raise RuntimeError("authored tuning still uses legacy aliases: " + path)
        if values != expected_values[path]:
            raise RuntimeError("saved tuning values differ from the migration receipt: " + path)
        report.append({"asset": path, "entries": len(values), "valuesPreserved": True})
    return report
