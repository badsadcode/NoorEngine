using System.Runtime.InteropServices;
using System.Text.Json.Nodes;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Shapes;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Threading;
using Dock.Model.Mvvm.Controls;
using NoorEditor.Interop;
using NoorEditor.Model;
using NoorEditor.Shell;
using IOPath = System.IO.Path;
using Path = Avalonia.Controls.Shapes.Path;  // wires (System.IO.Path is IOPath here)

namespace NoorEditor.Panels;

/// <summary>
/// A procedural graph open in the graph editor (Milestone 7, part 4; ADR-014): a tab next to the viewport.
/// Like the material editor, it edits the asset file directly: every change is saved at once (in the
/// engine's own file format, via noor_graph_check), Ctrl+Z / Ctrl+Y undo and redo inside the tab, and the
/// graph runs in the background after each change (the engine's cache makes reruns cheap). The engine does
/// all the checking and running; this class holds the tab's state so the view can be rebuilt any time.
/// </summary>
public sealed class GraphDocument : Document
{
    public EditorDocument Doc { get; }
    public string RelativePath { get; }
    public string FullPath => IOPath.Combine(Doc.ProjectRoot, RelativePath);
    public JsonObject Graph { get; private set; }

    public string? SelectedNode { get; private set; }
    public (string From, string To)? SelectedLink { get; private set; }
    public bool AutoRun { get; set; } = true;
    public JsonArray Issues { get; private set; } = [];
    public JsonObject? LastRun { get; private set; }  // the status of the last finished run
    public bool Running { get; private set; }
    public double RunProgress { get; private set; }

    /// <summary>The graph or its state changed: the view redraws.</summary>
    public event Action? Changed;
    public Func<GraphDocument, Control>? CreateView { get; set; }

    private readonly List<string> _undo = new(), _redo = new();
    private readonly DispatcherTimer _poll = new() { Interval = TimeSpan.FromMilliseconds(40) };
    private readonly DispatcherTimer _debounce = new() { Interval = TimeSpan.FromMilliseconds(250) };
    private readonly Dictionary<string, Bitmap?> _previews = new();
    private bool _rerun;

    private static JsonArray? s_types;
    /// <summary>Every node type the engine has (noor_graph_node_types), by name.</summary>
    public static IReadOnlyDictionary<string, JsonObject> Types => (s_types ??= JsonNode.Parse(Native.GraphNodeTypes() ?? "[]") as JsonArray ?? [])
        .OfType<JsonObject>().ToDictionary(t => t["name"]!.GetValue<string>());

    public GraphDocument(EditorDocument doc, string relativePath)
    {
        Doc = doc;
        RelativePath = relativePath;
        Id = "Graph:" + relativePath;
        Title = IOPath.GetFileName(relativePath);
        CanFloat = true;
        CanClose = true;
        Graph = Read() ?? NewGraph();
        _poll.Tick += (_, _) => Poll();
        _debounce.Tick += (_, _) => { _debounce.Stop(); Run(); };
        Check();
    }

    public static JsonObject NewGraph() => new() { ["noor_graph"] = 1, ["seed"] = 1, ["nodes"] = new JsonArray(), ["links"] = new JsonArray(), ["outputs"] = new JsonArray() };

    private JsonObject? Read()
    {
        try { return JsonNode.Parse(File.ReadAllText(FullPath)) as JsonObject; }
        catch (Exception e) when (e is IOException or System.Text.Json.JsonException) { EngineLog.Error("Graphs", $"{RelativePath}: {e.Message}"); return null; }
    }

    public JsonArray Nodes => Graph["nodes"] as JsonArray ?? (JsonArray)(Graph["nodes"] = new JsonArray());
    public JsonArray Links => Graph["links"] as JsonArray ?? (JsonArray)(Graph["links"] = new JsonArray());
    public JsonArray Outputs => Graph["outputs"] as JsonArray ?? (JsonArray)(Graph["outputs"] = new JsonArray());
    public JsonObject? Node(string id) => Nodes.OfType<JsonObject>().FirstOrDefault(n => n["id"]?.GetValue<string>() == id);
    public bool CanUndo => _undo.Count > 0;

    // ---- Editing (each call is one undo step, saved at once) ----

    private void Edit(Action change)
    {
        _undo.Add(Graph.ToJsonString());
        _redo.Clear();
        change();
        Save();
    }

    public void Undo() => Step(_undo, _redo);
    public void Redo() => Step(_redo, _undo);

    private void Step(List<string> from, List<string> to)
    {
        if (from.Count == 0) return;
        to.Add(Graph.ToJsonString());
        Graph = (JsonObject)JsonNode.Parse(from[^1])!;
        from.RemoveAt(from.Count - 1);
        if (SelectedNode != null && Node(SelectedNode) == null) SelectedNode = null;
        SelectedLink = null;
        Save();
    }

