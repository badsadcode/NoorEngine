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
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct RtHost
{
    public delegate* unmanaged<RtEntity, byte*, byte*, long> Create;
    public delegate* unmanaged<long, int, float, int> Call;
    public delegate* unmanaged<long, int, RtEntity, int> Event;
    public delegate* unmanaged<long, void> Release;
    public delegate* unmanaged<byte*> LastError;
}

/// <summary>The engine's function table, set by <see cref="ScriptHost.Initialize"/>.</summary>
internal static unsafe class Engine
{
    public const uint ExpectedVersion = 3;
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
