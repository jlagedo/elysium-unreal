"""Throwaway codemod: turn the scratch-directory `setUp` into pytest's `tmp_path`.

Deleted once the migration lands. Nineteen classes open a `TemporaryDirectory` in `setUp` and
clean it up through `addCleanup`, which is what `tmp_path` already is. Tests take `tmp_path`;
the helper methods that used the attribute take the directory as their first argument, and
their call sites are given it.

`_codemod_declass.py` hoists the resulting class afterwards.
"""

from __future__ import annotations

import ast
import re
import sys

SCRATCH = re.compile(
    r"^\s*self\.(?P<scratch>\w+) = tempfile\.TemporaryDirectory\(\)\n"
    r"\s*self\.(?P<root>\w+) = Path\(self\.(?P=scratch)\.name\)\n"
    r"\s*self\.addCleanup\(self\.(?P=scratch)\.cleanup\)\s*$"
)


def scratch_root(lines, node):
    """The attribute name a scratch-directory `setUp` binds, or None."""
    body = "\n".join(lines[node.body[0].lineno - 1:node.end_lineno])
    match = SCRATCH.match(body)
    return match.group("root") if match else None


def uses(node, root):
    for sub in ast.walk(node):
        if isinstance(sub, ast.Attribute) and isinstance(sub.value, ast.Name):
            if sub.value.id == "self" and sub.attr == root:
                return True
    return False


def add_parameter(text, parameter):
    """Insert a first parameter into a `def name(self, ...)` line."""
    if re.search(r"\(\s*self\s*\)", text):
        return re.sub(r"\(\s*self\s*\)", "(self, " + parameter + ")", text, count=1)
    return re.sub(r"\(\s*self\s*,", "(self, " + parameter + ",", text, count=1)


def convert(source):
    tree = ast.parse(source)
    lines = source.split("\n")
    drop = set()
    edits = {}  # line index -> new text
    converted = 0

    for node in tree.body:
        if not isinstance(node, ast.ClassDef):
            continue
        methods = [item for item in node.body if isinstance(item, ast.FunctionDef)]
        setup = next((m for m in methods if m.name == "setUp"), None)
        if setup is None or any(m.name == "tearDown" for m in methods):
            continue
        root = scratch_root(lines, setup)
        if root is None:
            continue

        # Helpers that read the scratch directory take it as an argument; so, transitively,
        # do the helpers that call them.
        threaded = {m.name for m in methods if not m.name.startswith("test") and uses(m, root)}
        for _ in range(len(methods)):
            for method in methods:
                if method.name in threaded or method.name.startswith("test"):
                    continue
                calls = {
                    sub.func.attr
                    for sub in ast.walk(method)
                    if isinstance(sub, ast.Call)
                    and isinstance(sub.func, ast.Attribute)
                    and isinstance(sub.func.value, ast.Name)
                    and sub.func.value.id == "self"
                }
                if calls & threaded:
                    threaded.add(method.name)

        for index in range(setup.lineno - 1, setup.end_lineno):
            drop.add(index)
        drop.add(setup.end_lineno)  # the blank line the method is followed by

        call = re.compile(r"\bself\.(" + "|".join(threaded) + r")\(") if threaded else None
        for method in methods:
            if method is setup:
                continue
            is_test = method.name.startswith("test")
            name = "tmp_path" if is_test else root
            needs = uses(method, root) or (
                call is not None
                and any(call.search(lines[i]) for i in range(method.lineno - 1, method.end_lineno))
            )
            for index in range(method.lineno - 1, method.end_lineno):
                text = edits.get(index, lines[index])
                if index == method.lineno - 1 and needs:
                    text = add_parameter(text, name + ": Path")
                text = re.sub(r"\bself\." + root + r"\b", name, text)
                if call is not None:
                    text = call.sub(r"self.\1(" + name + ", ", text)
                edits[index] = text.rstrip() if text.strip() else text
        converted += 1

    if not converted:
        return source, 0
    out = [edits.get(index, line) for index, line in enumerate(lines) if index not in drop]
    result = "\n".join(out)
    compile(result, "<codemod>", "exec")
    return result, converted


for path in sys.argv[1:]:
    with open(path, encoding="utf-8") as handle:
        source = handle.read()
    try:
        result, count = convert(source)
    except SyntaxError as error:
        print("SKIPPED " + path + ": " + str(error), file=sys.stderr)
        continue
    if count:
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(result)
        print("converted " + str(count) + " scratch setUp in " + path)
