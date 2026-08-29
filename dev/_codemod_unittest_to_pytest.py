"""Throwaway codemod: rewrite unittest asserts as bare pytest asserts.

Deleted once the migration lands. It does the mechanical part only -- asserts, `pytest.raises`,
`pytest.approx`, and the `unittest.main()` trailer. De-classing, fixtures and parametrization
are done by hand afterwards.
"""

from __future__ import annotations

import sys

import libcst as cst
import libcst.matchers as m

# assertX -> comparison operator
BINARY = {
    "assertEqual": cst.Equal,
    "assertNotEqual": cst.NotEqual,
    "assertIn": cst.In,
    "assertNotIn": cst.NotIn,
    "assertIs": cst.Is,
    "assertIsNot": cst.IsNot,
    "assertGreater": cst.GreaterThan,
    "assertGreaterEqual": cst.GreaterThanEqual,
    "assertLess": cst.LessThan,
    "assertLessEqual": cst.LessThanEqual,
}

# assertX(value) -> `value <op> <literal>`
UNARY_AGAINST = {
    "assertIsNone": (cst.Is, "None"),
    "assertIsNotNone": (cst.IsNot, "None"),
}

# Node types that bind more loosely than a comparison and so need parentheses.
NEEDS_PARENS = (
    cst.BooleanOperation,
    cst.Comparison,
    cst.IfExp,
    cst.Lambda,
    cst.NamedExpr,
    cst.Yield,
    cst.UnaryOperation,
)


def paren(node):
    if isinstance(node, NEEDS_PARENS) and not node.lpar:
        return node.with_changes(lpar=[cst.LeftParen()], rpar=[cst.RightParen()])
    return node


def compare(left, op, right):
    return cst.Comparison(
        left=paren(left),
        comparisons=[cst.ComparisonTarget(operator=op(), comparator=paren(right))],
    )


def split_message(args, positional):
    """Return (value expressions, message expression or None)."""
    values, message = [], None
    for index, arg in enumerate(args):
        if arg.keyword is not None and arg.keyword.value == "msg":
            message = arg.value
        elif arg.keyword is not None:
            return [], None
        elif index >= positional:
            message = arg.value
        else:
            values.append(arg.value)
    return values, message


def keyword_equal():
    return cst.AssignEqual(cst.SimpleWhitespace(""), cst.SimpleWhitespace(""))


def approx(expected, keywords):
    """unittest rounds the difference to N places; pytest.approx defaults to a relative
    tolerance. State an absolute tolerance so the two mean the same thing."""
    if "delta" in keywords:
        tolerance = keywords["delta"]
    elif "places" in keywords:
        places = cst.Module([]).code_for_node(keywords["places"])
        tolerance = cst.parse_expression("1e-" + places)
    else:
        tolerance = cst.parse_expression("1e-7")
    return cst.Call(
        func=cst.Attribute(value=cst.Name("pytest"), attr=cst.Name("approx")),
        args=[
            cst.Arg(expected),
            cst.Arg(tolerance, keyword=cst.Name("abs"), equal=keyword_equal()),
        ],
    )


