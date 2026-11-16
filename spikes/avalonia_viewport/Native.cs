using System.Runtime.InteropServices;

namespace NoorEditorSpike;

/// <summary>P/Invoke surface for engine/editor_api/include/noor_editor_api.h (API v1, viewport subset).</summary>
public static class Native
{
    private const string Dll = "noor_editor_api";

    public const uint ExpectedApiVersion = 9;  // v1-v9 kept the v0 viewport functions unchanged

    [DllImport(Dll)] public static extern uint noor_editor_api_version();
    [DllImport(Dll)] private static extern IntPtr noor_last_error();

    public static string LastError() => Marshal.PtrToStringUTF8(noor_last_error()) ?? "";

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void LogCallback(int level, IntPtr category, IntPtr message, IntPtr user);

    [DllImport(Dll)] public static extern void noor_set_log_callback(LogCallback? callback, IntPtr user);

    [StructLayout(LayoutKind.Sequential)]
    public struct ViewportDesc
    {
        public int EnableValidation;
    }

    public enum ViewportState { Starting = 0, Running = 1, Failed = 2, Stopped = 3 }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct ViewportStats
    {
        public ViewportState State;
        public ulong FramesPresented;
        public double FramesPerSecond;
        public double SecondsToFirstFrame;
        public uint Width;
        public uint Height;
        public ulong SwapchainRebuilds;
        public ulong ValidationErrors;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
        public string GpuName;
    }

    [DllImport(Dll)] public static extern IntPtr noor_viewport_create(IntPtr parentWindow, ref ViewportDesc desc);
    [DllImport(Dll)] public static extern IntPtr noor_viewport_native_handle(IntPtr viewport);
    [DllImport(Dll)] public static extern void noor_viewport_get_stats(IntPtr viewport, out ViewportStats stats);
    [DllImport(Dll)] public static extern void noor_viewport_destroy(IntPtr viewport);
}
