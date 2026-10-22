"""Resolve original HUD, nozzle-box and backpack bitmaps without runtime XML."""
from pathlib import Path
import sys,json
import xml.etree.ElementTree as ET
from import_mario import NS

def generate(root,out):
    paths={b.get('name').split('/')[-1]:Path(*[p.strip() for p in b.get('href').split('/')]).with_suffix('.png').as_posix() for b in ET.parse(root/'DOMDocument.xml').findall('.//x:DOMBitmapItem',NS)}
    groups={'hp':['HP '+str(i) for i in range(9)],'boxes':['Squirt Nozzle Box','Rocket Nozzle Box','Turbo Nozzle Box'],
            'nozzles':['Hover Nozzle','Rocket Nozzle','Turbo Nozzle'],
            'pack':['Fludd Base','Fludd Side - Hover','Fludd Back - Rocket','Fludd Back - Turbo'],
            'icons':['Mario Lives','Silver Star - 1']}
    lines=['#pragma once','#include <array>','namespace sm63::huddata {']
    for group,names in groups.items():
        for name in names:
            if not (root/'LIBRARY'/paths[name]).is_file():raise ValueError(name)
        lines.append(f'inline constexpr std::array<const char*,{len(names)}> {group} = '+'{'+','.join(json.dumps(paths[n]) for n in names)+'};')
    lines+=['}'];out.parent.mkdir(parents=True,exist_ok=True);out.write_text('\n'.join(lines))
if __name__=='__main__':generate(Path(sys.argv[1]),Path(sys.argv[2]))
