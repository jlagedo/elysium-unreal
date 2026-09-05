from elysium_pipeline.importers.cast_data import project_cast


def model(key, **fields):
    return {"assetId":"vtmb:model:"+key,"key":key,"meshAsset":"/ElysiumBaked/Models/SK_"+key.replace("/","_"),
            "skeletonAsset":None,"animationSkeletonAsset":None,**fields}


def test_basename_collisions_remain_ambiguous_and_full_identity_always_resolves():
    a,b=model("character/a/body"),model("character/b/body")
    data=project_cast({"assets":[a,b],"inventory":[]})
    assert "body" not in data["aliases"]
    assert data["ambiguousAliases"]["body"] == [a["assetId"],b["assetId"]]
    assert data["aliases"]["models/character/a/body.mdl"] == a["assetId"]
    assert data["aliases"]["character_b_body"] == b["assetId"]
    assert data["models"][a["assetId"]]["stem"] == "character_a_body"


def test_cinematic_actors_keep_exact_root_to_body_asset_bindings():
    entry=model("cinematic/set",nativeMainOwner=False,cinematic=True,actors=[{"root":"Bip01"},{"root":"Bip02"}])
    data=project_cast({"assets":[entry],"inventory":[]})
    roots=data["cinematics"][entry["assetId"]]["roots"]
    assert set(roots)=={"Bip01","Bip02"}
    assert roots["Bip02"]["ownerRoot"]=="Bip02"
    assert roots["Bip02"]["bodyData"].endswith("/DA_set_Bip02.DA_set_Bip02")
    assert data["models"][entry["assetId"]]["bodyData"]==""


def test_expanded_corpus_does_not_retarget_existing_vv_capture_key():
    original=model("character/npc/unique/downtown/vv/vv")
    variant=model("character/npc/unique/hollywood/vvstrip/vv")
    data=project_cast({"assets":[variant,original],"inventory":[]})
    assert data["aliases"]["vv"]==original["assetId"]
    assert data["models"][original["assetId"]]["stem"]=="vv"
    assert data["aliases"]["character_npc_unique_hollywood_vvstrip_vv"]==variant["assetId"]
    assert len(data["ambiguousAliases"]["vv"])==2
