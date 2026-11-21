using System.Globalization;
using System.Text.Json.Nodes;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;
using NoorEditor.Model;
using NoorEditor.Shell;

namespace NoorEditor.Panels;

/// <summary>
/// The Timeline panel (gameplay foundations, part 5; Godot's AnimationPlayer): select an entity, make a timeline for it,
/// then pose the scene and key it. Pick what to key (the entity or one of its children) and press Key position, Key
/// rotation, Key scale or Key property…: the value the scene shows now goes in at the scrub time. Scrubbing and Play
/// show the timeline in the viewport without changing the scene (Stop puts everything back). Click a key to select it,
/// Delete key removes it.
/// </summary>
public sealed class TimelinePanel : DockPanel
{
    private readonly EditorDocument _doc;
    private readonly ComboBox _timelines = new() { MinWidth = 140 };
    private readonly Button _new = new() { Content = "New timeline", Classes = { "tool" } };
    private readonly NumericUpDown _length = new() { Minimum = 0.01m, Maximum = 600, Increment = 0.5m, Width = 110, FormatString = "0.##" };
    private readonly CheckBox _loop = new() { Content = "Loop" };
    private readonly Slider _scrub = new() { Minimum = 0, Maximum = 1, Width = 320, VerticalAlignment = VerticalAlignment.Center };
    private readonly TextBlock _time = new() { Width = 56, VerticalAlignment = VerticalAlignment.Center, FontSize = 12 };
    private readonly Button _play = new() { Content = "▶ Play", Classes = { "tool" } };
    private readonly Button _stop = new() { Content = "■ Stop", Classes = { "tool" } };
    private readonly ComboBox _target = new() { MinWidth = 140 };
    private readonly ComboBox _ease = new() { MinWidth = 110 };
    private readonly Button _keyPosition = new() { Content = "Key position", Classes = { "tool" } };
    private readonly Button _keyRotation = new() { Content = "Key rotation", Classes = { "tool" } };
    private readonly Button _keyScale = new() { Content = "Key scale", Classes = { "tool" } };
    private readonly Button _keyProperty = new() { Content = "Key property…", Classes = { "tool" } };
    private readonly Button _addCall = new() { Content = "Add call…", Classes = { "tool" } };
    private readonly Button _deleteKey = new() { Content = "Delete key", Classes = { "tool" } };
    private readonly TextBlock _status = new() { FontSize = 11, Classes = { "muted" }, TextWrapping = TextWrapping.Wrap };
    private readonly TrackView _tracks;
    private readonly DispatcherTimer _player = new() { Interval = TimeSpan.FromMilliseconds(16) };
    private JsonObject? _info;  // the selected entity's timelines (noor_doc_timeline info)
    private string? _entity;
    private bool _syncing;
    private DateTime _playStart;
    private double _playFrom;

    /// <summary>The selected timeline's tracks as last read (tests look at them).</summary>
    public JsonArray Tracks => _tracks.Tracks;
    public string? Timeline => _timelines.SelectedItem as string;

