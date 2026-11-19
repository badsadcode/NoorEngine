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

    public virtual void OnStart() { }
    public virtual void OnUpdate(float dt) { }
    public virtual void OnFixedUpdate(float dt) { }
    public virtual void OnLateUpdate(float dt) { }
    public virtual void OnDestroy() { }
    public virtual void OnCollisionEnter(Entity other) { }
    public virtual void OnCollisionExit(Entity other) { }
    public virtual void OnTriggerEnter(Entity other) { }
    public virtual void OnTriggerExit(Entity other) { }
    /// <summary>A non-looping sprite animation this script played (Entity.PlayAnimation) has finished.</summary>
    public virtual void OnAnimationEnd(string animation) { }
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
    public bool IsGrounded => Engine.A.IsGrounded(Id) != 0;

    // ---- Sound (needs an audio_source component) ----
    /// <summary>(Re)starts the entity's audio_source.</summary>
    public bool PlaySound() => Engine.A.Sound(Id, 0) != 0;
    public void StopSound() => Engine.A.Sound(Id, 1);
    public bool IsPlaying => Engine.A.Sound(Id, 2) != 0;

    // ---- Sprites (needs a sprite component) ----
    /// <summary>Plays a sprite animation from its start; a non-looping one calls OnAnimationEnd when it's done.</summary>
    public bool PlayAnimation(string animation, float speed = 1)
    {
        fixed (byte* a = Engine.Utf8(animation)) return Engine.A.Sprite(Id, 0, a, speed) != 0;
    }
    /// <summary>Freezes the sprite on the frame it's showing.</summary>
    public void StopAnimation() => Engine.A.Sprite(Id, 1, null, 0);
    /// <summary>Plays on faster or slower from here (0 freezes it).</summary>
    public void SetAnimationSpeed(float speed) => Engine.A.Sprite(Id, 2, null, speed);
    /// <summary>The animation it's playing (or showing), or null.</summary>
    public string? Animation => Engine.Str(Engine.A.SpriteAnimation(Id));

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

    /// <summary>Seconds since play started.</summary>
    public static double Time => Engine.A.Time();

    public static Entity? Find(string name)
    {
        RtEntity e;
        fixed (byte* n = Engine.Utf8(name)) return Engine.A.Find(n, &e) != 0 ? new Entity(e) : null;
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
