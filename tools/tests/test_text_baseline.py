import unittest
from import_signs import Renderer

class TextBaselineTests(unittest.TestCase):
    def test_words_share_a_baseline(self):
        renderer=Renderer.__new__(Renderer)
        renderer.fonts={}
        renderer.symbol=lambda name:None
        renderer.render=lambda *args,**kwargs:None
        image=renderer.text_card('AAA xxx')
        # Capitals and lowercase have different ink tops but the same baseline.
        left=image.crop((0,0,225,90)).getbbox()
        right=image.crop((225,0,450,90)).getbbox()
        self.assertIsNotNone(left)
        self.assertIsNotNone(right)
        self.assertLessEqual(abs(left[3]-right[3]),1)
