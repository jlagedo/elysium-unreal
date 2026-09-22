"""The ai-schedule seam, over a synthetic PE32 built in this file.

CI never has the retail DLL, so the image these tests read is assembled here: a `.text` with real
call sites and real thunks, and a `.data` string pool with real schedule texts. That is enough to
exercise every shape the recovery depends on -- the two registration entry points, the pointer
indirection the base feeds through, a shared slot-580 getter, and the region rule -- and it means a
regression in the walk fails here rather than only on a machine that owns the game.

The parser's own tests need no image at all.
"""

from __future__ import annotations

import struct

import pytest

from elysium_pipeline.formats.ai_schedule_glb import census as census_module
from elysium_pipeline.formats.ai_schedule_glb import parser
from elysium_pipeline.formats.ai_schedule_glb.image import ImageError, PEImage
from elysium_pipeline.formats.ai_schedule_glb.model import (
    member_path,
    normalize_key,
    split_member_path,
    text_file_name,
)

IMAGE_BASE = 0x10000000
TEXT_RVA = 0x1000
DATA_RVA = 0x40000


# --- the parser -----------------------------------------------------------------------------------


def test_the_tokenizer_splits_a_prefixed_operand_into_three_tokens():
    # `NPCFlag:FORCE_RELAXED_ANIMS` is three tokens because `:` is a one-character token, and the
    # parser tests the middle one against the literal ":".
    tokens = parser.tokenize(b"NPCFlag:FORCE_RELAXED_ANIMS")
    assert [token.text for token in tokens] == ["NPCFlag", ":", "FORCE_RELAXED_ANIMS"]


def test_the_tokenizer_drops_a_comment_and_groups_a_quoted_run():
    tokens = parser.tokenize(b'one // two three\n"four five" six')
    assert [token.text for token in tokens] == ["one", "four five", "six"]


def test_a_text_whose_first_token_is_not_schedule_loads_nothing_and_succeeds():
    assert parser.parse(b"") == []
    assert parser.parse(b"Tasks TASK_WAIT 1") == []


def test_a_record_carries_its_tasks_interrupts_and_flags():
    body = (
        b"\n\tSchedule\n\t\tSCHED_TEST\tTasks"
        b"\t\tTASK_STOP_MOVING\t0"
        b"\t\tTASK_SET_ACTIVITY\tACTIVITY:ACT_IDLE"
        b"\t\tTASK_WAIT\t0.2"
        b"\tInterrupts\t\tCOND_NEW_ENEMY\t\t!COND_SEE_ENEMY"
        b"\tFlags\t\tDELAY_INTERRUPTS\n"
    )
    record = parser.parse(body)[0]
    assert record.name == "SCHED_TEST"
    assert [task.name for task in record.tasks] == [
        "TASK_STOP_MOVING",
        "TASK_SET_ACTIVITY",
        "TASK_WAIT",
    ]
    assert record.tasks[1].operand.prefix == "activity"
    assert record.tasks[1].operand.spelling == "ACT_IDLE"
    assert [(row.name, row.inverted) for row in record.interrupts] == [
        ("COND_NEW_ENEMY", False),
        ("COND_SEE_ENEMY", True),
    ]
    assert record.flag_word == 1


def test_the_three_raw_word_prefixes_are_marked_and_the_others_are_not():
    body = (
        b"Schedule SCHED_RAW Tasks"
        b" TASK_SET_NPC_FLAG NPCFlag:SLEEPING"
        b" TASK_SET_ACTIVITY ACTIVITY:ACT_IDLE"
    )
    tasks = parser.parse(body)[0].tasks
    assert tasks[0].operand.raw_word is True
    assert tasks[1].operand.raw_word is False


