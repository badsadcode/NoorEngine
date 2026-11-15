using System.Diagnostics;
using Avalonia;

namespace NoorEditor;

public sealed class EditorOptions
{
    /// <summary>Open this project directly instead of showing the project hub.</summary>
    public string? ProjectPath { get; private set; }

    /// <summary>Run the automated editor smoke test, writing results and screenshots here, then exit.</summary>
    public string? SmokeDir { get; private set; }

    /// <summary>Folder with test assets for the smoke test (the repository's tests/data).</summary>
    public string? TestData { get; private set; }

    public static EditorOptions Parse(string[] args)
    {
        var o = new EditorOptions();
        for (var i = 0; i < args.Length; i++)
        {
            var next = i + 1 < args.Length ? args[i + 1] : null;
            switch (args[i])
            {
                case "--project" when next != null: o.ProjectPath = next; i++; break;
                case "--smoke" when next != null: o.SmokeDir = Path.GetFullPath(next); i++; break;
                case "--test-data" when next != null: o.TestData = Path.GetFullPath(next); i++; break;
                default:
                    // A bare folder argument opens that project (e.g. "Open with NoorEditor").
                    if (!args[i].StartsWith("--", StringComparison.Ordinal) && Directory.Exists(args[i])) o.ProjectPath = args[i];
                    break;
            }
        }
        return o;
    }
}

internal static class Program
{
    public static readonly Stopwatch SinceStart = Stopwatch.StartNew();
    public static EditorOptions Options { get; private set; } = new();

    [STAThread]
    public static int Main(string[] args)
    {
        Options = EditorOptions.Parse(args);
        return BuildAvaloniaApp().StartWithClassicDesktopLifetime(args, Avalonia.Controls.ShutdownMode.OnExplicitShutdown);
    }

    public static AppBuilder BuildAvaloniaApp() =>
        AppBuilder.Configure<App>().UsePlatformDetect().WithInterFont().LogToTrace();
}
