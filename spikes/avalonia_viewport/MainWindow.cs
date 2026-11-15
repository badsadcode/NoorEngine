using System.Diagnostics;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;
using Dock.Avalonia.Controls;
using Dock.Model;
using Dock.Model.Controls;
using Dock.Model.Core;
using Dock.Model.Mvvm;
using DockOrientation = Dock.Model.Core.Orientation;
using LayoutOrientation = Avalonia.Layout.Orientation;

namespace NoorEditorSpike;

public sealed class MainWindow : Window
{
    public Factory DockFactory { get; } = new();
    public ViewportDocument ViewportDoc { get; } = new() { Id = "Viewport", Title = "Viewport", CanClose = false };

    private readonly TextBlock _status = new() { FontSize = 11, VerticalAlignment = VerticalAlignment.Center };
    private TimeSpan _lastCpu;
    private DateTime _lastCpuSample = DateTime.UtcNow;
    public double CpuPercent { get; private set; }

    public MainWindow()
    {
        Title = "NoorEditor — Avalonia spike (ADR-003)";
        Width = 1600;
        Height = 900;
        Background = NoorTheme.Brush(NoorTheme.Background);
        WindowStartupLocation = WindowStartupLocation.CenterScreen;

        var root = new DockPanel();
        var top = new StackPanel { Children = { MenuBar(), WorkspaceBar() } };
        DockPanel.SetDock(top, Avalonia.Controls.Dock.Top);
        var status = StatusBar();
        DockPanel.SetDock(status, Avalonia.Controls.Dock.Bottom);
        root.Children.Add(top);
        root.Children.Add(status);
        root.Children.Add(new DockControl { Factory = DockFactory, Layout = BuildLayout() });
        Content = root;

        _lastCpu = Process.GetCurrentProcess().TotalProcessorTime;
        var timer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
        timer.Tick += (_, _) => UpdateStatus();
        timer.Start();
    }

    private IRootDock BuildLayout()
    {
        var hierarchy = new HierarchyTool { Id = "Hierarchy", Title = "Scene" };
        var content = new ContentTool { Id = "Content", Title = "Content Browser" };
        var console = new ConsoleTool { Id = "Console", Title = "Console" };
        var inspector = new InspectorTool { Id = "Inspector", Title = "Inspector" };

        DockFactory
            .DocumentDock(out var documents, d => d.WithId("Documents").WithIsCollapsable(false))
            .ToolDock(out var left, Alignment.Left, t => t.WithId("Left").WithProportion(0.17))
            .ToolDock(out var right, Alignment.Right, t => t.WithId("Right").WithProportion(0.2))
            .ToolDock(out var bottom, Alignment.Bottom, t => t.WithId("Bottom").WithProportion(0.28))
            .ProportionalDockSplitter(out var splitV)
            .ProportionalDockSplitter(out var splitL)
            .ProportionalDockSplitter(out var splitR)
            .ProportionalDock(out var center, DockOrientation.Vertical, d => d.WithId("Center").Add(documents, splitV, bottom))
            .ProportionalDock(out var main, DockOrientation.Horizontal, d => d.WithId("Main").Add(left, splitL, center, splitR, right))
            .RootDock(out var rootDock, r => r.WithId("Root").Add(main).WithDefaultDockable(main).WithActiveDockable(main));

        // Note: in Dock 12.1.0.13, AppendTool/AppendDocument inside the fluent builder left the
        // docks empty (verified with DumpLayout), so panels are added explicitly here.
        void Fill(IDock dock, params IDockable[] items)
        {
            foreach (var item in items) DockFactory.AddDockable(dock, item);
            dock.ActiveDockable = items[^1];
        }
        Fill(documents, ViewportDoc);
        Fill(left, hierarchy);
        Fill(right, inspector);
        Fill(bottom, content, console);

        // Floating panels need a host window factory, otherwise FloatDockable has nowhere to go.
        DockFactory.DefaultHostWindowLocator = () => new HostWindow();
        DockFactory.HostWindowLocator = new Dictionary<string, Func<IHostWindow?>>
        {
            [nameof(IDockWindow)] = () => new HostWindow(),
        };

        DockFactory.InitLayout(rootDock);
        DumpLayout(rootDock, 0);
        return rootDock;
    }

