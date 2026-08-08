import struct
import unittest
from unittest import mock

from research.tooling.probes import animation_event_survey


def _event_model():
    descriptor = 300
    event_base = 1100
    label_base = 1180
    image = bytearray(1200)
    image[:4] = b"IDST"
    struct.pack_into("<i", image, 4, 2531)
    struct.pack_into("<ii", image, 272, 1, descriptor)
    struct.pack_into("<i", image, descriptor, label_base - descriptor)
    struct.pack_into("<ii", image, descriptor + 20, 1, event_base - descriptor)
    struct.pack_into("<fii64s", image, event_base, 0.5, 1003, 0, b"8\0")
    image[label_base:label_base + 5] = b"scene\0"
    return bytes(image)


class AnimationEventSurveyTests(unittest.TestCase):
    def test_surveys_patch_first_event_records_by_model_partition(self):
        index = {
            "models/character/test.mdl": ("synthetic", None),
            "models/readme.txt": ("synthetic", None),
        }
        with mock.patch.object(
            animation_event_survey.install, "read", return_value=_event_model()
        ):
            report = animation_event_survey.survey_models(index)

        all_models = report["partitions"]["all"]
        character = report["partitions"]["character"]
        self.assertEqual((all_models["models"], all_models["event_sequences"], all_models["events"]),
                         (1, 1, 1))
        self.assertEqual((character["models"], character["events"]), (1, 1))
        self.assertEqual(all_models["event_ids"], {"1003": 1})
        self.assertEqual(all_models["types"], {"0": 1})
        self.assertEqual(all_models["options"], {"8": 1})
        self.assertEqual(report["invalid_records"], [])
        self.assertEqual(
            report["event_sequences"][0]["events"][0],
            {"cycle": 0.5, "event": 1003, "type": 0, "options": "8"},
        )

    def test_policy_ledgers_cover_every_confirmed_native_handler_body(self):
        self.assertEqual(len(animation_event_survey.HANDLER_POLICIES), 20)
        self.assertEqual(len(animation_event_survey.SERVER_WEAPON_POLICIES), 7)
        self.assertEqual(len(animation_event_survey.CLIENT_FIRE_EVENT_POLICIES), 3)
        self.assertEqual(len(animation_event_survey.CLIENT_WEAPON_EVENT_POLICIES), 1)
        self.assertEqual(
            animation_event_survey.HANDLER_POLICIES[0x10071900]["routes"][0]["event_ids"],
            [1003],
        )
        self.assertEqual(
            animation_event_survey.HANDLER_POLICIES[0x1024F0C0]["routes"], []
        )
        self.assertEqual(
            animation_event_survey.SERVER_WEAPON_POLICIES[0x103E8BE0]["routes"][1]["event_ids"],
            [3045, 3046],
        )
        self.assertEqual(
            animation_event_survey.CLIENT_FIRE_EVENT_POLICIES[0x10099C00]["routes"][1]["event_ids"],
            [5120],
        )


if __name__ == "__main__":
    unittest.main()
