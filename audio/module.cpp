// Tracker modules (MOD/S3M/XM/IT) through libopenmpt, as a Stream per voice rather than decoded PCM.
//
// Repeat: a looping module keeps its own order list (repeat count -1), so a song whose end jumps back to a later order
// loops there, not from the top. An un-looped one stops where libopenmpt detects the end. Frequency: a module has no
// native rate, so it renders at the mixer's rate divided by the step. That changes pitch and tempo together, as
// resampling a sample does.
#include "audio/clip.h"
#if FUGUE_HAVE_OPENMPT
#include <libopenmpt/libopenmpt.hpp>
#include <sstream>
#endif

namespace fugue::audio {

#if FUGUE_HAVE_OPENMPT
namespace {
class ModuleStream : public Stream {
public:
    ModuleStream(const Bytes& b, bool loop, std::ostream& log) : m_(b.data(), b.data() + b.size(), log) {
        m_.set_repeat_count(loop ? -1 : 0);
    }
    size_t read(int rate, size_t frames, float* out) override { return m_.read_interleaved_stereo(rate, frames, out); }

private:
    openmpt::module m_;
};
}  // namespace

bool moduleDecoder() { return true; }

std::shared_ptr<const Clip> decodeModule(ByteSpan b, const std::string& kind, std::string* err, bool* unsupported) {
    if (unsupported) *unsupported = false;
    auto src = std::make_shared<const Bytes>(b.begin(), b.end());
    std::ostringstream log;  // libopenmpt's load warnings would otherwise go to std::clog on every play
    try {
        ModuleStream probe(*src, false, log);  // a file libopenmpt cannot load is refused here, not on every play
    } catch (const std::exception& e) {
        if (err) *err = kind + ": libopenmpt: " + e.what();
        return nullptr;
    }
    auto c = std::make_shared<Clip>();
    c->channels = 2;
    c->format = kind;
    c->sourceBytes = src->size();
    c->open = [src](bool loop) -> std::unique_ptr<Stream> {
        std::ostringstream quiet;
        try {
            return std::make_unique<ModuleStream>(*src, loop, quiet);
        } catch (const std::exception&) { return nullptr; }
    };
    return c;
}
#else
bool moduleDecoder() { return false; }

std::shared_ptr<const Clip> decodeModule(ByteSpan, const std::string& kind, std::string* err, bool* unsupported) {
    if (err) *err = kind + " (built without libopenmpt)";
    if (unsupported) *unsupported = true;
    return nullptr;
}
#endif

}  // namespace fugue::audio
