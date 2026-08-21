#!/usr/bin/env python3
"""Extract the CPython 2.1 C API from its own headers, for Ghidra to apply.

`vampire_python21.dll` is CPython **2.1.2** — it says so in its own version string, and 648 of
its 653 exports appear verbatim in the 2.1.2 source tree. That makes the released source an
exact oracle for every call `vampire.dll` and `engine.dll` make into it, and Ghidra imports
those calls with no signature at all.

The cost of no signature is not cosmetic. `PyArg_ParseTuple(PyObject *, char *, ...)` takes the
**format string** that states a script-API function's argument list, and with the parameter
untyped Ghidra leaves the operand as `&DAT_10590cb8` instead of `"Osfffff"`. Typing the import
is what turns 48 opaque data references into the declared signature of the script API.

This reads the headers and writes one prototype per line, reduced to a small closed vocabulary
of C types; `ApplyPythonApi.java` maps each and applies it to the matching external function.
Nothing here guesses: a declaration the headers do not carry, or a type outside the
vocabulary, is reported and skipped rather than defaulted.

Run through the CLI, which loads the local environment:

    uv run elysium research corpus pyapi
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "pipeline" / "src"))

from elysium_pipeline.paths import research_root  # noqa: E402

# `extern DL_IMPORT(int) PyArg_ParseTuple(PyObject *, char *, ...);` -- the whole declaration,
# across the line breaks the headers wrap long ones on.
DECLARATION = re.compile(
    r"\bDL_IMPORT\s*\(\s*([^)]*?)\s*\)\s*(\**)\s*([A-Za-z_]\w*)\s*\(([^;]*?)\)\s*;",
    re.DOTALL)

# A declaration of a variable rather than a function: `DL_IMPORT(char *) _Py_PackageContext;`.
DATUM = re.compile(r"\bDL_IMPORT\s*\(\s*([^)]*?)\s*\)\s*(\**)\s*([A-Za-z_]\w*)\s*;")

# The structs worth having as real Ghidra types. Everything else in the C API is reached
# through `PyObject *`, so these are the ones that turn `*(int *)(param_1 + 8)` into
# `param_1->ob_type` in the bridge's own bodies.
STRUCTS = ("object", "methodobject", "classobject", "stringobject", "intobject",
           "floatobject", "tupleobject", "listobject", "frameobject", "funcobject")

# Both spellings the headers use: `typedef struct <tag> { ... } Name;` and a bare
# `struct Name { ... };` (which is how `PyMethodDef` is declared).
DEFINITION = re.compile(r"typedef\s+struct\s*(\w*)\s*\{(.*?)\}\s*(\w+)\s*;", re.DOTALL)
BARE = re.compile(r"(?<!typedef )\bstruct\s+(\w+)\s*\{(.*?)\}\s*;", re.DOTALL)

# Which build the shipped DLL is. Every one of these changes a STRUCT LAYOUT, so guessing is
# not an option -- each is settled against the binary rather than assumed:
#
#   Py_TRACE_REFS   `#define`d only inside `#ifdef Py_DEBUG`, and a trace-refs build must export
#                   `_Py_NewReference` / `_Py_ForgetReference` / `_Py_Dealloc`. The DLL exports
#                   none of them, so the object header is the two-word form and PyObject is 8
#                   bytes, not 16.
#   COUNT_ALLOCS    never `#define`d, and an allocation-counting build would export its
#                   counters. It does not, so PyTypeObject stops at `tp_weaklistoffset` --
#                   `tp_alloc`, `tp_free`, `tp_maxalloc` and `tp_next` are not in the object.
#   CACHE_HASH,     `#define`d unconditionally at the top of `stringobject.h`, so `ob_shash`
#   INTERN_STRINGS  and `ob_sinterned` ARE in PyStringObject.
#
# A conditional this does not know about makes the struct unusable rather than approximate: the
# member count is the layout, and one dropped member shifts every offset after it.
DEFINED = {"CACHE_HASH", "INTERN_STRINGS"}
UNDEFINED = {"Py_TRACE_REFS", "Py_DEBUG", "Py_REF_DEBUG", "COUNT_ALLOCS", "USE_STACKCHECK",
             "HAVE_LIMITS_H", "MS_WIN32", "macintosh"}


class Undecidable(Exception):
    """A struct body asks about a macro whose state this does not know."""


def _resolve_conditionals(body: str) -> str:
    """Drop the members the shipped build does not compile, keep the ones it does."""
    keeping = [True]
    out: list[str] = []
    for line in body.splitlines():
        stripped = line.strip()
        if stripped.startswith("#"):
            parts = stripped.split()
            directive = parts[0]
            macro = parts[1] if len(parts) > 1 else ""
            if directive in ("#ifdef", "#ifndef", "#if"):
                if macro in DEFINED:
                    on = directive != "#ifndef"
                elif macro in UNDEFINED:
                    on = directive == "#ifndef"
                else:
                    raise Undecidable(stripped)
                keeping.append(keeping[-1] and on)
            elif directive == "#else":
                outer = keeping[-2] if len(keeping) > 1 else True
                keeping[-1] = outer and not keeping[-1]
            elif directive == "#endif":
                if len(keeping) > 1:
                    keeping.pop()
            continue
        if keeping[-1]:
            out.append(line)
    return "\n".join(out)


# `PyObject_HEAD` is defined twice, under `Py_TRACE_REFS` and not. The shipped DLL is a release
# build -- it exports none of the trace-refs machinery -- so the head is the two-word form, and
# resolving that here is the point: a header that answers conditionally cannot be forwarded to
# Ghidra, which has no preprocessor.
HEAD = "int ob_refcnt; struct _typeobject *ob_type;"
# Struct tags the headers declare under one name and typedef under another.
TAGS = {"_typeobject": "PyTypeObject", "_object": "PyObject", "_frame": "PyFrameObject",
        "_ts": "PyThreadState", "_is": "PyInterpreterState"}
VAR_HEAD = HEAD + " int ob_size;"

# Types the headers use that Ghidra has no definition for. Every one is a function pointer or a
# CPython struct; as a parameter each is pointer-sized, and the point of the signature is the
# ARITY and which slots are `char *`, so the opaque ones become `void *` rather than being
# dropped. A parameter dropped would silently change the arity, which is the one thing this
# pass exists to get right.
OPAQUE = re.compile(r"^(?:Py|_Py)\w*(?:\s*\*+)?$|^(?:va_list|FILE|struct \w+)")


COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)


def _flatten(text: str) -> str:
    # The headers annotate parameters inline -- `/* encoding */ const char *` -- and a comment
    # left in place reads as three more pointer levels.
    return re.sub(r"\s+", " ", COMMENT.sub(" ", text)).strip()


def _parameter(text: str) -> str | None:
    """One parameter, reduced to something Ghidra's C parser will accept."""
    text = _flatten(text)
    if not text or text == "void":
        return None
    if text == "...":
        return "..."
    # Drop the parameter's own name when the header supplies one: `char *name` -> `char *`.
    stars = text.count("*")
    bare = _flatten(text.replace("*", " "))
    words = bare.split()
    # `unsigned long x` -> keep the type words, drop a trailing identifier that is not a type.
    words = [one for one in words if one not in ("register", "volatile")]
    KEYWORDS = {"const", "unsigned", "signed", "long", "short", "int", "char", "float",
                "double", "void", "struct", "size_t"}
    while len(words) > 1 and words[-1] not in KEYWORDS and not words[-1].startswith("Py"):
        words.pop()
    kind = " ".join(words)
    if kind.startswith("const "):
        kind = kind[len("const "):]
    pointer = "*" * stars
    if OPAQUE.match(kind + (" " + pointer if pointer else "")) or OPAQUE.match(kind):
        # A CPython type. The named ones are emitted verbatim, because `ApplyPythonApi` builds
        # them as real structures first and the whole point of a `PyObject *` parameter is that
        # the decompiler then propagates it into the body -- `*(int *)(param_1 + 4)` becomes
        # `param_1->ob_type`. Anything else is a callback typedef or a parser type: pointer-sized
        # and carrying no layout, so it stays opaque rather than being invented.
        if kind in ("char",):
            return f"char {pointer}".strip()
        if pointer and re.fullmatch(r"Py\w+", kind):
            return f"{kind} {pointer}"
        if kind.startswith("struct"):
            return "void *" if pointer else "int"
        return "void *" if pointer else "int"
    return f"{kind} {pointer}".strip() if pointer else kind


