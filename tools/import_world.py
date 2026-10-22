"""Static area geometry, supported placements and source exit rectangles."""
from pathlib import Path
import sys,json,re
import xml.etree.ElementTree as ET
from import_level import NS,read_shapes,rasterize
from import_mario import matrix,compose
AREAS=[('Areas 1-1 & 1-2','1-1 Platforms','1-1 Bplatforms','1-1 Back Graphics','1-1 Front Graphics'),
       ('Areas 1-1 & 1-2','1-2 Platforms','1-2 Bplatforms','1-2 Back Graphics','1-2 Font Graphics'),
       ('Areas 1-3 & 1-5','1-3 Platforms','1-3 Bplatfomrs','1-3 Back GFX','1-3 Front GFX'),
       ('Area 1-4','1-4 Ground','1-4 Ground 2','1-4 BackGFX','1-4 Front GFX'),
       ('Areas 1-3 & 1-5','1-5 Ground','1-5 Bplatforms','1-5 Back GFX',None),
       ('','2-1 Ground','2-1 B platforms','2-1 Back GFX','2-1 Front GFX'),
       ('FiRsT Tutoria','8-E1-1 Ground','8-E1-1 Bplatforms','8-E1-1 Back GFX','8-E1-1 Front GFX'),
       ('FiRsT Tutoria','8-E1-1-2 Platforms','8-E1-1-2 BPlatorms','8-E1-1-2 Back GFX','8-E1-1-2 Front GFX')]
# Settled gameplay frames after each area's initialization (zero-based).
PLAY_FRAMES=[2,1,3,2,1,2,2,2]
def course_path(index):return "Course Clips/8 - Storyline Stars/8 - Tutorials" if index>=6 else "Course Clips/2 - Snowman's Land" if index==5 else "Course Clips/1 - Bob-omb Battlefield"
def clean_script(script):
    return re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/',lambda m:m[0] if m[0].startswith(('"',"'")) else '',script,flags=re.S)
def active_frame(node,index):
    return int(node.get('index','0'))<=index<int(node.get('index','0'))+int(node.get('duration','1'))
def playback_frame(node,index):
    frames=node.findall("./x:timeline/x:DOMTimeline/x:layers/x:DOMLayer/x:frames/x:DOMFrame",NS)
    end=max((int(f.get("index","0"))+int(f.get("duration","1")) for f in frames),default=1)
    return min(index,end-1)

def collect(root,index):
    folder,solid,oneway,back,front=AREAS[index];base=root/'LIBRARY'/course_path(index)/folder
    spans=[(*s,flag) for name,flag in [(solid,1),(oneway,2)] for shape in read_shapes(base/(name+'.xml'),frame_index=1 if index==6 and flag==1 else None,allow_script=index==6) for s in rasterize(shape)]
    bounds=(min(s[1] for s in spans),min(s[0] for s in spans),max(s[2] for s in spans),max(s[0] for s in spans)+1)
    objects=[];warps=[]
    def visit(node,parent=(1,0,0,1,0,0)):
        # Retain the frame spanning the default mission's settled timeline.
        if node.tag.endswith('DOMFrame') and not active_frame(node,selected_frame):return
        m=compose(parent,matrix(node));name=node.get('libraryItemName','').split('/')[-1]
        if name in ('Gold Coin Code','Red Coin Code','Breakable Orange Box','Goomba','Coded Sign','Squirt Nozzle Box Clip','Rocket Nozzle Box Clip','Turbo Nozzle Box Clip','Small Water Bottle Clip','Silver Star Clip','Bomb omb Code','Koopa Code','Shy Guy Code','Goonie Code','Shine Sprite Clip','Star Coin Code','Blue Coin Code','Rainbow Star','Heart Clip1','Penguin Code','Toad Main Clip','Cheep Cheep Code'):
            if m[:4]!=(1,0,0,1) and not (name=='Shine Sprite Clip' and all(abs(a-b)<.002 for a,b in zip(m[:4],(1,0,0,1)))):raise ValueError('Scaled object '+name)
            script=clean_script('\n'.join(s.text or '' for s in node.findall('.//x:script',NS)))
            reward=re.search(r'stuffinside\s*=\s*"(\d+)coins?"',script)
            text=re.search(r'signmessage\s*=\s*"((?:\\.|[^"\\])*)"',script)
            kind={'Gold Coin Code':0,'Red Coin Code':1,'Breakable Orange Box':2,'Goomba':3,'Coded Sign':4,'Squirt Nozzle Box Clip':5,'Rocket Nozzle Box Clip':6,'Turbo Nozzle Box Clip':7,'Small Water Bottle Clip':8,'Silver Star Clip':9,'Bomb omb Code':10,'Koopa Code':11,'Shy Guy Code':12,'Goonie Code':13,'Shine Sprite Clip':14,'Star Coin Code':15,'Blue Coin Code':16,'Rainbow Star':17,'Heart Clip1':18,'Penguin Code':19,'Toad Main Clip':20,'Cheep Cheep Code':21}[name]
            label=text[1].replace("\\'", "'") if text else ''
            expression=re.search(r'signmessage\s*=\s*("(?:\\.|[^"\\])*"(?:\s*\+\s*(?:newline|"(?:\\.|[^"\\])*"))*)',script)
            if expression:
                label=''.join('<br>' if part=='newline' else part[1:-1].replace("\\'","'").replace('\\n','<br>') for part in re.findall(r'"(?:\\.|[^"\\])*"|newline',expression[1]))
            if re.search(r'picture\s*=\s*true',script):label='@picture:'+label
            value=int(reward[1]) if reward else 3
            if 10<=kind<=13:
                value=(1 if re.search(r'State\s*=\s*"Flying"',script) else 0) | (2 if re.search(r'_xscale\s*=\s*-100',script) else 0) | (4 if re.search(r'tumble\s*=\s*true',script) else 0)
            if kind==21:value=1 if re.search(r'hungry\s*=\s*false',script) else 0
            if kind==19:
                value=1 if re.search(r'State\s*=\s*"Flying"',script) else 2 if re.search(r'walkspeed\s*=\s*0',script) else 0
            if kind==20:
                message=re.search(r'M1Txt\s*=\s*"((?:\\.|[^"\\])*)"',script)
                label=message[1] if message else 'Hello, I am a Toad.'
            if kind==14:
                value=int(re.search(r'starnum\s*=\s*(\d+)',script)[1])
            if kind==17:
                timer=re.search(r'PowerTimer\s*=\s*32\s*\*\s*(\d+)',script);value=int(timer[1]) if timer else 45
            if kind==15:value=int(re.search(r'StarCoinNum\s*=\s*(\d+)',script)[1])
            objects.append((*m[4:],kind,value,label))
        if node.get('name','').startswith('warp'):
            a,b,c,d,x,y=m
            points=[(a*u+c*v+x,b*u+d*v+y) for u in (-26.5,26.5) for v in (-8,8)]
            warps.append((int(node.get('name')[4:]),min(p[0] for p in points),min(p[1] for p in points),max(p[0] for p in points),max(p[1] for p in points)))
        for child in node:
            if child.tag.split('}')[-1] not in ('matrix','transformationPoint'):visit(child,m)
    for name in (back,front):
        if name:
            clip=ET.parse(base/(name+'.xml')).getroot()
            selected_frame=playback_frame(clip,PLAY_FRAMES[index])
            visit(clip)
    return spans,bounds,objects,warps

