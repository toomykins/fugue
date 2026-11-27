// Ports of Go standard-library math functions (BSD-3-Clause, Copyright 2009 The Go Authors; see THIRD-PARTY-NOTICES.md).
// Bit-exact results keep glides, transforms and easing deterministic across platforms, whatever the platform's libm.
// sin/cos/atan are the Cephes forms and hypot is max*sqrt(1+(min/max)^2). Arguments with |x| >= 2^29 fall back to libm.
// Must be compiled with -ffp-contract=off, so that a*b+c is never fused. model/ is.
#pragma once
#include <cmath>
#include <cstdint>

namespace fugue::gomath {

constexpr double kPi = 3.14159265358979323846;     // == float64(pi)
constexpr double k4OverPi = 0x1.45f306dc9c883p+0;  // float64(4/pi), correctly rounded const division

inline double hypot(double p, double q) {
    p = std::fabs(p);
    q = std::fabs(q);
    if (std::isinf(p) || std::isinf(q)) return INFINITY;
    if (std::isnan(p) || std::isnan(q)) return NAN;
    if (p < q) {
        double t = p;
        p = q;
        q = t;
    }
    if (p == 0) return 0;
    q = q / p;
    return p * std::sqrt(1 + q * q);
}

inline double xatan(double x) {
    const double P0 = -8.750608600031904122785e-01, P1 = -1.615753718733365076637e+01,
                 P2 = -7.500855792314704667340e+01, P3 = -1.228866684490136173410e+02,
                 P4 = -6.485021904942025371773e+01;
    const double Q0 = +2.485846490142306297962e+01, Q1 = +1.650270098316988542046e+02,
                 Q2 = +4.328810604912902668951e+02, Q3 = +4.853903996359136964868e+02,
                 Q4 = +1.945506571482613964425e+02;
    double z = x * x;
    z = z * ((((P0 * z + P1) * z + P2) * z + P3) * z + P4) / (((((z + Q0) * z + Q1) * z + Q2) * z + Q3) * z + Q4);
    z = x * z + x;
    return z;
}
inline double satan(double x) {
    const double Morebits = 6.123233995736765886130e-17, Tan3pio8 = 2.41421356237309504880;
    if (x <= 0.66) return xatan(x);
    if (x > Tan3pio8) return kPi / 2 - xatan(1 / x) + Morebits;
    return kPi / 4 + xatan((x - 1) / (x + 1)) + 0.5 * Morebits;
}
inline double atan(double x) {
    if (x == 0) return x;
    if (x > 0) return satan(x);
    return -satan(-x);
}
inline double atan2(double y, double x) {
    if (std::isnan(y) || std::isnan(x)) return NAN;
    if (y == 0) {
        if (x >= 0 && !std::signbit(x)) return std::copysign(0.0, y);
        return std::copysign(kPi, y);
    }
    if (x == 0) return std::copysign(kPi / 2, y);
    if (std::isinf(x)) {
        if (x > 0) return std::isinf(y) ? std::copysign(kPi / 4, y) : std::copysign(0.0, y);
        return std::isinf(y) ? std::copysign(3 * kPi / 4, y) : std::copysign(kPi, y);
    }
    if (std::isinf(y)) return std::copysign(kPi / 2, y);
    double q = atan(y / x);
    if (x < 0) {
        if (q <= 0) return q + kPi;
        return q - kPi;
    }
    return q;
}

// cos and sin share the reduction.
namespace detail {
constexpr double kSin[6] = {1.58962301576546568060e-10, -2.50507477628578072866e-8, 2.75573136213857245213e-6,
                            -1.98412698295895385996e-4, 8.33333333332211858878e-3,  -1.66666666666666307295e-1};
constexpr double kCos[6] = {-1.13585365213876817300e-11, 2.08757008419747316778e-9,  -2.75573141792967388112e-7,
                            2.48015872888517045348e-5,   -1.38888888888730564116e-3, 4.16666666666665929218e-2};
constexpr double PI4A = 7.85398125648498535156e-1, PI4B = 3.77489470793079817668e-8, PI4C = 2.69515142907905952645e-15;
constexpr double kReduceThreshold = 536870912.0;  // 1<<29
}  // namespace detail

inline double cos(double x) {
    using namespace detail;
    if (std::isnan(x) || std::isinf(x)) return NAN;
    bool sign = false;
    x = std::fabs(x);
    if (x >= kReduceThreshold) return std::cos(x);  // large argument: libm
    uint64_t j = uint64_t(x * k4OverPi);
    double y = double(j);
    if (j & 1) {
        j++;
        y++;
    }
    j &= 7;
    double z = ((x - y * PI4A) - y * PI4B) - y * PI4C;
    if (j > 3) {
        j -= 4;
        sign = !sign;
    }
    if (j > 1) sign = !sign;
    double zz = z * z;
    if (j == 1 || j == 2)
        y = z + z * zz * ((((((kSin[0] * zz) + kSin[1]) * zz + kSin[2]) * zz + kSin[3]) * zz + kSin[4]) * zz + kSin[5]);
    else
        y = 1.0 - 0.5 * zz +
            zz * zz * ((((((kCos[0] * zz) + kCos[1]) * zz + kCos[2]) * zz + kCos[3]) * zz + kCos[4]) * zz + kCos[5]);
    return sign ? -y : y;
}

inline double sin(double x) {
    using namespace detail;
    if (x == 0 || std::isnan(x)) return x;
    if (std::isinf(x)) return NAN;
    bool sign = false;
    if (x < 0) {
        x = -x;
        sign = true;
    }
    if (x >= kReduceThreshold) return sign ? -std::sin(x) : std::sin(x);  // large argument: libm
    uint64_t j = uint64_t(x * k4OverPi);
    double y = double(j);
    if (j & 1) {
        j++;
        y++;
    }
    j &= 7;
    double z = ((x - y * PI4A) - y * PI4B) - y * PI4C;
    if (j > 3) {
        sign = !sign;
        j -= 4;
    }
    double zz = z * z;
    if (j == 1 || j == 2)
        y = 1.0 - 0.5 * zz +
            zz * zz * ((((((kCos[0] * zz) + kCos[1]) * zz + kCos[2]) * zz + kCos[3]) * zz + kCos[4]) * zz + kCos[5]);
    else
        y = z + z * zz * ((((((kSin[0] * zz) + kSin[1]) * zz + kSin[2]) * zz + kSin[3]) * zz + kSin[4]) * zz + kSin[5]);
    return sign ? -y : y;
}

// Half away from zero.
inline double round(double x) { return std::round(x); }

}  // namespace fugue::gomath
