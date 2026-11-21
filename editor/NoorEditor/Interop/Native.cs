using System.Runtime.InteropServices;

namespace NoorEditor.Interop;

/// <summary>P/Invoke surface for engine/editor_api/include/noor_editor_api.h. Keep in sync with the header.</summary>
public static class Native
{
    private const string Dll = "noor_editor_api";

    public const uint ExpectedApiVersion = 31;

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
    [DllImport(Dll)] public static extern void noor_viewport_set_preview(IntPtr viewport, IntPtr doc);
    [DllImport(Dll)] public static extern void noor_viewport_set_face_highlight(IntPtr viewport, [MarshalAs(UnmanagedType.LPUTF8Str)] string? brushId, int face);
    [DllImport(Dll)] public static extern void noor_viewport_frame(IntPtr viewport, [MarshalAs(UnmanagedType.LPUTF8Str)] string? entityIdsJson);
    [DllImport(Dll)] public static extern void noor_viewport_set_selection(IntPtr viewport, [MarshalAs(UnmanagedType.LPUTF8Str)] string? entityIdsJson);
    [DllImport(Dll)] public static extern void noor_viewport_set_gizmo(IntPtr viewport, [MarshalAs(UnmanagedType.LPUTF8Str)] string? entityId, int mode);
    [DllImport(Dll)] private static extern IntPtr noor_viewport_poll_events(IntPtr viewport);
    [DllImport(Dll)] private static extern IntPtr noor_viewport_render_stats(IntPtr viewport);
    [DllImport(Dll)] public static extern void noor_viewport_set_options(IntPtr viewport, [MarshalAs(UnmanagedType.LPUTF8Str)] string optionsJson);
    [DllImport(Dll)] public static extern int noor_viewport_world_to_screen(IntPtr viewport, float[] world, float[] output);
    [DllImport(Dll)] public static extern void noor_viewport_test_pointer(IntPtr viewport, int enabled, int x, int y, int buttons);
    [DllImport(Dll)] public static extern void noor_viewport_set_mouse_capture(IntPtr viewport, int capture);
    [DllImport(Dll)] public static extern void noor_viewport_set_play(IntPtr viewport, IntPtr doc);
    [DllImport(Dll)] public static extern int noor_viewport_mouse_captured(IntPtr viewport);
    [DllImport(Dll)] public static extern int noor_viewport_has_focus(IntPtr viewport);

    /// <summary>Gizmo arrow length relative to its distance from the camera (NOOR_GIZMO_SCREEN_SCALE).</summary>
    public const float GizmoScreenScale = 0.16f;

    public static string PollEvents(IntPtr viewport) => Str(noor_viewport_poll_events(viewport)) ?? "[]";
    public static string RenderStats(IntPtr viewport) => Str(noor_viewport_render_stats(viewport)) ?? "{}";

