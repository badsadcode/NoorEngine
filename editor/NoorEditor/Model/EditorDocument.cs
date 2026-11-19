using System.Text.Json.Nodes;
using NoorEditor.Interop;

namespace NoorEditor.Model;

/// <summary>
/// The editor's handle on an engine document (ADR-025): an open project, at most one open scene,
/// its undo history and asset registry. The engine owns all of that. This class only caches the
/// latest JSON snapshots, tracks the selection (an editor concern) and raises change events.
/// UI thread only.
/// </summary>
public enum EditorTool { Select = 0, Move = 1, Rotate = 2, Scale = 3 }  // values match noor_gizmo_mode

public sealed class EditorDocument : IDisposable
{
    private IntPtr _handle;
    private ulong _seenRevision = ulong.MaxValue;

    public string ProjectName { get; }
    public string ProjectRoot { get; }
    public SceneSnapshot Scene { get; private set; } = SceneSnapshot.Empty;
    public IReadOnlyList<AssetInfo> Assets { get; private set; } = [];
    public IReadOnlyList<string> SceneFiles { get; private set; } = [];
    public string? SelectedId { get; private set; }

    /// <summary>Raised after any change to the scene (edit, undo, redo, open, save).</summary>
    public event Action? SceneChanged;
    public event Action? AssetsChanged;
    public event Action? SelectionChanged;
    public event Action? ToolChanged;

    /// <summary>The viewport tool (editor state, like the selection).</summary>
    public EditorTool Tool { get; private set; } = EditorTool.Move;

    public void SetTool(EditorTool tool)
    {
        SetSculpting(false);  // picking a transform tool puts the brush down
        SetPainting(false);
        SetScattering(null);
        if (tool == Tool) return;
        Tool = tool;
        ToolChanged?.Invoke();
    }

    // ---- Terrain sculpting (Milestone 6, part 6): editor state, like the tool ----

    /// <summary>A sculpt brush: tool (raise, lower, smooth, flatten), radius (m), strength, falloff (0 hard .. 1 soft).</summary>
    public sealed record SculptBrush(string Tool, double Radius, double Strength, double Falloff);
    public static readonly string[] SculptTools = ["raise", "lower", "smooth", "flatten"];

    public event Action? SculptChanged;
    /// <summary>On: a left-drag on the selected terrain sculpts it (the gizmo is hidden).</summary>
    public bool Sculpting { get; private set; }
    public SculptBrush Brush { get; private set; } = new("raise", 8, 2, 0.5);

    public void SetSculpting(bool on)
    {
        if (on == Sculpting) return;
        Sculpting = on;
        if (on) { Painting = false; Scattering = null; }
        SculptChanged?.Invoke();
    }

    // ---- Scatter editing (Milestone 7, part 7): plant and remove trees with the brush ----

    /// <summary>"plant" or "remove": left-drags on the selected scatter's ground plant or remove trees. Null: off.</summary>
    public string? Scattering { get; private set; }
    /// <summary>The model to plant (a model asset id), or null: the scatter's own.</summary>
    public string? ScatterAsset { get; private set; }

    public void SetScattering(string? tool)
    {
        if (tool == Scattering) return;
        Scattering = tool;
        if (tool != null) { Sculpting = false; Painting = false; }
        SculptChanged?.Invoke();
    }

    public void SetScatterAsset(string? asset)
    {
        if (asset == ScatterAsset) return;
        ScatterAsset = asset;
        SculptChanged?.Invoke();
    }

    /// <summary>One piece of a planting stroke (noor_doc_scatter_edit); panels refresh at its start and end.</summary>
    public JsonObject? ScatterEdit(JsonObject command, bool refresh)
    {
        var result = Native.ScatterEdit(_handle, command.ToJsonString());
        if (result == null)
        {
            EngineLog.Warn("Scatter", Native.LastError());
            return null;
        }
        if (refresh) Refresh();
        return JsonNode.Parse(result) as JsonObject;
    }

    // ---- Painting (Milestone 6, part 7): one tool for terrains (strokes) and CSG faces (clicks) ----