@pytest.mark.parametrize(
    "body, row",
    [
        (b"Schedule SCHED_A Tasks TASK_WAIT 1 Schedule SCHED_A Tasks TASK_WAIT 1",
         "duplicate-schedule-name"),
        (b"Schedule SCHED_A TASK_WAIT 1", "missing-tasks"),
        (b"Schedule SCHED_A Tasks TASK_WAIT", "missing-operand"),
        (b"Schedule SCHED_A Tasks TASK_WAIT TASK_STOP_MOVING 0", "bad-syntax-at-task"),
        (b"Schedule SCHED_A Tasks TASK_WAIT Interrupts COND_NEW_ENEMY", "bad-syntax-at-task"),
        (b"Schedule SCHED_A Tasks TASK_WAIT Nonsense:VALUE", "unknown-prefix"),
    ],
)
def test_the_failure_table_refuses_what_retail_refuses(body, row):
    with pytest.raises(parser.ScheduleTextError) as caught:
        parser.parse(body)
    assert caught.value.row == row


def test_the_sixty_fifth_task_is_a_failure():
    body = b"Schedule SCHED_LONG Tasks" + b" TASK_WAIT 1" * (parser.MAX_TASKS + 1)
    with pytest.raises(parser.ScheduleTextError) as caught:
        parser.parse(body)
    assert caught.value.row == "task-cap"


def test_an_unknown_condition_and_a_none_flag_do_not_fail_the_text():
    # Retail DevMsgs both and loads the text anyway, so an authored `Flags NONE` is harmless.
    record = parser.parse(
        b"Schedule SCHED_A Tasks TASK_WAIT 1 Interrupts COND_NOT_A_REAL_ONE Flags NONE"
    )[0]
    assert [row.name for row in record.interrupts] == ["COND_NOT_A_REAL_ONE"]
    assert record.flag_word == 0


def test_a_bare_non_number_operand_reads_as_zero_without_failing():
    record = parser.parse(b"Schedule SCHED_A Tasks TASK_WAIT banana")[0]
    assert record.tasks[0].operand.form == "number"
    assert parser.is_number(record.tasks[0].operand.spelling) is False


# --- identity -------------------------------------------------------------------------------------


def test_a_member_path_carries_its_own_deploy_path():
    path = member_path("cnpc_vbrujah", "SCHED_VBRUJAH_WALK")
    assert path.startswith("dlls/vampire.dll#")
    assert split_member_path(path) == "ai/schedules/cnpc_vbrujah/sched_vbrujah_walk.sch"


def test_the_image_member_carries_no_deploy_path():
    assert split_member_path("dlls/vampire.dll") is None


def test_a_key_tolerates_the_family_prefix_and_the_suffix():
    assert normalize_key("ai-schedules/CNPC_VBrujah.glb") == "cnpc_vbrujah"


def test_a_text_leaf_is_the_schedule_name_folded():
    assert text_file_name("SCHED_TROIKA_CHASE_ENEMY_FAILED") == (
        "sched_troika_chase_enemy_failed.sch"
    )


# --- the image mapper -----------------------------------------------------------------------------


def test_a_short_buffer_is_refused_as_an_image_rather_than_crashing():
    # The corpus-index walk asks every seam for its keys against a synthetic index, so "not our
    # image" has to be one nameable exception rather than whatever the parse tripped over.
    with pytest.raises(ImageError):
        PEImage(b"dlls/vampire.dll")


# --- the synthetic image --------------------------------------------------------------------------


