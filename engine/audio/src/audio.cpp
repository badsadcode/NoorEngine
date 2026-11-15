#include "noor/audio/audio.h"

#include "noor/foundation/log.h"
#include "noor/runtime/script_api.h"

#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <string>
#include <unordered_map>
#include <vector>

namespace noor::audio {
namespace {

using asset::Json;
namespace api = runtime::api;

constexpr const char* kBuses[] = {"music", "sfx", "ui", "voice"};

float number(const Json& j, const char* key, float fallback) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<float>() : fallback;
}
bool flag(const Json& j, const char* key, bool fallback) {
    const auto it = j.find(key);
    return it != j.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

// miniaudio objects must be uninitialised (detached from the engine's node graph) before their
// memory goes away; these owners make that impossible to forget.
struct SoundDeleter {
    void operator()(ma_sound* s) const {
        ma_sound_uninit(s);
        delete s;
    }
};
struct GroupDeleter {
    void operator()(ma_sound_group* g) const {
        ma_sound_group_uninit(g);
        delete g;
    }
};
using Sound = std::unique_ptr<ma_sound, SoundDeleter>;
using Group = std::unique_ptr<ma_sound_group, GroupDeleter>;

bool device_disabled_by_environment() {
#if defined(_MSC_VER)
    char* value = nullptr;
    std::size_t length = 0;
    const bool off = _dupenv_s(&value, &length, "NOOR_AUDIO_DEVICE") == 0 && value && std::string(value) == "none";
    std::free(value);
    return off;
#else
    const char* value = std::getenv("NOOR_AUDIO_DEVICE");
    return value && std::string(value) == "none";
#endif
}

} // namespace

struct AudioSystem::Impl {
    Options options;
    ma_engine engine{};
    bool engine_ok = false;
    bool device = false;
    std::unordered_map<std::string, Group> buses;
    struct Source {
        Sound sound;
        bool spatial = true;
    };
    std::unordered_map<Guid, Source> sources;
    std::vector<Sound> one_shots;
    std::vector<float> scratch;

    explicit Impl(Options o) : options(std::move(o)) {
        const bool want_device = options.use_device && !device_disabled_by_environment();
        ma_engine_config config = ma_engine_config_init();
        config.channels = 2;
        config.sampleRate = options.sample_rate;
        config.noDevice = want_device ? MA_FALSE : MA_TRUE;
        if (ma_engine_init(&config, &engine) != MA_SUCCESS && want_device) {
            NOOR_LOG_WARN("Audio", "No sound device could be opened; sounds play silently");
            config.noDevice = MA_TRUE;
            if (ma_engine_init(&config, &engine) != MA_SUCCESS) {
                NOOR_LOG_ERROR("Audio", "The audio engine could not start");
                return;
            }
        } else if (!want_device) {
            // noDevice init can't fail for a reason a retry would fix; report if it did.
        }
        engine_ok = true;
        device = config.noDevice == MA_FALSE;
        for (const char* name : kBuses) {
            auto* group = new ma_sound_group{};
            if (ma_sound_group_init(&engine, 0, nullptr, group) == MA_SUCCESS) buses[name] = Group(group);
            else delete group;
        }
        NOOR_LOG_INFO("Audio", "Audio ready ({}, {} Hz)", device ? "sound device" : "no device: mixing to memory", options.sample_rate);
    }

    ~Impl() {
        sources.clear();  // sounds, then their groups, then the engine
        one_shots.clear();
        buses.clear();
        if (engine_ok) ma_engine_uninit(&engine);
    }

    ma_sound_group* bus(std::string_view name) {
        const auto it = buses.find(std::string(name));
        return it == buses.end() ? nullptr : it->second.get();
    }

