"""Generate the committed, game-agnostic Audio Mixer routing assets.

Loose VtMB media stays under tools/out and is never imported. These assets contain policy only:
semantic classes, category submixes, the reverb return, concurrency groups and attenuation
templates. Idempotent and run by build_content.py.
"""
import unreal

ROOT = "/Game/VtMB/Audio"
CATEGORIES = ("Master", "Music", "Dialogue", "Ambience", "SFX", "UI")


def ensure(name, cls, factory_cls):
    path = f"{ROOT}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.EditorAssetLibrary.load_asset(path)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    asset = tools.create_asset(name, ROOT, cls, factory_cls())
    if not asset:
        raise RuntimeError(f"could not create {path}")
    return asset


def main():
    unreal.EditorAssetLibrary.make_directory(ROOT)

    classes = {
        category: ensure(f"SC_{category}", unreal.SoundClass, unreal.SoundClassFactory)
        for category in CATEGORIES
    }
    master = classes["Master"]
    master.set_editor_property(
        "child_classes", [classes[c] for c in CATEGORIES if c != "Master"])

    for category in CATEGORIES:
        ensure(f"SM_{category}", unreal.SoundSubmix, unreal.SoundSubmixFactory)
    ensure("SM_ReverbReturn", unreal.SoundSubmix, unreal.SoundSubmixFactory)

    for category in CATEGORIES:
        ensure(f"CB_{category}", unreal.SoundControlBus, unreal.SoundControlBusFactory)
    ensure("CBM_User", unreal.SoundControlBusMix, unreal.SoundControlBusMixFactory)

    # SoundClass is the semantic route a procedural component can override at runtime. Its default
    # submix makes that route concrete without importing or manufacturing one asset per VtMB file.
    for category, sound_class in classes.items():
        props = sound_class.get_editor_property("properties")
        props.set_editor_property(
            "default_submix",
            unreal.EditorAssetLibrary.load_asset(f"{ROOT}/SM_{category}"))
        sound_class.set_editor_property("properties", props)

    for name in ("UI", "Dialogue", "Music", "SFX", "Ambience"):
        ensure(f"Concurrency_{name}", unreal.SoundConcurrency, unreal.SoundConcurrencyFactory)
    for name in ("Point", "Dialogue", "Mover", "Ambient"):
        ensure(f"Attenuation_{name}", unreal.SoundAttenuation, unreal.SoundAttenuationFactory)

    for category, asset in classes.items():
        unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)
    unreal.EditorAssetLibrary.save_directory(ROOT, only_if_is_dirty=False, recursive=True)
    unreal.log(f"[audio-routing] generated semantic routing assets under {ROOT}")


main()