class _Builder:
    """Assembles a minimal PE32 with a `.text` and a `.data` this seam can read."""

    def __init__(self) -> None:
        self.text = bytearray()
        self.data = bytearray(b"\0")          # a leading NUL so no run starts at offset 0

    def string(self, body: bytes) -> int:
        offset = len(self.data)
        self.data.extend(body + b"\0")
        return IMAGE_BASE + DATA_RVA + offset

    def dword(self, value: int) -> int:
        while len(self.data) % 4:
            self.data.append(0)
        offset = len(self.data)
        self.data.extend(struct.pack("<I", value))
        return IMAGE_BASE + DATA_RVA + offset

    @property
    def here(self) -> int:
        return IMAGE_BASE + TEXT_RVA + len(self.text)

    def emit(self, blob: bytes) -> None:
        self.text.extend(blob)

    def pad(self, count: int = 8) -> None:
        self.text.extend(b"\xCC" * count)

    def call(self, target: int) -> None:
        site = self.here
        self.text.extend(b"\xE8" + struct.pack("<i", target - (site + 5)))

    def jmp(self, target: int) -> int:
        site = self.here
        self.text.extend(b"\xE9" + struct.pack("<i", target - (site + 5)))
        return site

    def build(self) -> bytes:
        text = bytes(self.text)
        data = bytes(self.data)
        text_raw, data_raw = 0x400, 0x400 + _align(len(text), 0x200)
        header = bytearray(b"\0" * 0x400)
        header[0:2] = b"MZ"
        pe = 0x80
        struct.pack_into("<I", header, 0x3C, pe)
        header[pe:pe + 4] = b"PE\0\0"
        struct.pack_into("<H", header, pe + 6, 2)              # two sections
        struct.pack_into("<H", header, pe + 20, 0xE0)          # optional header size
        optional = pe + 24
        struct.pack_into("<H", header, optional, 0x10B)        # PE32
        struct.pack_into("<I", header, optional + 28, IMAGE_BASE)
        table = optional + 0xE0
        for index, (name, rva, raw, size) in enumerate(
            ((b".text", TEXT_RVA, text_raw, len(text)), (b".data", DATA_RVA, data_raw, len(data)))
        ):
            base = table + index * 40
            header[base:base + len(name)] = name
            struct.pack_into("<IIII", header, base + 8, size, rva, _align(size, 0x200), raw)
        blob = bytearray(header)
        blob.extend(text.ljust(_align(len(text), 0x200), b"\0"))
        blob.extend(data.ljust(_align(len(data), 0x200), b"\0"))
        return bytes(blob)


