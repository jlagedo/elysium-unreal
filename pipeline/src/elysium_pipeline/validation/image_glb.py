"""Independent structural validator for Image GLB products.

Standalone validation checks the container, the KTX2 header, level index and data format
descriptor against the declared `vkFormat`, and the byte ledger -- everything a reader can prove
with no install present. Export-time validation additionally re-decodes the selected source
member through `formats.image_glb.decode` (never through the exporter's own `build_document`)
and compares the recomputed KTX2 payload byte for byte against the one the document declares.
"""

from __future__ import annotations

import hashlib
import struct
from pathlib import Path
from typing import Any

from elysium_pipeline.formats.image_glb import ktx2 as image_ktx2
from elysium_pipeline.formats.image_glb.decode import ImageDecodeError, decode_image
from elysium_pipeline.formats.image_glb.model import (
    IMAGE_EXTENSION,
    SCHEMA_VERSION,
    ImageSourceClosure,
    container_of,
)
from elysium_pipeline.formats.unit_contract import (
    UnitValidationError,
    completeness,
    read_glb as _read_glb,
    reject_opaque_source,
    validate_accessors,
    validate_container,
    validate_extension_root,
    validate_ledgers,
    validate_sceneless,
)
from elysium_pipeline.formats.unit_contract.container import GlbContainerError


class ImageGlbValidationError(ValueError):
    pass


def read_glb(path: Path) -> tuple[dict[str, Any], bytes]:
    try:
        return _read_glb(path)
    except GlbContainerError as error:
        raise ImageGlbValidationError(str(error)) from error


def _check_ktx2(binary: bytes, payload: dict[str, Any], dimensions: dict[str, Any]) -> None:
    byte_length = int(payload.get("byteLength", -1))
    if byte_length < 0 or byte_length > len(binary):
        raise ImageGlbValidationError("payload byteLength does not fit the BIN chunk")
    ktx = binary[:byte_length]
    if hashlib.sha256(ktx).hexdigest() != payload.get("sha256"):
        raise ImageGlbValidationError("payload sha256 disagrees with the BIN chunk")
    if len(ktx) < 80 or ktx[:12] != image_ktx2.IDENTIFIER:
        raise ImageGlbValidationError("payload does not open with the KTX2 identifier")
    (
        vk_format, type_size, width, height, pixel_depth, layer_count, face_count, level_count,
        supercompression,
    ) = struct.unpack_from("<9I", ktx, 12)
    if vk_format != int(payload.get("vkFormatValue", -1)):
        raise ImageGlbValidationError("KTX2 vkFormat disagrees with the declared payload")
    if (width, height) != (int(dimensions.get("width", -1)), int(dimensions.get("height", -1))):
        raise ImageGlbValidationError("KTX2 extent disagrees with the declared dimensions")
    try:
        expected_type_size = image_ktx2.type_size_for(vk_format)
    except KeyError as error:
        raise ImageGlbValidationError(f"unsupported vkFormat {vk_format}") from error
    if (type_size, pixel_depth, layer_count, face_count, level_count, supercompression) != (
        expected_type_size, 0, 0, 1, 1, 0,
    ):
        raise ImageGlbValidationError(
            "KTX2 header is not one level, one face, non-array, uncompressed"
        )
    dfd_offset, dfd_length = struct.unpack_from("<2I", ktx, 48)
    try:
        expected_dfd = image_ktx2.dfd_bytes(vk_format)
    except KeyError as error:
        raise ImageGlbValidationError(f"unsupported vkFormat {vk_format}") from error
    if ktx[dfd_offset:dfd_offset + dfd_length] != expected_dfd or dfd_length != len(expected_dfd):
        raise ImageGlbValidationError("KTX2 data format descriptor disagrees with its vkFormat")
    level_offset, level_length, level_uncompressed = struct.unpack_from("<3Q", ktx, 80)
    plane = image_ktx2.bytes_per_pixel(vk_format)
    if level_length != level_uncompressed or level_length != width * height * plane:
        raise ImageGlbValidationError("KTX2 level index disagrees with its declared extent")
    if level_offset + level_length != len(ktx):
        # This seam's KTX2 payload holds exactly one level and nothing after it, so the level
        # index has to name the trailing bytes of the payload and no others.
        raise ImageGlbValidationError("KTX2 level bytes are not where the level index says")


