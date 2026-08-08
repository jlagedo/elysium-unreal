# -*- coding: utf-8 -*-
"""Inventory action/animation producers in the exported VtMB corpus.

The common player/NPC activity resolver is only half of the action map.  This
tool records the demand side across the regenerable export corpus:

* authored NPC activity, disposition and weapon-policy fields;
* scripted-sequence, schedule, prop-animation and choreography fields;
* MorphModel, SetModel and Transform routes that change the later animation owner;
* action-facing Source I/O wires, resolved to their target entities; and
* direct action-facing calls in level scripts, dialogue snippets, map Python
  payloads, ``logic_pythoncheck`` expressions, ``usescript`` fields and
  literal ``ScheduleTask`` payloads.

The report deliberately distinguishes activity requests from exact sequence
labels.  It also keeps ignored authoring such as ``demo_sequence`` visible so
an exporter field cannot be mistaken for an engine input.

Read-only unless ``--json`` is supplied.  Generated reports belong below
``$ELYSIUM_WORK_ROOT/research``; game-derived values must not be committed.

Usage::

    uv run elysium research action_animation_survey
    uv run elysium research action_animation_survey --json <external-path>
"""
from __future__ import print_function

import argparse
import ast
import collections
import glob
import io
import json
import os
import re
import tokenize

from elysium_pipeline.paths import export_root

SCRIPT_CLASSES = {"scripted_sequence", "aiscripted_sequence"}
PROP_DYNAMIC_CLASSES = {"prop_dynamic", "prop_dynamic_ornament", "prop_dynamic-wesp"}
SCRIPT_SEQUENCE_FIELDS = {
    "m_iszIdle": "script_pre_idle",
    "m_iszPlay": "script_action",
    "m_iszPostIdle": "script_post_idle",
    "m_iszCustomMove": "script_custom_move",
}
NPC_POLICY_FIELDS = {
    "combat_start_activity": ("activity", "npc_combat_start"),
    "default_disposition": ("disposition", "npc_default_disposition"),
    "additionalequipment": ("weapon_policy", "npc_primary_equipment"),
    "alternateequipment": ("weapon_policy", "npc_alternate_equipment"),
}
CHOREOGRAPHY_FIELDS = {
    "SceneFile": ("choreography", "scene_file"),
    "BaseAnim": ("cinematic_model", "base_animation_model"),
    "MaleAnim": ("cinematic_model", "male_animation_model"),
    "FemaleAnim": ("cinematic_model", "female_animation_model"),
}
ACTION_INPUTS = {
    "SetAnimation": ("direct_sequence", "prop_one_shot"),
    "SetDefaultAnimation": ("direct_sequence", "default_animation"),
    "SetModel": ("model_selection", "entity_set_model"),
    "Transform": ("model_transform", "transform_npc"),
    "BeginSequence": ("sequence_control", "begin_scripted_sequence"),
    "CancelSequence": ("sequence_control", "cancel_scripted_sequence"),
    "MoveToPosition": ("sequence_control", "move_scripted_sequence_actor"),
    "StartSchedule": ("schedule_control", "start_ai_schedule"),
}
PYTHON_ACTION_CALLS = {
    "SetAnimation": ("direct_sequence", "entity_set_animation", 0),
    "SetDefaultAnimation": ("direct_sequence", "entity_set_default_animation", 0),
    "SetModel": ("model_selection", "entity_set_model", 0),
    "Transform": ("model_transform", "transform_npc", None),
    "BeginSequence": ("sequence_control", "begin_scripted_sequence", None),
    "CancelSequence": ("sequence_control", "cancel_scripted_sequence", None),
    "MoveToPosition": ("sequence_control", "move_scripted_sequence_actor", None),
    "StartSchedule": ("schedule_control", "start_ai_schedule", None),
    "SetDisposition": ("disposition", "character_set_disposition", 0),
    "SetGesture": ("direct_sequence", "character_set_gesture", 0),
    "SetExpression": ("expression", "character_set_expression", 1),
    "React": ("reaction", "character_react", 1),
    "SetRelationship": ("relationship", "entity_set_relationship", 0),
    "SeductiveFeed": ("player_action", "character_seductive_feed", None),
}


def key_ci(fields, name, default=""):
    """Return an entity field case-insensitively."""
    needle = name.casefold()
    for key, value in fields.items():
        if str(key).casefold() == needle:
            return value
    return default


def entity_fields(entity):
    """Flatten exporter metadata and the optional raw-key dictionary."""
    fields = {key: value for key, value in entity.items()
              if key not in ("outputs", "keys")}
    keys = entity.get("keys")
    if isinstance(keys, dict):
        fields.update(keys)
    return fields


def load_maps(root):
    maps = []
    pattern = os.path.join(os.fspath(root), "*", "*.ents")
    for path in sorted(glob.glob(pattern)):
        try:
            with open(path, "r", errors="replace") as handle:
                data = json.load(handle)
        except (OSError, ValueError):
            continue
        if not isinstance(data, dict) or not isinstance(data.get("entities"), list):
            continue
        data = dict(data)
        data["_path"] = path
        maps.append(data)
    return maps


