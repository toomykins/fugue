// exp(), ported from Go's math.Exp for amd64 (exp_amd64.s, a SLEEF-derived polynomial; BSD-3-Clause, Copyright 2009
// The Go Authors; see THIRD-PARTY-NOTICES.md). It has two bodies, chosen at run time by CPU feature (AVX and FMA).
// Bit-exact results keep blur kernels and colour rounding deterministic across platforms; glibc's exp differs by an
// ulp on some inputs.
//
// Compiled with -ffp-contract=off (render/CMakeLists.txt): the non-FMA body must not fuse, and the FMA body fuses
// exactly where the assembly does (std::fma).
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#endif

namespace fugue::gomath {

namespace detail {
inline bool useFma() {
    static const bool v = [] {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
        int regs[4];
        __cpuid(regs, 1);
        constexpr int required = (1 << 12) | (1 << 27) | (1 << 28);  // FMA, OSXSAVE, AVX
        return (regs[2] & required) == required && (_xgetbv(0) & 6) == 6;
#elif defined(__x86_64__) || defined(__i386__)
        return __builtin_cpu_supports("avx") && __builtin_cpu_supports("fma");
#else
        return false;
#endif
    }();
    return v;
}
inline double bitsToDouble(uint64_t b) {
    double d;
    std::memcpy(&d, &b, 8);
    return d;
}
}  // namespace detail

// Test-only hook: -1 (the shipped behaviour) picks the body by CPU feature; 0 forces the non-FMA body, 1 the FMA body.
inline int& expForce() {
    static int f = -1;
    return f;
}

inline double exp(double x) {
    constexpr double LOG2E = 1.4426950408889634073599246810018920;
    constexpr double LN2U = 0.69314718055966295651160180568695068359375;
    constexpr double LN2L = 0.28235290563031577122588448175013436025525412068e-12;
    constexpr double Overflow = 7.09782712893384e+02;
    constexpr double c0 = 0.5, c8 = 1.0, c16 = 2.0, c24 = 1.6666666666666666667e-1, c32 = 4.1666666666666666667e-2,
                     c40 = 8.3333333333333333333e-3, c48 = 1.3888888888888888889e-3, c56 = 1.9841269841269841270e-4,
                     c64 = 2.4801587301587301587e-5;
    uint64_t bits;
    std::memcpy(&bits, &x, 8);
    constexpr uint64_t PosInf = 0x7FF0000000000000ull, NegInf = 0xFFF0000000000000ull;
    if ((bits & ~(1ull << 63)) >= PosInf) {  // not finite
        if (bits == NegInf) return 0;
        return x;  // NaN or +Inf
    }
    if (x > Overflow) return detail::bitsToDouble(PosInf);
    double x0 = x;
    double x1 = LOG2E * x0;
    int32_t bx = int32_t(std::nearbyint(x1));  // round to nearest even
    x1 = double(bx);
    bool fma = expForce() < 0 ? detail::useFma() : expForce() == 1;
    if (fma) {
        x0 = std::fma(-x1, LN2U, x0);
        x0 = std::fma(-x1, LN2L, x0);
        x0 *= 0.0625;
        x1 = c64;
        x1 = std::fma(x1, x0, c56);
        x1 = std::fma(x1, x0, c48);
        x1 = std::fma(x1, x0, c40);
        x1 = std::fma(x1, x0, c32);
        x1 = std::fma(x1, x0, c24);
        x1 = std::fma(x1, x0, c0);
        x1 = std::fma(x1, x0, c8);
        x0 = x0 * x1;
        x1 = x0 + c16;
        x0 = x0 * x1;
        x1 = x0 + c16;
        x0 = x0 * x1;
        x1 = x0 + c16;
        x0 = x0 * x1;
        x1 = x0 + c16;
        x0 = std::fma(x0, x1, c8);
    } else {
        x0 = x0 - x1 * LN2U;
        x0 = x0 - x1 * LN2L;
        x0 *= 0.0625;
        x1 = c64;
        x1 = x1 * x0;
        x1 = x1 + c56;
        x1 = x1 * x0;
        x1 = x1 + c48;
        x1 = x1 * x0;
        x1 = x1 + c40;
        x1 = x1 * x0;
        x1 = x1 + c32;
        x1 = x1 * x0;
        x1 = x1 + c24;
        x1 = x1 * x0;
        x1 = x1 + c0;
        x1 = x1 * x0;
        x1 = x1 + c8;
        x0 = x1 * x0;
        for (int k = 0; k < 4; k++) {
            x1 = c16 + x0;
            x0 = x0 * x1;
        }
        x0 = x0 + c8;
    }
    // ldexp
    int32_t b = bx + 0x3FF;
    if (b <= 0) {  // denormal
        if (b < -52) return 0;
        b += 0x3FE;
        x0 = x0 * detail::bitsToDouble(uint64_t(uint32_t(b)) << 52);
        b = 1;
    } else if (b >= 0x7FF) {
        return detail::bitsToDouble(PosInf);
    }
    return x0 * detail::bitsToDouble(uint64_t(uint32_t(b)) << 52);
}

}  // namespace fugue::gomath
