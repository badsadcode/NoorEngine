using System.Numerics;
using System.Text.Json.Nodes;
using NoorEngine.Interop;

namespace NoorEngine;

/// <summary>
/// A NoorEngine C# script. Derive from it, name the class like the file (Scripts/Spinner.cs →
/// class Spinner), override the hooks you need. Same hooks and API as Lua (docs/scripting).
/// </summary>
public abstract class NoorScript
{
    /// <summary>The entity this script runs on.</summary>
    public Entity Entity { get; internal set; } = null!;  // set before OnStart

    /// <summary>The script component's params (edit them in the Inspector with + Param).</summary>
    public JsonObject Params { get; internal set; } = new();

    /// <summary>A number param, or <paramref name="fallback"/> if it's missing or not a number.</summary>
    protected float Param(string name, float fallback) =>
        Params[name] is JsonValue v && v.TryGetValue<double>(out var d) ? (float)d : fallback;

    // ---- Timers and waiting (gameplay foundations, part 2) ----
    internal readonly Scheduler Jobs = new();

    /// <summary>Calls `action` once, `seconds` from now (game time; `real`: real time, which runs while paused for
    /// scripts marked "pause": "always"). Cancel with the returned timer. Gone with the entity.</summary>
    protected Timer After(double seconds, Action action, bool real = false) => Jobs.Add(seconds, 0, () => { action(); return true; }, real);
    /// <summary>Calls `action` every `seconds`; it stops when `action` returns false, or when cancelled.</summary>
    protected Timer Every(double seconds, Func<bool> action, bool real = false) =>
        Jobs.Add(seconds, seconds > 0 ? seconds : throw new ArgumentException("Every: the period must be more than 0 seconds"), action, real);
    /// <summary>Calls `action` every `seconds` until cancelled.</summary>
    protected Timer Every(double seconds, Action action, bool real = false) => Every(seconds, () => { action(); return true; }, real);
    /// <summary>`await Wait(1.5)` in an async method (async void hooks too): carries on that much later.</summary>
    protected WaitAwaitable Wait(double seconds, bool real = false) => new(Jobs, seconds, real);

    public virtual void OnStart() { }
    public virtual void OnUpdate(float dt) { }
    public virtual void OnFixedUpdate(float dt) { }
    public virtual void OnLateUpdate(float dt) { }
    public virtual void OnDestroy() { }
    public virtual void OnCollisionEnter(Entity other) { }
    public virtual void OnCollisionExit(Entity other) { }
    public virtual void OnTriggerEnter(Entity other) { }
    public virtual void OnTriggerExit(Entity other) { }
    /// <summary>A non-looping animation (a sprite's, or a model's clip) this script played (Entity.PlayAnimation) has finished.</summary>
    public virtual void OnAnimationEnd(string animation) { }
    /// <summary>A named moment of the playing animation was passed (a controller state's "events": footsteps...).</summary>
    public virtual void OnAnimationEvent(string name) { }
    /// <summary>A character's state in the water changed: "dry", "wading", "swimming" or "under" (its head under).</summary>
    public virtual void OnWater(string state) { }
}

/// <summary>
/// An entity in the running scene. Transforms are local (relative to the parent) unless the name
/// says World; angles are degrees; Euler is the Inspector's (pitch X, yaw Y, roll Z). Calls on an
/// entity that no longer exists do nothing and return defaults.
/// A class (a handle; equal when the ids are), so `Entity.Position = v` and
/// `hit.Entity.Position = v` compile: with a struct, C# refuses to set properties on a copy (CS1612).
/// </summary>
public sealed unsafe class Entity : IEquatable<Entity>
{
    internal readonly RtEntity Id;
    internal Entity(RtEntity id) => Id = id;

    public bool Exists => Engine.A.Exists(Id) != 0;
    public string Name => Engine.Str(Engine.A.Name(Id)) ?? "";

    public Entity? Parent
    {
        get
        {
            RtEntity p;
            return Engine.A.Parent(Id, &p) != 0 ? new Entity(p) : null;
        }
    }

