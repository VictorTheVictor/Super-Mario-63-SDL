"""Import course interaction placements and bake their original visual symbols."""
from pathlib import Path
import sys,re,json,math
from PIL import Image
from import_signs import Renderer,NS
from import_mario import matrix,compose
from import_world import AREAS,PLAY_FRAMES,active_frame,playback_frame

def generate(root,out):
 out.mkdir(parents=True,exist_ok=True);r=Renderer(root)
 for symbol in r.symbols.values():
  for parent in symbol.iter():
   for child in list(parent):
    if (child.tag.endswith('DOMLayer') and child.get('layerType') in ('mask','guide')) or child.get('name')=='body':parent.remove(child)
 paths=["Bowling Ball Animation","1-Bowling Ball Animation 2","1- Bowling Ball 3"]
 tracks=[('cannon_hole','Cannon (main) Clip',2),('cannon_barrel','Cannon Clip',0),('sling','Sling Star BackGFX',2),('falling_log','Falling Log Code (graphics)',0),('ball','Bowling Ball',0),('star_coin','Star Coin Code',0),('rainbow','Rainbow Star',0),('wing','Wing Clip',0),('wing_flap','Wing Clip',3),('heart','Heart Clip1',0),('toad','Toad - Idle Body',0),('shock_front','Ground Shockwaves Front GFX',64),('shock_back','Ground Shockwaves Back Graphics',64)]+[(f'block_{i}','Rotating Block Plat',i-1) for i in (1,2,8,21)]
 lines=['#pragma once','#include <array>','#include <vector>','namespace sm63::objectdata {',
        'struct Spec {float x,y; int kind; float a,b,c,d,e,f;};',
        'inline const std::array<std::vector<Spec>,5> areas={{{']
 # C++ std::array initializer has two outer braces.
 lines[-1]=f'inline const std::array<std::vector<Spec>,{len(AREAS)}> areas={{{{' 
 for index,(folder,_,_,back,front) in enumerate(AREAS):
  rows=[]
  for file in (back,front):
   if not file:continue
   node=r.symbol(file);selected=playback_frame(node,PLAY_FRAMES[index])
   def visit(n,parent=(1,0,0,1,0,0)):
    if n.tag.endswith('DOMFrame') and not active_frame(n,selected):return
    m=compose(parent,matrix(n));name=n.get('libraryItemName','').split('/')[-1]
    script='\n'.join(s.text or '' for s in n.findall('.//x:script',NS))
    def num(key,default):
     match=re.search(r'\b'+key+r'\s*=\s*(-?[\d.]+)',script);return float(match[1]) if match else default
    values=None
    if name=='Cannon (main) Clip':
     side=-1 if 'side = "Left"' in script else 0 if 'side = "Both"' in script else 1
     values=[0,num('Xpower',35),num('Ypower',25),side,0,0,0]
    if name=='Sling Star BackGFX':
     values=[1,num('xblast',0),num('yblast',-25),math.degrees(math.atan2(m[1],m[0])),math.hypot(m[0],m[1]),0,0]
    if name=='King Bomb-omb Code':values=[3,0,0,0,0,0,0]
    if name in paths:
     values=[2,paths.index(name),0,math.hypot(m[0],m[1]),0,0,0]
    if values is not None:rows.append([*m[4:],*values])
    for child in n:
     if child.tag.split('}')[-1] not in ('matrix','transformationPoint'):visit(child,m)
   visit(node)
  lines.append('{')
  for row in rows:lines.append('{'+','.join(str(int(v)) if i==2 else f'{v:.8f}f' for i,v in enumerate(row))+'},')
  lines.append('},')
 lines+=['}};','struct BallPose {float x,y,scale;bool hurts;};','inline const std::array<std::vector<BallPose>,3> paths={{']
 for name in paths:
  node=r.symbol(name);frames=[]
  for f in node.findall('.//x:DOMFrame',NS):
   e=f.find('./x:elements/x:DOMSymbolInstance',NS)
   if e is not None:frames.append((f,e))
  frames.sort(key=lambda pair:int(pair[0].get('index')))
  length=max(int(f.get('index'))+int(f.get('duration','1')) for f,e in frames)
  lines.append('{')
  for tick in range(length):
   i=max(i for i,(f,e) in enumerate(frames) if int(f.get('index'))<=tick)
   f,e=frames[i];m=matrix(e)
   if f.get('tweenType')=='motion' and i+1<len(frames):
    nextf,nexte=frames[i+1];t=(tick-int(f.get('index')))/(int(nextf.get('index'))-int(f.get('index')))
    m=tuple(a+(b-a)*t for a,b in zip(m,matrix(nexte)))
   hurts=e.get('libraryItemName','').endswith('/Bowling Ball')
   lines.append('{'+f'{m[4]:.5f}f,{m[5]:.5f}f,{m[0]/.375:.5f}f,'+('true' if hurts else 'false')+'},')
  lines.append('},')
 lines+=['}};','struct Art {const char* name;};',f'inline constexpr std::array<Art,{len(tracks)}> art={{{{']
 for key,symbol,frame in tracks:
  if key.startswith('shock_'):
   node=r.symbol(symbol)
   for color in node.findall('.//x:SolidColor',NS):color.set('alpha','1')
   canvas=Image.new('RGBA',(1024,64));r.render(canvas,node,(1,0,0,1,512,32),frame,0)
  else:
   canvas=Image.new('RGBA',(192,192));r.render(canvas,r.symbol(symbol),(.375,0,0,.375,96,128) if key=='ball' else (1,0,0,1,96,128),frame,0)
  canvas.save(out/(key+'.png'))
  lines.append('{'+json.dumps(key)+'},')
 lines+=['}};','struct BossTrack {const char* name;int length;};',
          'inline constexpr std::array<BossTrack,6> bossTracks={{{"idle",1},{"walk",24},{"throw",45},{"jump",14},{"grab",85},{"hurt",67}}};']
 parent=r.symbol('King Bomb-omb Clip')
 for key,index,length in [('idle',0,1),('walk',1,24),('throw',2,45),('jump',5,14),('grab',3,85),('hurt',6,67)]:
  for tick in range(length):
   canvas=Image.new('RGBA',(192,192));r.render(canvas,parent,(1,0,0,1,87,98.05),index,tick);canvas.save(out/f'boss_{key}_{tick}.png')
 lines+=['}'];(out.parent/'objects_data.hpp').write_text('\n'.join(lines))
if __name__=='__main__':generate(Path(sys.argv[1]),Path(sys.argv[2]))
