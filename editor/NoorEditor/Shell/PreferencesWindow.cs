using Avalonia.Platform.Storage;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using NoorEditor.Model;

namespace NoorEditor.Shell;

/// <summary>Edit ▸ Preferences: camera and gizmo settings, and the code editor for scripts; saved per user.</summary>
public sealed class PreferencesWindow : Window
{
    private readonly Preferences _edit;
    private readonly Dictionary<string, TextBox> _fields = new();
    private readonly CheckBox _snap = new() { Content = "Snap while dragging gizmos (hold Ctrl to invert)" };
    private readonly ComboBox _msaa = new() { ItemsSource = new[] { "Off", "2× MSAA", "4× MSAA", "8× MSAA" }, Width = 140, FontSize = 12 };
    private static readonly int[] MsaaSamples = [1, 2, 4, 8];
    private readonly CheckBox _noorCode = new() { Content = "Open scripts in NoorCode, the built-in editor" };
    private readonly ComboBox _editor = new() { HorizontalAlignment = HorizontalAlignment.Stretch, FontSize = 12 };
    private readonly TextBox _program = new() { Classes = { "field" }, PlaceholderText = "the program (.exe)" };
    private readonly TextBox _arguments = new() { Classes = { "field" } };
    private readonly Grid _custom = new() { ColumnDefinitions = new ColumnDefinitions("150,*,Auto"), RowDefinitions = new RowDefinitions("Auto,Auto") };
    private readonly List<CodeEditors.Editor?> _choices = [];  // null: Windows default; last: Other program
    private bool _filling;
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
        _msaa.SelectedIndex = Math.Max(0, Array.IndexOf(MsaaSamples, _edit.Msaa));
        ToolTip.SetTip(_msaa, "Smooths the jagged edges of the scene. More samples cost more GPU time (see Window ▸ Render).");
        _noorCode.IsChecked = _edit.UseNoorCode;
        ToolTip.SetTip(_noorCode, "Off: scripts open straight in the external editor below.");

