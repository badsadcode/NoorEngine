#include "noor/scripting/dotnet_host.h"

#include "noor/foundation/log.h"
#include "noor/scripting/csharp_scripts.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace noor::scripting {
namespace stdfs = std::filesystem;

#if defined(_WIN32)
namespace {

// The few declarations we need from hostfxr.h and coreclr_delegates.h (MIT, dotnet/runtime). On
// Windows char_t is wchar_t.
using hostfxr_handle = void*;
struct hostfxr_initialize_parameters {
    std::size_t size;
    const wchar_t* host_path;
    const wchar_t* dotnet_root;
};
constexpr int hdt_load_assembly_and_get_function_pointer = 5;
using hostfxr_initialize_for_runtime_config_fn = std::int32_t(__cdecl*)(const wchar_t*, const hostfxr_initialize_parameters*, hostfxr_handle*);
using hostfxr_get_runtime_delegate_fn = std::int32_t(__cdecl*)(const hostfxr_handle, int, void**);
using hostfxr_close_fn = std::int32_t(__cdecl*)(const hostfxr_handle);
using hostfxr_error_writer_fn = void(__cdecl*)(const wchar_t*);
using hostfxr_set_error_writer_fn = hostfxr_error_writer_fn(__cdecl*)(hostfxr_error_writer_fn);
using load_assembly_and_get_function_pointer_fn = int(__stdcall*)(const wchar_t* assembly_path, const wchar_t* type_name,
                                                                  const wchar_t* method_name, const wchar_t* delegate_type_name,
                                                                  void* reserved, void** delegate);
const wchar_t* const kUnmanagedCallersOnly = reinterpret_cast<const wchar_t*>(-1);

// ScriptHost.Boot(nint api, byte* scriptsPath, byte* error, int errorCapacity) -> nint host
using boot_fn = std::intptr_t(__stdcall*)(const void*, const char*, char*, int);

std::wstring env(const wchar_t* name) {
    wchar_t* value = nullptr;
    std::size_t length = 0;
    std::wstring out;
    if (_wdupenv_s(&value, &length, name) == 0 && value) out = value;
    std::free(value);
    return out;
}

// "10.0.2" > "9.0.11": compares dot-separated numbers; non-numeric parts (previews) sort first.
bool newer(const std::string& a, const std::string& b) {
    auto parts = [](const std::string& v) {
        std::vector<int> out;
        std::size_t i = 0;
        while (i < v.size()) {
            int n = 0;
            bool digits = false;
            while (i < v.size() && v[i] >= '0' && v[i] <= '9') {
                n = n * 10 + (v[i++] - '0');
                digits = true;
            }
            out.push_back(digits ? n : -1);
            while (i < v.size() && v[i] != '.') ++i;
            if (i < v.size()) ++i;
        }
        return out;
    };
    return parts(a) > parts(b);
}

stdfs::path dotnet_root() {
    if (auto root = env(L"DOTNET_ROOT"); !root.empty()) return root;
    auto program_files = env(L"ProgramFiles");
    return stdfs::path(program_files.empty() ? L"C:\\Program Files" : program_files) / "dotnet";
}

std::mutex g_mutex;
std::wstring g_errors;  // hostfxr's own messages for the current call
void __cdecl collect_error(const wchar_t* message) {
    if (!g_errors.empty()) g_errors += L" ";
    g_errors += message;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

Error fail(std::string message) { return Error{ErrorCode::PlatformError, std::move(message)}; }

struct Started {
    const noor_rt_host* host = nullptr;
    stdfs::path scripts;
};
std::optional<Started> g_started;

} // namespace

Result<stdfs::path> find_hostfxr() {
    const stdfs::path fxr = dotnet_root() / "host" / "fxr";
    std::error_code ec;
    std::string best;
    for (stdfs::directory_iterator it(fxr, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string version = it->path().filename().string();
        if (stdfs::exists(it->path() / "hostfxr.dll", ec) && (best.empty() || newer(version, best))) best = version;
    }
    if (best.empty())
        return std::unexpected(fail(std::format("No .NET runtime found (looked in {}). C# scripts need the .NET 10 runtime: "
                                                "https://dotnet.microsoft.com/download/dotnet/10.0",
                                                fxr.generic_string())));
    return fxr / best / "hostfxr.dll";
}

Result<const noor_rt_host*> start_dotnet(const stdfs::path& managed_dir, const stdfs::path& scripts_dll) {
    std::scoped_lock lock(g_mutex);
    if (g_started) {
        if (stdfs::equivalent(g_started->scripts, scripts_dll)) return g_started->host;
        return std::unexpected(fail(".NET is already running other scripts; it can only start once per process"));
    }
    const stdfs::path config = managed_dir / "NoorEngine.Scripting.runtimeconfig.json";
    const stdfs::path library = managed_dir / "NoorEngine.Scripting.dll";
    std::error_code ec;
    for (const auto& f : {config, library, scripts_dll}) {
        if (!stdfs::exists(f, ec)) return std::unexpected(fail(std::format("Missing {}", f.generic_string())));
    }
    auto fxr_path = find_hostfxr();
    if (!fxr_path) return std::unexpected(fxr_path.error());
    HMODULE fxr = LoadLibraryW(fxr_path->c_str());
    if (!fxr) return std::unexpected(fail(std::format("Can't load {} (error {})", fxr_path->generic_string(), GetLastError())));
    auto init = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(GetProcAddress(fxr, "hostfxr_initialize_for_runtime_config"));
    auto get_delegate = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(GetProcAddress(fxr, "hostfxr_get_runtime_delegate"));
    auto close = reinterpret_cast<hostfxr_close_fn>(GetProcAddress(fxr, "hostfxr_close"));
    auto set_error_writer = reinterpret_cast<hostfxr_set_error_writer_fn>(GetProcAddress(fxr, "hostfxr_set_error_writer"));
    if (!init || !get_delegate || !close) return std::unexpected(fail(std::format("{} is too old for hosting", fxr_path->generic_string())));

    g_errors.clear();
    if (set_error_writer) set_error_writer(&collect_error);
    const std::wstring root = dotnet_root().wstring();
    const hostfxr_initialize_parameters params{sizeof(hostfxr_initialize_parameters), nullptr, root.c_str()};
    hostfxr_handle context = nullptr;
    const std::int32_t rc = init(config.c_str(), &params, &context);
    if (rc < 0 || !context) {
        if (set_error_writer) set_error_writer(nullptr);
        if (context) close(context);
        return std::unexpected(fail(std::format("Starting .NET failed (0x{:08X}){}{}", static_cast<std::uint32_t>(rc),
                                                g_errors.empty() ? "" : ": ", narrow(g_errors))));
    }
    void* raw = nullptr;
    const std::int32_t rd = get_delegate(context, hdt_load_assembly_and_get_function_pointer, &raw);
    close(context);  // the runtime stays loaded; the context handle isn't needed any more
    if (rd < 0 || !raw) {
        if (set_error_writer) set_error_writer(nullptr);
        return std::unexpected(fail(std::format("Getting the .NET loader failed (0x{:08X})", static_cast<std::uint32_t>(rd))));
    }
    auto load = reinterpret_cast<load_assembly_and_get_function_pointer_fn>(raw);
    void* boot_raw = nullptr;
    const int rl = load(library.c_str(), L"NoorEngine.ScriptHost, NoorEngine.Scripting", L"Boot", kUnmanagedCallersOnly, nullptr, &boot_raw);
    if (set_error_writer) set_error_writer(nullptr);
    if (rl < 0 || !boot_raw)
        return std::unexpected(fail(std::format("Loading NoorEngine.Scripting failed (0x{:08X}){}{}", static_cast<std::uint32_t>(rl),
                                                g_errors.empty() ? "" : ": ", narrow(g_errors))));

    std::array<char, 1024> error{};
    const std::string scripts = narrow(scripts_dll.wstring());
    const auto host = reinterpret_cast<boot_fn>(boot_raw)(rt_api(), scripts.c_str(), error.data(), static_cast<int>(error.size()));
    if (!host) return std::unexpected(fail(std::format("Loading the C# scripts failed: {}", error.data())));
    g_started = Started{reinterpret_cast<const noor_rt_host*>(host), scripts_dll};
    NOOR_LOG_INFO("CSharp", ".NET started ({}), scripts loaded from {}", fxr_path->parent_path().filename().string(),
                  scripts_dll.filename().string());
    return g_started->host;
}

#else

Result<stdfs::path> find_hostfxr() {
    return std::unexpected(Error{ErrorCode::Unsupported, ".NET hosting is Windows-only for now"});
}
Result<const noor_rt_host*> start_dotnet(const stdfs::path&, const stdfs::path&) {
    return std::unexpected(Error{ErrorCode::Unsupported, ".NET hosting is Windows-only for now"});
}

#endif

} // namespace noor::scripting
