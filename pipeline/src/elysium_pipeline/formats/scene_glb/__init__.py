"""Isolated lossless Scene GLB format aggregation.

One binary glTF 2.0 unit per Faceposer `.vcd` choreography file below `sound/`.
The scene seam owns the format contract this package implements.
"""

from elysium_pipeline.formats.scene_glb.decode import (
    DEGENERATE_DURATION_SECONDS,
    SceneDecodeError,
    decode_scene,
)
from elysium_pipeline.formats.scene_glb.model import (
    EVENT_TYPE_IDS,
    RECOGNISED_UNUSED_TOKENS,
    SCENE_EXTENSION,
    SCENE_ROOT,
    SCENE_SUFFIX,
    SCHEMA_VERSION,
    UNHANDLED_EVENT_TYPES,
    SceneModel,
    asset_id,
    normalize_scene_key,
    output_relative_path,
    source_path_for,
)
from elysium_pipeline.formats.scene_glb.source import (
    SceneSourceClosure,
    SceneSourceError,
    load_source_closure,
)

__all__ = [
    "DEGENERATE_DURATION_SECONDS",
    "EVENT_TYPE_IDS",
    "RECOGNISED_UNUSED_TOKENS",
    "SCENE_EXTENSION",
    "SCENE_ROOT",
    "SCENE_SUFFIX",
    "SCHEMA_VERSION",
    "UNHANDLED_EVENT_TYPES",
    "SceneDecodeError",
    "SceneModel",
    "SceneSourceClosure",
    "SceneSourceError",
    "asset_id",
    "decode_scene",
    "load_source_closure",
    "normalize_scene_key",
    "output_relative_path",
    "source_path_for",
]
