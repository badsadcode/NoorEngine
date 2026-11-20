#include "noor/platform/process.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

namespace noor::platform {

namespace {
std::uint64_t ticks(const FILETIME& t) { return (static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime; }
} // namespace

ProcessStats process_stats() {
    ProcessStats s;
    HANDLE self = GetCurrentProcess();
    PROCESS_MEMORY_COUNTERS_EX mem{};
    mem.cb = sizeof(mem);
    if (GetProcessMemoryInfo(self, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&mem), sizeof(mem))) {
        s.working_set = mem.WorkingSetSize;
        s.peak_working_set = mem.PeakWorkingSetSize;
        s.private_bytes = mem.PrivateUsage;
    }
    const DWORD pid = GetCurrentProcessId();
    if (HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0); snap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te{};
        te.dwSize = sizeof(te);
        for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
            if (te.th32OwnerProcessID == pid) ++s.threads;
        CloseHandle(snap);
    }
    FILETIME created{}, exited{}, kernel{}, user{}, now{};
    if (GetProcessTimes(self, &created, &exited, &kernel, &user)) {
        GetSystemTimePreciseAsFileTime(&now);
        s.seconds_alive = static_cast<double>(ticks(now) - ticks(created)) * 1e-7;  // 100 ns units
    }
    return s;
}

} // namespace noor::platform
