"""The whole-image recovery: who owns which id space, which names it registers, which texts it feeds.

One pass over the image answers every unit's question at once, because the image is 7.8 MB and the
seam has 57 units; deciding this per unit would read `.text` 57 times for one answer. The result is
memoized on the image's digest, so the plural export pays for it once.

The shape it reads is `docs/vtmb/npc-ai/schedule-kernel.md` § "The schedule owners and their
registrations". Everything here is a literal immediate ahead of a call: the recovery never executes
a body, and where it cannot read an operand it raises rather than inferring one.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import hashlib
import re
import struct
from typing import Iterable

from elysium_pipeline.formats.ai_schedule_glb.image import (
    PEImage,
    PINNED_BYTE_LENGTH,
    PINNED_SHA256,
    entry_points,
    follow_jump,
    iter_call_sites,
)
from elysium_pipeline.formats.ai_schedule_glb.rtti import (
    SCHEDULE_ID_SPACE_SLOT,
    classes_deriving_from,
)
from elysium_pipeline.formats.ai_schedule_glb.walk import WalkError, body_bounds, walk_body


class CensusError(ValueError):
    """The image does not hold the shape this seam recovers."""


#: The bodies every recovery anchors on, by the name this seam calls them.
#: Each is expanded to itself plus every `JMP rel32` thunk in front of it, because the image reaches
#: `Register` through at least two and a decoder that knew one of them under-counted `CAI_BaseNPC`'s
#: schedule names by eleven.
ANCHORS: dict[str, int] = {
    "parse": 0x1030D850,            # the schedule-text parser
    "init": 0x102EA0E0,             # CAI_LocalIdSpace::Init
    # `CAI_BaseNPCTroika` does not call `Init` three times; it calls one helper that does,
    # deriving `+0x18` and `+0x30` from the space it is handed and `+8` / `+0x10` from the
    # namespace. Its three arguments arrive as the return values of one-line getters rather
    # than as immediates, which is why the plain `Init` reader cannot see them.
    "space_init_triple": 0x102BE9F0,
    "register": 0x102EA130,         # CAI_LocalIdSpace::Register, five arguments
    "register_schedule": 0x102BEA80,  # the four-argument helper, category "schedule"
    "register_task": 0x102BEAB0,      # ... "task", space + 0x18
    "register_condition": 0x102BEAE0,  # ... "condition", space + 0x30
    "pair_vector_ctor": 0x102C66F0,  # construct one stack vector of (name, id) pairs
    "pair_append": 0x102C6A50,      # append one (name, id) pair to a stack vector
    # The same append, twice more. MSVC emitted three bodies for one template: `CAI_BaseNPCTroika`
    # uses `0x102c6a50` for its first 158 schedules, `0x102c6780` for exactly ONE (`0xe2`, at the
    # boundary between its two text blocks) and `0x102beb50` for the remaining 115. Anchoring on
    # only the first two leaves that single name unregistered, and then retail's own parser would
    # be said to fail a text the game plainly runs.
    "pair_append_alt": 0x102C6780,
    "pair_append_direct": 0x102BEB50,  # the same, taking (name, id) as direct arguments
    "text_append": 0x102C6920,      # append one text pointer to a stack vector
    "text_append_alt": 0x102C65D0,  # the second form, used by CAI_BaseNPCTroika's tail block
}

#: The category a four-argument register helper supplies for itself.
_HELPER_CATEGORY = {
    "register_schedule": "schedule",
    "register_task": "task",
    "register_condition": "condition",
}

#: The categories `CAI_LocalIdSpace::Register` accepts, lower-cased.
CATEGORIES = ("schedule", "task", "condition", "squadslot")

#: The ownerless `vdata/schedules/<name>.sch` loader. It calls the parser and is not an owner.
DEAD_SCH_LOADER = 0x1030F220

#: The schedule manager the feed loop passes as `this`.
SCHEDULE_MANAGER = 0x10936B68

#: The section the shipped schedule texts live in. The oracle's prose says `.rdata`, and in this
#: image the linker merged the read-only string pool into `.data`: every one of the 691 fed texts
#: resolves there and `.rdata` admits none. The cut region follows the bytes.
_TEXT_POOL_SECTION = ".data"

#: A schedule record's opening, and it takes BOTH halves of retail's acceptance test: the first
#: token is `Schedule`, and the token after the schedule's name is `Tasks`.
#:
#: The first half alone is not enough, because the image keeps the parser's own diagnostics in the
#: same string pool -- `"Schedule has invalid state ID '%s'"`, `"Schedule not found"`,
#: `"Schedule file: %s"` -- and every one of them opens with the token `Schedule` while being a
#: format string rather than a program. Retail refuses a record with no `Tasks` section, so
#: requiring it is the parser's own rule rather than a filter invented to make a count come out.
_SCHEDULE_RECORD = re.compile(
    rb"^[\x00-\x20]*Schedule[\x00-\x20]+[^\x00-\x20]+[\x00-\x20]+Tasks(?=[\x00-\x20])",
    re.IGNORECASE,
)


@dataclass(frozen=True, slots=True)
class Registration:
    """One `(name, localId)` the owner registers into one of its four spaces."""

    category: str
    name: str
    local_id: int
    source_va: int


@dataclass(frozen=True, slots=True)
class ScheduleText:
    """One text the owner feeds to the parser, in feed order."""

    order: int
    string_va: int
    source_offset: int
    byte_length: int
    feed_va: int
    body: bytes

    @property
    def name(self) -> str:
        """The text's declared schedule name, or `""` when it declares none."""

        tokens = self.body.split()
        if len(tokens) >= 2 and tokens[0].lower() == b"schedule":
            return tokens[1].decode("ascii", "replace")
        return ""