    public Entity? Child(string name)
    {
        RtEntity c;
        fixed (byte* n = Engine.Utf8(name)) return Engine.A.Child(Id, n, &c) != 0 ? new Entity(c) : null;
    }

    /// <summary>The first entity of that name anywhere below this one: a model's bone (parent props to it).</summary>
    public Entity? Bone(string name)
    {
        RtEntity c;
        fixed (byte* n = Engine.Utf8(name)) return Engine.A.Bone(Id, n, &c) != 0 ? new Entity(c) : null;
    }

    public void Destroy() => Engine.A.Destroy(Id);

    private Vector3 Get(int channel)
    {
        RtVec3 v;
        return Engine.A.GetVec(Id, channel, &v) != 0 ? v.ToVector() : Vector3.Zero;
    }
    private void Set(int channel, Vector3 v) => Engine.A.SetVec(Id, channel, new RtVec3(v));

    public Vector3 Position { get => Get(Channel.Position); set => Set(Channel.Position, value); }
    public Vector3 Euler { get => Get(Channel.Euler); set => Set(Channel.Euler, value); }
    public Vector3 Scale { get => Get(Channel.Scale); set => Set(Channel.Scale, value); }
    public Vector3 WorldPosition => Get(Channel.WorldPosition);
    public Vector3 Forward => Get(Channel.Forward);
    public Vector3 Right => Get(Channel.Right);
    public Vector3 Up => Get(Channel.Up);

    /// <summary>Moves along the parent's axes.</summary>
    public void Translate(Vector3 delta) => Engine.A.Translate(Id, new RtVec3(delta), 0);
    /// <summary>Moves along the entity's own axes.</summary>
    public void TranslateLocal(Vector3 delta) => Engine.A.Translate(Id, new RtVec3(delta), 1);
    /// <summary>Turns about an axis (parent space).</summary>
    public void Rotate(Vector3 axis, float degrees) => Engine.A.Rotate(Id, new RtVec3(axis), degrees);

    /// <summary>A copy of the component's data, or null.</summary>
    public JsonNode? Get(string component)
    {
        fixed (byte* t = Engine.Utf8(component))
        {
            var json = Engine.Str(Engine.A.GetComponent(Id, t));
            return json == null ? null : JsonNode.Parse(json);
        }
    }

    /// <summary>Replaces the component's data (null removes the component).</summary>
    public void Set(string component, JsonNode? value)
    {
        fixed (byte* t = Engine.Utf8(component))
        {
            if (value == null)
            {
                Engine.A.SetComponent(Id, t, null);
                return;
            }
            fixed (byte* j = Engine.Utf8(value.ToJsonString())) Engine.A.SetComponent(Id, t, j);
        }
    }

