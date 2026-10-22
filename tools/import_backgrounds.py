"""Bake the source SecretLevelBG screen-space gradient."""
from pathlib import Path
import sys
from PIL import Image
from import_signs import Renderer

def generate(root,out):
    r=Renderer(root)
    canvas=Image.new('RGBA',(450,300))
    r.render(canvas,r.symbol('Secret Level BG'),(1,0,0,1,225,150))
    out.parent.mkdir(parents=True,exist_ok=True)
    canvas.save(out)
    snow=Image.new('RGBA',(450,300))
    r.render(snow,r.symbol('2-1 BG'),(1,0,0,1,225,150))
    snow.save(out.parent/'snow-background.png')
    tutorial=Image.new('RGBA',(450,300),(141,201,240,255))
    r.render(tutorial,r.symbol('Castle Colored Far BG'),(1,0,0,1,225,150))
    r.render(tutorial,r.symbol('Castle Colored BG'),(1,0,0,1,225,150))
    tutorial.save(out.parent/'tutorial-background.png')

if __name__=='__main__':generate(Path(sys.argv[1]),Path(sys.argv[2]))
