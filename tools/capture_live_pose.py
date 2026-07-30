"""Non-invasive retail VtMB skeletal-palette recorder.

This reads the CStudioRender-owned buffers with ReadProcessMemory. It never
attaches a debugger, suspends a thread, or writes to the game process.
"""

from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import sys
import time

import install
import mdl_skel


PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010
TH32CS_SNAPPROCESS = 0x00000002
TH32CS_SNAPMODULE = 0x00000008
TH32CS_SNAPMODULE32 = 0x00000010
MAX_PATH = 260
MAX_MODULE_NAME32 = 255

STUDIO_RENDER_OBJECT_RVA = 0x82B30
STUDIO_RENDER_VTABLE_RVA = 0x6C150
EXPECTED_STUDIO_RENDER_SHA256 = (
    "13d56ce90de2c5faedc0df26d36b24e90f30eded5055625f616cd97e301e124b"
)
DEFAULT_MODEL = (
    "models/character/pc/male/tremere/armor0/tremere_Male_Armor_0.mdl"
)


class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [
        ("dwSize", wintypes.DWORD),
        ("cntUsage", wintypes.DWORD),
        ("th32ProcessID", wintypes.DWORD),
        ("th32DefaultHeapID", ctypes.c_void_p),
        ("th32ModuleID", wintypes.DWORD),
        ("cntThreads", wintypes.DWORD),
        ("th32ParentProcessID", wintypes.DWORD),
        ("pcPriClassBase", wintypes.LONG),
        ("dwFlags", wintypes.DWORD),
        ("szExeFile", wintypes.WCHAR * MAX_PATH),
    ]


class MODULEENTRY32W(ctypes.Structure):
    _fields_ = [
        ("dwSize", wintypes.DWORD),
        ("th32ModuleID", wintypes.DWORD),
        ("th32ProcessID", wintypes.DWORD),
        ("GlblcntUsage", wintypes.DWORD),
        ("ProccntUsage", wintypes.DWORD),
        ("modBaseAddr", ctypes.POINTER(ctypes.c_byte)),
        ("modBaseSize", wintypes.DWORD),
        ("hModule", wintypes.HMODULE),
        ("szModule", wintypes.WCHAR * (MAX_MODULE_NAME32 + 1)),
        ("szExePath", wintypes.WCHAR * MAX_PATH),
    ]


kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
kernel32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
kernel32.OpenProcess.restype = wintypes.HANDLE
kernel32.ReadProcessMemory.argtypes = [
    wintypes.HANDLE,
    ctypes.c_void_p,
    ctypes.c_void_p,
    ctypes.c_size_t,
    ctypes.POINTER(ctypes.c_size_t),
]


def _close(handle: wintypes.HANDLE) -> None:
    if handle:
        kernel32.CloseHandle(handle)