    Sound load(std::string_view clip, bool stream, std::string_view bus_name, std::string& error) {
        if (!engine_ok) {
            error = "the audio engine isn't running";
            return nullptr;
        }
        std::optional<std::filesystem::path> path = options.resolve_clip ? options.resolve_clip(clip) : std::nullopt;
        if (!path && std::filesystem::exists(std::filesystem::path(std::string(clip)))) path = std::filesystem::path(std::string(clip));
        if (!path) {
            error = std::format("unknown clip \"{}\"", clip);
            return nullptr;
        }
        auto* raw = new ma_sound{};
        const ma_uint32 flags = stream ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
        const ma_result r = ma_sound_init_from_file_w(&engine, path->wstring().c_str(), flags, bus(bus_name.empty() ? "sfx" : bus_name), nullptr, raw);
        if (r != MA_SUCCESS) {
            delete raw;  // never initialised: nothing to uninit
            error = std::format("can't play \"{}\" ({})", path->filename().string(), ma_result_description(r));
            return nullptr;
        }
        Sound sound(raw);
        ma_sound_set_doppler_factor(sound.get(), 0.0f);  // no pitch warble when things teleport
        return sound;
    }

    void place(ma_sound* s, math::Vec3 p) { ma_sound_set_position(s, p.x, p.y, p.z); }

    void clean_one_shots() {
        std::erase_if(one_shots, [](const Sound& s) { return !ma_sound_is_playing(s.get()) && ma_sound_at_end(s.get()); });
    }