@dataclass(frozen=True, slots=True)
class Space:
    """One `CAI_LocalIdSpace` the owner's body initialises."""

    category: str
    address: int
    namespace: int
    parent: int | None
    init_va: int


@dataclass
class Owner:
    """One `InitCustomSchedules` body and everything it declares."""

    class_name: str
    init_body: int
    #: The schedule space the feed loop hands the parser. This is the owner's identity even when
    #: its `Init` calls and its registrations live in other bodies, as `CAI_BaseNPC`'s do.
    feed_space: int | None = None
    spaces: dict[str, Space] = field(default_factory=dict)
    registrations: list[Registration] = field(default_factory=list)
    texts: list[ScheduleText] = field(default_factory=list)
    #: Classnames whose slot-580 getter answers this owner's schedule space, sorted.
    class_names: list[str] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)

    @property
    def key(self) -> str:
        return self.class_name.lower()

    @property
    def schedule_space(self) -> int | None:
        space = self.spaces.get("schedule")
        if space is not None:
            return space.address
        return self.feed_space

    def registrations_of(self, category: str) -> list[Registration]:
        return [row for row in self.registrations if row.category == category]


@dataclass
class Census:
    """Everything one image says about this seam."""

    sha256: str
    byte_length: int
    owners: list[Owner]
    #: The runs the cut region admits, as `{string_va: (file offset, length, body)}`.
    region: dict[int, tuple[int, int, bytes]]
    anomalies: list[dict]
    dead_doors: list[dict]

    @property
    def matches_pin(self) -> bool:
        return self.sha256 == PINNED_SHA256 and self.byte_length == PINNED_BYTE_LENGTH

    def owner_of_space(self, space: int) -> Owner | None:
        for owner in self.owners:
            if owner.schedule_space == space:
                return owner
        return None

    def by_key(self) -> dict[str, Owner]:
        return {owner.key: owner for owner in self.owners}


_MEMO: dict[str, Census] = {}


def build(data: bytes) -> Census:
    """Recover the census from one image's bytes, memoized on its digest."""

    digest = hashlib.sha256(data).hexdigest()
    cached = _MEMO.get(digest)
    if cached is not None:
        return cached
    census = _build(PEImage(data), digest, len(data))
    _MEMO[digest] = census
    return census


def _build(image: PEImage, digest: str, byte_length: int) -> Census:
    anchor_targets: dict[int, str] = {}
    for label, va in ANCHORS.items():
        resolved = entry_points(image, va)
        if not resolved:
            raise CensusError(f"anchor {label} ({va:#010x}) resolves to nothing in this image")
        for target in resolved:
            anchor_targets[target] = label

    sites: dict[str, list[int]] = {label: [] for label in ANCHORS}
    for site, target in iter_call_sites(image, frozenset(anchor_targets)):
        sites[anchor_targets[target]].append(site)
    if not sites["parse"]:
        raise CensusError("no call site reaches the schedule-text parser; wrong image")

    # Every body holding a parse call is an owner, except the ownerless .sch loader.
    bodies: dict[int, tuple[int, int]] = {}
    for site in sites["parse"]:
        start, end = body_bounds(image, site)
        bodies[start] = (start, end)

    dead_doors: list[dict] = []
    anomalies: list[dict] = []
    owners: list[Owner] = []

    for start, (body_start, body_end) in sorted(bodies.items()):
        if body_start == DEAD_SCH_LOADER:
            dead_doors.append(
                {
                    "address": f"{DEAD_SCH_LOADER:#010x}",
                    "role": "vdata/schedules/<name>.sch loader",
                    "reason": "calls the parser, has no caller in the image and no shipped file",
                }
            )
            continue
        owners.append(_read_owner(image, body_start, body_end, anchor_targets))

    _attach_outlying_spaces(image, owners, anchor_targets, set(bodies), anomalies)
    _attach_outlying_registrations(image, owners, anchor_targets, set(bodies))
    _attach_shared_spaces(image, owners, anomalies)
    _prove_parents(owners, anomalies)
    region = _cut_region(image)
    _prove_partition(owners, region, anomalies)
    _check_registrations(owners, anomalies)

    if digest != PINNED_SHA256 or byte_length != PINNED_BYTE_LENGTH:
        anomalies.append(
            {
                "row": "image-differs-from-pin",
                "sha256": digest,
                "byteLength": byte_length,
                "pinnedSha256": PINNED_SHA256,
                "pinnedByteLength": PINNED_BYTE_LENGTH,
                "reason": "the census is published as measured; the addresses are the pin's",
            }
        )

    return Census(
        sha256=digest,
        byte_length=byte_length,
        owners=owners,
        region=region,
        anomalies=anomalies,
        dead_doors=dead_doors,
    )


