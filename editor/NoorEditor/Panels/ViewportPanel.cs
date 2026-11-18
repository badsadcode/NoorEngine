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
    private readonly EditorSettings _settings;
    private readonly ToggleButton _snap = new() { Content = "Snap", Classes = { "tool" } };
    private readonly DispatcherTimer _events = new() { Interval = TimeSpan.FromMilliseconds(30) };
    private readonly Dictionary<EditorTool, ToggleButton> _toolButtons = new();

    public VulkanViewport Viewport { get; }

    public ViewportPanel(EditorDocument doc, EditorSettings settings)
    {
        _doc = doc;
        _settings = settings;
        Viewport = new VulkanViewport(doc);

        var toolbar = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 2, Margin = new Thickness(6, 3) };
        foreach (var (tool, label, key) in new[] { (EditorTool.Select, "Select", "Q"), (EditorTool.Move, "Move", "W"), (EditorTool.Rotate, "Rotate", "E"), (EditorTool.Scale, "Scale", "R") })
        {
            var button = new ToggleButton { Content = label, Classes = { "tool" } };
            ToolTip.SetTip(button, $"{label} ({key})");
            button.Click += (_, _) => { _doc.SetTool(tool); SyncToolButtons(); };
            _toolButtons[tool] = button;
            toolbar.Children.Add(button);
        }
        ToolTip.SetTip(_snap, "Snap gizmo drags to steps (hold Ctrl while dragging to invert). Steps: Edit ▸ Preferences");
        _snap.Click += (_, _) =>
        {
            var p = _settings.Preferences.Copy();
            p.Snap = _snap.IsChecked == true;
            _settings.SetPreferences(p);
        };
        toolbar.Children.Add(_snap);
        toolbar.Children.Add(new Border { Width = 1, Background = NoorTheme.Brush(NoorTheme.Border), Margin = new Thickness(6, 4) });
        var frameAll = new Button { Content = "Frame all", Classes = { "tool" } };
        frameAll.Click += (_, _) => Viewport.Frame(null);
        var frameSel = new Button { Content = "Frame selected", Classes = { "tool" } };
        frameSel.Click += (_, _) => { if (doc.SelectedId is { } id) Viewport.Frame(doc.Subtree(id)); };
        toolbar.Children.Add(frameAll);
        toolbar.Children.Add(frameSel);
        toolbar.Children.Add(new TextBlock
        {
            Text = "Click select · Hold right: look + WASD/QE fly · Alt+drag orbit · Middle pan · Wheel zoom · F frame",
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
        _doc.SculptChanged += SyncViewport;
        _settings.PreferencesChanged += ApplyPreferences;
        ApplyPreferences();
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
        _doc.SculptChanged -= SyncViewport;
        _settings.PreferencesChanged -= ApplyPreferences;
    }

    private void ApplyPreferences()
    {
        _snap.IsChecked = _settings.Preferences.Snap;
        Viewport.SetOptions(_settings.Preferences.ToViewportJson());
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
        // Sculpting the selected terrain: the brush takes the left button, the gizmo goes away.
        var sculpting = (_doc.Sculpting || (_doc.Painting && _doc.PaintMaterial != null)) && selected is { FromPrefab: false } &&
                        selected.Components["terrain"] != null && !_doc.Playing;
        // Planting and removing scattered trees: the brush works on the scatter's ground terrain.
        var ground = _doc.Scattering != null && selected is { FromPrefab: false } && !_doc.Playing ? selected.Components["scatter"]?["ground"]?.GetValue<string>() : null;
        if (ground != null && _doc.Scene.Entities.All(e => e.Id != ground)) ground = null;
        sculpting = sculpting || ground != null;
        Viewport.SetBrush(ground ?? (sculpting ? selected!.Id : null), _doc.Brush.Radius);
        var mode = selected is { FromPrefab: false } && !_doc.Playing && !sculpting ? (int)_doc.Tool : 0;  // EditorTool values match noor_gizmo_mode
        Viewport.SetGizmo(mode == 0 ? null : selected!.Id, mode);
        Viewport.SetFaceHighlight(selected != null && _doc.SelectedFace is { } face ? selected.Id : null, _doc.SelectedFace ?? 0);
    }

    private bool _stroking;

    /// <summary>A sculpt stroke event from the viewport: begin carries the brush; each piece is sent on to the document.</summary>
    private void Stroke(JsonNode ev)
    {
        var phase = ev["phase"]?.GetValue<string>() ?? "dab";
        var terrain = ev["terrain"]?.GetValue<string>();
        if (phase == "begin") _stroking = terrain != null;
        if (!_stroking) return;
        var command = new JsonObject { ["terrain"] = terrain, ["phase"] = phase, ["dabs"] = new JsonArray() };
        if (ev["point"] is JsonArray point)
            ((JsonArray)command["dabs"]!).Add(new JsonObject { ["point"] = point.DeepClone(), ["seconds"] = ev["seconds"]?.GetValue<double>() ?? 0 });
        if (_doc.Scattering != null && _doc.Selected?.Components["scatter"] != null)
        {
            // Planting or removing scattered trees (Milestone 7, part 7).
            command.Remove("terrain");
            command["scatter"] = _doc.Selected.Id;
            if (phase == "begin")
            {
                command["tool"] = _doc.Scattering;
                command["radius"] = _doc.Brush.Radius;
                if (_doc.ScatterAsset != null) command["asset"] = _doc.ScatterAsset;
            }
            if (phase == "end") _stroking = false;
            if (_doc.ScatterEdit(command, refresh: phase != "dab") == null) _stroking = false;
            return;
        }
        if (phase == "begin")
        {
            var b = _doc.Brush;
            command["tool"] = _doc.Painting ? "paint" : b.Tool;
            if (_doc.Painting) command["material"] = _doc.PaintMaterial;
            command["radius"] = b.Radius;
            command["strength"] = b.Strength;
            command["falloff"] = b.Falloff;
        }
        if (phase == "end") _stroking = false;
        if (!_doc.Sculpt(command, refresh: phase != "dab")) _stroking = false;
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
                case "click" when _doc.Painting && _doc.PaintMaterial != null && ev["face"] is JsonValue paintFace &&
                                  ev["entity"]?.GetValue<string>() is { } paintBrush && _doc.Scene.Entities.Any(e => e.Id == paintBrush && e.Components["brush"] != null && !e.FromPrefab):
                    // Painting: a click on a brush face gives it the material (the selection stays).
                    if (!_doc.PaintFace(paintBrush, paintFace.GetValue<int>())) EngineLog.Warn("Paint", Native.LastError());
                    break;
                case "click":
                    var hitId = ev["entity"]?.GetValue<string>();
                    var target = _doc.ClickTarget(hitId);
                    _doc.Select(target);
                    // A CSG face: when the click selected the brush itself, remember which face.
                    _doc.SelectFace(target != null && target == hitId && ev["face"] is JsonValue f ? f.GetValue<int>() : null);
                    break;
                case "stroke":
                    Stroke(ev);
                    break;
                case "mouse_capture":  // a script asked (play mode); the cursor belongs to the UI thread
                    Native.noor_viewport_set_mouse_capture(Viewport.EngineHandle, ev["capture"]?.GetValue<bool>() == true ? 1 : 0);
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
