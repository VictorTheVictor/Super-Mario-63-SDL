"""Bake area 1-1 static bitmap/filled-shape layers from XFL into RGBA PNGs.

Interactive symbols are excluded and enumerated in art-report.json. No scripts
are executed. Pillow is only required on the build host, never at runtime.
"""
import json
import math
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET
from PIL import Image
from import_level import NS, AREA, edges, rasterize
from import_mario import matrix, compose

IDENTITY = (1, 0, 0, 1, 0, 0)
ORIGIN = (-300, -400)
SIZE = (2650, 1000)


def point(m, x, y):
    a, b, c, d, tx, ty = m
    return a*x+c*y+tx, b*x+d*y+ty


def inverse(m):
    a, b, c, d, x, y = m
    det = a*d-b*c
    if abs(det) < 1e-10:
        raise ValueError("Singular graphics matrix")
    return d/det, -b/det, -c/det, a/det, (c*y-d*x)/det, (b*x-a*y)/det


def shape_transform(parent, local, kind, group_matrix=None):
    # Raw shape matrices in these XFL groups repeat the group's transform.
    return parent if kind == 'DOMShape' and local == group_matrix else compose(parent, local)


def hidden_instance(item):
    script='\n'.join(s.text or '' for s in item.findall('./x:Actionscript/x:script',NS))
    return bool(re.search(r'\b_visible\s*=\s*false\b',script))


def apply_color(image, color):
    """Apply XFL instance color to the composited symbol, including bitmap edges."""
    channels = []
    brightness = float(color.get('brightness', '0'))
    for channel, name in zip(image.split(), ('red', 'green', 'blue', 'alpha')):
        multiplier = float(color.get(name+'Multiplier', '1'))
        offset = float(color.get(name+'Offset', '0'))
        if name != 'alpha' and brightness:
            multiplier *= 1-abs(brightness)
            offset += max(0, brightness)*255
        channels.append(channel.point([max(0, min(255, round(v*multiplier+offset))) for v in range(256)]))
    return Image.merge('RGBA', channels)


