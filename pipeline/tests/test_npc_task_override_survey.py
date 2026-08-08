import unittest

from research.tooling.probes.npc_task_override_survey import (
    _task_id_at_reference,
    _task_owner,
    group_handlers,
)


class NpcTaskOverrideSurveyTests(unittest.TestCase):
    def test_decodes_shared_push8_and_push32_registrations(self):
        push8 = b"\x6a\x4b\x68" + b"\x11\x22\x33\x44"
        self.assertEqual(_task_id_at_reference(push8, 3), (0x4B, "push8"))

        push32 = b"\x68\x3a\x01\x00\x00\x68" + b"\x11\x22\x33\x44"
        self.assertEqual(_task_id_at_reference(push32, 6), (0x13A, "push32"))

    def test_decodes_class_local_stack_pair(self):
        code = (
            b"\xc7\x44\x24\x0c" + b"\x11\x22\x33\x44" +
            b"\xc7\x44\x24\x10" + b"\x5a\x01\x00\x00"
        )
        self.assertEqual(_task_id_at_reference(code, 4), (0x15A, "stack_pair"))

    def test_owner_prefers_long_ming_tentacle_prefix(self):
        self.assertEqual(_task_owner("TASK_WAIT", 1), "shared")
        self.assertEqual(
            _task_owner("TASK_VMING_XIAO_TENTACLE_PLAY_HIT", 0x154),
            "CNPC_VMingXiaoTentacle",
        )
        self.assertEqual(
            _task_owner("TASK_VMING_XIAO_PLAY_HEAD_HIT", 0x15B),
            "CNPC_VMingXiao",
        )

    def test_handler_group_marks_shared_and_policy_bodies(self):
        classes = [
            {"cpp_class": "CAI_BaseNPC", "task_functions": {
                "start": "0x102827f0", "run": "0x10288780"}},
            {"cpp_class": "CNPC_VTest", "task_functions": {
                "start": "0x10300000", "run": "0x10300020"}},
        ]
        policies = [{"phase": "start", "handler": "0x10300000"}]
        groups = group_handlers(classes, policies)
        by_key = {(row["phase"], row["handler"]): row for row in groups}
        self.assertTrue(by_key[("start", "0x102827f0")]["shared_dispatcher"])
        self.assertTrue(by_key[("start", "0x10300000")]["direct_animation_policy"])
        self.assertFalse(by_key[("run", "0x10300020")]["direct_animation_policy"])


if __name__ == "__main__":
    unittest.main()
