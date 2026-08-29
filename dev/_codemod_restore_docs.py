"""Throwaway: put the hoisted classes' docstrings back as comment headers.

Deleted once the migration lands. `_codemod_declass.py` drops the class docstring along with
the class, and some of those docstrings carry the reason the tests below them exist. This reads
the pre-hoist revision, and re-attaches each one above the first function that came out of that
class. A module-level `@staticmethod` left behind by the hoist is dropped at the same time.
"""

from __future__ import annotations

import ast
import subprocess
import sys

BASE = sys.argv[1]


def headers(path):
    """(first method name, docstring) for every hoisted class in the base revision."""
    show = subprocess.run(
        ["git", "show", BASE + ":" + path.replace("\\", "/")],
        capture_output=True, text=True, encoding="utf-8", check=True,
    )
    found = []
    for node in ast.parse(show.stdout).body:
        if not isinstance(node, ast.ClassDef):
            continue
        doc = ast.get_docstring(node)
        if not doc:
            continue
        for item in node.body:
            if isinstance(item, ast.FunctionDef):
                found.append((item.name, node.name, doc))
                break
    return found


def comment(name, doc, indent=""):
    lines = [indent + "# " + name.rstrip()]
    for line in doc.strip().split("\n"):
        lines.append((indent + "# " + line).rstrip())
    return lines


for path in sys.argv[2:]:
    with open(path, encoding="utf-8") as handle:
        lines = handle.read().split("\n")

    # A `@staticmethod` at column zero is a leftover of the hoist.
    lines = [line for line in lines if line != "@staticmethod"]

    tree = ast.parse("\n".join(lines))
    functions = {
        node.name: node.lineno - 1
        for node in tree.body
        if isinstance(node, ast.FunctionDef)
    }
    inserts = {}
    for method, class_name, doc in headers(path):
        start = functions.get(method)
        if start is None or lines[start - 1].lstrip().startswith("#"):
            continue
        inserts[start] = comment(class_name, doc) + [""]

    if not inserts:
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write("\n".join(lines))
        continue

    out = []
    for index, line in enumerate(lines):
        if index in inserts:
            out.extend(inserts[index])
        out.append(line)
    result = "\n".join(out)
    compile(result, path, "exec")
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(result)
    print("restored " + str(len(inserts)) + " header(s) in " + path)
