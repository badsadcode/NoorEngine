using System.Globalization;
using System.Text.Json.Nodes;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;
using NoorEditor.Interop;
using NoorEditor.Model;
using NoorEditor.Shell;

namespace NoorEditor.Panels;

/// <summary>
/// The Animation panel (Milestone 8C, part 6, ADR-036), a tab beside the Content Browser so the viewport stays in view.
/// It shows one character of the scene (the selected one, or picked at the top) and previews it in the viewport
/// without changing the scene (noor_doc_animation "preview"): a clip scrubbed on the timeline, played at a speed and
/// looped; its controller running with parameters you set, one of its states, or one transition (the fade is shaded on
/// the timeline); the skeleton drawn over the model. The Controller tab edits the .nooranim's states and transitions
/// (a state graph to click, and fields), saved as you change them, so the preview follows. The Bone map tab shows the
/// humanoid map retargeting uses (its preset, warnings, each bone's node) and corrects it in the model's .noormeta;
/// picking a row lights the bone up in the viewport. The preview ends when the panel is hidden.
/// </summary>
public sealed class AnimationPanel : DockPanel
{
    private readonly EditorDocument _doc;
    private readonly ComboBox _character = new() { MinWidth = 200, Classes = { "field" } };
    private readonly CheckBox _skeleton = new() { Content = "Skeleton", IsChecked = true };
    private readonly CheckBox _inPlace = new() { Content = "In place" };
    private readonly TextBlock _about = new() { FontSize = 11, Classes = { "muted" }, VerticalAlignment = VerticalAlignment.Center, TextTrimming = TextTrimming.CharacterEllipsis };
    // The three pages (a compact switch in the header, so a short dock still has room for them).
    private readonly Button[] _tabButtons = new Button[3];
    private readonly Control[] _pages = new Control[3];
    private readonly Panel _pagesHost = new();
    private int _tab;
    private readonly TextBlock _empty = new()
    {
        Text = "No animated characters in this scene yet. Add a model with animations or a skin, or use Create ▸ Player.",
        Classes = { "muted" }, FontSize = 12, Margin = new Thickness(12), TextWrapping = TextWrapping.Wrap,
    };

    // The timeline.
    private readonly Button _play = new() { Content = "▶", Classes = { "tool" }, Width = 34 };
    private readonly Button _restart = new() { Content = "⏮", Classes = { "tool" }, Width = 34 };
    private readonly TimelineView _timeline = new() { Height = 34, Margin = new Thickness(6, 0) };
    private readonly TextBlock _timeText = new() { FontSize = 12, MinWidth = 110, VerticalAlignment = VerticalAlignment.Center, FontFamily = new FontFamily("Consolas, monospace") };
    private readonly ComboBox _speed = new() { ItemsSource = new[] { "0.25×", "0.5×", "1×", "2×" }, SelectedIndex = 2, Classes = { "field" } };
    private readonly CheckBox _loop = new() { Content = "Loop", IsChecked = true };
    private readonly TextBlock _modeText = new() { FontSize = 11, Classes = { "muted" }, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(8, 0, 0, 0) };

    // Clips.
    private readonly ListBox _clips = new();

    // Controller.
    private readonly TextBlock _controllerPath = new() { FontSize = 11, Classes = { "muted" }, TextWrapping = TextWrapping.Wrap };
    private readonly TextBlock _controllerError = new() { FontSize = 11, TextWrapping = TextWrapping.Wrap, Foreground = NoorTheme.Brush(NoorTheme.Warn) };
    private readonly ComboBox _layer = new() { MinWidth = 140, Classes = { "field" } };
    private readonly Button _run = new() { Content = "▶ Run", Classes = { "tool" } };
    private readonly StateGraphView _graph = new() { MinHeight = 180 };
    private readonly StackPanel _inspector = new() { Spacing = 6, Margin = new Thickness(10, 8), MinWidth = 300 };
    private readonly StackPanel _parameters = new() { Spacing = 4 };

    // Bone map.
    private readonly ComboBox _mapModel = new() { MinWidth = 200, Classes = { "field" } };
    private readonly TextBlock _mapPreset = new() { FontSize = 12, VerticalAlignment = VerticalAlignment.Center };
    private readonly StackPanel _mapWarnings = new() { Spacing = 2 };
    private readonly WrapPanel _mapRows = new();  // compact rows, flowing into columns

    private JsonArray _characters = [];
    private JsonObject? _info;              // the shown character (from "characters")
    private JsonObject _controller = new(); // its controller, as last loaded or saved
    private string? _controllerFile;
    private string _mode = "clip";          // clip | run | state | transition
    private string? _clip;
    private double _time, _length = 1;
    private int _layerIndex;
    private string? _state;
    private int _transition = -1;
    private string? _highlight;
    private readonly JsonObject _values = new();  // the preview's parameter values
    private readonly DispatcherTimer _timer = new() { Interval = TimeSpan.FromMilliseconds(33) };
    private DateTime _last = DateTime.UtcNow;
    private bool _building;

    /// <summary>The engine's last preview answer (tests read it).</summary>
    public JsonObject? Preview { get; private set; }
    /// <summary>The model whose bone map is shown, as the engine last described it.</summary>
    public JsonObject? Model { get; private set; }
    public string? CharacterId => _info?["entity"]?.GetValue<string>();
    public bool Playing { get; private set; }
    public double Time => _time;
    public string Mode => _mode;
    /// <summary>Why the last controller edit wasn't saved (null: it was).</summary>
    public string? ControllerError { get; private set; }

