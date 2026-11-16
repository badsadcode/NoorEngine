using System.Xml;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using AvaloniaEdit;
using AvaloniaEdit.Document;
using AvaloniaEdit.Highlighting;
using AvaloniaEdit.Highlighting.Xshd;
using AvaloniaEdit.Rendering;
using AvaloniaEdit.Search;
using Dock.Model.Mvvm.Controls;
using NoorEditor.Model;
using NoorEditor.Shell;

namespace NoorEditor.Panels;

/// <summary>An error NoorCode marks in a script: its line and the message.</summary>
public sealed record ScriptError(int Line, string Message);

/// <summary>
/// One script open in NoorCode: a tab in the documents area next to the viewport. The text lives
/// here, not in the view, because Dock re-creates views (floating a tab into its own window) and
/// the unsaved text must survive that.
/// </summary>
public sealed class ScriptDocument : Document
{
    public string ProjectRoot { get; }
    public string RelativePath { get; }
    public string FullPath => Path.Combine(ProjectRoot, RelativePath);
    public TextDocument Text { get; }
    public bool Modified { get; private set; }
    public List<ScriptError> Errors { get; } = new();

    /// <summary>The file changed on disk while there were unsaved edits here.</summary>
    public bool ChangedOnDisk { get; private set; }

    /// <summary>Raised when errors, the modified state or the disk state change (views refresh).</summary>
    public event Action? StateChanged;

    /// <summary>Raised to move the caret (a view shows it; the line is 1-based).</summary>
    public event Action<int>? RevealRequested;

    /// <summary>Asked when a tab with unsaved changes is closed; MainWindow asks the user.</summary>
    public Func<ScriptDocument, Task>? ConfirmClose { get; set; }

    /// <summary>Makes the view (MainWindow sets it; Dock calls it whenever the tab is shown).</summary>
    public Func<ScriptDocument, Control>? CreateView { get; set; }

    /// <summary>The line the caret goes to when the next view appears (opening at an error).</summary>
    public int PendingLine { get; set; }

    private DateTime _diskTime;
    private bool _closeAccepted;

    public ScriptDocument(string projectRoot, string relativePath)
    {
        ProjectRoot = projectRoot;
        RelativePath = relativePath;
        Id = "Script:" + relativePath;
        CanFloat = true;
        CanClose = true;
        Text = new TextDocument(ReadDisk());
        Text.UndoStack.ClearAll();
        Text.TextChanged += (_, _) => SetModified(true);
        _diskTime = DiskTime();
        UpdateTitle();
    }

    public string FileName => Path.GetFileName(RelativePath);

    public bool Save()
    {
        try
        {
            File.WriteAllText(FullPath, Text.Text);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Interop.EngineLog.Error("NoorCode", $"Couldn't save {RelativePath}: {e.Message}");
            return false;
        }
        _diskTime = DiskTime();
        ChangedOnDisk = false;
        Errors.Clear();  // old errors are about the old text; new ones arrive when it reloads
        SetModified(false);
        Interop.EngineLog.Info("NoorCode", $"Saved {RelativePath}");
        return true;
    }

    /// <summary>Re-reads the file (throws away unsaved edits).</summary>
    public void Reload()
    {
        Text.Text = ReadDisk();
        _diskTime = DiskTime();
        ChangedOnDisk = false;
        SetModified(false);
    }

    /// <summary>
    /// Checks the file on disk (the editor polls twice a second). Unchanged here: take the new text
    /// (an outside editor saved). Changed here too: keep ours and say so.
    /// </summary>
    public void PollDisk()
    {
        var time = DiskTime();
        if (time == _diskTime) return;
        _diskTime = time;
        if (!File.Exists(FullPath)) return;
        if (!Modified)
        {
            var text = ReadDisk();
            if (text != Text.Text)
            {
                Text.Text = text;
                SetModified(false);
            }
            return;
        }
        ChangedOnDisk = true;
        StateChanged?.Invoke();
    }

    public void AddError(int line, string message)
    {
        if (Errors.Any(e => e.Line == line && e.Message == message)) return;
        Errors.Add(new ScriptError(line, message));
        StateChanged?.Invoke();
    }

    public void Reveal(int line)
    {
        PendingLine = line;
        RevealRequested?.Invoke(line);
    }

    public override bool OnClose()
    {
        if (!Modified || _closeAccepted || ConfirmClose == null) return true;
        _ = ConfirmClose(this);  // asks, then closes through the factory with AcceptClose()
        return false;
    }

    /// <summary>The next close goes through without asking (the user chose Save or Don't save).</summary>
    public void AcceptClose() => _closeAccepted = true;

    private void SetModified(bool modified)
    {
        if (Modified == modified) { StateChanged?.Invoke(); return; }
        Modified = modified;
        UpdateTitle();
        StateChanged?.Invoke();
    }

    private void UpdateTitle() => Title = Modified ? $"{FileName} •" : FileName;

    private string ReadDisk()
    {
        try
        {
            return File.Exists(FullPath) ? File.ReadAllText(FullPath) : "";
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Interop.EngineLog.Error("NoorCode", $"Couldn't read {RelativePath}: {e.Message}");
            return "";
        }
    }

