#include "as/Value.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <unordered_map>

#include "as/Object.h"
#include "as/Runtime.h"

namespace as {

const Value undefined;

// ---------------------------------------------------------------- atoms

namespace {

std::unordered_map<std::string_view, AtomData*>& internTable() {
    static std::unordered_map<std::string_view, AtomData*> t(4096);
    return t;
}

int64_t canonicalIndex(std::string_view s) {
    if (s.empty() || s.size() > 15) return -1;
    if (s.size() > 1 && s[0] == '0') return -1;
    int64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return -1;
        v = v * 10 + (c - '0');
    }
    return v;
}

}  // namespace

Atom intern(std::string_view s) {
    auto& t = internTable();
    auto it = t.find(s);
    if (it != t.end()) return it->second;
    auto* a = new AtomData{std::string(s), std::hash<std::string_view>()(s), canonicalIndex(s)};
    t.emplace(std::string_view(a->str), a);
    return a;
}

Atom internLiteral(const char* literal) {
    static std::unordered_map<const char*, Atom> cache(4096);
    auto it = cache.find(literal);
    if (it != cache.end()) return it->second;
    Atom a = intern(literal);
    cache.emplace(literal, a);
    return a;
}

// ---------------------------------------------------------------- construction

Value::Value(std::string_view s) : type_(Type::String) { u_.s = new StrRep{1, std::string(s)}; }

Value::Value(const Ref& r) : Value(r.get()) {}

// ---------------------------------------------------------------- conversions

std::string numberToString(double n) {
    if (std::isnan(n)) return "NaN";
    if (std::isinf(n)) return n > 0 ? "Infinity" : "-Infinity";
    if (n == 0) return "0";
    if (std::fabs(n) < 1e15 && n == std::trunc(n)) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.0f", n);
        return buf;
    }
    // 15 significant digits, like Flash Player.
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.14e", n);
    // buf = "-d.dddddddddddddde+XX"
    std::string s(buf);
    bool neg = s[0] == '-';
    if (neg) s.erase(0, 1);
    size_t e = s.find('e');
    int exp = std::atoi(s.c_str() + e + 1);
    std::string digits = s.substr(0, 1) + s.substr(2, e - 2);
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
    std::string out;
    if (exp >= 15 || exp < -5) {
        out = digits.substr(0, 1);
        if (digits.size() > 1) out += "." + digits.substr(1);
        out += exp < 0 ? "e-" : "e+";
        out += std::to_string(std::abs(exp));
    } else if (exp < 0) {
        out = "0." + std::string(-exp - 1, '0') + digits;
    } else {
        if ((int)digits.size() <= exp + 1) {
            out = digits + std::string(exp + 1 - digits.size(), '0');
        } else {
            out = digits.substr(0, exp + 1) + "." + digits.substr(exp + 1);
        }
    }
    return neg ? "-" + out : out;
}

double stringToNumber(std::string_view s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
    s = s.substr(i);
    if (s.empty()) return std::numeric_limits<double>::quiet_NaN();
    bool neg = false;
    std::string_view body = s;
    if (body[0] == '-' || body[0] == '+') {
        neg = body[0] == '-';
        body = body.substr(1);
    }
    if (body.size() > 2 && body[0] == '0' && (body[1] == 'x' || body[1] == 'X')) {
        double v = 0;
        for (char c : body.substr(2)) {
            int d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else return std::numeric_limits<double>::quiet_NaN();
            v = v * 16 + d;
        }
        return neg ? -v : v;
    }
    if (body == "Infinity") return neg ? -INFINITY : INFINITY;
    std::string tmp(s);
    char* end = nullptr;
    double v = std::strtod(tmp.c_str(), &end);
    if (end == tmp.c_str()) return std::numeric_limits<double>::quiet_NaN();
    // Reject inf/nan spellings strtod accepts but Flash does not.
    for (const char* p = tmp.c_str(); p < end; p++)
        if ((*p >= 'a' && *p <= 'z' && *p != 'e') || (*p >= 'A' && *p <= 'Z' && *p != 'E'))
            return std::numeric_limits<double>::quiet_NaN();
    while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') end++;
    if (*end) return std::numeric_limits<double>::quiet_NaN();
    return v;
}

