#pragma once
// What this process costs the machine right now (Milestone 9A, part 1: the yardsticks): memory, threads, and how long
// it has been running. For reports and budgets; read it when you want it (a few system calls, not per frame).

#include <cstdint>

namespace noor::platform {

struct ProcessStats {
    std::uint64_t working_set = 0;       // bytes of RAM in use now
    std::uint64_t peak_working_set = 0;  // the most it has used
    std::uint64_t private_bytes = 0;     // memory committed to this process alone
    std::uint32_t threads = 0;           // threads alive now
    double seconds_alive = 0;            // since the process started (the OS's clock, not time_seconds())
};

// Zeros where the platform can't say.
[[nodiscard]] ProcessStats process_stats();

} // namespace noor::platform