    // ---- Documents (ADR-025). Strings in and out are UTF-8; returned strings are copied at once. ----
    [DllImport(Dll)] public static extern IntPtr noor_doc_open_project([MarshalAs(UnmanagedType.LPUTF8Str)] string projectPath);
    [DllImport(Dll)] public static extern IntPtr noor_doc_create_project([MarshalAs(UnmanagedType.LPUTF8Str)] string folder, [MarshalAs(UnmanagedType.LPUTF8Str)] string name);
    [DllImport(Dll)] public static extern void noor_doc_close(IntPtr doc);
    // v31 (2D part 7): project templates.
    [DllImport(Dll)] private static extern IntPtr noor_templates([MarshalAs(UnmanagedType.LPUTF8Str)] string dir);
    [DllImport(Dll)] public static extern IntPtr noor_doc_create_project_from_template([MarshalAs(UnmanagedType.LPUTF8Str)] string folder, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, [MarshalAs(UnmanagedType.LPUTF8Str)] string templateFolder);
    public static string Templates(string dir) => Str(noor_templates(dir)) ?? "[]";
    [DllImport(Dll)] private static extern IntPtr noor_doc_project_info(IntPtr doc);
    [DllImport(Dll)] private static extern IntPtr noor_doc_assets(IntPtr doc, int rescan);
    [DllImport(Dll)] private static extern IntPtr noor_doc_asset_details(IntPtr doc, [MarshalAs(UnmanagedType.LPUTF8Str)] string assetId);
    [DllImport(Dll)] private static extern IntPtr noor_doc_scatter_edit(IntPtr doc, [MarshalAs(UnmanagedType.LPUTF8Str)] string command);
    [DllImport(Dll)] private static extern IntPtr noor_doc_make_material(IntPtr doc, [MarshalAs(UnmanagedType.LPUTF8Str)] string texturePath);
    [DllImport(Dll)] private static extern IntPtr noor_environment_presets();
    [DllImport(Dll)] private static extern IntPtr noor_doc_environment(IntPtr doc, [MarshalAs(UnmanagedType.LPUTF8Str)] string command);
    [DllImport(Dll)] private static extern IntPtr noor_effect_presets();
    [DllImport(Dll)] private static extern IntPtr noor_water_presets();
    [DllImport(Dll)] private static extern IntPtr noor_doc_effect(IntPtr doc, [MarshalAs(UnmanagedType.LPUTF8Str)] string command);
    [DllImport(Dll)] private static extern IntPtr noor_doc_water(IntPtr doc, [MarshalAs(UnmanagedType.LPUTF8Str)] string command);
    [DllImport(Dll)] private static extern IntPtr noor_doc_tilemap(IntPtr doc, [MarshalAs(UnmanagedType.LPUTF8Str)] string command);
    [DllImport(Dll)] private static extern IntPtr noor_doc_sprite(IntPtr doc, [MarshalAs(UnmanagedType.LPUTF8Str)] string command);
    [DllImport(Dll)] private static extern IntPtr noor_doc_animation(IntPtr doc, [MarshalAs(UnmanagedType.LPUTF8Str)] string command);
    [DllImport(Dll)] private static extern IntPtr noor_doc_csg(IntPtr doc);
    [DllImport(Dll)] private static extern IntPtr noor_doc_sculpt(IntPtr doc, [MarshalAs(UnmanagedType.LPUTF8Str)] string command);
    // Procedural graphs (Milestone 7): background runs, previews, node types, checks.
    [DllImport(Dll)] public static extern int noor_doc_generate_start(IntPtr doc, [MarshalAs(UnmanagedType.LPUTF8Str)] string graphPath, [MarshalAs(UnmanagedType.LPUTF8Str)] string? outDir);
    [DllImport(Dll)] private static extern IntPtr noor_doc_generate_status(IntPtr doc);
    [DllImport(Dll)] public static extern void noor_doc_generate_cancel(IntPtr doc);
    [DllImport(Dll)] private static extern IntPtr noor_doc_generate_preview(IntPtr doc, [MarshalAs(UnmanagedType.LPUTF8Str)] string node, [MarshalAs(UnmanagedType.LPUTF8Str)] string pin, int size);
    [DllImport(Dll)] private static extern IntPtr noor_graph_node_types();
    [DllImport(Dll)] private static extern IntPtr noor_graph_check([MarshalAs(UnmanagedType.LPUTF8Str)] string graph);
    [DllImport(Dll)] public static extern void noor_viewport_set_brush(IntPtr viewport, [MarshalAs(UnmanagedType.LPUTF8Str)] string? brush);
    [DllImport(Dll)] private static extern IntPtr noor_doc_export_csg(IntPtr doc, [MarshalAs(UnmanagedType.LPUTF8Str)] string modelId,
                                                                    [MarshalAs(UnmanagedType.LPUTF8Str)] string glbPath);
    [DllImport(Dll)] private static extern IntPtr noor_doc_texture(IntPtr doc, [MarshalAs(UnmanagedType.LPUTF8Str)] string texturePath);
    [DllImport(Dll)] public static extern int noor_doc_set_texture_settings(IntPtr doc, [MarshalAs(UnmanagedType.LPUTF8Str)] string texturePath,
                                                                           [MarshalAs(UnmanagedType.LPUTF8Str)] string settingsJson);
    [DllImport(Dll)] public static extern int noor_doc_set_preview(IntPtr doc, [MarshalAs(UnmanagedType.LPUTF8Str)] string json);
    [DllImport(Dll)] private static extern IntPtr noor_doc_scenes(IntPtr doc);
    [DllImport(Dll)] public static extern int noor_doc_open_scene(IntPtr doc, [MarshalAs(UnmanagedType.LPUTF8Str)] string relativePath);
    [DllImport(Dll)] public static extern int noor_doc_new_scene(IntPtr doc, [MarshalAs(UnmanagedType.LPUTF8Str)] string relativePath, [MarshalAs(UnmanagedType.LPUTF8Str)] string name);
    [DllImport(Dll)] public static extern int noor_doc_save_scene(IntPtr doc);
    [DllImport(Dll)] private static extern IntPtr noor_doc_scene(IntPtr doc);
    [DllImport(Dll)] public static extern int noor_doc_execute(IntPtr doc, [MarshalAs(UnmanagedType.LPUTF8Str)] string commandJson);
    [DllImport(Dll)] public static extern int noor_doc_undo(IntPtr doc);
    [DllImport(Dll)] public static extern int noor_doc_redo(IntPtr doc);
    [DllImport(Dll)] public static extern ulong noor_doc_revision(IntPtr doc);
    [DllImport(Dll)] public static extern int noor_doc_play(IntPtr doc);
    [DllImport(Dll)] public static extern void noor_doc_stop(IntPtr doc);
    [DllImport(Dll)] public static extern void noor_doc_tick(IntPtr doc, double seconds, IntPtr inputViewport);
    [DllImport(Dll)] private static extern IntPtr noor_doc_poll_files(IntPtr doc);
    [DllImport(Dll)] public static extern IntPtr noor_rt_get_api();
    [DllImport(Dll)] public static extern void noor_doc_set_script_host(IntPtr doc, IntPtr host);
    [DllImport(Dll)] public static extern void noor_doc_set_audio_device(IntPtr doc, int enabled);

