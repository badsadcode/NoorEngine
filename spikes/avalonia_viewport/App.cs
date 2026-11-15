using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Styling;
using Dock.Avalonia.Themes.Fluent;

namespace NoorEditorSpike;

public sealed class App : Application
{
    public override void Initialize()
    {
        RequestedThemeVariant = ThemeVariant.Dark;
        Styles.Add(NoorTheme.CreateFluent());
        Styles.Add(new DockFluentTheme());
        Styles.Add(NoorTheme.CreateStyles());
        NoorTheme.ApplyResources(Resources);
        PanelTemplates.Register(this);
    }

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            var version = Native.noor_editor_api_version();
            if (version != Native.ExpectedApiVersion)
                throw new InvalidOperationException($"noor_editor_api version {version}, expected {Native.ExpectedApiVersion}");
            EngineLog.Install();
            EngineLog.Add(2, "Editor", $"Avalonia spike started, engine API v{version}");

            var window = new MainWindow();
            desktop.MainWindow = window;
            desktop.Exit += (_, _) => EngineLog.Uninstall();
            if (Program.Options.SmokeSeconds > 0)
                SmokeTest.Attach(window, desktop);
        }
        base.OnFrameworkInitializationCompleted();
    }
}
