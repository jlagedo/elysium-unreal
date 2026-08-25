"""Turn a `life_rig_resolution` Frida session into the retail rig-resolution guide.

The capture answers three questions per action and this reads them back as one
table. `vampire.get_model_ptr` and `client.get_studio_hdr` name the body's rig
set -- slot -1 is its own model, slots 0 and 1 the two extra animation-model
slots -- and each studiohdr names itself at `Name`@12. The activity ladder and
the weighted draws name the global sequence number they committed, an index into
the space `LookupSequence` builds by walking those three slots in order.
`client.resolve_sequence_owner` then recurses through the include-model groups
until the number falls inside a bank's own `NumLocalSeq`@272, so the bank that
actually owns the clip is read rather than guessed -- which is what settles a
label the shared banks repeat (`docs/vtmb/animation_and_movers.md` A.7).

Only the leaf label is resolved offline, off the owning bank's own file in the
owner's install. The activity enum ids are named the same way: a committed
sequence's descriptor carries `szactivitynameindex`@4, so joining the captured
enum against the chosen descriptor recovers the id/name pairs the session
exercised.

Usage:
    uv run elysium research analyze_rig_resolution
    uv run elysium research analyze_rig_resolution --session <capture directory>
"""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass, field
import json
from pathlib import Path
import struct
import sys
from typing import Any, Iterable

from elysium_pipeline.formats import install, mdl_skel
from elysium_pipeline.paths import research_root


#: Both recipes answer the same question and write the same records; the chaos variant
#: only keys its selection targets per entity so a crowd fits the budget.
RECIPES = ("life_rig_resolution", "life_rig_chaos")
RECIPE = RECIPES[0]

#: The server hooks whose return value is a committed global sequence number.
SEQUENCE_PRODUCERS = (
    "vampire.select_weighted_sequence",
    "vampire.select_heaviest_sequence",
    "vampire.player_select_melee_sequence",
    "vampire.npc_choose_melee_sequence",
    "vampire.lookup_sequence",
)

#: The server hooks whose return value is an activity enum rather than a
#: sequence: the weapon's translation and its override both answer one.
ACTIVITY_PRODUCERS = (
    "vampire.weapon_translate_activity",
    "vampire.weapon_activity_override",
)

#: The hooks whose first argument is an activity enum. Nothing else may be read
#: as one: `lookup_sequence` takes a `char*` there, and reading that pointer as
#: an id would invent an activity out of an address.
ACTIVITY_ARGUMENT = (
    "vampire.select_weighted_sequence",
    "vampire.select_heaviest_sequence",
    "vampire.weapon_translate_activity",
    "vampire.weapon_activity_override",
    "vampire.weapon_melee_request_activity",
    "vampire.player_select_melee_sequence",
    "vampire.npc_choose_melee_sequence",
)

#: A hook whose answer is not its return value. `ChooseMeleeAttackSequence` returns a bool in
#: AL and writes the chosen sequence through its fourth argument, so reading `eax` as an index
#: would record a truncated boolean as a sequence number. The tuple is (the stack word carrying
#: the requested activity, the leave-phase field carrying the answer).
OUT_PARAMETER_TARGETS = {
    "vampire.npc_choose_melee_sequence": (3, "chosen_sequence"),
}

#: A discrete player action, in the order the capture can be read as a story.
ACTION_TARGETS = (
    "vampire.weapon_melee_primary_attack",
    "vampire.weapon_melee_heavy_attack",
    "vampire.weapon_melee_request_activity",
    "vampire.viewmodel_ranged_shot",
    "vampire.viewmodel_dry_fire",
    "vampire.viewmodel_reload_request",
    "vampire.viewmodel_ranged_anim_event",
    "vampire.apply_player_activity_and_sequence",
)


def _signed(value: int) -> int:
    return value - 0x100000000 if value >= 0x80000000 else value


def _word(record: dict[str, Any], index: int) -> int | None:
    words = record.get("stack_words") or []
    if index >= len(words) or words[index] is None:
        return None
    return _signed(int(str(words[index]), 16))


def _float_word(record: dict[str, Any], index: int) -> float | None:
    words = record.get("stack_words") or []
    if index >= len(words) or words[index] is None:
        return None
    raw = int(str(words[index]), 16) & 0xFFFFFFFF
    return struct.unpack("<f", struct.pack("<I", raw))[0]


def _return(record: dict[str, Any] | None) -> int | None:
    if record is None or record.get("return_value") is None:
        return None
    return _signed(int(str(record["return_value"]), 16) & 0xFFFFFFFF)


