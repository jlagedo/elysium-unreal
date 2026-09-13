"""The accessor pass (`name_passes.accessor_pass`) on an in-memory corpus.

The pass reads the whole-body corpus and the recovered layout; here both are built by hand.
What is checked: the shapes it names (value, address, setter), the shapes it refuses (a derived
expression, an interior word, a zero test of a non-bool word), where the receiver's class comes
from (the typed access, the slot's holder, a typed caller), and that an SDK header's own
one-line body overrides the coined spelling.
"""

from __future__ import annotations

import os
import sqlite3
import sys
import tempfile
from pathlib import Path

os.environ.setdefault("ELYSIUM_WORK_ROOT", tempfile.gettempdir())

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "research" / "tooling" / "ghidra" / "driver"))

import name_passes as np_  # noqa: E402
import sdk_layout as sl  # noqa: E402


def _corpus(functions, accesses, listing, vtables=(), edges=(), strings=()):
    """A whole-body corpus reduced to what `accessor_pass` reads, in memory."""
    db = sqlite3.connect(":memory:")
    db.executescript("""
        CREATE TABLE functions (module, addr, name, ns, size, cc, thunk, code);
        CREATE TABLE vtables (module, cls, slot, func, sub);
        CREATE TABLE edges (module, caller, callee, kind);
        CREATE TABLE string_refs (module, func_addr, str_addr);
        CREATE TABLE accesses (module, func_addr, cls, off, field, kind);
    """)
    db.executemany("INSERT INTO functions VALUES ('vampire.dll',?,?,?,?,?,0,?)", functions)
    db.executemany("INSERT INTO vtables VALUES ('vampire.dll',?,?,?,0)", vtables)
    db.executemany("INSERT INTO edges VALUES ('vampire.dll',?,?,'direct')", edges)
    db.executemany("INSERT INTO string_refs VALUES ('vampire.dll',?,'s')", strings)
    db.executemany("INSERT INTO accesses VALUES ('vampire.dll',?,?,?,?,?)", accesses)
    asm = sqlite3.connect(":memory:")
    asm.execute("CREATE TABLE listing (module, addr, asm)")
    asm.executemany("INSERT INTO listing VALUES ('vampire.dll',?,?)", listing)
    return np_.Corpus(db, "vampire.dll", asm)


def _layout():
    words = [
        np_.Word("CAI_BaseNPCTroika", 0x1bc, 0x1c0, "m_aThinkFunctions.m_Size", "int", "CBaseEntity", "interior"),
        np_.Word("CAI_BaseNPCTroika", 0x200, 0x204, "m_lifeState", "int", "CBaseEntity", "datamap"),
        np_.Word("CAI_BaseNPCTroika", 0x268, 0x26c, "m_iEFlags", "int", "CBaseEntity", "datamap"),
        np_.Word("CAI_BaseNPCTroika", 0x26c, 0x270, "m_Collision", "CCollisionProperty", "CBaseEntity", "datamap"),
        np_.Word("CAI_BaseNPCTroika", 0x3d4, 0x3e0, "m_vecVelocity", "Vector", "CBaseEntity", "datamap"),
        np_.Word("CAI_BaseNPCTroika", 0x63f0, 0x63f4, "m_bForceFrequentThink", "bool", "CAI_BaseNPCTroika", "walked"),
    ]
    return {"CAI_BaseNPCTroika": words}


CHAINS = {"CAI_BaseNPCTroika": ["CAI_BaseNPCTroika", "CAI_BaseNPC", "CBaseEntity"],
          "CAI_BaseNPC": ["CAI_BaseNPC", "CBaseEntity"], "CBaseEntity": ["CBaseEntity"],
          "CAISound": ["CAISound", "CBaseEntity"]}


def _fn(addr, size, code, name=None, ns="Global"):
    return (addr, name or f"FUN_{addr}", ns, size, "__thiscall", code)


def test_stem_strips_the_hungarian_prefix():
    assert np_.stem("m_flStealthVisionCone") == "StealthVisionCone"
    assert np_.stem("m_bForceFrequentThink") == "ForceFrequentThink"
    assert np_.stem("m_lifeState") == "LifeState"
    assert np_.stem("m_Collision") == "Collision"
    assert np_.stem("m_iEFlags") == "EFlags"


