using System.Globalization;
using System.Text.Json.Nodes;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Shapes;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Threading;
using Dock.Model.Mvvm.Controls;
using NoorEditor.Interop;
using NoorEditor.Model;
using NoorEditor.Shell;

namespace NoorEditor.Panels;

/// <summary>
/// A sprite sheet (.noorsprite, Milestone 8B part 5, ADR-035): the picture cut into frames by a grid, named animations
/// over those frames, its size in the world and its filtering. A tab next to the viewport; every change is saved at once
/// (noor_doc_sprite "save"), and sprites in the viewport show it straight away.
/// </summary>
public sealed class SpriteDocument : Document
{
    public EditorDocument Doc { get; }
    public string RelativePath { get; }

    /// <summary>Makes the view (MainWindow sets it; Dock calls it whenever the tab is shown).</summary>
    public Func<SpriteDocument, Control>? CreateView { get; set; }

    public SpriteDocument(EditorDocument doc, string relativePath)
    {
        Doc = doc;
        RelativePath = relativePath;
        Id = "Sprite:" + relativePath;
        Title = System.IO.Path.GetFileName(relativePath);
        CanFloat = true;
        CanClose = true;
    }

    /// <summary>The engine's view of the sheet: the file, the image size and the sliced frames (or "error").</summary>
    public JsonObject? Read() => Doc.Sprite(new JsonObject { ["op"] = "read", ["asset"] = RelativePath }) as JsonObject;

    public JsonObject? Save(JsonObject sheet) =>
        Doc.Sprite(new JsonObject { ["op"] = "save", ["asset"] = RelativePath, ["sheet"] = sheet.DeepClone() }) as JsonObject;

    /// <summary>A new entity showing the sheet (with `animation`), or the selected entity's sprite switched to it.</summary>
    public JsonObject? Place(string? animation, string? entity, double[]? position = null)
    {
        var command = new JsonObject { ["op"] = "apply", ["asset"] = RelativePath };
        if (animation != null) command["animation"] = animation;
        if (entity != null) command["entity"] = entity;
        else command["position"] = new JsonArray((position ?? [0, 0, 0]).Select(v => (JsonNode?)v).ToArray());
        return Doc.Sprite(command) as JsonObject;
    }
}

public sealed class SpritePanel : Grid
{
    public SpriteDocument Sprite { get; }

