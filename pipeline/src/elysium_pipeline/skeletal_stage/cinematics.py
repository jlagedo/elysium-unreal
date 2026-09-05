"""Cinematic actor projections over decoded bones and sampled clips."""
from elysium_pipeline.asset_names import rig_bone_name
from elysium_pipeline.skeletal_stage.payload import _single_root
from elysium_pipeline.skeletal_stage import payload, sequences
from elysium_pipeline.formats import mdl_skel


def actor_rows(sub, root):
    """One actor's bones as (rows, {StudioBone index: emitted index}, reparented), prefix
    folded to `Bip01`. `reparented` is `_single_root`'s fold record restated by ORIGINAL
    StudioBone index, the shape the clip writer addresses bones by.

    Folded for the reason the glb half folds: a scene's `bonerename "BipNN" "Bip01"` is how an
    actor picks its own skeleton out of the shared performance, so writing the bank under the
    ordinary names is what lets the existing bone-name binding apply it to an ordinary body with
    no runtime rule of its own.

    Parents are remapped into the subset. A subset normally has exactly one bone whose parent
    lies outside it -- that actor's own root -- and goes through the same `_single_root`
    resolution as any other multi-rooted rig when it does not.
    """
    low = root.lower()
    order = {b.index: slot for slot, b in enumerate(sub)}
    rows = [(rig_bone_name(("Bip01" + b.name[len(root):])
                           if b.name[:len(root)].lower() == low else b.name),
             order.get(b.parent, -1), b.pos, b.quat)
            for b in sub]
    rows, position_of, reparented = _single_root(rows)
    # `_single_root` keyed its fold record by subset slot; the clip writer addresses bones by
    # original StudioBone index, so restate it the way `order` maps the other direction.
    original_of = {slot: index for index, slot in order.items()}
    return (rows, {index: position_of[slot] for index, slot in order.items()},
            {original_of[stray]: original_of[chosen] for stray, chosen in reparented.items()})


def actor_payloads(unit):
    """Ordered (root, native payload) pairs, from the unit alone; single roots use the main owner."""
    roots = mdl_skel.cinematic_roots(unit.bones)
    if len(roots) <= 1:
        return []
    extra, _ = sequences.blend_clip_plan(unit, unit.sequences)
    result = []
    for root in roots:
        sub = [bone for bone in unit.bones if (mdl_skel._bone_root(bone.name) or "").lower() == root.lower()]
        rows, order, reparented = actor_rows(sub, root)
        bone_map = [-1] * len(unit.bones)
        for index, slot in order.items():
            bone_map[index] = slot
        masks = {}
        animations, count = payload._anim_section(
            unit, unit.bones, unit.sequences + extra, bone_map, len(rows), masks, reparented=reparented)
        if count:
            result.append((root, payload._assemble([
                (b"SKEL", payload._skel_section(rows)),
                (b"MASK", payload._mask_section(masks, len(rows))), (b"ANIM", animations)])))
    return result


def discover_sets(export_root):
    """The legacy scene seed, read losslessly from V2 entity units rather than loose .ents."""
    from elysium_pipeline.skeletal_stage.unit import read_document
    from elysium_pipeline.formats.model_glb.model import asset_id
    result = {}
    for path in sorted((export_root / "maps").glob("*.entities.glb")):
        extension = read_document(path)["extensions"]["ELYSIUM_vtmb_map_entities"]
        for entity in extension["entities"]:
            if entity.get("classname") != "logic_choreographed_scene":
                continue
            values = {row["key"].lower(): row["value"].strip() for row in entity["keyValues"]}
            for key in ("baseanim", "maleanim", "femaleanim"):
                value = values.get(key, "")
                if not value.lower().endswith(".mdl"):
                    continue
                result.setdefault(asset_id(value), []).append({
                    "map": extension["identity"]["asset"], "entity": entity["index"], "key": key})
    return result
