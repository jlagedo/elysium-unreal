"""The per-map light store (`pipeline/unreal/light_store.py`): a lighting pass saved in the Unreal
editor, harvested off the baked level by the next bake and applied by that same bake.

The store's whole value rests on one property that nothing else in the pipeline checks: the loop
**converges**. A bake harvests the level it placed on the previous bake, so if a harvested record
did not equal the record that placed the actor, every `export map` would rewrite the store, dirty
the working tree and re-author a level nobody edited -- for as long as the project exists. The
round-trip case below is that property, driven through the same sRGB quantisation the real
`ULightComponent::SetLightColor` applies.

The rest pins the format: the record's key set is the contract `_place_one_light` reads, and a
store the bake cannot parse has to mean "derive", never "fail", or a typo in a hand-edited JSON
would strand a map in the dark instead of lighting it from `UElysiumLightingSettings`.
"""

from __future__ import annotations

import json
import os
from types import SimpleNamespace

import pytest

from pipeline.unreal import light_store

#: `derive_light`'s output for a plain point light, the input `light_store.record` is built from.
FINAL = {
    "kind": 1,
    "position": [100.0, -200.0, 300.0],
    "color": [1.0, 0.5, 0.25],
    "intensity": 0.3,
    "reach_cm": 1300.48,
    "falloff_exponent": 1.0,
    "outer_cone_deg": 44.0,
    "inner_cone_deg": 44.0,
    "cast_shadows": True,
    "specular_scale": 1.0,
    "indirect_lighting_intensity": 1.0,
    "volumetric_scattering_intensity": 1.0,
    "sun_source_angle_deg": 0.5357,
    "sun_soft_source_angle_deg": 0.0,
    "allow_mega_lights": True,
}

#: Every key `bake_map_v2._place_one_light` reads off a record. Pinned here because the two halves
#: are in different files and a record that lost a key would not fail until a bake was hours in.
PLACEMENT_KEYS = {
    "src", "kind", "style", "sky", "label", "position", "rotation", "color", "intensity",
    "reach_cm", "falloff_exponent", "use_inverse_squared_falloff", "outer_cone_deg",
    "inner_cone_deg", "cast_shadows", "specular_scale", "indirect_lighting_intensity",
    "volumetric_scattering_intensity", "sun_source_angle_deg", "sun_soft_source_angle_deg",
    "allow_mega_lights",
}


def _record(**overrides):
    row = light_store.record(
        dict(FINAL, **{k: v for k, v in overrides.items() if k in FINAL}),
        src=overrides.get("src", 7),
        style=overrides.get("style", 0),
        sky=overrides.get("sky", False),
        rotation=overrides.get("rotation", [0.0, 0.0, 0.0]),
        label=overrides.get("label", "Light_7_point"))
    return row


def _linear_to_srgb_byte(channel):
    """`FLinearColor::ToFColor(true)`: the encode `ULightComponent::SetLightColor` applies before
    the component stores the colour as `FColor` bytes. `light_store._srgb_to_linear` is its
    inverse, and this is the only lossy step in the whole round trip."""

    channel = min(max(float(channel), 0.0), 1.0)
    encoded = (channel * 12.92 if channel <= 0.0031308
               else channel ** (1.0 / 2.4) * 1.055 - 0.055)
    return int(round(encoded * 255.0))


class _Component:
    """The `ULightComponent` surface `_record_from_actor` reads, holding what a placement wrote."""

    def __init__(self, rec):
        colour = [_linear_to_srgb_byte(channel) for channel in rec["color"]]
        self._values = {
            "light_color": SimpleNamespace(r=colour[0], g=colour[1], b=colour[2]),
            "intensity": rec["intensity"],
            "attenuation_radius": rec["reach_cm"],
            "light_falloff_exponent": rec["falloff_exponent"],
            "use_inverse_squared_falloff": rec["use_inverse_squared_falloff"],
            "outer_cone_angle": rec["outer_cone_deg"],
            "inner_cone_angle": rec["inner_cone_deg"],
            "cast_shadows": rec["cast_shadows"],
            "specular_scale": rec["specular_scale"],
            "indirect_lighting_intensity": rec["indirect_lighting_intensity"],
            "volumetric_scattering_intensity": rec["volumetric_scattering_intensity"],
            "light_source_angle": rec["sun_source_angle_deg"],
            "light_source_soft_angle": rec["sun_soft_source_angle_deg"],
            "allow_mega_lights": rec["allow_mega_lights"],
        }

    def get_editor_property(self, name):
        return self._values[name]


def _place(rec):
    """The actor `bake_map_v2._place_one_light` would leave in the level for this record."""

    tags = [light_store.TAG_LIGHT]
    if rec["src"] is not None:
        tags.append("elysium.src=%d" % rec["src"])
    tags.extend(["elysium.type=%d" % rec["kind"], "elysium.style=%d" % rec["style"]])
    position = SimpleNamespace(x=rec["position"][0], y=rec["position"][1], z=rec["position"][2])
    rotation = SimpleNamespace(
        pitch=rec["rotation"][0], yaw=rec["rotation"][1], roll=rec["rotation"][2])
    return SimpleNamespace(
        light_component=_Component(rec),
        tags=tags,
        get_actor_location=lambda: position,
        get_actor_rotation=lambda: rotation,
        get_folder_path=lambda: "Sky/Lights" if rec["sky"] else "Lights",
        get_actor_label=lambda: rec["label"])


