using System.Diagnostics;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;
using NoorEditor.Interop;

namespace NoorEditor.Model;

/// <summary>
/// File ▸ Build Game: runs <c>noor_cli build</c> (copied next to the editor with noor_player.exe) and
/// reports its output in the Console. The result is a folder with the game's .exe, game.json and the
/// project's content in game/ (ADR-028).
/// </summary>
public static partial class GameBuilder
{
    public sealed record Result(bool Ok, string? Executable, string Message);

    public static string CliPath => Path.Combine(AppContext.BaseDirectory, "noor_cli.exe");

    public static async Task<Result> BuildAsync(string projectRoot, string outputDir, string? startScene)
    {
        if (!File.Exists(CliPath))
            return new Result(false, null, $"noor_cli.exe isn't next to the editor ({CliPath}). Build the engine first.");
        var info = new ProcessStartInfo(CliPath)
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
        };
        info.ArgumentList.Add("build");
        info.ArgumentList.Add(projectRoot);
        info.ArgumentList.Add(outputDir);
        if (!string.IsNullOrEmpty(startScene))
        {
            info.ArgumentList.Add("--scene");
            info.ArgumentList.Add(startScene);
        }
        EngineLog.Info("Build", $"Building into {outputDir}…");
        using var process = Process.Start(info)!;
        var stdout = process.StandardOutput.ReadToEndAsync();
        var stderr = process.StandardError.ReadToEndAsync();
        await process.WaitForExitAsync();
        foreach (var line in (await stdout + await stderr).Split('\n'))
        {
            var text = Ansi().Replace(line, "").TrimEnd();
            if (text.Length == 0) continue;
            var level = text.Contains("[ERROR]") || text.StartsWith("FAIL") ? 4 : text.Contains("[WARN") ? 3 : 2;
            EngineLog.Add(level, "Build", Prefix().Replace(text, ""));
        }
        if (process.ExitCode != 0) return new Result(false, null, "The build failed: the Console says why.");
        var manifest = JsonNode.Parse(await File.ReadAllTextAsync(Path.Combine(outputDir, "game.json")));
        var exe = Path.Combine(outputDir, manifest?["executable"]?.GetValue<string>() ?? "");
        return new Result(File.Exists(exe), exe, File.Exists(exe) ? $"Built {Path.GetFileName(exe)}" : "The build didn't produce a game.");
    }

    [GeneratedRegex(@"\x1b\[[0-9;]*m")]
    private static partial Regex Ansi();

    // "2026-11-16 16:19:56.577Z [ERROR] [CLI] [t28268] message (main.cpp:356)" -> "message (main.cpp:356)"
    [GeneratedRegex(@"^\S+ \S+ \[[A-Z ]+\] \[[^\]]+\] \[t\d+\] ")]
    private static partial Regex Prefix();
}
