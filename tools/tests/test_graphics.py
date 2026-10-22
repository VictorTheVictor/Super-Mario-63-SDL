import unittest
from tools.import_mario import compose, matrix, generate
from pathlib import Path
import tempfile
import xml.etree.ElementTree as ET


class MarioGraphicsTests(unittest.TestCase):
    def test_mirrored_parent_transform(self):
        self.assertEqual(compose((-1, 0, 0, 1, 3, -12), (1, 0, 0, 1, -8, -16)),
                         (-1, 0, 0, 1, 11, -28))

    def test_default_matrix(self):
        self.assertEqual(matrix(ET.fromstring('<item/>')), (1, 0, 0, 1, 0, 0))

    def test_instance_alpha_and_shadow(self):
        from tools.import_art import apply_color
        from PIL import Image
        pixel = Image.new('RGBA', (1,1), (200,100,50,255))
        faded = apply_color(pixel, ET.fromstring('<Color alphaMultiplier="0.5"/>'))
        self.assertEqual(faded.getpixel((0,0)), (200,100,50,128))
        shadow = apply_color(pixel, ET.fromstring('<Color brightness="-0.3"/>'))
        self.assertEqual(shadow.getpixel((0,0)), (140,70,35,255))

    def test_original_character_tracks(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / 'mario.hpp'
            generate(Path('Super_Mario_63_2010'), out)
            text = out.read_text()
            self.assertIn('array<Sprite, 8> walk', text)
            self.assertIn('array<Sprite, 9> run', text)
            self.assertIn('array<Sprite, 10> pound', text)
            self.assertIn('array<Sprite, 8> dive', text)
            self.assertIn('array<Sprite, 5> diveflip', text)
            self.assertIn('array<Sprite, 5> backflip', text)
            self.assertIn('Mario Air (none) - 2.png', text)
            self.assertNotIn('Cinema', text)
            self.assertIn('struct Wing',text)
            flying=text.split('flying = {{')[1].split('}};')[0]
            self.assertIn('1.30000000f,-18.50000000f',flying)
            self.assertIn('-8.95000000f,-19.50000000f',flying)


if __name__ == '__main__':
    unittest.main()
