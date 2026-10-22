"""Import one simple bitmap timeline; reject features we cannot preserve yet.

Python is a build-time tool only. The native runtime consumes generated C++ data.
"""
import json
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

NS = {"x": "http://ns.adobe.com/xfl/2008/"}


def generate(root, symbol, output):
    document = ET.parse(root / "DOMDocument.xml").getroot()
    library = root / "LIBRARY"
    images = sorted(p.relative_to(library).as_posix() for p in library.rglob("*.png"))
    bitmap_paths = {}
    for bitmap in document.findall(".//x:DOMBitmapItem", NS):
        # This repository's PNG exports trim whitespace on each folder component.
        path = Path(*[part.strip() for part in bitmap.attrib["href"].split("/")])
        bitmap_paths[bitmap.attrib["name"]] = path.with_suffix(".png").as_posix()

    tree = ET.parse(symbol).getroot()
    layers = tree.findall("./x:timeline/x:DOMTimeline/x:layers/x:DOMLayer", NS)
    if len(layers) != 1:
        raise ValueError("Preview importer requires exactly one bitmap layer")
    frames = []
    end = 0
    for frame in layers[0].findall("./x:frames/x:DOMFrame", NS):
        if frame.get("tweenType") or frame.find("x:Actionscript", NS) is not None:
            raise ValueError("Scripted/tweened frames require the full timeline importer")
        index = int(frame.attrib["index"])
        duration = int(frame.get("duration", "1"))
        if index != end or duration < 1:
            raise ValueError("Timeline must contain contiguous positive frame spans")
        elements = list(frame.find("x:elements", NS))
        if len(elements) != 1 or elements[0].tag != "{" + NS["x"] + "}DOMBitmapInstance":
            raise ValueError("Only single-bitmap frames are supported in this milestone")
        bitmap = elements[0]
        if any(child.tag != "{" + NS["x"] + "}matrix" for child in bitmap):
            raise ValueError("Unsupported bitmap effects")
        matrix = bitmap.find("./x:matrix/x:Matrix", NS)
        values = {} if matrix is None else matrix.attrib
        for key, default in (("a", 1), ("d", 1), ("b", 0), ("c", 0)):
            if float(values.get(key, default)) != default:
                raise ValueError("Scaled/rotated bitmap frames are not supported yet")
        path = bitmap_paths[bitmap.attrib["libraryItemName"]]
        if path not in images:
            raise ValueError("Missing exported bitmap: " + path)
        frames.append((path, index, duration, float(values.get("tx", 0)), float(values.get("ty", 0))))
        end += duration
    if not frames:
        raise ValueError("Empty animation")
    quote = lambda value: json.dumps(str(value), ensure_ascii=True)
    data = ["// Generated from original XFL. Do not edit.", "#pragma once", "#include <array>",
            "namespace sm63::assets {", "struct Frame { const char* path; int start; int duration; float x; float y; };",
            f"inline constexpr int width = {int(document.attrib['width'])};",
            f"inline constexpr int height = {int(document.attrib['height'])};",
            f"inline constexpr double tickRate = {float(document.attrib['frameRate'])};",
            f"inline constexpr const char* defaultRoot = {quote(library.resolve())};",
            f"inline constexpr int animationLength = {end};",
            f"inline constexpr std::array<Frame, {len(frames)}> animation = {{{{"]
    for path, index, duration, x, y in frames:
        data.append(f"    {{{quote(path)}, {index}, {duration}, {x}f, {y}f}},")
    data += ["}};", f"inline constexpr std::array<const char*, {len(images)}> catalog = {{{{"]
    data += ["    " + quote(path) + "," for path in images]
    data += ["}};", "}", ""]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(data), encoding="utf-8")
    print(f"Imported {len(frames)} animation spans ({end} ticks), {len(images)} PNGs")


if __name__ == "__main__":
    generate(*(Path(arg) for arg in sys.argv[1:]))
