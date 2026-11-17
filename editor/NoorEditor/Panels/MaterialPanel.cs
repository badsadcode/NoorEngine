using System.Diagnostics;
using System.Text.Json;
using System.Text.Json.Nodes;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;
using Dock.Model.Mvvm.Controls;
using NoorEditor.Interop;
using NoorEditor.Model;
using NoorEditor.Shell;

namespace NoorEditor.Panels;

/// <summary>How the material preview is shown (shared by every material tab, remembered while the editor runs).</summary>
public sealed class MaterialPreviewSettings
{
    public string Shape { get; set; } = "sphere";
    public string Hdri { get; set; } = "";  // project path of an .hdr, "" = procedural sky
    public double Ev100 { get; set; } = 14;
    public double Rotation { get; set; }
}

/// <summary>
/// A material open in the material editor (Milestone 5, part 6): a tab next to the viewport. Holds
/// the file's state, so the view can be rebuilt whenever Dock shows the tab again. Edits are saved
/// to the .noormat at once (after a short pause), so the scene, running games and the preview all
/// read the same file; Ctrl+Z / Ctrl+Y undo and redo inside the tab.
/// </summary>
public sealed class MaterialDocument : Document
{
    public static MaterialPreviewSettings Preview { get; } = new();
    private static ulong s_reload;

    public EditorDocument Doc { get; }
    public string RelativePath { get; }
    public string FullPath => Path.Combine(Doc.ProjectRoot, RelativePath);
    public MaterialFile Material { get; }

    /// <summary>Raised when the material changed from outside a field (undo, reset, the file on disk).</summary>
    public event Action? Reloaded;

    /// <summary>Makes the view (MainWindow sets it; Dock calls it whenever the tab is shown).</summary>
    public Func<MaterialDocument, Control>? CreateView { get; set; }

    private readonly List<string> _undo = new(), _redo = new();
    private string? _lastField;
    private readonly Stopwatch _sinceEdit = Stopwatch.StartNew();
    private readonly DispatcherTimer _saveTimer = new() { Interval = TimeSpan.FromMilliseconds(150) };
    private DateTime _diskTime;

    public MaterialDocument(EditorDocument doc, string relativePath)
    {
        Doc = doc;
        RelativePath = relativePath;
        Id = "Material:" + relativePath;
        Title = Path.GetFileName(relativePath);
        CanFloat = true;
        CanClose = true;
        Material = new MaterialFile(MaterialFile.Read(FullPath) ?? new JsonObject(), LoadById);
        _diskTime = DiskTime();
        _saveTimer.Tick += (_, _) => Flush();
    }

    public bool CanUndo => _undo.Count > 0;
    public bool Saved => !_saveTimer.IsEnabled;

    /// <summary>A material's file by asset id (instances read their parents with it).</summary>
    private JsonObject? LoadById(string guid)
    {
        var asset = Doc.Assets.FirstOrDefault(a => a.Guid == guid);
        return asset == null ? null : MaterialFile.Read(Path.Combine(Doc.ProjectRoot, asset.Path));
    }

    public AssetInfo? Asset => Doc.Assets.FirstOrDefault(a => string.Equals(a.Path, RelativePath, StringComparison.OrdinalIgnoreCase));
    public AssetInfo? Parent => Material.ParentId is { } p ? Doc.Assets.FirstOrDefault(a => a.Guid == p) : null;

    /// <summary>Changes one field (or resets it with <paramref name="reset"/>) and saves soon.</summary>
    public void Edit(string field, JsonNode? value, bool reset = false)
    {
        // A slider drag is one undo step: edits of the same field close together merge.
        if (field != _lastField || _sinceEdit.ElapsedMilliseconds > 800)
        {
            _undo.Add(Material.Serialize());
            _redo.Clear();
        }
        _lastField = field;
        _sinceEdit.Restart();
        if (reset) Material.Reset(field);
        else Material.Set(field, value);
        _saveTimer.Stop();
        _saveTimer.Start();
    }

    public void Undo() => Step(_undo, _redo);
    public void Redo() => Step(_redo, _undo);

