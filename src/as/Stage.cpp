#include "as/Stage.h"

#include <cmath>
#include <map>
#include <unordered_map>

#include "as/Host.h"
#include "flash/Text.h"

namespace as {

using flash::DisplayObject;
using flash::MovieClip;
using flash::Matrix;
using flash::Rect;

namespace {

Runtime& R() { return Runtime::get(); }

Object* g_movieClipProto = nullptr;
Object* g_textFieldProto = nullptr;
Object* g_buttonProto = nullptr;
SoundHost g_silentSound;
SoundHost* g_soundHost = &g_silentSound;

// ---------------------------------------------------------------- registry

std::unordered_map<uint64_t, ScriptFn>& frameScripts() {
    static std::unordered_map<uint64_t, ScriptFn> m;
    return m;
}
std::unordered_map<uint64_t, std::vector<ClipHandler>>& clipEvents() {
    static std::unordered_map<uint64_t, std::vector<ClipHandler>> m;
    return m;
}
std::unordered_map<uint16_t, std::vector<ClipHandler>>& buttonActions() {
    static std::unordered_map<uint16_t, std::vector<ClipHandler>> m;
    return m;
}
uint64_t key(uint16_t sprite, uint32_t index) { return (uint64_t)sprite << 32 | index; }

}  // namespace

void registerFrameScript(uint16_t spriteId, uint32_t actionIndex, ScriptFn fn) {
    frameScripts()[key(spriteId, actionIndex)] = fn;
}
void registerClipEvent(uint16_t spriteId, uint32_t placeIndex, uint32_t events, ScriptFn fn) {
    clipEvents()[key(spriteId, placeIndex)].push_back({events, fn});
}
void registerButtonAction(uint16_t buttonId, uint32_t conditions, ScriptFn fn) {
    buttonActions()[buttonId].push_back({conditions, fn});
}
ScriptFn findFrameScript(uint16_t spriteId, uint32_t actionIndex) {
    auto it = frameScripts().find(key(spriteId, actionIndex));
    return it == frameScripts().end() ? nullptr : it->second;
}
std::vector<ClipHandler> findClipEvents(uint16_t spriteId, uint32_t placeIndex) {
    auto it = clipEvents().find(key(spriteId, placeIndex));
    return it == clipEvents().end() ? std::vector<ClipHandler>{} : it->second;
}
std::vector<ClipHandler> findButtonActions(uint16_t buttonId) {
    auto it = buttonActions().find(buttonId);
    return it == buttonActions().end() ? std::vector<ClipHandler>{} : it->second;
}

void setSoundHost(SoundHost* host) { g_soundHost = host ? host : &g_silentSound; }

StageContext& stage() {
    static StageContext s;
    return s;
}

// ---------------------------------------------------------------- paths

bool isRemoved(const DisplayObject* d) {
    // Off the display list if any ancestor was removed: removing a clip takes its whole
    // subtree with it (its children keep their parent pointers).
    if (!d) return false;
    const DisplayObject* root = stage().root.get();
    while (d != root) {
        if (!d->parent()) return true;
        d = d->parent();
    }
    return false;
}

std::string targetPath(const DisplayObject* d) {
    if (!d) return "";
    if (d == stage().root.get() || !d->parent()) return "_level0";
    return targetPath(d->parent()) + "." + d->name;
}

static std::string slashPath(const DisplayObject* d) {
    if (!d || d == stage().root.get() || !d->parent()) return "/";
    std::string p = slashPath(d->parent());
    return (p == "/" ? "" : p) + "/" + d->name;
}

DisplayObject* resolvePath(DisplayObject* base, const std::string& path) {
    if (path.empty()) return base;
    DisplayObject* cur = base;
    std::string p = path;
    // Slash syntax: "/a/b" is absolute, "../a" goes up.
    bool slash = p.find('/') != std::string::npos;
    if (slash && p[0] == '/') {
        cur = stage().root.get();
        p = p.substr(1);
    }
    std::vector<std::string> parts;
    std::string part;
    for (char c : p) {
        if (c == '.' && !slash) {
            parts.push_back(part);
            part.clear();
        } else if (c == '/' && slash) {
            parts.push_back(part);
            part.clear();
        } else if (c == ':' ) {
            break;  // "path:var" — variable part handled by callers
        } else {
            part.push_back(c);
        }
    }
    parts.push_back(part);
    for (size_t i = 0; i < parts.size(); i++) {
        const std::string& n = parts[i];
        if (n.empty() || n == "this") continue;
        if (!cur) return nullptr;
        if (n == "_root" || n == "_level0") cur = stage().root.get();
        else if (n == "_parent" || n == "..") cur = cur->parent();
        else if (auto* mc = dynamic_cast<MovieClip*>(cur)) cur = mc->childByName(n);
        else return nullptr;
    }
    return cur;
}

// ---------------------------------------------------------------- StageObject

StageObject::StageObject(const std::shared_ptr<DisplayObject>& d, Object* proto)
    : Object(proto), ref(d) {
    isClip_ = dynamic_cast<MovieClip*>(d.get()) != nullptr;
    path = targetPath(d.get());
}

StageObject::~StageObject() {
    if (auto d = ref.lock())
        if (d->scriptObject == this) d->scriptObject = nullptr;
}

DisplayObject* StageObject::display() {
    auto d = ref.lock();
    if (d && !isRemoved(d.get())) return d.get();
    // Removed: a new object at the same path takes over the reference.
    if (DisplayObject* again = resolvePath(stage().root.get(), path)) {
        if (again != d.get() && (dynamic_cast<MovieClip*>(again) != nullptr) == isClip_) {
            if (auto shared = again->shared_from_this()) return shared.get();
        }
    }
    return d.get();  // still alive (e.g. during onUnload) or null
}

MovieClip* StageObject::clip() { return dynamic_cast<MovieClip*>(display()); }
flash::TextField* StageObject::textField() { return dynamic_cast<flash::TextField*>(display()); }

uint32_t StageObject::handlerMask() const {
    uint32_t m = 0;
    for (auto& h : handlers) m |= h.events;
    return m;
}

Scope* StageObject::targetScope() {
    if (!targetScope_) targetScope_ = R().heap.make<Scope>(Scope::Target, this, R().globalScope);
    return targetScope_;
}

const char* StageObject::typeName() const { return isClip_ ? "movieclip" : "object"; }

std::string StageObject::defaultString() const {
    auto d = ref.lock();
    return d ? targetPath(d.get()) : path;
}

void StageObject::trace(Gc& gc) {
    Object::trace(gc);
    gc.mark(targetScope_);
}

StageObject* stageObject(DisplayObject* d) {
    if (!d) return nullptr;
    if (d->scriptObject) return static_cast<StageObject*>(d->scriptObject);
    Object* proto = R().objectProto;
    if (dynamic_cast<MovieClip*>(d)) proto = g_movieClipProto;
    else if (dynamic_cast<flash::TextField*>(d)) proto = g_textFieldProto;
    else if (dynamic_cast<flash::ButtonObject*>(d)) proto = g_buttonProto;
    auto* so = R().heap.make<StageObject>(d->shared_from_this(), proto);
    d->scriptObject = so;
    return so;
}

Value valueOf(DisplayObject* d) {
    StageObject* so = stageObject(d);
    return so ? Value(so) : undefined;
}

DisplayObject* displayOf(const Value& v) {
    auto* so = dynamic_cast<StageObject*>(v.object());
    return so ? so->display() : nullptr;
}

// ---------------------------------------------------------------- magic properties

namespace {

enum Magic {
    M_x, M_y, M_xscale, M_yscale, M_rotation, M_alpha, M_visible, M_width, M_height,
    M_currentframe, M_totalframes, M_framesloaded, M_name, M_parent, M_target, M_url,
    M_xmouse, M_ymouse, M_droptarget, M_quality, M_highquality, M_focusrect,
    M_soundbuftime, M_root, M_level0, M_lockroot,
    // TextField
    T_text, T_htmlText, T_textColor, T_html, T_variable, T_length, T_textWidth, T_textHeight,
};

const std::unordered_map<Atom, Magic>& magicTable() {
    static std::unordered_map<Atom, Magic> t = [] {
        std::unordered_map<Atom, Magic> m;
        const std::pair<const char*, Magic> names[] = {
            {"_x", M_x}, {"_y", M_y}, {"_xscale", M_xscale}, {"_yscale", M_yscale},
            {"_rotation", M_rotation}, {"_alpha", M_alpha}, {"_visible", M_visible},
            {"_width", M_width}, {"_height", M_height}, {"_currentframe", M_currentframe},
            {"_totalframes", M_totalframes}, {"_framesloaded", M_framesloaded}, {"_name", M_name},
            {"_parent", M_parent}, {"_target", M_target}, {"_url", M_url}, {"_xmouse", M_xmouse},
            {"_ymouse", M_ymouse}, {"_droptarget", M_droptarget}, {"_quality", M_quality},
            {"_highquality", M_highquality}, {"_focusrect", M_focusrect},
            {"_soundbuftime", M_soundbuftime}, {"_root", M_root}, {"_level0", M_level0},
            {"_lockroot", M_lockroot}, {"text", T_text}, {"htmlText", T_htmlText},
            {"textColor", T_textColor}, {"html", T_html}, {"variable", T_variable},
            {"length", T_length}, {"textWidth", T_textWidth}, {"textHeight", T_textHeight}};
        for (auto& [n, v] : names) m[intern(n)] = v;
        return m;
    }();
    return t;
}

double normalizeDegrees(double deg) {
    deg = std::fmod(deg, 360.0);
    if (deg > 180) deg -= 360;
    if (deg <= -180) deg += 360;
    return deg;
}

Rect parentBounds(DisplayObject* d) { return d->matrix.apply(d->localBounds()); }

}  // namespace

bool StageObject::getMagic(DisplayObject* d, Atom key, Value& out) {
    auto it = magicTable().find(key);
    if (it == magicTable().end()) return false;
    Magic m = it->second;
    auto* tf = dynamic_cast<flash::TextField*>(d);
    if (m >= T_text && !tf) return false;
    auto* mc = dynamic_cast<MovieClip*>(d);
    switch (m) {
        case M_x: out = d->matrix.tx / 20; return true;
        case M_y: out = d->matrix.ty / 20; return true;
        case M_xscale: out = d->transform().scaleX * 100; return true;
        case M_yscale: out = d->transform().scaleY * 100; return true;
        case M_rotation: out = normalizeDegrees(d->transform().rotation * 180 / M_PI); return true;
        case M_alpha: out = (double)d->cxform.aMul * 100; return true;
        case M_visible: out = d->visible; return true;
        case M_width: {
            Rect b = parentBounds(d);
            out = b.empty() ? 0.0 : (b.xMax - b.xMin) / 20;
            return true;
        }
        case M_height: {
            Rect b = parentBounds(d);
            out = b.empty() ? 0.0 : (b.yMax - b.yMin) / 20;
            return true;
        }
        case M_currentframe: out = mc ? Value(mc->currentFrame()) : undefined; return true;
        case M_totalframes:
        case M_framesloaded: out = mc ? Value(mc->totalFrames()) : undefined; return true;
        case M_name: out = Value(d->name); return true;
        case M_parent: out = d->parent() ? valueOf(d->parent()) : undefined; return true;
        case M_target: out = Value(slashPath(d)); return true;
        case M_url: out = Value(stage().url); return true;
        case M_xmouse:
        case M_ymouse: {
            flash::Point p = d->worldMatrix().inverse().apply(stage().mouseX * 20, stage().mouseY * 20);
            out = (m == M_xmouse ? p.x : p.y) / 20;
            return true;
        }
        case M_droptarget: out = ""; return true;
        case M_quality: out = Value(stage().quality); return true;
        case M_highquality: out = stage().quality == "LOW" ? 0 : stage().quality == "BEST" ? 2 : 1; return true;
        case M_focusrect: out = true; return true;
        case M_soundbuftime: out = 5; return true;
        case M_root:
        case M_level0: out = valueOf(stage().root.get()); return true;
        case M_lockroot: out = false; return true;
        case T_text: out = Value(tf->text()); return true;
        case T_htmlText: out = Value(tf->text()); return true;
        case T_textColor: {
            flash::Color c = tf->textColor();
            out = (double)(c.r << 16 | c.g << 8 | c.b);
            return true;
        }
        case T_html: out = tf->html(); return true;
        case T_variable: out = tf->variable.empty() ? Value::null() : Value(tf->variable); return true;
        case T_length: out = Value(tf->text())["length"].get(); return true;
        case T_textWidth:
        case T_textHeight: {
            out = (m == T_textWidth ? tf->bounds.xMax - tf->bounds.xMin : tf->bounds.yMax - tf->bounds.yMin) / 20 - 4;
            return true;
        }
    }
    return false;
}

bool StageObject::setMagic(DisplayObject* d, Atom key, const Value& v) {
    auto it = magicTable().find(key);
    if (it == magicTable().end()) return false;
    Magic m = it->second;
    auto* tf = dynamic_cast<flash::TextField*>(d);
    if (m >= T_text && !tf) return false;
    double n = v.toNumber();
    auto transform = [&]() { d->transformedByScript = true; };
    switch (m) {
        case M_x:
            if (std::isfinite(n)) d->matrix.tx = n * 20, transform();
            return true;
        case M_y:
            if (std::isfinite(n)) d->matrix.ty = n * 20, transform();
            return true;
        case M_xscale:
        case M_yscale:
        case M_rotation: {
            if (!std::isfinite(n)) return true;
            auto t = d->transform();
            if (m == M_xscale) t.scaleX = n / 100;
            else if (m == M_yscale) t.scaleY = n / 100;
            else t.rotation = normalizeDegrees(n) * M_PI / 180;
            d->setTransform(t);
            transform();
            return true;
        }
        case M_width:
        case M_height: {
            if (!std::isfinite(n)) return true;
            Rect b = parentBounds(d);
            double cur = m == M_width ? b.xMax - b.xMin : b.yMax - b.yMin;
            if (b.empty() || cur <= 0) return true;
            auto t = d->transform();
            if (m == M_width) t.scaleX *= n * 20 / cur;
            else t.scaleY *= n * 20 / cur;
            d->setTransform(t);
            transform();
            return true;
        }
        case M_alpha:
            if (std::isfinite(n)) d->cxform.aMul = (float)(n / 100), transform();
            return true;
        case M_visible: d->visible = v.toBoolean(); return true;
        case M_name: d->name = v.toString(); return true;
        case M_quality: {
            std::string q = v.toString();
            for (auto& c : q) c = (char)std::toupper((unsigned char)c);
            if (q == "LOW" || q == "MEDIUM" || q == "HIGH" || q == "BEST") stage().quality = q;
            return true;
        }
        case M_highquality:
            stage().quality = n >= 2 ? "BEST" : n >= 1 ? "HIGH" : "LOW";
            return true;
        case T_text: tf->setText(v.toString()); return true;
        case T_htmlText:
            if (tf->html()) tf->setHtmlText(v.toString());
            else tf->setText(v.toString());
            return true;
        case T_textColor: {
            uint32_t c = v.toUint32();
            tf->setTextColor({(uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c, 255});
            return true;
        }
        case T_variable: tf->variable = v.isNullish() ? "" : v.toString(); return true;
        default: return true;  // read-only: ignored
    }
}

bool StageObject::getOwn(Atom k, Value& out) {
    DisplayObject* d = display();
    if (d && getMagic(d, k, out)) return true;
    if (Object::getOwn(k, out)) return true;
    if (auto* mc = dynamic_cast<MovieClip*>(d))
        if (DisplayObject* child = mc->childByName(k->str)) {
            out = valueOf(child);
            return true;
        }
    return false;
}

bool StageObject::setOwn(Atom k, const Value& v) {
    DisplayObject* d = display();
    if (d && setMagic(d, k, v)) return true;
    return Object::setOwn(k, v);
}

bool StageObject::hasOwn(Atom k) {
    DisplayObject* d = display();
    if (d) {
        auto it = magicTable().find(k);
        if (it != magicTable().end() && (it->second < T_text || dynamic_cast<flash::TextField*>(d)))
            return true;
    }
    if (Object::hasOwn(k)) return true;
    if (auto* mc = dynamic_cast<MovieClip*>(d)) return mc->childByName(k->str) != nullptr;
    return false;
}

void StageObject::ownKeys(std::vector<Atom>& out) {
    Object::ownKeys(out);
    if (auto* mc = dynamic_cast<MovieClip*>(display()))
        for (auto it = mc->children().rbegin(); it != mc->children().rend(); ++it)
            if (!it->second->name.empty()) out.push_back(intern(it->second->name));
}

// ---------------------------------------------------------------- MovieClip methods

namespace {

MovieClip* thisClip(const Value& self) { return dynamic_cast<MovieClip*>(displayOf(self)); }

void gotoImpl(MovieClip* mc, const Value& frame, bool play) {
    if (!mc) return;
    if (frame.isString()) {
        if (mc->gotoLabel(frame.string(), play)) return;
        double n = frame.toNumber();
        if (std::isfinite(n)) mc->gotoFrame((int)std::clamp(n, -1e9, 1e9), play);
        return;
    }
    double n = frame.toNumber();
    if (std::isfinite(n)) mc->gotoFrame((int)std::clamp(n, -1e9, 1e9), play);
}

Value mc_gotoAndStop(const Value& self, Args a) { gotoImpl(thisClip(self), a[0], false); return undefined; }
Value mc_gotoAndPlay(const Value& self, Args a) { gotoImpl(thisClip(self), a[0], true); return undefined; }
Value mc_play(const Value& self, Args) { if (auto* mc = thisClip(self)) mc->play(); return undefined; }
Value mc_stop(const Value& self, Args) { if (auto* mc = thisClip(self)) mc->stop(); return undefined; }
Value mc_nextFrame(const Value& self, Args) { if (auto* mc = thisClip(self)) mc->nextFrame(); return undefined; }
Value mc_prevFrame(const Value& self, Args) { if (auto* mc = thisClip(self)) mc->prevFrame(); return undefined; }

void applyInitObject(const Value& target, const Value& init) {
    Object* o = init.object();
    if (!o) return;
    std::vector<Atom> keys;
    o->ownKeys(keys);
    for (size_t i = keys.size(); i-- > 0;) setMember(target, keys[i], getMember(init, keys[i]));
}

Value mc_attachMovie(const Value& self, Args a) {
    MovieClip* mc = thisClip(self);
    if (!mc || !stage().movie) return undefined;
    flash::Character* def = stage().movie->exported(a[0].toString());
    if (!def) return undefined;
    auto obj = mc->attach(def, a[2].toInt32() + kDepthOffset, a[1].toString());
    if (!obj) return undefined;
    Value v = valueOf(obj.get());
    applyInitObject(v, a[3]);
    return v;
}

Value mc_createEmptyMovieClip(const Value& self, Args a) {
    MovieClip* mc = thisClip(self);
    if (!mc) return undefined;
    auto obj = mc->createEmpty(a[1].toInt32() + kDepthOffset, a[0].toString());
    return obj ? valueOf(obj.get()) : undefined;
}

Value mc_duplicateMovieClip(const Value& self, Args a) {
    MovieClip* mc = thisClip(self);
    if (!mc || !mc->parent()) return undefined;
    auto obj = mc->parent()->attach(mc->definition(), a[1].toInt32() + kDepthOffset, a[0].toString());
    if (!obj) return undefined;
    obj->matrix = mc->matrix;
    obj->cxform = mc->cxform;
    obj->transformedByScript = true;
    Value v = valueOf(obj.get());
    if (auto* src = stageObject(mc)) stageObject(obj.get())->handlers = src->handlers;
    applyInitObject(v, a[2]);
    return v;
}

Value mc_removeMovieClip(const Value& self, Args) {
    DisplayObject* d = displayOf(self);
    if (!d || !d->parent()) return undefined;
    int asDepth = d->depth - kDepthOffset;
    if (asDepth >= 0 && asDepth < 1048576) d->parent()->removeChild(d->depth);
    return undefined;
}

Value mc_swapDepths(const Value& self, Args a) {
    DisplayObject* d = displayOf(self);
    if (!d || !d->parent()) return undefined;
    int to;
    if (DisplayObject* other = displayOf(a[0])) {
        if (other->parent() != d->parent()) return undefined;
        to = other->depth;
    } else {
        double n = a[0].toNumber();
        if (!std::isfinite(n)) return undefined;
        to = (int)std::clamp(n, -1048576.0, 2130690045.0) + kDepthOffset;
    }
    d->parent()->swapChildDepth(d->depth, to);
    return undefined;
}

Value mc_getDepth(const Value& self, Args) {
    DisplayObject* d = displayOf(self);
    return d ? Value(d->depth - kDepthOffset) : undefined;
}

Value mc_getNextHighestDepth(const Value& self, Args) {
    MovieClip* mc = thisClip(self);
    if (!mc) return undefined;
    int next = 0;
    if (!mc->children().empty()) next = std::max(0, mc->children().rbegin()->first - kDepthOffset + 1);
    return next;
}

Value mc_getInstanceAtDepth(const Value& self, Args a) {
    MovieClip* mc = thisClip(self);
    if (!mc) return undefined;
    DisplayObject* d = mc->childAt(a[0].toInt32() + kDepthOffset);
    return d ? valueOf(d) : undefined;
}

Rect worldBounds(DisplayObject* d) { return d->worldMatrix().apply(d->localBounds()); }

Value mc_hitTest(const Value& self, Args a) {
    DisplayObject* d = displayOf(self);
    if (!d || isRemoved(d)) return false;
    if (a.size() == 1) {
        DisplayObject* other = displayOf(a[0]);
        if (!other || isRemoved(other)) return false;
        Rect r1 = worldBounds(d), r2 = worldBounds(other);
        if (r1.empty() || r2.empty()) return false;
        return r1.xMin <= r2.xMax && r2.xMin <= r1.xMax && r1.yMin <= r2.yMax && r2.yMin <= r1.yMax;
    }
    double x = a[0].toNumber() * 20, y = a[1].toNumber() * 20;
    if (std::isnan(x) || std::isnan(y)) return false;
    StageContext& s = stage();
    if (std::fabs(x - s.mouseX * 20) <= 10 && std::fabs(y - s.mouseY * 20) <= 10 && s.mouseProbes.size() < 4096)
        s.mouseProbes.push_back(d->weak_from_this());
    if (a[2].toBoolean()) return d->hitTest(d->worldMatrix(), {x, y});
    Rect b = worldBounds(d);
    return !b.empty() && x >= b.xMin && x <= b.xMax && y >= b.yMin && y <= b.yMax;
}

Value mc_getBounds(const Value& self, Args a) {
    DisplayObject* d = displayOf(self);
    if (!d) return undefined;
    Matrix m;  // to target space
    if (DisplayObject* t = displayOf(a[0])) m = t->worldMatrix().inverse() * d->worldMatrix();
    Rect b = m.apply(d->localBounds());
    if (b.empty()) {
        const double e = 6710886.35;
        return obj({{"xMin", e}, {"xMax", e}, {"yMin", e}, {"yMax", e}});
    }
    return obj({{"xMin", b.xMin / 20}, {"xMax", b.xMax / 20}, {"yMin", b.yMin / 20}, {"yMax", b.yMax / 20}});
}

Value convertPoint(const Value& self, Args a, bool toGlobal) {
    DisplayObject* d = displayOf(self);
    if (!d || !a[0].isObject()) return undefined;
    Matrix m = d->worldMatrix();
    if (!toGlobal) m = m.inverse();
    flash::Point p = m.apply(a[0]["x"].get().toNumber() * 20, a[0]["y"].get().toNumber() * 20);
    a[0]["x"] = p.x / 20;
    a[0]["y"] = p.y / 20;
    return undefined;
}
Value mc_localToGlobal(const Value& self, Args a) { return convertPoint(self, a, true); }
Value mc_globalToLocal(const Value& self, Args a) { return convertPoint(self, a, false); }

flash::Color rgbAlpha(const Value& rgb, const Value& alpha) {
    uint32_t c = rgb.toUint32();
    double al = alpha.isUndefined() ? 100 : alpha.toNumber();
    return {(uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c, (uint8_t)std::clamp(al * 2.55, 0.0, 255.0)};
}
Value mc_lineStyle(const Value& self, Args a) {
    if (auto* mc = thisClip(self)) mc->drawing().lineStyle(a[0].isUndefined() ? -1 : a[0].toNumber() * 20, rgbAlpha(a[1], a[2]));
    return undefined;
}
Value mc_beginFill(const Value& self, Args a) {
    if (auto* mc = thisClip(self)) {
        if (a[0].isUndefined()) mc->drawing().endFill();
        else mc->drawing().beginFill(rgbAlpha(a[0], a[1]));
    }
    return undefined;
}
Value mc_endFill(const Value& self, Args) { if (auto* mc = thisClip(self)) mc->drawing().endFill(); return undefined; }
Value mc_moveTo(const Value& self, Args a) {
    if (auto* mc = thisClip(self)) mc->drawing().moveTo({a[0].toNumber() * 20, a[1].toNumber() * 20});
    return undefined;
}
Value mc_lineTo(const Value& self, Args a) {
    if (auto* mc = thisClip(self)) mc->drawing().lineTo({a[0].toNumber() * 20, a[1].toNumber() * 20});
    return undefined;
}
Value mc_curveTo(const Value& self, Args a) {
    if (auto* mc = thisClip(self))
        mc->drawing().curveTo({a[0].toNumber() * 20, a[1].toNumber() * 20}, {a[2].toNumber() * 20, a[3].toNumber() * 20});
    return undefined;
}
Value mc_clear(const Value& self, Args) { if (auto* mc = thisClip(self)) mc->drawing().clear(); return undefined; }
Value mc_getBytes(const Value&, Args) { return 1024; }
Value mc_noop(const Value&, Args) { return undefined; }

Value mc_getURL(const Value&, Args a) {
    if (a.size() > 0) openUrl(a[0].toString());
    return undefined;
}
Value mc_toString(const Value& self, Args) {
    DisplayObject* d = displayOf(self);
    return Value(d ? targetPath(d) : std::string());
}

// ---------------------------------------------------------------- global timeline functions

Value targetValue() {
    Activation& a = currentActivation();
    return a.target ? Value(a.target) : valueOf(stage().root.get());
}
Value g_stop(const Value&, Args a) { return mc_stop(targetValue(), a); }
Value g_play(const Value&, Args a) { return mc_play(targetValue(), a); }
Value g_gotoAndStop(const Value&, Args a) { return mc_gotoAndStop(targetValue(), a); }
Value g_gotoAndPlay(const Value&, Args a) { return mc_gotoAndPlay(targetValue(), a); }
Value g_nextFrame(const Value&, Args a) { return mc_nextFrame(targetValue(), a); }
Value g_prevFrame(const Value&, Args a) { return mc_prevFrame(targetValue(), a); }
Value g_removeMovieClip(const Value&, Args a) {
    Value t = a[0].isString() ? valueOf(resolvePath(displayOf(targetValue()), a[0].string())) : a[0];
    return mc_removeMovieClip(t, {});
}
Value g_targetPath(const Value&, Args a) {
    DisplayObject* d = displayOf(a[0]);
    return d ? Value(targetPath(d)) : undefined;
}
Value g_stopAllSounds(const Value&, Args) {
    g_soundHost->stopAll();
    return undefined;
}
// Used by TellTarget for string targets.
Value g_resolvePath(const Value&, Args a) {
    DisplayObject* base = displayOf(a[0]);
    if (!base) base = stage().root.get();
    DisplayObject* d = resolvePath(base, a[1].toString());
    return d ? valueOf(d) : undefined;
}

// ---------------------------------------------------------------- Color

DisplayObject* colorTarget(const Value& self) { return displayOf(getMember(self, internLiteral("__target"))); }

Value color_ctor(Function* fn, Args a) {
    Object* o = R().newObject();
    Value proto = getMember(Value(fn), internLiteral("prototype"));
    if (proto.isObject()) o->proto = proto.object();
    Value target = a[0];
    if (target.isString()) target = valueOf(resolvePath(displayOf(targetValue()), target.string()));
    o->defineSlot(internLiteral("__target"), target, DontEnum);
    return Value(o);
}
Value color_setRGB(const Value& self, Args a) {
    if (DisplayObject* d = colorTarget(self)) {
        uint32_t c = a[0].toUint32();
        d->cxform.rMul = d->cxform.gMul = d->cxform.bMul = 0;
        d->cxform.rAdd = (float)(c >> 16 & 255);
        d->cxform.gAdd = (float)(c >> 8 & 255);
        d->cxform.bAdd = (float)(c & 255);
        d->transformedByScript = true;
    }
    return undefined;
}
Value color_getRGB(const Value& self, Args) {
    DisplayObject* d = colorTarget(self);
    if (!d) return undefined;
    auto c = [](float v) { return (uint32_t)std::clamp(v, 0.f, 255.f); };
    return (double)(c(d->cxform.rAdd) << 16 | c(d->cxform.gAdd) << 8 | c(d->cxform.bAdd));
}
Value color_setTransform(const Value& self, Args a) {
    DisplayObject* d = colorTarget(self);
    if (!d || !a[0].isObject()) return undefined;
    // A value that isn't a number (NaN: Goomboss's hurt effect computes its tint from
    // its parent's undefined hp) leaves that channel as it is instead of zeroing it,
    // which turned the level black.
    auto apply = [&](const char* name, float& field, float scale) {
        Value v = getMember(a[0], intern(name));
        if (v.isUndefined()) return;
        double n = v.toNumber();
        if (std::isfinite(n)) field = (float)(n * scale);
    };
    apply("ra", d->cxform.rMul, 0.01f);
    apply("ga", d->cxform.gMul, 0.01f);
    apply("ba", d->cxform.bMul, 0.01f);
    apply("aa", d->cxform.aMul, 0.01f);
    apply("rb", d->cxform.rAdd, 1);
    apply("gb", d->cxform.gAdd, 1);
    apply("bb", d->cxform.bAdd, 1);
    apply("ab", d->cxform.aAdd, 1);
    d->transformedByScript = true;
    return undefined;
}
Value color_getTransform(const Value& self, Args) {
    DisplayObject* d = colorTarget(self);
    if (!d) return undefined;
    const auto& c = d->cxform;
    return obj({{"ra", c.rMul * 100.0}, {"ga", c.gMul * 100.0}, {"ba", c.bMul * 100.0}, {"aa", c.aMul * 100.0},
                {"rb", (double)c.rAdd}, {"gb", (double)c.gAdd}, {"bb", (double)c.bAdd}, {"ab", (double)c.aAdd}});
}

// ---------------------------------------------------------------- Sound

Value sound_ctor(Function* fn, Args a) {
    Object* o = R().newObject();
    Value proto = getMember(Value(fn), internLiteral("prototype"));
    if (proto.isObject()) o->proto = proto.object();
    o->defineSlot(internLiteral("__target"), a[0], DontEnum);
    o->defineSlot(internLiteral("__volume"), 100, DontEnum);
    o->defineSlot(internLiteral("__pan"), 0, DontEnum);
    o->defineSlot(internLiteral("__linkage"), Value(""), DontEnum);
    o->setOwn(internLiteral("duration"), 0);
    o->setOwn(internLiteral("position"), 0);
    return Value(o);
}
Value sound_attachSound(const Value& self, Args a) {
    Object* o = self.object();
    if (!o) return undefined;
    std::string linkage = a[0].toString();
    o->defineSlot(internLiteral("__linkage"), Value(linkage), DontEnum);
    if (stage().movie)
        if (auto* c = stage().movie->exported(linkage))
            if (c->kind == flash::Character::Sound)
                o->setOwn(internLiteral("duration"), static_cast<const flash::SoundCharacter*>(c)->durationMs());
    return undefined;
}
Value sound_start(const Value& self, Args a) {
    if (Object* o = self.object())
        g_soundHost->start(o, getMember(self, internLiteral("__linkage")).toString(),
                           a[0].isUndefined() ? 0 : a[0].toNumber(), a[1].isUndefined() ? 1 : std::max(1, a[1].toInt32()));
    return undefined;
}
Value sound_stop(const Value& self, Args a) {
    if (Object* o = self.object()) g_soundHost->stop(o, a[0].isUndefined() ? "" : a[0].toString());
    return undefined;
}
Value sound_setVolume(const Value& self, Args a) {
    if (Object* o = self.object()) {
        o->defineSlot(internLiteral("__volume"), a[0].toNumber(), DontEnum);
        g_soundHost->volumeChanged(o);
    }
    return undefined;
}
Value sound_getVolume(const Value& self, Args) { return getMember(self, internLiteral("__volume")); }
Value sound_setPan(const Value& self, Args a) {
    if (Object* o = self.object()) {
        o->defineSlot(internLiteral("__pan"), a[0].toNumber(), DontEnum);
        g_soundHost->volumeChanged(o);
    }
    return undefined;
}
Value sound_getPan(const Value& self, Args) { return getMember(self, internLiteral("__pan")); }
Value sound_setTransform(const Value& self, Args a) {
    if (Object* o = self.object()) o->defineSlot(internLiteral("__transform"), a[0], DontEnum);
    return undefined;
}
Value sound_getTransform(const Value& self, Args) {
    Value t = getMember(self, internLiteral("__transform"));
    return t.isObject() ? t : obj({{"ll", 100}, {"lr", 0}, {"rr", 100}, {"rl", 0}});
}

Object* makeProto(Runtime& rt, std::initializer_list<std::pair<const char*, Function::Native>> methods) {
    Object* p = rt.newObject();
    for (auto& [n, f] : methods) rt.method(p, n, f);
    return p;
}

}  // namespace

void installStageBuiltins(Runtime& rt) {
    Object* G = rt.global;
    g_movieClipProto = makeProto(rt, {
        {"gotoAndStop", mc_gotoAndStop}, {"gotoAndPlay", mc_gotoAndPlay}, {"play", mc_play},
        {"stop", mc_stop}, {"nextFrame", mc_nextFrame}, {"prevFrame", mc_prevFrame},
        {"attachMovie", mc_attachMovie}, {"createEmptyMovieClip", mc_createEmptyMovieClip},
        {"duplicateMovieClip", mc_duplicateMovieClip}, {"removeMovieClip", mc_removeMovieClip},
        {"swapDepths", mc_swapDepths}, {"getDepth", mc_getDepth},
        {"getNextHighestDepth", mc_getNextHighestDepth}, {"getInstanceAtDepth", mc_getInstanceAtDepth},
        {"hitTest", mc_hitTest}, {"getBounds", mc_getBounds}, {"getRect", mc_getBounds},
        {"localToGlobal", mc_localToGlobal}, {"globalToLocal", mc_globalToLocal},
        {"lineStyle", mc_lineStyle}, {"beginFill", mc_beginFill}, {"endFill", mc_endFill},
        {"moveTo", mc_moveTo}, {"lineTo", mc_lineTo}, {"curveTo", mc_curveTo}, {"clear", mc_clear},
        {"getBytesLoaded", mc_getBytes}, {"getBytesTotal", mc_getBytes}, {"loadMovie", mc_noop},
        {"unloadMovie", mc_noop}, {"getURL", mc_getURL}, {"startDrag", mc_noop}, {"stopDrag", mc_noop},
        {"setMask", mc_noop}, {"toString", mc_toString}});
    rt.defineClass(G, "MovieClip", nullptr, nullptr, g_movieClipProto);
    g_textFieldProto = makeProto(rt, {{"toString", mc_toString}, {"removeTextField", mc_removeMovieClip},
                                      {"getDepth", mc_getDepth}, {"swapDepths", mc_swapDepths}});
    rt.defineClass(G, "TextField", nullptr, nullptr, g_textFieldProto);
    g_buttonProto = makeProto(rt, {{"toString", mc_toString}, {"getDepth", mc_getDepth},
                                   {"swapDepths", mc_swapDepths}, {"hitTest", mc_hitTest},
                                   {"getBounds", mc_getBounds}});
    rt.defineClass(G, "Button", nullptr, nullptr, g_buttonProto);

    for (auto [n, f] : std::initializer_list<std::pair<const char*, Function::Native>>{
             {"stop", g_stop}, {"play", g_play}, {"gotoAndStop", g_gotoAndStop},
             {"gotoAndPlay", g_gotoAndPlay}, {"nextFrame", g_nextFrame}, {"prevFrame", g_prevFrame},
             {"removeMovieClip", g_removeMovieClip}, {"targetPath", g_targetPath},
             {"stopAllSounds", g_stopAllSounds}, {"__resolvePath", g_resolvePath}})
        rt.method(G, n, f);

    rt.defineClass(G, "Color", nullptr, color_ctor,
                   makeProto(rt, {{"setRGB", color_setRGB}, {"getRGB", color_getRGB},
                                  {"setTransform", color_setTransform}, {"getTransform", color_getTransform}}));
    rt.defineClass(G, "Sound", nullptr, sound_ctor,
                   makeProto(rt, {{"attachSound", sound_attachSound}, {"start", sound_start},
                                  {"stop", sound_stop}, {"setVolume", sound_setVolume},
                                  {"getVolume", sound_getVolume}, {"setPan", sound_setPan},
                                  {"getPan", sound_getPan}, {"setTransform", sound_setTransform},
                                  {"getTransform", sound_getTransform}}));

    Object* stageObj = rt.newObject();
    G->defineSlot(intern("Stage"), Value(stageObj), DontEnum);
    stageObj->setOwn(intern("width"), 450);
    stageObj->setOwn(intern("height"), 300);
    stageObj->setOwn(intern("scaleMode"), "showAll");
    stageObj->setOwn(intern("align"), "");
    stageObj->setOwn(intern("showMenu"), true);
    stageObj->defineSlot(internLiteral("_listeners"), Value(rt.newArray()), DontEnum);

    rt.rootProviders.push_back([](Gc& gc) {
        gc.mark(g_movieClipProto);
        gc.mark(g_textFieldProto);
        gc.mark(g_buttonProto);
    });
}

}  // namespace as
