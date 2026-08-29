"""`install.build_index` memoization over a synthetic install.

The search roots are patched to a temporary tree, so nothing here reads the user's
game. Importing `install` needs ELYSIUM_VTMB_ROOT to name some existing directory,
and any directory serves because every test overrides the derived roots.
"""
import contextlib
import io
import os
import struct
import tempfile
import unittest
from unittest import mock

os.environ.setdefault("ELYSIUM_VTMB_ROOT", tempfile.gettempdir())

from elysium_pipeline.formats import install, vpk


def pack_bytes(files):
    """One synthetic pack: concatenated payloads, the entry table, then the observed
    n-5 footer pointing at the directory start."""
    payload = b"".join(files.values())
    directory = bytearray()
    at = 0
    for name, blob in files.items():
        encoded = name.encode("ascii")
        directory += struct.pack("<I", len(encoded)) + encoded
        directory += struct.pack("<II", at, len(blob))
        at += len(blob)
    return payload + bytes(directory) + struct.pack("<I", len(payload)) + b"\x00"


class BuildIndexMemo(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        root = self._tmp.name
        self.game = os.path.join(root, "Vampire")
        self.patch = os.path.join(root, "Unofficial_Patch")
        os.makedirs(os.path.join(self.game, "materials"))
        os.makedirs(os.path.join(self.patch, "materials"))
        os.makedirs(os.path.join(self.patch, "particles"))
        with open(os.path.join(self.game, "pack000.vpk"), "wb") as f:
            f.write(pack_bytes({"materials/Shared.vmt": b"vpk shared",
                                "materials/VpkOnly.vmt": b"vpk only"}))
        with open(os.path.join(self.game, "materials", "retail_only.vmt"), "wb") as f:
            f.write(b"retail loose")
        with open(os.path.join(self.patch, "materials", "shared.vmt"), "wb") as f:
            f.write(b"patch shadow")
        with open(os.path.join(self.patch, "particles", "rain.txt"), "wb") as f:
            f.write(b"patch particle")
        self.enterContext(mock.patch.object(install, "GAME", self.game))
        self.enterContext(mock.patch.object(install, "PATCH", self.patch))
        self.enterContext(mock.patch.object(install, "LOOSE_ROOTS", [self.patch]))
        install.invalidate_index_cache()

    def tearDown(self):
        vpk.close_handles()
        install.invalidate_index_cache()
        self._tmp.cleanup()

    def test_retail_runtime_caches_below_maps_are_not_indexed(self):
        # The engine writes AI node graphs and sound caches under maps/ while it runs; a play
        # session must not re-stamp the index, and nothing reads those files.
        os.makedirs(os.path.join(self.patch, "maps", "graphs"))
        os.makedirs(os.path.join(self.patch, "maps", "soundcache"))
        for rel in (("maps", "sm_hub_1.bsp"), ("maps", "graphs", "sm_hub_1.ain"),
                    ("maps", "graphs", ".loc"), ("maps", "soundcache", "sm_hub_1.cache")):
            with open(os.path.join(self.patch, *rel), "wb") as f:
                f.write(b"x")
        index = install.build_index(verbose=False)
        assert "maps/sm_hub_1.bsp" in index
        assert not [k for k in index if k.startswith("maps/graphs/")
                          or k.startswith("maps/soundcache/")]

    def test_a_repeat_call_returns_the_same_index_object(self):
        with mock.patch.object(vpk, "index_all", wraps=vpk.index_all) as spy:
            first = install.build_index(verbose=False)
            second = install.build_index(verbose=False)
        assert first is second
        assert spy.call_count == 1

    def test_distinct_dirs_are_distinct_keys(self):
        materials = install.build_index(("materials",), verbose=False)
        particles = install.build_index(("particles",), verbose=False)
        assert materials is not particles
        assert "materials/retail_only.vmt" in materials
        assert "particles/rain.txt" not in materials
        assert "particles/rain.txt" in particles

    def test_a_list_and_a_tuple_of_dirs_share_one_key(self):
        assert install.build_index(["materials"], verbose=False) is install.build_index(("materials",), verbose=False)

    def test_changed_roots_are_distinct_keys(self):
        first = install.build_index(verbose=False)
        other_game = os.path.join(self._tmp.name, "OtherGame")
        os.makedirs(other_game)
        with mock.patch.object(install, "GAME", other_game):
            other = install.build_index(verbose=False)
        assert first is not other
        # The other game root has no packs and no retail loose tree; only the patch
        # root (still on LOOSE_ROOTS) contributes.
        assert "materials/vpkonly.vmt" not in other
        assert "materials/retail_only.vmt" not in other
        assert "materials/shared.vmt" in other

    def test_invalidate_forces_a_rebuild(self):
        first = install.build_index(verbose=False)
        install.invalidate_index_cache()
        second = install.build_index(verbose=False)
        assert first is not second
        assert first == second

    def test_shadowing_and_read_are_unchanged(self):
        idx = install.build_index(verbose=False)
        assert idx["materials/shared.vmt"] == ("loose", os.path.join(self.patch, "materials", "shared.vmt"))
        assert idx["materials/vpkonly.vmt"][0] == "vpk"
        assert install.read(idx, "materials/VpkOnly.vmt") == b"vpk only"
        assert install.read(idx, "materials/shared.vmt") == b"patch shadow"
        assert install.read(idx, "materials/retail_only.vmt") == b"retail loose"
        assert install.read(idx, "materials/absent.vmt") is None
