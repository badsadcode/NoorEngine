using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Platform.Storage;
using NoorEditor.Interop;
using NoorEditor.Model;
using NoorEditor.Shell;

namespace NoorEditor.Hub;

/// <summary>Start window: recent projects, create a project, open a project folder.</summary>
public sealed class ProjectHub : Window
{
    private readonly EditorSettings _settings;
    private readonly TextBlock _error = new() { Foreground = NoorTheme.Brush(NoorTheme.Bad), FontSize = 12, TextWrapping = TextWrapping.Wrap };
    private readonly StackPanel _recent = new() { Spacing = 4 };

    /// <summary>Raised with the opened document; the app then shows the editor window.</summary>
    public event Action<EditorDocument>? ProjectOpened;

    public ProjectHub(EditorSettings settings)
    {
        _settings = settings;
        Title = "NoorEditor — Projects";
        Width = 880;
        Height = 560;
        WindowStartupLocation = WindowStartupLocation.CenterScreen;
        Background = NoorTheme.Brush(NoorTheme.Background);

        var newButton = new Button { Content = "New Project…", Classes = { "primary" }, HorizontalAlignment = HorizontalAlignment.Stretch };
        newButton.Click += async (_, _) => await NewProject();
        var openButton = new Button { Content = "Open Project…", Classes = { "tool" }, HorizontalAlignment = HorizontalAlignment.Stretch };
        openButton.Click += async (_, _) => await OpenProjectDialog();

        var left = new StackPanel
        {
            Width = 260,
            Margin = new Thickness(28, 32, 20, 28),
            Spacing = 10,
            Children =
            {
                new TextBlock { Text = "N", FontSize = 44, FontWeight = FontWeight.Bold, Foreground = NoorTheme.Brush(NoorTheme.Accent) },
                new TextBlock { Text = "NoorEditor", Classes = { "title" } },
                new TextBlock { Text = "Milestone 3 preview", Classes = { "muted" }, FontSize = 12, Margin = new Thickness(0, 0, 0, 18) },
                newButton,
                openButton,
                _error,
            },
        };

        var recentHeader = new TextBlock { Text = "Recent projects", Classes = { "section" }, Margin = new Thickness(0, 0, 0, 8) };
        var right = new Border
        {
            Background = NoorTheme.Brush(NoorTheme.Panel),
            CornerRadius = new CornerRadius(10),
            Margin = new Thickness(0, 28, 28, 28),
            Padding = new Thickness(18),
            Child = new DockPanel { Children = { recentHeader, new ScrollViewer { Content = _recent } } },
        };
        DockPanel.SetDock(recentHeader, Avalonia.Controls.Dock.Top);

        var root = new DockPanel();
        DockPanel.SetDock(left, Avalonia.Controls.Dock.Left);
        root.Children.Add(left);
        root.Children.Add(right);
        Content = root;
        FillRecent();
    }

    private void FillRecent()
    {
        _recent.Children.Clear();
        if (_settings.RecentProjects.Count == 0)
        {
            _recent.Children.Add(new TextBlock { Text = "Nothing here yet. Create a project, or open an existing project folder.", Classes = { "muted" }, FontSize = 12, TextWrapping = TextWrapping.Wrap });
            return;
        }
        foreach (var path in _settings.RecentProjects)
        {
            var exists = Directory.Exists(path);
            var card = new Border
            {
                Background = NoorTheme.Brush(NoorTheme.PanelRaised),
                CornerRadius = new CornerRadius(6),
                Padding = new Thickness(12, 8),
                Cursor = new Cursor(StandardCursorType.Hand),
                Child = new StackPanel
                {
                    Children =
                    {
                        new TextBlock { Text = Path.GetFileName(path.TrimEnd('\\', '/')), FontSize = 13, FontWeight = FontWeight.SemiBold },
                        new TextBlock { Text = exists ? path : $"{path} (missing)", FontSize = 11, Classes = { "muted" }, TextTrimming = TextTrimming.CharacterEllipsis },
                    },
                },
                Opacity = exists ? 1.0 : 0.5,
            };
            card.PointerPressed += (_, _) => Open(path);
            _recent.Children.Add(card);
        }
    }

    public void Open(string path)
    {
        var doc = EditorDocument.Open(path, out var error);
        if (doc == null)
        {
            _error.Text = $"Could not open {path}: {error}";
            return;
        }
        _settings.AddRecent(doc.ProjectRoot.Length > 0 ? doc.ProjectRoot : path);
        ProjectOpened?.Invoke(doc);
    }

    private async Task OpenProjectDialog()
    {
        var folders = await StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions { Title = "Open a NoorEngine project folder" });
        if (folders.Count > 0 && folders[0].TryGetLocalPath() is { } path) Open(path);
    }

    /// <summary>NoorEngine's project templates, next to the editor (copied there by the build).</summary>
    public static IReadOnlyList<Dialogs.TemplateChoice> TemplateChoices()
    {
        var choices = new List<Dialogs.TemplateChoice> { new("Empty project", "Nothing but an empty scene: for a 3D game, or to set up everything yourself.", null) };
        try
        {
            using var json = System.Text.Json.JsonDocument.Parse(Native.Templates(Path.Combine(AppContext.BaseDirectory, "templates")));
            foreach (var t in json.RootElement.EnumerateArray())
                choices.Add(new(t.GetProperty("name").GetString() ?? "", t.GetProperty("description").GetString() ?? "", t.GetProperty("folder").GetString()));
        }
        catch (System.Text.Json.JsonException)
        {
            // No templates readable: an empty project is still offered.
        }
        return choices;
    }

    private async Task NewProject()
    {
        var picked = await Dialogs.NewProject(this, TemplateChoices());
        if (picked == null) return;
        var (name, template) = picked.Value;
        var parents = await StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions { Title = $"Where should the '{name}' folder go?" });
        if (parents.Count == 0 || parents[0].TryGetLocalPath() is not { } parent) return;

        var folder = Path.Combine(parent, name);
        var doc = EditorDocument.Create(folder, name, out var error, template.Folder);
        if (doc == null)
        {
            _error.Text = $"Could not create the project: {error}";
            return;
        }
        // A fresh empty project starts with an empty, saved main scene; a template's has its own.
        if (template.Folder == null) { if (doc.NewScene("Scenes/Main.noorscene", "Main")) doc.Save(); }
        else doc.OpenScene("Scenes/Main.noorscene");
        _settings.AddRecent(folder);
        ProjectOpened?.Invoke(doc);
    }
}