def _align(value: int, to: int) -> int:
    return ((value + to - 1) // to) * to


def _schedule_text(name: str, task: str = "TASK_WAIT", operand: str = "1") -> bytes:
    return f"\n\tSchedule\n\t\t{name}\tTasks\t\t{task}\t\t{operand}\n".encode("ascii")


def _stub_image() -> bytes:
    """One base owner feeding through a pointer table, one species owner feeding from a vector.

    The addresses this seam anchors on are real code in the retail image and arbitrary here, so the
    builder lays the anchor bodies down first and the census is pointed at them by monkeypatching
    `ANCHORS` in the test that uses it.
    """

    builder = _Builder()
    builder.pad()

    anchors: dict[str, int] = {}
    for label in (
        "parse", "init", "register", "register_schedule", "register_task",
        "register_condition", "pair_vector_ctor", "pair_append", "pair_append_alt",
        "pair_append_direct", "text_append", "text_append_alt",
    ):
        anchors[label] = builder.here
        builder.emit(b"\xC2\x08\x00")                      # RET 8 -- two stack arguments
        builder.pad()

    # `CAI_LocalIdSpace::Register` is reached through a thunk, as it is in the image.
    register_thunk = builder.jmp(anchors["register"])
    builder.pad()

    base_space = 0x1090FF08
    species_space = 0x1093A740
    strings = {
        "base": builder.string(b"CAI_BaseNPC"),
        "species": builder.string(b"CNPC_VStub"),
        "schedule": builder.string(b"schedule"),
        "idle": builder.string(b"IDLE_STAND"),
        "walk": builder.string(b"SCHED_STUB_WALK"),
    }
    base_text = builder.string(_schedule_text("IDLE_STAND"))
    species_text = builder.string(_schedule_text("SCHED_STUB_WALK"))
    # A `Schedule`-shaped diagnostic the region rule must reject: no `Tasks` section.
    builder.string(b"Schedule has invalid state ID '%s'\n")
    cell = builder.dword(base_text)

    # --- the base: registers through the thunk, feeds through a pointer indirection -------------
    base_body = builder.here
    # Arguments go right to left, as the compiler emits them: className, category, id, name.
    builder.emit(b"\x68" + struct.pack("<I", strings["base"]))          # push className
    builder.emit(b"\x68" + struct.pack("<I", strings["schedule"]))      # push "schedule"
    builder.emit(b"\x6A\x01")                                          # push localId 1
    builder.emit(b"\x68" + struct.pack("<I", strings["idle"]))          # push name
    builder.emit(b"\xB9" + struct.pack("<I", base_space))               # mov ecx, space
    builder.call(register_thunk)
    builder.emit(b"\x68" + struct.pack("<I", base_space))               # push space
    builder.emit(b"\xA1" + struct.pack("<I", cell))                     # mov eax, [cell]
    builder.emit(b"\x50")                                              # push eax
    builder.emit(b"\x68" + struct.pack("<I", strings["base"]))          # push className
    builder.call(anchors["parse"])
    builder.emit(b"\xC3")
    builder.pad()

    # --- the species: a pair vector, a register loop, a text vector ----------------------------
    species_body = builder.here
    builder.emit(b"\x6A\x00\x6A\x00")                                  # two argument pushes
    builder.emit(b"\x8D\x4C\x24\x08")                                  # lea ecx, [esp+8]
    builder.call(anchors["pair_vector_ctor"])
    builder.emit(b"\x6A\x00\x6A\x00")
    builder.emit(b"\x8D\x4C\x24\x08")                                  # the same vector
    builder.emit(b"\xC7\x44\x24\x10" + struct.pack("<I", strings["walk"]))
    builder.emit(b"\xC7\x44\x24\x14" + struct.pack("<I", 0x158))
    builder.call(anchors["pair_append"])
    builder.emit(b"\x68" + struct.pack("<I", 0))                       # push parent
    builder.emit(b"\x68" + struct.pack("<I", 0x109203CC))              # push namespace
    builder.emit(b"\xB9" + struct.pack("<I", species_space))           # mov ecx, space
    builder.call(anchors["init"])
    # The register LOOP, as the compiler writes it: the vector's data pointer is loaded once, the
    # class, id and name come out of registers, and the category is the only immediate.
    builder.emit(b"\x8B\x4C\x24\x00")                                  # mov ecx, [esp+0] -- vector
    builder.emit(b"\xB8" + struct.pack("<I", strings["species"]))      # mov eax, className
    builder.emit(b"\x50")                                              # push eax
    builder.emit(b"\x68" + struct.pack("<I", strings["schedule"]))      # push "schedule"
    builder.emit(b"\x52")                                              # push edx -- localId
    builder.emit(b"\x51")                                              # push ecx -- name
    builder.emit(b"\xB9" + struct.pack("<I", species_space))
    builder.call(register_thunk)
    builder.emit(b"\x6A\x00\x6A\x00")
    builder.emit(b"\x8D\x4C\x24\x08")
    builder.emit(b"\xC7\x44\x24\x10" + struct.pack("<I", species_text))
    builder.call(anchors["text_append"])
    builder.emit(b"\x68" + struct.pack("<I", species_space))
    builder.emit(b"\x68" + struct.pack("<I", species_text))
    builder.emit(b"\x68" + struct.pack("<I", strings["species"]))
    builder.call(anchors["parse"])
    builder.emit(b"\xC3")
    builder.pad()

    del base_body, species_body
    return builder.build(), anchors


def test_the_synthetic_image_parses_as_a_pe32():
    data, _ = _stub_image()
    image = PEImage(data)
    assert {section["name"] for section in image.sections} == {".text", ".data"}
    assert image.image_base == IMAGE_BASE


def test_the_region_rule_rejects_the_parsers_own_diagnostics():
    # "Schedule has invalid state ID '%s'" opens with the token `Schedule` and is a format string,
    # not a program. Requiring a `Tasks` third token is what separates them, and it is retail's
    # own requirement rather than a filter invented to make a count come out.
    data, _ = _stub_image()
    image = PEImage(data)
    region = census_module._cut_region(image)
    bodies = {body for _, _, body in region.values()}
    assert any(b"IDLE_STAND" in body for body in bodies)
    assert not any(b"invalid state ID" in body for body in bodies)


def test_the_census_reads_both_owners_out_of_the_synthetic_image(monkeypatch):
    data, anchors = _stub_image()
    monkeypatch.setattr(census_module, "ANCHORS", anchors)
    monkeypatch.setattr(census_module, "DEAD_SCH_LOADER", 0)
    census_module._MEMO.clear()
    census = census_module.build(data)
    owners = {owner.class_name: owner for owner in census.owners}
    assert set(owners) == {"CAI_BaseNPC", "CNPC_VStub"}
    assert [text.name for text in owners["CAI_BaseNPC"].texts] == ["IDLE_STAND"]
    assert [text.name for text in owners["CNPC_VStub"].texts] == ["SCHED_STUB_WALK"]
    assert [(row.name, row.local_id) for row in owners["CAI_BaseNPC"].registrations] == [
        ("IDLE_STAND", 1)
    ]
    census_module._MEMO.clear()


def test_a_one_line_getter_is_read_and_anything_else_is_refused():
    """`_constant_getter` accepts exactly `MOV EAX, imm32 / RET` and nothing else.

    Three of the seam's recoveries stand on it -- Troika's three space-init arguments, and every
    class's slot-580 answer -- and each of them is only decidable BECAUSE the body is that one
    shape. A getter with a branch in it is a different recovery and has to say so.
    """

    builder = _Builder()
    builder.pad()
    getter = builder.here
    builder.emit(b"\xB8" + struct.pack("<I", 0x1090FF08) + b"\xC3")   # MOV EAX, imm32 ; RET
    builder.pad()
    falls_through = builder.here
    builder.emit(b"\xB8" + struct.pack("<I", 0x1090FF08) + b"\x90")   # ... NOP, so not a getter
    builder.pad()
    xor_eax = builder.here
    builder.emit(b"\x33\xC0\xC3")                                    # XOR EAX, EAX ; RET
    builder.pad()
    # Reached through a JMP thunk, which is how slot 580 reaches it in the retail image.
    thunk = builder.jmp(getter)
    builder.pad()
    image = PEImage(builder.build())

    assert census_module._constant_getter(image, getter) == 0x1090FF08
    assert census_module._constant_getter(image, thunk) == 0x1090FF08
    assert census_module._constant_getter(image, falls_through) is None
    assert census_module._constant_getter(image, xor_eax) is None
    assert census_module._constant_getter(image, 0x7FFFFFFF) is None


def test_a_schedule_space_parenting_on_nothing_refuses_the_seam():
    """Every schedule space must parent on a space some unit initialises.

    The load order the runtime builds IS this graph, so a schedule space whose parent names no unit
    is a class loaded before the class it inherits names from -- which shows up not as an error but
    as an NPC that silently chooses nothing. A squad-slot space parenting on nothing is a different
    fact and only a row: the squad-slot root is real, and nothing registers into it.
    """

    def space(category, address, parent):
        return census_module.Space(
            category=category, address=address, namespace=0x109203CC, parent=parent, init_va=0
        )

    base = census_module.Owner(class_name="CAI_BaseNPC", init_body=0x1000)
    base.spaces["schedule"] = space("schedule", 0x1090FF08, None)
    child = census_module.Owner(class_name="CNPC_VStub", init_body=0x2000)
    child.spaces["schedule"] = space("schedule", 0x1093A740, 0x1090FF08)
    child.spaces["squadslot"] = space("squadslot", 0x1093A788, 0x10920484)

    anomalies: list[dict] = []
    census_module._prove_parents([base, child], anomalies)
    assert [row["row"] for row in anomalies] == ["parent-space-with-no-unit"]
    assert anomalies[0]["space"] == "0x10920484"

    child.spaces["schedule"] = space("schedule", 0x1093A740, 0xDEADBEEF)
    with pytest.raises(census_module.CensusError, match="0xdeadbeef"):
        census_module._prove_parents([base, child], [])


def test_the_rtti_walk_answers_nothing_for_an_image_with_no_rtti():
    """A `.text` and a `.data` and nothing else names no classes -- an answer, not a failure.

    Every synthetic image in this file is exactly that shape, so a walk that refused one would make
    the whole seam untestable without the retail DLL.
    """

    from elysium_pipeline.formats.ai_schedule_glb import rtti

    builder = _Builder()
    builder.emit(b"\xC3")
    builder.pad()
    image = PEImage(builder.build())
    assert rtti.classes_deriving_from(image, "CAI_BaseNPC") == []
