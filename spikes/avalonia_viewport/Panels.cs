using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Templates;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;
using Dock.Model.Mvvm.Controls;

namespace NoorEditorSpike;

// ---- Dockable view models (Dock maps them to views through DataTemplates) ----

public sealed class ViewportDocument : Document { }
public sealed class HierarchyTool : Tool { }
public sealed class InspectorTool : Tool { }
public sealed class ConsoleTool : Tool { }
public sealed class ContentTool : Tool { }

public static class PanelTemplates
{
    public static void Register(Avalonia.Application app)
    {
        app.DataTemplates.Add(new FuncDataTemplate<ViewportDocument>((_, _) => Panels.Viewport()));
        app.DataTemplates.Add(new FuncDataTemplate<HierarchyTool>((_, _) => Panels.Hierarchy()));
        app.DataTemplates.Add(new FuncDataTemplate<InspectorTool>((_, _) => Panels.Inspector()));
        app.DataTemplates.Add(new FuncDataTemplate<ConsoleTool>((_, _) => Panels.Console()));
        app.DataTemplates.Add(new FuncDataTemplate<ContentTool>((_, _) => Panels.ContentBrowser()));
    }
}

/// <summary>Panel views. Data is either real (viewport stats, engine log) or clearly labeled sample data.</summary>
public static class Panels
{
    private static TextBlock Muted(string text, double size = 11) =>
        new() { Text = text, FontSize = size, Classes = { "muted" }, TextWrapping = TextWrapping.Wrap };

    private static Border SampleBadge(string text) => new()
    {
        Background = NoorTheme.Brush(Color.Parse("#2A2140")),
        CornerRadius = new CornerRadius(4),
        Padding = new Thickness(6, 2),
        Margin = new Thickness(8, 6, 8, 4),
        HorizontalAlignment = HorizontalAlignment.Left,
        Child = new TextBlock { Text = text, FontSize = 10, Foreground = NoorTheme.Brush(NoorTheme.Warn) },
    };

    public static Control Viewport()
    {
        var toolbar = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 2, Margin = new Thickness(6, 4) };
        foreach (var label in new[] { "Select", "Move", "Rotate", "Scale" })
            toolbar.Children.Add(new Button { Content = label, Classes = { "tool" } });
        toolbar.Children.Add(new Border { Width = 1, Background = NoorTheme.Brush(NoorTheme.Border), Margin = new Thickness(6, 2) });
        toolbar.Children.Add(new Button { Content = "Perspective ▾", Classes = { "tool" } });
        toolbar.Children.Add(new Button { Content = "Lit ▾", Classes = { "tool" } });
        toolbar.Children.Add(Muted("   Hover: hue follows pointer · Hold left button: brighter", 11));
        foreach (Control c in toolbar.Children) c.VerticalAlignment = VerticalAlignment.Center;

