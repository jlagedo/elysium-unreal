from pathlib import Path
from types import SimpleNamespace

from elysium_pipeline.formats import eskm, mdl_skel
from elysium_pipeline.skeletal_stage import families


def test_partition_reads_the_whole_bank_set_without_sampling_animation(monkeypatch):
    definitions = {"a": [("Bip01", -1), ("spine", 0), ("hair", 1)],
                   "b": [("Bip01", -1), ("spine", 0), ("arm", 1)],
                   "c": [("Bip01", -1), ("spine", 0), ("hair", 0)]}
    units = {"vtmb:model:shared/" + name: {"path": Path(name), "identity": {"roles": ["include-only"]}}
             for name in definitions}
    def metadata(path):
        bones = [mdl_skel.Bone(index=i, name=name, parent=parent, pos=(0., 0., 0.),
                               quat=(0., 0., 0., 1.), flags=0)
                 for i, (name, parent) in enumerate(definitions[path.name])]
        return SimpleNamespace(bones=bones,
                               sequences=[mdl_skel.Seq("idle", 0, 1, 30., "", 0, 0)],
                               bone_weights=lambda base: [1.] * len(bones))
    monkeypatch.setattr(families.ModelUnit, "metadata", metadata)
    result, rigs = families.build(units, {})
    assert result["scope"] == "whole-model-corpus"
    assert len(result["owners"]) == 3
    assert sorted(len(g["members"]) for g in result["families"]) == [1, 2]
    assert all(set(eskm.directory(data)) == {b"SKEL"} for data in rigs.values())
    reversed_result, _ = families.build(dict(reversed(list(units.items()))), {})
    assert result == reversed_result
    assert all("/Models/_Corpus/SKEL_Family_" in g["skeletonAsset"] for g in result["families"])
