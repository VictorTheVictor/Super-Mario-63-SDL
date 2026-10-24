#pragma once
// Included by the generated script code (game/generated/*.cpp). Provides AS2-looking
// names for globals and the helpers the transpiler emits.

#include <initializer_list>
#include <utility>
#include <vector>

#include "as/Builtins.h"
#include "as/Runtime.h"
#include "as/Scripts.h"

namespace game {

using namespace as;

// A property of _global, looked up on each use: Math, Key, trace, _root (= _level0)...
class GlobalRef {
public:
    constexpr explicit GlobalRef(const char* literal) : name_(literal) {}
    Value get() const { return getMember(Value(Runtime::get().global), internLiteral(name_)); }
    operator Value() const { return get(); }
    explicit operator bool() const { return get().toBoolean(); }
    template <size_t N>
    Ref operator[](const char (&key)[N]) const { return get()[key]; }
    Ref operator[](const Value& key) const { return get()[key]; }
    template <typename... A>
    Value operator()(A&&... args) const { return get()(std::forward<A>(args)...); }

private:
    const char* name_;
};

inline const GlobalRef _root{"_level0"};

inline const GlobalRef Math{"Math"}, Key{"Key"}, Mouse{"Mouse"}, Stage{"Stage"}, System{"System"},
    SharedObject{"SharedObject"}, String{"String"}, Number{"Number"}, Boolean{"Boolean"},
    Array{"Array"}, Object{"Object"}, Sound{"Sound"}, Color{"Color"}, LoadVars{"LoadVars"},
    XML{"XML"}, ContextMenu{"ContextMenu"}, ContextMenuItem{"ContextMenuItem"},
    MovieClip{"MovieClip"}, TextField{"TextField"}, Button{"Button"}, Function{"Function"};

inline const GlobalRef trace{"trace"}, random{"random"}, getTimer{"getTimer"},
    setInterval{"setInterval"}, clearInterval{"clearInterval"}, isNaN{"isNaN"},
    isFinite{"isFinite"}, parseInt{"parseInt"}, parseFloat{"parseFloat"}, escape{"escape"},
    unescape{"unescape"}, getURL{"getURL"}, fscommand{"fscommand"}, targetPath{"targetPath"},
    chr{"chr"}, ord{"ord"}, updateAfterEvent{"updateAfterEvent"}, getVersion{"getVersion"},
    NaN{"NaN"}, Infinity{"Infinity"};
inline const GlobalRef int_{"int"};  // AS2 int() (int is a C++ keyword)

// Unqualified timeline opcodes act on the current target clip, as in AVM1.
template <typename... A>
Value callGlobal(const char* literal, A&&... args) {
    return GlobalRef(literal)(std::forward<A>(args)...);
}
inline Value stop() { return callGlobal("stop"); }
inline Value play() { return callGlobal("play"); }
inline Value nextFrame() { return callGlobal("nextFrame"); }
inline Value prevFrame() { return callGlobal("prevFrame"); }
inline Value stopAllSounds() { return callGlobal("stopAllSounds"); }
inline Value gotoAndStop(const Value& frame) { return callGlobal("gotoAndStop", frame); }
inline Value gotoAndPlay(const Value& frame) { return callGlobal("gotoAndPlay", frame); }

// Operators and statements without a direct C++ equivalent.
inline Value typeOf(const Value& v) { return Value(v.typeOf()); }
bool instanceOf(const Value& v, const Value& ctor);
// for (k in obj): enumerable keys of obj and its prototypes, as strings.
std::vector<Value> forIn(const Value& v);
// switch: index of the first case strictly equal to v, or -1 (default).
int caseIndex(const Value& v, std::initializer_list<Value> cases);

// a && b / a || b as values (short-circuit, returning an operand like AS2).
template <typename F>
Value logicalAnd(const Value& a, F&& b) { return a.toBoolean() ? Value(b()) : a; }
template <typename F>
Value logicalOr(const Value& a, F&& b) { return a.toBoolean() ? a : Value(b()); }

// new Ctor(args...)
template <typename C, typename... A>
Value new_(const C& ctor, A&&... args) {
    Value argv[sizeof...(A) + 1] = {Value(std::forward<A>(args))...};
    return construct(Value(ctor), Args{argv, sizeof...(A)});
}

}  // namespace game