    float mix(std::uint32_t frames) {
        if (!engine_ok || device) return 0.0f;
        scratch.resize(static_cast<std::size_t>(frames) * 2);
        ma_uint64 read = 0;
        ma_engine_read_pcm_frames(&engine, scratch.data(), frames, &read);
        float peak = 0.0f;
        for (std::size_t i = 0; i < static_cast<std::size_t>(read) * 2; ++i) peak = std::max(peak, std::fabs(scratch[i]));
        return peak;
    }
};

AudioSystem::AudioSystem(Options options) : impl_(std::make_unique<Impl>(std::move(options))) {}
AudioSystem::~AudioSystem() = default;

bool AudioSystem::device_open() const noexcept { return impl_->device; }

void AudioSystem::entity_started(runtime::World& world, scene::Entity& entity) {
    const auto c = entity.components.find("audio_source");
    if (c == entity.components.end() || !c->is_object()) return;
    auto& d = *impl_;
    const std::string clip = c->value("clip", std::string{});
    std::string error;
    auto sound = clip.empty() ? nullptr : d.load(clip, flag(*c, "stream", false), c->value("bus", std::string("sfx")), error);
    if (!sound) {
        NOOR_LOG_ERROR("Audio", "'{}': audio_source not created: {}", entity.name, clip.empty() ? std::string("no clip set") : error);
        return;
    }
    ma_sound* s = sound.get();
    ma_sound_set_volume(s, number(*c, "volume", 1.0f));
    ma_sound_set_pitch(s, std::max(0.01f, number(*c, "pitch", 1.0f)));
    ma_sound_set_looping(s, flag(*c, "loop", false) ? MA_TRUE : MA_FALSE);
    const bool spatial = flag(*c, "spatial", true);
    ma_sound_set_spatialization_enabled(s, spatial ? MA_TRUE : MA_FALSE);
    ma_sound_set_attenuation_model(s, ma_attenuation_model_inverse);
    ma_sound_set_min_distance(s, std::max(0.01f, number(*c, "min_distance", 1.0f)));
    ma_sound_set_max_distance(s, std::max(0.02f, number(*c, "max_distance", 60.0f)));
    if (spatial) {
        if (const auto p = api::world_position(world, entity.id)) d.place(s, *p);
    }
    if (flag(*c, "play_on_start", true)) ma_sound_start(s);
    d.sources[entity.id] = {std::move(sound), spatial};
}

void AudioSystem::late_update(runtime::World& world, double dt) {
    auto& d = *impl_;
    if (!d.engine_ok) return;
    // The ears: an audio_listener, else the first camera.
    std::optional<Guid> ears;
    for (const auto& e : world.scene().entities) {
        if (e.has("audio_listener")) {
            ears = e.id;
            break;
        }
        if (!ears && e.has("camera")) ears = e.id;
    }
    if (ears) {
        if (const auto p = api::world_position(world, *ears)) ma_engine_listener_set_position(&d.engine, 0, p->x, p->y, p->z);
        const auto f = api::forward(world, *ears);
        const auto u = api::up(world, *ears);
        if (f) ma_engine_listener_set_direction(&d.engine, 0, f->x, f->y, f->z);
        if (u) ma_engine_listener_set_world_up(&d.engine, 0, u->x, u->y, u->z);
    }
    for (auto& [id, src] : d.sources) {
        if (!src.spatial) continue;
        if (const auto p = api::world_position(world, id)) d.place(src.sound.get(), *p);
    }
    if (!d.device && d.options.advance_without_device && dt > 0) {
        (void)d.mix(static_cast<std::uint32_t>(std::min(dt, 0.25) * d.options.sample_rate));
    }
    d.clean_one_shots();
}

void AudioSystem::entity_destroyed(runtime::World&, const scene::Entity& entity) {
    auto& d = *impl_;
    d.sources.erase(entity.id);
}

void AudioSystem::shutdown(runtime::World&) {
    auto& d = *impl_;
    d.sources.clear();
    d.one_shots.clear();
}

bool AudioSystem::play(const Guid& entity) {
    const auto it = impl_->sources.find(entity);
    if (it == impl_->sources.end()) return false;
    ma_sound_seek_to_pcm_frame(it->second.sound.get(), 0);
    return ma_sound_start(it->second.sound.get()) == MA_SUCCESS;
}

void AudioSystem::stop(const Guid& entity) {
    if (const auto it = impl_->sources.find(entity); it != impl_->sources.end()) ma_sound_stop(it->second.sound.get());
}

bool AudioSystem::is_playing(const Guid& entity) const {
    const auto it = impl_->sources.find(entity);
    return it != impl_->sources.end() && ma_sound_is_playing(it->second.sound.get());
}

bool AudioSystem::play_one_shot(std::string_view clip, std::optional<math::Vec3> position, float volume, std::string_view bus) {
    auto& d = *impl_;
    std::string error;
    auto sound = d.load(clip, false, bus, error);
    if (!sound) {
        NOOR_LOG_ERROR("Audio", "play_sound: {}", error);
        return false;
    }
    ma_sound_set_volume(sound.get(), volume);
    ma_sound_set_spatialization_enabled(sound.get(), position ? MA_TRUE : MA_FALSE);
    ma_sound_set_attenuation_model(sound.get(), ma_attenuation_model_inverse);
    if (position) d.place(sound.get(), *position);
    ma_sound_start(sound.get());
    d.one_shots.push_back(std::move(sound));
    return true;
}

void AudioSystem::set_bus_volume(std::string_view bus, float volume) {
    auto& d = *impl_;
    if (!d.engine_ok) return;
    volume = std::max(0.0f, volume);
    if (bus == "master") ma_engine_set_volume(&d.engine, volume);
    else if (auto* g = d.bus(bus)) ma_sound_group_set_volume(g, volume);
}

float AudioSystem::bus_volume(std::string_view bus) const {
    auto& d = *impl_;
    if (!d.engine_ok) return 0.0f;
    if (bus == "master") return ma_engine_get_volume(&d.engine);
    auto* g = d.bus(bus);
    return g ? ma_sound_group_get_volume(g) : 0.0f;
}

std::size_t AudioSystem::playing_count() const {
    std::size_t n = 0;
    for (const auto& [id, src] : impl_->sources) n += ma_sound_is_playing(src.sound.get()) ? 1 : 0;
    for (const auto& s : impl_->one_shots) n += ma_sound_is_playing(s.get()) ? 1 : 0;
    return n;
}

float AudioSystem::mix(std::uint32_t frames) { return impl_->mix(frames); }

} // namespace noor::audio
