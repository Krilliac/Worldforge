#pragma once
// ---------------------------------------------------------------------------
// MiniAudioDevice: the host realisation of AudioDevice, backed by miniaudio
// (a single-header, zero-dependency audio library that opens the OS device on
// Windows/macOS/Linux). It is the audio analog of the OpenGL RHI backend:
// gated behind WFORGE_AUDIO and OFF by default, because it needs a real audio
// device, while NullAudioDevice is the tested realisation in headless CI.
//
// Both clip kinds the engine produces are handled: PCM (decoded WAV) plays from
// an in-memory audio buffer; encoded clips (MP3 music/cinematics) are decoded by
// miniaudio's built-in decoder from their raw bytes. Each voice owns its sample
// data for its lifetime, so AudioClips need not outlive the play() call.
//
// Build: -DWFORGE_AUDIO=ON (FetchContent pulls miniaudio). Compiled out entirely
// otherwise, so this header is only meaningful in a WFORGE_AUDIO build.
// ---------------------------------------------------------------------------
#ifdef WFORGE_AUDIO

#include <map>
#include <memory>

#include "audio.hpp"

struct ma_engine;   // opaque (miniaudio types live in the .cpp)

namespace wf {

class MiniAudioDevice : public AudioDevice {
public:
    MiniAudioDevice();
    ~MiniAudioDevice() override;

    MiniAudioDevice(const MiniAudioDevice&)            = delete;
    MiniAudioDevice& operator=(const MiniAudioDevice&) = delete;

    bool ok() const { return ok_; }   // false if the audio device failed to open

    VoiceId play(const AudioClip& clip, const PlayParams& params = {}) override;
    void    stop(VoiceId voice)        override;
    void    stopAll()                  override;
    void    setMasterVolume(float v)   override;
    float   masterVolume() const       override;
    size_t  activeVoices() const       override;

    // Retire voices that have finished playing (call periodically from the host
    // loop so one-shot resources are freed).
    void    reapFinished();

private:
    struct Voice;
    std::unique_ptr<ma_engine>            engine_;
    std::map<VoiceId, std::unique_ptr<Voice>> voices_;
    VoiceId next_   = 0;
    float   master_ = 1.0f;
    bool    ok_     = false;
};

} // namespace wf

#endif // WFORGE_AUDIO
