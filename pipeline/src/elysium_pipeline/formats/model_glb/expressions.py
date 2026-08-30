"""Which Faceposer tables a model stem selects, and what it would fall back to.

The tables themselves are `vtmb:expression-table:` units with one owner each -- `phonemes.vfe` and
`phonemes_male.vfe` are the client's literal fallbacks for every model without a same-stem table,
so inlining them here would make hundreds of models authoritative for one file. This module
answers only the join: the ID the stem selects, the ID it falls back to, and whether either
resolves.
"""

from __future__ import annotations

from typing import Any, Iterable

from elysium_pipeline.formats.unit_contract import asset_id, dependency, missing_sentinel

#: The unit kind the rows name.
TABLE_KIND = "expression-table"

#: The classes `client.dll` formats into `expressions/%s_%s.vfe`, each with the literal fallback
#: stems the shipped binaries name (`docs/vtmb/facial_animation.md` § The file is chosen by the
#: actor's model).
CLASS_FALLBACKS: dict[str, tuple[str, ...]] = {
    "expressions": ("phonemes", "phonemes_male"),
    "phonemes": ("phonemes", "phonemes_male"),
}

#: The members a stem may ship. The compiled table is the runtime authority; a stem that ships
#: only the readable twin is still a unit of its own seam, so it still resolves this reference.
TABLE_SUFFIXES = (".vfe", ".txt")


def table_stem(model_key: str, table_class: str) -> str:
    """`<model basename>_<class>`, which is what the client formats from the model path."""

    basename = model_key.rsplit("/", 1)[-1]
    return f"{basename}_{table_class}"


def _resolve(index: dict, stem: str) -> tuple[str | None, str]:
    """The install path that answers a table stem, and the member kind that answered it."""

    for suffix in TABLE_SUFFIXES:
        candidate = f"expressions/{stem}{suffix}"
        if candidate in index:
            return candidate, suffix.lstrip(".")
    return None, ""


def selected_tables(model_key: str, index: dict) -> list[dict[str, Any]]:
    """One row per facial class: the selected table, its fallback, and whether each resolves.

    A stem the install has no table for keeps a `vtmb:missing-expression-table:` sentinel: the
    engine reaches its generic fallback instead, so the authored spelling stays visible without
    claiming a unit exists for it.
    """

    rows: list[dict[str, Any]] = []
    for table_class, fallbacks in CLASS_FALLBACKS.items():
        stem = table_stem(model_key, table_class)
        path, kind = _resolve(index, stem)
        row: dict[str, Any] = {
            "class": table_class,
            "stem": stem,
            "asset": asset_id(TABLE_KIND, stem) if path else missing_sentinel(TABLE_KIND, stem),
            "sourcePath": path or f"expressions/{stem}.vfe",
            "sourceKind": kind or None,
            "resolved": path is not None,
            "fallbacks": [],
        }
        for fallback in fallbacks:
            fallback_path, fallback_kind = _resolve(index, fallback)
            row["fallbacks"].append(
                {
                    "stem": fallback,
                    "asset": (
                        asset_id(TABLE_KIND, fallback)
                        if fallback_path
                        else missing_sentinel(TABLE_KIND, fallback)
                    ),
                    "sourcePath": fallback_path or f"expressions/{fallback}.vfe",
                    "sourceKind": fallback_kind or None,
                    "resolved": fallback_path is not None,
                }
            )
        rows.append(row)
    return rows


def table_dependencies(rows: Iterable[dict[str, Any]]) -> list[dict[str, Any]]:
    """One `dependencies` row per distinct table the selection can reach.

    A selected table that resolves is a reference the model makes; the fallback of an unresolved
    selection is the reference the engine actually follows, so both are declared and a table named
    twice is declared once.
    """

    seen: set[str] = set()
    dependencies: list[dict[str, Any]] = []
    for row in rows:
        reachable = [row] if row["resolved"] else list(row["fallbacks"])
        for entry in reachable:
            if not entry["resolved"] or entry["asset"] in seen:
                continue
            seen.add(entry["asset"])
            dependencies.append(
                dependency(
                    "expression-table", entry["asset"], entry["sourcePath"], True
                )
            )
    return dependencies


def omitted_selections(rows: Iterable[dict[str, Any]]) -> list[dict[str, Any]]:
    """The `omitted-proven` evidence for every stem the install ships no table for."""

    return [
        {
            "path": f"facial.selectedTables[{row['class']}]",
            "reason": "model-stem-has-no-expression-table",
            "stem": row["stem"],
            "asset": row["asset"],
            "fallback": next(
                (entry["asset"] for entry in row["fallbacks"] if entry["resolved"]), None
            ),
        }
        for row in rows
        if not row["resolved"]
    ]
