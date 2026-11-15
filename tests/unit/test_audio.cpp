// Audio (miniaudio, ADR-011) as a runtime system. No sound card needed: the engine mixes into
// memory and the tests measure the result (silence, sound, louder near, quieter far).
#include "noor/audio/audio.h"
#include "noor/foundation/guid.h"
#include "noor/runtime/script_api.h"
#include "noor/runtime/world.h"
#include "noor/scripting/lua_scripts.h"

#include <ostream>
#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>

using namespace noor;
using audio::AudioSystem;
namespace stdfs = std::filesystem;

namespace {

// A mono 16-bit WAV: a 440 Hz tone at half volume.
stdfs::path write_tone(double seconds) {
    const auto path = stdfs::temp_directory_path() / ("noor_tone_" + Guid::generate().to_string() + ".wav");
    const std::uint32_t rate = 44100;
    const auto frames = static_cast<std::uint32_t>(seconds * rate);
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](std::uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4);
    u32(36 + frames * 2);
    f.write("WAVEfmt ", 8);
    u32(16);
    u16(1);  // PCM
    u16(1);  // mono
    u32(rate);
    u32(rate * 2);
    u16(2);
    u16(16);
    f.write("data", 4);
    u32(frames * 2);
    for (std::uint32_t i = 0; i < frames; ++i) {
        const auto s = static_cast<std::int16_t>(16383.0 * std::sin(2.0 * 3.14159265358979 * 440.0 * i / rate));
        f.write(reinterpret_cast<const char*>(&s), 2);
    }
    return path;
}

struct Fixture {
    stdfs::path tone = write_tone(1.0);
    Guid listener = Guid::generate();
    Guid near_src = Guid::generate();
    Guid far_src = Guid::generate();
    runtime::World world;
    AudioSystem* audio = nullptr;

    explicit Fixture(bool both = true) : world(make(both)) {
        AudioSystem::Options o;
        o.use_device = false;
        o.advance_without_device = false;  // the tests mix themselves
        o.resolve_clip = [this](std::string_view clip) -> std::optional<stdfs::path> {
            if (clip == "tone") return tone;
            return std::nullopt;
        };
        auto a = std::make_unique<AudioSystem>(std::move(o));
        audio = a.get();
        world.add_system(std::move(a));
    }
    ~Fixture() {
        std::error_code ec;
        stdfs::remove(tone, ec);
    }

    scene::Scene make(bool both) {
        scene::Scene s;
        scene::Entity ears;
        ears.id = listener;
        ears.name = "Ears";
        ears.components["audio_listener"] = asset::Json::object();
        s.entities.push_back(ears);
        scene::Entity a;
        a.id = near_src;
        a.name = "Near";
        a.transform.position = {0, 0, -2};
        a.components["audio_source"] = {{"clip", "tone"}, {"loop", true}, {"play_on_start", false}};
        s.entities.push_back(a);
        if (both) {
            scene::Entity b;
            b.id = far_src;
            b.name = "Far";
            b.transform.position = {0, 0, -40};
            b.components["audio_source"] = {{"clip", "tone"}, {"loop", true}, {"play_on_start", false}};
            s.entities.push_back(b);
        }
        return s;
    }
};

} // namespace

TEST_CASE("audio: silence until something plays, a source plays, loops and stops") {
    Fixture f(false);
    f.world.frame(0.016, {});
    CHECK_FALSE(f.audio->device_open());
    CHECK(f.audio->mix(4800) < 1e-4f);
    REQUIRE(f.audio->play(f.near_src));
    CHECK(f.audio->is_playing(f.near_src));
    CHECK(f.audio->mix(4800) > 0.05f);
    CHECK(f.audio->mix(48000 * 2) > 0.05f);  // 2 s of a 1 s clip: still looping
    CHECK(f.audio->is_playing(f.near_src));
    f.audio->stop(f.near_src);
    CHECK_FALSE(f.audio->is_playing(f.near_src));
    (void)f.audio->mix(4800);              // the stop's short fade-out (no click)
    CHECK(f.audio->mix(4800) < 1e-3f);     // then silence
}

