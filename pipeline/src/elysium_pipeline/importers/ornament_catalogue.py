"""Cooked 4100/4102 ornament model join, keyed by the retail-formatted model path.

The runtime formats the same string `CBaseCombatCharacter::HandleAnimEvent` formats
(`vampire.dll 0x1032e330`), lower-cases it and looks the row up here; nothing on the native side
re-derives a path or guesses a package. A row whose source model exists in no VPK is carried with
`sourceAbsent`, so "retail asked for a file that never shipped" stays distinguishable from
"the bake did not produce this yet". The result is passed to
`UElysiumOrnamentCatalogue::ApplyJson` at editor finalization.
"""
from elysium_pipeline.importers.catalogue_common import catalogue, evidence, native_ref, unique
from elysium_pipeline.ornament_models import ATTACH_EVENTS, GENDERS

KIND = "OrnamentModels"


def project_ornament_catalogue(demand, entries, bones_for):
    """`demand` is `ornament_models.resolve`; `entries` the staged character manifest rows.

    `bones_for(assetId)` answers the model's native reference-skeleton bone names, in the
    skeleton's own order. Ornaments are bone-merge rigs -- a proper subset of the character
    skeleton plus the prop's own bone -- so the bone list is what the runtime binds by.
    """
    unique(entries.values(), "assetId")
    models = {}
    for row in demand["models"]:
        path, id = row["path"], row["assetId"]
        if path in models:
            raise ValueError(f"duplicate ornament path key: {path}")
        if not path.endswith(".mdl") or path != path.lower() or "\\" in path:
            raise ValueError(f"noncanonical ornament path key: {path}")
        if any(event not in ATTACH_EVENTS for event in row["events"]) or not row["events"]:
            raise ValueError(f"{path}: ornament row does not name a spawning event")
        if any(gender not in GENDERS for gender in row["genders"]):
            raise ValueError(f"{path}: unknown gender word")
        mesh = skeleton = ""
        bones = []
        if row["sourceAbsent"]:
            if id in entries:
                raise ValueError(f"{path}: source gap has a staged unit after all")
        else:
            entry = entries.get(id)
            if entry is None:
                raise ValueError(f"{path}: ornament model {id} is not in the character stage")
            if not entry.get("meshAsset") or not entry.get("skeletonAsset"):
                raise ValueError(f"{path}: ornament model {id} was staged without its own mesh/skeleton")
            mesh = native_ref(id, "SK", entry["meshAsset"])
            skeleton = native_ref(id, "SKEL", entry["skeletonAsset"])
            bones = list(bones_for(id))
            if not bones or len(set(name.casefold() for name in bones)) != len(bones):
                raise ValueError(f"{path}: ornament model {id} has no distinct bone inventory")
        models[path] = {
            "assetId": id, "mesh": mesh, "skeleton": skeleton,
            "sourceAbsent": bool(row["sourceAbsent"]), "bones": bones,
            "events": list(row["events"]), "genders": list(row["genders"]),
            "options": list(row["options"]), "requestCount": len(row["requests"]),
            "sourceEvidence": evidence(row["requests"]),
        }
    return catalogue(KIND, {"models": models, "sourceGaps": evidence(demand["sourceGaps"]),
                            "requestCount": demand["requestCount"]})
