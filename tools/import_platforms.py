"""Import moving platform parameters and original platform bitmap parts."""
from pathlib import Path
import sys,json,re
import xml.etree.ElementTree as ET
from import_mario import NS,matrix,compose
from import_world import AREAS,course_path,PLAY_FRAMES,active_frame,playback_frame

def generate(root,out):
 lib=root/'LIBRARY';paths={b.get('name'):Path(*[p.strip() for p in b.get('href').split('/')]).with_suffix('.png').as_posix() for b in ET.parse(root/'DOMDocument.xml').findall('.//x:DOMBitmapItem',NS)}
 lines=['#pragma once','#include <vector>','#include <array>','namespace sm63::platformdata {',
 'struct Part { const char* path; float a,b,c,d,x,y; };']
 for track,file in [('green','Green PL Clip'),('log','Tipping Log')]:
  frames={}
  def parts(node,parent,rows):
   m=compose(parent,matrix(node))
   if node.tag.endswith('DOMBitmapInstance'):
    path=paths[node.get('libraryItemName')]
    if not (lib/path).is_file():raise ValueError(path)
    rows.append((path,m))
   for child in node:
    if child.tag.split('}')[-1] not in ('matrix','transformationPoint'):parts(child,m,rows)
  for f in ET.parse(next(lib.rglob(file+'.xml'))).findall('.//x:DOMFrame',NS):
   rows=[]
   for e in f.findall('./x:elements/*',NS):parts(e,(1,0,0,1,0,0),rows)
   if rows:frames[int(f.get('index'))]=rows
  lines.append(f'inline const std::vector<std::vector<Part>> {track} = {{')
  for index,rows in sorted(frames.items()):
   lines.append('{');lines += ['{'+json.dumps(path)+','+','.join(f'{v:.8f}f' for v in m)+'},' for path,m in rows];lines.append('},')
  lines.append('};')
 from import_level import read_shapes
 points=sorted(set((x,y) for shape in read_shapes(next(lib.rglob('Rotating Block Plat Code.xml'))) for x0,y0,x1,y1 in shape for x,y in ((x0,y0),(x1,y1))))
 def cross(o,a,b):return (a[0]-o[0])*(b[1]-o[1])-(a[1]-o[1])*(b[0]-o[0])
 lower=[];upper=[]
 for point in points:
  while len(lower)>1 and cross(lower[-2],lower[-1],point)<=0:lower.pop()
  lower.append(point)
 for point in reversed(points):
  while len(upper)>1 and cross(upper[-2],upper[-1],point)<=0:upper.pop()
  upper.append(point)
 lines.append('inline const std::vector<std::array<float,2>> blockHull={'+','.join('{'+f'{x:.6f}f,{y:.6f}f'+'}' for x,y in lower[:-1]+upper[:-1])+'};')
 lines+=['struct Spec { float x,y; int kind,scale,periodX,periodY,dirX,dirY; float accelX,accelY,speedX,speedY; float radius=0,phase=0,angularSpeed=0,wait=72,style=1,offsetX=0,offsetY=0,reappear=20; };',f'inline const std::array<std::vector<Spec>,{len(AREAS)}> areas = {{{{']
 for index,(folder,_,_,back,front) in enumerate(AREAS):
  rows=[]
  for file in [back,front]:
   if not file:continue
   def visit(node,parent=(1,0,0,1,0,0)):
    if node.tag.endswith('DOMFrame') and not active_frame(node,selected_frame):return
    m=compose(parent,matrix(node));name=node.get('libraryItemName','').split('/')[-1]
    if name in ('Moving Green Platform','Tipping Log','Green Circle Platform','Falling Log Code (graphics)','Rotating Block Plat','Touch-Move Green Platforms'):
     if name!='Rotating Block Plat' and m[:4]!=(1,0,0,1):raise ValueError('Transformed moving platform')
     script='\n'.join(s.text or '' for s in node.findall('.//x:script',NS));kind={'Moving Green Platform':0,'Tipping Log':1,'Green Circle Platform':2,'Falling Log Code (graphics)':3,'Rotating Block Plat':4,'Touch-Move Green Platforms':5}[name]
     def num(key,default):
      a=re.search(r'\b'+key+r'\s*=\s*(-?[\d.]+)(?:\s*/\s*([\d.]+))?',script);return float(a[1])/(float(a[2]) if a[2] else 1) if a else default
     dx=-1 if 'DirectionX = "Left"' in script else 1
     dy=1 if 'DirectionY = "Down"' in script else -1
     vals=[*m[4:],kind,int(num('scale',1 if kind==1 else 2)),int(num('distanceX',25)),int(num('distanceY',0)),dx,dy,num('accelX',1),num('accelY',0),num('speedX',5),num('speedY',0),num('distance',50) if kind==2 else 0,num('angle',0),num('speed',5) if kind==2 else 0]
     if kind==3:
      vals[4]=int(num('fallwait',10));vals[5]=int(num('fallamount',24));vals[9]=num('quakeamount',3)
     if kind==4:
      vals[3]=int(num('scale',80))
      vals[4]=int(num('distanceX',0));vals[5]=int(num('distanceY',0))
      vals[8]=num('accelX',0);vals[9]=num('accelY',0);vals[10]=num('speedX',0);vals[11]=num('speedY',0)
      vals[14]=num('rotspeed',3)*(1 if 'rotdirection = "Right"' in script else -1)
     vals += [-1 if 'rotwait = "none"' in script else num('rotwait',72),num('blocktype',1),num('Xoffstart',0),num('Yoffstart',0)]
     vals += [num('reapearspeed',num('disapearspeed',20))]
     if kind==5:vals[15]=num('disapearcount',32);vals[16]=num('disapearspeed',20)
     rows.append(vals)
    for child in node:
     if child.tag.split('}')[-1] not in ('matrix','transformationPoint'):visit(child,m)
   clip=ET.parse(lib/course_path(index)/folder/(file+'.xml')).getroot()
   selected_frame=playback_frame(clip,PLAY_FRAMES[index]);visit(clip)
  lines.append('{')
  lines += ['{'+','.join(str(v) if 2<=i<=7 else f'{v:.8f}f' for i,v in enumerate(row))+'},' for row in rows]+['},']
 lines+=['}};','}'];out.parent.mkdir(parents=True,exist_ok=True);out.write_text('\n'.join(lines))
if __name__=='__main__':generate(Path(sys.argv[1]),Path(sys.argv[2]))