    public AnimationPanel(EditorDocument doc)
    {
        _doc = doc;

        // Header: which character, and how it's shown.
        ToolTip.SetTip(_character, "The character previewed in the viewport (selecting one in the scene picks it too)");
        ToolTip.SetTip(_skeleton, "Draw the skeleton over the model");
        ToolTip.SetTip(_inPlace, "Hold the hips where they are horizontally, as root motion shows a walk");
        _character.SelectionChanged += (_, _) =>
        {
            if (_building || _character.SelectedIndex < 0 || _character.SelectedIndex >= _characters.Count) return;
            Show(_characters[_character.SelectedIndex] as JsonObject);
        };
        _skeleton.IsCheckedChanged += (_, _) => SendPreview();
        _inPlace.IsCheckedChanged += (_, _) => SendPreview();
        var header = new DockPanel { Margin = new Thickness(8, 6, 8, 4) };
        var switcher = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 2, Margin = new Thickness(8, 0) };
        string[] titles = ["Clips", "Controller", "Bone map"];
        for (var i = 0; i < 3; i++)
        {
            var index = i;
            _tabButtons[i] = new Button { Content = titles[i], Classes = { "tool" }, Padding = new Thickness(10, 3) };
            _tabButtons[i].Click += (_, _) => ShowTab(index, user: true);
            switcher.Children.Add(_tabButtons[i]);
        }
        var left = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, Children = { new TextBlock { Text = "Character", VerticalAlignment = VerticalAlignment.Center, FontSize = 12 }, _character, switcher, _skeleton, _inPlace } };
        SetDock(left, Avalonia.Controls.Dock.Left);
        header.Children.Add(left);
        _about.Margin = new Thickness(12, 0, 0, 0);
        header.Children.Add(_about);
        SetDock(header, Avalonia.Controls.Dock.Top);
        Children.Add(header);

        // Timeline.
        ToolTip.SetTip(_play, "Play or pause the preview (Space in the panel)");
        ToolTip.SetTip(_restart, "Back to the start");
        ToolTip.SetTip(_timeline, "Drag to scrub");
        _play.Click += (_, _) => SetPlaying(!Playing);
        _restart.Click += (_, _) => Scrub(0);
        _timeline.Scrubbed += t => Scrub(t);
        var bar = new DockPanel { Margin = new Thickness(8, 4, 8, 6) };
        var buttons = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 4, Children = { _restart, _play } };
        var right = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, Children = { _timeText, _speed, _loop, _modeText } };
        SetDock(buttons, Avalonia.Controls.Dock.Left);
        SetDock(right, Avalonia.Controls.Dock.Right);
        bar.Children.Add(buttons);
        bar.Children.Add(right);
        bar.Children.Add(_timeline);
        SetDock(bar, Avalonia.Controls.Dock.Bottom);
        Children.Add(bar);
        KeyDown += (_, e) =>
        {
            if (e.Key == Key.Space && e.Source is not TextBox) { SetPlaying(!Playing); e.Handled = true; }
        };

        // Pages.
        _pages[0] = ClipsTab();
        _pages[1] = ControllerTab();
        _pages[2] = BoneMapTab();
        foreach (var page in _pages) _pagesHost.Children.Add(page);
        ShowTab(0);
        Children.Add(new Panel { Children = { _pagesHost, _empty } });

        _timer.Tick += (_, _) => Tick();
        AttachedToVisualTree += (_, _) =>
        {
            _last = DateTime.UtcNow;
            _timer.Start();
            RefreshCharacters();
        };
        DetachedFromVisualTree += (_, _) =>
        {
            _timer.Stop();
            Playing = false;
            _doc.Animation(new JsonObject { ["op"] = "preview", ["entity"] = null }, quiet: true);  // the viewport back to the scene
        };
        _doc.SceneChanged += () => { if (Attached()) RefreshCharacters(); };
        _doc.SelectionChanged += FollowSelection;
    }

    private bool Attached() => TopLevel.GetTopLevel(this) != null;

    /// <summary>Shows a page: 0 clips, 1 controller, 2 bone map. Picked by hand, Clips previews the clip again and
    /// Controller runs the controller (if a clip was showing).</summary>
    private void ShowTab(int index, bool user = false)
    {
        _tab = Math.Clamp(index, 0, 2);
        for (var i = 0; i < 3; i++)
        {
            _pages[i].IsVisible = i == _tab;
            _tabButtons[i].Background = i == _tab ? NoorTheme.Brush(NoorTheme.AccentDeep) : Brushes.Transparent;
        }
        if (!user) return;
        if (_tab == 0 && _clip != null) SetMode("clip");
        else if (_tab == 1 && _mode == "clip" && _controllerFile != null) SetMode("run");
    }

    // ---- characters ---------------------------------------------------------------------------------------------------

    private void RefreshCharacters()
    {
        if (_doc.Playing || !_doc.Scene.Open) return;
        _characters = _doc.Animation(new JsonObject { ["op"] = "characters" }, quiet: true) as JsonArray ?? [];
        _building = true;
        _character.ItemsSource = _characters.OfType<JsonObject>().Select(c => c["name"]?.GetValue<string>() ?? "?").ToList();
        var id = CharacterId;
        var index = id == null ? -1 : _characters.OfType<JsonObject>().ToList().FindIndex(c => c["entity"]?.GetValue<string>() == id);
        _character.SelectedIndex = index;
        _building = false;
        _empty.IsVisible = _characters.Count == 0;
        _pagesHost.IsVisible = _characters.Count > 0;
        if (index >= 0)
        {
            // Same character: its animator may have changed (another controller, retarget_from).
            var fresh = (JsonObject)_characters[index]!;
            var changed = !JsonNode.DeepEquals(fresh["animator"], _info?["animator"]) || fresh["model"]?.GetValue<string>() != _info?["model"]?.GetValue<string>();
            _info = fresh;
            if (changed) Show(fresh);
            else SendPreview();
        }
        else if (_characters.Count > 0)
        {
            Show(FromSelection() ?? (JsonObject)_characters[0]!);
        }
        else
        {
            _info = null;
            Preview = null;
        }
    }

    /// <summary>The character the selected entity belongs to (itself or an ancestor), if any.</summary>
    private JsonObject? FromSelection()
    {
        var id = _doc.SelectedId;
        for (var depth = 0; id != null && depth < 64; depth++)
        {
            if (_characters.OfType<JsonObject>().FirstOrDefault(c => c["entity"]?.GetValue<string>() == id) is { } found) return found;
            id = _doc.Scene.Entities.FirstOrDefault(e => e.Id == id)?.Parent;
        }
        return null;
    }

    private void FollowSelection()
    {
        if (!Attached() || FromSelection() is not { } c || c["entity"]?.GetValue<string>() == CharacterId) return;
        Show(c);
    }

    /// <summary>Shows a character (by its model root's id); false if the scene has no such character.</summary>
    public bool ShowCharacter(string entityId)
    {
        RefreshCharacters();
        if (_characters.OfType<JsonObject>().FirstOrDefault(c => c["entity"]?.GetValue<string>() == entityId) is not { } c) return false;
        Show(c);
        return true;
    }

    /// <summary>Shows the first character whose animator runs this controller (Edit on a .nooranim).</summary>
    public bool ShowController(string controllerPath)
    {
        RefreshCharacters();
        var c = _characters.OfType<JsonObject>().FirstOrDefault(x => string.Equals(x["controller"]?.GetValue<string>(), controllerPath, StringComparison.OrdinalIgnoreCase));
        if (c == null) return false;
        Show(c);
        ShowTab(1);
        SetMode("run");
        return true;
    }

    private void Show(JsonObject? info)
    {
        _info = info;
        _building = true;
        var index = info == null ? -1 : _characters.IndexOf(info);
        if (_character.SelectedIndex != index) _character.SelectedIndex = index;
        _building = false;
        if (info == null) return;
        var model = info["model"]?.GetValue<string>() ?? "?";
        var from = info["retarget_from"]?.GetValue<string>();
        _about.Text = from != null ? $"{model}, playing {System.IO.Path.GetFileName(from)}'s clips (retargeted)" : model;
        // Clips.
        var clips = info["clips"] as JsonArray ?? [];
        _clips.ItemsSource = clips.OfType<JsonObject>().Select(c =>
            $"{c["name"]?.GetValue<string>()}    {Num(c["duration"]?.GetValue<double>() ?? 0)} s, {c["nodes"]?.GetValue<int>()} bones").ToList();
        var names = ClipNames();
        if (_clip == null || !names.Contains(_clip)) _clip = names.FirstOrDefault();
        _building = true;
        _clips.SelectedIndex = _clip == null ? -1 : names.IndexOf(_clip);
        _building = false;
        // Controller.
        _controllerFile = info["controller"]?.GetValue<string>();
        LoadController();
        // Bone map: this model, or the clips' model.
        _building = true;
        var maps = new List<string> { model };
        if (from != null) maps.Add(from);
        _mapModel.ItemsSource = maps;
        _mapModel.SelectedIndex = 0;
        _building = false;
        LoadModel(model);
        _time = 0;
        SetMode(_tab == 1 && _controllerFile != null ? "run" : "clip");
    }

    private List<string> ClipNames() => (_info?["clips"] as JsonArray ?? []).OfType<JsonObject>().Select(c => c["name"]?.GetValue<string>() ?? "").ToList();

    // ---- the preview ----------------------------------------------------------------------------------------------------

    private void SetMode(string mode)
    {
        _mode = mode;
        _time = 0;
        SendPreview();
        _graph.Invalidate();
    }

    private void SetPlaying(bool playing)
    {
        Playing = playing;
        _last = DateTime.UtcNow;
        if (playing && !(_loop.IsChecked ?? true) && _time >= _length - 1e-6) _time = 0;
        _play.Content = playing ? "⏸" : "▶";
    }

    private double SpeedFactor => _speed.SelectedIndex switch { 0 => 0.25, 1 => 0.5, 3 => 2.0, _ => 1.0 };

    private void Tick()
    {
        var now = DateTime.UtcNow;
        var dt = Math.Min(0.1, (now - _last).TotalSeconds);
        _last = now;
        if (!Playing || CharacterId == null) return;
        _time += dt * SpeedFactor;
        if (_time > _length)
        {
            if (_loop.IsChecked ?? true) _time = _length > 1e-6 ? _time % _length : 0;
            else
            {
                _time = _length;
                SetPlaying(false);
            }
        }
        SendPreview();
    }

    /// <summary>Plays a clip from its start (the Clips list does this).</summary>
    public void PlayClip(string name)
    {
        _clip = name;
        _building = true;
        _clips.SelectedIndex = ClipNames().IndexOf(name);
        ShowTab(0);
        _building = false;
        SetMode("clip");
        SetPlaying(true);
    }

    /// <summary>Stops playing and shows this moment (seconds into the clip, or since the controller started).</summary>
    public void Scrub(double seconds)
    {
        SetPlaying(false);
        _time = Math.Clamp(seconds, 0, Math.Max(_length, 0));
        SendPreview();
    }

    /// <summary>The controller running from its start with the panel's parameter values.</summary>
    public void RunController()
    {
        ShowTab(1);
        _state = null;
        _transition = -1;
        SetMode("run");
        _graph.Invalidate();
        ShowInspector();
    }

    private void SendPreview()
    {
        if (CharacterId is not { } id || _doc.Playing) return;
        var command = new JsonObject
        {
            ["op"] = "preview", ["entity"] = id, ["time"] = _time, ["skeleton"] = _skeleton.IsChecked ?? true, ["in_place"] = _inPlace.IsChecked ?? false,
            ["highlight"] = _highlight,
        };
        var layerName = LayerAt(_layerIndex)?["name"]?.GetValue<string>() ?? "";
        switch (_mode)
        {
            case "clip":
                command["clip"] = _clip ?? "";
                break;
            case "state":
                command["controller"] = true;
                command["layer"] = layerName;
                command["state"] = _state;
                command["transition"] = null;
                command["parameters"] = _values.DeepClone();
                break;
            case "transition":
                command["controller"] = true;
                command["layer"] = layerName;
                command["state"] = null;
                command["transition"] = _transition;
                command["parameters"] = _values.DeepClone();
                break;
            default:
                command["controller"] = true;
                command["state"] = null;
                command["transition"] = null;
                command["parameters"] = _values.DeepClone();
                break;
        }
        Preview = _doc.Animation(command, quiet: true) as JsonObject;
        if (Preview?["length"]?.GetValue<double>() is { } length && length > 0) _length = length;
        else if (_mode == "clip") _length = (_info?["clips"] as JsonArray ?? []).OfType<JsonObject>().FirstOrDefault(c => c["name"]?.GetValue<string>() == _clip)?["duration"]?.GetValue<double>() ?? 1;
        _timeline.Length = _length;
        _timeline.Time = _time;
        _timeline.Shade = _mode == "transition" && Preview?["lead"] is JsonValue lead && Preview?["fade"] is JsonValue fade
            ? (lead.GetValue<double>(), lead.GetValue<double>() + fade.GetValue<double>()) : null;
        _timeText.Text = $"{_time:0.00} / {_length:0.00} s";
        var states = Preview?["states"] as JsonArray;
        _modeText.Text = _mode switch
        {
            "clip" => _clip == null ? "rest pose" : $"clip {_clip}",
            "state" => $"state {_state}",
            "transition" => $"{Preview?["from"]?.GetValue<string>()} → {Preview?["to"]?.GetValue<string>()} ({Preview?["part"]?.GetValue<string>()})",
            _ => states == null ? "controller" : "controller: " + string.Join(", ", states.Select(s => s?.GetValue<string>())),
        };
        if (Preview?["error"]?.GetValue<string>() is { } error)
        {
            _modeText.Text = "⚠ " + error;
            _modeText.Foreground = NoorTheme.Brush(NoorTheme.Warn);
        }
        else
        {
            _modeText.Foreground = NoorTheme.Brush(NoorTheme.TextMuted);
        }
        var active = states != null && _layerIndex < states.Count ? states[_layerIndex]?.GetValue<string>() : null;
        var weights = Preview?["fade_weights"] as JsonArray;
        var weight = weights != null && _layerIndex < weights.Count ? weights[_layerIndex]?.GetValue<double>() ?? 1 : 1;
        _graph.SetActive(_mode == "clip" ? null : active, weight);
    }

    // ---- clips --------------------------------------------------------------------------------------------------------

    private Control ClipsTab()
    {
        _clips.SelectionChanged += (_, _) =>
        {
            if (_building) return;
            var names = ClipNames();
            if (_clips.SelectedIndex >= 0 && _clips.SelectedIndex < names.Count) PlayClip(names[_clips.SelectedIndex]);
        };
        var hint = new TextBlock
        {
            Text = "Pick a clip to play it on the character in the viewport. Drag on the timeline to scrub; Space plays and pauses.",
            FontSize = 11, Classes = { "muted" }, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(10, 6),
        };
        SetDock(hint, Avalonia.Controls.Dock.Top);
        return new DockPanel { Children = { hint, _clips } };
    }

    // ---- the controller -----------------------------------------------------------------------------------------------

    private JsonArray Layers() => _controller["layers"] as JsonArray ?? (JsonArray)(_controller["layers"] = new JsonArray());
    private JsonObject? LayerAt(int i) => i >= 0 && i < Layers().Count ? Layers()[i] as JsonObject : null;
    private JsonArray StatesOf(JsonObject layer) => layer["states"] as JsonArray ?? (JsonArray)(layer["states"] = new JsonArray());
    private JsonArray TransitionsOf(JsonObject layer) => layer["transitions"] as JsonArray ?? (JsonArray)(layer["transitions"] = new JsonArray());
    private static string NameOf(JsonNode? state) => state?["name"]?.GetValue<string>() ?? "";

    private Control ControllerTab()
    {
        ToolTip.SetTip(_run, "Run the controller from its start with the parameter values below, as a game would");
        _run.Click += (_, _) => RunController();
        _layer.SelectionChanged += (_, _) =>
        {
            if (_building || _layer.SelectedIndex < 0) return;
            _layerIndex = _layer.SelectedIndex;
            _state = null;
            _transition = -1;
            ShowGraph();
            ShowInspector();
            SendPreview();
        };
        _graph.StateClicked += name => SelectState(name);
        _graph.TransitionClicked += index => SelectTransition(_layerIndex, index);
        var addState = new Button { Content = "+ State", Classes = { "tool" } };
        addState.Click += (_, _) => AddState();
        var top = new StackPanel
        {
            Orientation = Orientation.Horizontal, Spacing = 8, Margin = new Thickness(10, 6),
            Children = { new TextBlock { Text = "Layer", VerticalAlignment = VerticalAlignment.Center, FontSize = 12 }, _layer, _run, addState },
        };
        var head = new StackPanel { Children = { top, _controllerPath, _controllerError } };
        _controllerPath.Margin = new Thickness(10, 0);
        _controllerError.Margin = new Thickness(10, 0);
        SetDock(head, Avalonia.Controls.Dock.Top);
        var graphHost = new Border
        {
            Child = _graph, Background = NoorTheme.Brush(NoorTheme.Background), CornerRadius = new CornerRadius(6),
            Margin = new Thickness(10, 4, 4, 8), BorderBrush = NoorTheme.Brush(NoorTheme.Border), BorderThickness = new Thickness(1),
        };
        var side = new ScrollViewer { Content = _inspector, Background = NoorTheme.Brush(NoorTheme.Panel) };
        var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto") };
        Grid.SetColumn(side, 1);
        grid.Children.Add(graphHost);
        grid.Children.Add(side);
        return new DockPanel { Children = { head, grid } };
    }

    private void LoadController()
    {
        ControllerError = null;
        _controllerError.Text = "";
        _controller = new JsonObject();
        if (_controllerFile != null && _doc.Animation(new JsonObject { ["op"] = "controller", ["asset"] = _controllerFile }) is JsonObject file)
        {
            _controller = file["controller"]?.DeepClone() as JsonObject ?? new JsonObject();
            if (file["error"]?.GetValue<string>() is { } error) _controllerError.Text = "⚠ " + error;
        }
        _controllerPath.Text = _controllerFile ?? "Its animator has no controller: add one in the Inspector (\"controller\": a .nooranim) to edit it here.";
        _run.IsEnabled = _controllerFile != null;
        // The preview's parameter values start at the animator's, over the controller's defaults.
        _values.Clear();
        if (_controller["parameters"] is JsonObject ps)
            foreach (var (name, p) in ps)
            {
                if (p?["type"]?.GetValue<string>() is "bool" or "trigger") _values[name] = p["default"]?.GetValue<bool>() ?? false;
                else _values[name] = p?["default"]?.GetValue<double>() ?? 0.0;
            }
        if (_info?["animator"]?["parameters"] is JsonObject given)
            foreach (var (name, v) in given) _values[name] = v?.DeepClone();
        foreach (var (name, p) in (_controller["parameters"] as JsonObject)?.ToList() ?? [])
            if (p?["type"]?.GetValue<string>() == "trigger") _values[name] = false;
        _building = true;
        _layer.ItemsSource = Layers().OfType<JsonObject>().Select(l => l["name"]?.GetValue<string>() ?? "?").ToList();
        _layerIndex = Math.Clamp(_layerIndex, 0, Math.Max(0, Layers().Count - 1));
        _layer.SelectedIndex = Layers().Count > 0 ? _layerIndex : -1;
        _building = false;
        if (_state != null && LayerAt(_layerIndex) is { } l && StatesOf(l).All(s => NameOf(s) != _state)) _state = null;
        if (_transition >= 0 && (LayerAt(_layerIndex) is not { } l2 || _transition >= TransitionsOf(l2).Count)) _transition = -1;
        ShowGraph();
        ShowInspector();
    }

    private void ShowGraph() => _graph.Show(LayerAt(_layerIndex), _state, _transition);

    /// <summary>Writes the controller; a version the engine refuses isn't kept (the file is read back).</summary>
    private bool SaveController()
    {
        if (_controllerFile == null) return false;
        var saved = _doc.Animation(new JsonObject { ["op"] = "save_controller", ["asset"] = _controllerFile, ["controller"] = _controller.DeepClone() }, quiet: true);
        if (saved == null)
        {
            var why = Native.LastError();
            EngineLog.Warn("Animation", $"Not saved: {why}");
            LoadController();
            ControllerError = why;
            _controllerError.Text = "⚠ Not saved: " + why;
            SendPreview();
            return false;
        }
        ControllerError = null;
        _controllerError.Text = "";
        ShowGraph();
        SendPreview();
        return true;
    }

    public void SelectState(string name)
    {
        ShowTab(1);
        _state = name;
        _transition = -1;
        ShowGraph();
        ShowInspector();
        SetMode("state");
        SetPlaying(true);
    }

    /// <summary>Previews one transition of a layer (from the start of its lead-in) and shows its fields.</summary>
    public void SelectTransition(int layer, int index)
    {
        ShowTab(1);
        if (layer != _layerIndex && layer >= 0 && layer < Layers().Count)
        {
            _layerIndex = layer;
            _building = true;
            _layer.SelectedIndex = layer;
            _building = false;
        }
        _state = null;
        _transition = index;
        ShowGraph();
        ShowInspector();
        SetMode("transition");
        SetPlaying(true);
    }

    /// <summary>Sets the selected transition's fade (seconds) and saves; false if nothing's selected or it's refused.</summary>
    public bool SetTransitionFade(double seconds)
    {
        if (SelectedTransition() is not { } t) return false;
        t["fade"] = Math.Max(0, seconds);
        var ok = SaveController();
        ShowInspector();
        return ok;
    }

    private JsonObject? SelectedTransition() =>
        LayerAt(_layerIndex) is { } l && _transition >= 0 && _transition < TransitionsOf(l).Count ? TransitionsOf(l)[_transition] as JsonObject : null;

    private void ShowInspector()
    {
        _inspector.Children.Clear();
        var layer = LayerAt(_layerIndex);
        if (layer != null && SelectedTransition() is { } t) TransitionFields(layer, t);
        else if (layer != null && _state != null && StatesOf(layer).OfType<JsonObject>().FirstOrDefault(s => NameOf(s) == _state) is { } s) StateFields(layer, s);
        else
        {
            _inspector.Children.Add(new TextBlock { Text = "Controller", Classes = { "section" } });
            _inspector.Children.Add(new TextBlock
            {
                Text = layer == null ? "No layers." : "Click a state or a transition (an arrow) to preview and edit it, or Run to play the whole controller.",
                FontSize = 11, Classes = { "muted" }, TextWrapping = TextWrapping.Wrap, MaxWidth = 300,
            });
        }
        ParameterFields();
    }

    private void StateFields(JsonObject layer, JsonObject state)
    {
        _inspector.Children.Add(new TextBlock { Text = "State", Classes = { "section" } });
        var name = new TextBox { Text = NameOf(state), Classes = { "field" } };
        void Rename()
        {
            var old = NameOf(state);
            var now = name.Text?.Trim() ?? "";
            if (now.Length == 0 || now == old || StatesOf(layer).Any(s => NameOf(s) == now)) { name.Text = old; return; }
            state["name"] = now;
            foreach (var tr in TransitionsOf(layer).OfType<JsonObject>())
            {
                if (tr["from"]?.GetValue<string>() == old) tr["from"] = now;
                if (tr["to"]?.GetValue<string>() == old) tr["to"] = now;
            }
            if (layer["entry"]?.GetValue<string>() == old) layer["entry"] = now;
            _state = now;
            SaveController();
        }
        name.LostFocus += (_, _) => Rename();
        name.KeyDown += (_, e) => { if (e.Key == Key.Enter) Rename(); };
        _inspector.Children.Add(Row("Name", name));
        if (state["blend"] is JsonObject blend)
        {
            var samples = (blend["samples"] as JsonArray ?? []).OfType<JsonObject>()
                .Select(x => $"{x["clip"]?.GetValue<string>()} at {Num(x["x"]?.GetValue<double>() ?? 0)}" + (x["y"] is JsonValue y ? $", {Num(y.GetValue<double>())}" : ""));
            var axes = blend["y"] != null ? $"{blend["x"]} and {blend["y"]}" : $"{blend["x"]}";
            _inspector.Children.Add(new TextBlock
            {
                Text = $"A blend space on {axes}: {string.Join("; ", samples)}. (Its samples are edited in the .nooranim for now.)",
                FontSize = 11, TextWrapping = TextWrapping.Wrap, MaxWidth = 300,
            });
        }
        else
        {
            var clips = new List<string> { "(empty)" };
            clips.AddRange(ClipNames());
            var clip = new ComboBox { ItemsSource = clips, Classes = { "field" }, HorizontalAlignment = HorizontalAlignment.Stretch };
            clip.SelectedIndex = Math.Max(0, clips.IndexOf(state["clip"]?.GetValue<string>() ?? "(empty)"));
            ToolTip.SetTip(clip, "The clip this state plays (empty: an override layer passes what's below through)");
            clip.SelectionChanged += (_, _) =>
            {
                if (clip.SelectedIndex <= 0) state.Remove("clip");
                else state["clip"] = clips[clip.SelectedIndex];
                SaveController();
            };
            _inspector.Children.Add(Row("Clip", clip));
        }
        var loop = new CheckBox { Content = "Loops", IsChecked = state["loop"]?.GetValue<bool>() ?? true };
        loop.IsCheckedChanged += (_, _) => { state["loop"] = loop.IsChecked ?? true; SaveController(); };
        _inspector.Children.Add(Row("", loop));
        var speed = new TextBox { Text = state["speed"]?.ToString() ?? "1", Classes = { "field" } };
        ToolTip.SetTip(speed, "1 = as authored; or a parameter's name (multiplies)");
        void SetSpeed()
        {
            var text = speed.Text?.Trim() ?? "";
            if (double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out var x)) state["speed"] = x;
            else if (text.Length > 0) state["speed"] = text;
            else state.Remove("speed");
            SaveController();
        }
        speed.LostFocus += (_, _) => SetSpeed();
        speed.KeyDown += (_, e) => { if (e.Key == Key.Enter) SetSpeed(); };
        _inspector.Children.Add(Row("Speed", speed));
        var entry = new Button { Content = layer["entry"]?.GetValue<string>() == NameOf(state) ? "The entry state" : "Make it the entry state", Classes = { "tool" } };
        entry.IsEnabled = layer["entry"]?.GetValue<string>() != NameOf(state);
        entry.Click += (_, _) => { layer["entry"] = NameOf(state); SaveController(); ShowInspector(); };
        var transition = new Button { Content = "+ Transition from here", Classes = { "tool" } };
        transition.Click += (_, _) => AddTransition(layer, NameOf(state));
        var delete = new Button { Content = "Delete state", Classes = { "tool" } };
        delete.IsEnabled = StatesOf(layer).Count > 1;
        delete.Click += (_, _) => DeleteState(layer, NameOf(state));
        _inspector.Children.Add(new WrapPanel { Children = { entry, transition, delete } });
    }

    private static readonly string[] Ops = ["is on", ">", "<", "=", "≠"];
    private static readonly string?[] OpKeys = [null, "greater", "less", "equals", "not_equals"];

    private void TransitionFields(JsonObject layer, JsonObject t)
    {
        _inspector.Children.Add(new TextBlock { Text = "Transition", Classes = { "section" } });
        var names = StatesOf(layer).Select(NameOf).ToList();
        var froms = new List<string> { "* (any state)" };
        froms.AddRange(names);
        var from = new ComboBox { ItemsSource = froms, Classes = { "field" }, HorizontalAlignment = HorizontalAlignment.Stretch };
        var f = t["from"]?.GetValue<string>() ?? "*";
        from.SelectedIndex = f == "*" ? 0 : Math.Max(0, froms.IndexOf(f));
        from.SelectionChanged += (_, _) => { t["from"] = from.SelectedIndex <= 0 ? "*" : froms[from.SelectedIndex]; SaveController(); };
        _inspector.Children.Add(Row("From", from));
        var to = new ComboBox { ItemsSource = names, Classes = { "field" }, HorizontalAlignment = HorizontalAlignment.Stretch };
        to.SelectedIndex = Math.Max(0, names.IndexOf(t["to"]?.GetValue<string>() ?? ""));
        to.SelectionChanged += (_, _) => { if (to.SelectedIndex >= 0) { t["to"] = names[to.SelectedIndex]; SaveController(); } };
        _inspector.Children.Add(Row("To", to));
        var fade = NumberBox(t["fade"]?.GetValue<double>() ?? 0.2, x => SetTransitionFade(x));
        ToolTip.SetTip(fade, "How long the cross-fade lasts, in seconds (shaded on the timeline)");
        _inspector.Children.Add(Row("Fade (s)", fade));
        var hasExit = t["exit_time"] != null;
        var exit = new CheckBox { Content = "Wait until through", IsChecked = hasExit };
        var exitAt = NumberBox(t["exit_time"]?.GetValue<double>() ?? 1, x => { t["exit_time"] = Math.Max(0, x); SaveController(); });
        exitAt.IsEnabled = hasExit;
        ToolTip.SetTip(exitAt, "How far through \"From\" it must be: 1 = once through");
        exit.IsCheckedChanged += (_, _) =>
        {
            if (exit.IsChecked ?? false) t["exit_time"] = 1.0;
            else t.Remove("exit_time");
            SaveController();
            ShowInspector();
        };
        _inspector.Children.Add(Row("Exit time", new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6, Children = { exit, exitAt } }));

        _inspector.Children.Add(new TextBlock { Text = "When (all of)", FontSize = 12, FontWeight = FontWeight.SemiBold, Margin = new Thickness(0, 4, 0, 0) });
        var when = t["when"] as JsonArray ?? (JsonArray)(t["when"] = new JsonArray());
        var parameters = ((_controller["parameters"] as JsonObject)?.Select(p => p.Key) ?? []).ToList();
        for (var i = 0; i < when.Count; i++)
        {
            if (when[i] is not JsonObject c) continue;
            var index = i;
            var p = new ComboBox { ItemsSource = parameters, Classes = { "field" }, MinWidth = 90 };
            p.SelectedIndex = parameters.IndexOf(c["parameter"]?.GetValue<string>() ?? "");
            p.SelectionChanged += (_, _) => { if (p.SelectedIndex >= 0) { c["parameter"] = parameters[p.SelectedIndex]; SaveController(); } };
            var opIndex = Array.FindIndex(OpKeys, k => k != null && c.ContainsKey(k));
            var op = new ComboBox { ItemsSource = Ops, SelectedIndex = Math.Max(0, opIndex), Classes = { "field" } };
            var value = new TextBox { Text = opIndex > 0 ? c[OpKeys[opIndex]!]?.ToString() ?? "0" : "", Classes = { "field" }, MinWidth = 50, IsEnabled = opIndex > 0 };
            void Store()
            {
                foreach (var k in OpKeys) if (k != null) c.Remove(k);
                if (op.SelectedIndex > 0)
                {
                    var text = value.Text?.Trim() ?? "0";
                    c[OpKeys[op.SelectedIndex]!] = text is "true" or "false" ? JsonValue.Create(text == "true")
                        : JsonValue.Create(double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out var x) ? x : 0.0);
                }
                SaveController();
            }
            op.SelectionChanged += (_, _) => { value.IsEnabled = op.SelectedIndex > 0; if (op.SelectedIndex > 0 && string.IsNullOrEmpty(value.Text)) value.Text = "0"; Store(); };
            value.LostFocus += (_, _) => Store();
            value.KeyDown += (_, e) => { if (e.Key == Key.Enter) Store(); };
            var remove = new Button { Content = "✕", Classes = { "tool" } };
            remove.Click += (_, _) => { when.RemoveAt(index); SaveController(); ShowInspector(); };
            _inspector.Children.Add(new StackPanel { Orientation = Orientation.Horizontal, Spacing = 4, Children = { p, op, value, remove } });
        }
        if (when.Count == 0)
            _inspector.Children.Add(new TextBlock { Text = t["exit_time"] != null ? "Nothing else: it goes at its exit time." : "No conditions: it goes at once.", FontSize = 11, Classes = { "muted" } });
        var add = new Button { Content = "+ Condition", Classes = { "tool" }, IsEnabled = parameters.Count > 0 };
        add.Click += (_, _) => { when.Add(new JsonObject { ["parameter"] = parameters[0] }); SaveController(); ShowInspector(); };
        var delete = new Button { Content = "Delete transition", Classes = { "tool" } };
        delete.Click += (_, _) =>
        {
            TransitionsOf(layer).RemoveAt(_transition);
            _transition = -1;
            SaveController();
            ShowInspector();
            SetMode("run");
        };
        _inspector.Children.Add(new WrapPanel { Children = { add, delete } });
    }

    private void ParameterFields()
    {
        if (_controller["parameters"] is not JsonObject ps || ps.Count == 0) return;
        _inspector.Children.Add(new TextBlock { Text = "Preview parameters", Classes = { "section" }, Margin = new Thickness(0, 10, 0, 0) });
        _inspector.Children.Add(new TextBlock { Text = "What the preview runs with (the scene keeps its own).", FontSize = 11, Classes = { "muted" } });
        foreach (var (name, p) in ps)
        {
            var type = p?["type"]?.GetValue<string>() ?? "float";
            Control editor;
            if (type == "float")
            {
                editor = NumberBox(_values[name]?.GetValue<double>() ?? 0, x => { _values[name] = x; SendPreview(); });
            }
            else if (type == "bool")
            {
                var box = new CheckBox { IsChecked = _values[name]?.GetValue<bool>() ?? false };
                box.IsCheckedChanged += (_, _) => { _values[name] = box.IsChecked ?? false; SendPreview(); };
                editor = box;
            }
            else
            {
                var fire = new Button { Content = "Fire", Classes = { "tool" } };
                ToolTip.SetTip(fire, "Run the controller from its start with this trigger set (it fires once)");
                fire.Click += (_, _) =>
                {
                    _values[name] = true;
                    RunController();
                    SetPlaying(true);
                    _values[name] = false;  // the next run starts without it
                };
                editor = fire;
            }
            _inspector.Children.Add(Row($"{name} ({type})", editor));
        }
    }

    private void AddState()
    {
        if (LayerAt(_layerIndex) is not { } layer) return;
        var n = 1;
        while (StatesOf(layer).Any(s => NameOf(s) == $"State {n}")) n++;
        var state = new JsonObject { ["name"] = $"State {n}" };
        if (ClipNames().FirstOrDefault() is { } clip) state["clip"] = clip;
        StatesOf(layer).Add(state);
        if (SaveController()) SelectState($"State {n}");
    }

    private void DeleteState(JsonObject layer, string name)
    {
        var states = StatesOf(layer);
        if (states.Count <= 1) return;
        var index = states.Select(NameOf).ToList().IndexOf(name);
        if (index < 0) return;
        states.RemoveAt(index);
        var transitions = TransitionsOf(layer);
        for (var i = transitions.Count - 1; i >= 0; i--)
            if (transitions[i]?["from"]?.GetValue<string>() == name || transitions[i]?["to"]?.GetValue<string>() == name) transitions.RemoveAt(i);
        if (layer["entry"]?.GetValue<string>() == name) layer["entry"] = NameOf(states[0]);
        _state = null;
        SaveController();
        ShowInspector();
        SetMode("run");
    }

    private void AddTransition(JsonObject layer, string from)
    {
        var to = StatesOf(layer).Select(NameOf).FirstOrDefault(n => n != from);
        if (to == null) return;
        TransitionsOf(layer).Add(new JsonObject { ["from"] = from, ["to"] = to, ["fade"] = 0.2 });
        if (SaveController()) SelectTransition(_layerIndex, TransitionsOf(layer).Count - 1);
    }

    // ---- the bone map -------------------------------------------------------------------------------------------------

    private Control BoneMapTab()
    {
        ToolTip.SetTip(_mapModel, "Whose map: this character's model, or the model its clips come from (retargeting)");
        _mapModel.SelectionChanged += (_, _) =>
        {
            if (_building || _mapModel.SelectedItem is not string path) return;
            LoadModel(path);
        };
        var detect = new Button { Content = "Detect all again", Classes = { "tool" } };
        ToolTip.SetTip(detect, "Forget every correction: the map as detected from the bone names");
        detect.Click += (_, _) =>
        {
            if (Model?["path"]?.GetValue<string>() is not { } path) return;
            if (_doc.Animation(new JsonObject { ["op"] = "set_bone", ["asset"] = path, ["bone"] = "*", ["auto"] = true }) is JsonObject m) ShowModel(m);
            RefreshAfterMap();
        };
        var top = new StackPanel
        {
            Spacing = 6, Margin = new Thickness(10, 6),
            Children =
            {
                new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, Children = { new TextBlock { Text = "Model", VerticalAlignment = VerticalAlignment.Center, FontSize = 12 }, _mapModel, _mapPreset, detect } },
                _mapWarnings,
                new TextBlock { Text = "Which bone is which, for retargeting clips between characters. Pick a row to see the bone in the viewport; a change is saved in the model's .noormeta.",
                                FontSize = 11, Classes = { "muted" }, TextWrapping = TextWrapping.Wrap },
            },
        };
        SetDock(top, Avalonia.Controls.Dock.Top);
        return new DockPanel { Children = { top, new ScrollViewer { Content = _mapRows, Margin = new Thickness(10, 0, 10, 8) } } };
    }

    private void LoadModel(string path)
    {
        if (_doc.Animation(new JsonObject { ["op"] = "model", ["asset"] = path }) is JsonObject m) ShowModel(m);
    }

    private void ShowModel(JsonObject model)
    {
        Model = model;
        var map = model["bone_map"] as JsonObject;
        var usable = map?["usable"]?.GetValue<bool>() ?? false;
        _mapPreset.Text = $"{(usable ? "✓" : "⚠")} {map?["preset"]?.GetValue<string>()}";
        _mapPreset.Foreground = NoorTheme.Brush(usable ? NoorTheme.Good : NoorTheme.Warn);
        _mapWarnings.Children.Clear();
        foreach (var w in (map?["warnings"] as JsonArray ?? []).OfType<JsonValue>())
            _mapWarnings.Children.Add(new TextBlock { Text = "⚠ " + w.GetValue<string>(), FontSize = 11, Foreground = NoorTheme.Brush(NoorTheme.Warn), TextWrapping = TextWrapping.Wrap });
        var nodes = (model["nodes"] as JsonArray ?? []).OfType<JsonObject>().ToList();
        var joints = nodes.Where(n => n["joint"]?.GetValue<bool>() ?? false).Select(n => n["name"]!.GetValue<string>()).ToList();
        if (joints.Count == 0) joints = nodes.Select(n => n["name"]!.GetValue<string>()).ToList();
        var choices = new List<string> { "(none)" };
        choices.AddRange(joints);
        _mapRows.Children.Clear();
        foreach (var b in (map?["bones"] as JsonArray ?? []).OfType<JsonObject>())
        {
            var bone = b["bone"]!.GetValue<string>();
            var node = b["node"]?.GetValue<string>();
            var required = b["required"]?.GetValue<bool>() ?? false;
            var corrected = b["corrected"]?.GetValue<bool>() ?? false;
            var label = new TextBlock
            {
                Text = bone + (required ? "  •" : ""), FontSize = 12, VerticalAlignment = VerticalAlignment.Center,
                FontWeight = required ? FontWeight.SemiBold : FontWeight.Normal,
                Foreground = NoorTheme.Brush(node == null && required ? NoorTheme.Warn : NoorTheme.Text),
            };
            if (required) ToolTip.SetTip(label, "Required: retargeting needs it");
            var pick = new ComboBox { ItemsSource = choices, Classes = { "field" }, HorizontalAlignment = HorizontalAlignment.Stretch, FontSize = 12, MinHeight = 26, Padding = new Thickness(6, 2) };
            pick.SelectedIndex = node == null ? 0 : Math.Max(0, choices.IndexOf(node));
            pick.SelectionChanged += (_, _) =>
            {
                if (_building || pick.SelectedIndex < 0) return;
                var chosen = pick.SelectedIndex == 0 ? null : choices[pick.SelectedIndex];
                if (chosen == node) return;
                SetBone(bone, chosen);
            };
            pick.GotFocus += (_, _) => HighlightBone(pick.SelectedIndex > 0 ? choices[pick.SelectedIndex] : null);
            var see = new Button { Content = "◎", Classes = { "tool" }, IsEnabled = node != null };
            ToolTip.SetTip(see, "Show this bone in the viewport");
            see.Click += (_, _) => HighlightBone(node);
            var reset = new Button { Content = "↺", Classes = { "tool" }, IsVisible = corrected };
            ToolTip.SetTip(reset, "Corrected by hand: back to the detected bone");
            reset.Click += (_, _) => SetBone(bone, null, auto: true);
            var line = new Grid { ColumnDefinitions = new ColumnDefinitions("132,190,Auto,Auto"), Width = 400, Margin = new Thickness(0, 1, 16, 1) };
            Grid.SetColumn(pick, 1);
            Grid.SetColumn(see, 2);
            Grid.SetColumn(reset, 3);
            line.Children.Add(label);
            line.Children.Add(pick);
            line.Children.Add(see);
            line.Children.Add(reset);
            _mapRows.Children.Add(line);
        }
    }

    /// <summary>Corrects one humanoid bone of the shown model's map (node null: none; auto: as detected).</summary>
    public JsonObject? SetBone(string bone, string? node, bool auto = false)
    {
        if (Model?["path"]?.GetValue<string>() is not { } path) return null;
        var command = new JsonObject { ["op"] = "set_bone", ["asset"] = path, ["bone"] = bone };
        if (auto) command["auto"] = true;
        else command["node"] = node;
        var model = _doc.Animation(command) as JsonObject;
        if (model != null)
        {
            ShowModel(model);
            if (!auto && node != null) HighlightBone(node);
        }
        RefreshAfterMap();
        return model;
    }

    /// <summary>Lights a bone up in the viewport (null: none).</summary>
    public void HighlightBone(string? node)
    {
        _highlight = node;
        SendPreview();
    }

    private void RefreshAfterMap()
    {
        // Retargeted clips are baked again from the new map: their list (and the preview) may change.
        RefreshCharacters();
    }

    /// <summary>Selects a tab: 0 clips, 1 controller, 2 bone map (tests).</summary>
    public void SelectTab(int index) => ShowTab(index, user: true);

    // ---- small things -------------------------------------------------------------------------------------------------

    private static string Num(double v) => v.ToString("0.###", CultureInfo.InvariantCulture);

    private static TextBox NumberBox(double value, Action<double> set)
    {
        var box = new TextBox { Text = value.ToString(CultureInfo.InvariantCulture), Classes = { "field" }, MinWidth = 60 };
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
        var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("110,*") };
        var text = new TextBlock { Text = label, FontSize = 12, VerticalAlignment = VerticalAlignment.Center, TextWrapping = TextWrapping.Wrap };
        Grid.SetColumn(editor, 1);
        grid.Children.Add(text);
        grid.Children.Add(editor);
        return grid;
    }
}

