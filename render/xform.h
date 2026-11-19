// Affine transforms of one icon cell (applyTransform, lerpMatrix) plus dmMatrixToImage. Float64 throughout, no fused
// multiply-add (render/ is built -ffp-contract=off), trig through model/gomath.h so the results are bit-reproducible.
#pragma once
#include <array>
#include <cstdint>
#include <vector>

#include "render/ir.h"

namespace fugue::render {

// Matrix: x' = a*x + b*y + c, y' = d*x + e*y + f.
struct Mat {
    double a = 1, b = 0, c = 0, d = 0, e = 1, f = 0;
};

enum class Sampling { Nearest, Bilinear };

Mat matMul(const Mat& m, const Mat& n);  // Apply n first, then m
bool invertMat(const Mat& m, Mat& out);  // false when singular
// dmMatrixToImage: the wire matrix (DM, +y up, six float32 cells) -> the raster (+y down) matrix.
Mat dmMatrixToImage(const std::array<float, 6>& t);
// applyTransform: resample `src` (w x h straight-alpha RGBA8) about its centre; the output canvas is the
// bounding box of the linear part (translation is a pure shift applied after growth).
SpritePtr applyTransform(const Sprite& src, const Mat& m, Sampling s);
// warpAboutCentre: resample a whole viewport-sized RGBA8 surface (w x h) IN PLACE through a plane-master
// transform, keeping its size: the linear part [[a,b],[d,e]] (DM y-up) about the centre, then a shift of (tx,ty)
// IMAGE px; bilinear at pixel centres, premultiplied. Degenerate matrix -> fully transparent.
void warpAboutCentre(std::vector<uint8_t>& px, int w, int h, double a, double b, double d, double e, double tx,
                     double ty);
// lerpMatrix: rigid (decomposed) interpolation. false when either linear part is degenerate.
bool lerpMatrix(const Mat& from, const Mat& to, double t, Mat& out);

}  // namespace fugue::render