def _text(value: Any) -> str | None:
    """A field read that failed carries its reason instead of a value."""
    return value if isinstance(value, str) else None


@dataclass
class Bank:
    """One installed `.mdl`, read only for the names a captured index resolves to."""

    key: str
    present: bool
    labels: list[str] = field(default_factory=list)
    activities: list[str] = field(default_factory=list)

    def label(self, index: int) -> str | None:
        return self.labels[index] if 0 <= index < len(self.labels) else None

    def activity(self, index: int) -> str | None:
        name = self.activities[index] if 0 <= index < len(self.activities) else None
        return name or None

    def owns(self, index: int) -> bool:
        return 0 <= index < len(self.labels)


class BankLibrary:
    """The owner's installed banks, read once each and only when a capture names one."""

    def __init__(self) -> None:
        self._index = install.build_index(dirs=("models",), verbose=False)
        self._banks: dict[str, Bank] = {}
        self.missing: set[str] = set()

    @staticmethod
    def key_for(model_name: str) -> str:
        key = model_name.replace("\\", "/").lower().lstrip("/")
        return key if key.startswith("models/") else "models/" + key

    def bank(self, model_name: str | None) -> Bank | None:
        if not model_name:
            return None
        key = self.key_for(model_name)
        cached = self._banks.get(key)
        if cached is not None:
            return cached
        data = install.read(self._index, key)
        if data is None:
            self.missing.add(key)
            bank = Bank(key=key, present=False)
        else:
            bank = Bank(
                key=key,
                present=True,
                labels=mdl_skel.local_sequence_labels(data),
                activities=mdl_skel.local_sequence_activities(data),
            )
        self._banks[key] = bank
        return bank


def load_session(path: Path) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    manifest_path = path / "manifest.json"
    events_path = path / "events.jsonl"
    if not events_path.is_file():
        raise FileNotFoundError(events_path)
    manifest = (
        json.loads(manifest_path.read_text(encoding="utf-8"))
        if manifest_path.is_file()
        else {}
    )
    events = []
    for line in events_path.read_text(encoding="utf-8").splitlines():
        if line.strip():
            events.append(json.loads(line))
    return manifest, events


def latest_session() -> Path:
    root = research_root() / "frida"
    candidates = sorted(
        (
            path
            for recipe in RECIPES
            for path in root.glob(f"*-{recipe}")
            if (path / "events.jsonl").is_file()
        ),
        key=lambda path: path.name,
    )
    if not candidates:
        raise FileNotFoundError(
            f"no {' or '.join(RECIPES)} capture exists below {root}; run "
            f"`uv run elysium research frida_probe attach --recipe {RECIPE}` first"
        )
    return candidates[-1]


def _pair(events: Iterable[dict[str, Any]]) -> tuple[
    list[dict[str, Any]], dict[tuple[str, int], dict[str, Any]]
]:
    calls: list[dict[str, Any]] = []
    returns: dict[tuple[str, int], dict[str, Any]] = {}
    for event in events:
        kind = event.get("kind")
        if kind == "call":
            calls.append(event)
        elif kind == "return":
            returns[(event["target"], event["sequence"])] = event
    calls.sort(key=lambda event: event["sequence"])
    return calls, returns


def rig_sets(calls: list[dict[str, Any]], returns: dict[tuple[str, int], dict[str, Any]]):
    """Per entity, the slot -> bank triple every model resolution named.

    Both sides are kept apart on purpose: the server picks the sequence and the
    client builds the pose, and a disagreement between their slot tables would be
    the finding rather than noise to merge away.
    """
    rows = []
    for call in calls:
        target = call["target"]
        if target not in ("vampire.get_model_ptr", "client.get_studio_hdr"):
            continue
        answer = returns.get((target, call["sequence"]))
        model = _text(((answer or {}).get("fields") or {}).get("model"))
        rows.append(
            {
                "sequence": call["sequence"],
                "side": "server" if target.startswith("vampire.") else "client",
                "entity": call["ecx"],
                "slot": _word(call, 1),
                "studiohdr": (answer or {}).get("return_value"),
                "model": model,
            }
        )
    return rows


