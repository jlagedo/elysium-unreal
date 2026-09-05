"""Stage the texture corpus out of the published GLB units, and measure what Unreal built.

`uv run elysium import textures` lands every `$ELYSIUM_EXPORT_V2_ROOT/textures/**/*.glb` as one
Unreal texture asset below `/ElysiumBaked/Textures` (`docs/architecture/seam_map_texture.md` →
"Import"). This module is the offline half of that lane:

**Phase 1 — stage.** For each unit: hash the GLB, parse its KTX2 payload, reorder a cubemap's
faces into Unreal's order (`texture_cube`, block-exact), decode every block-compressed level to
8-bit pixels and write them as an uncompressed DX10 DDS (`texture_dds` -- Unreal 5.8 refuses a
BC DDS on both of its import paths, so the lane's own spec decoder is the one decode a block
gets), decide the colour/data role from the material bindings (`texture_roles`), and write the
DDS, a provenance sidecar and one `manifest.json` under
`$ELYSIUM_WORK_ROOT/import/textures/`. The manifest is the contract the editor phase
(`pipeline/unreal/import_textures.py`) consumes; its schema is the one the seam document prints.

**Phase 3 — measure.** After the editor phase has written each built asset's mip 0 back beside
the staged DDS (`<stem>.built.dds`), decode both and record the per-unit texel delta, so the
accepted re-encode loss is a number in `measure.json` rather than a belief.

Staging is idempotent -- a unit whose hash the existing sidecar already names, and whose staged
DDS still hashes as that sidecar recorded, is left alone -- and prunes: anything under the staging
root this run did not produce (other than the phase-2/3 products beside a produced DDS and the
root reports) is deleted. Every file is written to a temporary sibling and renamed into place, so
a run killed mid-write never leaves a truncated product that a later run mistakes for current.

`select` names a **directory** of unit keys (`hud/signs`), never a stem prefix: `hud` selects
`hud/**` and nothing under `hudson/`. With `select`, pruning stays inside that directory and the
manifest carries the folded package folder (`pruneScope`) so the editor phase prunes only there.

A unit that fails is one failure among many: it is named with its reason, the run goes on, and
the command exits non-zero at the end. A failed unit's assets are **protected**, not orphaned: the
manifest lists them under `keep`, the editor phase never prunes them, and the staging prune leaves
the unit's staged files alone, so a transient read error costs one relaunch and never a
previously good asset.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath

import numpy as np

from elysium_pipeline.asset_names import safe_name
from elysium_pipeline.formats.texture_glb.model import TEXTURE_EXTENSION
from elysium_pipeline.formats.unit_contract.container import GlbContainerError, decode_glb
from elysium_pipeline.importers import texture_cube
from elysium_pipeline.importers.texture_dds import (
    VK_FORMATS,
    TextureDdsError,
    build_dds,
    parse_dds,
    parse_ktx2,
    staged_format,
)
from elysium_pipeline.importers.texture_roles import (
    ROLE_COLOUR,
    ROLE_NORMAL,
    data_role,
    role_for,
    scan_material_bindings,
)

#: The family directory this lane reads below the export_v2 root.
FAMILY = "textures"
#: The package every asset lands under.
PACKAGE_ROOT = "/ElysiumBaked/Textures"
#: Bumped whenever the manifest mapping changes in a way that must re-import every asset.
SETTINGS_VERSION = "elysium-texture-import-v2"   # v2: BC levels staged decoded (BGRA8)
MANIFEST_SCHEMA = "1.0.0"
MANIFEST_NAME = "manifest.json"
MEASURE_NAME = "measure.json"
#: Written by the editor phase; read back by the CLI for its summary.
IMPORT_REPORT_NAME = "import_report.json"
#: Root files that are never pruned.
ROOT_FILES = frozenset({MANIFEST_NAME, MEASURE_NAME, IMPORT_REPORT_NAME})
#: The editor phase writes this beside a staged DDS; it lives and dies with that DDS.
BUILT_SUFFIX = ".built.dds"
PROVENANCE_SUFFIX = ".provenance.json"
LINEAR_TWIN_SUFFIX = "_linear"

CLASS_PREFIX = {"Texture2D": "T_", "TextureCube": "TC_", "Texture2DArray": "TA_"}

#: The VTF header's 32-bit flag word, bit by bit. The unit already publishes the word itself
#: (`sourceFormat.flags`) and the eight sampler bits it needs as `sampling`; this table names
#: every bit so the sidecar states what the source said rather than an opaque integer. Names are
#: Source's own (`CompiledVtfFlags`); the nine textures of the water cast decode against it
#: exactly (`U1b_textures.md` section 1 -- `0x2C0` = noCompress|normal|noLod on `dev/water_normal`,
#: `0x416E` = trilinear|clampS|clampT|hintDxt5|noCompress|noMip|envMap on the pier's fallback cube).
VTF_FLAG_BITS = {
    0x00000001: "pointSample",
    0x00000002: "trilinear",
    0x00000004: "clampS",
    0x00000008: "clampT",
    0x00000010: "anisotropic",
    0x00000020: "hintDxt5",
    0x00000040: "noCompress",
    0x00000080: "normal",
    0x00000100: "noMip",
    0x00000200: "noLod",
    0x00000400: "allMips",
    0x00000800: "procedural",
    0x00001000: "oneBitAlpha",
    0x00002000: "eightBitAlpha",
    0x00004000: "envMap",
    0x00008000: "renderTarget",
    0x00010000: "depthRenderTarget",
    0x00020000: "noDebugOverride",
    0x00040000: "singleCopy",
    0x00080000: "preSrgb",
    0x00100000: "premultiplyByOneOverMipLevel",
    0x00200000: "normalToDuDv",
    0x00400000: "alphaTestMipGeneration",
    0x00800000: "noDepthBuffer",
    0x01000000: "niceFiltered",
    0x02000000: "clampU",
    0x04000000: "vertexTexture",
    0x08000000: "ssBump",
    0x20000000: "border",
}
#: The one bit this lane reads. It is a **role-conflict tie-breaker only**: the bit is set on a
#: texture the author compiled as a normal map, and it is accurate on 4/4 of the conflict set, but
#: 27 units bind a normal with it clear, so it may never create a role or decide sRGB. No VTF bit
#: encodes colour space at all -- `preSrgb` (`0x80000`) is clear on every member of the water cast
#: (`PHASE0_VERDICT.md` A3), and sRGB stays the material binding's answer (`texture_roles`).
VTF_NORMAL = 0x00000080
#: Every bit the table above names, for reporting the ones it does not.
VTF_NAMED_FLAGS = sum(VTF_FLAG_BITS)


def vtf_flag_names(flags: int) -> list[str]:
    """The named bits the VTF flag word carries, in bit order."""

    return [name for bit, name in sorted(VTF_FLAG_BITS.items()) if flags & bit]


class TextureImportError(RuntimeError):
    """The texture corpus could not be staged."""


def staging_root(work_root: Path) -> Path:
    return Path(work_root) / "import" / FAMILY


def unit_root(export_v2_root: Path) -> Path:
    return Path(export_v2_root) / FAMILY


def normalize_select(select: str | None) -> str | None:
    """A `--select` as a forward-slashed, lower-cased directory key without edge slashes."""

    if not select:
        return None
    normalized = select.replace("\\", "/").strip("/").lower()
    return normalized or None


def in_selection(key: str, select: str | None) -> bool:
    """Whether a unit key sits inside the selected directory (or is that key itself)."""

    select = normalize_select(select)
    if select is None:
        return True
    key = key.lower()
    return key == select or key.startswith(select + "/")


def units(export_v2_root: Path, select: str | None = None) -> list[Path]:
    """Every published texture unit, in a stable order, narrowed to a directory when asked."""

    root = unit_root(export_v2_root)
    if not root.is_dir():
        return []
    found = sorted(root.rglob("*.glb"))
    if select:
        found = [unit for unit in found if in_selection(unit_key(export_v2_root, unit), select)]
    return found


def prune_scope_for(select: str | None) -> str:
    """The package folder (trailing slash) the editor phase may prune, folded like the assets."""

    select = normalize_select(select)
    if select is None:
        return PACKAGE_ROOT + "/"
    folded = "/".join(safe_name(part) for part in PurePosixPath(select).parts)
    return f"{PACKAGE_ROOT}/{folded}/"


def protected_asset_paths(key: str) -> list[str]:
    """Every asset path a unit key could own, for a unit whose class the stage could not read."""

    from elysium_pipeline.importers.sky_composites import STORAGE_PREFIX, sky_cube_path
    if key.startswith(STORAGE_PREFIX):
        return [sky_cube_path(key[len(STORAGE_PREFIX):])]
    paths = []
    for asset_class in CLASS_PREFIX:
        for twin in (False, True):
            paths.append(asset_path_for(key, asset_class, twin=twin))
    return paths


def unit_key(export_v2_root: Path, unit: Path) -> str:
    """The unit's key: its path below the family root, forward-slashed, without `.glb`."""

    relative = Path(unit).relative_to(unit_root(export_v2_root))
    return PurePosixPath(*relative.parts).with_suffix("").as_posix()