def test_accessor_names_getters_setters_and_refs_from_the_layout():
    functions = [
        _fn("100b4ef0", 7, "int __thiscall CAISound::FUN_100b4ef0(CAISound *this)\n\n{\n  return this->m_iEFlags;\n}\n"),
        _fn("100b4f10", 13, "void __thiscall FUN_100b4f10(void *this,int param_1)\n\n{\n"
                            "  *(int *)((int)this + 0x268) = param_1;\n  return;\n}\n"),
        _fn("10027610", 7, "undefined4 * __thiscall CAISound::FUN_10027610(CAISound *this)\n\n{\n  return &this->m_Collision;\n}\n"),
        _fn("100274b0", 7, "int __thiscall CAISound::FUN_100274b0(CAISound *this)\n\n{\n  return (int)this->m_vecVelocity;\n}\n"),
        _fn("10027490", 9, "uint __thiscall CAISound::FUN_10027490(CAISound *this)\n\n{\n  return this->m_iEFlags & 1;\n}\n"),
        _fn("101aa770", 7, "bool __thiscall FUN_101aa770(int param_1)\n\n{\n  return *(bool *)(param_1 + 0x63f0);\n}\n"),
        _fn("10099000", 7, "int __thiscall CAISound::FUN_10099000(CAISound *this)\n\n{\n  return this->m_aThinkFunctions.m_Size;\n}\n"),
        _fn("10099100", 7, "int __thiscall FUN_10099100(int param_1)\n\n{\n  return *(int *)(param_1 + 0x200);\n}\n"),
        # a typed caller handing its own `this` to the untyped leaf 10099100
        _fn("10099200", 40, "void __thiscall CAI_BaseNPCTroika::Think(CAI_BaseNPCTroika *this)\n\n{\n"
                            "  FUN_10099100(this);\n  return;\n}\n", "Think", "CAI_BaseNPCTroika"),
    ]
    accesses = [
        ("100b4ef0", "CAISound", 0x268, "m_iEFlags", "named"),
        ("100b4f10", "", 0x268, "", "this"),
        ("10027610", "CAISound", 0x26c, "m_Collision", "named"),
        ("100274b0", "CAISound", 0x3d4, "m_vecVelocity", "named"),
        ("10027490", "CAISound", 0x268, "m_iEFlags", "named"),
        ("101aa770", "", 0x63f0, "", "this"),
        ("10099000", "CAISound", 0x1bc, "m_aThinkFunctions.m_Size", "named"),
        ("10099100", "", 0x200, "", "this"),
    ]
    listing = [(f[0], "RET 0x4" if f[0] == "100b4f10" else "RET") for f in functions]
    vtables = [("CAI_BaseNPCTroika", 7, "101aa770"), ("CAI_BaseNPCTroika", 8, "100b4f10")]
    corpus = _corpus(functions, accesses, listing, vtables, edges=[("10099200", "10099100")])
    rows, report = np_.accessor_pass(corpus, None, CHAINS, set(), _layout())
    got = {r.addr: (r.name, r.tier) for r in rows}
    assert got == {
        "100b4ef0": ("CBaseEntity::GetEFlags", "accessor"),
        "100b4f10": ("CAI_BaseNPCTroika::SetEFlags", "accessor"),      # a slot body: its holder owns it
        "10027610": ("CBaseEntity::GetCollision", "accessor"),         # `&m_Collision`: the address
        "100274b0": ("CBaseEntity::GetVelocity", "accessor"),          # a struct member read typed: the address
        "101aa770": ("CAI_BaseNPCTroika::GetForceFrequentThink", "accessor"),
        "10099100": ("CBaseEntity::GetLifeState", "accessor"),         # receiver typed from the caller
    }
    evidence = {r.addr: r.evidence for r in rows}
    assert "returns the address of m_vecVelocity" in evidence["100274b0"]
    assert "receiver typed from 1 call site(s) passing this (CAI_BaseNPCTroika)" in evidence["10099100"]
    assert "derived" in report["reasons"]["10027490"]
    assert "interior word" in report["reasons"]["10099000"]
    assert report["proposed"] == 6


