#pragma once
// ---------------------------------------------------------------------------
// Minimal RIFF/WAVE (PCM) codec. The vanilla sound MPQs store effects + a lot
// of UI/ambient audio as uncompressed WAV; music and cinematics are MP3 (handed
// to the backend as raw bytes -- see audio.hpp/AudioClip). This decoder reads
// the canonical "fmt "/"data" chunk pair for integer PCM, skipping any other
// chunks (LIST/fact/etc). The tiny encoder exists so the audio path is fully
// unit-testable headlessly -- author a WAV, pack it in an MPQ, read it back.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <vector>

namespace wf {

// Decoded integer PCM. `samples` is interleaved little-endian by channel.
struct PcmData {
    uint32_t             sampleRate    = 0;
    uint16_t             channels      = 0;
    uint16_t             bitsPerSample = 0;
    std::vector<uint8_t> samples;      // interleaved frames

    uint32_t bytesPerFrame() const { return channels * (bitsPerSample / 8u); }
    size_t   frameCount()    const { uint32_t f = bytesPerFrame(); return f ? samples.size() / f : 0; }
    double   durationSeconds() const { return sampleRate ? double(frameCount()) / sampleRate : 0.0; }
    bool     valid()         const { return sampleRate && channels && bitsPerSample && !samples.empty(); }
};

// Parse a RIFF/WAVE PCM buffer. Returns a default (invalid) PcmData if the
// buffer is not a PCM WAVE (e.g. an MP3, or a compressed/extensible format).
PcmData parseWav(const std::vector<uint8_t>& buf);

// True if `buf` begins with a RIFF/WAVE header (cheap sniff, no full parse).
bool isWav(const std::vector<uint8_t>& buf);

// Encode integer PCM as a canonical 44-byte-header WAV (for tests / export).
std::vector<uint8_t> encodeWav(const PcmData& pcm);

} // namespace wf