def _members(body: str) -> list[tuple[str, str]]:
    """(type, name) for each member of a struct body.

    Split on `;`, not by line. Expanding `PyObject_HEAD` puts two declarations on one line, and
    a line-anchored match silently drops the object header -- which is the half every one of
    these structs shares and the part every `->ob_type` read goes through.
    """
    body = _resolve_conditionals(body)
    body = body.replace("PyObject_VAR_HEAD", VAR_HEAD).replace("PyObject_HEAD", HEAD)
    # `struct _typeobject *ob_type` names the TAG; the emitted struct carries the typedef name.
    # Left alone the two-word type splits wrong and the member is dropped without a word said.
    body = re.sub(r"\bstruct\s+(\w+)", lambda one: TAGS.get(one.group(1), one.group(1)), body)
    fields: list[tuple[str, str]] = []
    for statement in body.split(";"):
        statement = _flatten(statement)
        if not statement or statement.startswith("#"):
            continue
        words = statement.replace("*", " * ").split()
        # The type is every word up to the first declarator; a declarator is the last word of
        # each comma-separated group.
        pieces = " ".join(words).split(",")
        head = pieces[0].split()
        if len(head) < 2:
            continue
        kind_words: list[str] = []
        for word in head:
            if word == "*" or not re.fullmatch(r"[A-Za-z_]\w*", word):
                break
            kind_words.append(word)
        kind = " ".join(kind_words[:-1]) if len(kind_words) == len(head) else " ".join(kind_words)
        if not kind or kind in ("typedef", "return", "extern", "union", "enum"):
            continue
        for piece in pieces:
            tokens = piece.split()
            stars = tokens.count("*")
            array = ""
            rest = [one for one in tokens if one != "*"]
            if kind_words and rest and rest[0] in kind_words:
                rest = rest[len(kind_words):] or rest[-1:]
            if not rest:
                continue
            name = rest[-1]
            if "[" in name:
                array = name[name.index("["):]
                name = name[:name.index("[")]
            if not name.isidentifier():
                continue
            fields.append((f"{kind}{' ' + '*' * stars if stars else ''}", name + array))
    return fields


