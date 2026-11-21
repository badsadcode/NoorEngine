using System.Text.Json.Nodes;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;
using NoorEditor.Interop;
using NoorEditor.Model;

namespace NoorEditor.Panels;

/// <summary>
/// The UI tab (gameplay foundations, part 6d): the project's .noorui pages, built without writing markup. Pick a page or
/// make one from a template; the viewport shows it over the scene. Click an element in the viewport or in the tree to
/// select it (it's outlined), change its id, classes, text and common styles, add widgets inside it, move or delete it.
/// Every change is written to the page's file at once, keeps the rest of the file as it was, and can be undone. Edit
/// text in NoorCode opens the page for anything the tab doesn't offer; the viewport follows the file either way.
/// </summary>
public sealed class UiPanel : DockPanel
{
    private static readonly string[] StyleProperties =
        { "width", "height", "margin", "padding", "color", "background-color", "font-size", "border-radius", "text-align" };

    private readonly EditorDocument _doc;
    private readonly ComboBox _pages = new() { MinWidth = 200 };
    private readonly ComboBox _template = new() { MinWidth = 120 };
    private readonly TextBox _newName = new() { Width = 150, PlaceholderText = "UI/menu.noorui" };
    private readonly Button _create = new() { Content = "New page", Classes = { "tool" } };
    private readonly Button _undo = new() { Content = "Undo", Classes = { "tool" } };
    private readonly Button _redo = new() { Content = "Redo", Classes = { "tool" } };
    private readonly Button _code = new() { Content = "Edit text in NoorCode", Classes = { "tool" } };
    private readonly Button _place = new() { Content = "Put in the scene", Classes = { "tool" } };
    private readonly TreeView _tree = new() { MinWidth = 240 };
    private readonly TextBlock _selected = new() { FontWeight = FontWeight.SemiBold };
    private readonly TextBox _id = new() { Width = 220 };
    private readonly TextBox _class = new() { Width = 220 };
    private readonly TextBox _text = new() { Width = 220 };
    private readonly Dictionary<string, TextBox> _styles = new();
    private readonly WrapPanel _palette = new();
    private readonly Button _up = new() { Content = "Move up", Classes = { "tool" } };
    private readonly Button _down = new() { Content = "Move down", Classes = { "tool" } };
    private readonly Button _delete = new() { Content = "Delete", Classes = { "tool" } };
    private readonly TextBlock _status = new() { FontSize = 11, Classes = { "muted" }, TextWrapping = TextWrapping.Wrap };
    private JsonObject? _element;  // the selected element's node in the tree
    private bool _syncing;
    // The page's file as the tree was read from it: changed elsewhere (NoorCode), the tree is read again, and an edit
    // made from the old tree is refused (its paths could point at other elements now).
    private DateTime _readAt;
    private readonly DispatcherTimer _watch = new() { Interval = TimeSpan.FromSeconds(1) };

    /// <summary>The page shown and its element tree as last read (tests look at them).</summary>
    public string? Page => _doc.UiPage;
    public JsonObject? Tree { get; private set; }
    public JsonObject? Element => _element;
    /// <summary>Opens a file in NoorCode (the main window sets it).</summary>
    public Action<string>? OpenFile { get; set; }

