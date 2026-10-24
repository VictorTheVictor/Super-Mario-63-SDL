#include "as/Diagnostics.h"

#include <algorithm>
#include <vector>

namespace as {

Diagnostics& diagnostics() {
    static Diagnostics d;
    return d;
}

void Diagnostics::print(std::FILE* out, size_t top) const {
    auto list = [&](const char* title, const std::map<std::string, long>& m) {
        std::vector<std::pair<long, std::string>> v;
        for (auto& [k, n] : m) v.push_back({n, k});
        std::sort(v.rbegin(), v.rend());
        std::fprintf(out, "%s: %zu distinct\n", title, v.size());
        for (size_t i = 0; i < v.size() && i < top; i++)
            std::fprintf(out, "  %8ld  %s\n", v[i].first, v[i].second.c_str());
    };
    list("calls to non-functions", uncallable);
    list("property reads on undefined/null", undefinedBase);
}

}  // namespace as