    /// <summary>A new node of this type, with default settings, at (x, y) on the canvas. Returns its id.</summary>
    public string AddNode(string type, double x, double y)
    {
        var id = type;
        for (var n = 2; Node(id) != null; n++) id = $"{type}_{n}";
        Edit(() => Nodes.Add(new JsonObject
        {
            ["id"] = id, ["type"] = type, ["version"] = Types.TryGetValue(type, out var t) ? t["version"]!.GetValue<int>() : 1,
            ["params"] = new JsonObject(), ["position"] = new JsonArray(Math.Round(x), Math.Round(y)),
        }));
        Select(id);
        return id;
    }

    public void Move(string id, double x, double y)
    {
        if (Node(id) is not { } n) return;
        Edit(() => n["position"] = new JsonArray(Math.Round(x), Math.Round(y)));
    }

    /// <summary>Links an output to an input (an input takes one link: an old one is replaced).</summary>
    public void Connect(string fromNode, string fromPin, string toNode, string toPin)
    {
        if (fromNode == toNode) return;
        var to = $"{toNode}.{toPin}";
        Edit(() =>
        {
            foreach (var old in Links.OfType<JsonObject>().Where(l => l["to"]?.GetValue<string>() == to).ToList()) Links.Remove(old);
            Links.Add(new JsonObject { ["from"] = $"{fromNode}.{fromPin}", ["to"] = to });
        });
    }

    public void SetParam(string id, string name, JsonNode? value)
    {
        if (Node(id) is not { } n) return;
        Edit(() =>
        {
            var p = n["params"] as JsonObject ?? (JsonObject)(n["params"] = new JsonObject());
            if (value == null) p.Remove(name);
            else p[name] = value;
        });
    }

    public void SetSeed(ulong seed) => Edit(() => Graph["seed"] = seed);

    /// <summary>Makes `node.pin` a graph output called `name` (null: not an output). Outputs are what files and terrains get.</summary>
    public void SetOutput(string node, string pin, string? name)
    {
        var from = $"{node}.{pin}";
        Edit(() =>
        {
            foreach (var o in Outputs.OfType<JsonObject>().Where(o => o["from"]?.GetValue<string>() == from).ToList()) Outputs.Remove(o);
            if (!string.IsNullOrWhiteSpace(name)) Outputs.Add(new JsonObject { ["name"] = name.Trim(), ["from"] = from });
        });
    }

    public string? OutputName(string node, string pin) =>
        Outputs.OfType<JsonObject>().FirstOrDefault(o => o["from"]?.GetValue<string>() == $"{node}.{pin}")?["name"]?.GetValue<string>();

    /// <summary>Deletes the selected node (with its links and outputs) or the selected link.</summary>
    public void DeleteSelected()
    {
        if (SelectedLink is { } link)
        {
            Edit(() =>
            {
                foreach (var l in Links.OfType<JsonObject>().Where(l => l["from"]?.GetValue<string>() == link.From && l["to"]?.GetValue<string>() == link.To).ToList()) Links.Remove(l);
            });
            SelectedLink = null;
            Changed?.Invoke();
            return;
        }
        if (SelectedNode is not { } id || Node(id) is not { } n) return;
        Edit(() =>
        {
            Nodes.Remove(n);
            bool Touches(string? r) => r != null && r.StartsWith(id + ".", StringComparison.Ordinal);
            foreach (var l in Links.OfType<JsonObject>().Where(l => Touches(l["from"]?.GetValue<string>()) || Touches(l["to"]?.GetValue<string>())).ToList()) Links.Remove(l);
            foreach (var o in Outputs.OfType<JsonObject>().Where(o => Touches(o["from"]?.GetValue<string>())).ToList()) Outputs.Remove(o);
        });
        SelectedNode = null;
        Changed?.Invoke();
    }

    public void Select(string? node)
    {
        SelectedNode = node;
        SelectedLink = null;
        Changed?.Invoke();
    }

    public void SelectLink(string from, string to)
    {
        SelectedLink = (from, to);
        SelectedNode = null;
        Changed?.Invoke();
    }

    // ---- Saving and checking ----

    /// <summary>Checks the graph with the engine and writes it in the engine's own format.</summary>
    private void Save()
    {
        var text = Check();
        try { File.WriteAllText(FullPath, text ?? Graph.ToJsonString()); }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { EngineLog.Error("Graphs", $"Couldn't save {RelativePath}: {e.Message}"); }
        Changed?.Invoke();
        if (AutoRun) { _debounce.Stop(); _debounce.Start(); }
    }

    private string? Check()
    {
        var json = Native.GraphCheck(Graph.ToJsonString());
        if (json == null || JsonNode.Parse(json) is not JsonObject r)
        {
            Issues = [new JsonObject { ["node"] = "", ["pin"] = "", ["message"] = Native.LastError() }];
            return null;
        }
        Issues = r["issues"] as JsonArray ?? [];
        return r["text"]?.GetValue<string>();
    }

