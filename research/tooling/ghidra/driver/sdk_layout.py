# -*- coding: utf-8 -*-
"""The Source SDK's class declarations, read as MSVC would lay out their primary vtables.

A vtable's slot order is the header's virtual order: a class's table is its primary base's
table, then every virtual it declares that overrides nothing, in declaration order. MSVC
groups the overloads of one name at the first one's position, in reverse declaration order.
That is the whole rule, and it is what lets a slot whose neighbours are named name itself.

Only the declarations are read -- names, parameter shapes, `virtual`, the macros that expand
to a virtual (`DECLARE_DATADESC`, `DECLARE_SERVERCLASS`) -- and the `.cpp` definition order,
which is the address order a translation unit's bodies are emitted in. No body is read.

The tree is third-party reference source under ``$ELYSIUM_WORK_ROOT/research``; nothing it
holds enters the checkout. The only game-DLL headers on disk are Source SDK 2013's, which
descends from the 2003 tree VtMB was built on and differs from it by insertions: the caller
treats a disagreement in gap length as "this stretch changed" and names nothing inside it.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

# Searched in order; the first root holding `game/server/baseentity.h` (or `dlls/`) wins.
SDK_CANDIDATES = (
    ("reference-source", "Bloodlines SDK"),
    ("sources", "source-sdk-2013", "src"),
)
HEADER_DIRS = ("public", "game/shared", "game/server", "dlls", "common")
# `#ifdef` names taken as defined. The server DLL, on Windows, release; no later game's flags.
DEFINED = frozenset({"GAME_DLL", "_WIN32", "WIN32"})

# The primary-base chain from the handle interface to the NPC, with the header each declares in.
NPC_CHAIN = ("IHandleEntity", "IServerUnknown", "IServerEntity", "CBaseEntity",
             "CBaseAnimating", "CBaseAnimatingOverlay", "CBaseFlex", "CBaseCombatCharacter",
             "CAI_BaseNPC")

# Macros that declare virtuals only the later tree has: VScript's script descriptor and the
# per-member `NetworkStateChanged_<name>` hooks. Expanded, they shift every slot after them
# against a 2003 table (and a token-pasted name reads as a member: `m_nNextThinkTick`).
LATER_MACROS = ("DECLARE_ENT_SCRIPTDESC", "CNetwork", "IMPLEMENT_NETWORK_VAR")

# One machine word on the stack.
WORD_TYPES = ("int", "char", "short", "long", "bool", "float", "byte", "uint", "int8", "int16",
              "int32", "uint8", "uint16", "uint32", "DWORD", "string_t", "HSCRIPT", "intp", "uintp",
              "unsignedint",
              "unsignedchar", "unsignedshort", "unsignedlong")
# Structures passed by value, in stack words.
BY_VALUE_WORDS = {"Vector": 3, "QAngle": 3, "AngularImpulse": 3, "RadianEuler": 3, "Vector2D": 2,
                  "Vector4D": 4, "Quaternion": 4, "variant_t": 5, "color32": 1, "Color": 1,
                  "EHANDLE": 1, "CBaseHandle": 1}

_TYPE_WORDS = frozenset({
    "const", "unsigned", "signed", "int", "char", "short", "long", "float", "double", "bool",
    "void", "struct", "class", "enum", "volatile", "inline", "static", "virtual", "explicit",
    "OVERRIDE", "override", "final", "FINAL"})


@dataclass
class Method:
    cls: str
    name: str
    params: str          # normalised parameter types, `,`-joined
    const: bool
    virtual: bool
    line: int
    ret: str = ""        # the declared return type, normalised like a parameter

    @property
    def key(self) -> tuple[str, str, bool]:
        return (self.name, self.params, self.const)


@dataclass
class Slot:
    name: str
    introduced: str      # the class whose declaration added the slot
    key: tuple[str, str, bool]
    owners: list[str] = field(default_factory=list)   # every class that declares it, base first
    ret: str = ""


def sdk_root(research: Path) -> Path | None:
    for parts in SDK_CANDIDATES:
        root = research.joinpath(*parts)
        if not root.is_dir():
            continue
        for sub in ("game/server/baseentity.h", "dlls/baseentity.h"):
            if (root / sub).is_file():
                return root
        for found in root.rglob("baseentity.h"):
            return found.parent.parent if found.parent.name == "server" else found.parent
    return None


def _strip(text: str) -> str:
    """Comments and literals out, line structure kept (so `#` directives stay on their lines)."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            chunk = text[i:(n if j < 0 else j + 2)]
            out.append("\n" * chunk.count("\n"))
            i = n if j < 0 else j + 2
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(c + c)
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def _condition(expr: str) -> bool:
    expr = expr.strip()
    expr = re.sub(r"defined\s*\(\s*(\w+)\s*\)", lambda m: "1" if m.group(1) in DEFINED else "0", expr)
    expr = re.sub(r"defined\s+(\w+)", lambda m: "1" if m.group(1) in DEFINED else "0", expr)
    expr = re.sub(r"\b[A-Za-z_]\w*\b", lambda m: "1" if m.group(0) in DEFINED else "0", expr)
    expr = expr.replace("&&", " and ").replace("||", " or ").replace("!", " not ")
    if not re.fullmatch(r"[\d\s()]*(?:(?:and|or|not|==|!=|<=|>=|<|>)[\d\s()]*)*", expr):
        return False
    try:
        return bool(eval(expr, {"__builtins__": {}}))  # noqa: S307 -- digits and and/or/not only
    except Exception:  # noqa: BLE001
        return False


