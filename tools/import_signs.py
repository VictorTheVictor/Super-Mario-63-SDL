"""Bake original sign artwork and illustrated cards; source text and vector fills retained."""
from pathlib import Path
import sys,json,re,math,subprocess
import xml.etree.ElementTree as ET
from PIL import Image,ImageDraw,ImageFont
from import_mario import NS,matrix,compose
from import_level import edges,rasterize
from import_art import inverse,point,shape_transform
from import_world import collect, AREAS
I=(1,0,0,1,0,0)
class Renderer:
 def __init__(self,root):
  self.lib=root/'LIBRARY';self.symbols={};self.images={};self.fonts={}
  for p in self.lib.rglob('*.xml'):
   r=ET.parse(p).getroot();self.symbols[r.get('name')]=r
  self.paths={b.get('name'):self.lib/Path(*[s.strip() for s in b.get('href').split('/')]).with_suffix('.png') for b in ET.parse(root/'DOMDocument.xml').findall('.//x:DOMBitmapItem',NS)}
 def symbol(self,short):return next(n for k,n in self.symbols.items() if k and k.split('/')[-1]==short)
 def image(self,key):
  if key not in self.images:self.images[key]=Image.open(self.paths[key]).convert('RGBA')
  return self.images[key]
 def color(self,node):
  color=node.get('color','#000000');return tuple(int(color[i:i+2],16) for i in (1,3,5))+(round(float(node.get('alpha','1'))*255),)
 def font(self,size,bold=False,italic=False):
  key=(size,bold,italic)
  if key not in self.fonts:
   style='Bold Italic' if bold and italic else 'Bold' if bold else 'Italic' if italic else 'Regular'
   path=subprocess.check_output(['fc-match','Arial:style='+style,'-f','%{file}']).decode();self.fonts[key]=ImageFont.truetype(path,max(1,round(size)))
  return self.fonts[key]
 def warp(self,canvas,img,m):
  a,b,c,d,x,y=inverse(m);canvas.alpha_composite(img.transform(canvas.size,Image.Transform.AFFINE,(a,c,x,b,d,y),Image.Resampling.BICUBIC))
 def render(self,canvas,node,m=I,frame=0,clock=0,depth=0):
  if depth>18:raise ValueError('Recursive sign asset')
  layers=node.findall('./x:timeline/x:DOMTimeline/x:layers/x:DOMLayer',NS)
  for layer in reversed(layers):
   frames=layer.findall('./x:frames/x:DOMFrame',NS)
   f=next((f for f in frames if int(f.get('index','0'))<=frame<int(f.get('index','0'))+int(f.get('duration','1'))),None)
   if f is None:continue
   for item in f.findall('./x:elements/*',NS):self.element(canvas,item,m,clock,depth)
 def element(self,canvas,e,parent,clock,depth,group=None):
  kind=e.tag.split('}')[-1];local=matrix(e);m=shape_transform(parent,local,kind,group)
  alpha=e.find('./x:color/x:Color',NS)
  if alpha is not None and float(alpha.get('alphaMultiplier','1'))<1:
   copy=ET.fromstring(ET.tostring(e));copy.remove(copy.find('x:color',NS));layer=Image.new('RGBA',canvas.size)
   self.element(layer,copy,parent,clock,depth,group);layer.putalpha(layer.getchannel('A').point(lambda a:round(a*float(alpha.get('alphaMultiplier','1')))));canvas.alpha_composite(layer);return
  if kind=='DOMGroup':
   for c in e.findall('./x:members/*',NS):self.element(canvas,c,m,clock,depth,local)
  elif kind=='DOMSymbolInstance':
   if e.get('name') in ('box','aa'):return
   node=self.symbols.get(e.get('libraryItemName'))
   if node is None:return
   frames=node.findall('.//x:DOMFrame',NS);length=max((int(f.get('index','0'))+int(f.get('duration','1')) for f in frames),default=1)
   tick=int(e.get('firstFrame','0'))+(0 if e.get('loop')=='single frame' else clock)
   self.render(canvas,node,m,tick%length,clock,depth+1)
  elif kind=='DOMBitmapInstance':self.warp(canvas,self.image(e.get('libraryItemName')),m)
  elif kind in ('DOMStaticText','DOMDynamicText'):
   if kind=='DOMDynamicText':return
   w=max(1,math.ceil(float(e.get('width','400')))+8);h=max(1,math.ceil(float(e.get('height','100')))+8);img=Image.new('RGBA',(w,h));draw=ImageDraw.Draw(img)
   y=0
   for run in e.findall('./x:textRuns/x:DOMTextRun',NS):
    attr=run.find('./x:textAttrs/x:DOMTextAttrs',NS);value=run.findtext('x:characters','',NS).replace('\r','\n');size=float(attr.get('size','16'));font=self.font(size,attr.get('bold')=='true' or 'Bold' in attr.get('face',''),attr.get('italic')=='true')
    color=attr.get('fillColor','#000000');lines=[]
    for paragraph in value.split('\n'):
     line=''
     for word in paragraph.split():
      candidate=(line+' '+word).strip()
      if line and draw.textlength(candidate,font=font)>w-8:lines.append(line);line=word
      else:line=candidate
     lines.append(line)
    for line in lines:
     align=attr.get('alignment','left');xx=(w-8-draw.textlength(line,font=font))/2 if align=='center' else 0
     draw.text((xx,y),line,font=font,fill=color,anchor='lt');y+=size*1.15
   m=compose(m,(1,0,0,1,float(e.get('left','0')),float(e.get('top','0'))));self.warp(canvas,img,m)
  elif kind=='DOMShape':
   for fill in e.findall('./x:fills/x:FillStyle',NS):
    index=fill.get('index');segments=[]
    for edge in e.findall('./x:edges/x:Edge',NS):
     if (edge.get('fillStyle0')==index)!=(edge.get('fillStyle1')==index) and edge.get('edges'):
      segments.extend((*point(m,x0,y0),*point(m,x1,y1)) for x0,y0,x1,y1 in edges(re.sub(r'S\d+','',edge.get('edges'))))
    if not segments:continue
    solid=fill.find('x:SolidColor',NS);bitmap=fill.find('x:BitmapFill',NS);gradient=fill.find('x:LinearGradient',NS)
    radial=fill.find('x:RadialGradient',NS);gradient=radial if radial is not None else gradient
    if solid is not None:color=self.color(solid)
    elif bitmap is not None:
     source=self.image(bitmap.get('bitmapPath'));a,b,c,d,x,y=matrix(bitmap);mapping=inverse(compose(m,(a/20,b/20,c/20,d/20,x,y)))
    elif gradient is not None:
     mapping=inverse(compose(m,matrix(gradient)));stops=[(float(s.get('ratio')),self.color(s)) for s in gradient.findall('x:GradientEntry',NS)]
    else:continue
    layer=Image.new('RGBA',canvas.size);pixels=layer.load()
    for row,left,right in rasterize(segments):
     if not 0<=row<canvas.height:continue
     for col in range(max(0,left),min(canvas.width,right)):
      if bitmap is not None:
       u,v=point(mapping,col+.5,row+.5);u,v=math.floor(u),math.floor(v)
       if bitmap.get('bitmapIsClipped')=='true' and not(0<=u<source.width and 0<=v<source.height):continue
       color=source.getpixel((u%source.width,v%source.height))
      elif gradient is not None:
       u,v=point(mapping,col+.5,row+.5);t=min(1,max(0,math.hypot(u,v)/819.2 if radial is not None else (u/819.2+1)/2));color=stops[-1][1]
       for (a,c0),(b,c1) in zip(stops,stops[1:]):
        if t<=b:
         f=min(1,max(0,(t-a)/max(.0001,b-a)));color=tuple(round(c0[k]+(c1[k]-c0[k])*f) for k in range(4));break
      pixels[col,row]=color
    canvas.alpha_composite(layer)
   draw=ImageDraw.Draw(canvas)
   for style in e.findall('./x:strokes/x:StrokeStyle',NS):
    stroke=style.find('x:SolidStroke',NS)
    if stroke is None:continue
    color=stroke.find('./x:fill/x:SolidColor',NS)
    if color is None:continue
    width=max(1,round(float(stroke.get('weight','1'))*math.hypot(m[0],m[1])))
    for edge in e.findall('./x:edges/x:Edge',NS):
     if edge.get('strokeStyle')==style.get('index') and edge.get('edges'):
      for x,y,u,v in edges(re.sub(r'S\d+','',edge.get('edges'))):draw.line((*point(m,x,y),*point(m,u,v)),fill=self.color(color),width=width)
 def text_card(self,html):
  from html import unescape
  if not hasattr(self,'card_background'):
   self.card_background=Image.new('RGBA',(450,300));self.render(self.card_background,self.symbol('Message Pop-up Clip'),frame=5)
  img=self.card_background.copy()
  # Preserve paragraphs and italic spans from the source HTML.
  tokens=re.split(r'(<[^>]+>|\s+)',unescape(html));lines=[];line=[];width=0;italic=False;font=self.font(17,True)
  for token in tokens:
   if not token:continue
   if token.startswith('<'):
    if token.lower()=='<i>':italic=True
    elif token.lower()=='</i>':italic=False
    elif token.lower() in ('<br>','<br/>','</p>'):lines.append(line);line=[];width=0
    continue
   f=self.font(17,True,italic);tw=f.getlength(token)
   if width+tw>342 and line:lines.append(line);line=[];width=0
   if token.isspace() and not line:continue
   line.append((token,italic,tw));width+=tw
  if line:lines.append(line)
  draw=ImageDraw.Draw(img)
  for row,line in enumerate(lines):
   x=225-sum(t[2] for t in line)/2
   for token,it,w in line:draw.text((x,31+row*21),token,font=self.font(17,True,it),fill='white',anchor='la');x+=w
  return img

