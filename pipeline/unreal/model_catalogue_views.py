"""Plain editor views of cooked model catalogues; no source files or retained UObject wrappers."""
from elysium_pipeline.asset_paths import baked_unit
from elysium_pipeline.placed_models import fnv1a_32


def model_id(value):
    value = str(value).strip().replace("\\", "/").lower()
    if value.startswith("vtmb:model:"):
        result = value
    elif value.startswith("models/") and value.endswith(".mdl"):
        result = "vtmb:model:" + value[7:-4]
    else:
        raise ValueError("expected a model ID or full source model path: " + value)
    baked_unit(result, "SM")  # Validate every segment through the common resolver.
    return result


def static_mesh_path(value):
    return baked_unit(model_id(value), "SM")


def _get(value, name):
    return value.get_editor_property(name)


def asset_path(value):
    if value is None:
        return ""
    if isinstance(value, str):
        path = value
    elif hasattr(value, "get_path_name"):
        path = value.get_path_name()
    elif hasattr(value, "to_string"):
        path = value.to_string()
    else:
        raise ValueError("unsupported native catalogue asset reference")
    if path and not path.startswith("/"):
        raise ValueError("invalid native catalogue asset reference: " + path)
    return path


def placed_view(asset, selected_ids=None):
    if asset is None:
        raise ValueError("cooked placed-model catalogue is absent; import model catalogues first")
    result = {}
    for key, model in _get(_get(asset, "data"), "models").items():
        id = model_id(key)
        if selected_ids is not None and id not in selected_ids:
            continue
        if id != str(_get(model, "asset_id")) or _get(model, "acceptance_issues"):
            raise ValueError("unaccepted placed-model catalogue row: " + id)
        clips = [{"name": str(_get(clip, "label")), "weight": int(_get(clip, "weight")),
                  "sequence": asset_path(_get(clip, "sequence")),
                  "baseCell": asset_path(_get(clip, "base_cell"))}
                 for clip in _get(model, "clips")]
        candidates = list(_get(model, "rest_candidates"))
        if any(type(i) is not int or not 0 <= i < len(clips) for i in candidates):
            raise ValueError("invalid cooked rest candidate index: " + id)
        source_absent = bool(_get(model, "source_absent"))
        has_cloth = bool(_get(model, "has_cloth"))
        static_mesh = asset_path(_get(model, "static_mesh"))
        rest_suffices = bool(_get(model, "static_rest_suffices"))
        # `FElysiumCataloguePlacedModel::CanUseStatic(false)`, mirrored: the static-source lane
        # (a static/rigid-shape unit on its static mesh, unproven) or a proven rest equivalence.
        # The bake decides static-vs-skeletal placement on exactly what the runtime decides on.
        can_use_static = (not source_absent and not has_cloth and bool(static_mesh)
                          and bool(_get(model, "static_topology_equivalent"))
                          and (bool(_get(model, "static_source_representation"))
                               or (rest_suffices and bool(_get(model, "static_equivalent_proven"))
                                   and bool(_get(model, "static_equivalent")))))
        result[id] = {"model": str(_get(model, "model_path")),
                      "sourceAbsent": source_absent,
                      "staticRestSuffices": rest_suffices,
                      "canUseStatic": can_use_static,
                      "hasCloth": has_cloth,
                      "mesh": asset_path(_get(model, "skeletal_mesh")),
                      "staticMesh": static_mesh,
                      "clips": clips, "restCandidates": candidates}
    return result


def select_rest(record, placement_token):
    candidates = [record["clips"][i] for i in record["restCandidates"]]
    if not candidates:
        raise ValueError("model has no cooked rest candidate: " + record["model"])
    total = sum(max(1, clip["weight"]) for clip in candidates)
    pick = fnv1a_32(record["model"], placement_token) % total
    for clip in candidates:
        weight = max(1, clip["weight"])
        if pick < weight:
            return clip
        pick -= weight
    raise ValueError("invalid cooked rest weights")


def skin_view(asset, selected_ids=None):
    if asset is None:
        raise ValueError("cooked model skin catalogue is absent; import model catalogues first")
    result = {}
    for key, model in _get(_get(asset, "data"), "models").items():
        id = model_id(key)
        if selected_ids is not None and id not in selected_ids:
            continue
        if id != str(_get(model, "asset_id")):
            raise ValueError("skin model identity mismatch: " + id)
        for representation in _get(model, "representations"):
            kind = str(_get(representation, "kind"))
            if kind not in ("static", "skeletal") or (id, kind) in result:
                raise ValueError("invalid or duplicate skin representation: " + id)
            families = []
            for index, family in enumerate(_get(representation, "families")):
                if _get(family, "index") != index:
                    raise ValueError("cooked skin family order differs: " + id)
                cells = {int(_get(c, "skin_reference")): asset_path(_get(c, "material"))
                         for c in _get(family, "cells")}
                if len(cells) != len(_get(family, "cells")):
                    raise ValueError("duplicate cooked skin source column: " + id)
                slots = []
                for slot in _get(representation, "slots"):
                    paths = {cells.get(int(column), "") for column in _get(slot, "skin_references")}
                    if len(paths) != 1 or "" in paths:
                        raise ValueError("skin render slot has unresolved or conflicting source columns: " + id)
                    slots.append((int(_get(slot, "index")), str(_get(slot, "slot_name")), paths.pop()))
                if [slot[0] for slot in slots] != list(range(len(slots))):
                    raise ValueError("cooked skin render slot order differs: " + id)
                families.append(slots)
            if len(families) != int(_get(model, "family_count")):
                raise ValueError("cooked skin family inventory differs: " + id)
            result[id, kind] = families
    return result
