using System.Text.RegularExpressions;

namespace NoorEditor.Model;

/// <summary>
/// A place in a script named by an error message, so NoorCode can jump there. Recognises what the
/// engine and the editor write:
///   Lua:        "Scripts/Rider.luau:5: attempt to index nil" (also inside tracebacks)
///   C# compile: "Scripts/Spinner.cs:7: CS1002: ; expected"
///   C# runtime: "InvalidOperationException: boom at Spinner.cs:12" (file name only)
/// </summary>
public sealed partial record ScriptLocation(string RelativePath, int Line)
{
    [GeneratedRegex(@"(?<file>[A-Za-z0-9_\-./\\]*?[A-Za-z0-9_\-]+\.(?:luau|lua|cs)):(?<line>\d+)")]
    private static partial Regex Pattern();

    /// <summary>The first script location in <paramref name="message"/> that exists in the project.</summary>
    public static ScriptLocation? Find(string message, string projectRoot)
    {
        foreach (Match m in Pattern().Matches(message))
        {
            if (!int.TryParse(m.Groups["line"].Value, out var line) || line < 1) continue;
            if (Resolve(m.Groups["file"].Value.Trim(), projectRoot) is { } relative) return new ScriptLocation(relative, line);
        }
        return null;
    }

    /// <summary>
    /// A project-relative path ("Scripts/X.luau") for a path from a message: relative to the project,
    /// or a bare file name found under Scripts/ (C# stack traces only carry the name).
    /// </summary>
    public static string? Resolve(string file, string projectRoot)
    {
        file = file.Replace('\\', '/');
        try
        {
            var full = Path.GetFullPath(Path.IsPathRooted(file) ? file : Path.Combine(projectRoot, file));
            if (File.Exists(full) && Inside(full, projectRoot)) return Relative(full, projectRoot);
            var scripts = ScriptBuilder.ScriptsDir(projectRoot);
            if (!file.Contains('/') && Directory.Exists(scripts))
            {
                var match = Directory.EnumerateFiles(scripts, file, SearchOption.AllDirectories)
                    .FirstOrDefault(f => !IsBuildOutput(f));
                if (match != null) return Relative(match, projectRoot);
            }
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException)
        {
        }
        return null;
    }

    public static string Relative(string full, string projectRoot) => Path.GetRelativePath(projectRoot, full).Replace('\\', '/');

    private static bool Inside(string full, string root) =>
        full.StartsWith(Path.GetFullPath(root).TrimEnd('\\', '/') + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);

    private static bool IsBuildOutput(string path) =>
        path.Contains($"{Path.DirectorySeparatorChar}bin{Path.DirectorySeparatorChar}") ||
        path.Contains($"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}");
}
