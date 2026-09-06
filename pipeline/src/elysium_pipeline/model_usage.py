"""Representation selection shared by skeletal staging and material consumer routing."""


def skeletal_candidate(identity, bone_count, *, has_cloth=False):
    """Characters retain their skeletal representation even when their geometry is rigid.

    Shape describes the source mesh. It does not remove a character's bones, attachments or
    rest clip from the native lane. Other skeletal shapes are admitted regardless of role.
    Cloth also needs a skeletal representation when its authored skin has only one bone.
    A placed role alone does not turn an old static representation into a skeletal
    requirement. Explicit cinematic/wield selections and include closure are joined
    by the stage caller. Static-source cloth keeps both representations.
    """
    return bool(bone_count and (identity.get("family") == "character" or identity["shape"] == "skeletal"
                               or has_cloth))


def placed_animation_models(placements, sequence_labels_for):
    """Select source-present animation demand, not every model with a placed role.

    Full vocabulary requests include a one-frame sequence: it still has a label,
    timing and dispatch semantics. Missing intrinsic labels remain source absence.
    sequence_labels_for returns the owner's source labels (empty for a boneless model).
    """
    selected = set()
    for id, use in placements.items():
        if use["sourceAbsent"] or not (use["fullClips"] or use["requiredClips"]):
            continue
        labels = {label.casefold() for label in sequence_labels_for(id)}
        if labels and (use["fullClips"] or any(label.casefold() in labels for label in use["requiredClips"])):
            selected.add(id)
    return selected


def discover_placed_animation_models(export_root, published_ids):
    """Read GLB entity requests and only the requested models' JSON metadata.

    Character default selection joins this set before its existing include closure.
    Explicit --bodies selection stays selective. Material routing joins the same set.
    """
    from pathlib import Path
    from elysium_pipeline.formats.unit_contract.container import read_document
    from elysium_pipeline.importers.placed_catalogue import collect_placed_references

    root = Path(export_root)
    entities = (read_document(path)["extensions"]["ELYSIUM_vtmb_map_entities"]
                for path in sorted((root / "maps").glob("*.entities.glb")))
    placements = collect_placed_references(entities, [], published_ids=published_ids)

    def labels_for(id):
        if not id.startswith("vtmb:model:") or any(part in ("", ".", "..") for part in id[11:].split("/")) or "\\" in id:
            raise ValueError(f"invalid placed model identity: {id}")
        unit = read_document(root / "models" / (id[11:] + ".glb"))["extensions"]["ELYSIUM_vtmb_model"]
        if unit["identity"]["asset"] != id:
            raise ValueError(f"placed model source identity differs: {id}")
        return [row["label"] for row in unit["mdl"]["sequences"]] if unit["mdl"]["bones"] else []

    return placed_animation_models(placements, labels_for)
