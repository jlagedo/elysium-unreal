"""Keep asset I/O and transport flags out of the owned prop runtime adapter."""
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
FILES = (
    "Source/ElysiumUE/Private/Visual/ElysiumEntityBodiesProps.cpp",
    "Source/ElysiumUE/Private/Visual/ElysiumPreparedPropModels.cpp",
)


def code(path):
    text = (ROOT / path).read_text(encoding="utf-8")
    return re.sub(r"/\*[\s\S]*?\*/|//[^\n]*", "", text)


def test_prop_runtime_never_loads_assets_files_or_legacy_indexes():
    for path in FILES:
        text = code(path)
        assert not re.search(r"\b(?:LoadObject|StaticLoadObject|LoadSynchronous|TryLoad|LoadFileToString|LoadFileToArray|GetIndex|ResolveGridClip)\s*(?:<[^>]+>)?\s*\(", text), path
        assert "FFileHelper" not in text and "FJsonSerializer" not in text, path


def test_prop_transport_has_no_runtime_feature_flag():
    for path in FILES:
        text = code(path)
        assert "TAutoConsoleVariable" not in text and "IConsoleManager" not in text, path


def test_r8_prop_adapter_does_not_add_deferred_physics_activation():
    for path in FILES:
        text = code(path)
        assert "SetSimulatePhysics(true" not in text, path
        assert "GetPhysicsAsset(" not in text and "SetPhysicsAsset(" not in text, path
