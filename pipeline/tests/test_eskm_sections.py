"""`eskm.read_sections`: named section payloads off the directory seek path.

Every container here is synthesised in-code from the documented header layout; nothing
depends on the user's game install.
"""
import hashlib
import os
import tempfile
import struct
import unittest

from elysium_pipeline.formats import eskm
import pytest


def container_bytes(sections, magic=eskm.MAGIC, version=eskm.VERSION):
    """One synthetic container: the 16-byte header, the section directory, then the
    payloads packed back to back."""
    at = 16 + 20 * len(sections)
    head = bytearray(struct.pack("<4sIII", magic, version, len(sections), 0))
    body = bytearray()
    for tag, payload in sections.items():
        head += struct.pack("<4sQQ", tag, at, len(payload))
        body += payload
        at += len(payload)
    return bytes(head + body)


SECTIONS = {
    b"SKEL": b"skeleton payload bytes",
    b"MATL": b"material table",
    b"ANIM": b"\x01" * 4096,
}


class ReadSections(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = self._tmp.name
        self.addCleanup(self._tmp.cleanup)

    def _write(self, blob, name="body.eskm"):
        path = os.path.join(self.dir, name)
        with open(path, "wb") as f:
            f.write(blob)
        return path

    def test_returns_the_named_payloads_verbatim(self):
        blob = container_bytes(SECTIONS)
        path = self._write(blob)
        out = eskm.read_sections(path, (b"SKEL", b"MATL"))
        assert out == {b"SKEL": SECTIONS[b"SKEL"], b"MATL": SECTIONS[b"MATL"]}
        # The payloads are the same bytes `directory` locates in the full blob.
        for tag, payload in out.items():
            at, size = eskm.directory(blob)[tag]
            assert payload == blob[at:at + size]

    def test_an_absent_section_maps_to_none(self):
        path = self._write(container_bytes({b"SKEL": b"only section"}))
        out = eskm.read_sections(path, (b"SKEL", b"MATL"))
        assert out == {b"SKEL": b"only section", b"MATL": None}

    def test_agrees_with_section_digest_about_the_bytes(self):
        path = self._write(container_bytes(SECTIONS))
        tags = (b"SKEL", b"ATCH", b"MATL")
        out = eskm.read_sections(path, tags)
        expected = hashlib.sha256()
        for tag in tags:
            expected.update(tag)
            expected.update(b"\0")
        for tag in tags:
            if out[tag] is None:
                expected.update(b"absent\0")
            else:
                expected.update(out[tag])
                expected.update(b"\0")
        assert eskm.section_digest(path, tags) == expected.hexdigest()

    def test_a_foreign_container_raises(self):
        path = self._write(container_bytes(SECTIONS, magic=b"NOPE"))
        with pytest.raises(ValueError, match="not an .eskm container"):
            eskm.read_sections(path, (b"SKEL",))

    def test_a_stale_version_raises(self):
        path = self._write(container_bytes(SECTIONS, version=eskm.VERSION + 1))
        with pytest.raises(ValueError, match="re-export"):
            eskm.read_sections(path, (b"SKEL",))

    def test_a_truncated_header_raises(self):
        path = self._write(container_bytes(SECTIONS)[:10])
        with pytest.raises(ValueError, match="truncated .eskm header"):
            eskm.read_sections(path, (b"SKEL",))

    def test_a_truncated_directory_raises(self):
        path = self._write(container_bytes(SECTIONS)[:16 + 20 * 3 - 4])
        with pytest.raises(ValueError, match="truncated .eskm section directory"):
            eskm.read_sections(path, (b"SKEL",))

    def test_a_truncated_section_raises(self):
        path = self._write(container_bytes(SECTIONS)[:-100])
        with pytest.raises(ValueError, match="truncated b'ANIM' section"):
            eskm.read_sections(path, (b"ANIM",))

    def test_a_missing_file_raises_oserror(self):
        with pytest.raises(FileNotFoundError):
            eskm.read_sections(os.path.join(self.dir, "absent.eskm"), (b"SKEL",))

    def test_section_digest_error_shape_is_unchanged(self):
        # The digest encodes failure rather than raising; a caller deciding staleness
        # depends on that shape, so `read_sections` must not have altered it.
        missing = eskm.section_digest(os.path.join(self.dir, "absent.eskm"), (b"SKEL",))
        assert missing == "missing:FileNotFoundError"
        foreign = self._write(container_bytes(SECTIONS, magic=b"NOPE"), name="foreign.eskm")
        assert eskm.section_digest(foreign, (b"SKEL",)) == "missing:magic"
