using System.Globalization;
using System.Text.Json.Nodes;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Shapes;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using NoorEditor.Model;
using NoorEditor.Shell;

namespace NoorEditor.Panels;

/// <summary>
/// The Effects panel (Milestone 8B, part 4, ADR-035). Left: the six built-in effect presets and the project's own
/// effects (Assets/Effects/*.noorfx). Add to scene puts one on a new entity (a preset is copied into the project first,
/// once); Use on selected puts it on the selected entity; Duplicate and Reset as in the Environments gallery. Right: the
/// selected project effect as a stack of modules (spawn, shape, life and speed, forces, over life, render), saved as
/// you change them, so the viewport's preview follows at once. The preview bar solos, mutes, restarts or pauses the
/// selected entity's effect while editing, and rolls a new seed. The built-in presets are never written.
/// </summary>
public sealed class EffectsPanel : DockPanel
{
    private readonly EditorDocument _doc;
    private readonly WrapPanel _presets = new();
    private readonly WrapPanel _project = new();
    private readonly StackPanel _editor = new() { Spacing = 6, Margin = new Thickness(10, 8), MinWidth = 340 };
    private readonly TextBlock _status = new() { FontSize = 11, Classes = { "muted" }, TextWrapping = TextWrapping.Wrap };
    private readonly Button _add = new() { Content = "Add to scene", Classes = { "tool" } };
    private readonly Button _use = new() { Content = "Use on selected", Classes = { "tool" } };
    private readonly Button _duplicate = new() { Content = "Duplicate", Classes = { "tool" } };
    private readonly Button _reset = new() { Content = "Reset", Classes = { "tool" } };
    private JsonObject? _settings;  // the selected project effect's emitter settings, as last read or saved

    public string? SelectedPreset { get; private set; }
    public string? SelectedAsset { get; private set; }
    /// <summary>The project's effects as last listed (tests read them).</summary>
    public JsonArray ProjectEffects { get; private set; } = [];
    /// <summary>The preview's state as the document last reported it (tests read it).</summary>
    public JsonObject PreviewState { get; private set; } = new();

