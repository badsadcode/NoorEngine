using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Templates;
using Avalonia.Layout;
using Avalonia.Media;
using NoorEditor.Interop;
using NoorEditor.Shell;

namespace NoorEditor.Panels;

/// <summary>Engine and editor log, newest at the bottom.</summary>
public sealed class ConsolePanel : DockPanel
{
    private readonly ListBox _list;
    private readonly CheckBox _debug = new() { Content = "Debug", FontSize = 11, IsChecked = false };
    private readonly List<LogLine> _shown = new();

    public ConsolePanel()
    {
        _list = new ListBox
        {
            FontFamily = new FontFamily("Cascadia Mono, Consolas, monospace"),
            FontSize = 11,
            Background = Brushes.Transparent,
            ItemTemplate = new FuncDataTemplate<LogLine>((line, _) => new SelectableTextBlock
            {
                Text = line?.Text,
                Foreground = NoorTheme.Brush(line?.Level switch { >= 4 => NoorTheme.Bad, 3 => NoorTheme.Warn, <= 1 => NoorTheme.TextMuted, _ => NoorTheme.Text }),
                TextWrapping = TextWrapping.NoWrap,
            }),
        };

        var clear = new Button { Content = "Clear", Classes = { "tool" } };
        clear.Click += (_, _) => EngineLog.Lines.Clear();
        var bar = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, Margin = new Thickness(6, 2), Children = { clear, _debug } };
        SetDock(bar, Avalonia.Controls.Dock.Top);
        Children.Add(bar);
        Children.Add(_list);

        _debug.IsCheckedChanged += (_, _) => Fill();
    }

    private bool _pending;

    // Log lines arrive in bursts; refresh the list at most once per UI tick.
    private void OnLines(object? sender, System.Collections.Specialized.NotifyCollectionChangedEventArgs e)
    {
        if (_pending) return;
        _pending = true;
        Avalonia.Threading.Dispatcher.UIThread.Post(() => { _pending = false; Fill(); }, Avalonia.Threading.DispatcherPriority.Background);
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        EngineLog.Lines.CollectionChanged += OnLines;
        Fill();
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnDetachedFromVisualTree(e);
        EngineLog.Lines.CollectionChanged -= OnLines;
    }

    private void Fill()
    {
        var showDebug = _debug.IsChecked == true;
        _shown.Clear();
        _shown.AddRange(EngineLog.Lines.Where(l => showDebug || l.Level >= 2));
        _list.ItemsSource = null;
        _list.ItemsSource = _shown;
        if (_shown.Count > 0) _list.ScrollIntoView(_shown.Count - 1);
    }
}
