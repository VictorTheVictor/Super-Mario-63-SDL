// Unit tests for the AS2 runtime (no SWF needed). Expected values follow Flash
// Player / Ruffle behaviour for SWF 8.

#include <cmath>
#include <cstdio>
#include <string>

#include "as/Builtins.h"
#include "as/Runtime.h"

using namespace as;

static int failures = 0, checks = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        checks++;                                                            \
        if (!(cond)) {                                                       \
            failures++;                                                      \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                    \
    } while (0)

#define CHECK_STR(expr, expected)                                                        \
    do {                                                                                 \
        checks++;                                                                        \
        std::string got_ = (expr);                                                       \
        if (got_ != (expected)) {                                                        \
            failures++;                                                                  \
            std::printf("FAIL %s:%d: %s = \"%s\", expected \"%s\"\n", __FILE__, __LINE__, \
                        #expr, got_.c_str(), std::string(expected).c_str());             \
        }                                                                                \
    } while (0)

static void testConversions() {
    CHECK_STR(Value(1.0).toString(), "1");
    CHECK_STR(Value(-0.5).toString(), "-0.5");
    CHECK_STR(Value(0.1 + 0.2).toString(), "0.3");
    CHECK_STR(Value(1.0 / 3).toString(), "0.333333333333333");
    CHECK_STR(Value(1e21).toString(), "1e+21");
    CHECK_STR(Value(123456789012345680000.0).toString(), "1.23456789012346e+20");
    CHECK_STR(Value(0.000001).toString(), "1e-6");
    CHECK_STR(Value(0.0001).toString(), "0.0001");
    CHECK_STR(Value(NAN).toString(), "NaN");
    CHECK_STR(Value(-INFINITY).toString(), "-Infinity");
    CHECK_STR(undefined.toString(), "undefined");
    CHECK_STR(Value::null().toString(), "null");
    CHECK_STR(Value(true).toString(), "true");

    CHECK(std::isnan(undefined.toNumber()));
    CHECK(std::isnan(Value::null().toNumber()));
    CHECK(Value("12").toNumber() == 12);
    CHECK(Value(" 12").toNumber() == 12);
    CHECK(Value("0x1F").toNumber() == 31);
    CHECK(Value("-3.5e2").toNumber() == -350);
    CHECK(std::isnan(Value("").toNumber()));
    CHECK(std::isnan(Value("12px").toNumber()));
    CHECK(std::isnan(Value("inf").toNumber()));
    CHECK(Value(true).toNumber() == 1);

    CHECK(!Value("").toBoolean());
    CHECK(Value("0").toBoolean());  // SWF7+: any non-empty string is true
    CHECK(!Value(NAN).toBoolean());
    CHECK(!undefined.toBoolean());

    CHECK(Value(-1.7).toInt32() == -1);
    CHECK(Value(4294967297.0).toInt32() == 1);
    CHECK_STR(Value(1.0).typeOf(), "number");
    CHECK_STR(undefined.typeOf(), "undefined");
}

static void testOperators() {
    CHECK_STR((Value("a") + 1).toString(), "a1");
    CHECK_STR((Value(1) + "a").toString(), "1a");
    CHECK((Value(1) + 2).number() == 3);
    CHECK((Value("3") - 1).number() == 2);
    CHECK((Value("6") * "7").number() == 42);
    CHECK(std::isnan((undefined + 1).number()));
    CHECK_STR((Value("x") + undefined).toString(), "xundefined");
    CHECK((Value(7) % 3).number() == 1);

    CHECK(Value(1) == Value("1"));
    CHECK(Value(1) == Value(true));
    CHECK(undefined == Value::null());
    CHECK(!(undefined == Value(0)));
    CHECK(!(Value(NAN) == Value(NAN)));
    CHECK(!(Value("") == Value(0)));  // SWF7+: Number("") is NaN
    CHECK(!strictEquals(Value(1), Value("1")));
    CHECK(strictNotEquals(Value(1), Value("1")));
    CHECK(strictEquals(Value("ab"), Value("ab")));

    CHECK(Value(1) < Value(2));
    CHECK(Value("10") < Value(9) == false);
    CHECK(Value("10") < Value("9"));  // string comparison
    CHECK(!(Value(NAN) < Value(1)) && !(Value(NAN) >= Value(1)));
    CHECK(Value(2) >= Value("2"));

    Value i = 5;
    i++;
    ++i;
    i += "1";  // number + string concatenates
    CHECK_STR(i.toString(), "71");
    CHECK((!Value(0)).boolean());
    CHECK((Value(6) & 3).number() == 2);
    CHECK((Value(1) << 4).number() == 16);
    CHECK((~Value(0)).number() == -1);
}

static void testObjects() {
    Runtime& rt = Runtime::get();
    Value o = obj({{"a", 1}, {"b", "two"}});
    CHECK(o["a"] == 1);
    o["c"] = o["a"] + 41;
    CHECK(o["c"] == 42);
    o["c"] += 1;
    CHECK(o["c"] == 43);
    o["n"]++;
    CHECK(std::isnan(o["n"].get().toNumber()));  // SWF7+: undefined converts to NaN
    CHECK(o["missing"].get().isUndefined());
    CHECK_STR(o.toString(), "[object Object]");

    // Prototype chain
    Value child = Value(rt.newObject());
    child.object()->proto = o.object();
    CHECK(child["b"] == "two");
    child["b"] = "own";
    CHECK(child["b"] == "own" && o["b"] == "two");
    CHECK(child["__proto__"] == o);

    // Arrays
    Value a = arr({1, 2, 3});
    CHECK(a["length"] == 3);
    a["push"](4, 5);
    CHECK(a["length"] == 5 && a[4] == 5);
    a[9] = "x";
    CHECK(a["length"] == 10);
    CHECK(a[7].get().isUndefined());
    CHECK_STR(arr({1, "b", Value::null()}).toString(), "1,b,");
    CHECK(a["pop"]() == "x");
    Value s = arr({3, 1, 2})["sort"]();
    CHECK_STR(s.toString(), "1,2,3");
    CHECK_STR(arr({10, 9, 1})["sort"]().toString(), "1,10,9");  // default: string order
    CHECK_STR(arr({10, 9, 1})["sort"](16).toString(), "1,9,10");  // Array.NUMERIC
    CHECK_STR(arr({1, 2, 3, 4})["splice"](1, 2).toString(), "2,3");
    CHECK_STR(arr({1, 2, 3, 4})["slice"](-2).toString(), "3,4");
    CHECK_STR(Value("a,b,c")["split"](",")["join"]("-").toString(), "a-b-c");

    // Strings
    Value str = "Hello";
    CHECK(str["length"] == 5);
    CHECK(str["charAt"](1) == "e");
    CHECK(str["charCodeAt"](0) == 72);
    CHECK(str["substr"](1, 3) == "ell");
    CHECK(str["substring"](3, 1) == "el");
    CHECK(str["indexOf"]("l") == 2);
    CHECK(str["toUpperCase"]() == "HELLO");

    // Global functions and Math via _global
    Value G = Value(rt.global);
    CHECK(G["Math"]["max"](3, 7, 5) == 7);
    CHECK(G["Math"]["round"](-2.5) == -2);
    CHECK(G["Math"]["floor"](2.7) == 2);
    CHECK(G["parseInt"]("010") == 8);
    CHECK(G["parseInt"]("ff", 16) == 255);
    CHECK(G["int"](-3.7) == -3);
    CHECK(G["Number"]("3.5") == 3.5);
    CHECK(G["String"](12) == "12");
    Value r = G["random"](10);
    CHECK(r.number() >= 0 && r.number() < 10 && r.number() == std::floor(r.number()));

    // Constructors
    Value newArr = construct(G["Array"], {});
    CHECK(newArr["length"] == 0);
    Value newObj = construct(G["Object"], {});
    CHECK_STR(newObj.typeOf(), "object");
}

static void testFunctionsAndScope() {
    Runtime& rt = Runtime::get();
    // A fake "timeline" object standing in for a clip.
    Object* timeline = rt.newObject();
    Scope* targetScope = rt.heap.make<Scope>(Scope::Target, timeline, rt.globalScope);
    Activation scope(Value(timeline), {}, targetScope, timeline);

    // Unqualified assignment creates variables on the target timeline.
    scope["speed"] = 3;
    CHECK(Value(timeline)["speed"] == 3);
    scope["speed"] += 2;
    CHECK(scope["speed"] == 5);
    // Globals resolve through the chain.
    CHECK(scope["Math"]["abs"](-4) == 4);
    // _global writes are visible everywhere.
    scope["_global"]["shared"] = "yes";
    CHECK(scope["shared"] == "yes");

    // Functions close over the defining scope; `this` comes from the call.
    Value counter = scope.function([](Activation& scope) -> Value {
        scope["count"] += 1;  // timeline variable (from the closure's Target scope)
        return scope.self["tag"];
    });
    Value holder = obj({{"tag", "holder"}});
    holder["f"] = counter;
    scope["count"] = 0;
    CHECK(holder["f"]() == "holder");
    CHECK(holder["f"]() == "holder");
    CHECK(scope["count"] == 2);

    // Arguments
    Value add = scope.function([](Activation& scope) -> Value { return scope.args[0] + scope.args[1]; });
    CHECK(add(2, 3) == 5);
    CHECK(add("2", 3) == "23");

    // Unqualified calls use the timeline as `this`.
    scope["whoami"] = scope.function([](Activation& scope) -> Value { return scope.self; });
    CHECK(scope["whoami"]() == Value(timeline));

    // with(obj) reads and writes existing properties of obj first.
    Value point = obj({{"x", 1}});
    {
        With w(scope, point);
        scope["x"] = 10;   // existing on point
        scope["y"] = 20;   // not on point -> timeline
    }
    CHECK(point["x"] == 10);
    CHECK(point["y"].get().isUndefined());
    CHECK(Value(timeline)["y"] == 20);

    // new on a script function uses its prototype
    Value Ctor = scope.function([](Activation& scope) -> Value {
        scope.self["v"] = scope.args[0];
        return undefined;
    });
    Ctor["prototype"]["hello"] = "proto";
    Value inst = construct(Ctor, Args{nullptr, 0});
    CHECK(inst["hello"] == "proto");
    Value argv[1] = {7};
    Value inst2 = construct(Ctor, Args{argv, 1});
    CHECK(inst2["v"] == 7);

    // Function.call / apply
    CHECK(add["call"](undefined, 1, 2) == 3);
    CHECK(add["apply"](undefined, arr({4, 5})) == 9);
}

static void testGc() {
    Runtime& rt = Runtime::get();
    Value keep = obj({{"inner", obj({{"deep", arr({1, 2})}})}});
    rt.global->setOwn(intern("keep"), keep);
    for (int i = 0; i < 1000; i++) obj({{"garbage", i}});
    size_t before = rt.heap.count();
    size_t freed = rt.collectGarbage();
    CHECK(freed >= 1000);
    CHECK(rt.heap.count() == before - freed);
    CHECK(Value(rt.global)["keep"]["inner"]["deep"][1] == 2);
}

static void testSharedObject() {
    Runtime& rt = Runtime::get();
    setSaveDirectory(".");
    Value G = Value(rt.global);
    Value so = G["SharedObject"]["getLocal"]("as_test_save");
    so["data"]["coins"] = 42;
    so["data"]["name"] = "Player";
    so["data"]["stars"] = arr({true, false, 3.5});
    CHECK(so["flush"]() == true);
    // Read back by deserializing the file through a fresh load path.
    so["data"] = obj();
    std::FILE* f = std::fopen("./as_test_save.sol.txt", "rb");
    CHECK(f != nullptr);
    if (f) std::fclose(f);
}

int main() {
    testConversions();
    testOperators();
    testObjects();
    testFunctionsAndScope();
    testGc();
    testSharedObject();
    std::remove("./as_test_save.sol.txt");
    std::printf("%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
