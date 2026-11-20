using System.Globalization;
using System.Text.Json.Nodes;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using NoorEditor.Model;
using NoorEditor.Shell;

namespace NoorEditor.Panels;

/// <summary>
/// The Water panel (Milestone 8D, part 7): the built-in water looks (Lake, Pond, Sea, Tropical, Swamp, River, Deep sea)
/// and the project's own (Assets/Water/*.noorwater) as cards with a picture. Add makes a new 20 x 20 m surface with the
/// picked look; Use on selected gives the selected entity's water that look (its shape and size kept); a preset is
/// copied into the project first, once. Duplicate copies a project look; Edit shows its settings (saved as you type:
/// the viewport follows at once, and so does a playing game); Reset puts a copy back to its preset. The debug view
/// shows the water's depth, flow, waves, normals or cost in the viewports. The built-in presets are never written.
/// </summary>
public sealed class WaterPanel : DockPanel
{
    private readonly EditorDocument _doc;
    private readonly WrapPanel _presets = new();
    private readonly WrapPanel _project = new();
    private readonly StackPanel _inspector = new() { Spacing = 4, Margin = new Thickness(10, 8), MinWidth = 260 };
    private readonly TextBlock _status = new() { FontSize = 11, Classes = { "muted" }, TextWrapping = TextWrapping.Wrap };
    private readonly Button _add = new() { Content = "Add", Classes = { "tool" } };
    private readonly Button _use = new() { Content = "Use on selected", Classes = { "tool" } };
    private readonly Button _duplicate = new() { Content = "Duplicate", Classes = { "tool" } };
    private readonly Button _edit = new() { Content = "Edit", Classes = { "tool" } };
    private readonly Button _reset = new() { Content = "Reset", Classes = { "tool" } };
    private readonly ComboBox _debug = new() { MinWidth = 120 };

    public static readonly string[] DebugViews = ["none", "depth", "flow", "waves", "normals", "cost"];

    /// <summary>What's selected: a preset by name, or a project look by path.</summary>
    public string? SelectedPreset { get; private set; }
    public string? SelectedAsset { get; private set; }
    /// <summary>The project's water looks as last listed (tests read them).</summary>
    public JsonArray ProjectWater { get; private set; } = [];

    public WaterPanel(EditorDocument doc)
    {
        _doc = doc;
        ToolTip.SetTip(_add, "A new 20 x 20 m water surface with this look (one undo step). A preset is copied into Assets/Water first.");
        ToolTip.SetTip(_use, "Give the selected entity's water this look, keeping its shape and size (one undo step)");
        ToolTip.SetTip(_duplicate, "A copy in the project, to change without touching this one");
        ToolTip.SetTip(_edit, "Change this look's settings (saved as you go; the viewport follows at once)");
        ToolTip.SetTip(_reset, "Put this copy back to the built-in preset it came from");
        ToolTip.SetTip(_debug, "Show water in the viewports as: its depth, its flow, its waves, its normals, or what it costs");
        _add.Click += (_, _) => Add();
        _use.Click += (_, _) => UseOnSelected();
        _duplicate.Click += (_, _) => Duplicate();
        _edit.Click += (_, _) => ShowInspector();
        _reset.Click += (_, _) => Reset();
        foreach (var v in DebugViews) _debug.Items.Add(char.ToUpperInvariant(v[0]) + v[1..]);
        _debug.SelectedIndex = 0;
        _debug.SelectionChanged += (_, _) => SetDebugView(DebugViews[Math.Max(0, _debug.SelectedIndex)]);

        var gallery = new StackPanel
        {
            Spacing = 6, Margin = new Thickness(10, 8),
            Children =
            {
                new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6, Children = { _add, _use, _duplicate, _edit, _reset } },
                new StackPanel
                {
                    Orientation = Orientation.Horizontal, Spacing = 6,
                    Children = { new TextBlock { Text = "Debug view", FontSize = 12, Classes = { "muted" }, VerticalAlignment = VerticalAlignment.Center }, _debug },
                },
                _status,
                new TextBlock { Text = "Presets (built in)", Classes = { "section" } },
                _presets,
                new TextBlock { Text = "In this project", Classes = { "section" } },
                _project,
            },
        };
        var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto") };
        var inspectorScroll = new ScrollViewer { Content = _inspector };
        Grid.SetColumn(inspectorScroll, 1);
        grid.Children.Add(new ScrollViewer { Content = gallery });
        grid.Children.Add(inspectorScroll);
        Children.Add(grid);

