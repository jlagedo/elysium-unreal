"""The schedule-text parser, ported: the engine's tokenizer, the grammar, the failure table.

This is `0x1030d850` and the tokenizer it reads through (`VEngineServer` slot 98, engine side
`0x2003c800`), transcribed from `docs/vtmb/npc-ai/schedule-kernel.md` § "The schedule-text parser
`0x1030d850`, walked". That section is the contract; where this file and it disagree, it is right.

The export runs this rather than a regex for two reasons. The byte ledger has to partition each
text into the tokens the engine actually reads, which only a real tokenizer can do; and the seam
must refuse to publish a text retail would refuse, because retail stops that owner's load at the
first failure and every text after it never becomes a program. A regex would publish a program the
game never had.

The runtime half of this story ports the same body into C++. This one is its fixture: both read the
same oracle section, and the tests compare them on the same 691 texts.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import re
from typing import Iterator, Sequence

#: The engine tokenizer's separators: everything at or below space.
_SEPARATORS = bytes(range(0x00, 0x21))

#: Each of these is a token on its own, however it is spelled against its neighbours. This is why
#: `NPCFlag:FORCE_RELAXED_ANIMS` arrives as THREE tokens and the parser tests the middle one
#: against the literal `":"`.
_SINGLE_CHARACTER = b"{}()':"

#: `Flags`, the only two spellings the engine has (`0x1030d7e0`). Anything else is a load-time
#: Error that still reads as 0, and the parser then reports `Unknown schedule flag` for every 0 --
#: so an authored `Flags NONE` prints that diagnostic harmlessly.
SCHEDULE_FLAGS = {"none": 0, "delay_interrupts": 1}

#: The seventeen operand prefixes, in the order `0x1030d9ce`..`0x1030e6b7` tests them. The value is
#: the resolver's retail address, carried so a published operand can name what answered it.
OPERAND_PREFIXES: dict[str, str] = {
    "activity": "0x1025d760",
    "task": "0x10316fd0",
    "schedule": "0x102cadb0",
    "state": "0x1030c600",
    "memory": "0x1030c800",
    "path": "0x1030ca70",
    "goal": "0x1030cb00",
    "hintflags": "0x102d3f50",
    "npcflag": "0x1030cbd0",
    "miscflag": "0x1030d390",
    "model": "0x1030d3d0",
    "sound": "0x1030d400",
    "expression": "0x1030f5f0",
    "sto": "0x1030d480",
    "dist": "0x1030d4f0",
    "mxtphase": "0x1030d650",
    "tomode": "0x1030d710",
}

#: The three prefixes whose resolved word is stored RAW, not converted to float. Every other
#: resolver's integer answer is stored as that number converted (`FILD`); these three store the
#: 32-bit word itself (`0x1030e06d`, `0x1030e128`, `0x1030e1e1`).
RAW_WORD_PREFIXES = frozenset({"npcflag", "miscflag", "model"})

#: The four boolean words, and what they store.
BOOLEAN_WORDS = {"true": 1.0, "on": 1.0, "false": 0.0, "off": 0.0}

_NUMBER = re.compile(r"^[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?$")


class ScheduleTextError(ValueError):
    """A text the retail parser would refuse, with the row of its failure table that refuses it."""

    def __init__(self, row: str, message: str, offset: int = 0) -> None:
        super().__init__(message)
        self.row = row
        self.offset = int(offset)


@dataclass(frozen=True, slots=True)
class Token:
    """One token, with the span it occupies in the text."""

    text: str
    offset: int
    length: int

    @property
    def end(self) -> int:
        return self.offset + self.length

    def equals(self, other: str) -> bool:
        """Case-insensitive compare. Every keyword and value test in the parser is `strcmpi`."""

        return self.text.lower() == other.lower()


@dataclass(frozen=True, slots=True)
class Operand:
    """One task's single operand, as authored and as resolved."""

    form: str                       # "prefix" | "boolean" | "number"
    spelling: str
    prefix: str | None = None
    resolver: str | None = None
    raw_word: bool = False
    offset: int = 0
    length: int = 0


@dataclass(frozen=True, slots=True)
class TaskStatement:
    name: str
    operand: Operand
    name_offset: int = 0
    name_length: int = 0


@dataclass(frozen=True, slots=True)
class Interrupt:
    name: str
    inverted: bool
    offset: int = 0
    length: int = 0