# --- results -------------------------------------------------------------------------------------


@dataclass(slots=True)
class StageResult:
    staging_root: Path
    manifest_path: Path | None = None
    staged: int = 0
    unchanged: int = 0
    twins: int = 0
    pruned: int = 0
    assets: int = 0
    failures: list[tuple[str, str]] = field(default_factory=list)
    material_failures: list[tuple[str, str]] = field(default_factory=list)
    protected: int = 0

    def summary(self) -> str:
        return (
            f"texture staging: {self.assets} assets ({self.twins} linear twins) from "
            f"{self.staged} staged + {self.unchanged} unchanged units, {self.pruned} pruned, "
            f"{len(self.failures)} failed ({self.protected} asset paths protected), "
            f"{len(self.material_failures)} material units unreadable -> {self.staging_root}"
        )


@dataclass(slots=True)
class MeasureResult:
    staging_root: Path
    measured: int = 0
    unmeasured: int = 0
    max_of_max: int | float = 0
    mean_of_mean: float = 0.0
    worst: list[dict] = field(default_factory=list)
    failures: list[tuple[str, str]] = field(default_factory=list)

    def summary(self) -> str:
        return (
            f"texture re-encode delta: {self.measured} measured, {self.unmeasured} unmeasured, "
            f"max {self.max_of_max}, mean {self.mean_of_mean:.3f}, {len(self.failures)} failed"
        )