        _doc.AssetsChanged += Rebuild;
        _doc.SceneChanged += UpdateButtons;
        _doc.SelectionChanged += UpdateButtons;
        Rebuild();
    }

    // ---- the gallery ----

    private void Rebuild()
    {
        _presets.Children.Clear();
        foreach (var preset in EditorDocument.WaterPresets().OfType<JsonObject>())
        {
            var name = preset["name"]!.GetValue<string>();
            _presets.Children.Add(Card(name, preset["description"]?.GetValue<string>() ?? "", Thumbnail(name), false,
                                       SelectedPreset == name, () => Select(name, null)));
        }
        _project.Children.Clear();
        ProjectWater = _doc.Water(new JsonObject { ["op"] = "list" }) as JsonArray ?? [];
        var used = UsedGuids();
        foreach (var water in ProjectWater.OfType<JsonObject>())
        {
            var path = water["path"]!.GetValue<string>();
            var preset = water["preset"]?.GetValue<string>() ?? "";
            var subtitle = (used.Contains(water["guid"]?.GetValue<string>() ?? "") ? "In the scene. " : "") +
                           (preset.Length > 0 ? $"From {preset}" : "Custom");
            _project.Children.Add(Card(water["name"]?.GetValue<string>() ?? path, subtitle, Thumbnail(preset),
                                       preset.Length == 0 || !SameAsPreset(water, preset), SelectedAsset == path, () => Select(null, path)));
        }
        if (ProjectWater.Count == 0)
            _project.Children.Add(new TextBlock { Text = "None yet: add a preset, or use one on a water surface.", FontSize = 11, Classes = { "muted" } });
        UpdateButtons();
    }

    private HashSet<string> UsedGuids() =>
        _doc.Scene.Entities.Select(e => e.Components["water"]?["asset"]?.GetValue<string>()).OfType<string>().ToHashSet();

    private static bool SameAsPreset(JsonObject water, string preset) =>
        EditorDocument.WaterPresets().OfType<JsonObject>().FirstOrDefault(p => p["name"]?.GetValue<string>() == preset) is { } p &&
        JsonNode.DeepEquals(p["water"], water["water"]);

    private Control Card(string title, string subtitle, Bitmap? picture, bool edited, bool selected, Action pick)
    {
        var image = picture != null
            ? (Control)new Image { Source = picture, Width = 176, Height = 99, Stretch = Stretch.UniformToFill }
            : new Border { Width = 176, Height = 99, Background = new LinearGradientBrush
              {
                  StartPoint = new RelativePoint(0, 0, RelativeUnit.Relative), EndPoint = new RelativePoint(0, 1, RelativeUnit.Relative),
                  GradientStops = { new GradientStop(Color.Parse("#2E6F80"), 0), new GradientStop(Color.Parse("#0E3A4A"), 1) },
              } };
        var stack = new StackPanel { Spacing = 2, Children = { image, new TextBlock { Text = title, FontSize = 12, FontWeight = FontWeight.SemiBold } } };
        if (edited) stack.Children.Add(new TextBlock { Text = "edited", FontSize = 10, Foreground = NoorTheme.Brush(NoorTheme.Warn) });
        stack.Children.Add(new TextBlock { Text = subtitle, FontSize = 10, Classes = { "muted" }, TextWrapping = TextWrapping.Wrap, MaxWidth = 176 });
        var card = new Border
        {
            Child = stack, Padding = new Thickness(6), Margin = new Thickness(0, 0, 8, 8), CornerRadius = new CornerRadius(6), Width = 190,
            BorderThickness = new Thickness(2), Cursor = new Cursor(StandardCursorType.Hand),
            BorderBrush = NoorTheme.Brush(selected ? NoorTheme.Accent : NoorTheme.Border),
            Background = NoorTheme.Brush(NoorTheme.PanelRaised),
        };
        card.PointerPressed += (_, e) =>
        {
            pick();
            if (e.ClickCount == 2) { if (SelectedWaterEntity() != null) UseOnSelected(); else Add(); }  // double-click applies
        };
        return card;
    }

    /// <summary>A preset's picture (rendered by tools/samples/render_water_thumbnails.py, embedded); null for none.</summary>
    private static Bitmap? Thumbnail(string preset)
    {
        if (string.IsNullOrEmpty(preset)) return null;
        using var stream = typeof(WaterPanel).Assembly.GetManifestResourceStream($"NoorEditor.Water.{preset}.png");
        return stream == null ? null : new Bitmap(stream);
    }

    /// <summary>The selected entity, if it has a water component.</summary>
    private string? SelectedWaterEntity() =>
        _doc.SelectedId is { } id && _doc.Scene.Entities.FirstOrDefault(e => e.Id == id)?.Components["water"] != null ? id : null;

    public void Select(string? preset, string? asset)
    {
        SelectedPreset = preset;
        SelectedAsset = asset;
        Rebuild();
        if (asset != null && _inspector.Children.Count > 0) ShowInspector();  // keep the inspector on the selection
        else _inspector.Children.Clear();
    }

    private void UpdateButtons()
    {
        var picked = SelectedPreset != null || SelectedAsset != null;
        _add.IsEnabled = picked && _doc.Scene.Open;
        _use.IsEnabled = picked && SelectedWaterEntity() != null;
        _duplicate.IsEnabled = SelectedAsset != null;
        _edit.IsEnabled = SelectedAsset != null;
        var selected = ProjectWater.OfType<JsonObject>().FirstOrDefault(e => e["path"]?.GetValue<string>() == SelectedAsset);
        _reset.IsEnabled = selected != null && (selected["preset"]?.GetValue<string>() ?? "").Length > 0;
        _status.Text = SelectedPreset != null ? "A built-in look: Add or Use copies it into the project (once) and uses it."
                     : SelectedAsset != null ? SelectedAsset : "Pick a look. Select a water surface in the scene to change its look.";
    }

    // ---- the actions (public: the smoke test uses them as the buttons do) ----

    private JsonObject? Command(string op)
    {
        var command = new JsonObject { ["op"] = op };
        if (SelectedPreset != null) command["preset"] = SelectedPreset;
        else if (SelectedAsset != null) command["asset"] = SelectedAsset;
        else return null;
        return command;
    }

    public JsonNode? Add()
    {
        if (!_doc.Scene.Open || Command("apply") is not { } command) return null;
        var result = _doc.Water(command);
        if (result?["entity"]?.GetValue<string>() is { } id) _doc.Select(id);
        if (result?["path"]?.GetValue<string>() is { } path) Select(null, path);
        return result;
    }

    public JsonNode? UseOnSelected()
    {
        if (SelectedWaterEntity() is not { } id || Command("apply") is not { } command) return null;
        command["entity"] = id;
        var result = _doc.Water(command);
        if (result?["path"]?.GetValue<string>() is { } path) Select(null, path);
        return result;
    }

    public JsonNode? Duplicate()
    {
        if (SelectedAsset == null) return null;
        var result = _doc.Water(new JsonObject { ["op"] = "duplicate", ["asset"] = SelectedAsset });
        if (result?["path"]?.GetValue<string>() is { } path) Select(null, path);
        return result;
    }

    public JsonNode? Reset()
    {
        if (SelectedAsset == null) return null;
        var result = _doc.Water(new JsonObject { ["op"] = "reset", ["asset"] = SelectedAsset });
        if (_inspector.Children.Count > 0) ShowInspector();
        return result;
    }

    /// <summary>The viewports' debug view of water: none, depth, flow, waves, normals or cost.</summary>
    public string? SetDebugView(string view)
    {
        var result = _doc.Water(new JsonObject { ["op"] = "debug", ["view"] = view })?["view"]?.GetValue<string>();
        var index = Array.IndexOf(DebugViews, result ?? "none");
        if (index >= 0 && _debug.SelectedIndex != index) _debug.SelectedIndex = index;
        return result;
    }

    // ---- the inspector: every setting of the selected project look ----

    private void ShowInspector()
    {
        _inspector.Children.Clear();
        if (SelectedAsset == null) return;
        if (_doc.Water(new JsonObject { ["op"] = "read", ["asset"] = SelectedAsset }) is not JsonObject water) return;
        _inspector.Children.Add(new TextBlock { Text = water["name"]?.GetValue<string>() ?? SelectedAsset, Classes = { "section" } });
        _inspector.Children.Add(new TextBlock { Text = SelectedAsset, FontSize = 10, Classes = { "muted" } });
        if (water["water"] is not JsonObject settings) return;
        foreach (var (field, value) in settings.OrderBy(kv => kv.Key))
            _inspector.Children.Add(Row(field, value));
    }

    private Control Row(string field, JsonNode? value)
    {
        var label = new TextBlock { Text = field, FontSize = 12, Classes = { "muted" }, VerticalAlignment = VerticalAlignment.Center };
        var text = value is JsonValue v && v.TryGetValue<double>(out var d) ? d.ToString(CultureInfo.InvariantCulture)
                 : value is JsonValue s && s.TryGetValue<string>(out var str) ? str : value?.ToJsonString() ?? "";
        var box = new TextBox { Text = text, Classes = { "field" } };
        void Commit()
        {
            if (box.Text == text) return;
            // Numbers stay numbers; "#RRGGBB" colours and words stay text; [r, g, b] lists are parsed.
            JsonNode? node = double.TryParse(box.Text, NumberStyles.Float, CultureInfo.InvariantCulture, out var x) ? JsonValue.Create(x)
                           : (box.Text ?? "").TrimStart().StartsWith('[') ? JsonNode.Parse(box.Text!) : JsonValue.Create(box.Text ?? "");
            if (node != null && SaveField(field, node)) text = box.Text ?? "";
        }
        box.LostFocus += (_, _) => Commit();
        box.KeyDown += (_, e) => { if (e.Key == Key.Enter) Commit(); };
        var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("120,*") };
        Grid.SetColumn(box, 1);
        grid.Children.Add(label);
        grid.Children.Add(box);
        return grid;
    }

    /// <summary>Changes one setting of the selected project look and saves it (the scene follows at once).</summary>
    public bool SaveField(string field, JsonNode value)
    {
        if (SelectedAsset == null) return false;
        if (_doc.Water(new JsonObject { ["op"] = "read", ["asset"] = SelectedAsset }) is not JsonObject water ||
            water["water"] is not JsonObject settings) return false;
        var next = (JsonObject)settings.DeepClone();
        next[field] = value;
        return _doc.Water(new JsonObject { ["op"] = "save", ["asset"] = SelectedAsset, ["water"] = next }) != null;
    }
}
