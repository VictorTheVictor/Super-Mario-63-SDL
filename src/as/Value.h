#pragma once
// ActionScript 2 values (AVM1 semantics for SWF 7+, which is case-sensitive).
//
// Ported game code uses these types directly, so they are written to read like AS2:
//
//     _root["gravity"] = _root[a + "Gravity"];
//     if (scope["attack"] == true) scope["tall"] = 25;
//     _root["StartChar"](a, b, c);

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

namespace as {

class Object;
class Activation;
class Ref;

// ---------------------------------------------------------------- atoms

// Interned property name; pointers are stable for the program's lifetime.
struct AtomData {
    std::string str;
    size_t hash;
    int64_t index;  // >= 0 when the name is a canonical array index ("0", "17")
};
using Atom = const AtomData*;

Atom intern(std::string_view s);
// Interns a string literal, caching by address. Only pass string literals.
Atom internLiteral(const char* literal);

// ---------------------------------------------------------------- values

enum class Type : uint8_t { Undefined, Null, Boolean, Number, String, Object };

struct StrRep {
    int refs;
    std::string str;
};

class Value {
public:
    Value() : type_(Type::Undefined) { u_.n = 0; }
    Value(std::nullptr_t) : type_(Type::Null) { u_.n = 0; }
    Value(bool b) : type_(Type::Boolean) { u_.b = b; }
    Value(double n) : type_(Type::Number) { u_.n = n; }
    Value(float n) : Value((double)n) {}
    Value(int n) : Value((double)n) {}
    Value(unsigned n) : Value((double)n) {}
    Value(long n) : Value((double)n) {}
    Value(unsigned long n) : Value((double)n) {}
    Value(long long n) : Value((double)n) {}
    Value(unsigned long long n) : Value((double)n) {}
    Value(const char* s) : Value(std::string_view(s)) {}
    Value(std::string_view s);
    Value(const std::string& s) : Value(std::string_view(s)) {}
    Value(Atom a) : Value(std::string_view(a->str)) {}
    Value(Object* o) : type_(o ? Type::Object : Type::Undefined) { u_.o = o; }
    Value(const Ref& r);

    Value(const Value& v) : type_(v.type_), u_(v.u_) {
        if (type_ == Type::String) u_.s->refs++;
    }
    Value(Value&& v) noexcept : type_(v.type_), u_(v.u_) { v.type_ = Type::Undefined; }
    Value& operator=(const Value& v) {
        if (this != &v) {
            if (v.type_ == Type::String) v.u_.s->refs++;
            release();
            type_ = v.type_;
            u_ = v.u_;
        }
        return *this;
    }
    Value& operator=(Value&& v) noexcept {
        if (this != &v) {
            release();
            type_ = v.type_;
            u_ = v.u_;
            v.type_ = Type::Undefined;
        }
        return *this;
    }
    ~Value() { release(); }

    static Value null() { return Value(nullptr); }