    private static string? Str(IntPtr p) => p == IntPtr.Zero ? null : Marshal.PtrToStringUTF8(p);
    public static string? ProjectInfo(IntPtr doc) => Str(noor_doc_project_info(doc));
    public static string? Assets(IntPtr doc, bool rescan) => Str(noor_doc_assets(doc, rescan ? 1 : 0));
    public static string? AssetDetails(IntPtr doc, string assetId) => Str(noor_doc_asset_details(doc, assetId));
    public static string? ScatterEdit(IntPtr doc, string command) => Str(noor_doc_scatter_edit(doc, command));
    public static string? MakeMaterial(IntPtr doc, string texturePath) => Str(noor_doc_make_material(doc, texturePath));
    // v24: environment assets and the built-in presets (Milestone 8A, part 6).
    public static string? EnvironmentPresets() => Str(noor_environment_presets());
    public static string? Environment(IntPtr doc, string command) => Str(noor_doc_environment(doc, command));
    // v25: effect assets, the built-in effect presets and the viewport's effect preview (Milestone 8B, part 4).
    public static string? EffectPresets() => Str(noor_effect_presets());
    public static string? WaterPresets() => Str(noor_water_presets());
    public static string? Effect(IntPtr doc, string command) => Str(noor_doc_effect(doc, command));
    public static string? Water(IntPtr doc, string command) => Str(noor_doc_water(doc, command));
    /// <summary>The Tilemap panel (v30, 2D part 5): list, info, paint (noor_doc_tilemap).</summary>
    public static string? Tilemap(IntPtr doc, string command) => Str(noor_doc_tilemap(doc, command));
    // v26: sprite sheets (Milestone 8B, part 5).
    public static string? Sprite(IntPtr doc, string command) => Str(noor_doc_sprite(doc, command));
    // v27: the Animation panel (Milestone 8C, part 6): models, bone maps, controllers, characters, the preview.
    public static string? Animation(IntPtr doc, string command) => Str(noor_doc_animation(doc, command));
    public static string? Texture(IntPtr doc, string texturePath) => Str(noor_doc_texture(doc, texturePath));
    public static string Csg(IntPtr doc) => Str(noor_doc_csg(doc)) ?? "[]";
    public static string? ExportCsg(IntPtr doc, string modelId, string glbPath) => Str(noor_doc_export_csg(doc, modelId, glbPath));
    public static string? Sculpt(IntPtr doc, string command) => Str(noor_doc_sculpt(doc, command));
    public static string? GenerateStatus(IntPtr doc) => Str(noor_doc_generate_status(doc));
    public static string? GeneratePreview(IntPtr doc, string node, string pin, int size) => Str(noor_doc_generate_preview(doc, node, pin, size));
    public static string? GraphNodeTypes() => Str(noor_graph_node_types());
    public static string? GraphCheck(string graph) => Str(noor_graph_check(graph));
    public static string? Scenes(IntPtr doc) => Str(noor_doc_scenes(doc));
    public static string? Scene(IntPtr doc) => Str(noor_doc_scene(doc));
    public static string PollFiles(IntPtr doc) => Str(noor_doc_poll_files(doc)) ?? "[]";
}