TEST_CASE("audio: 3D sources are quieter far away, bus volume 0 silences them") {
    float near_peak = 0, far_peak = 0;
    {
        Fixture f;
        f.world.frame(0.016, {});
        f.audio->play(f.near_src);
        (void)f.audio->mix(2400);
        near_peak = f.audio->mix(4800);
    }
    {
        Fixture f;
        f.world.frame(0.016, {});
        f.audio->play(f.far_src);
        (void)f.audio->mix(2400);
        far_peak = f.audio->mix(4800);
        f.audio->set_bus_volume("sfx", 0.0f);
        (void)f.audio->mix(2400);
        CHECK(f.audio->mix(4800) < 1e-4f);
        CHECK(f.audio->bus_volume("sfx") == 0.0f);
    }
    CHECK(near_peak > 0.05f);
    CHECK(far_peak > 0.0f);
    CHECK(near_peak > far_peak * 4.0f);  // 2 m vs 40 m with inverse attenuation
}

TEST_CASE("audio: one-shots play once and are cleaned up, bad clips are reported, not fatal") {
    Fixture f(false);
    f.world.frame(0.016, {});
    CHECK(f.audio->play_one_shot("tone", std::nullopt, 1.0f));
    CHECK(f.audio->playing_count() == 1);
    CHECK(f.audio->mix(4800) > 0.05f);
    (void)f.audio->mix(48000);  // past the 1 s clip
    f.world.frame(0.016, {});   // late_update cleans finished one-shots
    CHECK(f.audio->playing_count() == 0);
    CHECK_FALSE(f.audio->play_one_shot("no such clip", std::nullopt));
}

TEST_CASE("audio + lua: scripts start sounds, check them, fire one-shots and set bus volumes") {
    Fixture f(false);
    f.world.find(f.near_src)->components["script"] = {{"file", "Scripts/radio.luau"}};
    runtime::World world(f.world.scene());
    auto lua = std::make_unique<scripting::LuaScriptSystem>(scripting::LuaScriptSystem::Options{"."});
    auto* lua_ptr = lua.get();
    lua->add_source("Scripts/radio.luau", R"(
        local Radio = {}
        function Radio:on_start()
            self.entity:play_sound()
            noor.set_volume("music", 0.5)
            self.entity:set("radio", { playing = self.entity:is_playing(), shot = noor.play_sound("tone", vector.create(1, 0, 0), 0.5) })
        end
        return Radio
    )");
    world.add_system(std::move(lua));
    AudioSystem::Options o;
    o.use_device = false;
    o.advance_without_device = false;
    o.resolve_clip = [&](std::string_view clip) -> std::optional<stdfs::path> {
        return clip == "tone" ? std::optional(f.tone) : std::nullopt;
    };
    auto a = std::make_unique<AudioSystem>(std::move(o));
    auto* audio = a.get();
    world.add_system(std::move(a));
    world.frame(0.016, {});  // engine systems start the entity first, then its script
    CHECK(lua_ptr->errors().empty());
    CHECK(audio->bus_volume("music") == doctest::Approx(0.5f));
    const auto* radio = runtime::api::component(world, f.near_src, "radio");
    REQUIRE(radio);
    CHECK((*radio)["playing"] == true);  // on_start could play the source the audio system had just made
    CHECK((*radio)["shot"] == true);
}

TEST_CASE("audio: a lost sound device is rebuilt (needs a real device, skipped without one)") {
    AudioSystem::Options o;
    o.use_device = true;
    o.device_stall_seconds = 0.1;
    AudioSystem audio(o);
    if (!audio.device_open()) {
        MESSAGE("no sound device here: skipped");
        return;
    }
    audio.debug_stall_device();  // what Windows does when the earbuds go back in their case
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    audio.maintain_device();  // a frame: sees the last audio the device took
    CHECK(audio.device_reconnects() == 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    audio.maintain_device();  // a later frame: nothing for 0.3 s (> 0.1 s), rebuilds the device
    CHECK(audio.device_open());
    CHECK(audio.device_reconnects() == 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    audio.maintain_device();  // the new device is pulling audio: nothing to do
    CHECK(audio.device_open());
    CHECK(audio.device_reconnects() == 1);
}