    public UiPanel(EditorDocument doc)
    {
        _doc = doc;
        var catalog = _doc.Ui(new JsonObject { ["op"] = "templates" });
        foreach (var t in catalog?["templates"] as JsonArray ?? [])
            _template.Items.Add(new ComboBoxItem { Content = t?["label"]?.GetValue<string>(), Tag = t?["name"]?.GetValue<string>() });
        _template.SelectedIndex = Math.Min(1, _template.ItemCount - 1);  // the pause menu: a good first page
        foreach (var w in catalog?["palette"] as JsonArray ?? [])
        {
            var name = w?["name"]?.GetValue<string>() ?? "";
            var button = new Button { Content = w?["label"]?.GetValue<string>(), Classes = { "tool" }, Margin = new Thickness(0, 0, 4, 4) };
            ToolTip.SetTip(button, $"Adds a {button.Content} inside the selected element (the page's body if nothing is selected)");
            button.Click += (_, _) => Insert(name);
            _palette.Children.Add(button);
        }
        ToolTip.SetTip(_create, "Makes the page from the template (the name is a path in the project, ending in .noorui)");
        ToolTip.SetTip(_code, "Opens the page's markup and style sheet: anything the tab doesn't offer. The viewport follows the file.");
        _pages.SelectionChanged += (_, _) => { if (!_syncing && _pages.SelectedItem is string p) Open(p); };
        _create.Click += (_, _) =>
        {
            var name = _newName.Text?.Trim() ?? "";
            if (name.Length == 0) name = "UI/menu.noorui";
            if (!name.EndsWith(".noorui", StringComparison.OrdinalIgnoreCase)) name += ".noorui";
            NewPage(name, (_template.SelectedItem as ComboBoxItem)?.Tag as string ?? "blank");
        };
        _undo.Click += (_, _) => Undo();
        _redo.Click += (_, _) => Redo();
        _code.Click += (_, _) => { if (Page != null) OpenFile?.Invoke(Page); };
        ToolTip.SetTip(_place, "Adds an entity with this page as its UI document: the game shows it (scripts can hide it and show it)");
        _place.Click += (_, _) => PutInScene();
        _tree.SelectionChanged += (_, _) =>
        {
            if (!_syncing && _tree.SelectedItem is TreeViewItem { Tag: JsonObject node }) _doc.SelectUiElement(node["path"] as JsonArray);
        };
        _up.Click += (_, _) => Move(-1);
        _down.Click += (_, _) => Move(1);
        _delete.Click += (_, _) => Remove();
        Apply(_id, () => SetAttribute("id", _id.Text));
        Apply(_class, () => SetAttribute("class", _class.Text));
        Apply(_text, () => SetText(_text.Text ?? ""));

        var top = new WrapPanel { Margin = new Thickness(6) };
        foreach (var c in new Control[] { new TextBlock { Text = "Page", VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 0, 6, 0) },
                                           _pages, Gap(), _template, _newName, _create, Gap(), _undo, _redo, Gap(), _code, _place })
            top.Children.Add(c);
        SetDock(top, Avalonia.Controls.Dock.Top);
        Children.Add(top);
        SetDock(_status, Avalonia.Controls.Dock.Bottom);
        _status.Margin = new Thickness(6, 2);
        Children.Add(_status);