def resolve_targets(entities, target):
    query = str(target or "").casefold()
    if not query or query.startswith("!"):
        return []
    def names(entity):
        fields = entity_fields(entity)
        return (
            str(entity.get("targetname", "")).casefold(),
            str(key_ci(fields, "NPCTargetname", "")).casefold(),
        )

    if query.endswith("*"):
        prefix = query[:-1]
        return [entity for entity in entities
                if any(name.startswith(prefix) for name in names(entity) if name)]
    return [entity for entity in entities
            if query in names(entity)]


def target_descriptor(entity):
    fields = entity_fields(entity)
    return {
        "classname": str(entity.get("classname", "")),
        "targetname": str(entity.get("targetname", "")),
        "model": str(key_ci(fields, "model", "")),
        "npc_type": str(key_ci(fields, "NPCType", "")),
        "spawn_targetname": str(key_ci(fields, "NPCTargetname", "")),
    }


def producer_record(map_name, index, entity, producer, value_kind, value,
                    field=None, status="live", owner_target="", owners=None):
    row = {
        "map": map_name,
        "entity_index": index,
        "source_class": str(entity.get("classname", "")),
        "source_name": str(entity.get("targetname", "")),
        "producer": producer,
        "value_kind": value_kind,
        "value": str(value),
        "field": field or "",
        "status": status,
    }
    if owner_target:
        row["owner_target"] = str(owner_target)
        row["owners"] = owners or []
        row["owner_resolution"] = (
            "special_target" if str(owner_target).startswith("!") else
            "resolved" if owners else "missing_target")
    return row


