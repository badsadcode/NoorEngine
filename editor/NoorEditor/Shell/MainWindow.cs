using Avalonia.Platform.Storage;
using System.Diagnostics;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;
using Dock.Avalonia.Controls;
using Dock.Model;
using Dock.Model.Controls;
using Dock.Model.Core;
using Dock.Model.Mvvm;
using NoorEditor.Interop;
using NoorEditor.Model;
using NoorEditor.Panels;
using Avalonia.VisualTree;
using DockOrientation = Dock.Model.Core.Orientation;
using LayoutOrientation = Avalonia.Layout.Orientation;

namespace NoorEditor.Shell;

/// <summary>The editor window for one open project.</summary>
public sealed class MainWindow : Window
{
    public EditorDocument Doc { get; }
    public Factory DockFactory { get; } = new();
    public PanelDocument ViewportDockable { get; }

    // The live panel views (Dock may recreate them, e.g. when a panel is floated).
    public InspectorPanel? Inspector { get; private set; }
    public HierarchyPanel? Hierarchy { get; private set; }
    public ContentPanel? ContentBrowser { get; private set; }
    /// <summary>The environment gallery (Milestone 8A, part 6), once shown.</summary>
    public EnvironmentPanel? Environments { get; private set; }
    public EffectsPanel? Effects { get; private set; }
    public AnimationPanel? Animation { get; private set; }
    public ViewportPanel? ViewportView { get; private set; }

    // NoorCode: the scripts open as tabs next to the viewport (they outlive layout rebuilds).
    private readonly List<ScriptDocument> _scripts = new();
    public IReadOnlyList<ScriptDocument> OpenScripts => _scripts;
    private readonly List<MaterialDocument> _materials = new();
    public IReadOnlyList<MaterialDocument> OpenMaterials => _materials;
    private readonly List<TextureDocument> _textures = new();
    public IReadOnlyList<TextureDocument> OpenTextures => _textures;
    private readonly List<SpriteDocument> _sprites = new();
    public IReadOnlyList<SpriteDocument> OpenSprites => _sprites;
    private readonly List<GraphDocument> _graphs = new();
    public IReadOnlyList<GraphDocument> OpenGraphs => _graphs;

    private readonly EditorSettings _settings;
    private readonly TextBlock _statusLeft = new() { FontSize = 11, VerticalAlignment = VerticalAlignment.Center };
    private readonly TextBlock _statusRight = new() { FontSize = 11, VerticalAlignment = VerticalAlignment.Center, Classes = { "muted" } };
    private readonly MenuItem _undo = new() { InputGesture = KeyGesture.Parse("Ctrl+Z") };
    private readonly MenuItem _redo = new() { InputGesture = KeyGesture.Parse("Ctrl+Y") };
    private readonly MenuItem _save = new() { Header = "_Save Scene", InputGesture = KeyGesture.Parse("Ctrl+S") };
    private readonly MenuItem _openScene = new() { Header = "_Open Scene" };
    private bool _closeConfirmed;
    private readonly Button _play = new() { Content = "▶  Play", Classes = { "primary" }, VerticalAlignment = VerticalAlignment.Center };
    // Play: the engine viewport's render thread runs the game, one frame per drawn frame
    // (noor_viewport_set_play), in step with the screen like the fly camera. The UI thread only
    // refreshes the panels a few times a second. (A 16 ms DispatcherTimer used to tick the game:
    // Windows timers fire in ~15.6 ms steps and the UI thread is often busy, so it managed ~35
    // updates/s and walking "didn't feel like 60 FPS".)
    private readonly DispatcherTimer _panelTimer = new() { Interval = TimeSpan.FromMilliseconds(200) };
    private IntPtr _playViewport;  // the viewport ticking the game (it changes if the viewport is re-created)
    private bool _playLoop;        // fallback without an engine viewport: tick on Avalonia's frames
    private int _playLoopId;       // a quick Stop + Play must not leave the old loop running alongside
    private readonly DispatcherTimer _fileTimer = new() { Interval = TimeSpan.FromMilliseconds(500) };
    private readonly Stopwatch _playClock = new();
    private TimeSpan _lastTick;
    private readonly DockControl _dockControl;
    private IRootDock _rootDock;

    /// <summary>Set when the user picked File ▸ Close Project (the app shows the hub again).</summary>
    public bool ReturnToHub { get; private set; }

    public MainWindow(EditorDocument doc, EditorSettings settings)
    {
        Doc = doc;
        _settings = settings;
        Width = settings.WindowWidth;
        Height = settings.WindowHeight;
        if (settings.WindowMaximized) WindowState = WindowState.Maximized;
        WindowStartupLocation = WindowStartupLocation.CenterScreen;
        Background = NoorTheme.Brush(NoorTheme.Background);

        ViewportDockable = new PanelDocument
        {
            Id = "Viewport", Title = "Viewport", CanClose = false,
            CreateView = () => ViewportView = new ViewportPanel(Doc, _settings),
        };

        var root = new DockPanel();
        var menu = MenuBar();
        DockPanel.SetDock(menu, Avalonia.Controls.Dock.Top);
        var status = StatusBar();
        DockPanel.SetDock(status, Avalonia.Controls.Dock.Bottom);
        root.Children.Add(menu);
        root.Children.Add(status);
        _rootDock = BuildLayout(useSaved: true);
        _dockControl = new DockControl { Factory = DockFactory, Layout = _rootDock };
        root.Children.Add(_dockControl);
        Content = root;

        AddHandler(KeyDownEvent, OnKeyDown, RoutingStrategies.Tunnel);
        Doc.SceneChanged += OnSceneChanged;
        Doc.AssetsChanged += RebuildOpenSceneMenu;
        OnSceneChanged();
        RebuildOpenSceneMenu();

        var timer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
        timer.Tick += (_, _) => UpdateStatusRight();
        _fileTimer.Tick += (_, _) =>
        {
            Doc.PollFiles();  // the engine logs and reloads what changed
            foreach (var script in _scripts) script.PollDisk();
            foreach (var material in _materials) material.PollDisk();
        };
        EngineLog.Lines.CollectionChanged += OnLogLines;
        DockFactory.DockableClosed += (_, e) =>
        {
            if (e.Dockable is ScriptDocument closed) _scripts.Remove(closed);
            if (e.Dockable is MaterialDocument closedMaterial) _materials.Remove(closedMaterial);
            if (e.Dockable is TextureDocument closedTexture) _textures.Remove(closedTexture);
            if (e.Dockable is SpriteDocument closedSprite) _sprites.Remove(closedSprite);
            if (e.Dockable is GraphDocument closedGraph) _graphs.Remove(closedGraph);
        };
        _fileTimer.Start();
        timer.Start();

        _panelTimer.Tick += (_, _) => RefreshWhilePlaying();
        Closing += OnClosing;
        Closed += (_, _) =>
        {
            timer.Stop();
            _playLoop = false;
            _panelTimer.Stop();
            _fileTimer.Stop();
            Doc.SceneChanged -= OnSceneChanged;
            Doc.AssetsChanged -= RebuildOpenSceneMenu;
            EngineLog.Lines.CollectionChanged -= OnLogLines;
            // Viewports keep working on a closed document (they keep the last snapshot), and Avalonia
            // destroys them with the window. Close the document last.
            ViewportView?.Viewport.Detach();
            Doc.Dispose();
        };
    }

    // ---- Layout ----