    private DateTime DiskTime()
    {
        try
        {
            return File.Exists(FullPath) ? File.GetLastWriteTimeUtc(FullPath) : DateTime.MinValue;
        }
        catch (Exception)
        {
            return DateTime.MinValue;
        }
    }
}

/// <summary>
/// NoorCode (Milestone 4, part 7b): the view of one script. AvaloniaEdit with Lua/C# highlighting,
/// line numbers, find (Ctrl+F), error lines marked in red with the message under the editor
/// (click one to jump to it), and a button to open the script in the external editor.
/// </summary>
public sealed class CodePanel : DockPanel
{
    public ScriptDocument Script { get; }
    public TextEditor Editor { get; }

    private readonly TextBlock _status = new() { FontSize = 11, VerticalAlignment = VerticalAlignment.Center, Classes = { "muted" } };
    private readonly StackPanel _errors = new() { Spacing = 2 };
    private readonly Border _errorBox;
    private readonly Border _diskBar;
    private readonly ErrorLines _marker;

    public CodePanel(ScriptDocument script, string externalName, Action<int> openExternal)
    {
        Script = script;
        Editor = new TextEditor
        {
            Document = script.Text,
            FontFamily = new FontFamily("Cascadia Mono, Cascadia Code, Consolas, monospace"),
            FontSize = 13,
            ShowLineNumbers = true,
            Background = NoorTheme.Brush(NoorTheme.Background),
            Foreground = NoorTheme.Brush(NoorTheme.Text),
            LineNumbersForeground = NoorTheme.Brush(Color.Parse("#5C5677")),
            SyntaxHighlighting = Highlighting(script.RelativePath),
            HorizontalScrollBarVisibility = Avalonia.Controls.Primitives.ScrollBarVisibility.Auto,
            VerticalScrollBarVisibility = Avalonia.Controls.Primitives.ScrollBarVisibility.Auto,
            Padding = new Thickness(4, 2),
        };
        Editor.Options.ConvertTabsToSpaces = true;
        Editor.Options.IndentationSize = 4;
        Editor.Options.HighlightCurrentLine = true;
        Editor.Options.EnableHyperlinks = false;
        Editor.Options.EnableEmailHyperlinks = false;
        Editor.TextArea.TextView.CurrentLineBackground = new SolidColorBrush(Color.Parse("#1C1928"));
        Editor.TextArea.TextView.CurrentLineBorder = new Pen(Brushes.Transparent);
        Editor.TextArea.SelectionBrush = new SolidColorBrush(Color.Parse("#557C4DFF"));
        Editor.TextArea.SelectionForeground = null;
        Editor.TextArea.Caret.CaretBrush = NoorTheme.Brush(NoorTheme.Text);
        SearchPanel.Install(Editor);
        _marker = new ErrorLines(script);
        Editor.TextArea.TextView.BackgroundRenderers.Add(_marker);
        Editor.TextArea.Caret.PositionChanged += (_, _) => UpdateStatus();

        var save = new Button { Content = "Save", Classes = { "tool" }, FontSize = 11 };
        ToolTip.SetTip(save, "Save (Ctrl+S). A running game picks up the change straight away.");
        save.Click += (_, _) => script.Save();
        var external = new Button { Content = $"Open in {externalName}", Classes = { "tool" }, FontSize = 11 };
        external.Click += (_, _) =>
        {
            if (script.Modified) script.Save();
            openExternal(Editor.TextArea.Caret.Line);
        };
        var path = new TextBlock { Text = script.RelativePath, FontSize = 11, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(4, 0, 12, 0) };
        var bar = new DockPanel { Margin = new Thickness(6, 3), Children = { path, new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6, HorizontalAlignment = HorizontalAlignment.Right, Children = { _status, save, external } } } };
        DockPanel.SetDock(path, Avalonia.Controls.Dock.Left);

        var reload = new Button { Content = "Load theirs", Classes = { "tool" }, FontSize = 11 };
        reload.Click += (_, _) => script.Reload();
        var keep = new Button { Content = "Keep mine", Classes = { "tool" }, FontSize = 11 };
        keep.Click += (_, _) => script.Save();
        _diskBar = new Border
        {
            Background = new SolidColorBrush(Color.Parse("#3A2E10")),
            Padding = new Thickness(8, 4),
            Child = new StackPanel
            {
                Orientation = Orientation.Horizontal, Spacing = 8,
                Children =
                {
                    new TextBlock { Text = "The file was changed outside NoorCode, and you have unsaved changes here.", FontSize = 11, VerticalAlignment = VerticalAlignment.Center, Foreground = NoorTheme.Brush(NoorTheme.Warn) },
                    reload, keep,
                },
            },
        };

        _errorBox = new Border
        {
            Background = new SolidColorBrush(Color.Parse("#2A1620")),
            BorderBrush = NoorTheme.Brush(NoorTheme.Border),
            BorderThickness = new Thickness(0, 1, 0, 0),
            Padding = new Thickness(6, 4),
            MaxHeight = 120,
            Child = new ScrollViewer { Content = _errors },
        };

