#include "render/jpeg.h"

#include <csetjmp>
#include <cstdio>
#include <utility>
#include <vector>

#include <jpeglib.h>

namespace fugue::assets {

namespace {
// libjpeg reports a fatal error by calling error_exit, which must not return: jump back to the armed setjmp instead of
// the default exit(). The message is kept for `why`.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)  // jmp_buf's required ABI alignment pads this error-manager wrapper.
#endif
struct ErrMgr {
    jpeg_error_mgr pub;
    std::jmp_buf jump;
    char msg[JMSG_LENGTH_MAX];
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
void onError(j_common_ptr c) {
    auto* e = reinterpret_cast<ErrMgr*>(c->err);
    (*c->err->format_message)(c, e->msg);
    std::longjmp(e->jump, 1);
}
void onMessage(j_common_ptr) {}  // warnings (e.g. a corrupt but recoverable stream) are not printed

// The setjmp frames below hold no object with a destructor: a longjmp across C++ unwind state is not portable.
struct Jpeg {
    jpeg_decompress_struct c;
    ErrMgr err;
};

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4611)  // These two C-style frames own no destructible objects; all owners live in decodeJpeg.
#endif
const char* readHeader(Jpeg& j, const uint8_t* d, size_t n, uint64_t maxPixels) {
    if (setjmp(j.err.jump)) return j.err.msg;
    jpeg_create_decompress(&j.c);
    jpeg_mem_src(&j.c, const_cast<unsigned char*>(d), static_cast<unsigned long>(n));
    if (jpeg_read_header(&j.c, TRUE) != JPEG_HEADER_OK) return "no image";
    if (j.c.jpeg_color_space == JCS_CMYK || j.c.jpeg_color_space == JCS_YCCK) return "CMYK is not supported";
    if (j.c.image_width == 0 || j.c.image_height == 0 || j.c.image_width > 8192 || j.c.image_height > 8192)
        return "unsupported image dimensions";
    if (uint64_t(j.c.image_width) * j.c.image_height > maxPixels) return "image size";
    j.c.out_color_space =
        JCS_RGB;  // grayscale expands; otherwise libjpeg's defaults (JDCT_ISLOW, as stock DreamDaemon uses)
    jpeg_calc_output_dimensions(&j.c);
    return nullptr;
}

const char* readPixels(Jpeg& j, uint8_t* px, uint8_t* row) {
    if (setjmp(j.err.jump)) return j.err.msg;
    jpeg_start_decompress(&j.c);
    const size_t w = j.c.output_width;
    while (j.c.output_scanline < j.c.output_height) {
        uint8_t* dst = px + size_t(j.c.output_scanline) * w * 4;
        JSAMPROW rp = row;
        jpeg_read_scanlines(&j.c, &rp, 1);
        for (size_t x = 0; x < w; x++) {
            dst[x * 4 + 0] = row[x * 3 + 0];
            dst[x * 4 + 1] = row[x * 3 + 1];
            dst[x * 4 + 2] = row[x * 3 + 2];
        }
    }
    jpeg_finish_decompress(&j.c);
    return nullptr;
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}  // namespace

bool decodeJpeg(const uint8_t* d, size_t n, RgbaImage& out, std::string& why, uint64_t maxPixels) {
    Jpeg j{};
    j.c.err = jpeg_std_error(&j.err.pub);
    j.err.pub.error_exit = onError;
    j.err.pub.output_message = onMessage;
    const char* bad = readHeader(j, d, n, maxPixels);
    if (!bad) {
        const int w = int(j.c.output_width), h = int(j.c.output_height);
        std::vector<uint8_t> px(size_t(w) * size_t(h) * 4, 255), row(size_t(w) * 3);
        bad = readPixels(j, px.data(), row.data());
        if (!bad) {
            out.w = w;
            out.h = h;
            out.px = std::move(px);
        }
    }
    jpeg_destroy_decompress(&j.c);
    if (bad) why = std::string("jpeg: ") + bad;
    return !bad;
}

}  // namespace fugue::assets
