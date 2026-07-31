from __future__ import annotations

import json
from pathlib import Path
import struct
import tempfile
import unittest

from research.tooling.capture.inventory_player_animations import (
    parse_owner,
    player_slots,
)
from research.tooling.capture.capture_player_sequence import (
    ARM_AFTER_TARGET_SECONDS,
    MAX_PRE_SEQUENCE_FRAMES,
    MOVEMENT_SETTLE_WAITS,
    POST_SEQUENCE_WAITS,
    PRE_THIRD_PERSON_WAITS,
    REPEAT_GAP_WAITS,
    SEQUENCE_REPETITIONS,
    TARGET_READY_WAITS,
    assess_seed,
    build_config,
    read_load_command,
)

REPO_ROOT = Path(__file__).resolve().parents[2]
NATIVE_ROOT = REPO_ROOT / "research" / "tooling" / "capture" / "native"


class RetailCaptureTests(unittest.TestCase):
    def test_player_animation_inventory_preserves_duplicate_sequences_and_grid(self) -> None:
        data = bytearray(4096)
        data[:4] = b"IDST"
        struct.pack_into("<i", data, 4, 2531)
        struct.pack_into("<i", data, 240, 1)
        struct.pack_into("<ii", data, 264, 2, 2048)
        struct.pack_into("<ii", data, 272, 2, 512)
        for index in range(2):
            base = 512 + index * 764
            label = 3600 + index * 32
            activity = label + 12
            struct.pack_into("<i", data, base, label - base)
            struct.pack_into("<i", data, base + 4, activity - base)
            struct.pack_into("<i", data, base + 12, -1)
            struct.pack_into("<i", data, base + 16, 1)
            struct.pack_into("<i", data, base + 52, 2)
            struct.pack_into("<h", data, base + 56, index)
            struct.pack_into("<h", data, base + 56 + 16 * 2, 1 - index)
            struct.pack_into("<2i", data, base + 572, 2, 1)
            data[label : label + 10] = b"duplicate\0"
            data[activity : activity + 9] = b"ACT_TEST\0"
        for index in range(2):
            base = 2048 + index * 72
            name = 3800 + index * 24
            struct.pack_into("<i", data, base, name - base)
            struct.pack_into("<f", data, base + 4, 30.0)
            struct.pack_into("<i", data, base + 12, 81)
            struct.pack_into("<i", data, base + 48, 512)
            data[name : name + 7] = f"@anim{index}".encode() + b"\0"

        parsed = parse_owner("models/test.mdl", bytes(data))
        self.assertEqual(
            [sequence["label"] for sequence in parsed["sequences"]],
            ["duplicate", "duplicate"],
        )
        self.assertEqual(
            parsed["sequences"][0]["blend_grid"][1][0],
            1,
        )
        self.assertEqual(
            [cell["animation_index"] for cell in parsed["sequences"][0]["active_blend_cells"]],
            [0, 1],
        )
        self.assertEqual(len(parsed["sequences"][0]["descriptor_hex"]), 764 * 2)

    def test_player_animation_inventory_reads_only_indexed_body_slots(self) -> None:
        slots = player_slots(
            b'''ClanDataTables
            {
                ClanData
                {
                    General
                    {
                        Clan "Tremere"
                        M_Body "models/npc/not_player.mdl"
                        M_Body0 "models/character/pc/male/test.mdl"
                        M_Body1 "models/character/pc/male/test.mdl"
                        F_Body0 "models/character/pc/female/test.mdl"
                    }
                }
            }'''
        )
        self.assertEqual(len(slots), 3)
        self.assertEqual({slot["model"] for slot in slots}, {
            "models/character/pc/male/test.mdl",
            "models/character/pc/female/test.mdl",
        })

    def test_player_sequence_seed_recipe_orders_reset_arm_and_trigger(self) -> None:
        rendered = build_config("load Vampire-002", "howl")
        lines = rendered.splitlines()
        forward = lines.index("+forward")
        release = lines.index("-forward")
        triggers = [
            index
            for index, line in enumerate(lines)
            if line == "player_sequence howl"
        ]
        self.assertLess(lines.index("thirdperson"), forward)
        self.assertEqual(lines[forward + 1], "wait")
        self.assertEqual(release, forward + 2)
        self.assertEqual(len(triggers), SEQUENCE_REPETITIONS)
        self.assertEqual(lines[triggers[0] - 1], "echo ELYSIUM_CAP11_ARM")
        self.assertEqual(lines[triggers[1] - 1], "wait")
        between_triggers = lines[triggers[0] + 1 : triggers[1]]
        self.assertEqual(between_triggers[0], "echo ELYSIUM_CAP11_TRIGGERED")
        self.assertEqual(
            between_triggers.count("wait"),
            81 + REPEAT_GAP_WAITS,
        )
        self.assertNotIn("host_timescale 0", lines)
        self.assertFalse(any(line.startswith("writeconfig") for line in lines))
        self.assertEqual(ARM_AFTER_TARGET_SECONDS, 0.0)
        self.assertEqual(
            MAX_PRE_SEQUENCE_FRAMES,
            TARGET_READY_WAITS + 1 + MOVEMENT_SETTLE_WAITS,
        )
        self.assertEqual(
            lines.count("wait"),
            PRE_THIRD_PERSON_WAITS
            + TARGET_READY_WAITS
            + 1
            + MOVEMENT_SETTLE_WAITS
            + 81
            + REPEAT_GAP_WAITS
            + POST_SEQUENCE_WAITS,
        )

    def test_player_sequence_seed_reads_one_active_load_command(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "elysium_load.cfg"
            path.write_text(
                "// owner save\n\nload Vampire-002\n",
                encoding="utf-8",
            )
            self.assertEqual(read_load_command(path), "load Vampire-002")

    def test_player_sequence_seed_acceptance_requires_long_match(self) -> None:
        capture = {
            "complete": True,
            "records": {"dropped": 0, "incomplete": 0},
        }
        clean_pairs = [[live, live + 1] for live in range(8, 78)]
        clean = assess_seed(
            capture,
            {
                "clip": {"frames": 81},
                "live_to_authored_alignment": {"aligned_pairs": clean_pairs},
            },
        )
        self.assertTrue(clean["passed"])
        interrupted = assess_seed(
            capture,
            {
                "clip": {"frames": 81},
                "live_to_authored_alignment": {
                    "aligned_pairs": [[live, live - 1] for live in range(6, 16)]
                },
            },
        )
        self.assertFalse(interrupted["passed"])
        self.assertFalse(interrupted["checks"]["contiguous_authored_run"])

    def test_native_capture_project_has_explicit_win32_presets(self) -> None:
        presets = json.loads(
            (NATIVE_ROOT / "CMakePresets.json").read_text(encoding="utf-8")
        )
        configure = {
            preset["name"]: preset for preset in presets["configurePresets"]
        }
        for name, configuration in (
            ("win32-debug", "Debug"),
            ("win32-release", "Release"),
        ):
            self.assertEqual(
                configure[name]["cacheVariables"]["CMAKE_BUILD_TYPE"],
                configuration,
            )
            self.assertEqual(configure[name]["inherits"], "win32-base")
        base = configure["win32-base"]
        self.assertEqual(base["generator"], "Ninja")
        self.assertEqual(
            base["cacheVariables"]["ELYSIUM_TARGET_ARCH"], "x86"
        )
        self.assertIn("ELYSIUM_NATIVE_BUILD_ROOT", base["binaryDir"])

        cmake = (NATIVE_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("CMAKE_MSVC_RUNTIME_LIBRARY", cmake)
        self.assertIn("/W4", cmake)
        self.assertIn("/WX", cmake)
        self.assertIn("CMAKE_SIZEOF_VOID_P EQUAL 4", cmake)

    def test_synthetic_retail_contract_names_modules_and_hook_targets(self) -> None:
        cmake = (NATIVE_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        target = (NATIVE_ROOT / "synthetic_retail.cpp").read_text(
            encoding="utf-8"
        )
        module = (NATIVE_ROOT / "synthetic_module.cpp").read_text(
            encoding="utf-8"
        )
        for name in ("client.dll", "engine.dll", "StudioRender.dll"):
            self.assertIn(name, cmake)
            self.assertIn(name, target)
        self.assertIn("ElysiumSyntheticHookTarget", target)
        self.assertIn("ElysiumSyntheticHookTarget", module)
        self.assertIn("synthetic_retail_lifecycle", cmake)
        self.assertIn("event=shutdown_complete modules=3", cmake)

    def test_native_launcher_creates_and_verifies_a_suspended_process(self) -> None:
        cmake = (NATIVE_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        launcher = (NATIVE_ROOT / "retail_launcher.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("retail_launcher.cpp", cmake)
        self.assertIn("retail_supervision.cpp", cmake)
        self.assertIn("synthetic_suspended_launch", cmake)
        self.assertIn("CREATE_SUSPENDED", launcher)
        self.assertIn("CREATE_UNICODE_ENVIRONMENT", launcher)
        self.assertIn("CreateProcessW(", launcher)
        self.assertIn("SuspendThread(thread.Get())", launcher)
        self.assertIn("previousSuspendCount != 1", launcher)
        self.assertIn("BuildEnvironment(", launcher)
        self.assertIn("QuoteArgument(", launcher)
        self.assertIn("Unofficial_Patch", launcher)
        public_driver = (
            REPO_ROOT
            / "research"
            / "tooling"
            / "capture"
            / "retail_capture_launch.py"
        ).read_text(encoding="utf-8")
        self.assertIn('run_native("build"', public_driver)
        self.assertIn('"--inject-and-terminate"', public_driver)
        self.assertIn('"retail_probe_host.dll"', public_driver)
        self.assertIn('"--startup-profile"', public_driver)
        self.assertIn('target_arguments[:1] == ["--"]', public_driver)
        self.assertIn('"--target-argument"', public_driver)

    def test_bootstrap_injection_uses_a_versioned_loadlibrary_handshake(self) -> None:
        cmake = (NATIVE_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        launcher = (NATIVE_ROOT / "retail_launcher.cpp").read_text(
            encoding="utf-8"
        )
        host = (NATIVE_ROOT / "retail_probe_host.cpp").read_text(
            encoding="utf-8"
        )
        observer = (NATIVE_ROOT / "module_observer.cpp").read_text(
            encoding="utf-8"
        )
        contract = (NATIVE_ROOT / "bootstrap_contract.h").read_text(
            encoding="utf-8"
        )
        self.assertIn("add_library(retail_probe_host SHARED", cmake)
        self.assertIn("synthetic_bootstrap_termination", cmake)
        self.assertIn('"LoadLibraryW"', launcher)
        self.assertIn("CreateRemoteThread(", launcher)
        self.assertNotIn("manual map", launcher.lower())
        self.assertIn("BootstrapVersion = 5", contract)
        self.assertIn("ModuleObserverArmed", contract)
        self.assertIn("TransportArmed", contract)
        self.assertIn('"LdrRegisterDllNotification"', observer)
        self.assertIn("BootstrapState::Ready", host)
        self.assertIn("bootstrap->WaitReady", launcher)
        self.assertLess(
            launcher.index("bootstrap->WaitReady"),
            launcher.rindex("ResumeThread(thread.Get())"),
        )

    def test_hook_declarations_select_shared_instruction_aware_backends(self) -> None:
        capture_root = REPO_ROOT / "research" / "tooling" / "capture"
        registry = json.loads(
            (capture_root / "contracts" / "binary_profiles.json").read_text(
                encoding="utf-8"
            )
        )
        targets = {
            target["semantic_label"]: target
            for profile in registry["profiles"]
            for target in profile["targets"]
        }
        self.assertEqual(
            targets["client.resolve_virtual_model_pose"]["backend"],
            "inline_detour",
        )
        self.assertEqual(
            targets["studiorender.draw_model"]["backend"],
            "vtable_replacement",
        )
        self.assertEqual(
            targets["client.get_studio_hdr"]["backend"], "none"
        )
        backend = (NATIVE_ROOT / "hook_backend.cpp").read_text(
            encoding="utf-8"
        )
        retained_probe = (capture_root / "live_pose_hook.cpp").read_text(
            encoding="utf-8"
        )
        cmake = (NATIVE_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("DecodeInstruction(", backend)
        self.assertIn("RelativeDestination(", backend)
        self.assertIn("RelativeKind::ShortCondition", backend)
        self.assertIn("hook_backends_instruction_aware", cmake)
        self.assertIn("HookBackends::Install(", retained_probe)
        self.assertNotIn("VirtualProtect(", retained_probe)
        self.assertNotIn("trampoline", retained_probe.lower())

    def test_supervision_owns_children_and_finalizes_partial_captures(self) -> None:
        cmake = (NATIVE_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        launcher = (NATIVE_ROOT / "retail_launcher.cpp").read_text(
            encoding="utf-8"
        )
        supervision = (NATIVE_ROOT / "retail_supervision.cpp").read_text(
            encoding="utf-8"
        )
        for name in (
            "synthetic_supervision_normal_exit",
            "synthetic_supervision_timeout",
            "synthetic_supervision_crash",
            "synthetic_supervision_collector_exit",
        ):
            self.assertIn(name, cmake)
        self.assertIn("RunSupervision(request)", launcher)
        self.assertIn("JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE", supervision)
        self.assertIn("SetConsoleCtrlHandler(", supervision)
        self.assertIn("CTRL_C_EVENT", supervision)
        self.assertNotIn("GenerateConsoleCtrlEvent", supervision)
        self.assertIn("MoveFileExW(", supervision)
        self.assertIn('"state=%s\\n"', supervision)
        for reason in ("process-crash", "collector-exit", "timeout", "ctrl-c"):
            self.assertIn(f'"{reason}"', supervision)

    def test_attach_fallback_is_explicit_and_non_owning(self) -> None:
        launcher = (NATIVE_ROOT / "retail_launcher.cpp").read_text(
            encoding="utf-8"
        )
        cmake = (NATIVE_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        attach = (
            REPO_ROOT
            / "research"
            / "tooling"
            / "capture"
            / "retail_capture_attach.py"
        ).read_text(encoding="utf-8")
        self.assertIn("--attach-pid", launcher)
        self.assertIn("OpenProcess(", launcher)
        self.assertIn("mode=attached", launcher)
        self.assertIn("mode=launched", launcher)
        self.assertIn("synthetic_attach_fallback", cmake)
        self.assertIn("retail_capture_launch", attach)
        self.assertNotIn("TerminateProcess", attach)

    def test_lifecycle_soak_runs_one_hundred_complete_capture_cycles(self) -> None:
        cmake = (NATIVE_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        soak = (NATIVE_ROOT / "verify_lifecycle_soak.ps1").read_text(
            encoding="utf-8"
        )
        collector = (NATIVE_ROOT / "synthetic_collector.cpp").read_text(
            encoding="utf-8"
        )
        contract = (
            NATIVE_ROOT / "synthetic_capture_contract.h"
        ).read_text(encoding="utf-8")
        self.assertIn("synthetic_lifecycle_soak_100", cmake)
        self.assertIn("-Cycles 100", cmake)
        self.assertIn("Get-SelfHandleCount", soak)
        self.assertIn("Assert-ProcessExited", soak)
        self.assertIn("event=module_unloaded", soak)
        self.assertIn("SyntheticTraceContract", collector)
        self.assertIn("elysium.synthetic-capture-trace", contract)
        self.assertIn("MOVEFILE_WRITE_THROUGH", collector)


if __name__ == "__main__":
    unittest.main()
