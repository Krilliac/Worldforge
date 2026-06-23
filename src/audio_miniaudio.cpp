#include "audio_miniaudio.hpp"

#ifdef WFORGE_AUDIO

// One translation unit defines the implementation. We don't need miniaudio's
// own file I/O (the engine reads from MPQ-extracted memory), but the default
// build is fine and keeps the integration simple.
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

namespace wf {

// A playing voice owns its decoded PCM (kept alive for the sound's lifetime),
// the audio buffer that views it, and the ma_sound bound to the engine.
struct MiniAudioDevice::Voice {
    std::vector<uint8_t> pcm;        // owned interleaved samples
    ma_audio_buffer      buffer{};
    ma_sound             sound{};
    bool                 bufferInit = false;
    bool                 soundInit  = false;

    ~Voice() {
        if (soundInit)  ma_sound_uninit(&sound);
        if (bufferInit) ma_audio_buffer_uninit(&buffer);
    }
};

namespace {
// Map our integer PCM bit depth to a miniaudio sample format.
bool toMaFormat(uint16_t bits, ma_format& out) {
    switch (bits) {
        case 8:  out = ma_format_u8;  return true;
        case 16: out = ma_format_s16; return true;
        case 24: out = ma_format_s24; return true;
        case 32: out = ma_format_s32; return true;
        default: return false;
    }
}

// Decode an encoded (e.g. MP3) clip's bytes into interleaved s16 PCM.
bool decodeEncoded(const std::vector<uint8_t>& bytes, std::vector<uint8_t>& outPcm,
                   ma_format& fmt, uint32_t& channels, uint32_t& rate) {
    ma_decoder dec;
    ma_decoder_config cfg = ma_decoder_config_init_default();
    if (ma_decoder_init_memory(bytes.data(), bytes.size(), &cfg, &dec) != MA_SUCCESS)
        return false;
    fmt      = dec.outputFormat;
    channels = dec.outputChannels;
    rate     = dec.outputSampleRate;

    ma_uint64 total = 0;
    ma_decoder_get_length_in_pcm_frames(&dec, &total);
    const ma_uint32 frameSize = ma_get_bytes_per_frame(fmt, channels);

    std::vector<uint8_t> buf;
    std::vector<uint8_t> chunk(4096u * frameSize);
    ma_uint64 read = 0;
    for (;;) {
        ma_uint64 got = 0;
        ma_result r = ma_decoder_read_pcm_frames(&dec, chunk.data(), 4096, &got);
        if (got == 0) break;
        buf.insert(buf.end(), chunk.begin(), chunk.begin() + got * frameSize);
        read += got;
        if (r != MA_SUCCESS) break;
    }
    ma_decoder_uninit(&dec);
    (void)total; (void)read;
    outPcm = std::move(buf);
    return !outPcm.empty();
}
} // namespace

MiniAudioDevice::MiniAudioDevice() : engine_(new ma_engine) {
    ok_ = (ma_engine_init(nullptr, engine_.get()) == MA_SUCCESS);
}

MiniAudioDevice::~MiniAudioDevice() {
    voices_.clear();               // uninit sounds before the engine
    if (ok_) ma_engine_uninit(engine_.get());
}

VoiceId MiniAudioDevice::play(const AudioClip& clip, const PlayParams& params) {
    if (!ok_ || clip.empty()) return kInvalidVoice;

    auto v = std::make_unique<Voice>();
    ma_format fmt = ma_format_s16;
    uint32_t channels = 0, rate = 0;

    if (clip.isPcm()) {
        if (!toMaFormat(clip.pcm.bitsPerSample, fmt)) return kInvalidVoice;
        channels = clip.pcm.channels;
        rate     = clip.pcm.sampleRate;
        v->pcm   = clip.pcm.samples;
    } else {
        if (!decodeEncoded(clip.encoded, v->pcm, fmt, channels, rate)) return kInvalidVoice;
    }
    if (v->pcm.empty() || channels == 0 || rate == 0) return kInvalidVoice;

    const ma_uint64 frameCount = v->pcm.size() / ma_get_bytes_per_frame(fmt, channels);
    ma_audio_buffer_config bcfg =
        ma_audio_buffer_config_init(fmt, channels, frameCount, v->pcm.data(), nullptr);
    bcfg.sampleRate = rate;
    if (ma_audio_buffer_init(&bcfg, &v->buffer) != MA_SUCCESS) return kInvalidVoice;
    v->bufferInit = true;

    if (ma_sound_init_from_data_source(engine_.get(), &v->buffer,
                                       MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr,
                                       &v->sound) != MA_SUCCESS)
        return kInvalidVoice;
    v->soundInit = true;

    ma_sound_set_volume(&v->sound, params.volume);
    ma_sound_set_looping(&v->sound, params.loop ? MA_TRUE : MA_FALSE);

    // Exclusive music: stop any currently-playing music voice first.
    if (params.music) {
        for (auto& kv : voices_)
            ma_sound_stop(&kv.second->sound);
    }

    if (ma_sound_start(&v->sound) != MA_SUCCESS) return kInvalidVoice;

    VoiceId id = next_++;
    voices_[id] = std::move(v);
    return id;
}

void MiniAudioDevice::stop(VoiceId voice) {
    auto it = voices_.find(voice);
    if (it != voices_.end()) voices_.erase(it);   // dtor stops + uninits
}

void MiniAudioDevice::stopAll() { voices_.clear(); }

void MiniAudioDevice::setMasterVolume(float v) {
    master_ = v;
    if (ok_) ma_engine_set_volume(engine_.get(), v);
}

float MiniAudioDevice::masterVolume() const { return master_; }

size_t MiniAudioDevice::activeVoices() const {
    size_t n = 0;
    for (const auto& kv : voices_)
        if (ma_sound_is_playing(&kv.second->sound)) ++n;
    return n;
}

void MiniAudioDevice::reapFinished() {
    for (auto it = voices_.begin(); it != voices_.end(); ) {
        if (ma_sound_at_end(&it->second->sound)) it = voices_.erase(it);
        else ++it;
    }
}

} // namespace wf

#endif // WFORGE_AUDIO
