"""Decode VtMB's renderer-side cloth payload for selected v2531 models.

The low-address cloth path in ``StudioRender.dll`` addresses authored data at
``StudioModel +200/+204``, collision primitives at ``+208..+220``, and
per-render-vertex maps at ``StudioMesh +48/+52/+56``.  This probe validates
every relative address and the cross-record invariants before writing a JSON
report below ``ELYSIUM_WORK_ROOT/research``.

Usage:
    uv run elysium research cloth_payload_audit [model.mdl ...] [--report PATH]
"""

from __future__ import annotations

import argparse
import json
import struct
from collections import Counter
from pathlib import Path
from typing import Any

from elysium_pipeline.formats import install
from elysium_pipeline.paths import research_root


DEFAULT_MODELS = (
    "models/character/npc/unique/santa_monica/jeanette/jeanette.mdl",
    "models/character/npc/unique/downtown/sheriff/sheriff.mdl",
)

MODEL_STRIDE = 224
MESH_STRIDE = 60
TEXTURE_STRIDE = 20


def i32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<i", data, offset)[0]


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def f32(data: bytes, offset: int) -> float:
    return struct.unpack_from("<f", data, offset)[0]


def cstr(data: bytes, offset: int) -> str:
    end = data.find(b"\0", offset)
    if end < 0:
        raise ValueError(f"unterminated string at {offset}")
    return data[offset:end].decode("ascii", "replace")


def checked_relative(data: bytes, owner: int, relative: int, size: int, label: str) -> int:
    absolute = owner + relative
    if relative <= 0 or absolute < 0 or absolute + size > len(data):
        raise ValueError(
            f"{label}: relative {relative} from {owner} resolves outside {len(data)} bytes"
        )
    return absolute


def materials(data: bytes) -> list[str]:
    count = i32(data, 292)
    base = i32(data, 296)
    result = []
    for index in range(count):
        record = base + index * TEXTURE_STRIDE
        result.append(cstr(data, record + i32(data, record)))
    return result


def cloth_definition(data: bytes, model: int, cloth_index: int) -> dict[str, Any]:
    count = i32(data, model + 200)
    table = checked_relative(data, model, i32(data, model + 204), count * 4, "cloth table")
    relative = i32(data, table + cloth_index * 4)
    record = checked_relative(data, model, relative, 92, f"cloth definition {cloth_index}")

    particles = i32(data, record + 4)
    anchored = i32(data, record + 8)
    dynamic = i32(data, record + 12)
    total_constraints = i32(data, record + 20)
    distance_constraints = i32(data, record + 24)
    compression_constraints = i32(data, record + 28)
    collision_triangles = i32(data, record + 48)
    if anchored + dynamic != particles:
        raise ValueError(
            f"cloth definition {cloth_index}: {anchored} anchored + {dynamic} dynamic "
            f"!= {particles} particles"
        )
    if distance_constraints + compression_constraints != total_constraints:
        raise ValueError(
            f"cloth definition {cloth_index}: {distance_constraints} distance + "
            f"{compression_constraints} compression != {total_constraints} constraints"
        )

    anchors = []
    if anchored:
        anchor_base = checked_relative(
            data, record, i32(data, record + 16), anchored * 2, "anchor vertex indices"
        )
        anchors = [u16(data, anchor_base + index * 2) for index in range(anchored)]

    constraints = []
    constraint_count = total_constraints
    if constraint_count:
        constraint_base = checked_relative(
            data,
            record,
            i32(data, record + 32),
            constraint_count * 16,
            "distance constraints",
        )
        for index in range(constraint_count):
            base = constraint_base + index * 16
            constraints.append(
                {
                    "a": u16(data, base),
                    "b": u16(data, base + 2),
                    "move_a": f32(data, base + 4),
                    "move_b": f32(data, base + 8),
                    "rest_length_squared": f32(data, base + 12),
                    "kind": (
                        "distance"
                        if index < distance_constraints
                        else "compression_only"
                    ),
                }
            )

    triangles = []
    if collision_triangles:
        triangle_base = checked_relative(
            data,
            record,
            i32(data, record + 52),
            collision_triangles * 6,
            "collision triangles",
        )
        for index in range(collision_triangles):
            base = triangle_base + index * 6
            triangles.append([u16(data, base), u16(data, base + 2), u16(data, base + 4)])

    return {
        "record_offset": record - model,
        "gravity_scale": f32(data, record),
        "particles": particles,
        "anchored_particles": anchored,
        "dynamic_particles": dynamic,
        "anchor_vertex_indices": anchors,
        "total_constraint_count": total_constraints,
        "distance_constraint_count": distance_constraints,
        "compression_constraint_count": compression_constraints,
        "constraints": constraints,
        "distance_simd_block_count": i32(data, record + 36),
        "compression_simd_block_count": i32(data, record + 40),
        "packed_simd_payload_offset": i32(data, record + 44),
        "collision_triangle_count": collision_triangles,
        "collision_triangles": triangles,
        "tangent_edge_count": i32(data, record + 56),
        "normal_edge_count": i32(data, record + 60),
        "edge_pair_offset": i32(data, record + 64),
        "normal_contribution_count": i32(data, record + 68),
        "normal_contribution_offset": i32(data, record + 72),
        "extra_tangent_output_count": i32(data, record + 76),
        "tangent_interpolation_offset": i32(data, record + 80),
        "optional_seed_a_offset": i32(data, record + 84),
        "optional_seed_b_offset": i32(data, record + 88),
        "raw_dwords_0x00_0x58": [i32(data, record + offset) for offset in range(0, 92, 4)],
    }