def ownership_chains(
    calls: list[dict[str, Any]],
    library: BankLibrary,
    live_map: dict[tuple[str, int], dict[str, Any]] | None = None,
):
    """Rebuild each `resolve_sequence_owner` recursion into one ownership row.

    Frida reports the interceptor nesting depth, so a chain is a run of calls on
    one thread whose depth keeps climbing; a run ends where the depth falls back.
    A chain whose first link is not the outermost one is reported as orphaned
    rather than silently reparented -- the on-change key can suppress a repeated
    parent while a fresh child still emits.
    """
    chains: list[list[dict[str, Any]]] = []
    current: list[dict[str, Any]] = []
    for call in calls:
        if call["target"] != "client.resolve_sequence_owner":
            continue
        depth = call.get("depth")
        if current and (
            call.get("thread_id") != current[-1].get("thread_id")
            or depth is None
            or current[-1].get("depth") is None
            or depth <= current[-1]["depth"]
        ):
            chains.append(current)
            current = []
        current.append(call)
    if current:
        chains.append(current)

    rows = []
    for chain in chains:
        links = []
        for call in chain:
            model = _text((call.get("fields") or {}).get("model"))
            links.append({"model": model, "index": _word(call, 4)})
        head, leaf = links[0], links[-1]
        live = (live_map or {}).get(
            (BankLibrary.key_for(head["model"]), head["index"])
            if head["model"] and head["index"] is not None
            else ("", -1)
        )
        if live is not None and len(links) == 1 or (
            live is not None
            and not library.bank(leaf["model"]).owns(leaf["index"] or -1)
        ):
            leaf = {"model": live["owner_model"], "index": int(live["owner_index"])}
        bank = library.bank(leaf["model"])
        rows.append(
            {
                "sequence": chain[0]["sequence"],
                "requested_model": head["model"],
                "requested_index": head["index"],
                "owner_model": leaf["model"],
                "owner_index": leaf["index"],
                "depth": len(links),
                "label": bank.label(leaf["index"]) if bank else None,
                "activity_name": bank.activity(leaf["index"]) if bank else None,
                "bank_installed": None if bank is None else bank.present,
                "owner_resolved": bool(
                    bank is not None
                    and leaf["index"] is not None
                    and bank.owns(leaf["index"])
                ),
                "orphan": chain[0].get("depth") != min(
                    (call.get("depth") for call in chain if call.get("depth") is not None),
                    default=None,
                ),
                "chain": " -> ".join(
                    f"{link['model']}#{link['index']}" for link in links
                ),
            }
        )
    return rows


def sequence_map(session: Path) -> dict[tuple[str, int], dict[str, Any]]:
    """The live global-index table `capture_rig_remap` read, if it ran.

    An ownership chain is only as complete as the calls that survived the
    capture's change keys, so a repeated inner link can be suppressed and leave
    its parent looking unowned. This table has no such gap: it resolves every
    number a body answers, from the group ranges the engine filled at load.
    """
    path = session / "sequence_map.csv"
    if not path.is_file():
        return {}
    table: dict[tuple[str, int], dict[str, Any]] = {}
    with path.open(encoding="utf-8", newline="") as stream:
        for row in csv.DictReader(stream):
            if row.get("resolved") != "True":
                continue
            key = (BankLibrary.key_for(row["body_model"]), int(row["global_index"]))
            table[key] = row
    return table


def _owner_lookup(ownership: list[dict[str, Any]]) -> dict[tuple[str, int], dict[str, Any]]:
    lookup: dict[tuple[str, int], dict[str, Any]] = {}
    for row in ownership:
        key = (row["requested_model"] or "", row["requested_index"])
        lookup.setdefault(key, row)
        # The server names a sequence against the body's own model, which is the
        # chain head; keying on the head alone lets a selection join without
        # knowing which client entity built the pose.
        if row["requested_index"] is not None:
            lookup.setdefault(("", row["requested_index"]), row)
    return lookup


