using System.Text.Json;
using System.Text.Json.Nodes;

namespace NoorEditor.Model;

/// <summary>
/// A <c>.noormat</c> file as the material editor sees it (docs/rendering/MATERIALS.md): the file's
/// JSON, the values in effect (an instance's come from its parents unless it overrides them), and
/// edits. No engine calls: the engine reads the saved file like any other.
/// </summary>
public sealed class MaterialFile
{
    /// <summary>Texture slots, in the order the editor shows them.</summary>
    public static readonly string[] MapFields =
        ["base_color_map", "metallic_roughness_map", "roughness_map", "metallic_map", "normal_map", "occlusion_map", "emissive_map"];

    /// <summary>What a field is when nothing sets it (the engine's defaults, asset::Material).</summary>
    public static JsonNode? Default(string field) => field switch
    {
        "base_color" => new JsonArray(1.0, 1.0, 1.0, 1.0),
        "metallic" => 0.0,
        "roughness" => 0.5,
        "normal_scale" => 1.0,
        "occlusion_strength" => 1.0,
        "emissive" => new JsonArray(0.0, 0.0, 0.0),
        "emissive_strength" => 1.0,
        "alpha" => "opaque",
        "alpha_cutoff" => 0.5,
        "double_sided" => false,
        "uv_scale" => new JsonArray(1.0, 1.0),
        "uv_offset" => new JsonArray(0.0, 0.0),
        _ => null,  // maps: none
    };

    public JsonObject Root { get; private set; }

    /// <summary>Reads a parent's file by its asset id (null if it can't be found or read).</summary>
    private readonly Func<string, JsonObject?> _loadParent;

    public MaterialFile(JsonObject root, Func<string, JsonObject?> loadParent)
    {
        Root = root;
        _loadParent = loadParent;
    }

    public static JsonObject? Read(string path)
    {
        try { return JsonNode.Parse(File.ReadAllText(path)) as JsonObject; }
        catch (Exception e) when (e is IOException or JsonException or UnauthorizedAccessException) { return null; }
    }

    public string? ParentId => Root["parent"] is JsonValue v && v.TryGetValue<string>(out var s) ? s : null;
    public bool IsInstance => ParentId != null;

    /// <summary>Where this file's own values live: the root, or an instance's "overrides".</summary>
    private JsonObject Own
    {
        get
        {
            if (!IsInstance) return Root;
            if (Root["overrides"] is not JsonObject o) Root["overrides"] = o = new JsonObject();
            return o;
        }
    }

    /// <summary>An instance changes this field (base materials: true when the file sets it).</summary>
    public bool Sets(string field) => Own.ContainsKey(field);

    /// <summary>The value in effect: this file's, else the parents', else the default.</summary>
    public JsonNode? Get(string field)
    {
        if (Own.TryGetPropertyValue(field, out var v)) return v?.DeepClone();
        var parent = ParentId;
        var seen = new HashSet<string>();
        for (var depth = 0; parent != null && depth < 16 && seen.Add(parent); ++depth)
        {
            if (_loadParent(parent) is not { } p) break;
            var isInstance = p["parent"] is JsonValue;
            var own = isInstance ? p["overrides"] as JsonObject : p;
            if (own != null && own.TryGetPropertyValue(field, out var pv)) return pv?.DeepClone();
            parent = isInstance && p["parent"] is JsonValue pid && pid.TryGetValue<string>(out var next) ? next : null;
        }
        return Default(field);
    }

    public double GetNumber(string field) => Get(field) is JsonValue v && v.TryGetValue<double>(out var d) ? d : 0;

    public double[] GetVector(string field, int n)
    {
        var result = new double[n];
        var def = Default(field) as JsonArray;
        var arr = Get(field) as JsonArray;
        for (var i = 0; i < n; ++i)
            result[i] = arr != null && i < arr.Count && arr[i] is JsonValue v && v.TryGetValue<double>(out var d) ? d
                      : def != null && i < def.Count ? def[i]!.GetValue<double>() : 0;
        return result;
    }

    public string? GetMap(string field) => Get(field) is JsonValue v && v.TryGetValue<string>(out var s) ? s : null;

    /// <summary>Sets a field (an instance overrides it). A null map: none (an instance hides its parent's).</summary>
    public void Set(string field, JsonNode? value)
    {
        if (value == null && !IsInstance) Own.Remove(field);
        else Own[field] = value;
    }

    /// <summary>An instance goes back to its parent's value; a base material to the default.</summary>
    public void Reset(string field) => Own.Remove(field);

    public void Replace(JsonObject root) => Root = root;

    /// <summary>The file's text (two-space indents, "noor_material" first).</summary>
    public string Serialize()
    {
        var ordered = new JsonObject { ["noor_material"] = 1 };
        foreach (var (k, v) in Root)
            if (k != "noor_material") ordered[k] = v?.DeepClone();
        return ordered.ToJsonString(new JsonSerializerOptions { WriteIndented = true }) + "\n";
    }

    /// <summary>A new instance of the material with id <paramref name="parentId"/>, changing nothing yet.</summary>
    public static string InstanceText(string parentId) =>
        new JsonObject { ["noor_material"] = 1, ["parent"] = parentId, ["overrides"] = new JsonObject() }
            .ToJsonString(new JsonSerializerOptions { WriteIndented = true }) + "\n";

    // ---- Colours: files hold linear numbers, people think in sRGB hex ----

    public static double LinearToSrgb(double c) =>
        c <= 0.0031308 ? 12.92 * c : 1.055 * Math.Pow(c, 1 / 2.4) - 0.055;

    public static double SrgbToLinear(double c) =>
        c <= 0.04045 ? c / 12.92 : Math.Pow((c + 0.055) / 1.055, 2.4);

    public static string ToHex(double[] linear) =>
        "#" + string.Concat(linear.Take(3).Select(c => ((int)Math.Round(Math.Clamp(LinearToSrgb(Math.Clamp(c, 0, 1)), 0, 1) * 255)).ToString("X2")));

    public static double[]? FromHex(string? text)
    {
        var s = (text ?? "").Trim().TrimStart('#');
        if (s.Length != 6 || !int.TryParse(s, System.Globalization.NumberStyles.HexNumber, null, out var rgb)) return null;
        return [SrgbToLinear(((rgb >> 16) & 0xFF) / 255.0), SrgbToLinear(((rgb >> 8) & 0xFF) / 255.0), SrgbToLinear((rgb & 0xFF) / 255.0)];
    }
}
