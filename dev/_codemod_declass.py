"""Throwaway codemod: hoist `unittest.TestCase` bodies to module level.

Deleted once the migration lands. A class is only converted when every `self.` reference in
it names one of its own members -- a class attribute or a helper method -- so the rewrite is a
rename plus a dedent. Anything with `setUp`/`tearDown`, a name that would collide at module
level, or a `self` used as a value is left alone for hand conversion.

The dedent is textual rather than done on the tree so that continuation lines inside
multi-line expressions shift with the statement they belong to.
"""

from __future__ import annotations

import ast
import re
import sys

SKIP_METHODS = {"setUp", "tearDown", "setUpClass", "tearDownClass", "asyncSetUp"}


def is_test_case(node):
    for base in node.bases:
        if isinstance(base, ast.Attribute) and base.attr == "TestCase":
            return True
        if isinstance(base, ast.Name) and base.id == "TestCase":
            return True
    return False


def member_names(node):
    """Class attributes and methods, or None when the class cannot be hoisted."""
    names = []
    for item in node.body:
        if isinstance(item, ast.Assign):
            for target in item.targets:
                if not isinstance(target, ast.Name):
                    return None
                names.append(target.id)
        elif isinstance(item, ast.AnnAssign):
            if not isinstance(item.target, ast.Name):
                return None
            names.append(item.target.id)
        elif isinstance(item, ast.FunctionDef):
            if item.name in SKIP_METHODS:
                return None
            names.append(item.name)
        elif isinstance(item, ast.Expr) and isinstance(item.value, ast.Constant):
            continue  # the class docstring
        else:
            return None
    return names


def bare_self_is_only_a_receiver(node, names):
    """True when every `self` in the class is the receiver of one of its own members."""
    receivers = set()
    for sub in ast.walk(node):
        if isinstance(sub, ast.Attribute) and isinstance(sub.value, ast.Name):
            if sub.value.id == "self" and sub.attr in names:
                receivers.add(id(sub.value))
    for sub in ast.walk(node):
        if isinstance(sub, ast.Name) and sub.id == "self" and id(sub) not in receivers:
            return False
    return True


def module_level_names(tree):
    names = set()
    for node in tree.body:
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            names.add(node.name)
        elif isinstance(node, ast.Assign):
            names.update(t.id for t in node.targets if isinstance(t, ast.Name))
        elif isinstance(node, ast.AnnAssign) and isinstance(node.target, ast.Name):
            names.add(node.target.id)
        elif isinstance(node, (ast.Import, ast.ImportFrom)):
            for alias in node.names:
                names.add((alias.asname or alias.name).split(".")[0])
    return names


def strip_self_parameter(line):
    """`def name(self, x)` -> `def name(x)`; also handles `(self)` and a wrapped signature."""
    line = re.sub(r"\(\s*self\s*,\s*", "(", line, count=1)
    line = re.sub(r"\(\s*self\s*\)", "()", line, count=1)
    line = re.sub(r"\(\s*self\s*$", "(", line, count=1)
    return line


def block(lines, start, end, indent, names):
    """Source lines [start, end) dedented by `indent` with `self.member` renamed."""
    pattern = re.compile(r"\bself\.(" + "|".join(re.escape(n) for n in names) + r")\b")
    out = []
    for offset, raw in enumerate(lines[start:end]):
        text = raw[indent:] if raw[:indent].strip() == "" else raw.lstrip()
        if offset == 0 or "def " in text[:8]:
            text = strip_self_parameter(text)
        out.append(pattern.sub(r"\1", text))
    return out


def first_line(node):
    decorators = getattr(node, "decorator_list", None)
    return (decorators[0].lineno if decorators else node.lineno) - 1


def convert(source):
    tree = ast.parse(source)
    lines = source.split("\n")
    taken = module_level_names(tree)
    replacements = {}  # class start line -> (end line, new lines)
    converted = 0

    for node in tree.body:
        if not isinstance(node, ast.ClassDef) or not is_test_case(node):
            continue
        names = member_names(node)
        if names is None or not names:
            continue
        if not bare_self_is_only_a_receiver(node, names):
            continue
        if any(name in taken for name in names):
            continue

        body = [item for item in node.body
                if not (isinstance(item, ast.Expr) and isinstance(item.value, ast.Constant))]
        indent = len(lines[body[0].lineno - 1]) - len(lines[body[0].lineno - 1].lstrip())
        emitted = []
        for index, item in enumerate(body):
            if index:
                emitted.append("")
                if isinstance(item, ast.FunctionDef):
                    emitted.append("")
            emitted.extend(block(lines, first_line(item), item.end_lineno, indent, names))
        while emitted and not emitted[-1].strip():
            emitted.pop()
        replacements[first_line(node)] = (node.end_lineno, emitted)
        taken.update(names)
        converted += 1

    if not converted:
        return source, 0

    out, index = [], 0
    while index < len(lines):
        if index in replacements:
            end, emitted = replacements[index]
            out.extend(emitted)
            index = end
        else:
            out.append(lines[index])
            index += 1
    return "\n".join(out).rstrip("\n") + "\n", converted


for path in sys.argv[1:]:
    with open(path, encoding="utf-8") as handle:
        source = handle.read()
    try:
        result, count = convert(source)
        if count:
            compile(result, path, "exec")
    except SyntaxError as error:
        print("SKIPPED " + path + ": " + str(error), file=sys.stderr)
        continue
    if count:
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(result)
        print("hoisted " + str(count) + " class(es) in " + path)