    private IRootDock BuildLayout(bool useSaved)
    {
        var hierarchy = new PanelTool
        {
            Id = "Hierarchy", Title = "Scene", CanClose = false,
            CreateView = () =>
            {
                Hierarchy = new HierarchyPanel(Doc);
                Hierarchy.FrameRequested += FrameEntity;
                return Hierarchy;
            },
        };
        var inspector = new PanelTool
        {
            Id = "Inspector", Title = "Inspector", CanClose = false,
            CreateView = () =>
            {
                Inspector = new InspectorPanel(Doc);
                Inspector.FrameRequested += FrameEntity;
                Inspector.Components.OpenFile = file => OpenScript(file);
                return Inspector;
            },
        };
        var content = new PanelTool
        {
            Id = "Content", Title = "Content Browser", CanClose = false,
            CreateView = () =>
            {
                ContentBrowser = new ContentPanel(Doc);
                ContentBrowser.AddRequested += async a => { if (await ReadyToEdit()) AddAsset(a); };
                ContentBrowser.OpenSceneRequested += s => _ = OpenScene(s);
                ContentBrowser.EditRequested += a =>
                {
                    if (a.Importer == "texture") OpenTexture(a.Path);
                    else if (a.Importer == "sprite") OpenSprite(a.Path);
                    else if (a.Importer == "procgraph") OpenGraph(a.Path);
                    else if (a.Importer == "anim_controller") ShowAnimationController(a.Path);
                    else OpenMaterial(a.Path);
                };
                return ContentBrowser;
            },
        };
        var console = new PanelTool { Id = "Console", Title = "Console", CanClose = false, CreateView = () => new ConsolePanel(Doc.ProjectRoot, l => OpenScript(l.RelativePath, l.Line)) };
        var render = new PanelTool { Id = "Render", Title = "Render", CanClose = false, CreateView = () => new RenderPanel() };
        var environments = new PanelTool
        {
            Id = "Environments", Title = "Environments", CanClose = false,
            CreateView = () => Environments = new EnvironmentPanel(Doc),
        };
        var effects = new PanelTool
        {
            Id = "Effects", Title = "Effects", CanClose = false,
            CreateView = () => Effects = new EffectsPanel(Doc),
        };
        var animation = new PanelTool
        {
            Id = "Animation", Title = "Animation", CanClose = false,
            CreateView = () => Animation = new AnimationPanel(Doc),
        };

        DockFactory
            .DocumentDock(out var documents, d => d.WithId("Documents").WithIsCollapsable(false))
            .ToolDock(out var left, Alignment.Left, t => t.WithId("Left").WithProportion(0.18))
            .ToolDock(out var right, Alignment.Right, t => t.WithId("Right").WithProportion(0.22))
            .ToolDock(out var bottom, Alignment.Bottom, t => t.WithId("Bottom").WithProportion(0.28))
            .ProportionalDockSplitter(out var splitV)
            .ProportionalDockSplitter(out var splitL)
            .ProportionalDockSplitter(out var splitR)
            .ProportionalDock(out var center, DockOrientation.Vertical, d => d.WithId("Center").Add(documents, splitV, bottom))
            .ProportionalDock(out var main, DockOrientation.Horizontal, d => d.WithId("Main").Add(left, splitL, center, splitR, right))
            .RootDock(out var rootDock, r => r.WithId("Root").Add(main).WithDefaultDockable(main).WithActiveDockable(main));

        // Dock 12.1.0.13: AppendTool/AppendDocument in the fluent builder left docks empty (see the
        // ADR-003 spike), so panels are added explicitly.
        void Fill(IDock dock, params IDockable[] items)
        {
            foreach (var item in items) DockFactory.AddDockable(dock, item);
            dock.ActiveDockable = items[0];
        }
        Fill(documents, ViewportDockable);
        foreach (var script in _scripts) DockFactory.AddDockable(documents, script);  // a layout rebuild keeps open scripts
        foreach (var material in _materials) DockFactory.AddDockable(documents, material);
        foreach (var texture in _textures) DockFactory.AddDockable(documents, texture);
        foreach (var sprite in _sprites) DockFactory.AddDockable(documents, sprite);
        foreach (var graph in _graphs) DockFactory.AddDockable(documents, graph);
        Fill(left, hierarchy);
        Fill(right, inspector);
        Fill(bottom, content, console, render, environments, effects, animation);

        DockFactory.DefaultHostWindowLocator = () => new HostWindow();
        DockFactory.HostWindowLocator = new Dictionary<string, Func<IHostWindow?>> { [nameof(IDockWindow)] = () => new HostWindow() };
        DockFactory.InitLayout(rootDock);
        if (useSaved) ApplySavedLayout(rootDock);
        return rootDock;
    }

    // Layout persistence: panel sizes (dock proportions) and each dock's active tab, by dock id.
    // Panels moved to another dock or floated are not remembered yet; they return home next run.

    private static IEnumerable<IDock> Docks(IDockable dockable)
    {
        if (dockable is not IDock dock) yield break;
        yield return dock;
        foreach (var child in dock.VisibleDockables ?? [])
            foreach (var d in Docks(child)) yield return d;
    }

    private void ApplySavedLayout(IRootDock root)
    {
        foreach (var dock in Docks(root))
        {
            if (dock.Id is not { } id) continue;
            if (_settings.DockProportions.TryGetValue(id, out var p) && p is > 0.02 and < 0.98) dock.Proportion = p;
            if (_settings.ActiveTabs.TryGetValue(id, out var tab) && dock.VisibleDockables?.FirstOrDefault(d => d.Id == tab) is { } active)
                dock.ActiveDockable = active;
        }
    }

    /// <summary>Copies the current panel sizes and active tabs into the settings (saved with them).</summary>
    public void RememberLayout()
    {
        _settings.DockProportions.Clear();
        _settings.ActiveTabs.Clear();
        foreach (var dock in Docks(_rootDock))
        {
            if (dock.Id is not { } id || dock is IRootDock) continue;
            if (double.IsFinite(dock.Proportion) && dock.Proportion > 0) _settings.DockProportions[id] = Math.Round(dock.Proportion, 4);
            if (dock.ActiveDockable?.Id is { } tab && dock is IToolDock) _settings.ActiveTabs[id] = tab;
        }
    }

    /// <summary>Window ▸ Reset Layout: the default arrangement, and forget the saved one.</summary>
    public void ResetLayout()
    {
        _settings.DockProportions.Clear();
        _settings.ActiveTabs.Clear();
        _settings.Save();
        _rootDock = BuildLayout(useSaved: false);
        _dockControl.Layout = _rootDock;
    }

    /// <summary>Rebuilds the layout from the saved settings (tests).</summary>
    public void ReloadLayout()
    {
        _rootDock = BuildLayout(useSaved: true);
        _dockControl.Layout = _rootDock;
    }

    /// <summary>The panel (dockable) with this id in the current layout (tests).</summary>
    public IDockable? FindDockable(string id) =>
        Docks(_rootDock).SelectMany(d => d.VisibleDockables ?? []).FirstOrDefault(d => d.Id == id);

    /// <summary>The dock with this id in the current layout (tests).</summary>
    public IDock? FindDock(string id) => Docks(_rootDock).FirstOrDefault(d => d.Id == id);

