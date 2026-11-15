using System.Text.Json.Nodes;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Layout;
using Avalonia.Threading;
using NoorEditor.Interop;
using NoorEditor.Model;
using NoorEditor.Shell;
using Transform = NoorEditor.Model.Transform;

namespace NoorEditor.Panels;

/// <summary>
/// Toolbar + engine viewport. Camera: hold right to look and fly (WASD, Q/E), Alt + left-drag orbits,
/// middle-drag pans, the wheel zooms, F frames.
/// Left click selects (again to go one level deeper); the gizmo moves or rotates the selection.
/// The viewport reports clicks and finished drags as events; this panel turns them into
/// selection changes and undoable document commands.
/// </summary>
public sealed class ViewportPanel : DockPanel
{
    private readonly EditorDocument _doc;
    private readonly DispatcherTimer _events = new() { Interval = TimeSpan.FromMilliseconds(30) };
    private readonly Dictionary<EditorTool, ToggleButton> _toolButtons = new();

    public VulkanViewport Viewport { get; }

    public ViewportPanel(EditorDocument doc)
    {
        _doc = doc;
        Viewport = new VulkanViewport(doc);

        var toolbar = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 2, Margin = new Thickness(6, 3) };
        foreach (var (tool, label, key) in new[] { (EditorTool.Select, "Select", "Q"), (EditorTool.Move, "Move", "W"), (EditorTool.Rotate, "Rotate", "E") })
        {
            var button = new ToggleButton { Content = label, Classes = { "tool" } };
            ToolTip.SetTip(button, $"{label} ({key})");
            button.Click += (_, _) => { _doc.SetTool(tool); SyncToolButtons(); };
            _toolButtons[tool] = button;
            toolbar.Children.Add(button);
        }
        toolbar.Children.Add(new Border { Width = 1, Background = NoorTheme.Brush(NoorTheme.Border), Margin = new Thickness(6, 4) });
        var frameAll = new Button { Content = "Frame all", Classes = { "tool" } };
        frameAll.Click += (_, _) => Viewport.Frame(null);
        var frameSel = new Button { Content = "Frame selected", Classes = { "tool" } };
        frameSel.Click += (_, _) => { if (doc.SelectedId is { } id) Viewport.Frame(doc.Subtree(id)); };
        toolbar.Children.Add(frameAll);
        toolbar.Children.Add(frameSel);
        toolbar.Children.Add(new TextBlock
        {
            Text = "Click select · Hold right: look + WASD/QE fly (Shift fast, wheel speed) · Alt+drag orbit · Middle pan · Wheel zoom · F frame",
            FontSize = 11, Classes = { "muted" }, Margin = new Thickness(12, 0, 0, 0),
        });
        foreach (var c in toolbar.Children) c.VerticalAlignment = VerticalAlignment.Center;

        var host = new Border { Child = toolbar, Background = NoorTheme.Brush(NoorTheme.Panel) };
        SetDock(host, Avalonia.Controls.Dock.Top);
        Children.Add(host);
        Children.Add(Viewport);

        _events.Tick += (_, _) => DrainEvents();
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        _doc.SelectionChanged += SyncViewport;
        _doc.SceneChanged += SyncViewport;
        _doc.ToolChanged += OnToolChanged;
        SyncToolButtons();
        SyncViewport();
        _events.Start();
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnDetachedFromVisualTree(e);
        _events.Stop();
        _doc.SelectionChanged -= SyncViewport;
        _doc.SceneChanged -= SyncViewport;
        _doc.ToolChanged -= OnToolChanged;
    }

    private void OnToolChanged()
    {
        SyncToolButtons();
        SyncViewport();
    }

    private void SyncToolButtons()
    {
        foreach (var (tool, button) in _toolButtons) button.IsChecked = tool == _doc.Tool;
    }

    /// <summary>Pushes selection highlight and gizmo state to the engine viewport.</summary>
    private void SyncViewport()
    {
        var selected = _doc.Selected;
        Viewport.SetSelection(selected == null ? [] : _doc.Subtree(selected.Id));
        var mode = selected is { FromPrefab: false } ? (int)_doc.Tool : 0;  // EditorTool values match noor_gizmo_mode
        Viewport.SetGizmo(mode == 0 ? null : selected!.Id, mode);
    }

    /// <summary>Handles the viewport's clicks and gizmo results (also called by the smoke test).</summary>
    public void DrainEvents()
    {
        var json = Viewport.PollEvents();
        if (json == "[]") return;
        foreach (var ev in JsonNode.Parse(json) as JsonArray ?? [])
        {
            switch (ev?["type"]?.GetValue<string>())
            {
                case "click":
                    _doc.Select(_doc.ClickTarget(ev["entity"]?.GetValue<string>()));
                    break;
                case "transform":
                    var id = ev["entity"]!.GetValue<string>();
                    var label = ev["label"]?.GetValue<string>() ?? "Move";
                    if (!_doc.SetTransform(id, Transform.FromJson(ev["transform"]), label))
                        EngineLog.Warn("Editor", $"{label} was not applied: {Native.LastError()}");
                    break;
            }
        }
    }
}