/// <summary>
/// The timeline: a track with second (and tenth) ticks, the playhead, and for a transition its fade shaded. Pressing or
/// dragging on it scrubs.
/// </summary>
internal sealed class TimelineView : Control
{
    private double _length = 1, _time;
    private (double From, double To)? _shade;
    public event Action<double>? Scrubbed;

    public double Length { get => _length; set { if (Math.Abs(_length - value) > 1e-9) { _length = Math.Max(1e-3, value); InvalidateVisual(); } } }
    public double Time { get => _time; set { if (Math.Abs(_time - value) > 1e-9) { _time = value; InvalidateVisual(); } } }
    public (double From, double To)? Shade { get => _shade; set { if (_shade != value) { _shade = value; InvalidateVisual(); } } }

    public TimelineView()
    {
        ClipToBounds = true;
        Cursor = new Cursor(StandardCursorType.Hand);
    }

    private double X(double t) => 6 + (Bounds.Width - 12) * Math.Clamp(t / _length, 0, 1);

    public override void Render(DrawingContext context)
    {
        var w = Bounds.Width;
        var h = Bounds.Height;
        context.DrawRectangle(NoorTheme.Brush(NoorTheme.PanelRaised), null, new Rect(0, 4, w, h - 8), 4, 4);
        if (_shade is { } s)
            context.DrawRectangle(new SolidColorBrush(NoorTheme.Accent, 0.35), null, new Rect(X(s.From), 4, Math.Max(1, X(s.To) - X(s.From)), h - 8));
        var tick = new Pen(NoorTheme.Brush(NoorTheme.Border), 1);
        var major = new Pen(NoorTheme.Brush(NoorTheme.TextMuted), 1);
        var step = _length > 20 ? 5.0 : _length > 6 ? 1.0 : _length > 1.5 ? 0.5 : 0.1;
        for (var t = 0.0; t <= _length + 1e-9; t += step)
        {
            var whole = Math.Abs(t - Math.Round(t)) < 1e-6;
            var x = Math.Round(X(t)) + 0.5;
            context.DrawLine(whole ? major : tick, new Point(x, h - 6), new Point(x, h - (whole ? 16 : 11)));
            if (whole)
            {
                var label = new FormattedText($"{t:0}", CultureInfo.InvariantCulture, FlowDirection.LeftToRight, Typeface.Default, 9, NoorTheme.Brush(NoorTheme.TextMuted));
                context.DrawText(label, new Point(x + 2, 5));
            }
        }
        var px = Math.Round(X(_time)) + 0.5;
        context.DrawLine(new Pen(NoorTheme.Brush(NoorTheme.Cyan), 2), new Point(px, 2), new Point(px, h - 2));
    }