Value Value::toPrimitive() const {
    Object* o = object();
    if (!o) return *this;
    Value valueOf = getMember(*this, internLiteral("valueOf"));
    if (valueOf.isObject() && valueOf.object()->isCallable()) {
        Value v = valueOf.object()->call(*this, {});
        if (!v.isObject()) return v;
    }
    Value toStr = getMember(*this, internLiteral("toString"));
    if (toStr.isObject() && toStr.object()->isCallable()) {
        Value v = toStr.object()->call(*this, {});
        if (!v.isObject()) return v;
    }
    return Value(o->defaultString());
}

double Value::toNumber() const {
    switch (type_) {
        case Type::Undefined:
        case Type::Null: return std::numeric_limits<double>::quiet_NaN();
        case Type::Boolean: return u_.b ? 1 : 0;
        case Type::Number: return u_.n;
        case Type::String: return stringToNumber(u_.s->str);
        case Type::Object: {
            Value p = toPrimitive();
            return p.isObject() ? std::numeric_limits<double>::quiet_NaN() : p.toNumber();
        }
    }
    return 0;
}

std::string Value::toString() const {
    switch (type_) {
        case Type::Undefined: return "undefined";
        case Type::Null: return "null";
        case Type::Boolean: return u_.b ? "true" : "false";
        case Type::Number: return numberToString(u_.n);
        case Type::String: return u_.s->str;
        case Type::Object: {
            Value toStr = getMember(*this, internLiteral("toString"));
            if (toStr.isObject() && toStr.object()->isCallable()) {
                Value v = toStr.object()->call(*this, {});
                if (!v.isObject()) return v.toString();
            }
            return u_.o->defaultString();
        }
    }
    return "";
}

bool Value::toBoolean() const {
    switch (type_) {
        case Type::Undefined:
        case Type::Null: return false;
        case Type::Boolean: return u_.b;
        case Type::Number: return u_.n != 0 && !std::isnan(u_.n);
        case Type::String: return !u_.s->str.empty();  // SWF 7+
        case Type::Object: return true;
    }
    return false;
}

int32_t Value::toInt32() const { return (int32_t)toUint32(); }

uint32_t Value::toUint32() const {
    double n = toNumber();
    if (std::isnan(n) || std::isinf(n)) return 0;
    n = std::trunc(n);
    double m = std::fmod(n, 4294967296.0);
    if (m < 0) m += 4294967296.0;
    return (uint32_t)m;
}

Atom Value::toAtom() const {
    if (type_ == Type::String) return intern(u_.s->str);
    return intern(toString());
}

const char* Value::typeOf() const {
    switch (type_) {
        case Type::Undefined: return "undefined";
        case Type::Null: return "null";
        case Type::Boolean: return "boolean";
        case Type::Number: return "number";
        case Type::String: return "string";
        case Type::Object: return u_.o->typeName();
    }
    return "undefined";
}

// ---------------------------------------------------------------- member access

Ref Value::operator[](const Value& key) const { return Ref(*this, key); }
Ref Value::operator[](int index) const { return Ref(*this, Value(index)); }
Ref Value::operator[](double index) const { return Ref(*this, Value(index)); }
Ref Value::operator[](Atom key) const { return Ref(*this, key); }

// ---------------------------------------------------------------- arithmetic