# --- planning one unit ---------------------------------------------------------------------------


def _check_key(key: str) -> tuple[list[str], str]:
    """`(directory parts, stem)` for a unit key, refusing anything that could leave the roots."""

    parts = PurePosixPath(key).parts
    if not parts or any(part in ("", ".", "..") or ":" in part for part in parts) or "\\" in key:
        raise TextureImportError(f"{key!r} is not a relative texture key")
    return list(parts[:-1]), parts[-1]


def asset_path_for(key: str, asset_class: str, *, twin: bool = False) -> str:
    from elysium_pipeline.asset_paths import baked_path
    _check_key(key)
    return baked_path("texture", key, CLASS_PREFIX[asset_class].rstrip("_"),
                      role="linear" if twin else None)


def _texture_class(extension: dict, ktx) -> str:
    if ktx.faces == 6:
        return "TextureCube"
    frames = int((extension.get("dimensions") or {}).get("frames") or 1)
    if frames > 1 or ktx.layers > 1:
        return "Texture2DArray"
    return "Texture2D"


def _compression(vk_format: int) -> str:
    kind = VK_FORMATS[vk_format][4]
    if kind in ("rgba16f", "rgba32f"):
        return "hdr-f32"
    return "default" if kind.startswith("bc") else "uncompressed"


def _full_mip_count(width: int, height: int) -> int:
    return int(math.log2(max(width, height))) + 1


