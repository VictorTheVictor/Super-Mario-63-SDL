"""Minimal SWF reader for the transpiler: sprite timelines, placements, scripts and
the strings inside AVM1 bytecode (used to verify script mapping)."""

import struct
import zlib


class Bits:
    def __init__(self, data, pos=0):
        self.d, self.p, self.bit = data, pos, 0

    def ub(self, n):
        v = 0
        for _ in range(n):
            byte = self.d[self.p] if self.p < len(self.d) else 0
            v = (v << 1) | ((byte >> (7 - self.bit)) & 1)
            self.bit += 1
            if self.bit == 8:
                self.bit, self.p = 0, self.p + 1
        return v

    def sb(self, n):
        v = self.ub(n)
        return v - (1 << n) if n and v & (1 << (n - 1)) else v

    def align(self):
        if self.bit:
            self.bit, self.p = 0, self.p + 1


def read_matrix(data, pos):
    """Returns ((a, b, c, d, tx, ty) in pixels / unit scale, end position)."""
    b = Bits(data, pos)
    a = d = 1.0
    rb = rc = 0.0
    if b.ub(1):
        n = b.ub(5)
        a, d = b.sb(n) / 65536.0, b.sb(n) / 65536.0
    if b.ub(1):
        n = b.ub(5)
        rb, rc = b.sb(n) / 65536.0, b.sb(n) / 65536.0
    n = b.ub(5)
    tx, ty = b.sb(n) / 20.0, b.sb(n) / 20.0
    b.align()
    return (a, rb, rc, d, tx, ty), b.p


def skip_cxform(data, pos):
    b = Bits(data, pos)
    has_add, has_mult, n = b.ub(1), b.ub(1), b.ub(4)
    b.ub(n * 4 * (has_add + has_mult))
    b.align()
    return b.p


def read_cstr(data, pos):
    end = data.index(b"\0", pos)
    return data[pos:end].decode("utf-8", "replace"), end + 1


class Place:
    def __init__(self):
        self.index = 0          # index within the sprite's PlaceObject list
        self.frame = 0          # 1-based
        self.depth = 0
        self.char_id = None
        self.move = False
        self.matrix = None
        self.name = None
        self.clip_actions = []  # [(event_flags, bytecode)]


class Sprite:
    def __init__(self, sid, frame_count):
        self.id = sid
        self.frame_count = frame_count
        self.places = []
        self.actions = []       # [(frame, bytecode)] in tag order; index = action index
        self.labels = {}


