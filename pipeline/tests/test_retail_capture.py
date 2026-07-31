from __future__ import annotations

import json
from pathlib import Path
import unittest

REPO_ROOT = Path(__file__).resolve().parents[2]
NATIVE_ROOT = REPO_ROOT / "research" / "tooling" / "capture" / "native"


class RetailCaptureTests(unittest.TestCase):
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
