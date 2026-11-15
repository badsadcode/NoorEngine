using System.Diagnostics;
using System.Text.Json;
using System.Text.Json.Nodes;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using NoorEditor.Hub;
using NoorEditor.Interop;
using NoorEditor.Model;
using NoorEditor.Panels;
using NoorEditor.Shell;

namespace NoorEditor.Testing;

/// <summary>
/// End-to-end editor check (run: NoorEditor --smoke OUTDIR --test-data REPO/tests/data).
/// Drives the same code paths a user's clicks and typing take: create a project, add a model
/// from the content browser, edit it in the inspector, undo/redo, save, delete, float the
/// viewport. Screenshots capture only this app's own windows (PrintWindow). Writes
/// editor_results.json; exit code 0 only if every check passes.
/// </summary>
internal static class SmokeTest
{
    private const string BuildingGlb = "osm_w117821721";

    public static void Start(IClassicDesktopStyleApplicationLifetime desktop, EditorOptions options, EditorSettings settings)
    {
        var dir = options.SmokeDir!;
        Directory.CreateDirectory(dir);
        var results = new Dictionary<string, object>();
        var failures = new List<string>();

        try
        {
            var doc = CreateProject(dir, options.TestData, settings);
            var window = new MainWindow(doc, settings);
            desktop.MainWindow = window;
            window.Opened += async (_, _) =>
            {
                int code;
                try
                {
                    await Run(desktop, window, settings, dir, results, failures);
                }
                catch (Exception e)
                {
                    failures.Add($"exception: {e.Message}");
                    EngineLog.Add(5, "Smoke", e.ToString());
                }
                code = Finish(results, failures, dir);
                window.CloseWithoutPrompt();
                desktop.Shutdown(code);
            };
            window.Show();
        }
        catch (Exception e)
        {
            failures.Add($"setup: {e.Message}");
            desktop.Shutdown(Finish(results, failures, dir));
        }
    }

    private static EditorDocument CreateProject(string dir, string? testData, EditorSettings settings)
    {
        var data = testData ?? throw new InvalidOperationException("--test-data <repo>/tests/data is required");
        var project = Path.Combine(dir, "SmokeProject");
        if (Directory.Exists(project)) Directory.Delete(project, recursive: true);
        var doc = EditorDocument.Create(project, "SmokeProject", out var error)
                  ?? throw new InvalidOperationException($"create project: {error}");
        var assets = Path.Combine(project, "Assets", "Buildings");
        Directory.CreateDirectory(assets);
        foreach (var ext in new[] { ".glb", ".noor.json", ".recipe.json" })
            File.Copy(Path.Combine(data, "badbuilding", BuildingGlb + ext), Path.Combine(assets, BuildingGlb + ext));
        doc.RefreshAssets(rescan: true);
        if (!doc.NewScene("Scenes/Main.noorscene", "Main") || !doc.Save()) throw new InvalidOperationException("new scene failed");
        settings.AddRecent(project);
        return doc;
    }

