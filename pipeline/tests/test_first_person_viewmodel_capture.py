from __future__ import annotations

import ctypes
import math
from pathlib import Path
import tempfile
import unittest

from research.tooling.capture.capture_first_person_viewmodel import (
    ACTION_SCENARIOS,
    MODULES,
    PROJECTION_SCENARIOS,
)
from research.tooling.capture.verify_first_person_viewmodel import (
    BOUNDARY,
    ENTRY,
    EXIT,
    EXPECTED_TARGET_RVAS,
    FileHeader,
    Record,
    Stream,
    VerificationError,
    read_stream,
    verify_streams,
)


def _header(scenario: str) -> FileHeader:
    header = FileHeader()
    header.magic = b"ELGVM1"
    header.format_version = 1
    header.header_bytes = ctypes.sizeof(FileHeader)
    header.record_bytes = ctypes.sizeof(Record)
    header.qpc_frequency = 1_000_000
    header.start_qpc = 100
    header.process_id = 42
    header.vampire_base = 0x10000000
    header.client_base = 0x20000000
    header.engine_base = 0x30000000
    for index, rva in enumerate(EXPECTED_TARGET_RVAS):
        header.target_rvas[index] = rva
    header.vampire_sha256 = MODULES["vampire.dll"].encode("ascii")
    header.client_sha256 = MODULES["client.dll"].encode("ascii")
    header.engine_sha256 = MODULES["engine.dll"].encode("ascii")
    header.scenario = scenario.encode("ascii")
    return header


def _record(
    ordinal: int,
    scenario: str,
    boundary: int,
    phase: int = EXIT,
    frame: int = 1,
) -> Record:
    record = Record()
    record.magic = b"VMR1"
    record.record_bytes = ctypes.sizeof(Record)
    record.ordinal = ordinal
    record.qpc = 100 + ordinal
    record.thread_id = 7
    record.frame = frame
    record.boundary_id = boundary
    record.call_phase = phase
    record.server_handle = 0xFFFFFFFF
    record.server_owner_handle = 0xFFFFFFFF
    record.client_handle = 0xFFFFFFFF
    record.client_weapon_handle = 0xFFFFFFFF
    record.viewmodel_index = -1
    record.server_sequence = -1
    record.client_sequence = -1
    record.event_id = -1
    record.ammo_before = -1
    record.ammo_after = -1
    record.root_bone_index = 0xFFFFFFFF
    record.alignment_bone_index = 0xFFFFFFFF
    record.scenario = scenario.encode("ascii")
    return record


def _visual_records(scenario: str, *, projection_fov: float = 54.0) -> list[Record]:
    player_fov = 75.0
    viewmodel_fov = 54.0
    aspect = 16.0 / 9.0
    if scenario.startswith("projection-"):
        _, player, viewmodel, aspect_label = scenario.split("-")
        player_fov = float(player[1:])
        viewmodel_fov = float(viewmodel[1:])
        aspect = 4.0 / 3.0 if aspect_label == "4x3" else 16.0 / 9.0
        if projection_fov == 54.0:
            projection_fov = viewmodel_fov
    records: list[Record] = []
    draw = _record(1, scenario, BOUNDARY["draw"], ENTRY)
    draw.player_fov = player_fov
    draw.viewmodel_fov = viewmodel_fov
    draw.near_plane = 1.0
    draw.far_plane = 28400.0
    draw.viewport_width = 1600
    draw.viewport_height = 900
    records.append(draw)

    projection = _record(2, scenario, BOUNDARY["projection"])
    projection.player_fov = player_fov
    projection.viewmodel_fov = projection_fov
    projection.near_plane = 1.0
    projection.far_plane = 28400.0
    projection.aspect = aspect
    projection.tan_half_fov = math.tan(math.radians(projection_fov) / 2.0)
    records.append(projection)

    hands = _record(3, scenario, BOUNDARY["setup_bones"])
    hands.client_entity = 0x1000
    hands.client_handle = 10
    hands.client_weapon_handle = 50
    hands.root_bone_index = 0
    hands.alignment_bone_index = 20
    hands.bone_count = 42
    hands.hands_model = b"models/hands/male/tremere/v_tremere_male_hands.mdl"
    records.append(hands)

    weapon = _record(4, scenario, BOUNDARY["setup_bones"])
    weapon.client_entity = 0x2000
    weapon.client_handle = 11
    weapon.client_weapon_handle = 50
    weapon.root_bone_index = 0
    weapon.alignment_bone_index = 20
    weapon.bone_count = 42
    weapon.hands_model = hands.hands_model
    weapon.weapon_model = b"models/weapons/glock/view/v_glock.mdl"
    records.append(weapon)

    server = _record(5, scenario, BOUNDARY["server_update"])
    server.server_handle = 50
    records.append(server)
    return records