CONSTANT = re.compile(r"^\s*#\s*define\s+([A-Za-z_]\w*)\s+(\d+)\s*(?:/\*.*)?$", re.MULTILINE)


def _constants(source: Path) -> dict[str, str]:
    """Integer `#define`s across the headers, for the array bounds that use them.

    `PyFrameObject` sizes a member `[CO_MAXBLOCKS]`, and a bound left unresolved is not a
    smaller struct -- it is a struct whose every later offset is wrong.
    """
    found: dict[str, str] = {}
    for header in sorted((source / "Include").glob("*.h")):
        text = header.read_text(encoding="latin-1")
        for name, value in CONSTANT.findall(text):
            found.setdefault(name, value)
    return found


def structs(source: Path) -> list[str]:
    """Each struct as `struct <Name> { <type> <field>; ... }` on one line.

    Emitted flat and in the headers' own order, because `ApplyPythonApi` builds them in that
    order and a member typed as a struct declared later would resolve to nothing.
    """
    out: list[str] = []
    seen: set[str] = set()
    numbers = _constants(source)
    for stem in STRUCTS:
        header = source / "Include" / f"{stem}.h"
        if not header.is_file():
            continue
        text = COMMENT.sub(" ", header.read_text(encoding="latin-1"))
        found = [(name, body) for tag, body, name in DEFINITION.findall(text)]
        found += [(name, body) for name, body in BARE.findall(text)]
        for name, body in found:
            if name in seen or not body.strip():
                continue
            body = re.sub(r"\[\s*([A-Za-z_]\w*)\s*\]",
                          lambda one: f"[{numbers[one.group(1)]}]"
                          if one.group(1) in numbers else one.group(0), body)
            try:
                if re.search(r"\[\s*[A-Za-z_]", body):
                    raise Undecidable(re.search(r"\[\s*[A-Za-z_]\w*\s*\]", body).group(0))
                fields = _members(body)
            except Undecidable as error:
                print(f"  {name}: skipped — its layout depends on `{error}`, whose state "
                      f"this does not know; a dropped member shifts every offset after it",
                      file=sys.stderr)
                seen.add(name)
                continue
            if not fields:
                continue
            seen.add(name)
            rendered = " ".join(f"{kind} {field};" for kind, field in fields)
            out.append(f"struct {name} {{ {rendered} }}")
    return out