def generate(root, output, area_index=0):
    global ORIGIN, SIZE
    from import_world import AREAS, course_path, collect, PLAY_FRAMES, active_frame, playback_frame
    folder, _, _, back_name, front_name = AREAS[area_index]
    if area_index:
        _, (left, top, right, bottom), _, _ = collect(root, area_index)
        ORIGIN=(left-32,top-32);SIZE=(right-left+64,bottom-top+128)
    output.mkdir(parents=True, exist_ok=True)
    library = root / "LIBRARY"
    document = ET.parse(root / "DOMDocument.xml").getroot()
    bitmaps = {}
    for bitmap in document.findall(".//x:DOMBitmapItem", NS):
        path = Path(*[part.strip() for part in bitmap.attrib["href"].split("/")]).with_suffix(".png")
        bitmaps[bitmap.attrib["name"]] = library / path
    symbols = {}
    # Only explicitly static course decoration symbols are eligible for recursion.
    for path in (library / course_path(area_index)).rglob("*.xml"):
        node = ET.parse(path).getroot()
        symbols[node.get("name")] = node
    platform=ET.parse(library/'Course Items-PL/In-course Items/Transportation/Cannon/Cannon Platform.xml').getroot()
    symbols[platform.get('name')]=platform
    images, skipped = {}, set()

    def image(name):
        if name not in images:
            images[name] = Image.open(bitmaps[name]).convert("RGBA")
        return images[name]

    def render_symbol(canvas, node, transform, depth=0):
        if depth > 16:
            raise ValueError("Recursive graphics symbol")
        layers = node.findall("./x:timeline/x:DOMTimeline/x:layers/x:DOMLayer", NS)
        selected_frame=playback_frame(node, 1 if area_index>=6 and "Color Front GFX" in node.get("name", "") else PLAY_FRAMES[area_index] if depth==0 else 0)
        for layer in reversed(layers):
            frame = next((f for f in layer.findall("./x:frames/x:DOMFrame", NS)
                          if active_frame(f, selected_frame)), None)
            if frame is not None:
                for item in frame.findall("./x:elements/*", NS):
                    render(canvas, item, transform, depth)

    def render(canvas, item, parent, depth, group_matrix=None, colored=False):
        color = item.find('./x:color/x:Color', NS)
        if color is not None and not colored:
            layer = Image.new('RGBA', SIZE)
            render(layer, item, parent, depth, group_matrix, True)
            canvas.alpha_composite(apply_color(layer, color))
            return
        kind = item.tag.split("}")[-1]
        local=matrix(item)
        # XFL repeats a group transform on its raw shape members. Apply it once.
        transform = shape_transform(parent,local,kind,group_matrix)
        if kind == "DOMGroup":
            for child in item.findall("./x:members/*", NS):
                render(canvas, child, transform, depth, local)
        elif kind == "DOMSymbolInstance":
            if hidden_instance(item):return
            name = item.attrib["libraryItemName"]
            target = symbols.get(name)
            if target is not None and (target.find(".//x:script", NS) is None or (area_index>=6 and "Color" in name)):
                render_symbol(canvas, target, transform, depth+1)
            else:
                skipped.add(name)
        elif kind == "DOMBitmapInstance":
            bitmap = image(item.attrib["libraryItemName"])
            a, b, c, d, x, y = inverse(transform)
            ox, oy = ORIGIN
            warped = bitmap.transform(SIZE, Image.Transform.AFFINE,
                                      (a, c, a*ox+c*oy+x, b, d, b*ox+d*oy+y),
                                      Image.Resampling.NEAREST)
            canvas.alpha_composite(warped)
        elif kind == "DOMShape":
            for fill in item.findall("./x:fills/x:FillStyle", NS):
                index = fill.attrib["index"]
                segments = []
                for edge in item.findall("./x:edges/x:Edge", NS):
                    if (edge.get("fillStyle0") == index) != (edge.get("fillStyle1") == index):
                        # Selection flags are authoring metadata, not geometry.
                        for x0, y0, x1, y1 in edges(re.sub(r"S\d+", "", edge.attrib["edges"])):
                            segments.append((*point(transform, x0, y0), *point(transform, x1, y1)))
                if not segments:
                    continue
                color = fill.find("x:SolidColor", NS)
                bitmap_fill = fill.find("x:BitmapFill", NS)
                radial=fill.find('x:RadialGradient',NS)
                gradient=radial if radial is not None else fill.find('x:LinearGradient',NS)
                if color is not None:
                    rgb = color.get("color", "#000000")
                    rgba = tuple(int(rgb[i:i+2], 16) for i in (1,3,5)) + (round(float(color.get("alpha", "1"))*255),)
                elif bitmap_fill is not None:
                    bitmap = image(bitmap_fill.attrib["bitmapPath"])
                    a, b, c, d, x, y = matrix(bitmap_fill)
                    # Bitmap-fill scale coefficients are in twips; translations are pixels.
                    mapping = inverse(compose(transform, (a/20, b/20, c/20, d/20, x, y)))
                    pixels = bitmap.load()
                elif gradient is not None:
                    mapping=inverse(compose(transform,matrix(gradient)))
                    stops=[]
                    for stop in gradient.findall('x:GradientEntry',NS):
                        rgb=stop.get('color','#000000')
                        stops.append((float(stop.get('ratio')),tuple(int(rgb[i:i+2],16) for i in (1,3,5))+(round(float(stop.get('alpha','1'))*255),)))
                else:
                    raise ValueError("Unsupported fill in static area graphics")
                layer = Image.new("RGBA", SIZE)
                pixels_out = layer.load()
                for row, left, right in rasterize(segments):
                    dy = row - ORIGIN[1]
                    if not 0 <= dy < SIZE[1]:
                        continue
                    for col in range(max(left, ORIGIN[0]), min(right, ORIGIN[0]+SIZE[0])):
                        if bitmap_fill is not None:
                            u, v = point(mapping, col+0.5, row+0.5)
                            u, v = math.floor(u), math.floor(v)
                            if bitmap_fill.get("bitmapIsClipped") == "true" and not (0 <= u < bitmap.width and 0 <= v < bitmap.height):
                                continue
                            rgba = pixels[u % bitmap.width, v % bitmap.height]
                        elif gradient is not None:
                            u,v=point(mapping,col+.5,row+.5)
                            t=min(1,max(0,math.hypot(u,v)/819.2 if radial is not None else (u/819.2+1)/2))
                            rgba=stops[-1][1]
                            for (a,c0),(b,c1) in zip(stops,stops[1:]):
                                if t<=b:
                                    f=min(1,max(0,(t-a)/max(.0001,b-a)))
                                    rgba=tuple(round(c0[k]+(c1[k]-c0[k])*f) for k in range(4));break
                        pixels_out[col-ORIGIN[0], dy] = rgba
                canvas.alpha_composite(layer)
            if item.find("x:strokes", NS) is not None:
                skipped.add("Vector strokes (fill geometry retained)")
        else:
            raise ValueError("Unsupported static graphics element: " + kind)

    for name, filename in (("back", back_name), ("front", front_name)):
        canvas = Image.new("RGBA", SIZE)
        if filename:
            render_symbol(canvas, ET.parse(library / course_path(area_index) / folder / (filename+".xml")).getroot(), IDENTITY)
        canvas.save(output / (name + ".png"))
    (output / "art-report.json").write_text(json.dumps({"omitted": sorted(skipped), "canvas_origin": ORIGIN, "canvas_size": SIZE}, indent=2))
    (output / "art.hpp").write_text('#pragma once\nnamespace sm63::art {\n' +
        'inline constexpr const char* defaultRoot = ' + json.dumps(str(output.resolve())) + ';\n' +
        f'inline constexpr int x = {ORIGIN[0]}, y = {ORIGIN[1]}, width = {SIZE[0]}, height = {SIZE[1]};\n' +
        '}\n')
    print(f"Baked original static area art; {len(skipped)} omitted symbol/stroke categories recorded")


if __name__ == "__main__":
    generate(Path(sys.argv[1]), Path(sys.argv[2]), int(sys.argv[3]) if len(sys.argv)>3 else 0)
