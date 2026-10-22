"""Import static 1-1 collision fills, rasterized at one world pixel per cell.

XFL edges use twips and signed 24.8 hex coordinates. Quadratics are flattened
to <=0.1 pixel chord error. Unsupported structures fail rather than disappear.
"""
import math
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

NS = {"x": "http://ns.adobe.com/xfl/2008/"}
TOKEN = re.compile(r"#[0-9A-Fa-f]+(?:\.[0-9A-Fa-f]+)?|[-+]?(?:\d+(?:\.\d*)?|\.\d+)|[!|/\[\]]")
AREA = "Course Clips/1 - Bob-omb Battlefield/Areas 1-1 & 1-2"


def coordinate(token):
    if not token.startswith("#"):
        return float(token) / 20
    whole, _, fraction = token[1:].partition(".")
    if len(whole) > 6 or len(fraction) > 2:
        raise ValueError("Invalid 24.8 coordinate: " + token)
    value = int(whole.zfill(6) + fraction.ljust(2, "0"), 16)
    if value & 0x80000000:
        value -= 1 << 32
    return value / 256 / 20


def flatten(start, control, end, segments, depth=0):
    # Distance between quadratic midpoint and chord midpoint, bounded recursively.
    error = math.hypot(start[0] - 2 * control[0] + end[0], start[1] - 2 * control[1] + end[1]) / 4
    if error <= 0.1:
        segments.append((*start, *end))
        return
    if depth >= 20:
        raise ValueError("Curve subdivision limit")
    a = tuple((p + q) / 2 for p, q in zip(start, control))
    b = tuple((p + q) / 2 for p, q in zip(control, end))
    mid = tuple((p + q) / 2 for p, q in zip(a, b))
    flatten(start, a, mid, segments, depth + 1)
    flatten(mid, b, end, segments, depth + 1)


def edges(text):
    if TOKEN.sub("", text).strip():
        raise ValueError("Unsupported XFL edge syntax")
    tokens = TOKEN.findall(text)
    segments, point = [], None
    i = 0
    while i < len(tokens):
        command = tokens[i]
        i += 1
        if command not in ("!", "|", "/", "[", "]"):
            raise ValueError("Missing edge command")
        size = 4 if command in ("[", "]") else 2
        values = [coordinate(token) for token in tokens[i:i + size]]
        if len(values) != size:
            raise ValueError("Truncated edge")
        i += size
        end = tuple(values[-2:])
        if command != "!":
            if point is None:
                raise ValueError("Edge without initial move")
            if size == 4:
                flatten(point, tuple(values[:2]), end, segments)
            else:
                segments.append((*point, *end))
        point = end
    return segments


def read_shapes(path, frame_index=None, allow_script=False):
    root = ET.parse(path).getroot()
    result = []
    from import_mario import matrix,compose
    identity=(1,0,0,1,0,0)
    def visit(shape,parent=identity,group=None):
        kind=shape.tag.split('}')[-1];local=matrix(shape)
        transform=parent if kind=='DOMShape' and local==group else compose(parent,local)
        if kind=='DOMGroup':
            for child in shape.findall('./x:members/*',NS):visit(child,transform,local)
            return
        if kind!='DOMShape':raise ValueError("Unsupported collision element")
        fills=shape.findall('./x:fills/x:FillStyle',NS)
        indices={int(fill.get('index')) for fill in fills}
        a,b,c,d,tx,ty=transform
        for index in indices:
            segments=[]
            for edge in shape.findall('./x:edges/x:Edge',NS):
                f0,f1=int(edge.get('fillStyle0','0')),int(edge.get('fillStyle1','0'))
                if f0 not in indices|{0} or f1 not in indices|{0}:raise ValueError("Unknown collision fill")
                if (f0==index)!=(f1==index):
                    for x,y,u,v in edges(re.sub(r'S\d+','',edge.attrib['edges'])):
                        segments.append((a*x+c*y+tx,b*x+d*y+ty,a*u+c*v+tx,b*u+d*v+ty))
            if segments:result.append(segments)
    for layer in root.findall("./x:timeline/x:DOMTimeline/x:layers/x:DOMLayer",NS):
        frames=layer.findall('./x:frames/x:DOMFrame',NS)
        if frame_index is None and (len(frames)!=1 or frames[0].get('index')!='0'):raise ValueError("Only static collision layers supported: "+str(path))
        frame=frames[0] if frame_index is None else next((f for f in frames if int(f.get('index','0'))<=frame_index<int(f.get('index','0'))+int(f.get('duration','1'))),None)
        if frame is None:continue
        if (not allow_script and frame.find('x:Actionscript',NS) is not None) or frame.get('tweenType'):raise ValueError("Scripted collision layer")
        for shape in frame.find('x:elements',NS):visit(shape)
    return result


def rasterize(segments):
    """Even-odd fill per shape. Independent shapes are unioned by the runtime."""
    ymin = math.floor(min(min(e[1], e[3]) for e in segments))
    ymax = math.ceil(max(max(e[1], e[3]) for e in segments))
    spans = []
    for y in range(ymin, ymax):
        scan = y + 0.5
        crossings = sorted(x1 + (scan - y1) * (x2 - x1) / (y2 - y1)
                           for x1, y1, x2, y2 in segments if (y1 > scan) != (y2 > scan))
        if len(crossings) % 2:
            raise ValueError("Unclosed fill at scanline " + str(y))
        for left, right in zip(crossings[::2], crossings[1::2]):
            x0, x1 = math.ceil(left - 0.5), math.ceil(right - 0.5)
            if x1 > x0:
                spans.append((y, x0, x1))
    return spans


def generate(root, output):
    spans = []
    for filename, flag in (("1-1 Platforms.xml", 1), ("1-1 Bplatforms.xml", 2)):
        for shape in read_shapes(root / "LIBRARY" / AREA / filename):
            spans.extend((*span, flag) for span in rasterize(shape))
    xmin, xmax = min(s[1] for s in spans), max(s[2] for s in spans)
    ymin, ymax = min(s[0] for s in spans), max(s[0] for s in spans) + 1
    document = ET.parse(root / "DOMDocument.xml").getroot()
    scripts = "\n".join(e.text or "" for e in document.findall(".//x:script", NS))
    data = ["// Generated from original 1-1 collision symbols. Do not edit.", "#pragma once", "#include <array>",
            "namespace sm63::level {", "struct Span { int y, x0, x1, flag; };",
            f"inline constexpr int minX = {xmin}, minY = {ymin}, width = {xmax-xmin}, height = {ymax-ymin};"]
    for source, name in (("walkspeed", "walkAcceleration"), ("Mariojumpspeed", "jumpSpeed"),
                         ("airwalkspeed", "airSpeed"), ("airmaxWalkspeed", "airDivisor")):
        found = re.findall(r"_root\." + source + r"\s*=\s*([\d.]+);", scripts)
        if len(found) != 1:
            raise ValueError("Ambiguous player constant: " + source)
        data.append(f"inline constexpr float {name} = {float(found[0])}f;")
    data.append(f"inline constexpr std::array<Span, {len(spans)}> spans = {{{{")
    data.extend("    {" + ", ".join(map(str, span)) + "}," for span in spans)
    data += ["}};", "}", ""]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(data), encoding="utf-8")
    print(f"Imported 1-1: {xmax-xmin} x {ymax-ymin} collision cells, {len(spans)} spans")


if __name__ == "__main__":
    generate(*(Path(arg) for arg in sys.argv[1:]))
