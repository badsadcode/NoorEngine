#pragma once
// Audio (ADR-011: miniaudio) as a runtime system (ADR-027).
//
// Components:
//   "audio_source":   {"clip": "<asset id or Assets/... path>", "volume": 1, "pitch": 1,
//                      "loop": false, "spatial": true, "min_distance": 1, "max_distance": 60,
//                      "bus": "sfx", "play_on_start": true, "stream": false}
//                     A sound placed at its entity (spatial) or heard everywhere at the same
//                     volume (spatial = false, e.g. music). "stream" reads long files gradually
//                     instead of decoding them up front.
//   "audio_listener": {} The ears: usually on the camera. Without one, the first `camera` entity
//                     listens; without that, the listener stays at the origin.
// Buses: master, music, sfx, ui, voice (each source and one-shot plays on one; volumes per bus).
//
// Without a sound device (Options::use_device = false, the environment variable
// NOOR_AUDIO_DEVICE=none, or no device available) the engine still mixes, into memory: sounds
// play, end and report "playing" exactly as with speakers, and tests can measure the result.

#include "noor/runtime/world.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace noor::audio {

class AudioSystem final : public runtime::System {
public:
    struct Options {
        bool use_device = true;
        std::uint32_t sample_rate = 48000;
        // Without a device: advance (mix and discard) as much audio as game time passes, so sounds
        // end on time. Tests that measure the mix themselves with mix() turn this off.
        bool advance_without_device = true;
        // A clip reference (an asset id, or a project-relative path like "Assets/Audio/step.wav") to
        // a file on disk.
        std::function<std::optional<std::filesystem::path>(std::string_view clip)> resolve_clip;
    };

    explicit AudioSystem(Options options);
    ~AudioSystem() override;
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    [[nodiscard]] std::string_view name() const override { return "audio"; }
    void entity_started(runtime::World& world, scene::Entity& entity) override;
    void late_update(runtime::World& world, double dt) override;
    void entity_destroyed(runtime::World& world, const scene::Entity& entity) override;
    void shutdown(runtime::World& world) override;

    // ---- The scripting API ----
    bool play(const Guid& entity);  // (re)starts the entity's audio_source
    void stop(const Guid& entity);
    [[nodiscard]] bool is_playing(const Guid& entity) const;
    // Fire-and-forget: at a world position (3D) or everywhere (no position).
    bool play_one_shot(std::string_view clip, std::optional<math::Vec3> position, float volume = 1.0f, std::string_view bus = "sfx");
    void set_bus_volume(std::string_view bus, float volume);  // master, music, sfx, ui, voice
    [[nodiscard]] float bus_volume(std::string_view bus) const;

    // ---- State, tests ----
    [[nodiscard]] bool device_open() const noexcept;      // speakers in use
    [[nodiscard]] std::size_t playing_count() const;       // sources + one-shots currently sounding
    // Without a device: mixes `frames` stereo frames and returns the loudest sample (0..1).
    [[nodiscard]] float mix(std::uint32_t frames);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace noor::audio
