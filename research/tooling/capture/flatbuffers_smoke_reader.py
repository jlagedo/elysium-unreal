"""Run the x86 smoke producer and read its FlatBuffer with Python."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile


EXPECTED_SEQUENCE = 0x1020304050607080
EXPECTED_VALUES = (1.25, -2.5, 3.75)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--producer", required=True, type=Path)
    parser.add_argument("--generated-python", required=True, type=Path)
    args = parser.parse_args()

    generated_python = args.generated_python.resolve()
    sys.path.insert(0, str(generated_python))
    from Elysium.Capture.Smoke import SmokeRecord  # noqa: PLC0415

    with tempfile.TemporaryDirectory() as temporary:
        output = Path(temporary) / "smoke.flatbuffer"
        subprocess.run([str(args.producer.resolve()), str(output)], check=True)
        payload = output.read_bytes()

    if not SmokeRecord.SmokeRecord.SmokeRecordBufferHasIdentifier(payload, 0):
        raise RuntimeError("Python rejected the smoke file identifier")
    record = SmokeRecord.SmokeRecord.GetRootAs(payload, 0)
    label = record.Label()
    values = tuple(record.Values(index) for index in range(record.ValuesLength()))
    if record.Sequence() != EXPECTED_SEQUENCE:
        raise RuntimeError(f"unexpected sequence: {record.Sequence():#x}")
    if label != b"capture-smoke":
        raise RuntimeError(f"unexpected label: {label!r}")
    if values != EXPECTED_VALUES:
        raise RuntimeError(f"unexpected values: {values!r}")
    print(
        "flatbuffers-smoke-python-v1 state=complete "
        f"bytes={len(payload)} values={len(values)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