    Type type() const { return type_; }
    bool isUndefined() const { return type_ == Type::Undefined; }
    bool isNull() const { return type_ == Type::Null; }
    bool isNullish() const { return type_ <= Type::Null; }
    bool isBoolean() const { return type_ == Type::Boolean; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isObject() const { return type_ == Type::Object; }

    // Raw accessors (no conversion).
    bool boolean() const { return u_.b; }
    double number() const { return u_.n; }
    const std::string& string() const { return u_.s->str; }
    Object* object() const { return type_ == Type::Object ? u_.o : nullptr; }

    // AVM1 conversions.
    double toNumber() const;
    std::string toString() const;
    bool toBoolean() const;
    int32_t toInt32() const;
    uint32_t toUint32() const;
    Value toPrimitive() const;  // valueOf()/toString() for objects
    Atom toAtom() const;
    const char* typeOf() const;  // "undefined", "number", "movieclip", ...

    explicit operator bool() const { return toBoolean(); }

    // Member access: v["name"], v[index], v[expr]
    template <size_t N>
    Ref operator[](const char (&literal)[N]) const;
    Ref operator[](const Value& key) const;
    Ref operator[](int index) const;
    Ref operator[](double index) const;
    Ref operator[](Atom key) const;

    // Calls this value as a function with `this` = undefined.
    template <typename... A>
    Value operator()(A&&... args) const;

    // Compound assignment and increments operate on the value in place.
    Value& operator+=(const Value& v);
    Value& operator-=(const Value& v);
    Value& operator*=(const Value& v);
    Value& operator/=(const Value& v);
    Value& operator%=(const Value& v);
    Value& operator++();
    Value operator++(int);
    Value& operator--();
    Value operator--(int);

private:
    void release() {
        if (type_ == Type::String && --u_.s->refs == 0) delete u_.s;
    }
    Type type_;
    union {
        bool b;
        double n;
        StrRep* s;
        Object* o;
    } u_;
};

// Shared undefined, handy for references.
extern const Value undefined;

// ---------------------------------------------------------------- operators

// Arithmetic follows ActionAdd2/Subtract/etc.: `+` concatenates when either side is a
// string after ToPrimitive. Results are always Values.
Value operator+(const Value& a, const Value& b);
Value operator-(const Value& a, const Value& b);
Value operator*(const Value& a, const Value& b);
Value operator/(const Value& a, const Value& b);
Value operator%(const Value& a, const Value& b);
Value operator-(const Value& a);
Value operator+(const Value& a);
Value operator!(const Value& a);
Value operator~(const Value& a);
Value operator&(const Value& a, const Value& b);
Value operator|(const Value& a, const Value& b);
Value operator^(const Value& a, const Value& b);
Value operator<<(const Value& a, const Value& b);
Value operator>>(const Value& a, const Value& b);

// Abstract (==) and relational comparisons, ECMA-262 3rd edition.
bool operator==(const Value& a, const Value& b);
bool operator!=(const Value& a, const Value& b);
bool operator<(const Value& a, const Value& b);
bool operator>(const Value& a, const Value& b);
bool operator<=(const Value& a, const Value& b);
bool operator>=(const Value& a, const Value& b);

bool strictEquals(const Value& a, const Value& b);  // ===
inline bool strictNotEquals(const Value& a, const Value& b) { return !strictEquals(a, b); }

// Number <-> string conversions matching Flash's formatting.
std::string numberToString(double n);
double stringToNumber(std::string_view s);

// ---------------------------------------------------------------- argument lists

struct Args {
    const Value* v = nullptr;
    size_t n = 0;
    const Value& operator[](size_t i) const { return i < n ? v[i] : undefined; }
    size_t size() const { return n; }
};

// ---------------------------------------------------------------- member reference

// A property (or variable) reference that reads on conversion and writes on assignment.
class Ref {
public:
    Ref(Value base, Atom key) : base_(std::move(base)), atom_(key) {}
    Ref(Value base, Value key) : base_(std::move(base)), key_(std::move(key)) {}
    Ref(Activation* act, Atom key) : act_(act), atom_(key) {}
    Ref(Activation* act, Value key) : act_(act), key_(std::move(key)) {}
    Ref(const Ref&) = default;

    // Converts to Value through Value(const Ref&).
    Value get() const;
    void set(const Value& v) const;
    explicit operator bool() const { return get().toBoolean(); }

    Ref& operator=(const Value& v) {
        set(v);
        return *this;
    }
    Ref& operator=(const Ref& r) {
        set(r.get());
        return *this;
    }

    template <size_t N>
    Ref operator[](const char (&literal)[N]) const { return get()[literal]; }
    Ref operator[](const Value& key) const { return get()[key]; }
    Ref operator[](int index) const { return get()[index]; }
    Ref operator[](double index) const { return get()[index]; }
    Ref operator[](Atom key) const { return get()[key]; }

    // Method call: `this` is the base object (or the resolving scope object).
    template <typename... A>
    Value operator()(A&&... args) const;
    Value call(Args args) const;

    Ref& operator+=(const Value& v) { return *this = get() + v; }
    Ref& operator-=(const Value& v) { return *this = get() - v; }
    Ref& operator*=(const Value& v) { return *this = get() * v; }
    Ref& operator/=(const Value& v) { return *this = get() / v; }
    Ref& operator%=(const Value& v) { return *this = get() % v; }
    Ref& operator++() { return *this = get().toNumber() + 1; }
    Value operator++(int) {
        Value old = get().toNumber();
        set(old.number() + 1);
        return old;
    }
    Ref& operator--() { return *this = get().toNumber() - 1; }
    Value operator--(int) {
        Value old = get().toNumber();
        set(old.number() - 1);
        return old;
    }

    Atom atom() const;

private:
    Value base_;
    Activation* act_ = nullptr;
    Atom atom_ = nullptr;
    Value key_;
};

inline Value operator!(const Ref& r) { return !r.get(); }

// ---------------------------------------------------------------- calls

// Calls `fn` with an explicit `this`.
Value callFunction(const Value& fn, const Value& self, Args args);

template <typename... A>
Value Value::operator()(A&&... args) const {
    Value argv[sizeof...(A) + 1] = {Value(std::forward<A>(args))...};
    return callFunction(*this, undefined, Args{argv, sizeof...(A)});
}

template <typename... A>
Value Ref::operator()(A&&... args) const {
    Value argv[sizeof...(A) + 1] = {Value(std::forward<A>(args))...};
    return call(Args{argv, sizeof...(A)});
}

template <size_t N>
Ref Value::operator[](const char (&literal)[N]) const {
    return Ref(*this, internLiteral(literal));
}

}  // namespace as
