import math
import struct
import unittest

from elysium_pipeline.exporters import UE_mdl_skeletal
from elysium_pipeline.formats import eskm, mdl_skel


class MdlAttachmentTests(unittest.TestCase):
    @staticmethod
    def image(matrix=None):
        image = bytearray(560)
        struct.pack_into("<i", image, 240, 2)       # NumBones
        struct.pack_into("<ii", image, 328, 1, 400)
        struct.pack_into("<iii", image, 400, 120, 7, 1)
        values = matrix or (
            1.0, 0.0, 0.0, 1.4,
            0.0, 1.0, 0.0, 4.5,
            0.0, 0.0, 1.0, 0.0,
        )
        struct.pack_into("<12f", image, 412, *values)
        image[520:526] = b"mouth\0"
        return image

    def test_decodes_record_relative_name_bone_and_local_matrix(self):
        record = mdl_skel.attachments(self.image())[0]

        self.assertEqual(record.name, "mouth")
        self.assertEqual(record.flags, 7)
        self.assertEqual(record.bone, 1)
        self.assertAlmostEqual(record.pos[0], 1.4, places=6)
        self.assertAlmostEqual(record.pos[1], 4.5, places=6)
        self.assertAlmostEqual(record.pos[2], 0.0, places=6)
        self.assertEqual(record.quat, (0.0, 0.0, 0.0, 1.0))

    def test_rotation_matrix_becomes_a_unit_quaternion(self):
        record = mdl_skel.attachments(self.image((
            0.0, -1.0, 0.0, 0.0,
            1.0, 0.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0,
        )))[0]

        self.assertAlmostEqual(record.quat[2], math.sqrt(0.5), places=6)
        self.assertAlmostEqual(record.quat[3], math.sqrt(0.5), places=6)

    def test_eskm_attachment_is_unreal_native_and_uses_emitted_bone(self):
        payload = UE_mdl_skeletal._attachment_section(self.image(), [0, 2])
        container = UE_mdl_skeletal._assemble([(b"ATCH", payload)])

        name, bone, translation, rotation = eskm.attachments(container)[0]
        self.assertEqual(name, "mouth")
        self.assertEqual(bone, 2)
        self.assertAlmostEqual(translation[0], 1.4 * 2.54, places=5)
        self.assertAlmostEqual(translation[1], -4.5 * 2.54, places=5)
        self.assertAlmostEqual(translation[2], 0.0, places=5)
        self.assertEqual(rotation, (0.0, 0.0, 0.0, 1.0))

    def test_rejects_attachment_outside_the_model_image(self):
        image = self.image()
        struct.pack_into("<i", image, 332, 540)
        with self.assertRaisesRegex(ValueError, "runs past"):
            mdl_skel.attachments(image)


if __name__ == "__main__":
    unittest.main()
