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

    /// <summary>The most recently created live viewport (one per editor window for now).</summary>
    public static VulkanViewport? Active { get; private set; }

    /// <summary>How many engine viewports have been created this session (re-parenting increments it).</summary>
    public static int CreatedCount { get; private set; }

    public VulkanViewport(EditorDocument doc) => _doc = doc;

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
        if (_doc.Handle != IntPtr.Zero) Native.noor_viewport_set_document(_viewport, _doc.Handle);
        CreatedCount++;
        Active = this;
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