def survey_maps(maps):
    producers = []
    wires = []
    sequence_links = []
    levelscript_maps = collections.defaultdict(list)
    by_map = []
    total_entities = 0

    for root in maps:
        map_name = str(root.get("map") or os.path.splitext(
            os.path.basename(root.get("_path", "<unknown>")))[0])
        entities = root["entities"]
        total_entities += len(entities)
        per_map = collections.Counter()

        for index, entity in enumerate(entities):
            classname = str(entity.get("classname", ""))
            class_lc = classname.casefold()
            fields = entity_fields(entity)

            if class_lc == "worldspawn":
                module = str(key_ci(fields, "levelscript", "")).strip()
                if module:
                    levelscript_maps[module.casefold()].append(map_name)

            if class_lc in SCRIPT_CLASSES:
                actor_target = key_ci(fields, "m_iszEntity", "")
                actor_owners = [target_descriptor(item)
                                for item in resolve_targets(entities, actor_target)]
                for field, producer in SCRIPT_SEQUENCE_FIELDS.items():
                    value = key_ci(fields, field, "")
                    if str(value).strip():
                        producers.append(producer_record(
                            map_name, index, entity, producer, "direct_sequence", value, field,
                            owner_target=actor_target, owners=actor_owners))
                        per_map[producer] += 1
                # The literal m_iszPreIdle spelling is not in VtMB's datamap.  Keep it in the
                # ledger, but never count it as a live exact-sequence demand.
                value = key_ci(fields, "m_iszPreIdle", "")
                if str(value).strip():
                    producers.append(producer_record(
                        map_name, index, entity, "script_pre_idle_misspelled",
                        "direct_sequence", value, "m_iszPreIdle", "engine_ignored",
                        actor_target, actor_owners))
                    per_map["script_pre_idle_misspelled"] += 1
                move_to = key_ci(fields, "m_fMoveTo", "")
                if str(move_to).strip():
                    producers.append(producer_record(
                        map_name, index, entity, "script_movement_mode", "script_move_mode",
                        move_to, "m_fMoveTo", owner_target=actor_target,
                        owners=actor_owners))
                    per_map["script_movement_mode"] += 1
                next_script = key_ci(fields, "m_iszNextScript", "")
                if str(next_script).strip():
                    sequence_links.append({
                        "map": map_name,
                        "source": str(entity.get("targetname", "")),
                        "target": str(next_script),
                        "target_exists": bool(resolve_targets(entities, next_script)),
                    })

            if class_lc == "aiscripted_schedule":
                actor_target = key_ci(fields, "m_iszEntity", "")
                actor_owners = [target_descriptor(item)
                                for item in resolve_targets(entities, actor_target)]
                for field, kind, producer in (
                        ("schedule", "schedule_enum", "ai_schedule"),
                        ("forcestate", "npc_state_enum", "ai_schedule_force_state"),
                        ("goalent", "entity_target", "ai_schedule_goal")):
                    value = key_ci(fields, field, "")
                    if str(value).strip():
                        producers.append(producer_record(
                            map_name, index, entity, producer, kind, value, field,
                            owner_target=actor_target, owners=actor_owners))
                        per_map[producer] += 1

            if class_lc in PROP_DYNAMIC_CLASSES:
                prop_owner = [target_descriptor(entity)]
                loop = key_ci(fields, "LoopSequence", "")
                if str(loop).strip():
                    producers.append(producer_record(
                        map_name, index, entity, "prop_loop", "direct_sequence", loop,
                        "LoopSequence", owner_target=entity.get("targetname") or "<unnamed>",
                        owners=prop_owner))
                    per_map["prop_loop"] += 1
                random_animation = key_ci(fields, "RandomAnimation", "")
                if str(random_animation).strip():
                    status = ("live" if str(random_animation).strip() not in ("0", "0.0")
                              else "disabled")
                    producers.append(producer_record(
                        map_name, index, entity, "prop_random_idle", "activity_selector",
                        random_animation, "RandomAnimation", status,
                        entity.get("targetname") or "<unnamed>", prop_owner))
                    per_map["prop_random_idle"] += 1

            if class_lc == "logic_choreographed_scene":
                for field, (kind, producer) in CHOREOGRAPHY_FIELDS.items():
                    value = key_ci(fields, field, "")
                    if str(value).strip():
                        producers.append(producer_record(
                            map_name, index, entity, producer, kind, value, field,
                            owner_target=entity.get("targetname") or "<unnamed>",
                            owners=[target_descriptor(entity)]))
                        per_map[producer] += 1

            for field, (kind, producer) in NPC_POLICY_FIELDS.items():
                value = key_ci(fields, field, "")
                if not str(value).strip():
                    continue
                status = "live"
                if field == "combat_start_activity" and str(value).strip().casefold() in (
                        "-1", "act_invalid"):
                    status = "invalid_sentinel"
                elif field in ("additionalequipment", "alternateequipment") and str(
                        value).strip() == "0":
                    status = "empty_sentinel"
                producers.append(producer_record(
                    map_name, index, entity, producer, kind, value, field, status,
                    entity.get("targetname") or "<unnamed>", [target_descriptor(entity)]))
                per_map[producer] += 1

            morph_model = key_ci(fields, "MorphModel", "")
            if str(morph_model).strip():
                producers.append(producer_record(
                    map_name, index, entity, "npc_morph_model", "model_selection",
                    morph_model, "MorphModel", owner_target=entity.get("targetname") or
                    "<unnamed>", owners=[target_descriptor(entity)]))
                per_map["npc_morph_model"] += 1

            demo = key_ci(fields, "demo_sequence", "")
            if "demo_sequence" in {str(key).casefold(): key for key in fields}:
                demo_text = "" if demo is None else str(demo).strip()
                if demo is None:
                    demo_status = "engine_ignored_null"
                elif demo_text.casefold() in ("", "none"):
                    demo_status = "engine_ignored_sentinel"
                else:
                    demo_status = "engine_ignored_label"
                producers.append(producer_record(
                    map_name, index, entity, "demo_sequence_authoring", "direct_sequence",
                    demo_text, "demo_sequence", demo_status,
                    entity.get("targetname") or "<unnamed>", [target_descriptor(entity)]))
                per_map["demo_sequence_authoring"] += 1

            for output in entity.get("outputs", []):
                input_name = str(output.get("input", ""))
                if input_name not in ACTION_INPUTS:
                    continue
                kind, producer = ACTION_INPUTS[input_name]
                target = str(output.get("target", ""))
                resolved = [target_descriptor(item) for item in resolve_targets(entities, target)]
                row = {
                    "map": map_name,
                    "source_index": index,
                    "source_class": classname,
                    "source_name": str(entity.get("targetname", "")),
                    "event": str(output.get("name", "")),
                    "target": target,
                    "input": input_name,
                    "param": str(output.get("param", "")),
                    "delay": str(output.get("delay", "")),
                    "times": str(output.get("times", "")),
                    "python": str(output.get("python", "")),
                    "producer": producer,
                    "value_kind": kind,
                    "resolved_targets": resolved,
                    "resolution": ("special_target" if target.startswith("!") else
                                   "resolved" if resolved else "missing_target"),
                }
                wires.append(row)
                per_map["io_" + input_name] += 1

        map_row = {"map": map_name, "entities": len(entities),
                   "producer_records": sum(per_map.values())}
        map_row.update(dict(per_map))
        by_map.append(map_row)

    return {
        "maps": len(maps),
        "entities": total_entities,
        "producers": producers,
        "io_wires": wires,
        "sequence_links": sequence_links,
        "levelscript_maps": {key: sorted(value)
                             for key, value in sorted(levelscript_maps.items())},
        "by_map": sorted(by_map, key=lambda row: (-row["producer_records"], row["map"])),
    }