def extract(source: Path) -> tuple[list[str], list[str], list[str]]:
    """(prototypes, data symbols, skipped) from every header in the tree."""
    prototypes: dict[str, str] = {}
    data: dict[str, str] = {}
    skipped: list[str] = []
    headers = sorted((source / "Include").glob("*.h"))
    if not headers:
        raise FileNotFoundError(f"no CPython headers under {source / 'Include'}")
    for header in headers:
        text = COMMENT.sub(" ", header.read_text(encoding="latin-1"))
        for returns, stars, name, arguments in DECLARATION.findall(text):
            parameters = []
            broken = False
            for one in re.split(r",(?![^(]*\))", arguments):
                rendered = _parameter(one)
                if rendered is None and _flatten(one) not in ("", "void"):
                    broken = True
                    break
                if rendered is not None:
                    parameters.append(rendered)
            if broken:
                skipped.append(f"{name}  (a parameter did not reduce: {_flatten(arguments)})")
                continue
            result = _parameter(returns + " " + stars) or "void"
            prototypes[name] = f"{result} {name}({', '.join(parameters) or 'void'});"
        for returns, stars, name in DATUM.findall(text):
            if name not in prototypes:
                data[name] = _flatten(returns + " " + stars)
    return ([prototypes[k] for k in sorted(prototypes)],
            [f"{data[k]} {k};" for k in sorted(data)], skipped)


def main() -> int:
    parser = argparse.ArgumentParser(prog="pyapi", description=__doc__)
    parser.add_argument("--source", type=Path, default=None,
                        help="the CPython 2.1.2 source tree (default: under reference-source)")
    parser.add_argument("--out", type=Path, default=None)
    args = parser.parse_args()

    source = args.source or (research_root() / "reference-source" / "Python-2.1.2")
    if not source.is_dir():
        print(f"no CPython source at {source}\n"
              f"fetch Python-2.1.2.tgz from python.org and unpack it there; it is the exact "
              f"version vampire_python21.dll reports", file=sys.stderr)
        return 1
    out = args.out or (research_root() / "ghidra" / "corpus" / "python21-api.txt")
    prototypes, data, skipped = extract(source)
    out.parent.mkdir(parents=True, exist_ok=True)
    definitions = structs(source)
    lines = [f"// CPython {source.name} public C API, extracted from Include/*.h",
             f"// {len(definitions)} structs, {len(prototypes)} functions, "
             f"{len(data)} data symbols"]
    lines += definitions
    lines += prototypes
    lines += [f"// data: {one}" for one in data]
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"{len(definitions)} structs, {len(prototypes)} function prototypes, "
          f"{len(data)} data symbols -> {out}")
    for one in definitions[:4]:
        print(f"  {one[:150]}")
    if skipped:
        print(f"{len(skipped)} declaration(s) skipped:")
        for one in skipped[:20]:
            print(f"  {one}")
    for probe in ("PyArg_ParseTuple", "Py_InitModule4", "PyObject_GetAttrString",
                  "PyDict_SetItemString", "Py_BuildValue"):
        found = next((one for one in prototypes if re.search(rf"\b{probe}\(", one)), None)
        print(f"  {found or f'{probe}: NOT FOUND'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
