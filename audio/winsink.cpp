// Native Windows PCM output. A bounded queue provides backpressure to Player's output thread.
#include "audio/sink.h"
#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#include <array>
#include "audio/clip.h"

namespace fugue::audio {
namespace {
class WaveSink final : public Sink {
public:
    ~WaveSink() override { close(); }
    bool open(int rate, std::string* err) {
        event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event_) {
            if (err) *err = "cannot create audio event";
            return false;
        }
        WAVEFORMATEX fmt{};
        fmt.wFormatTag = WAVE_FORMAT_PCM;
        fmt.nChannels = 2;
        fmt.nSamplesPerSec = DWORD(rate);
        fmt.wBitsPerSample = 16;
        fmt.nBlockAlign = 4;
        fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
        return check(waveOutOpen(&out_, WAVE_MAPPER, &fmt, DWORD_PTR(event_), 0, CALLBACK_EVENT), err);
    }
    bool write(const float* samples, size_t frames, std::string* err) override {
        if (!out_) {
            if (err) *err = "audio device closed";
            return false;
        }
        auto& b = buffers_[next_];
        if (b.header.dwFlags & WHDR_PREPARED) {
            const ULONGLONG deadline = GetTickCount64() + 2000;
            while (!(b.header.dwFlags & WHDR_DONE)) {
                if (GetTickCount64() >= deadline) {
                    if (err) *err = "audio device stalled";
                    return false;
                }
                WaitForSingleObject(event_, 50);
            }
            if (!check(waveOutUnprepareHeader(out_, &b.header, sizeof(WAVEHDR)), err)) return false;
        }
        if (frames > (1u << 24)) {
            if (err) *err = "audio block too large";
            return false;
        }
        b.samples.resize(frames * 2);
        for (size_t i = 0; i < b.samples.size(); ++i) b.samples[i] = toS16(samples[i]);
        b.header = {};
        b.header.lpData = reinterpret_cast<LPSTR>(b.samples.data());
        b.header.dwBufferLength = DWORD(b.samples.size() * sizeof(int16_t));
        if (!check(waveOutPrepareHeader(out_, &b.header, sizeof(WAVEHDR)), err)) return false;
        if (!check(waveOutWrite(out_, &b.header, sizeof(WAVEHDR)), err)) return false;
        next_ = (next_ + 1) % buffers_.size();
        return true;
    }
    void close() override {
        if (out_) {
            waveOutReset(out_);
            for (auto& b : buffers_)
                if (b.header.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(out_, &b.header, sizeof(WAVEHDR));
            waveOutClose(out_);
            out_ = nullptr;
        }
        if (event_) {
            CloseHandle(event_);
            event_ = nullptr;
        }
    }
    std::string name() const override { return "waveout"; }

private:
    static bool check(MMRESULT result, std::string* err) {
        if (result == MMSYSERR_NOERROR) return true;
        char text[MAXERRORLENGTH]{};
        waveOutGetErrorTextA(result, text, MAXERRORLENGTH);
        if (err) *err = std::string("waveOut: ") + text;
        return false;
    }
    struct Buffer {
        WAVEHDR header{};
        std::vector<int16_t> samples;
    };
    std::array<Buffer, 3> buffers_{};
    size_t next_ = 0;
    HWAVEOUT out_ = nullptr;
    HANDLE event_ = nullptr;
};
}  // namespace
std::unique_ptr<Sink> openWindowsSink(int rate, std::string* err) {
    auto sink = std::make_unique<WaveSink>();
    if (!sink->open(rate, err)) return nullptr;
    return sink;
}
}  // namespace fugue::audio
#endif
