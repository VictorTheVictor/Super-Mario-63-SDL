"""Reader for uncompressed XFL documents (Flash CS5/CS6): symbols, timelines,
keyframes, symbol instances and their ActionScript."""

import os
import re
import xml.etree.ElementTree as ET

NS = "{http://ns.adobe.com/xfl/2008/}"


def _float(e, name, default):
    v = e.get(name)
    return float(v) if v is not None else default


def read_matrix(element):
    m = element.find(f"{NS}matrix/{NS}Matrix")
    if m is None:
        return (1.0, 0.0, 0.0, 1.0, 0.0, 0.0)
    return (_float(m, "a", 1.0), _float(m, "b", 0.0), _float(m, "c", 0.0),
            _float(m, "d", 1.0), _float(m, "tx", 0.0), _float(m, "ty", 0.0))


def read_script(element):
    s = element.find(f"{NS}Actionscript/{NS}script")
    return s.text if s is not None and s.text and s.text.strip() else None


class Instance:
    def __init__(self, e, order):
        self.library_item = e.get("libraryItemName")
        self.name = e.get("name") or None
        self.symbol_type = e.get("symbolType", "movie clip")
        self.matrix = read_matrix(e)
        self.script = read_script(e)
        self.order = order  # position among the keyframe's elements


class Keyframe:
    def __init__(self, e):
        self.index = int(e.get("index", "0"))
        self.duration = int(e.get("duration", "1"))
        self.label = e.get("name") if e.get("labelType", "name") == "name" else None
        self.script = read_script(e)
        self.instances = []
        elements = e.find(f"{NS}elements")
        if elements is not None:
            self._collect(elements)

    def _collect(self, parent):
        for child in parent:
            tag = child.tag[len(NS):]
            if tag == "DOMSymbolInstance":
                self.instances.append(Instance(child, len(self.instances)))
            elif tag == "DOMGroup":
                members = child.find(f"{NS}members")
                if members is not None:
                    self._collect(members)


class Layer:
    def __init__(self, e, index):
        self.index = index
        self.name = e.get("name", "")
        self.type = e.get("layerType", "normal")
        self.parent_index = e.get("parentLayerIndex")
        frames = e.find(f"{NS}frames")
        self.keyframes = [Keyframe(f) for f in frames] if frames is not None else []


class Timeline:
    def __init__(self, e):
        self.name = e.get("name", "")
        layers = e.find(f"{NS}layers")
        all_layers = [Layer(l, i) for i, l in enumerate(layers)] if layers is not None else []
        # Guide and folder layers are not compiled into the SWF.
        self.layers = [l for l in all_layers if l.type not in ("guide", "folder")]
        self.frame_count = max((k.index + k.duration for l in self.layers for k in l.keyframes), default=1)


class Symbol:
    def __init__(self, path):
        root = ET.parse(path).getroot()
        self.name = root.get("name")
        self.symbol_type = root.get("symbolType", "movie clip")
        self.linkage = root.get("linkageIdentifier") if root.get("linkageExportForAS") == "true" else None
        tl = root.find(f"{NS}timeline/{NS}DOMTimeline")
        self.timeline = Timeline(tl) if tl is not None else None


class Document:
    def _library_path(self, href):
        """Finds a library file. Checkouts may differ from the hrefs: leading spaces in
        folder names (used to sort the Flash library) can be lost, and characters such
        as '=' are escaped as "&#061" in file names."""
        base = os.path.join(self.folder, "LIBRARY")
        candidates = [href, "/".join(p.strip() for p in href.split("/"))]
        candidates += [re.sub(r"&#(\d+);?", lambda m: chr(int(m.group(1))), c) for c in list(candidates)]
        for c in candidates:
            path = os.path.join(base, c)
            if os.path.exists(path):
                return path
        raise FileNotFoundError(f"library item not found: {href}")

    def __init__(self, folder):
        self.folder = folder
        root = ET.parse(os.path.join(folder, "DOMDocument.xml")).getroot()
        scenes = root.findall(f"{NS}timelines/{NS}DOMTimeline")
        if len(scenes) != 1:
            raise ValueError(f"expected one scene, found {len(scenes)}")
        self.main = Timeline(scenes[0])
        self.symbols = {}
        for inc in root.findall(f"{NS}symbols/{NS}Include"):
            sym = Symbol(self._library_path(inc.get("href")))
            self.symbols[sym.name] = sym
