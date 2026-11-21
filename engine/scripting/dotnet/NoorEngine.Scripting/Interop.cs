using System.Runtime.InteropServices;
using System.Text;

namespace NoorEngine.Interop;

// Mirrors engine/scripting/include/noor_rt_api.h. Field order and types must match exactly.

[StructLayout(LayoutKind.Sequential)]
public struct RtEntity
{
    public ulong Lo, Hi;
}

[StructLayout(LayoutKind.Sequential)]
public struct RtVec3
{
    public float X, Y, Z;
    public RtVec3(System.Numerics.Vector3 v) { X = v.X; Y = v.Y; Z = v.Z; }
    public readonly System.Numerics.Vector3 ToVector() => new(X, Y, Z);
}

[StructLayout(LayoutKind.Sequential)]
public struct RtHit
{
    public int Hit;
    public RtEntity Entity;
    public RtVec3 Point, Normal;
    public float Distance;
}

internal static class Channel
{
    public const int Position = 0, Euler = 1, Scale = 2, WorldPosition = 3, Forward = 4, Right = 5, Up = 6, Velocity = 7;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct RtApi
{
    public uint Version;
    public uint Size;
    public delegate* unmanaged<int, byte*, void> Log;
    public delegate* unmanaged<double> Time;
    public delegate* unmanaged<byte*, RtEntity*, int> Find;
    public delegate* unmanaged<RtEntity, int> Exists;
    public delegate* unmanaged<RtEntity, byte*> Name;
    public delegate* unmanaged<RtEntity, RtEntity*, int> Parent;
    public delegate* unmanaged<RtEntity, byte*, RtEntity*, int> Child;
    public delegate* unmanaged<RtEntity, void> Destroy;
    public delegate* unmanaged<RtEntity, int, RtVec3*, int> GetVec;
    public delegate* unmanaged<RtEntity, int, RtVec3, void> SetVec;
    public delegate* unmanaged<RtEntity, RtVec3, int, void> Translate;
    public delegate* unmanaged<RtEntity, RtVec3, float, void> Rotate;
    public delegate* unmanaged<RtEntity, byte*, byte*> GetComponent;
    public delegate* unmanaged<RtEntity, byte*, byte*, void> SetComponent;
    public delegate* unmanaged<int, byte*, int> Input;
    public delegate* unmanaged<byte*, float> Axis;
    public delegate* unmanaged<int, void> CaptureMouse;
    public delegate* unmanaged<int> MouseCaptured;
    public delegate* unmanaged<RtVec3, RtVec3, float, RtEntity*, RtHit*, void> Raycast;
    public delegate* unmanaged<RtEntity, int, RtVec3, void> Push;
    public delegate* unmanaged<RtEntity, float, void> CharacterJump;
    public delegate* unmanaged<RtEntity, int> IsGrounded;
    // v2: sound
    public delegate* unmanaged<RtEntity, int, int> Sound;
    public delegate* unmanaged<byte*, RtVec3*, float, byte*, int> PlayOneShot;
    public delegate* unmanaged<byte*, float, void> SetBusVolume;
    // v3: HUD
    public delegate* unmanaged<byte*, float, float, float, byte*, byte*, int> UiText;
    public delegate* unmanaged<float, float, float, float, byte*, byte*, int> UiRect;
    // v4: sprites and effects
    public delegate* unmanaged<RtEntity, int, byte*, float, int> Sprite;
    public delegate* unmanaged<RtEntity, byte*> SpriteAnimation;
    public delegate* unmanaged<RtEntity, int, int> Burst;
    public delegate* unmanaged<byte*> EventName;
    // v5: skeletal animation
    public delegate* unmanaged<RtEntity, byte*, float, int, int> AnimationParameter;
    // v6: bones, animation events
    public delegate* unmanaged<RtEntity, byte*, RtEntity*, int> Bone;
    // v7: water
    public delegate* unmanaged<float, float, float*, RtVec3*, int> Water;
    // v8: characters in water
    public delegate* unmanaged<RtEntity, float*, float*, int> CharacterWater;
    // v9: rivers' flow
    public delegate* unmanaged<float, float, RtVec3*, int> WaterFlow;
    // v11: shape casts, overlaps, character contacts (2D part 2b)
    public delegate* unmanaged<byte*, RtVec3, RtVec3, RtEntity*, RtHit*, void> ShapeCast;
    public delegate* unmanaged<byte*, RtVec3, RtEntity*, RtEntity*, int, int> Overlap;
    public delegate* unmanaged<RtEntity, RtVec3*, int> CharacterContacts;
    // v12: tilemaps (2D part 4)
    public delegate* unmanaged<RtEntity, int, int, int> Tile;
    public delegate* unmanaged<RtEntity, int, int, int, int> SetTile;
    // v13: spawning, teleport, groups (gameplay foundations, part 1)
    public delegate* unmanaged<byte*, RtVec3, RtVec3, RtEntity*, RtEntity*, int> Spawn;
    public delegate* unmanaged<RtEntity, RtVec3, RtVec3*, int> Teleport;
    public delegate* unmanaged<RtEntity, int, byte*, int> Tag;
    public delegate* unmanaged<byte*, RtEntity*, int, int> FindAll;
    // v14: the clocks (gameplay foundations, part 2)
    public delegate* unmanaged<int, double> Clock;
    public delegate* unmanaged<int, double, void> SetClock;
    // v15: messages and calls (gameplay foundations, part 3)
    public delegate* unmanaged<RtEntity*, byte*, byte*, int> Emit;
    public delegate* unmanaged<RtEntity*, byte*, RtEntity, long, long> Listen;
    public delegate* unmanaged<long, void> Unlisten;
    public delegate* unmanaged<RtEntity, byte*, byte*, int*, byte*> Call;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct RtHost
{
    public delegate* unmanaged<RtEntity, byte*, byte*, long> Create;
    public delegate* unmanaged<long, int, float, int> Call;
    public delegate* unmanaged<long, int, RtEntity, int> Event;
    public delegate* unmanaged<long, void> Release;
    public delegate* unmanaged<byte*> LastError;
    public delegate* unmanaged<long, long, RtEntity*, byte*, byte*, int> Deliver;
    public delegate* unmanaged<long, byte*, byte*, int*, byte*> Invoke;
}

/// <summary>The engine's function table, set by <see cref="ScriptHost.Initialize"/>.</summary>
internal static unsafe class Engine
{
    public const uint ExpectedVersion = 15;
    public static RtApi* Api;

    public static ref RtApi A
    {
        get
        {
            if (Api == null) throw new InvalidOperationException("NoorEngine isn't running scripts right now.");
            return ref *Api;
        }
    }

    /// <summary>A NUL-terminated UTF-8 copy of <paramref name="s"/> for one call.</summary>
    public static byte[] Utf8(string s) => Encoding.UTF8.GetBytes(s + "\0");

    public static string? Str(byte* p) => p == null ? null : Marshal.PtrToStringUTF8((nint)p);
}
