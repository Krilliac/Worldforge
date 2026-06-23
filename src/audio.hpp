#pragma once
// ---------------------------------------------------------------------------
// Audio abstraction, mirroring the RHI split (rhi::Device): a backend-agnostic
// AudioDevice plus a headless NullAudioDevice that records every voice it is
// asked to play. That makes the whole sound path -- extract from the sound MPQ,
// decode/route, mix, start/stop voices -- unit-testable with no audio hardware,
// the same way SoftwareDevice makes rendering testable. A real backend
// (miniaudio, gated WFORGE_AUDIO) implements the same interface for the host:
// WAV is decoded to PCM here; MP3 music/cinematics are passed through as encoded
// bytes for the backend's own decoder.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string>
#include <vector>

#include "wav.hpp"

namespace wf {

// A playable sound. Either decoded integer PCM (WAV) or the raw encoded file
// bytes (MP3 etc.) for a backend that decodes them itself.
struct AudioClip {
    std::string          name;       // archived path it came from (diagnostics)
    PcmData              pcm;        // populated for WAV
    std::vector<uint8_t> encoded;    // raw bytes when pcm is empty (e.g. MP3)
    enum class Codec { None, Pcm, Mp3, Unknown };
    Codec codec = Codec::None;

    bool   isPcm()  const { return codec == Codec::Pcm && pcm.valid(); }
    bool   empty()  const { return codec == Codec::None; }
    // Best-effort length in seconds (0 for encoded clips we don't decode here).
    double durationSeconds() const { return isPcm() ? pcm.durationSeconds() : 0.0; }
};

// Wrap a file's bytes (e.g. read from the MPQ) in an AudioClip: WAV is decoded,
// MP3 kept as encoded bytes, anything else marked Unknown (bytes preserved).
AudioClip makeClip(const std::string& name, const std::vector<uint8_t>& fileBytes);

// A started sound. >=0 is a valid voice handle; kInvalidVoice means "not played".
using VoiceId = int;
constexpr VoiceId kInvalidVoice = -1;

struct PlayParams {
    float volume = 1.0f;   // 0..1, scaled by the device master volume
    bool  loop   = false;  // ambience / music loop
    bool  music  = false;  // a "music" voice: starting one stops the previous
};

class AudioDevice {
public:
    virtual ~AudioDevice() = default;

    // Start a voice for `clip`. Returns kInvalidVoice if the clip is empty.
    virtual VoiceId play(const AudioClip& clip, const PlayParams& params = {}) = 0;
    virtual void    stop(VoiceId voice)        = 0;
    virtual void    stopAll()                  = 0;
    virtual void    setMasterVolume(float v)   = 0;
    virtual float   masterVolume() const       = 0;
    // Number of voices currently considered playing.
    virtual size_t  activeVoices() const       = 0;
};

// Headless device: no output, but records what was played so callers/tests can
// assert on it. Looping voices stay "active" until stopped; one-shots can be
// retired with finishOneShots() (a stand-in for natural completion).
class NullAudioDevice : public AudioDevice {
public:
    struct Voice {
        VoiceId     id = kInvalidVoice;
        std::string name;
        PlayParams  params;
        bool        active = true;
    };

    VoiceId play(const AudioClip& clip, const PlayParams& params = {}) override;
    void    stop(VoiceId voice)      override;
    void    stopAll()                override;
    void    setMasterVolume(float v) override { master_ = v; }
    float   masterVolume() const     override { return master_; }
    size_t  activeVoices() const     override;

    // ---- test / inspection helpers ----------------------------------------
    const std::vector<Voice>& history() const { return voices_; }   // all, incl. stopped
    const Voice* voice(VoiceId id) const;
    int  playCount() const { return static_cast<int>(voices_.size()); }
    void finishOneShots();   // retire non-looping active voices (simulate end)

private:
    std::vector<Voice> voices_;
    VoiceId            next_   = 0;
    float              master_ = 1.0f;
};

} // namespace wf
