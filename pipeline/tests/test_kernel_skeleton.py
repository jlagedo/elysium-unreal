"""The skeleton's pure parts on a fixture listing; its oracle on the real corpus when present.

`kernel_skeleton` reads the listing database and `vampire.dll`, neither of which can be fixtured.
What it derives from a listing — the arm inventory, the operand classification, the `this`
tracking across an early-return epilogue, the slot arithmetic — is exercised here on the pinned
listing of `CAI_BaseNPC::UpdateEnemyPos 0x10271900`, the function pass R was piloted on, through a
builder whose corpus lookups are stubbed. The oracle test at the bottom runs the real command and
asserts the two constants the pilot read by hand, and skips when the corpus is not on the machine.
"""

from __future__ import annotations

import os
import sys
import tempfile
from pathlib import Path

import pytest

os.environ.setdefault("ELYSIUM_WORK_ROOT", tempfile.gettempdir())
os.environ.setdefault("ELYSIUM_VTMB_ROOT", tempfile.gettempdir())

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "research" / "tooling" / "ghidra" / "driver"))

import kernel_skeleton as ks  # noqa: E402

LISTING = """\
10271900  SUB ESP,0x14
10271903  PUSH EBP
10271904  PUSH ESI
10271905  MOV ESI,ECX
10271907  PUSH EDI
10271908  MOV ECX,dword ptr [ESI + 0x5d34]
1027190e  MOV EAX,dword ptr [ECX + 0x18]
10271911  CMP EAX,0x3
10271914  JZ 0x10271a93
1027191a  CMP EAX,0x1
1027191d  JZ 0x10271a93
10271923  CALL 0x100037e2
10271928  CMP EAX,0x2
1027192b  JNZ 0x10271a93
10271931  MOV ECX,dword ptr [ESI + 0x5d34]
10271937  PUSH EBX
10271938  CALL 0x10015906
1027193d  MOV EBP,dword ptr [0x10566458]
10271943  LEA EDI,[ESI + 0x5ce0]
10271949  PUSH EDI
1027194a  MOV ECX,EBP
1027194c  MOV EBX,EAX
1027194e  CALL 0x10009c8c
10271953  CMP EAX,EBX
10271955  POP EBX
10271956  JZ 0x10271996
10271958  MOV ECX,dword ptr [EDI]
1027195a  CMP ECX,-0x1
1027195d  JZ 0x1027197c
1027197c  XOR EAX,EAX
1027197e  MOV ECX,dword ptr [ESI + 0x5d34]
10271984  PUSH 0x1070d1b0
10271989  PUSH EAX
1027198a  CALL 0x10008abc
1027198f  POP EDI
10271990  POP ESI
10271991  POP EBP
10271992  ADD ESP,0x14
10271995  RET
10271996  MOV EDX,dword ptr [ESI]
10271998  MOV ECX,ESI
1027199a  CALL dword ptr [EDX + 0x29c]
102719b7  MOV ECX,dword ptr [ESI + 0x5d34]
102719bd  CALL 0x1000df94
102719c2  FSTP float ptr [ESP + 0x10]
10271a36  FCOMP float ptr [0x104454c8]
10271a41  AND EAX,0x4100
10271a46  JZ 0x10271a68
10271a59  FCOMP double ptr [0x10497c80]
10271a66  JNZ 0x10271a93
10271a68  MOV EAX,dword ptr [ESP + 0x10]
10271a93  POP EDI
10271a99  RET
"""


class FakeRow(dict):
    def __getitem__(self, key):
        return dict.__getitem__(self, key)

    def keys(self):
        return dict.keys(self)


class FakeImage:
    base = 0x10000000

    def read(self, va, width):
        return {0x104454c8: (".rdata", 80.0), 0x10497c80: (".rdata", 12.0),
                0x10566458: (".data", 0x106eb5d8)}.get(va, ("?", "unread"))

    def dwords_while(self, va, keep):
        return []


class FakeNames:
    datamap = {"CAI_BaseNPC": {"base": None, "records": [
        {"name": "m_pNavigator", "offset": 0x5d34, "typeName": "embedded", "embedded": "CAI_Navigator"},
        {"name": "m_hEnemy", "offset": 0x5ce0, "typeName": "ehandle", "embedded": None},
        {"name": "InputSetRelationship", "offset": 0, "typeName": "string", "flagNames": ["INPUT"]},
    ]}, "CAI_Navigator": {"base": None, "records": [
        {"name": "m_navType", "offset": 0x18, "typeName": "int", "embedded": None}]}}
    layout = {0x5d34: ("m_pNavigator", "CAI_Navigator*")}
    shape = {0x5d34: "FElysiumScriptedCharacter::Motor"}
    slots = {167: ("0x101a67e0", "walked", "CBaseEntity* GetEnemy() const")}
    field = ks.Names.field
    describe = ks.Names.describe