    private Control MenuBar()
    {
        var newScene = new MenuItem { Header = "_New Scene…" };
        var newGraph = new MenuItem { Header = "New Procedural _Graph…" };
        newGraph.Click += async (_, _) =>
        {
            if (await Dialogs.Prompt(this, "New procedural graph", "Name (saved as Assets/Graphs/<name>.noorgraph):", "Terrain") is { } name) NewGraph(name);
        };
        newScene.Click += async (_, _) => await NewScene();
        _save.Click += (_, _) => Save();
        var rescan = new MenuItem { Header = "_Rescan Assets" };
        rescan.Click += (_, _) => Doc.RefreshAssets(rescan: true);
        var closeProject = new MenuItem { Header = "_Close Project" };
        closeProject.Click += async (_, _) =>
        {
            if (Doc.Playing) TogglePlay();
            if (!await ConfirmDiscard() || !await ConfirmScripts()) return;
            ReturnToHub = true;
            _closeConfirmed = true;
            Close();
        };
        var buildGame = new MenuItem { Header = "_Build Game…", InputGesture = KeyGesture.Parse("Ctrl+B") };
        ToolTip.SetTip(buildGame, "Makes a folder with your game as a program that runs without the editor.");
        buildGame.Click += async (_, _) => await BuildGameInteractive();
        var exit = new MenuItem { Header = "E_xit", InputGesture = KeyGesture.Parse("Alt+F4") };
        exit.Click += (_, _) => Close();

        _undo.Click += (_, _) => Doc.Undo();
        _redo.Click += (_, _) => Doc.Redo();
        var delete = new MenuItem { Header = "_Delete", InputGesture = KeyGesture.Parse("Delete") };
        delete.Click += (_, _) => DeleteSelected();
        var frame = new MenuItem { Header = "_Frame Selected", InputGesture = KeyGesture.Parse("F") };
        var moveUp = new MenuItem { Header = "Move _Up (earlier)", InputGesture = KeyGesture.Parse("Alt+Up") };
        moveUp.Click += (_, _) => MoveSelected(-1);
        var moveDown = new MenuItem { Header = "Move Dow_n (later)", InputGesture = KeyGesture.Parse("Alt+Down") };
        moveDown.Click += (_, _) => MoveSelected(1);
        frame.Click += (_, _) => FrameSelectedOrAll();

        var preferences = new MenuItem { Header = "_Preferences…" };
        preferences.Click += async (_, _) => await ShowPreferences();
        var resetLayout = new MenuItem { Header = "_Reset Layout" };
        resetLayout.Click += (_, _) => ResetLayout();
        MenuItem Tool(string header, string key, EditorTool tool)
        {
            var item = new MenuItem { Header = header, InputGesture = KeyGesture.Parse(key) };
            item.Click += (_, _) => Doc.SetTool(tool);
            return item;
        }

        var about = new MenuItem { Header = "_About NoorEditor" };
        about.Click += async (_, _) => await Dialogs.Ask(this, "About NoorEditor",
            $"NoorEditor (Milestone 3 preview)\nEngine API v{Native.noor_editor_api_version()} · MIT licence\nBad Ideas Works", "OK");

        var renderStats = new MenuItem { Header = "_Render stats" };
        renderStats.Click += (_, _) =>
        {
            if (FindDockable("Render") is { } d && d.Owner is Dock.Model.Core.IDock owner)
            {
                DockFactory.SetActiveDockable(d);
                DockFactory.SetFocusedDockable(owner, d);
            }
        };
        var menu = new Menu
        {
            FontSize = 12,
            Items =
            {
                new MenuItem { Header = "_File", Items = { newScene, _openScene, _save, new Separator(), newGraph, new Separator(), rescan, new Separator(), buildGame, new Separator(), closeProject, exit } },
                new MenuItem { Header = "_Edit", Items = { _undo, _redo, new Separator(), delete, frame, moveUp, moveDown, new Separator(),
                                                         Tool("_Select tool", "Q", EditorTool.Select), Tool("_Move tool", "W", EditorTool.Move),
                                                         Tool("_Rotate tool", "E", EditorTool.Rotate), Tool("Sc_ale tool", "R", EditorTool.Scale),
                                                         new Separator(), preferences } },
                CreateMenu(),
                new MenuItem { Header = "_Window", Items = { renderStats, new Separator(), resetLayout } },
                new MenuItem { Header = "_Help", Items = { about } },
            },
        };

        var logo = new TextBlock
        {
            Text = "N", FontWeight = FontWeight.Bold, FontSize = 18, Margin = new Thickness(12, 0, 6, 0),
            Foreground = NoorTheme.Brush(NoorTheme.Accent), VerticalAlignment = VerticalAlignment.Center,
        };
        var bar = new DockPanel { Height = 34, Background = NoorTheme.Brush(NoorTheme.Panel) };
        DockPanel.SetDock(logo, Avalonia.Controls.Dock.Left);
        bar.Children.Add(logo);
        ToolTip.SetTip(_play, "Play / Stop (Ctrl+P). Runs a copy of the scene; Stop throws it away.");
        _play.Click += (_, _) => TogglePlay();
        var right = new StackPanel { Orientation = LayoutOrientation.Horizontal, Margin = new Thickness(0, 0, 12, 0), Children = { _play } };
        DockPanel.SetDock(right, Avalonia.Controls.Dock.Right);
        bar.Children.Add(right);
        bar.Children.Add(menu);
        return new Border { Child = bar, BorderBrush = NoorTheme.Brush(NoorTheme.Border), BorderThickness = new Thickness(0, 0, 0, 1) };
    }

    private MenuItem CreateMenu()
    {
        var menu = new MenuItem { Header = "_Create" };
        foreach (var t in EntityTemplates.All)
        {
            var item = new MenuItem { Header = t.Name };
            item.Click += async (_, _) => { if (await ReadyToEdit()) CreateEntity(t); };
            menu.Items.Add(item);
        }
        menu.Items.Add(new Separator());
        var brushes = new MenuItem { Header = "_Brush" };
        foreach (var shape in EntityTemplates.BrushShapes)
        {
            var item = new MenuItem { Header = char.ToUpperInvariant(shape[0]) + shape[1..] + (shape == "arch" ? " (doorway, subtracts)" : "") };
            item.Click += async (_, _) => { if (await ReadyToEdit()) CreateBrush(shape); };
            brushes.Items.Add(item);
        }
        menu.Items.Add(brushes);
        menu.Items.Add(new Separator());
        var player = new MenuItem { Header = EntityTemplates.PlayerTemplateName };
        player.Click += async (_, _) => { if (await ReadyToEdit()) CreatePlayer(new Vec3(0, 0.05, 0)); };
        menu.Items.Add(player);
        var thirdPerson = new MenuItem { Header = EntityTemplates.ThirdPersonTemplateName };
        thirdPerson.Click += async (_, _) => { if (await ReadyToEdit()) CreateThirdPersonPlayer(new Vec3(0, 0.05, 0)); };
        menu.Items.Add(thirdPerson);
        return menu;
    }

    /// <summary>
    /// Create ▸ Player (first person): the player entity, its body and camera, and
    /// Scripts/PlayerController.luau (written from the template if the project doesn't have it yet).
    /// One undo step.
    /// </summary>
    public string? CreatePlayer(Vec3 feet)
    {
        if (!Doc.Scene.Open || Doc.Playing) return null;
        var script = Path.Combine(Doc.ProjectRoot, EntityTemplates.PlayerScriptPath);
        if (!File.Exists(script))
        {
            Directory.CreateDirectory(Path.GetDirectoryName(script)!);
            File.WriteAllText(script, EntityTemplates.PlayerScript);
        }
        Doc.Execute(new System.Text.Json.Nodes.JsonObject { ["op"] = "begin", ["label"] = "Create Player" });
        var id = Doc.Create("Player", EntityTemplates.PlayerComponents(), feet);
        if (id != null)
        {
            Doc.Create("Body", EntityTemplates.PlayerBody(), new Vec3(0, 0.9, 0), id);
            Doc.Create("Camera", EntityTemplates.PlayerCamera(), new Vec3(0, 1.6, 0), id);
        }
        Doc.Execute(new System.Text.Json.Nodes.JsonObject { ["op"] = "end" });
        if (id != null) Doc.Select(id);
        return id;
    }

