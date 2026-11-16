using System.Diagnostics;

namespace NoorEditor.Model;

/// <summary>
/// Which program opens scripts (Edit ▸ Preferences ▸ Scripts). Empty program: whatever Windows opens
/// the file type with. Arguments may use {file} (the script), {line} (1 when unknown) and {folder}
/// (the project's Scripts folder, so IDEs open the whole script project).
/// </summary>
public static class CodeEditors
{
    public sealed record Editor(string Name, string Program, string Arguments);

    public const string DefaultArguments = "\"{file}\"";

    /// <summary>Code editors found on this PC, best known first. Notepad is always there.</summary>
    public static IReadOnlyList<Editor> Detect()
    {
        var found = new List<Editor>();
        var local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        var programs = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles);
        var programsX86 = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86);

        void Add(string name, string arguments, params string?[] candidates)
        {
            var path = candidates.FirstOrDefault(p => !string.IsNullOrEmpty(p) && File.Exists(p));
            if (path != null) found.Add(new Editor(name, path, arguments));
        }

        Add("Visual Studio Code", "\"{folder}\" -g \"{file}:{line}\"",
            Path.Combine(local, "Programs", "Microsoft VS Code", "Code.exe"), Path.Combine(programs, "Microsoft VS Code", "Code.exe"));
        Add("Visual Studio", "/Edit \"{file}\"", VisualStudio());
        Add("JetBrains Rider", "--line {line} \"{file}\"",
            new[] { Path.Combine(local, "Programs", "Rider", "bin", "rider64.exe") }
                .Concat(SubFolders(Path.Combine(programs, "JetBrains"), "JetBrains Rider*").Select(d => Path.Combine(d, "bin", "rider64.exe")))
                .ToArray());
        Add("Notepad++", "\"{file}\" -n{line}", Path.Combine(programs, "Notepad++", "notepad++.exe"), Path.Combine(programsX86, "Notepad++", "notepad++.exe"));
        Add("Notepad", DefaultArguments, Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "notepad.exe"));
        return found;
    }

    /// <summary>The program and arguments for opening <paramref name="file"/>; null program means the Windows default.</summary>
    public static (string? Program, string Arguments) CommandLine(Preferences prefs, string projectRoot, string file, int line = 1)
    {
        file = Path.GetFullPath(file);  // one kind of slash: some editors show mixed ones oddly
        if (string.IsNullOrWhiteSpace(prefs.CodeEditor)) return (null, file);
        var template = string.IsNullOrWhiteSpace(prefs.CodeEditorArguments) ? DefaultArguments : prefs.CodeEditorArguments;
        var args = template.Replace("{file}", file).Replace("{line}", Math.Max(1, line).ToString())
                           .Replace("{folder}", Path.GetFullPath(ScriptBuilder.ScriptsDir(projectRoot)));
        return (prefs.CodeEditor, args);
    }

    /// <summary>Opens a script with the chosen editor (falls back to Notepad when nothing opens the file type).</summary>
    public static void Open(Preferences prefs, string projectRoot, string file, int line = 1)
    {
        var (program, args) = CommandLine(prefs, projectRoot, file, line);
        try
        {
            if (program == null) Process.Start(new ProcessStartInfo(file) { UseShellExecute = true });
            else Process.Start(new ProcessStartInfo(program, args) { UseShellExecute = false, WorkingDirectory = projectRoot });
        }
        catch (System.ComponentModel.Win32Exception e)
        {
            if (program != null) Interop.EngineLog.Warn("Editor", $"Couldn't start the code editor ({program}): {e.Message}. Opening Notepad instead.");
            Process.Start("notepad.exe", $"\"{file}\"");
        }
    }

    private static string? VisualStudio()
    {
        var vswhere = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Microsoft Visual Studio", "Installer", "vswhere.exe");
        if (!File.Exists(vswhere)) return null;
        try
        {
            using var p = Process.Start(new ProcessStartInfo(vswhere, "-latest -property productPath")
            {
                RedirectStandardOutput = true, UseShellExecute = false, CreateNoWindow = true,
            })!;
            var path = p.StandardOutput.ReadToEnd().Trim();
            p.WaitForExit(3000);
            return path.EndsWith("devenv.exe", StringComparison.OrdinalIgnoreCase) ? path : null;
        }
        catch (Exception)
        {
            return null;
        }
    }

    private static IEnumerable<string> SubFolders(string parent, string pattern)
    {
        try
        {
            return Directory.Exists(parent) ? Directory.GetDirectories(parent, pattern).OrderDescending() : [];
        }
        catch (Exception)
        {
            return [];
        }
    }
}
