using System.Text.Json;
using Avalonia.Controls;
using Avalonia.Platform;
using NoorEditor.Interop;
using NoorEditor.Model;

namespace NoorEditor.Panels;

/// <summary>
/// Hosts an engine viewport (native child window + engine render thread) inside Avalonia and
/// points it at the open document. Avalonia calls Create/DestroyNativeControlCore whenever the
/// control moves between top-level windows (e.g. a docked panel is floated), so the engine
/// viewport is recreated and re-attached to the document then.
/// </summary>
public sealed class VulkanViewport : NativeControlHost
{
    private IntPtr _viewport;
    private readonly EditorDocument _doc;
    private readonly bool _preview;

    /// <summary>The most recently created live viewport (one per editor window for now).</summary>
    public static VulkanViewport? Active { get; private set; }

    /// <summary>How many engine viewports have been created this session (re-parenting increments it).</summary>
    public static int CreatedCount { get; private set; }

    /// <summary><paramref name="preview"/>: shows the document's material preview, not its scene.</summary>
    public VulkanViewport(EditorDocument doc, bool preview = false)
    {
        _doc = doc;
        _preview = preview;
    }

    public bool IsPreview => _preview;

    /// <summary>The engine viewport handle (noor_viewport*), e.g. as the game view for play mode.</summary>
    public IntPtr EngineHandle => _viewport;

    /// <summary>The engine's child window (HWND), used to tell whether keyboard focus is in the viewport.</summary>
    public IntPtr NativeWindow => _viewport == IntPtr.Zero ? IntPtr.Zero : Native.noor_viewport_native_handle(_viewport);

    public Native.ViewportStats? Stats
    {
        get
        {
            if (_viewport == IntPtr.Zero) return null;
            Native.noor_viewport_get_stats(_viewport, out var stats);
            return stats;
        }
    }

    /// <summary>Moves the camera to show the given entities, or everything when null/empty.</summary>
    public void Frame(IReadOnlyList<string>? ids)
    {
        if (_viewport == IntPtr.Zero) return;
        Native.noor_viewport_frame(_viewport, ids is { Count: > 0 } ? JsonSerializer.Serialize(ids) : null);
    }

    // Kept here so a recreated engine viewport (after floating the panel) gets them again.
    private string _selectionJson = "[]";
    private string? _gizmoEntity;
    private int _gizmoMode;
    private string? _optionsJson;

    /// <summary>Viewport preferences (noor_viewport_set_options JSON).</summary>
    public void SetOptions(string json)
    {
        _optionsJson = json;
        if (_viewport != IntPtr.Zero) Native.noor_viewport_set_options(_viewport, json);
    }

    /// <summary>Highlights these entities (pass a whole subtree to highlight an object).</summary>
    public void SetSelection(IReadOnlyList<string> ids)
    {
        _selectionJson = JsonSerializer.Serialize(ids);
        if (_viewport != IntPtr.Zero) Native.noor_viewport_set_selection(_viewport, _selectionJson);
    }

    /// <summary>Shows the gizmo (mode 1 move, 2 rotate) on one entity, or hides it (null / 0).</summary>
    public void SetGizmo(string? entityId, int mode)
    {
        _gizmoEntity = entityId;
        _gizmoMode = mode;
        if (_viewport != IntPtr.Zero) Native.noor_viewport_set_gizmo(_viewport, entityId, mode);
    }

    /// <summary>The renderer's numbers (noor_viewport_render_stats JSON), or null when not running.</summary>
    public string? RenderStats() => _viewport == IntPtr.Zero ? null : Native.RenderStats(_viewport);

    private string? _faceBrush;
    private int _face;

    /// <summary>Outlines one face of a CSG brush (null: none).</summary>
    public void SetFaceHighlight(string? brush, int face)
    {
        _faceBrush = brush;
        _face = face;
        if (_viewport != IntPtr.Zero) Native.noor_viewport_set_face_highlight(_viewport, brush, face);
    }

    /// <summary>Clicks and finished gizmo drags since the last call (JSON array).</summary>
    public string PollEvents() => _viewport == IntPtr.Zero ? "[]" : Native.PollEvents(_viewport);

    /// <summary>Where a world point is in the viewport (physical pixels) and its distance from the camera.</summary>
    public (float X, float Y, float Distance)? WorldToScreen(Vec3 p)
    {
        if (_viewport == IntPtr.Zero) return null;
        var output = new float[3];
        return Native.noor_viewport_world_to_screen(_viewport, [(float)p.X, (float)p.Y, (float)p.Z], output) == 1
            ? (output[0], output[1], output[2]) : null;
    }

    /// <summary>Detaches from the document; call before the document is disposed.</summary>
    public void Detach()
    {
        if (_viewport != IntPtr.Zero) Native.noor_viewport_set_document(_viewport, IntPtr.Zero);
    }

    protected override IPlatformHandle CreateNativeControlCore(IPlatformHandle parent)
    {
        var desc = new Native.ViewportDesc { EnableValidation = 1 };
        _viewport = Native.noor_viewport_create(parent.Handle, ref desc);
        if (_viewport == IntPtr.Zero)
        {
            EngineLog.Error("Editor", $"Viewport creation failed: {Native.LastError()}");
            return base.CreateNativeControlCore(parent);
        }
        if (_doc.Handle != IntPtr.Zero)
        {
            if (_preview) Native.noor_viewport_set_preview(_viewport, _doc.Handle);
            else Native.noor_viewport_set_document(_viewport, _doc.Handle);
        }
        Native.noor_viewport_set_selection(_viewport, _selectionJson);
        Native.noor_viewport_set_gizmo(_viewport, _gizmoEntity, _gizmoMode);
        if (_optionsJson != null) Native.noor_viewport_set_options(_viewport, _optionsJson);
        if (_faceBrush != null) Native.noor_viewport_set_face_highlight(_viewport, _faceBrush, _face);
        if (!_preview)
        {
            CreatedCount++;
            Active = this;  // the scene viewport (previews don't count)
        }
        return new PlatformHandle(Native.noor_viewport_native_handle(_viewport), "HWND");
    }

    protected override void DestroyNativeControlCore(IPlatformHandle control)
    {
        if (_viewport == IntPtr.Zero)
        {
            base.DestroyNativeControlCore(control);
            return;
        }
        // The engine destroys its own child window after stopping the render thread.
        Native.noor_viewport_destroy(_viewport);
        _viewport = IntPtr.Zero;
        if (Active == this) Active = null;
    }
}