@dataclass(slots=True)
class _Plan:
    key: str
    dds_relative: str
    dds_bytes: bytes | None      # None when the existing staged DDS is current
    entries: list[dict]
    sidecars: dict[str, dict]    # relative path -> content


def plan_unit(
    key: str,
    document: dict,
    binary: bytes,
    unit_sha256: str,
    bindings: dict[str, set[str]],
    *,
    existing_sidecar: dict | None = None,
    existing_dds_sha256: str | None = None,
) -> _Plan:
    """Everything the stage writes for one unit, with the DDS built only when it is stale.

    `existing_dds_sha256` is the hash of the DDS already staged for this unit, if any; the unit
    is current only when the sidecar names this GLB, these settings **and** that DDS hash, so a
    truncated or tampered product is rebuilt rather than trusted.
    """

    extension = (document.get("extensions") or {}).get(TEXTURE_EXTENSION)
    if not isinstance(extension, dict):
        raise TextureImportError(f"not a {TEXTURE_EXTENSION} unit")
    identity = extension.get("identity") or {}
    asset_id = identity.get("asset")
    if not isinstance(asset_id, str) or not asset_id.startswith("vtmb:texture:"):
        raise TextureImportError("unit declares no texture identity")
    ktx = parse_ktx2(binary)
    asset_class = _texture_class(extension, ktx)
    dims = extension.get("dimensions") or {}
    source_format = extension.get("sourceFormat") or {}
    sampling = extension.get("sampling") or {}
    payload = extension.get("payload") or {}
    frames = int(dims.get("frames") or max(1, ktx.layers))
    faces = 6 if ktx.faces == 6 else 1
    mip_count = len(ktx.levels)

    parameters = set(bindings.get(asset_id, ()))
    flags = int(source_format.get("flags") or 0)
    role, srgb, conflict = role_for(parameters)
    if conflict and flags & VTF_NORMAL:
        # G15: a colour binding beside a data binding normally stages both readings -- the sRGB
        # asset and a `_linear` twin -- because nothing else says which the author meant. The VTF
        # header does say it: the compiler set `normal` on this texture. So the conflict resolves
        # to the data reading alone and no colour asset is staged (`dev/water_normal`: the colour
        # `TA_water_normal` exists only because `%tooltexture` -- Hammer's thumbnail key, which no
        # shader samples -- is a colour parameter, and 0 of 19,713 material rows bind it).
        #
        # The bit is a TIE-BREAKER between the colour and the data reading (verdict A3) and states
        # nothing about WHICH data role wins, so the data role stays `role_for`'s own -- dropping
        # the colour reading and the `_linear` twin, and nothing else. Forcing `ROLE_NORMAL` here
        # would restyle a conflicted `$dudvmap` binding as a normal map and lose the signed UVWQ
        # payload the water lane reads; no corpus member trips it (`dev/water_dudv` has the bit
        # clear), and it must stay untrippable.
        role, srgb, conflict = data_role(parameters), False, False
    if ktx.vk_format == 41:
        # UVWQ8888 is signed displacement data: a colour reading is meaningless whatever binds it,
        # so it is always a linear data asset and never twins.
        role, srgb, conflict = (data_role(parameters) if parameters else ROLE_NORMAL), False, False
    if ktx.vk_format in (97, 109):
        # Native HDR is already linear. Retain float data, never quantize it through an icon preset.
        srgb, conflict = False, False
    directories, stem = _check_key(key)
    dds_relative = "/".join([*directories, stem + ".dds"])
    colour_path = asset_path_for(key, asset_class)

    current = (
        existing_sidecar is not None
        and existing_sidecar.get("unitSha256") == unit_sha256
        and existing_sidecar.get("settingsVersion") == SETTINGS_VERSION
        and existing_sidecar.get("stagedFormat") == staged_format(ktx.vk_format)
        and existing_dds_sha256 is not None
        and existing_sidecar.get("ddsSha256") == existing_dds_sha256
    )
    face_mapping: list[dict] = []
    dds_bytes: bytes | None = None
    if ktx.faces == 6:
        cube_faces, face_mapping = texture_cube.unreal_faces(ktx, texture_cube.face_rows(extension))
        if not current:
            dds_bytes = build_dds(ktx, faces=cube_faces)
    elif not current:
        dds_bytes = build_dds(ktx)
    if current and existing_sidecar is not None and existing_sidecar.get("faceMapping"):
        face_mapping = existing_sidecar["faceMapping"]
    dds_sha256 = existing_dds_sha256 if dds_bytes is None else hashlib.sha256(dds_bytes).hexdigest()

    def setting_block() -> dict:
        point = bool(sampling.get("pointSample"))
        trilinear = bool(sampling.get("trilinear"))
        return {
            "compression": _compression(ktx.vk_format),
            "mipGen": "leave-existing" if mip_count > 1 else "no-mipmaps",
            "addressX": "clamp" if sampling.get("clampS") else "wrap",
            "addressY": "clamp" if sampling.get("clampT") else "wrap",
            "filter": "nearest" if point else ("trilinear" if trilinear else "default"),
            "neverStream": bool(sampling.get("noLod") or sampling.get("allMips")),
            "expected": {"width": ktx.width, "height": ktx.height, "mips": mip_count,
                         "faces": faces, "slices": 1 if faces == 6 else frames},
        }

    members = []
    for row in (extension.get("sourceResolution") or {}).get("members") or ():
        origin = row.get("origin") or {}
        members.append({
            "role": row.get("role"), "path": row.get("path"),
            "originKind": origin.get("kind"), "container": origin.get("container") or "",
            "offset": int(origin.get("offset") or 0), "size": int(origin.get("size") or 0),
            "byteLength": int(row.get("byteLength") or 0), "sha256": row.get("sha256"),
        })

    def sidecar(asset_path: str, this_role: str, this_srgb: bool, twin_of: str | None) -> dict:
        return {
            "assetId": asset_id,
            "texturePath": identity.get("texturePath"),
            "unitSchemaVersion": extension.get("schemaVersion"),
            "unitSha256": unit_sha256,
            "payloadSha256": payload.get("sha256"),
            "ddsSha256": dds_sha256,
            "stagedFormat": staged_format(ktx.vk_format),
            "settingsVersion": SETTINGS_VERSION,
            "sourceFormat": source_format.get("sourceFormat"),
            "sourceFormatEnum": source_format.get("sourceFormatEnum"),
            "vkFormat": ktx.vk_format,
            "vkFormatName": payload.get("vkFormat"),
            "vtfVersion": str(source_format.get("vtfVersion", "")),
            "tthVersion": int(source_format.get("tthVersion") or 0),
            "flags": flags,
            "flagNames": vtf_flag_names(flags),
            "unnamedFlagBits": flags & ~VTF_NAMED_FLAGS,
            "reflectivity": list(source_format.get("reflectivity") or [0.0, 0.0, 0.0]),
            "bumpScale": float(source_format.get("bumpScale") or 0.0),
            "startFrame": int(source_format.get("startFrame") or 0),
            "width": ktx.width,
            "height": ktx.height,
            "frames": frames,
            "faces": faces,
            "mipCount": mip_count,
            "sourceMipCount": int(source_format.get("sourceMipCount") or mip_count),
            "sampling": {name: bool(sampling.get(name)) for name in (
                "pointSample", "trilinear", "clampS", "clampT", "anisotropic",
                "noMip", "noLod", "allMips")},
            "members": members,
            "role": this_role,
            "roleEvidence": sorted(parameters),
            "roleConflict": conflict,
            "twinOf": twin_of,
            "faceMapping": face_mapping,
            "expandedFromRgb8": VK_FORMATS[ktx.vk_format][4] == "rgb8",
            # A chain that starts and stops above 1x1; a single level is a normal texture to Unreal.
            "partialMipChain": 1 < mip_count < _full_mip_count(ktx.width, ktx.height),
            "assetPath": asset_path,
            "unitGlb": f"{FAMILY}/{key}.glb",
        }

    def entry(asset_path: str, provenance_relative: str, this_role: str, this_srgb: bool,
              twin_of: str | None) -> dict:
        settings = setting_block()
        return {
            "assetPath": asset_path,
            "class": asset_class,
            "dds": dds_relative,
            "stagedFormat": staged_format(ktx.vk_format),
            "provenance": provenance_relative,
            "unit": asset_id,
            "unitGlb": f"{FAMILY}/{key}.glb",
            "unitSha256": unit_sha256,
            "twinOf": twin_of,
            "role": this_role,
            "roleEvidence": sorted(parameters),
            "roleConflict": conflict,
            "srgb": this_srgb,
            **settings,
            "recipe": {
                "unitSha256": unit_sha256,
                "settingsVersion": SETTINGS_VERSION,
                "role": this_role,
                "srgb": this_srgb,
                "compression": settings["compression"],
                "twin": twin_of is not None,
            },
        }

    colour_sidecar = "/".join([*directories, stem + PROVENANCE_SUFFIX])
    entries = [entry(colour_path, colour_sidecar, role, srgb, None)]
    sidecars = {colour_sidecar: sidecar(colour_path, role, srgb, None)}
    if conflict:
        twin_path = asset_path_for(key, asset_class, twin=True)
        twin_role = data_role(parameters)
        twin_sidecar = "/".join([*directories, stem + LINEAR_TWIN_SUFFIX + PROVENANCE_SUFFIX])
        entries.append(entry(twin_path, twin_sidecar, twin_role, False, colour_path))
        sidecars[twin_sidecar] = sidecar(twin_path, twin_role, False, colour_path)
    return _Plan(key, dds_relative, dds_bytes, entries, sidecars)