def validate_document(
    document: dict[str, Any], binary: bytes, *, source_members=None
) -> dict[str, Any]:
    try:
        root = validate_extension_root(
            document, IMAGE_EXTENSION, asset_prefix="vtmb:image:", schema_version=SCHEMA_VERSION
        )
        validate_container(document, binary)
        validate_accessors(document, binary)
        validate_sceneless(document)
        validate_ledgers(root, source_members)
        reject_opaque_source(document, binary, source_members)
    except UnitValidationError as error:
        raise ImageGlbValidationError(str(error)) from error

    gaps = completeness(root)
    if gaps["unresolved"] or gaps["unsupported"]:
        raise ImageGlbValidationError("image extension is incomplete")
    if root.get("dependencies"):
        raise ImageGlbValidationError("an image unit names no other unit")

    identity = root["identity"]
    source_path = str(identity.get("sourcePath", ""))
    container = str(identity.get("container", ""))
    if container not in ("tga", "bmp") or container_of(source_path) != container:
        raise ImageGlbValidationError("identity.container disagrees with the source path")

    payload = root.get("payload")
    dimensions = root.get("dimensions")
    if not isinstance(payload, dict) or not isinstance(dimensions, dict):
        raise ImageGlbValidationError("an image unit publishes payload and dimensions")
    if payload.get("bufferView") != 0 or payload.get("mimeType") != "image/ktx2":
        raise ImageGlbValidationError("payload does not name the KTX2 buffer view")
    buffer_views = document.get("bufferViews") or []
    if len(buffer_views) != 1 or buffer_views[0].get("byteOffset", 0) != 0:
        raise ImageGlbValidationError(
            "an image unit declares exactly one bufferView, at byte offset 0"
        )
    _check_ktx2(binary, payload, dimensions)

    members = (root.get("sourceResolution") or {}).get("members") or []
    if len(members) != 1:
        raise ImageGlbValidationError("an image unit owns exactly one source member")

    if source_members is not None:
        if len(source_members) != 1:
            raise ImageGlbValidationError("export-time validation needs the one selected member")
        member = source_members[0]
        closure = ImageSourceClosure(source_path, identity["asset"], container, member)
        try:
            model = decode_image(closure)
        except ImageDecodeError as error:
            raise ImageGlbValidationError(f"independent re-decode failed: {error}") from error
        if (model.width, model.height, model.vk_format_value) != (
            int(dimensions["width"]), int(dimensions["height"]), int(payload["vkFormatValue"]),
        ):
            raise ImageGlbValidationError("the re-decoded image disagrees with the declared extent")
        if root.get("orientation") != model.orientation:
            # The independent re-decode applies `orientation` to the source pixels on its own,
            # from the source bytes alone -- comparing its result against the published record
            # is how a wrong `orientation` (source rows reversed one way, the field spelling the
            # other) gets caught, the same guarantee the KTX2 byte comparison below gives the
            # pixel bytes themselves.
            raise ImageGlbValidationError(
                "the declared orientation disagrees with the independent re-decode"
            )
        recomputed, _meta = image_ktx2.build(
            model.width, model.height, model.vk_format_value, model.pixel_data
        )
        if binary[:len(recomputed)] != recomputed:
            raise ImageGlbValidationError(
                "the re-decoded KTX2 payload disagrees with the published one, byte for byte"
            )

    return {
        "asset": identity["asset"],
        "sourcePath": source_path,
        "container": container,
        "width": int(dimensions["width"]),
        "height": int(dimensions["height"]),
        "vkFormat": payload.get("vkFormat"),
        "hasPalette": root.get("palette") is not None,
        "hasFooter": root.get("footer") is not None,
        "omissions": [str(row.get("role")) for row in root.get("omissions") or []],
        "anomalies": [str(row.get("role")) for row in root.get("anomalies") or []],
        "typedUnidentified": gaps["typedUnidentified"],
        "sourceBytes": int((root["coverage"]["byteLedger"][0])["byteLength"]),
        "accountedBytes": int((root["coverage"]["byteLedger"][0])["accountedBytes"]),
        "byteCoveragePercent": float((root["coverage"]["byteLedger"][0])["coveragePercent"]),
    }


def validate(path: Path) -> dict[str, Any]:
    document, binary = read_glb(path)
    return validate_document(document, binary)


def warnings_for(summary: dict[str, Any]) -> list[str]:
    warnings: list[str] = []
    for role in summary.get("omissions") or []:
        warnings.append(f"omitted: {role}")
    for role in summary.get("anomalies") or []:
        warnings.append(f"anomaly: {role}")
    if summary.get("typedUnidentified"):
        warnings.append(f"typed but unidentified: {summary['typedUnidentified']} field(s)")
    return warnings