    // ---- Physics (needs a collider + rigidbody, or a character) ----
    public Vector3 Velocity { get => Get(Channel.Velocity); set => Set(Channel.Velocity, value); }
    public void AddImpulse(Vector3 impulse) => Engine.A.Push(Id, 0, new RtVec3(impulse));
    public void AddForce(Vector3 force) => Engine.A.Push(Id, 1, new RtVec3(force));
    /// <summary>Character controller: walk with this horizontal velocity (m/s, world) this frame.</summary>
    public void CharacterMove(Vector3 velocity) => Engine.A.Push(Id, 2, new RtVec3(velocity));
    /// <summary>Character controller: jump (upward m/s) if standing on ground.</summary>
    public void CharacterJump(float speed = 5) => Engine.A.CharacterJump(Id, speed);
    /// <summary>Character controller: the jump button was let go; a rising jump keeps jump_cut of its speed (shorter jumps).</summary>
    public void CharacterJumpRelease() => Engine.A.Push(Id, 3, default);
    /// <summary>Character controller: fall through the one-way platform it stands on.</summary>
    public void CharacterDrop() => Engine.A.Push(Id, 4, default);
    public bool IsGrounded => Engine.A.IsGrounded(Id) != 0;
    /// <summary>Tilemap (2D part 4): the tile at cell (x, y), (0, 0) the bottom-left; -1 empty; null without a tilemap.</summary>
    public int? Tile(int x, int y) => Engine.A.Tile(Id, x, y) is var t && t >= -1 ? t : null;
    /// <summary>Tilemap: sets cell (x, y) (-1 empties it); false outside the map. Drawing and collision follow.</summary>
    public bool SetTile(int x, int y, int tile) => Engine.A.SetTile(Id, x, y, tile) != 0;
    /// <summary>Moves it at once (gameplay foundations, part 1): a body or character through physics, its velocity and
    /// jumps reset; `euler` null keeps its rotation. A respawn, a portal.</summary>
    public unsafe bool Teleport(Vector3 position, Vector3? euler = null)
    {
        var e = euler.HasValue ? new RtVec3(euler.Value) : default;
        return Engine.A.Teleport(Id, new RtVec3(position), euler.HasValue ? &e : null) != 0;
    }
    /// <summary>Groups (Godot's): whether it has the tag; add one; remove one.</summary>
    public unsafe bool HasTag(string tag) { fixed (byte* t = Engine.Utf8(tag)) return Engine.A.Tag(Id, 0, t) != 0; }
    public unsafe void AddTag(string tag) { fixed (byte* t = Engine.Utf8(tag)) Engine.A.Tag(Id, 1, t); }
    public unsafe void RemoveTag(string tag) { fixed (byte* t = Engine.Utf8(tag)) Engine.A.Tag(Id, 2, t); }
    /// <summary>Character (2D part 2b, Godot-style): what it touched in its last step.</summary>
    public unsafe bool IsOnFloor => Engine.A.CharacterContacts(Id, null) is var c && c > 0 && (c & 1) != 0;
    public unsafe bool IsOnWall => Engine.A.CharacterContacts(Id, null) is var c && c > 0 && (c & 2) != 0;
    public unsafe bool IsOnCeiling => Engine.A.CharacterContacts(Id, null) is var c && c > 0 && (c & 4) != 0;
    public unsafe Vector3? FloorNormal
    {
        get
        {
            RtVec3 n;
            var c = Engine.A.CharacterContacts(Id, &n);
            return c > 0 && (c & 1) != 0 ? n.ToVector() : null;
        }
    }
    /// <summary>Character controller in the water: "dry", "wading", "swimming" or "under" (null: not a character).</summary>
    public unsafe string? WaterState => Engine.A.CharacterWater(Id, null, null) switch
    {
        0 => "dry", 1 => "wading", 2 => "swimming", 3 => "under", _ => null,
    };
    /// <summary>How deep the water is at the character's feet (metres; negative: above it).</summary>
    public unsafe float FeetDepth { get { float f = 0; Engine.A.CharacterWater(Id, &f, null); return f; } }
    /// <summary>How deep the top of the character's head is under the water (positive: under).</summary>
    public unsafe float HeadDepth { get { float h = 0; Engine.A.CharacterWater(Id, null, &h); return h; } }

    // ---- Sound (needs an audio_source component) ----
    /// <summary>(Re)starts the entity's audio_source.</summary>
    public bool PlaySound() => Engine.A.Sound(Id, 0) != 0;
    public void StopSound() => Engine.A.Sound(Id, 1);
    public bool IsPlaying => Engine.A.Sound(Id, 2) != 0;