def generate(root,out):
    lines=['#pragma once','#include "level.hpp"','#include <vector>','namespace sm63::world {',
           'struct Object { float x,y; int kind,reward; const char* text; };',
           'struct Exit { int number; float left,top,right,bottom; };',
           'struct Water {float left,top,right,bottom;bool cosmetic;};',
           'struct Area { int minX,minY,width,height; std::vector<level::Span> spans; std::vector<Object> objects; std::vector<Exit> exits; std::vector<level::Span> ice; std::vector<Water> water; };',
           'inline const std::vector<Area> areas = {']
    for i in range(len(AREAS)):
        spans,(x,y,r,b),objects,warps=collect(root,i)
        lines += [f'{{{x},{y},{r-x},{b-y},{{']
        lines += ['{'+','.join(map(str,s))+'},' for s in spans]+['},{']
        lines += ['{'+f'{x:.6f}f,{y:.6f}f,{k},{v},'+json.dumps(t)+'},' for x,y,k,v,t in objects]+['},{']
        lines += ['{'+str(num)+','+','.join(f'{v:.6f}f' for v in rect)+'},' for num,*rect in warps]+['},{']
        icefile=root/'LIBRARY'/course_path(i)/'2-1 Ice.xml' if i==5 else None
        if icefile:
            lines += ['{'+f'{y},{x0},{x1},4'+'},' for shape in read_shapes(icefile,allow_script=True) for y,x0,x1 in rasterize(shape)]
        lines += ['},{']
        if i>=6:
            file=next((root/'LIBRARY').rglob(('8-E1-1 Water' if i==6 else '8-E1-1-2 Water')+'.xml'))
            for node in ET.parse(file).findall('.//x:DOMSymbolInstance',NS):
                if not node.get('libraryItemName','').endswith('Water Code'):continue
                script=clean_script('\n'.join(s.text or '' for s in node.findall('.//x:script',NS)))
                def number(key,default):
                    m=re.search(r'\b'+key+r'\s*=\s*([\d.]+)',script);return float(m[1]) if m else default
                length=number('WaterLength',500);polys=number('polygons',50);deep=number('WaterDeepness',400)
                a,b,c,d,x,y=matrix(node)
                rect=(x-50*a,y,x+a*(-50+(length+100)*(polys-1)/polys),y+d*deep)
                lines.append('{'+','.join(f'{v:.5f}f' for v in rect)+','+('true' if re.search(r'graphics\s*=\s*true',script) else 'false')+'},')
        lines += ['}},']
    lines+=['};','}'];out.parent.mkdir(parents=True,exist_ok=True);out.write_text('\n'.join(lines))
if __name__=='__main__':generate(Path(sys.argv[1]),Path(sys.argv[2]))