    private void Step(List<string> from, List<string> to)
    {
        if (from.Count == 0) return;
        to.Add(Material.Serialize());
        Material.Replace((JsonObject)JsonNode.Parse(from[^1])!);
        from.RemoveAt(from.Count - 1);
        _lastField = null;
        Flush();
        Reloaded?.Invoke();
    }

    /// <summary>Writes the file now (if there is anything waiting) and refreshes the preview.</summary>
    public void Flush()
    {
        _saveTimer.Stop();
        try
        {
            File.WriteAllText(FullPath, Material.Serialize());
            _diskTime = DiskTime();
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            EngineLog.Error("Materials", $"Couldn't save {RelativePath}: {e.Message}");
            return;
        }
        ShowPreview();
    }

    /// <summary>Points the preview at this material (the engine has one preview per document).</summary>
    public void ShowPreview()
    {
        var p = Preview;
        var json = new JsonObject
        {
            ["shape"] = p.Shape, ["material"] = RelativePath, ["hdri"] = p.Hdri,
            ["ev100"] = p.Ev100, ["rotation"] = p.Rotation, ["reload"] = ++s_reload,
        };
        if (Doc.Handle != IntPtr.Zero && Native.noor_doc_set_preview(Doc.Handle, json.ToJsonString()) == 0)
            EngineLog.Warn("Materials", $"Preview: {Native.LastError()}");
    }

    /// <summary>The file changed on disk and not by us (a text editor, Make material, git): take it.</summary>
    public void PollDisk()
    {
        var time = DiskTime();
        if (time == _diskTime || _saveTimer.IsEnabled) return;
        _diskTime = time;
        if (MaterialFile.Read(FullPath) is not { } root) return;
        if (root.ToJsonString() == Material.Root.ToJsonString()) return;
        _undo.Add(Material.Serialize());
        Material.Replace(root);
        Reloaded?.Invoke();
        ShowPreview();
    }

    private DateTime DiskTime() => File.Exists(FullPath) ? File.GetLastWriteTimeUtc(FullPath) : default;

    public override bool OnClose()
    {
        if (_saveTimer.IsEnabled) Flush();
        return true;
    }
}

/// <summary>
/// The material editor's view: a live preview (its own engine viewport on the document's preview
/// channel) and the material's fields. An instance shows its parents' values; a field it changes
/// is marked, and ↺ gives it back to the parent.
/// </summary>
public sealed class MaterialPanel : Grid
{
    public MaterialDocument Material { get; }
    public VulkanViewport Preview { get; }

    private readonly EditorDocument _doc;
    private readonly StackPanel _fields = new() { Spacing = 2, Margin = new Thickness(10, 4, 10, 12) };
    private readonly Dictionary<string, (TextBlock Label, Button Reset)> _rows = new();
    private readonly List<Action> _refreshers = new();
    private readonly TextBlock _heading = new() { FontSize = 13, FontWeight = FontWeight.SemiBold, VerticalAlignment = VerticalAlignment.Center };
    private readonly TextBlock _parentText = new() { FontSize = 11, Classes = { "muted" }, VerticalAlignment = VerticalAlignment.Center };
    private bool _updating;

    /// <summary>"Save as instance": MainWindow makes the file and opens it.</summary>
    public event Action<MaterialDocument>? SaveAsInstanceRequested;
    public event Action<string>? OpenRequested;

