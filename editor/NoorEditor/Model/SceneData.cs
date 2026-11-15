using System.Globalization;
using System.Text.Json.Nodes;

namespace NoorEditor.Model;

/// <summary>A 3-component vector in editor terms (metres or degrees).</summary>
public readonly record struct Vec3(double X, double Y, double Z)
{
    public JsonArray ToJson() => new(X, Y, Z);
    public double this[int i] => i switch { 0 => X, 1 => Y, _ => Z };
    public Vec3 With(int i, double v) => i switch { 0 => this with { X = v }, 1 => this with { Y = v }, _ => this with { Z = v } };
}

/// <summary>Unit quaternion (x, y, z, w), the engine's rotation format.</summary>
public readonly record struct Quat(double X, double Y, double Z, double W)
{
    public static readonly Quat Identity = new(0, 0, 0, 1);
    public JsonArray ToJson() => new(X, Y, Z, W);

    private static Quat Mul(Quat a, Quat b) => new(
        a.W * b.X + a.X * b.W + a.Y * b.Z - a.Z * b.Y,
        a.W * b.Y - a.X * b.Z + a.Y * b.W + a.Z * b.X,
        a.W * b.Z + a.X * b.Y - a.Y * b.X + a.Z * b.W,
        a.W * b.W - a.X * b.X - a.Y * b.Y - a.Z * b.Z);

    /// <summary>
    /// Euler angles in degrees, applied roll (Z) first, then pitch (X), then yaw (Y): q = qY · qX · qZ.
    /// Yaw about +Y is the natural "turn" in a +Y-up world.
    /// </summary>
    public static Quat FromEulerDegrees(Vec3 e)
    {
        const double toRad = Math.PI / 180.0;
        Quat Axis(double angle, int axis)
        {
            var s = Math.Sin(angle * toRad / 2);
            var c = Math.Cos(angle * toRad / 2);
            return axis switch { 0 => new(s, 0, 0, c), 1 => new(0, s, 0, c), _ => new(0, 0, s, c) };
        }
        return Mul(Mul(Axis(e.Y, 1), Axis(e.X, 0)), Axis(e.Z, 2));
    }

    /// <summary>Inverse of <see cref="FromEulerDegrees"/>. At ±90° pitch, yaw and roll share one axis (gimbal lock).</summary>
    public Vec3 ToEulerDegrees()
    {
        const double toDeg = 180.0 / Math.PI;
        var sinPitch = Math.Clamp(2 * (W * X - Y * Z), -1.0, 1.0);
        var pitch = Math.Asin(sinPitch);
        var yaw = Math.Atan2(2 * (W * Y + X * Z), 1 - 2 * (X * X + Y * Y));
        var roll = Math.Atan2(2 * (W * Z + X * Y), 1 - 2 * (X * X + Z * Z));
        return new Vec3(Clean(pitch * toDeg), Clean(yaw * toDeg), Clean(roll * toDeg));
    }

    // Avoid showing "-0" or 89.99999999 noise for values that are round in practice.
    private static double Clean(double deg)
    {
        var r = Math.Round(deg, 4);
        return r == 0 ? 0 : r;
    }
}

public sealed record Transform(Vec3 Position, Quat Rotation, Vec3 Scale)
{
    public static Transform FromJson(JsonNode? t)
    {
        static double N(JsonNode? a, int i, double fallback) => a is JsonArray arr && i < arr.Count ? arr[i]!.GetValue<double>() : fallback;
        var p = t?["position"];
        var r = t?["rotation"];
        var s = t?["scale"];
        return new Transform(
            new Vec3(N(p, 0, 0), N(p, 1, 0), N(p, 2, 0)),
            new Quat(N(r, 0, 0), N(r, 1, 0), N(r, 2, 0), N(r, 3, 1)),
            new Vec3(N(s, 0, 1), N(s, 1, 1), N(s, 2, 1)));
    }

    public JsonObject ToJson() => new()
    {
        ["position"] = Position.ToJson(),
        ["rotation"] = Rotation.ToJson(),
        ["scale"] = Scale.ToJson(),
    };
}

public sealed record EntityInfo(
    string Id, string Name, string? Parent, string Layer, IReadOnlyList<string> Tags, Transform Transform,
    JsonObject Components, bool FromPrefab, string? Instance)
{
    public static EntityInfo FromJson(JsonNode e) => new(
        e["id"]!.GetValue<string>(),
        e["name"]?.GetValue<string>() ?? "",
        e["parent"]?.GetValue<string>(),
        e["layer"]?.GetValue<string>() ?? "",
        e["tags"] is JsonArray tags ? tags.Select(t => t!.GetValue<string>()).ToList() : [],
        Transform.FromJson(e["transform"]),
        e["components"] as JsonObject ?? new JsonObject(),
        e["from_prefab"]?.GetValue<bool>() ?? false,
        e["instance"]?.GetValue<string>());

    /// <summary>Short description of what this entity is, for the hierarchy and inspector.</summary>
    public string Kind =>
        Components.ContainsKey("prefab") ? "Prefab instance" :
        Components.ContainsKey("mesh") ? "Mesh" :
        Components.ContainsKey("marker") ? "Marker" :
        Components.ContainsKey("model") ? "Model" : "Entity";
}

public sealed record SceneSnapshot(
    bool Open, string Path, string Name, bool Modified, bool CanUndo, string UndoLabel, bool CanRedo, string RedoLabel,
    ulong Revision, IReadOnlyList<EntityInfo> Entities, IReadOnlyList<string> Issues, bool Playing = false, double PlaySeconds = 0)
{
    public static readonly SceneSnapshot Empty = new(false, "", "", false, false, "", false, "", 0, [], []);

    public static SceneSnapshot FromJson(string json)
    {
        var j = JsonNode.Parse(json)!;
        if (!(j["open"]?.GetValue<bool>() ?? false)) return Empty;
        return new SceneSnapshot(
            true,
            j["path"]?.GetValue<string>() ?? "",
            j["name"]?.GetValue<string>() ?? "",
            j["modified"]?.GetValue<bool>() ?? false,
            j["can_undo"]?.GetValue<bool>() ?? false,
            j["undo_label"]?.GetValue<string>() ?? "",
            j["can_redo"]?.GetValue<bool>() ?? false,
            j["redo_label"]?.GetValue<string>() ?? "",
            j["revision"]?.GetValue<ulong>() ?? 0,
            (j["entities"] as JsonArray ?? []).Select(e => EntityInfo.FromJson(e!)).ToList(),
            (j["issues"] as JsonArray ?? []).Select(i => i?["message"]?.GetValue<string>() ?? "").ToList(),
            j["playing"]?.GetValue<bool>() ?? false,
            j["play_seconds"]?.GetValue<double>() ?? 0);
    }
}

public sealed record AssetInfo(string Guid, string Path, string Importer, string Name)
{
    public string Kind => Importer switch { "gltf" => "Model", "prefab" => "Prefab", _ => Importer };
    public string Folder => System.IO.Path.GetDirectoryName(Path)?.Replace('\\', '/') ?? "";
}

/// <summary>Number formatting for the UI: always '.' decimals, whatever the OS locale says.</summary>
public static class Num
{
    public static string Format(double v) => Math.Round(v, 4).ToString("0.####", CultureInfo.InvariantCulture);

    /// <summary>Accepts "1.5" and "1,5" (Polish keyboards type commas).</summary>
    public static bool TryParse(string? text, out double v) =>
        double.TryParse((text ?? "").Trim().Replace(',', '.'), NumberStyles.Float, CultureInfo.InvariantCulture, out v) && double.IsFinite(v);
}
