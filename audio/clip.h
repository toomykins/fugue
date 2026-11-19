// Audio: resource bytes to PCM (this file), a DM-semantics channel mixer (mixer.h), a platform sink (sink.h), and the
// seam from the 0x006d wire command (player.h). No Qt or sockets; everything is testable by rendering to a buffer or
// a WAV file.
//
// The decoder is chosen by magic bytes, not filename: a `.ogg` resource may hold another format. Ogg decoding uses
// stb_vorbis (audio/third_party); tracker modules use libopenmpt (audio/module.cpp).
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "net/bytes.h"

namespace fugue::audio {

// A sequenced source rendered on demand: a tracker module, which loops by its own order list (a looping one has no
// length to hold as PCM). One per playing voice; only the mixer touches it, under its lock.
class Stream {
public:
    virtual ~Stream() = default;
    // Up to `frames` interleaved stereo frames rendered at `rate` Hz into `out`; fewer = the song ended, 0 = already over.
    virtual size_t read(int rate, size_t frames, float* out) = 0;
};

// A decoded sound: interleaved float samples in [-1,1], `channels` per frame, at the file's own rate. A module is the
// other shape: no samples and rate 0 ("the mixer's own"), and `open` makes each voice its own Stream (`loop` = the
// module's repeat count -1, so a looping song follows its own order list instead of restarting from the top).
struct Clip {
    int rate = 0;
    int channels = 0;
    std::vector<float> samples;
    std::string format;  // "ogg", "wav", "mod", ... - reporting only
    std::function<std::unique_ptr<Stream>(bool loop)> open;
    size_t sourceBytes = 0;  // what `open` holds (the module file), for the clip cache's budget
    bool streamed() const { return bool(open); }
    size_t frames() const { return channels > 0 ? samples.size() / size_t(channels) : 0; }
};

// The container a payload is in, from its magic bytes: "ogg" "wav" "mp3" "midi" "flac" "aiff" "xm" "s3m" "it" "mod",
// or "".
std::string sniff(ByteSpan b);

// Decode by magic bytes. nullptr on failure with `err` set. `unsupported` marks a recognised-but-refused container (MIDI,
// MP3 for now, tracker modules without libopenmpt) or an unrecognised one, as opposed to a corrupt file of a supported
// type. These are counted separately so unplayable sounds are reportable.
std::shared_ptr<const Clip> decode(ByteSpan b, std::string* err = nullptr, bool* unsupported = nullptr);

std::shared_ptr<const Clip> decodeWav(ByteSpan b, std::string* err, bool* unsupported);
std::shared_ptr<const Clip> decodeOgg(ByteSpan b, std::string* err);
// A tracker module as a streamed Clip. Refused as unsupported when this build has no libopenmpt (FUGUE_OPENMPT,
// audio/CMakeLists.txt).
std::shared_ptr<const Clip> decodeModule(ByteSpan b, const std::string& kind, std::string* err, bool* unsupported);
bool moduleDecoder();  // this build plays tracker modules

// 16-bit PCM RIFF/WAVE of interleaved float samples. Tests assert against the file, not a sound card.
Bytes encodeWav16(int rate, int channels, const std::vector<float>& samples);
// Saturating float -> s16 (a mix that overshoots must clip, not wrap into noise).
int16_t toS16(float s);

}  // namespace fugue::audio