    public MaterialPanel(MaterialDocument material)
    {
        Material = material;
        _doc = material.Doc;
        Preview = new VulkanViewport(_doc, preview: true);
        ColumnDefinitions = new ColumnDefinitions("*,4,360");
        RowDefinitions = new RowDefinitions("Auto,*");

        // Heading: name, parent, actions.
        var instance = new Button { Content = "Save as instance", Classes = { "tool" } };
        ToolTip.SetTip(instance, "A new material that starts as this one and changes only what you change (an instance)");
        instance.Click += (_, _) => SaveAsInstanceRequested?.Invoke(Material);
        var openParent = new Button { Content = "Open parent", Classes = { "tool" } };
        openParent.Click += (_, _) => { if (Material.Parent is { } p) OpenRequested?.Invoke(p.Path); };
        openParent.IsVisible = Material.Material.IsInstance;
        var header = new StackPanel
        {
            Orientation = Orientation.Horizontal, Spacing = 8, Margin = new Thickness(8, 6),
            Children = { new TextBlock { Text = "●", Foreground = NoorTheme.Brush(NoorTheme.Cyan), VerticalAlignment = VerticalAlignment.Center },
                         _heading, _parentText, instance, openParent },
        };
        SetColumnSpan(header, 3);
        Children.Add(header);

        // Preview with its own little toolbar.
        var previewArea = new DockPanel { Background = NoorTheme.Brush(NoorTheme.Background) };
        var bar = PreviewBar();
        DockPanel.SetDock(bar, Avalonia.Controls.Dock.Top);
        previewArea.Children.Add(bar);
        previewArea.Children.Add(Preview);
        SetRow(previewArea, 1);
        Children.Add(previewArea);

        var splitter = new GridSplitter { Background = NoorTheme.Brush(NoorTheme.Border), ResizeDirection = GridResizeDirection.Columns };
        SetRow(splitter, 1);
        SetColumn(splitter, 1);
        Children.Add(splitter);

        var scroll = new ScrollViewer { Content = _fields, Background = NoorTheme.Brush(NoorTheme.Panel) };
        SetRow(scroll, 1);
        SetColumn(scroll, 2);
        Children.Add(scroll);

        BuildFields();
        UpdateHeading();
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        Material.Reloaded += OnReloaded;
        _doc.AssetsChanged += OnReloaded;  // texture lists and the parent's name
        Material.ShowPreview();            // this tab's material in the (shared) preview
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnDetachedFromVisualTree(e);
        Material.Reloaded -= OnReloaded;
        _doc.AssetsChanged -= OnReloaded;
        Preview.Detach();
    }

    private void OnReloaded()
    {
        _updating = true;
        foreach (var refresh in _refreshers) refresh();
        _updating = false;
        foreach (var field in _rows.Keys) UpdateRow(field);
        UpdateHeading();
    }

    private void UpdateHeading()
    {
        _heading.Text = Material.RelativePath;
        _parentText.Text = Material.Material.IsInstance
            ? $"instance of {Material.Parent?.Path ?? "a missing material"}"
            : "";
    }

    // ---- The preview's toolbar ----

    private Control PreviewBar()
    {
        var p = MaterialDocument.Preview;
        var shape = new ComboBox { ItemsSource = new[] { "Sphere", "Cube", "Plane" }, Width = 90, Classes = { "field" } };
        shape.SelectedItem = char.ToUpperInvariant(p.Shape[0]) + p.Shape[1..];
        shape.SelectionChanged += (_, _) => { p.Shape = (shape.SelectedItem as string ?? "Sphere").ToLowerInvariant(); Material.ShowPreview(); };

        const string procedural = "Sky (procedural)";
        var hdris = new List<string> { procedural };
        hdris.AddRange(_doc.Assets.Where(a => a.Path.EndsWith(".hdr", StringComparison.OrdinalIgnoreCase)).Select(a => a.Path));
        var env = new ComboBox { ItemsSource = hdris, Width = 190, Classes = { "field" } };
        env.SelectedItem = hdris.Contains(p.Hdri) ? p.Hdri : procedural;
        env.SelectionChanged += (_, _) =>
        {
            p.Hdri = env.SelectedItem is string s && s != procedural ? s : "";
            Material.ShowPreview();
        };
        ToolTip.SetTip(env, "What lights the preview: the default sky and sun, or an HDRI from the project");

        var ev = new Slider { Minimum = 4, Maximum = 17, Value = p.Ev100, Width = 110, VerticalAlignment = VerticalAlignment.Center };
        var evText = new TextBlock { Text = $"EV {p.Ev100:0.#}", Width = 46, FontSize = 11, VerticalAlignment = VerticalAlignment.Center };
        ev.ValueChanged += (_, _) =>
        {
            p.Ev100 = Math.Round(ev.Value * 2) / 2;
            evText.Text = $"EV {p.Ev100:0.#}";
            Material.ShowPreview();
        };
        ToolTip.SetTip(ev, "Exposure: 14 for sunlight, about 13 for most HDRIs, lower for dim scenes");
        var presets = new Button { Content = "▾", Classes = { "tool" }, Padding = new Thickness(5, 0) };
        ToolTip.SetTip(presets, "Exposure presets");
        var presetMenu = new MenuFlyout();
        foreach (var preset in ExposurePresets.All)
        {
            var item = new MenuItem { Header = ExposurePresets.Label(preset) };
            item.Click += (_, _) => ev.Value = Math.Clamp(preset.Ev100, ev.Minimum, ev.Maximum);
            presetMenu.Items.Add(item);
        }
        presets.Flyout = presetMenu;

        var turn = new Slider { Minimum = 0, Maximum = 360, Value = p.Rotation, Width = 90, VerticalAlignment = VerticalAlignment.Center };
        turn.ValueChanged += (_, _) => { p.Rotation = Math.Round(turn.Value); Material.ShowPreview(); };
        ToolTip.SetTip(turn, "Turns the environment around the object");

        return new StackPanel
        {
            Orientation = Orientation.Horizontal, Spacing = 6, Margin = new Thickness(6, 4),
            Children =
            {
                shape, env, evText, ev, presets,
                new TextBlock { Text = "Turn", FontSize = 11, Classes = { "muted" }, VerticalAlignment = VerticalAlignment.Center }, turn,
            },
        };
    }