def preprocess(text: str) -> tuple[list[tuple[int, str]], dict[str, tuple[list[str], str]]]:
    """Resolve `#if` blocks and collect `#define`s. Returns (line number, text) and the macros."""
    lines = _strip(text).split("\n")
    kept: list[tuple[int, str]] = []
    macros: dict[str, tuple[list[str], str]] = {}
    stack: list[tuple[bool, bool]] = []   # (this branch live, some branch already taken)
    live = True
    i = 0
    while i < len(lines):
        number = i + 1
        line = lines[i]
        while line.rstrip().endswith("\\") and i + 1 < len(lines):
            i += 1
            line = line.rstrip()[:-1] + " " + lines[i]
        i += 1
        stripped = line.strip()
        if not stripped.startswith("#"):
            if live:
                kept.append((number, line))
            continue
        directive = re.match(r"#\s*(\w+)\s*(.*)", stripped)
        if not directive:
            continue
        word, rest = directive.group(1), directive.group(2)
        if word in ("if", "ifdef", "ifndef"):
            if word == "ifdef":
                value = rest.split()[0] in DEFINED if rest.split() else False
            elif word == "ifndef":
                value = rest.split()[0] not in DEFINED if rest.split() else True
            else:
                value = _condition(rest)
            stack.append((live, value))
            live = live and value
        elif word == "elif" and stack:
            outer, taken = stack[-1]
            value = not taken and _condition(rest)
            stack[-1] = (outer, taken or value)
            live = outer and value
        elif word == "else" and stack:
            outer, taken = stack[-1]
            stack[-1] = (outer, True)
            live = outer and not taken
        elif word == "endif" and stack:
            live = stack.pop()[0]
        elif word == "define" and live:
            m = re.match(r"(\w+)(\(([^)]*)\))?\s*(.*)", rest)
            if m and m.group(2):   # function-like only: an object-like macro declares nothing here
                params = [p.strip() for p in (m.group(3) or "").split(",") if p.strip()]
                macros[m.group(1)] = (params, m.group(4))
    return kept, macros


def virtual_macros(macros: dict[str, tuple[list[str], str]]) -> set[str]:
    """The macros whose expansion declares a virtual, directly or through another macro."""
    words = {name: set(re.findall(r"[A-Za-z_]\w*", body)) for name, (_, body) in macros.items()
             if not name.startswith(LATER_MACROS)}
    found = {name for name, tokens in words.items() if "virtual" in tokens}
    while True:
        more = {name for name, tokens in words.items() if name not in found and tokens & found}
        if not more:
            return found
        found |= more


def _expand(text: str, macros: dict[str, tuple[list[str], str]], wanted: set[str],
            depth: int = 0) -> str:
    """Expand the function-like macros in `wanted`: the ones that declare a virtual."""
    present = [name for name in wanted if name in text]
    if depth > 4 or not present:
        return text
    pattern = re.compile(r"\b(" + "|".join(map(re.escape, present)) + r")\s*\(([^()]*)\)")

    def replace(m: re.Match) -> str:
        params, body = macros[m.group(1)]
        args = [a.strip() for a in m.group(2).split(",")] if m.group(2).strip() else []
        for p, a in zip(params, args):
            body = re.sub(rf"\b{re.escape(p)}\b", a, body)
        for p in params[len(args):]:
            body = re.sub(rf"\b{re.escape(p)}\b", "", body)
        return " " + body + " ; "

    expanded = pattern.sub(replace, text)
    # An invocation of a macro that declares no virtual (`DECLARE_SIMPLE_DATADESC()` inside
    # `DECLARE_DATADESC`) ends a declaration: left in place it would read as the declarator of
    # the virtual that follows it.
    expanded = re.sub(r"\b([A-Z_][A-Z0-9_]*)\s*\([^()]*\)",
                      lambda m: " ; " if m.group(1) in macros and m.group(1) not in wanted
                      else m.group(0), expanded)
    return expanded if expanded == text else _expand(expanded, macros, wanted, depth + 1)