    /// <summary>
    /// Create ▸ Player (third person) (8C part 4): the player entity (character controller and
    /// Scripts/ThirdPersonPlayer.luau), a camera rig behind it, and Ward on his controller as its model. Ward's
    /// model and controller go into Assets/Characters/Ward/ the first time (from the editor; ours, CC0), the script
    /// into Scripts/. One undo step.
    /// </summary>
    public string? CreateThirdPersonPlayer(Vec3 feet)
    {
        if (!Doc.Scene.Open || Doc.Playing) return null;
        void Write(string relative, Func<Stream?> content)
        {
            var path = Path.Combine(Doc.ProjectRoot, relative);
            if (File.Exists(path)) return;
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            using var from = content();
            if (from == null) return;
            using var to = File.Create(path);
            from.CopyTo(to);
        }
        Stream? Resource(string name) => typeof(MainWindow).Assembly.GetManifestResourceStream(name);
        Write(EntityTemplates.ThirdPersonScriptPath, () => new MemoryStream(System.Text.Encoding.UTF8.GetBytes(EntityTemplates.ThirdPersonScript)));
        Write(EntityTemplates.WardModelPath, () => Resource("NoorEditor.Characters.Ward.glb"));
        Write(EntityTemplates.WardControllerPath, () => Resource("NoorEditor.Characters.Ward.nooranim"));
        Doc.RefreshAssets(rescan: true);  // they get their ids
        string? Asset(string relative) =>
            Doc.Assets.FirstOrDefault(a => string.Equals(a.Path.Replace('\\', '/'), relative, StringComparison.OrdinalIgnoreCase))?.Guid;
        if (Asset(EntityTemplates.WardModelPath) is not { } model || Asset(EntityTemplates.WardControllerPath) is not { } controller)
        {
            EngineLog.Error("Editor", "Create Player (third person): Ward's model or controller didn't make it into the project");
            return null;
        }

        Doc.Execute(new System.Text.Json.Nodes.JsonObject { ["op"] = "begin", ["label"] = "Create Player (third person)" });
        var id = Doc.Create("Player", EntityTemplates.ThirdPersonComponents(), feet);
        if (id != null)
        {
            var rig = Doc.Create("CameraRig", new System.Text.Json.Nodes.JsonObject(), new Vec3(0, 1.5, 0), id);
            if (rig != null) Doc.Create("Camera", EntityTemplates.ThirdPersonCamera(), new Vec3(0, 0.3, 4.0), rig);
            var before = Doc.Scene.Entities.Select(e => e.Id).ToHashSet();
            Doc.Execute(new System.Text.Json.Nodes.JsonObject { ["op"] = "add_asset", ["asset"] = model, ["position"] = feet.ToJson(), ["name"] = "Ward" });
            if (Doc.Scene.Entities.FirstOrDefault(e => e.Name == "Ward" && e.Parent == null && !before.Contains(e.Id)) is { } ward)
            {
                Doc.Execute(new System.Text.Json.Nodes.JsonObject { ["op"] = "reparent", ["entity"] = ward.Id, ["parent"] = id });
                Doc.Execute(new System.Text.Json.Nodes.JsonObject
                {
                    ["op"] = "set_transform", ["entity"] = ward.Id,
                    ["transform"] = new System.Text.Json.Nodes.JsonObject { ["position"] = new Vec3(0, 0, 0).ToJson() },
                });
                Doc.Execute(new System.Text.Json.Nodes.JsonObject
                {
                    ["op"] = "set_component", ["entity"] = ward.Id, ["type"] = "animator", ["value"] = EntityTemplates.WardAnimator(controller),
                });
            }
        }
        Doc.Execute(new System.Text.Json.Nodes.JsonObject { ["op"] = "end" });
        if (id != null) Doc.Select(id);
        return id;
    }

    /// <summary>Create ▸ template: added to the open scene and selected.</summary>
    /// <summary>
    /// Create ▸ Brush: into the selected CSG model (or the model of the selected brush); without
    /// one, a new "CSG model" entity first. One undo step. Returns the brush's id.
    /// </summary>
    public string? CreateBrush(string shape)
    {
        if (!Doc.Scene.Open || Doc.Playing) return null;
        var selected = Doc.Selected;
        string? model = null;
        if (selected?.Components["csg_model"] != null) model = selected.Id;
        else if (selected?.Components["brush"] != null && selected.Parent is { } p &&
                 Doc.Scene.Entities.FirstOrDefault(e => e.Id == p)?.Components["csg_model"] != null) model = p;
        var brush = EntityTemplates.Brush(shape);
        Doc.Execute(new System.Text.Json.Nodes.JsonObject { ["op"] = "begin", ["label"] = $"Create {shape} brush" });
        // A new model collides with its own shape (a mesh collider from its cooked triangles).
        model ??= Doc.Create("CSG model", new System.Text.Json.Nodes.JsonObject
        {
            ["csg_model"] = new System.Text.Json.Nodes.JsonObject(),
            ["collider"] = new System.Text.Json.Nodes.JsonObject { ["shape"] = "mesh" },
        }, new Vec3(0, 0, 0));
        string? id = null;
        if (model != null)
        {
            var count = Doc.Scene.Entities.Count(e => e.Parent == model && e.Components["brush"]?["shape"]?.GetValue<string>() == shape);
            id = Doc.Create(char.ToUpperInvariant(shape[0]) + shape[1..] + (count > 0 ? $" {count + 1}" : ""),
                            new System.Text.Json.Nodes.JsonObject { ["brush"] = brush }, EntityTemplates.BrushPosition(brush), model);
        }
        Doc.Execute(new System.Text.Json.Nodes.JsonObject { ["op"] = "end" });
        return id;
    }

    /// <summary>Edit ▸ Move Up / Move Down: one place earlier or later among its siblings (CSG brushes: the order of operations).</summary>
    public bool MoveSelected(int direction)
    {
        if (Doc.Selected is not { } e || Doc.Playing) return false;
        var siblings = Doc.Scene.Entities.Where(s => s.Parent == e.Parent).Select(s => s.Id).ToList();
        var i = siblings.IndexOf(e.Id);
        if (direction < 0 && i > 0) return Doc.Move(e.Id, siblings[i - 1]);
        if (direction > 0 && i >= 0 && i + 1 < siblings.Count) return Doc.Move(e.Id, i + 2 < siblings.Count ? siblings[i + 2] : null);
        return false;
    }

    public string? CreateEntity(EntityTemplates.Template t)
    {
        if (!Doc.Scene.Open)
        {
            _ = Dialogs.Ask(this, "No scene open", "Create or open a scene first (File ▸ New Scene).", "OK");
            return null;
        }
        if (Doc.Playing) return null;
        return Doc.Create(t.Name, t.Components(), t.Position, rotation: t.EulerDegrees is { } e ? Quat.FromEulerDegrees(e) : null);
    }

    private Control StatusBar()
    {
        var bar = new DockPanel();
        DockPanel.SetDock(_statusRight, Avalonia.Controls.Dock.Right);
        bar.Children.Add(_statusRight);
        bar.Children.Add(_statusLeft);
        return new Border
        {
            Height = 26,
            Background = NoorTheme.Brush(NoorTheme.Panel),
            BorderBrush = NoorTheme.Brush(NoorTheme.Border),
            BorderThickness = new Thickness(0, 1, 0, 0),
            Padding = new Thickness(12, 0),
            Child = bar,
        };
    }

    // ---- Document state → chrome ----

    private void OnSceneChanged()
    {
        var s = Doc.Scene;
        var scene = s.Open ? $"{s.Name}{(s.Modified ? " •" : "")}" : "no scene";
        Title = $"NoorEditor — {Doc.ProjectName} — {scene}{(s.Playing ? "  ▶ Playing" : "")}";
        _play.Content = _compiling ? "Compiling…" : s.Playing ? "■  Stop" : "▶  Play";
        _play.IsEnabled = s.Open && !_compiling;
        _undo.Header = s.CanUndo ? $"_Undo {s.UndoLabel}" : "_Undo";
        _undo.IsEnabled = s.CanUndo;
        _redo.Header = s.CanRedo ? $"_Redo {s.RedoLabel}" : "_Redo";
        _redo.IsEnabled = s.CanRedo;
        _save.IsEnabled = s.Open;
        var issues = s.Issues.Count > 0 ? $"   ·   ⚠ {s.Issues.Count} issue(s)" : "";
        _statusLeft.Text = s.Open
            ? $"{Doc.ProjectName}   ·   {s.Path}{(s.Modified ? " (modified)" : "")}   ·   {s.Entities.Count} entities{issues}"
            : $"{Doc.ProjectName}   ·   no scene open";
        _statusLeft.Foreground = NoorTheme.Brush(s.Issues.Count > 0 ? NoorTheme.Warn : NoorTheme.Text);
        if (s.Playing)
        {
            _statusLeft.Text = $"▶ Playing {s.PlaySeconds:0.0} s   ·   edits are off while playing; Stop (Ctrl+P) to edit again   ·   {s.Entities.Count} entities";
            _statusLeft.Foreground = NoorTheme.Brush(NoorTheme.Cyan);
        }
    }

