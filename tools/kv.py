"""Minimal Source KeyValues parser (the grammar shared by .res / .vmt / the
particle .txt files). Returns nested dicts; duplicate keys keep the last value,
except that repeated block keys are collected into a list.

Tokens are either "quoted" or bare (whitespace-delimited); `//` starts a line
comment; `{ }` nest. Keys are lowercased for stable lookup; values keep case.
"""
import re

_TOKEN = re.compile(r'"([^"]*)"|([^\s{}]+)|([{}])')


def _tokenize(text: str):
    for line in text.splitlines():
        # strip // comments (not inside quotes — VtMB data has no quoted //)
        q = line.find("//")
        if q >= 0 and line.count('"', 0, q) % 2 == 0:
            line = line[:q]
        for m in _TOKEN.finditer(line):
            quoted, bare, brace = m.groups()
            if brace:
                yield brace
            elif quoted is not None:
                yield quoted
            else:
                yield bare


def parse(text: str) -> dict:
    toks = list(_tokenize(text))
    pos = 0

    def parse_block():
        nonlocal pos
        d = {}
        while pos < len(toks):
            t = toks[pos]
            if t == "}":
                pos += 1
                break
            pos += 1
            key = t.lower()
            if pos < len(toks) and toks[pos] == "{":
                pos += 1
                val = parse_block()
            else:
                val = toks[pos] if pos < len(toks) else ""
                pos += 1
            if key in d:                      # repeated key -> list of blocks
                if not isinstance(d[key], list):
                    d[key] = [d[key]]
                d[key].append(val)
            else:
                d[key] = val
        return d

    # skip an optional leading root key ("GameMenu" { ... }); return the top map.
    if toks and toks[0] != "{" and pos + 1 < len(toks) and toks[1] == "{":
        pos = 2
        return parse_block()
    return parse_block()