def _read_owner(
    image: PEImage, body_start: int, body_end: int, anchor_targets: dict[int, str]
) -> Owner:
    """Read one init body: its spaces, its registrations and the texts it feeds."""

    try:
        walk = walk_body(image, body_start, body_end)
    except WalkError as error:
        raise CensusError(f"{body_start:#010x}: {error}") from error

    labelled = [(call, anchor_targets.get(call.target)) for call in walk.calls]

    # --- the spaces ---------------------------------------------------------------------------
    spaces: dict[str, Space] = {}
    init_calls = [call for call, label in labelled if label == "init"]
    order = ("schedule", "task", "condition", "squadslot")
    for index, call in enumerate(init_calls):
        if call.this_immediate is None or len(call.pushed_immediates) < 2:
            raise CensusError(
                f"{call.va:#010x}: CAI_LocalIdSpace::Init without literal space/namespace/parent"
            )
        namespace = int(call.pushed_immediates[-1])
        parent = int(call.pushed_immediates[-2])
        if index >= len(order):
            raise CensusError(f"{body_start:#010x}: more than four id spaces initialised")
        spaces[order[index]] = Space(
            category=order[index],
            address=int(call.this_immediate),
            namespace=namespace,
            parent=parent or None,
            init_va=int(call.va),
        )

    # --- the class name, from the feed loop ----------------------------------------------------
    class_name = ""
    parse_calls = [call for call, label in labelled if label == "parse"]
    for call in parse_calls:
        for immediate in call.pushed_immediates:
            text = image.read_cstring_va(immediate)
            if text and _looks_like_class(text):
                class_name = text
                break
        if class_name:
            break
    if not class_name:
        raise CensusError(f"{body_start:#010x}: the feed loop names no class")

    # The parser is handed `(manager, className, text, space)`; the space is the pushed immediate
    # that is not the classname string, and it identifies the owner even when this body initialises
    # nothing -- `CAI_BaseNPC` feeds from one body, initialises in another and registers in three
    # more.
    feed_space: int | None = None
    for call in parse_calls:
        for immediate in call.pushed_immediates:
            if image.read_cstring_va(immediate) == class_name:
                continue
            if image.section_of_va(immediate) == ".data":
                feed_space = int(immediate)
        if feed_space is not None:
            break

    owner = Owner(
        class_name=class_name,
        init_body=int(body_start),
        feed_space=feed_space,
        spaces=spaces,
    )

    # --- the registrations ---------------------------------------------------------------------
    # A vector is a frame address. Pairs are appended to it; the register loop that later walks it
    # names the category. Binding the two is the whole reason the walk tracks ESP.
    pairs: dict[int, list[tuple[str, int, int]]] = {}
    for call, label in labelled:
        if label in ("pair_append", "pair_append_alt"):
            # The pair is staged in a frame slot and the vector is `this`.
            if call.this_frame is None or len(call.stores) < 2:
                raise CensusError(
                    f"{call.va:#010x}: a pair append whose vector or pair is not literal"
                )
            name = image.read_cstring_va(call.stores[0].value)
            local_id = int(call.stores[1].value)
        elif label == "pair_append_direct":
            # The same append, with the pair passed as two arguments instead of staged. Pushed
            # right to left, so the name is the last immediate. `CAI_BaseNPCTroika` builds its
            # tail block of 116 schedules this way and its head block the other way.
            if call.this_frame is None or len(call.pushed_immediates) < 2:
                raise CensusError(
                    f"{call.va:#010x}: a direct pair append whose vector or pair is not literal"
                )
            name = image.read_cstring_va(call.pushed_immediates[-1])
            local_id = int(call.pushed_immediates[-2])
        else:
            continue
        if not name:
            raise CensusError(f"{call.va:#010x}: a pair append with no readable name")
        pairs.setdefault(call.this_frame, []).append((name, local_id, int(call.va)))

    # Binding a pair vector to its category. The body constructs its vectors in category order
    # -- schedule, task, condition, squadslot -- and later walks them in that same order, which is
    # the recipe `schedule-kernel.md` records. So the Nth register loop walks the Nth vector.
    #
    # That ordinal binding is the recovery. Where a loop also STATES its category (the five-argument
    # `Register` pushes it as a literal) the two are cross-checked and a disagreement refuses the
    # body, so the ordinal rule can never quietly mis-file a name.
    loop_categories: list[str | None] = []
    for call, label in labelled:
        if label == "register":
            immediates = call.pushed_immediates
            if not immediates:
                raise CensusError(f"{call.va:#010x}: a register loop naming no category")
            # Two shapes reach the five-argument `Register`. In a LOOP the class, id and name are
            # registers and the category is the only immediate. Called DIRECTLY, once per name,
            # all four are immediates -- and because x86 pushes right to left they read backwards
            # from the end: className, category, localId, name.
            if len(immediates) >= 4:
                name = image.read_cstring_va(immediates[-1])
                stated = image.read_cstring_va(immediates[-3]).lower()
                if name and stated in CATEGORIES:
                    owner.registrations.append(
                        Registration(
                            category=stated,
                            name=name,
                            local_id=int(immediates[-2]),
                            source_va=int(call.va),
                        )
                    )
                    continue
            stated = image.read_cstring_va(immediates[0]).lower()
            if stated not in CATEGORIES:
                raise CensusError(f"{call.va:#010x}: unknown register category {stated!r}")
            loop_categories.append(stated)
        elif label in _HELPER_CATEGORY:
            # The four-argument helpers carry the category in the callee. Called with the pair
            # pushed inline they are a registration on their own; called in a loop they are the
            # Nth walk.
            if len(call.pushed_immediates) >= 2:
                name = image.read_cstring_va(call.pushed_immediates[-1])
                if name:
                    owner.registrations.append(
                        Registration(
                            category=_HELPER_CATEGORY[label],
                            name=name,
                            local_id=int(call.pushed_immediates[-2]),
                            source_va=int(call.va),
                        )
                    )
                    continue
            loop_categories.append(_HELPER_CATEGORY[label])

    # The vectors, in construction order. Anchoring on the constructor rather than on "any call
    # that touched this frame" is what keeps accessor calls inside the loops out of the ordering.
    seen_order: list[int] = []
    for call, label in labelled:
        if label == "pair_vector_ctor" and call.this_frame is not None:
            if call.this_frame not in seen_order:
                seen_order.append(call.this_frame)
    for frame in sorted(pairs, key=lambda f: pairs[f][0][2]):
        if frame not in seen_order:
            raise CensusError(
                f"{body_start:#010x}: pairs are appended to a vector at frame {frame:#x} that no "
                "constructor in this body created"
            )

    category_of_frame: dict[int, str] = {}
    for index, frame in enumerate(seen_order):
        if index >= len(loop_categories):
            raise CensusError(
                f"{body_start:#010x}: vector {index} at frame {frame:#x} took pairs and no "
                "register loop walks it"
            )
        category_of_frame[frame] = loop_categories[index] or CATEGORIES[index]

    for call, label in labelled:
        if label != "register" or not call.pushed_immediates:
            continue
        stated = image.read_cstring_va(call.pushed_immediates[0]).lower()
        bound = _bind_vector(walk, call, pairs)
        if bound is not None and category_of_frame.get(bound) not in (None, stated):
            raise CensusError(
                f"{call.va:#010x}: the loop states category {stated!r} but the vector at "
                f"{bound:#x} was bound to {category_of_frame[bound]!r}"
            )

    for frame, rows in pairs.items():
        category = category_of_frame.get(frame)
        if category is None:
            raise CensusError(
                f"{body_start:#010x}: {len(rows)} pair(s) at frame {frame:#x} are appended to a "
                "vector no register loop walks; the category cannot be recovered"
            )
        for name, local_id, va in rows:
            owner.registrations.append(
                Registration(category=category, name=name, local_id=local_id, source_va=va)
            )

    # --- the texts -------------------------------------------------------------------------------
    owner.texts.extend(_read_texts(image, labelled, parse_calls))
    return owner


