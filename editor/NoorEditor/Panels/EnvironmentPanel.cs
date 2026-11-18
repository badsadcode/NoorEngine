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
/// The environment gallery (Milestone 8A, part 6): the six built-in presets and the project's own environments
/// (Assets/Environments/*.noorenv) as cards with a picture. Apply makes the scene's environment point at one (a preset
/// is copied into the project first, once); Duplicate copies a project environment; Edit shows its settings to
/// change (saved as you go); Reset puts a copy back to the preset it came from. The built-in presets themselves are
/// never written.
/// </summary>
public sealed class EnvironmentPanel : DockPanel
{
    private readonly EditorDocument _doc;
    private readonly WrapPanel _presets = new();
    private readonly WrapPanel _project = new();
    private readonly StackPanel _inspector = new() { Spacing = 4, Margin = new Thickness(10, 8), MinWidth = 260 };
    private readonly TextBlock _status = new() { FontSize = 11, Classes = { "muted" }, TextWrapping = TextWrapping.Wrap };
    private readonly Button _apply = new() { Content = "Apply", Classes = { "tool" } };
    private readonly Button _duplicate = new() { Content = "Duplicate", Classes = { "tool" } };
    private readonly Button _edit = new() { Content = "Edit", Classes = { "tool" } };
    private readonly Button _reset = new() { Content = "Reset", Classes = { "tool" } };

    /// <summary>What's selected: a preset by name, or a project environment by path.</summary>
    public string? SelectedPreset { get; private set; }
    public string? SelectedAsset { get; private set; }
    /// <summary>The project's environments as last listed (tests read them).</summary>
    public JsonArray ProjectEnvironments { get; private set; } = [];