    /// <summary>On: left-drags on the selected terrain paint PaintMaterial; clicks on brush faces give them it.</summary>
    public bool Painting { get; private set; }
    /// <summary>The material being painted (a .noormat asset id), or null.</summary>
    public string? PaintMaterial { get; private set; }

    public void SetPainting(bool on)
    {
        if (on == Painting) return;
        Painting = on;
        if (on) { Sculpting = false; Scattering = null; }
        SculptChanged?.Invoke();
    }

    public void SetPaintMaterial(string? material)
    {
        if (material == PaintMaterial) return;
        PaintMaterial = material;
        SculptChanged?.Invoke();
    }

    /// <summary>Gives a CSG brush's face the paint material (one undo step; its UV settings stay).</summary>
    public bool PaintFace(string brushId, int face)
    {
        if (PaintMaterial == null || face < 0 || Scene.Entities.FirstOrDefault(e => e.Id == brushId)?.Components["brush"] is not JsonObject brush) return false;
        var next = (JsonObject)brush.DeepClone();
        var faces = next["faces"] as JsonArray ?? new JsonArray();
        while (faces.Count <= face) faces.Add(null);
        var own = faces[face] as JsonObject ?? new JsonObject();
        own["material"] = PaintMaterial;
        faces[face] = own.DeepClone();
        next["faces"] = faces.DeepClone();
        return Execute(new JsonObject { ["op"] = "set_component", ["entity"] = brushId, ["type"] = "brush", ["value"] = next });
    }

    public void SetBrush(SculptBrush brush)
    {
        if (brush == Brush) return;
        Brush = brush;
        SculptChanged?.Invoke();
    }

    /// <summary>
    /// One piece of a stroke (noor_doc_sculpt). Panels refresh only when asked (the stroke's start and
    /// end), not for every dab.
    /// </summary>
    public bool Sculpt(JsonObject command, bool refresh)
    {
        var result = Native.Sculpt(_handle, command.ToJsonString());
        if (result == null)
        {
            EngineLog.Warn("Sculpt", Native.LastError());
            return false;
        }
        // The terrain's first stroke made its sculpt layer file: show it in the asset list too.
        var parsed = JsonNode.Parse(result);
        if ((parsed?["sculpt"] ?? parsed?["paint"])?.GetValue<string>() is { Length: > 0 } layer && Assets.All(a => a.Guid != layer)) RefreshAssets(false);
        if (refresh) Refresh();
        return true;
    }

    /// <summary>Native handle for noor_viewport_set_document. Valid until Dispose.</summary>
    public IntPtr Handle => _handle;

    private EditorDocument(IntPtr handle)
    {
        _handle = handle;
        var info = JsonNode.Parse(Native.ProjectInfo(handle) ?? "{}")!;
        ProjectName = info["name"]?.GetValue<string>() ?? "Project";
        ProjectRoot = info["root"]?.GetValue<string>() ?? "";
        RefreshAssets(rescan: false);
        Refresh();
    }

    public static EditorDocument? Open(string projectPath, out string error)
    {
        var h = Native.noor_doc_open_project(projectPath);
        error = h == IntPtr.Zero ? Native.LastError() : "";
        return h == IntPtr.Zero ? null : new EditorDocument(h);
    }