def _action_stream(
    outside_shot: bool = False,
    scenario: str = "tremere-normal-glock",
) -> Stream:
    records = _visual_records(scenario)
    ordinal = 10

    def add(boundary: int, phase: int, ammo_before: int = -1, ammo_after: int = -1) -> None:
        nonlocal ordinal
        record = _record(ordinal, scenario, boundary, phase)
        record.server_entity = 0x3000
        record.ammo_before = ammo_before
        record.ammo_after = ammo_after
        records.append(record)
        ordinal += 1

    if outside_shot:
        add(BOUNDARY["server_shot"], ENTRY, 10)
        add(BOUNDARY["server_shot"], EXIT, 10, 9)
    add(BOUNDARY["server_event"], ENTRY, 10)
    if not outside_shot:
        add(BOUNDARY["server_shot"], ENTRY, 10)
        add(BOUNDARY["server_shot"], EXIT, 10, 9)
    add(BOUNDARY["server_event"], EXIT, 10, 9)
    add(BOUNDARY["client_event"], ENTRY)
    add(BOUNDARY["client_event"], EXIT)
    add(BOUNDARY["server_dry"], ENTRY, 0)
    add(BOUNDARY["server_dry"], EXIT, 0, 0)
    for boundary in (
        BOUNDARY["reload_request"],
        BOUNDARY["reload_frame"],
        BOUNDARY["reload_finish"],
    ):
        add(boundary, ENTRY, 0)
        add(boundary, EXIT, 0, 10)
        if boundary == BOUNDARY["reload_frame"] and scenario.endswith("m37"):
            add(boundary, ENTRY, 1)
            add(boundary, EXIT, 1, 2)
            add(boundary, ENTRY, 2)
            add(boundary, EXIT, 2, 3)
    return Stream(Path("synthetic.elgvm"), _header(scenario), tuple(records))


class FirstPersonViewmodelCaptureTests(unittest.TestCase):
    def test_complete_synthetic_scenario_matrix_passes(self) -> None:
        streams = [
            _action_stream(scenario=scenario)
            for scenario in ACTION_SCENARIOS
        ]
        streams.extend(
            Stream(
                Path(f"{scenario}.elgvm"),
                _header(scenario),
                tuple(_visual_records(scenario)),
            )
            for scenario in PROJECTION_SCENARIOS
        )
        scenario = "drawviewmodel-off"
        records = _visual_records(scenario)
        hidden = _record(20, scenario, BOUNDARY["draw"], ENTRY, frame=2)
        hidden.draw_policy = 0
        records.append(hidden)
        streams.append(
            Stream(Path(f"{scenario}.elgvm"), _header(scenario), tuple(records))
        )
        report = verify_streams(streams)
        self.assertEqual(report["state"], "complete")

    def test_synthetic_projection_and_component_join_pass(self) -> None:
        scenario = "projection-p75-v54-16x9"
        stream = Stream(
            Path("synthetic.elgvm"),
            _header(scenario),
            tuple(_visual_records(scenario)),
        )
        report = verify_streams([stream], allow_partial=True)
        self.assertEqual(report["state"], "complete")

    def test_synthetic_shot_reload_authority_pass(self) -> None:
        report = verify_streams([_action_stream()], allow_partial=True)
        self.assertEqual(report["state"], "complete")

    def test_shot_outside_server_event_is_rejected(self) -> None:
        with self.assertRaises(VerificationError):
            verify_streams([_action_stream(outside_shot=True)], allow_partial=True)

    def test_wrong_projection_fov_is_rejected(self) -> None:
        scenario = "projection-p75-v54-16x9"
        stream = Stream(
            Path("synthetic.elgvm"),
            _header(scenario),
            tuple(_visual_records(scenario, projection_fov=68.0)),
        )
        with self.assertRaises(VerificationError):
            verify_streams([stream], allow_partial=True)

    def test_truncated_and_malformed_records_are_rejected(self) -> None:
        scenario = "projection-p75-v54-16x9"
        records = _visual_records(scenario)
        payload = bytes(_header(scenario)) + b"".join(bytes(record) for record in records)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            truncated = root / "truncated.elgvm"
            truncated.write_bytes(payload[:-1])
            with self.assertRaisesRegex(VerificationError, "truncated"):
                read_stream(truncated)
            malformed = root / "malformed.elgvm"
            damaged = bytearray(payload)
            damaged[ctypes.sizeof(FileHeader)] = ord("X")
            malformed.write_bytes(damaged)
            with self.assertRaisesRegex(VerificationError, "malformed"):
                read_stream(malformed)


if __name__ == "__main__":
    unittest.main()
