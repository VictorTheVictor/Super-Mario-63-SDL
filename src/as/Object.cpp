#include "as/Object.h"

#include <algorithm>
#include <cmath>

#include "as/Diagnostics.h"
#include "as/Runtime.h"

namespace as {

// ---------------------------------------------------------------- Object

int Object::slotIndex(Atom key) const {
    if (!index_.empty()) {
        auto it = index_.find(key);
        return it == index_.end() ? -1 : (int)it->second;
    }
    for (size_t i = 0; i < slots_.size(); i++)
        if (slots_[i].key == key) return (int)i;
    return -1;
}

Value* Object::findSlot(Atom key) {
    int i = slotIndex(key);
    return i < 0 ? nullptr : &slots_[i].value;
}

void Object::defineSlot(Atom key, const Value& v, uint8_t flags) {
    int i = slotIndex(key);
    if (i >= 0) {
        slots_[i].value = v;
        slots_[i].flags = flags;
        return;
    }
    slots_.push_back({key, v, flags});
    if (!index_.empty()) index_.emplace(key, (uint32_t)slots_.size() - 1);
    else if (slots_.size() > 8)
        for (size_t k = 0; k < slots_.size(); k++) index_.emplace(slots_[k].key, (uint32_t)k);
}

bool Object::getOwn(Atom key, Value& out) {
    int i = slotIndex(key);
    if (i < 0) return false;
    out = slots_[i].value;
    return true;
}

bool Object::setOwn(Atom key, const Value& v) {
    int i = slotIndex(key);
    if (i >= 0) {
        if (slots_[i].flags & ReadOnly) return false;
        slots_[i].value = v;
        return true;
    }
    defineSlot(key, v);
    return true;
}

bool Object::hasOwn(Atom key) { return slotIndex(key) >= 0; }

bool Object::deleteOwn(Atom key) {
    int i = slotIndex(key);
    if (i < 0 || (slots_[i].flags & DontDelete)) return false;
    slots_.erase(slots_.begin() + i);
    if (!index_.empty()) {
        index_.clear();
        if (slots_.size() > 8)
            for (size_t k = 0; k < slots_.size(); k++) index_.emplace(slots_[k].key, (uint32_t)k);
    }
    return true;
}

void Object::ownKeys(std::vector<Atom>& out) {
    // AVM1 enumerates most recently added properties first.
    for (size_t i = slots_.size(); i-- > 0;)
        if (!(slots_[i].flags & DontEnum)) out.push_back(slots_[i].key);
}

Value Object::call(const Value&, Args) { return undefined; }

void Object::trace(Gc& gc) {
    gc.mark(proto);
    for (auto& s : slots_) gc.mark(s.value);
}

// ---------------------------------------------------------------- member access

namespace {

size_t utf16Length(const std::string& s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size(); i++) {
        uint8_t c = (uint8_t)s[i];
        if ((c & 0xC0) != 0x80) n += (c >= 0xF0) ? 2 : 1;
    }
    return n;
}

Object* protoForPrimitive(const Value& v) {
    Runtime& rt = Runtime::get();
    switch (v.type()) {
        case Type::String: return rt.stringProto;
        case Type::Number: return rt.numberProto;
        case Type::Boolean: return rt.booleanProto;
        default: return nullptr;
    }
}

}  // namespace

Value getMember(const Value& base, Atom key) {
    Object* o = base.object();
    if (!o) {
        if (base.isNullish() && diagnostics().enabled) diagnostics().undefinedBase["." + key->str]++;
        if (base.isString() && key == internLiteral("length"))
            return (double)utf16Length(base.string());
        o = protoForPrimitive(base);
        if (!o) return undefined;
    } else if (key == internLiteral("__proto__")) {
        return Value(o->proto ? Value(o->proto) : Value::null());
    }
    Value out;
    for (int depth = 0; o && depth < 256; depth++, o = o->proto)
        if (o->getOwn(key, out)) return out;
    return undefined;
}

Value getMember(const Value& base, const Value& key) {
    if (key.isNumber()) {
        if (auto* a = dynamic_cast<Array*>(base.object())) {
            double d = key.number();
            if (d >= 0 && d == std::floor(d)) {
                size_t i = (size_t)d;
                return i < a->elements.size() ? a->elements[i] : undefined;
            }
        }
    }
    return getMember(base, key.toAtom());
}

void setMember(const Value& base, Atom key, const Value& v) {
    Object* o = base.object();
    if (!o) return;  // writes to primitives are ignored
    if (key == internLiteral("__proto__")) {
        o->proto = v.object();
        return;
    }
    o->setOwn(key, v);
}