        BuildEditorChoice();

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
            _msaa.SelectedIndex = Array.IndexOf(MsaaSamples, d.Msaa);
            _noorCode.IsChecked = d.UseNoorCode;
            _editor.SelectedIndex = 0;
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
                new StackPanel
                {
                    Orientation = Orientation.Horizontal, Spacing = 12,
                    Children = { new TextBlock { Text = "Anti-aliasing", Width = 138, FontSize = 12, VerticalAlignment = VerticalAlignment.Center, Classes = { "muted" } }, _msaa },
                },
                new TextBlock { Text = "Scripts", Classes = { "section" }, Margin = new Thickness(0, 6, 0, 0) },
                _noorCode,
                EditorRow(),
                _custom,
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
        _edit.Msaa = MsaaSamples[Math.Clamp(_msaa.SelectedIndex, 0, MsaaSamples.Length - 1)];
        _edit.UseNoorCode = _noorCode.IsChecked == true;
        var choice = _editor.SelectedIndex;
        if (choice <= 0)
        {
            _edit.CodeEditor = "";
            _edit.CodeEditorArguments = CodeEditors.DefaultArguments;
        }
        else
        {
            var program = (_program.Text ?? "").Trim().Trim('"');
            if (!File.Exists(program))
            {
                _error.Text = "Code editor: choose a program that exists (Browse…), or Windows default.";
                _program.Focus();
                return;
            }
            _edit.CodeEditor = program;
            _edit.CodeEditorArguments = string.IsNullOrWhiteSpace(_arguments.Text) ? CodeEditors.DefaultArguments : _arguments.Text.Trim();
        }
        Result = _edit;
        Close();
    }

    private Control EditorRow()
    {
        var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("150,*") };
        var label = new TextBlock { Text = "External editor", FontSize = 12, VerticalAlignment = VerticalAlignment.Center, Classes = { "muted" } };
        Grid.SetColumn(_editor, 1);
        grid.Children.Add(label);
        grid.Children.Add(_editor);
        ToolTip.SetTip(_editor, "NoorCode's \"Open in …\" button uses it (and the Inspector's Open button when NoorCode is off).");
        return grid;
    }

    // Windows default, the editors found on this PC, the one already chosen (if not found), Other program.
    private void BuildEditorChoice()
    {
        _choices.Add(null);
        _editor.Items.Add("Windows default (the app that opens .cs / .luau files)");
        foreach (var e in CodeEditors.Detect())
        {
            _choices.Add(e);
            _editor.Items.Add(e.Name);
        }
        var current = _edit.CodeEditor;
        var selected = 0;
        if (!string.IsNullOrEmpty(current))
        {
            selected = _choices.FindIndex(c => c != null && string.Equals(c.Program, current, StringComparison.OrdinalIgnoreCase));
            if (selected < 0)
            {
                _choices.Add(new CodeEditors.Editor(Path.GetFileNameWithoutExtension(current), current, _edit.CodeEditorArguments));
                _editor.Items.Add(Path.GetFileName(current));
                selected = _choices.Count - 1;
            }
        }
        _choices.Add(null);
        _editor.Items.Add("Other program…");

        var browse = new Button { Content = "Browse…", Classes = { "tool" }, Margin = new Thickness(6, 3, 0, 3) };
        browse.Click += async (_, _) =>
        {
            var files = await StorageProvider.OpenFilePickerAsync(new Avalonia.Platform.Storage.FilePickerOpenOptions
            {
                Title = "Choose your code editor",
                FileTypeFilter = [new Avalonia.Platform.Storage.FilePickerFileType("Programs") { Patterns = ["*.exe"] }],
            });
            if (files.Count > 0 && files[0].TryGetLocalPath() is { } path) _program.Text = path;
        };
        var programLabel = new TextBlock { Text = "Program", FontSize = 12, VerticalAlignment = VerticalAlignment.Center, Classes = { "muted" } };
        var argsLabel = new TextBlock { Text = "Arguments", FontSize = 12, VerticalAlignment = VerticalAlignment.Center, Classes = { "muted" } };
        _program.Margin = new Thickness(0, 3);
        _arguments.Margin = new Thickness(0, 3);
        ToolTip.SetTip(_arguments, "{file} the script, {line} the line, {folder} the project's Scripts folder");
        Grid.SetColumn(_program, 1);
        Grid.SetColumn(browse, 2);
        Grid.SetRow(argsLabel, 1);
        Grid.SetRow(_arguments, 1);
        Grid.SetColumn(_arguments, 1);
        Grid.SetColumnSpan(_arguments, 2);
        _custom.Children.Add(programLabel);
        _custom.Children.Add(_program);
        _custom.Children.Add(browse);
        _custom.Children.Add(argsLabel);
        _custom.Children.Add(_arguments);

        _editor.SelectionChanged += (_, _) =>
        {
            if (_filling) return;
            var i = _editor.SelectedIndex;
            _custom.IsVisible = i > 0;
            if (i > 0 && _choices[i] is { } e)
            {
                _program.Text = e.Program;
                _arguments.Text = e.Arguments;
            }
            else if (i == _choices.Count - 1)
            {
                _program.Text = "";
                _arguments.Text = CodeEditors.DefaultArguments;
            }
        };
        _filling = true;
        _editor.SelectedIndex = selected;
        _filling = false;
        _custom.IsVisible = selected > 0;
        _program.Text = _edit.CodeEditor;
        _arguments.Text = _edit.CodeEditorArguments;
    }

    private static double Get(Preferences p, string key) => (double)typeof(Preferences).GetProperty(key)!.GetValue(p)!;
    private static void Set(Preferences p, string key, double v) => typeof(Preferences).GetProperty(key)!.SetValue(p, v);
}
