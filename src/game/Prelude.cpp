#include "game/Prelude.h"

#include <unordered_set>

namespace game {

// Note: inside this namespace, Object/Array/String... name the AS2 globals (GlobalRef);
// use as::Object for the C++ type.

bool instanceOf(const Value& v, const Value& ctor) {
    as::Object* o = v.object();
    Value proto = getMember(ctor, internLiteral("prototype"));
    if (!o || !proto.isObject()) return false;
    for (as::Object* p = o->proto; p; p = p->proto)
        if (p == proto.object()) return true;
    return false;
}

std::vector<Value> forIn(const Value& v) {
    std::vector<Value> out;
    std::unordered_set<Atom> seen;
    int depth = 0;
    for (as::Object* o = v.object(); o && depth < 256; o = o->proto, depth++) {
        std::vector<Atom> keys;
        o->ownKeys(keys);
        for (Atom k : keys)
            if (seen.insert(k).second) out.push_back(Value(k));
    }
    return out;
}

int caseIndex(const Value& v, std::initializer_list<Value> cases) {
    int i = 0;
    for (const Value& c : cases) {
        if (strictEquals(v, c)) return i;
        i++;
    }
    return -1;
}

}  // namespace game