def _bind_vector(walk, call, pairs: dict[int, list]) -> int | None:
    """Which pair vector this register loop walks, read from its own setup.

    The loop loads the vector's data pointer LAST, immediately before the call, so the last
    matching frame is the binding and an earlier one belongs to the loop before it. No lookback
    beyond the call's own setup: a loop whose vector this cannot see answers None and is left to
    the ordinal rule, which is a recovery rather than a nearby guess.
    """

    for frame in reversed(call.loads):
        if frame in pairs:
            return frame
    return None


def _read_texts(image: PEImage, labelled, parse_calls) -> list[ScheduleText]:
    """The texts this body feeds, in feed order.

    Three operand shapes ship. A stack vector filled by text appends (every species, and Troika in
    two blocks); a literal `PUSH imm32`; and a pointer indirection -- `CAI_BaseNPC`'s 64 calls, each
    reading its own cell of a static table. The cells are NOT in feed order, so the indirection is
    read per call site rather than by walking the table.
    """

    pointers: list[int] = []
    for call, label in labelled:
        if label in ("text_append", "text_append_alt"):
            if not call.stores:
                raise CensusError(f"{call.va:#010x}: a text append whose pointer is not literal")
            pointers.append(int(call.stores[-1].value))

    feed_va = parse_calls[0].va if parse_calls else 0
    if not pointers:
        # No vector: the parse sites carry the text themselves, by value or through a cell.
        for call in parse_calls:
            found_here = False
            for immediate in call.pushed_immediates:
                body = image.read_cstring_va(immediate)
                if body and _SCHEDULE_RECORD.match(body.encode("latin1")):
                    pointers.append(int(immediate))
                    found_here = True
                    break
            if found_here:
                continue
            # `CAI_BaseNPC` pushes its text through a register loaded from its own cell of a
            # static pointer table. Reading the call site's cell is what keeps the texts in FEED
            # order; the table's own order is different, and `FAIL` is fed last.
            for cell in call.absolute_loads:
                pointer = image.read_u32_va(cell)
                if not pointer:
                    continue
                indirect = image.read_cstring_va(pointer)
                if indirect and _SCHEDULE_RECORD.match(indirect.encode("latin1")):
                    pointers.append(int(pointer))
                    break

    texts: list[ScheduleText] = []
    for order, string_va in enumerate(pointers):
        offset = image.va_to_offset(string_va)
        if offset is None:
            raise CensusError(f"{string_va:#010x}: a fed text at an unmapped address")
        end = image.data.find(b"\0", offset)
        if end < 0:
            raise CensusError(f"{string_va:#010x}: a fed text with no terminator")
        body = image.data[offset:end]
        texts.append(
            ScheduleText(
                order=order,
                string_va=int(string_va),
                source_offset=int(offset),
                byte_length=len(body),
                feed_va=int(feed_va),
                body=body,
            )
        )
    return texts


