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
        if (tool == Tool) return;
        Tool = tool;
        ToolChanged?.Invoke();
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
    public string? Create(string name, JsonObject components, Vec3 position, string? parent = null)
    {
        var id = Guid.NewGuid().ToString();
        var cmd = new JsonObject
        {
            ["op"] = "create", ["id"] = id, ["name"] = name, ["components"] = components,
            ["transform"] = new JsonObject { ["position"] = position.ToJson() },
        };
        if (parent != null) cmd["parent"] = parent;
        if (!Execute(cmd)) return null;
        Select(id);
        return id;
    }

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
