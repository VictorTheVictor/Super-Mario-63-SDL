// Definitions of the glibc functions the portable Linux release would otherwise need a
// recent glibc for (2.36: arc4random; 2.38: strlcpy and friends, and the C23 variants of
// strtol/sscanf that headers redirect to). Linked into the release executable only.
//
// Compiled as C11 without _GNU_SOURCE so the calls below reach the original strtol/scanf
// entry points rather than being redirected to the C23 ones.
#define _DEFAULT_SOURCE
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <sys/syscall.h>
#include <unistd.h>

long __isoc23_strtol(const char* s, char** end, int base) { return strtol(s, end, base); }
long long __isoc23_strtoll(const char* s, char** end, int base) { return strtoll(s, end, base); }
unsigned long __isoc23_strtoul(const char* s, char** end, int base) { return strtoul(s, end, base); }
unsigned long long __isoc23_strtoull(const char* s, char** end, int base) { return strtoull(s, end, base); }

int __isoc23_vsscanf(const char* s, const char* format, va_list args) { return vsscanf(s, format, args); }

int __isoc23_sscanf(const char* s, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int n = vsscanf(s, format, args);
    va_end(args);
    return n;
}

int __isoc23_fscanf(FILE* f, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int n = vfscanf(f, format, args);
    va_end(args);
    return n;
}

size_t strlcpy(char* dst, const char* src, size_t size) {
    size_t n = strlen(src);
    if (size) {
        size_t m = n < size - 1 ? n : size - 1;
        memcpy(dst, src, m);
        dst[m] = 0;
    }
    return n;
}

size_t strlcat(char* dst, const char* src, size_t size) {
    size_t used = strnlen(dst, size);
    if (used == size) return size + strlen(src);
    return used + strlcpy(dst + used, src, size - used);
}

size_t wcslcpy(wchar_t* dst, const wchar_t* src, size_t size) {
    size_t n = wcslen(src);
    if (size) {
        size_t m = n < size - 1 ? n : size - 1;
        wmemcpy(dst, src, m);
        dst[m] = 0;
    }
    return n;
}

size_t wcslcat(wchar_t* dst, const wchar_t* src, size_t size) {
    size_t used = wcsnlen(dst, size);
    if (used == size) return size + wcslen(src);
    return used + wcslcpy(dst + used, src, size - used);
}

uint32_t arc4random(void) {
    uint32_t v = 0;
    if (syscall(SYS_getrandom, &v, sizeof v, 0) != (long)sizeof v) {
        // No getrandom (kernels before 3.17): fall back to /dev/urandom.
        FILE* f = fopen("/dev/urandom", "rb");
        if (f) {
            if (fread(&v, sizeof v, 1, f) != 1) v = 0;
            fclose(f);
        }
    }
    return v;
}
