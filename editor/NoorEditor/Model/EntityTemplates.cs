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

    // ---- Player (first person): an entity with a character controller and a Lua script, a capsule
    // body to see it by, and a camera at eye height. ----

    public const string PlayerTemplateName = "Player (first person)";
    public const string PlayerScriptPath = "Scripts/PlayerController.luau";

    public static JsonObject PlayerComponents() => new()
    {
        ["character"] = new JsonObject { ["radius"] = 0.3, ["height"] = 1.8, ["step_height"] = 0.4, ["max_slope"] = 50 },
        ["script"] = new JsonObject
        {
            ["file"] = PlayerScriptPath,
            ["params"] = new JsonObject { ["speed"] = 5, ["sprint"] = 1.8, ["jump_speed"] = 5, ["look_sensitivity"] = 0.12 },
        },
    };

    public static JsonObject PlayerBody() => new() { ["primitive"] = Capsule(color: "#7C4DFF") };
    public static JsonObject PlayerCamera() => new()
    {
        ["camera"] = new JsonObject { ["fov"] = 75, ["near"] = 0.05 },
        ["audio_listener"] = new JsonObject(),  // the player's ears
    };

    /// <summary>A sound emitter for an audio asset (Content Browser ▸ Add to scene).</summary>
    public static JsonObject AudioSource(string clip) => new()
    {
        ["clip"] = clip, ["volume"] = 1, ["loop"] = false, ["spatial"] = true,
        ["min_distance"] = 1, ["max_distance"] = 60, ["bus"] = "sfx", ["play_on_start"] = true,
    };

    public const string PlayerScript = """
        -- PlayerController: first-person walking, looking and jumping (a NoorEngine template).
        -- Click into the game view to capture the mouse for looking; Esc gives it back.
        -- Params (Inspector): speed, sprint (multiplier), jump_speed, look_sensitivity.
        -- API: docs/scripting/LUA_API.md

        local Player = {}

        function Player:on_start()
            self.yaw = self.entity:euler().y
            self.pitch = 0
            self.camera = self.entity:child("Camera")
        end

        function Player:on_update(dt)
            local p = self.params

            -- Looking: only while the mouse is captured.
            if noor.input.pressed("fire") and not noor.input.mouse_captured() then
                noor.input.capture_mouse(true)
            end
            if noor.input.mouse_captured() then
                local look = p.look_sensitivity or 0.12
                self.yaw -= noor.input.axis("look_x") * look
                self.pitch = math.clamp(self.pitch - noor.input.axis("look_y") * look, -85, 85)
            end
            self.entity:set_euler(vector.create(0, self.yaw, 0))
            if self.camera then
                self.camera:set_euler(vector.create(self.pitch, 0, 0))
            end

            -- Walking: W/S forward and back, A/D sideways, Shift to sprint.
            local move = self.entity:forward() * noor.input.axis("move_y") + self.entity:right() * noor.input.axis("move_x")
            if vector.magnitude(move) > 1 then
                move = vector.normalize(move)
            end
            local speed = (p.speed or 5) * (if noor.input.down("sprint") then (p.sprint or 1.8) else 1)
            self.entity:character_move(move * speed)

            if noor.input.pressed("jump") then
                self.entity:character_jump(p.jump_speed or 5)
            end
        end

        return Player

        """;

    /// <summary>Component presets for Inspector ▸ Add component (type, menu label, value).</summary>
    public static IReadOnlyList<(string Type, string Label, Func<JsonObject> Value)> ComponentPresets { get; } =
    [
        ("rigidbody", "Rigidbody (dynamic)", () => new JsonObject { ["type"] = "dynamic", ["mass"] = 1, ["friction"] = 0.5, ["restitution"] = 0 }),
        ("collider", "Box collider", () => new JsonObject { ["shape"] = "box", ["size"] = new JsonArray(1, 1, 1) }),
        ("collider", "Sphere collider", () => new JsonObject { ["shape"] = "sphere", ["radius"] = 0.5 }),
        ("collider", "Capsule collider", () => new JsonObject { ["shape"] = "capsule", ["radius"] = 0.3, ["height"] = 1.8 }),
        ("collider", "Mesh collider (static, from the model)", () => new JsonObject { ["shape"] = "mesh" }),
        ("audio_source", "Audio source", () => AudioSource("")),
        ("audio_listener", "Audio listener (ears)", () => new JsonObject()),
        ("primitive", "Shape: box", () => Box()),
        ("primitive", "Shape: sphere", () => Sphere()),
        ("primitive", "Shape: capsule", () => Capsule()),
    ];
}
