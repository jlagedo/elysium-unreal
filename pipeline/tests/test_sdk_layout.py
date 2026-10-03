"""`sdk_layout`'s preprocessing on hand-made header text: comments and literals out with the line
structure kept, `#if` arithmetic answered without a warning, a class's members read once."""

from __future__ import annotations

import sys
import warnings
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "research" / "tooling" / "ghidra" / "driver"))

import sdk_layout  # noqa: E402


@pytest.mark.parametrize("text, stripped", [
    ("int a; // note\nint b;", "int a; \nint b;"),
    ("a /* one\ntwo */ b", "a \n b"),
    ('x = "//not a comment"; y', 'x = ""; y'),
    ("c = '\\''; d", "c = ''; d"),
    ('s = "a\\"b" t', 's = "" t'),
    ("/*/ still open */ z", " z"),
    ("x /* never closed\n\n", "x \n\n"),
    ('q "never closed\nnext', 'q ""'),
    ('w "ends in a backslash\\', 'w ""'),
    ("// last line", ""),
    ("#define A(x) x // why\n#if 1 /* c */\n", "#define A(x) x \n#if 1 \n"),
])
def test_strip_drops_comments_and_literals_and_keeps_lines(text, stripped):
    assert sdk_layout._strip(text) == stripped


def test_condition_answers_a_macro_call_false_without_a_warning():
    with warnings.catch_warnings():
        warnings.simplefilter("error")             # a SyntaxWarning would raise here
        assert sdk_layout._condition("defined(GAME_DLL) && !defined(CLIENT_DLL)") is True
        assert sdk_layout._condition("SOME_MACRO(1)") is False      # `0 (1)`: an int "called"
        assert sdk_layout._condition("0") is False


HEADER = """\
class CBase
{
public:
    virtual void Spawn( void );
    int m_iHealth; // hit points
    float m_flSpeed;
};
"""


def test_members_and_methods_are_read_once_and_handed_out_as_copies(tmp_path):
    root = tmp_path / "sdk"
    (root / "game" / "server").mkdir(parents=True)
    header = root / "game" / "server" / "base.h"
    header.write_text(HEADER, encoding="utf-8")
    sdk = sdk_layout.Sdk(root)
    members = sdk.members("CBase")
    assert members == [("int", "m_iHealth"), ("float", "m_flSpeed")]
    members.clear()
    base, methods = sdk.methods("CBase")
    assert [m.name for m in methods] == ["Spawn"]
    header.write_text("class CBase\n{\n};\n", encoding="utf-8")
    assert sdk.members("CBase") == [("int", "m_iHealth"), ("float", "m_flSpeed")]
    assert sdk.answers() == 2
    assert sdk.members("CNotThere") == []
