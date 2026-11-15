using System.Diagnostics;
using Avalonia;

namespace NoorEditorSpike;

public sealed class SpikeOptions
{
    /// <summary>Run the automated spike checks for this many seconds of measurement, then exit.</summary>
    public int SmokeSeconds { get; private set; }
    public string OutputDir { get; private set; } = ".";

    public static SpikeOptions Parse(string[] args)
    {
        var o = new SpikeOptions();
        for (var i = 0; i < args.Length; i++)
        {
            if (args[i] == "--smoke" && i + 1 < args.Length) o.SmokeSeconds = int.Parse(args[++i]);
            else if (args[i] == "--out" && i + 1 < args.Length) o.OutputDir = args[++i];
        }
        return o;
    }
}

internal static class Program
{
    public static readonly Stopwatch SinceStart = Stopwatch.StartNew();
    public static SpikeOptions Options { get; private set; } = new();

    [STAThread]
    public static int Main(string[] args)
    {
        Options = SpikeOptions.Parse(args);
        return BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
    }

    public static AppBuilder BuildAvaloniaApp() =>
        AppBuilder.Configure<App>().UsePlatformDetect().WithInterFont().LogToTrace();
}
