import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
DEFINITION = ROOT / "pipeline" / "unreal" / "dialogue_camera_set.json"
GENERATOR = ROOT / "pipeline" / "unreal" / "make_dialogue_camera_set.py"


class DialogueCameraSetTests(unittest.TestCase):
    def test_project_owned_definition_is_complete(self):
        data = json.loads(DEFINITION.read_text(encoding="utf-8"))
        self.assertEqual(data["schema"], 1)
        self.assertEqual(
            data["asset"], "/Game/Elysium/Camera/DA_ElysiumDialogueCameraSet")
        profiles = data["profiles"]
        self.assertEqual(len(profiles), 7)
        self.assertEqual(len({row["name"] for row in profiles}), len(profiles))
        self.assertEqual(profiles[0]["kind"], "TWO_SHOT")
        self.assertTrue(profiles[0]["establishing"])
        self.assertEqual(profiles[-1]["kind"], "FALLBACK")
        self.assertTrue(any(
            row["kind"].startswith("OVER_SHOULDER") for row in profiles))
        self.assertTrue(any(row["allow_close_up"] for row in profiles))

        authored_text = DEFINITION.read_text(encoding="utf-8").lower()
        for forbidden in ("/elysiumbaked/", "/game/vtmb/", "vdata/camerashots/"):
            self.assertNotIn(forbidden, authored_text)

        generator_text = GENERATOR.read_text(encoding="utf-8")
        self.assertIn("DA_ElysiumDialogueCameraSet", generator_text)
        self.assertIn("ElysiumDialogueCameraSet", generator_text)


if __name__ == "__main__":
    unittest.main()