def _looks_like_class(text: str) -> bool:
    return (
        text.startswith(("CNPC_", "CAI_", "CNPCMaker", "CGeneric"))
        and " " not in text
        and len(text) < 64
    )


#: The root of every class's schedule vocabulary. Slot 580 is read off the vtable of each class
#: whose RTTI base chain names it, which is how a class that has no init body of its own -- and so
#: no unit of its own -- is still placed on the space it actually uses.
ROOT_CLASS = "CAI_BaseNPC"

#: The three sub-space offsets, and the stride between the three namespace pointers. Both are the
#: helper's own arithmetic (`0x102be9f0`), not a convention this seam chose.
_SUB_SPACE_STRIDE = (0x00, 0x18, 0x30)
_NAMESPACE_STRIDE = (0x00, 0x08, 0x10)


def _constant_getter(image: PEImage, target: int) -> int | None:
    """The immediate a one-line `MOV EAX, imm32 / RET` answers, or None for any other body.

    Nothing else is accepted. The point of reading these at all is that they are decidable without
    executing anything; a getter with a branch in it would be a different recovery, and should say
    so by failing rather than by being guessed at.
    """

    offset = image.va_to_offset(follow_jump(image, target))
    if offset is None:
        return None
    body = image.data[offset : offset + 6]
    if len(body) < 6 or body[0] != 0xB8 or body[5] != 0xC3:
        return None
    return int(struct.unpack_from("<I", body, 1)[0])