@dataclass
class ScheduleRecord:
    """One `Schedule <name> Tasks ... [Interrupts ...] [Flags ...]` record."""

    name: str
    tasks: list[TaskStatement] = field(default_factory=list)
    interrupts: list[Interrupt] = field(default_factory=list)
    flags: list[str] = field(default_factory=list)
    flag_word: int = 0
    #: Token spans, for the byte ledger: `(owner, offset, length)`.
    spans: list[tuple[str, int, int]] = field(default_factory=list)


#: Retail caps a schedule at 64 tasks; the 65th is a failure row.
MAX_TASKS = 64


def tokenize(body: bytes) -> list[Token]:
    """The engine's tokenizer, not the parser's.

    Bytes at or below space separate; `//` runs to end of line; a double quote groups a token with
    NO escape processing; and each of `{ } ( ) ' :` is a one-character token.
    """

    tokens: list[Token] = []
    cursor = 0
    limit = len(body)
    while cursor < limit:
        byte = body[cursor]
        if byte in _SEPARATORS:
            cursor += 1
            continue
        if byte == 0x2F and cursor + 1 < limit and body[cursor + 1] == 0x2F:   # `//`
            end = body.find(b"\n", cursor)
            cursor = limit if end < 0 else end + 1
            continue
        if byte == 0x22:                                                       # a quoted group
            end = body.find(b'"', cursor + 1)
            end = limit if end < 0 else end
            tokens.append(
                Token(
                    text=body[cursor + 1:end].decode("latin1"),
                    offset=cursor,
                    length=min(end + 1, limit) - cursor,
                )
            )
            cursor = min(end + 1, limit)
            continue
        if byte in _SINGLE_CHARACTER:
            tokens.append(Token(text=chr(byte), offset=cursor, length=1))
            cursor += 1
            continue
        start = cursor
        while cursor < limit:
            here = body[cursor]
            if here in _SEPARATORS or here in _SINGLE_CHARACTER or here == 0x22:
                break
            cursor += 1
        tokens.append(
            Token(text=body[start:cursor].decode("latin1"), offset=start, length=cursor - start)
        )
    return tokens


class _Reader:
    def __init__(self, tokens: Sequence[Token]) -> None:
        self._tokens = list(tokens)
        self._index = 0

    def peek(self) -> Token | None:
        return self._tokens[self._index] if self._index < len(self._tokens) else None

    def next(self) -> Token | None:
        token = self.peek()
        if token is not None:
            self._index += 1
        return token

    @property
    def exhausted(self) -> bool:
        return self._index >= len(self._tokens)


def parse(body: bytes) -> list[ScheduleRecord]:
    """Parse one text into its records, raising `ScheduleTextError` where retail fails.

    A text whose first token is not `Schedule` -- an EMPTY text included -- returns success and
    loads nothing, so this answers an empty list rather than raising.
    """

    reader = _Reader(tokenize(body))
    first = reader.peek()
    if first is None or not first.equals("Schedule"):
        return []

    records: list[ScheduleRecord] = []
    declared: set[str] = set()
    while not reader.exhausted:
        keyword = reader.peek()
        if keyword is None:
            break
        if not keyword.equals("Schedule"):
            raise ScheduleTextError(
                "unknown-token",
                f"expected `Schedule` and found {keyword.text!r}",
                keyword.offset,
            )
        reader.next()
        record = _parse_record(reader, keyword)
        lowered = record.name.lower()
        if lowered in declared:
            raise ScheduleTextError(
                "duplicate-schedule-name",
                f"the text declares {record.name!r} twice",
            )
        declared.add(lowered)
        records.append(record)
    return records


def _parse_record(reader: _Reader, keyword: Token) -> ScheduleRecord:
    name = reader.next()
    if name is None:
        raise ScheduleTextError("unknown-schedule-name", "a `Schedule` with no name", keyword.offset)

    record = ScheduleRecord(name=name.text)
    record.spans.append(("keyword", keyword.offset, keyword.length))
    record.spans.append(("name", name.offset, name.length))

    tasks_keyword = reader.next()
    if tasks_keyword is None or not tasks_keyword.equals("Tasks"):
        raise ScheduleTextError(
            "missing-tasks",
            f"{name.text!r} has no `Tasks` section",
            tasks_keyword.offset if tasks_keyword else name.end,
        )
    record.spans.append(("tasksKeyword", tasks_keyword.offset, tasks_keyword.length))

    _parse_tasks(reader, record)

    section = reader.peek()
    if section is not None and section.equals("Interrupts"):
        reader.next()
        record.spans.append(("interruptsKeyword", section.offset, section.length))
        _parse_interrupts(reader, record)
        section = reader.peek()
    if section is not None and section.equals("Flags"):
        reader.next()
        record.spans.append(("flagsKeyword", section.offset, section.length))
        _parse_flags(reader, record)
    return record