    // ---- Animations (needs an animator component, a model's clips, or a sprite component) ----
    /// <summary>Plays an animation from its start; a non-looping one calls OnAnimationEnd when it's done.</summary>
    public bool PlayAnimation(string animation, float speed = 1)
    {
        fixed (byte* a = Engine.Utf8(animation)) return Engine.A.Sprite(Id, 0, a, speed) != 0;
    }
    /// <summary>Freezes it where it is.</summary>
    public void StopAnimation() => Engine.A.Sprite(Id, 1, null, 0);
    /// <summary>Plays on faster or slower from here (0 freezes it).</summary>
    public void SetAnimationSpeed(float speed) => Engine.A.Sprite(Id, 2, null, speed);
    /// <summary>The animation it's playing (or showing), or null.</summary>
    public string? Animation => Engine.Str(Engine.A.SpriteAnimation(Id));
    /// <summary>From what's playing to <paramref name="animation"/> over <paramref name="seconds"/> (with a controller: a state).</summary>
    public bool CrossFade(string animation, float seconds)
    {
        fixed (byte* a = Engine.Utf8(animation)) return Engine.A.Sprite(Id, 3, a, seconds) != 0;
    }
    /// <summary>A parameter of the animator's controller.</summary>
    public bool SetAnimationParameter(string name, float value)
    {
        fixed (byte* n = Engine.Utf8(name)) return Engine.A.AnimationParameter(Id, n, value, 0) != 0;
    }
    /// <summary>A bool parameter of the animator's controller.</summary>
    public bool SetAnimationParameter(string name, bool value)
    {
        fixed (byte* n = Engine.Utf8(name)) return Engine.A.AnimationParameter(Id, n, value ? 1 : 0, 1) != 0;
    }
    /// <summary>Sets a trigger: the controller's next transition that tests it fires, and resets it.</summary>
    public bool SetAnimationTrigger(string name) => SetAnimationParameter(name, true);

    // ---- Effects (needs an emitter component) ----
    /// <summary>Spawns <paramref name="count"/> particles from the entity's emitter at the next fixed step.</summary>
    public bool Burst(int count) => Engine.A.Burst(Id, count) != 0;

    /// <summary>The entity's id (the same text as in the scene file and the Inspector).</summary>
    public string IdText
    {
        get
        {
            Span<byte> b = stackalloc byte[16];
            BitConverter.TryWriteBytes(b, Id.Lo);
            BitConverter.TryWriteBytes(b[8..], Id.Hi);
            var hex = Convert.ToHexString(b).ToLowerInvariant();
            return $"{hex[..8]}-{hex[8..12]}-{hex[12..16]}-{hex[16..20]}-{hex[20..]}";
        }
    }

    public bool Equals(Entity? other) => other is not null && Id.Lo == other.Id.Lo && Id.Hi == other.Id.Hi;
    public override bool Equals(object? obj) => obj is Entity e && Equals(e);
    public override int GetHashCode() => HashCode.Combine(Id.Lo, Id.Hi);
    public static bool operator ==(Entity? a, Entity? b) => a is null ? b is null : a.Equals(b);
    public static bool operator !=(Entity? a, Entity? b) => !(a == b);
    public override string ToString() => $"Entity({Name})";
}

public readonly record struct RaycastHit(Entity Entity, Vector3 Point, Vector3 Normal, float Distance);

/// <summary>World-level functions: logging, time, finding entities, raycasts.</summary>
public static unsafe class Noor
{
    public static void Log(string message) => Write(2, message);
    public static void Warn(string message) => Write(3, message);
    public static void Error(string message) => Write(4, message);

    private static void Write(int level, string message)
    {
        fixed (byte* m = Engine.Utf8(message)) Engine.A.Log(level, m);
    }

    /// <summary>Seconds of game time since play started (stops while paused, stretched by the time scale).</summary>
    public static double Time => Engine.A.Time();
    /// <summary>Seconds really passed since play started, paused or not (gameplay foundations, part 2).</summary>
    public static double RealTime => Engine.A.Clock(1);
    /// <summary>Pauses the game: its clock stops (physics, animation, game-time timers); only scripts marked
    /// "pause": "always" keep running.</summary>
    public static bool Paused { get => Engine.A.Clock(3) != 0; set => Engine.A.SetClock(3, value ? 1 : 0); }
    /// <summary>How fast game time runs: 1 normal, 0.25 slow motion (0..10).</summary>
    public static double TimeScale { get => Engine.A.Clock(2); set => Engine.A.SetClock(2, value); }

    public static Entity? Find(string name)
    {
        RtEntity e;
        fixed (byte* n = Engine.Utf8(name)) return Engine.A.Find(n, &e) != 0 ? new Entity(e) : null;
    }

