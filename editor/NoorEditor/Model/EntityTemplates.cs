using System.Text.Json.Nodes;

namespace NoorEditor.Model;

/// <summary>Ready-made entities for the Create menu, and component presets for the Inspector.</summary>
public static class EntityTemplates
{
    public sealed record Template(string Name, Func<JsonObject> Components, Vec3 Position, Vec3? EulerDegrees = null);

    private const string Stone = "#8E86A8";

    public const string SunTemplateName = "Sun (directional light)";

    /// <summary>An environment from an HDRI (Milestone 5, part 5): intensity in nits per HDRI unit.</summary>
    public static JsonObject Environment(string hdri) => new()
    {
        ["hdri"] = hdri, ["intensity"] = 8000, ["rotation"] = 0, ["background"] = true,
    };

    public static JsonObject Sun() => new()
    {
        ["type"] = "directional", ["color"] = "#FFF5E6", ["illuminance"] = 100000,
        ["sky_color"] = "#6B9EFF", ["sky_luminance"] = 8000,
    };

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
        // The sun (Milestone 5): shines along the entity's forward (-Z); rotate it to move the sun.
        // Illuminance in lux (100000 = bright sunshine); its sky too, until environments (part 5).
        new(SunTemplateName, () => new JsonObject { ["light"] = Sun() }, new Vec3(0, 10, 0), new Vec3(-50, 30, 0)),
        // Camera (2D) (Milestone 9A, part 1): an orthographic game camera looking down -Z at the XY plane, 10 m high; for 2D
        // and side-on games (NoorPong). Perspective cameras come with the player templates.
        new("Camera (2D)", () => new JsonObject
        {
            ["camera"] = new JsonObject { ["projection"] = "orthographic", ["height"] = 10, ["near"] = 0.1, ["far"] = 100, ["ev100"] = 14 },
        }, new Vec3(0, 0, 10)),
        // Lamps (Milestone 5, part 4): intensity in lumens (a 60 W-equivalent bulb is ~800 lm),
        // range in metres. Indoors or at night use a lower camera exposure (ev100 6..9) to see them.
        new("Point light", () => new JsonObject
        {
            ["light"] = new JsonObject { ["type"] = "point", ["color"] = "#FFD9A0", ["intensity"] = 1500, ["range"] = 10 },
        }, new Vec3(0, 2.5, 0)),
        new("Spot light", () => new JsonObject
        {
            ["light"] = new JsonObject { ["type"] = "spot", ["color"] = "#FFF0D8", ["intensity"] = 3000, ["range"] = 15, ["angle"] = 30 },
        }, new Vec3(0, 4, 0), new Vec3(-90, 0, 0)),
        // Terrain (Milestone 6, part 5, ADR-031): generated from these settings, centred on the entity
        // (its rotation and scale are ignored). Tiles of 256 cells; height and scale in metres.
        new(TerrainTemplateName, () => new JsonObject { ["terrain"] = Terrain() }, new Vec3(0, 0, 0)),
        // Scatter (Milestone 7, part 7): a procedural graph's instance set output (a forest), with trees
        // removed and planted by hand on top. Pick the graph and a terrain to stand on in the Inspector.
        new(ScatterTemplateName, () => new JsonObject { ["scatter"] = Scatter() }, new Vec3(0, 0, 0)),
        // Weather zone (Milestone 8A, part 7): a box where the environment is different (a fog bank here), blended
        // in over `blend` metres by where the scene's camera is. Pick a project environment in the Inspector, or
        // copy a preset in.
        new(WeatherZoneTemplateName, () => new JsonObject { ["weather_zone"] = WeatherZone() }, new Vec3(0, 0, 0)),
        // Water (Milestone 8D, part 1, ADR-037): a level rectangle of water (size in metres; the entity's scale and
        // turn about +Y apply) with a preset's look; any look field added in the Inspector overrides the preset's.
        // Put it where the ground is lower than it: the water shows where it's deeper than the ground beneath.
        new(WaterTemplateName, () => new JsonObject { ["water"] = Water() }, new Vec3(0, 0, 0)),
    ];

    public const string WaterTemplateName = "Water";

    public static JsonObject Water() => new() { ["preset"] = "Lake", ["size"] = new JsonArray(20, 20) };

    public const string WeatherZoneTemplateName = "Weather zone";

    public static JsonObject WeatherZone() => new()
    {
        ["shape"] = "box", ["size"] = new JsonArray(40, 20, 40), ["blend"] = 15, ["priority"] = 0,
        ["environment"] = new JsonObject { ["fog_density"] = 0.02, ["fog_height"] = 10, ["fog_color"] = "#C8CCD2" },
    };

    public const string ScatterTemplateName = "Scatter";

    public static JsonObject Scatter() => new()
    {
        ["graph"] = null, ["output"] = "trees", ["ground"] = null, ["removed"] = new JsonArray(), ["added"] = new JsonArray(),
    };

    public const string TerrainTemplateName = "Terrain";

    public static JsonObject Terrain() => new()
    {
        ["tiles"] = new JsonArray(1, 1), ["cell_size"] = 1.0, ["base"] = "noise",
        ["seed"] = 1, ["height"] = 30, ["scale"] = 200, ["octaves"] = 5,
        ["heightmap"] = null, ["min_height"] = 0, ["max_height"] = 50, ["flat_height"] = 0,
        ["material"] = null, ["collision"] = true,
    };

    // ---- CSG brushes (Milestone 6, ADR-016): children of an entity with a `csg_model` component,
    // combined in child order. Sizes are a sensible start for building at human scale. ----

    public static readonly string[] BrushShapes = ["box", "wedge", "cylinder", "cone", "sphere", "stairs", "arch"];
    public static readonly string[] BrushOperations = ["add", "subtract", "intersect"];

    public static JsonObject Brush(string shape) => new()
    {
        ["shape"] = shape,
        ["operation"] = shape == "arch" ? "subtract" : "add",  // an arch is a doorway: it cuts
        ["size"] = shape switch
        {
            "box" => new JsonArray(4.0, 3.0, 0.3),          // a wall
            "wedge" => new JsonArray(4.0, 1.5, 3.0),        // a roof slope
            "stairs" => new JsonArray(1.2, 1.5, 2.4),
            "arch" => new JsonArray(1.0, 2.2, 1.0),     // a door opening (deeper than walls, to cut through)
            "sphere" => new JsonArray(1.0, 1.0, 1.0),
            _ => new JsonArray(1.0, 2.0, 1.0),
        },
        ["sides"] = shape is "sphere" ? 24 : 16,
        ["steps"] = 8,
        ["uv_scale"] = new JsonArray(1.0, 1.0),
        ["material"] = null,
    };

    /// <summary>A brush shape's logical faces, in the engine's order (noor/csg/csg.h, face_name).</summary>
    public static string[] BrushFaces(string shape) => shape switch
    {
        "box" => ["+X side", "-X side", "top", "bottom", "front", "back"],
        "wedge" => ["slope", "bottom", "back", "+X side", "-X side"],
        "cylinder" => ["side", "top", "bottom"],
        "cone" => ["side", "bottom"],
        "sphere" => ["surface"],
        "stairs" => ["treads", "risers", "sides", "back", "bottom"],
        "arch" => ["front", "back", "sides and bottom", "curve"],
        _ => [],
    };

    /// <summary>Where a new brush sits in its model: on the model's floor (y = half its height).</summary>
    public static Vec3 BrushPosition(JsonObject brush) =>
        new(0, (brush["size"] as JsonArray)?[1] is JsonNode h && Num.TryParse(h.ToJsonString(), out var y) ? y / 2 : 0.5, 0);

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
        ["camera"] = new JsonObject { ["fov"] = 75, ["near"] = 0.05, ["ev100"] = 14 },
        ["audio_listener"] = new JsonObject(),  // the player's ears
    };

    // ---- Create ▸ Player (third person) (Milestone 8C, part 4): Ward, our generated test character, on his controller.
    public const string ThirdPersonTemplateName = "Player (_third person)";
    public const string ThirdPersonScriptPath = "Scripts/ThirdPersonPlayer.luau";
    public const string WardModelPath = "Assets/Characters/Ward/Ward.glb";
    public const string WardControllerPath = "Assets/Characters/Ward/Ward.nooranim";

    public static JsonObject ThirdPersonComponents() => new()
    {
        ["character"] = new JsonObject { ["radius"] = 0.3, ["height"] = 1.8, ["step_height"] = 0.4, ["max_slope"] = 50 },
        ["script"] = new JsonObject
        {
            ["file"] = ThirdPersonScriptPath,
            ["params"] = new JsonObject { ["walk_speed"] = 1.6, ["run_speed"] = 4, ["jump_speed"] = 5, ["look_sensitivity"] = 0.12 },
        },
    };
    public static JsonObject ThirdPersonCamera() => new()
    {
        ["camera"] = new JsonObject { ["fov"] = 60, ["near"] = 0.05, ["ev100"] = 14 },
        ["audio_listener"] = new JsonObject(),
    };
    public static JsonObject WardAnimator(string controller) => new()
    {
        ["controller"] = controller,
        ["parameters"] = new JsonObject { ["speed"] = 0 },
    };

    public const string ThirdPersonScript = """
        -- ThirdPersonPlayer: walking and running with a camera behind (a NoorEngine template).
        -- The model (a child with an animator) turns to face where it goes; its controller's "speed" parameter
        -- blends idle, walk and run. Click into the game view to orbit the camera with the mouse; Esc gives it back.
        -- Params (Inspector): walk_speed, run_speed (Shift), jump_speed, look_sensitivity.
        -- API: docs/scripting/LUA_API.md; animation: docs/animation/ANIMATION.md

        local Player = {}

        function Player:on_start()
            self.rig = self.entity:child("CameraRig")
            self.model = self.entity:child("Ward")
            self.yaw, self.pitch, self.facing = 0, -10, 0
        end

        function Player:on_update(dt)
            local p = self.params

            -- The camera orbits while the mouse is captured.
            if noor.input.pressed("fire") and not noor.input.mouse_captured() then
                noor.input.capture_mouse(true)
            end
            if noor.input.mouse_captured() then
                local look = p.look_sensitivity or 0.12
                self.yaw -= noor.input.axis("look_x") * look
                self.pitch = math.clamp(self.pitch - noor.input.axis("look_y") * look, -60, 30)
            else
                noor.ui.text("Click to look around · Esc gives the mouse back", 0, -40, { size = 20, anchor = "bottom", color = "#FFFFFFB0" })
            end
            if self.rig then
                self.rig:set_euler(vector.create(self.pitch, self.yaw, 0))
            end

            -- Walking relative to where the camera looks (level), Shift to run.
            local yaw = math.rad(self.yaw)
            local forward = vector.create(-math.sin(yaw), 0, -math.cos(yaw))
            local right = vector.create(math.cos(yaw), 0, -math.sin(yaw))
            local move = forward * noor.input.axis("move_y") + right * noor.input.axis("move_x")
            if vector.magnitude(move) > 1 then
                move = vector.normalize(move)
            end
            local speed = if noor.input.down("sprint") then (p.run_speed or 4) else (p.walk_speed or 1.6)
            self.entity:character_move(move * speed)
            if noor.input.pressed("jump") then
                self.entity:character_jump(p.jump_speed or 5)
            end

            -- The model faces where it's going (it faces +Z), and its walk keeps pace.
            local moving = vector.magnitude(move)
            if self.model then
                if moving > 0.01 then
                    self.facing = math.deg(math.atan2(move.x, move.z))
                end
                self.model:set_euler(vector.create(0, self.facing, 0))
                self.model:set_animation_parameter("speed", moving * speed)
            end
        end

        return Player

        """;

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
                -- HUD: a small crosshair in the middle of the screen.
                noor.ui.rect(0, 0, 4, 4, { color = "#FFFFFFC0", anchor = "center" })
            else
                noor.ui.text("Click to look around · Esc gives the mouse back", 0, -40, { size = 20, anchor = "bottom", color = "#FFFFFFB0" })
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
