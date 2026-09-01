"""The V2 map bake's geometry and placement reader (R5.1).

`docs/architecture/seam_map_map.md` -> "## Import -- geometry and placements" states where a map's
world/sky/brush meshes and its static-prop placements come from once a map is on the V2 lane. Two
things in that section can change shipped content without changing any other test: the **frame**
(one reflection and one quaternion conjugation, wrong in a way that is invisible until a whole map
is inside out) and the **placement mapping** (which record field becomes collision, which becomes a
skin, which becomes a cull distance).

The first two cases pin those against arithmetic. The last two pin the reader against the legacy
exporter's own output on the real corpus -- the only witness that "the producer changed" and "the
geometry changed" are separable -- and skip, loudly, when that corpus is not on this machine.
"""
from __future__ import annotations

import pytest

from elysium_pipeline import map_transport, paths, shared_corpus
from elysium_pipeline.formats.bsp import source_quat_to_unreal, source_to_unreal
from elysium_pipeline.importers import map_geometry as MG

#: The three-map working corpus (`seam_migration.md` -> R1); a whole-corpus run is a separate,
#: owner-approved step and this module never asks for one.
WORKING_MAPS = ("sp_tutorial_1", "sm_pawnshop_1", "sm_hub_1")


def test_gltf_frame_matches_source_to_unreal_through_the_units_own_transform():
    # The unit publishes `(x, y, z)_gltf = (x, z, -y)_source * 0.0254` and the bake wants
    # `source_to_unreal`. Composing the published transform with this module's inverse has to land
    # exactly on the value the legacy exporter wrote, for positions and for rotations alike.
    for source_point in ((0.0, 0.0, 0.0), (128.0, -64.0, 32.0), (-7831.0, 3759.0, 6441.0)):
        x, y, z = source_point
        gltf = (x * 0.0254, z * 0.0254, -y * 0.0254)
        assert MG.gltf_position_to_unreal(gltf) == pytest.approx(
            source_to_unreal(x, y, z), abs=1e-6)

    for source_quat in ((0.0, 0.0, 0.0, 1.0), (0.0, 0.0, 0.7071067811865476, 0.7071067811865476),
                        (0.1825741858350554, 0.3651483716701108, 0.5477225575051661,
                         0.7302967433402215)):
        qx, qy, qz, qw = source_quat
        gltf = (qx, qz, -qy, qw)
        expected = source_quat_to_unreal(qx, qy, qz, qw)
        actual = MG.gltf_quat_to_unreal(gltf)
        # q and -q name one rotation; the reader is free in the overall sign, nothing else.
        assert (actual == pytest.approx(expected, abs=1e-12)
                or actual == pytest.approx(tuple(-v for v in expected), abs=1e-12))


def test_placement_reads_solid_skin_and_fade_off_the_record_not_the_model():
    def placement(**overrides):
        fields = dict(
            index=0, stem="models_scenery_lamp", model_path="models/scenery/lamp.mdl",
            position=(0.0, 0.0, 0.0), rotation=(0.0, 0.0, 0.0, 1.0),
            solid=6, flags=0, skin=0, fade_min_cm=0.0, fade_max_cm=0.0, sky=False)
        fields.update(overrides)
        return MG.Placement(**fields)

    # SOLID_NONE is the one value that does not block; every other SolidType_t does, including the
    # VPHYSICS placements of models that ship no `.phy` (the ruling in seam_map_map.md).
    assert placement(solid=MG.SOLID_NONE).solid_blocks is False
    for solid in (1, 2, 3, 4, 5, 6):
        assert placement(solid=solid).solid_blocks is True

    # Fade needs BOTH the flag and a distance: 442 of sp_tutorial_1's 809 records carry (0, 0), and
    # culling those at zero would empty the map.
    assert placement(flags=0x1, fade_max_cm=6350.0).fades is True
    assert placement(flags=0x1, fade_max_cm=0.0).fades is False
    assert placement(flags=0x10, fade_max_cm=6350.0).fades is False
    assert MG.source_inches_to_unreal(2500.0) == pytest.approx(6350.0)


