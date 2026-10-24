"""Maps ActionScript in an XFL document to the SWF locations it was compiled to.

Symbols are paired with SWF sprites starting from known anchors (main timeline = sprite 0,
exported symbols by linkage name) and propagated through placements, which are matched
frame by frame using transform matrices, instance names and already-known pairings.
Every mapped script is checked against the strings in its compiled AVM1 bytecode."""

import itertools
import re
from collections import deque

from swf import avm1_strings

TOKEN = re.compile(r'"((?:\\.|[^"\\])*)"|\'((?:\\.|[^\'\\])*)\'|([A-Za-z_$][\w$]*)')


def source_tokens(src):
    out = set()
    for m in TOKEN.finditer(src):
        if m.group(3) is not None:
            out.add(m.group(3))
        else:
            s = m.group(1) if m.group(1) is not None else m.group(2)
            out.add(s.encode().decode("unicode_escape", "replace") if "\\" in s else s)
    return out


COMMENT = re.compile(r'//[^\n]*|/\*.*?\*/', re.S)


def strip_comments(src):
    return COMMENT.sub("", src).strip()


def match_score(src, bytecode_strings):
    """Fraction of bytecode strings that appear in the source (1.0 = perfect)."""
    if not bytecode_strings:
        return 1.0
    toks = source_tokens(src)
    hit = 0
    for s in bytecode_strings:
        # Dotted paths and "/" targets may be split by the compiler or the source.
        if s in toks or all(p in toks for p in re.split(r"[./:]", s) if p) or s in src:
            hit += 1
    return hit / len(bytecode_strings)


class Mapped:
    def __init__(self, kind, key, source, origin, score):
        self.kind = kind      # 'frame' | 'clip' | 'button'
        self.key = key        # (sprite, action index) | (sprite, place index) | (button id,)
        self.source = source
        self.origin = origin  # human-readable XFL location
        self.score = score


def matrix_distance(a, b):
    if a is None or b is None:
        return 1e9
    return (abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2]) + abs(a[3] - b[3])) * 100 + \
        abs(a[4] - b[4]) + abs(a[5] - b[5])


