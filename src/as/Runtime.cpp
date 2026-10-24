#include "as/Runtime.h"

#include <cctype>
#include <cstdlib>
#include <chrono>
#include <cstdio>

#include "as/Diagnostics.h"

namespace as {

// ---------------------------------------------------------------- Runtime

Runtime& Runtime::get() {
    // Builtin installation calls back into get(), so publish the instance first.
    static Runtime* rt = nullptr;
    if (!rt) {
        rt = new Runtime();
        installBuiltins(*rt);
    }
    return *rt;
}

namespace {
const auto kStart = std::chrono::steady_clock::now();
}

Runtime::Runtime() {
    objectProto = heap.make<Object>(nullptr);
    functionProto = heap.make<Object>(objectProto);
    arrayProto = heap.make<Object>(objectProto);
    stringProto = heap.make<Object>(objectProto);
    numberProto = heap.make<Object>(objectProto);
    booleanProto = heap.make<Object>(objectProto);
    global = heap.make<Object>(objectProto);
    globalScope = heap.make<Scope>(Scope::Global, global, nullptr);
    traceOutput = [](const std::string& s) { std::printf("%s\n", s.c_str()); };
}

double Runtime::timeMs() const {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - kStart).count();
}

Object* Runtime::newObject() { return heap.make<Object>(objectProto); }

Array* Runtime::newArray(std::vector<Value> elements) {
    auto* a = heap.make<Array>(arrayProto);
    a->elements = std::move(elements);
    return a;
}

Function* Runtime::newNative(Function::Native fn, const char* name) {
    auto* f = heap.make<Function>(functionProto);
    f->native = fn;
    f->name = name;
    return f;
}

void Runtime::method(Object* obj, const char* name, Function::Native fn) {
    obj->defineSlot(internLiteral(name), Value(newNative(fn, name)), DontEnum);
}

Function* Runtime::defineClass(Object* on, const char* name, Function::Native callFn,
                               Function::Constructor ctor, Object* proto) {
    Function* f = newNative(callFn ? callFn : [](const Value&, Args) { return undefined; }, name);
    f->constructor = ctor;
    f->defineSlot(internLiteral("prototype"), Value(proto), DontEnum | DontDelete);
    proto->defineSlot(internLiteral("constructor"), Value(f), DontEnum);
    on->defineSlot(intern(name), Value(f), DontEnum);
    return f;
}

size_t Runtime::collectGarbage() {
    return heap.collect([this](Gc& gc) {
        gc.mark(objectProto);
        gc.mark(functionProto);
        gc.mark(arrayProto);
        gc.mark(stringProto);
        gc.mark(numberProto);
        gc.mark(booleanProto);
        gc.mark(global);
        gc.mark(globalScope);
        for (Activation* a = current; a; a = a->prev) {
            gc.mark(a->locals);
            gc.mark(a->callee);
            gc.mark(a->self);
            gc.mark(a->scope);
            gc.mark(a->target);
            for (size_t i = 0; i < a->args.n; i++) gc.mark(a->args.v[i]);
        }
        for (auto& p : rootProviders) p(gc);
    });
}

void Runtime::maybeCollect() {
    if (heap.allocatedSinceCollect() > std::max<size_t>(20000, heap.count() / 2)) collectGarbage();
}

// ---------------------------------------------------------------- Activation

Activation::Activation(Value self, Args args, Scope* scope, Object* target)
    : self(std::move(self)), args(args), scope(scope), target(target) {
    Runtime& rt = Runtime::get();
    prev = rt.current;
    rt.current = this;
}

Activation::~Activation() { Runtime::get().current = prev; }

namespace {

// Names resolved before the scope chain.
bool specialVariable(Activation& a, Atom name, Value& out) {
    Runtime& rt = Runtime::get();
    if (name == internLiteral("_global")) {
        out = Value(rt.global);
        return true;
    }
    if (name == internLiteral("this")) {
        out = a.self;
        return true;
    }
    if (name == internLiteral("_root") || name == internLiteral("_level0")) {
        out = getMember(Value(rt.global), internLiteral("_level0"));
        return true;
    }
    return false;
}

}  // namespace

Value Activation::getVariable(Atom name) {
    Object* found;
    return getVariable(name, found);
}

Value Activation::getVariable(Atom name, Object*& foundOn) {
    foundOn = nullptr;
    Value out;
    if (specialVariable(*this, name, out)) return out;
    for (Scope* s = scope; s; s = s->parent) {
        if (s->obj && hasProperty(s->obj, name)) {
            if (s->kind == Scope::Target || s->kind == Scope::With) foundOn = s->obj;
            return getMember(Value(s->obj), name);
        }
    }
    return undefined;
}

void Activation::setVariable(Atom name, const Value& v) {
    for (Scope* s = scope; s; s = s->parent) {
        if (!s->obj) continue;
        // New variables are created on the target timeline (or global as a last resort).
        if (s->kind == Scope::Target || !s->parent || hasProperty(s->obj, name)) {
            setMember(Value(s->obj), name, v);
            return;
        }
    }
}

Ref Activation::var(Atom name) {
    Runtime& rt = Runtime::get();
    Object* holder;
    if (isFunction) {
        if (!locals) {
            locals = rt.heap.make<Object>(nullptr);
            scope = rt.heap.make<Scope>(Scope::Local, locals, scope);
        }
        holder = locals;
    } else {
        holder = target ? target : rt.global;
    }
    if (!holder->hasOwn(name)) holder->setOwn(name, undefined);
    return Ref(Value(holder), name);
}

