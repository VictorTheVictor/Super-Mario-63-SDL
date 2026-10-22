"""Extract no-powerup Mario bitmap tracks and their Mario Main transforms.

Wing attachment matrices are imported alongside bitmap tracks. FLUDD overlays
are rendered separately. Script behavior is implemented in
the native controller; this tool does not execute ActionScript or tween curves.
"""
import json
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

NS = {"x": "http://ns.adobe.com/xfl/2008/"}


def matrix(element):
    node = element.find("./x:matrix/x:Matrix", NS)
    a = {} if node is None else node.attrib
    return tuple(float(a.get(k, v)) for k, v in zip(("a", "b", "c", "d", "tx", "ty"), (1, 0, 0, 1, 0, 0)))


def compose(p, q):
    a, b, c, d, x, y = p
    e, f, g, h, u, v = q
    return (a*e+c*f, b*e+d*f, a*g+c*h, b*g+d*h, a*u+c*v+x, b*u+d*v+y)


def generate(root, output):
    directory = root / "LIBRARY/Char/Mario CLIPS"
    document = ET.parse(root / "DOMDocument.xml").getroot()
    paths = {}
    for bitmap in document.findall(".//x:DOMBitmapItem", NS):
        path = Path(*[part.strip() for part in bitmap.attrib["href"].split("/")]).with_suffix(".png")
        paths[bitmap.attrib["name"]] = path.as_posix()
    main = ET.parse(directory / "Mario Main.xml").getroot()
    parents = {}
    for frame in main.findall(".//x:DOMFrame", NS):
        for symbol in frame.findall("./x:elements/x:DOMSymbolInstance", NS):
            if frame.get("name") in ("diveflip", "backflip") or symbol.get("name") == "frame":
                parents[int(frame.attrib["index"])] = matrix(symbol)
    lines = ["// Generated from Mario's original bitmap tracks.", "#pragma once", "#include <array>",
             "namespace sm63::mario {",
             "struct Wing {float a,b,c,d,x,y;};",
             "struct Sprite { const char* path; int start, duration; float a,b,c,d,x,y; int wingCount=0; std::array<Wing,2> wings{}; };"]
    for name, filename, main_frame, limit in [
        ("hover_dive", "Mario Slide (Hover)", 25, 8),
        ("rocket_dive", "Mario Rocket Slide", 51, 10),
        ("turbo_dive", "Mario Slide (Turbo)", 38, 10),
        ("hover_diveflip", "Mario (hover) Diveflip", 26, 9),
        ("hover_backflip", "Mario (hover) backflip", 27, 22),
        ("rocket_diveflip", "Mario (rocket) Diveflip", 52, 9),
        ("rocket_backflip", "Mario (Rocket) backflip", 53, 22),
        ("turbo_diveflip", "Mario (turbo) Diveflip", 39, 9),
        ("turbo_backflip", "Mario (turbo) backflip", 40, 22),
        ("swim", "Mario Swimming Up", 10, 24),
        ("flying", "Mario Flying", 71, 3),
        ("walk", "Mario (None) Walk", 1, 80),
        ("spin", "Mario (none) Spin Attack", 8, 79),
        ("run", "Mario Running", 66, 87),
        ("rise", "Mario (None) Air - 2", 2, 1),
        ("fall", "Mario (None) Air - 1", 3, 1),
        ("flip", "Mario (none) Frontflip", 7, 1),
        ("pound", "Mario Ground Pound", 67, 43),
        ("dive", "Mario (None) Dive", 4, 10),
        ("diveflip", "Mario Diveflip", 5, 9),
        ("backflip", "Mario Backflip", 6, 22),
    ]:
        symbol = ET.parse(next((root/"LIBRARY").rglob(filename+".xml")) if name=="flying" else directory / (filename + ".xml")).getroot()
        rows = []
        for frame in symbol.findall(".//x:DOMFrame", NS):
            start = int(frame.attrib["index"])
            if start >= limit:
                continue
            bitmaps = [(bitmap, matrix(bitmap)) for bitmap in frame.findall("./x:elements/x:DOMBitmapInstance", NS)]
            if name == "pound":
                for child in frame.findall("./x:elements/x:DOMSymbolInstance", NS):
                    if child.get("libraryItemName", "").endswith("/Mario Ground Pound Blur"):
                        blur = ET.parse(directory / "Mario Ground Pound Blur.xml").getroot()
                        for bitmap in blur.findall(".//x:DOMBitmapInstance", NS):
                            bitmaps.append((bitmap, compose(matrix(child), matrix(bitmap))))
            for bitmap, local in bitmaps:
                if name not in ("flip", "pound", "diveflip", "backflip", "hover_dive", "rocket_dive", "turbo_dive") and not name.endswith(("diveflip", "backflip")) and frame.get("tweenType"):
                    raise ValueError("Unexpected tween in " + filename)
                path = paths[bitmap.attrib["libraryItemName"]]
                if not (root / "LIBRARY" / path).is_file():
                    raise ValueError("Missing Mario bitmap: " + path)
                parent=parents.get(main_frame)
                if parent is None:
                    parent=next(matrix(e) for f in main.findall('.//x:DOMFrame',NS) if int(f.get('index'))==main_frame for e in f.findall('./x:elements/x:DOMSymbolInstance',NS) if e.get('libraryItemName','').endswith('/'+filename))
                transform = compose(parent, local)
                wings=[]
                for wingframe in symbol.findall('.//x:DOMFrame',NS):
                    if int(wingframe.get('index','0'))<=start<int(wingframe.get('index','0'))+int(wingframe.get('duration','1')):
                        for child in wingframe.findall('./x:elements/x:DOMSymbolInstance',NS):
                            if child.get('libraryItemName','').endswith('Wing Code'):wings.append(compose(parent,matrix(child)))
                if len(wings)>2:raise ValueError('Too many Wing Cap layers')
                rows.append((path, start, min(int(frame.get("duration", "1")), limit-start), transform,wings))
        rows.sort(key=lambda r: r[1])
        if not rows or rows[0][1] != 0:
            raise ValueError("Missing first bitmap in " + filename)
        for left, right in zip(rows, rows[1:]):
            if left[1] + left[2] != right[1]:
                raise ValueError("Noncontiguous bitmap track in " + filename)
        lines.append(f"inline constexpr std::array<Sprite, {len(rows)}> {name} = {{{{")
        for path, start, duration, transform,wings in rows:
            lines.append("    {" + json.dumps(path) + f", {start}, {duration}, " +
                         ", ".join(f"{value:.8f}f" for value in transform) + f",{len(wings)},{{{{"+",".join("{"+",".join(f"{v:.8f}f" for v in w)+"}" for w in wings)+"}}},")
        lines.append("}};")
    lines += ["}", ""]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    generate(*(Path(arg) for arg in sys.argv[1:]))