def selections(
    calls: list[dict[str, Any]],
    returns: dict[tuple[str, int], dict[str, Any]],
    ownership: list[dict[str, Any]],
    live_map: dict[tuple[str, int], dict[str, Any]] | None = None,
):
    """The ordered guide: one row per action, draw or commit the session produced."""
    owners = _owner_lookup(ownership)
    bodies: dict[str, str] = {}
    slots: dict[str, dict[int, str]] = {}
    rows = []
    for call in calls:
        target = call["target"]
        if target == "vampire.get_model_ptr":
            answer = returns.get((target, call["sequence"]))
            model = _text(((answer or {}).get("fields") or {}).get("model"))
            slot = _word(call, 1)
            if slot is not None and model:
                slots.setdefault(call["ecx"], {})[slot] = model
                if slot < 0:
                    bodies[call["ecx"]] = model
            continue
        if target not in SEQUENCE_PRODUCERS + ACTIVITY_PRODUCERS + ACTION_TARGETS:
            continue
        answer = returns.get((target, call["sequence"]))
        fields = call.get("fields") or {}
        result = _return(answer)
        body = bodies.get(call["ecx"])
        entity_slots = slots.get(call["ecx"], {})
        out_parameter = OUT_PARAMETER_TARGETS.get(target)
        if out_parameter is not None:
            activity_word, answer_label = out_parameter
            answered = ((answer or {}).get("fields") or {}).get(answer_label)
            result = answered if isinstance(answered, int) else None
        row = {
            "sequence": call["sequence"],
            "target": target,
            "entity": call["ecx"],
            "body_model": body,
            "anim_slot0": entity_slots.get(0),
            "anim_slot1": entity_slots.get(1),
            "curtime": fields.get("curtime"),
            "buttons": fields.get("buttons"),
            "buttons_pressed": fields.get("buttons_pressed"),
            "active_weapon": fields.get("active_weapon"),
            "activity": fields.get("activity"),
            "ideal_activity": fields.get("ideal_activity"),
            "player_sequence": fields.get("sequence"),
            "requested": (
                _word(call, out_parameter[0]) if out_parameter is not None
                else _word(call, 1) if target in ACTIVITY_ARGUMENT
                else None
            ),
            "label_argument": _text(fields.get("label")),
            "result": result,
            "result_kind": (
                "sequence" if out_parameter is not None
                else "activity" if target in ACTIVITY_PRODUCERS
                else "sequence" if target in SEQUENCE_PRODUCERS
                else "call"
            ),
            "resolved_label": None,
            "resolved_owner": None,
            "resolved_activity_name": None,
            "resolved_from": None,
        }
        if row["result_kind"] == "sequence" and result is not None and result >= 0:
            live = (live_map or {}).get(
                (BankLibrary.key_for(body), result) if body else ("", result)
            )
            if live is not None:
                row["resolved_label"] = live["label"]
                row["resolved_owner"] = live["owner_model"]
                row["resolved_activity_name"] = live["activity_name"] or None
                row["resolved_from"] = "live_map"
            else:
                owner = owners.get((body or "", result)) or owners.get(("", result))
                if owner is not None:
                    row["resolved_label"] = owner["label"]
                    row["resolved_owner"] = owner["owner_model"]
                    row["resolved_activity_name"] = owner["activity_name"]
                    row["resolved_from"] = "chain"
        rows.append(row)
    return rows


def blend_contributions(calls: list[dict[str, Any]], library: BankLibrary):
    """Every distinct pose contribution the client accumulated, with its weight."""
    rows = []
    for call in calls:
        if call["target"] != "client.accumulate_sequence_pose":
            continue
        model = _text((call.get("fields") or {}).get("model"))
        index = _word(call, 4)
        bank = library.bank(model)
        rows.append(
            {
                "sequence": call["sequence"],
                "model": model,
                "index": index,
                "weight": _float_word(call, 7),
                "label": bank.label(index) if bank and index is not None else None,
            }
        )
    return rows


def activity_table(selection_rows: list[dict[str, Any]]):
    """The enum ids this session exercised, named by the descriptor they committed.

    VtMB resolves an activity literal to an enum at model load, so the id is a
    property of the running process and not of any file. A committed sequence
    carries the literal, which makes every committed row one recovered pair.
    """
    counts: dict[tuple[int, str], int] = {}
    for row in selection_rows:
        if row.get("target") not in ACTIVITY_ARGUMENT:
            continue
        name = row.get("resolved_activity_name")
        requested = row.get("requested")
        if name and isinstance(requested, int) and requested >= 0:
            counts[(requested, name)] = counts.get((requested, name), 0) + 1
    return [
        {"activity_id": key[0], "activity_name": key[1], "observations": value}
        for key, value in sorted(counts.items())
    ]


