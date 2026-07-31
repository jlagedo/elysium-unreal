"""Minimal Source KeyValues parser (the grammar shared by .res / .vmt / the
particle .txt files and the `vdata/` rulebook). Returns nested dicts; duplicate
keys keep the last value, except that repeated block keys are collected into a
list.

Tokens are either "quoted" or bare (whitespace-delimited); `//` outside a quoted
string starts a comment that runs to end of line; `{ }` nest. Keys are lowercased
for stable lookup; values keep case.

A quoted string may span lines and may carry `\\"` — `clandoc000.txt` uses both
(a `ShortDescription` that opens with a newline, a description quoting the word
"insight"). Scanning is therefore character-wise with quote state carried across
lines, not line-by-line: a stray quote shifts every following key/value pair by
one, which turns the next `{` into a value and collapses the block nesting.
"""

_BARE_END = set(' \t\r\n{}"')


def _tokenize(text: str):
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c in " \t\r\n":
            i += 1
        elif c == "/" and text[i + 1:i + 2] == "/":
            j = text.find("\n", i)
            i = n if j < 0 else j + 1
        elif c == '"':
            i += 1
            out = []
            while i < n and text[i] != '"':
                if text[i] == "\\" and text[i + 1:i + 2] == '"':
                    out.append('"')
                    i += 2
                else:
                    out.append(text[i])
                    i += 1
            i += 1                            # past the closing quote (or EOF)
            yield "".join(out)
        elif c in "{}":
            yield c
            i += 1
        else:
            j = i
            while j < n and text[j] not in _BARE_END and text[j:j + 2] != "//":
                j += 1
            yield text[i:j]
            i = j


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
