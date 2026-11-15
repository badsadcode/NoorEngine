using System.Diagnostics;
using System.Text.Json;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;

namespace NoorEditorSpike;

/// <summary>
/// Automated ADR-003 spike checks (run with --smoke N --out DIR):
///  1. viewport starts and presents frames; time to first frame
///  2. frame rate, process CPU and memory over N seconds
///  3. window resize propagates to the swapchain
///  4. floating the viewport panel into its own window re-creates a working viewport
///  5. zero Vulkan validation errors and zero engine errors throughout
/// Writes spike_results.json and screenshots into DIR. Exit code 0 only if all checks pass.
/// </summary>
internal static class SmokeTest
{
    public static void Attach(MainWindow window, IClassicDesktopStyleApplicationLifetime desktop)
    {
        window.Opened += async (_, _) =>
        {
            int code;
            try { code = await Run(window, desktop); }
            catch (Exception e) { EngineLog.Add(5, "Smoke", e.ToString()); code = 1; }
            desktop.Shutdown(code);
        };
    }

    private static async Task<int> Run(MainWindow window, IClassicDesktopStyleApplicationLifetime desktop)
    {
        var opts = Program.Options;
        Directory.CreateDirectory(opts.OutputDir);
        var results = new Dictionary<string, object>();
        var failures = new List<string>();
        void Check(bool ok, string what) { if (!ok) failures.Add(what); EngineLog.Add(ok ? 2 : 4, "Smoke", $"{(ok ? "PASS" : "FAIL")}: {what}"); }

        results["window_opened_ms"] = Math.Round(Program.SinceStart.Elapsed.TotalMilliseconds);

        // 1. First frame
        var wait = Stopwatch.StartNew();
        Native.ViewportStats? stats = null;
        while (wait.Elapsed < TimeSpan.FromSeconds(15))
        {
            stats = VulkanViewport.Active?.Stats;
            if (stats is { State: Native.ViewportState.Running, FramesPresented: > 0 } or { State: Native.ViewportState.Failed }) break;
            await Task.Delay(20);
        }
        Check(stats is { State: Native.ViewportState.Running, FramesPresented: > 0 }, "viewport presents frames");
        if (failures.Count > 0)
        {
            ScreenCapture.CaptureWindow(window, Path.Combine(opts.OutputDir, "spike_0_failed_start.png"));
            return Finish(results, failures, opts.OutputDir);
        }
        results["process_start_to_first_frame_ms"] = Math.Round(Program.SinceStart.Elapsed.TotalMilliseconds);
        results["viewport_create_to_first_frame_ms"] = Math.Round(stats!.Value.SecondsToFirstFrame * 1000);
        results["gpu"] = stats.Value.GpuName;

        // 2. Steady-state measurement
        await Task.Delay(1500);
        var process = Process.GetCurrentProcess();
        var cpu0 = process.TotalProcessorTime;
        var frames0 = VulkanViewport.Active!.Stats!.Value.FramesPresented;
        var t0 = Stopwatch.StartNew();
        await Task.Delay(TimeSpan.FromSeconds(opts.SmokeSeconds));
        process.Refresh();
        var seconds = t0.Elapsed.TotalSeconds;
        var frames1 = VulkanViewport.Active!.Stats!.Value.FramesPresented;
        results["measure_seconds"] = Math.Round(seconds, 2);
        results["average_fps"] = Math.Round((frames1 - frames0) / seconds, 1);
        results["process_cpu_percent_of_machine"] = Math.Round((process.TotalProcessorTime - cpu0).TotalSeconds / seconds / Environment.ProcessorCount * 100, 2);
        results["process_cpu_percent_of_one_core"] = Math.Round((process.TotalProcessorTime - cpu0).TotalSeconds / seconds * 100, 1);
        results["working_set_mb"] = process.WorkingSet64 / (1024 * 1024);
        results["logical_cores"] = Environment.ProcessorCount;
        Check((frames1 - frames0) / seconds > 30, "frame rate above 30 FPS while docked");
        ScreenCapture.CaptureWindow(window, Path.Combine(opts.OutputDir, "spike_1_docked.png"));

        // 3. Resize
        var before = VulkanViewport.Active!.Stats!.Value;
        window.Width = 1200;
        window.Height = 760;
        await Task.Delay(1500);
        var after = VulkanViewport.Active!.Stats!.Value;
        results["resize_from"] = $"{before.Width}x{before.Height}";
        results["resize_to"] = $"{after.Width}x{after.Height}";
        Check(after.Width < before.Width && after.Height < before.Height && after.State == Native.ViewportState.Running,
              "window resize reaches the swapchain");

        // 4. Float the viewport into its own window (re-parents the native child window)
        var created = VulkanViewport.CreatedCount;
        var floatTimer = Stopwatch.StartNew();
        window.DockFactory.FloatDockable(window.ViewportDoc);
        Native.ViewportStats? floated = null;
        while (floatTimer.Elapsed < TimeSpan.FromSeconds(10))
        {
            await Task.Delay(20);
            floated = VulkanViewport.CreatedCount > created ? VulkanViewport.Active?.Stats : null;
            if (floated is { State: Native.ViewportState.Running, FramesPresented: > 0 }) break;
        }
        results["float_to_first_frame_ms"] = Math.Round(floatTimer.Elapsed.TotalMilliseconds);
        results["viewports_created"] = VulkanViewport.CreatedCount;
        Check(floated is { State: Native.ViewportState.Running, FramesPresented: > 0 }, "floated viewport renders again");
        await Task.Delay(1500);
        var i = 0;
        foreach (var w in desktop.Windows.Where(w => w != window))
            ScreenCapture.CaptureWindow(w, Path.Combine(opts.OutputDir, $"spike_2_floating_{i++}.png"));
        ScreenCapture.CaptureWindow(window, Path.Combine(opts.OutputDir, "spike_3_main_after_float.png"));

        // 5. Error budget
        var last = VulkanViewport.Active?.Stats;
        results["validation_errors_final_viewport"] = last?.ValidationErrors ?? 0;
        await Task.Delay(200);  // let posted log lines land
        results["engine_errors_logged"] = EngineLog.ErrorCount;
        Check((last?.ValidationErrors ?? 1) == 0, "zero Vulkan validation errors");
        Check(EngineLog.ErrorCount == 0, "zero engine errors logged");

        return Finish(results, failures, opts.OutputDir);
    }

    private static int Finish(Dictionary<string, object> results, List<string> failures, string dir)
    {
        results["failures"] = failures;
        results["passed"] = failures.Count == 0;
        var json = JsonSerializer.Serialize(results, new JsonSerializerOptions { WriteIndented = true });
        File.WriteAllText(Path.Combine(dir, "spike_results.json"), json);
        Console.WriteLine(json);
        return failures.Count == 0 ? 0 : 1;
    }
}
