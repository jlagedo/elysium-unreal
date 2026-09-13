"""The naming passes' derivations (`name_passes.py`, `sdk_layout.py`) on hand-built inputs.

The passes read the whole-body corpus and a third-party SDK tree, neither of which can be
fixtured. What they derive is pure -- a header read as MSVC lays out its vtable, the stack words
a signature pops, the alignment of named slots, the screen that holds a run to the image's
arities, slot identity across a hierarchy -- and those run here on strings and dictionaries.
"""

from __future__ import annotations

import os
import sys
import tempfile
from pathlib import Path

os.environ.setdefault("ELYSIUM_WORK_ROOT", tempfile.gettempdir())

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "research" / "tooling" / "ghidra" / "driver"))

import name_passes as np_  # noqa: E402
import sdk_layout as sl  # noqa: E402

HEADERS = {
    "public/ihandleentity.h": """
class IHandleEntity
{
public:
    virtual ~IHandleEntity() {}
    virtual void SetRefEHandle( const CBaseHandle &handle ) = 0;
};
""",
    "public/datamap.h": """
#define DECLARE_SIMPLE_DATADESC() \\
    static datamap_t m_DataMap;
#define DECLARE_DATADESC() \\
    DECLARE_SIMPLE_DATADESC() \\
    virtual datamap_t *GetDataDescMap( void );
#define CNetworkVarForDerived( type, name ) \\
    virtual void NetworkStateChanged_##name() {}
typedef enum { CLASS_NONE } Class_T;
""",
    "game/server/baseentity.h": """
class CBaseEntity : public IHandleEntity
{
public:
    DECLARE_DATADESC();
    CBaseEntity();
    virtual ~CBaseEntity();
    virtual void Spawn( void );
    // virtual void Commented( void );
    virtual bool KeyValue( const char *szKeyName, const char *szValue );
    virtual bool KeyValue( const char *szKeyName, float flValue );
    virtual int  TakeHealth( float flHealth, int bitsDamageType ) { return 0; }
    CNetworkVarForDerived( int, m_nNextThinkTick );
    virtual Vector EyePosition( void );
    virtual Class_T Classify( void );
    void NotVirtual( int x );
#ifdef TF_DLL
    virtual void TeamFortressOnly( void );
#endif
    struct Nested { virtual void Inner(); };
    int m_iHealth;
};
""",
    "game/server/baseanimating.h": """
class CBaseAnimating : public CBaseEntity
{
public:
    virtual void Spawn( void );
    bool KeyValue( const char *szKeyName, const char *szValue );
    virtual void StudioFrameAdvance();
};
""",
}


def _sdk(tmp_path: Path) -> sl.Sdk:
    for rel, text in HEADERS.items():
        path = tmp_path / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    return sl.Sdk(tmp_path)


def test_vtable_follows_msvc_layout(tmp_path):
    sdk = _sdk(tmp_path)
    slots, ends = sdk.vtable(("IHandleEntity", "CBaseEntity", "CBaseAnimating"))
    names = [s.name for s in slots]
    # the destructor overrides slot 0; DECLARE_DATADESC expands; overloads sit together reversed;
    # the later tree's network-var hook, a comment, an #ifdef'd-out virtual, a nested type's
    # virtual and a non-virtual method add nothing; an override adds nothing
    assert names == ["~", "SetRefEHandle", "GetDataDescMap", "Spawn", "KeyValue", "KeyValue",
                     "TakeHealth", "EyePosition", "Classify", "StudioFrameAdvance"]
    assert slots[4].key[1] == "char*,float" and slots[5].key[1] == "char*,char*"
    assert ends == {"IHandleEntity": 2, "CBaseEntity": 9, "CBaseAnimating": 10}
    assert slots[3].owners == ["CBaseEntity", "CBaseAnimating"]


def test_stack_words_counts_by_value_structs_and_hidden_returns(tmp_path):
    sdk = _sdk(tmp_path)
    slots, _ = sdk.vtable(("IHandleEntity", "CBaseEntity"))
    words = {s.name + s.key[1]: sdk.stack_words(s.key[1], s.ret) for s in slots}
    assert words["TakeHealthfloat,int"] == 2
    assert words["EyePosition"] == 1          # Vector returned by value: hidden return slot
    assert words["Classify"] == 0             # an enum returns in EAX
    assert words["KeyValuechar*,char*"] == 2
    assert sdk.stack_words("variant_t,int", "bool") == 6
    assert sdk.stack_words("SomeUnknownStruct", "") is None


