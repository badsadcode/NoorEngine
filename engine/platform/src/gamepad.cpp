// Gamepads through SDL3 (gamepad.h).
#include "noor/platform/gamepad.h"

#include "noor/foundation/log.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <format>
#include <vector>

namespace noor::platform {

namespace {
constexpr SDL_GamepadButton kButtons[kPadButtons] = {
    SDL_GAMEPAD_BUTTON_SOUTH,       SDL_GAMEPAD_BUTTON_EAST,        SDL_GAMEPAD_BUTTON_WEST,          SDL_GAMEPAD_BUTTON_NORTH,
    SDL_GAMEPAD_BUTTON_BACK,        SDL_GAMEPAD_BUTTON_GUIDE,       SDL_GAMEPAD_BUTTON_START,         SDL_GAMEPAD_BUTTON_LEFT_STICK,
    SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_DPAD_UP,
    SDL_GAMEPAD_BUTTON_DPAD_DOWN,   SDL_GAMEPAD_BUTTON_DPAD_LEFT,   SDL_GAMEPAD_BUTTON_DPAD_RIGHT};
constexpr SDL_GamepadAxis kAxes[kPadAxes] = {SDL_GAMEPAD_AXIS_LEFTX,  SDL_GAMEPAD_AXIS_LEFTY,        SDL_GAMEPAD_AXIS_RIGHTX,
                                             SDL_GAMEPAD_AXIS_RIGHTY, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER};

std::string guid_of(SDL_JoystickID id) {
    char text[64] = {};
    SDL_GUIDToString(SDL_GetGamepadGUIDForID(id), text, sizeof(text));
    return text;
}
} // namespace

struct Gamepads::Impl {
    struct Slot {
        SDL_Gamepad* pad = nullptr;
        SDL_JoystickID id = 0;
        std::string guid;  // kept after it's unplugged: the same pad comes back here
    };
    std::array<Slot, kPadSlots> slots;
    std::array<PadReading, kPadSlots> readings;
};

Gamepads::Gamepads(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Gamepads::~Gamepads() {
    for (auto& s : impl_->slots)
        if (s.pad) SDL_CloseGamepad(s.pad);
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

Result<std::unique_ptr<Gamepads>> Gamepads::open() {
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
        return fail(ErrorCode::PlatformError, std::format("gamepads: SDL_InitSubSystem failed: {}", SDL_GetError()));
    auto pads = std::unique_ptr<Gamepads>(new Gamepads(std::make_unique<Impl>()));
    pads->poll();
    return pads;
}

void Gamepads::poll() {
    auto& slots = impl_->slots;
    SDL_UpdateGamepads();
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    std::vector<SDL_JoystickID> present(ids, ids + (ids ? count : 0));
    SDL_free(ids);
    // Gone: the slot is free (its GUID stays, for when it comes back).
    for (std::size_t i = 0; i < kPadSlots; ++i) {
        auto& s = slots[i];
        if (s.pad && std::find(present.begin(), present.end(), s.id) == present.end()) {
            NOOR_LOG_INFO("Platform", "Gamepad {} unplugged: {}", i + 1, impl_->readings[i].name);
            SDL_CloseGamepad(s.pad);
            s.pad = nullptr;
            s.id = 0;
        }
    }
    // New: its old slot if free, else the first free one.
    for (const SDL_JoystickID id : present) {
        if (std::any_of(slots.begin(), slots.end(), [&](const Impl::Slot& s) { return s.pad && s.id == id; })) continue;
        const std::string guid = guid_of(id);
        std::size_t at = kPadSlots;
        for (std::size_t i = 0; i < kPadSlots; ++i)
            if (!slots[i].pad && slots[i].guid == guid) at = i;
        for (std::size_t i = 0; i < kPadSlots && at == kPadSlots; ++i)
            if (!slots[i].pad) at = i;
        if (at == kPadSlots) continue;  // four pads already
        SDL_Gamepad* pad = SDL_OpenGamepad(id);
        if (!pad) {
            NOOR_LOG_WARN("Platform", "A gamepad couldn't be opened: {}", SDL_GetError());
            continue;
        }
        slots[at] = {pad, id, guid};
        const char* name = SDL_GetGamepadName(pad);
        NOOR_LOG_INFO("Platform", "Gamepad {} plugged in: {}", at + 1, name ? name : "?");
        impl_->readings[at].name = name ? name : "Gamepad";
    }
    for (std::size_t i = 0; i < kPadSlots; ++i) {
        auto& r = impl_->readings[i];
        SDL_Gamepad* pad = slots[i].pad;
        r.connected = pad != nullptr;
        r.buttons.reset();
        r.axes.fill(0.0f);
        if (!pad) continue;
        for (std::size_t b = 0; b < kPadButtons; ++b) r.buttons.set(b, SDL_GetGamepadButton(pad, kButtons[b]));
        for (std::size_t a = 0; a < kPadAxes; ++a) {
            const float v = static_cast<float>(SDL_GetGamepadAxis(pad, kAxes[a])) / 32767.0f;
            r.axes[a] = std::clamp(v, -1.0f, 1.0f);
        }
    }
}

const std::array<PadReading, kPadSlots>& Gamepads::pads() const noexcept { return impl_->readings; }

} // namespace noor::platform
