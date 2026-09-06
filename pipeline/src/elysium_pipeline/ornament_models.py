"""Ornament model demand recovered from animation events 4100/4102.

`CBaseCombatCharacter::HandleAnimEvent` (vampire.dll `0x1032e330`) answers three events with
the `m_hAnimFollowModel` handle at `this+0x5a8`: 4100 (`0x1004`) and 4102 (`0x1006`) release the
previous ornament and spawn a new `prop_dynamic_ornament` parented to the character, 4101
(`0x1005`) only releases it. The model path is `sprintf`'d from the event's own options string:

    1032e444  PUSH [EDI+4]              ; event->options
    1032e448  PUSH 0x10620300           ; "%s.mdl"                       (4100)

    1032e41d  CALL 0x100092be           ; CBaseCombatCharacter::IsMale
    1032e424  MOV EAX,0x105994a0        ; "male"
    1032e42b  MOV EAX,0x10599490        ; "female"
    1032e430  MOV EDX,[EDI+4]           ; event->options
    1032e433  PUSH EAX / PUSH EDX / PUSH 0x10620308  ; "%s_%s.mdl"       (4102)

The options string is used verbatim. Retail never strips an extension it already carries, so
`models/scenery/misc/wineglass/wineglass.mdl` under 4102 really does ask for
`models/scenery/misc/wineglass/wineglass.mdl_male.mdl` -- and the Unofficial Patch ships exactly
that file, so the typo is load-bearing and is reproduced here rather than repaired.

The demand is read from the published V2 model corpus (`extensions.ELYSIUM_vtmb_model.mdl`
`sequences[i].events`), never from the install. A named path with no published unit is a source
gap carried into the catalogue, not a staging failure: `models/items/walkie_talkie.mdl` is asked
for by `items/walkie_talkie/walkie_talkie`'s own `Crooked_Cop_Walkie_Talkie_Into` and exists in
no VPK, so retail's own spawn silently answers nothing there too.
"""
from __future__ import annotations

from pathlib import Path

#: Event ids as `HandleAnimEvent`'s switch spells them.
ATTACH_EVENT = 4100
DETACH_EVENT = 4101
GENDERED_ATTACH_EVENT = 4102
ATTACH_EVENTS = (ATTACH_EVENT, GENDERED_ATTACH_EVENT)

#: The two literals at 0x105994a0 / 0x10599490, in the order the branch selects them.
GENDERS = ("male", "female")


class OrnamentDemandError(ValueError):
    """An ornament request cannot be expanded into an addressable model path."""


def model_key(value):
    """The lookup key: the retail-formatted path, lower-cased and forward-slashed.

    Nothing else is folded. The `models/` prefix is NOT synthesised -- the option is a game-root
    relative path and an option that omitted the prefix would name a different file.
    """
    return str(value).strip().replace("\\", "/").lower()


def event_model_paths(event, options):
    """Retail's own `sprintf` for one event record, as a list of lookup keys.

    4100 formats `"%s.mdl"`; 4102 formats `"%s_%s.mdl"` once per gender word. An empty options
    string is retail's own no-op (the formatted name resolves to nothing), so it yields nothing.
    """
    if event not in ATTACH_EVENTS:
        raise OrnamentDemandError(f"event {event} does not spawn an ornament model")
    text = str(options or "").strip()
    if not text:
        return []
    if event == ATTACH_EVENT:
        return [model_key(text + ".mdl")]
    return [model_key(f"{text}_{gender}.mdl") for gender in GENDERS]


def _record(event):
    """(cycle, event, type, options) from either the GLB JSON row or `mdl_skel.Event`."""
    if isinstance(event, dict):
        return (float(event.get("cycle", 0.)), int(event["event"]),
                int(event.get("type", 0)), str(event.get("options") or ""))
    return (float(event.cycle), int(event.event), int(event.type), str(event.options or ""))


def _label(sequence):
    return sequence["label"] if isinstance(sequence, dict) else sequence.label


def _events(sequence):
    return (sequence.get("events") or ()) if isinstance(sequence, dict) else (sequence.events or ())