    public EffectsPanel(EditorDocument doc)
    {
        _doc = doc;
        ToolTip.SetTip(_add, "A new entity with this effect at the origin (one undo step). A preset is copied into Assets/Effects first.");
        ToolTip.SetTip(_use, "Put this effect on the selected entity (one undo step)");
        ToolTip.SetTip(_duplicate, "A copy in the project, to change without touching this one");
        ToolTip.SetTip(_reset, "Put this copy back to the built-in preset it came from");
        _add.Click += (_, _) => AddToScene();
        _use.Click += (_, _) => UseOnSelected();
        _duplicate.Click += (_, _) => Duplicate();
        _reset.Click += (_, _) => Reset();

        var gallery = new StackPanel
        {
            Spacing = 6, Margin = new Thickness(10, 8),
            Children =
            {
                new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6, Children = { _add, _use, _duplicate, _reset } },
                _status,
                new TextBlock { Text = "Presets (built in)", Classes = { "section" } },
                _presets,
                new TextBlock { Text = "In this project", Classes = { "section" } },
                _project,
            },
        };
        var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto") };
        var editorScroll = new ScrollViewer { Content = _editor };
        Grid.SetColumn(editorScroll, 1);
        grid.Children.Add(new ScrollViewer { Content = gallery });
        grid.Children.Add(editorScroll);
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
        foreach (var preset in EditorDocument.EffectPresets().OfType<JsonObject>())
        {
            var name = preset["name"]!.GetValue<string>();
            _presets.Children.Add(Card(name, preset["description"]?.GetValue<string>() ?? "", Swatch(preset["emitter"] as JsonObject), false,
                                       SelectedPreset == name, () => Select(name, null)));
        }
        _project.Children.Clear();
        ProjectEffects = _doc.Effect(new JsonObject { ["op"] = "list" }) as JsonArray ?? [];
        foreach (var fx in ProjectEffects.OfType<JsonObject>())
        {
            var path = fx["path"]!.GetValue<string>();
            var preset = fx["preset"]?.GetValue<string>() ?? "";
            var edited = preset.Length == 0 || !SameAsPreset(fx, preset);
            _project.Children.Add(Card(fx["name"]?.GetValue<string>() ?? path, preset.Length > 0 ? $"From {preset}" : "Custom",
                                       Swatch(fx["emitter"] as JsonObject), edited, SelectedAsset == path, () => Select(null, path)));
        }
        if (ProjectEffects.Count == 0)
            _project.Children.Add(new TextBlock { Text = "None yet: add a preset to the scene, or duplicate one.", FontSize = 11, Classes = { "muted" } });
        UpdateButtons();
    }

    private static bool SameAsPreset(JsonObject fx, string preset) =>
        EditorDocument.EffectPresets().OfType<JsonObject>().FirstOrDefault(p => p["name"]?.GetValue<string>() == preset) is { } p &&
        JsonNode.DeepEquals(p["emitter"], fx["emitter"]);

    /// <summary>A strip of the effect's colours over its life (its first and last gradient keys).</summary>
    private static IBrush Swatch(JsonObject? emitter)
    {
        var stops = new GradientStops();
        var color = emitter?["color"];
        if (color is JsonArray keys && keys.Count > 0)
        {
            foreach (var key in keys.OfType<JsonArray>())
                if (key.Count == 2 && key[1] is JsonValue v && v.TryGetValue<string>(out var hex) && Color.TryParse(hex, out var c))
                    stops.Add(new GradientStop(c, Math.Clamp(key[0]?.GetValue<double>() ?? 0, 0, 1)));
        }
        else if (color is JsonValue one && one.TryGetValue<string>(out var hex) && Color.TryParse(hex, out var c))
        {
            stops.Add(new GradientStop(c, 0));
            stops.Add(new GradientStop(c, 1));
        }
        if (stops.Count == 0) stops.Add(new GradientStop(Colors.White, 0));
        return new LinearGradientBrush
        {
            StartPoint = new RelativePoint(0, 0, RelativeUnit.Relative), EndPoint = new RelativePoint(1, 0, RelativeUnit.Relative), GradientStops = stops,
        };
    }

    private Control Card(string title, string subtitle, IBrush swatch, bool edited, bool selected, Action pick)
    {
        var stack = new StackPanel
        {
            Spacing = 2,
            Children = { new Border { Height = 14, Background = swatch, CornerRadius = new CornerRadius(3) }, new TextBlock { Text = title, FontSize = 12, FontWeight = FontWeight.SemiBold } },
        };
        if (edited) stack.Children.Add(new TextBlock { Text = "edited", FontSize = 10, Foreground = NoorTheme.Brush(NoorTheme.Warn) });
        stack.Children.Add(new TextBlock { Text = subtitle, FontSize = 10, Classes = { "muted" }, TextWrapping = TextWrapping.Wrap, MaxWidth = 150 });
        var card = new Border
        {
            Child = stack, Padding = new Thickness(6), Margin = new Thickness(0, 0, 8, 8), CornerRadius = new CornerRadius(6), Width = 164,
            BorderThickness = new Thickness(2), Cursor = new Cursor(StandardCursorType.Hand),
            BorderBrush = NoorTheme.Brush(selected ? NoorTheme.Accent : NoorTheme.Border), Background = NoorTheme.Brush(NoorTheme.PanelRaised),
        };
        card.PointerPressed += (_, e) =>
        {
            pick();
            if (e.ClickCount == 2) AddToScene();  // double-click adds
        };
        return card;
    }

    public void Select(string? preset, string? asset)
    {
        SelectedPreset = preset;
        SelectedAsset = asset;
        _settings = null;
        Rebuild();
        ShowEditor();
    }

    private void UpdateButtons()
    {
        var any = SelectedPreset != null || SelectedAsset != null;
        _add.IsEnabled = any && _doc.Scene.Open;
        _use.IsEnabled = any && _doc.Scene.Open && _doc.SelectedId != null;
        _duplicate.IsEnabled = SelectedAsset != null;
        var selected = ProjectEffects.OfType<JsonObject>().FirstOrDefault(e => e["path"]?.GetValue<string>() == SelectedAsset);
        _reset.IsEnabled = selected != null && (selected["preset"]?.GetValue<string>() ?? "").Length > 0;
        _status.Text = SelectedPreset != null ? "A built-in preset: Add to scene copies it into the project (once) and places it."
                     : SelectedAsset != null ? $"{SelectedAsset}: edits are saved as you go, and the viewport shows them at once."
                     : "Pick an effect.";
    }

    // ---- the actions (public: the smoke test uses them as the buttons do) ----

    public JsonNode? AddToScene() => ApplyTo(null);
    public JsonNode? UseOnSelected() => _doc.SelectedId is { } id ? ApplyTo(id) : null;

    private JsonNode? ApplyTo(string? entity)
    {
        if (!_doc.Scene.Open) return null;
        var command = new JsonObject { ["op"] = "apply" };
        if (SelectedPreset != null) command["preset"] = SelectedPreset;
        else if (SelectedAsset != null) command["asset"] = SelectedAsset;
        else return null;
        if (entity != null) command["entity"] = entity;
        var result = _doc.Effect(command);
        if (result == null) return null;
        Interop.EngineLog.Info("Effects", $"Added {result["name"]} ({result["path"]})");
        if (result["entity"]?.GetValue<string>() is { } placed) _doc.Select(placed);
        if (result["path"]?.GetValue<string>() is { } path) Select(null, path);  // edit the project copy from here on
        return result;
    }

    public JsonNode? Duplicate()
    {
        if (SelectedAsset == null) return null;
        var result = _doc.Effect(new JsonObject { ["op"] = "duplicate", ["asset"] = SelectedAsset });
        if (result?["path"]?.GetValue<string>() is { } path) Select(null, path);
        return result;
    }

    public JsonNode? Reset()
    {
        if (SelectedAsset == null) return null;
        var result = _doc.Effect(new JsonObject { ["op"] = "reset", ["asset"] = SelectedAsset });
        _settings = null;
        ShowEditor();
        return result;
    }

    /// <summary>The preview's controls for the selected entity: "solo", "mute", "restart", "pause", "play".</summary>
    public JsonObject? Preview(string what)
    {
        var id = _doc.SelectedId;
        var command = new JsonObject { ["op"] = "preview" };
        var muted = PreviewState["muted"] as JsonArray ?? [];
        switch (what)
        {
            case "solo":
                command["solo"] = PreviewState["solo"]?.GetValue<string>() == id ? null : id;
                break;
            case "mute" when id != null:
                var list = muted.Select(m => m?.GetValue<string>()).Where(m => m != null).ToList();
                if (!list.Remove(id)) list.Add(id);
                command["muted"] = new JsonArray(list.Select(m => (JsonNode?)JsonValue.Create(m)).ToArray());
                break;
            case "restart":
                command["restart"] = true;
                break;
            case "pause":
                command["paused"] = true;
                break;
            case "play":
                command["paused"] = false;
                break;
        }
        if (_doc.Effect(command) is JsonObject state) PreviewState = state;
        return PreviewState;
    }

    // ---- the module stack ----

    private static readonly string[] Shapes = ["point", "sphere", "box", "cone"];
    private static readonly string[] Blends = ["alpha", "additive", "premultiplied"];
    private static readonly string[] Facings = ["camera", "velocity"];
    private static readonly string[] Spaces = ["world", "local"];
    private static readonly string[] FrameModes = ["life", "random"];

    private void ShowEditor()
    {
        _editor.Children.Clear();
        if (SelectedAsset == null)
        {
            if (SelectedPreset != null)
                _editor.Children.Add(new TextBlock { Text = "Add the preset to the scene (or duplicate a project effect) to edit it: the built-in presets never change.",
                                                     FontSize = 11, Classes = { "muted" }, TextWrapping = TextWrapping.Wrap, MaxWidth = 320 });
            return;
        }
        if (_settings == null && _doc.Effect(new JsonObject { ["op"] = "read", ["asset"] = SelectedAsset }) is JsonObject fx)
            _settings = fx["emitter"] as JsonObject;
        if (_settings == null) return;
        _editor.Children.Add(new TextBlock { Text = System.IO.Path.GetFileNameWithoutExtension(SelectedAsset), Classes = { "section" } });
        _editor.Children.Add(PreviewBar());
        Module("Spawn", [
            Number("rate", "Rate (per second)"), Number("duration", "Duration (s, 0 = forever)"), Bool("loop", "Loop"),
            Number("max_particles", "At most"), Seed(), Json("bursts", "Bursts (JSON)"),
        ]);
        Module("Shape", [
            Choice("shape.type", "Shape", Shapes, "point"), Number("shape.radius", "Radius (m)"), Vector("shape.size", "Box size (m)"),
            Number("shape.angle", "Cone angle (°)"), Bool("shape.surface", "From the surface"),
        ]);
        Module("Life and speed", [Range("lifetime", "Lifetime (s)"), Range("speed", "Speed (m/s)")]);
        Module("Forces", [
            Number("gravity", "Gravity (× 9.81)"), Number("drag", "Drag (/s)"), Number("wind", "Wind"), Choice("space", "Space", Spaces, "world"),
        ]);
        Module("Over life", [Curve("size", "Size (m)"), Gradient("color", "Colour"), Curve("alpha", "Alpha")]);
        // Part 7: collisions (the terrain and planes) and sub-emitters (on hits and deaths).
        Module("Collisions", [
            Collide(), BoolOr("collide.ground", "The ground (terrains)", true), NumberOr("collide.bounce", "Bounce (0..1)", 0.3),
            NumberOr("collide.friction", "Friction (0..1)", 0.2), NumberOr("collide.radius", "Radius (m)", 0),
            BoolOr("collide.kill", "Die on hit", false), NumberOr("collide.hit_speed", "Hit speed (m/s)", 0.5),
            Json("collide.planes", "Planes (JSON: {\"y\": 0})"),
        ]);
        Module("Sub-emitters", [Json("sub_emitters", "On hit / death (JSON)")]);
        Module("Render", [
            Choice("render.blend", "Blend", Blends, "alpha"), Choice("render.facing", "Facing", Facings, "camera"), Number("render.stretch", "Stretch (s)"),
            Bool("render.lit", "Lit"), Number("render.emission", "Emission (nits)"), Text("render.texture", "Texture (asset id)"),
            Vector("render.frames", "Flipbook (columns, rows)", 2), Choice("render.frame", "Frame", FrameModes, "life"),
            NumberOr("render.max_distance", "Hide beyond (m, 0 = never)", 0),
        ]);
    }

    private Control PreviewBar()
    {
        Button B(string label, string what, string tip)
        {
            var b = new Button { Content = label, Classes = { "tool" } };
            ToolTip.SetTip(b, tip);
            b.Click += (_, _) => Preview(what);
            return b;
        }
        return new StackPanel
        {
            Orientation = Orientation.Horizontal, Spacing = 4,
            Children =
            {
                new TextBlock { Text = "Preview:", FontSize = 11, Classes = { "muted" }, VerticalAlignment = VerticalAlignment.Center },
                B("Solo", "solo", "Show only the selected entity's effect in the viewport (again: show all)"),
                B("Mute", "mute", "Hide the selected entity's effect in the viewport (again: show it)"),
                B("Restart", "restart", "Start every previewed effect again from nothing"),
                B("Pause", "pause", "Freeze the preview"),
                B("Play", "play", "Run the preview"),
            },
        };
    }

    private void Module(string title, Control[] rows)
    {
        var body = new StackPanel { Spacing = 3 };
        foreach (var r in rows) body.Children.Add(r);
        _editor.Children.Add(new Border
        {
            Background = NoorTheme.Brush(NoorTheme.PanelRaised), CornerRadius = new CornerRadius(6), Padding = new Thickness(8, 6),
            Child = new StackPanel { Spacing = 4, Children = { new TextBlock { Text = title, FontSize = 12, FontWeight = FontWeight.SemiBold }, body } },
        });
    }

    // Fields by path ("shape.radius"): read from and written into the settings, then saved.
    private JsonNode? Get(string path)
    {
        JsonNode? node = _settings;
        foreach (var part in path.Split('.')) node = (node as JsonObject)?[part];
        return node;
    }

    /// <summary>Changes one setting (by path, e.g. "shape.radius") of the selected project effect and saves it.</summary>
    public bool SetField(string path, JsonNode? value)
    {
        if (SelectedAsset == null) return false;
        if (_settings == null && _doc.Effect(new JsonObject { ["op"] = "read", ["asset"] = SelectedAsset }) is JsonObject fx) _settings = fx["emitter"] as JsonObject;
        if (_settings == null) return false;
        var next = (JsonObject)_settings.DeepClone();
        var parts = path.Split('.');
        var target = next;
        for (var i = 0; i < parts.Length - 1; ++i)
        {
            if (target[parts[i]] is not JsonObject child)
            {
                child = new JsonObject();
                target[parts[i]] = child;
            }
            target = child;
        }
        if (value == null) target.Remove(parts[^1]);
        else target[parts[^1]] = value.DeepClone();
        var saved = _doc.Effect(new JsonObject { ["op"] = "save", ["asset"] = SelectedAsset, ["emitter"] = next });
        if (saved?["emitter"] is not JsonObject emitter) return false;
        _settings = emitter;
        return true;
    }

    private static Grid Row(string label, Control editor)
    {
        var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("150,*") };
        var text = new TextBlock { Text = label, FontSize = 12, Classes = { "muted" }, VerticalAlignment = VerticalAlignment.Center };
        Grid.SetColumn(editor, 1);
        grid.Children.Add(text);
        grid.Children.Add(editor);
        return grid;
    }

    private static TextBox NumberBox(double value, Action<double> set)
    {
        var box = new TextBox { Text = value.ToString(CultureInfo.InvariantCulture), Classes = { "field" }, MinWidth = 50 };
        void Commit()
        {
            if (double.TryParse(box.Text, NumberStyles.Float, CultureInfo.InvariantCulture, out var x)) { if (x != value) set(x); }
            else box.Text = value.ToString(CultureInfo.InvariantCulture);
        }
        box.LostFocus += (_, _) => Commit();
        box.KeyDown += (_, e) => { if (e.Key == Key.Enter) Commit(); };
        return box;
    }

    private static double NumberOf(JsonNode? n, double fallback) => n is JsonValue v && v.TryGetValue<double>(out var d) ? d : fallback;

    private Control Number(string path, string label) => Row(label, NumberBox(NumberOf(Get(path), 0), x => SetField(path, JsonValue.Create(x))));

    // A number whose default isn't 0: shows the value the engine uses when it's missing.
    private Control NumberOr(string path, string label, double fallback) =>
        Row(label, NumberBox(NumberOf(Get(path), fallback), x => SetField(path, JsonValue.Create(x))));

    private Control BoolOr(string path, string label, bool fallback)
    {
        var check = new CheckBox { IsChecked = Get(path) is JsonValue v && v.TryGetValue<bool>(out var b) ? b : fallback };
        check.IsCheckedChanged += (_, _) => SetField(path, JsonValue.Create(check.IsChecked == true));
        return Row(label, check);
    }

    // Collisions on or off: the "collide" module is there (with its settings) or not. Setting any of its fields turns it on.
    private Control Collide()
    {
        var check = new CheckBox { IsChecked = Get("collide") is JsonObject };
        ToolTip.SetTip(check, "Particles stop on the terrain and the planes below, bounce and slide (world-space effects)");
        check.IsCheckedChanged += (_, _) => SetField("collide", check.IsChecked == true ? (Get("collide") as JsonObject)?.DeepClone() ?? new JsonObject() : null);
        return Row("Collide", check);
    }

    private Control Bool(string path, string label)
    {
        var check = new CheckBox { IsChecked = Get(path) is JsonValue v && v.TryGetValue<bool>(out var b) && b };
        check.IsCheckedChanged += (_, _) => SetField(path, JsonValue.Create(check.IsChecked == true));
        return Row(label, check);
    }

    private Control Choice(string path, string label, string[] options, string fallback)
    {
        var current = Get(path) is JsonValue v && v.TryGetValue<string>(out var s) ? s : fallback;
        var combo = new ComboBox { ItemsSource = options, SelectedItem = options.Contains(current) ? current : fallback, Classes = { "field" },
                                   HorizontalAlignment = HorizontalAlignment.Stretch };
        combo.SelectionChanged += (_, _) => { if (combo.SelectedItem is string pick && pick != current) SetField(path, JsonValue.Create(pick)); };
        return Row(label, combo);
    }

    private Control Text(string path, string label)
    {
        var current = Get(path) is JsonValue v && v.TryGetValue<string>(out var s) ? s : "";
        var box = new TextBox { Text = current, Classes = { "field" } };
        void Commit()
        {
            if (box.Text == current) return;
            SetField(path, string.IsNullOrWhiteSpace(box.Text) ? null : JsonValue.Create(box.Text.Trim()));
        }
        box.LostFocus += (_, _) => Commit();
        box.KeyDown += (_, e) => { if (e.Key == Key.Enter) Commit(); };
        return Row(label, box);
    }

    private Control Json(string path, string label)
    {
        var current = Get(path)?.ToJsonString() ?? "[]";
        var box = new TextBox { Text = current, Classes = { "field" }, TextWrapping = TextWrapping.Wrap };
        void Commit()
        {
            if (box.Text == current) return;
            try { SetField(path, JsonNode.Parse(box.Text ?? "[]")); }
            catch (System.Text.Json.JsonException) { box.Text = current; }
        }
        box.LostFocus += (_, _) => Commit();
        return Row(label, box);
    }

    // A number or [min, max]: two boxes; equal values write a plain number.
    private Control Range(string path, string label)
    {
        var node = Get(path);
        double lo = node is JsonArray a && a.Count == 2 ? NumberOf(a[0], 1) : NumberOf(node, 1), hi = node is JsonArray b && b.Count == 2 ? NumberOf(b[1], 1) : lo;
        void Write(double l, double h) => SetField(path, l == h ? JsonValue.Create(l) : new JsonArray(Math.Min(l, h), Math.Max(l, h)));
        var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 4,
                                   Children = { NumberBox(lo, x => Write(x, hi)), new TextBlock { Text = "to", VerticalAlignment = VerticalAlignment.Center, Classes = { "muted" } },
                                                NumberBox(hi, x => Write(lo, x)) } };
        return Row(label, row);
    }

    private Control Vector(string path, string label, int count = 3)
    {
        var node = Get(path) as JsonArray;
        var values = Enumerable.Range(0, count).Select(i => node != null && node.Count == count ? NumberOf(node[i], 1) : 1.0).ToArray();
        var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 4 };
        for (var i = 0; i < count; ++i)
        {
            var index = i;
            row.Children.Add(NumberBox(values[i], x =>
            {
                var next = values.ToArray();
                next[index] = x;
                SetField(path, new JsonArray(next.Select(v => (JsonNode?)JsonValue.Create(v)).ToArray()));
            }));
        }
        return Row(label, row);
    }

    private Control Seed()
    {
        var seed = NumberOf(Get("seed"), 1);
        var roll = new Button { Content = "🎲", Classes = { "tool" }, Padding = new Thickness(6, 0) };
        ToolTip.SetTip(roll, "A new seed: the same effect, different particles");
        roll.Click += (_, _) =>
        {
            SetField("seed", JsonValue.Create(Random.Shared.Next(1, 100000)));
            ShowEditor();
        };
        var row = new DockPanel { Children = { roll, NumberBox(seed, x => SetField("seed", JsonValue.Create(Math.Max(0, Math.Round(x))))) } };
        DockPanel.SetDock(roll, Avalonia.Controls.Dock.Right);
        return Row("Seed", row);
    }

    // A curve over life: [[t, value], ...] (or one number): keys to edit, and a small plot of it.
    private Control Curve(string path, string label)
    {
        var node = Get(path);
        var keys = node is JsonArray arr
            ? arr.OfType<JsonArray>().Where(k => k.Count == 2).Select(k => (t: NumberOf(k[0], 0), v: NumberOf(k[1], 0))).ToList()
            : [(0.0, NumberOf(node, 0))];
        void Write(List<(double t, double v)> next)
        {
            next.Sort((a, b) => a.t.CompareTo(b.t));
            SetField(path, next.Count == 1 ? JsonValue.Create(next[0].v)
                                           : new JsonArray(next.Select(k => (JsonNode?)new JsonArray(k.t, k.v)).ToArray()));
            ShowEditor();
        }
        var list = new StackPanel { Spacing = 2 };
        list.Children.Add(Plot(keys));
        for (var i = 0; i < keys.Count; ++i)
        {
            var index = i;
            var remove = new Button { Content = "✕", Classes = { "tool" }, Padding = new Thickness(4, 0), FontSize = 10, IsEnabled = keys.Count > 1 };
            remove.Click += (_, _) => Write(keys.Where((_, j) => j != index).ToList());
            list.Children.Add(new StackPanel
            {
                Orientation = Orientation.Horizontal, Spacing = 4,
                Children =
                {
                    new TextBlock { Text = "at", VerticalAlignment = VerticalAlignment.Center, Classes = { "muted" } },
                    NumberBox(keys[i].t, x => { var n = keys.ToList(); n[index] = (Math.Clamp(x, 0, 1), n[index].v); Write(n); }),
                    NumberBox(keys[i].v, x => { var n = keys.ToList(); n[index] = (n[index].t, x); Write(n); }),
                    remove,
                },
            });
        }
        var add = new Button { Content = "+ Key", Classes = { "tool" }, HorizontalAlignment = HorizontalAlignment.Left };
        add.Click += (_, _) => { var n = keys.ToList(); n.Add((1.0, keys[^1].v)); Write(n); };
        list.Children.Add(add);
        return Row(label, list);
    }

    private static Control Plot(List<(double t, double v)> keys)
    {
        const double w = 170, h = 34;
        var lo = Math.Min(0, keys.Min(k => k.v)); var hi = Math.Max(lo + 1e-6, keys.Max(k => k.v));
        var points = new List<Point>();
        foreach (var (t, v) in keys.OrderBy(k => k.t)) points.Add(new Point(Math.Clamp(t, 0, 1) * w, h - (v - lo) / (hi - lo) * h));
        if (points.Count == 1) points.Add(new Point(w, points[0].Y));
        points.Insert(0, new Point(0, points[0].Y));
        points.Add(new Point(w, points[^1].Y));
        return new Border
        {
            Width = w + 2, Height = h + 2, HorizontalAlignment = HorizontalAlignment.Left, BorderThickness = new Thickness(1),
            BorderBrush = NoorTheme.Brush(NoorTheme.Border),
            Child = new Canvas { Width = w, Height = h, Children = { new Polyline { Points = points, Stroke = NoorTheme.Brush(NoorTheme.Accent), StrokeThickness = 1.5 } } },
        };
    }

    // A colour over life: [[t, "#RRGGBB"], ...] (or one colour): keys with a swatch each.
    private Control Gradient(string path, string label)
    {
        var node = Get(path);
        var keys = node is JsonArray arr
            ? arr.OfType<JsonArray>().Where(k => k.Count == 2).Select(k => (t: NumberOf(k[0], 0), c: k[1]?.GetValue<string>() ?? "#FFFFFF")).ToList()
            : [(0.0, node is JsonValue v && v.TryGetValue<string>(out var s) ? s : "#FFFFFF")];
        void Write(List<(double t, string c)> next)
        {
            next.Sort((a, b) => a.t.CompareTo(b.t));
            SetField(path, next.Count == 1 ? JsonValue.Create(next[0].c)
                                           : new JsonArray(next.Select(k => (JsonNode?)new JsonArray(JsonValue.Create(k.t), JsonValue.Create(k.c))).ToArray()));
            ShowEditor();
        }
        var list = new StackPanel { Spacing = 2 };
        list.Children.Add(new Border { Height = 12, Width = 172, HorizontalAlignment = HorizontalAlignment.Left, CornerRadius = new CornerRadius(3),
                                       Background = Swatch(new JsonObject { ["color"] = Get(path)?.DeepClone() }) });
        for (var i = 0; i < keys.Count; ++i)
        {
            var index = i;
            var hex = new TextBox { Text = keys[i].c, Classes = { "field" }, Width = 80 };
            void CommitHex()
            {
                if (hex.Text is { } t && t != keys[index].c && Color.TryParse(t, out _)) { var n = keys.ToList(); n[index] = (n[index].t, t.ToUpperInvariant()); Write(n); }
            }
            hex.LostFocus += (_, _) => CommitHex();
            hex.KeyDown += (_, e) => { if (e.Key == Key.Enter) CommitHex(); };
            var remove = new Button { Content = "✕", Classes = { "tool" }, Padding = new Thickness(4, 0), FontSize = 10, IsEnabled = keys.Count > 1 };
            remove.Click += (_, _) => Write(keys.Where((_, j) => j != index).ToList());
            var swatch = new Border { Width = 14, Height = 14, CornerRadius = new CornerRadius(3),
                                      Background = Color.TryParse(keys[i].c, out var col) ? new SolidColorBrush(col) : Brushes.White };
            list.Children.Add(new StackPanel
            {
                Orientation = Orientation.Horizontal, Spacing = 4,
                Children =
                {
                    new TextBlock { Text = "at", VerticalAlignment = VerticalAlignment.Center, Classes = { "muted" } },
                    NumberBox(keys[i].t, x => { var n = keys.ToList(); n[index] = (Math.Clamp(x, 0, 1), n[index].c); Write(n); }),
                    swatch, hex, remove,
                },
            });
        }
        var add = new Button { Content = "+ Key", Classes = { "tool" }, HorizontalAlignment = HorizontalAlignment.Left };
        add.Click += (_, _) => { var n = keys.ToList(); n.Add((1.0, keys[^1].c)); Write(n); };
        list.Children.Add(add);
        return Row(label, list);
    }
}