    public IEnumerable<string> IssuesFor(string node) =>
        Issues.OfType<JsonObject>().Concat(LastRun?["issues"] as JsonArray ?? []).OfType<JsonObject>()
              .Where(i => i["node"]?.GetValue<string>() == node).Select(i => i["message"]!.GetValue<string>()).Distinct();

    // ---- Running (in the background; one run at a time) ----

    public void Run()
    {
        if (Running) { _rerun = true; Native.noor_doc_generate_cancel(Doc.Handle); return; }
        if (Native.noor_doc_generate_start(Doc.Handle, RelativePath, null) == 0)
        {
            EngineLog.Warn("Graphs", $"{RelativePath}: {Native.LastError()}");
            return;
        }
        Running = true;
        RunProgress = 0;
        _poll.Start();
        Changed?.Invoke();
    }

    public void Cancel()
    {
        _rerun = false;
        if (Running) Native.noor_doc_generate_cancel(Doc.Handle);
    }

    private void Poll()
    {
        if (JsonNode.Parse(Native.GenerateStatus(Doc.Handle) ?? "{}") is not JsonObject s) return;
        var total = s["total"]?.GetValue<double>() ?? 0;
        RunProgress = total > 0 ? (s["done"]?.GetValue<double>() ?? 0) / total : 0;
        if (s["running"]?.GetValue<bool>() == true) { Changed?.Invoke(); return; }
        _poll.Stop();
        Running = false;
        if (s["graph"]?.GetValue<string>() == RelativePath && s["cancelled"]?.GetValue<bool>() != true)
        {
            LastRun = s;
            _previews.Clear();
        }
        if (_rerun) { _rerun = false; Run(); return; }
        Changed?.Invoke();
    }

    /// <summary>Waits for the current run (tests).</summary>
    public async Task<bool> WaitForRun(double seconds = 60)
    {
        var until = DateTime.UtcNow.AddSeconds(seconds);
        while ((Running || _debounce.IsEnabled) && DateTime.UtcNow < until) await Task.Delay(30);
        return !Running;
    }

    public JsonObject? Result(string node) => LastRun?["nodes"]?[node] as JsonObject;

    /// <summary>A greyscale picture of a field output from the last run (cached per run), or null.</summary>
    public Bitmap? Preview(string node, string pin, int size = 128)
    {
        var key = $"{node}.{pin}.{size}";
        if (_previews.TryGetValue(key, out var cached)) return cached;
        Bitmap? bitmap = null;
        if (Native.GeneratePreview(Doc.Handle, node, pin, size) is { } json && JsonNode.Parse(json) is JsonObject p && p["pixels"] is JsonValue pv)
        {
            var w = p["width"]!.GetValue<int>();
            var h = p["height"]!.GetValue<int>();
            var grey = Convert.FromBase64String(pv.GetValue<string>());
            var wb = new WriteableBitmap(new PixelSize(w, h), new Vector(96, 96), PixelFormat.Bgra8888, AlphaFormat.Opaque);
            using (var fb = wb.Lock())
            {
                var row = new byte[w * 4];
                for (var y = 0; y < h; y++)
                {
                    for (var x = 0; x < w; x++)
                    {
                        var g = grey[y * w + x];
                        row[x * 4] = g; row[x * 4 + 1] = g; row[x * 4 + 2] = g; row[x * 4 + 3] = 255;
                    }
                    Marshal.Copy(row, 0, fb.Address + y * fb.RowBytes, row.Length);
                }
            }
            bitmap = wb;
        }
        _previews[key] = bitmap;
        return bitmap;
    }
}

/// <summary>
/// The graph editor's view: a toolbar, a canvas of nodes and wires, and a side panel for the selected node.
/// Drag a node's title to move it; drag from an output's dot to an input's dot to link them; click a wire
/// to select it; Delete removes what's selected. Pin colours: field = cyan, number = gold, others = grey.
/// </summary>
public sealed class GraphPanel : DockPanel
{
    public const double NodeWidth = 200, HeaderHeight = 28, RowHeight = 22, ThumbSize = 96;

    public GraphDocument Graph { get; }
    private readonly Canvas _canvas = new() { Width = 3000, Height = 2000, Background = NoorTheme.Brush(NoorTheme.Background) };
    private readonly ScrollViewer _scroll;
    private readonly StackPanel _side = new() { Spacing = 6, Margin = new Thickness(10) };
    private readonly TextBlock _status = new() { FontSize = 11, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(10, 0) };
    private readonly ProgressBar _progress = new() { Width = 90, Height = 4, Minimum = 0, Maximum = 1, VerticalAlignment = VerticalAlignment.Center };
    private Path? _wireDrag;
    private (string Node, string Pin)? _dragFrom;
    private string? _moving;
    private Point _moveOffset;
    private readonly Dictionary<string, Canvas> _views = new();  // node id -> its view on the canvas