    public TimelinePanel(EditorDocument doc)
    {
        _doc = doc;
        _tracks = new TrackView { Height = 150 };
        foreach (var e in new[] { "linear", "ease_in", "ease_out", "ease_in_out", "cubic_in_out", "sine_in_out", "back_out", "elastic_out", "bounce_out" })
            _ease.Items.Add(e);
        _ease.SelectedIndex = 0;
        ToolTip.SetTip(_new, "A new timeline for the selected entity (its animation player gets one if it has none)");
        ToolTip.SetTip(_keyPosition, "The target's position as the scene shows it now, as a key at the scrub time");
        ToolTip.SetTip(_keyProperty, "Any number or colour in a component (\"light.intensity\", \"sprite.color\"...) at the scrub time");
        ToolTip.SetTip(_addCall, "Calls a function of the target's script when play passes this time");
        ToolTip.SetTip(_scrub, "Drag to see the timeline at that time in the viewport (the scene isn't changed)");
        _new.Click += async (_, _) => await NewTimeline();
        _timelines.SelectionChanged += (_, _) => { if (!_syncing) ShowSelected(); };
        _length.ValueChanged += (_, _) => { if (!_syncing && _length.Value is { } v) Set(new JsonObject { ["length"] = (double)v }); };
        _loop.IsCheckedChanged += (_, _) => { if (!_syncing) Set(new JsonObject { ["loop"] = _loop.IsChecked == true }); };
        _scrub.ValueChanged += (_, _) => { if (!_syncing) Preview(_scrub.Value); };
        _play.Click += (_, _) => Play();
        _stop.Click += (_, _) => Stop();
        _keyPosition.Click += (_, _) => Key("position");
        _keyRotation.Click += (_, _) => Key("rotation");
        _keyScale.Click += (_, _) => Key("scale");
        _keyProperty.Click += async (_, _) => await KeyProperty();
        _addCall.Click += async (_, _) => await AddCall();
        _deleteKey.Click += (_, _) => DeleteKey();
        _tracks.TimeClicked += t => { _scrub.Value = t; };
        _player.Tick += (_, _) => Advance();

        var content = new StackPanel
        {
            Spacing = 6, Margin = new Thickness(10, 8),
            Children =
            {
                new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6, Children = { _timelines, _new, new TextBlock { Text = "Length", VerticalAlignment = VerticalAlignment.Center }, _length, _loop } },
                new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6, Children = { _play, _stop, _scrub, _time } },
                // The key buttons above the tracks: what's used most stays in view in a short dock.
                new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6, Children = { new TextBlock { Text = "Key", VerticalAlignment = VerticalAlignment.Center }, _target, _ease, _keyPosition, _keyRotation, _keyScale, _keyProperty, _addCall, _deleteKey } },
                _status,
                _tracks,
            },
        };
        Children.Add(new ScrollViewer { Content = content, HorizontalScrollBarVisibility = Avalonia.Controls.Primitives.ScrollBarVisibility.Auto });
        _doc.SelectionChanged += Rebuild;
        _doc.SceneChanged += () => { if (!_player.IsEnabled) Rebuild(); };
        Rebuild();
    }

    private JsonNode? Run(JsonObject command)
    {
        if (_entity != null && command["entity"] == null && command["op"]?.GetValue<string>() is not ("list" or "preview_end")) command["entity"] = _entity;
        if (Timeline != null && command["timeline"] == null) command["timeline"] = Timeline;
        return _doc.TimelineCommand(command);
    }

    private void Rebuild()
    {
        var selected = _doc.Selected is { FromPrefab: false } e ? e : null;
        if (selected?.Id != _entity) Stop();
        _entity = selected?.Id;
        _syncing = true;
        try
        {
            var before = Timeline;
            _timelines.Items.Clear();
            _info = _entity == null ? null : Run(new JsonObject { ["op"] = "info" }) as JsonObject;
            if (_info?["timelines"] is JsonObject list)
                foreach (var (name, _) in list) _timelines.Items.Add(name);
            _timelines.SelectedItem = before != null && _timelines.Items.Contains(before) ? before : _timelines.Items.Count > 0 ? _timelines.Items[0] : null;
            // What can be keyed: the entity ("") and its children, by path.
            var targetBefore = _target.SelectedItem as string;
            _target.Items.Clear();
            if (selected != null)
            {
                _target.Items.Add("(itself)");
                foreach (var path in ChildPaths(selected.Id, "")) _target.Items.Add(path);
            }
            _target.SelectedItem = targetBefore != null && _target.Items.Contains(targetBefore) ? targetBefore : _target.Items.Count > 0 ? _target.Items[0] : null;
        }
        finally
        {
            _syncing = false;
        }
        _new.IsEnabled = selected != null;
        ShowSelected();
    }

    private IEnumerable<string> ChildPaths(string parent, string prefix)
    {
        foreach (var c in _doc.Scene.Entities.Where(e => e.Parent == parent))
        {
            var path = prefix.Length == 0 ? c.Name : $"{prefix}/{c.Name}";
            yield return path;
            foreach (var deeper in ChildPaths(c.Id, path)) yield return deeper;
        }
    }

    private void ShowSelected()
    {
        var t = Timeline != null ? _info?["timelines"]?[Timeline] as JsonObject : null;
        var has = t != null && t["error"] == null;
        foreach (var c in new Control[] { _length, _loop, _scrub, _play, _stop, _target, _ease, _keyPosition, _keyRotation, _keyScale, _keyProperty, _addCall, _deleteKey })
            c.IsEnabled = has;
        _syncing = true;
        try
        {
            var length = t?["length"]?.GetValue<double>() ?? 1.0;
            _length.Value = (decimal)length;
            _loop.IsChecked = t?["loop"]?.GetValue<bool>() ?? false;
            _scrub.Maximum = length;
            if (_scrub.Value > length) _scrub.Value = length;
        }
        finally
        {
            _syncing = false;
        }
        _tracks.Show(t?["tracks"] as JsonArray ?? [], t?["length"]?.GetValue<double>() ?? 1.0, _scrub.Value);
        _time.Text = _scrub.Value.ToString("0.00 s", CultureInfo.InvariantCulture);
        _status.Text = _entity == null ? "Select an entity to animate."
                     : t == null ? "No timeline yet: New timeline makes one for the selected entity."
                     : t["error"] is { } err ? err.GetValue<string>()
                     : $"{t["path"]} · {(_tracks.Tracks.Count == 0 ? "no keys yet: pose the scene, then Key position / rotation / scale" : $"{_tracks.Tracks.Count} track(s)")}";
    }

    private async Task NewTimeline()
    {
        if (TopLevel.GetTopLevel(this) is not Window owner || _entity == null) return;
        var name = await Dialogs.Prompt(owner, "New timeline", "Name (what scripts play: e:play_timeline(\"open\"))", _timelines.Items.Count == 0 ? "open" : "timeline");
        if (string.IsNullOrWhiteSpace(name)) return;
        if (Run(new JsonObject { ["op"] = "new", ["name"] = name, ["length"] = 1.0 }) == null) return;
        Rebuild();
        _timelines.SelectedItem = name;
    }

    private void Set(JsonObject changes)
    {
        changes["op"] = "set";
        if (Run(changes) != null) Reread();
    }

    // The timeline again (after a key changed), keeping the scrub time and the preview.
    private void Reread()
    {
        _info = Run(new JsonObject { ["op"] = "info" }) as JsonObject;
        ShowSelected();
        if (_doc.TimelinePreviewing) Preview(_scrub.Value);
    }

    /// <summary>What the next key is for: "" (the entity itself) or a child's path (tests; the Key box does the same).</summary>
    public void SetTarget(string path) => _target.SelectedItem = path.Length == 0 ? "(itself)" : path;
    /// <summary>Moves the scrub (and the preview) to `t` seconds.</summary>
    public void ScrubTo(double t) => _scrub.Value = t;

    private string TargetPath => _target.SelectedItem is string s && s != "(itself)" ? s : "";

    /// <summary>Keys a property of the chosen target from what the scene shows (Key position / rotation / scale).</summary>
    public bool Key(string property, JsonNode? value = null)
    {
        var command = new JsonObject
        {
            ["op"] = "key", ["t"] = _scrub.Value, ["target"] = TargetPath, ["property"] = property == "rotation" ? "euler" : property,
            ["ease"] = _ease.SelectedItem as string ?? "linear",
        };
        if (value != null) command["value"] = value.DeepClone();
        var done = Run(command) != null;
        Reread();
        return done;
    }

    private async Task KeyProperty()
    {
        if (TopLevel.GetTopLevel(this) is not Window owner) return;
        var property = await Dialogs.Prompt(owner, "Key a property", "A component's number or colour, like light.intensity or sprite.color", "light.intensity");
        if (!string.IsNullOrWhiteSpace(property)) Key(property.Trim());
    }

    private async Task AddCall()
    {
        if (TopLevel.GetTopLevel(this) is not Window owner) return;
        var function = await Dialogs.Prompt(owner, "Add a call", "The script function to call when play passes this time", "on_timeline_moment");
        if (string.IsNullOrWhiteSpace(function)) return;
        Run(new JsonObject { ["op"] = "event", ["t"] = _scrub.Value, ["target"] = TargetPath, ["function"] = function.Trim() });
        Reread();
    }

    private void DeleteKey()
    {
        if (_tracks.SelectedKey is not { } k) return;
        Run(new JsonObject { ["op"] = "remove_key", ["track"] = k.Track, ["key"] = k.Key });
        _tracks.SelectedKey = null;
        Reread();
    }

    /// <summary>Shows the timeline at `t` in the viewport (the scene isn't changed).</summary>
    public void Preview(double t)
    {
        if (Timeline == null || _entity == null) return;
        Run(new JsonObject { ["op"] = "preview", ["t"] = t });
        _doc.TimelinePreviewing = true;
        _time.Text = t.ToString("0.00 s", CultureInfo.InvariantCulture);
        _tracks.Playhead = t;
    }

    private void Play()
    {
        if (Timeline == null) return;
        _playStart = DateTime.Now;
        _playFrom = _scrub.Value >= _scrub.Maximum ? 0 : _scrub.Value;
        _player.Start();
    }

    private void Advance()
    {
        var length = _scrub.Maximum;
        var t = _playFrom + (DateTime.Now - _playStart).TotalSeconds;
        if (t >= length)
        {
            if (_loop.IsChecked == true) t %= length;
            else
            {
                t = length;
                _player.Stop();
            }
        }
        _syncing = true;
        _scrub.Value = t;
        _syncing = false;
        Preview(t);
    }

    /// <summary>Stops playing and puts the scene back as it really is.</summary>
    public void Stop()
    {
        _player.Stop();
        if (!_doc.TimelinePreviewing) return;
        _doc.TimelineCommand(new JsonObject { ["op"] = "preview_end" });
        _doc.TimelinePreviewing = false;
    }

    /// <summary>The tracks drawn as rows of keys under a time ruler; click the ruler to scrub, a key to select it.</summary>
    private sealed class TrackView : Control
    {
        public JsonArray Tracks { get; private set; } = [];
        private double _length = 1.0;
        private double _cursor;
        public (int Track, int Key)? SelectedKey { get; set; }
        public event Action<double>? TimeClicked;
        private const double Left = 150, Ruler = 20, Row = 22;

        public double Playhead
        {
            get => _cursor;
            set
            {
                _cursor = value;
                InvalidateVisual();
            }
        }

        public void Show(JsonArray tracks, double length, double cursor)
        {
            Tracks = tracks;
            _length = Math.Max(0.01, length);
            _cursor = cursor;
            if (SelectedKey is { } k && (k.Track >= tracks.Count || k.Key >= (tracks[k.Track]?["keys"] as JsonArray)?.Count)) SelectedKey = null;
            Height = Ruler + Math.Max(3, tracks.Count) * Row + 6;
            InvalidateVisual();
        }

        private double X(double t) => Left + (Bounds.Width - Left - 10) * t / _length;

        public override void Render(DrawingContext g)
        {
            var bg = NoorTheme.Brush(NoorTheme.PanelRaised);
            var line = new Pen(NoorTheme.Brush(NoorTheme.TextMuted), 1);
            var text = NoorTheme.Brush(NoorTheme.Text);
            g.FillRectangle(bg, new Rect(0, 0, Bounds.Width, Bounds.Height));
            // The ruler: a tick every tenth of the length, labelled every other.
            for (var i = 0; i <= 10; i++)
            {
                var t = _length * i / 10;
                g.DrawLine(line, new Point(X(t), Ruler - (i % 2 == 0 ? 8 : 4)), new Point(X(t), Ruler));
                if (i % 2 == 0)
                    g.DrawText(new FormattedText(t.ToString("0.##", CultureInfo.InvariantCulture), CultureInfo.InvariantCulture, FlowDirection.LeftToRight,
                                                 Typeface.Default, 10, text), new Point(X(t) + 2, 0));
            }
            for (var r = 0; r < Tracks.Count; r++)
            {
                var track = Tracks[r] as JsonObject;
                var y = Ruler + r * Row + Row / 2;
                var label = track?["kind"]?.GetValue<string>() == "value"
                    ? $"{TrackName(track)} · {track?["property"]}" : $"{TrackName(track)} · {track?["kind"]}";
                g.DrawText(new FormattedText(label, CultureInfo.InvariantCulture, FlowDirection.LeftToRight, Typeface.Default, 11, text), new Point(6, y - 7));
                g.DrawLine(line, new Point(Left, y), new Point(Bounds.Width - 10, y));
                if (track?["keys"] is not JsonArray keys) continue;
                for (var k = 0; k < keys.Count; k++)
                {
                    var t = keys[k]?["t"]?.GetValue<double>() ?? 0;
                    var selected = SelectedKey is { } s && s.Track == r && s.Key == k;
                    var fill = NoorTheme.Brush(selected ? NoorTheme.Accent : track?["kind"]?.GetValue<string>() == "value" ? NoorTheme.Text : NoorTheme.Warn);
                    g.DrawGeometry(fill, null, new PolylineGeometry(new[] { new Point(X(t), y - 6), new Point(X(t) + 6, y), new Point(X(t), y + 6), new Point(X(t) - 6, y) }, true));
                }
            }
            g.DrawLine(new Pen(NoorTheme.Brush(NoorTheme.Accent), 2), new Point(X(_cursor), 0), new Point(X(_cursor), Bounds.Height));
        }

        private static string TrackName(JsonObject? track) => track?["target"]?.GetValue<string>() is { Length: > 0 } s ? s : "(itself)";

        protected override void OnPointerPressed(PointerPressedEventArgs e)
        {
            base.OnPointerPressed(e);
            var p = e.GetPosition(this);
            if (p.X < Left) return;
            // A key under the pointer is selected; otherwise the time there is scrubbed to.
            for (var r = 0; r < Tracks.Count; r++)
            {
                var y = Ruler + r * Row + Row / 2;
                if (Math.Abs(p.Y - y) > Row / 2 || Tracks[r]?["keys"] is not JsonArray keys) continue;
                for (var k = 0; k < keys.Count; k++)
                    if (Math.Abs(X(keys[k]?["t"]?.GetValue<double>() ?? 0) - p.X) <= 7)
                    {
                        SelectedKey = (r, k);
                        InvalidateVisual();
                        return;
                    }
            }
            var time = Math.Clamp((p.X - Left) / Math.Max(1, Bounds.Width - Left - 10) * _length, 0, _length);
            TimeClicked?.Invoke(time);
        }
    }
}