    // ---- Fields ----

    private void BuildFields()
    {
        Section("Surface");
        ColorRow("base_color", "Base colour", 4);
        MapRow("base_color_map", "Base colour map");
        SliderRow("metallic", "Metallic", 0, 1);
        SliderRow("roughness", "Roughness", 0, 1);
        MapRow("metallic_roughness_map", "ORM map");
        MapRow("roughness_map", "Roughness map");
        MapRow("metallic_map", "Metallic map");

        Section("Detail");
        MapRow("normal_map", "Normal map");
        SliderRow("normal_scale", "Normal strength", 0, 2);
        MapRow("occlusion_map", "Occlusion map");
        SliderRow("occlusion_strength", "Occlusion", 0, 1);

        Section("Emission");
        ColorRow("emissive", "Emissive colour", 3);
        MapRow("emissive_map", "Emissive map");
        SliderRow("emissive_strength", "Strength", 0, 20);

        Section("Shape and UVs");
        AlphaRow();
        SliderRow("alpha_cutoff", "Alpha cutoff", 0, 1);
        DoubleSidedRow();
        VectorRow("uv_scale", "UV scale");
        VectorRow("uv_offset", "UV offset");

        _fields.Children.Add(new TextBlock
        {
            Text = "Changes are saved as you make them. Ctrl+Z undoes them.",
            FontSize = 11, Classes = { "muted" }, Margin = new Thickness(0, 12, 0, 0), TextWrapping = TextWrapping.Wrap,
        });
    }

    private void Section(string title) =>
        _fields.Children.Add(new TextBlock { Text = title, Classes = { "section" }, Margin = new Thickness(0, 10, 0, 4) });