def code_mask(text):
    """Blank strings/comments while preserving positions and executable punctuation."""
    out = list(text)
    try:
        tokens = tokenize.generate_tokens(io.StringIO(text).readline)
        offsets = [0]
        for line in text.splitlines(True):
            offsets.append(offsets[-1] + len(line))

        def absolute(position):
            line, column = position
            return offsets[min(line - 1, len(offsets) - 1)] + column

        for token in tokens:
            if token.type not in (tokenize.STRING, tokenize.COMMENT):
                continue
            begin = absolute(token.start)
            end = absolute(token.end)
            for index in range(begin, min(end, len(out))):
                if out[index] not in "\r\n":
                    out[index] = " "
    except (tokenize.TokenError, IndentationError):
        # Malformed retail snippets execute as errors in VtMB.  A conservative line fallback
        # still finds calls preceding the syntax error without inventing calls from comments.
        for match in re.finditer(r"#.*$", text, re.M):
            for index in range(match.start(), match.end()):
                out[index] = " "
    return "".join(out)


def closing_paren(text, opening):
    """Find a call's matching close paren, respecting Python string tokens."""
    depth = 0
    quote = None
    triple = False
    escaped = False
    index = opening
    while index < len(text):
        char = text[index]
        if quote:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif triple and text.startswith(quote * 3, index):
                quote = None
                triple = False
                index += 2
            elif not triple and char == quote:
                quote = None
        elif char in ("'", '"'):
            triple = text.startswith(char * 3, index)
            quote = char
            if triple:
                index += 2
        elif char == "#":
            newline = text.find("\n", index)
            if newline < 0:
                return None
            index = newline
        elif char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                return index
        index += 1
    return None


def split_args(text):
    """Split a call argument list at top-level commas."""
    parts = []
    start = 0
    depth = 0
    quote = None
    triple = False
    escaped = False
    index = 0
    while index < len(text):
        char = text[index]
        if quote:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif triple and text.startswith(quote * 3, index):
                quote = None
                triple = False
                index += 2
            elif not triple and char == quote:
                quote = None
        elif char in ("'", '"'):
            triple = text.startswith(char * 3, index)
            quote = char
            if triple:
                index += 2
        elif char in "([{":
            depth += 1
        elif char in ")]}" and depth:
            depth -= 1
        elif char == "," and depth == 0:
            parts.append(text[start:index].strip())
            start = index + 1
        index += 1
    tail = text[start:].strip()
    if tail or parts:
        parts.append(tail)
    return parts


def first_string_literal(expression):
    """Return the leading literal token even when followed by ``%`` formatting."""
    try:
        tokens = tokenize.generate_tokens(io.StringIO(expression).readline)
        for token in tokens:
            if token.type in (tokenize.NL, tokenize.NEWLINE, tokenize.INDENT,
                              tokenize.DEDENT):
                continue
            if token.type != tokenize.STRING:
                return None
            try:
                value = ast.literal_eval(token.string)
            except (SyntaxError, ValueError):
                return None
            return value if isinstance(value, str) else None
    except (tokenize.TokenError, IndentationError):
        return None
    return None


def literal_string(expression):
    """Return a string only when the complete expression is one literal."""
    try:
        value = ast.literal_eval(expression)
    except (SyntaxError, ValueError):
        return None
    return value if isinstance(value, str) else None


def iter_named_calls(text, names):
    mask = code_mask(text)
    expression = re.compile(r"\b(" + "|".join(re.escape(name) for name in sorted(
        names, key=len, reverse=True)) + r")\s*\(")
    for match in expression.finditer(mask):
        name = match.group(1)
        opening = mask.find("(", match.start(1) + len(name))
        end = closing_paren(text, opening)
        if end is None:
            continue
        prefix = mask[max(0, match.start(1) - 160):match.start(1)]
        receiver_match = re.search(
            r"([A-Za-z_]\w*(?:\s*\.\s*[A-Za-z_]\w*)*)\s*\.\s*$", prefix)
        receiver = re.sub(r"\s+", "", receiver_match.group(1)) if receiver_match else ""
        yield {
            "name": name,
            "receiver": receiver,
            "line": text.count("\n", 0, match.start(1)) + 1,
            "arguments": split_args(text[opening + 1:end]),
            "raw": text[match.start(1):end + 1],
            "start": match.start(1),
        }


def inline_find_target(text, start):
    prefix = text[max(0, start - 240):start]
    matches = list(re.finditer(
        r"(?:FindEntityByName|Find)\s*\(\s*([uUrR]*['\"](?:\\.|[^'\"])*['\"])",
        prefix))
    if not matches:
        return ""
    return first_string_literal(matches[-1].group(1)) or ""


