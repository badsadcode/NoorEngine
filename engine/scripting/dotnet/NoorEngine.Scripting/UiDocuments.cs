using System.Text.Json.Nodes;
using NoorEngine.Interop;

namespace NoorEngine;

/// <summary>
/// A game UI document (gameplay foundations part 6b, docs/ui/GAME_UI.md): a .noorui page drawn over the game. Open one
/// with <see cref="Ui.Open"/> or find one a ui_document component opened with <see cref="Ui.Find"/>; listen to its
/// elements with NoorScript.On(document, "click", "resume", e => ...). Elements are named by their id ("resume" or
/// "#resume"); one that isn't there throws, naming it.
/// </summary>
public sealed unsafe class UiDocument
{
    /// <summary>The number the engine knows it by.</summary>
    public uint Id { get; }
    /// <summary>What its events come from (listen to it like an entity).</summary>
    internal Entity Emitter { get; }

    internal UiDocument(uint id, Entity emitter)
    {
        Id = id;
        Emitter = emitter;
    }

    public void Show() => Request("show");
    public void Hide() => Request("hide");
    public void Close() => Request("close");
    public bool Visible => Request("visible")?.GetValue<bool>() ?? false;

    /// <summary>The element's content becomes this text (shown as it is, no markup).</summary>
    public void SetText(string element, string text) => Request("set_text", element, ("text", text));
    /// <summary>The element's content becomes this markup (RML).</summary>
    public void SetRml(string element, string rml) => Request("set_rml", element, ("text", rml));
    public void SetClass(string element, string name, bool on = true) => Request("set_class", element, ("name", name), ("on", on));
    /// <summary>An attribute ("disabled", "src", "value"...); null or false removes it, true sets it with no value.</summary>
    public void SetAttribute(string element, string name, JsonNode? value) => Request("set_attribute", element, ("name", name), ("value", value));
    /// <summary>A style property as RCSS text ("width", "40dp"); empty: back to the style sheet's.</summary>
    public void SetStyle(string element, string property, string value) => Request("set_style", element, ("property", property), ("value", value));
    /// <summary>A form control's value: text boxes and selects a string, ranges a number, checkboxes and radios
    /// true / false; other elements their content.</summary>
    public JsonNode? Value(string element) => Request("value", element);
    public void SetValue(string element, JsonNode? value) => Request("set_value", element, ("value", value));
    public void Focus(string element) => Request("focus", element);
    /// <summary>As if it were clicked: its click event comes next frame.</summary>
    public void Click(string element) => Request("click", element);

    private JsonNode? Request(string op, string? element = null, params (string Key, JsonNode? Value)[] fields)
    {
        var request = new JsonObject { ["op"] = op, ["document"] = Id };
        if (element != null) request["element"] = element;
        foreach (var (key, value) in fields) request[key] = value?.DeepClone();
        return Ui.Call(request, default);
    }
}

public static unsafe partial class Ui
{
    /// <summary>Opens a document (a path inside the project, "UI/pause.noorui") and shows it. With an owner (a
    /// script's Entity) it closes when that entity is destroyed.</summary>
    public static UiDocument Open(string path, Entity? owner = null) =>
        Document(Call(new JsonObject { ["op"] = "open", ["path"] = path }, owner?.Id ?? default))!;

    /// <summary>An open document by its path (one a ui_document component opened), or null.</summary>
    public static UiDocument? Find(string path) => Document(Call(new JsonObject { ["op"] = "find", ["path"] = path }, default));

    internal static JsonNode? Call(JsonObject request, RtEntity caller)
    {
        byte* result;
        fixed (byte* r = Engine.Utf8(request.ToJsonString())) result = Engine.A.Ui(caller, r);
        if (result == null) throw new InvalidOperationException(Engine.Str(Engine.A.LastError()));
        return JsonNode.Parse(Engine.Str(result)!);
    }

    private static UiDocument? Document(JsonNode? result)
    {
        if (result is not JsonObject o) return null;
        var hex = o["emitter"]!.GetValue<string>().Replace("-", "");
        var bytes = Convert.FromHexString(hex);
        var id = new RtEntity { Lo = BitConverter.ToUInt64(bytes, 0), Hi = BitConverter.ToUInt64(bytes, 8) };
        return new UiDocument(o["document"]!.GetValue<uint>(), new Entity(id));
    }
}
