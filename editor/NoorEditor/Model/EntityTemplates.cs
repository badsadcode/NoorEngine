using System.Text.Json.Nodes;

namespace NoorEditor.Model;

/// <summary>Ready-made entities for the Create menu, and component presets for the Inspector.</summary>
public static class EntityTemplates
{
    public sealed record Template(string Name, Func<JsonObject> Components, Vec3 Position);

    private const string Stone = "#8E86A8";

    public static JsonObject Box(double x = 1, double y = 1, double z = 1, string color = Stone) =>
        new() { ["shape"] = "box", ["size"] = new JsonArray(x, y, z), ["color"] = color };
    public static JsonObject Sphere(double radius = 0.5, string color = Stone) =>
        new() { ["shape"] = "sphere", ["radius"] = radius, ["color"] = color };
    public static JsonObject Capsule(double radius = 0.3, double height = 1.8, string color = Stone) =>
        new() { ["shape"] = "capsule", ["radius"] = radius, ["height"] = height, ["color"] = color };

    public static IReadOnlyList<Template> All { get; } =
    [
        new("Empty", () => new JsonObject(), new Vec3(0, 0, 0)),
        new("Box", () => new JsonObject { ["primitive"] = Box() }, new Vec3(0, 0.5, 0)),
        new("Sphere", () => new JsonObject { ["primitive"] = Sphere() }, new Vec3(0, 0.5, 0)),
        new("Capsule", () => new JsonObject { ["primitive"] = Capsule() }, new Vec3(0, 0.9, 0)),
        new("Floor", () => new JsonObject
        {
            ["primitive"] = new JsonObject { ["shape"] = "plane", ["size"] = new JsonArray(20, 1, 20), ["color"] = "#3A3550" },
            ["collider"] = new JsonObject { ["shape"] = "box", ["size"] = new JsonArray(20, 0.2, 20), ["offset"] = new JsonArray(0, -0.1, 0) },
        }, new Vec3(0, 0, 0)),
        new("Physics crate", () => new JsonObject
        {
            ["primitive"] = Box(color: "#C08A4A"),
            ["collider"] = new JsonObject { ["shape"] = "box", ["size"] = new JsonArray(1, 1, 1) },
            ["rigidbody"] = new JsonObject { ["type"] = "dynamic", ["mass"] = 10 },
        }, new Vec3(0, 3, 0)),
        new("Physics ball", () => new JsonObject
        {
            ["primitive"] = Sphere(color: "#4AA8C0"),
            ["collider"] = new JsonObject { ["shape"] = "sphere", ["radius"] = 0.5 },
            ["rigidbody"] = new JsonObject { ["type"] = "dynamic", ["mass"] = 2, ["restitution"] = 0.6 },
        }, new Vec3(0, 3, 0)),
    ];

    /// <summary>Component presets for Inspector ▸ Add component (type, menu label, value).</summary>
    public static IReadOnlyList<(string Type, string Label, Func<JsonObject> Value)> ComponentPresets { get; } =
    [
        ("rigidbody", "Rigidbody (dynamic)", () => new JsonObject { ["type"] = "dynamic", ["mass"] = 1, ["friction"] = 0.5, ["restitution"] = 0 }),
        ("collider", "Box collider", () => new JsonObject { ["shape"] = "box", ["size"] = new JsonArray(1, 1, 1) }),
        ("collider", "Sphere collider", () => new JsonObject { ["shape"] = "sphere", ["radius"] = 0.5 }),
        ("collider", "Capsule collider", () => new JsonObject { ["shape"] = "capsule", ["radius"] = 0.3, ["height"] = 1.8 }),
        ("collider", "Mesh collider (static, from the model)", () => new JsonObject { ["shape"] = "mesh" }),
        ("primitive", "Shape: box", () => Box()),
        ("primitive", "Shape: sphere", () => Sphere()),
        ("primitive", "Shape: capsule", () => Capsule()),
    ];
}
