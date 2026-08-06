"""Generate the first Enhanced Input slice from Config/ElysiumInputActions.csv.

The CSV is committed policy. The UInputAction, UInputMappingContext and UElysiumInputActionSet
packages are local, regenerable Unreal assets and remain ignored like every other policy package.
"""

import csv
from pathlib import Path

import unreal


ROOT = "/Game/Input"
ACTION_ROOT = ROOT + "/Actions"
CSV_PATH = Path(unreal.Paths.project_dir()) / "Config" / "ElysiumInputActions.csv"
EXPECTED_COLUMNS = (
    "Id", "Command", "Label", "Group", "ValueType", "Pair",
    "DefaultPrimary", "DefaultAlt", "DefaultPad",
)


def load_rows():
    with CSV_PATH.open("r", encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        if tuple(reader.fieldnames or ()) != EXPECTED_COLUMNS:
            raise RuntimeError(
                "input CSV columns are %r, expected %r"
                % (tuple(reader.fieldnames or ()), EXPECTED_COLUMNS)
            )
        rows = list(reader)

    ids = [row["Id"].strip() for row in rows]
    if not ids or any(not value for value in ids) or len(ids) != len(set(ids)):
        raise RuntimeError("input action ids must be non-empty and unique")
    if ids != ["Move", "Look", "Jump"]:
        raise RuntimeError(
            "the gamepad vertical slice must contain exactly Move, Look and Jump; got %r" % ids
        )
    return rows


def ensure_data_asset(name, package, cls):
    target = "%s/%s" % (package, name)
    asset = unreal.load_asset(target)
    if asset is not None:
        return asset
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", cls)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, package, cls, factory)
    if asset is None:
        raise RuntimeError("could not create %s" % target)
    return asset


def value_type(name):
    values = {
        "Boolean": unreal.InputActionValueType.BOOLEAN,
        "Axis2D": unreal.InputActionValueType.AXIS2D,
    }
    try:
        return values[name]
    except KeyError:
        raise RuntimeError("unsupported input ValueType %r" % name)


def make_modifier(cls, outer, name, properties=None):
    modifier = unreal.new_object(cls, outer=outer, name=name)
    for prop, value in (properties or {}).items():
        modifier.set_editor_property(prop, value)
    return modifier


def modifiers_for(action_id, context):
    # Microsoft publishes 7849/32767 and 8689/32767 as the established Xbox left/right
    # thumbstick baselines. Rounded values keep the generated policy legible while retaining
    # the radial shape and range remapping recommended by both Microsoft and Enhanced Input.
    lower_threshold = 0.24 if action_id == "Move" else 0.265
    dead_zone = make_modifier(
        unreal.InputModifierDeadZone,
        context,
        action_id + "_DeadZone",
        {
            "lower_threshold": lower_threshold,
            "upper_threshold": 1.0,
            "type": unreal.DeadZoneType.RADIAL,
        },
    )
    if action_id == "Move":
        return [dead_zone]
    if action_id == "Look":
        return [
            dead_zone,
            make_modifier(
                unreal.InputModifierResponseCurveExponential,
                context,
                "Look_Response",
                {"curve_exponent": unreal.Vector(1.0, 1.0, 1.0)},
            ),
            # Gamepad_Right2D reports physical stick-up on -Y. Elysium adds the action's Y
            # directly to FRotator::Pitch, where positive pitch is look-up, so invert Y once
            # in the mapping and leave the native GameInput axis untouched.
            make_modifier(
                unreal.InputModifierNegate,
                context,
                "Look_InvertNativeY",
                {"x": False, "y": True, "z": False},
            ),
            # The stack ends at the RATE. It deliberately carries neither ScaleByDeltaTime nor
            # FOVScaling:
            #
            # ScaleByDeltaTime would multiply by Enhanced Input's own raw frame delta, which
            # bypasses the ClampFrameDelta / time-dilation normalisation `SampleFrame` applies to
            # every other look source — a level-load stall would emit a full turn in one command
            # and a replay would not reproduce across frame rates. `Build` applies the clamped
            # delta instead.
            #
            # FOVScaling is not neutral at FOVScale 1.0: Standard normalises against an 80-degree
            # base, so the rate would be multiplied by tan(FOV/2)/tan(40) — about 1.19 at the
            # shipped 90-degree FOV, and roughly halved by any scene or scope that drops FOV.
            # `cl_yawspeed` is a flat rate and does not move with the camera.
            make_modifier(
                unreal.InputModifierScalar,
                context,
                "Look_Rate",
                {"scalar": unreal.Vector(210.0, 225.0, 1.0)},
            ),
        ]
    return []


def main():
    rows = load_rows()
    unreal.EditorAssetLibrary.make_directory(ACTION_ROOT)

    actions = {}
    for row in rows:
        action_id = row["Id"].strip()
        action = ensure_data_asset("IA_" + action_id, ACTION_ROOT, unreal.InputAction)
        action.set_editor_property("value_type", value_type(row["ValueType"].strip()))
        actions[action_id] = action

    context = ensure_data_asset("IMC_Player_Gamepad", ROOT, unreal.InputMappingContext)
    mappings = []
    for row in rows:
        pad_key = row["DefaultPad"].strip()
        if not pad_key:
            continue
        key = unreal.Key()
        key.set_editor_property("key_name", pad_key)
        mapping = unreal.EnhancedActionKeyMapping()
        mapping.set_editor_property("action", actions[row["Id"].strip()])
        mapping.set_editor_property("key", key)
        mapping.set_editor_property("modifiers", modifiers_for(row["Id"].strip(), context))
        mappings.append(mapping)
    mapping_data = unreal.InputMappingContextMappingData()
    mapping_data.set_editor_property("mappings", mappings)
    context.set_editor_property("default_key_mappings", mapping_data)

    action_set = ensure_data_asset(
        "DA_ElysiumInputActions", ROOT, unreal.ElysiumInputActionSet)
    definitions = []
    for row in rows:
        definition = unreal.ElysiumInputActionDefinition()
        definition.set_editor_property("id", row["Id"].strip())
        definition.set_editor_property("action", actions[row["Id"].strip()])
        definition.set_editor_property("command", row["Command"].strip())
        definition.set_editor_property("button_pair", row["Pair"].strip().lower() == "true")
        definitions.append(definition)
    action_set.set_editor_property("actions", definitions)

    for asset in list(actions.values()) + [context, action_set]:
        unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)
    unreal.log("[input-assets] generated 3 actions, IMC_Player_Gamepad and action set")


main()