    protected override void OnPointerPressed(PointerPressedEventArgs e)
    {
        base.OnPointerPressed(e);
        e.Pointer.Capture(this);
        ScrubTo(e.GetPosition(this).X);
    }

    protected override void OnPointerMoved(PointerEventArgs e)
    {
        base.OnPointerMoved(e);
        if (e.Pointer.Captured == this) ScrubTo(e.GetPosition(this).X);
    }

    protected override void OnPointerReleased(PointerReleasedEventArgs e)
    {
        base.OnPointerReleased(e);
        e.Pointer.Capture(null);
    }

    private void ScrubTo(double x) => Scrubbed?.Invoke(Math.Clamp((x - 6) / Math.Max(1, Bounds.Width - 12), 0, 1) * _length);
}

/// <summary>
/// A controller layer's states as boxes around an ellipse and its transitions as arrows (a pair going both ways side by
/// side; "*" transitions from an "Any" box). The entry state has a green edge, the state the preview is in is filled
/// (with how far its fade is), and the picked state or transition is outlined. Click one to pick it.
/// </summary>
internal sealed class StateGraphView : Control
{
    private JsonObject? _layer;
    private string? _selectedState;
    private int _selectedTransition = -1;
    private string? _active;
    private double _weight = 1;
    private readonly Dictionary<string, Rect> _boxes = new();
    private readonly List<(int Index, Point A, Point B)> _arrows = new();
    private const string Any = "\u0001any";