    private JsonObject _sheet = new();
    private Bitmap? _bitmap;
    private readonly SheetView _view = new();
    private readonly Image _preview = new() { Width = 160, Height = 160, Stretch = Stretch.Uniform };
    private readonly TextBlock _previewCaption = new() { FontSize = 11, Classes = { "muted" }, HorizontalAlignment = HorizontalAlignment.Center };
    private readonly TextBlock _info = new() { FontSize = 12, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 8, 0, 0) };
    private readonly StackPanel _fields = new() { Spacing = 6, Margin = new Thickness(12, 10) };
    private readonly StackPanel _animations = new() { Spacing = 4 };
    private readonly DispatcherTimer _timer = new();
    private readonly Dictionary<int, CroppedBitmap> _crops = new();
    private string? _previewing;  // the animation playing in the preview
    private int _step;            // its step (index into its frames)
    private int _direction;       // 8-direction sheets: the view the preview shows (0 faces you)
    private readonly TextBlock _directionText = new() { FontSize = 11, VerticalAlignment = VerticalAlignment.Center, MinWidth = 120,
                                                        TextAlignment = TextAlignment.Center };
    private readonly StackPanel _directionRow = new() { Orientation = Orientation.Horizontal, Spacing = 6, HorizontalAlignment = HorizontalAlignment.Center };

    /// <summary>The engine's last answer (tests read it).</summary>
    public JsonObject? Last { get; private set; }
    /// <summary>The frame the animated preview is showing now (-1: none).</summary>
    public int PreviewFrame { get; private set; } = -1;
    public string? PreviewAnimation => _previewing;

    public SpritePanel(SpriteDocument sprite)
    {
        Sprite = sprite;
        ColumnDefinitions = new ColumnDefinitions("*,4,380");

        // The sheet with its frames drawn over it, and the animated preview under it.
        var left = new DockPanel { Background = NoorTheme.Brush(NoorTheme.Background) };
        var caption = new TextBlock { Text = sprite.RelativePath, FontSize = 12, Margin = new Thickness(10, 6), Classes = { "muted" } };
        DockPanel.SetDock(caption, Avalonia.Controls.Dock.Top);
        left.Children.Add(caption);
        RenderOptions.SetBitmapInterpolationMode(_preview, BitmapInterpolationMode.None);
        var previewBox = new StackPanel
        {
            Spacing = 4, Margin = new Thickness(10),
            Children =
            {
                new Border { Child = _preview, Background = NoorTheme.Brush(NoorTheme.PanelRaised), CornerRadius = new CornerRadius(6), Padding = new Thickness(8),
                             HorizontalAlignment = HorizontalAlignment.Center },
                _previewCaption,
                _directionRow,
            },
        };
        // 8-direction sheets (8B part 6): turn the preview round.
        var turnLeft = new Button { Content = "⟲", Classes = { "tool" }, Padding = new Thickness(8, 0) };
        var turnRight = new Button { Content = "⟳", Classes = { "tool" }, Padding = new Thickness(8, 0) };
        ToolTip.SetTip(turnLeft, "Turn the preview (the next direction)");
        ToolTip.SetTip(turnRight, "Turn the preview (the previous direction)");
        turnLeft.Click += (_, _) => PreviewDirection = (_direction + 1) % Math.Max(1, Directions);
        turnRight.Click += (_, _) => PreviewDirection = (_direction + Directions - 1) % Math.Max(1, Directions);
        _directionRow.Children.Add(turnLeft);
        _directionRow.Children.Add(_directionText);
        _directionRow.Children.Add(turnRight);
        DockPanel.SetDock(previewBox, Avalonia.Controls.Dock.Bottom);
        left.Children.Add(previewBox);
        left.Children.Add(_view);
        Children.Add(left);

        var splitter = new GridSplitter { Background = NoorTheme.Brush(NoorTheme.Border), ResizeDirection = GridResizeDirection.Columns };
        SetColumn(splitter, 1);
        Children.Add(splitter);

        var scroll = new ScrollViewer { Content = _fields, Background = NoorTheme.Brush(NoorTheme.Panel) };
        SetColumn(scroll, 2);
        Children.Add(scroll);

        _timer.Tick += (_, _) => Tick();
        AttachedToVisualTree += (_, _) => _timer.Start();
        DetachedFromVisualTree += (_, _) => _timer.Stop();
        Load();
    }

    // ---- loading and saving -------------------------------------------------------------------------------------------

    private void Load()
    {
        Last = Sprite.Read();
        _sheet = Last?["sheet"]?.DeepClone() as JsonObject ?? new JsonObject { ["noor_sprite"] = 1 };
        var texture = Last?["texture_path"]?.GetValue<string>();
        _bitmap = null;
        if (texture != null)
        {
            try
            {
                _bitmap = new Bitmap(System.IO.Path.Combine(Sprite.Doc.ProjectRoot, texture));
            }
            catch (Exception e) when (e is IOException or ArgumentException or NotSupportedException or InvalidOperationException)
            {
                EngineLog.Warn("Sprites", $"No preview of {texture}: {e.Message}");
            }
        }
        _previewing ??= Animations().Select(kv => kv.Key).FirstOrDefault();
        BuildFields();
        Shown();
    }

    private void Save()
    {
        if (Sprite.Save(_sheet) is { } result) Last = result;
        _crops.Clear();
        Shown();
    }

    /// <summary>The engine's answer shown: the frames over the picture, the numbers, the preview.</summary>
    private void Shown()
    {
        var frames = new List<PixelRect>();
        if (Last?["frames"] is JsonArray fa)
            foreach (var f in fa.OfType<JsonArray>())
                frames.Add(new PixelRect(f[0]!.GetValue<int>(), f[1]!.GetValue<int>(), f[2]!.GetValue<int>(), f[3]!.GetValue<int>()));
        _view.Show(_bitmap, frames, AnimationFrames(_previewing));
        if (Last?["error"]?.GetValue<string>() is { } error)
        {
            _info.Text = $"⚠ {error}";
            _info.Foreground = NoorTheme.Brush(NoorTheme.Warn);
        }
        else
        {
            var size = Last?["frame_size_m"] as JsonArray;
            var first = frames.Count > 0 ? frames[0] : default;
            _info.Text = $"{frames.Count} frames ({Last?["columns"]} × {Last?["rows"]}) of {first.Width} × {first.Height} px, " +
                         $"each {Num(size?[0])} × {Num(size?[1])} m in the world";
            _info.Foreground = NoorTheme.Brush(NoorTheme.Text);
        }
        _step = 0;
        Tick();
    }

    // ---- the fields ---------------------------------------------------------------------------------------------------

    private JsonObject Grid() => _sheet["grid"] as JsonObject ?? (JsonObject)(_sheet["grid"] = new JsonObject { ["columns"] = 1, ["rows"] = 1 });
    private bool BySize => Grid().ContainsKey("frame_width") || Grid().ContainsKey("frame_height");
    private JsonObject AnimationsObject() => _sheet["animations"] as JsonObject ?? (JsonObject)(_sheet["animations"] = new JsonObject());
    private IEnumerable<KeyValuePair<string, JsonNode?>> Animations() => (_sheet["animations"] as JsonObject)?.ToList() ?? [];

    private void BuildFields()
    {
        _fields.Children.Clear();
        _fields.Children.Add(new TextBlock { Text = "Grid", Classes = { "section" } });
        var mode = new ComboBox { ItemsSource = new[] { "Columns and rows", "Frame size (px)" }, SelectedIndex = BySize ? 1 : 0, Classes = { "field" },
                                  HorizontalAlignment = HorizontalAlignment.Stretch };
        ToolTip.SetTip(mode, "Cut the picture into a number of columns and rows, or into frames of a size (as many as fit)");
        mode.SelectionChanged += (_, _) =>
        {
            if ((mode.SelectedIndex == 1) == BySize) return;
            var cols = Last?["columns"]?.GetValue<int>() ?? 1;
            var rows = Last?["rows"]?.GetValue<int>() ?? 1;
            var first = Last?["frames"] is JsonArray { Count: > 0 } f ? f[0] as JsonArray : null;
            var g = Grid();
            foreach (var k in new[] { "columns", "rows", "frame_width", "frame_height" }) g.Remove(k);
            if (mode.SelectedIndex == 1)
            {
                g["frame_width"] = first?[2]?.GetValue<int>() ?? 32;
                g["frame_height"] = first?[3]?.GetValue<int>() ?? 32;
            }
            else
            {
                g["columns"] = cols;
                g["rows"] = rows;
            }
            Save();
            BuildFields();
        };
        _fields.Children.Add(Row("Cut by", mode));
        if (BySize)
        {
            _fields.Children.Add(Row("Frame width", IntBox(Grid(), "frame_width", 32, 1)));
            _fields.Children.Add(Row("Frame height", IntBox(Grid(), "frame_height", 32, 1)));
        }
        else
        {
            _fields.Children.Add(Row("Columns", IntBox(Grid(), "columns", 1, 1)));
            _fields.Children.Add(Row("Rows", IntBox(Grid(), "rows", 1, 1)));
        }
        _fields.Children.Add(Row("Margin (px)", IntBox(Grid(), "margin", 0, 0)));
        _fields.Children.Add(Row("Spacing (px)", IntBox(Grid(), "spacing", 0, 0)));
        _fields.Children.Add(_info);

        _fields.Children.Add(new TextBlock { Text = "In the world", Classes = { "section" }, Margin = new Thickness(0, 10, 0, 0) });
        var ppm = NumberBox(Number(_sheet["pixels_per_metre"], 64), x => { _sheet["pixels_per_metre"] = Math.Max(0.001, x); Save(); });
        ToolTip.SetTip(ppm, "How many of the picture's pixels make a metre: 32 px frames at 16 are 2 m tall");
        _fields.Children.Add(Row("Pixels per metre", ppm));
        var pivot = _sheet["pivot"] as JsonArray;
        var px = NumberBox(Number(pivot?[0], 0.5), x => SetPivot(0, x));
        var py = NumberBox(Number(pivot?[1], 0.0), y => SetPivot(1, y));
        ToolTip.SetTip(py, "Where the entity stands on the frame: 0.5, 0 is the bottom middle (feet); 0.5, 0.5 the centre");
        var pivots = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6, Children = { px, py } };
        _fields.Children.Add(Row("Pivot (x, y)", pivots));
        var filter = new ComboBox { ItemsSource = new[] { "Nearest (crisp pixel art)", "Linear (smooth)" }, Classes = { "field" }, HorizontalAlignment = HorizontalAlignment.Stretch,
                                    SelectedIndex = _sheet["filter"]?.GetValue<string>() == "linear" ? 1 : 0 };
        filter.SelectionChanged += (_, _) => { _sheet["filter"] = filter.SelectedIndex == 1 ? "linear" : "nearest"; Save(); };
        _fields.Children.Add(Row("Filtering", filter));
        var directions = NumberBox(Directions, x => { _sheet["directions"] = Math.Max(1, (int)Math.Round(x)); _direction = 0; Save(); BuildAnimations(); });
        ToolTip.SetTip(directions, "8-direction characters: the columns hold the views, 0 facing you, then turning to your left " +
                                   "(2 left, 4 away, 6 right). Animations name their frames in the first column.");
        _fields.Children.Add(Row("Directions", directions));

        _fields.Children.Add(new TextBlock { Text = "Animations", Classes = { "section" }, Margin = new Thickness(0, 10, 0, 0) });
        _fields.Children.Add(new TextBlock { Text = "Frames: a range (8-11, or 3-1 backwards) or a list (0, 1, 1, 2). Frames count from 0, left to right, top to bottom.",
                                             FontSize = 10, Classes = { "muted" }, TextWrapping = TextWrapping.Wrap });
        _fields.Children.Add(_animations);
        BuildAnimations();
        var add = new Button { Content = "Add animation", Classes = { "tool" } };
        add.Click += (_, _) => AddAnimation(null, 0, Math.Max(0, (Last?["frames"] as JsonArray)?.Count - 1 ?? 0), 8);
        var place = new Button { Content = "Place in scene", Classes = { "tool" } };
        ToolTip.SetTip(place, "A new sprite entity showing this sheet (playing the animation in the preview)");
        place.Click += (_, _) => PlaceInScene();
        _fields.Children.Add(new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6, Margin = new Thickness(0, 6, 0, 0), Children = { add, place } });
    }

    private void BuildAnimations()
    {
        _animations.Children.Clear();
        foreach (var (name, node) in Animations())
        {
            if (node is not JsonObject a) continue;
            var play = new RadioButton { GroupName = "sprite-preview-" + Sprite.Id, IsChecked = name == _previewing, VerticalAlignment = VerticalAlignment.Center };
            ToolTip.SetTip(play, "Play in the preview");
            play.IsCheckedChanged += (_, _) => { if (play.IsChecked == true) Preview(name); };
            var nameBox = new TextBox { Text = name, Classes = { "field" }, Width = 92 };
            void Rename()
            {
                var to = nameBox.Text?.Trim() ?? "";
                if (to.Length == 0 || to == name || AnimationsObject().ContainsKey(to)) { nameBox.Text = name; return; }
                var anims = AnimationsObject();
                var body = anims[name]!.DeepClone();
                anims.Remove(name);
                anims[to] = body;
                if (_previewing == name) _previewing = to;
                Save();
                BuildAnimations();
            }
            nameBox.LostFocus += (_, _) => Rename();
            nameBox.KeyDown += (_, e) => { if (e.Key == Key.Enter) Rename(); };
            var frames = new TextBox { Text = FramesText(a["frames"]), Classes = { "field" }, Width = 80 };
            void CommitFrames()
            {
                if (ParseFrames(frames.Text) is { } parsed) { a["frames"] = parsed; Save(); }
                frames.Text = FramesText(a["frames"]);
            }
            frames.LostFocus += (_, _) => CommitFrames();
            frames.KeyDown += (_, e) => { if (e.Key == Key.Enter) CommitFrames(); };
            var fps = NumberBox(Number(a["fps"], 8), x => { a["fps"] = Math.Max(0, x); Save(); });
            fps.Width = 46;
            ToolTip.SetTip(fps, "Frames a second");
            var loop = new CheckBox { Content = "Loop", IsChecked = a["loop"]?.GetValue<bool>() ?? true, FontSize = 11 };
            loop.IsCheckedChanged += (_, _) => { a["loop"] = loop.IsChecked == true; Save(); };
            var turns = new CheckBox { Content = "Turns", IsChecked = a["directional"]?.GetValue<bool>() ?? true, FontSize = 11, IsVisible = Directions > 1 };
            ToolTip.SetTip(turns, "Shows the view for the camera's direction (off: the same frames from every side, like lying down)");
            turns.IsCheckedChanged += (_, _) =>
            {
                if (turns.IsChecked == true) a.Remove("directional");
                else a["directional"] = false;
                Save();
            };
            var remove = new Button { Content = "✕", Classes = { "tool" }, Padding = new Thickness(6, 0) };
            ToolTip.SetTip(remove, "Remove this animation");
            remove.Click += (_, _) =>
            {
                AnimationsObject().Remove(name);
                if (_previewing == name) _previewing = Animations().Select(kv => kv.Key).FirstOrDefault();
                Save();
                BuildAnimations();
            };
            _animations.Children.Add(new StackPanel { Orientation = Orientation.Horizontal, Spacing = 4, Children = { play, nameBox, frames, fps, loop, turns, remove } });
        }
    }

    private void SetPivot(int axis, double value)
    {
        var pivot = _sheet["pivot"] as JsonArray ?? new JsonArray(0.5, 0.0);
        var xy = new[] { Number(pivot[0], 0.5), Number(pivot[1], 0.0) };
        xy[axis] = value;
        _sheet["pivot"] = new JsonArray(xy[0], xy[1]);
        Save();
    }

    // ---- the animated preview -----------------------------------------------------------------------------------------

    private int Directions => Math.Max(1, (int)Number(_sheet["directions"], 1));

    /// <summary>The view the preview shows on an 8-direction sheet (0 faces you, turning to your left).</summary>
    public int PreviewDirection
    {
        get => _direction;
        set
        {
            _direction = Math.Clamp(value, 0, Directions - 1);
            _crops.Clear();
            _view.Show(_bitmap, _view.Frames, AnimationFrames(_previewing));
            Tick();
        }
    }

    // The frames an animation shows in the previewed direction (directional animations move along the columns).
    private List<int> AnimationFrames(string? name)
    {
        var frames = new List<int>();
        if (name == null || AnimationsObject()[name] is not JsonObject a) return frames;
        if (a["frames"] is JsonArray list)
            frames.AddRange(list.Select(f => (int)Number(f, -1)).Where(f => f >= 0));
        else if (a["frames"] is JsonObject range)
        {
            int from = (int)Number(range["from"], 0), to = (int)Number(range["to"], 0);
            for (var f = from; from <= to ? f <= to : f >= to; f += from <= to ? 1 : -1) frames.Add(f);
        }
        var turn = Directions > 1 && a["directional"]?.GetValue<bool>() != false ? Math.Min(_direction, Directions - 1) : 0;
        return turn == 0 ? frames : frames.Select(f => f + turn).ToList();
    }

    private void Tick()
    {
        var frames = AnimationFrames(_previewing);
        var all = Last?["frames"] as JsonArray;
        var fps = _previewing != null && AnimationsObject()[_previewing] is JsonObject a ? Number(a["fps"], 8) : 0;
        _timer.Interval = TimeSpan.FromSeconds(fps > 0 ? 1.0 / Math.Min(fps, 60) : 0.5);
        var loop = _previewing != null && (AnimationsObject()[_previewing] as JsonObject)?["loop"]?.GetValue<bool>() != false;
        if (all == null || all.Count == 0 || _bitmap == null)
        {
            _preview.Source = null;
            PreviewFrame = -1;
            _previewCaption.Text = "";
            return;
        }
        var frame = frames.Count == 0 ? 0 : frames[loop ? _step % frames.Count : Math.Min(_step, frames.Count - 1)];
        frame = Math.Clamp(frame, 0, all.Count - 1);
        if (!_crops.TryGetValue(frame, out var crop) && all[frame] is JsonArray r)
        {
            crop = new CroppedBitmap(_bitmap, new PixelRect(r[0]!.GetValue<int>(), r[1]!.GetValue<int>(), r[2]!.GetValue<int>(), r[3]!.GetValue<int>()));
            _crops[frame] = crop;
        }
        _preview.Source = crop;
        PreviewFrame = frame;
        _view.Current = frame;
        _previewCaption.Text = _previewing == null ? $"frame {frame}" : $"{_previewing} · frame {frame}";
        _directionRow.IsVisible = Directions > 1;
        _directionText.Text = $"direction {_direction} · {DirectionName(_direction, Directions)}";
        _step++;
    }

    // ---- for tests and buttons ----------------------------------------------------------------------------------------

    /// <summary>Cut by columns and rows (as if typed in the fields).</summary>
    public void SetGrid(int columns, int rows)
    {
        var g = Grid();
        foreach (var k in new[] { "frame_width", "frame_height" }) g.Remove(k);
        g["columns"] = columns;
        g["rows"] = rows;
        Save();
        BuildFields();
    }

    public void SetPixelsPerMetre(double ppm)
    {
        _sheet["pixels_per_metre"] = ppm;
        Save();
        BuildFields();
    }

    /// <summary>A new animation over frames <paramref name="from"/>..<paramref name="to"/>, previewed at once.</summary>
    public void AddAnimation(string? name, int from, int to, double fps)
    {
        var anims = AnimationsObject();
        name ??= "animation";
        var unique = name;
        for (var n = 2; anims.ContainsKey(unique); n++) unique = $"{name} {n}";
        anims[unique] = new JsonObject { ["frames"] = new JsonObject { ["from"] = from, ["to"] = to }, ["fps"] = fps, ["loop"] = true };
        _previewing = unique;
        Save();
        BuildAnimations();
    }

    public void Preview(string name)
    {
        _previewing = name;
        _step = 0;
        _view.Show(_bitmap, _view.Frames, AnimationFrames(name));
        Tick();
    }

    /// <summary>The selected entity's sprite switched to this sheet, or (none selected) a new sprite entity.</summary>
    public JsonObject? PlaceInScene(double[]? position = null)
    {
        var selected = Sprite.Doc.SelectedId;
        var hasSprite = selected != null && Sprite.Doc.Scene.Entities.FirstOrDefault(e => e.Id == selected)?.Components["sprite"] is JsonObject;
        var placed = Sprite.Place(_previewing, hasSprite ? selected : null, position);
        if (placed?["entity"]?.GetValue<string>() is { } id) Sprite.Doc.Select(id);
        return placed;
    }

    // ---- small helpers ------------------------------------------------------------------------------------------------

    private static string DirectionName(int d, int count)
    {
        if (count != 8 && count != 4) return $"{d * 360 / Math.Max(1, count)}°";
        var names = new[] { "facing you", "your left, front", "your left", "your left, away", "away", "your right, away", "your right", "your right, front" };
        return names[count == 4 ? d * 2 : d];
    }

    /// <summary>For tests: the number of directions (as if typed in the field).</summary>
    public void SetDirections(int directions)
    {
        _sheet["directions"] = directions;
        _direction = 0;
        Save();
        BuildFields();
    }

    private static string Num(JsonNode? n) => Number(n, 0).ToString("0.##", CultureInfo.InvariantCulture);
    // A JsonValue made in C# from an int won't give a double (one parsed from text will), so ask for both.
    private static double Number(JsonNode? n, double fallback) =>
        n is not JsonValue v ? fallback : v.TryGetValue<double>(out var d) ? d : v.TryGetValue<int>(out var i) ? i : v.TryGetValue<long>(out var l) ? l : fallback;

    private static string FramesText(JsonNode? frames) => frames switch
    {
        JsonObject r => $"{Number(r["from"], 0)}-{Number(r["to"], 0)}",
        JsonArray list => string.Join(", ", list.Select(f => Num(f))),
        _ => "0",
    };

    /// <summary>"8-11" → {"from": 8, "to": 11}; "0, 1, 1, 2" → [0, 1, 1, 2]; anything else → null.</summary>
    public static JsonNode? ParseFrames(string? text)
    {
        text = text?.Trim() ?? "";
        var dash = text.IndexOf('-');
        if (dash > 0 && int.TryParse(text[..dash].Trim(), out var from) && int.TryParse(text[(dash + 1)..].Trim(), out var to) && from >= 0 && to >= 0)
            return new JsonObject { ["from"] = from, ["to"] = to };
        var parts = text.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        if (parts.Length == 0) return null;
        var list = new JsonArray();
        foreach (var p in parts)
        {
            if (!int.TryParse(p, out var f) || f < 0) return null;
            list.Add(f);
        }
        return list;
    }

    private Control IntBox(JsonObject target, string key, int fallback, int min)
    {
        var box = NumberBox(Number(target[key], fallback), x => { target[key] = Math.Max(min, (int)Math.Round(x)); Save(); });
        return box;
    }

    private static TextBox NumberBox(double value, Action<double> set)
    {
        var box = new TextBox { Text = value.ToString(CultureInfo.InvariantCulture), Classes = { "field" }, MinWidth = 50 };
        void Commit()
        {
            if (double.TryParse(box.Text, NumberStyles.Float, CultureInfo.InvariantCulture, out var x)) { if (x != value) { value = x; set(x); } }
            else box.Text = value.ToString(CultureInfo.InvariantCulture);
        }
        box.LostFocus += (_, _) => Commit();
        box.KeyDown += (_, e) => { if (e.Key == Key.Enter) Commit(); };
        return box;
    }

    private static Control Row(string label, Control editor)
    {
        var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("128,*") };
        var text = new TextBlock { Text = label, FontSize = 12, VerticalAlignment = VerticalAlignment.Center };
        SetColumn(editor, 1);
        grid.Children.Add(text);
        grid.Children.Add(editor);
        return grid;
    }
}

