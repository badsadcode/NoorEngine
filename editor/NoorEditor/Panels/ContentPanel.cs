using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Templates;
using Avalonia.Layout;
using Avalonia.Media;
using NoorEditor.Model;
using NoorEditor.Shell;

namespace NoorEditor.Panels;

/// <summary>
/// Scenes and importable assets of the project (from the engine's asset registry).
/// Double-click a scene to open it; double-click a model or prefab to add it to the open scene.
/// </summary>
public sealed class ContentPanel : DockPanel
{
    private readonly EditorDocument _doc;
    private readonly TextBox _search = new() { PlaceholderText = "Search assets", Classes = { "field" }, Width = 220 };
    private readonly ListBox _scenes = new() { Background = Brushes.Transparent };
    private readonly ListBox _assets = new() { Background = Brushes.Transparent };
    private readonly TextBlock _summary = new() { FontSize = 11, Classes = { "muted" }, VerticalAlignment = VerticalAlignment.Center };

    public event Action<AssetInfo>? AddRequested;
    public event Action<string>? OpenSceneRequested;

    public ContentPanel(EditorDocument doc)
    {
        _doc = doc;

        var rescan = new Button { Content = "Rescan", Classes = { "tool" } };
        ToolTip.SetTip(rescan, "Scan the Assets folder for new, moved or changed files");
        rescan.Click += (_, _) => _doc.RefreshAssets(rescan: true);
        var add = new Button { Content = "Add to scene", Classes = { "tool" } };
        add.Click += (_, _) => { if (_assets.SelectedItem is AssetInfo a) AddRequested?.Invoke(a); };
        var bar = new StackPanel
        {
            Orientation = Orientation.Horizontal, Spacing = 6, Margin = new Thickness(6, 6, 6, 4),
            Children = { _search, rescan, add, _summary },
        };
        SetDock(bar, Avalonia.Controls.Dock.Top);
        Children.Add(bar);

        _scenes.ItemTemplate = new FuncDataTemplate<string>((path, _) => Row("◇", NoorTheme.Cyan, path ?? "", "Scene"));
        _assets.ItemTemplate = new FuncDataTemplate<AssetInfo>((a, _) => a == null ? new TextBlock() :
            Row(a.Kind switch { "Prefab" => "◆", "Sound" => "♪", _ => "▣" }, a.Kind == "Prefab" ? NoorTheme.Cyan : NoorTheme.Accent, a.Path, a.Kind));
        _scenes.DoubleTapped += (_, _) => { if (_scenes.SelectedItem is string s) OpenSceneRequested?.Invoke(s); };
        _assets.DoubleTapped += (_, _) => { if (_assets.SelectedItem is AssetInfo a) AddRequested?.Invoke(a); };

        var scenesHeader = Header("Scenes");
        var assetsHeader = Header("Assets");
        var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("260,*"), RowDefinitions = new RowDefinitions("Auto,*") };
        Grid.SetColumn(assetsHeader, 1);
        Grid.SetRow(_scenes, 1);
        Grid.SetRow(_assets, 1);
        Grid.SetColumn(_assets, 1);
        grid.Children.Add(scenesHeader);
        grid.Children.Add(assetsHeader);
        grid.Children.Add(_scenes);
        grid.Children.Add(_assets);
        Children.Add(grid);

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

    private void Fill()
    {
        var q = _search.Text?.Trim() ?? "";
        var assets = _doc.Assets.Where(a => q.Length == 0 || a.Path.Contains(q, StringComparison.OrdinalIgnoreCase)).ToList();
        if (!_assets.ItemsSource?.Cast<AssetInfo>().SequenceEqual(assets) ?? true) _assets.ItemsSource = assets;
        if (!_scenes.ItemsSource?.Cast<string>().SequenceEqual(_doc.SceneFiles) ?? true) _scenes.ItemsSource = _doc.SceneFiles.ToList();
        _summary.Text = $"{_doc.Assets.Count} assets · {_doc.SceneFiles.Count} scenes";
    }
}