def find_process(name: str) -> int:
    snapshot = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    if snapshot == wintypes.HANDLE(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        entry = PROCESSENTRY32W()
        entry.dwSize = ctypes.sizeof(entry)
        matches: list[int] = []
        if kernel32.Process32FirstW(snapshot, ctypes.byref(entry)):
            while True:
                if entry.szExeFile.lower() == name.lower():
                    matches.append(entry.th32ProcessID)
                if not kernel32.Process32NextW(snapshot, ctypes.byref(entry)):
                    break
        if not matches:
            raise RuntimeError(f"{name} is not running")
        return max(matches)
    finally:
        _close(snapshot)


def find_module(pid: int, name: str) -> tuple[int, int, Path]:
    flags = TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32
    snapshot = kernel32.CreateToolhelp32Snapshot(flags, pid)
    if snapshot == wintypes.HANDLE(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        entry = MODULEENTRY32W()
        entry.dwSize = ctypes.sizeof(entry)
        if kernel32.Module32FirstW(snapshot, ctypes.byref(entry)):
            while True:
                if entry.szModule.lower() == name.lower():
                    base = ctypes.cast(entry.modBaseAddr, ctypes.c_void_p).value
                    if base is None:
                        raise RuntimeError(f"{name} has no module base")
                    return base, entry.modBaseSize, Path(entry.szExePath)
                if not kernel32.Module32NextW(snapshot, ctypes.byref(entry)):
                    break
        raise RuntimeError(f"{name} is not loaded in PID {pid}")
    finally:
        _close(snapshot)


class ProcessReader:
    def __init__(self, pid: int):
        self.pid = pid
        self.handle = kernel32.OpenProcess(
            PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, pid
        )
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())

    def close(self) -> None:
        _close(self.handle)
        self.handle = None

    def read(self, address: int, size: int) -> bytes:
        buffer = ctypes.create_string_buffer(size)
        actual = ctypes.c_size_t()
        ok = kernel32.ReadProcessMemory(
            self.handle,
            ctypes.c_void_p(address),
            buffer,
            size,
            ctypes.byref(actual),
        )
        if not ok or actual.value != size:
            raise ctypes.WinError(ctypes.get_last_error())
        return buffer.raw

    def u32(self, address: int) -> int:
        return struct.unpack("<I", self.read(address, 4))[0]

    def cstring(self, address: int, maximum: int = 260) -> str:
        return self.read(address, maximum).split(b"\0", 1)[0].decode(
            "ascii", "replace"
        )


def matrices_are_finite(data: bytes) -> bool:
    values = struct.unpack(f"<{len(data) // 4}f", data)
    return all(math.isfinite(value) and abs(value) < 1.0e7 for value in values)


def stable_target_snapshot(
    reader: ProcessReader,
    studio_object: int,
    target_checksum: int,
    target_bones: int,
    matrix_bytes: int,
) -> tuple[bytes, bytes] | None:
    header_before = reader.u32(studio_object + 0x68)
    if not header_before:
        return None
    if reader.u32(header_before + 0x08) != target_checksum:
        return None
    if reader.u32(header_before + 0xF0) != target_bones:
        return None

    bone_pointer = reader.u32(studio_object + 0x5C)
    skin_pointer = reader.u32(studio_object + 0x60)
    if not bone_pointer or not skin_pointer:
        return None

    bones_a = reader.read(bone_pointer, matrix_bytes)
    skin_a = reader.read(skin_pointer, matrix_bytes)
    bones_b = reader.read(bone_pointer, matrix_bytes)
    skin_b = reader.read(skin_pointer, matrix_bytes)
    header_after = reader.u32(studio_object + 0x68)

    if header_before != header_after or bones_a != bones_b or skin_a != skin_b:
        return None
    if not matrices_are_finite(bones_a) or not matrices_are_finite(skin_a):
        return None
    return bones_a, skin_a


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pid", type=int)
    parser.add_argument(
        "--model",
        default=DEFAULT_MODEL,
        help="Patch-first models/... MDL key whose rendered palettes to capture.",
    )
    parser.add_argument(
        "--label",
        help="Filesystem-safe session label (defaults to the model stem).",
    )
    parser.add_argument("--frames", type=int, default=81)
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument(
        "--allow-partial",
        action="store_true",
        help=(
            "Write a successful session when timeout expires before every "
            "requested unique palette is observed."
        ),
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).resolve().parent / "out" / "_live_pose",
    )
    parser.add_argument(
        "--arm-file",
        type=Path,
        help="Marker file; defaults to <output>/ARM_LIVE_POSE.",
    )
    parser.add_argument(
        "--log-file",
        type=Path,
        help="Write line-buffered stdout/stderr to this file for detached capture.",
    )
    args = parser.parse_args()

    if args.log_file:
        args.log_file.parent.mkdir(parents=True, exist_ok=True)
        log_stream = args.log_file.open("w", encoding="utf-8", buffering=1)
        sys.stdout = log_stream
        sys.stderr = log_stream

    if args.frames < 1:
        parser.error("--frames must be positive")

    model_key = args.model.replace("\\", "/")
    if not model_key.lower().startswith("models/"):
        model_key = "models/" + model_key
    if not model_key.lower().endswith(".mdl"):
        model_key += ".mdl"
    install_index = install.build_index(verbose=False)
    model_data = install.read(install_index, model_key)
    if model_data is None:
        raise FileNotFoundError(f"{model_key} is not present in the merged install")
    target_checksum = struct.unpack_from("<I", model_data, 8)[0]
    target_bones = len(mdl_skel.read_bones(model_data))
    matrix_bytes = target_bones * 12 * 4
    studio_model_name = model_key[7:] if model_key.lower().startswith("models/") else model_key

    pid = args.pid or find_process("vampire.exe")
    module_base, _, module_path = find_module(pid, "studiorender.dll")
    module_hash = hashlib.sha256(module_path.read_bytes()).hexdigest()
    if module_hash != EXPECTED_STUDIO_RENDER_SHA256:
        raise RuntimeError(
            f"unexpected StudioRender.dll SHA-256: {module_hash}"
        )

    output_root = args.output.resolve()
    output_root.mkdir(parents=True, exist_ok=True)
    arm_file = (
        args.arm_file.resolve()
        if args.arm_file
        else output_root / "ARM_LIVE_POSE"
    )
    if arm_file.exists():
        arm_file.unlink()

    raw_label = args.label or Path(model_key).stem
    session_label = re.sub(r"[^a-zA-Z0-9_.-]+", "_", raw_label).strip("_.-")
    if not session_label:
        parser.error("--label contains no filesystem-safe characters")
    session_stamp = time.strftime("%Y%m%d_%H%M%S")
    session = output_root / f"{session_label}_{session_stamp}"
    session.mkdir(parents=True, exist_ok=False)

    reader = ProcessReader(pid)
    try:
        studio_object = module_base + STUDIO_RENDER_OBJECT_RVA
        expected_vtable = module_base + STUDIO_RENDER_VTABLE_RVA
        actual_vtable = reader.u32(studio_object)
        if actual_vtable != expected_vtable:
            raise RuntimeError(
                f"unexpected CStudioRender vtable: 0x{actual_vtable:08x}"
            )

        print(f"PID={pid}")
        print(f"StudioRender=0x{module_base:08x}")
        print(f"CStudioRender=0x{studio_object:08x}")
        print(f"Target={studio_model_name}")
        print(f"Checksum=0x{target_checksum:08x}")
        print(f"Bones={target_bones}")
        print(f"ArmFile={arm_file}")
        print("Waiting for the target and arm marker...", flush=True)

        last_prearm_hash: str | None = None
        pending_snapshot: tuple[bytes, bytes] | None = None
        target_seen = False
        arm_seen = False
        arm_detected_at: float | None = None
        wait_deadline = time.perf_counter() + args.timeout
        while time.perf_counter() < wait_deadline:
            snapshot = stable_target_snapshot(
                reader,
                studio_object,
                target_checksum,
                target_bones,
                matrix_bytes,
            )
            if snapshot is not None:
                if not target_seen:
                    target_seen = True
                    print("Target visible.", flush=True)
                if arm_file.exists():
                    pending_snapshot = snapshot
                    break
                last_prearm_hash = hashlib.sha256(
                    snapshot[0] + snapshot[1]
                ).hexdigest()
            if arm_file.exists() and not arm_seen:
                arm_seen = True
                arm_detected_at = time.perf_counter()
                print(
                    "Arm marker detected; waiting for the target to render...",
                    flush=True,
                )
        if pending_snapshot is None:
            missing = []
            if not target_seen:
                missing.append("target model")
            if not arm_file.exists():
                missing.append("arm marker")
            raise TimeoutError(
                f"{' and '.join(missing) or 'capture prerequisites'} "
                "not observed before timeout"
            )

        capture_started_at = time.perf_counter()
        armed_at = arm_detected_at or capture_started_at
        deadline = capture_started_at + args.timeout
        frames: list[dict[str, object]] = []
        payloads: list[tuple[bytes, bytes]] = []
        last_hash = last_prearm_hash
        print("ARMED. Waiting for the first post-trigger palette...", flush=True)

        while len(frames) < args.frames and time.perf_counter() < deadline:
            snapshot = pending_snapshot
            pending_snapshot = None
            if snapshot is None:
                snapshot = stable_target_snapshot(
                    reader,
                    studio_object,
                    target_checksum,
                    target_bones,
                    matrix_bytes,
                )
            if snapshot is None:
                continue
            bones, skin = snapshot
            combined_hash = hashlib.sha256(bones + skin).hexdigest()
            if combined_hash == last_hash:
                continue

            index = len(frames)
            stem = f"frame_{index:03d}"
            bone_name = f"{stem}_bone_to_world.bin"
            skin_name = f"{stem}_skin_palette.bin"
            elapsed = time.perf_counter() - armed_at
            frames.append(
                {
                    "index": index,
                    "seconds_after_arm": elapsed,
                    "bone_to_world": bone_name,
                    "skin_palette": skin_name,
                    "bone_to_world_sha256": hashlib.sha256(bones).hexdigest(),
                    "skin_palette_sha256": hashlib.sha256(skin).hexdigest(),
                    "combined_sha256": combined_hash,
                }
            )
            payloads.append((bones, skin))
            last_hash = combined_hash
            print(
                f"Captured {index + 1:02d}/{args.frames} "
                f"at +{elapsed:.6f}s",
                flush=True,
            )

        # Keep disk I/O out of the sampling loop: the renderer exposes the target
        # header only briefly, and a synchronous write can make the next rendered
        # palette unobservable even at 30 FPS.
        for frame, (bones, skin) in zip(frames, payloads):
            (session / frame["bone_to_world"]).write_bytes(bones)
            (session / frame["skin_palette"]).write_bytes(skin)

        manifest = {
            "version": 1,
            "method": "ReadProcessMemory",
            "pid": pid,
            "model": studio_model_name,
            "model_key": model_key,
            "model_checksum": f"0x{target_checksum:08x}",
            "bone_count": target_bones,
            "matrix_layout": "row-major matrix3x4, 12 little-endian float32",
            "matrix_bytes_per_palette": matrix_bytes,
            "studio_render_sha256": module_hash,
            "requested_frames": args.frames,
            "captured_frames": len(frames),
            "complete": len(frames) == args.frames,
            "allow_partial": args.allow_partial,
            "frames": frames,
        }
        (session / "manifest.json").write_text(
            json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
        )
        print(f"Manifest={session / 'manifest.json'}", flush=True)

        if not frames:
            raise TimeoutError("captured no post-trigger palettes")
        if len(frames) != args.frames and not args.allow_partial:
            raise TimeoutError(
                f"captured {len(frames)}/{args.frames} unique palettes"
            )
        if len(frames) != args.frames:
            print(
                f"Partial capture accepted: {len(frames)}/{args.frames} "
                "unique palettes.",
                flush=True,
            )
        return 0
    finally:
        reader.close()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr, flush=True)
        raise
