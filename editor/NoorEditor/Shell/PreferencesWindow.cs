using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using NoorEditor.Model;

namespace NoorEditor.Shell;

/// <summary>Edit ▸ Preferences: camera and gizmo settings, saved per user.</summary>
public sealed class PreferencesWindow : Window
{
    private readonly Preferences _edit;
    private readonly Dictionary<string, TextBox> _fields = new();
    private readonly CheckBox _snap = new() { Content = "Snap while dragging gizmos (hold Ctrl to invert)" };
    private readonly TextBlock _error = new() { FontSize = 12, Foreground = NoorTheme.Brush(NoorTheme.Bad), TextWrapping = Avalonia.Media.TextWrapping.Wrap };

    /// <summary>The accepted preferences, or null if cancelled.</summary>
    public Preferences? Result { get; private set; }

    private static readonly (string Key, string Label, string Unit, double Min, double Max)[] Rows =
    [
        ("FlySpeed", "Fly speed", "m/s", 0.25, 2000),
        ("LookSensitivity", "Look sensitivity", "×", 0.1, 5),
        ("GizmoScale", "Gizmo size", "×", 0.3, 3),
        ("MoveStep", "Move snap", "m", 0.001, 1000),
        ("RotateStep", "Rotate snap", "°", 0.1, 180),
        ("ScaleStep", "Scale snap", "", 0.001, 100),
    ];

    public PreferencesWindow(Preferences current)
    {
        _edit = current.Copy();
        Title = "Preferences";
        Width = 460;
        SizeToContent = SizeToContent.Height;
        CanResize = false;
        ShowInTaskbar = false;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        Background = NoorTheme.Brush(NoorTheme.Panel);

        var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("150,*,40"), Margin = new Thickness(0, 4) };
        for (var i = 0; i < Rows.Length; i++)
        {
            var row = Rows[i];
            grid.RowDefinitions.Add(new RowDefinition(GridLength.Auto));
            var label = new TextBlock { Text = row.Label, FontSize = 12, VerticalAlignment = VerticalAlignment.Center, Classes = { "muted" } };
            var box = new TextBox { Classes = { "field" }, Margin = new Thickness(0, 3), Text = Num.Format(Get(_edit, row.Key)) };
            var unit = new TextBlock { Text = row.Unit, FontSize = 12, Margin = new Thickness(8, 0, 0, 0), VerticalAlignment = VerticalAlignment.Center, Classes = { "muted" } };
            Grid.SetRow(label, i);
            Grid.SetRow(box, i);
            Grid.SetColumn(box, 1);
            Grid.SetRow(unit, i);
            Grid.SetColumn(unit, 2);
            grid.Children.Add(label);
            grid.Children.Add(box);
            grid.Children.Add(unit);
            _fields[row.Key] = box;
        }
        _snap.IsChecked = _edit.Snap;

        var ok = new Button { Content = "OK", Classes = { "primary" }, IsDefault = true };
        var cancel = new Button { Content = "Cancel", Classes = { "tool" }, IsCancel = true };
        var defaults = new Button { Content = "Defaults", Classes = { "tool" } };
        ok.Click += (_, _) => Accept();
        cancel.Click += (_, _) => Close();
        defaults.Click += (_, _) =>
        {
            var d = new Preferences();
            foreach (var row in Rows) _fields[row.Key].Text = Num.Format(Get(d, row.Key));
            _snap.IsChecked = d.Snap;
        };
        var buttons = new DockPanel { Children = { defaults, new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, HorizontalAlignment = HorizontalAlignment.Right, Children = { ok, cancel } } } };
        DockPanel.SetDock(defaults, Avalonia.Controls.Dock.Left);

        Content = new StackPanel
        {
            Margin = new Thickness(20, 16),
            Spacing = 10,
            Children =
            {
                new TextBlock { Text = "Viewport", Classes = { "section" } },
                grid,
                _snap,
                _error,
                buttons,
            },
        };
    }

    private void Accept()
    {
        foreach (var row in Rows)
        {
            if (!Num.TryParse(_fields[row.Key].Text, out var v) || v < row.Min || v > row.Max)
            {
                _error.Text = $"{row.Label}: enter a number from {Num.Format(row.Min)} to {Num.Format(row.Max)}.";
                _fields[row.Key].Focus();
                return;
            }
            Set(_edit, row.Key, v);
        }
        _edit.Snap = _snap.IsChecked == true;
        Result = _edit;
        Close();
    }

    private static double Get(Preferences p, string key) => (double)typeof(Preferences).GetProperty(key)!.GetValue(p)!;
    private static void Set(Preferences p, string key, double v) => typeof(Preferences).GetProperty(key)!.SetValue(p, v);
}
