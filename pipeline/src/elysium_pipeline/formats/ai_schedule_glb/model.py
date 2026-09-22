"""Identity and stable-name rules for the ai-schedule GLB seam.

A unit is one `CAI_ClassScheduleIdSpace` -- not one class. Four pairs of classes share a space
through a common slot-580 getter, and the second of each pair has no init body at all, so keying on
the classname would publish one unit twice and leave four classes with none. The key names the
OWNING class, the one whose body runs `CAI_LocalIdSpace::Init` on the space; every classname whose
getter answers that space is listed in the unit's identity.

One more unit has no owning class: the root, `vtmb:ai-schedule:vocabulary`, which carries the
parser's operand tables, the global namespaces, the class map and the image partition. Those are
the PARSER's, not any one class's, and the unit contract's first rule is that a unit never carries
data another unit owns.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any

from elysium_pipeline.formats.unit_contract import extension_name
from elysium_pipeline.formats.unit_contract import asset_id as _asset_id
from elysium_pipeline.formats.unit_contract.origin import SourceMember

KIND = "ai-schedule"
KIND_TITLE = "AI Schedule"
#: 1.0.0 -- the seam adopts the source capsule from its first publication, and its root unit is
#: the first to carry an image member WITHOUT one (`seam_map_unit_contract.md`, the
#: executable-image rule).
SCHEMA_VERSION = "1.0.0"
AI_SCHEDULE_EXTENSION = extension_name(KIND)

#: The one source member of the whole seam.
SOURCE_MEMBER = "dlls/vampire.dll"

#: The root unit's key. It owns the vocabulary and the image partition.
ROOT_KEY = "vocabulary"

#: Where the import lane deploys, below `Content/ElysiumCorpus/`.
CORPUS_ROOT = "ai/schedules/"

#: The extension a deployed text carries. Retail's own generic loader spells it `.sch`
#: (`vdata/schedules/<name>.sch`, `0x1030f220`), so the deployed corpus uses the engine's own
#: spelling even though that loader has no caller.
TEXT_SUFFIX = ".sch"


class AiScheduleModelError(ValueError):
    """A name or identity does not fit the seam's rules."""


def normalize_key(raw: str) -> str:
    """The unit key: the owning classname, lower-cased.

    The singular export command tolerates the family prefix and the `.glb` suffix on its argument,
    as every seam's does.
    """

    if not raw:
        raise AiScheduleModelError("an ai-schedule key is empty")
    normalized = str(raw).strip().replace("\\", "/").lower()
    if normalized.startswith("ai-schedules/"):
        normalized = normalized[len("ai-schedules/"):]
    if normalized.endswith(".glb"):
        normalized = normalized[: -len(".glb")]
    normalized = normalized.strip("/")
    if not normalized or "/" in normalized:
        raise AiScheduleModelError(f"invalid ai-schedule key {raw!r}: expected one classname")
    return normalized


def asset_id(key: str) -> str:
    return _asset_id(KIND, normalize_key(key))


def output_relative_path(key: str) -> str:
    """Where the unit lands BELOW its family root, which `GlbUnitSeam.family` already names."""

    return f"{normalize_key(key)}.glb"


def corpus_directory(key: str) -> str:
    """Where this unit's texts and sidecar deploy, below the corpus root."""

    return f"{CORPUS_ROOT}{normalize_key(key)}"


def text_file_name(schedule_name: str) -> str:
    """The deployed leaf for one text.

    One file per TEXT rather than per owner: retail's failure is per text, so "which text failed to
    load" should be a filename in the log rather than a line number inside a blob.
    """

    folded = str(schedule_name).strip().lower()
    if not folded:
        raise AiScheduleModelError("a schedule text declares no name")
    safe = "".join(char if char.isalnum() or char in "_-" else "_" for char in folded)
    return f"{safe}{TEXT_SUFFIX}"


def member_path(key: str, schedule_name: str) -> str:
    """One text's member path: the image, then the corpus-relative path after a `#`.

    The tail after `#` IS the deploy path, which is what lets the import lane split one string and
    know nothing else about this seam (the precedent is `sound_script_glb`'s table members).
    """

    return f"{SOURCE_MEMBER}#{corpus_directory(key)}/{text_file_name(schedule_name)}"


def split_member_path(path: str) -> str | None:
    """The corpus-relative deploy path a member path carries, or None when it carries none."""

    _, separator, tail = str(path).partition("#")
    if not separator or not tail.startswith(CORPUS_ROOT):
        return None
    return tail


@dataclass(frozen=True, slots=True)
class SpaceRow:
    """One of the unit's four `CAI_LocalIdSpace`s, as the unit publishes it."""

    category: str
    address: int
    namespace: int
    parent: int | None
    parent_unit: str | None
    init_va: int

    def to_json(self) -> dict[str, Any]:
        return {
            "address": f"{self.address:#010x}",
            "namespace": f"{self.namespace:#010x}",
            "parent": f"{self.parent:#010x}" if self.parent else None,
            "parentUnit": self.parent_unit,
            "initVa": f"{self.init_va:#010x}",
        }


@dataclass
class SpaceUnitModel:
    """One space unit: its identity, its four spaces, its registrations, its texts and records."""

    key: str
    class_name: str
    class_names: list[str]
    init_body: int
    spaces: dict[str, SpaceRow] = field(default_factory=dict)
    registrations: dict[str, list[dict]] = field(default_factory=dict)
    texts: list[dict] = field(default_factory=list)
    records: list[dict] = field(default_factory=list)
    activities: list[str] = field(default_factory=list)
    members: list[SourceMember] = field(default_factory=list)
    dependencies: list[dict] = field(default_factory=list)
    anomalies: list[dict] = field(default_factory=list)
    omissions: list[dict] = field(default_factory=list)
    ledger: list[dict] = field(default_factory=list)
    typed_unidentified: list[dict] = field(default_factory=list)

    @property
    def asset(self) -> str:
        return asset_id(self.key)


@dataclass
class VocabularyUnitModel:
    """The root unit: the parser's vocabulary, the class map and the image partition."""

    key: str = ROOT_KEY
    member: SourceMember | None = None
    partition: list[dict] = field(default_factory=list)
    namespaces: list[dict] = field(default_factory=list)
    squad_slots: list[dict] = field(default_factory=list)
    vocabulary: dict[str, Any] = field(default_factory=dict)
    classes: list[dict] = field(default_factory=list)
    order: dict[str, Any] = field(default_factory=dict)
    dead_doors: list[dict] = field(default_factory=list)
    census: dict[str, Any] = field(default_factory=dict)
    dependencies: list[dict] = field(default_factory=list)
    anomalies: list[dict] = field(default_factory=list)
    omissions: list[dict] = field(default_factory=list)
    ledger: list[dict] = field(default_factory=list)

    @property
    def asset(self) -> str:
        return asset_id(self.key)


__all__ = [
    "AI_SCHEDULE_EXTENSION",
    "AiScheduleModelError",
    "CORPUS_ROOT",
    "KIND",
    "KIND_TITLE",
    "ROOT_KEY",
    "SCHEMA_VERSION",
    "SOURCE_MEMBER",
    "SpaceRow",
    "SpaceUnitModel",
    "VocabularyUnitModel",
    "asset_id",
    "corpus_directory",
    "member_path",
    "normalize_key",
    "output_relative_path",
    "split_member_path",
    "text_file_name",
]