    private void Row(string field, string label, Control editor)
    {
        var text = new TextBlock { Text = label, FontSize = 12, VerticalAlignment = VerticalAlignment.Center, TextTrimming = TextTrimming.CharacterEllipsis };
        var reset = new Button { Content = "↺", Classes = { "tool" }, Padding = new Thickness(5, 0), Margin = new Thickness(4, 0, 0, 0) };
        reset.Click += (_, _) =>
        {
            Material.Edit(field, null, reset: true);
            OnReloaded();
        };
        _rows[field] = (text, reset);
        var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("118,*,26"), MinHeight = 28 };
        SetColumn(editor, 1);
        SetColumn(reset, 2);
        grid.Children.Add(text);
        grid.Children.Add(editor);
        grid.Children.Add(reset);
        _fields.Children.Add(grid);
        UpdateRow(field);
    }

    /// <summary>Instances: fields they change are cyan with a ↺; the rest show the parent's values.</summary>
    private void UpdateRow(string field)
    {
        var (label, reset) = _rows[field];
        var mine = Material.Material.Sets(field);
        var instance = Material.Material.IsInstance;
        label.Foreground = instance && mine ? NoorTheme.Brush(NoorTheme.Cyan) : NoorTheme.Brush(NoorTheme.Text);
        label.FontWeight = instance && mine ? FontWeight.SemiBold : FontWeight.Normal;
        reset.IsVisible = mine;
        ToolTip.SetTip(reset, instance ? "Use the parent's value" : "Back to the default");
    }

    private void Changed(string field, JsonNode? value)
    {
        if (_updating) return;
        Material.Edit(field, value);
        UpdateRow(field);
    }

    private void SliderRow(string field, string label, double min, double max)
    {
        var slider = new Slider { Minimum = min, Maximum = max, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 0, 6, 0) };
        var box = new TextBox { Classes = { "field" }, Width = 56, VerticalAlignment = VerticalAlignment.Center };
        void Show()
        {
            var v = Material.Material.GetNumber(field);
            slider.Value = Math.Clamp(v, min, max);
            box.Text = Num.Format(v);
        }
        slider.ValueChanged += (_, _) =>
        {
            if (_updating) return;
            var v = Math.Round(slider.Value, 3);
            box.Text = Num.Format(v);
            Changed(field, v);
        };
        void Commit()
        {
            if (!Num.TryParse(box.Text, out var v)) { Show(); return; }
            if (Math.Abs(v - Material.Material.GetNumber(field)) < 1e-9) return;
            _updating = true;
            slider.Value = Math.Clamp(v, min, max);
            _updating = false;
            Changed(field, v);
        }
        box.KeyDown += (_, e) => { if (e.Key == Key.Enter) { Commit(); e.Handled = true; } };
        box.LostFocus += (_, _) => Commit();
        var editor = new DockPanel { Children = { box, slider } };
        DockPanel.SetDock(box, Avalonia.Controls.Dock.Right);
        _updating = true;
        Show();
        _updating = false;
        _refreshers.Add(Show);
        Row(field, label, editor);
    }

    private void ColorRow(string field, string label, int components)
    {
        var swatch = new Border { Width = 22, Height = 18, CornerRadius = new CornerRadius(3), BorderBrush = NoorTheme.Brush(NoorTheme.Border), BorderThickness = new Thickness(1), Margin = new Thickness(0, 0, 6, 0) };
        var box = new TextBox { Classes = { "field" }, VerticalAlignment = VerticalAlignment.Center };
        ToolTip.SetTip(box, "sRGB hex, like a paint program (saved as linear numbers)");
        void Show()
        {
            var c = Material.Material.GetVector(field, components);
            box.Text = MaterialFile.ToHex(c);
            swatch.Background = new SolidColorBrush(Color.Parse(box.Text));
        }
        void Commit()
        {
            if (MaterialFile.FromHex(box.Text) is not { } rgb) { Show(); return; }
            var current = Material.Material.GetVector(field, components);
            if (MaterialFile.ToHex(current) == MaterialFile.ToHex(rgb)) { Show(); return; }
            var arr = new JsonArray(Math.Round(rgb[0], 4), Math.Round(rgb[1], 4), Math.Round(rgb[2], 4));
            if (components == 4) arr.Add(current[3]);
            Changed(field, arr);
            Show();
        }
        box.KeyDown += (_, e) => { if (e.Key == Key.Enter) { Commit(); e.Handled = true; } };
        box.LostFocus += (_, _) => Commit();
        Show();
        _refreshers.Add(Show);
        var editor = new DockPanel { Children = { swatch, box } };
        DockPanel.SetDock(swatch, Avalonia.Controls.Dock.Left);
        Row(field, label, editor);
    }

    private const string NoMap = "(none)";

    private void MapRow(string field, string label)
    {
        var combo = new ComboBox { Classes = { "field" }, HorizontalAlignment = HorizontalAlignment.Stretch };
        void Show()
        {
            var textures = _doc.Assets.Where(a => a.Importer == "texture" && !a.Path.EndsWith(".hdr", StringComparison.OrdinalIgnoreCase)).ToList();
            var items = new List<string> { NoMap };
            items.AddRange(textures.Select(t => t.Path));
            var id = Material.Material.GetMap(field);
            var selected = id == null ? NoMap : textures.FirstOrDefault(t => t.Guid == id)?.Path ?? $"(missing {id[..Math.Min(8, id.Length)]}…)";
            if (!items.Contains(selected)) items.Add(selected);
            combo.ItemsSource = items;
            combo.SelectedItem = selected;
            ToolTip.SetTip(combo, selected);
        }
        combo.SelectionChanged += (_, _) =>
        {
            if (_updating || combo.SelectedItem is not string path || path.StartsWith("(missing", StringComparison.Ordinal)) return;
            var guid = path == NoMap ? null : _doc.Assets.FirstOrDefault(a => a.Path == path)?.Guid;
            if (guid == Material.Material.GetMap(field)) return;
            Changed(field, guid == null ? null : JsonValue.Create(guid));
            ToolTip.SetTip(combo, path);
        };
        _updating = true;
        Show();
        _updating = false;
        _refreshers.Add(Show);
        Row(field, label, combo);
    }

    private void AlphaRow()
    {
        var combo = new ComboBox { ItemsSource = new[] { "opaque", "cutout", "blend" }, Classes = { "field" }, HorizontalAlignment = HorizontalAlignment.Stretch };
        ToolTip.SetTip(combo, "Cutout: leaves, fences (pixels below the cutoff vanish). Blend: see-through, like glass (base colour alpha says how much).");
        void Show() => combo.SelectedItem = Material.Material.Get("alpha") is JsonValue v && v.TryGetValue<string>(out var s) ? s : "opaque";
        combo.SelectionChanged += (_, _) => { if (combo.SelectedItem is string s) Changed("alpha", s); };
        _updating = true;
        Show();
        _updating = false;
        _refreshers.Add(Show);
        Row("alpha", "Alpha", combo);
    }

    private void DoubleSidedRow()
    {
        var check = new CheckBox { Content = "Both sides visible" };
        void Show() => check.IsChecked = Material.Material.Get("double_sided") is JsonValue v && v.TryGetValue<bool>(out var b) && b;
        check.IsCheckedChanged += (_, _) => Changed("double_sided", check.IsChecked == true);
        _updating = true;
        Show();
        _updating = false;
        _refreshers.Add(Show);
        Row("double_sided", "Double sided", check);
    }

    private void VectorRow(string field, string label)
    {
        var boxes = new[] { new TextBox { Classes = { "field" }, VerticalAlignment = VerticalAlignment.Center }, new TextBox { Classes = { "field" }, VerticalAlignment = VerticalAlignment.Center } };
        void Show()
        {
            var v = Material.Material.GetVector(field, 2);
            for (var i = 0; i < 2; ++i) boxes[i].Text = Num.Format(v[i]);
        }
        void Commit()
        {
            var v = Material.Material.GetVector(field, 2);
            var next = new double[2];
            for (var i = 0; i < 2; ++i)
                if (!Num.TryParse(boxes[i].Text, out next[i])) { Show(); return; }
            if (next[0] == v[0] && next[1] == v[1]) return;
            Changed(field, new JsonArray(next[0], next[1]));
        }
        foreach (var box in boxes)
        {
            box.KeyDown += (_, e) => { if (e.Key == Key.Enter) { Commit(); e.Handled = true; } };
            box.LostFocus += (_, _) => Commit();
        }
        Show();
        _refreshers.Add(Show);
        var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("*,*") };
        SetColumn(boxes[1], 1);
        boxes[0].Margin = new Thickness(0, 0, 3, 0);
        grid.Children.Add(boxes[0]);
        grid.Children.Add(boxes[1]);
        Row(field, label, grid);
    }

    /// <summary>For tests: the row's editor shows this field as changed by this file.</summary>
    public bool ShowsOverride(string field) => _rows.TryGetValue(field, out var r) && r.Reset.IsVisible;
}
