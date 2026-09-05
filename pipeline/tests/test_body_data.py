from types import SimpleNamespace as NS

from elysium_pipeline.formats.mdl_skel import Seq
from elysium_pipeline.importers.body_data import first_reference_bases, project_body


def unit(id, labels, includes=(), invalid=0, layers=None):
    seq = [Seq(label, i, 2, 30., "ACT_IDLE", i, 0, autolayers=tuple((layers or {}).get(label, [])))
           for i,label in enumerate(labels)]
    return NS(id=id, sequences=seq, mdl={"sequences":[{"label":label,"index":i} for i,label in enumerate(labels)]
                   + [{"label":"", "index":len(labels)+i} for i in range(invalid)],
                   "includeModels":[{"asset":key} for key in includes]})


def test_repeated_include_blocks_count_even_after_first_owner_was_seen():
    units = {"a":unit("a",["a"],["b","c"]), "b":unit("b",["b"],["d"]),
             "c":unit("c",["c"],["b","e"]), "d":unit("d",["d"],invalid=2), "e":unit("e",["e"])}
    assert first_reference_bases(units,"a") == [("a",0),("b",1),("d",2),("c",5),("e",10)]


def test_true_cycles_stop_without_discarding_a_later_branch():
    units={"a":unit("a",["a"],["b","c"]),"b":unit("b",["b"],["a"]),"c":unit("c",["c"])}
    assert first_reference_bases(units,"a") == [("a",0),("b",1),("c",2)]


def test_rows_keep_duplicate_labels_and_layer_declaration_order():
    root, bank = "vtmb:model:body", "vtmb:model:bank"
    units={root:unit(root,["idle","host"],[bank],layers={"host":["delta","aim"]}),
           bank:unit(bank,["idle","aim","delta"])}
    products={root:{"clips":{"idle":{},"host":{}},"blendSpaces":{}},
              bank:{"clips":{"idle":{},"aim@host":{},"delta":{}},"blendSpaces":{}}}
    result=project_body(units,root,lambda owner,role:products[owner])
    assert [r["owner"] for r in result["sequences"] if r["label"]=="idle"] == [root,bank]
    assert [r["rawIndex"] for r in result["sequences"]] == [0,1,2,3,4]
    layers=result["sequences"][1]["layers"]
    assert [r["label"] for r in layers] == ["delta","aim"]
    assert layers[0]["sequence"].endswith("/A_delta.A_delta")
    assert layers[1]["sequence"].endswith("/A_aim_host.A_aim_host")
    assert result["unresolvedLayers"] == []