def bound_find_target(text, start, receiver):
    """Recover a local ``x = Find('name')`` binding inside the current function."""
    if not receiver or "." in receiver:
        return ""
    prefix = text[:start]
    function_starts = list(re.finditer(r"(?m)^\s*def\s+\w+\s*\(", prefix))
    if function_starts:
        prefix = prefix[function_starts[-1].start():]
    string_token = r"([uUrR]*['\"](?:\\.|[^'\"])*['\"](?:\s*%[^\n,)]*)?)"
    direct = re.compile(
        r"(?m)^\s*" + re.escape(receiver) +
        r"\s*=\s*(?:__main__\s*\.\s*)?(?:FindEntityByName|Find)\s*\(\s*" +
        string_token)
    matches = list(direct.finditer(prefix))
    if matches:
        return first_string_literal(matches[-1].group(1)) or ""

    loops = list(re.finditer(
        r"(?m)^\s*for\s+" + re.escape(receiver) + r"\s+in\s+(\w+)\s*:", prefix))
    if not loops:
        return ""
    collection = loops[-1].group(1)
    plural = re.compile(
        r"(?m)^\s*" + re.escape(collection) +
        r"\s*=\s*(?:__main__\s*\.\s*)?(?:FindEntitiesByName|Finds)\s*\(\s*" +
        string_token)
    matches = list(plural.finditer(prefix[:loops[-1].start()]))
    return first_string_literal(matches[-1].group(1)) if matches else ""


def scan_python_text(text, surface, source, base_line=1, scheduled=False):
    rows = []
    for call in iter_named_calls(text, set(PYTHON_ACTION_CALLS) | {"ScheduleTask"}):
        if call["name"] == "ScheduleTask":
            args = call["arguments"]
            payload = first_string_literal(args[1]) if len(args) > 1 else None
            if payload:
                rows.extend(scan_python_text(
                    payload, "scheduled", "%s:%d" % (source, base_line + call["line"] - 1),
                    1, True))
            continue

        kind, producer, value_index = PYTHON_ACTION_CALLS[call["name"]]
        args = call["arguments"]
        value_expression = (args[value_index] if value_index is not None and
                            value_index < len(args) else "")
        literal = literal_string(value_expression) if value_expression else None
        template = (first_string_literal(value_expression)
                    if value_expression and literal is None else None)
        rows.append({
            "surface": surface,
            "source": source,
            "line": base_line + call["line"] - 1,
            "scheduled": bool(scheduled),
            "call": call["name"],
            "receiver": call["receiver"],
            "inline_target": inline_find_target(text, call["start"]),
            "bound_target": bound_find_target(text, call["start"], call["receiver"]),
            "producer": producer,
            "value_kind": kind,
            "value": (literal if literal is not None else
                      template if template is not None else value_expression),
            "value_is_literal": literal is not None,
            "value_is_template": template is not None,
            "arguments": args,
            "raw": call["raw"],
        })
    return rows


def dialogue_snippets(path):
    with open(path, "r", encoding="latin-1", errors="replace") as handle:
        for line_no, line in enumerate(handle, 1):
            fields = re.findall(r"\{(.*?)\}", line)
            if len(fields) < 6:
                continue
            for index in (4, 5):
                snippet = fields[index].strip()
                if snippet and snippet != "#":
                    yield line_no, "dlg%d" % index, snippet


def survey_python(root, maps):
    root = os.fspath(root)
    rows = []
    py_files = [path for path in sorted(glob.glob(
        os.path.join(root, "scripts", "**", "*.py"), recursive=True))
                if os.sep + "lib" + os.sep not in path]
    for path in py_files:
        rel = os.path.relpath(path, root).replace("\\", "/")
        with open(path, "r", encoding="latin-1", errors="replace") as handle:
            rows.extend(scan_python_text(handle.read(), "py", rel))

    dlg_files = sorted(glob.glob(os.path.join(root, "dlg", "**", "*.dlg"), recursive=True))
    dlg_rows = 0
    for path in dlg_files:
        rel = os.path.relpath(path, root).replace("\\", "/")
        for line_no, surface, snippet in dialogue_snippets(path):
            dlg_rows += 1
            rows.extend(scan_python_text(snippet, surface, rel, line_no))

    payloads = 0
    pythonchecks = 0
    usescripts = 0
    for root_map in maps:
        map_name = str(root_map.get("map", "<unknown>"))
        for index, entity in enumerate(root_map["entities"]):
            fields = entity_fields(entity)
            for output_index, output in enumerate(entity.get("outputs", [])):
                payload = str(output.get("python", "")).strip()
                if not payload:
                    continue
                payloads += 1
                source = "%s.ents:entity[%d].outputs[%d]" % (map_name, index, output_index)
                rows.extend(scan_python_text(payload, "ents_output", source))
            if str(entity.get("classname", "")).casefold() == "logic_pythoncheck":
                snippet = str(key_ci(fields, "python_script", "")).strip()
                if snippet:
                    pythonchecks += 1
                    source = "%s.ents:entity[%d].python_script" % (map_name, index)
                    rows.extend(scan_python_text(snippet, "logic_pythoncheck", source))
            snippet = str(key_ci(fields, "usescript", "")).strip()
            if snippet:
                usescripts += 1
                source = "%s.ents:entity[%d].usescript" % (map_name, index)
                rows.extend(scan_python_text(snippet, "usescript", source))

    module_maps = collections.defaultdict(set)
    dialogue_maps = collections.defaultdict(set)
    map_names = set()
    for root_map in maps:
        map_name = str(root_map.get("map", "<unknown>"))
        map_names.add(map_name)
        for entity in root_map["entities"]:
            fields = entity_fields(entity)
            if str(entity.get("classname", "")).casefold() == "worldspawn":
                module = str(key_ci(fields, "levelscript", "")).strip().casefold()
                if module:
                    module_maps[module].add(map_name)
            dialogue = str(key_ci(fields, "dialogname", "")).replace("\\", "/").casefold()
            if dialogue:
                dialogue_maps[dialogue].add(map_name)

    for row in rows:
        source = row["source"].replace("\\", "/")
        source_maps = set()
        if row["surface"] in ("dlg4", "dlg5"):
            source_maps.update(dialogue_maps.get(source.casefold(), ()))
        elif ".ents:" in source:
            candidate = source.split(".ents:", 1)[0]
            if candidate in map_names:
                source_maps.add(candidate)
        elif source.startswith("scripts/"):
            base_source = source.split(":", 1)[0]
            parts = base_source.split("/")
            if len(parts) >= 3 and os.path.splitext(parts[-1])[0].casefold() == parts[1].casefold():
                source_maps.update(module_maps.get(parts[1].casefold(), ()))
            elif len(parts) == 2:
                source_maps.update(module_maps.get(os.path.splitext(parts[-1])[0].casefold(), ()))
        row["source_maps"] = sorted(source_maps)

    return {
        "files": len(py_files),
        "dialogue_files": len(dlg_files),
        "dialogue_snippets": dlg_rows,
        "entity_python_payloads": payloads,
        "logic_pythonchecks": pythonchecks,
        "usescripts": usescripts,
        "calls": rows,
    }