def _parse_tasks(reader: _Reader, record: ScheduleRecord) -> None:
    while True:
        token = reader.peek()
        if token is None:
            return
        if token.equals("Interrupts") or token.equals("Flags") or token.equals("Schedule"):
            return
        reader.next()
        if len(record.tasks) >= MAX_TASKS:
            raise ScheduleTextError(
                "task-cap",
                f"{record.name!r} declares more than {MAX_TASKS} tasks",
                token.offset,
            )
        operand = _parse_operand(reader, record, token)
        record.tasks.append(
            TaskStatement(
                name=token.text,
                operand=operand,
                name_offset=token.offset,
                name_length=token.length,
            )
        )
        record.spans.append(("task", token.offset, token.length))
        record.spans.append(("operand", operand.offset, operand.length))


def _parse_operand(reader: _Reader, record: ScheduleRecord, task: Token) -> Operand:
    """Exactly one operand per task; a task followed by a section keyword is `Bad syntax`."""

    token = reader.next()
    if token is None:
        raise ScheduleTextError(
            "missing-operand",
            f"task {task.text!r} in {record.name!r} has no operand",
            task.end,
        )
    if token.equals("Interrupts") or token.equals("Flags") or token.text.upper().startswith("TASK_"):
        raise ScheduleTextError(
            "bad-syntax-at-task",
            f"bad syntax at task #{len(record.tasks)} ({task.text!r}): "
            f"its operand is {token.text!r}",
            token.offset,
        )

    following = reader.peek()
    if following is not None and following.text == ":":
        prefix = token.text.lower()
        reader.next()                                          # the `:` token
        value = reader.next()
        if value is None:
            raise ScheduleTextError(
                "missing-operand",
                f"prefix {token.text!r} in {record.name!r} has no value",
                following.end,
            )
        if prefix not in OPERAND_PREFIXES:
            raise ScheduleTextError(
                "unknown-prefix",
                f"{token.text!r} is not one of the seventeen operand prefixes",
                token.offset,
            )
        stray = reader.peek()
        if stray is not None and stray.text == ":":
            raise ScheduleTextError(
                "stray-colon", f"a stray `:` after {value.text!r}", stray.offset
            )
        return Operand(
            form="prefix",
            spelling=value.text,
            prefix=prefix,
            resolver=OPERAND_PREFIXES[prefix],
            raw_word=prefix in RAW_WORD_PREFIXES,
            offset=token.offset,
            length=value.end - token.offset,
        )

    if token.text == ":":
        raise ScheduleTextError("stray-colon", "an operand that is a bare `:`", token.offset)

    lowered = token.text.lower()
    if lowered in BOOLEAN_WORDS:
        return Operand(
            form="boolean", spelling=token.text, offset=token.offset, length=token.length
        )
    # Anything else goes to `_atof`, and a non-number reads as 0.0 without failing.
    return Operand(form="number", spelling=token.text, offset=token.offset, length=token.length)


def _parse_interrupts(reader: _Reader, record: ScheduleRecord) -> None:
    while True:
        token = reader.peek()
        if token is None:
            return
        if token.equals("Flags") or token.equals("Schedule"):
            return
        reader.next()
        inverted = False
        start = token
        if token.text == "!":
            inverted = True
            token = reader.next()
            if token is None:
                raise ScheduleTextError(
                    "missing-operand", "a `!` with no condition after it", start.end
                )
        elif token.text.startswith("!") and len(token.text) > 1:
            inverted = True
            token = Token(text=token.text[1:], offset=token.offset + 1, length=token.length - 1)
        record.interrupts.append(
            Interrupt(
                name=token.text, inverted=inverted, offset=start.offset,
                length=token.end - start.offset,
            )
        )
        record.spans.append(("interrupt", start.offset, token.end - start.offset))


def _parse_flags(reader: _Reader, record: ScheduleRecord) -> None:
    while True:
        token = reader.peek()
        if token is None:
            return
        if token.equals("Schedule"):
            return
        reader.next()
        record.flags.append(token.text)
        record.spans.append(("flag", token.offset, token.length))
        record.flag_word |= SCHEDULE_FLAGS.get(token.text.lower(), 0)


def is_number(spelling: str) -> bool:
    """Whether `_atof` would read this operand as a number rather than as 0.0."""

    return bool(_NUMBER.match(spelling.strip()))
