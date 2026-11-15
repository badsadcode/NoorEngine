using System.Text.Json;
using System.Text.Json.Nodes;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
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

    private void OpenScript(string relative)
    {
        var full = Path.Combine(_doc.ProjectRoot, relative);
        try
        {
            System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(full) { UseShellExecute = true });
        }
        catch (System.ComponentModel.Win32Exception)
        {
            // No program is associated with .luau yet (NoorCode arrives in part 7): fall back to Notepad.
            System.Diagnostics.Process.Start("notepad.exe", $"\"{full}\"");
        }
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
        else if (value is JsonObject obj && editable)
        {
            foreach (var (field, fieldValue) in obj) body.Children.Add(FieldRow(type, field, field, fieldValue));
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
            case JsonValue v when v.TryGetValue<double>(out var d):
                editor = NumberBox(d, x => SetField(type, field, JsonValue.Create(x)));
                break;
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
