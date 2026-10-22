"""Bake original enemy visual timelines, excluding scripted collision helpers."""
from pathlib import Path
import sys
from PIL import Image
from import_signs import Renderer,NS
TRACKS=[('cheep_swim','Cheep Cheep Swim Animation',0,20),('penguin_idle','Penguin Idle',-10,1),('penguin_walk','Pengion Walk Animation',-10,16),('penguin_fly_down','Penguin Fly 1 Animation',-11,16),('penguin_fly_up','Penguin Fly 2 Animation',-9,16),('penguin_tumble','Penguin Tumble',-9,12),('bobomb_walk','Bomb omb Walking',-6,30),('bobomb_tumble','Bomb omb tumbling',-4,12),
 ('bobomb_explode','Bomb omb Explosion',-10,30),('koopa_walk','Koopa Walk Animation',-15,24),
 ('koopa_fly','Flying Koopa Animation',-19,24),('koopa_shell','Shell Spin',-7,8),
 ('shy_walk','Shy Guy Walking',-10,12),('shy_fly','ShyGuy Flying',-7,24),
 ('shy_tumble','Shy Guy Tumbling',-10,12),('shy_squish','Shy Guy Squish',-1,12),
 ('goonie_fly','Goonie Flying',0,18),('goonie_ride','Goonie Flying (Riding)',0,18),('goonie_dive','Goonie Diving',8,18)]
def generate(root,out):
 out.mkdir(parents=True,exist_ok=True);r=Renderer(root)
 lines=['#pragma once','#include <array>','namespace sm63::enemyart {','struct Track {const char* name; int length;};',f'inline constexpr std::array<Track,{len(TRACKS)}> tracks={{{{']
 for name,symbol,y,length in TRACKS:
  node=r.symbol(symbol);end=max(int(f.get('index','0'))+int(f.get('duration','1')) for f in node.findall('.//x:DOMFrame',NS))
  for tick in range(length):
   img=Image.new('RGBA',(192,192));r.render(img,node,(1,0,0,1,96,128+y),tick%end,tick);img.save(out/f'{name}-{tick}.png')
  lines.append('{"'+name+'",'+str(length)+'},')
 lines+=['}};','}'];(out.parent/'enemy_art.hpp').write_text('\n'.join(lines))
if __name__=='__main__':generate(Path(sys.argv[1]),Path(sys.argv[2]))
