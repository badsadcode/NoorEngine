using System.Collections.ObjectModel;
using System.Runtime.InteropServices;
using Avalonia.Threading;

namespace NoorEditor.Interop;

public sealed record LogLine(DateTime Time, int Level, string Category, string Message)
{
    public string LevelName => Level switch { 0 => "TRACE", 1 => "DEBUG", 2 => "INFO", 3 => "WARN", 4 => "ERROR", _ => "FATAL" };
    public string Text => $"{Time:HH:mm:ss}  {LevelName,-5}  [{Category}] {Message}";
}

/// <summary>Receives engine log records (from any engine thread) and republishes them on the UI thread.</summary>
public static class EngineLog
{
    // Keep the delegate alive for the lifetime of the process: native code holds a pointer to it.
    private static readonly Native.LogCallback Callback = OnNativeLog;

    public static ObservableCollection<LogLine> Lines { get; } = new();

    /// <summary>Errors logged by the engine or the editor (the smoke test's own FAIL lines excluded).</summary>
    public static int ErrorCount { get; private set; }

    public static void Install() => Native.noor_set_log_callback(Callback, IntPtr.Zero);
    public static void Uninstall() => Native.noor_set_log_callback(null, IntPtr.Zero);

    public static void Info(string category, string message) => Add(2, category, message);
    public static void Warn(string category, string message) => Add(3, category, message);
    public static void Error(string category, string message) => Add(4, category, message);

    public static void Add(int level, string category, string message)
    {
        var line = new LogLine(DateTime.Now, level, category, message);
        Console.Error.WriteLine(line.Text);
        Dispatcher.UIThread.Post(() =>
        {
            if (level >= 4 && category != "Smoke") ErrorCount++;
            Lines.Add(line);
            if (Lines.Count > 5000) Lines.RemoveAt(0);
        });
    }

    private static void OnNativeLog(int level, IntPtr category, IntPtr message, IntPtr user) =>
        Add(level, Marshal.PtrToStringUTF8(category) ?? "", Marshal.PtrToStringUTF8(message) ?? "");
}
