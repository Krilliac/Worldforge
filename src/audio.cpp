#include "audio.hpp"

#include <cstring>

namespace wf {

namespace {
bool looksLikeMp3(const std::vector<uint8_t>& b) {
    if (b.size() < 3) return false;
    if (std::memcmp(b.data(), "ID3", 3) == 0) return true;          // ID3v2 tag
    return (b[0] == 0xFF && (b[1] & 0xE0) == 0xE0);                 // MPEG frame sync
}
} // namespace

AudioClip makeClip(const std::string& name, const std::vector<uint8_t>& fileBytes) {
    AudioClip clip;
    clip.name = name;
    if (fileBytes.empty()) { clip.codec = AudioClip::Codec::None; return clip; }

    if (isWav(fileBytes)) {
        clip.pcm = parseWav(fileBytes);
        if (clip.pcm.valid()) { clip.codec = AudioClip::Codec::Pcm; return clip; }
        // A WAV we can't decode (compressed/extensible): keep bytes for a backend.
        clip.encoded = fileBytes;
        clip.codec   = AudioClip::Codec::Unknown;
        return clip;
    }
    clip.encoded = fileBytes;
    clip.codec   = looksLikeMp3(fileBytes) ? AudioClip::Codec::Mp3 : AudioClip::Codec::Unknown;
    return clip;
}

// --- NullAudioDevice -------------------------------------------------------
VoiceId NullAudioDevice::play(const AudioClip& clip, const PlayParams& params) {
    if (clip.empty()) return kInvalidVoice;

    // A new music voice stops any currently-playing music voice (cross-fade
    // would live in a real backend; here we model the exclusivity).
    if (params.music) {
        for (Voice& v : voices_)
            if (v.active && v.params.music) v.active = false;
    }

    Voice v;
    v.id     = next_++;
    v.name   = clip.name;
    v.params = params;
    v.active = true;
    voices_.push_back(v);
    return v.id;
}

void NullAudioDevice::stop(VoiceId voice) {
    for (Voice& v : voices_)
        if (v.id == voice) v.active = false;
}

void NullAudioDevice::stopAll() {
    for (Voice& v : voices_) v.active = false;
}

size_t NullAudioDevice::activeVoices() const {
    size_t n = 0;
    for (const Voice& v : voices_) if (v.active) ++n;
    return n;
}

const NullAudioDevice::Voice* NullAudioDevice::voice(VoiceId id) const {
    for (const Voice& v : voices_) if (v.id == id) return &v;
    return nullptr;
}

void NullAudioDevice::finishOneShots() {
    for (Voice& v : voices_)
        if (v.active && !v.params.loop) v.active = false;
}

} // namespace wf
