"""Synthetic coverage for Source area-portal-window render-helper translation.

`func_areaportalwindow.target` names the black brush Source fades over its background model while
a PVS portal opens and closes. Unreal owns visibility, so that render-only backing never becomes a
brush mesh.

0018 story 21-5 re-pointed these off the deleted decoder's `source_visibility_backing_models` and
onto the producer's `visibility_backing_models`, which is the live reader. One divergence, stated
rather than absorbed: the decoder also returned a warning per malformed link ("has no target",
"does not resolve", "has no brush model") and the producer returns the model set alone, so the
third case below pins what is suppressed and no longer pins the diagnostic text.
"""

from __future__ import annotations

from elysium_pipeline.exporters.UE_map_sidecars import visibility_backing_models


def _entity(**keys: str) -> list[tuple[str, str]]:
    return list(keys.items())


def _models(*entities: list[tuple[str, str]]) -> set[int]:
    return visibility_backing_models(list(entities))


def test_only_the_linked_backing_model_is_suppressed() -> None:
    # `wndwblack1` is reached through the window's `target`; the similarly-named
    # `unrelated_black` is not, and neither is the background model itself.
    assert _models(
        _entity(
            classname="func_areaportalwindow",
            target="WNDWBLACK1",
            BackgroundBModel="wndw1",
        ),
        _entity(classname="func_brush", targetname="wndw1", model="*20"),
        _entity(classname="func_brush", targetname="wndwblack1", model="*21"),
        _entity(classname="func_brush", targetname="unrelated_black", model="*22"),
    ) == {21}


def test_multiple_windows_resolve_in_entity_order() -> None:
    # Case-insensitively, and on the FIRST entity carrying a name -- two brushes are called
    # `black` and only `*4` backs the window, which is the engine's own rule.
    assert _models(
        _entity(classname="func_brush", targetname="black", model="*4"),
        _entity(classname="func_brush", targetname="black", model="*5"),
        _entity(classname="FUNC_AREAPORTALWINDOW", target="Black"),
        _entity(classname="func_areaportalwindow", target="black2"),
        _entity(classname="func_brush", targetname="black2", model="*6"),
    ) == {4, 6}


def test_malformed_links_do_not_hide_an_unrelated_model() -> None:
    # No target, a target resolving to nothing, and a target resolving to a point entity with no
    # brush model. None of the three may suppress anything, least of all the unrelated `*8`.
    assert _models(
        _entity(classname="func_areaportalwindow"),
        _entity(classname="func_areaportalwindow", target="missing"),
        _entity(classname="func_areaportalwindow", target="point_helper"),
        _entity(classname="info_target", targetname="point_helper"),
        _entity(classname="func_brush", targetname="other", model="*8"),
    ) == set()
