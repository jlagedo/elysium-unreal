"""The Python 2.1 opcode table the shipped `.pyc` companions were compiled against.

CPython 2.1 is the VM the game embeds, and its table is not the one `dis` in this interpreter
carries: 2.1 emits `SET_LINENO` between statements, resolves `for` with `FOR_LOOP` rather than
`FOR_ITER`, and has none of the iterator, generator or new-style-class opcodes a later table
would offer for the same numbers. Disassembling against the running interpreter's table would
therefore name the wrong instruction, so the table is stated here and nowhere else.
"""

from __future__ import annotations

#: Instructions numbered at or above this carry a two-byte little-endian argument.
HAVE_ARGUMENT = 90

#: The `.pyc` magic word Python 2.1 writes, and the four bytes that spell it.
PYTHON_21_MAGIC = 60202
PYTHON_21_MAGIC_BYTES = b"\x2a\xeb\x0d\x0a"

#: `opcode -> name`, exactly the 2.1 table.
OPNAME: dict[int, str] = {
    0: "STOP_CODE",
    1: "POP_TOP",
    2: "ROT_TWO",
    3: "ROT_THREE",
    4: "DUP_TOP",
    5: "ROT_FOUR",
    10: "UNARY_POSITIVE",
    11: "UNARY_NEGATIVE",
    12: "UNARY_NOT",
    13: "UNARY_CONVERT",
    15: "UNARY_INVERT",
    19: "BINARY_POWER",
    20: "BINARY_MULTIPLY",
    21: "BINARY_DIVIDE",
    22: "BINARY_MODULO",
    23: "BINARY_ADD",
    24: "BINARY_SUBTRACT",
    25: "BINARY_SUBSCR",
    30: "SLICE+0",
    31: "SLICE+1",
    32: "SLICE+2",
    33: "SLICE+3",
    40: "STORE_SLICE+0",
    41: "STORE_SLICE+1",
    42: "STORE_SLICE+2",
    43: "STORE_SLICE+3",
    50: "DELETE_SLICE+0",
    51: "DELETE_SLICE+1",
    52: "DELETE_SLICE+2",
    53: "DELETE_SLICE+3",
    55: "INPLACE_ADD",
    56: "INPLACE_SUBTRACT",
    57: "INPLACE_MULTIPLY",
    58: "INPLACE_DIVIDE",
    59: "INPLACE_MODULO",
    60: "STORE_SUBSCR",
    61: "DELETE_SUBSCR",
    62: "BINARY_LSHIFT",
    63: "BINARY_RSHIFT",
    64: "BINARY_AND",
    65: "BINARY_XOR",
    66: "BINARY_OR",
    67: "INPLACE_POWER",
    70: "PRINT_EXPR",
    71: "PRINT_ITEM",
    72: "PRINT_NEWLINE",
    73: "PRINT_ITEM_TO",
    74: "PRINT_NEWLINE_TO",
    75: "INPLACE_LSHIFT",
    76: "INPLACE_RSHIFT",
    77: "INPLACE_AND",
    78: "INPLACE_XOR",
    79: "INPLACE_OR",
    80: "BREAK_LOOP",
    82: "LOAD_LOCALS",
    83: "RETURN_VALUE",
    84: "IMPORT_STAR",
    85: "EXEC_STMT",
    87: "POP_BLOCK",
    88: "END_FINALLY",
    89: "BUILD_CLASS",
    90: "STORE_NAME",
    91: "DELETE_NAME",
    92: "UNPACK_SEQUENCE",
    95: "STORE_ATTR",
    96: "DELETE_ATTR",
    97: "STORE_GLOBAL",
    98: "DELETE_GLOBAL",
    99: "DUP_TOPX",
    100: "LOAD_CONST",
    101: "LOAD_NAME",
    102: "BUILD_TUPLE",
    103: "BUILD_LIST",
    104: "BUILD_MAP",
    105: "LOAD_ATTR",
    106: "COMPARE_OP",
    107: "IMPORT_NAME",
    108: "IMPORT_FROM",
    110: "JUMP_FORWARD",
    111: "JUMP_IF_FALSE",
    112: "JUMP_IF_TRUE",
    113: "JUMP_ABSOLUTE",
    114: "FOR_LOOP",
    116: "LOAD_GLOBAL",
    119: "CONTINUE_LOOP",
    120: "SETUP_LOOP",
    121: "SETUP_EXCEPT",
    122: "SETUP_FINALLY",
    124: "LOAD_FAST",
    125: "STORE_FAST",
    126: "DELETE_FAST",
    127: "SET_LINENO",
    130: "RAISE_VARARGS",
    131: "CALL_FUNCTION",
    132: "MAKE_FUNCTION",
    133: "BUILD_SLICE",
    134: "MAKE_CLOSURE",
    135: "LOAD_CLOSURE",
    136: "LOAD_DEREF",
    137: "STORE_DEREF",
    140: "CALL_FUNCTION_VAR",
    141: "CALL_FUNCTION_KW",
    142: "CALL_FUNCTION_VAR_KW",
    143: "EXTENDED_ARG",
}

#: The comparison table `COMPARE_OP`'s argument indexes. 2.1 still spells inequality both ways.
CMP_OP = (
    "<",
    "<=",
    "==",
    "!=",
    ">",
    ">=",
    "in",
    "not in",
    "is",
    "is not",
    "exception match",
    "BAD",
)

#: How an instruction's argument is to be read.
HAS_CONST = frozenset({100})
HAS_NAME = frozenset({90, 91, 95, 96, 97, 98, 101, 105, 107, 108, 116})
HAS_LOCAL = frozenset({124, 125, 126})
HAS_FREE = frozenset({135, 136, 137})
HAS_COMPARE = frozenset({106})
HAS_LINE = frozenset({127})