def collect_requests(sequences, owner):
    """Every 4100/4102 record one model's sequences carry, in descriptor order.

    `sequences` are either the GLB `mdl.sequences` rows or decoded `mdl_skel.Seq` records; the
    dispatcher fires an ordered array, so the order here is the descriptor's own.
    """
    requests = []
    for index, sequence in enumerate(sequences):
        label = _label(sequence)
        for slot, raw in enumerate(_events(sequence)):
            cycle, event, type, options = _record(raw)
            if event not in ATTACH_EVENTS:
                continue
            requests.append({"owner": owner, "sequence": label, "sequenceIndex": index,
                             "eventIndex": slot, "cycle": cycle, "event": event,
                             "type": type, "options": options,
                             "paths": event_model_paths(event, options)})
    return requests


def _asset_id(path):
    from elysium_pipeline.formats.model_glb.model import ModelIdentityError, asset_id

    try:
        return asset_id(path)
    except ModelIdentityError as error:
        raise OrnamentDemandError(f"ornament path {path!r} is not addressable: {error}") from error


def resolve(requests, *, published_ids=None, export_root=None):
    """Join collected requests onto published model units.

    `published_ids` is the complete published-id set; when it is None the units are looked up on
    disk under `export_root/models`. A path with no unit is a source gap, never a failure.
    """
    if published_ids is None and export_root is None:
        raise OrnamentDemandError("supply published ids or an export root to resolve against")
    root = Path(export_root) / "models" if export_root is not None else None
    rows = {}
    for request in requests:
        for path in request["paths"]:
            id = _asset_id(path)
            row = rows.setdefault(path, {
                "path": path, "assetId": id, "sourceAbsent": False,
                "events": [], "genders": [], "options": [], "requests": []})
            if row["assetId"] != id:
                raise OrnamentDemandError(f"ornament path {path} resolves to two identities")
            if request["event"] not in row["events"]:
                row["events"].append(request["event"])
            if request["options"] not in row["options"]:
                row["options"].append(request["options"])
            if request["event"] == GENDERED_ATTACH_EVENT:
                gender = GENDERS[request["paths"].index(path)]
                if gender not in row["genders"]:
                    row["genders"].append(gender)
            row["requests"].append({key: request[key] for key in (
                "owner", "sequence", "sequenceIndex", "eventIndex", "cycle", "event", "type", "options")})
    models, gaps, model_ids = [], [], set()
    for path in sorted(rows):
        row = rows[path]
        key = row["assetId"].removeprefix("vtmb:model:")
        present = (row["assetId"] in published_ids if published_ids is not None
                   else (root / (key + ".glb")).is_file())
        row["sourceAbsent"] = not present
        row["events"].sort()
        row["genders"].sort()
        row["options"].sort()
        if present:
            model_ids.add(row["assetId"])
        else:
            gaps.append({"path": path, "assetId": row["assetId"],
                         "owners": sorted({r["owner"] for r in row["requests"]}),
                         "reason": "animation event names a model absent from the published source"})
        models.append(row)
    return {"models": models, "modelIds": sorted(model_ids), "sourceGaps": gaps,
            "requestCount": sum(len(r["requests"]) for r in models)}


def walk_units(export_root):
    """(assetId, sequences) for every published model unit; JSON chunk reads only."""
    from elysium_pipeline.formats.model_glb.model import MODEL_EXTENSION
    from elysium_pipeline.skeletal_stage.unit import read_document

    for path in sorted((Path(export_root) / "models").rglob("*.glb")):
        extension = read_document(path)["extensions"][MODEL_EXTENSION]
        yield extension["identity"]["asset"], extension["mdl"]["sequences"]


def discover(export_root, published_ids=None):
    """The whole corpus' ornament demand. Callers already walking the units use `collect_requests`."""
    requests = []
    for owner, sequences in walk_units(export_root):
        requests.extend(collect_requests(sequences, owner))
    return resolve(requests, published_ids=published_ids, export_root=export_root)
