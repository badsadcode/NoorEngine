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
        Native.noor_doc_set_audio_device(doc.Handle, 0);  // the test plays sounds silently
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
        await ScaleSnapAndComponents(window, settings, id, Check, Shot);
        await PlayReloadAndWindows(window, id, results, Check, Shot);
        await PhysicsChecks(window, id, results, Check, Shot);
        await PlayerChecks(window, results, Check, Shot);
        await CSharpChecks(window, results, Check, Shot);
        await AudioChecks(window, results, Check);

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
        results["engine_errors_expected"] = _expectedErrors;
        Check((last?.ValidationErrors ?? 1) == 0, "zero Vulkan validation errors");
        Check(EngineLog.ErrorCount - _expectedErrors == 0,
              $"zero unexpected engine/editor errors ({EngineLog.ErrorCount} logged, {_expectedErrors} provoked on purpose)");
    }

    // ---- Viewport interaction ----

    private const uint WmMouseMove = 0x0200, WmLButtonDown = 0x0201, WmLButtonUp = 0x0202;
    private const uint WmRButtonDown = 0x0204, WmRButtonUp = 0x0205, WmKeyDown = 0x0100, WmKeyUp = 0x0101;

    private static IntPtr MakeLParam(double x, double y) =>
        (IntPtr)((((int)Math.Round(y) & 0xFFFF) << 16) | ((int)Math.Round(x) & 0xFFFF));

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    private static extern bool PostMessageW(IntPtr hwnd, uint msg, IntPtr wParam, IntPtr lParam);

    // The viewport under test. Mouse input is injected into it (noor_viewport_test_pointer) so the
    // results don't depend on where the real cursor is; keys are posted to its window.
    private static IntPtr _testViewport;

    private static async Task Pointer(double x, double y, int buttons)
    {
        Native.noor_viewport_test_pointer(_testViewport, 1, (int)Math.Round(x), (int)Math.Round(y), buttons);
        await Task.Delay(60);  // a few frames: the render thread samples the pointer once per frame
    }

    private static async Task Click(IntPtr hwnd, double x, double y)
    {
        await Pointer(x, y, 0);
        await Pointer(x, y, 1);
        await Pointer(x, y, 0);
    }

    private static async Task Drag(IntPtr hwnd, IReadOnlyList<(float X, float Y)> path)
    {
        await Pointer(path[0].X, path[0].Y, 0);
        await Pointer(path[0].X, path[0].Y, 1);
        foreach (var p in path.Skip(1)) await Pointer(p.X, p.Y, 1);
        await Pointer(path[^1].X, path[^1].Y, 0);
    }

    private static async Task ViewportInteraction(MainWindow window, string rootId, Action<bool, string> check, Action<Window, string> shot)
    {
        var doc = window.Doc;
        var panel = window.ViewportView!;
        var vp = panel.Viewport;
        var hwnd = vp.NativeWindow;
        _testViewport = vp.EngineHandle;
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
        await Pointer(mid.X, mid.Y, 0);
        await Pointer(mid.X, mid.Y, 2);
        SetFocus(hwnd);  // keys go to the focused window (a real right-click focuses it)
        await Task.Delay(40);
        PostMessageW(hwnd, WmKeyDown, 'W', 1);
        await Task.Delay(600);
        PostMessageW(hwnd, WmKeyUp, 'W', unchecked((IntPtr)0xC0000001));
        await Task.Delay(100);
        var d1 = vp.WorldToScreen(p0)?.Distance ?? 0;
        check(d0 - d1 > 1.0, $"fly: holding W moves the camera forward ({d0 - d1:0.0} m closer)");
        check(doc.Tool == EditorTool.Rotate, "fly: W while flying is not the Move shortcut");
        var sx0 = vp.WorldToScreen(p0)?.X ?? 0;
        await Pointer(mid.X + 80, mid.Y, 2);
        await Task.Delay(90);
        var looked = vp.WorldToScreen(p0);
        await Pointer(mid.X + 80, mid.Y, 0);
        Native.noor_viewport_test_pointer(_testViewport, 0, 0, 0, 0);  // give the mouse back
        await Task.Delay(40);
        check(looked is { } l && l.X < sx0 - 20 && Math.Abs(l.Distance - d1) < 0.01,
              "fly: mouse-look turns the view without moving the camera");
        doc.SetTool(EditorTool.Move);
    }

    private static Vec3 Rotate(Quat q, Vec3 v)
    {
        // v' = q v q*, expanded
        var (x, y, z, w) = (q.X, q.Y, q.Z, q.W);
        var tx = 2 * (y * v.Z - z * v.Y);
        var ty = 2 * (z * v.X - x * v.Z);
        var tz = 2 * (x * v.Y - y * v.X);
        return new Vec3(v.X + w * tx + (y * tz - z * ty), v.Y + w * ty + (z * tx - x * tz), v.Z + w * tz + (x * ty - y * tx));
    }

    private static async Task ScaleSnapAndComponents(MainWindow window, EditorSettings settings, string rootId,
                                                     Action<bool, string> check, Action<Window, string> shot)
    {
        var doc = window.Doc;
        var panel = window.ViewportView!;
        var vp = panel.Viewport;
        var hwnd = vp.NativeWindow;
        _testViewport = vp.EngineHandle;
        async Task Settle()
        {
            await Task.Delay(250);
            panel.DrainEvents();
        }
        doc.Select(rootId);
        var t0 = doc.Selected!.Transform;
        var p0 = t0.Position;
        vp.Frame(doc.Subtree(rootId));
        await Task.Delay(300);

        // Scale gizmo: the X handle follows the building's rotation; pull its tip from 0.95 to 1.9.
        doc.SetTool(EditorTool.Scale);
        await Task.Delay(300);
        shot(window, "editor_7_scale_gizmo.png");
        var size = Native.GizmoScreenScale * (vp.WorldToScreen(p0)?.Distance ?? 0);
        var axis = Rotate(t0.Rotation, new Vec3(1, 0, 0));
        var path = new List<(float X, float Y)>();
        for (var i = 0; i <= 6; i++)
        {
            var along = size * (0.95 + 0.95 * i / 6.0);
            if (vp.WorldToScreen(new Vec3(p0.X + axis.X * along, p0.Y + axis.Y * along, p0.Z + axis.Z * along)) is { } s) path.Add((s.X, s.Y));
        }
        await Drag(hwnd, path);
        await Settle();
        var scaled = doc.Selected!.Transform.Scale;
        check(Math.Abs(scaled.X - 2.0) < 0.1 && Math.Abs(scaled.Y - 1) < 1e-6 && Math.Abs(scaled.Z - 1) < 1e-6,
              $"scale gizmo stretches along the building's own X (scale {Num.Format(scaled.X)}, expected 2)");
        check(doc.Scene.UndoLabel == "Scale" && doc.Undo() && doc.Selected!.Transform.Scale == t0.Scale, "undo gizmo scale");

        // Snapping: with a 1 m grid, a 2.37 m drag along X lands on a whole metre.
        var prefs = settings.Preferences.Copy();
        prefs.Snap = true;
        prefs.MoveStep = 1.0;
        settings.SetPreferences(prefs);
        doc.SetTool(EditorTool.Move);
        await Task.Delay(300);
        path.Clear();
        for (var i = 0; i <= 5; i++)
        {
            var along = 0.6 * size + 2.37 * i / 5.0;
            if (vp.WorldToScreen(p0 with { X = p0.X + along }) is { } s) path.Add((s.X, s.Y));
        }
        await Drag(hwnd, path);
        await Settle();
        var snappedX = doc.Selected!.Transform.Position.X;
        check(Math.Abs(snappedX - Math.Round(snappedX)) < 1e-4 && Math.Abs(snappedX - (p0.X + 2.37)) < 0.75,
              $"snapping lands moves on the 1 m grid (x = {Num.Format(snappedX)}, from {Num.Format(p0.X)})");
        check(doc.Undo(), "undo snapped move");
        prefs = settings.Preferences.Copy();
        prefs.Snap = false;
        prefs.FlySpeed = 20;
        settings.SetPreferences(prefs);
        var saved = File.ReadAllText(EditorSettings.FilePath);
        check(saved.Contains("\"FlySpeed\": 20") && saved.Contains("\"Snap\": false"), "preferences are saved");

        // Components: add, edit a field, undo, remove, undo (the Inspector's own code path).
        var comps = window.Inspector!.Components;
        check(comps.SetComponent("smoke_test", new System.Text.Json.Nodes.JsonObject { ["speed"] = 1.0, ["label"] = "hi" }),
              "add a custom component");
        check(comps.SetField("smoke_test", "speed", System.Text.Json.Nodes.JsonValue.Create(2.5)) &&
              doc.Selected!.Components["smoke_test"]?["speed"]?.GetValue<double>() == 2.5, "edit a component field");
        check(doc.Undo() && doc.Selected!.Components["smoke_test"]?["speed"]?.GetValue<double>() == 1.0, "undo a field edit");
        await Task.Delay(200);
        shot(window, "editor_8_components.png");
        check(comps.SetComponent("smoke_test", null) && !doc.Selected!.Components.ContainsKey("smoke_test"), "remove a component");
        check(doc.Undo() && doc.Selected!.Components.ContainsKey("smoke_test"), "undo the removal");
        check(doc.Undo() && doc.Undo() && !doc.Selected!.Components.ContainsKey("smoke_test"), "undo back to no component");
        Native.noor_viewport_test_pointer(_testViewport, 0, 0, 0, 0);  // give the mouse back
    }

    private static async Task PlayReloadAndWindows(MainWindow window, string rootId, Dictionary<string, object> results,
                                                   Action<bool, string> check, Action<Window, string> shot)
    {
        var doc = window.Doc;
        doc.Select(rootId);

        // HiDPI: the swapchain must match the viewport control in physical pixels at any scaling.
        var vp = window.ViewportView!.Viewport;
        var scaling = window.RenderScaling;
        var stats = vp.Stats!.Value;
        results["render_scaling"] = scaling;
        var expectW = vp.Bounds.Width * scaling;
        var expectH = vp.Bounds.Height * scaling;
        check(Math.Abs(stats.Width - expectW) <= 2 && Math.Abs(stats.Height - expectH) <= 2,
              $"HiDPI: swapchain {stats.Width}x{stats.Height} matches the control at scale {scaling:0.##} ({expectW:0}x{expectH:0})");

        // Live reload: the model file is rewritten on disk (as a generator would); the viewport reloads it.
        var glb = Path.Combine(doc.ProjectRoot, "Assets", "Buildings", BuildingGlb + ".glb");
        File.Copy(Path.Combine(Program.Options.TestData!, "badbuilding", BuildingGlb + ".glb"), glb, overwrite: true);
        File.SetLastWriteTimeUtc(glb, DateTime.UtcNow.AddSeconds(2));
        var reloaded = false;
        for (var i = 0; i < 40 && !reloaded; i++)
        {
            await Task.Delay(100);
            reloaded = EngineLog.Lines.Any(l => l.Category == "Viewport" && l.Message.Contains($"Reloaded {BuildingGlb}.glb"));
        }
        check(reloaded, "live reload: a model changed on disk is reloaded in the viewport");

        // Several windows: float the Inspector and edit through it.
        if (window.FindDockable("Inspector") is { } inspectorPanel)
        {
            window.DockFactory.FloatDockable(inspectorPanel);
            await Task.Delay(800);
            var floating = window.Inspector!;
            check(TopLevel.GetTopLevel(floating) != window, "the Inspector floats in its own window");
            floating.TypeName("Floating edit");
            check(doc.Selected?.Name == "Floating edit", "editing through the floating Inspector");
            check(doc.Undo() && doc.Selected?.Name != "Floating edit", "undo works across windows");
        }
        else
        {
            check(false, "Inspector panel found");
        }

        // Play: a rotator turns the building while playing; Stop restores the edit-time scene.
        var yaw0 = doc.Selected!.Transform.Rotation.ToEulerDegrees().Y;
        check(window.Inspector!.Components.SetComponent("rotator", new System.Text.Json.Nodes.JsonObject { ["degrees_per_second"] = 30.0 }),
              "add a rotator");
        window.TogglePlay();
        await Task.Delay(1000);
        doc.Refresh();
        var running = doc.Selected?.Transform.Rotation.ToEulerDegrees().Y ?? yaw0;
        var turned = Math.Abs(running - yaw0);
        check(doc.Playing && turned is > 15 and < 60, $"play: the rotator turns the building ({turned:0} degrees in about 1 s at 30/s)");
        check(!doc.Rename(rootId, "nope"), "play: edits are refused while playing");
        shot(window, "editor_9_playing.png");
        window.TogglePlay();
        check(!doc.Playing && Math.Abs(doc.Selected!.Transform.Rotation.ToEulerDegrees().Y - yaw0) < 1e-3,
              "stop: the scene is back as it was edited");
        check(doc.Undo() && !doc.Selected!.Components.ContainsKey("rotator"), "undo the rotator");

        // Game input (Milestone 4, ADR-027): keys pressed in the viewport drive the running game.
        // An input_mover follows W/A/S/D; W must not switch the editor to the Move tool while playing.
        var start = doc.Selected!.Transform.Position;
        check(window.Inspector!.Components.SetComponent("input_mover", new System.Text.Json.Nodes.JsonObject { ["speed"] = 5.0 }),
              "add an input_mover");
        doc.SetTool(EditorTool.Rotate);
        var gameView = window.ViewportView!.Viewport;
        var gameStats = gameView.Stats!.Value;
        // Test mode: the viewport counts as focused, whatever window Windows puts in front.
        Native.noor_viewport_test_pointer(gameView.EngineHandle, 1, (int)gameStats.Width / 2, (int)gameStats.Height / 2, 0);
        window.TogglePlay();
        var hwnd = window.ViewportView!.Viewport.NativeWindow;
        // Keys go to the focused window; another window (or app) may be active right now.
        var focused = false;
        for (var attempt = 0; attempt < 5 && !focused; attempt++)
        {
            window.Activate();
            await Task.Delay(50);
            SetFocus(hwnd);
            await Task.Delay(50);
            focused = window.ViewportHasFocus();
        }
        PostMessageW(hwnd, WmKeyDown, 'D', 1);
        PostMessageW(hwnd, WmKeyDown, 'W', 1);
        await Task.Delay(600);
        PostMessageW(hwnd, WmKeyUp, 'D', unchecked((IntPtr)0xC0000001));
        PostMessageW(hwnd, WmKeyUp, 'W', unchecked((IntPtr)0xC0000001));
        await Task.Delay(100);
        doc.Refresh();
        var moved = doc.Selected!.Transform.Position;
        var dx = moved.X - start.X;
        var dz = moved.Z - start.Z;
        // Moved right and forward equally. (Exact speed is a unit test; here a busy machine slows the
        // editor's play timer, so less game time may pass in the same 0.6 s.)
        check(doc.Playing && dx is > 0.3 and < 4.5 && dz is < -0.3 and > -4.5 && Math.Abs(dx + dz) < 0.05,
              $"game input: D and W move the input_mover while playing (dx {dx:0.0}, dz {dz:0.0} m in ~0.6 s at 5 m/s; viewport focused: {focused})");
        check(doc.Tool == EditorTool.Rotate, "game input: W while playing is not the Move-tool shortcut");
        window.TogglePlay();
        check(doc.Selected!.Transform.Position == start, "stop: the input_mover is back where it was");
        check(doc.Undo() && !doc.Selected!.Components.ContainsKey("input_mover"), "undo the input_mover");

        // Lua (Milestone 4, part 2): a script created from the Inspector drives the building.
        var comps = window.Inspector!.Components;
        check(comps.AddLuaScript("Driver") && doc.Selected!.Components["script"]?["file"]?.GetValue<string>() == "Scripts/Driver.luau",
              "Lua: Add component > Lua script attaches Scripts/Driver.luau");
        var scriptFile = Path.Combine(doc.ProjectRoot, "Scripts", "Driver.luau");
        check(File.Exists(scriptFile) && File.ReadAllText(scriptFile).Contains("function Driver:on_update(dt)"), "Lua: the script file is created from the template");
        File.WriteAllText(scriptFile, """
            local Driver = {}
            function Driver:on_start()
                noor.log("Driver ready on", self.entity:name())
            end
            function Driver:on_update(dt)
                local x = noor.input.axis("move_x")
                self.entity:translate(vector.create(x * self.params.speed * dt, 0, 0))
                if noor.input.pressed("jump") then
                    self.entity:rotate(vector.create(0, 1, 0), 90)
                end
            end
            return Driver
            """);
        check(comps.SetField("script", "params.speed", System.Text.Json.Nodes.JsonValue.Create(6.0)), "Lua: set the speed param");
        await Task.Delay(150);
        shot(TopLevel.GetTopLevel(window.Inspector!) as Window ?? window, "editor_10_lua_script.png");
        var yawBefore = doc.Selected!.Transform.Rotation.ToEulerDegrees().Y;
        window.TogglePlay();
        for (var attempt = 0; attempt < 5 && !window.ViewportHasFocus(); attempt++)
        {
            window.Activate();
            await Task.Delay(50);
            SetFocus(hwnd);
            await Task.Delay(50);
        }
        PostMessageW(hwnd, WmKeyDown, 'D', 1);
        await Task.Delay(600);
        PostMessageW(hwnd, WmKeyUp, 'D', unchecked((IntPtr)0xC0000001));
        PostMessageW(hwnd, WmKeyDown, 0x20, 1);  // Space: jump action
        await Task.Delay(100);
        PostMessageW(hwnd, WmKeyUp, 0x20, unchecked((IntPtr)0xC0000001));
        await Task.Delay(100);
        doc.Refresh();
        var driven = doc.Selected!.Transform;
        var driveDx = driven.Position.X - start.X;
        var turned90 = Math.Abs(((driven.Rotation.ToEulerDegrees().Y - yawBefore) % 360 + 360) % 360 - 90) < 1;
        check(doc.Playing && driveDx is > 0.3 and < 5.5, $"Lua: the script moves the building with D (dx {driveDx:0.0} m in ~0.6 s at 6 m/s)");
        check(turned90, $"Lua: Space (the jump action) turns it 90 degrees once (yaw {yawBefore:0.#} -> {driven.Rotation.ToEulerDegrees().Y:0.#}, still playing: {doc.Playing})");
        check(EngineLog.Lines.Any(l => l.Category == "Script" && l.Message.Contains("Driver ready on")), "Lua: noor.log reaches the Console");
        window.TogglePlay();
        check(doc.Selected!.Transform.Position == start, "stop: the scripted building is back");
        check(doc.Undo() && doc.Undo() && !doc.Selected!.Components.ContainsKey("script"), "undo the script component");
        Native.noor_viewport_test_pointer(gameView.EngineHandle, 0, 0, 0, 0);
        doc.SetTool(EditorTool.Move);
    }

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    private static extern IntPtr SetFocus(IntPtr hwnd);

    // Errors the test provokes on purpose (compile errors, a throwing script), excluded from the budget.
    private static int _expectedErrors;

    private static async Task<int> CountErrors(Func<Task> provoke)
    {
        await Task.Delay(150);
        var before = EngineLog.ErrorCount;
        await provoke();
        await Task.Delay(300);  // log lines are posted to the UI thread
        _expectedErrors += EngineLog.ErrorCount - before;
        return EngineLog.ErrorCount - before;
    }

    // Milestone 4, part 5: sound (mixed silently: the test switched the device off).
    private static async Task AudioChecks(MainWindow window, Dictionary<string, object> results, Action<bool, string> check)
    {
        var doc = window.Doc;
        var dir = Path.Combine(doc.ProjectRoot, "Assets", "Audio");
        Directory.CreateDirectory(dir);
        WriteTone(Path.Combine(dir, "beep.wav"), 1.0);
        doc.RefreshAssets(rescan: true);
        var asset = doc.Assets.FirstOrDefault(a => a.Path == "Assets/Audio/beep.wav");
        check(asset is { Kind: "Sound" }, "sound: a WAV in Assets/ becomes a Sound asset");
        if (asset == null) return;
        window.AddAsset(asset);
        var emitter = doc.Selected;
        check(emitter is { Kind: "Sound", Name: "beep" } && emitter.Components["audio_source"]?["clip"]?.GetValue<string>() == asset.Guid,
              "sound: Add to scene makes an emitter with an audio_source");
        if (emitter == null) return;
        var comps = window.Inspector!.Components;
        comps.SetField("audio_source", "loop", System.Text.Json.Nodes.JsonValue.Create(true));
        comps.AddCSharpScript("Radio");
        File.WriteAllText(Path.Combine(doc.ProjectRoot, "Scripts", "Radio.cs"), """
            using System.Text.Json.Nodes;
            using NoorEngine;

            public class Radio : NoorScript
            {
                private bool _shot;

                public override void OnStart()
                {
                    Noor.SetVolume("music", 0.5f);
                    _shot = Noor.PlaySound("Assets/Audio/beep.wav", Entity.WorldPosition, 0.5f);
                }

                public override void OnUpdate(float dt) =>
                    Entity.Set("heard", new JsonObject { ["playing"] = Entity.IsPlaying, ["shot"] = _shot });
            }
            """);
        check(await window.PlayAsync(), "sound: play starts with an emitter and a C# radio script");
        await Task.Delay(500);
        doc.Refresh();
        var heard = doc.Scene.Entities.First(e => e.Id == emitter.Id).Components["heard"];
        check(heard?["playing"]?.GetValue<bool>() == true, "sound: the emitter's looping clip is playing");
        check(heard?["shot"]?.GetValue<bool>() == true, "sound: Noor.PlaySound plays a one-shot from C#");
        window.TogglePlay();
        check(EngineLog.Lines.All(l => l.Category != "Audio" || l.Level < 4), "sound: no audio errors");
        results["audio"] = "mixed silently (no device in tests)";
    }

    // A mono 16-bit WAV with a 440 Hz tone.
    private static void WriteTone(string path, double seconds)
    {
        const int rate = 44100;
        var frames = (int)(seconds * rate);
        using var w = new BinaryWriter(File.Create(path));
        w.Write("RIFF"u8);
        w.Write(36 + frames * 2);
        w.Write("WAVEfmt "u8);
        w.Write(16);
        w.Write((short)1);
        w.Write((short)1);
        w.Write(rate);
        w.Write(rate * 2);
        w.Write((short)2);
        w.Write((short)16);
        w.Write("data"u8);
        w.Write(frames * 2);
        for (var i = 0; i < frames; i++) w.Write((short)(16383 * Math.Sin(2 * Math.PI * 440 * i / rate)));
    }

    // Milestone 4, part 4: C# scripts, compiled on Play.
    private static async Task CSharpChecks(MainWindow window, Dictionary<string, object> results,
                                           Action<bool, string> check, Action<Window, string> shot)
    {
        var doc = window.Doc;
        var box = window.CreateEntity(EntityTemplates.All.First(t => t.Name == "Box"));
        if (box == null)
        {
            check(false, "C#: a box to script");
            return;
        }
        var t0 = doc.Selected!.Transform;
        doc.SetTransform(box, t0 with { Position = new Vec3(-22, 0.5, 6) });
        var comps = window.Inspector!.Components;
        check(comps.AddCSharpScript("Spinner") && doc.Selected!.Components["script"]?["file"]?.GetValue<string>() == "Scripts/Spinner.cs",
              "C#: Add component > C# script attaches Scripts/Spinner.cs");
        var file = Path.Combine(doc.ProjectRoot, "Scripts", "Spinner.cs");
        check(File.Exists(file) && File.Exists(Path.Combine(doc.ProjectRoot, "Scripts", "Scripts.csproj")),
              "C#: the script and Scripts.csproj are created");
        comps.SetField("script", "params.speed", System.Text.Json.Nodes.JsonValue.Create(90.0));

        const string good = """
            using System.Numerics;
            using System.Text.Json.Nodes;
            using NoorEngine;

            public class Spinner : NoorScript
            {
                public override void OnStart() => Noor.Log($"Spinner ready on {Entity.Name}");

                public override void OnUpdate(float dt)
                {
                    Entity.Rotate(Vector3.UnitY, Param("speed", 0) * dt);
                    Entity.Set("csharp", new JsonObject { ["time"] = Noor.Time, ["floor"] = Noor.Find("Floor")?.Name });
                }
            }
            """;
        File.WriteAllText(file, good);
        var timer = System.Diagnostics.Stopwatch.StartNew();
        var started = await window.PlayAsync();
        results["csharp_first_compile_s"] = Math.Round(timer.Elapsed.TotalSeconds, 1);
        check(started && doc.Playing, $"C#: Play compiles the scripts and starts ({timer.Elapsed.TotalSeconds:0.0} s)");
        await Task.Delay(1000);
        doc.Refresh();
        var running = doc.Scene.Entities.First(e => e.Id == box);
        var yaw = Math.Abs(running.Transform.Rotation.ToEulerDegrees().Y);
        check(yaw > 20, $"C#: OnUpdate turns the box ({yaw:0} degrees after about 1 s at 90/s)");
        check(running.Components["csharp"]?["floor"]?.GetValue<string>() == "Floor", "C#: Entity.Set and Noor.Find work");
        check(EngineLog.Lines.Any(l => l.Category == "Script" && l.Message.Contains("Spinner ready on Box")), "C#: Noor.Log reaches the Console");
        await Task.Delay(200);
        shot(window, "editor_13_csharp.png");
        window.TogglePlay();

        // A compile error: shown with file and line; play doesn't start.
        File.WriteAllText(file, good.Replace("Noor.Log($\"Spinner ready on {Entity.Name}\");", "Noor.Log(\"oops\")"));
        var compileErrors = await CountErrors(async () => check(!await window.PlayAsync() && !doc.Playing, "C#: a compile error stops Play"));
        check(compileErrors >= 1 && EngineLog.Lines.Any(l => l.Category == "Script" && l.Message.StartsWith("Scripts/Spinner.cs:7:") && l.Message.Contains("CS1002")),
              "C#: the compile error names Scripts/Spinner.cs:7 (CS1002)");

        // An exception: reported with the line; that script stops, the game keeps running.
        File.WriteAllText(file, good.Replace("Entity.Rotate(Vector3.UnitY, Param(\"speed\", 0) * dt);", "throw new InvalidOperationException(\"boom\");"));
        await CountErrors(async () =>
        {
            check(await window.PlayAsync(), "C#: a script that throws still compiles and plays");
            await Task.Delay(300);
        });
        check(doc.Playing && EngineLog.Lines.Any(l => l.Category == "Script" && l.Message.Contains("InvalidOperationException: boom") && l.Message.Contains("Spinner.cs:")),
              "C#: an exception is reported with its line and the game keeps running");
        window.TogglePlay();

        // Fixed again: the new code is compiled and loaded (the old assembly was unloaded on Stop).
        File.WriteAllText(file, good.Replace("Param(\"speed\", 0)", "-Param(\"speed\", 0)"));
        check(await window.PlayAsync(), "C#: edited scripts reload on the next Play");
        await Task.Delay(700);
        doc.Refresh();
        var again = doc.Scene.Entities.First(e => e.Id == box).Transform.Rotation.ToEulerDegrees().Y;
        check(again < -10, $"C#: the new version runs (turning the other way: {again:0} degrees)");
        window.TogglePlay();
    }

    // Milestone 4, part 3b: a first-person player on the physics floor.
    private static async Task PlayerChecks(MainWindow window, Dictionary<string, object> results,
                                           Action<bool, string> check, Action<Window, string> shot)
    {
        var doc = window.Doc;
        var player = window.CreatePlayer(new Vec3(-25, 0.05, 3));  // on the floor made by PhysicsChecks
        var kids = doc.Scene.Entities.Where(e => e.Parent == player).Select(e => e.Name).ToHashSet();
        check(player != null && kids.SetEquals(["Body", "Camera"]) && File.Exists(Path.Combine(doc.ProjectRoot, EntityTemplates.PlayerScriptPath)),
              "Create > Player: player, body, camera and Scripts/PlayerController.luau");
        check(doc.Scene.UndoLabel == "Create Player", "Create > Player is one undo step");
        if (player == null) return;

        var vp = window.ViewportView!.Viewport;
        var hwnd = vp.NativeWindow;
        var stats = vp.Stats!.Value;
        _testViewport = vp.EngineHandle;
        await Pointer(stats.Width / 2.0, stats.Height / 2.0, 0);  // test mode: counts as focused
        window.TogglePlay();
        for (var attempt = 0; attempt < 5 && !window.ViewportHasFocus(); attempt++)
        {
            window.Activate();
            await Task.Delay(50);
            SetFocus(hwnd);
            await Task.Delay(50);
        }
        await Task.Delay(400);  // settle on the floor
        doc.Refresh();
        Vec3 P() => doc.Scene.Entities.First(e => e.Id == player).Transform.Position;
        var p0 = P();
        check(Math.Abs(p0.Y) < 0.05, $"player stands on the floor (feet at y = {p0.Y:0.000})");

        // The viewport now looks through the player's camera: a point straight ahead is mid-screen.
        var ahead = vp.WorldToScreen(new Vec3(p0.X, p0.Y + 1.6, p0.Z - 6));
        check(ahead is { } a && Math.Abs(a.X - stats.Width / 2.0) < stats.Width * 0.03 && Math.Abs(a.Y - stats.Height / 2.0) < stats.Height * 0.03,
              "play: the viewport looks through the player's camera");

        PostMessageW(hwnd, WmKeyDown, 'W', 1);
        await Task.Delay(800);
        PostMessageW(hwnd, WmKeyUp, 'W', unchecked((IntPtr)0xC0000001));
        await Task.Delay(150);
        doc.Refresh();
        var p1 = P();
        results["player_walk_m"] = Math.Round(p0.Z - p1.Z, 2);
        check(p1.Z < p0.Z - 0.5 && Math.Abs(p1.Y - p0.Y) < 0.05 && Math.Abs(p1.X - p0.X) < 0.05,
              $"W walks the player forward along -Z (moved {p0.Z - p1.Z:0.00} m, stayed on the floor)");

        PostMessageW(hwnd, WmKeyDown, 0x20, 1);  // Space: jump
        await Task.Delay(80);
        PostMessageW(hwnd, WmKeyUp, 0x20, unchecked((IntPtr)0xC0000001));
        var peak = 0.0;
        for (var i = 0; i < 10; i++)
        {
            await Task.Delay(40);
            doc.Refresh();
            peak = Math.Max(peak, P().Y);
        }
        results["player_jump_peak_m"] = Math.Round(peak, 2);
        check(peak > 0.3, $"Space jumps (peak {peak:0.00} m)");

        // Mouse capture: a click asks for it (the script), Escape gives it back. The test pointer
        // stands in for the mouse, so this never takes the real cursor.
        await Pointer(stats.Width / 2.0, stats.Height / 2.0, 1);
        await Pointer(stats.Width / 2.0, stats.Height / 2.0, 0);
        await Task.Delay(100);
        check(Native.noor_viewport_mouse_captured(vp.EngineHandle) == 1, "a click in the game view captures the mouse (script request)");
        shot(window, "editor_12_player_view.png");
        PostMessageW(hwnd, WmKeyDown, 0x1B, 1);  // Escape
        await Task.Delay(80);
        PostMessageW(hwnd, WmKeyUp, 0x1B, unchecked((IntPtr)0xC0000001));
        await Task.Delay(80);
        check(Native.noor_viewport_mouse_captured(vp.EngineHandle) == 0, "Escape releases the mouse");
        Native.noor_viewport_test_pointer(_testViewport, 0, 0, 0, 0);

        window.TogglePlay();
        doc.Refresh();
        check(P().Z == 3 && !doc.Playing, "stop: the player is back at the start");
        check(EngineLog.Lines.All(l => l.Category != "Script" || l.Level < 4), "the PlayerController template runs without script errors");
    }

    // Milestone 4, part 3: physics in play mode.
    private static async Task PhysicsChecks(MainWindow window, string buildingId, Dictionary<string, object> results,
                                            Action<bool, string> check, Action<Window, string> shot)
    {
        var doc = window.Doc;
        EntityTemplates.Template T(string name) => EntityTemplates.All.First(t => t.Name == name);
        var floor = window.CreateEntity(T("Floor"));
        var crate = window.CreateEntity(T("Physics crate"));
        check(floor != null && crate != null && doc.Scene.Entities.Any(e => e.Id == crate && e.Kind == "Shape"),
              "Create menu: a floor and a physics crate");
        // Off to the side of the building, so the screenshot shows them.
        foreach (var (eid, y) in new[] { (floor!, 0.0), (crate!, 3.0) })
        {
            var t = doc.Scene.Entities.First(e => e.Id == eid).Transform;
            doc.SetTransform(eid, t with { Position = new Vec3(-25, y, 0) });
        }

        // The building gets a static mesh collider made from its own GLB triangles; a ball drops on it.
        var building = doc.Scene.Entities.First(e => e.Id == buildingId);
        var top = building.Transform.Position;
        doc.Select(buildingId);
        check(window.Inspector!.Components.SetComponent("collider", new System.Text.Json.Nodes.JsonObject { ["shape"] = "mesh" }),
              "a mesh collider on the BadBuilding block");
        var ball = doc.Create("Roof ball", T("Physics ball").Components(), new Vec3(top.X, 60, top.Z));
        check(ball != null, "a ball above the building");

        window.TogglePlay();
        await Task.Delay(2500);
        doc.Refresh();
        double Y(string? id) => doc.Scene.Entities.FirstOrDefault(e => e.Id == id)?.Transform.Position.Y ?? double.NaN;
        var crateY = Y(crate);
        var ballY = Y(ball);
        results["crate_rest_y"] = Math.Round(crateY, 3);
        results["roof_ball_y"] = Math.Round(ballY, 2);
        doc.Select(crate);
        window.ViewportView?.Viewport.Frame([floor!, ball!, buildingId]);
        await Task.Delay(400);
        shot(window, "editor_11_physics.png");
        check(Math.Abs(crateY - 0.5) < 0.05, $"physics: the crate fell 2.5 m and rests on the floor (y = {crateY:0.000}, expected 0.5)");
        check(ballY > 4 && ballY < 59, $"physics: the ball landed on the building's roof, not the ground (y = {ballY:0.0} m)");
        window.TogglePlay();
        check(Math.Abs(Y(crate) - 3) < 1e-6 && Math.Abs(Y(ball) - 60) < 1e-6, "stop: the crate and ball are back where they were placed");
        check(EngineLog.Lines.All(l => l.Category != "Physics" || l.Level < 4), "physics: no collider errors");
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