def load_animation_manifest(root):
    path = os.path.join(os.fspath(root), "npc", "npc_manifest.json")
    try:
        with open(path, "r", errors="replace") as handle:
            manifest = json.load(handle)
    except (OSError, ValueError):
        return None, path
    return manifest, path


def animation_model_index(manifest):
    index = {}
    for section in ("npcs", "animated_props"):
        for stem, entry in manifest.get(section, {}).items():
            model = str(entry.get("model", "")).replace("\\", "/").casefold()
            if model:
                index[model] = {"section": section, "stem": stem, "entry": entry}
    return index


def clip_answer(manifest, model_index, model, label):
    """Resolve one exact-label demand against the baked character manifest."""
    model_key = str(model or "").replace("\\", "/").casefold()
    if not model_key:
        return {"status": "owner_has_no_model", "model": str(model or ""),
                "requested_label": str(label)}
    indexed = model_index.get(model_key)
    if indexed is None:
        return {"status": "model_not_in_character_manifest", "model": str(model),
                "requested_label": str(label)}

    entry = indexed["entry"]
    clips = entry.get("clips", {})
    requested = str(label)
    matched = requested if requested in clips else None
    match_mode = "exact"
    if matched is None:
        folded = collections.defaultdict(list)
        for candidate in clips:
            folded[str(candidate).casefold()].append(candidate)
        candidates = folded.get(requested.casefold(), [])
        if len(candidates) == 1:
            matched = candidates[0]
            match_mode = "casefold"
        elif len(candidates) > 1:
            return {
                "status": "ambiguous_casefold_label",
                "model": str(model),
                "requested_label": requested,
                "candidates": candidates,
                "model_kind": indexed["section"],
                "model_stem": indexed["stem"],
            }
    if matched is None:
        return {
            "status": "label_missing",
            "model": str(model),
            "requested_label": requested,
            "model_kind": indexed["section"],
            "model_stem": indexed["stem"],
        }

    if indexed["section"] == "npcs":
        owner = clips[matched]
        if owner == indexed["stem"]:
            owner_entry = entry
            metadata = entry.get("own_clips", {}).get(matched, {})
        else:
            owner_entry = manifest.get("banks", {}).get(owner, {})
            metadata = owner_entry.get("clips", {}).get(matched, {})
    else:
        owner = indexed["stem"]
        owner_entry = entry
        metadata = clips.get(matched, {})

    return {
        "status": "resolved",
        "model": str(model),
        "requested_label": requested,
        "matched_label": matched,
        "match_mode": match_mode,
        "model_kind": indexed["section"],
        "model_stem": indexed["stem"],
        "owner": owner,
        "glb": str(owner_entry.get("glb", "")),
        "activity": str(metadata.get("activity", "")),
        "weight": metadata.get("weight"),
        "flags": metadata.get("flags"),
        "frames": metadata.get("frames"),
        "fps": metadata.get("fps"),
        "fade": metadata.get("fade"),
    }