    public event Action<string>? StateClicked;
    public event Action<int>? TransitionClicked;

    public StateGraphView()
    {
        ClipToBounds = true;
        SizeChanged += (_, _) => InvalidateVisual();
    }

    public void Show(JsonObject? layer, string? selectedState, int selectedTransition)
    {
        _layer = layer;
        _selectedState = selectedState;
        _selectedTransition = selectedTransition;
        InvalidateVisual();
    }

    public void SetActive(string? state, double weight)
    {
        if (_active == state && Math.Abs(_weight - weight) < 1e-3) return;
        _active = state;
        _weight = weight;
        InvalidateVisual();
    }

    public void Invalidate() => InvalidateVisual();

    private void Layout(List<string> names, bool any)
    {
        _boxes.Clear();
        var w = Bounds.Width;
        var h = Bounds.Height;
        const double bw = 112, bh = 34;
        var cx = w / 2;
        var cy = h / 2 + (any ? 10 : 0);
        var rx = Math.Max(0, w / 2 - bw / 2 - 16);
        var ry = Math.Max(0, h / 2 - bh / 2 - (any ? 26 : 12));
        for (var i = 0; i < names.Count; i++)
        {
            double x = cx, y = cy;
            if (names.Count == 2) x = cx + (i == 0 ? -1 : 1) * Math.Min(rx, 150);
            else if (names.Count > 2)
            {
                var a = Math.PI + 2 * Math.PI * i / names.Count;  // the first on the left, then clockwise
                x = cx + rx * Math.Cos(a);
                y = cy + ry * Math.Sin(a);
            }
            _boxes[names[i]] = new Rect(x - bw / 2, y - bh / 2, bw, bh);
        }
        if (any) _boxes[Any] = new Rect(10, 8, 70, 24);
    }

