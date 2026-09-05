"""Source guards for the native character transport; native tests own behavioral assertions."""
from pathlib import Path

ROOT = Path(__file__).parents[2] / "Source/ElysiumUE"


def function(path, signature):
    text = (ROOT / path).read_text(encoding="utf-8")
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 1
    position = opening + 1
    while depth:
        depth += (text[position] == "{") - (text[position] == "}")
        position += 1
    return text[opening:position]


def test_entity_metadata_preserves_source_identity_instead_of_basename():
    body = function("Private/Substrate/ElysiumAnimatingImpl.cpp", "FString FElysiumAnimating::ModelStem() const")
    assert "ElysiumCharacterModel::IdFromSource(Model)" in body
    assert "GetBaseFilename" not in body
    assert "Model =" not in body


def test_character_mesh_resolution_never_uses_legacy_loader_or_loads_native_on_demand():
    body = function("Private/Visual/ElysiumEntityBodies.cpp", "USkeletalMesh* UElysiumEntityBodies::ResolveNpcMesh(")
    assert "Native->Mesh(Id, Error)" in body
    assert "RequireBodyModel" in body
    for forbidden in ("LoadMesh(", "LoadObject<", "LoadSynchronous(", "Prepare(", "PrepareNativeModel("):
        assert forbidden not in body
    # Residency must be consulted before a locally cached mesh can be returned.
    assert "NpcMeshCache.Find(" not in body


def test_body_cache_clear_does_not_release_main_owned_native_preparation():
    body = function("Private/Visual/ElysiumEntityBodies.cpp", "void UElysiumEntityBodies::ForgetNpcVisuals()")
    assert "NpcMeshCache.Empty()" in body and "NpcAnimCache.Empty()" in body
    assert "ReleaseNativeModels" not in body
    assert "ReleasePrepared" not in body


def test_character_clip_cache_keeps_mesh_identity_and_does_not_remember_failed_admission():
    body = function("Private/Visual/ElysiumEntityBodies.cpp", "UAnimSequence* UElysiumEntityBodies::ResolveNpcClip(")
    assert body.index("RequireBodyModel") < body.index("NpcAnimCache.Find")
    assert "ResolveClip(Stem, ClipName, TargetMesh" in body
    assert "if (Anim) NpcAnimCache.Add(Key, Anim)" in body


def test_disposition_expressions_use_prepared_native_selection_without_changing_evaluator():
    body = function("Private/Substrate/ElysiumAnimatingImpl.cpp", "void FElysiumAnimating::RefreshDispositionExpression()")
    assert "LoadPreparedModelSelection" in body
    assert "ElysiumExpressions::Load(" not in body
    assert "ExpressionRow.Weights[Key] * Row.ExpressionIntensity" in body
    assert "ExpressionRow.Values[Key] * Influence" in body
