"""Synthetic coverage for Source area-portal-window render-helper translation."""

from __future__ import annotations

import os
from pathlib import Path
import unittest

# Importing the map exporter resolves its configured roots at module load. Point those
# contracts at the committed synthetic fixture tree; these pure tests read no game data.
_FIXTURES = Path(__file__).resolve().parent / "fixtures" / "synthetic"
os.environ.setdefault("ELYSIUM_VTMB_ROOT", os.fspath(_FIXTURES))
os.environ.setdefault("ELYSIUM_WORK_ROOT", os.fspath(_FIXTURES))

from elysium_pipeline.exporters.UE_bsp_to_scene import source_visibility_backing_models


def _entity(**keys: str) -> str:
    body = "\n".join('"%s" "%s"' % item for item in keys.items())
    return "{\n%s\n}" % body


def test_only_the_linked_backing_model_is_suppressed() -> None:
    entities = "\n".join(
        (
            _entity(
                classname="func_areaportalwindow",
                target="WNDWBLACK1",
                BackgroundBModel="wndw1",
            ),
            _entity(classname="func_brush", targetname="wndw1", model="*20"),
            _entity(classname="func_brush", targetname="wndwblack1", model="*21"),
            _entity(classname="func_brush", targetname="unrelated_black", model="*22"),
        )
    )

    models, warnings = source_visibility_backing_models(entities)

    assert models == {21}
    assert warnings == []


def test_multiple_windows_resolve_in_entity_order() -> None:
    entities = "\n".join(
        (
            _entity(classname="func_brush", targetname="black", model="*4"),
            _entity(classname="func_brush", targetname="black", model="*5"),
            _entity(classname="FUNC_AREAPORTALWINDOW", target="Black"),
            _entity(classname="func_areaportalwindow", target="black2"),
            _entity(classname="func_brush", targetname="black2", model="*6"),
        )
    )

    models, warnings = source_visibility_backing_models(entities)

    assert models == {4, 6}
    assert warnings == []


def test_malformed_links_warn_and_do_not_hide_an_unrelated_model() -> None:
    entities = "\n".join(
        (
            _entity(classname="func_areaportalwindow"),
            _entity(classname="func_areaportalwindow", target="missing"),
            _entity(classname="func_areaportalwindow", target="point_helper"),
            _entity(classname="info_target", targetname="point_helper"),
            _entity(classname="func_brush", targetname="other", model="*8"),
        )
    )

    models, warnings = source_visibility_backing_models(entities)

    assert models == set()
    assert len(warnings) == 3
    assert "has no target" in warnings[0]
    assert "does not resolve" in warnings[1]
    assert "has no brush model" in warnings[2]
