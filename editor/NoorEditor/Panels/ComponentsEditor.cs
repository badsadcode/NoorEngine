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
            foreach (var (field, fieldValue) in obj)
                if (!(type == "brush" && field == "faces")) body.Children.Add(FieldRow(type, field, field, fieldValue));
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
