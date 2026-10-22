import unittest
from tools.import_level import coordinate, edges, rasterize


class LevelImporterTests(unittest.TestCase):
    def test_signed_hex_and_twips(self):
        self.assertAlmostEqual(coordinate("#FFFD24.27"), -731.84765625 / 20)
        self.assertAlmostEqual(coordinate("#19F.2"), 415.125 / 20)
        self.assertEqual(coordinate("-20"), -1)

    def test_rectangle_and_hole(self):
        shape = edges("!0 0|200 0!200 0|200 200!200 200|0 200!0 200|0 0"
                      "!40 40|40 160!40 160|160 160!160 160|160 40!160 40|40 40")
        spans = rasterize(shape)
        self.assertEqual(sum(x1 - x0 for _, x0, x1 in spans), 64)
        self.assertEqual([s for s in spans if s[0] == 3], [(3, 0, 2), (3, 8, 10)])

    def test_quadratic_is_flattened(self):
        segments = edges("!0 0[100 200 200 0")
        self.assertGreater(len(segments), 4)
        self.assertEqual(segments[0][:2], (0, 0))
        self.assertEqual(segments[-1][2:], (10, 0))
        self.assertAlmostEqual(max(s[3] for s in segments), 5)

    def test_unsupported_syntax_fails(self):
        with self.assertRaises(ValueError):
            edges("!0 0S4|20 20")


if __name__ == "__main__":
    unittest.main()
