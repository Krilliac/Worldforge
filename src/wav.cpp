#include "wav.hpp"
#include "byte_reader.hpp"

#include <cstring>

namespace wf {

bool isWav(const std::vector<uint8_t>& buf) {
    return buf.size() >= 12 &&
           std::memcmp(buf.data(),     "RIFF", 4) == 0 &&
           std::memcmp(buf.data() + 8, "WAVE", 4) == 0;
}

PcmData parseWav(const std::vector<uint8_t>& buf) {
    PcmData pcm;
    if (!isWav(buf)) return pcm;

    try {
        ByteReader r(buf);
        r.skip(12);   // "RIFF" <size> "WAVE"

        uint16_t fmt = 0;
        bool haveFmt = false, haveData = false;
        while (r.remaining() >= 8) {
            const std::string id = r.fourccRaw();
            uint32_t sz = r.u32();
            if (sz > r.remaining()) sz = static_cast<uint32_t>(r.remaining());

            if (id == "fmt ") {
                size_t end = r.pos() + sz;
                fmt              = r.u16();   // 1 = integer PCM
                pcm.channels     = r.u16();
                pcm.sampleRate   = r.u32();
                r.u32();                      // byteRate
                r.u16();                      // blockAlign
                pcm.bitsPerSample = r.u16();
                r.seek(end);                  // skip any extension bytes
                haveFmt = true;
            } else if (id == "data") {
                pcm.samples.assign(r.ptr(), r.ptr() + sz);
                r.skip(sz);
                haveData = true;
            } else {
                r.skip(sz);
            }
            if (sz & 1u) { if (r.remaining()) r.skip(1); }   // chunks are word-aligned
            if (haveFmt && haveData) break;
        }

        if (fmt != 1) return PcmData{};   // only integer PCM is decoded here
    } catch (...) {
        return PcmData{};
    }
    return pcm;
}

std::vector<uint8_t> encodeWav(const PcmData& pcm) {
    std::vector<uint8_t> b;
    auto p16 = [&](uint16_t v){ b.push_back(v & 0xFF); b.push_back((v >> 8) & 0xFF); };
    auto p32 = [&](uint32_t v){ for (int i = 0; i < 4; ++i) b.push_back((v >> (8*i)) & 0xFF); };
    auto praw = [&](const char* s){ for (int i = 0; i < 4; ++i) b.push_back((uint8_t)s[i]); };

    const uint32_t dataSize  = static_cast<uint32_t>(pcm.samples.size());
    const uint32_t blockAlign = pcm.channels * (pcm.bitsPerSample / 8u);
    const uint32_t byteRate   = pcm.sampleRate * blockAlign;

    praw("RIFF"); p32(36 + dataSize); praw("WAVE");
    praw("fmt "); p32(16);
    p16(1);                       // PCM
    p16(pcm.channels);
    p32(pcm.sampleRate);
    p32(byteRate);
    p16(static_cast<uint16_t>(blockAlign));
    p16(pcm.bitsPerSample);
    praw("data"); p32(dataSize);
    b.insert(b.end(), pcm.samples.begin(), pcm.samples.end());
    return b;
}

} // namespace wf