Value operator+(const Value& a, const Value& b) {
    if (a.isNumber() && b.isNumber()) return a.number() + b.number();
    Value pa = a.toPrimitive(), pb = b.toPrimitive();
    if (pa.isString() || pb.isString()) {
        std::string s = pa.toString();
        s += pb.toString();
        return Value(s);
    }
    return pa.toNumber() + pb.toNumber();
}
Value operator-(const Value& a, const Value& b) { return a.toNumber() - b.toNumber(); }
Value operator*(const Value& a, const Value& b) { return a.toNumber() * b.toNumber(); }
Value operator/(const Value& a, const Value& b) { return a.toNumber() / b.toNumber(); }
Value operator%(const Value& a, const Value& b) { return std::fmod(a.toNumber(), b.toNumber()); }
Value operator-(const Value& a) { return -a.toNumber(); }
Value operator+(const Value& a) { return a.toNumber(); }
Value operator!(const Value& a) { return !a.toBoolean(); }
Value operator~(const Value& a) { return (double)~a.toInt32(); }
Value operator&(const Value& a, const Value& b) { return (double)(a.toInt32() & b.toInt32()); }
Value operator|(const Value& a, const Value& b) { return (double)(a.toInt32() | b.toInt32()); }
Value operator^(const Value& a, const Value& b) { return (double)(a.toInt32() ^ b.toInt32()); }
Value operator<<(const Value& a, const Value& b) {
    return (double)(int32_t)((uint32_t)a.toInt32() << (b.toUint32() & 31));
}
Value operator>>(const Value& a, const Value& b) { return (double)(a.toInt32() >> (b.toUint32() & 31)); }

Value& Value::operator+=(const Value& v) { return *this = *this + v; }
Value& Value::operator-=(const Value& v) { return *this = *this - v; }
Value& Value::operator*=(const Value& v) { return *this = *this * v; }
Value& Value::operator/=(const Value& v) { return *this = *this / v; }
Value& Value::operator%=(const Value& v) { return *this = *this % v; }
Value& Value::operator++() { return *this = toNumber() + 1; }
Value Value::operator++(int) {
    Value old = toNumber();
    *this = old.number() + 1;
    return old;
}
Value& Value::operator--() { return *this = toNumber() - 1; }
Value Value::operator--(int) {
    Value old = toNumber();
    *this = old.number() - 1;
    return old;
}

// ---------------------------------------------------------------- comparison

bool strictEquals(const Value& a, const Value& b) {
    if (a.type() != b.type()) return false;
    switch (a.type()) {
        case Type::Undefined:
        case Type::Null: return true;
        case Type::Boolean: return a.boolean() == b.boolean();
        case Type::Number: return a.number() == b.number();
        case Type::String: return a.string() == b.string();
        case Type::Object: return a.object() == b.object();
    }
    return false;
}

bool operator==(const Value& a, const Value& b) {
    if (a.type() == b.type()) return strictEquals(a, b);
    if (a.isNullish() && b.isNullish()) return true;
    if (a.isNullish() || b.isNullish()) return false;
    if (a.isNumber() && b.isString()) return a.number() == b.toNumber();
    if (a.isString() && b.isNumber()) return a.toNumber() == b.number();
    if (a.isBoolean()) return Value(a.toNumber()) == b;
    if (b.isBoolean()) return a == Value(b.toNumber());
    if (a.isObject() && !b.isObject()) {
        Value p = a.toPrimitive();
        return !p.isObject() && p == b;
    }
    if (b.isObject() && !a.isObject()) {
        Value p = b.toPrimitive();
        return !p.isObject() && a == p;
    }
    return false;
}

bool operator!=(const Value& a, const Value& b) { return !(a == b); }

namespace {
// Returns -1 (a<b), 0 (not less), or 2 (undefined: NaN involved).
int lessThan(const Value& a, const Value& b) {
    Value pa = a.toPrimitive(), pb = b.toPrimitive();
    if (pa.isString() && pb.isString()) return pa.string() < pb.string() ? -1 : 0;
    double x = pa.toNumber(), y = pb.toNumber();
    if (std::isnan(x) || std::isnan(y)) return 2;
    return x < y ? -1 : 0;
}
}  // namespace

bool operator<(const Value& a, const Value& b) {
    if (a.isNumber() && b.isNumber()) return a.number() < b.number();
    return lessThan(a, b) == -1;
}
bool operator>(const Value& a, const Value& b) {
    if (a.isNumber() && b.isNumber()) return a.number() > b.number();
    return lessThan(b, a) == -1;
}
bool operator<=(const Value& a, const Value& b) {
    if (a.isNumber() && b.isNumber()) return a.number() <= b.number();
    return lessThan(b, a) == 0;
}
bool operator>=(const Value& a, const Value& b) {
    if (a.isNumber() && b.isNumber()) return a.number() >= b.number();
    return lessThan(a, b) == 0;
}

}  // namespace as