    /// <summary>A new instance of a prefab (its asset id, or a path like "Assets/Bullet.noorprefab") at `position`,
    /// turned by `euler` (degrees), under `parent` if given; null if there's no such prefab. It starts next frame.</summary>
    public static Entity? Spawn(string prefab, Vector3 position, Vector3? euler = null, Entity? parent = null)
    {
        RtEntity e;
        var p = parent?.Id ?? default;
        fixed (byte* s = Engine.Utf8(prefab))
            return Engine.A.Spawn(s, new RtVec3(position), new RtVec3(euler ?? Vector3.Zero), parent is not null ? &p : null, &e) != 0 ? new Entity(e) : null;
    }

    /// <summary>Every entity with the tag (a group), in scene order.</summary>
    public static Entity[] FindAll(string tag)
    {
        var found = new RtEntity[64];
        int count;
        fixed (byte* t = Engine.Utf8(tag))
        fixed (RtEntity* o = found)
            count = Engine.A.FindAll(t, o, found.Length);
        if (count > found.Length)
        {
            found = new RtEntity[count];
            fixed (byte* t = Engine.Utf8(tag))
            fixed (RtEntity* o = found)
                count = Engine.A.FindAll(t, o, found.Length);
        }
        var result = new Entity[Math.Min(count, found.Length)];
        for (var i = 0; i < result.Length; i++) result[i] = new Entity(found[i]);
        return result;
    }

    /// <summary>
    /// Plays a sound once (an asset id or a path like "Assets/Audio/step.wav"), at a world position
    /// (3D) or everywhere (no position). Bus: master, music, sfx, ui, voice.
    /// </summary>
    public static bool PlaySound(string clip, Vector3? position = null, float volume = 1, string bus = "sfx")
    {
        var at = position.HasValue ? new RtVec3(position.Value) : default;
        fixed (byte* c = Engine.Utf8(clip))
        fixed (byte* b = Engine.Utf8(bus))
            return Engine.A.PlayOneShot(c, position.HasValue ? &at : null, volume, b) != 0;
    }

    /// <summary>Volume of a bus (master, music, sfx, ui, voice), 0..1 (more is louder).</summary>
    public static void SetVolume(string bus, float volume)
    {
        fixed (byte* b = Engine.Utf8(bus)) Engine.A.SetBusVolume(b, volume);
    }

    /// <summary>The first physics body along the ray, or null.</summary>
    public static RaycastHit? Raycast(Vector3 origin, Vector3 direction, float maxDistance = 1000, Entity? ignore = null)
    {
        RtHit hit;
        var ignored = ignore?.Id ?? default;
        Engine.A.Raycast(new RtVec3(origin), new RtVec3(direction), maxDistance, ignore is not null ? &ignored : null, &hit);
        return hit.Hit != 0 ? new RaycastHit(new Entity(hit.Entity), hit.Point.ToVector(), hit.Normal.ToVector(), hit.Distance) : null;
    }

    /// <summary>A shape (collider JSON, e.g. {"shape":"circle","radius":0.3}) swept from `from` to `to`: the first hit, or null.</summary>
    public static RaycastHit? ShapeCast(string shapeJson, Vector3 from, Vector3 to, Entity? ignore = null)
    {
        RtHit hit;
        var ignored = ignore?.Id ?? default;
        fixed (byte* s = Engine.Utf8(shapeJson))
            Engine.A.ShapeCast(s, new RtVec3(from), new RtVec3(to), ignore is not null ? &ignored : null, &hit);
        return hit.Hit != 0 ? new RaycastHit(new Entity(hit.Entity), hit.Point.ToVector(), hit.Normal.ToVector(), hit.Distance) : null;
    }