def player_clip_answer(manifest, model_index, label):
    player_models = [
        (model, indexed) for model, indexed in model_index.items()
        if str(indexed["entry"].get("model", "")).replace("\\", "/").casefold().startswith(
            "models/character/pc/")
    ]
    answers = [clip_answer(manifest, model_index, indexed["entry"].get("model", ""), label)
               for model, indexed in player_models]
    resolved = [answer for answer in answers if answer["status"] == "resolved"]
    grouped = collections.Counter(
        (answer.get("matched_label", ""), answer.get("owner", ""),
         answer.get("activity", "")) for answer in resolved)
    if resolved and len(resolved) == len(answers):
        status = "resolved_all_player_models"
    elif resolved:
        status = "resolved_some_player_models"
    else:
        status = "label_missing_all_player_models"
    return {
        "status": status,
        "requested_label": str(label),
        "player_models": len(answers),
        "resolved_models": len(resolved),
        "answers": [
            {"matched_label": matched, "owner": owner, "activity": activity, "models": count}
            for (matched, owner, activity), count in grouped.most_common()
        ],
        "missing_model_stems": [answer.get("model_stem", "") for answer in answers
                                if answer["status"] != "resolved"],
    }


def owner_clip_answers(manifest, model_index, owners, label, special=False):
    if special:
        return [player_clip_answer(manifest, model_index, label)]
    if not owners:
        return [{"status": "owner_not_resolved", "requested_label": str(label)}]
    return [clip_answer(manifest, model_index, owner.get("model", ""), label)
            for owner in owners]


def join_animation_manifest(data, maps, root):
    manifest, path = load_animation_manifest(root)
    if manifest is None:
        return {"available": False, "path": path}
    model_index = animation_model_index(manifest)

    joined = []
    for row in data["maps"]["producers"]:
        if row["value_kind"] != "direct_sequence" or row["status"] != "live":
            continue
        answers = owner_clip_answers(
            manifest, model_index, row.get("owners", []), row["value"],
            row.get("owner_resolution") == "special_target")
        row["clip_answers"] = answers
        joined.extend(answers)

    for row in data["maps"]["io_wires"]:
        if row["input"] not in ("SetAnimation", "SetDefaultAnimation"):
            continue
        answers = owner_clip_answers(
            manifest, model_index, row["resolved_targets"], row["param"],
            row["resolution"] == "special_target")
        row["clip_answers"] = answers
        joined.extend(answers)

    # A direct FindEntityByName call can be joined to any currently exported map that carries
    # that target.  Multiple maps/owners remain explicit rather than being guessed down to one.
    for row in data["python"]["calls"]:
        if row["value_kind"] != "direct_sequence" or not row["value_is_literal"]:
            continue
        target = row.get("inline_target", "") or row.get("bound_target", "")
        owners = []
        target_pattern = re.sub(r"%[-+0-9.#]*[diouxXeEfFgGcrs]", "*", target)
        candidate_maps = set(row.get("source_maps", []))
        if target_pattern:
            for root_map in maps:
                map_name = str(root_map.get("map", "<unknown>"))
                if candidate_maps and map_name not in candidate_maps:
                    continue
                for entity in resolve_targets(root_map["entities"], target_pattern):
                    owner = target_descriptor(entity)
                    owner["map"] = map_name
                    owners.append(owner)
        # Dialogue's `npc` receiver names the participant whose dialogname is this file.
        if not owners and row["surface"] in ("dlg4", "dlg5") and row["receiver"].casefold() == "npc":
            dialogue = row["source"].replace("\\", "/").casefold()
            for root_map in maps:
                map_name = str(root_map.get("map", "<unknown>"))
                for entity in root_map["entities"]:
                    if str(key_ci(entity_fields(entity), "dialogname", "")).replace(
                            "\\", "/").casefold() != dialogue:
                        continue
                    owner = target_descriptor(entity)
                    owner["map"] = map_name
                    owners.append(owner)
        if row["receiver"].casefold() in ("pc", "player"):
            answers = owner_clip_answers(manifest, model_index, owners, row["value"], True)
        elif owners:
            answers = owner_clip_answers(manifest, model_index, owners, row["value"])
        elif not row.get("source_maps"):
            answers = [{"status": "source_outside_exported_map_corpus",
                        "requested_label": str(row["value"])}]
        elif target_pattern:
            answers = [{"status": "target_absent_from_source_maps",
                        "requested_label": str(row["value"]),
                        "target": target_pattern}]
        else:
            answers = [{"status": "target_not_statically_bound",
                        "requested_label": str(row["value"])}]
        row["resolved_owners"] = owners
        row["clip_answers"] = answers
        joined.extend(answers)

    statuses = collections.Counter(answer["status"] for answer in joined)

    model_selections = []
    for row in data["maps"]["producers"]:
        if row["value_kind"] == "model_selection" and row["status"] == "live":
            model_selections.append(row)
    for row in data["maps"]["io_wires"]:
        if row["value_kind"] == "model_selection" and str(row["param"]).strip():
            model_selections.append(row)
    for row in data["python"]["calls"]:
        if row["value_kind"] == "model_selection" and row["value_is_literal"]:
            model_selections.append(row)

    selection_statuses = collections.Counter()
    for row in model_selections:
        value = str(row.get("value", row.get("param", "")))
        indexed = model_index.get(value.replace("\\", "/").casefold())
        if indexed is None:
            answer = {"status": "outside_character_manifest", "model": value}
        else:
            answer = {
                "status": "resolved_animation_owner",
                "model": value,
                "model_kind": indexed["section"],
                "model_stem": indexed["stem"],
            }
        row["selected_model_answer"] = answer
        selection_statuses[answer["status"]] += 1

    dynamic_model_selections = sum(
        1 for row in data["python"]["calls"]
        if row["value_kind"] == "model_selection" and not row["value_is_literal"])
    return {
        "available": True,
        "path": path,
        "version": manifest.get("manifest_version"),
        "models": len(model_index),
        "joined_demands": len(joined),
        "statuses": dict(statuses.most_common()),
        "model_selection_demands": len(model_selections),
        "model_selection_statuses": dict(selection_statuses.most_common()),
        "dynamic_model_selections": dynamic_model_selections,
    }


