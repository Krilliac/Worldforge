#include "sound_player.hpp"

namespace wf {

void SoundPlayer::setSoundEntries(std::vector<SoundEntry> entries) {
    entries_ = std::move(entries);
}

const SoundEntry* SoundPlayer::findEntry(uint32_t id) const {
    for (const SoundEntry& e : entries_)
        if (e.id == id) return &e;
    return nullptr;
}

VoiceId SoundPlayer::playFile(const std::string& archivedPath, const PlayParams& params) {
    AudioClip clip = loader_.soundClip(archivedPath);
    if (clip.empty()) return kInvalidVoice;
    VoiceId v = device_.play(clip, params);
    if (params.music && v != kInvalidVoice) music_ = v;
    return v;
}

VoiceId SoundPlayer::playSound(const std::string& archivedPath, float volume) {
    PlayParams p; p.volume = volume; p.loop = false; p.music = false;
    return playFile(archivedPath, p);
}

VoiceId SoundPlayer::playMusic(const std::string& archivedPath, bool loop, float volume) {
    PlayParams p; p.volume = volume; p.loop = loop; p.music = true;
    return playFile(archivedPath, p);
}

VoiceId SoundPlayer::playSoundId(uint32_t soundEntryId, float volume) {
    const SoundEntry* e = findEntry(soundEntryId);
    if (!e) return kInvalidVoice;
    const std::string path = e->firstPath();
    if (path.empty()) return kInvalidVoice;
    return playSound(path, volume * e->volume);
}

VoiceId SoundPlayer::playMusicId(uint32_t soundEntryId, bool loop, float volume) {
    const SoundEntry* e = findEntry(soundEntryId);
    if (!e) return kInvalidVoice;
    const std::string path = e->firstPath();
    if (path.empty()) return kInvalidVoice;
    return playMusic(path, loop, volume * e->volume);
}

void SoundPlayer::stopMusic() {
    if (music_ != kInvalidVoice) { device_.stop(music_); music_ = kInvalidVoice; }
}

void SoundPlayer::stopAll() {
    device_.stopAll();
    music_ = kInvalidVoice;
}

} // namespace wf
