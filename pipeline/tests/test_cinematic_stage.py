from types import SimpleNamespace

from elysium_pipeline.formats import eskm, mdl_skel
from elysium_pipeline.skeletal_stage.cinematics import actor_payloads


def test_cinematic_actors_keep_only_their_tracks_and_rename_the_root():
    bones = [mdl_skel.Bone(index=i, name=name, parent=parent, pos=(x, 0., 0.),
                           quat=(0., 0., 0., 1.), flags=0)
             for i, (name, parent, x) in enumerate([
                 ("Bip01", -1, 1.), ("Bip01 Hand", 0, 2.),
                 ("Bip02", -1, 10.), ("Bip02 Hand", 2, 3.)])]
    clip = mdl_skel.Seq("scene", 0, 2, 30., "", 0, 0)
    unit = SimpleNamespace(
        bones=bones, sequences=[clip],
        animation_frames=lambda base, frames: [
            [((bone.pos[0] + frame, 0., 0.), bone.quat) for bone in bones] for frame in range(frames)],
        bone_weights=lambda base: [1.] * 4,
        authored_channels=lambda base: [(True, True)] * 4)
    actors = actor_payloads(unit)
    assert [root for root, _ in actors] == ["Bip01", "Bip02"]
    for root, blob in actors:
        rows = eskm.bone_locals(blob)
        assert [(r[0], r[1]) for r in rows] == [("Bip01", -1), ("Bip01 Hand", 0)]
        assert eskm.clip_track_bones(blob) == {"scene": {0, 1}}
        assert eskm.clip_payloads(blob)[0].frames == 2
    # The second actor's model-space bind survives the rename; it is not copied from actor 1.
    assert abs(eskm.bone_locals(actors[1][1])[0][2][0] - 25.4) < 1e-5


def test_single_actor_reuses_the_main_owner_instead_of_duplicating_clips():
    unit = SimpleNamespace(bones=[mdl_skel.Bone(name="Bip01")])
    assert actor_payloads(unit) == []