    public GraphPanel(GraphDocument graph)
    {
        Graph = graph;
        Focusable = true;

        var add = new Button { Content = "+ Node ▾", Classes = { "tool" } };
        var menu = new MenuFlyout();
        foreach (var (name, type) in GraphDocument.Types.OrderBy(t => t.Key))
        {
            var item = new MenuItem { Header = name };
            ToolTip.SetTip(item, type["help"]?.GetValue<string>());
            item.Click += (_, _) => Graph.AddNode(name, _scroll!.Offset.X + 80 + 30 * (Graph.Nodes.Count % 5), _scroll.Offset.Y + 60 + 30 * (Graph.Nodes.Count % 5));
            menu.Items.Add(item);
        }
        add.Flyout = menu;
        var run = new Button { Content = "Run", Classes = { "tool" } };
        run.Click += (_, _) => Graph.Run();
        var cancel = new Button { Content = "Cancel", Classes = { "tool" } };
        cancel.Click += (_, _) => Graph.Cancel();
        var auto = new CheckBox { Content = "Run after each change", IsChecked = Graph.AutoRun, FontSize = 11, Margin = new Thickness(8, 0) };
        auto.IsCheckedChanged += (_, _) => Graph.AutoRun = auto.IsChecked == true;
        var toolbar = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 4, Margin = new Thickness(6, 3), Children = { add, run, cancel, auto, _progress, _status } };
        foreach (var c in toolbar.Children) c.VerticalAlignment = VerticalAlignment.Center;
        var top = new Border { Child = toolbar, Background = NoorTheme.Brush(NoorTheme.Panel) };
        SetDock(top, Avalonia.Controls.Dock.Top);
        Children.Add(top);

        var side = new Border { Width = 300, Background = NoorTheme.Brush(NoorTheme.Panel), Child = new ScrollViewer { Content = _side } };
        SetDock(side, Avalonia.Controls.Dock.Right);
        Children.Add(side);