def _attach_outlying_spaces(
    image: PEImage,
    owners: list[Owner],
    anchor_targets: dict[int, str],
    owner_bodies: set[int],
    anomalies: list[dict],
) -> None:
    """The two owners whose `Init` calls their own body does not make.

    Every species initialises its four spaces inline and `_read_owner` reads those. The two roots do
    not, for two different reasons:

    * `CAI_BaseNPC` initialises its three spaces in `0x1030c4e0`, which calls no parser and is
      therefore not an owner. The calls there are ordinary `Init` calls with literal operands, so
      this is the same reader pointed at a body found by its `Init` sites and bound to an owner by
      the space address that owner's own feed loop names.
    * `CAI_BaseNPCTroika` calls one helper (`0x102be9f0`) that initialises all three, and hands it
      `(space, namespaceBase, parentBase)` as the return values of three one-line getters. The
      helper derives `+0x18` / `+0x30` and `+8` / `+0x10` itself, so three constants are the whole
      answer -- but they arrive in registers, and which is which is decided by what they ARE: the
      namespace is one other owners already name, the parent is a space they already name, and what
      is left over is the space being initialised.

    Without this, twelve of the fifty-six units carry no parent at all -- the two roots, and the ten
    species whose parent is Troika's space and therefore mapped to no unit.
    """

    by_feed = {owner.feed_space: owner for owner in owners if owner.feed_space is not None}
    by_body = {owner.init_body: owner for owner in owners}
    known_namespaces = {space.namespace for owner in owners for space in owner.spaces.values()}
    known_spaces = {
        space.address for owner in owners for space in owner.spaces.values()
    } | set(by_feed)

    order = ("schedule", "task", "condition", "squadslot")

    # --- the outlying `Init` body -----------------------------------------------------------------
    seen_bodies: set[int] = set()
    for site, target in iter_call_sites(image, frozenset(anchor_targets)):
        if anchor_targets[target] != "init":
            continue
        try:
            body_start, body_end = body_bounds(image, site)
        except WalkError:
            continue
        if body_start in owner_bodies or body_start in seen_bodies:
            continue
        seen_bodies.add(body_start)
        try:
            walk = walk_body(image, body_start, body_end)
        except WalkError:
            continue
        calls = [
            call
            for call in walk.calls
            if anchor_targets.get(call.target) == "init"
            and call.this_immediate is not None
            and len(call.pushed_immediates) >= 2
        ]
        if not calls:
            continue
        owner = by_feed.get(int(calls[0].this_immediate))
        if owner is None or owner.spaces:
            continue
        for index, call in enumerate(calls[: len(order)]):
            owner.spaces[order[index]] = Space(
                category=order[index],
                address=int(call.this_immediate),
                namespace=int(call.pushed_immediates[-1]),
                parent=int(call.pushed_immediates[-2]) or None,
                init_va=int(call.va),
            )
        owner.notes.append(f"spaces initialised in {body_start:#010x}")

    # --- the helper that initialises three at once -------------------------------------------------
    for site, target in iter_call_sites(image, frozenset(anchor_targets)):
        if anchor_targets[target] != "space_init_triple":
            continue
        try:
            body_start, body_end = body_bounds(image, site)
            walk = walk_body(image, body_start, body_end)
        except WalkError as error:
            raise CensusError(f"{site:#010x}: {error}") from error
        owner = by_body.get(body_start)
        if owner is None or owner.spaces:
            continue
        index = next(
            (position for position, call in enumerate(walk.calls) if call.va == site), None
        )
        if index is None or index < 3:
            raise CensusError(
                f"{site:#010x}: the three-space helper is not preceded by three calls"
            )
        constants = [
            _constant_getter(image, call.target) for call in walk.calls[index - 3 : index]
        ]
        if any(value is None for value in constants):
            raise CensusError(
                f"{site:#010x}: the three-space helper's arguments are not one-line getters"
            )
        namespaces = [value for value in constants if value in known_namespaces]
        parents = [
            value
            for value in constants
            if value not in known_namespaces and value in known_spaces
        ]
        rest = [
            value
            for value in constants
            if value not in known_namespaces and value not in known_spaces
        ]
        if len(namespaces) != 1 or len(parents) != 1 or len(rest) != 1:
            raise CensusError(
                f"{site:#010x}: cannot tell the helper's space, namespace and parent apart "
                f"({[hex(value) for value in constants]})"
            )
        space, namespace, parent = rest[0], namespaces[0], parents[0]
        for position, category in enumerate(order[:3]):
            owner.spaces[category] = Space(
                category=category,
                address=space + _SUB_SPACE_STRIDE[position],
                namespace=namespace + _NAMESPACE_STRIDE[position],
                parent=(parent + _SUB_SPACE_STRIDE[position]) if parent else None,
                init_va=int(site),
            )
        owner.notes.append(f"three spaces initialised through {target:#010x}")


def _attach_outlying_registrations(
    image: PEImage,
    owners: list[Owner],
    anchor_targets: dict[int, str],
    owner_bodies: set[int],
) -> None:
    """Registrations made OUTSIDE any init body, attached to the space they name.

    `CAI_BaseNPC` does not register in the body that feeds its texts: it registers schedules in
    `0x102cadd0`, tasks in `0x10316ff0` and conditions in `0x102c8ce0`, none of which calls the
    parser and none of which would therefore be an owner. The attachment is by SPACE ADDRESS -- the
    register call names the space as a literal -- so nothing here depends on knowing those three
    addresses, and a fourth would attach itself.
    """

    space_owner: dict[int, tuple[Owner, str]] = {}
    for owner in owners:
        if owner.feed_space is not None:
            space_owner.setdefault(owner.feed_space, (owner, "schedule"))
            space_owner.setdefault(owner.feed_space + 0x18, (owner, "task"))
            space_owner.setdefault(owner.feed_space + 0x30, (owner, "condition"))
        for category, space in owner.spaces.items():
            space_owner[space.address] = (owner, category)
            # The task and condition spaces sit 0x18 and 0x30 past the schedule one, and the
            # four-argument helpers reach them by adding that offset to the space they are given.
            if category == "schedule":
                space_owner.setdefault(space.address + 0x18, (owner, "task"))
                space_owner.setdefault(space.address + 0x30, (owner, "condition"))

    register_labels = {"register", "register_schedule", "register_task", "register_condition"}
    seen_bodies: set[int] = set()
    for site, target in iter_call_sites(image, frozenset(anchor_targets)):
        if anchor_targets[target] not in register_labels:
            continue
        try:
            body_start, body_end = body_bounds(image, site)
        except WalkError:
            continue
        if body_start in owner_bodies or body_start in seen_bodies:
            continue
        seen_bodies.add(body_start)
        try:
            walk = walk_body(image, body_start, body_end)
        except WalkError:
            continue                      # not a body this walk can read; it registers nothing here
        for call in walk.calls:
            label = anchor_targets.get(call.target)
            if label not in register_labels:
                continue
            if call.this_immediate is None:
                continue
            bound = space_owner.get(int(call.this_immediate))
            if bound is None:
                continue
            owner, category = bound
            # x86 pushes arguments right to left, so the call's own operands read backwards from
            # the end: `(name, localId, [category], className)` arrives as `className` first.
            immediates = call.pushed_immediates
            if label in _HELPER_CATEGORY:
                category = _HELPER_CATEGORY[label]
                if len(immediates) < 2:
                    continue
                name, local_id = image.read_cstring_va(immediates[-1]), int(immediates[-2])
            else:
                if len(immediates) < 3:
                    continue
                stated = image.read_cstring_va(immediates[-3]).lower()
                if stated in CATEGORIES:
                    category = stated
                name, local_id = image.read_cstring_va(immediates[-1]), int(immediates[-2])
            if not name:
                continue
            owner.registrations.append(
                Registration(
                    category=category, name=name, local_id=local_id, source_va=int(call.va)
                )
            )


