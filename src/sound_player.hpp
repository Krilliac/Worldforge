#pragma once
// ---------------------------------------------------------------------------
// SoundPlayer: the bridge between assets and audio output. It extracts a sound
// or music file from the MPQ chain (via AssetLoader) and starts it on an
// AudioDevice, applying the right voice policy:
//   * playSound  -- a one-shot effect.
//   * playMusic  -- a looping, exclusive music voice (starting new music stops
//                   the old; the music MPQ stores these as MP3 -> encoded clip).
//   * playSoundId/playMusicId -- resolve a SoundEntries.dbc id to its file
//                   first, the way the server's PLAY_SOUND / PLAY_MUSIC do.
// This is the engine-side endpoint for the FX bridge's PLAY_SOUND / PLAY_MUSIC
// opcodes (clientfx). With a NullAudioDevice it is fully testable headless.
// ---------------------------------------------------------------------------
#include <string>
#include <vector>

#include "asset_loader.hpp"
#include "audio.hpp"
#include "sound_entries.hpp"

namespace wf {

class SoundPlayer {
public:
    SoundPlayer(AssetLoader& loader, AudioDevice& device)
        : loader_(loader), device_(device) {}

    // Optionally give the player a SoundEntries table so it can resolve ids.
    void setSoundEntries(std::vector<SoundEntry> entries);

    // Play an archived file directly. Returns the voice, or kInvalidVoice if the
    // file is missing/undecodable.
    VoiceId playFile(const std::string& archivedPath, const PlayParams& params);
    VoiceId playSound(const std::string& archivedPath, float volume = 1.0f);
    VoiceId playMusic(const std::string& archivedPath, bool loop = true, float volume = 1.0f);

    // Resolve a SoundEntries id -> file, then play. kInvalidVoice if the id is
    // unknown or its file can't be extracted.
    VoiceId playSoundId(uint32_t soundEntryId, float volume = 1.0f);
    VoiceId playMusicId(uint32_t soundEntryId, bool loop = true, float volume = 1.0f);

    void stopMusic();
    void stopAll();

    VoiceId currentMusic() const { return music_; }
    const SoundEntry* findEntry(uint32_t id) const;

private:
    AssetLoader&            loader_;
    AudioDevice&            device_;
    std::vector<SoundEntry> entries_;
    VoiceId                 music_ = kInvalidVoice;
};

} // namespace wf