def summarize(data):
    producers = data["maps"]["producers"]
    wires = data["maps"]["io_wires"]
    calls = data["python"]["calls"]
    producer_counts = collections.Counter(row["producer"] for row in producers)
    status_counts = collections.Counter(row["status"] for row in producers)
    wire_counts = collections.Counter(row["input"] for row in wires)
    wire_resolution = collections.Counter(row["resolution"] for row in wires)
    call_counts = collections.Counter(row["call"] for row in calls)
    call_surfaces = collections.Counter(row["surface"] for row in calls)
    literal_counts = collections.Counter(
        (row["call"], str(row["value"])) for row in calls if row["value_is_literal"])
    producer_values = collections.defaultdict(collections.Counter)
    for row in producers:
        producer_values[row["producer"]][row["value"]] += 1
    return {
        "producer_counts": dict(producer_counts.most_common()),
        "producer_status": dict(status_counts.most_common()),
        "io_inputs": dict(wire_counts.most_common()),
        "io_resolution": dict(wire_resolution.most_common()),
        "python_calls": dict(call_counts.most_common()),
        "python_surfaces": dict(call_surfaces.most_common()),
        "python_literal_values": [
            {"call": call, "value": value, "count": count}
            for (call, value), count in literal_counts.most_common()
        ],
        "producer_values": {
            producer: [{"value": value, "count": count}
                       for value, count in values.most_common()]
            for producer, values in sorted(producer_values.items())
        },
    }


def print_report(data):
    maps = data["maps"]
    python = data["python"]
    summary = data["summary"]
    print("=" * 78)
    print("VtMB action / animation producer surface")
    print("=" * 78)
    print("corpus: %d maps | %d entities | %d Python files | %d dialogue files" %
          (maps["maps"], maps["entities"], python["files"], python["dialogue_files"]))
    print("map producer records: %d | action I/O wires: %d | Python action calls: %d" %
          (len(maps["producers"]), len(maps["io_wires"]), len(python["calls"])))
    print()
    print("Authored producers")
    for name, count in summary["producer_counts"].items():
        print("  %-36s %5d" % (name, count))
    print("  status: " + ", ".join("%s=%d" % item
          for item in summary["producer_status"].items()))
    print()
    print("Action-facing I/O")
    for name, count in summary["io_inputs"].items():
        print("  %-36s %5d" % (name, count))
    print("  target resolution: " + ", ".join("%s=%d" % item
          for item in summary["io_resolution"].items()))
    print()
    print("Python action-facing calls")
    for name, count in summary["python_calls"].items():
        print("  %-36s %5d" % (name, count))
    print("  surfaces: " + ", ".join("%s=%d" % item
          for item in summary["python_surfaces"].items()))
    print()
    manifest = data["animation_manifest"]
    if manifest["available"]:
        print("Exact-label join (npc_manifest v%s, %d models, %d owner answers)" %
              (manifest["version"], manifest["models"], manifest["joined_demands"]))
        for name, count in manifest["statuses"].items():
            print("  %-36s %5d" % (name, count))
        print("Model-selection join (%d literal paths, %d dynamic expressions)" %
              (manifest["model_selection_demands"],
               manifest["dynamic_model_selections"]))
        for name, count in manifest["model_selection_statuses"].items():
            print("  %-36s %5d" % (name, count))
        print()
    print("Maps with the most producer records")
    for row in maps["by_map"][:12]:
        print("  %-24s %5d" % (row["map"], row["producer_records"]))


def build_report(root=None):
    if root is None:
        root = os.fspath(export_root())
    maps = load_maps(root)
    data = {"maps": survey_maps(maps), "python": survey_python(root, maps)}
    data["animation_manifest"] = join_animation_manifest(data, maps, root)
    data["summary"] = summarize(data)
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", help="write the complete survey ledger here")
    args = parser.parse_args()
    data = build_report()
    print_report(data)
    if args.json:
        parent = os.path.dirname(os.path.abspath(args.json))
        if parent:
            os.makedirs(parent, exist_ok=True)
        with open(args.json, "w") as handle:
            json.dump(data, handle, indent=1, sort_keys=True)
        print("\nwrote %s" % args.json)


if __name__ == "__main__":
    main()
