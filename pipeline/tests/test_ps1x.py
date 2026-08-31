"""Contract tests for the ps.1.x interpreter, `elysium_pipeline.validation.ps1x`.

Every hand-built test constructs the same shape the shader-source unit publishes -- a `source`
block with `version`, `defines[]` and `instructions[]` rows -- so nothing here depends on a VtMB
installation. The corpus-backed tests at the bottom skip themselves when
`$ELYSIUM_WORK_ROOT/exports_v2/shader-programs/source` is not present on this machine, the same
way `test_vdata_corpus_import.py`'s legacy-parity check abstains.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any

import numpy as np
import pytest

from elysium_pipeline.validation.ps1x import (
    Program,
    Ps1xError,
    Ps1xUnsupported,
    evaluate,
    evaluate_graph,
)

# --------------------------------------------------------------------------------- builders


def _reg(cls: str, index: int, *, selector: str | None = None, negate: bool = False,
         complement: bool = False, modifier: str | None = None) -> dict[str, Any]:
    return {
        "registerClass": cls,
        "registerIndex": index,
        "selector": selector,
        "negate": negate,
        "complement": complement,
        "modifier": modifier,
    }


def _dest(cls: str, index: int, write_mask: str = "") -> dict[str, Any]:
    return {"registerClass": cls, "registerIndex": index, "writeMask": write_mask}


def _instr(opcode: str, destination: dict[str, Any] | None = None,
           sources: tuple[dict[str, Any], ...] = (), modifiers: tuple[str, ...] = (),
           co_issued: bool = False, line: int = 0) -> dict[str, Any]:
    return {
        "line": line,
        "coIssued": co_issued,
        "opcode": opcode,
        "modifiers": list(modifiers),
        "destination": destination,
        "sources": list(sources),
    }


def _unit(instructions: list[dict[str, Any]], version: tuple[int, int] = (1, 1),
          defines: tuple[dict[str, Any], ...] = ()) -> dict[str, Any]:
    return {
        "source": {
            "version": {"major": version[0], "minor": version[1]},
            "defines": list(defines),
            "instructions": instructions,
        }
    }


def _program(instructions: list[dict[str, Any]], version: tuple[int, int] = (1, 1)) -> Program:
    return Program.from_unit(_unit(instructions, version=version), unit_name="test")


def _run(instructions: list[dict[str, Any]], *, version: tuple[int, int] = (1, 1),
          constants: dict[int, Any] | None = None, vertex: dict[int, Any] | None = None,
          texcoords: dict[int, Any] | None = None, textures: dict[int, Any] | None = None,
          n: int = 1):
    program = _program(instructions, version=version)
    return evaluate(
        program,
        textures=textures or {},
        constants=constants or {},
        vertex=vertex or {},
        texcoords=texcoords or {i: np.zeros((n, 4), dtype=np.float32) for i in range(1)},
    )


# --------------------------------------------------------------------------------- ALU ops


def test_mov_copies_the_source_register():
    result = _run(
        [_instr("mov", _dest("temp", 0), (_reg("const", 0),))],
        constants={0: (0.1, 0.2, 0.3, 0.4)},
    )
    np.testing.assert_allclose(result, [[0.1, 0.2, 0.3, 0.4]], atol=1e-6)


def test_add_sums_the_two_sources():
    result = _run(
        [_instr("add", _dest("temp", 0), (_reg("const", 0), _reg("const", 1)))],
        constants={0: (0.1, 0.1, 0.1, 0.1), 1: (0.2, 0.2, 0.2, 0.2)},
    )
    np.testing.assert_allclose(result, [[0.3, 0.3, 0.3, 0.3]], atol=1e-6)


def test_sub_subtracts_the_second_source():
    result = _run(
        [_instr("sub", _dest("temp", 0), (_reg("const", 0), _reg("const", 1)))],
        constants={0: (0.5, 0.5, 0.5, 0.5), 1: (0.2, 0.2, 0.2, 0.2)},
    )
    np.testing.assert_allclose(result, [[0.3, 0.3, 0.3, 0.3]], atol=1e-6)


def test_mul_multiplies_the_two_sources():
    result = _run(
        [_instr("mul", _dest("temp", 0), (_reg("const", 0), _reg("const", 1)))],
        constants={0: (0.5, 0.4, 0.3, 0.2), 1: (0.2, 0.2, 0.2, 0.2)},
    )
    np.testing.assert_allclose(result, [[0.1, 0.08, 0.06, 0.04]], atol=1e-6)


def test_mad_multiplies_then_adds_the_third_source():
    result = _run(
        [_instr("mad", _dest("temp", 0), (_reg("const", 0), _reg("const", 1), _reg("const", 2)))],
        constants={0: (0.5, 0.5, 0.5, 0.5), 1: (0.2, 0.2, 0.2, 0.2), 2: (0.1, 0.1, 0.1, 0.1)},
    )
    np.testing.assert_allclose(result, [[0.2, 0.2, 0.2, 0.2]], atol=1e-6)


def test_lrp_operand_order_is_t_a_b_not_a_b_t():
    # D3D9: lrp dst, t, a, b == t*a + (1-t)*b
    result = _run(
        [_instr("lrp", _dest("temp", 0), (_reg("const", 0), _reg("const", 1), _reg("const", 2)))],
        constants={0: (0.25,) * 4, 1: (1.0,) * 4, 2: (0.0,) * 4},
    )
    # t=0.25, a=1, b=0 -> 0.25*1 + 0.75*0 = 0.25
    np.testing.assert_allclose(result, [[0.25, 0.25, 0.25, 0.25]], atol=1e-6)


def test_dp3_dots_the_first_three_components_and_replicates():
    result = _run(
        [_instr("dp3", _dest("temp", 0), (_reg("const", 0), _reg("const", 1)))],
        version=(1, 4),
        constants={0: (1.0, 2.0, 3.0, 99.0), 1: (0.5, 0.5, 0.5, -99.0)},
    )
    expected = 1.0 * 0.5 + 2.0 * 0.5 + 3.0 * 0.5
    np.testing.assert_allclose(result, [[expected] * 4], atol=1e-6)


def test_dp4_dots_all_four_components():
    result = _run(
        [_instr("dp4", _dest("temp", 0), (_reg("const", 0), _reg("const", 1)))],
        version=(1, 4),
        constants={0: (1.0, 2.0, 3.0, 4.0), 1: (0.5, 0.5, 0.5, 0.5)},
    )
    expected = (1.0 + 2.0 + 3.0 + 4.0) * 0.5
    np.testing.assert_allclose(result, [[expected] * 4], atol=1e-6)


def test_cnd_selects_per_component_on_cond_greater_than_half():
    result = _run(
        [_instr("cnd", _dest("temp", 0), (_reg("const", 0), _reg("const", 1), _reg("const", 2)))],
        constants={0: (0.6, 0.4, 0.6, 0.4), 1: (1.0, 1.0, 1.0, 1.0), 2: (0.0, 0.0, 0.0, 0.0)},
    )
    np.testing.assert_allclose(result, [[1.0, 0.0, 1.0, 0.0]], atol=1e-6)


def test_cmp_selects_per_component_on_source_greater_or_equal_zero():
    result = _run(
        [_instr("cmp", _dest("temp", 0), (_reg("const", 0), _reg("const", 1), _reg("const", 2)))],
        constants={0: (0.5, -0.5, 0.0, -1.0), 1: (1.0, 1.0, 1.0, 1.0), 2: (0.0, 0.0, 0.0, 0.0)},
    )
    np.testing.assert_allclose(result, [[1.0, 0.0, 1.0, 0.0]], atol=1e-6)


# --------------------------------------------------------------------------------- operand modifiers


def test_source_modifier_bx2_expands_zero_to_one_into_negative_one_to_one():
    result = _run(
        [_instr("mov", _dest("temp", 0), (_reg("const", 0, modifier="_bx2"),))],
        version=(1, 4),
        constants={0: (0.75,) * 4},
    )
    np.testing.assert_allclose(result, [[0.5] * 4], atol=1e-6)


def test_source_modifier_bias_subtracts_one_half():
    result = _run(
        [_instr("mov", _dest("temp", 0), (_reg("const", 0, modifier="_bias"),))],
        version=(1, 4),
        constants={0: (0.75,) * 4},
    )
    np.testing.assert_allclose(result, [[0.25] * 4], atol=1e-6)


def test_source_modifier_x2_doubles():
    result = _run(
        [_instr("mov", _dest("temp", 0), (_reg("const", 0, modifier="_x2"),))],
        version=(1, 4),
        constants={0: (0.75,) * 4},
    )
    np.testing.assert_allclose(result, [[1.5] * 4], atol=1e-6)


def test_negate_flips_sign_before_complement():
    result = _run(
        [_instr("mov", _dest("temp", 0), (_reg("const", 0, negate=True),))],
        constants={0: (0.3,) * 4},
    )
    np.testing.assert_allclose(result, [[-0.3] * 4], atol=1e-6)


def test_complement_computes_one_minus_x():
    result = _run(
        [_instr("mov", _dest("temp", 0), (_reg("const", 0, complement=True),))],
        constants={0: (0.3,) * 4},
    )
    np.testing.assert_allclose(result, [[0.7] * 4], atol=1e-6)


def test_selector_a_replicates_the_alpha_channel():
    result = _run(
        [_instr("mov", _dest("temp", 0), (_reg("const", 0, selector="a"),))],
        constants={0: (0.1, 0.2, 0.3, 0.9)},
    )
    np.testing.assert_allclose(result, [[0.9, 0.9, 0.9, 0.9]], atol=1e-6)


def test_ps14_arbitrary_four_letter_swizzle_reorders_channels():
    result = _run(
        [_instr("mov", _dest("temp", 0), (_reg("const", 0, selector="abgr"),))],
        version=(1, 4),
        constants={0: (0.1, 0.2, 0.3, 0.9)},
    )
    np.testing.assert_allclose(result, [[0.9, 0.3, 0.2, 0.1]], atol=1e-6)


# --------------------------------------------------------------------------------- destination modifiers


def test_destination_modifier_x2_scales_the_result():
    result = _run(
        [_instr("mov", _dest("temp", 0), (_reg("const", 0),), modifiers=("_x2",))],
        version=(1, 4),
        constants={0: (0.6,) * 4},
    )
    np.testing.assert_allclose(result, [[1.2] * 4], atol=1e-6)


def test_destination_modifier_sat_clamps_to_zero_one():
    result = _run(
        [_instr("mov", _dest("temp", 0), (_reg("const", 0),), modifiers=("_sat",))],
        version=(1, 4),
        constants={0: (1.5,) * 4},
    )
    np.testing.assert_allclose(result, [[1.0] * 4], atol=1e-6)


def test_write_mask_merge_leaves_untouched_channels_alone():
    result = _run(
        [
            _instr("mov", _dest("temp", 0), (_reg("const", 0),)),
            _instr("mov", _dest("temp", 0, "rg"), (_reg("const", 1),)),
        ],
        constants={0: (0.1, 0.1, 0.1, 0.1), 1: (0.9, 0.9, 0.9, 0.9)},
    )
    np.testing.assert_allclose(result, [[0.9, 0.9, 0.1, 0.1]], atol=1e-6)


# --------------------------------------------------------------------------------- version clamp


def test_ps11_clamps_register_writes_to_minus_one_one():
    result = _run(
        [_instr("mul", _dest("temp", 0), (_reg("const", 0), _reg("const", 1)))],
        version=(1, 1),
        constants={0: (2.0,) * 4, 1: (2.0,) * 4},
    )
    np.testing.assert_allclose(result, [[1.0] * 4], atol=1e-6)


def test_ps14_clamps_register_writes_to_minus_eight_eight():
    result = _run(
        [_instr("mul", _dest("temp", 0), (_reg("const", 0), _reg("const", 1)))],
        version=(1, 4),
        constants={0: (3.0,) * 4, 1: (3.0,) * 4},
    )
    # 3*3 = 9, clamped to 8 under ps.1.4 but not clamped to 1 the way ps.1.1 would.
    np.testing.assert_allclose(result, [[8.0] * 4], atol=1e-6)
    unclamped = _run(
        [_instr("mul", _dest("temp", 0), (_reg("const", 0), _reg("const", 1)))],
        version=(1, 4),
        constants={0: (2.0,) * 4, 1: (2.0,) * 4},
    )
    np.testing.assert_allclose(unclamped, [[4.0] * 4], atol=1e-6)


# --------------------------------------------------------------------------------- texkill


def test_texkill_marks_pixels_with_a_negative_component():
    instructions = [
        _instr("texcoord", _dest("texture", 0)),
        _instr("texkill", _dest("texture", 0)),
    ]
    program = _program(instructions)
    result = evaluate(
        program,
        textures={},
        constants={},
        vertex={},
        texcoords={0: np.array([[0.1, 0.1, 0.1, 0.1], [-0.2, 0.1, 0.1, 0.1]], dtype=np.float32)},
    )
    np.testing.assert_array_equal(result.killed, [False, True])


# --------------------------------------------------------------------------------- co-issue


def test_co_issued_instructions_with_disjoint_masks_parse_cleanly():
    _program(
        [
            _instr("mov", _dest("temp", 0, "rgb"), (_reg("const", 0),), line=1),
            _instr("mov", _dest("temp", 0, "a"), (_reg("const", 1),), co_issued=True, line=2),
        ]
    )


def test_co_issued_instructions_with_overlapping_masks_raise():
    with pytest.raises(Ps1xError):
        _program(
            [
                _instr("mov", _dest("temp", 0, "rgb"), (_reg("const", 0),), line=1),
                _instr("mov", _dest("temp", 0, "rg"), (_reg("const", 1),), co_issued=True, line=2),
            ]
        )


# --------------------------------------------------------------------------------- unsupported opcodes


def test_texm3x3vspec_raises_ps1x_unsupported_naming_the_opcode_and_unit():
    with pytest.raises(Ps1xUnsupported, match="texm3x3vspec"):
        Program.from_unit(
            _unit(
                [
                    _instr("texm3x3vspec", _dest("texture", 3), (_reg("texture", 0, modifier="_bx2"),)),
                ]
            ),
            unit_name="basetimeslightmapwet",
        )
    try:
        Program.from_unit(
            _unit([_instr("texm3x3vspec", _dest("texture", 3), (_reg("texture", 0),))]),
            unit_name="basetimeslightmapwet",
        )
    except Ps1xUnsupported as exc:
        assert "basetimeslightmapwet" in str(exc)


def test_evaluate_graph_is_a_documented_stub():
    with pytest.raises(NotImplementedError):
        evaluate_graph(object(), {})


# --------------------------------------------------------------------------------- corpus


def _shader_source_root() -> Path | None:
    try:
        from elysium_pipeline.paths import export_v2_root
    except Exception:  # pragma: no cover - import contract, not a runtime path
        return None
    try:
        root = export_v2_root() / "shader-programs" / "source"
    except RuntimeError:
        return None
    return root if root.is_dir() else None


def test_the_corpus_parses_every_unit_or_names_an_excluded_opcode():
    root = _shader_source_root()
    if root is None:
        pytest.skip("exports_v2/shader-programs/source is not present on this machine")

    paths = sorted(root.glob("*.glb"))
    assert paths, f"no shader-source units found under {root}"

    supported_units = 0
    excluded: dict[str, int] = {}
    for path in paths:
        try:
            Program.from_glb(path)
        except Ps1xUnsupported as exc:
            opcode = exc.opcode or "<unknown>"
            assert opcode.startswith(("texm3x2", "texm3x3", "texreg2", "texdp3")), (
                f"{path.name} was excluded for opcode {opcode!r}, outside the documented "
                "texm3x2*/texm3x3*/texreg2*/texdp3* exclusion family"
            )
            excluded[opcode] = excluded.get(opcode, 0) + 1
        else:
            supported_units += 1

    print(f"\nps.1.x corpus census over {len(paths)} units:")
    print(f"  fully parsed: {supported_units}")
    for opcode, count in sorted(excluded.items()):
        print(f"  excluded ({opcode}): {count}")

    assert supported_units + sum(excluded.values()) == len(paths)


def test_vertexlitgeneric_maskedenvmapv2_matches_its_closed_form_algebra():
    root = _shader_source_root()
    if root is None:
        pytest.skip("exports_v2/shader-programs/source is not present on this machine")
    path = root / "vertexlitgeneric_maskedenvmapv2.glb"
    if not path.is_file():
        pytest.skip(f"{path} is not present on this machine")

    program = Program.from_glb(path)

    # Verify the instruction sequence this closed form is derived from, rather than assuming it:
    # tex t0; tex t1; tex t2; texkill t3; mul r0,t0,c3; mul r1,t1,t2; mul r0.rgb,v0,r0;
    # mul_x2 r0.rgb,c0,r0; mad r0.rgb,r1,c2,r0
    opcodes = [instr.opcode for instr in program.instructions]
    assert opcodes == ["tex", "tex", "tex", "texkill", "mul", "mul", "mul", "mul", "mad"]

    n = 4
    rng = np.random.default_rng(0)
    # Small positive magnitudes so no intermediate register write saturates the ps.1.1
    # [-1, 1] clamp -- the closed form below is the unclamped algebra, and only holds while
    # every intermediate stays inside that range.
    t0 = rng.uniform(0.1, 0.4, size=(n, 4)).astype(np.float32)
    t1 = rng.uniform(0.1, 0.4, size=(n, 4)).astype(np.float32)
    t2 = rng.uniform(0.1, 0.4, size=(n, 4)).astype(np.float32)
    t0[:, 3] = 1.0
    t1[:, 3] = 1.0
    t2[:, 3] = 1.0
    c3 = np.array([0.5, 0.6, 0.7, 1.0], dtype=np.float32)
    c0 = np.array([0.4, 0.4, 0.4, 1.0], dtype=np.float32)
    c2 = np.array([0.3, 0.2, 0.5, 1.0], dtype=np.float32)
    v0 = np.array([0.5, 0.5, 0.5, 1.0], dtype=np.float32)

    result = evaluate(
        program,
        textures={0: lambda uv: uv, 1: lambda uv: uv, 2: lambda uv: uv},
        constants={3: c3, 0: c0, 2: c2},
        vertex={0: v0},
        texcoords={0: t0, 1: t1, 2: t2},
    )

    expected_rgb = (t0[:, :3] * c3[:3] * v0[:3] * c0[:3] * 2.0) + t1[:, :3] * t2[:, :3] * c2[:3]
    expected_a = t0[:, 3] * c3[3]

    np.testing.assert_allclose(result[:, :3], expected_rgb, atol=1e-6)
    np.testing.assert_allclose(result[:, 3], expected_a, atol=1e-6)
