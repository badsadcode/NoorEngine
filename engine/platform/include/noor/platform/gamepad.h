#pragma once
// Gamepads through SDL (gameplay foundations part 6e): up to four, each in a player's slot. A pad takes the first free
// slot; one unplugged and plugged back in (the same model, by its GUID) gets its old slot back while that slot is
// free, so player 2 stays player 2. Xbox, PlayStation, Switch and generic pads all read the same way (SDL's gamepad
// mapping): buttons by where they are, sticks from -1 to 1, triggers from 0 to 1.

#include "noor/foundation/error.h"

#include <array>
#include <bitset>
#include <cstdint>
#include <memory>
#include <string>

namespace noor::platform {

// The same order as runtime::PadButton and runtime::PadAxis (the host copies one into the other).
inline constexpr std::size_t kPadButtons = 15;  // South, East, West, North, Back, Guide, Start, LS, RS, LB, RB, Up, Down, Left, Right
inline constexpr std::size_t kPadAxes = 6;      // LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger
inline constexpr std::size_t kPadSlots = 4;

struct PadReading {
    bool connected = false;
    std::string name;
    std::bitset<kPadButtons> buttons;
    std::array<float, kPadAxes> axes{};
};

class Gamepads {
public:
    // Starts SDL's gamepad support (after platform::initialize).
    [[nodiscard]] static Result<std::unique_ptr<Gamepads>> open();
    ~Gamepads();
    Gamepads(const Gamepads&) = delete;
    Gamepads& operator=(const Gamepads&) = delete;

    // Once a frame, after pump_events: pads plugged in or out, and every pad's buttons and sticks.
    void poll();
    [[nodiscard]] const std::array<PadReading, kPadSlots>& pads() const noexcept;

private:
    struct Impl;
    explicit Gamepads(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace noor::platform