class Swf:
    def __init__(self, path):
        raw = open(path, "rb").read()
        self.version = raw[3]
        body = zlib.decompress(raw[8:]) if raw[:3] == b"CWS" else raw[8:]
        nbits = body[0] >> 3
        pos = (5 + 4 * nbits + 7) // 8 + 4
        frame_count = struct.unpack_from("<H", body, pos - 2)[0]
        self.sprites = {}
        self.exports = {}       # name -> id
        self.kinds = {}         # id -> 'sprite' | 'button' | 'shape' | ...
        self.buttons = {}       # id -> [(conditions, bytecode)]
        self.sprites[0] = Sprite(0, frame_count)
        self._walk(body, pos, len(body), self.sprites[0])

    def _walk(self, data, pos, end, sprite):
        frame = 1
        while pos < end:
            header = struct.unpack_from("<H", data, pos)[0]
            pos += 2
            code, length = header >> 6, header & 63
            if length == 63:
                length = struct.unpack_from("<I", data, pos)[0]
                pos += 4
            t0, t1 = pos, pos + length
            if code == 0:
                return
            if code == 1:
                frame += 1
            elif code == 39:
                sid, fc = struct.unpack_from("<HH", data, t0)
                child = Sprite(sid, fc)
                self.sprites[sid] = child
                self.kinds[sid] = "sprite"
                self._walk(data, t0 + 4, t1, child)
            elif code in (26, 70):
                sprite.places.append(self._place(data, t0, t1, code, frame, len(sprite.places)))
            elif code == 12:
                sprite.actions.append((frame, data[t0:t1]))
            elif code == 43:
                name, _ = read_cstr(data, t0)
                sprite.labels[name] = frame
            elif code == 56:
                n = struct.unpack_from("<H", data, t0)[0]
                p = t0 + 2
                for _ in range(n):
                    cid = struct.unpack_from("<H", data, p)[0]
                    name, p = read_cstr(data, p + 2)
                    self.exports[name] = cid
            elif code == 34:
                self._button(data, t0, t1)
            elif code in (2, 22, 32, 83):
                self.kinds[struct.unpack_from("<H", data, t0)[0]] = "shape"
            elif code in (11, 33):
                self.kinds[struct.unpack_from("<H", data, t0)[0]] = "text"
            elif code == 37:
                self.kinds[struct.unpack_from("<H", data, t0)[0]] = "edittext"
            elif code in (46, 84):
                self.kinds[struct.unpack_from("<H", data, t0)[0]] = "morph"
            pos = t1

    def _place(self, data, p, end, code, frame, index):
        pl = Place()
        pl.index, pl.frame = index, frame
        f1 = data[p]
        p += 1
        f2 = 0
        if code == 70:
            f2 = data[p]
            p += 1
        pl.move = bool(f1 & 1)
        pl.depth = struct.unpack_from("<H", data, p)[0]
        p += 2
        if code == 70 and (f2 & 0x08 or (f2 & 0x10 and f1 & 0x02)):
            _, p = read_cstr(data, p)
        if f1 & 0x02:
            pl.char_id = struct.unpack_from("<H", data, p)[0]
            p += 2
        if f1 & 0x04:
            pl.matrix, p = read_matrix(data, p)
        if f1 & 0x08:
            p = skip_cxform(data, p)
        if f1 & 0x10:
            p += 2
        if f1 & 0x20:
            pl.name, p = read_cstr(data, p)
        if f1 & 0x40:
            p += 2
        if f1 & 0x80:
            # Filters, blend mode etc. (PlaceObject3) come before clip actions; locate
            # the clip actions from the end instead: they are the final field.
            pl.clip_actions = self._clip_actions_from(data, p, end, f2)
        return pl

    def _clip_actions_from(self, data, p, end, f2):
        if f2 & 0x01:  # filter list
            n = data[p]
            p += 1
            for _ in range(n):
                fid = data[p]
                p += 1
                p += {0: 23, 1: 9, 2: 15, 3: 27, 6: 80}.get(fid, 0)
                if fid in (4, 7):
                    nc = data[p]
                    p += 1 + nc * 5 + 19
                elif fid == 5:
                    mx, my = data[p], data[p + 1]
                    p += 2 + 8 + 4 * mx * my + 5
        if f2 & 0x02:
            p += 1
        if f2 & 0x04:
            p += 1
        if f2 & 0x20:
            p += 1
        if f2 & 0x40:
            p += 4
        p += 2 + 4  # reserved, all event flags
        records = []
        while p + 4 <= end:
            flags = struct.unpack_from("<I", data, p)[0]
            p += 4
            if flags == 0:
                break
            size = struct.unpack_from("<I", data, p)[0]
            p += 4
            body = data[p:p + size]
            if flags & (1 << 17):  # key press: first byte is the key code
                body = body[1:]
            records.append((flags, body))
            p += size
        return records

    def _button(self, data, p, end):
        bid = struct.unpack_from("<H", data, p)[0]
        self.kinds[bid] = "button"
        offset = struct.unpack_from("<H", data, p + 3)[0]
        conds = []
        if offset:
            q = p + 3 + offset
            while q + 4 <= end:
                size, flags = struct.unpack_from("<HH", data, q)
                body_end = q + size if size else end
                conds.append((flags, data[q + 4:body_end]))
                if not size:
                    break
                q += size
        self.buttons[bid] = conds


# ---------------------------------------------------------------- AVM1 strings

def avm1_strings(code):
    """Strings referenced by AVM1 bytecode (constant pool, pushes, gotos, functions),
    recursing into nested function bodies."""
    out = set()
    p = 0
    n = len(code)
    while p < n:
        op = code[p]
        p += 1
        if op == 0:
            continue
        if op < 0x80:
            continue
        if p + 2 > n:
            break
        length = struct.unpack_from("<H", code, p)[0]
        p += 2
        payload = code[p:p + length]
        if op == 0x88:  # ConstantPool
            count = struct.unpack_from("<H", payload, 0)[0]
            q = 2
            for _ in range(count):
                s, q = read_cstr(payload, q)
                out.add(s)
        elif op == 0x96:  # Push
            q = 0
            while q < len(payload):
                t = payload[q]
                q += 1
                if t == 0:
                    s, q = read_cstr(payload, q)
                    out.add(s)
                else:
                    q += {1: 4, 2: 0, 3: 0, 4: 1, 5: 1, 6: 8, 7: 4, 8: 1, 9: 2}.get(t, 0)
        elif op in (0x8C, 0x83, 0x8B):  # GoToLabel, GetURL, SetTarget
            for s in payload.split(b"\0"):
                if s:
                    out.add(s.decode("utf-8", "replace"))
        elif op in (0x9B, 0x8E):  # DefineFunction / DefineFunction2
            name, q = read_cstr(payload, 0)
            if name:
                out.add(name)
            # Parameter names are register-allocated in DefineFunction2; the body
            # follows the header inline in the stream, so keep scanning normally.
        p += length
    return out
