using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Templates;
using Avalonia.Layout;
using Avalonia.Media;
using System.Globalization;
using System.Text.Json.Nodes;
using NoorEditor.Model;
using NoorEditor.Shell;

namespace NoorEditor.Panels;

/// <summary>
/// Scenes and importable assets of the project (from the engine's asset registry).
/// Double-click a scene to open it, a material or texture to edit it; double-click a model or prefab
/// to add it to the open scene (Add to scene does it for materials and textures too).
/// </summary>
public sealed class ContentPanel : DockPanel
{
    private readonly EditorDocument _doc;
    private readonly TextBox _search = new() { PlaceholderText = "Search assets", Classes = { "field" }, Width = 220 };
    private readonly ListBox _scenes = new() { Background = Brushes.Transparent };
    private readonly ListBox _assets = new() { Background = Brushes.Transparent };
    private readonly TextBlock _summary = new() { FontSize = 11, Classes = { "muted" }, VerticalAlignment = VerticalAlignment.Center };
    private readonly StackPanel _details = new() { Spacing = 2, Margin = new Thickness(10, 0, 10, 10) };

    /// <summary>The details of the selected asset as shown (for tests): every line's text.</summary>
    public string DetailsText => string.Join("\n", Texts(_details));
    private static IEnumerable<string?> Texts(Control c) => c switch
    {
        TextBlock t => [t.Text],
        Panel p => p.Children.SelectMany(Texts),
        _ => [],
    };
    /// <summary>The engine's details of the selected asset (null: none selected).</summary>
    public JsonObject? Details { get; private set; }

    /// <summary>Selects an asset in the list (and shows its details).</summary>
    public void Select(AssetInfo asset)
    {
        _search.Text = "";
        Fill();
        _assets.SelectedItem = _doc.Assets.FirstOrDefault(a => a.Guid == asset.Guid);
        _assets.ScrollIntoView(_assets.SelectedItem!);
    }

    public event Action<AssetInfo>? AddRequested;
    private readonly Button _makeMaterial = new() { Content = "Make material", Classes = { "tool" }, IsEnabled = false };

    /// <summary>Make material (textures): the set's .noormat, then selects it in the list.</summary>
    public string? MakeMaterial(AssetInfo texture)
    {
        var made = _doc.MakeMaterial(texture.Path);
        if (made is not { } m) return null;
        Interop.EngineLog.Info("Editor", m.Created ? $"Material {m.Path} made from {m.Maps} texture(s)" : $"{m.Path} already exists: kept as it is");
        _assets.SelectedItem = _doc.Assets.FirstOrDefault(a => a.Path == m.Path);
        return m.Path;
    }
    public event Action<string>? OpenSceneRequested;
    /// <summary>Edit: opens the material editor (materials) or the import settings (textures).</summary>
    public event Action<AssetInfo>? EditRequested;
    private readonly Button _edit = new() { Content = "Edit", Classes = { "tool" }, IsEnabled = false };

    public ContentPanel(EditorDocument doc)
    {
        _doc = doc;

        var rescan = new Button { Content = "Rescan", Classes = { "tool" } };
        ToolTip.SetTip(rescan, "Scan the Assets folder for new, moved or changed files");
        rescan.Click += (_, _) => _doc.RefreshAssets(rescan: true);
        var add = new Button { Content = "Add to scene", Classes = { "tool" } };
        add.Click += (_, _) => { if (_assets.SelectedItem is AssetInfo a) AddRequested?.Invoke(a); };
        _makeMaterial.Click += (_, _) => { if (_assets.SelectedItem is AssetInfo { Importer: "texture" } a) MakeMaterial(a); };
        ToolTip.SetTip(_makeMaterial, "A material from this texture's set (its _albedo, _normal, _orm… files), saved beside it");
        _edit.Click += (_, _) => { if (_assets.SelectedItem is AssetInfo { Importer: "material" or "texture" } a) EditRequested?.Invoke(a); };
        ToolTip.SetTip(_edit, "Materials: the material editor. Textures: import settings (compression, size, kind). Double-click does the same.");
        _assets.SelectionChanged += (_, _) =>
        {
            _makeMaterial.IsEnabled = _assets.SelectedItem is AssetInfo { Importer: "texture" };
            _edit.IsEnabled = _assets.SelectedItem is AssetInfo { Importer: "material" or "texture" };
            ShowDetails(_assets.SelectedItem as AssetInfo);
        };
        var bar = new StackPanel
        {
            Orientation = Orientation.Horizontal, Spacing = 6, Margin = new Thickness(6, 6, 6, 4),
            Children = { _search, rescan, add, _edit, _makeMaterial, _summary },
        };
        SetDock(bar, Avalonia.Controls.Dock.Top);
        Children.Add(bar);

        _scenes.ItemTemplate = new FuncDataTemplate<string>((path, _) => Row("◇", NoorTheme.Cyan, path ?? "", "Scene"));
        _assets.ItemTemplate = new FuncDataTemplate<AssetInfo>((a, _) => a == null ? new TextBlock() :
            Row(a.Kind switch { "Prefab" => "◆", "Sound" => "♪", "Material" => "●", _ => "▣" },
                a.Kind is "Prefab" or "Material" ? NoorTheme.Cyan : NoorTheme.Accent, a.Path, a.Kind));
        _scenes.DoubleTapped += (_, _) => { if (_scenes.SelectedItem is string s) OpenSceneRequested?.Invoke(s); };
        _assets.DoubleTapped += (_, _) =>
        {
            if (_assets.SelectedItem is AssetInfo { Importer: "material" or "texture" } m) EditRequested?.Invoke(m);
            else if (_assets.SelectedItem is AssetInfo a) AddRequested?.Invoke(a);
        };

        var scenesHeader = Header("Scenes");
        var assetsHeader = Header("Assets");
        var detailsHeader = Header("Details");
        var details = new ScrollViewer { Content = _details, HorizontalScrollBarVisibility = Avalonia.Controls.Primitives.ScrollBarVisibility.Disabled };
        var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("260,*,380"), RowDefinitions = new RowDefinitions("Auto,*") };
        Grid.SetColumn(assetsHeader, 1);
        Grid.SetColumn(detailsHeader, 2);
        Grid.SetRow(_scenes, 1);
        Grid.SetRow(_assets, 1);
        Grid.SetColumn(_assets, 1);
        Grid.SetRow(details, 1);
        Grid.SetColumn(details, 2);
        grid.Children.Add(scenesHeader);
        grid.Children.Add(assetsHeader);
        grid.Children.Add(detailsHeader);
        grid.Children.Add(_scenes);
        grid.Children.Add(_assets);
        grid.Children.Add(details);
        Children.Add(grid);
        ShowDetails(null);

