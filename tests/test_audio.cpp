#include "test.hpp"
#include "wav.hpp"
#include "audio.hpp"
#include "sound_entries.hpp"
#include "sound_player.hpp"
#include "asset_loader.hpp"
#include "dbc_defs.hpp"
#include "mpq.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

using namespace wf;

namespace {
// A short sine-ish PCM clip (mono 16-bit), content irrelevant for these tests.
PcmData makePcm(uint32_t rate, uint16_t ch, uint16_t bits, size_t frames) {
    PcmData p; p.sampleRate = rate; p.channels = ch; p.bitsPerSample = bits;
    p.samples.resize(frames * ch * (bits / 8));
    for (size_t i = 0; i < p.samples.size(); ++i) p.samples[i] = uint8_t(i * 7 + 1);
    return p;
}
uint32_t f2u(float f){ uint32_t u; std::memcpy(&u, &f, 4); return u; }
} // namespace

void test_audio() {
    std::printf("[audio]\n");

    // --- WAV round-trip ------------------------------------------------------
    PcmData src = makePcm(22050, 1, 16, 100);
    std::vector<uint8_t> wav = encodeWav(src);
    CHECK(isWav(wav));
    PcmData back = parseWav(wav);
    CHECK(back.valid());
    CHECK(back.sampleRate == 22050 && back.channels == 1 && back.bitsPerSample == 16);
    CHECK(back.frameCount() == 100);
    CHECK(back.samples == src.samples);
    CHECK_APPROX(back.durationSeconds(), 100.0 / 22050.0);
    // A non-WAV buffer parses to invalid, not a throw.
    CHECK(!parseWav(std::vector<uint8_t>{1,2,3,4}).valid());

    // --- makeClip codec detection -------------------------------------------
    AudioClip wavClip = makeClip("a.wav", wav);
    CHECK(wavClip.isPcm() && wavClip.codec == AudioClip::Codec::Pcm);
    CHECK(wavClip.durationSeconds() > 0.0);

    std::vector<uint8_t> mp3 = { 'I','D','3', 3,0,0, 0,0,0,0 };   // ID3v2 header
    AudioClip mp3Clip = makeClip("b.mp3", mp3);
    CHECK(mp3Clip.codec == AudioClip::Codec::Mp3 && !mp3Clip.isPcm());
    CHECK(mp3Clip.encoded == mp3);

    CHECK(makeClip("empty", {}).empty());

    // --- NullAudioDevice voice policy ---------------------------------------
    NullAudioDevice dev;
    VoiceId s1 = dev.play(wavClip, PlayParams{});
    CHECK(s1 != kInvalidVoice && dev.activeVoices() == 1);
    CHECK(dev.play(makeClip("x", {}), {}) == kInvalidVoice);   // empty clip ignored

    // Two music voices: the second stops the first (exclusive music).
    PlayParams music; music.music = true; music.loop = true;
    VoiceId m1 = dev.play(mp3Clip, music);
    CHECK(dev.voice(m1)->active);
    VoiceId m2 = dev.play(mp3Clip, music);
    CHECK(!dev.voice(m1)->active && dev.voice(m2)->active);

    // One-shots retire on "completion"; the looping music survives.
    dev.finishOneShots();
    CHECK(!dev.voice(s1)->active);     // one-shot ended
    CHECK(dev.voice(m2)->active);      // loop still playing
    dev.stopAll();
    CHECK(dev.activeVoices() == 0);

    // --- AssetLoader::soundClip + SoundEntries + SoundPlayer over a real MPQ -
    // Author a SoundEntries.dbc (29 fields, vanilla layout) with one record
    // pointing at our WAV; pack both into an MPQ; resolve + play by id.
    DbcBuilder b(29);
    uint32_t nameOff = b.addString("TestSound");
    uint32_t fileOff = b.addString("test.wav");
    uint32_t dirOff  = b.addString("Sound\\Test");
    std::vector<uint32_t> rec(29, 0);
    rec[0] = 555;                  // id
    rec[1] = 1;                    // type
    rec[2] = nameOff;              // name
    rec[3] = fileOff;              // File[0]
    rec[23] = dirOff;              // DirectoryBase
    rec[24] = f2u(0.5f);           // volume
    b.addRecord(rec);
    std::vector<uint8_t> dbcBlob = b.build();

    Dbc dbc = Dbc::parse(dbcBlob);
    SoundEntry e = soundEntry(dbc, 0);
    CHECK(e.id == 555 && e.name == "TestSound");
    CHECK(e.files[0] == "test.wav" && e.directory == "Sound\\Test");
    CHECK(e.path(0) == "Sound\\Test\\test.wav");
    CHECK(e.firstPath() == "Sound\\Test\\test.wav");
    CHECK_APPROX(e.volume, 0.5f);

    const char* mpqPath = "wforge_audio_test.mpq";
    std::vector<std::pair<std::string, std::vector<uint8_t>>> files = {
        { "Sound\\Test\\test.wav", wav },
        { "DBFilesClient\\SoundEntries.dbc", dbcBlob },
    };
    CHECK(writeMpqArchive(mpqPath, files));
    MpqManager mgr;
    CHECK(mgr.addArchive(mpqPath));
    AssetLoader loader(mgr);

    // Extract straight from the MPQ as a decoded clip.
    AudioClip fromMpq = loader.soundClip("Sound\\Test\\test.wav");
    CHECK(fromMpq.isPcm() && fromMpq.pcm.frameCount() == 100);
    CHECK(loader.soundClip("Sound\\Missing.wav").empty());

    // Player: resolve SoundEntries id -> file -> play through the device.
    NullAudioDevice pdev;
    SoundPlayer player(loader, pdev);
    std::vector<uint8_t> seBuf;
    CHECK(loader.sound("DBFilesClient\\SoundEntries.dbc", seBuf));
    player.setSoundEntries(soundEntries(Dbc::parse(seBuf)));

    VoiceId v = player.playSoundId(555);
    CHECK(v != kInvalidVoice);
    CHECK(pdev.voice(v)->name == "Sound\\Test\\test.wav");
    CHECK_APPROX(pdev.voice(v)->params.volume, 0.5f);   // 1.0 * entry volume
    CHECK(player.playSoundId(9999) == kInvalidVoice);    // unknown id

    // Music exclusivity through the player.
    VoiceId mu1 = player.playMusic("Sound\\Test\\test.wav");
    CHECK(mu1 != kInvalidVoice && player.currentMusic() == mu1);
    player.stopMusic();
    CHECK(player.currentMusic() == kInvalidVoice);
    CHECK(!pdev.voice(mu1)->active);

    std::remove(mpqPath);
}