def _attach_shared_spaces(image: PEImage, owners: list[Owner], anomalies: list[dict]) -> None:
    """Place every `CAI_BaseNPC` subclass on the space its own slot 580 answers.

    A unit is an init BODY, and there are fifty-six of them; there are seventy-seven classes. The
    difference is classes that have no init body of their own and therefore inherit the getter of
    the class above them: `CNPC_VRat` uses `CNPC_VScurrying`'s space, `CNPC_VBaseBoss` and
    `CPayphone` use Troika's, and nine classes including `CGenericNPC` and `CCineNPC` use the base's.
    Nothing in any init body says so. What says so is slot 580 -- `GetClassScheduleIdSpace`, a
    one-line `MOV EAX, imm32 / RET` -- answering the same address from two different vtables, so the
    mapping is READ off the vtables rather than tabled here.

    That matters beyond tidiness: a class this seam does not place is a class whose spawned NPC
    finds no schedules at all, and the only signal would be an NPC that never chooses one.

    A getter answering a space no owner claims is an anomaly row, not a refusal: it is a class whose
    space nothing ever registers into, which is a true fact about the image rather than a failure of
    this walk.
    """

    by_space: dict[int, Owner] = {}
    for owner in owners:
        space = owner.schedule_space
        if space is not None:
            by_space.setdefault(space, owner)
        owner.class_names = [owner.class_name]

    claimed: dict[int, list[str]] = {}
    for entry in classes_deriving_from(image, ROOT_CLASS):
        getter = entry.slot(image, SCHEDULE_ID_SPACE_SLOT)
        if getter is None:
            anomalies.append(
                {
                    "row": "class-without-schedule-id-space-slot",
                    "className": entry.name,
                    "vtable": f"{entry.vtable_va:#010x}",
                    "reason": "the vtable is shorter than slot 580",
                }
            )
            continue
        space = _constant_getter(image, getter)
        if space is None:
            anomalies.append(
                {
                    "row": "schedule-id-space-getter-not-constant",
                    "className": entry.name,
                    "getter": f"{getter:#010x}",
                    "reason": "slot 580 is not a one-line MOV EAX, imm32 / RET",
                }
            )
            continue
        claimed.setdefault(space, []).append(entry.name)
        owner = by_space.get(space)
        if owner is None:
            continue
        if entry.name not in owner.class_names:
            owner.class_names.append(entry.name)
        owner.notes.append(f"slot-580 getter {getter:#010x} answers for {entry.name}")

    for owner in owners:
        owner.class_names = sorted(set(owner.class_names))

    for space, names in sorted(claimed.items()):
        if by_space.get(space) is None:
            anomalies.append(
                {
                    "row": "schedule-space-with-no-owner",
                    "space": f"{space:#010x}",
                    "classNames": sorted(names),
                    "reason": "slot 580 answers a space no init body registers into",
                }
            )


