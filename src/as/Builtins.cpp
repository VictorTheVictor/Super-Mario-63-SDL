// Built-in AS2 globals: functions, Object/Function/Array/String/Number/Boolean,
// Math, Key, Mouse, Stage, System, SharedObject, intervals and network stubs.
// Display-list classes (MovieClip, TextField, Color, Sound) live in Stage.cpp.

#include "as/Builtins.h"
#include "as/Deterministic.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <random>
#include <sstream>

namespace as {
namespace {

Runtime& R() { return Runtime::get(); }
double num(Args a, size_t i) { return a[i].toNumber(); }
Value nan() { return std::numeric_limits<double>::quiet_NaN(); }

std::mt19937& rng() {
    static std::mt19937 g(std::random_device{}());
    return g;
}

bool g_deterministic = false;
double g_clockMs = 0;

}  // namespace

void setDeterministic(uint32_t seed) {
    g_deterministic = true;
    rng().seed(seed);
}
bool deterministic() { return g_deterministic; }
void advanceClock(double ms) {
    if (g_deterministic) g_clockMs += ms;
}
double deterministicClock() { return g_clockMs; }

namespace {

// ---------------------------------------------------------------- UTF-8 helpers

std::u32string decode(const std::string& s) {
    std::u32string out;
    for (size_t i = 0; i < s.size();) {
        uint8_t c = (uint8_t)s[i];
        int extra = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        if (extra < 0 || i + extra >= s.size() + (extra ? 0 : 1)) {
            out.push_back(c);
            i++;
            continue;
        }
        uint32_t cp = extra == 0 ? c : c & (0x3F >> extra);
        for (int k = 1; k <= extra; k++) cp = (cp << 6) | ((uint8_t)s[i + k] & 0x3F);
        out.push_back(cp);
        i += extra + 1;
    }
    return out;
}

std::string encode(const std::u32string& s) {
    std::string out;
    for (char32_t c : s) {
        if (c < 0x80) out.push_back((char)c);
        else if (c < 0x800) {
            out.push_back((char)(0xC0 | c >> 6));
            out.push_back((char)(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            out.push_back((char)(0xE0 | c >> 12));
            out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (c & 0x3F)));
        } else {
            out.push_back((char)(0xF0 | c >> 18));
            out.push_back((char)(0x80 | ((c >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

std::u32string thisString(const Value& self) { return decode(self.toString()); }

// Clamps a possibly-negative index the way slice()/substr() do.
long clampIndex(double v, long len) {
    if (std::isnan(v)) return 0;
    long i = (long)v;
    if (i < 0) i = std::max(0L, len + i);
    return std::min(i, len);
}

// ---------------------------------------------------------------- global functions

Value g_trace(const Value&, Args a) {
    R().traceOutput(a[0].toString());
    return undefined;
}
Value g_random(const Value&, Args a) {
    double n = std::floor(num(a, 0));
    if (!(n > 0)) return 0;
    return (double)std::uniform_int_distribution<long>(0, (long)n - 1)(rng());
}
Value g_int(const Value&, Args a) { return (double)a[0].toInt32(); }
Value g_isNaN(const Value&, Args a) { return std::isnan(num(a, 0)); }
Value g_isFinite(const Value&, Args a) { return std::isfinite(num(a, 0)); }
Value g_getTimer(const Value&, Args) { return std::floor(R().timeMs()); }
Value g_parseFloat(const Value&, Args a) {
    std::string s = a[0].toString();
    size_t i = s.find_first_not_of(" \t\r\n");
    if (i == std::string::npos) return nan();
    char* end;
    double v = std::strtod(s.c_str() + i, &end);
    return end == s.c_str() + i ? nan() : Value(v);
}
Value g_parseInt(const Value&, Args a) {
    std::string s = a[0].toString();
    size_t i = s.find_first_not_of(" \t\r\n");
    if (i == std::string::npos) return nan();
    bool neg = false;
    if (s[i] == '-' || s[i] == '+') neg = s[i++] == '-';
    int radix = a.size() > 1 && !a[1].isUndefined() ? a[1].toInt32() : 0;
    if ((radix == 0 || radix == 16) && i + 1 < s.size() && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
        radix = 16;
        i += 2;
    } else if (radix == 0) {
        radix = (i + 1 < s.size() && s[i] == '0') ? 8 : 10;  // AVM1 treats leading 0 as octal
    }
    if (radix < 2 || radix > 36) return nan();
    double v = 0;
    size_t start = i;
    for (; i < s.size(); i++) {
        char c = s[i];
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'z' ? c - 'a' + 10 : c >= 'A' && c <= 'Z' ? c - 'A' + 10 : 99;
        if (d >= radix) break;
        v = v * radix + d;
    }
    if (i == start) return nan();
    return neg ? -v : v;
}
Value g_escape(const Value&, Args a) {
    std::string s = a[0].toString(), out;
    for (unsigned char c : s) {
        if (std::isalnum(c)) out.push_back((char)c);
        else {
            char buf[4];
            std::snprintf(buf, sizeof buf, "%%%02X", c);
            out += buf;
        }
    }
    return Value(out);
}
Value g_unescape(const Value&, Args a) {
    std::string s = a[0].toString(), out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit((unsigned char)s[i + 1]) &&
            std::isxdigit((unsigned char)s[i + 2])) {
            out.push_back((char)std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else {
            out.push_back(s[i]);
        }
    }
    return Value(out);
}
Value g_chr(const Value&, Args a) { return Value(encode(std::u32string(1, (char32_t)a[0].toUint32()))); }
Value g_ord(const Value&, Args a) {
    auto s = decode(a[0].toString());
    return s.empty() ? Value(0) : Value((double)s[0]);
}
Value g_getVersion(const Value&, Args) { return Value("WIN 8,0,0,0"); }
Value g_noop(const Value&, Args) { return undefined; }

// ---------------------------------------------------------------- Object / Function

Value obj_call(const Value&, Args a) {
    if (a[0].isObject()) return a[0];
    return Value(R().newObject());
}
Value obj_ctor(Function*, Args a) { return obj_call(undefined, a); }
Value obj_hasOwnProperty(const Value& self, Args a) {
    Object* o = self.object();
    return o && o->hasOwn(a[0].toAtom());
}
Value obj_toString(const Value& self, Args) {
    Object* o = self.object();
    return Value(o ? o->defaultString() : self.toString());
}
Value obj_valueOf(const Value& self, Args) { return self; }

Value fn_call(const Value& self, Args a) {
    Args rest{a.n > 0 ? a.v + 1 : nullptr, a.n > 0 ? a.n - 1 : 0};
    return callFunction(self, a[0], rest);
}
Value fn_apply(const Value& self, Args a) {
    std::vector<Value> argv;
    if (auto* arr = dynamic_cast<Array*>(a[1].object())) argv = arr->elements;
    return callFunction(self, a[0], Args{argv.data(), argv.size()});
}

// ---------------------------------------------------------------- Array

Array* thisArray(const Value& self) { return dynamic_cast<Array*>(self.object()); }

Value arr_ctor(Function*, Args a) {
    Array* arr = R().newArray();
    if (a.size() == 1 && a[0].isNumber()) arr->elements.resize((size_t)std::max(0.0, a[0].number()));
    else
        for (size_t i = 0; i < a.size(); i++) arr->elements.push_back(a[i]);
    return Value(arr);
}
Value arr_call(const Value&, Args a) { return arr_ctor(nullptr, a); }
Value arr_push(const Value& self, Args a) {
    Array* arr = thisArray(self);
    if (!arr) return undefined;
    for (size_t i = 0; i < a.size(); i++) arr->elements.push_back(a[i]);
    return (double)arr->elements.size();
}
Value arr_pop(const Value& self, Args) {
    Array* arr = thisArray(self);
    if (!arr || arr->elements.empty()) return undefined;
    Value v = arr->elements.back();
    arr->elements.pop_back();
    return v;
}
Value arr_shift(const Value& self, Args) {
    Array* arr = thisArray(self);
    if (!arr || arr->elements.empty()) return undefined;
    Value v = arr->elements.front();
    arr->elements.erase(arr->elements.begin());
    return v;
}
Value arr_unshift(const Value& self, Args a) {
    Array* arr = thisArray(self);
    if (!arr) return undefined;
    arr->elements.insert(arr->elements.begin(), a.v, a.v + a.n);
    return (double)arr->elements.size();
}
Value arr_join(const Value& self, Args a) {
    Array* arr = thisArray(self);
    if (!arr) return Value("");
    std::string sep = a[0].isUndefined() ? "," : a[0].toString(), out;
    for (size_t i = 0; i < arr->elements.size(); i++) {
        if (i) out += sep;
        if (!arr->elements[i].isNullish()) out += arr->elements[i].toString();
    }
    return Value(out);
}
Value arr_toString(const Value& self, Args) { return arr_join(self, {}); }
Value arr_reverse(const Value& self, Args) {
    if (Array* arr = thisArray(self)) std::reverse(arr->elements.begin(), arr->elements.end());
    return self;
}
Value arr_slice(const Value& self, Args a) {
    Array* arr = thisArray(self);
    if (!arr) return undefined;
    long len = (long)arr->elements.size();
    long s = clampIndex(a[0].isUndefined() ? 0 : num(a, 0), len);
    long e = a[1].isUndefined() ? len : clampIndex(num(a, 1), len);
    std::vector<Value> out;
    for (long i = s; i < e; i++) out.push_back(arr->elements[i]);
    return Value(R().newArray(std::move(out)));
}
Value arr_splice(const Value& self, Args a) {
    Array* arr = thisArray(self);
    if (!arr) return undefined;
    long len = (long)arr->elements.size();
    long s = clampIndex(num(a, 0), len);
    long count = a.size() < 2 ? len - s : std::clamp((long)num(a, 1), 0L, len - s);
    std::vector<Value> removed(arr->elements.begin() + s, arr->elements.begin() + s + count);
    arr->elements.erase(arr->elements.begin() + s, arr->elements.begin() + s + count);
    if (a.size() > 2) arr->elements.insert(arr->elements.begin() + s, a.v + 2, a.v + a.n);
    return Value(R().newArray(std::move(removed)));
}
Value arr_concat(const Value& self, Args a) {
    Array* arr = thisArray(self);
    std::vector<Value> out = arr ? arr->elements : std::vector<Value>{};
    for (size_t i = 0; i < a.size(); i++) {
        if (Array* other = dynamic_cast<Array*>(a[i].object()))
            out.insert(out.end(), other->elements.begin(), other->elements.end());
        else out.push_back(a[i]);
    }
    return Value(R().newArray(std::move(out)));
}

enum SortFlags { CaseInsensitive = 1, Descending = 2, UniqueSort = 4, ReturnIndexedArray = 8, Numeric = 16 };

int compareValues(const Value& x, const Value& y, int flags) {
    if (flags & Numeric) {
        double a = x.toNumber(), b = y.toNumber();
        return a < b ? -1 : a > b ? 1 : 0;
    }
    std::string a = x.toString(), b = y.toString();
    if (flags & CaseInsensitive) {
        for (auto& c : a) c = (char)std::tolower((unsigned char)c);
        for (auto& c : b) c = (char)std::tolower((unsigned char)c);
    }
    return a < b ? -1 : a > b ? 1 : 0;
}

Value sortImpl(Array* arr, const std::function<int(const Value&, const Value&)>& cmp, int flags) {
    std::vector<size_t> idx(arr->elements.size());
    for (size_t i = 0; i < idx.size(); i++) idx[i] = i;
    std::stable_sort(idx.begin(), idx.end(), [&](size_t i, size_t j) {
        int c = cmp(arr->elements[i], arr->elements[j]);
        return (flags & Descending) ? c > 0 : c < 0;
    });
    if (flags & UniqueSort)
        for (size_t k = 1; k < idx.size(); k++)
            if (cmp(arr->elements[idx[k - 1]], arr->elements[idx[k]]) == 0) return 0;
    if (flags & ReturnIndexedArray) {
        std::vector<Value> out;
        for (size_t i : idx) out.push_back((double)i);
        return Value(R().newArray(std::move(out)));
    }
    std::vector<Value> sorted;
    for (size_t i : idx) sorted.push_back(arr->elements[i]);
    arr->elements = std::move(sorted);
    return Value(arr);
}

Value arr_sort(const Value& self, Args a) {
    Array* arr = thisArray(self);
    if (!arr) return undefined;
    if (a[0].isObject() && a[0].object()->isCallable()) {
        Value fn = a[0];
        int flags = a[1].toInt32();
        return sortImpl(arr, [fn](const Value& x, const Value& y) {
            Value argv[2] = {x, y};
            double r = callFunction(fn, undefined, Args{argv, 2}).toNumber();
            return r < 0 ? -1 : r > 0 ? 1 : 0;
        }, flags);
    }
    int flags = a[0].toInt32();
    return sortImpl(arr, [flags](const Value& x, const Value& y) { return compareValues(x, y, flags); }, flags);
}
Value arr_sortOn(const Value& self, Args a) {
    Array* arr = thisArray(self);
    if (!arr) return undefined;
    std::vector<Atom> fields;
    if (Array* f = dynamic_cast<Array*>(a[0].object()))
        for (auto& v : f->elements) fields.push_back(v.toAtom());
    else fields.push_back(a[0].toAtom());
    int flags = a[1].toInt32();
    return sortImpl(arr, [&](const Value& x, const Value& y) {
        for (Atom f : fields) {
            int c = compareValues(getMember(x, f), getMember(y, f), flags);
            if (c) return c;
        }
        return 0;
    }, flags);
}

// ---------------------------------------------------------------- String

Value str_call(const Value&, Args a) { return a.size() ? Value(a[0].toString()) : Value(""); }
Value str_ctor(Function*, Args a) { return str_call(undefined, a); }
Value str_toString(const Value& self, Args) { return Value(self.toString()); }
Value str_charAt(const Value& self, Args a) {
    auto s = thisString(self);
    long i = (long)num(a, 0);
    return i >= 0 && i < (long)s.size() ? Value(encode(s.substr(i, 1))) : Value("");
}
Value str_charCodeAt(const Value& self, Args a) {
    auto s = thisString(self);
    long i = (long)num(a, 0);
    return i >= 0 && i < (long)s.size() ? Value((double)s[i]) : nan();
}
Value str_indexOf(const Value& self, Args a) {
    auto s = thisString(self), sub = decode(a[0].toString());
    size_t from = a.size() > 1 ? (size_t)std::max(0.0, num(a, 1)) : 0;
    size_t r = s.find(sub, from);
    return r == std::u32string::npos ? -1.0 : (double)r;
}
Value str_lastIndexOf(const Value& self, Args a) {
    auto s = thisString(self), sub = decode(a[0].toString());
    size_t from = a.size() > 1 ? (size_t)std::max(0.0, num(a, 1)) : std::u32string::npos;
    size_t r = s.rfind(sub, from);
    return r == std::u32string::npos ? -1.0 : (double)r;
}
Value str_substr(const Value& self, Args a) {
    auto s = thisString(self);
    long len = (long)s.size();
    long start = clampIndex(num(a, 0), len);
    long count = a[1].isUndefined() ? len - start : std::clamp((long)num(a, 1), 0L, len - start);
    return Value(encode(s.substr(start, count)));
}
Value str_substring(const Value& self, Args a) {
    auto s = thisString(self);
    long len = (long)s.size();
    auto clamp = [len](double v) { return std::isnan(v) ? 0L : std::clamp((long)v, 0L, len); };
    long st = clamp(num(a, 0)), en = a[1].isUndefined() ? len : clamp(num(a, 1));
    if (st > en) std::swap(st, en);
    return Value(encode(s.substr(st, en - st)));
}
Value str_slice(const Value& self, Args a) {
    auto s = thisString(self);
    long len = (long)s.size();
    long st = clampIndex(num(a, 0), len), en = a[1].isUndefined() ? len : clampIndex(num(a, 1), len);
    return Value(en > st ? encode(s.substr(st, en - st)) : std::string());
}
Value str_split(const Value& self, Args a) {
    std::string s = self.toString();
    std::vector<Value> out;
    if (a[0].isUndefined()) out.push_back(Value(s));
    else {
        std::string sep = a[0].toString();
        if (sep.empty()) {
            for (char32_t c : decode(s)) out.push_back(Value(encode(std::u32string(1, c))));
        } else {
            size_t p = 0, q;
            while ((q = s.find(sep, p)) != std::string::npos) {
                out.push_back(Value(s.substr(p, q - p)));
                p = q + sep.size();
            }
            out.push_back(Value(s.substr(p)));
        }
    }
    if (a.size() > 1 && !a[1].isUndefined()) out.resize(std::min(out.size(), (size_t)std::max(0.0, num(a, 1))));
    return Value(R().newArray(std::move(out)));
}
Value str_toLowerCase(const Value& self, Args) {
    std::string s = self.toString();
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return Value(s);
}
Value str_toUpperCase(const Value& self, Args) {
    std::string s = self.toString();
    for (auto& c : s) c = (char)std::toupper((unsigned char)c);
    return Value(s);
}
Value str_concat(const Value& self, Args a) {
    std::string s = self.toString();
    for (size_t i = 0; i < a.size(); i++) s += a[i].toString();
    return Value(s);
}
Value str_fromCharCode(const Value&, Args a) {
    std::u32string s;
    for (size_t i = 0; i < a.size(); i++) s.push_back((char32_t)(a[i].toUint32() & 0xFFFF));
    return Value(encode(s));
}

// ---------------------------------------------------------------- Number / Boolean

Value num_call(const Value&, Args a) { return a.size() ? Value(a[0].toNumber()) : Value(0); }
Value num_ctor(Function*, Args a) { return num_call(undefined, a); }
Value num_toString(const Value& self, Args a) {
    double n = self.toNumber();
    int radix = a[0].isUndefined() ? 10 : a[0].toInt32();
    if (radix == 10 || radix < 2 || radix > 36 || std::isnan(n) || std::isinf(n)) return Value(numberToString(n));
    bool neg = n < 0;
    unsigned long long v = (unsigned long long)std::fabs(std::trunc(n));
    std::string s;
    do {
        s.insert(s.begin(), "0123456789abcdefghijklmnopqrstuvwxyz"[v % radix]);
        v /= radix;
    } while (v);
    return Value(neg ? "-" + s : s);
}
Value num_valueOf(const Value& self, Args) { return self.toNumber(); }
Value bool_call(const Value&, Args a) { return a[0].toBoolean(); }
Value bool_ctor(Function*, Args a) { return bool_call(undefined, a); }
Value bool_toString(const Value& self, Args) { return Value(self.toBoolean() ? "true" : "false"); }
Value bool_valueOf(const Value& self, Args) { return self.toBoolean(); }

// ---------------------------------------------------------------- Math

#define MATH1(name, expr) \
    Value math_##name(const Value&, Args a) { double x = num(a, 0); return (double)(expr); }
MATH1(abs, std::fabs(x))
MATH1(acos, std::acos(x))
MATH1(asin, std::asin(x))
MATH1(atan, std::atan(x))
MATH1(ceil, std::ceil(x))
MATH1(cos, std::cos(x))
MATH1(exp, std::exp(x))
MATH1(floor, std::floor(x))
MATH1(log, std::log(x))
MATH1(round, std::floor(x + 0.5))
MATH1(sin, std::sin(x))
MATH1(sqrt, std::sqrt(x))
MATH1(tan, std::tan(x))
#undef MATH1
Value math_atan2(const Value&, Args a) { return std::atan2(num(a, 0), num(a, 1)); }
Value math_pow(const Value&, Args a) { return std::pow(num(a, 0), num(a, 1)); }
Value math_random(const Value&, Args) { return std::uniform_real_distribution<double>(0, 1)(rng()); }
Value math_max(const Value&, Args a) {
    double r = -INFINITY;
    for (size_t i = 0; i < a.size(); i++) {
        double v = num(a, i);
        if (std::isnan(v)) return nan();
        r = std::max(r, v);
    }
    return r;
}
Value math_min(const Value&, Args a) {
    double r = INFINITY;
    for (size_t i = 0; i < a.size(); i++) {
        double v = num(a, i);
        if (std::isnan(v)) return nan();
        r = std::min(r, v);
    }
    return r;
}

// ---------------------------------------------------------------- Key / Mouse

bool g_keys[256];
int g_lastCode = 0, g_lastAscii = 0;
Array* g_keyListeners = nullptr;
Array* g_mouseListeners = nullptr;

Value key_isDown(const Value&, Args a) {
    int c = a[0].toInt32();
    return c >= 0 && c < 256 && g_keys[c];
}
Value key_getCode(const Value&, Args) { return g_lastCode; }
Value key_getAscii(const Value&, Args) { return g_lastAscii; }
Value key_isToggled(const Value&, Args) { return false; }

Array* listenersOf(const Value& self) {
    return dynamic_cast<Array*>(getMember(self, internLiteral("_listeners")).object());
}
Value bc_addListener(const Value& self, Args a) {
    Array* l = listenersOf(self);
    if (!l || !a[0].isObject()) return false;
    for (auto& v : l->elements)
        if (v.object() == a[0].object()) return true;
    l->elements.push_back(a[0]);
    return true;
}
Value bc_removeListener(const Value& self, Args a) {
    Array* l = listenersOf(self);
    if (!l) return false;
    for (size_t i = 0; i < l->elements.size(); i++)
        if (l->elements[i].object() == a[0].object()) {
            l->elements.erase(l->elements.begin() + i);
            return true;
        }
    return false;
}

void broadcast(Array* listeners, const char* method) {
    if (!listeners) return;
    std::vector<Value> snapshot = listeners->elements;
    Atom m = intern(method);
    for (auto& l : snapshot) {
        Value fn = getMember(l, m);
        callFunction(fn, l, {});
    }
}

// ---------------------------------------------------------------- intervals

struct Interval {
    int id;
    Value fn, self;
    Atom methodName = nullptr;
    std::vector<Value> args;
    double period, next;
};
std::vector<Interval> g_intervals;
int g_nextInterval = 1;

Value g_setInterval(const Value&, Args a) {
    Interval iv;
    size_t first;
    if (a[0].isObject() && a[0].object()->isCallable()) {
        iv.fn = a[0];
        first = 1;
    } else {
        iv.self = a[0];
        iv.methodName = a[1].toAtom();
        first = 2;
    }
    iv.period = std::max(1.0, num(a, first));
    for (size_t i = first + 1; i < a.size(); i++) iv.args.push_back(a[i]);
    iv.id = g_nextInterval++;
    iv.next = R().timeMs() + iv.period;
    g_intervals.push_back(iv);
    return iv.id;
}
Value g_clearInterval(const Value&, Args a) {
    int id = a[0].toInt32();
    g_intervals.erase(std::remove_if(g_intervals.begin(), g_intervals.end(), [id](const Interval& i) { return i.id == id; }),
                      g_intervals.end());
    return undefined;
}

// ---------------------------------------------------------------- SharedObject

std::string g_saveDir = ".";
struct SharedFile {
    std::string name;
    Object* so;
};
std::vector<SharedFile> g_shared;

void serialize(const Value& v, std::string& out, int depth) {
    if (depth > 64) {
        out += "u";
        return;
    }
    switch (v.type()) {
        case Type::Undefined: out += "u"; break;
        case Type::Null: out += "n"; break;
        case Type::Boolean: out += v.boolean() ? "t" : "f"; break;
        case Type::Number: {
            char buf[40];
            std::snprintf(buf, sizeof buf, "d%.17g;", v.number());
            out += buf;
            break;
        }
        case Type::String:
            out += "s" + std::to_string(v.string().size()) + ":" + v.string();
            break;
        case Type::Object: {
            Object* o = v.object();
            if (o->isCallable()) {
                out += "u";
                break;
            }
            if (auto* arr = dynamic_cast<Array*>(o)) {
                out += "a" + std::to_string(arr->elements.size()) + ":";
                for (auto& e : arr->elements) serialize(e, out, depth + 1);
                break;
            }
            std::vector<Atom> keys;
            o->ownKeys(keys);
            out += "o" + std::to_string(keys.size()) + ":";
            for (Atom k : keys) {
                out += std::to_string(k->str.size()) + ":" + k->str;
                serialize(getMember(v, k), out, depth + 1);
            }
            break;
        }
    }
}

Value deserialize(const std::string& s, size_t& p) {
    if (p >= s.size()) return undefined;
    auto readLen = [&]() {
        size_t colon = s.find(':', p);
        size_t n = std::strtoul(s.c_str() + p, nullptr, 10);
        p = colon + 1;
        return n;
    };
    char t = s[p++];
    switch (t) {
        case 'n': return Value::null();
        case 't': return true;
        case 'f': return false;
        case 'd': {
            size_t semi = s.find(';', p);
            double d = std::strtod(s.substr(p, semi - p).c_str(), nullptr);
            p = semi + 1;
            return d;
        }
        case 's': {
            size_t n = readLen();
            Value v(s.substr(p, n));
            p += n;
            return v;
        }
        case 'a': {
            size_t n = readLen();
            Array* arr = R().newArray();
            for (size_t i = 0; i < n; i++) arr->elements.push_back(deserialize(s, p));
            return Value(arr);
        }
        case 'o': {
            size_t n = readLen();
            Object* o = R().newObject();
            std::vector<std::pair<Atom, Value>> props;
            for (size_t i = 0; i < n; i++) {
                size_t kl = readLen();
                Atom k = intern(s.substr(p, kl));
                p += kl;
                props.push_back({k, deserialize(s, p)});
            }
            // Keys were written in enumeration (reverse insertion) order.
            for (size_t i = props.size(); i-- > 0;) o->setOwn(props[i].first, props[i].second);
            return Value(o);
        }
        default: return undefined;
    }
}

std::string sharedPath(const std::string& name) {
    std::string safe;
    for (char c : name) safe.push_back(std::isalnum((unsigned char)c) || c == '_' || c == '-' ? c : '_');
    return g_saveDir + "/" + safe + ".sol.txt";
}

bool writeShared(const SharedFile& f) {
    std::string out;
    serialize(getMember(Value(f.so), internLiteral("data")), out, 0);
    std::ofstream file(sharedPath(f.name), std::ios::binary);
    file << out;
    return (bool)file;
}

Object* g_sharedObjectProto = nullptr;

Value so_getLocal(const Value&, Args a) {
    std::string name = a[0].toString();
    for (auto& f : g_shared)
        if (f.name == name) return Value(f.so);
    Object* so = R().heap.make<Object>(g_sharedObjectProto);
    Value data;
    std::ifstream file(sharedPath(name), std::ios::binary);
    if (file) {
        std::stringstream ss;
        ss << file.rdbuf();
        size_t p = 0;
        data = deserialize(ss.str(), p);
    }
    if (!data.isObject()) data = Value(R().newObject());
    so->setOwn(internLiteral("data"), data);
    g_shared.push_back({name, so});
    return Value(so);
}
Value so_flush(const Value& self, Args) {
    for (auto& f : g_shared)
        if (f.so == self.object()) return writeShared(f);
    return false;
}
Value so_clear(const Value& self, Args) {
    if (Object* o = self.object()) o->setOwn(internLiteral("data"), Value(R().newObject()));
    return undefined;
}
Value so_getSize(const Value& self, Args) {
    std::string out;
    serialize(getMember(self, internLiteral("data")), out, 0);
    return (double)out.size();
}

// ---------------------------------------------------------------- network stubs

// LoadVars/XML: the game's online features (level sharing, ads) have no server here.
// Requests "fail" on the next interval tick by calling onLoad(false).
Value net_load(const Value& self, Args) {
    Value target = self;
    Value argv[1] = {Value(false)};
    Function* f = R().newNative([](const Value&, Args) { return undefined; });
    (void)f;
    Value onLoad = getMember(target, internLiteral("onLoad"));
    if (onLoad.isObject()) callFunction(onLoad, target, Args{argv, 1});
    return true;
}
Value plain_ctor(Function* fn, Args) {
    Object* o = R().newObject();
    Value proto = getMember(Value(fn), internLiteral("prototype"));
    if (proto.isObject()) o->proto = proto.object();
    return Value(o);
}
Value menu_ctor(Function* fn, Args a) {
    Value o = plain_ctor(fn, a);
    setMember(o, internLiteral("customItems"), Value(R().newArray()));
    setMember(o, internLiteral("onSelect"), a[0]);
    setMember(o, internLiteral("builtInItems"), obj());
    return o;
}
Value menuItem_ctor(Function* fn, Args a) {
    Value o = plain_ctor(fn, a);
    setMember(o, internLiteral("caption"), a[0]);
    setMember(o, internLiteral("onSelect"), a[1]);
    setMember(o, internLiteral("enabled"), true);
    setMember(o, internLiteral("visible"), true);
    return o;
}

}  // namespace

// ---------------------------------------------------------------- host API

void keyEvent(int code, int ascii, bool down) {
    if (code >= 0 && code < 256) g_keys[code] = down;
    g_lastCode = code;
    if (ascii) g_lastAscii = ascii;
    broadcast(g_keyListeners, down ? "onKeyDown" : "onKeyUp");
}

bool isKeyDown(int code) { return code >= 0 && code < 256 && g_keys[code]; }

void mouseEvent(const char* method) { broadcast(g_mouseListeners, method); }

void runIntervals(double now) {
    // Callbacks may add or clear intervals, so work from ids.
    std::vector<int> due;
    for (auto& iv : g_intervals)
        if (now >= iv.next) due.push_back(iv.id);
    for (int id : due) {
        auto it = std::find_if(g_intervals.begin(), g_intervals.end(), [id](const Interval& i) { return i.id == id; });
        if (it == g_intervals.end()) continue;
        it->next = std::max(it->next + it->period, now);
        Interval iv = *it;
        if (iv.methodName) callFunction(getMember(iv.self, iv.methodName), iv.self, Args{iv.args.data(), iv.args.size()});
        else callFunction(iv.fn, undefined, Args{iv.args.data(), iv.args.size()});
    }
}

void setSaveDirectory(const std::string& dir) { g_saveDir = dir; }

void flushSharedObjects() {
    for (auto& f : g_shared) writeShared(f);
}

void traceBuiltins(Gc& gc) {
    gc.mark(g_keyListeners);
    gc.mark(g_mouseListeners);
    gc.mark(g_sharedObjectProto);
    for (auto& iv : g_intervals) {
        gc.mark(iv.fn);
        gc.mark(iv.self);
        for (auto& v : iv.args) gc.mark(v);
    }
    for (auto& f : g_shared) gc.mark(f.so);
}

void installBuiltins(Runtime& rt) {
    Object* G = rt.global;
    auto fn = [&](const char* name, Function::Native f) { rt.method(G, name, f); };
    auto constant = [](Object* o, const char* name, const Value& v) {
        o->defineSlot(intern(name), v, DontEnum | DontDelete | ReadOnly);
    };

    fn("trace", g_trace);
    fn("random", g_random);
    fn("int", g_int);
    fn("isNaN", g_isNaN);
    fn("isFinite", g_isFinite);
    fn("getTimer", g_getTimer);
    fn("parseInt", g_parseInt);
    fn("parseFloat", g_parseFloat);
    fn("escape", g_escape);
    fn("unescape", g_unescape);
    fn("chr", g_chr);
    fn("mbchr", g_chr);
    fn("ord", g_ord);
    fn("mbord", g_ord);
    fn("getVersion", g_getVersion);
    fn("fscommand", g_noop);
    fn("updateAfterEvent", g_noop);
    fn("setInterval", g_setInterval);
    fn("clearInterval", g_clearInterval);
    constant(G, "NaN", nan());
    constant(G, "Infinity", Value(INFINITY));
    constant(G, "newline", Value("\n"));  // AS2 `newline`

    // Object and Function
    rt.defineClass(G, "Object", obj_call, obj_ctor, rt.objectProto);
    rt.method(rt.objectProto, "hasOwnProperty", obj_hasOwnProperty);
    rt.method(rt.objectProto, "toString", obj_toString);
    rt.method(rt.objectProto, "valueOf", obj_valueOf);
    rt.defineClass(G, "Function", nullptr, nullptr, rt.functionProto);
    rt.method(rt.functionProto, "call", fn_call);
    rt.method(rt.functionProto, "apply", fn_apply);

    // Array
    Function* arrayClass = rt.defineClass(G, "Array", arr_call, arr_ctor, rt.arrayProto);
    for (auto [n, f] : std::initializer_list<std::pair<const char*, Function::Native>>{
             {"push", arr_push}, {"pop", arr_pop}, {"shift", arr_shift}, {"unshift", arr_unshift},
             {"join", arr_join}, {"toString", arr_toString}, {"reverse", arr_reverse},
             {"slice", arr_slice}, {"splice", arr_splice}, {"concat", arr_concat},
             {"sort", arr_sort}, {"sortOn", arr_sortOn}})
        rt.method(rt.arrayProto, n, f);
    constant(arrayClass, "CASEINSENSITIVE", 1);
    constant(arrayClass, "DESCENDING", 2);
    constant(arrayClass, "UNIQUESORT", 4);
    constant(arrayClass, "RETURNINDEXEDARRAY", 8);
    constant(arrayClass, "NUMERIC", 16);

    // String
    Function* stringClass = rt.defineClass(G, "String", str_call, str_ctor, rt.stringProto);
    for (auto [n, f] : std::initializer_list<std::pair<const char*, Function::Native>>{
             {"toString", str_toString}, {"valueOf", str_toString}, {"charAt", str_charAt},
             {"charCodeAt", str_charCodeAt}, {"indexOf", str_indexOf}, {"lastIndexOf", str_lastIndexOf},
             {"substr", str_substr}, {"substring", str_substring}, {"slice", str_slice},
             {"split", str_split}, {"toLowerCase", str_toLowerCase}, {"toUpperCase", str_toUpperCase},
             {"concat", str_concat}})
        rt.method(rt.stringProto, n, f);
    rt.method(stringClass, "fromCharCode", str_fromCharCode);

    // Number / Boolean
    Function* numberClass = rt.defineClass(G, "Number", num_call, num_ctor, rt.numberProto);
    rt.method(rt.numberProto, "toString", num_toString);
    rt.method(rt.numberProto, "valueOf", num_valueOf);
    constant(numberClass, "MAX_VALUE", std::numeric_limits<double>::max());
    constant(numberClass, "MIN_VALUE", std::numeric_limits<double>::denorm_min());
    constant(numberClass, "NaN", nan());
    constant(numberClass, "POSITIVE_INFINITY", Value(INFINITY));
    constant(numberClass, "NEGATIVE_INFINITY", Value(-INFINITY));
    rt.defineClass(G, "Boolean", bool_call, bool_ctor, rt.booleanProto);
    rt.method(rt.booleanProto, "toString", bool_toString);
    rt.method(rt.booleanProto, "valueOf", bool_valueOf);

    // Math
    Object* math = rt.newObject();
    G->defineSlot(intern("Math"), Value(math), DontEnum);
    for (auto [n, f] : std::initializer_list<std::pair<const char*, Function::Native>>{
             {"abs", math_abs}, {"acos", math_acos}, {"asin", math_asin}, {"atan", math_atan},
             {"atan2", math_atan2}, {"ceil", math_ceil}, {"cos", math_cos}, {"exp", math_exp},
             {"floor", math_floor}, {"log", math_log}, {"max", math_max}, {"min", math_min},
             {"pow", math_pow}, {"random", math_random}, {"round", math_round}, {"sin", math_sin},
             {"sqrt", math_sqrt}, {"tan", math_tan}})
        rt.method(math, n, f);
    constant(math, "E", M_E);
    constant(math, "LN10", M_LN10);
    constant(math, "LN2", M_LN2);
    constant(math, "LOG10E", M_LOG10E);
    constant(math, "LOG2E", M_LOG2E);
    constant(math, "PI", M_PI);
    constant(math, "SQRT1_2", M_SQRT1_2);
    constant(math, "SQRT2", M_SQRT2);

    // Key
    Object* key = rt.newObject();
    G->defineSlot(intern("Key"), Value(key), DontEnum);
    g_keyListeners = rt.newArray();
    key->defineSlot(internLiteral("_listeners"), Value(g_keyListeners), DontEnum);
    rt.method(key, "isDown", key_isDown);
    rt.method(key, "getCode", key_getCode);
    rt.method(key, "getAscii", key_getAscii);
    rt.method(key, "isToggled", key_isToggled);
    rt.method(key, "addListener", bc_addListener);
    rt.method(key, "removeListener", bc_removeListener);
    for (auto [n, c] : std::initializer_list<std::pair<const char*, int>>{
             {"BACKSPACE", keycode::Backspace}, {"CAPSLOCK", keycode::CapsLock}, {"CONTROL", keycode::Control},
             {"DELETEKEY", keycode::Delete}, {"DOWN", keycode::Down}, {"END", keycode::End},
             {"ENTER", keycode::Enter}, {"ESCAPE", keycode::Escape}, {"HOME", keycode::Home},
             {"INSERT", keycode::Insert}, {"LEFT", keycode::Left}, {"PGDN", keycode::PageDown},
             {"PGUP", keycode::PageUp}, {"RIGHT", keycode::Right}, {"SHIFT", keycode::Shift},
             {"SPACE", keycode::Space}, {"TAB", keycode::Tab}, {"UP", keycode::Up}})
        constant(key, n, c);

    // Mouse
    Object* mouse = rt.newObject();
    G->defineSlot(intern("Mouse"), Value(mouse), DontEnum);
    g_mouseListeners = rt.newArray();
    mouse->defineSlot(internLiteral("_listeners"), Value(g_mouseListeners), DontEnum);
    rt.method(mouse, "addListener", bc_addListener);
    rt.method(mouse, "removeListener", bc_removeListener);
    rt.method(mouse, "hide", g_noop);
    rt.method(mouse, "show", g_noop);

    // System
    Object* system = rt.newObject();
    G->defineSlot(intern("System"), Value(system), DontEnum);
    Object* security = rt.newObject();
    system->defineSlot(intern("security"), Value(security), DontEnum);
    rt.method(security, "allowDomain", g_noop);
    rt.method(security, "allowInsecureDomain", g_noop);
    rt.method(security, "loadPolicyFile", g_noop);
    rt.method(system, "showSettings", g_noop);
    rt.method(system, "setClipboard", g_noop);
    system->defineSlot(intern("capabilities"), obj({{"os", "Windows"}, {"version", "WIN 8,0,0,0"},
                                                     {"screenResolutionX", 1920}, {"screenResolutionY", 1080}}),
                       DontEnum);
    system->defineSlot(intern("useCodepage"), false);

    // SharedObject
    g_sharedObjectProto = rt.newObject();
    rt.method(g_sharedObjectProto, "flush", so_flush);
    rt.method(g_sharedObjectProto, "clear", so_clear);
    rt.method(g_sharedObjectProto, "getSize", so_getSize);
    Object* soClass = rt.newObject();
    G->defineSlot(intern("SharedObject"), Value(soClass), DontEnum);
    rt.method(soClass, "getLocal", so_getLocal);

    // Network and context-menu stubs
    for (const char* cls : {"LoadVars", "XML"}) {
        Object* proto = rt.newObject();
        rt.method(proto, "load", net_load);
        rt.method(proto, "sendAndLoad", net_load);
        rt.method(proto, "send", g_noop);
        rt.method(proto, "toString", obj_toString);
        rt.defineClass(G, cls, nullptr, plain_ctor, proto);
    }
    rt.defineClass(G, "ContextMenu", nullptr, menu_ctor, rt.newObject());
    Object* cmProto = getMember(Value(G), internLiteral("ContextMenu"))["prototype"].get().object();
    rt.method(cmProto, "hideBuiltInItems", g_noop);
    rt.defineClass(G, "ContextMenuItem", nullptr, menuItem_ctor, rt.newObject());
    fn("getURL", g_noop);

    rt.rootProviders.push_back(traceBuiltins);
}

}  // namespace as
