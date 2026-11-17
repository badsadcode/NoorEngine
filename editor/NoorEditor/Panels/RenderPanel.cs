using System.Globalization;
using System.Text.Json.Nodes;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;
using NoorEditor.Shell;

namespace NoorEditor.Panels;

/// <summary>
/// The renderer's numbers for the scene viewport (Milestone 5, part 7): GPU time per pass, draws,
/// triangles, memory and anti-aliasing. Read from noor_viewport_render_stats twice a second, so
/// what you change in the scene shows up here as a cost.
/// </summary>
public sealed class RenderPanel : DockPanel
{
    private readonly DispatcherTimer _timer = new() { Interval = TimeSpan.FromMilliseconds(500) };
    private readonly StackPanel _passes = new() { Spacing = 3 };
    private readonly Grid _numbers = new() { ColumnDefinitions = new ColumnDefinitions("Auto,*"), Margin = new Thickness(0, 0, 0, 0) };
    private readonly TextBlock _headline = new() { FontSize = 13, FontWeight = FontWeight.SemiBold };
    private readonly TextBlock _note = new() { FontSize = 11, Classes = { "muted" }, TextWrapping = TextWrapping.Wrap };

    /// <summary>The last numbers shown (tests read them).</summary>
    public JsonObject? Last { get; private set; }

    public RenderPanel()
    {
        var left = new StackPanel
        {
            Spacing = 6, Margin = new Thickness(12, 8), MinWidth = 280,
            Children = { _headline, new TextBlock { Text = "GPU time per pass", Classes = { "section" } }, _passes, _note },
        };
        var right = new StackPanel { Margin = new Thickness(12, 8), Children = { _numbers } };
        var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("*,*") };
        Grid.SetColumn(right, 1);
        grid.Children.Add(left);
        grid.Children.Add(right);
        Children.Add(new ScrollViewer { Content = grid });
        _timer.Tick += (_, _) => Refresh();
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        _timer.Start();
        Refresh();
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnDetachedFromVisualTree(e);
        _timer.Stop();
    }

    public void Refresh()
    {
        var viewport = VulkanViewport.Active;
        var json = viewport?.RenderStats();
        if (json == null || JsonNode.Parse(json) is not JsonObject s || !s.ContainsKey("draw_calls"))
        {
            _headline.Text = "The scene viewport isn't drawing";
            _note.Text = "Its tab is hidden or it hasn't started. Show the Viewport tab to see its numbers.";
            _passes.Children.Clear();
            _numbers.Children.Clear();
            return;
        }
        Last = s;
        var gpu = s["gpu_ms"]?.GetValue<double>() ?? -1;
        var fps = s["fps"]?.GetValue<double>() ?? 0;
        _headline.Text = gpu >= 0 ? $"{gpu:0.00} ms on the GPU · {fps:0} FPS" : $"{fps:0} FPS (GPU timings not available)";
        _note.Text = "Frames wait for the display (vsync), so FPS shows the screen's rate; GPU time is the real cost.";

        // Passes: a bar per pass, scaled to the slowest.
        _passes.Children.Clear();
        var passes = (s["passes"] as JsonArray ?? []).OfType<JsonObject>()
            .Select(p => (Name: p["name"]?.GetValue<string>() ?? "", Ms: p["ms"]?.GetValue<double>() ?? 0)).ToList();
        var max = Math.Max(0.01, passes.Count == 0 ? 0 : passes.Max(p => p.Ms));
        foreach (var (name, ms) in passes)
        {
            var bar = new Border
            {
                Height = 10, CornerRadius = new CornerRadius(2), HorizontalAlignment = HorizontalAlignment.Left, VerticalAlignment = VerticalAlignment.Center,
                Width = Math.Max(2, 160 * ms / max), Background = NoorTheme.Brush(NoorTheme.Accent),
            };
            var row = new Grid { ColumnDefinitions = new ColumnDefinitions("70,170,*") };
            var label = new TextBlock { Text = name, FontSize = 12, VerticalAlignment = VerticalAlignment.Center };
            var value = new TextBlock { Text = $"{ms:0.000} ms", FontSize = 12, Classes = { "muted" }, VerticalAlignment = VerticalAlignment.Center };
            Grid.SetColumn(bar, 1);
            Grid.SetColumn(value, 2);
            row.Children.Add(label);
            row.Children.Add(bar);
            row.Children.Add(value);
            _passes.Children.Add(row);
        }

        long N(string key) => s[key]?.GetValue<long>() ?? 0;
        var samples = N("msaa_samples");
        var rows = new (string Label, string Value)[]
        {
            ("Picture", $"{N("width")} × {N("height")}, {(samples > 1 ? $"{samples}× MSAA" : "no anti-aliasing")}"),
            ("Draw calls", $"{N("draw_calls")}"),
            ("  shadows", $"{N("shadow_draw_calls")} ({N("shadow_draws_culled")} skipped: outside their cascade)"),
            ("  see-through", $"{N("transparent_draws")}"),
            ("Triangles", N("triangles").ToString("N0", CultureInfo.InvariantCulture)),
            ("Textures", Bytes(N("texture_bytes"))),
            ("Meshes", Bytes(N("mesh_bytes"))),
            ("Frame images", Bytes(N("render_target_bytes"))),
        };
        _numbers.Children.Clear();
        _numbers.RowDefinitions.Clear();
        for (var i = 0; i < rows.Length; i++)
        {
            _numbers.RowDefinitions.Add(new RowDefinition(GridLength.Auto));
            var label = new TextBlock { Text = rows[i].Label, FontSize = 12, Classes = { "muted" }, Margin = new Thickness(0, 2, 16, 2) };
            var value = new TextBlock { Text = rows[i].Value, FontSize = 12, Margin = new Thickness(0, 2) };
            Grid.SetRow(label, i);
            Grid.SetRow(value, i);
            Grid.SetColumn(value, 1);
            _numbers.Children.Add(label);
            _numbers.Children.Add(value);
        }
    }

    public static string Bytes(long bytes) => bytes switch
    {
        >= 1L << 30 => $"{bytes / (double)(1L << 30):0.00} GB",
        >= 1L << 20 => $"{bytes / (double)(1L << 20):0.0} MB",
        >= 1L << 10 => $"{bytes / (double)(1L << 10):0} KB",
        _ => $"{bytes} bytes",
    };
}
