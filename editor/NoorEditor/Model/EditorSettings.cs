using System.Text.Json;
using NoorEditor.Interop;

namespace NoorEditor.Model;

/// <summary>Per-user editor settings in %APPDATA%\NoorEditor\settings.json (not part of any project).</summary>
public sealed class EditorSettings
{
    public List<string> RecentProjects { get; set; } = [];
    public double WindowWidth { get; set; } = 1600;
    public double WindowHeight { get; set; } = 920;
    public bool WindowMaximized { get; set; }

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