    private void RebuildOpenSceneMenu()
    {
        _openScene.Items.Clear();
        foreach (var path in Doc.SceneFiles)
        {
            var item = new MenuItem { Header = path };
            item.Click += async (_, _) => await OpenScene(path);
            _openScene.Items.Add(item);
        }
        _openScene.IsEnabled = Doc.SceneFiles.Count > 0;
    }

    private void UpdateStatusRight()
    {
        var s = ViewportView?.Viewport.Stats;
        var fps = s is { State: Native.ViewportState.Running } st ? $"{st.FramesPerSecond:0} FPS   ·   {st.GpuName}"
                : s == null ? "viewport resting (its tab is hidden)" : "viewport not running";
        _statusRight.Text = $"{fps}   ·   RAM {Process.GetCurrentProcess().WorkingSet64 / (1024 * 1024)} MB";
    }

    // ---- Commands ----

    /// <summary>
    /// Scene edits are off while playing. Instead of silently doing nothing, ask whether to stop.
    /// True when the scene can be edited now.
    /// </summary>
    private async Task<bool> ReadyToEdit()
    {
        if (!Doc.Playing) return true;
        var answer = await Dialogs.Ask(this, "The game is playing",
            "The scene can't be changed while the game is playing. Stop the game and continue?\n" +
            "(Stop throws away what changed during play, as always.)",
            "Stop and continue", "Keep playing");
        if (answer != "Stop and continue") return false;
        if (Doc.Playing) TogglePlay();
        return !Doc.Playing;
    }

    /// <summary>Play ▸ Stop ▸ Play. While playing the engine runs a copy of the scene.</summary>
    public void TogglePlay()
    {
        if (Doc.Playing)
        {
            StopPlayLoop();
            if (ViewportView?.Viewport.EngineHandle is { } vp && vp != IntPtr.Zero) Native.noor_viewport_set_mouse_capture(vp, 0);  // the mouse always comes back on Stop
            Doc.Stop();
            NoorEngine.ScriptHost.UnloadScripts();  // the next Play compiles and loads them fresh
            return;
        }
        _ = PlayAsync();
    }

    private bool _compiling;

    /// <summary>
    /// Starts play mode. If the project has C# scripts they are compiled first (ADR-008); compile
    /// errors go to the Console and play doesn't start. Returns whether play started.
    /// </summary>
    public async Task<bool> PlayAsync()
    {
        if (Doc.Playing || _compiling || !Doc.Scene.Open) return Doc.Playing;
        SaveScripts();  // Play runs what's in the editor, like an IDE's Run
        if (ScriptBuilder.HasScripts(Doc.ProjectRoot))
        {
            _compiling = true;
            _play.IsEnabled = false;
            _play.Content = "Compiling…";
            try
            {
                var build = await ScriptBuilder.BuildAsync(Doc.ProjectRoot);
                if (!build.Ok)
                {
                    foreach (var error in build.Errors) EngineLog.Error("Script", error);
                    EngineLog.Warn("Editor", $"Play didn't start: {build.Errors.Count} C# compile error(s)");
                    return false;
                }
                var classes = NoorEngine.ScriptHost.LoadScripts(build.Assembly!, build.Symbols);
                EngineLog.Info("Editor", $"C# scripts compiled in {build.Took.TotalSeconds:0.0} s ({classes.Count} script classes)");
                Native.noor_doc_set_script_host(Doc.Handle, NoorEngine.ScriptHost.HostTable);
            }
            finally
            {
                _compiling = false;
                OnSceneChanged();  // restores the Play button
            }
        }
        else
        {
            Native.noor_doc_set_script_host(Doc.Handle, NoorEngine.ScriptHost.HostTable);
        }
        if (!Doc.Play()) return false;  // the selection stays: the running copy keeps every entity's id
        _playClock.Restart();
        _lastTick = TimeSpan.Zero;
        StartPlayLoop();
        return true;
    }

    private void StartPlayLoop()
    {
        _panelTimer.Start();
        FollowViewport();
    }

    private void StopPlayLoop()
    {
        _panelTimer.Stop();
        if (_playViewport != IntPtr.Zero) Native.noor_viewport_set_play(_playViewport, IntPtr.Zero);  // returns once no tick runs
        _playViewport = IntPtr.Zero;
        _playLoop = false;
    }

    // The engine viewport runs the game; if it was re-created (the panel floated, say), the new one
    // takes over. Without one, Avalonia's frame callback ticks it.
    private void FollowViewport()
    {
        var vp = ViewportView?.Viewport.EngineHandle ?? IntPtr.Zero;
        if (vp == _playViewport) return;
        _playViewport = vp;
        if (vp != IntPtr.Zero)
        {
            _playLoop = false;
            Native.noor_viewport_set_play(vp, Doc.Handle);
            return;
        }
        if (_playLoop) return;
        _playLoop = true;
        var id = ++_playLoopId;
        void Frame(TimeSpan _)
        {
            if (!_playLoop || id != _playLoopId) return;
            if (Doc.Playing) PlayTick();
            RequestAnimationFrame(Frame);
        }
        RequestAnimationFrame(Frame);
    }

    // Fallback only (no engine viewport): one game frame per Avalonia frame.
    private void PlayTick()
    {
        var now = _playClock.Elapsed;
        Doc.Tick((now - _lastTick).TotalSeconds, IntPtr.Zero);
        _lastTick = now;
    }

    // Panels follow the running scene a few times per second, not every frame.
    private void RefreshWhilePlaying()
    {
        if (!Doc.Playing) return;
        FollowViewport();
        Doc.Refresh();
    }

    public async Task ShowPreferences()
    {
        var dialog = new PreferencesWindow(_settings.Preferences);
        await dialog.ShowDialog(this);
        if (dialog.Result != null) _settings.SetPreferences(dialog.Result);
    }

    /// <summary>Closes without the unsaved-changes prompt (automated tests).</summary>
    public void CloseWithoutPrompt()
    {
        _closeConfirmed = true;
        Close();
    }

    public void Save()
    {
        if (Doc.Scene.Open) Doc.Save();
    }