@pytest.mark.parametrize("map_name", WORKING_MAPS)
def test_reader_reproduces_the_legacy_scene_split_on_the_working_corpus(map_name):
    unit = MG.sidecars.unit_paths(map_name)["root"]
    legacy = paths.export_root() / map_name
    world_obj = legacy / f"{map_name}.obj"
    if not unit.is_file():
        pytest.skip(f"no exported map root unit at {unit}")
    if not world_obj.is_file():
        pytest.skip(f"no legacy scene at {world_obj}")

    geometry = MG.read_geometry(map_name)

    for scene, obj_path in ((geometry.world, world_obj),
                            (geometry.sky, legacy / f"{map_name}_sky.obj")):
        if not obj_path.is_file():
            assert scene.tri_count == 0
            continue
        vertices, groups = _read_obj_groups(obj_path)
        assert len(scene.positions) == vertices
        assert {key: len(indices) // 3 for key, indices in scene.groups.items()} == groups

    # The `WorldVertexTransition` blend channel is the one per-vertex value the OBJ itself does not
    # carry: the unit publishes a `DISP_VERT` alpha as the lump's own 0..255 byte and the bake's
    # vertex COLOR.r is 0..1, so a missing normalization would tint every sculpted surface hard onto
    # tex2 without changing a single triangle count above.
    blend_path = legacy / f"{map_name}.blend"
    if blend_path.is_file():
        legacy_blend = [float(value) for value in blend_path.read_text().split()]
        assert len(geometry.world.blend) == len(legacy_blend)
        # The sidecar prints four decimals, so 1/255 quantisation shows at 5e-5.
        assert geometry.world.blend == pytest.approx(legacy_blend, abs=1e-4)
    else:
        assert not any(geometry.world.blend)

    brush_dir = legacy / "brushes"
    if brush_dir.is_dir():
        assert sorted(geometry.brush_stems().values()) == sorted(
            path.stem for path in brush_dir.glob("brush_*.obj"))


@pytest.mark.parametrize("map_name", WORKING_MAPS)
def test_reader_reproduces_every_legacy_props_row_on_the_working_corpus(map_name):
    unit = MG.sidecars.unit_paths(map_name)["root"]
    props_path = paths.export_root() / map_name / f"{map_name}.props"
    if not unit.is_file():
        pytest.skip(f"no exported map root unit at {unit}")
    if not props_path.is_file():
        pytest.skip(f"no legacy placements at {props_path}")

    placements = MG.read_geometry(map_name).placements
    rows = [line.split() for line in props_path.read_text(
        encoding="utf-8", errors="replace").splitlines() if line.split()]
    assert len(placements) == len(rows)

    for placement, row in zip(placements, rows):
        # `.props` fields: stem, origin (cm), quaternion, solid, skin, sky, model path.
        assert placement.stem == row[0]
        assert placement.stem == shared_corpus.static_stem(placement.model_path)
        assert placement.position == pytest.approx(
            tuple(float(value) for value in row[1:4]), abs=0.01)
        legacy_quat = tuple(float(value) for value in row[4:8])
        assert (placement.rotation == pytest.approx(legacy_quat, abs=2e-4)
                or placement.rotation == pytest.approx(
                    tuple(-value for value in legacy_quat), abs=2e-4))
        assert placement.solid == int(row[8])
        assert placement.skin == int(row[9])
        assert placement.sky is bool(int(row[10]))


def test_the_v2_model_flag_is_its_own_list_and_excludes_sp_theatre():
    # sp_theatre is on the R4.6 entity/collision/environment transport but its models have not been
    # imported, so it must not be carried onto the V2 model root by reusing that list.
    v2 = map_transport.read_array(map_transport.V2_MODELS_KEY)
    transport = map_transport.read_array(map_transport.NEW_TRANSPORT_KEY)
    assert set(v2) == set(WORKING_MAPS)
    assert "sp_theatre" in transport
    assert map_transport.is_map_on_v2_models("sp_theatre") is False
    # Matched case-insensitively, exactly as `ElysiumMapTransport::IsMapOnV2Models` matches.
    assert map_transport.is_map_on_v2_models("SP_Tutorial_1") is True


def _read_obj_groups(path):
    """`(vertex count, {material: triangle count})` for one exported OBJ."""
    groups: dict[str, int] = {}
    vertices = 0
    current = None
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        token = line.split()
        if not token:
            continue
        if token[0] == "v":
            vertices += 1
        elif token[0] == "usemtl":
            current = token[1]
            groups.setdefault(current, 0)
        elif token[0] == "f":
            groups[current] = groups.get(current, 0) + 1
    return vertices, {key: value for key, value in groups.items() if value}