# --- staging -------------------------------------------------------------------------------------


def _write_if_changed(path: Path, data: bytes) -> bool:
    """Write `data` to `path` atomically unless the file already holds exactly these bytes."""

    if path.is_file() and path.stat().st_size == len(data) and path.read_bytes() == data:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_bytes(data)
    os.replace(temporary, path)
    return True


def _sha256_of(path: Path) -> str | None:
    try:
        return hashlib.sha256(path.read_bytes()).hexdigest()
    except OSError:
        return None


def _json_bytes(content: dict) -> bytes:
    return (json.dumps(content, indent=1, sort_keys=True) + "\n").encode("utf-8")


def _load_json(path: Path) -> dict | None:
    try:
        content = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    return content if isinstance(content, dict) else None


def _unit_files(root: Path, key: str) -> set[Path]:
    """Every staged file a unit key owns: DDS, its built twin, and both sidecars."""

    directories, stem = _check_key(key)
    base = root.joinpath(*directories)
    return {
        base / (stem + ".dds"),
        base / (stem + BUILT_SUFFIX),
        base / (stem + PROVENANCE_SUFFIX),
        base / (stem + LINEAR_TWIN_SUFFIX + PROVENANCE_SUFFIX),
    }


def _prune_stale(root: Path, produced: set[Path], select: str | None) -> int:
    """Delete every file under `root` this run did not produce, inside the selected directory."""

    if not root.is_dir():
        return 0
    select = normalize_select(select)
    scope = root
    if select:
        scope = root.joinpath(*PurePosixPath(select).parts)
        if not scope.is_dir():
            return 0
    keep = set(produced)
    for path in produced:
        if path.suffix == ".dds":
            keep.add(path.with_name(path.name[:-4] + BUILT_SUFFIX))
    pruned = 0
    for path in list(scope.rglob("*")):
        if path.is_dir() or path in keep:
            continue
        if path.parent == root and path.name in ROOT_FILES:
            continue
        path.unlink()
        pruned += 1
    for directory in sorted((p for p in scope.rglob("*") if p.is_dir()),
                            key=lambda p: len(p.parts), reverse=True):
        try:
            directory.rmdir()
        except OSError:
            pass
    return pruned


