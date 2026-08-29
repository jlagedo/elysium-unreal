"""Generate the player animation graph, ABP_ElysiumBiped, from its tracked graph text.

The graph is a **template** Animation Blueprint: no target skeleton, and no node holding an asset.
Every asset it plays arrives at runtime on a pin the native parent writes, so one graph poses every
model the resolver picks assets for and the tracked source references nothing generated.

The graph itself is `graphs/ABP_ElysiumBiped.t3d` — the engine's own clipboard format, which is what
lets a node graph be tracked as reviewable text instead of a binary package. The asset is a
generated, ignored, regenerable package like every other under `/Game/ElysiumGenerated`.

Round trip: run this, open the asset, edit it in the animation editor, select the graph, Ctrl+C, and
paste over the `.t3d`. The editor stays the authoring tool; the repository stores the text.
"""

from pathlib import Path

import unreal

from pipeline.unreal import _bootstrap  # noqa: F401, E402
from elysium_pipeline import mounts

PACKAGE = mounts.ANIMATION
ASSET = "ABP_ElysiumBiped"
GRAPH = "AnimGraph"
PARENT = unreal.ElysiumBipedAnimInstance
T3D_PATH = Path(unreal.Paths.project_dir()) / "pipeline" / "unreal" / "graphs" / (ASSET + ".t3d")


def log(message):
    unreal.log("[animbp] %s" % message)


def fail(message):
    unreal.log_error("[animbp] %s" % message)
    raise RuntimeError(message)


def create_blueprint():
    """The template Animation Blueprint, created once and re-imported into thereafter.

    Reused rather than deleted, because a loaded package cannot reliably be deleted from under a
    commandlet -- and nothing of a previous run needs to survive anyway: the import replaces every
    node in the graph, and the asset carries no variables or functions of its own.
    """
    target = "%s/%s" % (PACKAGE, ASSET)
    # Asked of the registry rather than by loading: `load_asset` on an absent package warns, and a
    # first run on a clean checkout is the ordinary case rather than something to report.
    if unreal.EditorAssetLibrary.does_asset_exist(target):
        log("re-importing into the existing %s" % target)
        return unreal.load_asset(target)

    factory = unreal.AnimBlueprintFactory()
    # A template carries no skeleton, which is what makes it legal for the asset to reference
    # nothing generated -- and what lets one graph bind to whichever rig family a body was baked
    # against.
    factory.set_editor_property("template", True)
    factory.set_editor_property("parent_class", PARENT)

    tools = unreal.AssetToolsHelpers.get_asset_tools()
    blueprint = tools.create_asset(ASSET, PACKAGE, None, factory)
    if blueprint is None:
        fail("could not create %s" % target)
    return blueprint


def import_graph(blueprint):
    if not T3D_PATH.is_file():
        fail("no graph text at %s" % T3D_PATH)
    text = T3D_PATH.read_text(encoding="utf-8")

    result = unreal.ElysiumAnimGraphLibrary.import_graph_from_text(blueprint, GRAPH, text)
    for error in result.errors:
        unreal.log_error("[animbp] %s" % error)
    if result.errors:
        fail("graph import failed with %d error(s)" % len(result.errors))
    log("imported %d node(s) into %s" % (result.nodes, GRAPH))


def main():
    blueprint = create_blueprint()
    import_graph(blueprint)

    if not unreal.BlueprintEditorLibrary.compile_blueprint(blueprint):
        fail("%s did not compile" % ASSET)
    if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint):
        fail("%s did not save" % ASSET)
    log("wrote %s/%s" % (PACKAGE, ASSET))


main()
