using System.Text.Json;
using System.Text.Json.Nodes;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Platform.Storage;
using NoorEditor.Model;
using NoorEditor.Shell;

namespace NoorEditor.Panels;

/// <summary>
/// The Inspector's component list. Components are JSON objects owned by the engine; this edits
/// their simple fields (numbers, text, true/false, short number lists) generically, adds and
/// removes components and fields. Every change is one set_component command (one undo step).
/// Engine-managed components (model, mesh, prefab) are shown read-only: their fields are asset
/// references and override tables that need dedicated editors.
/// </summary>
public sealed class ComponentsEditor : StackPanel
{
    private static readonly HashSet<string> SystemComponents = ["model", "mesh", "prefab"];
    private static readonly JsonSerializerOptions Pretty = new() { WriteIndented = true };

    private readonly EditorDocument _doc;
    private EntityInfo? _entity;

    public ComponentsEditor(EditorDocument doc)
    {
        _doc = doc;
        Spacing = 8;
    }

    public void Show(EntityInfo entity)
    {
        _entity = entity;
        Children.Clear();
        var editable = !entity.FromPrefab && !_doc.Playing;
        foreach (var (type, value) in entity.Components)
            Children.Add(Card(type, value, editable && !SystemComponents.Contains(type)));
        if (entity.Components.Count == 0)
            Children.Add(new TextBlock { Text = "No components.", FontSize = 11, Classes = { "muted" } });
        if (editable) Children.Add(AddComponentButton());
    }

    // ---- Terrain sculpting (Milestone 6, part 6) ----