bool Activation::deleteVariable(Atom name) {
    for (Scope* s = scope; s; s = s->parent)
        if (s->obj && s->obj->hasOwn(name)) return s->obj->deleteOwn(name);
    return false;
}

Value Activation::arguments() const {
    Array* a = Runtime::get().newArray(std::vector<Value>(args.v, args.v + args.n));
    a->defineSlot(internLiteral("callee"), callee, DontEnum);
    return Value(a);
}

Value Activation::function(Function::Body body, const char* name) {
    Runtime& rt = Runtime::get();
    auto* f = rt.heap.make<Function>(rt.functionProto);
    f->body = std::move(body);
    f->closure = scope;
    f->baseClip = target;
    f->name = name;
    Object* proto = rt.newObject();
    proto->defineSlot(internLiteral("constructor"), Value(f), DontEnum);
    f->defineSlot(internLiteral("prototype"), Value(proto), DontEnum);
    return Value(f);
}

With::With(Activation& a, const Value& obj) : a_(a), saved_(a.scope) {
    if (Object* o = obj.object()) a.scope = Runtime::get().heap.make<Scope>(Scope::With, o, a.scope);
}

With::~With() { a_.scope = saved_; }

TellTarget::TellTarget(Activation& a, const Value& target)
    : a_(a), savedScope_(a.scope), savedTarget_(a.target) {
    Object* clip = target.object();
    if (!clip && target.isString()) {
        // Resolve "path" relative to the current target via the display list host.
        Value resolved = getMember(Value(Runtime::get().global), internLiteral("__resolvePath"));
        if (resolved.isObject()) {
            Value argv[2] = {Value(a.target), target};
            clip = callFunction(resolved, undefined, Args{argv, 2}).object();
        }
    }
    if (!clip) return;
    a.target = clip;
    // Replace the innermost Target scope with the new clip, keeping scopes above it.
    std::vector<Scope*> above;
    Scope* s = a.scope;
    while (s && s->kind != Scope::Target) {
        above.push_back(s);
        s = s->parent;
    }
    Runtime& rt = Runtime::get();
    Scope* chain = rt.heap.make<Scope>(Scope::Target, clip, s ? s->parent : rt.globalScope);
    for (size_t i = above.size(); i-- > 0;)
        chain = rt.heap.make<Scope>(above[i]->kind, above[i]->obj, chain);
    a.scope = chain;
}

TellTarget::~TellTarget() {
    a_.scope = savedScope_;
    a_.target = savedTarget_;
}

Activation& currentActivation() {
    Runtime& rt = Runtime::get();
    if (rt.current) return *rt.current;
    static Activation* top = new Activation(undefined, {}, rt.globalScope, nullptr);
    rt.current = top;  // keep it as the outermost activation
    top->prev = nullptr;
    return *top;
}

// ---------------------------------------------------------------- Ref

Atom Ref::atom() const { return atom_ ? atom_ : key_.toAtom(); }

Value Ref::get() const {
    if (act_) return act_->getVariable(atom());
    return atom_ ? getMember(base_, atom_) : getMember(base_, key_);
}

void Ref::set(const Value& v) const {
    if (act_) act_->setVariable(atom(), v);
    else if (atom_) setMember(base_, atom_, v);
    else setMember(base_, key_, v);
}

Value Ref::call(Args args) const {
    if (act_) {
        Object* foundOn = nullptr;
        Value fn = act_->getVariable(atom(), foundOn);
        Value self = foundOn ? Value(foundOn) : Value(act_->target);
        if (diagnostics().enabled && !(fn.object() && fn.object()->isCallable())) {
            // Name the calling timeline, with auto-generated instance names collapsed.
            std::string where = act_->target ? act_->target->defaultString() : "?";
            for (size_t p; std::getenv("SM63_DIAG_RAW") == nullptr && (p = where.find("instance")) != std::string::npos;) {
                size_t e = p + 8;
                while (e < where.size() && std::isdigit((unsigned char)where[e])) e++;
                where.replace(p, e - p, "#");
            }
            // Is the function reachable elsewhere (a parent timeline, or _root)?
            std::string found;
            Value self(act_->target);
            Value parent = getMember(self, internLiteral("_parent"));
            auto callable = [&](const Value& o) {
                Value f = getMember(o, atom());
                return f.object() && f.object()->isCallable();
            };
            if (parent.isObject() && callable(parent)) found = " [exists on _parent]";
            else if (callable(getMember(Value(Runtime::get().global), internLiteral("_level0")))) found = " [exists on _root]";
            diagnostics().uncallable[atom()->str + "() in " + where + found]++;
        }
        return callFunction(fn, self, args);
    }
    Value fn = atom_ ? getMember(base_, atom_) : getMember(base_, key_);
    if (diagnostics().enabled && !(fn.object() && fn.object()->isCallable()))
        diagnostics().uncallable[(base_.isNullish() ? std::string("<undefined>") : "<" + std::string(base_.typeOf()) + ">") +
                                 "." + atom()->str + "()"]++;
    return callFunction(fn, base_, args);
}

// ---------------------------------------------------------------- literals

Value obj(std::initializer_list<std::pair<const char*, Value>> props) {
    Object* o = Runtime::get().newObject();
    for (auto& [k, v] : props) o->setOwn(intern(k), v);
    return Value(o);
}

Value arr(std::initializer_list<Value> elements) {
    return Value(Runtime::get().newArray(std::vector<Value>(elements)));
}

}  // namespace as