/// <summary>
/// The sheet scaled to fit (whole multiples for pixel art, so every texel stays square), each frame outlined and
/// numbered, the previewed animation's frames tinted and the one showing now in cyan. Laid out by hand rather than in a
/// Viewbox, so the outlines stay one pixel wide at any zoom.
/// </summary>
internal sealed class SheetView : Canvas
{
    private Bitmap? _bitmap;
    private List<int> _highlight = [];
    private int _current = -1;
    private readonly List<Rectangle> _boxes = [];
    public List<PixelRect> Frames { get; private set; } = [];

    public SheetView()
    {
        ClipToBounds = true;
        SizeChanged += (_, _) => Rebuild();
    }

    public int Current
    {
        get => _current;
        set
        {
            if (_current == value) return;
            _current = value;
            Restyle();
        }
    }

    public void Show(Bitmap? bitmap, List<PixelRect> frames, List<int> highlight)
    {
        _bitmap = bitmap;
        Frames = frames;
        _highlight = highlight;
        Rebuild();
    }

    private void Rebuild()
    {
        Children.Clear();
        _boxes.Clear();
        if (_bitmap == null || Bounds.Width < 20 || Bounds.Height < 20) return;
        double w = _bitmap.PixelSize.Width, h = _bitmap.PixelSize.Height;
        var fit = Math.Min((Bounds.Width - 24) / w, (Bounds.Height - 24) / h);
        var scale = fit >= 1 ? Math.Floor(fit) : fit;
        var ox = Math.Round((Bounds.Width - w * scale) / 2);
        var oy = Math.Round((Bounds.Height - h * scale) / 2);
        // A checker behind it, so transparent pixels read as transparent.
        Children.Add(Placed(new Border { Width = w * scale, Height = h * scale, Background = Checker() }, ox, oy));
        var image = new Image { Source = _bitmap, Width = w * scale, Height = h * scale, Stretch = Stretch.Fill };
        RenderOptions.SetBitmapInterpolationMode(image, BitmapInterpolationMode.None);
        Children.Add(Placed(image, ox, oy));
        for (var i = 0; i < Frames.Count; i++)
        {
            var f = Frames[i];
            var box = new Rectangle { Width = Math.Max(1, f.Width * scale), Height = Math.Max(1, f.Height * scale), StrokeThickness = 1 };
            _boxes.Add(box);
            Children.Add(Placed(box, ox + f.X * scale, oy + f.Y * scale));
            if (f.Width * scale >= 18 && f.Height * scale >= 14)
                Children.Add(Placed(new TextBlock { Text = $"{i}", FontSize = 10, Foreground = NoorTheme.Brush(NoorTheme.Text), Opacity = 0.85 },
                                    ox + f.X * scale + 3, oy + f.Y * scale + 1));
        }
        Restyle();
    }

