using System.Text.Json;
using NoorEditor.Interop;

namespace NoorEditor.Model;

/// <summary>Edit ▸ Preferences. Viewport values go to the engine as noor_viewport_set_options JSON.</summary>
public sealed class Preferences
{
    public bool Snap { get; set; }
    public double MoveStep { get; set; } = 1.0;       // metres
    public double RotateStep { get; set; } = 15.0;    // degrees
    public double ScaleStep { get; set; } = 0.1;
    public double FlySpeed { get; set; } = 8.0;       // metres per second
    public double LookSensitivity { get; set; } = 1.0;
    public double GizmoScale { get; set; } = 1.0;

    public Preferences Copy() => (Preferences)MemberwiseClone();

    public string ToViewportJson() => System.Text.Json.JsonSerializer.Serialize(new Dictionary<string, object>
    {
        ["snap"] = Snap,
        ["move_step"] = MoveStep,
        ["rotate_step"] = RotateStep,
        ["scale_step"] = ScaleStep,
        ["fly_speed"] = FlySpeed,
        ["look_sensitivity"] = LookSensitivity,
        ["gizmo_scale"] = GizmoScale,
    });
}

/// <summary>Per-user editor settings in %APPDATA%\NoorEditor\settings.json (not part of any project).</summary>
public sealed class EditorSettings
{
    public Preferences Preferences { get; set; } = new();

    /// <summary>Raised after Preferences changed (and were saved).</summary>
    public event Action? PreferencesChanged;

    public void SetPreferences(Preferences p)
    {
        Preferences = p;
        Save();
        PreferencesChanged?.Invoke();
    }

    public List<string> RecentProjects { get; set; } = [];
    public double WindowWidth { get; set; } = 1600;
    public double WindowHeight { get; set; } = 920;
    public bool WindowMaximized { get; set; }
    /// <summary>Panel sizes by dock id (NoorEditor main window).</summary>
    public Dictionary<string, double> DockProportions { get; set; } = new();
    /// <summary>Active tab by dock id.</summary>
    public Dictionary<string, string> ActiveTabs { get; set; } = new();

    /// <summary>Settings file location. Tests point this somewhere temporary.</summary>
    public static string FilePath { get; set; } =
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "NoorEditor", "settings.json");

    private static readonly JsonSerializerOptions Json = new() { WriteIndented = true };

    public static EditorSettings Load()
    {
        try
        {
            if (File.Exists(FilePath))
                return JsonSerializer.Deserialize<EditorSettings>(File.ReadAllText(FilePath), Json) ?? new EditorSettings();
        }
        catch (Exception e)
        {
            EngineLog.Warn("Editor", $"Could not read {FilePath}, using defaults: {e.Message}");
        }
        return new EditorSettings();
    }

    public void Save()
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!);
            var tmp = FilePath + ".tmp";
            File.WriteAllText(tmp, JsonSerializer.Serialize(this, Json));
            File.Move(tmp, FilePath, overwrite: true);
        }
        catch (Exception e)
        {
            EngineLog.Warn("Editor", $"Could not save settings: {e.Message}");
        }
    }

    public void AddRecent(string projectRoot)
    {
        var full = Path.GetFullPath(projectRoot);
        RecentProjects.RemoveAll(p => string.Equals(Path.GetFullPath(p), full, StringComparison.OrdinalIgnoreCase));
        RecentProjects.Insert(0, full);
        if (RecentProjects.Count > 12) RecentProjects.RemoveRange(12, RecentProjects.Count - 12);
        Save();
    }
}