def stage_textures(
    export_v2_root: Path,
    staging_root_path: Path,
    *,
    select: str | None = None,
    bindings: dict[str, set[str]] | None = None,
) -> StageResult:
    """Phase 1: stage every selected unit and write the manifest. See the module docstring."""

    export_v2_root = Path(export_v2_root)
    root = Path(staging_root_path)
    result = StageResult(staging_root=root)
    found = units(export_v2_root, select)
    if not found:
        raise TextureImportError(
            f"no texture units under {unit_root(export_v2_root)}"
            + (f" matching {select!r}" if select else "")
            + "; run `uv run elysium export_v2 textures-glb` first"
        )
    if bindings is None:
        bindings, result.material_failures = scan_material_bindings(export_v2_root)

    plans: list[_Plan] = []
    failed_keys: list[str] = []

    def failed(key: str, reason: str) -> None:
        result.failures.append((key, reason))
        failed_keys.append(key)

    for unit in found:
        key = unit_key(export_v2_root, unit)
        try:
            data = unit.read_bytes()
            unit_sha256 = hashlib.sha256(data).hexdigest()
            document, binary = decode_glb(data, str(unit))
            directories, stem = _check_key(key)
            sidecar_path = root.joinpath(*directories, stem + PROVENANCE_SUFFIX)
            dds_path = root.joinpath(*directories, stem + ".dds")
            existing = _load_json(sidecar_path) if sidecar_path.is_file() else None
            # The DDS is hashed only when the sidecar could make it current; a stale sidecar
            # rebuilds regardless, so its DDS is not worth reading.
            dds_sha256 = None
            if (existing is not None and existing.get("unitSha256") == unit_sha256
                    and existing.get("settingsVersion") == SETTINGS_VERSION and dds_path.is_file()):
                dds_sha256 = _sha256_of(dds_path)
            plans.append(plan_unit(key, document, binary, unit_sha256, bindings,
                                   existing_sidecar=existing, existing_dds_sha256=dds_sha256))
        except (GlbContainerError, TextureDdsError, TextureImportError, OSError, ValueError,
                KeyError) as error:
            failed(key, str(error))

    # D1: composites are texture-lane products, included in the SAME collision and prune
    # accounting as units. Their staging namespace cannot overwrite a face or cube unit.
    from elysium_pipeline.importers.sky_composites import plan_composites
    sky_plans, sky_failures = plan_composites(export_v2_root, root, select)
    plans.extend(sky_plans)
    for key, reason in sky_failures:
        failed(key, reason)

    # Two units folding to one asset path is a defect in the fold, not a race one may win.
    owners: dict[str, list[str]] = {}
    for plan in plans:
        for entry in plan.entries:
            owners.setdefault(entry["assetPath"].lower(), []).append(plan.key)
    for key in failed_keys:
        try:
            for path in protected_asset_paths(key):
                owners.setdefault(path.lower(), []).append(key)
        except (TextureImportError, ValueError):
            continue  # an invalid key could never own a legal package
    collided = {key for keys in owners.values() if len(keys) > 1 for key in keys}
    for plan in plans:
        if plan.key in collided:
            others = sorted({k for keys in owners.values() if plan.key in keys for k in keys} - {plan.key})
            failed(plan.key, f"asset path collides with {', '.join(others)}")
    plans = [plan for plan in plans if plan.key not in collided]

    produced: set[Path] = set()
    assets: list[dict] = []
    for plan in plans:
        dds_path = root.joinpath(*PurePosixPath(plan.dds_relative).parts)
        try:
            if plan.dds_bytes is not None:
                _write_if_changed(dds_path, plan.dds_bytes)
                result.staged += 1
            else:
                result.unchanged += 1
            produced.add(dds_path)
            for relative, content in plan.sidecars.items():
                path = root.joinpath(*PurePosixPath(relative).parts)
                _write_if_changed(path, _json_bytes(content))
                produced.add(path)
        except OSError as error:
            failed(plan.key, str(error))
            continue
        assets.extend(plan.entries)
        result.twins += sum(1 for entry in plan.entries if entry["twinOf"])

    # A failed unit keeps whatever it already has: every asset path its key could own stays out
    # of the editor prune, and its staged files stay out of this one.
    keep: set[str] = set()
    for key in failed_keys:
        try:
            keep.update(protected_asset_paths(key))
            produced.update(path for path in _unit_files(root, key) if path.exists())
        except (TextureImportError, ValueError):
            continue   # an invalid key never owned an asset
    named = {entry["assetPath"] for entry in assets}
    keep -= named
    result.protected = len(keep)

    assets.sort(key=lambda entry: entry["assetPath"].lower())
    manifest = {
        "schemaVersion": MANIFEST_SCHEMA,
        "settingsVersion": SETTINGS_VERSION,
        "packageRoot": PACKAGE_ROOT,
        "select": normalize_select(select),
        "pruneScope": prune_scope_for(select),
        "keep": sorted(keep),
        "stageFailures": [{"unit": key, "reason": reason} for key, reason in result.failures],
        "assets": assets,
    }
    root.mkdir(parents=True, exist_ok=True)
    manifest_path = root / MANIFEST_NAME
    _write_if_changed(manifest_path, _json_bytes(manifest))
    result.manifest_path = manifest_path
    result.assets = len(assets)
    result.pruned = _prune_stale(root, produced, select)
    return result


