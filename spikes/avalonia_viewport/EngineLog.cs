using System.Collections.ObjectModel;
using System.Runtime.InteropServices;
using Avalonia.Threading;

namespace NoorEditorSpike;

public sealed record LogLine(int Level, string Category, string Message)
{
    public string LevelName => Level switch { 0 => "TRACE", 1 => "DEBUG", 2 => "INFO", 3 => "WARN", 4 => "ERROR", _ => "FATAL" };
    public string Text => $"[{LevelName}] [{Category}] {Message}";
}

/// <summary>Receives engine log records (from any engine thread) and republishes them on the UI thread.</summary>
public static class EngineLog
{
    // Keep the delegate alive for the lifetime of the process: native code holds a pointer to it.
    private static readonly Native.LogCallback Callback = OnNativeLog;

    public static ObservableCollection<LogLine> Lines { get; } = new();
    public static int ErrorCount { get; private set; }

    public static void Install() => Native.noor_set_log_callback(Callback, IntPtr.Zero);
    public static void Uninstall() => Native.noor_set_log_callback(null, IntPtr.Zero);

    public static void Add(int level, string category, string message)
    {
        var line = new LogLine(level, category, message);
        Dispatcher.UIThread.Post(() =>
        {
            if (level >= 4 && category != "Smoke") ErrorCount++;  // the test's own FAIL lines don't count
            Lines.Add(line);
            if (Lines.Count > 2000) Lines.RemoveAt(0);
        });
        Console.Error.WriteLine(line.Text);
    }

    private static void OnNativeLog(int level, IntPtr category, IntPtr message, IntPtr user) =>
        Add(level, Marshal.PtrToStringUTF8(category) ?? "", Marshal.PtrToStringUTF8(message) ?? "");
}
