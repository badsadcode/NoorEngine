using System.Diagnostics;
using System.Text.Json;
using System.Text.Json.Nodes;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Input;
using Avalonia.VisualTree;
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
        await NoorCodeChecks(window, results, Check, Shot);
        await AudioChecks(window, results, Check);
        await BuildChecks(window, results, Check, dir);

        // Milestone 5, part 1: Create ▸ Sun makes a directional light pointing down into the scene.
        var sunId = window.CreateEntity(EntityTemplates.All.First(t => t.Name == EntityTemplates.SunTemplateName));
        var sun = sunId == null ? null : doc.Scene.Entities.FirstOrDefault(e => e.Id == sunId);
        var sunPitch = sun?.Transform.Rotation.ToEulerDegrees().X ?? 0;
        Check(sun?.Components["light"]?["type"]?.GetValue<string>() == "directional" && sunPitch < -30,
              $"Create ▸ Sun: a directional light tilted down ({sunPitch:0}°)");
        if (sunId != null) doc.Undo();

        // Milestone 5, part 4: Create ▸ Point light / Spot light.
        foreach (var (template, type) in new[] { ("Point light", "point"), ("Spot light", "spot") })
        {
            var lightId = window.CreateEntity(EntityTemplates.All.First(t => t.Name == template));
            var light = lightId == null ? null : doc.Scene.Entities.FirstOrDefault(e => e.Id == lightId);
            Check(light?.Components["light"]?["type"]?.GetValue<string>() == type &&
                  light.Components["light"]?["intensity"]?.GetValue<double>() > 0, $"Create ▸ {template}: a {type} light in lumens");
            await Task.Delay(200);  // drawn (the viewport shows a marker in its colour)
            if (lightId != null) doc.Undo();
        }

        // Milestone 5, part 2: a texture in Assets/ is imported, and Add to scene puts it on a cube.
        var texDir = Path.Combine(doc.ProjectRoot, "Assets", "Textures");
        Directory.CreateDirectory(texDir);
        File.Copy(Path.Combine(Program.Options.TestData!, "render_scenes", "Assets", "Textures", "checker_color.png"),
                  Path.Combine(texDir, "checker_color.png"), overwrite: true);
        doc.RefreshAssets(rescan: true);
        var texture = doc.Assets.FirstOrDefault(a => a.Path.EndsWith("checker_color.png", StringComparison.Ordinal));
        Check(texture is { Importer: "texture", Kind: "Texture" }, $"textures: a PNG in Assets is a texture asset ({texture?.Importer})");
        if (texture != null)
        {
            var before = doc.Scene.Entities.Count;
            window.AddAsset(texture);
            var cube = doc.Selected;
            Check(doc.Scene.Entities.Count == before + 1 && cube?.Components["primitive"]?["base_color_map"]?.GetValue<string>() == texture.Guid,
                  "textures: Add to scene makes a cube wearing the texture");
            await Task.Delay(500);  // drawn a few times (the end of the test checks Vulkan validation)
            Shot(window, "editor_17_texture.png");
            doc.Undo();
        }

        // Milestone 5, part 5: an HDRI added to the scene becomes its environment (once: a second one replaces it).
        foreach (var hdr in new[] { "kloppenheim_06.hdr", "venice_sunset.hdr" })
            File.Copy(Path.Combine(Program.Options.TestData!, "render_scenes", "Assets", "Environments", hdr), Path.Combine(texDir, hdr), overwrite: true);
        doc.RefreshAssets(rescan: true);
        var envEntities = 0;
        foreach (var hdr in new[] { "kloppenheim_06.hdr", "venice_sunset.hdr" })
        {
            var hdri = doc.Assets.FirstOrDefault(a => a.Path.EndsWith(hdr, StringComparison.Ordinal));
            if (hdri != null) window.AddAsset(hdri);
            await Task.Delay(400);  // processed and drawn (the end of the test checks Vulkan validation)
            envEntities = doc.Scene.Entities.Count(e => e.Components["environment"] != null);
        }
        var envEntity = doc.Scene.Entities.FirstOrDefault(e => e.Components["environment"] != null);
        var venice = doc.Assets.FirstOrDefault(a => a.Path.EndsWith("venice_sunset.hdr", StringComparison.Ordinal));
        Check(envEntities == 1 && envEntity?.Components["environment"]?["hdri"]?.GetValue<string>() == venice?.Guid,
              "environments: an HDRI added to the scene becomes its environment, a second one replaces it");
        Shot(window, "editor_19_environment.png");
        if (envEntity != null) { doc.Undo(); doc.Undo(); }

        // Milestone 5, part 3: a texture set becomes a material; Add to scene puts it on a ball.
        foreach (var map in new[] { "tiles_albedo.png", "tiles_normal.png", "tiles_orm.png" })
            File.Copy(Path.Combine(Program.Options.TestData!, "render_scenes", "Assets", "Textures", map), Path.Combine(texDir, map), overwrite: true);
        doc.RefreshAssets(rescan: true);
        var albedo = doc.Assets.FirstOrDefault(a => a.Path.EndsWith("tiles_albedo.png", StringComparison.Ordinal));
        var made = albedo == null || window.ContentBrowser == null ? null : window.ContentBrowser.MakeMaterial(albedo);
        var materialAsset = doc.Assets.FirstOrDefault(a => a.Path == made);
        Check(made == "Assets/Textures/tiles.noormat" && materialAsset is { Kind: "Material" } &&
              File.ReadAllText(Path.Combine(doc.ProjectRoot, made)).Contains("metallic_roughness_map"),
              $"materials: Make material turns the tiles set (albedo, normal, ORM) into {made}");
        if (materialAsset != null)
        {
            window.AddAsset(materialAsset);
            Check(doc.Selected?.Components["primitive"]?["material"]?.GetValue<string>() == materialAsset.Guid,
                  "materials: Add to scene puts the material on a ball");
            await Task.Delay(600);
            Shot(window, "editor_18_material.png");
            doc.Undo();
            await MaterialEditorChecks(window, materialAsset, Check, Shot);
        }
        await RenderPanelChecks(window, settings, Check, Shot);
        await EnvironmentGalleryChecks(window, Check, Shot);
        await EffectsPanelChecks(window, Check, Shot);
        await TextureSettingsChecks(window, Check, Shot);
        await SpriteChecks(window, Check, Shot);
        await BrushChecks(window, Check, Shot);
        await FaceChecks(window, Check, Shot);
        await TerrainChecks(window, Check, Shot);
        await SculptChecks(window, Check, Shot);
        await PaintChecks(window, Check, Shot);
        await GraphChecks(window, Check, Shot);
        await OakChecks(window, Check, Shot);
        await ScatterChecks(window, Check, Shot);
        CodeEditorChecks(window, results, Check);

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

        // Play's update rate, from the engine's "Stop after 2.9 s (101 frames)" lines: one update per
        // display frame. (A 16 ms timer used to give ~34/s on a 60 Hz screen.)
        var rates = EngineLog.Lines.Select(l => System.Text.RegularExpressions.Regex.Match(l.Message, @"^Stop after ([\d.,]+) s \((\d+) frames\)"))
                                   .Where(m => m.Success)
                                   .Select(m => (Seconds: double.Parse(m.Groups[1].Value.Replace(',', '.'), System.Globalization.CultureInfo.InvariantCulture),
                                                 Frames: int.Parse(m.Groups[2].Value)))
                                   .Where(r => r.Seconds >= 1.0).ToList();
        var slowest = rates.Count == 0 ? 0 : rates.Min(r => r.Frames / r.Seconds);
        results["play_updates_per_second"] = Math.Round(slowest, 1);
        Check(rates.Count > 0 && slowest >= 45, $"play updates the game every display frame (slowest play: {slowest:0} updates/s)");

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

    // Edit ▸ Preferences ▸ Code editor: what the Open button would run (nothing is started here).
    private static void CodeEditorChecks(MainWindow window, Dictionary<string, object> results, Action<bool, string> check)
    {
        var root = window.Doc.ProjectRoot;
        var file = Path.Combine(root, "Scripts", "Radio.cs");
        var editors = CodeEditors.Detect();
        check(editors.Any(e => e.Name == "Notepad"), $"code editor: editors found on this PC ({string.Join(", ", editors.Select(e => e.Name))})");
        var prefs = new Preferences();
        check(prefs.UseNoorCode, "code editor: scripts open in NoorCode by default");
        check(CodeEditors.CommandLine(prefs, root, file).Program == null, "code editor: the external editor is the Windows default app by default");
        prefs.CodeEditor = @"C:\Tools\Code.exe";
        prefs.CodeEditorArguments = "\"{folder}\" -g \"{file}:{line}\"";
        var (program, args) = CodeEditors.CommandLine(prefs, root, file, 12);
        check(program == prefs.CodeEditor && args == $"\"{Path.GetFullPath(Path.Combine(root, "Scripts"))}\" -g \"{Path.GetFullPath(file)}:12\"",
              $"code editor: a chosen editor gets the file, line and Scripts folder ({args})");
        results["code_editors"] = editors.Select(e => e.Name).ToArray();
    }

    // Milestone 4, part 7b: NoorCode, the built-in script editor. Opening at a line, typing (keys
    // go to the text, not the editor's shortcuts), Ctrl+S, Play saving first, compile errors marked
    // on their line and linked from the Console, outside changes reloading, closing the tab.
    // Milestone 5, part 6: the material editor (preview, edits saved to the file, undo, instances).
    private static async Task MaterialEditorChecks(MainWindow window, AssetInfo tiles, Action<bool, string> check, Action<Window, string> shot)
    {
        var doc = window.Doc;
        var file = Path.Combine(doc.ProjectRoot, tiles.Path);
        var material = window.OpenMaterial(tiles.Path);
        await Task.Delay(400);
        var panel = window.GetVisualDescendants().OfType<MaterialPanel>().FirstOrDefault(p => p.Material == material);
        check(material != null && panel != null && window.FindDock("Documents")?.ActiveDockable == material,
              "material editor: Edit opens the material as a tab next to the viewport");
        if (material == null || panel == null) return;
        var stats = await WaitFor(() => panel.Preview.Stats, s => s is { State: Native.ViewportState.Running, FramesPresented: > 5 }, 10);
        check(stats is { State: Native.ViewportState.Running, FramesPresented: > 5 } && VulkanViewport.Active?.IsPreview != true,
              $"material editor: the preview renders ({stats?.FramesPresented} frames) in its own viewport");

        var before = File.ReadAllText(file);
        material.Edit("roughness", 0.2);
        material.Flush();
        var edited = MaterialFile.Read(file);
        check(edited?["roughness"]?.GetValue<double>() == 0.2 && edited["metallic_roughness_map"] != null,
              "material editor: an edit is saved to the .noormat (other fields kept)");
        material.Edit("base_color", new JsonArray(0.8, 0.3, 0.1, 1.0));
        await Task.Delay(700);  // saved by itself after a pause; the preview reloads it
        check(MaterialFile.Read(file)?["base_color"]?[1]?.GetValue<double>() == 0.3, "material editor: edits save themselves");
        shot(window, "editor_20_material_editor.png");
        material.Undo();
        material.Undo();
        check(JsonNode.DeepEquals(MaterialFile.Read(file), JsonNode.Parse(before)),
              "material editor: undo takes the edits back out of the file");
        check(MaterialFile.ToHex(MaterialFile.FromHex("#BC7A3C")!) == "#BC7A3C" && MaterialFile.ToHex([0.5, 0.5, 0.5]) == "#BCBCBC",
              "material editor: colours are sRGB hex in the UI and linear in the file");

        // Save as instance: a child that changes nothing yet, then one field.
        var variant = window.SaveMaterialInstance(material);
        await Task.Delay(400);
        var variantPanel = window.GetVisualDescendants().OfType<MaterialPanel>().FirstOrDefault(p => p.Material == variant);
        var variantFile = variant == null ? null : MaterialFile.Read(variant.FullPath);
        check(variant?.RelativePath == "Assets/Textures/tiles_variant.noormat" && variantFile?["parent"]?.GetValue<string>() == tiles.Guid &&
              doc.Assets.Any(a => a.Path == variant.RelativePath && a.Kind == "Material"),
              $"material instances: Save as instance writes {variant?.RelativePath} with the parent's id");
        if (variant != null && variantPanel != null)
        {
            check(!variantPanel.ShowsOverride("roughness") && variant.Material.GetMap("normal_map") == material.Material.GetMap("normal_map") &&
                  variant.Material.GetMap("normal_map") != null,
                  "material instances: an instance shows its parent's values and textures");
            variant.Edit("uv_scale", new JsonArray(4.0, 4.0));
            variant.Flush();
            await Task.Delay(100);
            check(variantPanel.ShowsOverride("uv_scale") || variant.Material.Sets("uv_scale"), "material instances: a changed field is marked as the instance's own");
            check(MaterialFile.Read(variant.FullPath)?["overrides"]?["uv_scale"] != null && MaterialFile.Read(file)?["uv_scale"] == null,
                  "material instances: the change goes into the instance's overrides, the parent stays as it was");
            variant.Edit("normal_map", null);
            check(variant.Material.GetMap("normal_map") == null && variant.Material.Root["overrides"]?.AsObject().ContainsKey("normal_map") == true,
                  "material instances: (none) hides the parent's texture (null override)");
            variant.Edit("normal_map", null, reset: true);
            await WaitFor(() => variantPanel.Preview.Stats, s => s is { FramesPresented: > 10 }, 10);
            variant.Flush();
            shot(window, "editor_21_material_instance.png");
            window.DockFactory.CloseDockable(variant);
        }
        window.DockFactory.CloseDockable(material);
        window.DockFactory.SetActiveDockable(window.ViewportDockable);
        await Task.Delay(300);
        check(window.OpenMaterials.Count == 0 && VulkanViewport.Active is { IsPreview: false },
              "material editor: closing the tabs leaves the scene viewport");
    }

    // Milestone 8A, part 6: the environment gallery. Applying a preset makes the scene point at a project copy of it, and
    // the scene viewport draws it (the live sky's GPU pass runs); one undo puts the old environment back.
    // Milestone 8B, part 4: the Effects panel. A preset goes into the scene, the viewport previews it while editing, an
    // edit in the panel reaches the preview at once and the file identically, the preset itself never changes, mute
    // hides it, and one undo removes the effect again.
    private static async Task EffectsPanelChecks(MainWindow window, Action<bool, string> check, Action<Window, string> shot)
    {
        if (window.FindDockable("Effects") is not { } dockable)
        {
            check(false, "effects: the Effects panel exists");
            return;
        }
        window.DockFactory.SetActiveDockable(dockable);
        await Task.Delay(400);
        var panel = window.GetVisualDescendants().OfType<EffectsPanel>().FirstOrDefault();
        var doc = window.Doc;
        if (panel == null || !doc.Scene.Open)
        {
            check(false, "effects: the panel shows with a scene open");
            return;
        }
        long Particles() => VulkanViewport.Active?.RenderStats() is { } text && JsonNode.Parse(text)?["particles"] is JsonValue v && v.TryGetValue<long>(out var n) ? n : -1;
        var count = doc.Scene.Entities.Count;
        panel.Select("Fire", null);
        var added = panel.AddToScene();
        var entity = added?["entity"]?.GetValue<string>();
        var file = Path.Combine(doc.ProjectRoot, "Assets", "Effects", "Fire.noorfx");
        check(entity != null && doc.Scene.Entities.Count == count + 1 && File.Exists(file) && panel.SelectedAsset == "Assets/Effects/Fire.noorfx",
              "effects: Add to scene puts a copy of the Fire preset on a new entity and selects the copy for editing");
        var full = await WaitFor(() => (long?)Particles(), n => n > 30, 6);
        check(full > 30, $"effects: the viewport previews the fire while editing ({full} particles)");
        shot(window, "editor_24_effects.png");
        // A much smaller fire, edited in the panel: the preview thins out, the file says so, the preset doesn't change.
        check(panel.SetField("rate", JsonValue.Create(8)), "effects: an edit in the panel saves");
        var thin = await WaitFor(() => (long?)Particles(), n => n is >= 0 and < 15, 6);
        check(thin is >= 0 and < 15, $"effects: the preview follows the edit at once ({full} particles before, {thin} after)");
        var saved = JsonNode.Parse(File.ReadAllText(file));
        check(saved?["emitter"]?["rate"]?.GetValue<double>() == 8 && saved?["preset"]?.GetValue<string>() == "Fire",
              "effects: the edit is in the file, which still remembers its preset");
        check(EditorDocument.EffectPresets().OfType<JsonObject>().First(p => p["name"]?.GetValue<string>() == "Fire")["emitter"]?["rate"]?.GetValue<double>() == 90,
              "effects: the built-in Fire preset is unchanged");
        // Part 7: a sub-emitter where the flames die (embers): the preview draws it as a second effect, and the file has it.
        long Draws() => VulkanViewport.Active?.RenderStats() is { } st && JsonNode.Parse(st)?["particle_draws"] is JsonValue dv && dv.TryGetValue<long>(out var dn) ? dn : -1;
        var embers = JsonNode.Parse("""[{"on": "death", "count": 1, "emitter": {"lifetime": 1, "speed": [0.2, 0.6], "size": 0.03, "color": "#FF8040", "render": {"blend": "additive", "emission": 20000}}}]""");
        check(panel.SetField("sub_emitters", embers), "effects: a sub-emitter can be added in the panel");
        var two = await WaitFor(() => (long?)Draws(), n => n >= 2, 6);
        check(two >= 2 && JsonNode.Parse(File.ReadAllText(file))?["emitter"]?["sub_emitters"]?[0]?["on"]?.GetValue<string>() == "death",
              $"effects: the preview draws the embers where the flames die, and the file has them ({two} effects drawn)");
        // Mute hides it in the preview (its sub-emitter too); mute again shows it.
        panel.Preview("mute");
        var hidden = await WaitFor(() => (long?)Particles(), n => n == 0, 4);
        check(hidden == 0, "effects: Mute hides the selected entity's effect in the preview");
        panel.Preview("mute");
        var back = await WaitFor(() => (long?)Particles(), n => n > 0, 4);
        check(back > 0, "effects: Mute again shows it");
        doc.Undo();
        check(doc.Scene.Entities.Count == count, "effects: one undo removes the effect's entity");
    }

    private static async Task EnvironmentGalleryChecks(MainWindow window, Action<bool, string> check, Action<Window, string> shot)
    {
        if (window.FindDockable("Environments") is not { } dockable)
        {
            check(false, "environments: the gallery has a panel");
            return;
        }
        window.DockFactory.SetActiveDockable(dockable);
        await Task.Delay(400);
        var panel = window.GetVisualDescendants().OfType<EnvironmentPanel>().FirstOrDefault();
        var doc = window.Doc;
        if (panel == null || !doc.Scene.Open)
        {
            check(false, "environments: the gallery shows with a scene open");
            return;
        }
        JsonNode? Component() => doc.Scene.Entities.Select(e => e.Components["environment"]).FirstOrDefault(c => c != null);
        var before = Component()?.ToJsonString();
        panel.Select("Foggy Dawn", null);
        var applied = panel.Apply();
        var guid = applied?["guid"]?.GetValue<string>();
        check(guid != null && Component()?["asset"]?.GetValue<string>() == guid &&
              File.Exists(Path.Combine(doc.ProjectRoot, "Assets", "Environments", "Foggy Dawn.noorenv")),
              "environments: applying Foggy Dawn copies it into Assets/Environments and the scene points at it");
        // The scene viewport now draws the live sky: its Environment pass shows up with a time.
        double env = 0;
        for (var i = 0; i < 25 && env <= 0; ++i)
        {
            await Task.Delay(200);
            var stats = VulkanViewport.Active?.RenderStats() is { } text ? JsonNode.Parse(text) : null;
            env = (stats?["passes"] as JsonArray)?.OfType<JsonObject>().FirstOrDefault(p => p["name"]?.GetValue<string>() == "Environment")?["ms"]?.GetValue<double>() ?? 0;
        }
        check(env > 0, $"environments: the viewport draws the applied environment (live sky pass {env:0.000} ms)");
        check(panel.ProjectEnvironments.Count >= 1, $"environments: the project lists its environment ({panel.ProjectEnvironments.Count})");
        shot(window, "editor_23_environment_gallery.png");
        doc.Undo();
        check(Component()?.ToJsonString() == before, "environments: one undo puts the old environment back");

        // Part 7: Create ▸ Weather zone makes a fog bank the scene's camera can walk into; one undo removes it.
        var count = doc.Scene.Entities.Count;
        var zoneId = window.CreateEntity(EntityTemplates.All.First(t => t.Name == EntityTemplates.WeatherZoneTemplateName));
        var zone = doc.Scene.Entities.FirstOrDefault(e => e.Id == zoneId)?.Components["weather_zone"];
        check(zone?["environment"]?["fog_density"]?.GetValue<double>() > 0 && zone?["blend"]?.GetValue<double>() > 0,
              "weather zones: Create ▸ Weather zone makes a fog bank with a blend distance");
        doc.Undo();
        check(doc.Scene.Entities.Count == count, "weather zones: one undo removes the zone");
    }

    // Milestone 5, part 7: the Render panel's numbers, and anti-aliasing switched from Preferences.
    private static async Task RenderPanelChecks(MainWindow window, EditorSettings settings, Action<bool, string> check, Action<Window, string> shot)
    {
        if (window.FindDockable("Render") is not { } dockable)
        {
            check(false, "render panel: Window ▸ Render stats has a panel to show");
            return;
        }
        window.DockFactory.SetActiveDockable(dockable);
        await Task.Delay(1200);  // two refreshes
        var panel = window.GetVisualDescendants().OfType<RenderPanel>().FirstOrDefault();
        // GPU pass timings arrive a frame or two after the frame (timestamp queries): wait for them
        // rather than reading once (a single read flaked on 2026-11-18).
        await WaitFor(() => { panel?.Refresh(); return (int?)((panel?.Last?["passes"] as JsonArray)?.Count ?? 0); }, n => n == 6, 5);
        var last = panel?.Last;
        long N(string key) => last?[key]?.GetValue<long>() ?? -1;
        check(panel != null && N("draw_calls") > 0 && N("triangles") > 0 && N("render_target_bytes") > 0 &&
              (last?["passes"] as JsonArray)?.Count == 6,  // Environment, Shadows, Scene, Tonemap, Gizmos, HUD
              $"render panel: shows the scene viewport's draws ({N("draw_calls")}), triangles, memory and 6 pass timings");
        check(N("msaa_samples") == 4, $"anti-aliasing: 4x MSAA by default ({N("msaa_samples")})");
        await Task.Delay(300);  // drawn on screen before the picture
        shot(window, "editor_22_render_panel.png");

        var prefs = settings.Preferences.Copy();
        prefs.Msaa = 1;
        settings.SetPreferences(prefs);
        await WaitFor(() => { panel?.Refresh(); return panel?.Last?["msaa_samples"]?.GetValue<long>(); }, v => v == 1, 5);
        check(panel?.Last?["msaa_samples"]?.GetValue<long>() == 1, "anti-aliasing: Preferences ▸ Anti-aliasing Off reaches the viewport");
        prefs = settings.Preferences.Copy();
        prefs.Msaa = 4;
        settings.SetPreferences(prefs);
        await WaitFor(() => { panel?.Refresh(); return panel?.Last?["msaa_samples"]?.GetValue<long>(); }, v => v == 4, 5);
        check(panel?.Last?["msaa_samples"]?.GetValue<long>() == 4 && VulkanViewport.Active?.Stats?.State == Native.ViewportState.Running,
              "anti-aliasing: switching back to 4x rebuilds the frame and keeps drawing");
        check(ExposurePresets.All.Any(p => p.Ev100 == 14) && ExposurePresets.All.Any(p => p.Ev100 <= 8),
              "exposure: presets from bright sun (EV 14) to indoor and night");
        window.DockFactory.SetActiveDockable(window.FindDockable("Content")!);
    }

    // Milestone 5: texture import settings and compression (BC5 normal map -> none -> back).
    private static async Task TextureSettingsChecks(MainWindow window, Action<bool, string> check, Action<Window, string> shot)
    {
        var doc = window.Doc;
        var normal = doc.Assets.FirstOrDefault(a => a.Path.EndsWith("tiles_normal.png", StringComparison.Ordinal));
        var texture = normal == null ? null : window.OpenTexture(normal.Path);
        await Task.Delay(400);
        var panel = window.GetVisualDescendants().OfType<TexturePanel>().FirstOrDefault(p => p.Texture == texture);
        check(texture != null && panel != null && window.FindDock("Documents")?.ActiveDockable == texture,
              "texture settings: Edit opens a texture's import settings as a tab");
        if (texture == null || panel == null) return;
        string? Format() => panel.Last?["format"]?.GetValue<string>();
        long Bytes(string key) => panel.Last?[key]?.GetValue<long>() ?? 0;
        check(Format() == "BC5" && Bytes("gpu_bytes") * 3 < Bytes("rgba8_bytes"),
              $"texture compression: a normal map is BC5 by default, under a third of the memory ({Format()}, {Bytes("gpu_bytes")} of {Bytes("rgba8_bytes")} bytes)");
        shot(window, "editor_23_texture_settings.png");
        panel.SetCompression(false);
        var meta = File.ReadAllText(texture.FullPath + ".noormeta");
        check(Format() == "RGBA8" && meta.Contains("\"compression\": \"none\""),
              "texture settings: Compression None is written to the .noormeta and shown as RGBA8");
        await Task.Delay(1200);  // the viewport reloads it (the end of the test checks Vulkan validation)
        panel.SetCompression(true);
        check(Format() == "BC5" && File.ReadAllText(texture.FullPath + ".noormeta").Contains("\"compression\": \"auto\""),
              "texture settings: back to compressed");
        window.DockFactory.CloseDockable(texture);
        window.DockFactory.SetActiveDockable(window.ViewportDockable);
        await Task.Delay(300);
    }

    // Milestone 8B, part 5: a sprite sheet made from a texture, sliced by a grid, an animation previewed, a sprite placed.
    private static async Task SpriteChecks(MainWindow window, Action<bool, string> check, Action<Window, string> shot)
    {
        var doc = window.Doc;
        var dir = Path.Combine(doc.ProjectRoot, "Assets", "Sprites");
        Directory.CreateDirectory(dir);
        File.Copy(Path.Combine(Program.Options.TestData!, "render_scenes", "Assets", "Sprites", "walker.png"), Path.Combine(dir, "walker.png"), overwrite: true);
        doc.RefreshAssets(rescan: true);
        // The texture panel's "Make sprite sheet" does exactly this create.
        var made = doc.Sprite(new JsonObject { ["op"] = "create", ["texture"] = "Assets/Sprites/walker.png" });
        var path = made?["path"]?.GetValue<string>();
        var sprite = path == null ? null : window.OpenSprite(path);
        await Task.Delay(400);
        var panel = window.GetVisualDescendants().OfType<SpritePanel>().FirstOrDefault(p => p.Sprite == sprite);
        check(sprite != null && panel != null && window.FindDock("Documents")?.ActiveDockable == sprite,
              "sprites: a sheet made from a texture opens in its own tab");
        if (sprite == null || panel == null)
        {
            if (sprite != null) window.DockFactory.CloseDockable(sprite);  // leave the viewport showing for the checks after
            window.DockFactory.SetActiveDockable(window.ViewportDockable);
            await Task.Delay(300);
            return;
        }
        // Sliced 4 x 2: eight frames of 32 x 32, exactly on the grid.
        panel.SetGrid(4, 2);
        var frames = panel.Last?["frames"] as JsonArray;
        var exact = frames?.Count == 8 && Enumerable.Range(0, 8).All(i =>
            frames[i] is JsonArray f && f[0]!.GetValue<int>() == i % 4 * 32 && f[1]!.GetValue<int>() == i / 4 * 32 && f[2]!.GetValue<int>() == 32 && f[3]!.GetValue<int>() == 32);
        check(exact, $"sprites: slicing 4 x 2 gives eight 32 x 32 frames exactly on the grid ({frames?.Count} frames)");
        panel.SetPixelsPerMetre(16);
        check(panel.Last?["frame_size_m"]?[1]?.GetValue<double>() is > 1.99 and < 2.01, "sprites: at 16 pixels a metre a frame is 2 m tall");
        // An animation over the walk frames: the preview steps through them.
        panel.AddAnimation("walk", 0, 3, 8);
        var seen = new HashSet<int>();
        for (var t = 0; t < 30 && seen.Count < 4; t++)
        {
            await Task.Delay(50);
            if (panel.PreviewFrame >= 0) seen.Add(panel.PreviewFrame);
        }
        check(panel.PreviewAnimation == "walk" && seen.SetEquals(new[] { 0, 1, 2, 3 }),
              $"sprites: the preview plays the walk, frames 0 to 3 ({string.Join(", ", seen.OrderBy(f => f))})");
        var file = JsonNode.Parse(File.ReadAllText(Path.Combine(doc.ProjectRoot, path!)));
        check(file?["grid"]?["columns"]?.GetValue<int>() == 4 && file?["animations"]?["walk"]?["fps"]?.GetValue<double>() == 8,
              "sprites: the grid and the animation are saved in the .noorsprite");
        // Directions (8B part 6): with 4 views across its 4 columns, turning the preview one step moves every walk frame
        // one column along; the sheet slices without complaint, and the file says so.
        panel.SetDirections(4);
        panel.PreviewDirection = 1;
        var turned = new HashSet<int>();
        for (var t = 0; t < 30 && turned.Count < 4; t++)
        {
            await Task.Delay(50);
            if (panel.PreviewFrame >= 0) turned.Add(panel.PreviewFrame);
        }
        check(panel.Last?["error"] == null && turned.SetEquals(new[] { 1, 2, 3, 4 }) &&
              JsonNode.Parse(File.ReadAllText(Path.Combine(doc.ProjectRoot, path!)))?["directions"]?.GetValue<int>() == 4,
              $"sprites: a direction picked in the preview moves the walk along the columns ({string.Join(", ", turned.OrderBy(f => f))})");
        panel.SetDirections(1);
        shot(window, "editor_32_sprite_sheet.png");
        // Place it: a new entity with a sprite playing the walk, drawn in the viewport.
        doc.Select(null);
        var count = doc.Scene.Entities.Count;
        var placed = panel.PlaceInScene([14, 0, 14]);  // on open ground, clear of the building
        var id = placed?["entity"]?.GetValue<string>();
        var component = doc.Scene.Entities.FirstOrDefault(e => e.Id == id)?.Components["sprite"];
        check(doc.Scene.Entities.Count == count + 1 && component?["animation"]?.GetValue<string>() == "walk" &&
              component?["sheet"]?.GetValue<string>() == made?["guid"]?.GetValue<string>(),
              "sprites: Place in scene makes a sprite entity playing the walk");
        window.DockFactory.CloseDockable(sprite);
        window.DockFactory.SetActiveDockable(window.ViewportDockable);
        long Sprites() => VulkanViewport.Active?.RenderStats() is { } text && JsonNode.Parse(text)?["sprites"] is JsonValue v && v.TryGetValue<long>(out var n) ? n : -1;
        var drawn = await WaitFor(() => (long?)Sprites(), n => n >= 1, 8);
        check(drawn >= 1, $"sprites: the viewport draws the placed sprite ({drawn} sprites)");
        if (id != null) window.ViewportView?.Viewport.Frame(doc.Subtree(id));
        await Task.Delay(800);
        shot(window, "editor_33_sprite_in_viewport.png");
        doc.Undo();
        check(doc.Scene.Entities.Count == count, "sprites: one undo removes the sprite entity");
        await Task.Delay(300);
    }

    // Milestone 6, part 2: CSG brushes in the editor (create, cut, reorder, edit, undo, save/reopen).
    private static async Task BrushChecks(MainWindow window, Action<bool, string> check, Action<Window, string> shot)
    {
        var doc = window.Doc;
        double Volume() => doc.CsgInfo().OfType<JsonObject>().Sum(m => m["volume"]!.GetValue<double>());
        long Triangles() => doc.CsgInfo().OfType<JsonObject>().Sum(m => m["triangles"]!.GetValue<long>());
        doc.Select(null);
        var wall = window.CreateBrush("box");
        var model = wall == null ? null : doc.Scene.Entities.FirstOrDefault(e => e.Id == wall)?.Parent;
        check(wall != null && model != null && doc.Scene.Entities.First(e => e.Id == model).Components["csg_model"] != null &&
              doc.CsgInfo().Count == 1 && Triangles() == 12 && Math.Abs(Volume() - 3.6) < 1e-4,
              $"brushes: Create ▸ Brush ▸ Box makes a CSG model with a 4 x 3 x 0.3 m wall ({Triangles()} triangles, {Volume():0.###} m³)");
        if (wall == null || model == null) return;

        // An arch (a doorway) goes into the selected brush's model and cuts through the wall.
        var door = window.CreateBrush("arch");
        var doorEntity = doc.Scene.Entities.FirstOrDefault(e => e.Id == door);
        // 1 x 1.7 m plus a half circle of radius 0.5 (an 8-segment half of a 16-gon), 0.3 m deep.
        var doorway = 0.3 * (1.7 + 0.5 * 16 * 0.25 * Math.Sin(2 * Math.PI / 16) / 2);
        check(doorEntity?.Parent == model && Math.Abs(Volume() - (3.6 - doorway)) < 1e-3 && Triangles() > 12,
              $"brushes: an arch brush cuts a doorway through the wall ({Volume():0.####} m³, expected {3.6 - doorway:0.####})");

        // The order of brushes is the order of operations: the cut moved before the wall cuts nothing.
        doc.Select(door);
        window.MoveSelected(-1);
        check(doc.Scene.Entities.Where(e => e.Parent == model).Select(e => e.Id).First() == door && Math.Abs(Volume() - 3.6) < 1e-4,
              "brushes: moving the doorway before the wall (Alt+Up) means it cuts nothing");
        doc.Undo();
        check(Math.Abs(Volume() - (3.6 - doorway)) < 1e-3, "brushes: undo puts the doorway back after the wall");

        // Edit the wall's size in place: the model is rebuilt.
        var brush = (JsonObject)doc.Scene.Entities.First(e => e.Id == wall).Components["brush"]!.DeepClone();
        brush["size"] = new JsonArray(6.0, 3.0, 0.3);
        brush["material"] = doc.Assets.FirstOrDefault(a => a.Path == "Assets/Textures/tiles.noormat")?.Guid;
        doc.Execute(new JsonObject { ["op"] = "set_component", ["entity"] = wall, ["type"] = "brush", ["value"] = brush });
        check(Math.Abs(Volume() - (6 * 3 * 0.3 - doorway)) < 1e-3, $"brushes: a wider wall rebuilds the model ({Volume():0.###} m³)");
        window.ViewportView?.Viewport.Frame(doc.Subtree(model));
        doc.Select(door);
        await Task.Delay(800);
        shot(window, "editor_24_brushes.png");

        // Saved and opened again: the same model.
        var before = Volume();
        var path = doc.Scene.Path;
        check(doc.Save() && doc.OpenScene(path) && Math.Abs(Volume() - before) < 1e-6 && doc.Scene.Entities.Count(e => e.Parent == model) == 2,
              "brushes: saved and opened again, the model is the same");
        // Saving cooked it for games (Milestone 6, part 4), and it exports as a Noor Interchange asset.
        var cooked = Path.Combine(doc.ProjectRoot, ".noor", "cooked", "csg", model + ".ncsg");
        check(File.Exists(cooked) && doc.Scene.Entities.First(e => e.Id == model).Components["collider"]?["shape"]?.GetValue<string>() == "mesh",
              "brushes: saving cooks the model for games; a new model collides as a mesh");
        var glb = Path.Combine(doc.ProjectRoot, "Exports", "house.glb");
        var exported = doc.ExportCsg(model, glb);
        var manifest = File.Exists(Path.ChangeExtension(glb, ".noor.json"))
            ? JsonNode.Parse(File.ReadAllText(Path.ChangeExtension(glb, ".noor.json"))) : null;
        check(exported != null && File.Exists(glb) && manifest?["id"]?.GetValue<string>() == model && manifest["collision"]?.GetValue<string>() == "mesh",
              "brushes: Export GLB writes a Noor Interchange GLB and manifest (id = the model's, with collision)");
        doc.Execute(new JsonObject { ["op"] = "remove", ["entity"] = model });
        check(doc.CsgInfo().Count == 0, "brushes: deleting the model removes it");
        await Task.Delay(300);
    }

    // Milestone 6, part 5: Create ▸ Terrain draws a tile of generated hills; a new seed reshapes it.
    private static async Task TerrainChecks(MainWindow window, Action<bool, string> check, Action<Window, string> shot)
    {
        var doc = window.Doc;
        long Triangles()
        {
            var json = window.ViewportView?.Viewport.RenderStats();
            return json != null && JsonNode.Parse(json) is JsonObject s && s["triangles"] is JsonNode t ? t.GetValue<long>() : -1;
        }
        await Task.Delay(500);
        var before = Triangles();
        doc.Select(null);
        var id = window.CreateEntity(EntityTemplates.All.First(t => t.Name == EntityTemplates.TerrainTemplateName));
        check(id != null, "terrain: Create ▸ Terrain makes an entity");
        if (id == null) return;
        window.ViewportView?.Viewport.Frame(doc.Subtree(id));
        await Task.Delay(1200);
        var with = Triangles();
        // One tile: 256 x 256 cells, two triangles each (the shadow passes may draw it again).
        check(with - before >= 2 * 256 * 256, $"terrain: Create ▸ Terrain draws a 256 m tile ({before} → {with} triangles)");
        var terrain = (JsonObject)doc.Scene.Entities.First(e => e.Id == id).Components["terrain"]!.DeepClone();
        terrain["seed"] = 42;
        terrain["material"] = doc.Assets.FirstOrDefault(a => a.Path == "Assets/Textures/tiles.noormat")?.Guid;
        doc.Execute(new JsonObject { ["op"] = "set_component", ["entity"] = id, ["type"] = "terrain", ["value"] = terrain });
        await Task.Delay(1200);
        check(Triangles() == with, "terrain: a new seed rebuilds it (same size, new hills)");
        shot(window, "editor_26_terrain.png");
        doc.Execute(new JsonObject { ["op"] = "remove", ["entity"] = id });
        await Task.Delay(600);
        // Triangles count what the camera sees (Milestone 7, part 6: culling), and the camera moved to
        // frame the terrain, so compare with the framed view: the tile's triangles are gone.
        check(Triangles() <= with - 2 * 256 * 256, $"terrain: deleting it frees its tiles ({with} → {Triangles()} triangles)");
    }

    // Milestone 6, part 6: sculpt a hill with the brush (a test pointer, never the real mouse), undo, save.
    private static async Task SculptChecks(MainWindow window, Action<bool, string> check, Action<Window, string> shot)
    {
        var doc = window.Doc;
        var vp = VulkanViewport.Active;
        doc.Select(null);
        var id = window.CreateEntity(EntityTemplates.All.First(t => t.Name == EntityTemplates.TerrainTemplateName));
        if (id == null || vp == null)
        {
            check(false, "sculpt: a terrain to work on");
            return;
        }
        var flat = (JsonObject)doc.Scene.Entities.First(e => e.Id == id).Components["terrain"]!.DeepClone();
        flat["base"] = "flat";
        flat["cell_size"] = 0.25;  // 64 m across: a hill shows up in the picture
        flat["material"] = doc.Assets.FirstOrDefault(a => a.Path == "Assets/Textures/tiles.noormat")?.Guid;
        doc.Execute(new JsonObject { ["op"] = "set_component", ["entity"] = id, ["type"] = "terrain", ["value"] = flat });
        doc.Select(id);
        doc.SetBrush(new EditorDocument.SculptBrush("raise", 10, 6, 0.6));
        doc.SetSculpting(true);
        _testViewport = vp.EngineHandle;
        vp.Frame([id]);
        await Task.Delay(1200);

        // Hold the left button on the terrain, off to one side, for about a second.
        var at = vp.WorldToScreen(new Vec3(12, 0, 8));
        if (at is { } p)
        {
            await Pointer(p.X, p.Y, 0);
            await Task.Delay(200);
            await Pointer(p.X, p.Y, 1);
            await Task.Delay(1000);
            await Pointer(p.X, p.Y, 0);
            await Task.Delay(300);
            window.ViewportView?.DrainEvents();
        }
        var sculptId = doc.Scene.Entities.First(e => e.Id == id).Components["terrain"]?["sculpt"]?.GetValue<string>();
        check(at != null && doc.Scene.UndoLabel == "Sculpt: Raise" && sculptId != null,
              $"sculpt: a drag with the Raise brush is one undo step ({doc.Scene.UndoLabel}) and gives the terrain a sculpt layer");

        // The circle sits on the new hill (the pointer hovers beside it) for the screenshot.
        if (at is { } q) await Pointer(q.X + 40, q.Y + 10, 0);
        await Task.Delay(500);
        shot(window, "editor_27_sculpt.png");

        // Saved: the layer file holds the hill (metres: about a second at 6 m/s; how long exactly depends on frame timing).
        doc.Save();
        var path = sculptId == null ? null : doc.Assets.FirstOrDefault(a => a.Guid == sculptId)?.Path;
        double highest = 0;
        if (path != null && File.Exists(Path.Combine(doc.ProjectRoot, path)))
        {
            var bytes = File.ReadAllBytes(Path.Combine(doc.ProjectRoot, path));
            for (var i = 24; i + 4 <= bytes.Length; i += 4) highest = Math.Max(highest, BitConverter.ToSingle(bytes, i));
        }
        check(highest is > 2 and < 12, $"sculpt: Save writes the layer ({path}, highest point {highest:0.##} m)");
        doc.Undo();
        check(doc.Scene.Entities.First(e => e.Id == id).Components["terrain"]?["sculpt"] == null, "sculpt: undo takes the stroke back");

        doc.SetSculpting(false);
        Native.noor_viewport_test_pointer(_testViewport, 0, 0, 0, 0);  // give the mouse back
        doc.Execute(new JsonObject { ["op"] = "remove", ["entity"] = id });
        await Task.Delay(300);
    }

    // Milestone 7, part 5: a tree from NoorMD (NoorForm), in the Noor Interchange layout: one asset with
    // four levels, its manifest's story in the Content Browser's details, placed with its trunk capsule.
    private static async Task OakChecks(MainWindow window, Action<bool, string> check, Action<Window, string> shot)
    {
        var doc = window.Doc;
        var trees = Path.Combine(doc.ProjectRoot, "Assets", "Trees");
        Directory.CreateDirectory(trees);
        foreach (var f in Directory.GetFiles(Path.Combine(Program.Options.TestData!, "noorform_oak")))
            File.Copy(f, Path.Combine(trees, Path.GetFileName(f)), overwrite: true);
        doc.RefreshAssets(true);
        var oak = doc.Assets.FirstOrDefault(a => a.Path == "Assets/Trees/Oak_01.glb");
        check(oak != null && oak.Guid == "719beb3e-327d-56ed-8d54-be8882f405fd" && !doc.Assets.Any(a => a.Path.Contains(".lod")),
              "NoorMD oak: one asset (its .lod files belong to it), with the id from its manifest");
        var browser = window.ContentBrowser;
        if (oak == null || browser == null) return;

        window.DockFactory.SetActiveDockable(window.FindDockable("Content")!);
        long Drawn() => VulkanViewport.Active?.RenderStats() is { } st && JsonNode.Parse(st) is JsonObject o ? o["triangles"]?.GetValue<long>() ?? 0 : 0;
        browser.Select(oak);
        var levels = browser.Details?["interchange"]?["levels"] as JsonArray;
        var text = browser.DetailsText;
        check(levels?.Count == 4 && levels.All(l => l?["exists"]?.GetValue<bool>() == true && (l["issues"] as JsonArray)?.Count == 0) &&
              text.Contains("seed 1") && text.Contains("NoorForm") && text.Contains("15,268 triangles") && text.Contains("up to 29 m") && text.Contains("billboard") && text.Contains("Oak, variant A of 9"),
              "NoorMD oak: the Content Browser shows its four levels, distances, recipe, seed and family");

        window.AddAsset(oak);
        var root = doc.SelectedId;
        var capsule = doc.Scene.Entities.FirstOrDefault(e => e.Name == "Trunk-colonly");
        var collider = capsule?.Components["collider"];
        check(root != null && capsule != null && collider?["shape"]?.GetValue<string>() == "capsule" &&
              Math.Abs((collider["radius"]?.GetValue<double>() ?? 0) - 0.682) < 1e-3 && capsule.Components["mesh"] == null,
              "NoorMD oak: placed, its trunk capsule is a collider (radius 0.682 m) and nothing drawn");
        // Out of the building at the origin, onto the floor PhysicsChecks made (around x = -25).
        if (root != null && doc.Scene.Entities.FirstOrDefault(e => e.Id == root) is { } placed)
            doc.SetTransform(root, placed.Transform with { Position = new Vec3(-25, 0, -6) }, "Move oak");
        var bottom = window.FindDock("Bottom");
        var height = bottom?.Proportion ?? 0.28;
        if (bottom != null) bottom.Proportion = 0.5;  // room for the details in the screenshot
        await Task.Delay(400);
        if (root != null) window.ViewportView?.Viewport.Frame(doc.Subtree(root));
        // The viewport loads the model (and compresses its textures) on its own thread: wait until it
        // draws it. Framed close up it's level 0 (15,268 triangles); the view counts only what the
        // camera sees (culling), so at least that many.
        var drawn = await WaitFor(() => (long?)Drawn(), t => t >= 15268, 60);
        check(drawn >= 15268, $"NoorMD oak: the viewport draws it ({drawn} triangles in view; its level 0 has 15,268)");
        await Task.Delay(600);
        shot(window, "editor_30_noorform_oak.png");
        if (bottom != null) bottom.Proportion = height;
        // Levels of detail in the viewport (Milestone 7, part 6): a model counts as further away the
        // smaller it is, so the same oak at 2% of its size is drawn from its billboard level (8 triangles).
        var full = Drawn();
        var lowered = false;
        if (root != null && doc.Scene.Entities.FirstOrDefault(e => e.Id == root) is { } oakNow)
        {
            doc.SetTransform(root, oakNow.Transform with { Scale = new Vec3(0.02, 0.02, 0.02) }, "Shrink oak");
            var small = await WaitFor(() => (long?)Drawn(), t => t <= full - 15000, 20);
            check(small <= full - 15000, $"NoorMD oak: the viewport switches it to a lower level of detail ({full} → {small} triangles in view)");
            lowered = true;
        }
        if (lowered) doc.Undo();
        doc.Undo();
        doc.Undo();
        window.DockFactory.SetActiveDockable(window.ViewportDockable);
        await Task.Delay(300);
    }

    // Milestone 7, part 7: a forest scattered by a graph on a terrain, trees removed and planted by hand
    // (as the Plant and Remove brushes do), the graph editor's preview of the instance set.
    private static async Task ScatterChecks(MainWindow window, Action<bool, string> check, Action<Window, string> shot)
    {
        var doc = window.Doc;
        var oak = doc.Assets.FirstOrDefault(a => a.Path == "Assets/Trees/Oak_01.glb");
        if (oak == null)
        {
            check(false, "scatter: the oak from the NoorMD checks is in the project");
            return;
        }
        var file = Path.Combine(doc.ProjectRoot, "Assets", "Graphs", "forest.noorgraph");
        Directory.CreateDirectory(Path.GetDirectoryName(file)!);
        File.WriteAllText(file, $$"""
            { "noor_graph": 1, "seed": 4,
              "nodes": [
                {"id": "clear", "type": "exclude", "params": {"tiles_x": 1, "tiles_z": 1, "cell_size": 1, "circles": "0,0,20", "soft": 2}, "position": [40, 40]},
                {"id": "trees", "type": "scatter", "params": {"asset": "{{oak.Guid}}", "density": 0.4, "spacing": 14, "width": 200, "depth": 200}, "position": [320, 40]}
              ],
              "links": [{"from": "clear.mask", "to": "trees.mask"}],
              "outputs": [{"name": "trees", "from": "trees.instances"}] }
            """);
        doc.RefreshAssets(true);
        var graph = doc.Assets.FirstOrDefault(a => a.Path == "Assets/Graphs/forest.noorgraph");
        var terrain = window.CreateEntity(EntityTemplates.All.First(t => t.Name == EntityTemplates.TerrainTemplateName));
        if (graph == null || terrain == null)
        {
            check(false, "scatter: a graph and a terrain to stand on");
            return;
        }
        var scatter = EntityTemplates.Scatter();
        scatter["graph"] = graph.Guid;
        scatter["ground"] = terrain;
        var forest = doc.Create("Forest", new JsonObject { ["scatter"] = scatter }, new Vec3(0, 0, 0));
        if (forest == null)
        {
            check(false, "scatter: Create ▸ Scatter makes a scatter entity");
            return;
        }
        doc.Select(forest);
        window.ViewportView?.Viewport.Frame(doc.Subtree(terrain));
        long Drawn() => VulkanViewport.Active?.RenderStats() is { } st && JsonNode.Parse(st) is JsonObject o ? o["instances"]?.GetValue<long>() ?? 0 : 0;
        var drawn = await WaitFor(() => (long?)Drawn(), n => n > 60, 60);  // tree parts (two per oak) and the terrain's tiles
        check(drawn > 60, $"scatter: the graph's forest stands on the terrain in the viewport ({drawn} objects drawn)");

        // Remove: a brush stroke in the clearing's edge removes nothing; one at a tree removes it.
        var trees = Path.Combine(doc.ProjectRoot, ".noor", "cooked", "graphs", graph.Guid, "trees.instances.json");
        var rows = File.Exists(trees) ? (JsonNode.Parse(File.ReadAllText(trees))?["instances"] as JsonArray) : null;
        check(rows is { Count: > 50 }, $"scatter: the graph was cooked into an instance set ({rows?.Count ?? 0} trees)");
        if (rows is not { Count: > 0 }) return;
        var row = (JsonArray)rows[rows.Count / 2]!;
        var at = new JsonArray(row[2]!.GetValue<double>(), 0.0, row[4]!.GetValue<double>());
        doc.ScatterEdit(new JsonObject { ["scatter"] = forest, ["phase"] = "begin", ["tool"] = "remove", ["radius"] = 3 }, false);
        var removed = doc.ScatterEdit(new JsonObject { ["scatter"] = forest, ["phase"] = "end", ["dabs"] = new JsonArray(new JsonObject { ["point"] = at }) }, true);
        doc.ScatterEdit(new JsonObject { ["scatter"] = forest, ["phase"] = "begin", ["tool"] = "plant", ["radius"] = 5 }, false);
        var planted = doc.ScatterEdit(new JsonObject { ["scatter"] = forest, ["phase"] = "end",
                                                       ["dabs"] = new JsonArray(new JsonObject { ["point"] = new JsonArray(0.0, 0.0, 0.0) }) }, true);
        var comp = doc.Scene.Entities.First(e => e.Id == forest).Components["scatter"];
        check(removed?["removed"]?.GetValue<int>() == 1 && planted?["planted"]?.GetValue<int>() == 1 &&
              (comp?["removed"] as JsonArray)?.Count == 1 && (comp?["added"] as JsonArray)?.Count == 1,
              "scatter: a Remove stroke takes out one tree, a Plant stroke adds one in the clearing (both kept in the component)");
        await Task.Delay(1200);
        shot(window, "editor_31_scatter.png");
        doc.Undo();
        comp = doc.Scene.Entities.First(e => e.Id == forest).Components["scatter"];
        check((comp?["added"] as JsonArray)?.Count == 0 && (comp?["removed"] as JsonArray)?.Count == 1, "scatter: undo takes back the planting stroke only");

        // The graph editor previews the instance set (dots from above) and counts it.
        var tab = window.OpenGraph("Assets/Graphs/forest.noorgraph");
        if (tab != null)
        {
            await Task.Delay(300);
            tab.Run();
            await tab.WaitForRun();
            check(tab.Preview("trees", "instances", 96) != null && tab.Result("trees")?["outputs"]?["instances"]?["count"]?.GetValue<int>() == rows.Count,
                  "scatter: the graph editor shows the instance set's preview and count");
            window.DockFactory.CloseDockable(tab);
            window.DockFactory.SetActiveDockable(window.ViewportDockable);
        }
        doc.Undo();  // the removal
        doc.Undo();  // the forest
        doc.Undo();  // the terrain
        await Task.Delay(300);
    }

    // Milestone 7, part 4: the graph editor: open a graph, run it, add and link a node, change a setting
    // (only what depends on it reruns), see an issue on a node, undo.
    private static async Task GraphChecks(MainWindow window, Action<bool, string> check, Action<Window, string> shot)
    {
        var doc = window.Doc;
        var file = Path.Combine(doc.ProjectRoot, "Assets", "Graphs", "smoke.noorgraph");
        Directory.CreateDirectory(Path.GetDirectoryName(file)!);
        File.WriteAllText(file, """
            { "noor_graph": 1, "seed": 7,
              "nodes": [
                {"id": "hills", "type": "noise", "params": {"height": 30, "scale": 120, "octaves": 4}, "position": [40, 40]},
                {"id": "valleys", "type": "noise", "params": {"height": 1, "scale": 300, "octaves": 2}, "position": [40, 260]},
                {"id": "mask", "type": "remap", "params": {"from_min": 0.3, "from_max": 0.7, "unit": "ratio"}, "position": [300, 260]},
                {"id": "flat", "type": "noise", "params": {"height": 2, "scale": 40, "octaves": 1}, "position": [40, 480]},
                {"id": "blend", "type": "combine", "params": {"op": "lerp"}, "position": [560, 120]},
                {"id": "limit", "type": "clamp", "params": {"min": 0, "max": 25}, "position": [820, 120]}
              ],
              "links": [
                {"from": "valleys.height", "to": "mask.in"}, {"from": "hills.height", "to": "blend.a"},
                {"from": "flat.height", "to": "blend.b"}, {"from": "mask.out", "to": "blend.t"}, {"from": "blend.out", "to": "limit.in"}
              ],
              "outputs": [{"name": "terrain", "from": "limit.out"}] }
            """);
        doc.RefreshAssets(true);
        var graph = window.OpenGraph("Assets/Graphs/smoke.noorgraph");
        if (graph == null)
        {
            check(false, "graph editor: the graph opens in a tab");
            return;
        }
        await Task.Delay(400);
        graph.Run();
        var ran = await graph.WaitForRun();
        var nodes = graph.LastRun?["nodes"] as JsonObject;
        check(ran && graph.LastRun?["ok"]?.GetValue<bool>() == true && nodes?.Count == 6 && graph.Preview("hills", "height", 96) != null,
              $"graph editor: Run evaluates all six nodes and shows previews ({graph.LastRun?["ran"]} ran)");

        // Add a terrace after the clamp, link it, make it an output.
        var terrace = graph.AddNode("terrace", 1080, 120);
        graph.Connect("limit", "out", terrace, "in");
        graph.SetOutput(terrace, "out", "stepped");
        await graph.WaitForRun();
        var saved = File.ReadAllText(file);
        check(graph.Result(terrace)?["state"]?.GetValue<string>() == "done" && graph.LastRun?["written"]?.GetValue<bool>() == true &&
              saved.Contains("\"terrace\"") && saved.Contains("\"stepped\""),
              "graph editor: a new node linked in runs, becomes an output, and the file is saved");

        // A setting changed upstream: only that node and what depends on it run (the rest from the cache).
        graph.SetParam("hills", "height", JsonValue.Create(45.0));
        await graph.WaitForRun();
        check(graph.LastRun?["ran"]?.GetValue<int>() == 4 && graph.LastRun?["cached"]?.GetValue<int>() == 3,
              $"graph editor: changing hills reruns hills, blend, limit and terrace only ({graph.LastRun?["ran"]} ran, {graph.LastRun?["cached"]} cached)");

        // An unconnected node shows its issue; undo takes it away, and again the setting.
        var lonely = graph.AddNode("clamp", 600, 480);
        await graph.WaitForRun();
        check(graph.IssuesFor(lonely).Any(i => i.Contains("isn't connected")), "graph editor: an unconnected node shows its issue on the node");
        graph.Select("blend");
        await Task.Delay(500);
        shot(window, "editor_29_graph.png");
        graph.Undo();
        graph.Undo();
        await graph.WaitForRun();
        var after = File.ReadAllText(file);
        check(graph.Node(lonely) == null && !after.Contains("45"), "graph editor: undo removes the node, then the setting change (saved too)");
        window.DockFactory.CloseDockable(graph);
        window.DockFactory.SetActiveDockable(window.ViewportDockable);
        await Task.Delay(300);
    }

    // Milestone 6, part 7: the Paint tool: a stroke on a terrain adds a material layer; a click gives a brush face the material.
    private static async Task PaintChecks(MainWindow window, Action<bool, string> check, Action<Window, string> shot)
    {
        var doc = window.Doc;
        var vp = VulkanViewport.Active;
        var tiles = doc.Assets.FirstOrDefault(a => a.Path == "Assets/Textures/tiles.noormat");
        doc.Select(null);
        var id = window.CreateEntity(EntityTemplates.All.First(t => t.Name == EntityTemplates.TerrainTemplateName));
        if (id == null || vp == null || tiles == null)
        {
            check(false, "paint: a terrain and a material to work with");
            return;
        }
        var flat = (JsonObject)doc.Scene.Entities.First(e => e.Id == id).Components["terrain"]!.DeepClone();
        flat["base"] = "flat";
        flat["cell_size"] = 0.25;
        doc.Execute(new JsonObject { ["op"] = "set_component", ["entity"] = id, ["type"] = "terrain", ["value"] = flat });
        doc.Select(id);
        doc.SetBrush(new EditorDocument.SculptBrush("raise", 8, 3, 0.4));
        doc.SetPaintMaterial(tiles.Guid);
        doc.SetPainting(true);
        _testViewport = vp.EngineHandle;
        vp.Frame([id]);
        await Task.Delay(1200);
        // In front of the building (it stands in the middle of the terrain and would hide the stripe).
        if (vp.WorldToScreen(new Vec3(-14, 0, 22)) is { } p)
        {
            await Pointer(p.X, p.Y, 0);
            await Task.Delay(150);
            await Pointer(p.X, p.Y, 1);
            await Pointer(p.X + 120, p.Y, 1);  // a short drag: a stripe of tiles
            await Task.Delay(400);
            await Pointer(p.X + 240, p.Y + 20, 1);
            await Task.Delay(400);
            await Pointer(p.X + 240, p.Y + 20, 0);
            await Task.Delay(300);
            window.ViewportView?.DrainEvents();
        }
        var terrain = doc.Scene.Entities.First(e => e.Id == id).Components["terrain"];
        check(doc.Scene.UndoLabel == "Paint: tiles" && (terrain?["layers"] as JsonArray)?.Count == 2 && terrain?["paint"] != null,
              $"paint: a stroke with the tiles material is one undo step ({doc.Scene.UndoLabel}) and adds it as layer 1");
        // Saved: the paint file has tiles (layer 1) somewhere: the stroke really painted.
        doc.Save();
        var paintPath = terrain?["paint"]?.GetValue<string>() is { } paintId ? doc.Assets.FirstOrDefault(a => a.Guid == paintId)?.Path : null;
        var painted = 0;
        if (paintPath != null && File.Exists(Path.Combine(doc.ProjectRoot, paintPath)))
        {
            var bytes = File.ReadAllBytes(Path.Combine(doc.ProjectRoot, paintPath));
            for (var i = 24 + 8; i + 4 <= bytes.Length; i += 4) if (bytes[i + 1] > 128) painted++;  // byte 1 = layer 1's weight
        }
        check(painted > 100, $"paint: the saved paint layer has the stripe ({painted} samples mostly tiles)");
        if (vp.WorldToScreen(new Vec3(14, 0, 18)) is { } hover) await Pointer(hover.X, hover.Y, 0);  // the brush circle, on the terrain
        await Task.Delay(400);
        shot(window, "editor_28_paint.png");

        // The same tool on a CSG brush: a click on its front face gives that face the material.
        doc.SetPainting(false);
        doc.Select(null);
        var wall = window.CreateBrush("box");
        var model = wall == null ? null : doc.Scene.Entities.First(e => e.Id == wall).Parent;
        if (wall != null && model != null)
        {
            doc.Select(wall);
            doc.SetPainting(true);
            vp.Frame(doc.Subtree(model));
            await Task.Delay(800);
            if (vp.WorldToScreen(new Vec3(0.6, 1.2, 0.15)) is { } at)
            {
                await Click(IntPtr.Zero, at.X, at.Y);
                window.ViewportView?.DrainEvents();
            }
            var faces = doc.Scene.Entities.First(e => e.Id == wall).Components["brush"]?["faces"] as JsonArray;
            check(faces != null && faces.Count > 4 && faces[4]?["material"]?.GetValue<string>() == tiles.Guid && doc.SelectedId == wall,
                  "paint: a click on a brush's front face gives that face the material (the selection stays)");
            doc.Execute(new JsonObject { ["op"] = "remove", ["entity"] = model });
        }
        else check(false, "paint: a wall to paint");

        doc.SetPainting(false);
        Native.noor_viewport_test_pointer(_testViewport, 0, 0, 0, 0);  // give the mouse back
        doc.Execute(new JsonObject { ["op"] = "remove", ["entity"] = id });
        await Task.Delay(300);
    }

    // Milestone 6, part 3: click a face, give it a material, cut a window after texturing.
    private static async Task FaceChecks(MainWindow window, Action<bool, string> check, Action<Window, string> shot)
    {
        var doc = window.Doc;
        doc.Select(null);
        var wall = window.CreateBrush("box");
        var model = wall == null ? null : doc.Scene.Entities.First(e => e.Id == wall).Parent;
        var vp = VulkanViewport.Active;
        if (wall == null || model == null || vp == null)
        {
            check(false, "faces: a wall to work on");
            return;
        }
        _testViewport = vp.EngineHandle;
        vp.Frame(doc.Subtree(model));
        await Task.Delay(700);
        JsonObject? Face(int face, string brush) => doc.CsgInfo().OfType<JsonObject>().SelectMany(m => (m["faces"] as JsonArray)!.OfType<JsonObject>())
            .FirstOrDefault(f => f["brush"]!.GetValue<string>() == brush && f["face"]!.GetValue<int>() == face);

        // Two clicks on the wall's front: the model, then the brush and the face under the pointer.
        if (vp.WorldToScreen(new Vec3(0.6, 1.2, 0.15)) is { } at)
        {
            await Click(IntPtr.Zero, at.X, at.Y);
            window.ViewportView?.DrainEvents();
            await Click(IntPtr.Zero, at.X, at.Y);
            window.ViewportView?.DrainEvents();
        }
        check(doc.SelectedId == wall && doc.SelectedFace == 4,
              $"faces: clicking the wall's front twice picks the brush and its front face ({doc.SelectedFace?.ToString() ?? "none"})");

        // That face gets its own material; the others keep the brush's (none).
        var tiles = doc.Assets.FirstOrDefault(a => a.Path == "Assets/Textures/tiles.noormat");
        doc.SelectFace(4);
        await Task.Delay(100);
        var set = tiles != null && window.Inspector?.Components.SetFace(4, new JsonObject { ["material"] = tiles.Guid }) == true;
        var front = Face(4, wall);
        check(set && front?["material"]?.GetValue<string>() == tiles!.Path && Face(2, wall)?["material"] == null,
              "faces: the front face takes the tiles material, the top keeps the brush's");
        var frontTriangles = front?["triangles"]?.GetValue<int>() ?? 0;

        // A window cut after texturing: the front keeps its tiles, the window's own faces don't get them.
        doc.Select(wall);
        var window1 = window.CreateBrush("box");
        if (window1 != null)
        {
            var brush = (JsonObject)doc.Scene.Entities.First(e => e.Id == window1).Components["brush"]!.DeepClone();
            brush["operation"] = "subtract";
            brush["size"] = new JsonArray(1.0, 1.0, 1.0);
            doc.Execute(new JsonObject { ["op"] = "set_component", ["entity"] = window1, ["type"] = "brush", ["value"] = brush });
        }
        var cut = Face(4, wall);
        var volume = doc.CsgInfo().OfType<JsonObject>().Sum(m => m["volume"]!.GetValue<double>());
        check(window1 != null && Math.Abs(volume - (3.6 - 0.3)) < 1e-3 && cut?["material"]?.GetValue<string>() == tiles?.Path &&
              (cut?["triangles"]?.GetValue<int>() ?? 0) > frontTriangles && Face(0, window1)?["material"] == null,
              $"faces: a window cut after texturing keeps the front's tiles ({cut?["triangles"]} triangles); its own faces don't take them");

        doc.Select(wall);
        doc.SelectFace(4);
        vp.Frame(doc.Subtree(model));
        await Task.Delay(800);
        shot(window, "editor_25_faces.png");
        Native.noor_viewport_test_pointer(_testViewport, 0, 0, 0, 0);  // give the mouse back
        doc.Execute(new JsonObject { ["op"] = "remove", ["entity"] = model });
        await Task.Delay(200);
    }

    private static async Task NoorCodeChecks(MainWindow window, Dictionary<string, object> results,
                                             Action<bool, string> check, Action<Window, string> shot)
    {
        var doc = window.Doc;
        var file = Path.Combine(doc.ProjectRoot, "Scripts", "Spinner.cs");
        var good = File.ReadAllText(file);
        check(CodePanel.Highlighting("Scripts/A.cs") != null && CodePanel.Highlighting("Scripts/B.luau") != null,
              "NoorCode: Lua and C# highlighting load");
        check(ScriptLocation.Find("Scripts/Spinner.cs:7: CS1002: ; expected", doc.ProjectRoot) == new ScriptLocation("Scripts/Spinner.cs", 7) &&
              ScriptLocation.Find("InvalidOperationException: boom at Spinner.cs:12", doc.ProjectRoot) == new ScriptLocation("Scripts/Spinner.cs", 12) &&
              ScriptLocation.Find("Scripts/Nope.luau:3: x", doc.ProjectRoot) == null,
              "NoorCode: error messages name script lines (Lua and C# compile errors, C# exceptions)");

        var script = window.OpenScript("Scripts/Spinner.cs", 11);
        await Task.Delay(500);
        var panel = window.GetVisualDescendants().OfType<CodePanel>().FirstOrDefault(p => p.Script == script);
        check(script != null && panel != null && window.FindDock("Documents")?.ActiveDockable == script,
              "NoorCode: Open shows the script as a tab next to the viewport");
        if (script == null || panel == null) return;
        check(panel.Editor.TextArea.Caret.Line == 11, $"NoorCode: opens at the asked line (caret on {panel.Editor.TextArea.Caret.Line})");
        check(window.OpenScript(file) == script && window.OpenScripts.Count == 1, "NoorCode: opening it again reuses the tab");

        // Keys typed into the editor are text, not the editor's shortcuts (E is the Rotate tool).
        panel.Editor.TextArea.Focus();
        await Task.Delay(100);
        var tool = doc.Tool;
        var area = panel.Editor.TextArea;
        var sceneModified = doc.Scene.Modified;
        area.RaiseEvent(new KeyEventArgs { RoutedEvent = InputElement.KeyDownEvent, Key = Key.E, Source = area });
        area.RaiseEvent(new TextInputEventArgs { RoutedEvent = InputElement.TextInputEvent, Text = "e", Source = area });
        check(doc.Tool == tool && script.Text.Text != File.ReadAllText(file) && script.Modified && script.Title?.Contains('•') == true,
              "NoorCode: typing goes into the script (E doesn't switch to the Rotate tool) and marks the tab unsaved");
        script.Text.UndoStack.Undo();
        script.Text.Insert(0, "// edited in NoorCode\n");
        area.RaiseEvent(new KeyEventArgs { RoutedEvent = InputElement.KeyDownEvent, Key = Key.S, KeyModifiers = KeyModifiers.Control, Source = area });
        check(!script.Modified && File.ReadAllText(file).StartsWith("// edited in NoorCode", StringComparison.Ordinal) && doc.Scene.Modified == sceneModified,
              "NoorCode: Ctrl+S saves the script (not the scene)");

        // A compile error typed in NoorCode: Play saves first, the error lands on its line.
        script.Text.Text = good.Replace("Noor.Log($\"Spinner ready on {Entity.Name}\");", "Noor.Log(\"oops\")");
        await CountErrors(async () => check(!await window.PlayAsync(), "NoorCode: Play saves the script first (and the compile error stops it)"));
        await Task.Delay(300);
        check(script.Errors.Any(e => e.Line == 7 && e.Message.Contains("CS1002")) && !script.Modified,
              $"NoorCode: the compile error is marked on line 7 ({string.Join("; ", script.Errors.Select(e => $"{e.Line}: {e.Message}"))})");
        var console = window.GetVisualDescendants().OfType<ConsolePanel>().FirstOrDefault();
        var errorLine = EngineLog.Lines.LastOrDefault(l => l.Level >= 4 && l.Message.StartsWith("Scripts/Spinner.cs:7:", StringComparison.Ordinal));
        check(console == null || (errorLine != null && console.LocationOf(errorLine) == new ScriptLocation("Scripts/Spinner.cs", 7)),
              "NoorCode: the Console links the error to Spinner.cs line 7");
        window.OpenScript("Scripts/Spinner.cs", 3);
        window.OpenScript("Scripts/Spinner.cs", 7);  // what the link does
        await Task.Delay(300);
        check(panel.Editor.TextArea.Caret.Line == 7, "NoorCode: following the link puts the caret on the error line");
        shot(window, "editor_16_noorcode.png");

        // Saving clears the old markers; a change made outside reloads an unmodified tab.
        script.Text.Text = good;
        check(script.Save() && script.Errors.Count == 0, "NoorCode: saving clears the old error markers");
        await Task.Delay(1100);  // a new write time
        File.WriteAllText(file, good + "// from outside\n");
        script.PollDisk();
        check(script.Text.Text.EndsWith("// from outside\n", StringComparison.Ordinal) && !script.Modified,
              "NoorCode: a script changed outside reloads in its tab");
        File.WriteAllText(file, good);
        await Task.Delay(50);
        script.PollDisk();

        window.DockFactory.CloseDockable(script);
        await Task.Delay(200);
        check(window.OpenScripts.Count == 0 && window.FindDockable(script.Id) == null, "NoorCode: closing the tab");
        results["noorcode_errors_marked"] = script.Errors.Count;
    }

    // Milestone 4, part 6: File ▸ Build Game (noor_cli build). The built game itself is run by the
    // player.* ctest tests; here the editor's side: the build runs, starts with the open scene, and
    // its output reaches the Console.
    private static async Task BuildChecks(MainWindow window, Dictionary<string, object> results, Action<bool, string> check, string dir)
    {
        var output = Path.Combine(dir, "built_game");
        if (Directory.Exists(output)) Directory.Delete(output, true);
        var result = await window.BuildGame(output);
        check(result.Ok && File.Exists(result.Executable), $"build: File ▸ Build Game made {Path.GetFileName(result.Executable ?? "nothing")}");
        var manifest = Path.Combine(output, "game.json");
        var scene = File.Exists(manifest) ? System.Text.Json.Nodes.JsonNode.Parse(File.ReadAllText(manifest))?["start_scene"]?.GetValue<string>() : null;
        check(scene == window.Doc.Scene.Path, $"build: the game starts with the open scene ({scene})");
        check(File.Exists(Path.Combine(output, "game", "Scripts", "PlayerController.luau")) && !Directory.Exists(Path.Combine(output, "game", ".noor")),
              "build: scripts are packed, derived data isn't");
        check(EngineLog.Lines.Any(l => l.Category == "Build" && l.Message.StartsWith("Built ")), "build: noor_cli's report reaches the Console");
        var csharp = File.Exists(manifest) ? System.Text.Json.Nodes.JsonNode.Parse(File.ReadAllText(manifest))?["csharp"]?.GetValue<string>() : null;
        check(csharp == "managed/Scripts.dll" && File.Exists(Path.Combine(output, "managed", "Scripts.dll")) &&
              File.Exists(Path.Combine(output, "managed", "NoorEngine.Scripting.runtimeconfig.json")),
              "build: the project's C# scripts are compiled into the game, with the managed engine half");
        results["build"] = output;
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