    /// <summary>Every entity whose body overlaps the shape placed at `at`.</summary>
    public static Entity[] Overlap(string shapeJson, Vector3 at, Entity? ignore = null)
    {
        var ignored = ignore?.Id ?? default;
        var found = new RtEntity[64];
        int count;
        fixed (byte* s = Engine.Utf8(shapeJson))
        fixed (RtEntity* o = found)
            count = Engine.A.Overlap(s, new RtVec3(at), ignore is not null ? &ignored : null, o, found.Length);
        if (count > found.Length)
        {
            found = new RtEntity[count];
            fixed (byte* s = Engine.Utf8(shapeJson))
            fixed (RtEntity* o = found)
                count = Engine.A.Overlap(s, new RtVec3(at), ignore is not null ? &ignored : null, o, found.Length);
        }
        var result = new Entity[Math.Min(count, found.Length)];
        for (var i = 0; i < result.Length; i++) result[i] = new Entity(found[i]);
        return result;
    }
}

/// <summary>Water (Milestone 8D): the surface where gameplay meets it, waves included.</summary>
public static unsafe class Water
{
    /// <summary>The water surface's height (world y) at world (x, z) now, or null where there's no water.</summary>
    public static float? Height(float x, float z)
    {
        float h;
        return Engine.A.Water(x, z, &h, null) != 0 ? h : null;
    }

    /// <summary>The surface's normal at world (x, z) now (up, tilted by the waves), or null where there's no water.</summary>
    public static Vector3? Normal(float x, float z)
    {
        RtVec3 n;
        return Engine.A.Water(x, z, null, &n) != 0 ? n.ToVector() : null;
    }

    /// <summary>The water's own velocity at world (x, z) now (m/s: a river's current; still water: zero), or null where there's no water.</summary>
    public static Vector3? Flow(float x, float z)
    {
        RtVec3 f;
        return Engine.A.WaterFlow(x, z, &f) != 0 ? f.ToVector() : null;
    }
}

/// <summary>Input actions and axes (the project's Settings/input.json) and mouse capture.</summary>
public static unsafe class Input
{
    public static bool Down(string action) => Query(0, action);
    public static bool Pressed(string action) => Query(1, action);
    public static bool Released(string action) => Query(2, action);

    private static bool Query(int q, string action)
    {
        fixed (byte* a = Engine.Utf8(action)) return Engine.A.Input(q, a) != 0;
    }

    /// <summary>-1..1 for key axes; mouse movement for look_x / look_y.</summary>
    public static float Axis(string name)
    {
        fixed (byte* n = Engine.Utf8(name)) return Engine.A.Axis(n);
    }

    /// <summary>Ask to capture the mouse for looking (works once the player clicked into the game view; Esc releases it).</summary>
    public static void CaptureMouse(bool capture = true) => Engine.A.CaptureMouse(capture ? 1 : 0);
    public static bool MouseCaptured => Engine.A.MouseCaptured() != 0;
}

/// <summary>
/// The HUD: text and rectangles drawn over the game this frame (call every frame you want them
/// shown). Positions and sizes in UI units: the screen is 720 tall, as wide as its shape gives.
/// Anchor: top_left, top, top_right, left, center, right, bottom_left, bottom, bottom_right; it is
/// both where the position is measured from and the point of the element placed there.
/// Colors: "#RRGGBB" or "#RRGGBBAA".
/// </summary>
public static unsafe class Ui
{
    public static void Text(string text, float x, float y, float size = 24, string color = "#FFFFFF", string anchor = "top_left")
    {
        fixed (byte* t = Engine.Utf8(text))
        fixed (byte* c = Engine.Utf8(color))
        fixed (byte* a = Engine.Utf8(anchor))
            if (Engine.A.UiText(t, x, y, size, c, a) == 0) throw Bad(color, anchor);
    }

    public static void Rect(float x, float y, float width, float height, string color = "#FFFFFF", string anchor = "top_left")
    {
        fixed (byte* c = Engine.Utf8(color))
        fixed (byte* a = Engine.Utf8(anchor))
            if (Engine.A.UiRect(x, y, width, height, c, a) == 0) throw Bad(color, anchor);
    }

    private static ArgumentException Bad(string color, string anchor) =>
        new($"Ui: color \"{color}\" or anchor \"{anchor}\" not understood (colors: \"#RRGGBB\" or \"#RRGGBBAA\"; anchors: top_left … bottom_right)");
}