        SetDock(bar, Avalonia.Controls.Dock.Top);
        SetDock(_diskBar, Avalonia.Controls.Dock.Top);
        SetDock(_errorBox, Avalonia.Controls.Dock.Bottom);
        Children.Add(bar);
        Children.Add(_diskBar);
        Children.Add(_errorBox);
        Children.Add(Editor);
        Background = NoorTheme.Brush(NoorTheme.Background);
        Refresh();
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        Script.StateChanged += Refresh;
        Script.RevealRequested += GoTo;
        var line = Script.PendingLine;
        Avalonia.Threading.Dispatcher.UIThread.Post(() =>
        {
            if (line > 0) GoTo(line);
            Editor.Focus();
        }, Avalonia.Threading.DispatcherPriority.Loaded);
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnDetachedFromVisualTree(e);
        Script.StateChanged -= Refresh;
        Script.RevealRequested -= GoTo;
    }

    /// <summary>Puts the caret at the start of <paramref name="line"/> (1-based) and scrolls to it.</summary>
    public void GoTo(int line)
    {
        line = Math.Clamp(line, 1, Math.Max(1, Script.Text.LineCount));
        var doc = Script.Text.GetLineByNumber(line);
        var text = Script.Text.GetText(doc);
        var indent = text.Length - text.TrimStart().Length;
        Editor.TextArea.Caret.Location = new TextLocation(line, indent + 1);
        Editor.Select(doc.Offset + indent, doc.Length - indent);
        Editor.ScrollTo(line, 1);
        Editor.TextArea.Focus();
        Script.PendingLine = 0;
    }

    private void Refresh()
    {
        _diskBar.IsVisible = Script.ChangedOnDisk;
        _errors.Children.Clear();
        foreach (var error in Script.Errors.OrderBy(e => e.Line))
        {
            var item = new Button
            {
                Classes = { "tool" },
                HorizontalAlignment = HorizontalAlignment.Stretch,
                HorizontalContentAlignment = HorizontalAlignment.Left,
                Padding = new Thickness(4, 1),
                Content = new TextBlock
                {
                    Text = $"Line {error.Line}: {error.Message}",
                    FontSize = 11,
                    Foreground = NoorTheme.Brush(NoorTheme.Bad),
                    TextWrapping = TextWrapping.Wrap,
                },
            };
            ToolTip.SetTip(item, "Go to the line");
            var line = error.Line;
            item.Click += (_, _) => GoTo(line);
            _errors.Children.Add(item);
        }
        _errorBox.IsVisible = Script.Errors.Count > 0;
        Editor.TextArea.TextView.InvalidateLayer(KnownLayer.Background);
        UpdateStatus();
    }

    private void UpdateStatus()
    {
        var caret = Editor.TextArea.Caret;
        var errors = Script.Errors.Count switch { 0 => "", 1 => "1 error · ", var n => $"{n} errors · " };
        _status.Text = $"{errors}Ln {caret.Line}, Col {caret.Column}{(Script.Modified ? " · unsaved" : "")}";
    }

    // ---- Highlighting ----

    private static readonly Dictionary<string, IHighlightingDefinition?> Definitions = new();

    /// <summary>Lua (.luau, .lua) or C# (.cs) highlighting from the embedded .xshd files; null for others.</summary>
    public static IHighlightingDefinition? Highlighting(string path)
    {
        var name = Path.GetExtension(path).ToLowerInvariant() switch
        {
            ".luau" or ".lua" => "Luau",
            ".cs" => "CSharp",
            _ => null,
        };
        if (name == null) return null;
        if (Definitions.TryGetValue(name, out var cached)) return cached;
        IHighlightingDefinition? definition = null;
        using var stream = typeof(CodePanel).Assembly.GetManifestResourceStream($"NoorEditor.Highlighting.{name}.xshd");
        if (stream != null)
        {
            using var reader = XmlReader.Create(stream);
            definition = HighlightingLoader.Load(reader, HighlightingManager.Instance);
        }
        Definitions[name] = definition;
        return definition;
    }

    /// <summary>Paints lines with errors red (behind the text).</summary>
    private sealed class ErrorLines(ScriptDocument script) : IBackgroundRenderer
    {
        private static readonly IBrush Fill = new SolidColorBrush(Color.Parse("#40F87171"));
        private static readonly IBrush Edge = new SolidColorBrush(Color.Parse("#F87171"));

        public KnownLayer Layer => KnownLayer.Background;

        public void Draw(TextView textView, DrawingContext drawingContext)
        {
            if (script.Errors.Count == 0 || !textView.VisualLinesValid) return;
            var lines = script.Errors.Select(e => e.Line).ToHashSet();
            foreach (var visual in textView.VisualLines)
            {
                if (!lines.Contains(visual.FirstDocumentLine.LineNumber)) continue;
                var top = visual.VisualTop - textView.VerticalOffset;
                var rect = new Rect(0, top, Math.Max(textView.Bounds.Width, 1), visual.Height);
                drawingContext.FillRectangle(Fill, rect);
                drawingContext.FillRectangle(Edge, new Rect(0, top, 3, visual.Height));
            }
        }
    }
}