        _scroll = new ScrollViewer { Content = _canvas, HorizontalScrollBarVisibility = Avalonia.Controls.Primitives.ScrollBarVisibility.Auto };
        Children.Add(_scroll);
        _canvas.PointerPressed += (_, e) =>
        {
            if (e.Source == _canvas) { Graph.Select(null); Focus(); }
        };
        _canvas.PointerMoved += OnPointerMoved;
        _canvas.PointerReleased += OnPointerReleased;
        KeyDown += (_, e) =>
        {
            if (e.Key == Key.Delete) { Graph.DeleteSelected(); e.Handled = true; }
        };
        Rebuild();
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        Graph.Changed += Rebuild;
        Rebuild();
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnDetachedFromVisualTree(e);
        Graph.Changed -= Rebuild;
    }

    private static Color PinColor(string? type) => type switch
    {
        "field" => NoorTheme.Cyan,
        "scalar" => NoorTheme.Warn,
        _ => NoorTheme.TextMuted,
    };

    private static Point Position(JsonObject node) =>
        node["position"] is JsonArray p && p.Count == 2 ? new Point(p[0]!.GetValue<double>(), p[1]!.GetValue<double>()) : new Point(40, 40);

    /// <summary>Where a pin's dot is on the canvas (inputs on the left edge, outputs on the right; one row each).</summary>
    public Point PinPoint(string node, string pin, bool output)
    {
        if (Graph.Node(node) is not { } n || !GraphDocument.Types.TryGetValue(n["type"]!.GetValue<string>(), out var t)) return default;
        var pins = (t[output ? "outputs" : "inputs"] as JsonArray ?? []).Select(p => p!["name"]!.GetValue<string>()).ToList();
        var row = Math.Max(0, pins.IndexOf(pin));
        var at = Position(n);
        return new Point(at.X + (output ? NodeWidth : 0), at.Y + HeaderHeight + row * RowHeight + RowHeight / 2);
    }

    // ---- Drawing ----

    private void Rebuild()
    {
        _canvas.Children.Clear();
        _views.Clear();
        // Wires first (under the nodes).
        foreach (var l in Graph.Links.OfType<JsonObject>())
        {
            var from = l["from"]?.GetValue<string>() ?? "";
            var to = l["to"]?.GetValue<string>() ?? "";
            var (fn, fp) = Split(from);
            var (tn, tp) = Split(to);
            var a = PinPoint(fn, fp, true);
            var b = PinPoint(tn, tp, false);
            var type = OutputType(fn, fp);
            var selected = Graph.SelectedLink is { } s && s.From == from && s.To == to;
            var wire = Wire(a, b, selected ? NoorTheme.Text : PinColor(type), selected ? 3.5 : 2.2);
            wire.PointerPressed += (_, e) => { Graph.SelectLink(from, to); Focus(); e.Handled = true; };
            ToolTip.SetTip(wire, $"{from} → {to} (click, then Delete to remove)");
            _canvas.Children.Add(wire);
        }
        foreach (var n in Graph.Nodes.OfType<JsonObject>()) _canvas.Children.Add(NodeView(n));
        if (_wireDrag != null) _canvas.Children.Add(_wireDrag);
        BuildSide();

        var run = Graph.LastRun;
        _progress.IsVisible = Graph.Running;
        _progress.Value = Graph.RunProgress;
        _status.Text = Graph.Running ? "Running…"
            : run == null ? (Graph.Issues.Count > 0 ? $"{Graph.Issues.Count} issue(s)" : "Not run yet")
            : $"{run["ran"]} node(s) ran, {run["cached"]} from the cache · tiles: {run["tiles_run"]} computed, {run["tiles_cached"]} cached, {run["tiles_passed"]} passed" +
              (run["written"]?.GetValue<bool>() == true ? " · outputs written" : "") +
              (Graph.Issues.Count + ((run["issues"] as JsonArray)?.Count ?? 0) > 0 ? " · see the issues on nodes" : "");
    }

    private static (string, string) Split(string r)
    {
        var dot = r.LastIndexOf('.');
        return dot < 0 ? (r, "") : (r[..dot], r[(dot + 1)..]);
    }

    private string? OutputType(string node, string pin) =>
        Graph.Node(node) is { } n && GraphDocument.Types.TryGetValue(n["type"]!.GetValue<string>(), out var t)
            ? (t["outputs"] as JsonArray ?? []).FirstOrDefault(p => p!["name"]!.GetValue<string>() == pin)?["type"]?.GetValue<string>() : null;

    private static Path Wire(Point a, Point b, Color color, double thickness)
    {
        var dx = Math.Max(40, Math.Abs(b.X - a.X) * 0.5);
        var figure = new PathFigure { StartPoint = a, IsClosed = false, IsFilled = false };
        figure.Segments!.Add(new BezierSegment { Point1 = new Point(a.X + dx, a.Y), Point2 = new Point(b.X - dx, b.Y), Point3 = b });
        return new Path { Data = new PathGeometry { Figures = new PathFigures { figure } }, Stroke = NoorTheme.Brush(color), StrokeThickness = thickness };
    }

    private Control NodeView(JsonObject n)
    {
        var id = n["id"]!.GetValue<string>();
        var typeName = n["type"]?.GetValue<string>() ?? "?";
        GraphDocument.Types.TryGetValue(typeName, out var type);
        var inputs = (type?["inputs"] as JsonArray ?? []).OfType<JsonObject>().ToList();
        var outputs = (type?["outputs"] as JsonArray ?? []).OfType<JsonObject>().ToList();
        var result = Graph.Result(id);
        var issues = Graph.IssuesFor(id).ToList();
        var state = result?["state"]?.GetValue<string>();
        var selected = Graph.SelectedNode == id;

        // Header: id · type, and how the last run went.
        var badge = new TextBlock
        {
            FontSize = 10, VerticalAlignment = VerticalAlignment.Center,
            Text = issues.Count > 0 || state == "failed" ? "✕" : state == "done" ? (result!["cached"]?.GetValue<bool>() == true ? "cached" : $"{result["ms"]?.GetValue<double>():0} ms") : state == "skipped" ? "–" : "",
            Foreground = NoorTheme.Brush(issues.Count > 0 || state == "failed" ? NoorTheme.Bad : NoorTheme.TextMuted),
        };
        var title = new TextBlock { Text = $"{id} · {typeName}", FontSize = 12, FontWeight = FontWeight.SemiBold, VerticalAlignment = VerticalAlignment.Center, TextTrimming = TextTrimming.CharacterEllipsis };
        var header = new DockPanel { Height = HeaderHeight, Background = NoorTheme.Brush(selected ? NoorTheme.AccentDeep : NoorTheme.PanelRaised), Cursor = new Cursor(StandardCursorType.SizeAll) };
        var badgeBox = new Border { Child = badge, Padding = new Thickness(0, 0, 8, 0) };
        DockPanel.SetDock(badgeBox, Avalonia.Controls.Dock.Right);  // the wrapper is what the DockPanel lays out
        header.Children.Add(badgeBox);
        header.Children.Add(new Border { Child = title, Padding = new Thickness(8, 0) });
        if (issues.Count > 0) ToolTip.SetTip(header, string.Join("\n", issues));
        header.PointerPressed += (_, e) =>
        {
            Graph.Select(id);
            _moving = id;
            _moveOffset = e.GetPosition(_canvas) - Position(n);
            e.Pointer.Capture(_canvas);
            Focus();
            e.Handled = true;
        };

        // Rows: input on the left, output on the right.
        var rows = new Grid { RowDefinitions = new RowDefinitions(string.Join(",", Enumerable.Repeat(RowHeight.ToString(System.Globalization.CultureInfo.InvariantCulture), Math.Max(1, Math.Max(inputs.Count, outputs.Count))))) };
        for (var i = 0; i < inputs.Count; i++)
        {
            var label = new TextBlock { Text = inputs[i]["name"]!.GetValue<string>() + (inputs[i]["optional"]?.GetValue<bool>() == true ? "?" : ""), FontSize = 11, Margin = new Thickness(10, 0, 0, 0), VerticalAlignment = VerticalAlignment.Center, Classes = { "muted" } };
            Grid.SetRow(label, i);
            rows.Children.Add(label);
        }
        for (var i = 0; i < outputs.Count; i++)
        {
            var pin = outputs[i]["name"]!.GetValue<string>();
            var output = Graph.OutputName(id, pin);
            var label = new TextBlock { Text = output != null ? $"{pin} → \"{output}\"" : pin, FontSize = 11, Margin = new Thickness(0, 0, 10, 0), HorizontalAlignment = HorizontalAlignment.Right, VerticalAlignment = VerticalAlignment.Center };
            Grid.SetRow(label, i);
            rows.Children.Add(label);
        }
        var body = new StackPanel { Children = { header, rows } };
        // A thumbnail of the first field output.
        var field = outputs.FirstOrDefault(o => o["type"]?.GetValue<string>() == "field");
        if (field != null && state == "done" && Graph.Preview(id, field["name"]!.GetValue<string>(), 96) is { } thumb)
            body.Children.Add(new Image { Source = thumb, Width = ThumbSize, Height = ThumbSize, Margin = new Thickness(0, 4, 0, 6), HorizontalAlignment = HorizontalAlignment.Center });

        var card = new Border
        {
            Width = NodeWidth, Child = body, CornerRadius = new CornerRadius(6), ClipToBounds = true,
            Background = NoorTheme.Brush(NoorTheme.Panel), BorderThickness = new Thickness(selected ? 2 : 1),
            BorderBrush = NoorTheme.Brush(issues.Count > 0 ? NoorTheme.Bad : selected ? NoorTheme.Accent : NoorTheme.Border),
        };
        var at = Position(n);
        var holder = new Canvas { Width = NodeWidth + 16, Height = 1 };
        Canvas.SetLeft(card, 0);
        holder.Children.Add(card);
        // Pin dots on the edges (drag from an output's dot to an input's dot).
        for (var i = 0; i < inputs.Count; i++)
        {
            var dot = Dot(PinColor(inputs[i]["type"]?.GetValue<string>()), $"{inputs[i]["name"]}: {inputs[i]["type"]} in {inputs[i]["unit"]}");
            Canvas.SetLeft(dot, -6);
            Canvas.SetTop(dot, HeaderHeight + i * RowHeight + RowHeight / 2 - 6);
            holder.Children.Add(dot);
        }
        for (var i = 0; i < outputs.Count; i++)
        {
            var pin = outputs[i]["name"]!.GetValue<string>();
            var dot = Dot(PinColor(outputs[i]["type"]?.GetValue<string>()), $"{pin}: {outputs[i]["type"]} in {outputs[i]["unit"]} (drag to an input)");
            Canvas.SetLeft(dot, NodeWidth - 6);
            Canvas.SetTop(dot, HeaderHeight + i * RowHeight + RowHeight / 2 - 6);
            dot.Cursor = new Cursor(StandardCursorType.Cross);
            dot.PointerPressed += (_, e) =>
            {
                _dragFrom = (id, pin);
                var p = PinPoint(id, pin, true);
                _wireDrag = Wire(p, p, NoorTheme.Text, 2);
                _canvas.Children.Add(_wireDrag);
                e.Pointer.Capture(_canvas);
                e.Handled = true;
            };
            holder.Children.Add(dot);
        }
        Canvas.SetLeft(holder, at.X);
        Canvas.SetTop(holder, at.Y);
        _views[id] = holder;
        return holder;
    }

    private static Ellipse Dot(Color color, string tip)
    {
        var e = new Ellipse { Width = 12, Height = 12, Fill = NoorTheme.Brush(color), Stroke = NoorTheme.Brush(NoorTheme.Background), StrokeThickness = 2 };
        ToolTip.SetTip(e, tip);
        return e;
    }

    // ---- Pointer: moving nodes, dragging wires ----

    private void OnPointerMoved(object? sender, PointerEventArgs e)
    {
        var p = e.GetPosition(_canvas);
        if (_moving != null)
        {
            // Move the node's view now; the graph changes (one undo step) when the button is released.
            if (_views.TryGetValue(_moving, out var holder))
            {
                Canvas.SetLeft(holder, p.X - _moveOffset.X);
                Canvas.SetTop(holder, p.Y - _moveOffset.Y);
            }
        }
        else if (_wireDrag != null && _dragFrom is { } from)
        {
            var a = PinPoint(from.Node, from.Pin, true);
            var dx = Math.Max(40, Math.Abs(p.X - a.X) * 0.5);
            var figure = new PathFigure { StartPoint = a, IsClosed = false, IsFilled = false };
            figure.Segments!.Add(new BezierSegment { Point1 = new Point(a.X + dx, a.Y), Point2 = new Point(p.X - dx, p.Y), Point3 = p });
            _wireDrag.Data = new PathGeometry { Figures = new PathFigures { figure } };
        }
    }

    private void OnPointerReleased(object? sender, PointerReleasedEventArgs e)
    {
        var p = e.GetPosition(_canvas);
        if (_moving != null)
        {
            var id = _moving;
            _moving = null;
            e.Pointer.Capture(null);
            Graph.Move(id, Math.Max(0, p.X - _moveOffset.X), Math.Max(0, p.Y - _moveOffset.Y));
        }
        else if (_dragFrom is { } from)
        {
            _dragFrom = null;
            _wireDrag = null;
            e.Pointer.Capture(null);
            // The input dot nearest the release point, within 14 px.
            (string Node, string Pin)? best = null;
            var bestDistance = 14.0;
            foreach (var n in Graph.Nodes.OfType<JsonObject>())
            {
                var nid = n["id"]!.GetValue<string>();
                if (!GraphDocument.Types.TryGetValue(n["type"]!.GetValue<string>(), out var t)) continue;
                foreach (var input in (t["inputs"] as JsonArray ?? []).OfType<JsonObject>())
                {
                    var pin = input["name"]!.GetValue<string>();
                    var q = PinPoint(nid, pin, false);
                    var d = Math.Sqrt((q.X - p.X) * (q.X - p.X) + (q.Y - p.Y) * (q.Y - p.Y));
                    if (d < bestDistance) { bestDistance = d; best = (nid, pin); }
                }
            }
            if (best is { } to) Graph.Connect(from.Node, from.Pin, to.Node, to.Pin);
            else Rebuild();
        }
    }

    // ---- The side panel: the selected node's settings, result and preview ----

    private void BuildSide()
    {
        _side.Children.Clear();
        if (Graph.SelectedNode is not { } id || Graph.Node(id) is not { } n)
        {
            _side.Children.Add(new TextBlock { Text = "Graph", Classes = { "section" } });
            var seed = TextField((Graph.Graph["seed"]?.ToString() ?? "0"), s => { if (ulong.TryParse(s, out var v)) Graph.SetSeed(v); });
            _side.Children.Add(Row("Seed", seed, "The graph's seed: every node's randomness comes from it and the node's id"));
            _side.Children.Add(new TextBlock
            {
                Text = "Click a node to edit it. Add nodes with “+ Node”. Drag from an output's dot to an input's dot to link them. " +
                       "Mark an output as a graph output (in the node's panel) to write it to a file, or to use it as a terrain's base.",
                FontSize = 11, TextWrapping = TextWrapping.Wrap, Classes = { "muted" },
            });
            foreach (var issue in Graph.Issues.OfType<JsonObject>().Where(i => string.IsNullOrEmpty(i["node"]?.GetValue<string>())))
                _side.Children.Add(new TextBlock { Text = issue["message"]?.GetValue<string>(), Foreground = NoorTheme.Brush(NoorTheme.Bad), FontSize = 11, TextWrapping = TextWrapping.Wrap });
            return;
        }
        var typeName = n["type"]?.GetValue<string>() ?? "?";
        GraphDocument.Types.TryGetValue(typeName, out var type);
        _side.Children.Add(new TextBlock { Text = $"{id} · {typeName}", Classes = { "section" } });
        if (type?["help"]?.GetValue<string>() is { } help)
            _side.Children.Add(new TextBlock { Text = help, FontSize = 11, TextWrapping = TextWrapping.Wrap, Classes = { "muted" } });
        foreach (var issue in Graph.IssuesFor(id))
            _side.Children.Add(new TextBlock { Text = "✕ " + issue, Foreground = NoorTheme.Brush(NoorTheme.Bad), FontSize = 11, TextWrapping = TextWrapping.Wrap });

        // Parameters (left empty: the default, shown greyed).
        var given = n["params"] as JsonObject ?? new JsonObject();
        foreach (var p in (type?["params"] as JsonArray ?? []).OfType<JsonObject>())
        {
            var name = p["name"]!.GetValue<string>();
            var kind = p["kind"]?.GetValue<string>();
            var current = given[name] ?? p["default"];
            Control editor = kind switch
            {
                "choice" => Choice(p, current, v => Graph.SetParam(id, name, JsonValue.Create(v))),
                "flag" => Flag(current, v => Graph.SetParam(id, name, JsonValue.Create(v))),
                "text" => TextField(current?.ToString() ?? "", v => Graph.SetParam(id, name, string.IsNullOrEmpty(v) ? null : JsonValue.Create(v))),
                _ => TextField(current?.ToString() ?? "", v =>
                {
                    if (double.TryParse(v, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var d))
                        Graph.SetParam(id, name, kind == "integer" ? JsonValue.Create((long)Math.Round(d)) : JsonValue.Create(d));
                }),
            };
            if (given[name] == null && editor is TextBox tb) tb.Foreground = NoorTheme.Brush(NoorTheme.TextMuted);
            _side.Children.Add(Row(name, editor, p["help"]?.GetValue<string>()));
        }

        // Outputs: which are graph outputs, and the result of the last run.
        var result = Graph.Result(id);
        foreach (var o in (type?["outputs"] as JsonArray ?? []).OfType<JsonObject>())
        {
            var pin = o["name"]!.GetValue<string>();
            _side.Children.Add(new TextBlock { Text = $"Output “{pin}” ({o["type"]}, {o["unit"]})", FontSize = 12, FontWeight = FontWeight.SemiBold, Margin = new Thickness(0, 6, 0, 0) });
            var outName = TextField(Graph.OutputName(id, pin) ?? "", v => Graph.SetOutput(id, pin, v));
            _side.Children.Add(Row("Graph output", outName, "A name makes this a graph output: written to a file by Run and noor_cli generate, usable as a terrain's base. Empty: not an output"));
            if (result?["outputs"]?[pin] is JsonObject r)
            {
                var line = r["value"] != null ? $"value {r["value"]}" : r["min"] != null ? $"{r["min"]?.GetValue<double>():0.##} … {r["max"]?.GetValue<double>():0.##}  ({r["samples"]?[0]} × {r["samples"]?[1]} samples)" : "";
                _side.Children.Add(new TextBlock { Text = line, FontSize = 11, Classes = { "muted" } });
                if (o["type"]?.GetValue<string>() == "field" && Graph.Preview(id, pin, 256) is { } big)
                    _side.Children.Add(new Image { Source = big, Width = 256, Height = 256, HorizontalAlignment = HorizontalAlignment.Left });
            }
        }
        if (result != null)
        {
            var state = result["state"]?.GetValue<string>();
            _side.Children.Add(new TextBlock
            {
                Text = state == "done"
                    ? $"Last run: {(result["cached"]?.GetValue<bool>() == true ? "from the cache" : $"{result["ms"]?.GetValue<double>():0.#} ms")}" +
                      (result["tiles_run"]?.GetValue<int>() + result["tiles_cached"]?.GetValue<int>() + result["tiles_passed"]?.GetValue<int>() is > 0
                          ? $" · tiles: {result["tiles_run"]} computed, {result["tiles_cached"]} cached, {result["tiles_passed"]} passed" : "")
                    : $"Last run: {state}: {result["message"]}",
                FontSize = 11, TextWrapping = TextWrapping.Wrap, Classes = { "muted" }, Margin = new Thickness(0, 6, 0, 0),
            });
        }
        var delete = new Button { Content = "Delete node", Classes = { "tool" }, HorizontalAlignment = HorizontalAlignment.Left, Margin = new Thickness(0, 8, 0, 0) };
        delete.Click += (_, _) => Graph.DeleteSelected();
        _side.Children.Add(delete);
    }

    private static Control Row(string label, Control editor, string? tip)
    {
        var text = new TextBlock { Text = label, FontSize = 12, Classes = { "muted" }, VerticalAlignment = VerticalAlignment.Center, TextTrimming = TextTrimming.CharacterEllipsis };
        if (tip != null) ToolTip.SetTip(text, tip);
        var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("100,*") };
        Grid.SetColumn(editor, 1);
        grid.Children.Add(text);
        grid.Children.Add(editor);
        return grid;
    }

    private static TextBox TextField(string value, Action<string> commit)
    {
        var box = new TextBox { Text = value, Classes = { "field" } };
        void Done() { if (box.Text != value) commit(box.Text ?? ""); }
        box.KeyDown += (_, e) => { if (e.Key == Key.Enter) { Done(); e.Handled = true; } };
        box.LostFocus += (_, _) => Done();
        return box;
    }

    private static ComboBox Choice(JsonObject p, JsonNode? current, Action<string> set)
    {
        var choices = (p["choices"] as JsonArray ?? []).Select(c => c!.GetValue<string>()).ToList();
        var now = current?.GetValue<string>();
        var combo = new ComboBox { ItemsSource = choices, SelectedItem = now, Classes = { "field" }, HorizontalAlignment = HorizontalAlignment.Stretch };
        combo.SelectionChanged += (_, _) => { if (combo.SelectedItem is string pick && pick != now) set(pick); };
        return combo;
    }

    private static CheckBox Flag(JsonNode? current, Action<bool> set)
    {
        var box = new CheckBox { IsChecked = current?.GetValue<bool>() == true };
        box.IsCheckedChanged += (_, _) => set(box.IsChecked == true);
        return box;
    }
}