def mapped_mesh(
    data: bytes,
    mesh: int,
    name: str,
    particle_counts: list[int],
) -> dict[str, Any]:
    vertices = i32(data, mesh + 8)
    selector_relative = i32(data, mesh + 48)
    normal_relative = i32(data, mesh + 52)
    tangent_relative = i32(data, mesh + 56)
    result: dict[str, Any] = {
        "material": name,
        "vertices": vertices,
        "selector_offset": selector_relative,
        "position_normal_map_offset": normal_relative,
        "tangent_map_offset": tangent_relative,
    }
    if not (selector_relative or normal_relative or tangent_relative):
        result["cloth_mapped"] = False
        return result
    if not (selector_relative and normal_relative and tangent_relative):
        raise ValueError(f"{name}: partial cloth map triple")

    selector_base = checked_relative(data, mesh, selector_relative, vertices, f"{name} selector")
    normal_base = checked_relative(
        data, mesh, normal_relative, vertices * 2, f"{name} position/normal map"
    )
    tangent_base = checked_relative(
        data, mesh, tangent_relative, vertices * 2, f"{name} tangent map"
    )
    selectors = list(data[selector_base : selector_base + vertices])
    normal_map = [u16(data, normal_base + index * 2) for index in range(vertices)]
    tangent_map = [u16(data, tangent_base + index * 2) for index in range(vertices)]
    bad_selectors = sorted(
        value for value in set(selectors) if value != 0xFF and value >= len(particle_counts)
    )
    if bad_selectors:
        raise ValueError(f"{name}: invalid cloth selectors {bad_selectors}")

    cloth_rows = [
        (selector, normal_map[index], tangent_map[index])
        for index, selector in enumerate(selectors)
        if selector != 0xFF
    ]
    bad_particle_indices = sorted(
        {
            position_normal & 0x7FFF
            for selector, position_normal, _ in cloth_rows
            if (position_normal & 0x7FFF) >= particle_counts[selector]
        }
    )
    if bad_particle_indices:
        raise ValueError(
            f"{name}: cloth particle indices outside definitions {bad_particle_indices}"
        )

    result.update(
        {
            "cloth_mapped": True,
            "ordinary_skinning_vertices": selectors.count(0xFF),
            "cloth_vertices": vertices - selectors.count(0xFF),
            "selector_histogram": {
                ("ordinary" if value == 0xFF else str(value)): count
                for value, count in sorted(Counter(selectors).items())
            },
            "position_index_min": min(value & 0x7FFF for _, value, _ in cloth_rows),
            "position_index_max": max(value & 0x7FFF for _, value, _ in cloth_rows),
            "flipped_normal_vertices": sum(bool(value & 0x8000) for _, value, _ in cloth_rows),
            "tangent_index_min": min(value for _, _, value in cloth_rows),
            "tangent_index_max": max(value for _, _, value in cloth_rows),
        }
    )
    return result


def collision_primitive_table(
    data: bytes,
    model: int,
    count_offset: int,
    table_offset: int,
    stride: int,
    label: str,
) -> dict[str, int]:
    count = i32(data, model + count_offset)
    relative = i32(data, model + table_offset)
    absolute = 0
    if count:
        absolute = checked_relative(data, model, relative, count * stride, label)
    return {
        "count": count,
        "offset": relative,
        "absolute_offset": absolute,
        "record_size": stride,
    }


