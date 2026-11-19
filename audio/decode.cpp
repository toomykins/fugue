#include "audio/clip.h"
#include <cmath>
#include <cstring>

// stb_vorbis's public API only (its implementation is compiled once, in stb_vorbis_impl.cpp, with the same defines).
#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "audio/third_party/stb_vorbis.c"

namespace fugue::audio {
namespace {
uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t rd32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
bool at(ByteSpan b, size_t off, const char* tag) {
    size_t n = std::strlen(tag);
    return b.size() >= off + n && std::memcmp(b.data() + off, tag, n) == 0;
}
void fail(std::string* err, bool* unsupported, std::string why, bool unsup) {
    if (err) *err = std::move(why);
    if (unsupported) *unsupported = unsup;
}
}  // namespace

// Magic bytes cover MOD's tag at offset 1080, IT's "IMPM", and the N-channel MOD tags ("6CHN", "16CH") that a fixed
// list would miss.
std::string sniff(ByteSpan b) {
    if (at(b, 0, "OggS")) return "ogg";
    if (at(b, 0, "RIFF") && at(b, 8, "WAVE")) return "wav";
    if (at(b, 0, "MThd")) return "midi";
    if (at(b, 0, "ID3")) return "mp3";
    if (b.size() >= 2 && b[0] == 0xff && (b[1] & 0xe0) == 0xe0) return "mp3";
    if (at(b, 0, "fLaC")) return "flac";
    if (at(b, 0, "FORM")) return "aiff";
    if (at(b, 0, "Extended Module: ")) return "xm";
    if (at(b, 0x2c, "SCRM")) return "s3m";
    if (at(b, 0, "IMPM")) return "it";
    if (b.size() >= 0x43c) {
        static const char* tags[] = {"M.K.", "M!K!", "M&K!", "N.T.", "FLT4", "FLT8", "CD81", "OKTA", "OCTA"};
        for (const char* t : tags)
            if (at(b, 0x438, t)) return "mod";
        const uint8_t* t = b.data() + 0x438;
        auto dig = [](uint8_t c) { return c >= '0' && c <= '9'; };
        if (dig(t[0]) && t[0] != '0' && std::memcmp(t + 1, "CHN", 3) == 0) return "mod";  // 1CHN..9CHN
        if (dig(t[0]) && dig(t[1]) && (std::memcmp(t + 2, "CH", 2) == 0 || std::memcmp(t + 2, "CN", 2) == 0))
            return "mod";  // 10CH..32CH
    }
    return "";
}

std::shared_ptr<const Clip> decode(ByteSpan b, std::string* err, bool* unsupported) {
    if (unsupported) *unsupported = false;
    std::string k = sniff(b);
    if (k == "ogg") return decodeOgg(b, err);
    if (k == "wav") return decodeWav(b, err, unsupported);
    if (k == "mod" || k == "s3m" || k == "xm" || k == "it") return decodeModule(b, k, err, unsupported);
    // MIDI needs a synth, and there is no MP3 decoder in this client yet.
    fail(err, unsupported, k.empty() ? "unrecognised container (" + std::to_string(b.size()) + " bytes)" : k, true);
    return nullptr;
}

std::shared_ptr<const Clip> decodeOgg(ByteSpan b, std::string* err) {
    int e = 0;
    stb_vorbis* v = stb_vorbis_open_memory(b.data(), int(b.size()), &e, nullptr);
    if (!v) {
        fail(err, nullptr, "ogg: stb_vorbis error " + std::to_string(e), false);
        return nullptr;
    }
    stb_vorbis_info info = stb_vorbis_get_info(v);
    auto c = std::make_shared<Clip>();
    c->rate = int(info.sample_rate);
    c->channels = info.channels;
    c->format = "ogg";
    if (c->rate <= 0 || c->channels <= 0) {
        stb_vorbis_close(v);
        fail(err, nullptr, "ogg: nonsense format", false);
        return nullptr;
    }
    std::vector<float> buf(size_t(4096) * size_t(c->channels));
    for (;;) {
        int n = stb_vorbis_get_samples_float_interleaved(v, c->channels, buf.data(), int(buf.size()));
        if (n <= 0) break;
        c->samples.insert(c->samples.end(), buf.begin(), buf.begin() + long(n) * c->channels);
    }
    stb_vorbis_close(v);
    return c;
}

// WAV parsing walks the chunks (a LIST chunk may sit between fmt and data). It reads PCM u8/s16/s24/s32 and IEEE
// float32, takes WAVE_FORMAT_EXTENSIBLE's real tag from the SubFormat GUID, and accepts a truncated final chunk as far
// as it goes. Anything else is refused rather than read as PCM.
std::shared_ptr<const Clip> decodeWav(ByteSpan b, std::string* err, bool* unsupported) {
    if (!at(b, 0, "RIFF") || !at(b, 8, "WAVE")) {
        fail(err, unsupported, "not a RIFF/WAVE file", true);
        return nullptr;
    }
    uint16_t format = 0;
    int channels = 0, rate = 0, bits = 0;
    bool haveFmt = false;
    ByteSpan data;
    bool haveData = false;
    for (size_t off = 12; off + 8 <= b.size();) {
        const uint8_t* h = b.data() + off;
        size_t sz = rd32(h + 4), body = off + 8;
        if (body + sz > b.size()) sz = b.size() - body;
        if (std::memcmp(h, "fmt ", 4) == 0) {
            if (sz < 16) {
                fail(err, unsupported, "wav: short fmt chunk", false);
                return nullptr;
            }
            const uint8_t* f = b.data() + body;
            format = rd16(f);
            channels = rd16(f + 2);
            rate = int(rd32(f + 4));
            bits = rd16(f + 14);
            if (format == 0xfffe && sz >= 40) format = rd16(f + 24);
            haveFmt = true;
        } else if (std::memcmp(h, "data", 4) == 0) {
            data = b.subspan(body, sz);
            haveData = true;
        }
        off = body + sz + (sz & 1);
    }
    if (!haveFmt || !haveData || channels <= 0 || rate <= 0) {
        fail(err, unsupported, "wav: missing or nonsense fmt/data", false);
        return nullptr;
    }
    auto c = std::make_shared<Clip>();
    c->rate = rate;
    c->channels = channels;
    c->format = "wav";
    const uint8_t* d = data.data();
    size_t n = data.size();
    if (format == 1 && bits == 8) {
        for (size_t i = 0; i < n; i++) c->samples.push_back((float(d[i]) - 128.f) / 128.f);
    } else if (format == 1 && bits == 16) {
        for (size_t i = 0; i + 2 <= n; i += 2) c->samples.push_back(float(int16_t(rd16(d + i))) / 32768.f);
    } else if (format == 1 && bits == 24) {
        for (size_t i = 0; i + 3 <= n; i += 3) {
            uint32_t u = uint32_t(d[i]) | uint32_t(d[i + 1]) << 8 | uint32_t(d[i + 2]) << 16;
            if (u & 0x800000) u |= 0xff000000u;
            c->samples.push_back(float(int32_t(u)) / 8388608.f);
        }
    } else if (format == 1 && bits == 32) {
        for (size_t i = 0; i + 4 <= n; i += 4) c->samples.push_back(float(double(int32_t(rd32(d + i))) / 2147483648.0));
    } else if (format == 3 && bits == 32) {
        for (size_t i = 0; i + 4 <= n; i += 4) {
            uint32_t u = rd32(d + i);
            float f;
            std::memcpy(&f, &u, 4);
            c->samples.push_back(f);
        }
    } else {
        fail(err, unsupported, "wav: format tag " + std::to_string(format) + " at " + std::to_string(bits) + " bits",
             true);
        return nullptr;
    }
    c->samples.resize(c->samples.size() - c->samples.size() % size_t(channels));
    return c;
}

int16_t toS16(float s) {
    double v = double(s) * 32767.0;
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return int16_t(std::lround(v));
}

Bytes encodeWav16(int rate, int channels, const std::vector<float>& samples) {
    Bytes o;
    auto p32 = [&](uint32_t v) {
        for (int i = 0; i < 4; i++) o.push_back(uint8_t(v >> (8 * i)));
    };
    auto p16 = [&](uint16_t v) {
        o.push_back(uint8_t(v));
        o.push_back(uint8_t(v >> 8));
    };
    auto tag = [&](const char* t) { o.insert(o.end(), t, t + 4); };
    uint32_t dataLen = uint32_t(samples.size() * 2);
    tag("RIFF");
    p32(36 + dataLen);
    tag("WAVE");
    tag("fmt ");
    p32(16);
    p16(1);
    p16(uint16_t(channels));
    p32(uint32_t(rate));
    p32(uint32_t(rate * channels * 2));
    p16(uint16_t(channels * 2));
    p16(16);
    tag("data");
    p32(dataLen);
    o.reserve(o.size() + dataLen);
    for (float s : samples) p16(uint16_t(toS16(s)));
    return o;
}

}  // namespace fugue::audio