class Mapper:
    def __init__(self, doc, swf):
        self.doc, self.swf = doc, swf
        self.sym_to_sprite = {}
        self.sprite_to_sym = {}
        self.results = []
        self.problems = []  # (origin, reason)

    def run(self):
        queue = deque()
        self._pair("<main>", 0, queue)
        for sym in self.doc.symbols.values():
            if sym.linkage and sym.linkage in self.swf.exports:
                self._pair(sym.name, self.swf.exports[sym.linkage], queue)
        while queue:
            name, sid = queue.popleft()
            timeline = self.doc.main if name == "<main>" else self.doc.symbols[name].timeline
            if timeline is not None and sid in self.swf.sprites:
                self._map_timeline(name, timeline, self.swf.sprites[sid], queue)
        return self

    def _pair(self, name, sid, queue):
        if name in self.sym_to_sprite or sid in self.sprite_to_sym:
            if self.sym_to_sprite.get(name) != sid:
                self.problems.append((name, f"conflicting pairing with sprite {sid}"))
            return
        if sid != 0 and self.swf.kinds.get(sid) != "sprite":
            return
        self.sym_to_sprite[name] = sid
        self.sprite_to_sym[sid] = name
        queue.append((name, sid))

    def _map_timeline(self, name, timeline, sprite, queue):
        places_by_frame = {}
        for p in sprite.places:
            if p.char_id is not None:
                places_by_frame.setdefault(p.frame, []).append(p)
        actions_by_frame = {}
        for i, (frame, code) in enumerate(sprite.actions):
            actions_by_frame.setdefault(frame, []).append(i)

        frame_scripts = {}
        instances_by_frame = {}
        previous = {}  # id(keyframe) -> previous keyframe on the same layer
        for layer in timeline.layers:
            for a, b in zip(layer.keyframes, layer.keyframes[1:]):
                previous[id(b)] = a
            for kf in layer.keyframes:
                f = kf.index + 1
                if kf.script:
                    frame_scripts.setdefault(f, []).append((layer, kf))
                for inst in kf.instances:
                    instances_by_frame.setdefault(f, []).append((layer, kf, inst))

        # Frame scripts: same frame, same count; pick the layer order that verifies best.
        for f, items in sorted(frame_scripts.items()):
            actions = actions_by_frame.get(f, [])
            origin = lambda layer, kf: f"{name} / layer '{layer.name}' / frame {kf.index + 1}"
            if len(actions) != len(items):
                for layer, kf in items:
                    self.problems.append((origin(layer, kf), f"{len(items)} scripts but {len(actions)} DoActions"))
                continue
            strings = [avm1_strings(sprite.actions[a][1]) for a in actions]
            orders = itertools.permutations(items) if len(items) <= 5 else [items, list(reversed(items))]
            best, best_score = None, -1
            for order in orders:
                score = sum(match_score(kf.script, s) for (layer, kf), s in zip(order, strings))
                if score > best_score:
                    best, best_score = order, score
            for (layer, kf), a, s in zip(best, actions, strings):
                self.results.append(Mapped("frame", (sprite.id, a), kf.script, origin(layer, kf),
                                           match_score(kf.script, s)))

        # Placements: match instances to PlaceObjects of the same frame.
        for f, items in sorted(instances_by_frame.items()):
            candidates = list(places_by_frame.get(f, []))
            # Scripted and named instances first: they are the ones we must get right.
            items.sort(key=lambda t: (t[2].script is None, t[2].name is None))
            for layer, kf, inst in items:
                known = self.sym_to_sprite.get(inst.library_item)
                best, best_cost = None, 1e9
                for c in candidates:
                    cost = matrix_distance(inst.matrix, c.matrix)
                    if inst.name or c.name:
                        cost += 0 if inst.name == c.name else 50
                    if known is not None and c.char_id != known:
                        cost += 1000
                    # A movie clip or button instance compiles to a sprite or button
                    # placement, never a plain shape (graphic symbols may be flattened);
                    # without this, placements sharing a matrix (often the identity) were
                    # matched to whichever came first.
                    if inst.symbol_type != "graphic" and self.swf.kinds.get(c.char_id) not in ("sprite", "button"):
                        cost += 20
                    if inst.script and not c.clip_actions and inst.symbol_type != "button":
                        cost += 1  # prefer the scripted placement on ties
                    if cost < best_cost:
                        best, best_cost = c, cost
                origin = f"{name} / layer '{layer.name}' / frame {kf.index + 1} / {inst.library_item}" + \
                    (f" '{inst.name}'" if inst.name else "")
                if best is None or best_cost >= 50:
                    if inst.script and not self._continues(inst, previous.get(id(kf))):
                        self.problems.append((origin, "no matching placement"))
                    continue
                candidates.remove(best)
                kind = self.swf.kinds.get(best.char_id)
                if kind == "sprite":
                    self._pair(inst.library_item, best.char_id, queue)
                if not inst.script:
                    continue
                if kind == "button":
                    conds = self.swf.buttons.get(best.char_id, [])
                    strings = set().union(*[avm1_strings(c) for _, c in conds]) if conds else set()
                    self.results.append(Mapped("button", (best.char_id,), inst.script, origin,
                                               match_score(inst.script, strings)))
                elif best.clip_actions:
                    strings = set().union(*[avm1_strings(c) for _, c in best.clip_actions])
                    self.results.append(Mapped("clip", (sprite.id, best.index), inst.script, origin,
                                               match_score(inst.script, strings)))
                elif strip_comments(inst.script):
                    if not self._continues(inst, previous.get(id(kf))):
                        self.problems.append((origin, "placement has no clip actions"))

    @staticmethod
    def _continues(inst, prev_kf):
        """True if the previous keyframe on the layer holds the same instance with the
        same script: Flash then keeps (or tweens) the existing object instead of placing
        a new one, and the script was already mapped there."""
        if prev_kf is None:
            return False
        norm = lambda s: " ".join(strip_comments(s or "").split())
        return any(p.library_item == inst.library_item and norm(p.script) == norm(inst.script)
                   for p in prev_kf.instances)

    def unpaired_used_symbols(self):
        """Symbols with scripts that a paired timeline places but that were never matched
        to a sprite: their scripts would be lost silently, so they are reported."""
        used = set()
        for name in self.sym_to_sprite:
            timeline = self.doc.main if name == "<main>" else self.doc.symbols[name].timeline
            for layer in (timeline.layers if timeline else []):
                for kf in layer.keyframes:
                    # Graphic symbols' scripts never run in Flash.
                    used.update(inst.library_item for inst in kf.instances if inst.symbol_type != "graphic")
        out = []
        for name in sorted(used - set(self.sym_to_sprite)):
            sym = self.doc.symbols.get(name)
            if sym and sym.timeline and any(kf.script or any(i.script for i in kf.instances)
                                            for layer in sym.timeline.layers for kf in layer.keyframes):
                out.append(name)
        return out

    def unreachable_scripts(self):
        """Scripts in symbols that never made it into the SWF (unused library items)."""
        n = 0
        for sym in self.doc.symbols.values():
            if sym.name in self.sym_to_sprite or sym.timeline is None:
                continue
            for layer in sym.timeline.layers:
                for kf in layer.keyframes:
                    n += bool(kf.script) + sum(1 for i in kf.instances if i.script)
        return n


if __name__ == "__main__":
    import sys
    import time
    from swf import Swf
    from xfl import Document

    t = time.time()
    doc = Document(sys.argv[1])
    swf = Swf(sys.argv[2])
    m = Mapper(doc, swf).run()
    kinds = {}
    for r in m.results:
        kinds[r.kind] = kinds.get(r.kind, 0) + 1
    low = [r for r in m.results if r.score < 0.9]
    print(f"symbols paired: {len(m.sym_to_sprite)} of {len(doc.symbols) + 1}; "
          f"sprites in SWF: {len(swf.sprites)}")
    print(f"scripts mapped: {kinds}; low verification (<0.9): {len(low)}")
    total_swf = sum(len(s.actions) for s in swf.sprites.values())
    clip_places = sum(1 for s in swf.sprites.values() for p in s.places if p.clip_actions)
    buttons = sum(1 for b in swf.buttons.values() if b)
    print(f"SWF has {total_swf} DoActions, {clip_places} placements with clip actions, "
          f"{buttons} buttons with actions")
    print(f"problems: {len(m.problems)}; scripts in unused symbols: {m.unreachable_scripts()}")
    print(f"used symbols with scripts left unpaired: {m.unpaired_used_symbols()}")
    reasons = {}
    for o, r in m.problems:
        reasons[r.split(' but ')[0] if 'DoActions' in r else r] = reasons.get(r, 0) + 1
    for r, c in sorted(reasons.items(), key=lambda x: -x[1])[:10]:
        print(f"  {c:5}  {r}")
    for r in sorted(low, key=lambda r: r.score)[:8]:
        print(f"  low {r.score:.2f} {r.kind} {r.key} {r.origin}")
    print(f"{time.time() - t:.1f} s")
