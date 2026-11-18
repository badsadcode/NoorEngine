using System.Text.Json;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using NoorEditor.Model;
using NoorEditor.Shell;
using Transform = NoorEditor.Model.Transform;

namespace NoorEditor.Panels;

/// <summary>
/// Shows the selected entity. Name and transform are editable; each committed field (Enter or
/// leaving the box) becomes one engine command, so it is one undo step. Components are edited
/// by <see cref="ComponentsEditor"/>.
/// </summary>
public sealed class InspectorPanel : ScrollViewer
{
    public enum Row { Position = 0, Rotation = 1, Scale = 2 }

    private readonly EditorDocument _doc;
    private readonly StackPanel _body = new() { Margin = new Thickness(10, 8), Spacing = 6 };
    private readonly TextBlock _placeholder = new() { Classes = { "muted" }, FontSize = 12, TextWrapping = TextWrapping.Wrap, Text = "Select an entity in the Scene panel." };
    private readonly StackPanel _content = new() { Spacing = 6 };
    private readonly TextBox _name = new() { Classes = { "field" } };
    private readonly TextBlock _kind = new() { FontSize = 11, Classes = { "muted" } };
    private readonly TextBlock _prefabNote = new() { FontSize = 11, TextWrapping = TextWrapping.Wrap, Foreground = NoorTheme.Brush(NoorTheme.Warn) };
    private readonly TextBox[,] _fields = new TextBox[3, 3];
    private readonly TextBlock _details = new() { FontSize = 11, TextWrapping = TextWrapping.Wrap, Classes = { "muted" } };
    private readonly ComponentsEditor _components;

    /// <summary>The component list (smoke test access).</summary>
    public ComponentsEditor Components => _components;
    private bool _loading;

    public event Action<string>? FrameRequested;

    public InspectorPanel(EditorDocument doc)
    {
        _doc = doc;
        _components = new ComponentsEditor(doc);
        Content = _body;
        _body.Children.Add(_placeholder);
        _body.Children.Add(_content);

        var frame = new Button { Content = "Frame", Classes = { "tool" }, VerticalAlignment = VerticalAlignment.Center };
        frame.Click += (_, _) => { if (_doc.SelectedId is { } id) FrameRequested?.Invoke(id); };
        var nameRow = new DockPanel();
        DockPanel.SetDock(frame, Avalonia.Controls.Dock.Right);
        nameRow.Children.Add(frame);
        nameRow.Children.Add(_name);
        _content.Children.Add(nameRow);
        _content.Children.Add(_kind);
        _content.Children.Add(_prefabNote);
        Hook(_name, () => CommitName());

        _content.Children.Add(new TextBlock { Text = "Transform", Classes = { "section" }, Margin = new Thickness(0, 8, 0, 0) });
        var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("72,*,*,*"), RowDefinitions = new RowDefinitions("Auto,Auto,Auto") };
        string[] labels = ["Position", "Rotation°", "Scale"];
        Color[] axis = [NoorTheme.AxisX, NoorTheme.AxisY, NoorTheme.AxisZ];
        for (var r = 0; r < 3; r++)
        {
            var label = new TextBlock { Text = labels[r], FontSize = 12, VerticalAlignment = VerticalAlignment.Center, Classes = { "muted" } };
            Grid.SetRow(label, r);
            grid.Children.Add(label);
            for (var c = 0; c < 3; c++)
            {
                var box = new TextBox
                {
                    Classes = { "field" }, Margin = new Thickness(2),
                    BorderBrush = NoorTheme.Brush(axis[c]), BorderThickness = new Thickness(0, 0, 0, 2),
                };
                ToolTip.SetTip(box, $"{labels[r]} {"XYZ"[c]}");
                var (row, col) = (r, c);
                Hook(box, () => CommitTransform((Row)row, col));
                Grid.SetRow(box, r);
                Grid.SetColumn(box, c + 1);
                grid.Children.Add(box);
                _fields[r, c] = box;
            }
        }
        _content.Children.Add(grid);

