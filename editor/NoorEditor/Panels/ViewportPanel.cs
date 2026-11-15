using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using NoorEditor.Model;
using NoorEditor.Shell;

namespace NoorEditor.Panels;

/// <summary>Toolbar + engine viewport. Camera: right-drag orbits, middle-drag pans, wheel zooms, F frames.</summary>
public sealed class ViewportPanel : DockPanel
{
    public VulkanViewport Viewport { get; }

    public ViewportPanel(EditorDocument doc)
    {
        Viewport = new VulkanViewport(doc);

        var frameAll = new Button { Content = "Frame all", Classes = { "tool" } };
        frameAll.Click += (_, _) => Viewport.Frame(null);
        var frameSel = new Button { Content = "Frame selected", Classes = { "tool" } };
        frameSel.Click += (_, _) => { if (doc.SelectedId is { } id) Viewport.Frame(doc.Subtree(id)); };
        var hint = new TextBlock
        {
            Text = "Right-drag orbit · Middle-drag pan · Wheel zoom · F frame",
            FontSize = 11, Classes = { "muted" }, Margin = new Thickness(12, 0, 0, 0),
        };
        var toolbar = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 2, Margin = new Thickness(6, 3), Children = { frameAll, frameSel, hint } };
        foreach (var c in toolbar.Children) c.VerticalAlignment = VerticalAlignment.Center;

        var host = new Border { Child = toolbar, Background = NoorTheme.Brush(NoorTheme.Panel) };
        SetDock(host, Avalonia.Controls.Dock.Top);
        Children.Add(host);
        Children.Add(Viewport);
    }
}
