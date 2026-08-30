"""Isolated one-image/one-KTX2 GLB product writer."""

from __future__ import annotations

from pathlib import Path

from elysium_pipeline.formats.image_glb import ktx2 as image_ktx2
from elysium_pipeline.formats.image_glb.coverage import build_image_coverage
from elysium_pipeline.formats.image_glb.decode import decode_image
from elysium_pipeline.formats.image_glb.model import (
    IMAGE_EXTENSION,
    IMAGE_EXTENSIONS,
    SCHEMA_VERSION,
    output_relative_path,
)
from elysium_pipeline.formats.image_glb.source import load_source_closure
from elysium_pipeline.formats.unit_contract import (
    asset_block,
    extension_root,
    identity_block,
    plain,
    source_resolution,
    write_glb,
)


def build_document(model) -> tuple[dict, bytes]:
    payload, payload_meta = image_ktx2.build(
        model.width, model.height, model.vk_format_value, model.pixel_data
    )
    payload_block = {
        "bufferView": 0,
        "mimeType": payload_meta["mimeType"],
        "byteLength": payload_meta["byteLength"],
        "sha256": payload_meta["sha256"],
        "vkFormat": payload_meta["vkFormat"],
        "vkFormatValue": payload_meta["vkFormatValue"],
    }
    mapped = ["identity", "sourceResolution", "payload", "dimensions", "sourceFormat", "orientation"]
    if model.palette is not None:
        mapped.append("palette")
    if model.footer is not None:
        mapped.append("footer")

    root = extension_root(
        schema_version=SCHEMA_VERSION,
        identity=identity_block(model.asset_id, model.image_path, container=model.container),
        source_resolution=source_resolution([model.member]),
        dependencies=[],
        coverage=build_image_coverage(
            ledger_row=model.ledger_row,
            mapped=mapped,
            typed_unidentified=model.typed_unidentified,
            omitted_proven=model.omissions,
            unresolved=model.unresolved,
        ),
        payload=payload_block,
        dimensions={
            "width": model.width,
            "height": model.height,
            "bitsPerPixel": model.bits_per_pixel,
        },
        sourceFormat=model.source_format,
        palette=model.palette,
        footer=model.footer,
        orientation=model.orientation,
        anomalies=model.anomalies,
        omissions=model.omissions,
    )
    document = {
        "asset": asset_block("Image"),
        "extensionsUsed": [IMAGE_EXTENSION],
        "extensionsRequired": [IMAGE_EXTENSION],
        "extensions": {IMAGE_EXTENSION: plain(root)},
        "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": len(payload)}],
        "buffers": [{"byteLength": len(payload)}],
    }
    return document, payload


def export(index: dict, key: str, output_root: Path, *, read_bytes=None) -> Path:
    closure = load_source_closure(index, key, read_bytes=read_bytes)
    model = decode_image(closure)
    document, binary = build_document(model)
    from elysium_pipeline.validation import image_glb as validation

    validation.validate_document(document, binary, source_members=closure.members())
    destination = output_root / Path(*output_relative_path(closure.image_path).parts)
    write_glb(document, binary, destination)
    return destination


def source_keys(index: dict) -> list[str]:
    """Every `.tga`/`.bmp` member the up-first install index resolves, folded and sorted."""

    return sorted(key for key in index if str(key).lower().endswith(IMAGE_EXTENSIONS))
