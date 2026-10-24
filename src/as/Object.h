#pragma once
// AS2 objects, functions, arrays, scopes and the mark-and-sweep heap.
//
// Collection only happens between frames (Runtime::collectGarbage), when no script
// code is on the C++ stack, so Values held in C++ locals never need to be rooted.

#include <functional>
#include <unordered_map>
#include <vector>

#include "as/Value.h"

namespace as {

class Gc;

enum PropFlags : uint8_t { DontEnum = 1, DontDelete = 2, ReadOnly = 4 };

class Object {
public:
    explicit Object(Object* proto = nullptr) : proto(proto) {}
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
    virtual ~Object() = default;

    Object* proto;  // __proto__

    // Own properties, including native ones (MovieClip._x, Array.length...).
    virtual bool getOwn(Atom key, Value& out);
    // Returns false only if the property is read-only.
    virtual bool setOwn(Atom key, const Value& v);
    virtual bool hasOwn(Atom key);
    virtual bool deleteOwn(Atom key);
    // Enumerable own keys, in for..in order.
    virtual void ownKeys(std::vector<Atom>& out);

    virtual bool isCallable() const { return false; }
    virtual Value call(const Value& self, Args args);
    virtual const char* typeName() const { return "object"; }
    // Fallback string when no toString method is found.
    virtual std::string defaultString() const { return "[object Object]"; }
    virtual void trace(Gc& gc);

    // Plain slot storage (no native hooks), used by subclasses.
    Value* findSlot(Atom key);
    void defineSlot(Atom key, const Value& v, uint8_t flags = 0);

    // GC bookkeeping
    Object* gcNext = nullptr;
    bool gcMark = false;

protected:
    struct Slot {
        Atom key;
        Value value;
        uint8_t flags;
    };
    int slotIndex(Atom key) const;
    std::vector<Slot> slots_;
    std::unordered_map<Atom, uint32_t> index_;  // built once there are many slots
};

// Property access along the prototype chain. Primitive bases use the String/Number/
// Boolean prototypes; strings also expose `length`.
Value getMember(const Value& base, Atom key);
Value getMember(const Value& base, const Value& key);
void setMember(const Value& base, Atom key, const Value& v);
void setMember(const Value& base, const Value& key, const Value& v);
bool hasProperty(Object* o, Atom key);  // own or inherited
bool deleteMember(const Value& base, const Value& key);

// ---------------------------------------------------------------- functions

class Scope;

class Function : public Object {
public:
    using Native = Value (*)(const Value& self, Args args);
    using Body = std::function<Value(Activation&)>;
    // Native constructors build the instance themselves (Array, Sound, Color...).
    using Constructor = Value (*)(Function* fn, Args args);

    explicit Function(Object* proto) : Object(proto) {}

    Native native = nullptr;
    Body body;               // script function
    Scope* closure = nullptr;  // defining scope chain
    Object* baseClip = nullptr;  // timeline the function was defined on
    Constructor constructor = nullptr;
    const char* name = "";

    bool isCallable() const override { return true; }
    Value call(const Value& self, Args args) override;
    const char* typeName() const override { return "function"; }
    std::string defaultString() const override { return "[type Function]"; }
    void trace(Gc& gc) override;
};

// `new fn(args)`
Value construct(const Value& fn, Args args);

// ---------------------------------------------------------------- arrays

class Array : public Object {
public:
    explicit Array(Object* proto) : Object(proto) {}
    std::vector<Value> elements;

    bool getOwn(Atom key, Value& out) override;
    bool setOwn(Atom key, const Value& v) override;
    bool hasOwn(Atom key) override;
    bool deleteOwn(Atom key) override;
    void ownKeys(std::vector<Atom>& out) override;
    std::string defaultString() const override;
    void trace(Gc& gc) override;
};

// ---------------------------------------------------------------- scope chain

class Scope : public Object {
public:
    enum Kind : uint8_t { Global, Target, With, Local };
    Scope(Kind kind, Object* obj, Scope* parent) : Object(nullptr), kind(kind), obj(obj), parent(parent) {}
    Kind kind;
    Object* obj;
    Scope* parent;
    void trace(Gc& gc) override;
};

// ---------------------------------------------------------------- heap

class Gc {
public:
    void mark(Object* o) {
        if (o && !o->gcMark) {
            o->gcMark = true;
            stack_.push_back(o);
        }
    }
    void mark(const Value& v) { mark(v.object()); }
    void drain() {
        while (!stack_.empty()) {
            Object* o = stack_.back();
            stack_.pop_back();
            o->trace(*this);
        }
    }

private:
    std::vector<Object*> stack_;
};

class Heap {
public:
    ~Heap();
    template <typename T, typename... A>
    T* make(A&&... args) {
        T* o = new T(std::forward<A>(args)...);
        o->gcNext = head_;
        head_ = o;
        count_++;
        allocatedSinceCollect_++;
        return o;
    }
    // Marks from `roots`, then frees everything unmarked. Returns objects freed.
    size_t collect(const std::function<void(Gc&)>& roots);
    size_t count() const { return count_; }
    size_t allocatedSinceCollect() const { return allocatedSinceCollect_; }

private:
    Object* head_ = nullptr;
    size_t count_ = 0;
    size_t allocatedSinceCollect_ = 0;
};

}  // namespace as
