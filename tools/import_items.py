"""Import area 1-1 coin/box placements, rewards and original bitmap tracks."""
from pathlib import Path
import sys, json, re
import xml.etree.ElementTree as ET
from import_mario import NS, matrix, compose

def generate(root, output):
    lib=root/'LIBRARY'
    paths={b.get('name'):Path(*[p.strip() for p in b.get('href').split('/')]).with_suffix('.png').as_posix()
           for b in ET.parse(root/'DOMDocument.xml').findall('.//x:DOMBitmapItem',NS)}
    lines=['#pragma once','#include <array>','namespace sm63::itemdata {',
           'struct Sprite { const char* path; int start; float a,b,c,d,x,y; };']
    for name,file in [('gold','Gold Coin Animation'),('red','Red Coin Animation'),('blue','Blue Coin Clip')]:
        rows=[]
        for f in ET.parse(next(lib.rglob(file+'.xml'))).findall('.//x:DOMFrame',NS):
            for b in f.findall('./x:elements/x:DOMBitmapInstance',NS):
                path=paths[b.get('libraryItemName')]
                if not (lib/path).is_file(): raise ValueError(path)
                rows.append((int(f.get('index')),path,matrix(b)))
        rows.sort()
        lines.append(f'inline constexpr std::array<Sprite,{len(rows)}> {name} = {{{{')
        lines += ['{'+json.dumps(p)+f',{i},'+','.join(f'{v:.8f}f' for v in m)+'},' for i,p,m in rows]
        lines += ['}};']
    brick=next(v for k,v in paths.items() if k.endswith('/Switch Brick'))
    lines += ['inline constexpr Sprite box={'+json.dumps(brick)+',0,1,0,0,1,-16,-32};']
    coins=[];boxes=[]
    def visit(node,parent=(1,0,0,1,0,0)):
        m=compose(parent,matrix(node)); name=node.get('libraryItemName','').split('/')[-1]
        if name in ('Gold Coin Code','Red Coin Code','Breakable Orange Box'):
            if m[:4]!=(1,0,0,1): raise ValueError('Unsupported item placement transform')
            if name=='Breakable Orange Box':
                script='\n'.join(s.text or '' for s in node.findall('.//x:script',NS))
                reward=re.search(r'stuffinside\s*=\s*"(\d+)coins?"',script)
                boxes.append((*m[4:],int(reward[1]) if reward else 3))
            else: coins.append((*m[4:],2 if name=='Red Coin Code' else 1))
        for c in node:
            if c.tag.split('}')[-1] not in ('matrix','transformationPoint'):visit(c,m)
    area=lib/'Course Clips/1 - Bob-omb Battlefield/Areas 1-1 & 1-2'
    for file in ['1-1 Back Graphics.xml','1-1 Front Graphics.xml']:visit(ET.parse(area/file).getroot())
    lines += ['struct Spawn { float x,y; int value; };']
    for name,rows in [('coins',coins),('boxes',boxes)]:
        lines += [f'inline constexpr std::array<Spawn,{len(rows)}> {name} = {{{{']
        lines += [f'{{{x:.8f}f,{y:.8f}f,{v}}},' for x,y,v in rows]+['}};']
    lines+=['}'];output.parent.mkdir(parents=True,exist_ok=True);output.write_text('\n'.join(lines)+'\n')
    print(f'Imported {len(coins)} coins and {len(boxes)} boxes')
if __name__=='__main__':generate(*(Path(a) for a in sys.argv[1:]))
