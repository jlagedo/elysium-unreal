# Generates Content/VtMB/Materials/M_Gizmo.uasset and M_Gizmo_XRay.uasset: the master materials
# for the P2.4 retained entity-gizmo layer (one UInstancedStaticMeshComponent of unit cubes, one
# instance per entity). Both are unlit + two-sided + translucent and read their colour+opacity from
# PER-INSTANCE CUSTOM DATA (floats 0..3 = R,G,B,A), so the runtime tints and dims each instance with
# SetCustomDataValue — no per-frame draw calls, just an occasional single-instance GPU upload on an
# entity state change. M_Gizmo depth-tests (walls occlude, the "visible" mode); M_Gizmo_XRay disables
# the depth test (drawn on top, the "all" x-ray mode). A UMaterial graph only compiles offline, so
# these are generated locally; the runtime only instances them.
#
# Normally rebuilt by the umbrella (dev/elysium.ps1 content -> pipeline/unreal/build_content.py, which the export runs);
# also runnable standalone:
#   UnrealEditor-Cmd.exe ElysiumUE.uproject -run=pythonscript -script="pipeline/unreal/make_gizmo_material.py" -unattended -nosplash -nopause
import unreal

PKG = "/Game/VtMB/Materials"
mel = unreal.MaterialEditingLibrary


def build_gizmo(name, xray):
    asset = "%s/%s" % (PKG, name)
    if unreal.EditorAssetLibrary.does_asset_exist(asset):
        unreal.EditorAssetLibrary.delete_asset(asset)

    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset(name, PKG, unreal.Material, unreal.MaterialFactoryNew())
    if not mat:
        unreal.log_error("[make_gizmo_material] create_asset failed: %s" % asset)
        raise SystemExit(1)

    # Unlit, two-sided, translucent: a flat coloured see-through volume regardless of scene lighting.
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("two_sided", True)
    # Rendered through UInstancedStaticMeshComponent, so the ISM shader permutation must exist
    # (a packaged build has no shader compiler to recover it at runtime).
    mat.set_editor_property("used_with_instanced_static_meshes", True)

    # X-ray variant: draw on top of the world (no depth test), so gizmos are visible through walls.
    # The property name has moved across engine versions; guard so the asset still builds if it is
    # absent (the mode then reads the same as depth-tested rather than failing the content build).
    if xray:
        for prop in ("disable_depth_test", "b_disable_depth_test"):
            try:
                mat.set_editor_property(prop, True)
                break
            except Exception:
                continue

    # Per-instance colour+opacity: floats 0..3 = R,G,B,A. The runtime writes these with
    # SetCustomDataValue(instanceIndex, 0..3, value); the material reads them here.
    def custom(index, y):
        # DataIndex picks the float slot; the node's own default is never used because the runtime
        # always writes all four custom-data floats per instance (SetCustomDataValue).
        node = mel.create_material_expression(mat, unreal.MaterialExpressionPerInstanceCustomData, -700, y)
        node.set_editor_property("data_index", index)
        return node

    r = custom(0, -180)
    g = custom(1, -60)
    b = custom(2, 60)
    a = custom(3, 200)

    # Assemble R,G,B into an emissive float3.
    rg = mel.create_material_expression(mat, unreal.MaterialExpressionAppendVector, -480, -120)
    mel.connect_material_expressions(r, "", rg, "A")
    mel.connect_material_expressions(g, "", rg, "B")
    rgb = mel.create_material_expression(mat, unreal.MaterialExpressionAppendVector, -300, -60)
    mel.connect_material_expressions(rg, "", rgb, "A")
    mel.connect_material_expressions(b, "", rgb, "B")

    mel.connect_material_property(rgb, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(a, "", unreal.MaterialProperty.MP_OPACITY)

    mel.recompile_material(mat)
    ok = unreal.EditorAssetLibrary.save_asset(asset, only_if_is_dirty=False)
    unreal.log("[make_gizmo_material] saved %s: %s" % (asset, "ok" if ok else "FAILED"))
    if not ok:
        raise SystemExit(1)


build_gizmo("M_Gizmo", xray=False)
build_gizmo("M_Gizmo_XRay", xray=True)
