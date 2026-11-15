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

    public bool SetField(string type, string field, JsonNode? value)
    {
        if (_entity?.Components[type] is not JsonObject current) return false;
        var next = (JsonObject)current.DeepClone();
        next[field] = value?.DeepClone();
        return SetComponent(type, next);
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
        if (value is JsonObject obj && editable)
        {
            foreach (var (field, fieldValue) in obj) body.Children.Add(FieldRow(type, field, fieldValue));
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

    private Control FieldRow(string type, string field, JsonNode? value)
    {
        var label = new TextBlock { Text = field, FontSize = 12, Classes = { "muted" }, VerticalAlignment = VerticalAlignment.Center, TextTrimming = TextTrimming.CharacterEllipsis };
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
            var next = (JsonObject)current.DeepClone();
            next.Remove(field);
            SetComponent(type, next);
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
        button.Flyout = new MenuFlyout { Items = { marker, custom } };
        return button;
    }

    private async Task AddField(string type)
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
        SetField(type, name, value);
    }
}