    private static async Task Run(IClassicDesktopStyleApplicationLifetime desktop, MainWindow window, EditorSettings settings,
                                  string dir, Dictionary<string, object> results, List<string> failures)
    {
        void Check(bool ok, string what)
        {
            if (!ok) failures.Add(what);
            EngineLog.Add(ok ? 2 : 4, "Smoke", $"{(ok ? "PASS" : "FAIL")}: {what}");
        }
        void Shot(Window w, string name) => ScreenCapture.CaptureWindow(w, Path.Combine(dir, name));
        var doc = window.Doc;

        // 0. Rotation maths round-trips (what the inspector shows is what gets stored).
        var worst = 0.0;
        foreach (var e in new[] { new Vec3(0, 0, 0), new Vec3(30, 45, 10), new Vec3(-80, 170, -35), new Vec3(10, -120, 90) })
        {
            var back = Quat.FromEulerDegrees(e).ToEulerDegrees();
            worst = Math.Max(worst, Math.Max(Math.Abs(back.X - e.X), Math.Max(Math.Abs(back.Y - e.Y), Math.Abs(back.Z - e.Z))));
        }
        Check(worst < 1e-3, $"euler ↔ quaternion round trip (worst {worst:0.######}°)");

        // 1. Viewport comes up on the document.
        var stats = await WaitFor(() => VulkanViewport.Active?.Stats, s => s is { State: Native.ViewportState.Running, FramesPresented: > 0 } or { State: Native.ViewportState.Failed }, 15);
        Check(stats is { State: Native.ViewportState.Running }, "viewport renders");
        results["window_to_first_frame_ms"] = Math.Round(Program.SinceStart.Elapsed.TotalMilliseconds);
        results["gpu"] = stats?.GpuName ?? "";
        Check(window.Hierarchy != null && window.Inspector != null && window.ContentBrowser != null, "panels created");

        // 2. Add the building from the content browser (same handler as a double-click).
        var asset = doc.Assets.FirstOrDefault(a => a.Path.EndsWith(BuildingGlb + ".glb", StringComparison.Ordinal));
        Check(asset != null, "asset registry lists the model");
        if (asset == null) return;
        window.AddAsset(asset);
        Check(doc.Scene.Entities.Count == 29, $"model added as 29 entities (got {doc.Scene.Entities.Count})");
        Check(doc.Selected is { Parent: null }, "new model root is selected");
        Check(window.Hierarchy!.ItemCount == 29, $"hierarchy shows 29 rows (got {window.Hierarchy.ItemCount})");
        Check(doc.Scene.Modified && window.Title!.Contains('•'), "title marks the scene modified");
        await Task.Delay(1500);
        Shot(window, "editor_1_added.png");
        var id = doc.SelectedId!;

        // 3. Inspector edits: comma decimals are accepted; each commit is one undo step.
        var inspector = window.Inspector!;
        inspector.TypeField(InspectorPanel.Row.Position, 0, "12,5");
        Check(Math.Abs(doc.Selected!.Transform.Position.X - 12.5) < 1e-6, "inspector position edit reaches the engine (\"12,5\")");
        inspector.TypeField(InspectorPanel.Row.Rotation, 1, "45");
        var yaw = doc.Selected!.Transform.Rotation.ToEulerDegrees().Y;
        Check(Math.Abs(yaw - 45) < 1e-3, $"inspector rotation edit (yaw {yaw:0.###}°)");
        Check(inspector.FieldText(InspectorPanel.Row.Rotation, 1) == "45", "inspector shows the stored yaw");
        inspector.TypeName("Smoke Block");
        Check(doc.Selected!.Name == "Smoke Block", "rename through the inspector");
        inspector.TypeField(InspectorPanel.Row.Scale, 2, "not a number");
        Check(doc.Selected!.Transform.Scale.Z == 1 && doc.Scene.UndoLabel == "Rename", "invalid input is rejected without a command");

        // 4. Undo / redo walk the history.
        Check(doc.Undo() && doc.Selected?.Name != "Smoke Block", "undo rename");
        Check(doc.Undo() && Math.Abs(doc.Selected!.Transform.Rotation.ToEulerDegrees().Y) < 1e-3, "undo rotation");
        Check(doc.Redo() && Math.Abs(doc.Selected!.Transform.Rotation.ToEulerDegrees().Y - 45) < 1e-3, "redo rotation");
        Check(doc.Selected!.Transform.Position.X == 12.5, "position edit survives undo/redo of later edits");
        window.ViewportView?.Viewport.Frame(doc.Subtree(id));
        await Task.Delay(1200);
        Shot(window, "editor_2_edited.png");

        // 5. Save writes the scene file.
        window.Save();
        Check(!doc.Scene.Modified, "save clears the modified flag");
        var file = Path.Combine(doc.ProjectRoot, "Scenes", "Main.noorscene");
        var saved = JsonNode.Parse(File.ReadAllText(file))!;
        var savedEntities = saved["entities"] as JsonArray ?? [];
        var savedRoot = savedEntities.FirstOrDefault(e => e?["id"]?.GetValue<string>() == id);
        Check(savedEntities.Count == 29 && savedRoot?["transform"]?["position"]?[0]?.GetValue<double>() == 12.5,
              "scene file has the edited building");

        // 6. Delete and undo the delete.
        Check(doc.Remove(id) && doc.Scene.Entities.Count == 0 && doc.SelectedId == null, "delete removes the whole model");
        Check(doc.Undo() && doc.Scene.Entities.Count == 29, "undo delete restores 29 entities");
        window.Save();

        // 7. Float the viewport into its own window: a new engine viewport, same document.
        var created = VulkanViewport.CreatedCount;
        window.DockFactory.FloatDockable(window.ViewportDockable);
        var floated = await WaitFor(() => VulkanViewport.CreatedCount > created ? VulkanViewport.Active?.Stats : null,
                                    s => s is { State: Native.ViewportState.Running, FramesPresented: > 0 }, 10);
        Check(floated is { State: Native.ViewportState.Running }, "floated viewport renders the document");
        await Task.Delay(1500);
        var i = 0;
        foreach (var w in desktop.Windows.Where(w => w != window).ToList()) Shot(w, $"editor_3_floating_{i++}.png");

        // 8. The project hub lists the project.
        var hub = new ProjectHub(settings);
        hub.Show();
        await Task.Delay(800);
        Shot(hub, "editor_4_hub.png");
        hub.Close();
        Check(settings.RecentProjects.FirstOrDefault()?.EndsWith("SmokeProject", StringComparison.Ordinal) == true, "recent projects remember the project");

        // 9. Error budget.
        await Task.Delay(300);  // let posted log lines land
        var last = VulkanViewport.Active?.Stats;
        results["validation_errors"] = last?.ValidationErrors ?? 0;
        results["engine_errors_logged"] = EngineLog.ErrorCount;
        Check((last?.ValidationErrors ?? 1) == 0, "zero Vulkan validation errors");
        Check(EngineLog.ErrorCount == 0, "zero engine/editor errors logged");
    }

    private static async Task<T?> WaitFor<T>(Func<T?> read, Func<T?, bool> done, double seconds) where T : struct
    {
        var timer = Stopwatch.StartNew();
        T? value = null;
        while (timer.Elapsed.TotalSeconds < seconds)
        {
            value = read();
            if (done(value)) break;
            await Task.Delay(20);
        }
        return value;
    }

    private static int Finish(Dictionary<string, object> results, List<string> failures, string dir)
    {
        results["failures"] = failures;
        results["passed"] = failures.Count == 0;
        var json = JsonSerializer.Serialize(results, new JsonSerializerOptions { WriteIndented = true });
        File.WriteAllText(Path.Combine(dir, "editor_results.json"), json);
        Console.WriteLine(json);
        return failures.Count == 0 ? 0 : 1;
    }
}
