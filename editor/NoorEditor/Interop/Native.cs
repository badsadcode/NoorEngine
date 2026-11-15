using System.Runtime.InteropServices;

namespace NoorEditor.Interop;

/// <summary>P/Invoke surface for engine/editor_api/include/noor_editor_api.h. Keep in sync with the header.</summary>
public static class Native
{
    private const string Dll = "noor_editor_api";

    public const uint ExpectedApiVersion = 1;

    [DllImport(Dll)] public static extern uint noor_editor_api_version();
    [DllImport(Dll)] private static extern IntPtr noor_last_error();

    public static string LastError() => Marshal.PtrToStringUTF8(noor_last_error()) ?? "";

    // ---- Logging ----
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void LogCallback(int level, IntPtr category, IntPtr message, IntPtr user);

    [DllImport(Dll)] public static extern void noor_set_log_callback(LogCallback? callback, IntPtr user);

    // ---- Viewports ----
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
    [DllImport(Dll)] public static extern void noor_viewport_set_document(IntPtr viewport, IntPtr doc);
    [DllImport(Dll)] public static extern void noor_viewport_frame(IntPtr viewport, [MarshalAs(UnmanagedType.LPUTF8Str)] string? entityIdsJson);

    // ---- Documents (ADR-025). Strings in and out are UTF-8; returned strings are copied at once. ----
    [DllImport(Dll)] public static extern IntPtr noor_doc_open_project([MarshalAs(UnmanagedType.LPUTF8Str)] string projectPath);
    [DllImport(Dll)] public static extern IntPtr noor_doc_create_project([MarshalAs(UnmanagedType.LPUTF8Str)] string folder, [MarshalAs(UnmanagedType.LPUTF8Str)] string name);
    [DllImport(Dll)] public static extern void noor_doc_close(IntPtr doc);
    [DllImport(Dll)] private static extern IntPtr noor_doc_project_info(IntPtr doc);
    [DllImport(Dll)] private static extern IntPtr noor_doc_assets(IntPtr doc, int rescan);
    [DllImport(Dll)] private static extern IntPtr noor_doc_scenes(IntPtr doc);
    [DllImport(Dll)] public static extern int noor_doc_open_scene(IntPtr doc, [MarshalAs(UnmanagedType.LPUTF8Str)] string relativePath);
    [DllImport(Dll)] public static extern int noor_doc_new_scene(IntPtr doc, [MarshalAs(UnmanagedType.LPUTF8Str)] string relativePath, [MarshalAs(UnmanagedType.LPUTF8Str)] string name);
    [DllImport(Dll)] public static extern int noor_doc_save_scene(IntPtr doc);
    [DllImport(Dll)] private static extern IntPtr noor_doc_scene(IntPtr doc);
    [DllImport(Dll)] public static extern int noor_doc_execute(IntPtr doc, [MarshalAs(UnmanagedType.LPUTF8Str)] string commandJson);
    [DllImport(Dll)] public static extern int noor_doc_undo(IntPtr doc);
    [DllImport(Dll)] public static extern int noor_doc_redo(IntPtr doc);
    [DllImport(Dll)] public static extern ulong noor_doc_revision(IntPtr doc);

    private static string? Str(IntPtr p) => p == IntPtr.Zero ? null : Marshal.PtrToStringUTF8(p);
    public static string? ProjectInfo(IntPtr doc) => Str(noor_doc_project_info(doc));
    public static string? Assets(IntPtr doc, bool rescan) => Str(noor_doc_assets(doc, rescan ? 1 : 0));
    public static string? Scenes(IntPtr doc) => Str(noor_doc_scenes(doc));
    public static string? Scene(IntPtr doc) => Str(noor_doc_scene(doc));
}