    public void AddAsset(AssetInfo asset)
    {
        if (!Doc.Scene.Open)
        {
            _ = Dialogs.Ask(this, "No scene open", "Create or open a scene first (File ▸ New Scene).", "OK");
            return;
        }
        if (asset.Importer == "sprite")
        {
            // A sprite standing on the ground, showing the sheet's first animation (if it has one).
            var sheet = Doc.Sprite(new System.Text.Json.Nodes.JsonObject { ["op"] = "read", ["asset"] = asset.Path });
            var first = (sheet?["sheet"]?["animations"] as System.Text.Json.Nodes.JsonObject)?.Select(kv => kv.Key).FirstOrDefault();
            var apply = new System.Text.Json.Nodes.JsonObject { ["op"] = "apply", ["asset"] = asset.Path, ["position"] = new System.Text.Json.Nodes.JsonArray(0, 0, 0) };
            if (first != null) apply["animation"] = first;
            if (Doc.Sprite(apply)?["entity"]?.GetValue<string>() is { } sid) FrameEntity(sid);
            return;
        }
        if (asset.Importer == "material")
        {
            // A ball wearing the material, the classic way to look at one.
            var ball = new System.Text.Json.Nodes.JsonObject { ["shape"] = "sphere", ["radius"] = 0.5, ["material"] = asset.Guid };
            if (Doc.Create(Path.GetFileNameWithoutExtension(asset.Path), new System.Text.Json.Nodes.JsonObject { ["primitive"] = ball },
                           new Vec3(0, 0.5, 0)) is { } mid)
                FrameEntity(mid);
            return;
        }
        if (asset.Importer == "texture" && asset.Path.EndsWith(".hdr", StringComparison.OrdinalIgnoreCase))
        {
            // An HDRI becomes the scene's environment (the sky and the light from all around): the
            // existing Environment entity gets it, or a new one is made.
            var existing = Doc.Scene.Entities.FirstOrDefault(e => e.Components["environment"] is System.Text.Json.Nodes.JsonObject);
            if (existing != null)
            {
                var env = (System.Text.Json.Nodes.JsonObject)existing.Components["environment"]!.DeepClone();
                env["hdri"] = asset.Guid;
                Doc.Execute(new System.Text.Json.Nodes.JsonObject { ["op"] = "set_component", ["entity"] = existing.Id, ["type"] = "environment", ["value"] = env });
                Doc.Select(existing.Id);
            }
            else
            {
                Doc.Create("Environment", new System.Text.Json.Nodes.JsonObject { ["environment"] = EntityTemplates.Environment(asset.Guid) }, new Vec3(0, 0, 0));
            }
            return;
        }
        if (asset.Importer == "texture")
        {
            // A 1 m test cube wearing the texture: as its colour, or as its normal map when the name
            // says so (the engine reads the real kind from the .noormeta; this only picks the slot).
            var normal = System.Text.RegularExpressions.Regex.IsMatch(Path.GetFileNameWithoutExtension(asset.Path),
                                                                       @"(^|[_\-. ])(normal|nrm|nor|norm)([_\-. ]|$)", System.Text.RegularExpressions.RegexOptions.IgnoreCase);
            var primitive = EntityTemplates.Box(color: normal ? "#B8B0A8" : "#FFFFFF");
            primitive[normal ? "normal_map" : "base_color_map"] = asset.Guid;
            if (Doc.Create(Path.GetFileNameWithoutExtension(asset.Path), new System.Text.Json.Nodes.JsonObject { ["primitive"] = primitive },
                           new Vec3(0, 0.5, 0)) is { } tid)
                FrameEntity(tid);
            return;
        }
        if (asset.Importer == "audio")
        {
            // A sound emitter at the origin, playing its clip when play starts.
            if (Doc.Create(Path.GetFileNameWithoutExtension(asset.Path), new System.Text.Json.Nodes.JsonObject { ["audio_source"] = EntityTemplates.AudioSource(asset.Guid) },
                           new Vec3(0, 1, 0)) is { } sid)
                FrameEntity(sid);
            return;
        }
        if (asset.Importer is "terrain_sculpt" or "terrain_paint")
        {
            _ = Dialogs.Ask(this, "A terrain layer", "This file holds a terrain's hand-made sculpting or paint. The terrain that made it uses it; it isn't added to scenes by itself.", "OK");
            return;
        }
        if (asset.Importer == "procgraph")
        {
            OpenGraph(asset.Path);  // graphs make things rather than being placed: open it in the graph editor
            return;
        }
        if (Doc.AddAsset(asset, new Vec3(0, 0, 0)) && Doc.SelectedId is { } id) FrameEntity(id);
    }

    public async Task<bool> OpenScene(string path)
    {
        if (Doc.Playing) TogglePlay();
        if (path == Doc.Scene.Path || !await ConfirmDiscard()) return false;
        var ok = Doc.OpenScene(path);
        if (ok) ViewportView?.Viewport.Frame(null);
        return ok;
    }

    private async Task NewScene()
    {
        if (Doc.Playing) TogglePlay();
        if (!await ConfirmDiscard()) return;
        var name = await Dialogs.Prompt(this, "New Scene", "Scene name (saved in Scenes/)", "Main");
        if (name == null) return;
        var safe = string.Concat(name.Select(c => Path.GetInvalidFileNameChars().Contains(c) || c == '/' ? '_' : c));
        var path = $"Scenes/{safe}.noorscene";
        if (Doc.SceneFiles.Contains(path))
        {
            await Dialogs.Ask(this, "Scene exists", $"{path} already exists. Open it from File ▸ Open Scene.", "OK");
            return;
        }
        if (Doc.NewScene(path, name)) Doc.Save();
    }

    private void DeleteSelected()
    {
        if (Doc.Selected is { FromPrefab: false } e) Doc.Remove(e.Id);
    }

    private void FrameEntity(string id) => ViewportView?.Viewport.Frame(Doc.Subtree(id));

    private void FrameSelectedOrAll()
    {
        if (Doc.SelectedId is { } id) FrameEntity(id);
        else ViewportView?.Viewport.Frame(null);
    }

    // ---- File ▸ Build Game ----

    private bool _building;

    /// <summary>Builds the game into <paramref name="outputDir"/>, starting with the open scene.</summary>
    public async Task<GameBuilder.Result> BuildGame(string outputDir)
    {
        if (_building) return new GameBuilder.Result(false, null, "A build is already running.");
        _building = true;
        try
        {
            return await GameBuilder.BuildAsync(Doc.ProjectRoot, outputDir, Doc.Scene.Open ? Doc.Scene.Path : null);
        }
        finally
        {
            _building = false;
        }
    }