# --- measuring ----------------------------------------------------------------------------------


def measure_textures(staging_root_path: Path) -> MeasureResult:
    """Phase 3: per-unit texel delta between the staged DDS and the built mip 0 Unreal wrote back."""

    root = Path(staging_root_path)
    result = MeasureResult(staging_root=root)
    manifest = _load_json(root / MANIFEST_NAME)
    if manifest is None:
        raise TextureImportError(f"no {MANIFEST_NAME} under {root}; stage first")
    rows: list[dict] = []
    seen_dds: set[str] = set()
    for entry in manifest.get("assets") or ():
        dds_relative = entry.get("dds")
        if not isinstance(dds_relative, str) or dds_relative in seen_dds:
            continue
        seen_dds.add(dds_relative)
        staged_path = root.joinpath(*PurePosixPath(dds_relative).parts)
        built_path = staged_path.with_name(staged_path.name[:-4] + BUILT_SUFFIX)
        if not built_path.is_file():
            result.unmeasured += 1
            continue
        try:
            staged = parse_dds(staged_path.read_bytes()).decode(0, 0)
            built = parse_dds(built_path.read_bytes()).decode(0, 0)
            if staged.shape != built.shape:
                raise TextureDdsError(
                    f"built mip 0 is {built.shape[1]}x{built.shape[0]}, staged is "
                    f"{staged.shape[1]}x{staged.shape[0]}")
            floating = (np.issubdtype(staged.dtype, np.floating)
                        or np.issubdtype(built.dtype, np.floating))
            dtype = np.float64 if floating else np.int16
            delta = np.abs(staged.astype(dtype) - built.astype(dtype))
            rows.append({"unit": entry.get("unit"), "dds": dds_relative,
                         "maxDelta": float(delta.max()) if floating else int(delta.max()),
                         "meanDelta": float(delta.mean())})
            result.measured += 1
        except (TextureDdsError, OSError, ValueError) as error:
            result.failures.append((str(entry.get("unit")), str(error)))
    if rows:
        result.max_of_max = max(row["maxDelta"] for row in rows)
        result.mean_of_mean = sum(row["meanDelta"] for row in rows) / len(rows)
        result.worst = sorted(rows, key=lambda row: (-row["maxDelta"], -row["meanDelta"]))[:10]
    (root / MEASURE_NAME).write_bytes(_json_bytes({
        "measured": result.measured,
        "unmeasured": result.unmeasured,
        "maxOfMax": result.max_of_max,
        "meanOfMean": result.mean_of_mean,
        "worst": result.worst,
        "failures": [{"unit": unit, "reason": reason} for unit, reason in result.failures],
        "units": rows,
    }))
    return result