void setMember(const Value& base, const Value& key, const Value& v) {
    if (key.isNumber()) {
        if (auto* a = dynamic_cast<Array*>(base.object())) {
            double d = key.number();
            if (d >= 0 && d == std::floor(d) && d < 1e9) {
                size_t i = (size_t)d;
                if (i >= a->elements.size()) a->elements.resize(i + 1);
                a->elements[i] = v;
                return;
            }
        }
    }
    setMember(base, key.toAtom(), v);
}

bool hasProperty(Object* o, Atom key) {
    for (int depth = 0; o && depth < 256; depth++, o = o->proto)
        if (o->hasOwn(key)) return true;
    return false;
}

bool deleteMember(const Value& base, const Value& key) {
    Object* o = base.object();
    return o && o->deleteOwn(key.toAtom());
}

// ---------------------------------------------------------------- Function

Value Function::call(const Value& self, Args args) {
    if (native) return native(self, args);
    if (!body) return undefined;
    Runtime& rt = Runtime::get();
    Activation act(self, args, closure ? closure : rt.globalScope, baseClip);
    act.isFunction = true;
    act.callee = Value(this);
    return body(act);
}

void Function::trace(Gc& gc) {
    Object::trace(gc);
    gc.mark(closure);
    gc.mark(baseClip);
}

Value callFunction(const Value& fn, const Value& self, Args args) {
    Object* o = fn.object();
    if (!o || !o->isCallable()) return undefined;
    return o->call(self, args);
}

Value construct(const Value& fn, Args args) {
    Runtime& rt = Runtime::get();
    auto* f = dynamic_cast<Function*>(fn.object());
    if (!f) return undefined;
    if (f->constructor) {
        Value made = f->constructor(f, args);
        if (Object* o = made.object())
            if (!o->hasOwn(internLiteral("__constructor__")))
                o->defineSlot(internLiteral("__constructor__"), fn, DontEnum);
        return made;
    }
    Object* instance = rt.newObject();
    Value proto = getMember(fn, internLiteral("prototype"));
    if (proto.isObject()) instance->proto = proto.object();
    // AVM1 records the constructor on the instance (used by clone-style helpers).
    instance->defineSlot(internLiteral("__constructor__"), fn, DontEnum);
    Value result = f->call(Value(instance), args);
    return result.isObject() ? result : Value(instance);
}

// ---------------------------------------------------------------- Array

bool Array::getOwn(Atom key, Value& out) {
    if (key->index >= 0) {
        if ((size_t)key->index >= elements.size()) return false;
        out = elements[key->index];
        return true;
    }
    if (key == internLiteral("length")) {
        out = (double)elements.size();
        return true;
    }
    return Object::getOwn(key, out);
}

bool Array::setOwn(Atom key, const Value& v) {
    if (key->index >= 0 && key->index < 1000000000) {
        if ((size_t)key->index >= elements.size()) elements.resize(key->index + 1);
        elements[key->index] = v;
        return true;
    }
    if (key == internLiteral("length")) {
        double n = v.toNumber();
        if (n >= 0 && n < 1e9) elements.resize((size_t)n);
        return true;
    }
    return Object::setOwn(key, v);
}

bool Array::hasOwn(Atom key) {
    if (key->index >= 0) return (size_t)key->index < elements.size();
    if (key == internLiteral("length")) return true;
    return Object::hasOwn(key);
}

bool Array::deleteOwn(Atom key) {
    if (key->index >= 0) {
        if ((size_t)key->index < elements.size()) elements[key->index] = undefined;
        return true;
    }
    return Object::deleteOwn(key);
}

void Array::ownKeys(std::vector<Atom>& out) {
    Object::ownKeys(out);
    for (size_t i = elements.size(); i-- > 0;) out.push_back(intern(std::to_string(i)));
}

std::string Array::defaultString() const {
    std::string s;
    for (size_t i = 0; i < elements.size(); i++) {
        if (i) s += ",";
        if (!elements[i].isNullish()) s += elements[i].toString();
    }
    return s;
}

void Array::trace(Gc& gc) {
    Object::trace(gc);
    for (auto& v : elements) gc.mark(v);
}

// ---------------------------------------------------------------- Scope

void Scope::trace(Gc& gc) {
    gc.mark(obj);
    gc.mark(parent);
}

// ---------------------------------------------------------------- Heap

Heap::~Heap() {
    while (head_) {
        Object* next = head_->gcNext;
        delete head_;
        head_ = next;
    }
}

size_t Heap::collect(const std::function<void(Gc&)>& roots) {
    Gc gc;
    roots(gc);
    gc.drain();
    size_t freed = 0;
    Object** link = &head_;
    while (*link) {
        Object* o = *link;
        if (o->gcMark) {
            o->gcMark = false;
            link = &o->gcNext;
        } else {
            *link = o->gcNext;
            delete o;
            freed++;
        }
    }
    count_ -= freed;
    allocatedSinceCollect_ = 0;
    return freed;
}

}  // namespace as
