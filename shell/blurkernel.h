// Bounded standalone stock Gaussian plan; no model state or persistent cache.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace fugue::shell {
struct StockBlurSteps {
    std::array<float,6> sizes{};
    int count = 0;
};
inline StockBlurSteps stockBlurSteps(double requested) {
    StockBlurSteps out;
    if (!std::isfinite(requested) || requested <= 0) return out;
    float size = float(std::min(requested,100.0));
    // Large passes use a scaled short kernel; the residual halves until a
    // final pair whose squared sizes sum to the remaining squared size.
    while (size > 6) {
        if (double(size) > std::sqrt(72.0)) {
            out.sizes[out.count++] = size;
            size *= .5f;
        } else {
            out.sizes[out.count++] = float(std::sqrt(double(size*size-36.f)));
            size = 6;
        }
    }
    out.sizes[out.count++] = size;
    return out;
}
struct StockBlurKernel {
    // Center, then paired positive taps; negative taps are symmetric. Packed
    // as five vec4 uniforms, fitting the existing 256-byte uniform alignment.
    std::array<float,20> taps{};
    int radius = 0;
};
inline StockBlurKernel stockBlurKernel(float size) {
    StockBlurKernel out;
    if (!std::isfinite(size) || size <= 0 || size > 100) return out;
    const double sigma = std::min(double(size),6.0);
    const double scale = size > 6 ? double(size)/7 : 1;
    std::array<double,19> weights{};
    weights[0] = 1/(sigma*std::sqrt(2*3.14159265358979323846));
    double total = weights[0];
    int count = 1;
    for (int i = 1; i < 19; ++i) {
        const double weight = weights[0]*std::exp(-double(i*i)/(2*sigma*sigma));
        // Stop only before a pair, so every bilinear pair is complete.
        if ((i&1) && weight < .001) break;
        weights[i] = weight; total += 2*weight; ++count;
    }
    out.taps[1] = float(weights[0]/total);
    for (int i = 1; i < count; i += 2) {
        const int tap = (i+1)/2;
        const double weight = weights[i]+weights[i+1];
        const float offset = float((i+weights[i+1]/weight)*scale);
        out.taps[2*tap] = offset;
        out.taps[2*tap+1] = float(weight/total);
        out.radius = int(std::ceil(offset));
    }
    return out;
}
}