    private static Point Edge(Rect r, Point toward)
    {
        // Where the line from the box's centre towards `toward` leaves the box.
        var c = r.Center;
        var dx = toward.X - c.X;
        var dy = toward.Y - c.Y;
        if (Math.Abs(dx) < 1e-9 && Math.Abs(dy) < 1e-9) return c;
        var sx = Math.Abs(dx) > 1e-9 ? r.Width / 2 / Math.Abs(dx) : double.MaxValue;
        var sy = Math.Abs(dy) > 1e-9 ? r.Height / 2 / Math.Abs(dy) : double.MaxValue;
        var s = Math.Min(sx, sy);
        return new Point(c.X + dx * s, c.Y + dy * s);
    }

    public override void Render(DrawingContext context)
    {
        _arrows.Clear();
        if (_layer == null) return;
        var states = (_layer["states"] as JsonArray ?? []).Select(s => s?["name"]?.GetValue<string>() ?? "").ToList();
        var transitions = (_layer["transitions"] as JsonArray ?? []).OfType<JsonObject>().ToList();
        var any = transitions.Any(t => (t["from"]?.GetValue<string>() ?? "*") == "*");
        Layout(states, any);
        var entry = _layer["entry"]?.GetValue<string>() ?? states.FirstOrDefault();

        // Arrows first (under the boxes).
        var pairs = new HashSet<(string, string)>();
        foreach (var t in transitions) pairs.Add((t["from"]?.GetValue<string>() ?? "*", t["to"]?.GetValue<string>() ?? ""));
        for (var i = 0; i < transitions.Count; i++)
        {
            var from = transitions[i]["from"]?.GetValue<string>() ?? "*";
            var to = transitions[i]["to"]?.GetValue<string>() ?? "";
            if (!_boxes.TryGetValue(from == "*" ? Any : from, out var a) || !_boxes.TryGetValue(to, out var b)) continue;
            var pa = Edge(a, b.Center);
            var pb = Edge(b, a.Center);
            if (pairs.Contains((to, from)))
            {
                // Both ways: each to its own side.
                var dx = pb.X - pa.X;
                var dy = pb.Y - pa.Y;
                var len = Math.Max(1e-6, Math.Sqrt(dx * dx + dy * dy));
                var off = new Vector(-dy / len * 6, dx / len * 6);
                pa += off;
                pb += off;
            }
            var picked = i == _selectedTransition;
            var color = picked ? NoorTheme.Accent : NoorTheme.TextMuted;
            var pen = new Pen(NoorTheme.Brush(color), picked ? 2.5 : 1.4);
            context.DrawLine(pen, pa, pb);
            // The head.
            var ux = pb.X - pa.X;
            var uy = pb.Y - pa.Y;
            var ul = Math.Max(1e-6, Math.Sqrt(ux * ux + uy * uy));
            ux /= ul;
            uy /= ul;
            var head = new StreamGeometry();
            using (var g = head.Open())
            {
                g.BeginFigure(pb, true);
                g.LineTo(new Point(pb.X - ux * 9 - uy * 4.5, pb.Y - uy * 9 + ux * 4.5));
                g.LineTo(new Point(pb.X - ux * 9 + uy * 4.5, pb.Y - uy * 9 - ux * 4.5));
                g.EndFigure(true);
            }
            context.DrawGeometry(NoorTheme.Brush(color), null, head);
            _arrows.Add((i, pa, pb));
        }

        foreach (var (name, r) in _boxes)
        {
            var isAny = name == Any;
            var fill = NoorTheme.Brush(NoorTheme.PanelRaised);
            if (!isAny && name == _active) fill = new SolidColorBrush(NoorTheme.AccentDeep, 0.45 + 0.4 * _weight);
            var border = name == _selectedState ? new Pen(NoorTheme.Brush(NoorTheme.Cyan), 2) : new Pen(NoorTheme.Brush(NoorTheme.Border), 1);
            context.DrawRectangle(fill, border, r, 6, 6);
            if (!isAny && name == entry) context.DrawRectangle(NoorTheme.Brush(NoorTheme.Good), null, new Rect(r.X + 1, r.Y + 6, 3, r.Height - 12), 1.5, 1.5);
            if (!isAny && name == _active && _weight < 1)  // the fade's progress, under the name
                context.DrawRectangle(NoorTheme.Brush(NoorTheme.Cyan), null, new Rect(r.X + 6, r.Bottom - 5, (r.Width - 12) * _weight, 2));
            var text = new FormattedText(isAny ? "Any state" : name, CultureInfo.InvariantCulture, FlowDirection.LeftToRight, Typeface.Default, isAny ? 10 : 12,
                                         NoorTheme.Brush(isAny ? NoorTheme.TextMuted : NoorTheme.Text)) { MaxTextWidth = r.Width - 10, Trimming = TextTrimming.CharacterEllipsis };
            context.DrawText(text, new Point(r.X + (r.Width - text.Width) / 2, r.Y + (r.Height - text.Height) / 2));
        }
    }

    protected override void OnPointerPressed(PointerPressedEventArgs e)
    {
        base.OnPointerPressed(e);
        var p = e.GetPosition(this);
        foreach (var (name, r) in _boxes)
            if (name != Any && r.Contains(p)) { StateClicked?.Invoke(name); return; }
        var best = -1;
        var bestDistance = 7.0;
        foreach (var (index, a, b) in _arrows)
        {
            var d = Distance(p, a, b);
            if (d < bestDistance) { bestDistance = d; best = index; }
        }
        if (best >= 0) TransitionClicked?.Invoke(best);
    }

    private static double Distance(Point p, Point a, Point b)
    {
        var dx = b.X - a.X;
        var dy = b.Y - a.Y;
        var len2 = dx * dx + dy * dy;
        var t = len2 < 1e-9 ? 0 : Math.Clamp(((p.X - a.X) * dx + (p.Y - a.Y) * dy) / len2, 0, 1);
        var x = a.X + t * dx - p.X;
        var y = a.Y + t * dy - p.Y;
        return Math.Sqrt(x * x + y * y);
    }
}