class Codemod(cst.CSTTransformer):
    def __init__(self):
        self.uses_pytest = False
        self.raised_names = set()

    # -- `self.assertX(...)` statements ------------------------------------------------
    def leave_SimpleStatementLine(self, original, updated):
        if len(updated.body) != 1 or not isinstance(updated.body[0], cst.Expr):
            return updated
        call = updated.body[0].value
        if not m.matches(call, m.Call(func=m.Attribute(value=m.Name("self"), attr=m.Name()))):
            return updated
        converted = self._assertion(call.func.attr.value, list(call.args))
        if converted is None:
            return updated
        expression, message = converted
        # The call's own parentheses were holding a multi-line argument together; without
        # them the continuation lines are an indentation error, so keep a pair.
        rendered = cst.Module([]).code_for_node(expression)
        if "\n" in rendered and not expression.lpar:
            expression = expression.with_changes(
                lpar=[cst.LeftParen()], rpar=[cst.RightParen()]
            )
        return updated.with_changes(body=[cst.Assert(test=expression, msg=message)])

    def _assertion(self, name, args):
        if name in BINARY:
            values, message = split_message(args, 2)
            if len(values) != 2:
                return None
            return compare(values[0], BINARY[name], values[1]), message
        if name in UNARY_AGAINST:
            values, message = split_message(args, 1)
            if len(values) != 1:
                return None
            op, literal = UNARY_AGAINST[name]
            return compare(values[0], op, cst.Name(literal)), message
        if name == "assertTrue":
            values, message = split_message(args, 1)
            if len(values) != 1:
                return None
            return values[0], message
        if name == "assertFalse":
            values, message = split_message(args, 1)
            if len(values) != 1:
                return None
            return cst.UnaryOperation(operator=cst.Not(), expression=paren(values[0])), message
        if name == "assertIsInstance":
            values, message = split_message(args, 2)
            if len(values) != 2:
                return None
            call = cst.Call(func=cst.Name("isinstance"), args=[cst.Arg(values[0]), cst.Arg(values[1])])
            return call, message
        if name in ("assertAlmostEqual", "assertNotAlmostEqual"):
            keywords = {
                arg.keyword.value: arg.value
                for arg in args
                if arg.keyword is not None and arg.keyword.value in ("places", "delta")
            }
            values = [arg.value for arg in args if arg.keyword is None]
            if len(values) != 2:
                return None
            self.uses_pytest = True
            op = cst.Equal if name == "assertAlmostEqual" else cst.NotEqual
            return compare(values[0], op, approx(values[1], keywords)), None
        return None

    # -- `self.fail(...)`, `self.assertRaises(...)` -------------------------------------
    def leave_Call(self, original, updated):
        if not m.matches(updated, m.Call(func=m.Attribute(value=m.Name("self"), attr=m.Name()))):
            return updated
        name = updated.func.attr.value
        if name == "fail":
            self.uses_pytest = True
            return updated.with_changes(
                func=cst.Attribute(value=cst.Name("pytest"), attr=cst.Name("fail"))
            )
        if name == "assertRaises":
            self.uses_pytest = True
            return updated.with_changes(
                func=cst.Attribute(value=cst.Name("pytest"), attr=cst.Name("raises"))
            )
        if name == "assertRaisesRegex":
            self.uses_pytest = True
            args = list(updated.args)
            if len(args) >= 2 and args[1].keyword is None:
                args[1] = args[1].with_changes(keyword=cst.Name("match"), equal=keyword_equal())
            return updated.with_changes(
                func=cst.Attribute(value=cst.Name("pytest"), attr=cst.Name("raises")),
                args=args,
            )
        return updated

    def visit_With(self, node):
        raises = m.Call(
            func=m.Attribute(
                value=m.Name("self") | m.Name("pytest"),
                attr=m.Name("assertRaises") | m.Name("assertRaisesRegex") | m.Name("raises"),
            )
        )
        for item in node.items:
            if item.asname is None or not m.matches(item.item, raises):
                continue
            target = item.asname.name
            if isinstance(target, cst.Name):
                self.raised_names.add(target.value)

    # `pytest.raises` exposes the exception as `.value`, not `.exception`.
    def leave_Attribute(self, original, updated):
        if (
            isinstance(updated.value, cst.Name)
            and updated.value.value in self.raised_names
            and updated.attr.value == "exception"
        ):
            return updated.with_changes(attr=cst.Name("value"))
        return updated

    # -- drop the `unittest.main()` trailer --------------------------------------------
    def leave_If(self, original, updated):
        if m.matches(
            updated.test,
            m.Comparison(
                left=m.Name("__name__"),
                comparisons=[
                    m.ComparisonTarget(operator=m.Equal(), comparator=m.SimpleString())
                ],
            ),
        ) and "unittest.main()" in cst.Module([]).code_for_node(updated):
            return cst.RemovalSentinel.REMOVE
        return updated


def add_pytest_import(module):
    """Insert `import pytest` after the last top-level import statement."""
    body = list(module.body)
    last = -1
    for index, statement in enumerate(body):
        if not isinstance(statement, cst.SimpleStatementLine):
            continue
        for small in statement.body:
            if isinstance(small, cst.Import):
                if any(alias.evaluated_name == "pytest" for alias in small.names):
                    return module
                last = index
            elif isinstance(small, cst.ImportFrom):
                last = index
    body.insert(last + 1, cst.parse_statement("import pytest"))
    return module.with_changes(body=body)


def drop_unittest_import(module):
    code = module.code
    if "unittest." in code or "unittest import" in code:
        return module
    lines = [line for line in code.split("\n") if line.strip() != "import unittest"]
    return cst.parse_module("\n".join(lines))


def rewrite(source):
    tree = cst.parse_module(source)
    codemod = Codemod()
    # Two passes: the first records the `as` names bound by assertRaises, the second uses them.
    tree = tree.visit(codemod).visit(codemod)
    if codemod.uses_pytest or "pytest." in tree.code:
        tree = add_pytest_import(tree)
    tree = drop_unittest_import(tree)
    result = tree.code.rstrip("\n") + "\n"
    compile(result, "<codemod>", "exec")  # never write a file the codemod broke
    return result


failed = []
for path in sys.argv[1:]:
    with open(path, encoding="utf-8") as handle:
        source = handle.read()
    try:
        result = rewrite(source)
    except Exception as error:  # noqa: BLE001 - report and move on to the next file
        failed.append((path, error))
        continue
    if result != source:
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(result)
        print("rewrote " + path)

for path, error in failed:
    print("SKIPPED " + path + ": " + str(error).splitlines()[0], file=sys.stderr)