    private Control SculptEditor()
    {
        var brush = _doc.Brush;
        var panel = new StackPanel { Spacing = 4, Margin = new Thickness(0, 0, 0, 6) };
        var toggle = new Avalonia.Controls.Primitives.ToggleButton
        {
            Content = _doc.Sculpting ? "Sculpting: drag on the terrain" : "Sculpt", IsChecked = _doc.Sculpting,
            Classes = { "tool" }, HorizontalAlignment = HorizontalAlignment.Stretch,
        };
        ToolTip.SetTip(toggle, "Left-drag on the terrain to sculpt it (Q/W/E/R put the brush down)");
        toggle.Click += (_, _) => _doc.SetSculpting(toggle.IsChecked == true);
        var paintToggle = PaintToggle("Left-drag on the terrain to paint the material (up to four per terrain)");
        var modes = new Avalonia.Controls.Primitives.UniformGrid { Rows = 1, Columns = 2, Children = { toggle, paintToggle } };
        panel.Children.Add(modes);
        if (_doc.Painting) panel.Children.Add(PaintMaterialPicker());

        var tools = new Avalonia.Controls.Primitives.UniformGrid { Rows = 1, Columns = EditorDocument.SculptTools.Length, IsVisible = !_doc.Painting };
        foreach (var tool in EditorDocument.SculptTools)
        {
            var button = new Avalonia.Controls.Primitives.ToggleButton { Content = char.ToUpper(tool[0]) + tool[1..], IsChecked = brush.Tool == tool, Classes = { "tool" },
                                            HorizontalAlignment = HorizontalAlignment.Stretch, Margin = new Thickness(1, 0) };
            ToolTip.SetTip(button, tool switch
            {
                "raise" => "Push the ground up (strength: metres per second at the centre)",
                "lower" => "Push the ground down",
                "smooth" => "Even out bumps (strength: how fast)",
                _ => "Level the ground to the height where the stroke starts",
            });
            button.Click += (_, _) => { _doc.SetBrush(_doc.Brush with { Tool = tool }); _doc.SetSculpting(true); };
            tools.Children.Add(button);
        }
        panel.Children.Add(tools);

        Control Row(string label, double value, Func<double, EditorDocument.SculptBrush> make, string tip)
        {
            var text = new TextBlock { Text = label, FontSize = 12, Classes = { "muted" }, VerticalAlignment = VerticalAlignment.Center };
            var box = NumberBox(value, x => _doc.SetBrush(make(x)));
            ToolTip.SetTip(box, tip);
            var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("96,*") };
            Grid.SetColumn(box, 1);
            grid.Children.Add(text);
            grid.Children.Add(box);
            return grid;
        }
        panel.Children.Add(Row("Radius (m)", brush.Radius, x => _doc.Brush with { Radius = Math.Clamp(x, 0.5, 200) }, "How big the brush is"));
        panel.Children.Add(Row("Strength", brush.Strength, x => _doc.Brush with { Strength = Math.Clamp(x, 0.05, 100) },
                               "Raise/Lower/Flatten: metres per second at the centre. Smooth and Paint: how fast (1 = all the way in a second)"));
        panel.Children.Add(Row("Falloff", brush.Falloff, x => _doc.Brush with { Falloff = Math.Clamp(x, 0, 1) }, "0: a hard edge. 1: soft from the centre out"));
        panel.Children.Add(new TextBlock
        {
            Text = "Each stroke is one undo step. Sculpting and paint sit on top of the generated hills, so changing the seed keeps them. Save writes them to Assets/Terrain.",
            FontSize = 10, TextWrapping = TextWrapping.Wrap, Classes = { "muted" },
        });
        panel.Children.Add(new Border { Height = 1, Background = NoorTheme.Brush(NoorTheme.Border), Margin = new Thickness(0, 4) });
        return panel;
    }

    // ---- Scatter (Milestone 7, part 7): the hand-made layer and its brush ----

    private Control ScatterEditor(JsonObject scatter)
    {
        var panel = new StackPanel { Spacing = 4, Margin = new Thickness(0, 4, 0, 0) };
        var removed = (scatter["removed"] as JsonArray)?.Count ?? 0;
        var added = (scatter["added"] as JsonArray)?.Count ?? 0;
        panel.Children.Add(new TextBlock
        {
            Text = $"By hand: {removed} removed · {added} planted. Regenerating the graph (a new seed, new settings) keeps these.",
            FontSize = 11, TextWrapping = TextWrapping.Wrap, Classes = { "muted" },
        });
        var groundId = scatter["ground"]?.GetValue<string>();
        var hasGround = groundId != null && _doc.Scene.Entities.Any(e => e.Id == groundId && e.Components["terrain"] != null);
        Avalonia.Controls.Primitives.ToggleButton Tool(string tool, string label, string tip)
        {
            var b = new Avalonia.Controls.Primitives.ToggleButton { Content = label, IsChecked = _doc.Scattering == tool, IsEnabled = hasGround, Classes = { "tool" },
                                                                    HorizontalAlignment = HorizontalAlignment.Stretch, Margin = new Thickness(1, 0) };
            ToolTip.SetTip(b, tip);
            b.Click += (_, _) => _doc.SetScattering(b.IsChecked == true ? tool : null);
            return b;
        }
        panel.Children.Add(new Avalonia.Controls.Primitives.UniformGrid
        {
            Rows = 1, Columns = 2,
            Children =
            {
                Tool("plant", _doc.Scattering == "plant" ? "Planting: drag" : "Plant", "Left-drag on the ground: a tree every brush radius"),
                Tool("remove", _doc.Scattering == "remove" ? "Removing: drag" : "Remove", "Left-drag on the ground: trees within the brush go"),
            },
        });
        if (!hasGround)
            panel.Children.Add(new TextBlock { Text = "Choose a terrain as the ground to plant and remove trees with the brush.", FontSize = 10,
                                               TextWrapping = TextWrapping.Wrap, Classes = { "muted" } });
        if (_doc.Scattering != null)
        {
            var text = new TextBlock { Text = "Radius (m)", FontSize = 12, Classes = { "muted" }, VerticalAlignment = VerticalAlignment.Center };
            var box = NumberBox(_doc.Brush.Radius, x => _doc.SetBrush(_doc.Brush with { Radius = Math.Clamp(x, 0.5, 200) }));
            var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("96,*") };
            Grid.SetColumn(box, 1);
            grid.Children.Add(text);
            grid.Children.Add(box);
            panel.Children.Add(grid);
        }
        if (_doc.Scattering == "plant")
        {
            var models = _doc.Assets.Where(a => a.Importer is "gltf" or "fbx").ToList();
            var items = new List<string> { "(the scatter's own)" };
            items.AddRange(models.Select(m => m.Path));
            var current = models.FirstOrDefault(m => m.Guid == _doc.ScatterAsset)?.Path ?? "(the scatter's own)";
            var combo = new ComboBox { ItemsSource = items, SelectedItem = current, Classes = { "field" }, HorizontalAlignment = HorizontalAlignment.Stretch };
            ToolTip.SetTip(combo, "The model to plant");
            combo.SelectionChanged += (_, _) =>
            {
                if (combo.SelectedItem is string path && path != current) _doc.SetScatterAsset(models.FirstOrDefault(m => m.Path == path)?.Guid);
            };
            panel.Children.Add(combo);
        }
        return panel;
    }

    // ---- Painting (Milestone 6, part 7) ----

    private Avalonia.Controls.Primitives.ToggleButton PaintToggle(string tip)
    {
        var toggle = new Avalonia.Controls.Primitives.ToggleButton
        {
            Content = _doc.Painting ? "Painting" : "Paint", IsChecked = _doc.Painting, Classes = { "tool" }, HorizontalAlignment = HorizontalAlignment.Stretch,
        };
        ToolTip.SetTip(toggle, tip);
        toggle.Click += (_, _) =>
        {
            if (toggle.IsChecked == true && _doc.PaintMaterial == null)
                _doc.SetPaintMaterial(_doc.Assets.FirstOrDefault(a => a.Importer == "material")?.Guid);
            _doc.SetPainting(toggle.IsChecked == true);
        };
        return toggle;
    }

    private Control PaintMaterialPicker()
    {
        var materials = _doc.Assets.Where(a => a.Importer == "material").ToList();
        var items = materials.Select(m => m.Path).ToList();
        var current = materials.FirstOrDefault(m => m.Guid == _doc.PaintMaterial)?.Path;
        var combo = new ComboBox { ItemsSource = items, SelectedItem = current, Classes = { "field" }, HorizontalAlignment = HorizontalAlignment.Stretch,
                                   PlaceholderText = materials.Count == 0 ? "No materials in the project yet" : "Pick a material" };
        ToolTip.SetTip(combo, "The material to paint");
        combo.SelectionChanged += (_, _) =>
        {
            if (combo.SelectedItem is string path && path != current) _doc.SetPaintMaterial(materials.First(m => m.Path == path).Guid);
        };
        return combo;
    }

    /// <summary>A terrain's material layers: up to four, blended by the paint (layer 0 is the ground underneath).</summary>
    private Control LayersEditor(JsonObject terrain)
    {
        var panel = new StackPanel { Spacing = 3, Margin = new Thickness(0, 6, 0, 0) };
        panel.Children.Add(new TextBlock { Text = "Material layers", Classes = { "section" } });
        var layers = terrain["layers"] as JsonArray;
        if (layers == null || layers.Count == 0)
        {
            panel.Children.Add(new TextBlock
            {
                Text = "One layer: \"material\" above. Painting another material adds a layer (up to four).",
                FontSize = 10, Classes = { "muted" }, TextWrapping = TextWrapping.Wrap,
            });
            return panel;
        }
        var materials = _doc.Assets.Where(a => a.Importer == "material").ToList();
        for (var i = 0; i < layers.Count && i < 4; i++)
        {
            var index = i;
            var layer = layers[i] as JsonObject ?? new JsonObject();
            var id = layer["material"]?.GetValue<string>();
            var items = new List<string> { "(default)" };
            items.AddRange(materials.Select(m => m.Path));
            var selected = id == null ? "(default)" : materials.FirstOrDefault(m => m.Guid == id)?.Path ?? "(missing material)";
            if (!items.Contains(selected)) items.Add(selected);
            var combo = new ComboBox { ItemsSource = items, SelectedItem = selected, Classes = { "field" }, HorizontalAlignment = HorizontalAlignment.Stretch };
            combo.SelectionChanged += (_, _) =>
            {
                if (combo.SelectedItem is not string path || path == selected || path == "(missing material)") return;
                SetLayer(index, l => l["material"] = path == "(default)" ? null : materials.First(m => m.Path == path).Guid);
            };
            var tiling = NumberBox(layer["tiling"]?.GetValue<double>() ?? 1, x => SetLayer(index, l => l["tiling"] = Math.Clamp(x, 0.001, 1000)));
            ToolTip.SetTip(tiling, "Tiling: how many times the material repeats per metre of its own size (2 = twice as small)");
            tiling.Width = 54;
            var row = new DockPanel { Children = { new TextBlock { Text = $"{index}", Width = 16, FontSize = 12, Classes = { "muted" }, VerticalAlignment = VerticalAlignment.Center }, tiling, combo } };
            DockPanel.SetDock(row.Children[0], Avalonia.Controls.Dock.Left);
            DockPanel.SetDock(tiling, Avalonia.Controls.Dock.Right);
            panel.Children.Add(row);
        }
        return panel;

        void SetLayer(int index, Action<JsonObject> change)
        {
            if (_entity?.Components["terrain"] is not JsonObject current || current["layers"] is not JsonArray list || index >= list.Count) return;
            var next = (JsonObject)current.DeepClone();
            var nextLayers = (JsonArray)next["layers"]!;
            var layer = nextLayers[index] as JsonObject ?? new JsonObject();
            change(layer);
            nextLayers[index] = layer.DeepClone();
            SetComponent("terrain", next);
        }
    }

    // ---- CSG brush faces (Milestone 6, part 3) ----

    private static readonly HashSet<int> s_expandedFaces = [];

    /// <summary>
    /// A brush's faces: each can take its own material and UV settings (the brush's otherwise).
    /// The face picked in the viewport is first, highlighted and open.
    /// </summary>
    private Control FacesEditor(JsonObject brush)
    {
        var shape = brush["shape"]?.GetValue<string>() ?? "box";
        var names = EntityTemplates.BrushFaces(shape);
        var faces = brush["faces"] as JsonArray;
        var panel = new StackPanel { Spacing = 3, Margin = new Thickness(0, 6, 0, 0) };
        panel.Children.Add(new TextBlock { Text = "Faces", Classes = { "section" } });
        // The Paint tool works on faces too: click faces of brushes to give them the material.
        panel.Children.Add(PaintToggle("Click faces of brushes in the viewport to give them the material"));
        if (_doc.Painting) panel.Children.Add(PaintMaterialPicker());
        panel.Children.Add(new TextBlock
        {
            Text = "Click a face in the viewport (on the selected brush) to pick it. A face without its own settings uses the brush's.",
            FontSize = 10, Classes = { "muted" }, TextWrapping = TextWrapping.Wrap,
        });
        var order = Enumerable.Range(0, names.Length).ToList();
        if (_doc.SelectedFace is { } picked && picked < names.Length)
        {
            order.Remove(picked);
            order.Insert(0, picked);
            s_expandedFaces.Add(picked);
        }
        foreach (var f in order)
            panel.Children.Add(FaceRow(brush, faces, f, names[f], f == _doc.SelectedFace));
        return panel;
    }

    private JsonObject? FaceOf(JsonArray? faces, int face) => faces != null && face < faces.Count ? faces[face] as JsonObject : null;

    /// <summary>Writes one face's settings (null: back to the brush's) into the brush's "faces" list.</summary>
    public bool SetFace(int face, JsonObject? settings)
    {
        if (_entity?.Components["brush"] is not JsonObject brush) return false;
        var count = EntityTemplates.BrushFaces(brush["shape"]?.GetValue<string>() ?? "box").Length;
        var faces = new JsonArray();
        var old = brush["faces"] as JsonArray;
        for (var i = 0; i < count; i++) faces.Add(i == face ? settings?.DeepClone() : FaceOf(old, i)?.DeepClone());
        var next = (JsonObject)brush.DeepClone();
        if (faces.All(x => x == null)) next.Remove("faces");
        else next["faces"] = faces;
        return SetComponent("brush", next);
    }

    private Control FaceRow(JsonObject brush, JsonArray? faces, int face, string name, bool picked)
    {
        var own = FaceOf(faces, face);
        var materials = _doc.Assets.Where(a => a.Importer == "material").ToList();
        string Path(string? guid) => guid == null ? "(none)" : materials.FirstOrDefault(m => m.Guid == guid)?.Path ?? "(missing material)";
        var brushMaterial = brush["material"] is JsonValue bm && bm.TryGetValue<string>(out var bmg) ? bmg : null;
        const string Inherit = "(the brush's)";
        var items = new List<string> { Inherit, "(none)" };
        items.AddRange(materials.Select(m => m.Path));
        var current = own == null || !own.ContainsKey("material") ? Inherit : Path(own["material"] is JsonValue v && v.TryGetValue<string>(out var g) ? g : null);
        var combo = new ComboBox { ItemsSource = items, SelectedItem = items.Contains(current) ? current : Inherit, Classes = { "field" }, HorizontalAlignment = HorizontalAlignment.Stretch };
        combo.SelectionChanged += (_, _) =>
        {
            if (combo.SelectedItem is not string choice || choice == current) return;
            var next = (JsonObject?)own?.DeepClone() ?? new JsonObject();
            if (choice == Inherit) next.Remove("material");
            else next["material"] = choice == "(none)" ? null : JsonValue.Create(materials.First(m => m.Path == choice).Guid);
            SetFace(face, next.Count == 0 ? null : next);
        };
        var label = new TextBlock
        {
            Text = name, FontSize = 12, VerticalAlignment = VerticalAlignment.Center, TextTrimming = TextTrimming.CharacterEllipsis,
            Foreground = NoorTheme.Brush(picked ? NoorTheme.Cyan : NoorTheme.Text), FontWeight = picked ? FontWeight.SemiBold : FontWeight.Normal,
        };
        var more = new Avalonia.Controls.Primitives.ToggleButton { Content = "UV", Classes = { "tool" }, Padding = new Thickness(5, 0), Margin = new Thickness(3, 0, 0, 0),
                                      IsChecked = s_expandedFaces.Contains(face) };
        ToolTip.SetTip(more, "Texture placement on this face");
        var head = new Grid { ColumnDefinitions = new ColumnDefinitions("96,*,Auto") };
        Grid.SetColumn(combo, 1);
        Grid.SetColumn(more, 2);
        head.Children.Add(label);
        head.Children.Add(combo);
        head.Children.Add(more);
        var block = new StackPanel { Spacing = 2, Children = { head } };
        var details = UvEditor(brush, own, face, materials, own != null && own.ContainsKey("material") ? (own["material"] as JsonValue)?.GetValue<string>() : brushMaterial);
        details.IsVisible = more.IsChecked == true;
        more.IsCheckedChanged += (_, _) =>
        {
            details.IsVisible = more.IsChecked == true;
            if (more.IsChecked == true) s_expandedFaces.Add(face);
            else s_expandedFaces.Remove(face);
        };
        block.Children.Add(details);
        return new Border
        {
            Background = picked ? NoorTheme.Brush(NoorTheme.PanelRaised) : Brushes.Transparent,
            BorderBrush = picked ? NoorTheme.Brush(NoorTheme.Cyan) : Brushes.Transparent,
            BorderThickness = new Thickness(picked ? 1 : 0), CornerRadius = new CornerRadius(4), Padding = new Thickness(4, 2),
            Child = block,
        };
    }

    private Control UvEditor(JsonObject brush, JsonObject? own, int face, List<AssetInfo> materials, string? materialGuid)
    {
        double Get(string key, int i, double fallback)
        {
            var node = own?[key] ?? brush[key];
            if (node is JsonArray a && i < a.Count && a[i] is JsonNode n && Num.TryParse(n.ToJsonString(), out var x)) return x;
            if (node is JsonValue && i == 0 && Num.TryParse(node.ToJsonString(), out var y)) return y;
            return fallback;
        }
        void Set(string key, JsonNode value)
        {
            var next = (JsonObject?)own?.DeepClone() ?? new JsonObject();
            next[key] = value;
            SetFace(face, next);
        }
        Control Pair(string key, double d)
        {
            var a = Get(key, 0, d);
            var b = Get(key, 1, d);
            var row = new Avalonia.Controls.Primitives.UniformGrid { Rows = 1, Columns = 2 };
            row.Children.Add(NumberBox(a, x => Set(key, new JsonArray(x, b))));
            row.Children.Add(NumberBox(b, x => Set(key, new JsonArray(a, x))));
            return row;
        }
        var align = new ComboBox { ItemsSource = new[] { "world", "brush" }, Classes = { "field" }, HorizontalAlignment = HorizontalAlignment.Stretch };
        align.SelectedItem = (own?["uv_align"] ?? brush["uv_align"])?.GetValue<string>() == "brush" ? "brush" : "world";
        align.SelectionChanged += (_, _) => { if (align.SelectedItem is string s && s != ((own?["uv_align"] ?? brush["uv_align"])?.GetValue<string>() ?? "world")) Set("uv_align", s); };
        ToolTip.SetTip(align, "world: the texture stays put in space (cuts line up). brush: it turns with the brush (rotated walls keep level rows)");
        var reset = new Button { Content = "Use the brush's", Classes = { "tool" }, HorizontalAlignment = HorizontalAlignment.Left, IsEnabled = own != null };
        reset.Click += (_, _) => SetFace(face, null);

        var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("96,*"), Margin = new Thickness(8, 2, 0, 4) };
        var rows = new (string Label, Control Editor)[]
        {
            ("Repeats / m", Pair("uv_scale", 1)), ("Offset", Pair("uv_offset", 0)),
            ("Rotation °", NumberBox(Get("uv_rotation", 0, 0), x => Set("uv_rotation", x))), ("Aligned to", align),
        };
        for (var i = 0; i < rows.Length; i++)
        {
            grid.RowDefinitions.Add(new RowDefinition(GridLength.Auto));
            var l = new TextBlock { Text = rows[i].Label, FontSize = 11, Classes = { "muted" }, VerticalAlignment = VerticalAlignment.Center };
            Grid.SetRow(l, i);
            Grid.SetRow(rows[i].Editor, i);
            Grid.SetColumn(rows[i].Editor, 1);
            grid.Children.Add(l);
            grid.Children.Add(rows[i].Editor);
        }
        var density = new TextBlock { FontSize = 10, Classes = { "muted" }, TextWrapping = TextWrapping.Wrap, Text = TexelDensity(materialGuid, Get("uv_scale", 0, 1)) };
        return new StackPanel { Spacing = 3, Children = { grid, density, reset } };
    }

    /// <summary>"1024 px/m (a 1024 px texture repeating every 1 m)": how sharp the face's texture is.</summary>
    private string TexelDensity(string? materialGuid, double repeatsPerMetre)
    {
        var every = repeatsPerMetre > 0 ? $"repeats every {Num.Format(1 / repeatsPerMetre)} m" : "doesn't repeat";
        var material = materialGuid == null ? null : _doc.Assets.FirstOrDefault(a => a.Guid == materialGuid);
        if (material == null) return $"Texture {every}.";
        var json = MaterialFile.Read(System.IO.Path.Combine(_doc.ProjectRoot, material.Path));
        var parent = json;
        for (var depth = 0; depth < 16 && parent != null && parent["base_color_map"] == null && parent["overrides"]?["base_color_map"] == null &&
                            parent["parent"] is JsonValue pid; depth++)
        {
            var p = _doc.Assets.FirstOrDefault(a => a.Guid == pid.GetValue<string>());
            parent = p == null ? null : MaterialFile.Read(System.IO.Path.Combine(_doc.ProjectRoot, p.Path));
        }
        var mapId = (parent?["overrides"]?["base_color_map"] ?? parent?["base_color_map"]) is JsonValue mv && mv.TryGetValue<string>(out var m) ? m : null;
        var texture = mapId == null ? null : _doc.Assets.FirstOrDefault(a => a.Guid == mapId);
        var info = texture == null ? null : JsonNode.Parse(Interop.Native.Texture(_doc.Handle, texture.Path) ?? "null");
        var width = info?["width"]?.GetValue<int>() ?? 0;
        return width > 0 ? $"{Num.Format(width * repeatsPerMetre)} px per metre (a {width} px texture that {every})." : $"Texture {every}.";
    }

    // ---- Commands (also used by the smoke test) ----

    public bool SetComponent(string type, JsonNode? value)
    {
        if (_entity == null) return false;
        return _doc.Execute(new JsonObject { ["op"] = "set_component", ["entity"] = _entity.Id, ["type"] = type, ["value"] = value?.DeepClone() });
    }

    /// <summary>Sets a field; <paramref name="path"/> may go into nested objects ("params.speed").</summary>
    public bool SetField(string type, string path, JsonNode? value)
    {
        if (_entity?.Components[type] is not JsonObject current) return false;
        return SetComponent(type, WithPath(current, path, value, remove: false));
    }

    private static JsonObject WithPath(JsonObject root, string path, JsonNode? value, bool remove)
    {
        var next = (JsonObject)root.DeepClone();
        var parts = path.Split('.');
        var node = next;
        foreach (var part in parts[..^1])
        {
            if (node[part] is not JsonObject child)
            {
                child = new JsonObject();
                node[part] = child;
            }
            node = child;
        }
        if (remove) node.Remove(parts[^1]);
        else node[parts[^1]] = value?.DeepClone();
        return next;
    }

    // ---- Lua scripts ----

    /// <summary>Creates Scripts/&lt;name&gt;.luau from a template (if missing) and attaches it.</summary>
    public bool AddLuaScript(string name)
    {
        var relative = $"Scripts/{name}.luau";
        var full = Path.Combine(_doc.ProjectRoot, relative);
        if (!File.Exists(full))
        {
            Directory.CreateDirectory(Path.GetDirectoryName(full)!);
            File.WriteAllText(full, LuaTemplate(name));
        }
        return SetComponent("script", new JsonObject { ["file"] = relative, ["params"] = new JsonObject() });
    }

    /// <summary>Creates Scripts/&lt;name&gt;.cs (and Scripts.csproj) if missing, and attaches it.</summary>
    public bool AddCSharpScript(string name)
    {
        var relative = ScriptBuilder.CreateScript(_doc.ProjectRoot, name);
        return SetComponent("script", new JsonObject { ["file"] = relative, ["params"] = new JsonObject() });
    }

    private static string LuaTemplate(string name) => $$"""
        -- {{name}}: a NoorEngine Lua (Luau) script.
        -- Hooks: on_start, on_update(dt), on_fixed_update(dt), on_late_update(dt), on_destroy.
        -- self.entity is the entity this script runs on; self.params holds the component's params.
        -- API: docs/scripting/LUA_API.md

        local {{name}} = {}

        function {{name}}:on_start()
            noor.log("{{name}} started on", self.entity:name())
        end

        function {{name}}:on_update(dt)
            -- Example: turn around the vertical axis.
            -- self.entity:rotate(vector.create(0, 1, 0), 45 * dt)
        end

        return {{name}}

        """;

    /// <summary>Opens a script file (absolute path). MainWindow points this at Edit ▸ Preferences ▸ Code editor.</summary>
    public Action<string>? OpenFile { get; set; }

    private void OpenScript(string relative)
    {
        var full = Path.Combine(_doc.ProjectRoot, relative);
        if (OpenFile != null) OpenFile(full);
        else CodeEditors.Open(new Preferences(), _doc.ProjectRoot, full);
    }

    // ---- Views ----

    private Control Card(string type, JsonNode? value, bool editable)
    {
        var title = new TextBlock { Text = type, FontWeight = FontWeight.SemiBold, FontSize = 12, VerticalAlignment = VerticalAlignment.Center };
        var header = new DockPanel { Children = { title } };
        if (editable)
        {
            var remove = new Button { Content = "✕", Classes = { "tool" }, Padding = new Thickness(6, 0) };
            ToolTip.SetTip(remove, $"Remove {type}");
            remove.Click += (_, _) => SetComponent(type, null);
            DockPanel.SetDock(remove, Avalonia.Controls.Dock.Right);
            header.Children.Insert(0, remove);
        }

        var body = new StackPanel { Spacing = 4, Margin = new Thickness(0, 4, 0, 0) };
        if (type == "script" && value is JsonObject script && editable)
        {
            // The script file, an Open button, and its params as editable fields.
            var file = script["file"]?.GetValue<string>() ?? "";
            var open = new Button { Content = "Open", Classes = { "tool" }, IsEnabled = file.Length > 0 };
            ToolTip.SetTip(open, "Open the script in your editor");
            open.Click += (_, _) => OpenScript(file);
            var fileRow = new DockPanel { Children = { open, new TextBlock { Text = file, FontSize = 12, VerticalAlignment = VerticalAlignment.Center, TextTrimming = TextTrimming.CharacterEllipsis } } };
            DockPanel.SetDock(open, Avalonia.Controls.Dock.Right);
            body.Children.Add(fileRow);
            if (script["params"] is JsonObject parameters)
                foreach (var (field, fieldValue) in parameters) body.Children.Add(FieldRow(type, $"params.{field}", field, fieldValue));
            var addParam = new Button { Content = "+ Param", Classes = { "tool" }, HorizontalAlignment = HorizontalAlignment.Left };
            ToolTip.SetTip(addParam, "Values the script reads as self.params.<name>");
            addParam.Click += async (_, _) => await AddField(type, "params.");
            body.Children.Add(addParam);
        }
        else if (type == "csg_model" && _entity != null)
        {
            // A CSG model: what it's made of, its collision, and Export (Milestone 6, part 4).
            var info = _doc.CsgInfo().OfType<JsonObject>().FirstOrDefault(m => m["model"]?.GetValue<string>() == _entity.Id);
            var triangles = info?["triangles"]?.GetValue<long>() ?? 0;
            var collides = _entity.Components["collider"] != null;
            body.Children.Add(new TextBlock
            {
                Text = info == null ? "No brushes yet: Create ▸ Brush adds one."
                     : $"{info["brushes"]} brush(es) · {triangles:N0} triangles · {info["volume"]?.GetValue<double>():0.##} m³\n" +
                       (collides ? $"Collision: a mesh of the same {triangles:N0} triangles" + (triangles > 20000 ? " (expensive: consider simpler collision)" : "")
                                 : "No collision (add a collider with shape \"mesh\" to walk on it)"),
                FontSize = 11, TextWrapping = TextWrapping.Wrap, Classes = { "muted" },
            });
            var export = new Button { Content = "Export GLB…", Classes = { "tool" }, HorizontalAlignment = HorizontalAlignment.Left, IsEnabled = info != null };
            ToolTip.SetTip(export, "A Noor Interchange asset: GLB with embedded textures and a collision node, plus a .noor.json manifest (opens in Blender and Godot)");
            export.Click += async (_, _) =>
            {
                if (TopLevel.GetTopLevel(this) is not Window owner || _entity == null) return;
                var file = await owner.StorageProvider.SaveFilePickerAsync(new Avalonia.Platform.Storage.FilePickerSaveOptions
                {
                    Title = "Export CSG model", SuggestedFileName = _entity.Name + ".glb", DefaultExtension = "glb",
                    FileTypeChoices = [new Avalonia.Platform.Storage.FilePickerFileType("glTF binary") { Patterns = ["*.glb"] }],
                });
                var path = file?.TryGetLocalPath();
                if (path == null) return;
                var result = _doc.ExportCsg(_entity.Id, path);
                if (result != null) Interop.EngineLog.Info("Export", $"Exported {_entity.Name} to {path} (+ .noor.json)");
            };
            body.Children.Add(export);
        }
        else if (value is JsonObject obj && editable)
        {
            if (type == "terrain") body.Children.Add(SculptEditor());
            if (type == "environment") body.Children.Add(SkyModeRow(obj));
            foreach (var (field, fieldValue) in obj)
                if (!(type == "environment" && field == "sky") && !(type == "brush" && field == "faces") && !(type == "terrain" && field == "layers") && !(type == "scatter" && field is "removed" or "added"))
                    body.Children.Add(FieldRow(type, field, field, fieldValue));
            if (type == "scatter") body.Children.Add(ScatterEditor(obj));
            if (type == "terrain") body.Children.Add(LayersEditor(obj));
            if (type == "brush") body.Children.Add(FacesEditor(obj));
            var addField = new Button { Content = "+ Field", Classes = { "tool" }, HorizontalAlignment = HorizontalAlignment.Left };
            addField.Click += async (_, _) => await AddField(type);
            body.Children.Add(addField);
        }
        else
        {
            body.Children.Add(new SelectableTextBlock
            {
                Text = value?.ToJsonString(Pretty) ?? "null",
                FontSize = 11, TextWrapping = TextWrapping.Wrap, FontFamily = new FontFamily("Cascadia Mono, Consolas, monospace"),
                Foreground = NoorTheme.Brush(NoorTheme.TextMuted),
            });
            if (SystemComponents.Contains(type))
                body.Children.Add(new TextBlock { Text = "Managed by the engine (read-only here).", FontSize = 10, Classes = { "muted" } });
        }

        return new Border
        {
            Background = NoorTheme.Brush(NoorTheme.PanelRaised),
            CornerRadius = new CornerRadius(6),
            Padding = new Thickness(8, 6),
            Child = new StackPanel { Children = { header, body } },
        };
    }

    private Control FieldRow(string type, string field, string labelText, JsonNode? value)
    {
        var label = new TextBlock { Text = labelText, FontSize = 12, Classes = { "muted" }, VerticalAlignment = VerticalAlignment.Center, TextTrimming = TextTrimming.CharacterEllipsis };
        Control editor;
        switch (value)
        {
            case JsonValue v when v.TryGetValue<bool>(out var b):
                var check = new CheckBox { IsChecked = b };
                check.IsCheckedChanged += (_, _) => SetField(type, field, JsonValue.Create(check.IsChecked == true));
                editor = check;
                break;
            case JsonValue v when v.TryGetValue<double>(out var d) && field == "ev100":
                // Exposure: a number, or a preset for the situation (Model/ExposurePresets.cs).
                var presets = new Button { Content = "▾", Classes = { "tool" }, Padding = new Thickness(5, 0), Margin = new Thickness(2, 0, 0, 0) };
                ToolTip.SetTip(presets, "Exposure presets");
                var menu = new MenuFlyout();
                foreach (var preset in ExposurePresets.All)
                {
                    var item = new MenuItem { Header = ExposurePresets.Label(preset) };
                    item.Click += (_, _) => SetField(type, field, JsonValue.Create(preset.Ev100));
                    menu.Items.Add(item);
                }
                presets.Flyout = menu;
                var ev = new DockPanel { Children = { presets, NumberBox(d, x => SetField(type, field, JsonValue.Create(x))) } };
                DockPanel.SetDock(presets, Avalonia.Controls.Dock.Right);
                editor = ev;
                break;
            case JsonValue v when v.TryGetValue<double>(out var d):
                editor = NumberBox(d, x => SetField(type, field, JsonValue.Create(x)));
                break;
            case JsonValue v when v.TryGetValue<string>(out var choice) && type == "brush" && field is "shape" or "operation":
            {
                // Brushes: their shape and operation from the lists (ADR-016).
                var options = field == "shape" ? EntityTemplates.BrushShapes : EntityTemplates.BrushOperations;
                var combo = new ComboBox { ItemsSource = options, SelectedItem = options.Contains(choice) ? choice : null, Classes = { "field" },
                                           HorizontalAlignment = HorizontalAlignment.Stretch };
                combo.SelectionChanged += (_, _) => { if (combo.SelectedItem is string pick && pick != choice) SetField(type, field, JsonValue.Create(pick)); };
                editor = combo;
                break;
            }
            case JsonValue v when v.TryGetValue<string>(out var choice) && type is "environment" or "weather_zone" && field is "cloud_type" or "cloud2_type":
            {
                // A cloud layer's style (part 7b, ADR-034): a flat layer (the classic look) or a ray-marched volume.
                string[] options = ["layer", "volume"];
                var combo = new ComboBox { ItemsSource = options, SelectedItem = options.Contains(choice) ? choice : null, Classes = { "field" },
                                           HorizontalAlignment = HorizontalAlignment.Stretch };
                ToolTip.SetTip(combo, "layer: a flat cloud layer (cheap, a classic look). volume: clouds with depth, lit sides and dark bellies (about 1.5 ms)");
                combo.SelectionChanged += (_, _) => { if (combo.SelectedItem is string pick && pick != choice) SetField(type, field, JsonValue.Create(pick)); };
                editor = combo;
                break;
            }
            case JsonValue v when v.TryGetValue<string>(out var choice) && type == "terrain" && field == "base":
            {
                // Terrains: what the heights start from (ADR-031).
                string[] options = ["noise", "flat", "heightmap"];
                var combo = new ComboBox { ItemsSource = options, SelectedItem = options.Contains(choice) ? choice : null, Classes = { "field" },
                                           HorizontalAlignment = HorizontalAlignment.Stretch };
                combo.SelectionChanged += (_, _) => { if (combo.SelectedItem is string pick && pick != choice) SetField(type, field, JsonValue.Create(pick)); };
                editor = combo;
                break;
            }
            case var _ when type == "scatter" && field == "graph":
            {
                // The graph whose instance set output this shows: any procedural graph in the project.
                var current = (value as JsonValue)?.GetValue<string>();
                var graphs = _doc.Assets.Where(a => a.Importer == "procgraph").ToList();
                var items = new List<string> { "(none)" };
                items.AddRange(graphs.Select(m => m.Path));
                var selected = current == null ? "(none)" : graphs.FirstOrDefault(m => m.Guid == current)?.Path ?? "(missing graph)";
                if (!items.Contains(selected)) items.Add(selected);
                var combo = new ComboBox { ItemsSource = items, SelectedItem = selected, Classes = { "field" }, HorizontalAlignment = HorizontalAlignment.Stretch };
                ToolTip.SetTip(combo, "The procedural graph that places the instances (its output named below)");
                combo.SelectionChanged += (_, _) =>
                {
                    if (combo.SelectedItem is not string path || path == selected || path == "(missing graph)") return;
                    var guid = path == "(none)" ? null : graphs.FirstOrDefault(m => m.Path == path)?.Guid;
                    SetField(type, field, guid == null ? null : JsonValue.Create(guid));
                };
                editor = combo;
                break;
            }
            case var _ when type == "scatter" && field == "ground":
            {
                // A terrain to stand on (its sculpting included); the brush plants and removes on it.
                var current = (value as JsonValue)?.GetValue<string>();
                var terrains = _doc.Scene.Entities.Where(e => e.Components["terrain"] != null).ToList();
                var items = new List<string> { "(none)" };
                items.AddRange(terrains.Select(e => e.Name));
                var selected = current == null ? "(none)" : terrains.FirstOrDefault(e => e.Id == current)?.Name ?? "(missing terrain)";
                if (!items.Contains(selected)) items.Add(selected);
                var combo = new ComboBox { ItemsSource = items, SelectedItem = selected, Classes = { "field" }, HorizontalAlignment = HorizontalAlignment.Stretch };
                ToolTip.SetTip(combo, "A terrain the instances stand on (they follow its sculpting); the Plant and Remove brushes work on it");
                combo.SelectionChanged += (_, _) =>
                {
                    if (combo.SelectedItem is not string name || name == selected || name == "(missing terrain)") return;
                    var id = name == "(none)" ? null : terrains.FirstOrDefault(e => e.Name == name)?.Id;
                    SetField(type, field, id == null ? null : JsonValue.Create(id));
                };
                editor = combo;
                break;
            }
            case var _ when type == "weather_zone" && field == "environment":
            {
                // A weather zone's environment (part 7): one of the project's .noorenv files, or settings of its own
                // (written inline; a preset's are copied in, so the zone doesn't change when the preset list does).
                var current = (value as JsonValue)?.TryGetValue<string>(out var g) == true ? g : null;
                var envs = (_doc.Environment(new JsonObject { ["op"] = "list" }) as JsonArray ?? []).OfType<JsonObject>()
                    .Select(e => (Guid: e["guid"]?.GetValue<string>(), Label: e["path"]?.GetValue<string>() ?? "")).Where(e => e.Guid != null).ToList();
                var envPresets = EditorDocument.EnvironmentPresets().OfType<JsonObject>().ToList();
                const string Inline = "(its own settings)";
                var items = new List<string> { Inline };
                items.AddRange(envs.Select(e => e.Label));
                items.AddRange(envPresets.Select(p => "Copy preset: " + p["name"]!.GetValue<string>()));
                var selected = current == null ? Inline : envs.FirstOrDefault(e => e.Guid == current).Label ?? "(missing environment)";
                if (!items.Contains(selected)) items.Add(selected);
                var combo = new ComboBox { ItemsSource = items, SelectedItem = selected, Classes = { "field" }, HorizontalAlignment = HorizontalAlignment.Stretch };
                ToolTip.SetTip(combo, value is JsonObject own ? own.ToJsonString() : "The environment inside the zone, blended in by where the camera is");
                combo.SelectionChanged += (_, _) =>
                {
                    if (combo.SelectedItem is not string pick || pick == selected || pick == "(missing environment)") return;
                    if (pick == Inline)
                        SetField(type, field, new JsonObject());
                    else if (pick.StartsWith("Copy preset: ", StringComparison.Ordinal))
                    {
                        var name = pick["Copy preset: ".Length..];
                        if (envPresets.FirstOrDefault(p => p["name"]?.GetValue<string>() == name)?["environment"] is JsonObject settings)
                            SetField(type, field, settings.DeepClone());
                    }
                    else if (envs.FirstOrDefault(e => e.Label == pick).Guid is { } guid)
                        SetField(type, field, JsonValue.Create(guid));
                };
                editor = combo;
                break;
            }
            case var _ when type == "terrain" && field == "sculpt":
            {
                // The sculpt layer: made by the first stroke, written on Save. Not for typing into.
                var id = (value as JsonValue)?.GetValue<string>();
                var path = id == null ? null : _doc.Assets.FirstOrDefault(a => a.Guid == id)?.Path;
                editor = new TextBlock { Text = path ?? (id == null ? "(none)" : "(not saved yet)"), FontSize = 11, Classes = { "muted" },
                                         VerticalAlignment = VerticalAlignment.Center, TextTrimming = TextTrimming.CharacterEllipsis };
                ToolTip.SetTip(editor, "Hand-sculpted heights, added on top of the generated base (a new seed keeps them)");
                break;
            }
            case var _ when type == "terrain" && field == "heightmap" && (value is null || value is JsonValue hv && hv.TryGetValue<string>(out _)):
            {
                // A heightmap: any picture in the project (16-bit greyscale PNGs keep the most steps).
                var current = (value as JsonValue)?.GetValue<string>();
                var pictures = _doc.Assets.Where(a => a.Path.EndsWith(".png", StringComparison.OrdinalIgnoreCase)).ToList();
                var items = new List<string> { "(none)" };
                items.AddRange(pictures.Select(m => m.Path));
                var selected = current == null ? "(none)" : pictures.FirstOrDefault(m => m.Guid == current)?.Path ?? "(missing picture)";
                if (!items.Contains(selected)) items.Add(selected);
                var combo = new ComboBox { ItemsSource = items, SelectedItem = selected, Classes = { "field" }, HorizontalAlignment = HorizontalAlignment.Stretch };
                combo.SelectionChanged += (_, _) =>
                {
                    if (combo.SelectedItem is not string path || path == selected || path == "(missing picture)") return;
                    var guid = path == "(none)" ? null : pictures.FirstOrDefault(m => m.Path == path)?.Guid;
                    SetField(type, field, guid == null ? null : JsonValue.Create(guid));
                };
                editor = combo;
                break;
            }
            case var _ when field == "material" && (value is null || value is JsonValue mv && mv.TryGetValue<string>(out _)):
            {
                // A material: picked from the project's materials (or none: the default).
                var current = (value as JsonValue)?.GetValue<string>();
                var materials = _doc.Assets.Where(a => a.Importer == "material").ToList();
                var items = new List<string> { "(none)" };
                items.AddRange(materials.Select(m => m.Path));
                var selected = current == null ? "(none)" : materials.FirstOrDefault(m => m.Guid == current)?.Path ?? "(missing material)";
                if (!items.Contains(selected)) items.Add(selected);
                var combo = new ComboBox { ItemsSource = items, SelectedItem = selected, Classes = { "field" }, HorizontalAlignment = HorizontalAlignment.Stretch };
                combo.SelectionChanged += (_, _) =>
                {
                    if (combo.SelectedItem is not string path || path == selected || path == "(missing material)") return;
                    var guid = path == "(none)" ? null : materials.FirstOrDefault(m => m.Path == path)?.Guid;
                    SetField(type, field, guid == null ? null : JsonValue.Create(guid));
                };
                editor = combo;
                break;
            }
            case JsonValue v when v.TryGetValue<string>(out var s):
                var text = new TextBox { Text = s, Classes = { "field" } };
                Commit(text, () => { if (text.Text != s) SetField(type, field, JsonValue.Create(text.Text ?? "")); });
                editor = text;
                break;
            case JsonArray arr when arr.Count is > 0 and <= 4 && arr.All(n => n is JsonValue jv && jv.TryGetValue<double>(out _)):
                var values = arr.Select(n => n!.GetValue<double>()).ToArray();
                var row = new Avalonia.Controls.Primitives.UniformGrid { Rows = 1, Columns = values.Length };
                for (var i = 0; i < values.Length; i++)
                {
                    var index = i;
                    row.Children.Add(NumberBox(values[i], x =>
                    {
                        var next = values.ToArray();
                        next[index] = x;
                        SetField(type, field, new JsonArray(next.Select(n => (JsonNode?)JsonValue.Create(n)).ToArray()));
                    }));
                }
                editor = row;
                break;
            default:
                editor = new TextBlock { Text = value?.ToJsonString() ?? "null", FontSize = 11, Classes = { "muted" }, TextWrapping = TextWrapping.Wrap };
                break;
        }

        var remove = new Button { Content = "✕", Classes = { "tool" }, Padding = new Thickness(4, 0), FontSize = 10 };
        ToolTip.SetTip(remove, $"Remove field {field}");
        remove.Click += (_, _) =>
        {
            if (_entity?.Components[type] is not JsonObject current) return;
            SetComponent(type, WithPath(current, field, null, remove: true));
        };
        var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("96,*,Auto") };
        Grid.SetColumn(editor, 1);
        Grid.SetColumn(remove, 2);
        grid.Children.Add(label);
        grid.Children.Add(editor);
        grid.Children.Add(remove);
        return grid;
    }

    private static TextBox NumberBox(double value, Action<double> set)
    {
        var box = new TextBox { Text = Num.Format(value), Classes = { "field" }, Margin = new Thickness(1, 0) };
        Commit(box, () =>
        {
            if (Num.TryParse(box.Text, out var x)) { if (x != value) set(x); }
            else box.Text = Num.Format(value);
        });
        return box;
    }

    private static void Commit(TextBox box, Action commit)
    {
        box.KeyDown += (_, e) => { if (e.Key == Key.Enter) { commit(); e.Handled = true; } };
        box.LostFocus += (_, _) => commit();
    }

    /// <summary>The environment's sky (ADR-033): a photo (HDRI), the old three-colour gradient, or the
    /// physically based atmosphere built from the sun. Shown even when the scene doesn't say yet.</summary>
    private Control SkyModeRow(JsonObject env)
    {
        string[] options = ["hdri", "gradient", "atmosphere"];
        var current = env["sky"] is JsonValue v && v.TryGetValue<string>(out var s) && options.Contains(s) ? s : env["hdri"] != null ? "hdri" : "gradient";
        var combo = new ComboBox { ItemsSource = options, SelectedItem = current, Classes = { "field" }, HorizontalAlignment = HorizontalAlignment.Stretch };
        ToolTip.SetTip(combo, "atmosphere: the sky and the sun's colour from the sun's direction (red at sunset); the sun's illuminance then means outside the air");
        combo.SelectionChanged += (_, _) =>
        {
            if (combo.SelectedItem is not string pick || pick == current) return;
            var next = (JsonObject)env.DeepClone();
            next["sky"] = pick;
            if (pick == "atmosphere")
            {
                next["turbidity"] ??= 1;
                next["ground_albedo"] ??= 0.3;
                next["altitude"] ??= 0;
            }
            SetComponent("environment", next);
        };
        var text = new TextBlock { Text = "sky", FontSize = 12, Classes = { "muted" }, VerticalAlignment = VerticalAlignment.Center };
        var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("96,*") };
        Grid.SetColumn(combo, 1);
        grid.Children.Add(text);
        grid.Children.Add(combo);
        var buttons = new WrapPanel();
        if (env["fog_density"] == null)
        {
            // Height fog: thick below fog_height, thinning above (Milestone 8A, part 3).
            var fog = new Button { Content = "+ Fog", Classes = { "tool" }, Margin = new Thickness(0, 0, 4, 0) };
            ToolTip.SetTip(fog, "Height fog: full density below fog_height, thinning above it; lit by the sun and the sky");
            fog.Click += (_, _) =>
            {
                var next = (JsonObject)env.DeepClone();
                next["fog_density"] = 0.004;
                next["fog_height"] = 0;
                next["fog_falloff"] = 0.05;
                next["fog_anisotropy"] = 0.6;
                next["fog_color"] = "#FFFFFF";
                SetComponent("environment", next);
            };
            buttons.Children.Add(fog);
        }
        if (env["cloud_coverage"] == null)
        {
            // A cloud layer (Milestone 8A, part 4; drawn with the atmosphere sky).
            var clouds = new Button { Content = "+ Clouds", Classes = { "tool" }, Margin = new Thickness(0, 0, 4, 0) };
            ToolTip.SetTip(clouds, "A cloud layer (atmosphere sky): coverage 0 clear to 1 overcast, drifting with the wind");
            clouds.Click += (_, _) =>
            {
                var next = (JsonObject)env.DeepClone();
                next["cloud_coverage"] = 0.45;
                next["cloud_density"] = 6;
                next["cloud_altitude"] = 1500;
                next["cloud_scale"] = 2500;
                next["cloud_wind_speed"] = 10;
                next["cloud_wind_direction"] = 60;
                next["cloud_seed"] = 1;
                SetComponent("environment", next);
            };
            buttons.Children.Add(clouds);
        }
        if (env["time_of_day"] != null)
            return buttons.Children.Count == 0 ? grid : new StackPanel { Spacing = 4, Children = { grid, buttons } };
        // The time of day places the sun (it then ignores the sun light's rotation).
        var time = new Button { Content = "+ Time of day", Classes = { "tool" }, HorizontalAlignment = HorizontalAlignment.Left };
        ToolTip.SetTip(time, "Place the sun by the hour (and latitude, day of the year) instead of by turning the sun light");
        time.Click += (_, _) =>
        {
            var next = (JsonObject)env.DeepClone();
            next["time_of_day"] = 12;
            next["latitude"] ??= 50;
            next["day_of_year"] ??= 172;
            SetComponent("environment", next);
        };
        buttons.Children.Add(time);
        return new StackPanel { Spacing = 4, Children = { grid, buttons } };
    }

    private Control AddComponentButton()
    {
        var button = new Button { Content = "+ Add component", Classes = { "tool" }, HorizontalAlignment = HorizontalAlignment.Left };
        var marker = new MenuItem { Header = "Marker", IsEnabled = _entity?.Components.ContainsKey("marker") == false };
        marker.Click += (_, _) => SetComponent("marker", new JsonObject { ["kind"] = "spawn" });
        var custom = new MenuItem { Header = "Custom…" };
        custom.Click += async (_, _) =>
        {
            if (TopLevel.GetTopLevel(this) is not Window owner) return;
            var name = await Dialogs.Prompt(owner, "Add component", "Component type (letters, digits, _ and .)", "my_component");
            if (name == null) return;
            if (!name.All(c => char.IsLetterOrDigit(c) || c is '_' or '.') || SystemComponents.Contains(name) || _entity?.Components.ContainsKey(name) == true)
            {
                await Dialogs.Ask(owner, "Add component", $"\"{name}\" can't be used: it's invalid, reserved or already on this entity.", "OK");
                return;
            }
            SetComponent(name, new JsonObject());
        };
        var lua = new MenuItem { Header = "Lua script…", IsEnabled = _entity?.Components.ContainsKey("script") == false };
        lua.Click += async (_, _) =>
        {
            if (TopLevel.GetTopLevel(this) is not Window owner) return;
            var name = await Dialogs.Prompt(owner, "Add Lua script", "Script name (creates Scripts/<name>.luau if it doesn't exist)", "MyScript");
            if (name == null) return;
            if (name.Length == 0 || !(char.IsLetter(name[0]) || name[0] == '_') || !name.All(c => char.IsLetterOrDigit(c) || c == '_'))
            {
                await Dialogs.Ask(owner, "Add Lua script", $"\"{name}\" isn't a valid script name: use letters, digits and _, starting with a letter.", "OK");
                return;
            }
            AddLuaScript(name);
        };
        var physics = new MenuItem { Header = "Physics" };
        var shapes = new MenuItem { Header = "Shape" };
        var sound = new MenuItem { Header = "Sound" };
        foreach (var (type, label, value) in EntityTemplates.ComponentPresets)
        {
            var item = new MenuItem { Header = label, IsEnabled = _entity?.Components.ContainsKey(type) == false };
            item.Click += (_, _) => SetComponent(type, value());
            (type == "primitive" ? shapes : type.StartsWith("audio") ? sound : physics).Items.Add(item);
        }
        var csharp = new MenuItem { Header = "C# script…", IsEnabled = _entity?.Components.ContainsKey("script") == false };
        csharp.Click += async (_, _) =>
        {
            if (TopLevel.GetTopLevel(this) is not Window owner) return;
            var name = await Dialogs.Prompt(owner, "Add C# script", "Class name (creates Scripts/<name>.cs if it doesn't exist)", "MyScript");
            if (name == null) return;
            if (name.Length == 0 || !(char.IsLetter(name[0]) || name[0] == '_') || !name.All(c => char.IsLetterOrDigit(c) || c == '_'))
            {
                await Dialogs.Ask(owner, "Add C# script", $"\"{name}\" isn't a valid C# class name: use letters, digits and _, starting with a letter.", "OK");
                return;
            }
            AddCSharpScript(name);
        };
        button.Flyout = new MenuFlyout { Items = { lua, csharp, physics, sound, shapes, marker, custom } };
        return button;
    }

    private async Task AddField(string type, string prefix = "")
    {
        if (TopLevel.GetTopLevel(this) is not Window owner) return;
        var text = await Dialogs.Prompt(owner, $"Add field to {type}", "name = value  (value: a number, true/false, [1, 2, 3], or text)", "speed = 1");
        if (text == null) return;
        var eq = text.IndexOf('=');
        var name = (eq < 0 ? text : text[..eq]).Trim();
        var raw = eq < 0 ? "0" : text[(eq + 1)..].Trim();
        if (name.Length == 0) return;
        JsonNode? value;
        try { value = JsonNode.Parse(raw.Replace(',', raw.StartsWith('[') ? ',' : '.')); }
        catch (JsonException) { value = JsonValue.Create(raw); }
        SetField(type, prefix + name, value);
    }
}
