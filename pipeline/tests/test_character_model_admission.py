"""Source invariants for runtime admission; native fixtures test stale/cancelled delivery."""
from pathlib import Path

from test_character_body_routing import function

ROOT = Path(__file__).parents[2] / "Source/ElysiumUE"
NATIVE = "Private/Visual/ElysiumNativeAnimationData.cpp"


def test_async_workflow_has_no_blocking_discovery_load_or_compile_wait():
    for signature in (
        "uint64 UElysiumNativeAnimationData::AdmitModelAsync(",
        "void UElysiumNativeAnimationData::AdvanceModelAdmission(",
        "void UElysiumNativeAnimationData::LoadAdmissionPaths(",
        "bool UElysiumNativeAnimationData::IsModelReady(",
    ):
        body = function(NATIVE, signature)
        for forbidden in ("LoadSynchronous(", "LoadObject<", "WaitUntilComplete(",
                          "PrepareMany(", "FinishCompilation(", "FinishPreparation("):
            assert forbidden not in body, (signature, forbidden)
    load = function(NATIVE, "void UElysiumNativeAnimationData::LoadAdmissionPaths(")
    assert "RequestAsyncLoad" in load
    assert load.index("Request->Handles.Add(Handle)") < load.index("Handle->StartStalledHandle()")


def test_owner_graph_is_published_only_after_dependencies_and_compile_readiness():
    body = function(NATIVE, "void UElysiumNativeAnimationData::AdvanceModelAdmission(")
    assert body.index("Data->GatherAnimationPaths") < body.index("Request->bDependenciesRequested = true")
    assert body.index("AnimationReferencesResident") < body.index("Bodies.Append")
    assert body.index("AdmissionAssetsCompiling") < body.index("Bodies.Append")
    assert "Request->Generation != AdmissionGeneration" in body
    assert "Request->Epoch != PreparedEpoch" in body
    assert "wrong UObject class" in body


def test_native_retirement_invalidates_async_generation_before_releasing_assets():
    body = function(NATIVE, "void UElysiumNativeAnimationData::ReleasePrepared()")
    assert body.index("++AdmissionGeneration") < body.index("CancelModelAdmission") < body.index("Bodies.Reset()")


def test_map_completion_checks_current_ticket_before_any_visual_callback():
    path = "Private/Map/ElysiumMapActorModelPreparation.cpp"
    request = function(path, "EElysiumCharacterModelAdmission AElysiumMapActor::RequestCharacterModel(")
    assert request.index("OwnsPreparationEpoch(MapEpoch)") < request.index("IsModelReady(ModelId)")
    body = function(path, "void AElysiumMapActor::CompleteCharacterModel(")
    assert body.index("CharacterModelRequests.IsCurrent") < body.index("CompletePreparedCharacterVisual")
    assert "EntityWorld->GetEpoch()" in body and "!Live->IsDead()" in body
    assert "IdFromSource(Live->Model)" in body
    assert "if (!bCurrent) return" in body
    assert "SetRuntimeModel(" not in body and "OnRuntimeModelChanged(" not in body


def test_completion_cannot_replay_source_input_or_character_rebuild_side_effects():
    body = function("Private/Substrate/ElysiumAnimatingImpl.cpp",
                    "void FElysiumAnimating::CompletePreparedCharacterVisual(")
    for expected in ("Generation != CharacterVisualGeneration", "ModelStem() != ModelId", "World->Resolve(Handle) != this",
                     "OnPreparedVisualAttached()", "RefreshPreparedExpressions()"):
        assert expected in body
    for forbidden in ("SetRuntimeModel(", "OnRuntimeModelChanged(", "EndScriptMove(", "RefreshAnimationPreload(",
                      "RequestCharacterModel(", "FireOutput("):
        assert forbidden not in body
    source = function("Private/Substrate/ElysiumEntity.cpp", "void FElysiumEntity::SetRuntimeModel(")
    assert source.index("Model = NewModel") < source.index("OnRuntimeModelChanged()")


def test_model_children_survive_pending_completion_and_player_state_effects_stay_at_input():
    rebuild = function("Private/Substrate/ElysiumAnimatingImpl.cpp", "void FElysiumAnimating::OnRuntimeModelChanged()")
    assert "PendingModelChildren.Add" in rebuild
    assert "RefreshAnimationPreload()" not in rebuild
    player = function("Private/Substrate/ElysiumPlayerEntity.cpp", "void FElysiumPlayer::OnRuntimeModelChanged()")
    assert player.index("ReleaseHeldReaction()") < player.index("PrepareCharacterVisual()")
    install = function("Private/Substrate/ElysiumPlayerEntity.cpp", "void FElysiumPlayer::InstallPreparedCharacterVisual()")
    assert "ReleaseHeldReaction()" not in install and "SetRuntimeModel(" not in install
    npc = (ROOT / "Private/Substrate/ElysiumNpc.h").read_text(encoding="utf-8")
    assert "StanceResolvedFor.Reset();" in npc
    assert "FElysiumScriptedCharacter::OnPreparedVisualAttached();" in npc
