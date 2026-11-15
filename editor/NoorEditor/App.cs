using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Styling;
using Dock.Avalonia.Themes.Fluent;
using NoorEditor.Hub;
using NoorEditor.Interop;
using NoorEditor.Model;
using NoorEditor.Shell;
using NoorEditor.Testing;

namespace NoorEditor;

public sealed class App : Application
{
    private IClassicDesktopStyleApplicationLifetime? _desktop;
    private EditorSettings _settings = new();

    public override void Initialize()
    {
        RequestedThemeVariant = ThemeVariant.Dark;
        Styles.Add(NoorTheme.CreateFluent());
        Styles.Add(new DockFluentTheme());
        Styles.Add(NoorTheme.CreateStyles());
        NoorTheme.ApplyResources(Resources);
        DockTemplates.Register(this);
    }

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            _desktop = desktop;
            // Windows come and go (hub ↔ editor), so the app decides when to exit.
            desktop.ShutdownMode = ShutdownMode.OnExplicitShutdown;

            var version = Native.noor_editor_api_version();
            if (version != Native.ExpectedApiVersion)
                throw new InvalidOperationException($"noor_editor_api.dll is API v{version}, this editor needs v{Native.ExpectedApiVersion}. Rebuild the engine.");
            EngineLog.Install();
            NoorEngine.ScriptHost.Initialize(Native.noor_rt_get_api());  // C# scripts call the engine through this table
            EngineLog.Info("Editor", $"NoorEditor started, engine API v{version}");
            desktop.Exit += (_, _) => EngineLog.Uninstall();

            var options = Program.Options;
            if (options.SmokeDir != null)
            {
                EditorSettings.FilePath = Path.Combine(options.SmokeDir, "settings.json");
                _settings = EditorSettings.Load();
                SmokeTest.Start(desktop, options, _settings);
            }
            else
            {
                _settings = EditorSettings.Load();
                var error = "";
                var doc = options.ProjectPath != null ? EditorDocument.Open(options.ProjectPath, out error) : null;
                if (doc != null)
                    ShowEditor(doc, null);
                else
                {
                    if (options.ProjectPath != null) EngineLog.Error("Editor", $"Could not open {options.ProjectPath}: {error}");
                    ShowHub();
                }
            }
        }
        base.OnFrameworkInitializationCompleted();
    }

    private void ShowHub()
    {
        var hub = new ProjectHub(_settings);
        var opened = false;
        hub.ProjectOpened += doc =>
        {
            opened = true;
            ShowEditor(doc, hub);
        };
        hub.Closed += (_, _) => { if (!opened) _desktop!.Shutdown(); };
        _desktop!.MainWindow = hub;
        hub.Show();
    }

    private void ShowEditor(EditorDocument doc, Window? replacing)
    {
        var window = new MainWindow(doc, _settings);
        window.Closed += (_, _) =>
        {
            if (window.ReturnToHub) ShowHub();
            else _desktop!.Shutdown();
        };
        _desktop!.MainWindow = window;
        window.Show();
        replacing?.Close();

        // Open the first scene so the editor never starts on a blank canvas when there is something to show.
        if (!doc.Scene.Open && doc.SceneFiles.Count > 0) doc.OpenScene(doc.SceneFiles[0]);
    }
}
