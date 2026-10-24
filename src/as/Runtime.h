#pragma once
// The AS2 runtime: global object, built-in prototypes, activations (the executing
// script's `this`, scope chain and target timeline) and garbage collection.

#include <functional>
#include <string>
#include <vector>

#include "as/Object.h"

namespace as {

// One executing script or function call. Ported code receives it as `scope`:
//
//     scope["tall"] = 36;          // unqualified variable (scope chain)
//     scope.self["_x"] += 5;       // this._x += 5
class Activation {
public:
    Activation(Value self, Args args, Scope* scope, Object* target);
    ~Activation();
    Activation(const Activation&) = delete;

    Value self;     // `this`
    Args args;      // `arguments`
    Scope* scope;   // innermost scope (never null; ends at the global scope)
    Object* target; // timeline for stop()/play()/gotoAndStop() and new variables

    // Unqualified variable access through the scope chain.
    template <size_t N>
    Ref operator[](const char (&literal)[N]) { return Ref(this, internLiteral(literal)); }
    Ref operator[](const Value& name) { return Ref(this, name); }
    Ref operator[](Atom name) { return Ref(this, name); }

    Value getVariable(Atom name);
    // Returns the object the name was found on (for `this` of unqualified calls).
    Value getVariable(Atom name, Object*& foundOn);
    void setVariable(Atom name, const Value& v);

    // `var name`: the variable in the current variable scope — the target timeline in
    // frame scripts and event handlers, the activation object inside functions (used
    // for function locals that nested functions capture). Defines it if missing.
    template <size_t N>
    Ref var(const char (&literal)[N]) { return var(internLiteral(literal)); }
    Ref var(Atom name);
    bool deleteVariable(Atom name);
    template <size_t N>
    bool deleteVariable(const char (&literal)[N]) { return deleteVariable(internLiteral(literal)); }
    Value arguments() const;  // `arguments` as an Array

    // Defines a function closing over this activation's scope chain.
    Value function(Function::Body body, const char* name = "");

    bool isFunction = false;  // a function call (not a frame script or event handler)
    Value callee;             // the function being called (arguments.callee)
    Object* locals = nullptr;  // activation object for captured function locals

    Activation* prev;
};

// RAII scope pushes for `with (obj) {...}` and `tellTarget (path) {...}`.
class With {
public:
    With(Activation& a, const Value& obj);
    ~With();

private:
    Activation& a_;
    Scope* saved_;
};

class TellTarget {
public:
    TellTarget(Activation& a, const Value& target);  // clip or path string
    ~TellTarget();

private:
    Activation& a_;
    Scope* savedScope_;
    Object* savedTarget_;
};

class Runtime {
public:
    static Runtime& get();

    Heap heap;
    Object* global = nullptr;
    Scope* globalScope = nullptr;

    Object* objectProto = nullptr;
    Object* functionProto = nullptr;
    Object* arrayProto = nullptr;
    Object* stringProto = nullptr;
    Object* numberProto = nullptr;
    Object* booleanProto = nullptr;

    Activation* current = nullptr;  // innermost executing activation

    // Output for trace(); defaults to stdout.
    std::function<void(const std::string&)> traceOutput;
    // Extra GC roots supplied by the host (display list, timers, listeners...).
    std::vector<std::function<void(Gc&)>> rootProviders;

    // Creates basic objects.
    Object* newObject();
    Array* newArray(std::vector<Value> elements = {});
    Function* newNative(Function::Native fn, const char* name = "");
    // Adds a native method to `obj` (not enumerable).
    void method(Object* obj, const char* name, Function::Native fn);
    // Adds a native class: constructor function with a prototype object.
    Function* defineClass(Object* on, const char* name, Function::Native callFn,
                          Function::Constructor ctor, Object* proto);

    size_t collectGarbage();
    // Collects when enough objects were allocated since the last collection.
    void maybeCollect();

    double timeMs() const;  // ms since startup (getTimer)

private:
    Runtime();
};

// Object literal helper: obj({{"_x", 5}, {"_y", 7}})
Value obj(std::initializer_list<std::pair<const char*, Value>> props = {});
// Array literal helper: arr({1, 2, 3})
Value arr(std::initializer_list<Value> elements = {});

// Registers all built-in globals (Math, String, Array, Key, ...). Called once.
void installBuiltins(Runtime& rt);

// Current activation (for global functions such as stop() and trace()).
Activation& currentActivation();

}  // namespace as
