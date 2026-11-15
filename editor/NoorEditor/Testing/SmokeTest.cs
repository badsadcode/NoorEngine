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

        // 7. Viewport interaction with real (posted) mouse input on the engine's child window.
        await ViewportInteraction(window, id, Check, Shot);

        // 8. Layout: panel sizes are remembered and restored; Reset Layout restores the default.
        var left = window.FindDock("Left");
        if (left != null)
        {
            left.Proportion = 0.3;
            window.RememberLayout();
            Check(settings.DockProportions.GetValueOrDefault("Left") == 0.3, "layout: panel size saved to settings");
            window.ReloadLayout();
            Check(Math.Abs((window.FindDock("Left")?.Proportion ?? 0) - 0.3) < 1e-6, "layout: saved panel size restored");
            window.ResetLayout();
            Check(Math.Abs((window.FindDock("Left")?.Proportion ?? 0) - 0.18) < 1e-6 && settings.DockProportions.Count == 0, "layout: reset restores the default");
            await WaitFor(() => VulkanViewport.Active?.Stats, s => s is { State: Native.ViewportState.Running, FramesPresented: > 0 }, 10);
        }
        else
        {
            Check(false, "layout: Left dock found");
        }

        // 9. Float the viewport into its own window: a new engine viewport, same document.
        var created = VulkanViewport.CreatedCount;
        window.DockFactory.FloatDockable(window.ViewportDockable);
        var floated = await WaitFor(() => VulkanViewport.CreatedCount > created ? VulkanViewport.Active?.Stats : null,
                                    s => s is { State: Native.ViewportState.Running, FramesPresented: > 0 }, 10);
        Check(floated is { State: Native.ViewportState.Running }, "floated viewport renders the document");
        await Task.Delay(1500);
        var i = 0;
        foreach (var w in desktop.Windows.Where(w => w != window).ToList()) Shot(w, $"editor_3_floating_{i++}.png");

        // 10. The project hub lists the project.
        var hub = new ProjectHub(settings);
        hub.Show();
        await Task.Delay(800);
        Shot(hub, "editor_4_hub.png");
        hub.Close();
        Check(settings.RecentProjects.FirstOrDefault()?.EndsWith("SmokeProject", StringComparison.Ordinal) == true, "recent projects remember the project");

        // 11. Error budget.
        await Task.Delay(300);  // let posted log lines land
        var last = VulkanViewport.Active?.Stats;
        results["validation_errors"] = last?.ValidationErrors ?? 0;
        results["engine_errors_logged"] = EngineLog.ErrorCount;
        Check((last?.ValidationErrors ?? 1) == 0, "zero Vulkan validation errors");
        Check(EngineLog.ErrorCount == 0, "zero engine/editor errors logged");
    }

    // ---- Viewport interaction ----

    private const uint WmMouseMove = 0x0200, WmLButtonDown = 0x0201, WmLButtonUp = 0x0202;
    private const uint WmRButtonDown = 0x0204, WmRButtonUp = 0x0205, WmKeyDown = 0x0100, WmKeyUp = 0x0101;

    private static IntPtr MakeLParam(double x, double y) =>
        (IntPtr)((((int)Math.Round(y) & 0xFFFF) << 16) | ((int)Math.Round(x) & 0xFFFF));

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    private static extern bool PostMessageW(IntPtr hwnd, uint msg, IntPtr wParam, IntPtr lParam);

    /// <summary>Sends a mouse message to OUR viewport child window only (never to the desktop).</summary>
    private static async Task Mouse(IntPtr hwnd, uint msg, double x, double y, bool leftDown)
    {
        var lParam = (IntPtr)((((int)Math.Round(y) & 0xFFFF) << 16) | ((int)Math.Round(x) & 0xFFFF));
        PostMessageW(hwnd, msg, leftDown ? 1 : 0, lParam);
        await Task.Delay(60);  // a few frames: the render thread samples the pointer once per frame
    }

    private static async Task Click(IntPtr hwnd, double x, double y)
    {
        await Mouse(hwnd, WmMouseMove, x, y, false);
        await Mouse(hwnd, WmLButtonDown, x, y, true);
        await Mouse(hwnd, WmLButtonUp, x, y, false);
    }

    private static async Task Drag(IntPtr hwnd, IReadOnlyList<(float X, float Y)> path)
    {
        await Mouse(hwnd, WmMouseMove, path[0].X, path[0].Y, false);
        await Mouse(hwnd, WmLButtonDown, path[0].X, path[0].Y, true);
        foreach (var p in path.Skip(1)) await Mouse(hwnd, WmMouseMove, p.X, p.Y, true);
        await Mouse(hwnd, WmLButtonUp, path[^1].X, path[^1].Y, false);
    }

    private static async Task ViewportInteraction(MainWindow window, string rootId, Action<bool, string> check, Action<Window, string> shot)
    {
        var doc = window.Doc;
        var panel = window.ViewportView!;
        var vp = panel.Viewport;
        var hwnd = vp.NativeWindow;
        var stats = vp.Stats!.Value;
        async Task Settle()
        {
            await Task.Delay(250);
            panel.DrainEvents();
        }

        // Picking: frame the building, then click near the middle until something is hit.
        doc.Select(null);
        vp.Frame(doc.Subtree(rootId));
        await Task.Delay(400);
        (double X, double Y)? hitAt = null;
        foreach (var (ox, oy) in new[] { (0, 0), (0, 40), (-60, 20), (60, 20), (0, -40), (-100, 40), (100, 40) })
        {
            var x = stats.Width / 2.0 + ox;
            var y = stats.Height / 2.0 + oy;
            await Click(hwnd, x, y);
            await Settle();
            if (doc.SelectedId != null)
            {
                hitAt = (x, y);
                break;
            }
        }
        check(doc.SelectedId == rootId, "viewport click selects the whole building first");
        if (hitAt is { } at)
        {
            // With the Select tool: under the Move tool a click on the building's middle can land
            // on the gizmo's Y arrow, which (correctly) starts a drag instead of selecting.
            doc.SetTool(EditorTool.Select);
            await Task.Delay(150);
            await Click(hwnd, at.X, at.Y);
            await Settle();
            var deeper = doc.Selected;
            check(deeper?.Parent == rootId, $"second click goes one level deeper (selected {(deeper == null ? "nothing" : deeper.Id == rootId ? "the root again" : deeper.Name)}, gizmo tool {doc.Tool})");
        }
        await Click(hwnd, 4, 4);  // the sky: nothing there
        await Settle();
        check(doc.SelectedId == null, "clicking empty space clears the selection");

        // Move gizmo: drag the X arrow 3 m.
        doc.Select(rootId);
        doc.SetTool(EditorTool.Move);
        await Task.Delay(300);
        shot(window, "editor_5_selected_move_gizmo.png");
        var before = doc.Selected!.Transform;
        var p0 = before.Position;
        if (vp.WorldToScreen(p0) is not { } c0)
        {
            check(false, "gizmo centre is on screen");
            return;
        }
        var size = Native.GizmoScreenScale * c0.Distance;
        var path = new List<(float X, float Y)>();
        for (var i = 0; i <= 5; i++)
        {
            var along = 0.6 * size + 3.0 * i / 5.0;
            if (vp.WorldToScreen(p0 with { X = p0.X + along }) is { } s) path.Add((s.X, s.Y));
        }
        await Drag(hwnd, path);
        await Settle();
        var moved = doc.Selected!.Transform.Position;
        check(Math.Abs(moved.X - (p0.X + 3.0)) < 0.2 && Math.Abs(moved.Y - p0.Y) < 0.01 && Math.Abs(moved.Z - p0.Z) < 0.01,
              $"move gizmo drags along X (dx = {moved.X - p0.X:0.###} m, expected 3)");
        check(doc.Scene.UndoLabel == "Move", "gizmo move is one undo step");
        check(doc.Undo() && doc.Selected!.Transform.Position == p0, "undo gizmo move");

        // Rotate gizmo: drag the Y ring 40 degrees (clockwise seen from above = +yaw).
        doc.SetTool(EditorTool.Rotate);
        await Task.Delay(300);
        var radius = 0.85 * size;
        (float X, float Y)? Ring(double degrees)
        {
            var a = degrees * Math.PI / 180.0;
            return vp.WorldToScreen(new Vec3(p0.X + Math.Cos(a) * radius, p0.Y, p0.Z + Math.Sin(a) * radius)) is { } s ? (s.X, s.Y) : null;
        }
        var start = Enumerable.Range(0, 36).Select(i => i * 10.0).OrderByDescending(a => Ring(a)?.Y ?? -1).First();  // nearest the viewer
        var arc = Enumerable.Range(0, 9).Select(i => Ring(start - 5.0 * i)).Where(p => p != null).Select(p => p!.Value).ToList();
        var yaw0 = before.Rotation.ToEulerDegrees().Y;
        shot(window, "editor_6_rotate_gizmo.png");
        await Drag(hwnd, arc);
        await Settle();
        var yaw1 = doc.Selected!.Transform.Rotation.ToEulerDegrees().Y;
        check(Math.Abs(yaw1 - yaw0 - 40) < 2.5, $"rotate gizmo turns about Y (dyaw = {yaw1 - yaw0:0.#} deg, expected 40)");
        check(doc.Scene.UndoLabel == "Rotate", "gizmo rotation is labelled Rotate");
        check(doc.Undo(), "undo gizmo rotation");

        // Fly mode: hold the right button, press W. The camera moves toward what it looks at,
        // and W does NOT switch to the Move tool while flying. Then mouse-look to the right.
        doc.SetTool(EditorTool.Rotate);
        await Task.Delay(150);
        var mid = (X: stats.Width / 2.0, Y: stats.Height / 2.0);
        var d0 = vp.WorldToScreen(p0)?.Distance ?? 0;
        await Mouse(hwnd, WmMouseMove, mid.X, mid.Y, false);
        PostMessageW(hwnd, WmRButtonDown, 2, MakeLParam(mid.X, mid.Y));
        await Task.Delay(100);
        PostMessageW(hwnd, WmKeyDown, 'W', 1);
        await Task.Delay(600);
        PostMessageW(hwnd, WmKeyUp, 'W', unchecked((IntPtr)0xC0000001));
        await Task.Delay(100);
        var d1 = vp.WorldToScreen(p0)?.Distance ?? 0;
        check(d0 - d1 > 1.0, $"fly: holding W moves the camera forward ({d0 - d1:0.0} m closer)");
        check(doc.Tool == EditorTool.Rotate, "fly: W while flying is not the Move shortcut");
        var sx0 = vp.WorldToScreen(p0)?.X ?? 0;
        PostMessageW(hwnd, WmMouseMove, 2, MakeLParam(mid.X + 80, mid.Y));
        await Task.Delay(150);
        var looked = vp.WorldToScreen(p0);
        PostMessageW(hwnd, WmRButtonUp, 0, MakeLParam(mid.X + 80, mid.Y));
        await Task.Delay(100);
        check(looked is { } l && l.X < sx0 - 20 && Math.Abs(l.Distance - d1) < 0.01,
              "fly: mouse-look turns the view without moving the camera");
        doc.SetTool(EditorTool.Move);
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
