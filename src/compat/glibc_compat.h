// Force-included into every source file of the portable Linux release (see CMakeLists.txt).
// Newer glibc versions added new symbol versions of these math functions (same behavior,
// faster or with errno fixes); binding to the original x86-64 versions lets the release run
// on older distributions. glibc_compat.c covers the functions older glibc lacks entirely.
#pragma once
#if defined(__x86_64__)
#define SM63_OLD_SYMBOL(name) __asm__(".symver " #name "," #name "@GLIBC_2.2.5");
#elif defined(__aarch64__)
#define SM63_OLD_SYMBOL(name) __asm__(".symver " #name "," #name "@GLIBC_2.17");
#else
#define SM63_OLD_SYMBOL(name)
#endif
SM63_OLD_SYMBOL(acosf)
SM63_OLD_SYMBOL(asinf)
SM63_OLD_SYMBOL(atan2f)
SM63_OLD_SYMBOL(log10f)
SM63_OLD_SYMBOL(sqrtf)
SM63_OLD_SYMBOL(fmod)
SM63_OLD_SYMBOL(fmodf)
SM63_OLD_SYMBOL(hypot)
#undef SM63_OLD_SYMBOL