def test_accessor_takes_the_sdk_name_when_a_header_declares_the_body(tmp_path):
    header = tmp_path / "game" / "server" / "baseentity.h"
    header.parent.mkdir(parents=True)
    header.write_text(
        "class CBaseEntity\n{\npublic:\n"
        "\tint GetEFlags() const { return m_iEFlags; }\n"
        "\tbool IsAlive( void ) { return m_lifeState == LIFE_ALIVE; }\n"
        "\tCCollisionProperty *CollisionProp() { return &m_Collision; }\n"
        "\tvirtual ICollideable *GetCollideable();\n"
        "\tvirtual void Foo();\n};\n", encoding="utf-8")
    (tmp_path / "game" / "server" / "baseentity.cpp").write_text(
        "ICollideable *CBaseEntity::GetCollideable()\n{\n\treturn &m_Collision;\n}\n", encoding="utf-8")
    (tmp_path / "public").mkdir()
    (tmp_path / "public" / "const.h").write_text("#define\tLIFE_ALIVE\t0 // alive\n", encoding="utf-8")
    sdk = sl.Sdk(tmp_path)
    functions = [
        _fn("100b4ef0", 7, "int __thiscall CAISound::FUN_100b4ef0(CAISound *this)\n\n{\n  return this->m_iEFlags;\n}\n"),
        _fn("100b4dc0", 14, "bool __thiscall CAISound::FUN_100b4dc0(CAISound *this)\n\n{\n  return this->m_lifeState == 0;\n}\n"),
        _fn("100b4dd0", 14, "bool __thiscall CAISound::FUN_100b4dd0(CAISound *this)\n\n{\n  return this->m_lifeState != 0;\n}\n"),
        _fn("10027610", 7, "undefined4 * __thiscall CAISound::FUN_10027610(CAISound *this)\n\n{\n  return &this->m_Collision;\n}\n"),
        _fn("10027620", 7, "undefined4 * __thiscall CAISound::FUN_10027620(CAISound *this)\n\n{\n  return &this->m_Collision;\n}\n"),
    ]
    accesses = [("100b4ef0", "CAISound", 0x268, "m_iEFlags", "named"),
                ("100b4dc0", "CAISound", 0x200, "m_lifeState", "named"),
                ("100b4dd0", "CAISound", 0x200, "m_lifeState", "named"),
                ("10027610", "CAISound", 0x26c, "m_Collision", "named"),
                ("10027620", "CAISound", 0x26c, "m_Collision", "named")]
    vtables = [("CAISound", 2, "10027610")]       # only one of the two address-getters fills a slot
    corpus = _corpus(functions, accesses, [(f[0], "RET") for f in functions], vtables)
    rows, report = np_.accessor_pass(corpus, sdk, CHAINS, set(), _layout())
    got = {r.addr: (r.name, r.tier) for r in rows}
    assert got == {"100b4ef0": ("CBaseEntity::GetEFlags", "inferred"),
                   "100b4dc0": ("CBaseEntity::IsAlive", "inferred"),
                   "10027610": ("CAISound::GetCollideable", "inferred"),    # the virtual, at a slot
                   "10027620": ("CBaseEntity::GetCollision", "accessor")}   # two SDK bodies, no slot: coined
    # the opposite polarity is no SDK body, and a test of a non-bool word is not coined
    assert "enum it compares against" in report["reasons"]["100b4dd0"]
    assert "it is the virtual" in next(r.evidence for r in rows if r.addr == "10027610")


def test_identity_keeps_the_accessor_tier_of_a_coined_witness():
    chains = {"Base": ["Base"], "Troika": ["Troika", "Base"]}
    tables = {"Base": ["d0", "b1", "b2"], "Troika": ["d1", "t1", "t2"]}
    names = {"d0": "~Base", "d1": "~Troika", "b1": "FUN_1", "t1": "FUN_2", "b2": "FUN_3", "t2": "FUN_4"}

    class _Fake:
        module = "vampire.dll"

        def __init__(self):
            self.tables, self.name, self.ns, self.fills = tables, names, {}, {}
            for cls, table in tables.items():
                for slot, func in enumerate(table):
                    self.fills.setdefault(func, set()).add((cls, slot))

        def words(self, addr):
            return 0

        def label(self, addr):
            return self.name[addr]

    rows, _ = np_.identity_pass(_Fake(), None, chains,
                                {"t1": ("GetLastMoveThink", "accessor"), "t2": ("GetState", "inferred")})
    got = {r.addr: (r.name, r.tier) for r in rows}
    assert got == {"b1": ("Base::GetLastMoveThink", "accessor"), "b2": ("Base::GetState", "inferred")}
