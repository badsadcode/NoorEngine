using Avalonia.Controls;
using Avalonia.Platform;

namespace NoorEditorSpike;

/// <summary>
/// Hosts a NoorEngine viewport (native child window + engine render thread) inside Avalonia.
/// Avalonia calls Create/DestroyNativeControlCore whenever the control moves between
/// top-level windows (e.g. a docked panel is floated), so the engine viewport is recreated.
/// </summary>
public sealed class VulkanViewport : NativeControlHost
{
    private IntPtr _viewport;

    /// <summary>The most recently created live viewport (the spike has one at a time).</summary>
    public static VulkanViewport? Active { get; private set; }

    /// <summary>How many engine viewports have been created this session (re-parenting increments it).</summary>
    public static int CreatedCount { get; private set; }

    public Native.ViewportStats? Stats
    {
        get
        {
            if (_viewport == IntPtr.Zero) return null;
            Native.noor_viewport_get_stats(_viewport, out var stats);
            return stats;
        }
    }

    protected override IPlatformHandle CreateNativeControlCore(IPlatformHandle parent)
    {
        var desc = new Native.ViewportDesc { EnableValidation = 1 };
        _viewport = Native.noor_viewport_create(parent.Handle, ref desc);
        if (_viewport == IntPtr.Zero)
        {
            EngineLog.Add(4, "Editor", $"Viewport creation failed: {Native.LastError()}");
            return base.CreateNativeControlCore(parent);
        }
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