def _match_brace(text: str, open_at: int) -> int:
    depth = 0
    for j in range(open_at, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return j
    return len(text)


def _normalise_params(inside: str) -> str:
    parts, depth, cur = [], 0, []
    for c in inside:
        if c in "(<[":
            depth += 1
        elif c in ")>]":
            depth -= 1
        if c == "," and depth == 0:
            parts.append("".join(cur))
            cur = []
        else:
            cur.append(c)
    parts.append("".join(cur))
    out = []
    for p in parts:
        p = p.split("=")[0].strip()
        if not p or p == "void":
            continue
        tokens = re.findall(r"[A-Za-z_]\w*(?:::\w+)*|[*&]|\[\]|\.\.\.", p)
        if len(tokens) >= 2 and re.fullmatch(r"[A-Za-z_]\w*", tokens[-1]) \
                and tokens[-1] not in _TYPE_WORDS and tokens[-2] not in ("struct", "class", "enum"):
            tokens = tokens[:-1]
        out.append("".join(t for t in tokens if t not in ("const", "struct", "class", "enum")))
    return ",".join(out)


_DECL_NAME = re.compile(r"(~?[A-Za-z_]\w*|operator\s*[^\s(]+)\s*\($")


def class_methods(text: str, cls: str,
                  known: dict[str, tuple[list[str], str]] | None = None) -> tuple[str | None, list[Method]]:
    """The primary base and the member functions `cls` declares, in declaration order.

    `known` is the tree's macro table: `DECLARE_DATADESC` lives in `datamap.h`, not beside its use.
    """
    kept, local = preprocess(text)
    macros = {**(known or {}), **local}
    wanted = virtual_macros(macros)
    joined = "\n".join(line for _, line in kept)
    numbers = []
    for number, line in kept:
        numbers.extend([number] * (len(line) + 1))
    head = re.search(rf"\b(?:class|struct|abstract_class)\s+{re.escape(cls)}\s*(:[^;{{]*)?\{{", joined)
    if not head:
        return None, []
    base = None
    if head.group(1):
        first = re.search(r"^\s*(?:(?:public|protected|private|virtual)\s+)*([A-Za-z_]\w*)",
                          head.group(1)[1:])
        base = first.group(1) if first else None
    open_at = head.end() - 1
    close = _match_brace(joined, open_at)
    body_start = open_at + 1
    body = joined[body_start:close]
    line_of = lambda offset: numbers[min(body_start + offset, len(numbers) - 1)]  # noqa: E731

    methods: list[Method] = []
    statement: list[str] = []
    stmt_start = 0
    i = 0
    while i < len(body):
        c = body[i]
        if c == "{":
            text_so_far = "".join(statement)
            end = _match_brace(body, i)
            if "(" in text_so_far and not re.match(r"\s*(struct|class|union|enum)\b", text_so_far):
                _record(cls, text_so_far, line_of(stmt_start), macros, wanted, methods)
                statement, i = [], end + 1
                stmt_start = i
                continue
            # a nested type: skip through its closing `;`
            semi = body.find(";", end)
            statement, i = [], (semi + 1 if semi >= 0 else end + 1)
            stmt_start = i
            continue
        if c == ";":
            _record(cls, "".join(statement), line_of(stmt_start), macros, wanted, methods)
            statement, i = [], i + 1
            stmt_start = i
            continue
        if not statement and c.isspace():
            stmt_start = i + 1
        statement.append(c)
        i += 1
    return base, methods


def _record(cls: str, statement: str, line: int, macros: dict, wanted: set[str],
            methods: list[Method]) -> None:
    statement = re.sub(r"\b(public|protected|private)\s*:", " ", statement)
    expanded = _expand(statement, macros, wanted)
    for piece in expanded.split(";"):
        piece = piece.strip()
        if "(" not in piece or piece.startswith(("typedef", "friend", "template", "using", "return")):
            continue
        # Find the declarator: the identifier before the first top-level `(`.
        depth = 0
        cut = -1
        for j, c in enumerate(piece):
            if c == "(" and depth == 0:
                if _DECL_NAME.search(piece[:j + 1]):
                    cut = j
                    break
            if c in "<":
                depth += 1
            elif c in ">":
                depth -= 1
        if cut < 0:
            continue
        name = _DECL_NAME.search(piece[:cut + 1]).group(1).replace(" ", "")
        prefix = piece[:cut - len(name)] if piece[:cut].rstrip().endswith(name) else piece[:cut]
        if re.fullmatch(r"[A-Z_][A-Z0-9_]*", name) and not re.search(r"\w", prefix.replace("virtual", "")):
            continue   # an unexpanded macro invocation
        close = _match_paren(piece, cut)
        tail = piece[close + 1:]
        if "=" in tail and not re.search(r"=\s*0\b", tail) and "(" not in prefix:
            # `int x = f(y)`: a member with an initialiser, not a function
            continue
        if name == cls or name.startswith("m_"):
            continue   # a constructor, or a member a token-pasting macro left behind
        methods.append(Method(cls, name if not name.startswith("~") else "~",
                              _normalise_params(piece[cut + 1:close]) if not name.startswith("~") else "",
                              bool(re.match(r"\s*const\b", tail)),
                              bool(re.search(r"\bvirtual\b", prefix)), line,
                              _normalise_params(re.sub(r"\b(virtual|inline|static|explicit)\b",
                                                       " ", prefix))))


def _match_paren(text: str, open_at: int) -> int:
    depth = 0
    for j in range(open_at, len(text)):
        if text[j] == "(":
            depth += 1
        elif text[j] == ")":
            depth -= 1
            if depth == 0:
                return j
    return len(text) - 1


class Sdk:
    """The SDK tree, indexed by class declaration."""

    def __init__(self, root: Path):
        self.root = root
        self._decl: dict[str, Path] = {}
        self._text: dict[Path, str] = {}
        self.macros: dict[str, tuple[list[str], str]] = {}
        self._scalars: set[str] | None = None
        pattern = re.compile(r"^\s*(?:class|struct|abstract_class)\s+([A-Za-z_]\w*)\s*(?::[^;{]*)?\{?\s*$",
                             re.M)
        for sub in HEADER_DIRS:
            base = root / sub
            if not base.is_dir():
                continue
            for path in sorted(base.rglob("*.h")):
                text = path.read_text(encoding="utf-8", errors="replace")
                for m in pattern.finditer(text):
                    self._decl.setdefault(m.group(1), path)
                if "#define" in text:
                    for name, macro in preprocess(text)[1].items():
                        self.macros.setdefault(name, macro)

    def scalars(self) -> set[str]:
        """Type names that pass and return in one machine word: enums and typedefs of builtins."""
        if self._scalars is None:
            found = set(WORD_TYPES)
            enum = re.compile(r"\benum\s+(\w+)\s*\{")
            typedef_enum = re.compile(r"typedef\s+enum\s*\w*\s*\{[^}]*\}\s*(\w+)\s*;", re.S)
            typedef = re.compile(r"typedef\s+(?:unsigned\s+|signed\s+|const\s+)*"
                                 r"(?:int|short|char|long|bool|float|byte|uint\w*|int\d+|DWORD|"
                                 r"intp|uintp|[A-Za-z_]\w*\s*\*)\s+(\w+)\s*;")
            pointer_typedef = re.compile(r"typedef\s+[\w\s*]+\(\s*[\w\s]*\*\s*(\w+)\s*\)\s*\(")
            for sub in HEADER_DIRS:
                base = self.root / sub
                if not base.is_dir():
                    continue
                for path in base.rglob("*.h"):
                    text = path.read_text(encoding="utf-8", errors="replace")
                    found.update(enum.findall(text))
                    found.update(typedef_enum.findall(text))
                    found.update(typedef.findall(text))
                    found.update(pointer_typedef.findall(text))
            self._scalars = found
        return self._scalars

    def stack_words(self, params: str, ret: str) -> int | None:
        """Stack words a `__thiscall` body pops (`RET 4n`), or None where a type's size is unknown:
        by-value parameters by their size, plus one for a class returned by value (MSVC passes the
        return slot as a hidden first argument)."""
        scalars = self.scalars()
        total = 0
        for param in [p for p in params.split(",") if p]:
            if param == "...":
                return None
            if param.endswith(("*", "&", "[]")):
                total += 1
                continue
            bare = re.sub(r"^(unsigned|signed)", "", param) or "int"
            if bare in ("double", "int64", "longlong", "uint64", "__int64"):
                total += 2
            elif bare in BY_VALUE_WORDS:
                total += BY_VALUE_WORDS[bare]
            elif bare in scalars or bare.startswith(("CHandle<", "EHANDLE")):
                total += 1
            else:
                return None
        if ret and not ret.endswith(("*", "&")) and ret not in ("void", "bool", "double", "float") \
                and ret not in scalars:
            if ret in BY_VALUE_WORDS or ret.startswith(("CHandle<", "EHANDLE", "CBaseHandle")):
                total += 1
            else:
                return None
        return total

    def header(self, cls: str) -> Path | None:
        return self._decl.get(cls)

    def methods(self, cls: str) -> tuple[str | None, list[Method]]:
        path = self._decl.get(cls)
        if path is None:
            return None, []
        text = self._text.setdefault(path, path.read_text(encoding="utf-8", errors="replace"))
        return class_methods(text, cls, self.macros)

    def vtable(self, chain: tuple[str, ...] = NPC_CHAIN) -> tuple[list[Slot], dict[str, int]]:
        """The flattened primary vtable down `chain`, and each class's table length."""
        slots: list[Slot] = []
        ends: dict[str, int] = {}
        for cls in chain:
            _, methods = self.methods(cls)
            by_key = {s.key: s for s in slots}
            by_name: dict[str, list[Slot]] = {}
            for s in slots:
                by_name.setdefault(s.name, []).append(s)
            fresh: list[Method] = []
            for m in methods:
                hit = by_key.get(m.key)
                if hit is None and m.name == "~":
                    hit = by_name.get("~", [None])[0]
                if hit is None and m.virtual:
                    # A redeclaration whose parameter spelling differs from the base's still
                    # overrides it when the base has exactly one virtual of that name and arity.
                    same = [s for s in by_name.get(m.name, [])
                            if s.key[1].count(",") == m.params.count(",")
                            and bool(s.key[1]) == bool(m.params)]
                    hit = same[0] if len(same) == 1 and len(by_name.get(m.name, [])) == 1 else None
                if hit is not None:
                    if cls not in hit.owners:
                        hit.owners.append(cls)
                    continue
                if m.virtual and all(f.key != m.key for f in fresh):
                    fresh.append(m)
            # MSVC: overloads of one name sit together at the first one's position, reversed.
            ordered: list[Method] = []
            done: set[str] = set()
            for m in fresh:
                if m.name in done:
                    continue
                done.add(m.name)
                ordered.extend(reversed([o for o in fresh if o.name == m.name]))
            for m in ordered:
                slots.append(Slot(m.name, cls, m.key, [cls], m.ret))
            ends[cls] = len(slots)
        return slots, ends

    def definitions(self, source: Path) -> list[tuple[str, str, int, int | None]]:
        """`Class::Method` definitions in a `.cpp`, in file order: (class, method, line, stack
        words the definition's signature pops, None when a type's size is unknown)."""
        text = source.read_text(encoding="utf-8", errors="replace")
        kept, _ = preprocess(text)
        out = []
        depth = 0
        rx = re.compile(r"(?:^|[\s*&])([A-Za-z_]\w*)::(~?[A-Za-z_]\w*|operator\s*\S+?)\s*\(")
        previous = ""
        for k, (number, line) in enumerate(kept):
            if depth == 0:
                m = rx.search(line)
                if m and not line.lstrip().startswith(("return", "if", "else", "#")) \
                        and "=" not in line[:m.start()] and ";" not in line[m.end():].split(")")[-1]:
                    signature = " ".join(one for _, one in kept[k:k + 6])
                    open_at = signature.find("(", signature.find(m.group(2), m.start()))
                    close = _match_paren(signature, open_at)
                    ret = line[:m.start(1)].strip() or previous.strip()
                    ret = _normalise_params(re.sub(r"\b(inline|static|virtual)\b", " ", ret))
                    words = self.stack_words(_normalise_params(signature[open_at + 1:close]), ret) \
                        if not m.group(2).startswith("~") and m.group(1) != m.group(2) else None
                    out.append((m.group(1), m.group(2).replace(" ", ""), number, words))
            depth += line.count("{") - line.count("}")
            depth = max(depth, 0)
            if line.strip():
                previous = line if depth == 0 else ""
        return out

    def find_source(self, name: str) -> Path | None:
        lowered = name.lower()
        for sub in ("game/server", "game/shared", "dlls"):
            base = self.root / sub
            if not base.is_dir():
                continue
            for path in base.rglob("*.cpp"):
                if path.name.lower() == lowered:
                    return path
        return None
