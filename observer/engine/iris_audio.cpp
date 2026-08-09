// IRIS engine - audio via miniaudio. Spatial voices + 2D beds.
#include "iris.h"

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include <miniaudio/miniaudio.h>

#include <unordered_map>

namespace iris {

struct AudioEngine::Impl {
    ma_engine engine{};
    bool ok = false;
    bool disabled = false;
    std::vector<std::string> paths;                 // SoundId -> file path
    std::unordered_map<int32_t, ma_sound*> voices;  // VoiceId -> live sound
    int32_t nextVoice = 1;

    void reap() {
        for (auto it = voices.begin(); it != voices.end();) {
            if (!ma_sound_is_playing(it->second) && !ma_sound_is_looping(it->second)) {
                ma_sound_uninit(it->second);
                delete it->second;
                it = voices.erase(it);
            } else {
                ++it;
            }
        }
    }
};

AudioEngine::AudioEngine() : impl(new Impl) {}
AudioEngine::~AudioEngine() { shutdown(); }

bool AudioEngine::init(bool disabled) {
    impl->disabled = disabled;
    if (disabled) {
        IRIS_INFO("Audio disabled");
        return true;
    }
    ma_engine_config cfg = ma_engine_config_init();
    cfg.listenerCount = 1;
    if (ma_engine_init(&cfg, &impl->engine) != MA_SUCCESS) {
        IRIS_WARN("Audio device unavailable; continuing silent");
        impl->disabled = true;
        return true;
    }
    impl->ok = true;
    return true;
}

void AudioEngine::shutdown() {
    if (!impl) return;
    if (impl->ok) {
        stopAll();
        ma_engine_uninit(&impl->engine);
        impl->ok = false;
    }
}

SoundId AudioEngine::load(const std::string& path) {
    // Decoding is deferred: each play instantiates from file (miniaudio caches).
    impl->paths.push_back(path);
    return (SoundId)impl->paths.size() - 1;
}

VoiceId AudioEngine::play(SoundId snd, float volume, float pitch, bool loop, bool spatial, vec3 pos) {
    if (impl->disabled || snd < 0 || snd >= (SoundId)impl->paths.size()) return -1;
    impl->reap();
    ma_sound* s = new ma_sound();
    ma_uint32 flags = 0;
    if (!spatial) flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;
    if (ma_sound_init_from_file(&impl->engine, impl->paths[snd].c_str(), flags, nullptr, nullptr, s) !=
        MA_SUCCESS) {
        delete s;
        return -1;
    }
    ma_sound_set_volume(s, volume);
    ma_sound_set_pitch(s, pitch);
    ma_sound_set_looping(s, loop ? MA_TRUE : MA_FALSE);
    if (spatial) {
        ma_sound_set_position(s, pos.x, pos.y, pos.z);
        ma_sound_set_min_distance(s, 1.0f);
        ma_sound_set_max_distance(s, 24.0f);
        ma_sound_set_attenuation_model(s, ma_attenuation_model_linear);
    }
    ma_sound_start(s);
    VoiceId id = impl->nextVoice++;
    impl->voices[id] = s;
    return id;
}

void AudioEngine::stop(VoiceId v) {
    auto it = impl->voices.find(v);
    if (it == impl->voices.end()) return;
    ma_sound_stop(it->second);
    ma_sound_uninit(it->second);
    delete it->second;
    impl->voices.erase(it);
}

void AudioEngine::stopAll() {
    for (auto& [id, s] : impl->voices) {
        ma_sound_stop(s);
        ma_sound_uninit(s);
        delete s;
    }
    impl->voices.clear();
}

void AudioEngine::setVoiceVolume(VoiceId v, float vol) {
    auto it = impl->voices.find(v);
    if (it != impl->voices.end()) ma_sound_set_volume(it->second, vol);
}
void AudioEngine::setVoicePitch(VoiceId v, float p) {
    auto it = impl->voices.find(v);
    if (it != impl->voices.end()) ma_sound_set_pitch(it->second, p);
}
void AudioEngine::setVoicePos(VoiceId v, vec3 pos) {
    auto it = impl->voices.find(v);
    if (it != impl->voices.end()) ma_sound_set_position(it->second, pos.x, pos.y, pos.z);
}
bool AudioEngine::isPlaying(VoiceId v) {
    auto it = impl->voices.find(v);
    return it != impl->voices.end() && ma_sound_is_playing(it->second);
}

void AudioEngine::setListener(vec3 pos, vec3 fwd) {
    if (impl->disabled || !impl->ok) return;
    ma_engine_listener_set_position(&impl->engine, 0, pos.x, pos.y, pos.z);
    ma_engine_listener_set_direction(&impl->engine, 0, fwd.x, fwd.y, fwd.z);
    ma_engine_listener_set_world_up(&impl->engine, 0, 0, 1, 0);
}

void AudioEngine::setMasterVolume(float v) {
    if (impl->ok) ma_engine_set_volume(&impl->engine, v);
}

} // namespace iris