def test_record_carries_every_key_the_placement_reads() -> None:
    assert set(_record()) == PLACEMENT_KEYS
    # The module's own guard has to name the same set, or `load` would pass a row through that
    # `_place_one_light` then dies on hundreds of lights into a bake.
    assert set(light_store.RECORD_FIELDS) == PLACEMENT_KEYS
    # The derived path never asks for inverse-square falloff -- VtMB light is ~flat inside its
    # authored radius -- so a record built from `derive_light` must default it off.
    assert _record()["use_inverse_squared_falloff"] is False
    assert _record()["src"] == 7
    # A hand-added light has no lump-15 row, and `None` is how the next harvest tells it apart.
    assert _record(src=None)["src"] is None


def test_harvest_of_a_placed_record_converges() -> None:
    # One pass: everything but the colour survives exactly, and the colour survives to within the
    # single 8-bit sRGB step `FColor` quantises it to.
    placed = _record()
    harvested = light_store._record_from_actor(_place(placed))
    for key in PLACEMENT_KEYS - {"color"}:
        assert harvested[key] == placed[key], key
    for got, want in zip(harvested["color"], placed["color"]):
        assert got == pytest.approx(want, abs=0.01)

    # Two passes: the second harvest reads back a colour that is already a quantised one, so the
    # store has reached its fixed point and no later bake rewrites it. This is the property the
    # whole design rests on.
    again = light_store._record_from_actor(_place(harvested))
    assert again == harvested


def test_spot_and_sun_records_round_trip() -> None:
    spot = light_store.record(
        dict(FINAL, kind=2, outer_cone_deg=52.5, inner_cone_deg=11.25),
        src=3, style=32, sky=False, rotation=[-15.0, 90.0, 0.0], label="Light_3_spot")
    assert light_store._record_from_actor(_place(spot))["style"] == 32
    assert light_store._record_from_actor(_place(spot))["rotation"] == [-15.0, 90.0, 0.0]

    sun = light_store.record(
        dict(FINAL, kind=3, allow_mega_lights=False, sun_source_angle_deg=0.5357),
        src=0, style=0, sky=False, rotation=[-45.0, 0.0, 0.0], label="Light_0_sun")
    harvested = light_store._record_from_actor(_place(sun))
    assert harvested["kind"] == 3
    assert harvested["allow_mega_lights"] is False
    assert harvested["sun_source_angle_deg"] == pytest.approx(0.5357)

    # A miniature light is filed under `Sky/Lights`, which is the only place the flag survives on
    # the actor -- there is no `elysium.sky=` tag for the harvest to read instead.
    miniature = _record(sky=True)
    assert light_store._record_from_actor(_place(miniature))["sky"] is True


def test_save_load_round_trip_and_quiet_rewrite(tmp_path) -> None:
    content = os.fspath(tmp_path)
    rows = [_record(), _record(src=None, label="Light_hand_added")]
    assert light_store.save(content, "sp_tutorial_1", rows) is True

    loaded = light_store.load(content, "sp_tutorial_1")
    # Retail lights first in lump-15 order, then anything a hand pass added: a file whose rows
    # moved for no reason is a file nobody reads.
    assert [row["src"] for row in loaded] == [7, None]
    assert loaded == sorted(rows, key=light_store.sort_key)

    # A bake that harvests a level nobody edited must leave the file's mtime and its Git status
    # alone, or every `export map` would show up as a lighting change.
    assert light_store.save(content, "sp_tutorial_1", rows) is False
    assert light_store.save(content, "sp_tutorial_1", [_record(intensity=0.9)]) is True


def test_an_unreadable_store_means_derive_not_fail(tmp_path) -> None:
    content = os.fspath(tmp_path)
    assert light_store.load(content, "sm_hub_1") is None  # no file at all

    path = light_store.store_path(content, "sm_hub_1")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("{ not json")
    assert light_store.load(content, "sm_hub_1") is None

    with open(path, "w", encoding="utf-8") as handle:
        json.dump({"schema": light_store.SCHEMA + 1, "lights": []}, handle)
    assert light_store.load(content, "sm_hub_1") is None

    # A row missing a key the placement reads is caught here, where the answer is still "derive",
    # rather than hundreds of lights into a bake, where it would be a crash.
    maimed = _record()
    maimed.pop("reach_cm")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump({"schema": light_store.SCHEMA, "lights": [maimed]}, handle)
    assert light_store.load(content, "sm_hub_1") is None

    with open(path, "w", encoding="utf-8") as handle:
        json.dump({"schema": light_store.SCHEMA, "lights": [_record()]}, handle)
    assert len(light_store.load(content, "sm_hub_1")) == 1


def test_store_path_is_under_the_one_authored_tree() -> None:
    path = light_store.store_path("C:/proj/Content", "sp_tutorial_1")
    assert path.replace("\\", "/").endswith(
        "Content/ElysiumAuthored/Lighting/sp_tutorial_1.lights.json")
