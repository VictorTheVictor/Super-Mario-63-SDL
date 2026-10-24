#pragma once
// Optional runtime diagnostics for finding porting gaps. AS2 silently ignores calls to
// non-functions and property reads on undefined/null; when enabled, these are counted
// by name so missing built-ins or translation mistakes stand out.

#include <cstdio>
#include <map>
#include <string>

namespace as {

struct Diagnostics {
    bool enabled = false;
    std::map<std::string, long> uncallable;     // "name()" called but not a function
    std::map<std::string, long> undefinedBase;  // ".name" read on undefined/null
    void print(std::FILE* out, size_t top = 25) const;
};

Diagnostics& diagnostics();

}  // namespace as