    public EnvironmentPanel(EditorDocument doc)
    {
        _doc = doc;
        ToolTip.SetTip(_apply, "Make the scene's environment this one (one undo step). A preset is copied into Assets/Environments first.");
        ToolTip.SetTip(_duplicate, "A copy in the project, to change without touching this one");
        ToolTip.SetTip(_edit, "Change this environment's settings (saved as you go)");
        ToolTip.SetTip(_reset, "Put this copy back to the built-in preset it came from");
        _apply.Click += (_, _) => Apply();
        _duplicate.Click += (_, _) => Duplicate();
        _edit.Click += (_, _) => ShowInspector();
        _reset.Click += (_, _) => Reset();

        var gallery = new StackPanel
        {
            Spacing = 6, Margin = new Thickness(10, 8),
            Children =
            {
                new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6, Children = { _apply, _duplicate, _edit, _reset } },
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
        Rebuild();
    }

    // ---- the gallery ----

    private void Rebuild()
    {
        _presets.Children.Clear();
        foreach (var preset in EditorDocument.EnvironmentPresets().OfType<JsonObject>())
        {
            var name = preset["name"]!.GetValue<string>();
            _presets.Children.Add(Card(name, preset["description"]?.GetValue<string>() ?? "", Thumbnail(name), false,
                                       SelectedPreset == name, () => Select(name, null)));
        }
        _project.Children.Clear();
        ProjectEnvironments = _doc.Environment(new JsonObject { ["op"] = "list" }) as JsonArray ?? [];
        var active = ActiveAssetGuid();
        foreach (var env in ProjectEnvironments.OfType<JsonObject>())
        {
            var path = env["path"]!.GetValue<string>();
            var preset = env["preset"]?.GetValue<string>() ?? "";
            var subtitle = (env["guid"]?.GetValue<string>() == active ? "In the scene. " : "") +
                           (preset.Length > 0 ? $"From {preset}" : "Custom");
            _project.Children.Add(Card(env["name"]?.GetValue<string>() ?? path, subtitle, Thumbnail(preset), preset.Length == 0 ||
                                       !SameAsPreset(env, preset), SelectedAsset == path, () => Select(null, path)));
        }
        if (ProjectEnvironments.Count == 0)
            _project.Children.Add(new TextBlock { Text = "None yet: apply or duplicate a preset.", FontSize = 11, Classes = { "muted" } });
        UpdateButtons();
    }

    private static bool SameAsPreset(JsonObject env, string preset) =>
        EditorDocument.EnvironmentPresets().OfType<JsonObject>().FirstOrDefault(p => p["name"]?.GetValue<string>() == preset) is { } p &&
        JsonNode.DeepEquals(p["environment"], env["environment"]);

    private Control Card(string title, string subtitle, Bitmap? picture, bool edited, bool selected, Action pick)
    {
        var image = picture != null
            ? (Control)new Image { Source = picture, Width = 176, Height = 99, Stretch = Stretch.UniformToFill }
            : new Border { Width = 176, Height = 99, Background = new LinearGradientBrush
              {
                  StartPoint = new RelativePoint(0, 0, RelativeUnit.Relative), EndPoint = new RelativePoint(0, 1, RelativeUnit.Relative),
                  GradientStops = { new GradientStop(Color.Parse("#3A4E7A"), 0), new GradientStop(Color.Parse("#B7C3D8"), 1) },
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
            if (e.ClickCount == 2) Apply();  // double-click applies
        };
        return card;
    }

    /// <summary>A preset's picture (rendered by tools/samples/render_environment_thumbnails.py, embedded); null for none.</summary>
    private static Bitmap? Thumbnail(string preset)
    {
        if (string.IsNullOrEmpty(preset)) return null;
        using var stream = typeof(EnvironmentPanel).Assembly.GetManifestResourceStream($"NoorEditor.Environments.{preset}.png");
        return stream == null ? null : new Bitmap(stream);
    }

    private string? ActiveAssetGuid() =>
        _doc.Scene.Entities.Select(e => e.Components["environment"]).OfType<JsonObject>().FirstOrDefault()?["asset"]?.GetValue<string>();

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
        _apply.IsEnabled = SelectedPreset != null || SelectedAsset != null;
        _duplicate.IsEnabled = SelectedAsset != null;
        _edit.IsEnabled = SelectedAsset != null;
        var selected = ProjectEnvironments.OfType<JsonObject>().FirstOrDefault(e => e["path"]?.GetValue<string>() == SelectedAsset);
        _reset.IsEnabled = selected != null && (selected["preset"]?.GetValue<string>() ?? "").Length > 0;
        _status.Text = SelectedPreset != null ? "A built-in preset: Apply copies it into the project (once) and uses it."
                     : SelectedAsset != null ? SelectedAsset : "Pick an environment.";
    }

    // ---- the actions (public: the smoke test uses them as the buttons do) ----

    public JsonNode? Apply()
    {
        if (!_doc.Scene.Open) return null;
        var command = new JsonObject { ["op"] = "apply" };
        if (SelectedPreset != null) command["preset"] = SelectedPreset;
        else if (SelectedAsset != null) command["asset"] = SelectedAsset;
        else return null;
        var result = _doc.Environment(command);
        if (result != null) Interop.EngineLog.Info("Environment", $"Applied {result["name"]} ({result["path"]})");
        return result;
    }

    public JsonNode? Duplicate()
    {
        if (SelectedAsset == null) return null;
        var result = _doc.Environment(new JsonObject { ["op"] = "duplicate", ["asset"] = SelectedAsset });
        if (result?["path"]?.GetValue<string>() is { } path) Select(null, path);
        return result;
    }

    public JsonNode? Reset()
    {
        if (SelectedAsset == null) return null;
        var result = _doc.Environment(new JsonObject { ["op"] = "reset", ["asset"] = SelectedAsset });
        if (_inspector.Children.Count > 0) ShowInspector();
        return result;
    }

    // ---- the inspector: every setting of the selected project environment ----

    private void ShowInspector()
    {
        _inspector.Children.Clear();
        if (SelectedAsset == null) return;
        if (_doc.Environment(new JsonObject { ["op"] = "read", ["asset"] = SelectedAsset }) is not JsonObject env) return;
        _inspector.Children.Add(new TextBlock { Text = env["name"]?.GetValue<string>() ?? SelectedAsset, Classes = { "section" } });
        _inspector.Children.Add(new TextBlock { Text = SelectedAsset, FontSize = 10, Classes = { "muted" } });
        if (env["environment"] is not JsonObject settings) return;
        foreach (var (field, value) in settings.OrderBy(kv => kv.Key))
            _inspector.Children.Add(Row(field, value));
    }

    private Control Row(string field, JsonNode? value)
    {
        var label = new TextBlock { Text = field, FontSize = 12, Classes = { "muted" }, VerticalAlignment = VerticalAlignment.Center };
        Control editor;
        if (value is JsonValue v && v.TryGetValue<bool>(out var b))
        {
            var check = new CheckBox { IsChecked = b };
            check.IsCheckedChanged += (_, _) => SaveField(field, JsonValue.Create(check.IsChecked == true));
            editor = check;
        }
        else if (value is JsonValue n && n.TryGetValue<double>(out var d))
        {
            var box = new TextBox { Text = d.ToString(CultureInfo.InvariantCulture), Classes = { "field" } };
            void Commit()
            {
                if (double.TryParse(box.Text, NumberStyles.Float, CultureInfo.InvariantCulture, out var x) && x != d) SaveField(field, JsonValue.Create(x));
            }
            box.LostFocus += (_, _) => Commit();
            box.KeyDown += (_, e) => { if (e.Key == Key.Enter) Commit(); };
            editor = box;
        }
        else
        {
            var text = value is JsonValue s && s.TryGetValue<string>(out var str) ? str : value?.ToJsonString() ?? "";
            var box = new TextBox { Text = text, Classes = { "field" } };
            void Commit()
            {
                if (box.Text == text) return;
                // "auto" and words stay text; numbers typed into a text field become numbers.
                JsonNode node = double.TryParse(box.Text, NumberStyles.Float, CultureInfo.InvariantCulture, out var x) ? JsonValue.Create(x) : JsonValue.Create(box.Text ?? "");
                SaveField(field, node);
            }
            box.LostFocus += (_, _) => Commit();
            box.KeyDown += (_, e) => { if (e.Key == Key.Enter) Commit(); };
            editor = box;
        }
        var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("130,*") };
        Grid.SetColumn(editor, 1);
        grid.Children.Add(label);
        grid.Children.Add(editor);
        return grid;
    }

    /// <summary>Changes one setting of the selected project environment and saves it (the scene follows at once).</summary>
    public bool SaveField(string field, JsonNode value)
    {
        if (SelectedAsset == null) return false;
        if (_doc.Environment(new JsonObject { ["op"] = "read", ["asset"] = SelectedAsset }) is not JsonObject env ||
            env["environment"] is not JsonObject settings) return false;
        var next = (JsonObject)settings.DeepClone();
        next[field] = value;
        return _doc.Environment(new JsonObject { ["op"] = "save", ["asset"] = SelectedAsset, ["environment"] = next }) != null;
    }
}