        _content.Children.Add(new TextBlock { Text = "Entity", Classes = { "section" }, Margin = new Thickness(0, 8, 0, 0) });
        _content.Children.Add(_details);
        _content.Children.Add(new TextBlock { Text = "Components", Classes = { "section" }, Margin = new Thickness(0, 8, 0, 0) });
        _content.Children.Add(_components);

    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        _doc.SelectionChanged += Load;
        _doc.SceneChanged += Load;
        _doc.SculptChanged += Load;
        Load();
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnDetachedFromVisualTree(e);
        _doc.SelectionChanged -= Load;
        _doc.SceneChanged -= Load;
        _doc.SculptChanged -= Load;
    }

    // ---- Used by the editor smoke test: the same path a user's typing takes. ----

    public void TypeField(Row row, int axis, string text)
    {
        _fields[(int)row, axis].Text = text;
        CommitTransform(row, axis);
    }

    public void TypeName(string text)
    {
        _name.Text = text;
        CommitName();
    }

    public string FieldText(Row row, int axis) => _fields[(int)row, axis].Text ?? "";

    // ----

    private void Hook(TextBox box, Action commit)
    {
        box.KeyDown += (_, e) =>
        {
            if (e.Key == Key.Enter) { commit(); e.Handled = true; }
            else if (e.Key == Key.Escape) { Load(); e.Handled = true; }
        };
        box.LostFocus += (_, _) => commit();
    }

    private void Load()
    {
        _loading = true;
        try
        {
            var e = _doc.Selected;
            _placeholder.IsVisible = e == null;
            _content.IsVisible = e != null;
            if (e == null) return;

            _name.Text = e.Name;
            _kind.Text = e.Kind;
            _prefabNote.IsVisible = e.FromPrefab || _doc.Playing;
            _prefabNote.Text = _doc.Playing
                ? "Playing: these are the running values. Stop (Ctrl+P) to edit."
                : "Part of a prefab instance: read-only here. Edit the prefab file, or select the instance root.";
            var editable = !e.FromPrefab && !_doc.Playing;
            _name.IsEnabled = editable;

            var t = e.Transform;
            var euler = t.Rotation.ToEulerDegrees();
            Vec3[] rows = [t.Position, euler, t.Scale];
            for (var r = 0; r < 3; r++)
                for (var c = 0; c < 3; c++)
                {
                    _fields[r, c].Text = Num.Format(rows[r][c]);
                    _fields[r, c].IsEnabled = editable;
                }

            var tags = e.Tags.Count > 0 ? string.Join(", ", e.Tags) : "none";
            _details.Text = $"Id  {e.Id}\nLayer  {(e.Layer.Length > 0 ? e.Layer : "default")}\nTags  {tags}" +
                            (e.Instance != null ? $"\nInstance  {e.Instance}" : "");
            _components.Show(e);
        }
        finally
        {
            _loading = false;
        }
    }

    private void CommitName()
    {
        if (_loading || _doc.Selected is not { FromPrefab: false } e) return;
        var name = _name.Text?.Trim() ?? "";
        if (name.Length == 0 || name == e.Name)
        {
            _name.Text = e.Name;
            return;
        }
        _doc.Rename(e.Id, name);
    }

    private void CommitTransform(Row row, int axis)
    {
        if (_loading || _doc.Selected is not { FromPrefab: false } e) return;
        var box = _fields[(int)row, axis];
        var t = e.Transform;
        if (!Num.TryParse(box.Text, out var value))
        {
            Load();  // not a number: put the current value back
            return;
        }

        Transform next;
        switch (row)
        {
            case Row.Position:
                if (value == t.Position[axis]) return;
                next = t with { Position = t.Position.With(axis, value) };
                break;
            case Row.Scale:
                if (value == t.Scale[axis]) return;
                next = t with { Scale = t.Scale.With(axis, value) };
                break;
            default:
                // Use the three angles as displayed, so editing one never re-solves the others.
                var current = t.Rotation.ToEulerDegrees();
                if (Math.Abs(value - current[axis]) < 1e-4) return;
                var angles = new Vec3(Parse(0, current.X), Parse(1, current.Y), Parse(2, current.Z)).With(axis, value);
                next = t with { Rotation = Quat.FromEulerDegrees(angles) };
                break;
        }
        _doc.SetTransform(e.Id, next);
    }

    private double Parse(int axis, double fallback) => Num.TryParse(_fields[(int)Row.Rotation, axis].Text, out var v) ? v : fallback;
}
