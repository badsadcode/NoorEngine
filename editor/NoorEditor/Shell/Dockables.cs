using Avalonia.Controls;
using Avalonia.Controls.Templates;
using Dock.Model.Mvvm.Controls;

namespace NoorEditor.Shell;

// Dock creates a fresh view from these whenever a panel is (re)shown, e.g. after floating it
// into its own window, so each dockable carries a factory rather than a control instance.

public sealed class PanelTool : Tool
{
    public required Func<Control> CreateView { get; init; }
}

public sealed class PanelDocument : Document
{
    public required Func<Control> CreateView { get; init; }
}

public static class DockTemplates
{
    public static void Register(Avalonia.Application app)
    {
        app.DataTemplates.Add(new FuncDataTemplate<PanelTool>((t, _) => t?.CreateView()));
        app.DataTemplates.Add(new FuncDataTemplate<PanelDocument>((d, _) => d?.CreateView()));
    }
}
