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
        # A CPython struct or callback: pointer-sized and opaque to Ghidra. `char *` and the
        # arithmetic types are kept exactly, because those are the ones that carry meaning.
        if kind in ("char",):
            return f"char {pointer}".strip()
        return "void *" if pointer else "int"
    return f"{kind} {pointer}".strip() if pointer else kind


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
    lines = [f"// CPython {source.name} public C API, extracted from Include/*.h",
             f"// {len(prototypes)} functions, {len(data)} data symbols"]
    lines += prototypes
    lines += [f"// data: {one}" for one in data]
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"{len(prototypes)} function prototypes, {len(data)} data symbols -> {out}")
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
