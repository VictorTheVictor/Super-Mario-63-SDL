"""Import area 1-1 Goomba placements and original bitmap timelines."""
from pathlib import Path
import sys, json
import xml.etree.ElementTree as ET
from import_mario import NS, matrix, compose


def generate(root, output):
    library = root / 'LIBRARY'
    directory = library / 'Enemies/Normal/G-Z/Goomba'
    paths = {b.attrib['name']: Path(*[p.strip() for p in b.attrib['href'].split('/')]).with_suffix('.png').as_posix()
             for b in ET.parse(root / 'DOMDocument.xml').findall('.//x:DOMBitmapItem', NS)}
    lines = ['#pragma once', '#include <array>', 'namespace sm63::enemydata {',
             'struct Sprite { const char* path; int start; float a,b,c,d,x,y; };']
    for name, filename, parent in [('walk','Goomba Walking',(1,0,0,1,0,-8)),
                                    ('squish','Goomba Squished',(1,0,0,1,.35,-2.9))]:
        rows = []
        for f in ET.parse(directory / (filename+'.xml')).findall('.//x:DOMFrame', NS):
            for b in f.findall('./x:elements/x:DOMBitmapInstance', NS):
                path = paths[b.attrib['libraryItemName']]
                if not (library/path).is_file(): raise ValueError(path)
                rows.append((int(f.attrib['index']),path,compose(parent,matrix(b))))
        rows.sort()
        lines.append(f'inline constexpr std::array<Sprite,{len(rows)}> {name} = {{{{')
        for start,path,m in rows:
            lines.append('{'+json.dumps(path)+f', {start}, '+', '.join(f'{v:.8f}f' for v in m)+'},')
        lines.append('}};')
    area = library / 'Course Clips/1 - Bob-omb Battlefield/Areas 1-1 & 1-2/1-1 Back Graphics.xml'
    spawns=[]
    def visit(node, parent=(1,0,0,1,0,0)):
        transform=compose(parent,matrix(node))
        if node.tag.endswith('DOMSymbolInstance') and '/'.join(p.strip() for p in node.get('libraryItemName','').split('/')).endswith('/Goomba/Goomba'):
            if transform[:4] != (1,0,0,1): raise ValueError('Unsupported scaled Goomba placement')
            spawns.append(transform[4:])
        for child in node:
            if child.tag.split('}')[-1] not in ('matrix','transformationPoint'): visit(child,transform)
    visit(ET.parse(area).getroot())
    if len(spawns)!=6: raise ValueError('Unexpected area 1-1 Goomba count')
    lines += ['struct Spawn { float x,y; };', f'inline constexpr std::array<Spawn,{len(spawns)}> spawns = {{{{']
    lines += [f'{{{x:.8f}f,{y:.8f}f}},' for x,y in spawns]
    lines += ['}};', '}']
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text('\n'.join(lines)+'\n')

if __name__ == '__main__': generate(*(Path(x) for x in sys.argv[1:]))
