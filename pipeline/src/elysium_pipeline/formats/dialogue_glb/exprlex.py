"""The `dlgexpr` token classes a column-4/5 cell segments into.

The dialogue seam's `expressions[]` names four token classes the runtime
normalizer (`Source/ElysiumUE/Public/ElysiumDlg.h`, `ElysiumDlgExpr`) recognises: a skill-check
apply (`<Skill> <threshold>`, implicit `>=`), the condition joiners `&` and `|`, the action
separator `;`, and a Python-expression segment carried verbatim. This module names the segments
and their offsets; the rewrite into evaluated Python belongs to the runtime normalizer and is not
restated here (`ElysiumDlgExpr::ConditionToPython`/`ActionToPython`), matching the low-level
tokenizer `ElysiumDlgExprImpl::Tokenize` uses to find the same runs.
"""

from __future__ import annotations

from dataclasses import dataclass

TOKEN_SKILL_CHECK = "skill-check"
TOKEN_JOIN = "join"
TOKEN_SEPARATOR = "separator"
TOKEN_PYTHON = "python-expression"

_RELOPS = ("==", "!=", "<=", ">=", "<", ">")
_KEYWORDS = frozenset({"and", "or", "not", "None", "True", "False"})


def _is_ident_start(ch: str) -> bool:
    return ch.isalpha() or ch == "_"


def _is_ident_char(ch: str) -> bool:
    return ch.isalnum() or ch == "_"


@dataclass(frozen=True, slots=True)
class _Tok:
    kind: str          # "ident" | "int" | "op" | "string" | "space"
    text: str
    start: int
    end: int


def _tokenize(text: str) -> list[_Tok]:
    """A lightweight token stream: enough to find skill-checks, `&`/`|`/`;` and nothing more.

    A quoted string is passed through opaque so a `&`, `|`, `;` or digit inside one is never
    mistaken for syntax, mirroring `ElysiumDlgExprImpl::Tokenize`.
    """

    toks: list[_Tok] = []
    i, n = 0, len(text)
    while i < n:
        ch = text[i]
        if ch.isspace():
            start = i
            while i < n and text[i].isspace():
                i += 1
            toks.append(_Tok("space", text[start:i], start, i))
            continue
        if ch in "\"'":
            quote, start = ch, i
            i += 1
            while i < n and text[i] != quote:
                i += 1
            if i < n:
                i += 1
            toks.append(_Tok("string", text[start:i], start, i))
            continue
        if _is_ident_start(ch):
            start = i
            while i < n and _is_ident_char(text[i]):
                i += 1
            toks.append(_Tok("ident", text[start:i], start, i))
            continue
        if ch.isdigit():
            start = i
            while i < n and (text[i].isdigit() or text[i] == "."):
                i += 1
            toks.append(_Tok("int", text[start:i], start, i))
            continue
        two = text[i:i + 2]
        if two in ("==", "!=", "<=", ">="):
            toks.append(_Tok("op", two, i, i + 2))
            i += 2
            continue
        toks.append(_Tok("op", ch, i, i + 1))
        i += 1
    return toks


def tokenize_expression(text: str) -> list[dict]:
    """Segment one raw column-4/5 cell into the seam's four token classes, gapless over `text`.

    A skill-check run is `IDENT [relop] INT` -- `IDENT` not a keyword, not preceded by `.`, not
    followed by `(` or `.` -- carried with its parsed `skill`, `sexGate`, `relop` (and whether it
    was implicit) and `threshold`. Everything between/around the recognised runs, including
    whitespace, collapses into one contiguous `python-expression` segment so the corpus is not
    littered with single-character tokens the seam has no rule to interpret.
    """

    toks = _tokenize(text)
    segments: list[dict] = []
    pending_start: int | None = None

    def flush(end: int) -> None:
        nonlocal pending_start
        if pending_start is not None and end > pending_start:
            segments.append(
                {
                    "kind": TOKEN_PYTHON,
                    "offset": pending_start,
                    "length": end - pending_start,
                    "text": text[pending_start:end],
                }
            )
        pending_start = None

    i, n = 0, len(toks)
    significant = [t for t in toks if t.kind != "space"]
    sig_index_by_start = {t.start: idx for idx, t in enumerate(significant)}

    while i < n:
        tok = toks[i]
        if tok.kind == "space":
            if pending_start is None:
                pending_start = tok.start
            i += 1
            continue
        if tok.kind == "op" and tok.text in ("&", "|"):
            flush(tok.start)
            segments.append(
                {
                    "kind": TOKEN_JOIN,
                    "offset": tok.start,
                    "length": tok.end - tok.start,
                    "text": tok.text,
                    "symbol": tok.text,
                }
            )
            pending_start = tok.end
            i += 1
            continue
        if tok.kind == "op" and tok.text == ";":
            flush(tok.start)
            segments.append(
                {
                    "kind": TOKEN_SEPARATOR,
                    "offset": tok.start,
                    "length": tok.end - tok.start,
                    "text": tok.text,
                    "symbol": tok.text,
                }
            )
            pending_start = tok.end
            i += 1
            continue
        if tok.kind == "ident" and tok.text not in _KEYWORDS:
            sig_pos = sig_index_by_start.get(tok.start)
            prev_sig = significant[sig_pos - 1] if sig_pos is not None and sig_pos > 0 else None
            next_sig = (
                significant[sig_pos + 1]
                if sig_pos is not None and sig_pos + 1 < len(significant)
                else None
            )
            preceded_by_dot = bool(prev_sig and prev_sig.kind == "op" and prev_sig.text == ".")
            followed_by_call_or_member = bool(
                next_sig and next_sig.kind == "op" and next_sig.text in ("(", ".")
            )
            if not preceded_by_dot and not followed_by_call_or_member:
                relop_tok = None
                int_tok = None
                if next_sig is not None and next_sig.kind == "int":
                    int_tok = next_sig
                elif (
                    next_sig is not None
                    and next_sig.kind == "op"
                    and next_sig.text in _RELOPS
                    and sig_pos is not None
                    and sig_pos + 2 < len(significant)
                    and significant[sig_pos + 2].kind == "int"
                ):
                    relop_tok = next_sig
                    int_tok = significant[sig_pos + 2]
                if int_tok is not None:
                    flush(tok.start)
                    skill = tok.text
                    sex_gate = None
                    if skill.startswith("M_"):
                        sex_gate, skill = "male", skill[2:]
                    elif skill.startswith("F_"):
                        sex_gate, skill = "female", skill[2:]
                    segments.append(
                        {
                            "kind": TOKEN_SKILL_CHECK,
                            "offset": tok.start,
                            "length": int_tok.end - tok.start,
                            "text": text[tok.start:int_tok.end],
                            "skill": skill,
                            "sexGate": sex_gate,
                            "relop": relop_tok.text if relop_tok else ">=",
                            "implicitRelop": relop_tok is None,
                            "threshold": int_tok.text,
                        }
                    )
                    pending_start = int_tok.end
                    # Advance the raw token cursor past every token the run consumed.
                    i = n
                    for j, t in enumerate(toks):
                        if t.start >= int_tok.end:
                            i = j
                            break
                    continue
        if pending_start is None:
            pending_start = tok.start
        i += 1

    flush(len(text))
    return segments
