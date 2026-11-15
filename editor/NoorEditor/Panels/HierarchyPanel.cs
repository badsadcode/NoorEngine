using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using NoorEditor.Model;
using NoorEditor.Shell;

namespace NoorEditor.Panels;

/// <summary>
/// The scene tree. Rebuilt from the engine snapshot whenever the scene changes; expansion and
/// selection are kept by entity id. Entities that come from a prefab are shown dimmed (read-only).
/// </summary>
public sealed class HierarchyPanel : DockPanel
{
    private readonly EditorDocument _doc;
    private readonly TreeView _tree = new() { Margin = new Thickness(2, 0) };
    private readonly TextBox _filter = new() { PlaceholderText = "Filter", Classes = { "field" }, Margin = new Thickness(6, 6, 6, 4) };
    private readonly TextBlock _empty = new() { Classes = { "muted" }, FontSize = 12, Margin = new Thickness(10), TextWrapping = TextWrapping.Wrap };
    private readonly Dictionary<string, TreeViewItem> _items = new();
    private readonly HashSet<string> _expanded = new();
    private bool _syncing;

    /// <summary>Raised on double-click: frame this entity in the viewport.</summary>
    public event Action<string>? FrameRequested;

    public HierarchyPanel(EditorDocument doc)
    {
        _doc = doc;
        SetDock(_filter, Avalonia.Controls.Dock.Top);
        Children.Add(_filter);
        SetDock(_empty, Avalonia.Controls.Dock.Top);
        Children.Add(_empty);
        Children.Add(_tree);

        _filter.TextChanged += (_, _) => Rebuild();
        _tree.SelectionChanged += (_, _) =>
        {
            if (_syncing) return;
            _doc.Select((_tree.SelectedItem as TreeViewItem)?.Tag as string);
        };
        _tree.DoubleTapped += (_, e) =>
        {
            if (_doc.SelectedId is { } id) FrameRequested?.Invoke(id);
            e.Handled = true;
        };
        _tree.KeyDown += (_, e) =>
        {
            if (e.Key == Key.Delete && _doc.Selected is { FromPrefab: false } sel)
            {
                _doc.Remove(sel.Id);
                e.Handled = true;
            }
        };

    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        _doc.SceneChanged += Rebuild;
        _doc.SelectionChanged += SyncSelection;
        Rebuild();
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnDetachedFromVisualTree(e);
        _doc.SceneChanged -= Rebuild;
        _doc.SelectionChanged -= SyncSelection;
    }

    /// <summary>Visible rows (for tests).</summary>
    public int ItemCount => _items.Count;

    private void Rebuild()
    {
        var scene = _doc.Scene;
        _empty.IsVisible = !scene.Open || scene.Entities.Count == 0;
        _empty.Text = scene.Open
            ? "Empty scene. Double-click a model in the Content Browser to add it."
            : "No scene open. Use File ▸ New Scene, or double-click a scene in the Content Browser.";

        var filter = _filter.Text?.Trim() ?? "";
        var children = scene.Entities.GroupBy(e => e.Parent ?? "").ToDictionary(g => g.Key, g => g.ToList());
        _items.Clear();

        // With a filter, show matches flat (the tree path is in the tooltip).
        List<TreeViewItem> roots;
        if (filter.Length > 0)
        {
            roots = scene.Entities.Where(e => e.Name.Contains(filter, StringComparison.OrdinalIgnoreCase))
                                  .Select(e => Item(e, null)).ToList();
        }
        else
        {
            roots = (children.GetValueOrDefault("") ?? []).Select(e => Item(e, children)).ToList();
        }

        _syncing = true;
        _tree.ItemsSource = roots;
        _syncing = false;
        SyncSelection();
    }

    private TreeViewItem Item(EntityInfo e, Dictionary<string, List<EntityInfo>>? children)
    {
        var icon = e.Kind switch { "Prefab instance" => "◆", "Mesh" => "▲", "Marker" => "✚", "Model" => "▣", _ => "○" };
        var header = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            Spacing = 6,
            Children =
            {
                new TextBlock { Text = icon, FontSize = 10, VerticalAlignment = VerticalAlignment.Center,
                                Foreground = NoorTheme.Brush(e.Kind == "Prefab instance" ? NoorTheme.Cyan : NoorTheme.Accent) },
                new TextBlock { Text = e.Name.Length > 0 ? e.Name : "(unnamed)", VerticalAlignment = VerticalAlignment.Center,
                                Foreground = NoorTheme.Brush(e.FromPrefab ? NoorTheme.TextMuted : NoorTheme.Text),
                                FontStyle = e.FromPrefab ? FontStyle.Italic : FontStyle.Normal },
            },
        };
        var item = new TreeViewItem { Header = header, Tag = e.Id, IsExpanded = _expanded.Contains(e.Id) };
        ToolTip.SetTip(item, e.FromPrefab ? $"{e.Kind} · from prefab (edit the prefab file to change it)" : e.Kind);
        item.PropertyChanged += (_, args) =>
        {
            if (args.Property != TreeViewItem.IsExpandedProperty) return;
            if (item.IsExpanded) _expanded.Add(e.Id); else _expanded.Remove(e.Id);
        };
        _items[e.Id] = item;
        if (children != null && children.TryGetValue(e.Id, out var kids))
            foreach (var k in kids) item.Items.Add(Item(k, children));
        return item;
    }

    private void SyncSelection()
    {
        _syncing = true;
        try
        {
            if (_doc.SelectedId is { } id && _items.TryGetValue(id, out var item))
            {
                // Expand the ancestors so the selected row is visible.
                for (var p = item.Parent as TreeViewItem; p != null; p = p.Parent as TreeViewItem) p.IsExpanded = true;
                _tree.SelectedItem = item;
                item.BringIntoView();
            }
            else
            {
                _tree.SelectedItem = null;
            }
        }
        finally
        {
            _syncing = false;
        }
    }
}