def test_definitions_carry_their_arity(tmp_path):
    source = tmp_path / "game" / "server" / "ai_thing.cpp"
    source.parent.mkdir(parents=True, exist_ok=True)
    source.write_text("""
void CAI_Thing::First( int a,
                       float b )
{
    if ( a ) { CAI_Thing::Helper( 1 ); }
}

bool
CAI_Thing::Second()
{
    return true;
}
""", encoding="utf-8")
    sdk = _sdk(tmp_path)
    assert sdk.definitions(source) == [("CAI_Thing", "First", 2, 2), ("CAI_Thing", "Second", 9, 0)]


def test_consistent_keeps_the_longest_increasing_chain():
    pairs = [(1, 2), (3, 4), (4, 1), (5, 6), (8, 9), (9, 3)]
    assert np_.consistent(pairs) == [(1, 2), (3, 4), (5, 6), (8, 9)]
    # a left position is used once
    assert np_.consistent([(2, 1), (2, 5), (4, 7)]) in ([(2, 1), (4, 7)], [(2, 5), (4, 7)])


def test_fills_names_equal_runs_only():
    named, broken = np_.fills([(0, 0), (3, 3), (6, 7), (8, 9)])
    assert {k: v[0] for k, v in named.items()} == {1: 1, 2: 2, 7: 8}
    assert broken == [((3, 3), (6, 7))]


def test_screen_drops_substitutions_and_runs_that_do_not_hold():
    anchors = [(0, 0), (4, 4), (8, 8)]
    named, broken = np_.fills(anchors)
    arity = {1: (1, 1), 2: (0, 2), 3: (3, 3), 5: (0, 1), 6: (1, 0), 7: (2, 2)}
    np_.screen(anchors, named, broken, lambda k, i: arity[k])
    # run 1-3: one substitution, two confirmations -> only the substitution goes
    assert 2 not in named and 1 in named and 3 in named
    # run 5-7: two substitutions, one confirmation -> the run goes
    assert not any(k in named for k in (5, 6, 7))
    assert ((4, 4), (8, 8)) in broken


class _FakeCorpus:
    """Just what `identity_pass` reads."""

    module = "vampire.dll"

    def __init__(self, tables, names, words):
        self.tables = tables
        self.name = names
        self.ns = {}
        self._w = words
        self.fills = {}
        for cls, table in tables.items():
            for slot, func in enumerate(table):
                if func:
                    self.fills.setdefault(func, set()).add((cls, slot))

    def words(self, addr):
        return self._w.get(addr)

    def label(self, addr):
        return self.name[addr]


def test_identity_names_overrides_from_one_stated_method():
    chains = {"Base": ["Base"], "Troika": ["Troika", "Base"], "Human": ["Human", "Troika", "Base"],
              "Other": ["Other"]}
    tables = {
        "Base": ["d0", "b1", "b2"],
        "Troika": ["d1", "t1", "b2"],
        "Human": ["d2", "h1", "h2"],
        "Other": ["d3", "b2"],          # an unrelated class holding b2 at another slot: folded
    }
    names = {"d0": "~Base", "d1": "~Troika", "d2": "~Human", "d3": "~Other",
             "b1": "SelectSchedule", "t1": "FUN_100001f1", "h1": "FUN_100001a1",
             "b2": "FUN_100001b2", "h2": "RunTask"}
    words = {"b1": 0, "t1": 0, "h1": 0, "b2": 1, "h2": 1}
    corpus = _FakeCorpus(tables, names, words)
    rows, report = np_.identity_pass(corpus, None, chains, {})
    got = {r.addr: r.name for r in rows}
    assert got == {"t1": "Troika::SelectSchedule", "h1": "Human::SelectSchedule"}
    assert "folded" in report["reasons"]["b2"]
    # destructors never propagate
    assert not any(r.name.endswith("~Base") for r in rows)


def test_identity_refuses_disagreement_and_arity_drift():
    chains = {"Base": ["Base"], "A": ["A", "Base"], "B": ["B", "Base"], "C": ["C", "Base"]}
    tables = {"Base": ["d", "x0"], "A": ["d", "x1"], "B": ["d", "x2"], "C": ["d", "x3"]}
    names = {"d": "~Base", "x0": "Think", "x1": "Touch", "x2": "FUN_100002a2", "x3": "FUN_100002a3"}
    corpus = _FakeCorpus(tables, names, {"x0": 0, "x1": 0})
    rows, report = np_.identity_pass(corpus, None, chains, {})
    assert rows == [] and report["disagree"] == 1
    names["x1"] = "Think"
    corpus = _FakeCorpus(tables, names, {"x0": 0, "x1": 0, "x2": 1})
    rows, report = np_.identity_pass(corpus, None, chains, {})
    assert rows == [] and report["arity"] == 1
