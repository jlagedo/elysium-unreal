"""How one unit names another.

A unit never inlines the data another unit owns: it names it by stable ID, declares the reference
once in `dependencies`, and binds it object-locally through a reference extension where a glTF
object is what holds the binding.
"""

from __future__ import annotations

from typing import Any

MATERIAL_REFERENCE = "ELYSIUM_material_reference"
MODEL_REFERENCE = "ELYSIUM_model_reference"
TEXTURE_REFERENCE = "ELYSIUM_texture_reference"
ASSET_REFERENCE = "ELYSIUM_asset_reference"

#: Every cross-reference extension, for a seam that declares the ones it used.
REFERENCE_EXTENSIONS = (
    MATERIAL_REFERENCE,
    MODEL_REFERENCE,
    TEXTURE_REFERENCE,
    ASSET_REFERENCE,
)

#: The keys a dependency row may carry beyond the four it always carries. `byteLength` and
#: `sha256` pin the referenced bytes where the referrer's own decode depended on them.
DEPENDENCY_OPTIONAL_KEYS = ("resolution", "byteLength", "sha256")

#: The four a dependency row always carries, in publication order.
DEPENDENCY_REQUIRED_KEYS = ("role", "asset", "sourcePath", "resolved")


class ReferenceError(ValueError):
    """A reference does not carry what a dependency row needs."""


def normalize_key(key: str) -> str:
    """The joinable spelling of a unit key: lower case, forward slashes."""

    return str(key).replace("\\", "/").lower()


def asset_id(kind: str, key: str) -> str:
    """The stable identity of one unit: `vtmb:<kind>:<key>`, folded and forward-slashed."""

    return f"vtmb:{kind}:{normalize_key(key)}"


def missing_sentinel(kind: str, key: str) -> str:
    """The identity a reference keeps when the referenced kind's own rules make it unreachable.

    The sentinel namespace keeps the referrer's authored spelling visible without claiming that a
    unit exists for it: the row is `resolved: false`, produces no dependency, and is graded
    `omitted-proven` with the reason the seam names.
    """

    return f"vtmb:missing-{kind}:{normalize_key(key)}"


def dependency(
    role: str,
    asset: str,
    source_path: str,
    resolved: bool,
    **optional: Any,
) -> dict[str, Any]:
    """One `dependencies` row. Optional keys are emitted only where the caller passed them."""

    unknown = [key for key in optional if key not in DEPENDENCY_OPTIONAL_KEYS]
    if unknown:
        raise ReferenceError(f"unknown dependency field(s) {', '.join(sorted(unknown))}")
    if not role or not asset:
        raise ReferenceError("a dependency row names a role and the identity it needs")
    row: dict[str, Any] = {
        "role": str(role),
        "asset": str(asset),
        "sourcePath": str(source_path),
        "resolved": bool(resolved),
    }
    for key in DEPENDENCY_OPTIONAL_KEYS:
        if key in optional:
            row[key] = optional[key]
    return row


def reference_extension(name: str, asset: str) -> dict[str, Any]:
    """The object-local binding: `{"extensions": {<name>: {"asset": <id>}}}`."""

    if name not in REFERENCE_EXTENSIONS:
        raise ReferenceError(f"unknown reference extension {name!r}")
    if not asset:
        raise ReferenceError(f"{name} binds nothing")
    return {"extensions": {name: {"asset": str(asset)}}}