def audit_one(index: dict, key: str) -> dict[str, Any]:
    normalized = key.replace("\\", "/").lower()
    data = install.read(index, normalized)
    if not data:
        raise FileNotFoundError(key)
    if data[:4] != b"IDST" or i32(data, 4) != 2531:
        raise ValueError(f"{key}: not a v2531 MDL")

    texture_names = materials(data)
    models = []
    bodypart_count = i32(data, 320)
    bodypart_base = i32(data, 324)
    for bodypart_index in range(bodypart_count):
        bodypart = bodypart_base + bodypart_index * 16
        model_count = i32(data, bodypart + 4)
        model_base = bodypart + i32(data, bodypart + 12)
        for model_index in range(model_count):
            model = model_base + model_index * MODEL_STRIDE
            cloth_count = i32(data, model + 200)
            definitions = [cloth_definition(data, model, item) for item in range(cloth_count)]
            capsules = collision_primitive_table(
                data, model, 208, 212, 36, "authored cloth capsules"
            )
            spheres = collision_primitive_table(
                data, model, 216, 220, 20, "authored cloth spheres"
            )
            mesh_count = i32(data, model + 136)
            mesh_base = model + i32(data, model + 140)
            meshes = []
            for mesh_index in range(mesh_count):
                mesh = mesh_base + mesh_index * MESH_STRIDE
                material = i32(data, mesh)
                name = (
                    texture_names[material]
                    if 0 <= material < len(texture_names)
                    else f"#{material}"
                )
                meshes.append(
                    mapped_mesh(
                        data,
                        mesh,
                        name,
                        [item["particles"] for item in definitions],
                    )
                )
            cloth_vertices = sum(item.get("cloth_vertices", 0) for item in meshes)
            if (
                len(definitions) == 1
                and definitions[0]["extra_tangent_output_count"] != cloth_vertices
            ):
                raise ValueError(
                    f"{key} {cstr(data, model)}: {cloth_vertices} mapped cloth vertices != "
                    f"{definitions[0]['extra_tangent_output_count']} tangent outputs"
                )
            models.append(
                {
                    "bodypart": bodypart_index,
                    "model": model_index,
                    "name": cstr(data, model),
                    "cloth_definition_count": cloth_count,
                    "cloth_definition_table_offset": i32(data, model + 204),
                    "authored_collision_capsules": capsules,
                    "authored_collision_spheres": spheres,
                    "cloth_vertex_count": cloth_vertices,
                    "definitions": definitions,
                    "meshes": meshes,
                }
            )
    return {
        "model_path": normalized,
        "file_size": len(data),
        "header_flags": i32(data, 228),
        "cloth_header_flag": bool(i32(data, 228) & 0x400),
        "models": models,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("models", nargs="*", default=list(DEFAULT_MODELS))
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    index = install.build_index()
    report = {
        "format": "vtmb-cloth-payload-audit-v2",
        "models": [audit_one(index, key) for key in args.models],
    }
    report_path = args.report or research_root() / "cloth-payload-audit.json"
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")

    for item in report["models"]:
        print(
            f"{item['model_path']}: flags=0x{item['header_flags']:x} "
            f"cloth={item['cloth_header_flag']}"
        )
        for model in item["models"]:
            print(
                f"  {model['name']}: definitions={model['cloth_definition_count']} "
                f"capsules={model['authored_collision_capsules']['count']} "
                f"spheres={model['authored_collision_spheres']['count']} "
                f"cloth-vertices={model['cloth_vertex_count']}"
            )
            for number, definition in enumerate(model["definitions"]):
                print(
                    f"    cloth[{number}]: gravity-scale={definition['gravity_scale']:.6g} "
                    f"particles={definition['particles']} "
                    f"({definition['anchored_particles']} anchored + "
                    f"{definition['dynamic_particles']} dynamic) "
                    f"constraints={definition['distance_constraint_count']}+"
                    f"{definition['compression_constraint_count']} "
                    f"triangles={definition['collision_triangle_count']}"
                )
            for mesh in model["meshes"]:
                if mesh["cloth_mapped"]:
                    print(
                        f"    {mesh['material']}: {mesh['cloth_vertices']}/"
                        f"{mesh['vertices']} cloth vertices, selectors="
                        f"{mesh['selector_histogram']}, position-index="
                        f"{mesh['position_index_min']}..{mesh['position_index_max']}, "
                        f"tangent-index={mesh['tangent_index_min']}.."
                        f"{mesh['tangent_index_max']}"
                    )
    print(f"report: {report_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
