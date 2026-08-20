"""Generate the current Enhanced Input slice from Config/ElysiumInputActions.csv.

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
    # The slice is stated rather than inferred: a row silently dropped from the CSV would generate a
    # smaller mapping context that still loads, and the first sign of it would be a button that does
    # nothing in a live run. `Elysium.Content.InputAssets` asserts the same set from the other side.
    #
    # Every `+` command remains a press/release pair so its edges survive the same command/replay
    # seam as keyboard and console input. Duck is a pair because the mover turns its press edge into
    # the game's crouch latch; Camera and the selection verbs are genuine one-shots.
    #
    # The D-pad is the weapon cluster: direct melee/ranged categories, last weapon and holster.
    # Repeating a category verb advances inside that category. These rows carry no keyboard default
    # because the legacy keyboard front already fires the same command strings.
    expected = [
        "Move", "Look", "MouseLook", "Jump", "Use", "Feed", "Duck", "Camera",
        "WalkRun", "Attack", "SecondaryAttack", "Reload", "DisciplineCast",
        "WeaponRanged", "WeaponMelee", "WeaponLast", "Holster", "Character", "Pause",
    ]
    if ids != expected:
        raise RuntimeError(
            "the input slice must contain exactly %s; got %r" % (", ".join(expected), ids)
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


def triggers_for(action_id, key_name, context):
    """Turn an analog shoulder trigger into one digital command edge.

    GameInput publishes LT/RT as axes. The commands they carry are Boolean actions, so their
    physical half-pull crosses one explicit threshold and stays down until it falls back below
    it. Without a Down trigger, tiny resting noise can start the action and a later full pull has
    no new edge to deliver.
    """
    if key_name not in ("Gamepad_LeftTriggerAxis", "Gamepad_RightTriggerAxis"):
        return []
    trigger = unreal.new_object(
        unreal.InputTriggerDown,
        outer=context,
        name=action_id + "_Down",
    )
    trigger.set_editor_property("actuation_threshold", 0.5)
    return [trigger]


def modifiers_for(action_id, device, context):
    """Build the modifier stack owned by one device mapping.

    Neither stick has a dead zone, a saturation, a response curve, a scalar, a Smooth, a
    ScaleByDeltaTime or an FOVScaling modifier here, and the omission is the design rather than an
    oversight. All of it is `ElysiumInput::ShapeStickLook` / `ShapeStickMove`
    (`Source/ElysiumUE/Public/ElysiumLookCurve.h`), asserted as Elysium.Substrate.StickLook:

    * the filter is a half-life and the turn ramp is a charge, so both need the frame's *clamped,
      dilated* delta -- an Enhanced Input modifier only ever sees Enhanced Input's raw one, so a
      level-load stall would emit a full turn in one command and a replay would not reproduce
      across frame rates;
    * the dead zone, the saturation and the curve key on the deflection's magnitude, so they are
      one radial gesture, not two per-axis ones -- Enhanced Input applies its stack per component;
    * a value that lives half in a `.uasset` and half in a function has two owners and only one of
      them can be asserted.

    The tuning surface is the `joy_*` group in the VtMB console store, so a live run tunes with
    `elysium.cmd joy_yawsensitivity 220` and a `config.cfg` keeps the answer.
    """
    if device == "Gamepad" and action_id == "Look":
        # Gamepad_Right2D reports physical stick-up on -Y. Elysium adds the action's Y directly to
        # FRotator::Pitch, where positive pitch is look-up, so invert Y once in the mapping and
        # leave the native GameInput axis untouched. This is a statement about the device's frame,
        # not about feel, which is why it is the one modifier that belongs here.
        return [
            make_modifier(
                unreal.InputModifierNegate,
                context,
                "Look_InvertNativeY",
                {"x": False, "y": True, "z": False},
            ),
        ]
    # Mouse2D deliberately carries **no** modifier. It is a displacement the hand already made, so
    # the only thing between the device and the view is `sensitivity x m_yaw/m_pitch`, which is
    # where retail puts it too. `UInputModifierSmooth` is specifically excluded: it is the legacy
    # `UPlayerInput::SmoothMouse` port, it averages against a hardcoded 0.0083s sample window
    # regardless of the real frame rate, and it drops its residual whenever input returns to zero --
    # so a fast flick lands short and a slow drag does not, which is inconsistent degrees per count.
    return []


def make_context(name, device, key_column, rows, actions):
    context = ensure_data_asset(name, ROOT, unreal.InputMappingContext)
    mappings = []
    for row in rows:
        key_name = row[key_column].strip()
        if not key_name:
            continue
        key = unreal.Key()
        key.set_editor_property("key_name", key_name)
        mapping = unreal.EnhancedActionKeyMapping()
        mapping.set_editor_property("action", actions[row["Id"].strip()])
        mapping.set_editor_property("key", key)
        mapping.set_editor_property(
            "modifiers", modifiers_for(row["Id"].strip(), device, context))
        mapping.set_editor_property(
            "triggers", triggers_for(row["Id"].strip(), key_name, context))
        mappings.append(mapping)
    mapping_data = unreal.InputMappingContextMappingData()
    mapping_data.set_editor_property("mappings", mappings)
    context.set_editor_property("default_key_mappings", mapping_data)
    return context


def main():
    rows = load_rows()
    unreal.EditorAssetLibrary.make_directory(ACTION_ROOT)

    actions = {}
    for row in rows:
        action_id = row["Id"].strip()
        action = ensure_data_asset("IA_" + action_id, ACTION_ROOT, unreal.InputAction)
        action.set_editor_property("value_type", value_type(row["ValueType"].strip()))
        actions[action_id] = action

    keyboard_mouse_context = make_context(
        "IMC_Player_KBM", "KeyboardMouse", "DefaultPrimary", rows, actions)
    gamepad_context = make_context(
        "IMC_Player_Gamepad", "Gamepad", "DefaultPad", rows, actions)

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

    for asset in list(actions.values()) + [keyboard_mouse_context, gamepad_context, action_set]:
        unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)
    unreal.log(
        "[input-assets] generated %d actions, IMC_Player_KBM, IMC_Player_Gamepad and action set"
        % len(actions))


main()