        var dock = new DockPanel();
        var toolbarHost = new Border { Child = toolbar, Background = NoorTheme.Brush(NoorTheme.Panel) };
        DockPanel.SetDock(toolbarHost, Avalonia.Controls.Dock.Top);
        dock.Children.Add(toolbarHost);
        dock.Children.Add(new VulkanViewport());
        return dock;
    }

    public static Control Hierarchy()
    {
        TreeViewItem Node(string name, params TreeViewItem[] children)
        {
            var item = new TreeViewItem { Header = name, IsExpanded = true, FontSize = 12 };
            foreach (var c in children) item.Items.Add(c);
            return item;
        }
        var tree = new TreeView
        {
            Margin = new Thickness(4, 0),
            ItemsSource = new[]
            {
                Node("BalticDrive_Demo",
                    Node("World", Node("Terrain"), Node("Water_Lake"), Node("Environment (HDRI)")),
                    Node("Lighting", Node("Sun"), Node("SkyAtmosphere"), Node("Fog")),
                    Node("Level", Node("Road_Network"), Node("Buildings"), Node("Vegetation"), Node("PlayerStart"))),
            },
        };
        var panel = new DockPanel();
        var badge = SampleBadge("SAMPLE DATA · no scene system yet (Milestone 2)");
        DockPanel.SetDock(badge, Avalonia.Controls.Dock.Top);
        panel.Children.Add(badge);
        panel.Children.Add(tree);
        return panel;
    }

    public static Control Inspector()
    {
        var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("130,*"), Margin = new Thickness(10, 6) };
        var values = new Dictionary<string, TextBlock>();
        string[] rows = { "State", "GPU", "Resolution", "Frame rate", "Frames", "Swapchain builds", "Validation errors", "First frame", "Viewports created" };
        for (var i = 0; i < rows.Length; i++)
        {
            grid.RowDefinitions.Add(new RowDefinition(GridLength.Auto));
            var key = Muted(rows[i], 12);
            key.Margin = new Thickness(0, 3);
            var value = new TextBlock { FontSize = 12, Margin = new Thickness(0, 3), TextWrapping = TextWrapping.Wrap };
            Grid.SetRow(key, i);
            Grid.SetRow(value, i);
            Grid.SetColumn(value, 1);
            grid.Children.Add(key);
            grid.Children.Add(value);
            values[rows[i]] = value;
        }

        var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(250) };
        timer.Tick += (_, _) =>
        {
            var s = VulkanViewport.Active?.Stats;
            values["Viewports created"].Text = VulkanViewport.CreatedCount.ToString();
            if (s is not { } st)
            {
                values["State"].Text = "no viewport";
                return;
            }
            values["State"].Text = st.State.ToString();
            values["State"].Foreground = NoorTheme.Brush(st.State == Native.ViewportState.Running ? NoorTheme.Good : NoorTheme.Bad);
            values["GPU"].Text = st.GpuName;
            values["Resolution"].Text = $"{st.Width} × {st.Height} px";
            values["Frame rate"].Text = $"{st.FramesPerSecond:0} FPS (vsync)";
            values["Frames"].Text = st.FramesPresented.ToString("N0");
            values["Swapchain builds"].Text = st.SwapchainRebuilds.ToString();
            values["Validation errors"].Text = st.ValidationErrors.ToString();
            values["Validation errors"].Foreground = NoorTheme.Brush(st.ValidationErrors == 0 ? NoorTheme.Good : NoorTheme.Bad);
            values["First frame"].Text = st.SecondsToFirstFrame > 0 ? $"{st.SecondsToFirstFrame * 1000:0} ms after create" : "—";
        };
        timer.Start();

        var header = new TextBlock { Text = "Engine Viewport", Classes = { "section" }, Margin = new Thickness(10, 10, 10, 2) };
        var note = Muted("Live data from noor_editor_api.dll", 11);
        note.Margin = new Thickness(10, 0, 10, 4);
        return new StackPanel { Children = { header, note, grid } };
    }

    public static Control Console()
    {
        var list = new ListBox
        {
            ItemsSource = EngineLog.Lines,
            FontFamily = new FontFamily("Cascadia Mono, Consolas, monospace"),
            FontSize = 11,
            Background = Brushes.Transparent,
            ItemTemplate = new FuncDataTemplate<LogLine>((line, _) => new TextBlock
            {
                Text = line?.Text,
                Foreground = NoorTheme.Brush(line?.Level switch { >= 4 => NoorTheme.Bad, 3 => NoorTheme.Warn, <= 1 => NoorTheme.TextMuted, _ => NoorTheme.Text }),
                TextWrapping = TextWrapping.NoWrap,
            }),
        };
        EngineLog.Lines.CollectionChanged += (_, _) =>
        {
            if (EngineLog.Lines.Count > 0) list.ScrollIntoView(EngineLog.Lines.Count - 1);
        };
        return list;
    }

    public static Control ContentBrowser()
    {
        var wrap = new WrapPanel { Margin = new Thickness(8) };
        foreach (var name in new[] { "Models", "Textures", "Materials", "HDRI", "Audio", "Scripts" })
        {
            wrap.Children.Add(new Border
            {
                Width = 92, Height = 72, Margin = new Thickness(4),
                CornerRadius = new CornerRadius(6),
                Background = NoorTheme.Brush(NoorTheme.PanelRaised),
                BorderBrush = NoorTheme.Brush(NoorTheme.Border), BorderThickness = new Thickness(1),
                Child = new StackPanel
                {
                    VerticalAlignment = VerticalAlignment.Center,
                    Children =
                    {
                        new TextBlock { Text = "▣", FontSize = 22, HorizontalAlignment = HorizontalAlignment.Center, Foreground = NoorTheme.Brush(NoorTheme.Accent) },
                        new TextBlock { Text = name, FontSize = 11, HorizontalAlignment = HorizontalAlignment.Center },
                    },
                },
            });
        }
        var panel = new DockPanel();
        var badge = SampleBadge("SAMPLE DATA · asset system arrives in Milestone 2");
        DockPanel.SetDock(badge, Avalonia.Controls.Dock.Top);
        panel.Children.Add(badge);
        panel.Children.Add(wrap);
        return panel;
    }
}
