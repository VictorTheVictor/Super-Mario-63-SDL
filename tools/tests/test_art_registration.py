import sys, unittest
from pathlib import Path
import xml.etree.ElementTree as ET
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from import_art import shape_transform, point
from import_mario import matrix,compose,NS
from import_level import edges

class RegistrationTests(unittest.TestCase):
    def test_chain_chomp_log_registration(self):
        path=Path('Super_Mario_63_2010/LIBRARY/Course Clips/1 - Bob-omb Battlefield/Areas 1-1 & 1-2/1-2 Back Graphics.xml')
        for group in ET.parse(path).findall('.//x:DOMGroup',NS):
            for shape in group.findall('./x:members/x:DOMShape',NS):
                fill=shape.find('./x:fills/x:FillStyle/x:BitmapFill',NS)
                if fill is None or not fill.get('bitmapPath','').endswith('YI Log Side'):continue
                transform=shape_transform(matrix(group),matrix(shape),'DOMShape',matrix(group))
                import re
                pts=[point(transform,x,y) for edge in shape.findall('./x:edges/x:Edge',NS)
                     for a,b,c,d in edges(re.sub(r'S\d+','',edge.get('edges'))) for x,y in [(a,b),(c,d)]]
                self.assertAlmostEqual(min(x for x,y in pts),590.45)
                self.assertAlmostEqual(min(y for x,y in pts),4.6)
                self.assertAlmostEqual(max(y for x,y in pts),34.35)
                return
        self.fail('Source log below first red coin not found')

    def test_distinct_nested_transforms_are_retained(self):
        parent=(1,0,0,1,50,20);local=(1,0,0,1,2,3)
        self.assertEqual(shape_transform(parent,local,'DOMShape',parent),compose(parent,local))
        self.assertEqual(shape_transform(parent,parent,'DOMBitmapInstance',parent),compose(parent,parent))

    def test_invisible_camera_trigger_is_not_art(self):
        from import_art import hidden_instance
        path=Path('Super_Mario_63_2010/LIBRARY/Course Clips/1 - Bob-omb Battlefield/Areas 1-1 & 1-2/1-2 Back Graphics.xml')
        trigger=next(e for e in ET.parse(path).findall('.//x:DOMSymbolInstance',NS) if e.get('libraryItemName','').endswith('/1-2 Camera Zoom Out Clip'))
        self.assertTrue(hidden_instance(trigger))
        visible=ET.fromstring('<DOMSymbolInstance xmlns="http://ns.adobe.com/xfl/2008/"/>')
        self.assertFalse(hidden_instance(visible))
