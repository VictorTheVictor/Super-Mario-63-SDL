import sys
import unittest
from pathlib import Path
import xml.etree.ElementTree as ET
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from import_world import collect, active_frame, playback_frame

class WorldTests(unittest.TestCase):
    def test_frame_spans(self):
        frame=ET.fromstring('<DOMFrame index="2" duration="3"/>')
        self.assertFalse(active_frame(frame,1))
        self.assertTrue(active_frame(frame,2))
        self.assertTrue(active_frame(frame,4))
        self.assertFalse(active_frame(frame,5))

    def test_mountain_geometry_and_delayed_exits(self):
        root=Path('Super_Mario_63_2010')
        for index,exit_count in [(3,2),(4,1)]:
            spans,bounds,objects,exits=collect(root,index)
            self.assertGreater(len(spans),100)
            self.assertEqual(len(exits),exit_count)
            self.assertGreater(len(objects),40)
            self.assertLess(bounds[1],-1000)
        # The mountain has several collision fill styles; all must survive.
        self.assertEqual(collect(root,3)[1],(-166,-2001,2370,45))

    def test_short_foregrounds_keep_their_ground(self):
        from import_world import AREAS, NS, PLAY_FRAMES
        root=Path('Super_Mario_63_2010/LIBRARY/Course Clips/1 - Bob-omb Battlefield')
        for i in (0,1):
            folder,_,_,_,front=AREAS[i]
            node=ET.parse(root/folder/(front+'.xml')).getroot()
            selected=playback_frame(node,PLAY_FRAMES[i])
            self.assertEqual(selected,i)
            active=[f for f in node.findall('.//x:DOMFrame',NS) if active_frame(f,selected)]
            self.assertTrue(any(f.findall('./x:elements/*',NS) for f in active))

    def test_snowman_grouped_ground(self):
        spans,bounds,objects,exits=collect(Path('Super_Mario_63_2010'),5)
        self.assertGreater(len(spans),1000)
        self.assertGreater(len(objects),60)
        self.assertEqual(len(exits),2)
        self.assertTrue(any(o[2]==17 and o[3]==30 for o in objects))
        self.assertGreater(bounds[2]-bounds[0],2000)

    def test_grouped_collision_transform_applied_once(self):
        import tempfile
        from import_level import read_shapes, rasterize
        xml="""<DOMSymbolItem xmlns="http://ns.adobe.com/xfl/2008/"><timeline><DOMTimeline><layers><DOMLayer><frames><DOMFrame index="0"><elements>
        <DOMGroup><matrix><Matrix tx="10" ty="20"/></matrix><members>
        <DOMShape><matrix><Matrix tx="10" ty="20"/></matrix><fills><FillStyle index="1"/></fills><edges><Edge fillStyle1="1" edges="!0 0|40 0|40 40|0 40|0 0"/></edges></DOMShape>
        </members></DOMGroup></elements></DOMFrame></frames></DOMLayer></layers></DOMTimeline></timeline></DOMSymbolItem>"""
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'ground.xml';path.write_text(xml)
            spans=[span for shape in read_shapes(path) for span in rasterize(shape)]
        self.assertEqual(spans,[(20,10,12),(21,10,12)])

    def test_opening_frames_and_controls_script(self):
        root=Path('Super_Mario_63_2010')
        self.assertEqual(len(collect(root,6)[3]),1)
        self.assertEqual(len(collect(root,7)[3]),2)
        signs=[o[4] for o in collect(root,6)[2] if o[2]==4]
        self.assertIn('@picture:controls',signs)
        self.assertIn('@picture:Start',signs)
        self.assertTrue(any('<br>' in s and 'backflip' in s for s in signs))