    private static void DumpLayout(IDockable dockable, int depth)
    {
        var dock = dockable as IDock;
        EngineLog.Add(1, "Layout", $"{new string(' ', depth * 2)}{dockable.GetType().Name} '{dockable.Id}' " +
                                   $"visible={dock?.VisibleDockables?.Count.ToString() ?? "-"} active={dock?.ActiveDockable?.Id ?? "-"}");
        if (dock?.VisibleDockables is { } children)
            foreach (var child in children) DumpLayout(child, depth + 1);
    }

    private static Control MenuBar()
    {
        var menu = new Menu { FontSize = 12 };
        foreach (var name in new[] { "File", "Edit", "Project", "Scene", "Game", "Tools", "Window", "Help" })
            menu.Items.Add(new MenuItem { Header = name });

        var logo = new TextBlock
        {
            Text = "N", FontWeight = FontWeight.Bold, FontSize = 18, Margin = new Thickness(12, 0, 6, 0),
            Foreground = NoorTheme.Brush(NoorTheme.Accent), VerticalAlignment = VerticalAlignment.Center,
        };
        var play = new Button { Content = "▶  Play", Classes = { "primary" }, VerticalAlignment = VerticalAlignment.Center };
        ToolTip.SetTip(play, "Not implemented yet (Milestone 4)");
        var right = new StackPanel { Orientation = LayoutOrientation.Horizontal, Spacing = 8, Margin = new Thickness(0, 0, 12, 0), Children = { play } };

        var bar = new DockPanel { Height = 36, Background = NoorTheme.Brush(NoorTheme.Panel) };
        DockPanel.SetDock(logo, Avalonia.Controls.Dock.Left);
        DockPanel.SetDock(right, Avalonia.Controls.Dock.Right);
        bar.Children.Add(logo);
        bar.Children.Add(right);
        bar.Children.Add(menu);
        return bar;
    }

    private static Control WorkspaceBar()
    {
        var bar = new StackPanel { Orientation = LayoutOrientation.Horizontal, Spacing = 2, Margin = new Thickness(8, 4) };
        var names = new[] { "Scene", "Terrain", "World", "Buildings", "Vegetation", "Roads", "Materials", "Shaders", "Animation", "Particles", "Audio", "Scripting" };
        foreach (var name in names)
        {
            var active = name == "Scene";
            var tab = new Border
            {
                CornerRadius = new CornerRadius(6),
                Padding = new Thickness(12, 5),
                Background = active ? NoorTheme.Brush(NoorTheme.AccentDeep) : Brushes.Transparent,
                Child = new TextBlock
                {
                    Text = name, FontSize = 12,
                    Foreground = NoorTheme.Brush(active ? Colors.White : NoorTheme.TextMuted),
                },
            };
            if (!active) ToolTip.SetTip(tab, "Workspace planned, not implemented");
            bar.Children.Add(tab);
        }
        return new Border
        {
            Background = NoorTheme.Brush(NoorTheme.Background),
            BorderBrush = NoorTheme.Brush(NoorTheme.Border),
            BorderThickness = new Thickness(0, 0, 0, 1),
            Child = bar,
        };
    }

    private Control StatusBar() => new Border
    {
        Height = 26,
        Background = NoorTheme.Brush(NoorTheme.Panel),
        BorderBrush = NoorTheme.Brush(NoorTheme.Border),
        BorderThickness = new Thickness(0, 1, 0, 0),
        Padding = new Thickness(12, 0),
        Child = _status,
    };

    private void UpdateStatus()
    {
        var process = Process.GetCurrentProcess();
        var now = DateTime.UtcNow;
        var cpu = process.TotalProcessorTime;
        CpuPercent = (cpu - _lastCpu).TotalSeconds / (now - _lastCpuSample).TotalSeconds / Environment.ProcessorCount * 100.0;
        _lastCpu = cpu;
        _lastCpuSample = now;

        var s = VulkanViewport.Active?.Stats;
        var gpu = s?.GpuName ?? "—";
        var fps = s is { } st ? $"{st.FramesPerSecond:0} FPS" : "—";
        _status.Text = $"Engine API v{Native.noor_editor_api_version()}   ·   Renderer: Vulkan   ·   {gpu}   ·   {fps}   ·   " +
                       $"CPU {CpuPercent:0.0}%   ·   RAM {process.WorkingSet64 / (1024 * 1024)} MB   ·   Spike build, not NoorEditor";
    }
}