        var inspector = new StackPanel { Spacing = 6, Margin = new Thickness(10, 4) };
        inspector.Children.Add(_selected);
        inspector.Children.Add(Row("Id", _id));
        inspector.Children.Add(Row("Class", _class));
        inspector.Children.Add(Row("Text", _text));
        inspector.Children.Add(new TextBlock { Text = "Style", FontWeight = FontWeight.SemiBold, Margin = new Thickness(0, 6, 0, 0) });
        foreach (var p in StyleProperties)
        {
            var box = new TextBox { Width = 220, PlaceholderText = p switch { "color" or "background-color" => "#RRGGBB[AA]", "text-align" => "left, center, right", _ => "e.g. 24dp" } };
            _styles[p] = box;
            Apply(box, () => SetStyle(p, box.Text ?? ""));
            inspector.Children.Add(Row(p, box));
        }
        inspector.Children.Add(new TextBlock { Text = "Add inside", FontWeight = FontWeight.SemiBold, Margin = new Thickness(0, 6, 0, 0) });
        inspector.Children.Add(_palette);
        var arrange = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 4 };
        arrange.Children.Add(_up);
        arrange.Children.Add(_down);
        arrange.Children.Add(_delete);
        inspector.Children.Add(arrange);

        var tree = new Border { BorderThickness = new Thickness(0, 0, 1, 0), BorderBrush = Brushes.DimGray, Child = _tree, Width = 280 };
        SetDock(tree, Avalonia.Controls.Dock.Left);
        Children.Add(tree);
        Children.Add(new ScrollViewer { Content = inspector });
        RefreshPages();
        Sync();
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        _doc.UiChanged += OnUiChanged;
        _watch.Tick += OnWatch;
        _watch.Start();
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnDetachedFromVisualTree(e);
        _doc.UiChanged -= OnUiChanged;
        _watch.Stop();
        _watch.Tick -= OnWatch;
    }

    private DateTime FileTime() => Page == null ? default : File.GetLastWriteTimeUtc(System.IO.Path.Combine(_doc.ProjectRoot, Page));

    private bool ChangedElsewhere() => Page != null && FileTime() != _readAt;

    private void OnWatch(object? sender, EventArgs e)
    {
        if (!ChangedElsewhere()) return;
        ReadTree();
        _status.Text = "The page changed outside the tab: read again";
        Sync();
    }

    private void ReadTree()
    {
        if (Page == null) return;
        var page = _doc.Ui(new JsonObject { ["op"] = "tree", ["path"] = Page });
        Tree = page?["tree"] as JsonObject;
        _readAt = FileTime();
        if (page?["error"]?.GetValue<string>() is { } error) _status.Text = $"The page doesn't read: {error} (fix it in NoorCode)";
    }

    private static Control Gap() => new Border { Width = 12 };

    private static Control Row(string label, Control field)
    {
        var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
        row.Children.Add(new TextBlock { Text = label, Width = 120, VerticalAlignment = VerticalAlignment.Center });
        row.Children.Add(field);
        return row;
    }

    // A text field applies its value on Enter and when it loses focus.
    private void Apply(TextBox box, Action apply)
    {
        box.KeyDown += (_, e) => { if (e.Key == Key.Enter && !_syncing) apply(); };
        box.LostFocus += (_, _) => { if (!_syncing) apply(); };
    }

    // ---- What the buttons do (public: the smoke test uses them as a user would) ----

    public void RefreshPages()
    {
        _syncing = true;
        _pages.Items.Clear();
        foreach (var p in _doc.Ui(new JsonObject { ["op"] = "list" })?["pages"] as JsonArray ?? [])
            _pages.Items.Add(p!.GetValue<string>());
        _pages.SelectedItem = Page;
        _syncing = false;
    }

    public bool NewPage(string path, string template)
    {
        var made = _doc.Ui(new JsonObject { ["op"] = "new", ["path"] = path, ["template"] = template });
        if (made == null)
        {
            _status.Text = Native.LastError();
            return false;
        }
        Tree = made["tree"] as JsonObject;
        _doc.ShowUiPage(made["path"]!.GetValue<string>());
        _readAt = FileTime();
        RefreshPages();
        _status.Text = $"Made {path} from the {template} template";
        return true;
    }

    public void Open(string path)
    {
        _status.Text = "";
        _doc.ShowUiPage(path);
        ReadTree();
        Sync();
    }

    public void Select(JsonArray? path) => _doc.SelectUiElement(path);

    public bool Insert(string widget)
    {
        var path = _doc.UiSelected ?? new JsonArray();
        // A widget goes inside the selection, or next to it when the selection can't hold one (an input, an image).
        var tag = _element?["tag"]?.GetValue<string>() ?? "body";
        var index = -1;
        if (tag is "input" or "img" && path.Count > 0)
        {
            index = path[^1]!.GetValue<int>() + 1;
            path = new JsonArray(path.Take(path.Count - 1).Select(n => (JsonNode?)n!.GetValue<int>()).ToArray());
        }
        return Edit(new JsonObject { ["op"] = "insert", ["path"] = path.DeepClone(), ["index"] = index, ["widget"] = widget });
    }

    public bool SetAttribute(string name, string? value) =>
        _doc.UiSelected != null && Edit(new JsonObject { ["op"] = "set_attribute", ["path"] = _doc.UiSelected.DeepClone(), ["name"] = name,
                                                         ["value"] = string.IsNullOrWhiteSpace(value) ? null : value.Trim() });

    public bool SetText(string text) =>
        _doc.UiSelected != null && Edit(new JsonObject { ["op"] = "set_text", ["path"] = _doc.UiSelected.DeepClone(), ["text"] = text });

    public bool SetStyle(string property, string value) =>
        _doc.UiSelected != null && Edit(new JsonObject { ["op"] = "set_style", ["path"] = _doc.UiSelected.DeepClone(), ["property"] = property, ["value"] = value.Trim() });

    public bool Move(int by) =>
        _doc.UiSelected is { Count: > 0 } && Edit(new JsonObject { ["op"] = "move", ["path"] = _doc.UiSelected.DeepClone(), ["by"] = by });

    public bool Remove()
    {
        if (_doc.UiSelected is not { Count: > 0 } path) return false;
        var parent = new JsonArray(path.Take(path.Count - 1).Select(n => (JsonNode?)n!.GetValue<int>()).ToArray());
        if (!Edit(new JsonObject { ["op"] = "remove", ["path"] = path.DeepClone() })) return false;
        _doc.SelectUiElement(parent);
        return true;
    }

    /// <summary>An entity named after the page, with it as its ui_document (one undo step). Its id, or null.</summary>
    public string? PutInScene()
    {
        if (Page == null) return null;
        var name = System.IO.Path.GetFileNameWithoutExtension(Page);
        var id = _doc.Create(name.Length > 0 ? char.ToUpperInvariant(name[0]) + name[1..] : "UI",
                             new JsonObject { ["ui_document"] = new JsonObject { ["document"] = Page, ["visible"] = true } }, new Vec3(0, 0, 0));
        _status.Text = id != null ? $"Added {name} to the scene: Play to see it in the game" : Native.LastError();
        return id;
    }

    public bool Undo() => Step("undo");
    public bool Redo() => Step("redo");

    private bool Step(string op)
    {
        if (Page == null) return false;
        var r = _doc.Ui(new JsonObject { ["op"] = op, ["path"] = Page });
        if (r == null) return false;
        Tree = r["tree"] as JsonObject;
        _readAt = FileTime();
        _doc.SelectUiElement(Find(_doc.UiSelected) != null ? _doc.UiSelected : null);
        return true;
    }

    private bool Edit(JsonObject edit)
    {
        if (Page == null)
        {
            _status.Text = "Pick a page or make one first";
            return false;
        }
        if (ChangedElsewhere())
        {
            ReadTree();
            _doc.SelectUiElement(null);
            _status.Text = "The page changed outside the tab: it's read again, select the element again";
            return false;
        }
        var r = _doc.Ui(new JsonObject { ["op"] = "edit", ["path"] = Page, ["edit"] = edit });
        if (r == null)
        {
            _status.Text = Native.LastError();
            Sync();  // the fields back to what the page says
            return false;
        }
        Tree = r["tree"] as JsonObject;
        _readAt = FileTime();
        _status.Text = "";
        _doc.SelectUiElement(r["selected"] as JsonArray ?? _doc.UiSelected);
        return true;
    }

    // ---- Showing it ----

    private void OnUiChanged()
    {
        if (Page != null && (Tree == null || _pages.SelectedItem as string != Page))
        {
            ReadTree();
            RefreshPages();
        }
        Sync();
    }

    private JsonObject? Find(JsonArray? path)
    {
        if (Tree == null || path == null) return null;
        var node = Tree;
        foreach (var i in path)
        {
            var children = node["children"] as JsonArray;
            var k = i!.GetValue<int>();
            if (children == null || k < 0 || k >= children.Count) return null;
            node = (JsonObject)children[k]!;
        }
        return node;
    }

    private void Sync()
    {
        _syncing = true;
        _element = Find(_doc.UiSelected);
        _tree.Items.Clear();
        TreeViewItem? selectedItem = null;
        if (Tree != null) _tree.Items.Add(Item(Tree, ref selectedItem));
        if (selectedItem != null) _tree.SelectedItem = selectedItem;
        var el = _element;
        _selected.Text = el == null ? "Nothing selected: click an element in the viewport or the tree" : Describe(el);
        _id.Text = el?["id"]?.GetValue<string>() ?? "";
        _class.Text = el?["class"]?.GetValue<string>() ?? "";
        _text.Text = el?["text"]?.GetValue<string>() ?? "";
        var hasChildren = (el?["children"] as JsonArray)?.Count > 0;
        _text.IsEnabled = el != null && !hasChildren && el["tag"]?.GetValue<string>() is not ("input" or "img");
        var style = Declarations(el?["style"]?.GetValue<string>() ?? "");
        foreach (var (p, box) in _styles)
        {
            box.Text = style.TryGetValue(p, out var v) ? v : "";
            box.IsEnabled = el != null;
        }
        _id.IsEnabled = _class.IsEnabled = el != null;
        var path = _doc.UiSelected;
        _up.IsEnabled = _down.IsEnabled = _delete.IsEnabled = path is { Count: > 0 };
        _palette.IsEnabled = Page != null;
        _undo.IsEnabled = _redo.IsEnabled = _code.IsEnabled = _place.IsEnabled = Page != null;
        _syncing = false;
    }

    private TreeViewItem Item(JsonObject node, ref TreeViewItem? selected)
    {
        var item = new TreeViewItem { Header = Describe(node), Tag = node, IsExpanded = true };
        if (Same(node["path"] as JsonArray, _doc.UiSelected)) selected = item;
        foreach (var c in node["children"] as JsonArray ?? [])
            if (c is JsonObject child) item.Items.Add(Item(child, ref selected));
        return item;
    }

    private static bool Same(JsonArray? a, JsonArray? b) =>
        a != null && b != null && a.Count == b.Count && a.Zip(b).All(p => p.First!.GetValue<int>() == p.Second!.GetValue<int>());

    private static string Describe(JsonObject node)
    {
        var text = $"<{node["tag"]}>";
        if (node["id"]?.GetValue<string>() is { Length: > 0 } id) text += $" #{id}";
        if (node["class"]?.GetValue<string>() is { Length: > 0 } cls) text += $" .{cls.Replace(" ", " .")}";
        if (node["text"]?.GetValue<string>() is { Length: > 0 } t) text += $"  \"{(t.Length > 24 ? t[..24] + "…" : t)}\"";
        return text;
    }

    private static Dictionary<string, string> Declarations(string style)
    {
        var d = new Dictionary<string, string>();
        foreach (var part in style.Split(';'))
        {
            var colon = part.IndexOf(':');
            if (colon > 0) d[part[..colon].Trim().ToLowerInvariant()] = part[(colon + 1)..].Trim();
        }
        return d;
    }
}