        _search.TextChanged += (_, _) => Fill();
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        _doc.AssetsChanged += Fill;
        _doc.SceneChanged += Fill;  // saving a new scene adds a scene file
        Fill();
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnDetachedFromVisualTree(e);
        _doc.AssetsChanged -= Fill;
        _doc.SceneChanged -= Fill;
    }

    private static TextBlock Header(string text) => new() { Text = text, Classes = { "section" }, Margin = new Thickness(10, 4) };

    private static Control Row(string icon, Color color, string text, string kind) => new DockPanel
    {
        Children =
        {
            new TextBlock { Text = icon, Width = 18, FontSize = 11, Foreground = NoorTheme.Brush(color), VerticalAlignment = VerticalAlignment.Center },
            new TextBlock { Text = kind, FontSize = 11, Classes = { "muted" }, Margin = new Thickness(8, 0, 0, 0), VerticalAlignment = VerticalAlignment.Center,
                            [DockPanel.DockProperty] = Avalonia.Controls.Dock.Right },
            new TextBlock { Text = text, FontSize = 12, VerticalAlignment = VerticalAlignment.Center, TextTrimming = TextTrimming.CharacterEllipsis },
        },
    };

    // ---- Details (Milestone 7, part 5): what the asset is, and for generated assets where it came from.

    private static string Mb(long bytes) => (bytes / 1e6).ToString("0.00", CultureInfo.InvariantCulture) + " MB";
    private static string S(JsonNode? n) => n is JsonValue v && v.TryGetValue<string>(out var s) ? s : "";

    private void Line(string label, string value, IBrush? color = null)
    {
        if (value.Length == 0) return;
        _details.Children.Add(new DockPanel
        {
            Children =
            {
                new TextBlock { Text = label, Width = 92, FontSize = 11, Classes = { "muted" }, VerticalAlignment = VerticalAlignment.Top },
                new TextBlock { Text = value, FontSize = 12, TextWrapping = TextWrapping.Wrap, Foreground = color ?? NoorTheme.Brush(NoorTheme.Text) },
            },
        });
    }

    private void Note(string text, Color color) =>
        _details.Children.Add(new TextBlock { Text = text, FontSize = 11, TextWrapping = TextWrapping.Wrap, Foreground = NoorTheme.Brush(color), Margin = new Thickness(0, 2, 0, 0) });

    private void ShowDetails(AssetInfo? asset)
    {
        _details.Children.Clear();
        Details = asset == null ? null : _doc.AssetDetails(asset.Guid);
        if (asset == null || Details == null)
        {
            _details.Children.Add(new TextBlock { Text = asset == null ? "Select an asset to see what it is and where it came from." : Interop.Native.LastError(),
                                                  FontSize = 11, Classes = { "muted" }, TextWrapping = TextWrapping.Wrap });
            return;
        }
        var d = Details;
        _details.Children.Add(new TextBlock { Text = asset.Name, FontSize = 14, FontWeight = FontWeight.SemiBold, Margin = new Thickness(0, 0, 0, 4) });
        var interchangeKind = (d["interchange"] as JsonObject)?["kind"] is JsonValue kv && kv.TryGetValue<string>(out var k) ? k : "";
        Line("Kind", interchangeKind.Length > 0 ? $"{asset.Kind} · {interchangeKind}" : asset.Kind);
        Line("File", $"{asset.Path} · {Mb(d["bytes"]?.GetValue<long>() ?? 0)}");
        if (d["model"] is JsonObject model)
            Line("Contents", $"{model["triangles"]} triangles, {model["materials"]} material(s), {model["images"]} texture(s)");
        if (d["interchange"] is not JsonObject m) return;

        // A Noor Interchange asset: problems first, then where it came from, its levels, and the rest.
        foreach (var issue in (m["issues"] as JsonArray ?? []).OfType<JsonObject>())
        {
            var error = issue["error"]?.GetValue<bool>() == true;
            Note($"{(error ? "✕" : "⚠")} {S(issue["field"])}: {S(issue["message"])}", error ? NoorTheme.Bad : NoorTheme.Warn);
        }
        var g = m["generator"];
        var made = S(g?["tool"]);
        if (S(g?["version"]) is { Length: > 0 } version) made += version == "unknown" ? " (version unknown)" : " " + version;
        if (S(g?["commit"]) is { Length: > 0 } commit) made += $" ({commit})";
        Line("Made by", made);
        var recipe = m["recipe"];
        if (S(recipe?["file"]) is { Length: > 0 } recipeFile)
            Line("Recipe", $"{recipeFile}, seed {recipe?["seed"]?.ToJsonString() ?? "?"}");
        if (m["family"] is JsonObject f)
            Line("Family", $"{S(f["name"])}, variant {S(f["variant"])} of {f["count"]}");

        if (m["levels"] is JsonArray levels && levels.Count > 0)
        {
            _details.Children.Add(new TextBlock { Text = $"Levels of detail ({levels.Count}, {Mb(m["level_bytes"]?.GetValue<long>() ?? 0)})",
                                                  Classes = { "section" }, Margin = new Thickness(0, 6, 0, 2) });
            foreach (var l in levels.OfType<JsonObject>())
            {
                var distance = l["max_distance"] is JsonValue dv && dv.TryGetValue<double>(out var dist)
                    ? "up to " + dist.ToString("0.#", CultureInfo.InvariantCulture) + " m" : "any distance";
                var triangles = (l["triangles"]?.GetValue<long>() ?? 0).ToString("N0", CultureInfo.InvariantCulture);
                var what = l["exists"]?.GetValue<bool>() == true
                    ? $"{triangles} triangles · {Mb(l["bytes"]?.GetValue<long>() ?? 0)} · {distance}{(l["billboard"]?.GetValue<bool>() == true ? " · billboard" : "")}"
                    : $"{S(l["file"])} is missing";
                Line($"LOD{l["level"]}", what);
                foreach (var issue in (l["issues"] as JsonArray ?? []).Select(i => S(i))) Note("⚠ " + issue, NoorTheme.Warn);
            }
            _details.Children.Add(new Border { Height = 6 });
        }
        Line("Collision", S(m["collision"]));
        if (m["wind"] is JsonObject)
            Line("Wind", "data in the vertices (kept; NoorEngine doesn't animate wind yet)");
        if (S(g?["determinism"]) is { Length: > 0 } det)
            Line("Repeatable", det switch { "cross_platform" => "on any computer", "same_platform" => "on the same kind of computer", _ => "not promised" });
        Line("Asset id", S(m["id"]));
        if (m["tags"] is JsonArray { Count: > 0 } tags) Line("Tags", string.Join(", ", tags.Select(t => S(t))));
        var lic = m["license"];
        Line("Licence", string.Join(" · ", new[] { S(lic?["spdx"]), S(lic?["author"]), S(lic?["attribution"]) }.Where(s => s.Length > 0)));
        if (S(m["created"]) is { Length: > 0 } created) Line("Made on", created);
        if (m["tool_blocks"] is JsonArray { Count: > 0 } blocks)
            Note("Also kept from the tool: " + string.Join(", ", blocks.Select(b => S(b))), NoorTheme.TextMuted);
    }

    private void Fill()
    {
        var q = _search.Text?.Trim() ?? "";
        var assets = _doc.Assets.Where(a => q.Length == 0 || a.Path.Contains(q, StringComparison.OrdinalIgnoreCase)).ToList();
        if (!_assets.ItemsSource?.Cast<AssetInfo>().SequenceEqual(assets) ?? true) _assets.ItemsSource = assets;
        if (!_scenes.ItemsSource?.Cast<string>().SequenceEqual(_doc.SceneFiles) ?? true) _scenes.ItemsSource = _doc.SceneFiles.ToList();
        _summary.Text = $"{_doc.Assets.Count} assets · {_doc.SceneFiles.Count} scenes";
    }
}