def _write_csv(path: Path, rows: list[dict[str, Any]], columns: list[str]) -> None:
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=columns, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def analyze(session: Path) -> int:
    manifest, events = load_session(session)
    if manifest and manifest.get("recipe") not in (None,) + RECIPES:
        print(
            f"WARNING - session {session.name} was captured with recipe "
            f"{manifest.get('recipe')!r}, not {RECIPE!r}",
            file=sys.stderr,
        )
    calls, returns = _pair(events)
    if not calls:
        print(
            f"WARNING - no hook calls in {session}; the capture recorded "
            "no resolution to read",
            file=sys.stderr,
        )
        return 2

    library = BankLibrary()
    live_map = sequence_map(session)
    rigs = rig_sets(calls, returns)
    ownership = ownership_chains(calls, library, live_map)
    selection_rows = selections(calls, returns, ownership, live_map)
    blend = blend_contributions(calls, library)
    activities = activity_table(selection_rows)

    _write_csv(
        session / "rig_sets.csv",
        rigs,
        ["sequence", "side", "entity", "slot", "studiohdr", "model"],
    )
    _write_csv(
        session / "ownership.csv",
        ownership,
        [
            "sequence", "requested_model", "requested_index", "owner_model",
            "owner_index", "label", "activity_name", "depth", "bank_installed",
            "owner_resolved", "orphan", "chain",
        ],
    )
    _write_csv(
        session / "resolution.csv",
        selection_rows,
        [
            "sequence", "target", "entity", "body_model", "anim_slot0", "anim_slot1",
            "curtime", "buttons", "buttons_pressed", "active_weapon", "activity",
            "ideal_activity", "player_sequence", "requested", "label_argument",
            "result", "result_kind", "resolved_label", "resolved_owner",
            "resolved_activity_name", "resolved_from",
        ],
    )
    _write_csv(
        session / "blend.csv", blend, ["sequence", "model", "index", "weight", "label"]
    )
    _write_csv(
        session / "activities.csv",
        activities,
        ["activity_id", "activity_name", "observations"],
    )

    bodies = sorted({row["model"] for row in rigs if row["slot"] == -1 and row["model"]})
    slot_banks = sorted(
        {row["model"] for row in rigs if row["slot"] is not None
         and row["slot"] >= 0 and row["model"]}
    )
    banks = sorted(
        {
            row["owner_model"]
            for row in ownership
            if row["owner_model"] and row["owner_model"] != row["requested_model"]
        }
    )
    animating = sorted(
        {
            row["requested_model"]
            for row in ownership
            if row["requested_model"] and row["owner_model"] != row["requested_model"]
        }
    )
    unresolved = [row for row in ownership if not row["owner_resolved"]]
    summary = {
        "session": str(session),
        "events": len(events),
        "calls": len(calls),
        "bodies": bodies,
        "bodies_reaching_a_bank": animating,
        "animation_banks": banks,
        "extra_slot_banks": slot_banks,
        "ownership_chains": len(ownership),
        "ownership_unresolved": len(unresolved),
        "ownership_orphans": sum(1 for row in ownership if row["orphan"]),
        "selection_rows": len(selection_rows),
        "blend_contributions": len(blend),
        "recovered_activities": len(activities),
        "live_sequence_map": len(live_map),
        "banks_missing_from_install": sorted(library.missing),
        "agent_summary": manifest.get("summary"),
    }
    (session / "rig_resolution.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )

    print(f"Rig-resolution guide: {session}")
    # The full model census is in rig_resolution.json; a session sees every
    # scenery prop that resolved a pose, and printing all of them buries the
    # bodies that actually reached a shared bank.
    print(f"  models seen           {len(bodies)}")
    print(f"  bodies reaching banks {len(animating)}")
    for model in animating:
        print(f"    - {model}")
    print(f"  animation banks       {len(banks)}")
    for model in banks:
        print(f"    - {model}")
    print(f"  extra-slot banks      {len(slot_banks)}")
    for model in slot_banks:
        print(f"    - {model}")
    print(f"  ownership chains      {len(ownership)} ({len(unresolved)} unresolved)")
    print(f"  selection rows        {len(selection_rows)}")
    print(f"  blend contributions   {len(blend)}")
    print(f"  recovered activities  {len(activities)}")
    if library.missing:
        print(
            "WARNING - the install has no such bank: "
            + ", ".join(sorted(library.missing)),
            file=sys.stderr,
        )
    if unresolved:
        print(
            f"WARNING - {len(unresolved)} ownership chains ended on an index the "
            "owning bank does not carry; read ownership.csv",
            file=sys.stderr,
        )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--session",
        type=Path,
        help="A capture directory; the newest matching one is used when omitted.",
    )
    arguments = parser.parse_args()
    session = arguments.session.resolve() if arguments.session else latest_session()
    if not session.is_dir():
        raise NotADirectoryError(session)
    return analyze(session)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
    except Exception as exc:  # noqa: BLE001 - the CLI reports its own failure
        print(f"WARNING - rig-resolution analysis failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
