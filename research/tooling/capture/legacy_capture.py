"""Strict ELPOSE2/ELANIM2 adapters for the unified capture-reader interface."""

from __future__ import annotations

from dataclasses import dataclass
import json
from pathlib import Path
from typing import BinaryIO, Iterator

from research.tooling.capture.generated_record_schemas import (
    ANIMATION_FILE_HEADER,
    ANIMATION_RECORD_HEADER,
    POSE_FILE_HEADER,
    POSE_RECORD_HEADER,
    RECORDS,
)


ROOT = Path(__file__).resolve().parent
GOLDEN_METADATA = ROOT / "contracts" / "legacy_golden_metadata.json"


@dataclass(frozen=True)
class CaptureRecord:
    record_id: int
    schema_version: int
    magic: str
    ordinal: int
    qpc: int
    header: tuple[object, ...]
    payload: bytes


@dataclass(frozen=True)
class CaptureHeader:
    format: str
    record_id: int
    schema_version: int
    values: tuple[object, ...]


class LegacyCaptureReader:
    """Convert one retained legacy stream to stable typed record IDs."""

    def __init__(self, path: Path):
        self.path = path.resolve()
        self._stream: BinaryIO | None = None
        self.header: CaptureHeader | None = None

    def __enter__(self) -> "LegacyCaptureReader":
        self._stream = self.path.open("rb")
        prefix = self._stream.read(8)
        self._stream.seek(0)
        magic = prefix.rstrip(b"\0")
        if magic == b"ELPOSE2":
            values = self._read_complete(POSE_FILE_HEADER.size, "ELPOSE2 header")
            unpacked = POSE_FILE_HEADER.unpack(values)
            if unpacked[1] != 2 or unpacked[2] != POSE_FILE_HEADER.size:
                raise ValueError("ELPOSE2 header version/size mismatch")
            self.header = CaptureHeader(
                "ELPOSE2",
                RECORDS["pose_file_header"]["id"],
                RECORDS["pose_file_header"]["schema_version"],
                unpacked,
            )
        elif magic == b"ELANIM2":
            values = self._read_complete(ANIMATION_FILE_HEADER.size, "ELANIM2 header")
            unpacked = ANIMATION_FILE_HEADER.unpack(values)
            if unpacked[1] != 2 or unpacked[2] != ANIMATION_FILE_HEADER.size:
                raise ValueError("ELANIM2 header version/size mismatch")
            self.header = CaptureHeader(
                "ELANIM2",
                RECORDS["animation_file_header"]["id"],
                RECORDS["animation_file_header"]["schema_version"],
                unpacked,
            )
        else:
            raise ValueError(f"unsupported retained capture magic: {magic!r}")
        return self

    def __exit__(self, *_args: object) -> None:
        if self._stream:
            self._stream.close()
        self._stream = None

    def _read_complete(self, size: int, label: str) -> bytes:
        if not self._stream:
            raise RuntimeError("legacy reader is not open")
        data = self._stream.read(size)
        if len(data) != size:
            raise ValueError(f"incomplete {label}: {len(data)}/{size} bytes")
        return data

    def __iter__(self) -> Iterator[CaptureRecord]:
        if not self._stream or not self.header:
            raise RuntimeError("legacy reader must be used as a context manager")
        ordinal = 0
        if self.header.format == "ELPOSE2":
            while prefix := self._stream.read(POSE_RECORD_HEADER.size):
                if len(prefix) != POSE_RECORD_HEADER.size:
                    raise ValueError("incomplete ELPOSE2 record header")
                values = POSE_RECORD_HEADER.unpack(prefix)
                if values[0] != b"POSE":
                    raise ValueError(f"unexpected ELPOSE2 record magic {values[0]!r}")
                record_bytes = values[1]
                bone_count = values[8]
                expected = POSE_RECORD_HEADER.size + bone_count * 12 * 4 * 2
                schema = RECORDS["pose"]
                if bone_count > 1024 or record_bytes != expected:
                    raise ValueError("ELPOSE2 record bounds/layout mismatch")
                if not schema["minimum_bytes"] <= record_bytes <= schema["maximum_bytes"]:
                    raise ValueError("ELPOSE2 record violates registry bounds")
                payload = self._read_complete(
                    record_bytes - POSE_RECORD_HEADER.size,
                    f"ELPOSE2 record {ordinal} payload",
                )
                yield CaptureRecord(
                    schema["id"],
                    schema["schema_version"],
                    "POSE",
                    values[2],
                    values[3],
                    values,
                    payload,
                )
                ordinal += 1
            return
        while prefix := self._stream.read(ANIMATION_RECORD_HEADER.size):
            if len(prefix) != ANIMATION_RECORD_HEADER.size:
                raise ValueError("incomplete ELANIM2 record header")
            values = ANIMATION_RECORD_HEADER.unpack(prefix)
            magic = values[0].decode("ascii", "strict")
            schema_name = {
                "BASE": "animation_base",
                "FINL": "animation_final",
            }.get(magic)
            if not schema_name:
                raise ValueError(f"unexpected ELANIM2 record magic {magic!r}")
            record_bytes = values[1]
            bone_count = values[8]
            expected = (
                ANIMATION_RECORD_HEADER.size
                + bone_count * 7 * 4
                + ((bone_count + 31) // 32) * 4
            )
            schema = RECORDS[schema_name]
            if bone_count > 1024 or record_bytes != expected:
                raise ValueError("ELANIM2 record bounds/layout mismatch")
            if not schema["minimum_bytes"] <= record_bytes <= schema["maximum_bytes"]:
                raise ValueError("ELANIM2 record violates registry bounds")
            payload = self._read_complete(
                record_bytes - ANIMATION_RECORD_HEADER.size,
                f"ELANIM2 record {ordinal} payload",
            )
            yield CaptureRecord(
                schema["id"],
                schema["schema_version"],
                magic,
                values[2],
                values[3],
                values,
                payload,
            )
            ordinal += 1


def validate_legacy_golden_metadata() -> dict[str, object]:
    metadata = json.loads(GOLDEN_METADATA.read_text(encoding="utf-8"))
    if metadata.get("metadata_version") != 1:
        raise ValueError("unsupported legacy golden metadata version")
    pose = metadata["formats"]["ELPOSE2"]
    animation = metadata["formats"]["ELANIM2"]
    expected = {
        "pose_file_id": RECORDS["pose_file_header"]["id"],
        "pose_file_version": RECORDS["pose_file_header"]["schema_version"],
        "pose_file_bytes": POSE_FILE_HEADER.size,
        "pose_id": RECORDS["pose"]["id"],
        "pose_bytes": POSE_RECORD_HEADER.size,
        "animation_file_id": RECORDS["animation_file_header"]["id"],
        "animation_file_version": RECORDS["animation_file_header"]["schema_version"],
        "animation_file_bytes": ANIMATION_FILE_HEADER.size,
        "base_id": RECORDS["animation_base"]["id"],
        "final_id": RECORDS["animation_final"]["id"],
        "animation_bytes": ANIMATION_RECORD_HEADER.size,
    }
    actual = {
        "pose_file_id": pose["file_record_id"],
        "pose_file_version": pose["file_schema_version"],
        "pose_file_bytes": pose["file_header_bytes"],
        "pose_id": pose["record_ids"]["POSE"],
        "pose_bytes": pose["record_header_bytes"],
        "animation_file_id": animation["file_record_id"],
        "animation_file_version": animation["file_schema_version"],
        "animation_file_bytes": animation["file_header_bytes"],
        "base_id": animation["record_ids"]["BASE"],
        "final_id": animation["record_ids"]["FINL"],
        "animation_bytes": animation["record_header_bytes"],
    }
    if actual != expected:
        raise ValueError(f"legacy golden metadata drift: {actual!r} != {expected!r}")
    return metadata