def generate(root,out):
 out.mkdir(parents=True,exist_ok=True);r=Renderer(root);cards=[];lines=['#pragma once','#include <vector>','namespace sm63::signdata {','struct Card { const char* key; int frames; };','inline const std::vector<Card> cards={']
 for i in range(len(AREAS)):
  for obj in collect(root,i)[2]:
   if obj[2] in (4,20) and obj[4] not in cards:cards.append(obj[4])
 cards += ["Mario... I remember you... You can't grab me and throw me this time, so I'll destroy you with my bombs!!", "Next...time...I'll...be...stronger..."]
 for total in range(1,65):
  cards.append("You got a Star Coin!<br><br>"+("You have collected your First Star Coin!!" if total==1 else f"You have collected {total} Star Coins."))
 pictures=r.symbol('Message Picture - Pictures');labels={f.get('name'):int(f.get('index')) for f in pictures.findall('.//x:DOMFrame',NS) if f.get('name')}
 for idx,key in enumerate(cards):
  picture=key.startswith('@picture:');name=key[9:];count=32 if picture else 1
  for tick in range(count):
   if picture:
    img=Image.new('RGBA',(450,300));r.render(img,pictures,(1,0,0,1,225,150),labels[name],tick*2)
   else:img=r.text_card(key)
   img.save(out/f'{idx}-{tick}.png')
  lines.append('{'+json.dumps(key)+f',{count}'+'},')
 for short,name,origin,size in [('Coded Sign','sign',(40,90),(80,100)),('Coded Sign Z','prompt',(20,20),(40,40))]:
  img=Image.new('RGBA',size);r.render(img,r.symbol(short),(1,0,0,1,*origin),2 if name=='sign' else 0);img.save(out/(name+'.png'))
 lines+=['};','}'];(out.parent/'signs_data.hpp').write_text('\n'.join(lines))
if __name__=='__main__':generate(Path(sys.argv[1]),Path(sys.argv[2]))