def _prove_parents(owners: list[Owner], anomalies: list[dict]) -> None:
    """Every space's parent must be a space some unit initialises -- or say why it is not.

    The load order the runtime builds is this graph, so a parent that names no unit is a class whose
    programs would be loaded before the class they inherit names from. For the SCHEDULE spaces that
    is fatal and refuses the seam: it is exactly the failure that would otherwise appear as an NPC
    silently choosing nothing.

    One parent legitimately names no unit. The squad-slot root (`0x10920484`) is a `CAI_LocalIdSpace`
    constructed on its own in `0x10265660` with the root flag set, and nothing ever registers into
    it -- the two global squad slots are seeded straight into the namespace by `0x10316e80`. Every
    class's squad-slot space parents on it, and falling through to it finds nothing, which is why it
    is an anomaly row rather than a refusal.
    """

    index = {
        space.address for owner in owners for space in owner.spaces.values()
    }
    orphans: dict[int, list[str]] = {}
    for owner in owners:
        for category, space in owner.spaces.items():
            if space.parent is None or space.parent in index:
                continue
            if category == "schedule":
                raise CensusError(
                    f"{owner.class_name}: its schedule space parents on {space.parent:#010x}, "
                    "which no unit initialises"
                )
            orphans.setdefault(space.parent, []).append(f"{owner.key}:{category}")

    for parent, users in sorted(orphans.items()):
        anomalies.append(
            {
                "row": "parent-space-with-no-unit",
                "space": f"{parent:#010x}",
                "users": sorted(users),
                "reason": "a root space nothing registers into; falling through to it finds nothing",
            }
        )


def _cut_region(image: PEImage) -> dict[int, tuple[int, int, bytes]]:
    """Every `.rdata` run the cut region admits, keyed by its virtual address.

    The rule is retail's own acceptance test, stated in `docs/contracts/seam_map_ai_schedule.md`:
    a NUL-terminated printable-ASCII run whose first whitespace-delimited token is `Schedule`.

    The scan anchors on the token and then expands to the run's own NUL boundaries, rather than
    walking the section NUL by NUL. `.rdata` is not a string table -- it carries vtables, floats
    and jump tables between the strings -- so a boundary walk misaligns on the first blob of binary
    and swallows the next several hundred texts inside an unprintable "run".
    """

    start, rdata = image.section_bytes(_TEXT_POOL_SECTION)
    found: dict[int, tuple[int, int, bytes]] = {}
    seen_starts: set[int] = set()
    for match in re.finditer(rb"Schedule", rdata, re.IGNORECASE):
        run_start = rdata.rfind(b"\x00", 0, match.start()) + 1
        if run_start in seen_starts:
            continue
        seen_starts.add(run_start)
        run_end = rdata.find(b"\x00", match.start())
        if run_end < 0:
            continue
        run = rdata[run_start:run_end]
        if not run or not _SCHEDULE_RECORD.match(run) or not _is_printable(run):
            continue
        va = image.offset_to_va(start + run_start)
        if va is not None:
            found[int(va)] = (start + run_start, len(run), bytes(run))
    return found


def _is_printable(run: bytes) -> bool:
    return all(byte == 0x09 or byte == 0x0A or byte == 0x0D or 0x20 <= byte < 0x7F for byte in run)


def _prove_partition(
    owners: list[Owner], region: dict[int, tuple[int, int, bytes]], anomalies: list[dict]
) -> None:
    """Refuse the seam unless the cut region and the fed set are the same set of runs."""

    fed: dict[int, str] = {}
    for owner in owners:
        for text in owner.texts:
            if text.string_va in fed:
                raise CensusError(
                    f"{text.string_va:#010x}: fed by both {fed[text.string_va]} and "
                    f"{owner.class_name}; a text is fed once"
                )
            fed[text.string_va] = owner.class_name

    unfed = sorted(set(region) - set(fed))
    unadmitted = sorted(set(fed) - set(region))
    if unfed or unadmitted:
        detail = []
        for va in unfed[:8]:
            detail.append(f"unfed {va:#010x}: {region[va][2][:60]!r}")
        for va in unadmitted[:8]:
            detail.append(f"not admitted by the region rule {va:#010x} ({fed[va]})")
        raise CensusError(
            "the cut region and the fed set disagree: "
            f"{len(unfed)} unfed, {len(unadmitted)} unadmitted; " + "; ".join(detail)
        )


def _check_registrations(owners: list[Owner], anomalies: list[dict]) -> None:
    """Record, per owner, names registered with no text and texts naming no registration."""

    for owner in owners:
        registered = {row.name for row in owner.registrations_of("schedule")}
        declared = {text.name for text in owner.texts if text.name}
        for name in sorted(registered - declared):
            anomalies.append(
                {
                    "row": "registered-name-with-no-text",
                    "unit": owner.key,
                    "name": name,
                    "reason": "the space registers this schedule id and no text declares it",
                }
            )
        for name in sorted(declared - registered):
            anomalies.append(
                {
                    "row": "text-name-not-registered",
                    "unit": owner.key,
                    "name": name,
                    "reason": "a text declares a name this owner's space does not register",
                }
            )


def summary(census: Census) -> dict:
    """The counts the root unit publishes, measured rather than asserted."""

    feeding = [owner for owner in census.owners if owner.texts]
    return {
        "initBodies": len(census.owners),
        "feedingOwners": len(feeding),
        "texts": sum(len(owner.texts) for owner in census.owners),
        "registrations": {
            category: sum(len(owner.registrations_of(category)) for owner in census.owners)
            for category in CATEGORIES
        },
        "regionRuns": len(census.region),
        "deadDoors": len(census.dead_doors),
        "anomalies": len(census.anomalies),
    }
