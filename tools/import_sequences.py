"""Bake source Shine pickup poses and death frames; runtime owns script timing."""
from pathlib import Path
import sys
from PIL import Image
from import_signs import Renderer, NS
from import_mario import matrix, compose

class SequenceRenderer(Renderer):
    nozzle = 0
    def render(self, canvas, node, m=(1,0,0,1,0,0), frame=0, clock=0, depth=0):
        if node.get('name','').endswith('/Fludd Front Code'):
            frame = (3,0,1,2)[self.nozzle]
        return super().render(canvas,node,m,frame,clock,depth)

def generate(root, out):
    out.mkdir(parents=True, exist_ok=True)
    r = SequenceRenderer(root)
    main = r.symbol('Mario Main')
    tracks = []
    for name, symbol, parent_frame in [
        ('star_none', 'Mario (None) Star', 11),
        ('star_hover', 'Mario Hover Star', 33),
        ('star_rocket', 'Mario Shine Sprite (Rocket)', 59),
        ('star_turbo', 'Mario Shine Sprite (Turbo)', 46),
        ('death_none', 'Mario Fiant GFX Clip', None),
        ('death_hover', 'Mario Fiant GFX Clip', None),
        ('death_rocket', 'Mario Fiant GFX Clip', None),
        ('death_turbo', 'Mario Fiant GFX Clip', None),
        ('shine', 'Shine Sprite Clip', None),
    ]:
        node = r.symbol(symbol)
        parent = (1,0,0,1,0,0)
        if parent_frame is not None:
            parent = next(matrix(e) for f in main.findall('.//x:DOMFrame',NS)
                          if int(f.get('index')) == parent_frame
                          for e in f.findall('./x:elements/x:DOMSymbolInstance',NS)
                          if e.get('libraryItemName','').endswith('/'+symbol))
        r.nozzle = ['none','hover','rocket','turbo'].index(name.split('_')[1]) if name.startswith('death_') else 0
        length = 60 if name.startswith('death_') else 24 if name == 'shine' else 3
        for tick in range(length):
            canvas = Image.new('RGBA',(192,192))
            r.render(canvas,node,compose((1,0,0,1,96,128),parent),
                     0 if name=='shine' else tick, max(0,tick-16) if name.startswith('death_') else tick)
            canvas.save(out/f'{name}-{tick}.png')
        tracks.append((name,length))
    lines=['#pragma once','#include <array>','namespace sm63::sequenceart {',
           'struct Track {const char* name; int length;};',
           f'inline constexpr std::array<Track,{len(tracks)}> tracks={{{{']
    lines += ['{"'+n+'",'+str(l)+'},' for n,l in tracks]
    lines += ['}};','}']
    (out.parent/'sequence_art.hpp').write_text('\n'.join(lines))
if __name__=='__main__': generate(Path(sys.argv[1]),Path(sys.argv[2]))
