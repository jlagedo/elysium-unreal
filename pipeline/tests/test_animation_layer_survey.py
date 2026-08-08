import struct
import unittest
from unittest import mock

from research.tooling.probes import animation_layer_survey


def _layer_model():
    base = 300
    count = 3
    image = bytearray(base + count * animation_layer_survey.SEQDESC_STRIDE + 200)
    image[:4] = b"IDST"
    struct.pack_into("<i", image, 4, 2531)
    struct.pack_into("<ii", image, 272, count, base)
    names = ("host", "aim_layer", "swing_delta")
    name_base = base + count * animation_layer_survey.SEQDESC_STRIDE + 32
    cursor = name_base
    for index, name in enumerate(names):
        descriptor = base + index * animation_layer_survey.SEQDESC_STRIDE
        encoded = name.encode("ascii") + b"\0"
        struct.pack_into("<i", image, descriptor, cursor - descriptor)
        image[cursor:cursor + len(encoded)] = encoded
        cursor += len(encoded)
    host = base
    entries = cursor + 8
    struct.pack_into("<ii", image, host + 660, 2, entries - host)
    struct.pack_into("<ii", image, entries, 1, 2)
    return bytes(image)


class _FakeImage:
    def __init__(self, data):
        self.data = data

    def va_to_offset(self, va):
        offset = va - 0x10000000
        return offset if 0 <= offset < len(self.data) else None


class AnimationLayerSurveyTests(unittest.TestCase):
    def test_surveys_ordered_model_autolayer_bindings(self):
        index = {"models/character/shared/test.mdl": ("synthetic", None)}
        with mock.patch.object(
            animation_layer_survey.install, "read", return_value=_layer_model()
        ):
            report = animation_layer_survey.survey_models(index)

        self.assertEqual(
            (report["summary"]["v2531_models"], report["summary"]["sequences"],
             report["summary"]["hosts"], report["summary"]["entries"]),
            (1, 3, 1, 2),
        )
        self.assertEqual(report["count_histogram"], {"0": 2, "2": 1})
        self.assertEqual(report["target_suffixes"], {"delta": 1, "layer": 1})
        self.assertEqual(report["two_entry_order"], {"layer->delta": 1})
        self.assertEqual(
            report["bindings"][0]["targets"], ["aim_layer", "swing_delta"]
        )

    def test_pins_full_weight_autolayers_and_point_one_combat_layers(self):
        server = bytearray(0x99100)
        client = bytearray(0x8A200)
        push_offset = animation_layer_survey.CLIENT_AUTOLAYER_WEIGHT_PUSH - 0x10000000
        client[push_offset:push_offset + 5] = b"\x68" + struct.pack("<I", 0x3F800000)
        call_offset = animation_layer_survey.CLIENT_ACCUMULATOR_CALL - 0x10000000
        relative = (animation_layer_survey.CLIENT_ACCUMULATOR -
                    (animation_layer_survey.CLIENT_ACCUMULATOR_CALL + 5))
        client[call_offset:call_offset + 5] = b"\xe8" + struct.pack("<i", relative)
        constants = {
            0x1009905B: 0x3F800000,
            0x10099067: 0x3E4CCCCD,
            0x10099071: 0x3E4CCCCD,
            0x1009907B: 0x3DCCCCCD,
            0x10099085: 0x3F800000,
        }
        for va, value in constants.items():
            struct.pack_into("<I", server, va - 0x10000000, value)

        with mock.patch.object(animation_layer_survey, "PEImage", _FakeImage):
            policy = animation_layer_survey.decode_native_policy(
                bytes(server), bytes(client)
            )

        self.assertEqual(policy["model_autolayers"]["caller_weight"], 1.0)
        self.assertEqual(policy["combat_layers"]["initial_weight"], 0.1)
        self.assertEqual(policy["combat_layers"]["blend_fields"], [0.2, 0.2])


if __name__ == "__main__":
    unittest.main()