    private void Restyle()
    {
        var animated = new HashSet<int>(_highlight);
        for (var i = 0; i < _boxes.Count; i++)
        {
            var box = _boxes[i];
            box.Stroke = NoorTheme.Brush(i == _current ? NoorTheme.Cyan : animated.Contains(i) ? NoorTheme.Accent : Color.FromArgb(150, 154, 147, 176));
            box.StrokeThickness = i == _current ? 2 : 1;
            box.Fill = animated.Contains(i) ? new SolidColorBrush(Color.FromArgb(40, 124, 77, 255)) : null;
        }
    }

    private static Control Placed(Control c, double x, double y)
    {
        SetLeft(c, x);
        SetTop(c, y);
        return c;
    }

    private static IBrush Checker()
    {
        var a = new SolidColorBrush(Color.Parse("#2A2638"));
        var b = new SolidColorBrush(Color.Parse("#211D2E"));
        var group = new DrawingGroup
        {
            Children =
            {
                new GeometryDrawing { Brush = b, Geometry = new RectangleGeometry(new Rect(0, 0, 16, 16)) },
                new GeometryDrawing { Brush = a, Geometry = new RectangleGeometry(new Rect(0, 0, 8, 8)) },
                new GeometryDrawing { Brush = a, Geometry = new RectangleGeometry(new Rect(8, 8, 8, 8)) },
            },
        };
        return new DrawingBrush(group) { TileMode = TileMode.Tile, DestinationRect = new RelativeRect(0, 0, 16, 16, RelativeUnit.Absolute) };
    }
}