class FakeBuilder(ks.Builder):
    """The real walk over a pinned listing, with the two databases replaced by dictionaries."""

    def __init__(self):
        self.image = FakeImage()
        self.names = FakeNames()
        self.thunks = {0x100037e2: 0x102ee620, 0x10015906: 0x102ee160, 0x10009c8c: 0x100290c0,
                       0x10008abc: 0x102ed310, 0x1000df94: 0x102ee1a0}

    def function(self, addr):
        if addr == 0x10271900:
            return FakeRow(addr="10271900", ns="CAI_BaseNPC", name="UpdateEnemyPos",
                           module="vampire.dll", size=410, thunk=0, warn="")
        if addr in self.thunks:
            return FakeRow(addr=f"{addr:08x}", ns="Global", name=f"thunk_FUN_{self.thunks[addr]:08x}",
                           module="vampire.dll", size=5, thunk=1, warn="")
        return FakeRow(addr=f"{addr:08x}", ns="Global", name=f"FUN_{addr:08x}",
                       module="vampire.dll", size=1, thunk=0, warn="")

    @property
    def listing(self):
        thunks = self.thunks

        class L:
            @staticmethod
            def execute(_sql, params):
                addr = int(params[1], 16)
                if addr == 0x10271900:
                    return _One({"asm": LISTING})
                if addr in thunks:
                    return _One({"asm": f"{addr:08x}  JMP 0x{thunks[addr]:08x}\n"})
                return _One(None)
        return L()


class _One:
    def __init__(self, row):
        self.row = row

    def fetchone(self):
        return self.row


@pytest.fixture
def skeleton():
    return FakeBuilder().build(0x10271900)


def test_every_conditional_jump_is_an_arm_with_both_targets(skeleton):
    arms = {a["at"]: a for a in skeleton.arms}
    assert list(arms) == ["0x10271914", "0x1027191d", "0x1027192b", "0x10271956",
                          "0x1027195d", "0x10271a46", "0x10271a66"]
    # The fixture drops the instructions between 0x10271a46 and 0x10271a59, so the fall-through
    # is the next listed line; the taken target is what the port must branch to.
    assert arms["0x10271a46"] == {"at": "0x10271a46", "op": "JZ", "taken": "0x10271a68",
                                  "next": "0x10271a59"}
    assert arms["0x10271a66"]["taken"] == "0x10271a93" and arms["0x10271a66"]["next"] == "0x10271a68"
    assert skeleton.rets == [0x10271995, 0x10271a99]


def test_globals_are_read_at_the_instruction_width(skeleton):
    by_va = {g["va"]: g for g in skeleton.globals_}
    assert by_va["0x104454c8"]["width"] == "float" and by_va["0x104454c8"]["value"] == "80.0"
    assert by_va["0x10497c80"]["width"] == "double" and by_va["0x10497c80"]["value"] == "12.0"


def test_this_survives_the_early_return_epilogue(skeleton):
    """`POP ESI` at 0x10271990 belongs to the swap arm's own epilogue; the block at 0x10271996
    is reached by the jump at 0x10271956 with ESI still `this`."""
    reads = {r["at"]: r["what"] for r in skeleton.reads}
    assert reads["0x10271996"].startswith("this+0x0000  vtable pointer")
    assert reads["0x102719b7"].startswith("this+0x5d34  CAI_BaseNPC::m_pNavigator embedded → CAI_Navigator")
    assert reads["0x1027190e"].startswith("[this+0x5d34]+0x0018  CAI_Navigator::m_navType int")


def test_calls_follow_thunks_and_name_slots_off_this(skeleton):
    calls = {c["at"]: c for c in skeleton.calls}
    assert calls["0x10271923"]["target"] == "0x102ee620" and "(thunk)" in calls["0x10271923"]["what"]
    assert calls["0x1027199a"]["kind"] == "virtual" and calls["0x1027199a"]["slot"] == 167
    assert calls["0x1027199a"]["receiver"] == "this"
    assert "GetEnemy" in calls["0x1027199a"]["what"]


def test_writes_are_the_three_local_stores_only(skeleton):
    assert [w["at"] for w in skeleton.writes] == ["0x102719c2"]
    assert not [w for w in skeleton.writes if w["what"].startswith("this+")]


def test_family_rows_read_the_first_column(tmp_path):
    fam = tmp_path / "X.tsv"
    fam.write_text("# comment\n10271900\trule\t19-29\tT\tE\n0x10271B10\tdead\t19-29\t-\tE\n",
                   encoding="utf-8")
    assert ks.family_rows(fam) == [0x10271900, 0x10271b10]


# --- the oracle on the real corpus ------------------------------------------------------------

def _corpus_present() -> bool:
    try:
        return (ks.corpus._corpus_dir() / "corpus.sqlite").is_file() and \
            (ks.vtmb_root() / "Vampire" / "dlls" / "vampire.dll").is_file()
    except Exception:
        return False


@pytest.mark.skipif(not _corpus_present(), reason="the Ghidra corpus is not on this machine")
def test_pilot_function_against_the_hand_read():
    sk = ks.Builder(ks.Image(ks.vtmb_root() / "Vampire" / "dlls" / "vampire.dll"), ks.Names()).build(0x10271900)
    assert len(sk.arms) == 8
    values = {g["va"]: g["value"] for g in sk.globals_}
    assert values["0x104454c8"] == "80.0" and values["0x10497c80"] == "12.0"
    slots = sorted({c["slot"] for c in sk.calls if c["kind"] == "virtual"})
    assert slots == [167, 541, 563]
    assert all(c["receiver"] == "this" for c in sk.calls if c["kind"] == "virtual")
    assert not sk.damaged
