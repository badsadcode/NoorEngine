#pragma once
// Built-in behaviours (ADR-026/027): small components that work without scripts, for quick
// prototypes and tests. Scripts (Milestone 4) are the real way to write behaviour.
//
//   "rotator":     {"axis": [0, 1, 0], "degrees_per_second": 45}
//       Turns the entity about `axis` (in its parent's space). Defaults: +Y, 45.
//   "input_mover": {"speed": 5}
//       Moves the entity on the ground plane with the move_x / move_y axes (A/D, W/S, arrows),
//       in metres per second. Default speed 5.

#include "noor/runtime/world.h"

namespace noor::runtime {

class BehavioursSystem final : public System {
public:
    [[nodiscard]] std::string_view name() const override { return "behaviours"; }
    void update(World& world, double dt) override;
};

} // namespace noor::runtime