    private async Task BuildGameInteractive()
    {
        if (Doc.Playing) TogglePlay();
        SaveScripts();
        if (Doc.Scene.Modified)
        {
            var answer = await Dialogs.Ask(this, "Unsaved changes",
                $"The build uses the files on disk. Save {Doc.Scene.Path} first?", "Save and build", "Build without saving", "Cancel");
            if (answer == "Save and build") Doc.Save();
            else if (answer != "Build without saving") return;
        }
        var parent = Directory.GetParent(Doc.ProjectRoot)?.FullName;
        var start = parent != null ? await StorageProvider.TryGetFolderFromPathAsync(parent) : null;
        var folders = await StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions
        {
            Title = "Where should the game go? (an empty folder, or an earlier build to replace)",
            SuggestedStartLocation = start,
        });
        if (folders.Count == 0 || folders[0].TryGetLocalPath() is not { } outputDir) return;
        var result = await BuildGame(outputDir);
        if (!result.Ok)
        {
            await Dialogs.Ask(this, "Build failed", result.Message, "OK");
            return;
        }
        var choice = await Dialogs.Ask(this, "Game built",
            $"{result.Message} in\n{outputDir}\n\nDouble-click the .exe to play. Copy the whole folder to share it.", "Play it", "Show folder", "OK");
        if (choice == "Play it") Process.Start(new ProcessStartInfo(result.Executable!) { WorkingDirectory = outputDir, UseShellExecute = true });
        else if (choice == "Show folder") Process.Start(new ProcessStartInfo("explorer.exe", $"\"{outputDir}\"") { UseShellExecute = true });
    }

    /// <summary>True if it is fine to drop the open scene (saved, or the user chose not to save).</summary>
    private async Task<bool> ConfirmDiscard()
    {
        if (!Doc.Scene.Modified) return true;
        var answer = await Dialogs.Ask(this, "Unsaved changes", $"Save changes to {Doc.Scene.Path}?", "Save", "Don't save", "Cancel");
        return answer switch
        {
            "Save" => Doc.Save(),
            "Don't save" => true,
            _ => false,
        };
    }

    private async void OnClosing(object? sender, WindowClosingEventArgs e)
    {
        if (Doc.Playing) TogglePlay();
        if (_closeConfirmed || (!Doc.Scene.Modified && !_scripts.Any(s => s.Modified)))
        {
            RememberWindow();
            return;
        }
        e.Cancel = true;
        if (!await ConfirmDiscard() || !await ConfirmScripts()) return;
        _closeConfirmed = true;
        Close();
    }

    private void RememberWindow()
    {
        RememberLayout();
        _settings.WindowMaximized = WindowState == WindowState.Maximized;
        if (WindowState == WindowState.Normal)
        {
            _settings.WindowWidth = Width;
            _settings.WindowHeight = Height;
        }
        _settings.Save();
    }

    // ---- NoorCode (scripts) ----

    /// <summary>
    /// Opens a script (absolute or project-relative path) in NoorCode, or in the external editor when
    /// Preferences ▸ Scripts says so. <paramref name="line"/> (1-based, 0 = keep) puts the caret there.
    /// </summary>
    public ScriptDocument? OpenScript(string path, int line = 0)
    {
        var full = Path.GetFullPath(Path.IsPathRooted(path) ? path : Path.Combine(Doc.ProjectRoot, path));
        if (!_settings.Preferences.UseNoorCode)
        {
            CodeEditors.Open(_settings.Preferences, Doc.ProjectRoot, full, Math.Max(1, line));
            return null;
        }
        var relative = ScriptLocation.Relative(full, Doc.ProjectRoot);
        var script = _scripts.FirstOrDefault(s => string.Equals(s.RelativePath, relative, StringComparison.OrdinalIgnoreCase));
        if (script == null)
        {
            if (!File.Exists(full))
            {
                EngineLog.Warn("NoorCode", $"{relative} doesn't exist");
                return null;
            }
            script = new ScriptDocument(Doc.ProjectRoot, relative)
            {
                CreateView = s => new CodePanel(s, ExternalEditorName(), l => CodeEditors.Open(_settings.Preferences, Doc.ProjectRoot, s.FullPath, l)),
                ConfirmClose = ConfirmCloseScript,
            };
            // Errors already in the Console that are newer than the file are still true.
            var since = File.GetLastWriteTime(full);
            foreach (var l in EngineLog.Lines.Where(l => l.Level >= 4 && l.Time >= since)) RouteError(l, script);
            _scripts.Add(script);
            var documents = FindDock("Documents") ?? Docks(_rootDock).OfType<IDocumentDock>().FirstOrDefault();
            if (documents != null) DockFactory.AddDockable(documents, script);
        }
        if (script.Owner is IDock owner)
        {
            DockFactory.SetActiveDockable(script);
            DockFactory.SetFocusedDockable(owner, script);
        }
        if (line > 0) script.Reveal(line);
        return script;
    }

    /// <summary>Opens a material (project-relative .noormat path) in the material editor, as a tab.</summary>
    public MaterialDocument? OpenMaterial(string relativePath)
    {
        var material = _materials.FirstOrDefault(m => string.Equals(m.RelativePath, relativePath, StringComparison.OrdinalIgnoreCase));
        if (material == null)
        {
            if (!File.Exists(Path.Combine(Doc.ProjectRoot, relativePath)))
            {
                EngineLog.Warn("Materials", $"{relativePath} doesn't exist");
                return null;
            }
            material = new MaterialDocument(Doc, relativePath)
            {
                CreateView = m =>
                {
                    var panel = new MaterialPanel(m);
                    panel.SaveAsInstanceRequested += m2 => SaveMaterialInstance(m2);
                    panel.OpenRequested += p => OpenMaterial(p);
                    return panel;
                },
            };
            _materials.Add(material);
            var documents = FindDock("Documents") ?? Docks(_rootDock).OfType<IDocumentDock>().FirstOrDefault();
            if (documents != null) DockFactory.AddDockable(documents, material);
        }
        if (material.Owner is IDock owner)
        {
            DockFactory.SetActiveDockable(material);
            DockFactory.SetFocusedDockable(owner, material);
        }
        return material;
    }

    /// <summary>Opens a texture's import settings (project-relative path) as a tab.</summary>
    /// <summary>Opens a procedural graph in the graph editor (a tab; Milestone 7, part 4).</summary>
    public GraphDocument? OpenGraph(string relativePath)
    {
        var graph = _graphs.FirstOrDefault(g => string.Equals(g.RelativePath, relativePath, StringComparison.OrdinalIgnoreCase));
        if (graph == null)
        {
            if (!File.Exists(Path.Combine(Doc.ProjectRoot, relativePath)))
            {
                EngineLog.Warn("Graphs", $"{relativePath} doesn't exist");
                return null;
            }
            graph = new GraphDocument(Doc, relativePath) { CreateView = g => new GraphPanel(g) };
            _graphs.Add(graph);
            var documents = FindDock("Documents") ?? Docks(_rootDock).OfType<IDocumentDock>().FirstOrDefault();
            if (documents != null) DockFactory.AddDockable(documents, graph);
        }
        if (graph.Owner is IDock owner)
        {
            DockFactory.SetActiveDockable(graph);
            DockFactory.SetFocusedDockable(owner, graph);
        }
        return graph;
    }

    /// <summary>File > New Procedural Graph: Assets/Graphs/name.noorgraph, empty, opened.</summary>
    public GraphDocument? NewGraph(string name)
    {
        var safe = string.Concat(name.Trim().Select(c => Path.GetInvalidFileNameChars().Contains(c) ? '_' : c));
        if (safe.Length == 0) return null;
        var relative = $"Assets/Graphs/{safe}.noorgraph";
        var full = Path.Combine(Doc.ProjectRoot, relative);
        if (!File.Exists(full))
        {
            Directory.CreateDirectory(Path.GetDirectoryName(full)!);
            File.WriteAllText(full, GraphDocument.NewGraph().ToJsonString(new System.Text.Json.JsonSerializerOptions { WriteIndented = true }));
            Doc.RefreshAssets(true);
        }
        return OpenGraph(relative);
    }

    public TextureDocument? OpenTexture(string relativePath)
    {
        var texture = _textures.FirstOrDefault(t => string.Equals(t.RelativePath, relativePath, StringComparison.OrdinalIgnoreCase));
        if (texture == null)
        {
            if (!File.Exists(Path.Combine(Doc.ProjectRoot, relativePath)))
            {
                EngineLog.Warn("Textures", $"{relativePath} doesn't exist");
                return null;
            }
            texture = new TextureDocument(Doc, relativePath) { CreateView = t => new TexturePanel(t, p => OpenSprite(p)) };
            _textures.Add(texture);
            var documents = FindDock("Documents") ?? Docks(_rootDock).OfType<IDocumentDock>().FirstOrDefault();
            if (documents != null) DockFactory.AddDockable(documents, texture);
        }
        if (texture.Owner is IDock owner)
        {
            DockFactory.SetActiveDockable(texture);
            DockFactory.SetFocusedDockable(owner, texture);
        }
        return texture;
    }

    /// <summary>
    /// The Animation panel brought to the front (Milestone 8C, part 6), showing the first character whose animator runs
    /// this controller (Edit on a .nooranim). Its tab sits beside the Content Browser, so the viewport stays in view.
    /// </summary>
    public AnimationPanel? ShowAnimationPanel()
    {
        // Its graph and bone list want more height than the bottom dock's default: make room once (resizable after).
        if (FindDock("Bottom") is { } bottom && FindDock("Documents") is { } documents && bottom.Proportion is > 0 and < 0.42)
        {
            documents.Proportion = 1 - 0.42;
            bottom.Proportion = 0.42;
        }
        if (FindDockable("Animation") is { } tab && tab.Owner is IDock owner)
        {
            DockFactory.SetActiveDockable(tab);
            DockFactory.SetFocusedDockable(owner, tab);
        }
        return Animation;
    }

    public void ShowAnimationController(string relativePath)
    {
        ShowAnimationPanel();
        Dispatcher.UIThread.Post(() =>
        {
            if (Animation?.ShowController(relativePath) == false)
                EngineLog.Warn("Animation", $"No character in this scene runs {relativePath}: give an animator \"controller\": it, then edit it here");
        });
    }

    /// <summary>A sprite sheet's editor (Milestone 8B, part 5): its grid, animations and an animated preview, in a tab.</summary>
    public SpriteDocument? OpenSprite(string relativePath)
    {
        var sprite = _sprites.FirstOrDefault(s => string.Equals(s.RelativePath, relativePath, StringComparison.OrdinalIgnoreCase));
        if (sprite == null)
        {
            if (!File.Exists(Path.Combine(Doc.ProjectRoot, relativePath)))
            {
                EngineLog.Warn("Sprites", $"{relativePath} doesn't exist");
                return null;
            }
            sprite = new SpriteDocument(Doc, relativePath) { CreateView = s => new SpritePanel(s) };
            _sprites.Add(sprite);
            var documents = FindDock("Documents") ?? Docks(_rootDock).OfType<IDocumentDock>().FirstOrDefault();
            if (documents != null) DockFactory.AddDockable(documents, sprite);
        }
        if (sprite.Owner is IDock owner)
        {
            DockFactory.SetActiveDockable(sprite);
            DockFactory.SetFocusedDockable(owner, sprite);
        }
        return sprite;
    }

    /// <summary>Save as instance: <c>name_variant.noormat</c> beside the material, changing nothing yet; opens it.</summary>
    public MaterialDocument? SaveMaterialInstance(MaterialDocument parent)
    {
        parent.Flush();
        if (parent.Asset is not { } asset)
        {
            EngineLog.Error("Materials", $"{parent.RelativePath} isn't an imported asset yet (Rescan)");
            return null;
        }
        var folder = Path.GetDirectoryName(parent.RelativePath)?.Replace(Path.DirectorySeparatorChar, '/') ?? "";
        var stem = Path.GetFileNameWithoutExtension(parent.RelativePath);
        string relative;
        var n = 1;
        do relative = $"{folder}/{stem}_variant{(n == 1 ? "" : n.ToString())}.noormat".TrimStart('/');
        while (File.Exists(Path.Combine(Doc.ProjectRoot, relative)) && ++n < 1000);
        try
        {
            File.WriteAllText(Path.Combine(Doc.ProjectRoot, relative), MaterialFile.InstanceText(asset.Guid));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            EngineLog.Error("Materials", $"Couldn't write {relative}: {e.Message}");
            return null;
        }
        Doc.RefreshAssets(rescan: true);
        EngineLog.Info("Materials", $"Made {relative}, an instance of {parent.RelativePath}");
        return OpenMaterial(relative);
    }

    /// <summary>The external editor's name for NoorCode's "Open in …" button.</summary>
    private string ExternalEditorName()
    {
        var program = _settings.Preferences.CodeEditor;
        if (string.IsNullOrWhiteSpace(program)) return "default app";
        return CodeEditors.Detect().FirstOrDefault(e => string.Equals(e.Program, program, StringComparison.OrdinalIgnoreCase))?.Name
               ?? Path.GetFileNameWithoutExtension(program);
    }

    /// <summary>Saves every script with unsaved changes (Play and Build use the files on disk).</summary>
    public void SaveScripts()
    {
        foreach (var script in _scripts.Where(s => s.Modified).ToList()) script.Save();
    }

    private async Task ConfirmCloseScript(ScriptDocument script)
    {
        var answer = await Dialogs.Ask(this, "Unsaved changes", $"Save changes to {script.RelativePath}?", "Save", "Don't save", "Cancel");
        if (answer is not ("Save" or "Don't save")) return;
        if (answer == "Save" && !script.Save()) return;
        script.AcceptClose();
        DockFactory.CloseDockable(script);
    }

    /// <summary>True if every open script is saved, or the user chose what to do with the unsaved ones.</summary>
    private async Task<bool> ConfirmScripts()
    {
        var unsaved = _scripts.Where(s => s.Modified).ToList();
        if (unsaved.Count == 0) return true;
        var names = string.Join(Environment.NewLine, unsaved.Select(s => "  " + s.RelativePath));
        var answer = await Dialogs.Ask(this, "Unsaved scripts", $"Save changes to these scripts?{Environment.NewLine}{names}", "Save all", "Don't save", "Cancel");
        if (answer == "Save all") return unsaved.All(s => s.Save());
        return answer == "Don't save";
    }

    // Errors in the Console that name a line of an open script mark that line in NoorCode.
    private void OnLogLines(object? sender, System.Collections.Specialized.NotifyCollectionChangedEventArgs e)
    {
        if (e.NewItems == null || _scripts.Count == 0) return;
        foreach (LogLine line in e.NewItems)
            if (line.Level >= 4)
                foreach (var script in _scripts) RouteError(line, script);
    }

    private void RouteError(LogLine line, ScriptDocument script)
    {
        if (ScriptLocation.Find(line.Message, Doc.ProjectRoot) is not { } at ||
            !string.Equals(at.RelativePath, script.RelativePath, StringComparison.OrdinalIgnoreCase)) return;
        var message = line.Message.Split('\n')[0];
        var prefix = $"{at.RelativePath}:{at.Line}:";
        if (message.StartsWith(prefix, StringComparison.Ordinal)) message = message[prefix.Length..].Trim();
        script.AddError(at.Line, message);
    }

    // ---- Keyboard ----
    // Keys pressed while the engine viewport has focus arrive here too: the viewport's child window
    // forwards them to this window. Text boxes keep their own Ctrl+Z/Y and letters.

    private void OnKeyDown(object? sender, KeyEventArgs e)
    {
        var focused = ViewportHasFocus() ? null : FocusManager?.GetFocusedElement();
        var inCode = (focused as Visual)?.FindAncestorOfType<CodePanel>(includeSelf: true);
        var inText = focused is TextBox || inCode != null;
        var inMaterial = (focused as Visual)?.FindAncestorOfType<MaterialPanel>(includeSelf: true);
        var inGraph = (focused as Visual)?.FindAncestorOfType<GraphPanel>(includeSelf: true);
        // While playing, keys typed into the viewport belong to the game: only Ctrl shortcuts
        // (Ctrl+P stop, Ctrl+S save) reach the editor.
        if (Doc.Playing && ViewportHasFocus() && !e.KeyModifiers.HasFlag(KeyModifiers.Control))
        {
            e.Handled = true;  // the game's key: must not reach a focused editor button (Space would click it)
            return;
        }
        var ctrl = e.KeyModifiers.HasFlag(KeyModifiers.Control);
        var shift = e.KeyModifiers.HasFlag(KeyModifiers.Shift);
        switch (e.Key)
        {
            case Key.P when ctrl:
                TogglePlay();
                break;
            case Key.S when ctrl && inCode != null:
                inCode.Script.Save();
                break;
            case Key.S when ctrl:
                Save();
                break;
            case Key.Up when e.KeyModifiers == KeyModifiers.Alt && !inText:
                MoveSelected(-1);
                break;
            case Key.Down when e.KeyModifiers == KeyModifiers.Alt && !inText:
                MoveSelected(1);
                break;
            case Key.Z when ctrl && !shift && !inText && inGraph != null:
                inGraph.Graph.Undo();
                break;
            case Key.Y when ctrl && !inText && inGraph != null:
            case Key.Z when ctrl && shift && !inText && inGraph != null:
                inGraph.Graph.Redo();
                break;
            case Key.Z when ctrl && !shift && !inText && inMaterial != null:
                inMaterial.Material.Undo();
                break;
            case Key.Y when ctrl && !inText && inMaterial != null:
            case Key.Z when ctrl && shift && !inText && inMaterial != null:
                inMaterial.Material.Redo();
                break;
            case Key.Z when ctrl && !shift && !inText:
                Doc.Undo();
                break;
            case Key.Y when ctrl && !inText:
            case Key.Z when ctrl && shift && !inText:
                Doc.Redo();
                break;
            case Key.F when e.KeyModifiers == KeyModifiers.None && !inText:
                FrameSelectedOrAll();
                break;
            case Key.Q when e.KeyModifiers == KeyModifiers.None && !inText:
                Doc.SetTool(EditorTool.Select);
                break;
            case Key.W when e.KeyModifiers == KeyModifiers.None && !inText:
                Doc.SetTool(EditorTool.Move);
                break;
            case Key.E when e.KeyModifiers == KeyModifiers.None && !inText:
                Doc.SetTool(EditorTool.Rotate);
                break;
            case Key.R when e.KeyModifiers == KeyModifiers.None && !inText:
                Doc.SetTool(EditorTool.Scale);
                break;
            case Key.Delete when !inText && ViewportHasFocus():
                DeleteSelected();
                break;
            default:
                // Keys typed into the viewport are never meant for whichever editor control last had
                // Avalonia focus (Space/Enter would click a button).
                if (ViewportHasFocus()) e.Handled = true;
                return;
        }
        e.Handled = true;
    }

    public bool ViewportHasFocus()
    {
        var vp = ViewportView?.Viewport.EngineHandle ?? IntPtr.Zero;
        return vp != IntPtr.Zero && Native.noor_viewport_has_focus(vp) == 1;
    }

}