    public static EditorDocument? Create(string folder, string name, out string error)
    {
        var h = Native.noor_doc_create_project(folder, name);
        error = h == IntPtr.Zero ? Native.LastError() : "";
        return h == IntPtr.Zero ? null : new EditorDocument(h);
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero) return;
        Native.noor_doc_close(_handle);
        _handle = IntPtr.Zero;
    }

    // ---- Scenes ----

    public bool OpenScene(string relativePath) => Run(Native.noor_doc_open_scene(_handle, relativePath), $"Open scene {relativePath}", clearSelection: true);

    public bool NewScene(string relativePath, string name)
    {
        var ok = Run(Native.noor_doc_new_scene(_handle, relativePath, name), $"New scene {relativePath}", clearSelection: true);
        if (ok) RefreshSceneFiles();
        return ok;
    }

    public bool Save()
    {
        if (!Scene.Open) return false;
        var ok = Run(Native.noor_doc_save_scene(_handle), "Save scene");
        if (ok)
        {
            EngineLog.Info("Editor", $"Saved {Scene.Path}");
            RefreshSceneFiles();
        }
        return ok;
    }

    // ---- Editing: every change goes through the engine's history ----

    public bool Execute(JsonObject command) => Run(Native.noor_doc_execute(_handle, command.ToJsonString()), $"{command["op"]}");

    public bool Undo() => !Scene.Playing && Scene.CanUndo && Run(Native.noor_doc_undo(_handle), "Undo");
    public bool Redo() => !Scene.Playing && Scene.CanRedo && Run(Native.noor_doc_redo(_handle), "Redo");

    // ---- Play mode (ADR-026): the engine runs a copy of the scene; Stop discards it ----

    public bool Playing => Scene.Playing;

    public bool Play() => Scene.Open && Run(Native.noor_doc_play(_handle), "Play");

    public void Stop()
    {
        if (!Scene.Playing) return;
        Native.noor_doc_stop(_handle);
        Refresh();
    }

    /// <summary>
    /// Advances the running game. <paramref name="gameView"/> is the engine viewport whose keyboard and
    /// mouse are the game's input while it has focus (IntPtr.Zero: no input). Doesn't refresh the
    /// panels (call Refresh at a lower rate).
    /// </summary>
    public void Tick(double seconds, IntPtr gameView) => Native.noor_doc_tick(_handle, seconds, gameView);

    /// <summary>Asks the engine which asset files changed on disk (it reloads them); returns their paths.</summary>
    public IReadOnlyList<string> PollFiles()
    {
        var changed = (JsonNode.Parse(Native.PollFiles(_handle)) as JsonArray ?? []).Select(n => n!.GetValue<string>()).ToList();
        if (changed.Count > 0) Refresh();
        return changed;
    }

    public bool SetTransform(string id, Transform t, string? label = null)
    {
        var cmd = new JsonObject { ["op"] = "set_transform", ["entity"] = id, ["transform"] = t.ToJson() };
        if (label != null) cmd["label"] = label;
        return Execute(cmd);
    }

    /// <summary>
    /// What a click in the viewport on <paramref name="hitId"/> selects: first the outermost object
    /// (a whole building, a prefab instance); clicking again inside the current selection goes one
    /// level deeper toward the part that was hit.
    /// </summary>
    public string? ClickTarget(string? hitId)
    {
        if (hitId == null) return null;
        var byId = Scene.Entities.ToDictionary(e => e.Id);
        var chain = new List<string>();  // hit .. root
        for (var id = hitId; id != null && byId.TryGetValue(id, out var e) && chain.Count < 256; id = e.Parent) chain.Add(id);
        if (chain.Count == 0) return null;
        var at = SelectedId == null ? -1 : chain.IndexOf(SelectedId);
        return at > 0 ? chain[at - 1] : at == 0 ? chain[0] : chain[^1];
    }

    public bool Rename(string id, string name) => Execute(new JsonObject { ["op"] = "rename", ["entity"] = id, ["name"] = name });

    public bool Remove(string id)
    {
        var ok = Execute(new JsonObject { ["op"] = "remove", ["entity"] = id });
        if (ok && SelectedId == id) Select(null);
        return ok;
    }

    /// <summary>Creates an entity (one undo step) and selects it. Returns its id, or null.</summary>
    public string? Create(string name, JsonObject components, Vec3 position, string? parent = null, Quat? rotation = null)
    {
        var id = Guid.NewGuid().ToString();
        var transform = new JsonObject { ["position"] = position.ToJson() };
        if (rotation is { } r) transform["rotation"] = r.ToJson();
        var cmd = new JsonObject
        {
            ["op"] = "create", ["id"] = id, ["name"] = name, ["components"] = components,
            ["transform"] = transform,
        };
        if (parent != null) cmd["parent"] = parent;
        if (!Execute(cmd)) return null;
        Select(id);
        return id;
    }

    /// <summary>Moves an entity among its siblings: before <paramref name="before"/>, or to the end (null).</summary>
    public bool Move(string id, string? before) =>
        Execute(new JsonObject { ["op"] = "move", ["entity"] = id, ["before"] = before });

    /// <summary>Exports a CSG model as a Noor Interchange GLB + manifest. Returns the result JSON, or null (logged).</summary>
    public JsonObject? ExportCsg(string modelId, string glbPath)
    {
        var json = Native.ExportCsg(Handle, modelId, glbPath);
        if (json == null)
        {
            EngineLog.Error("Export", Native.LastError());
            return null;
        }
        return JsonNode.Parse(json) as JsonObject;
    }

    /// <summary>The scene's CSG models as the engine built them (noor_doc_csg).</summary>
    public JsonArray CsgInfo() => JsonNode.Parse(Native.Csg(Handle)) as JsonArray ?? [];

    public bool AddAsset(AssetInfo asset, Vec3 position)
    {
        var ok = Execute(new JsonObject { ["op"] = "add_asset", ["asset"] = asset.Guid, ["position"] = position.ToJson() });
        if (ok)
        {
            // Select the new root: the newest entity with no parent whose name matches.
            var added = Scene.Entities.LastOrDefault(e => e.Parent == null && e.Name == System.IO.Path.GetFileNameWithoutExtension(asset.Path));
            if (added != null) Select(added.Id);
        }
        return ok;
    }

    // ---- Selection (editor state, not part of the document history) ----

    public EntityInfo? Selected => SelectedId == null ? null : Scene.Entities.FirstOrDefault(e => e.Id == SelectedId);

    public void Select(string? id)
    {
        if (id == SelectedId) return;
        SelectedId = id;
        SelectedFace = null;
        SelectionChanged?.Invoke();
    }

    /// <summary>A face of the selected CSG brush (its logical face index), picked in the viewport.</summary>
    public int? SelectedFace { get; private set; }

    public void SelectFace(int? face)
    {
        if (face == SelectedFace) return;
        SelectedFace = face;
        SelectionChanged?.Invoke();
    }

    /// <summary>The entity and all of its descendants, for framing a whole object.</summary>
    public IReadOnlyList<string> Subtree(string id)
    {
        var result = new List<string> { id };
        for (var i = 0; i < result.Count; i++)
            result.AddRange(Scene.Entities.Where(e => e.Parent == result[i]).Select(e => e.Id));
        return result;
    }

    // ---- Snapshots ----

    /// <summary>
    /// Makes a material (.noormat) from the texture set <paramref name="texturePath"/> belongs to
    /// (bricks_albedo, bricks_normal, bricks_orm…), beside it. Returns the material's path and
    /// whether it was new, or null (the error is in the log).
    /// </summary>
    public (string Path, bool Created, int Maps)? MakeMaterial(string texturePath)
    {
        var json = Native.MakeMaterial(Handle, texturePath);
        if (json == null)
        {
            Interop.EngineLog.Error("Editor", $"Make material: {Native.LastError()}");
            return null;
        }
        var node = JsonNode.Parse(json)!;
        RefreshAssets(rescan: false);
        return (node["path"]!.GetValue<string>(), node["created"]!.GetValue<bool>(), (node["maps"] as JsonArray)?.Count ?? 0);
    }

    /// <summary>The built-in effect presets (Milestone 8B, part 4): [{name, description, emitter}].</summary>
    public static JsonArray EffectPresets() => Native.EffectPresets() is { } json && JsonNode.Parse(json) is JsonArray a ? a : [];

    /// <summary>
    /// One effect command (noor_doc_effect: list, read, apply, duplicate, reset, save, preview). Changes refresh the
    /// scene and the asset list; a failure is logged and gives null.
    /// </summary>
    public JsonNode? Effect(JsonObject command)
    {
        var json = Native.Effect(_handle, command.ToJsonString());
        var op = command["op"]?.GetValue<string>() ?? "";
        if (json == null)
        {
            Interop.EngineLog.Error("Editor", $"Effect {op}: {Native.LastError()}");
            return null;
        }
        if (op is "apply" or "duplicate" or "reset" or "save")
        {
            RefreshAssets(rescan: false);
            Refresh();
        }
        return JsonNode.Parse(json);
    }

    /// <summary>
    /// One sprite sheet command (noor_doc_sprite: list, read, create, save, apply; Milestone 8B, part 5). Changes refresh
    /// the asset list and the scene; a failure is logged and gives null.
    /// </summary>
    public JsonNode? Sprite(JsonObject command)
    {
        var json = Native.Sprite(_handle, command.ToJsonString());
        var op = command["op"]?.GetValue<string>() ?? "";
        if (json == null)
        {
            Interop.EngineLog.Error("Sprites", $"Sprite {op}: {Native.LastError()}");
            return null;
        }
        if (op is "create" or "save" or "apply")
        {
            RefreshAssets(rescan: false);
            Refresh();
        }
        return JsonNode.Parse(json);
    }

    /// <summary>The built-in environment presets (Milestone 8A, part 6): [{name, description, environment}].</summary>
    public static JsonArray EnvironmentPresets() => Native.EnvironmentPresets() is { } json && JsonNode.Parse(json) is JsonArray a ? a : [];

    /// <summary>
    /// One environment command (noor_doc_environment: list, read, apply, duplicate, reset, save). Changes refresh
    /// the scene and the asset list; a failure is logged and gives null.
    /// </summary>
    public JsonNode? Environment(JsonObject command)
    {
        var json = Native.Environment(_handle, command.ToJsonString());
        var op = command["op"]?.GetValue<string>() ?? "";
        if (json == null)
        {
            Interop.EngineLog.Error("Editor", $"Environment {op}: {Native.LastError()}");
            return null;
        }
        if (op is "apply" or "duplicate" or "reset" or "save")
        {
            RefreshAssets(rescan: false);
            Refresh();
        }
        return JsonNode.Parse(json);
    }

    /// <summary>Everything the engine knows about one asset: its manifest and levels for a Noor Interchange model (Milestone 7, part 5).</summary>
    public JsonObject? AssetDetails(string guid) => Native.AssetDetails(_handle, guid) is { } json ? JsonNode.Parse(json) as JsonObject : null;

    public void RefreshAssets(bool rescan)
    {
        var json = Native.Assets(_handle, rescan);
        if (json == null)
        {
            EngineLog.Error("Editor", $"Asset scan failed: {Native.LastError()}");
            return;
        }
        Assets = (JsonNode.Parse(json) as JsonArray ?? [])
            .Select(a => new AssetInfo(a!["guid"]!.GetValue<string>(), a["path"]!.GetValue<string>(),
                                       a["importer"]?.GetValue<string>() ?? "", a["name"]?.GetValue<string>() ?? ""))
            .OrderBy(a => a.Path, StringComparer.OrdinalIgnoreCase).ToList();
        RefreshSceneFiles();
        AssetsChanged?.Invoke();
        // A rescan can change the scene's revision (moved files are repaired), so pick that up too.
        Refresh();
    }

    private void RefreshSceneFiles()
    {
        SceneFiles = (JsonNode.Parse(Native.Scenes(_handle) ?? "[]") as JsonArray ?? [])
            .Select(s => s!.GetValue<string>()).ToList();
    }

    /// <summary>Re-reads the scene snapshot if the engine's revision moved.</summary>
    public void Refresh()
    {
        var rev = Native.noor_doc_revision(_handle);
        if (rev == _seenRevision) return;
        _seenRevision = rev;
        Scene = SceneSnapshot.FromJson(Native.Scene(_handle) ?? "{}");
        if (SelectedId != null && Scene.Entities.All(e => e.Id != SelectedId)) Select(null);
        SceneChanged?.Invoke();
    }

    private bool Run(int result, string what, bool clearSelection = false)
    {
        if (result == 0)
        {
            EngineLog.Warn("Editor", $"{what} failed: {Native.LastError()}");
            return false;
        }
        if (clearSelection) Select(null);
        Refresh();
        return true;
    }
}
