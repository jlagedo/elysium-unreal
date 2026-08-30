"""Plain semantic values shared by the isolated decoder and GLB writer."""

from __future__ import annotations

from dataclasses import asdict, dataclass, field, is_dataclass
from pathlib import PurePosixPath
from typing import Any


CHARACTER_EXTENSION = "ELYSIUM_vtmb_character"
MATERIAL_EXTENSION = "ELYSIUM_material_reference"
SCHEMA_VERSION = "1.2.0"


@dataclass(frozen=True, slots=True)
class SourceIdentity:
    role: str
    path: str
    origin: dict[str, Any]
    byte_length: int
    sha256: str


@dataclass(slots=True)
class CharacterModel:
    model_path: str
    asset_id: str
    sources: list[SourceIdentity]
    header: dict[str, Any]
    bones: list[dict[str, Any]]
    lods: list[dict[str, Any]]
    materials: list[dict[str, Any]]
    skin_families: list[list[str]]
    local_animations: list[dict[str, Any]]
    sequences: list[dict[str, Any]]
    pose_parameters: list[dict[str, Any]]
    attachments: list[dict[str, Any]]
    hitbox_sets: list[dict[str, Any]]
    ik_chains: list[dict[str, Any]]
    facial: dict[str, Any]
    procedural: dict[str, Any]
    secondary_motion: list[dict[str, Any]]
    cloth: dict[str, Any]
    physics: dict[str, Any] | None
    dependencies: list[dict[str, Any]]
    variant_comparison: dict[str, Any]
    byte_coverage: list[dict[str, Any]]
    typed_unidentified: list[dict[str, Any]]
    omitted_proven: list[dict[str, Any]]
    unresolved: list[dict[str, Any]] = field(default_factory=list)
    unsupported: list[dict[str, Any]] = field(default_factory=list)


def asset_id(model_path: str) -> str:
    """Stable character identity for one normalized models/character path."""
    normalized = model_path.replace("\\", "/").lower()
    prefix = "models/character/"
    if not normalized.startswith(prefix) or not normalized.endswith(".mdl"):
        raise ValueError(
            f"character model must be below {prefix} and end in .mdl: {model_path}"
        )
    return "vtmb:character-body:" + normalized[len(prefix):-4]


def output_relative_path(model_path: str) -> PurePosixPath:
    """Path below the isolated ``glb/characters`` export root."""
    normalized = model_path.replace("\\", "/").lower()
    prefix = "models/character/"
    return PurePosixPath(normalized[len(prefix):-4] + ".glb")


def plain(value: Any) -> Any:
    """Recursively turn decoder records into deterministic JSON-compatible values."""
    if is_dataclass(value):
        return plain(asdict(value))
    if hasattr(value, "_asdict"):
        return {key: plain(item) for key, item in value._asdict().items()}
    if isinstance(value, dict):
        return {str(key): plain(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [plain(item) for item in value]
    if hasattr(value, "item"):
        return value.item()
    return value
