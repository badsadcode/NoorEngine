using System.Numerics;
using NoorEngine;

// FpsPlayer: walk (WASD), sprint (Shift), jump (Space), look (mouse), footsteps.
// Before the first click the game runs its demo: the player stands still and turns towards the
// nearest target by itself, and Shooter (on the Camera) fires when one is under the crosshair.
// Shooter says which part of the game we're in through the Camera's "game" component.
// The Lua twin is FpsPlayer.luau (Scenes/Main_Lua.noorscene).
public class FpsPlayer : NoorScript
{
    private const string StepSound = "Assets/Audio/step.wav";
    private const float DemoTurnSpeed = 150;  // degrees per second

    private float _yaw;
    private float _pitch;
    private float _stepTimer;
    private Entity? _camera;
    private readonly List<Entity> _targets = new();

    public override void OnStart()
    {
        _yaw = Entity.Euler.Y;
        _camera = Entity.Child("Camera");
        for (var i = 1; Noor.Find($"Target {i}") is { } target; i++) _targets.Add(target);
    }

    public override void OnUpdate(float dt)
    {
        var state = _camera?.Get("game")?["state"]?.GetValue<string>() ?? "demo";

        if (state == "demo")
        {
            AimAtNearestTarget(dt);
        }
        else if (Input.MouseCaptured)
        {
            var look = Param("look_sensitivity", 0.12f);
            _yaw -= Input.Axis("look_x") * look;
            _pitch = Math.Clamp(_pitch - Input.Axis("look_y") * look, -85, 85);
        }
        Entity.Euler = new Vector3(0, _yaw, 0);
        if (_camera is { } camera) camera.Euler = new Vector3(_pitch, 0, 0);

        if (state != "playing") return;  // no walking in the demo, while paused or on the results

        var move = Entity.Forward * Input.Axis("move_y") + Entity.Right * Input.Axis("move_x");
        if (move.Length() > 1) move = Vector3.Normalize(move);
        var sprinting = Input.Down("sprint");
        var speed = Param("speed", 5) * (sprinting ? Param("sprint", 1.7f) : 1);
        Entity.CharacterMove(move * speed);
        if (Input.Pressed("jump")) Entity.CharacterJump(Param("jump_speed", 5));

        // A footstep every so often while walking on the ground (faster when sprinting).
        if (Entity.IsGrounded && move.Length() > 0.1f)
        {
            _stepTimer -= dt;
            if (_stepTimer <= 0)
            {
                Noor.PlaySound(StepSound, Entity.WorldPosition, 0.5f);
                _stepTimer = sprinting ? 0.3f : 0.45f;
            }
        }
        else
        {
            _stepTimer = 0.1f;  // the first step comes soon after starting to walk
        }
    }

    private void AimAtNearestTarget(float dt)
    {
        if (_camera is not { } camera) return;
        var eye = camera.WorldPosition;
        Vector3? nearest = null;
        foreach (var target in _targets)
        {
            var to = target.WorldPosition - eye;
            if (nearest == null || to.LengthSquared() < nearest.Value.LengthSquared()) nearest = to;
        }
        if (nearest is not { } aim) return;
        // Forward is -Z: yaw turns it about +Y, pitch (positive looks up) about +X.
        var wantYaw = MathF.Atan2(-aim.X, -aim.Z) * 180 / MathF.PI;
        var wantPitch = MathF.Atan2(aim.Y, MathF.Sqrt(aim.X * aim.X + aim.Z * aim.Z)) * 180 / MathF.PI;
        _yaw = TurnTowards(_yaw, wantYaw, DemoTurnSpeed * dt);
        _pitch = TurnTowards(_pitch, wantPitch, DemoTurnSpeed * dt);
    }

    private static float TurnTowards(float current, float target, float maxStep)
    {
        var delta = ((target - current) % 360 + 540) % 360 - 180;  // the short way round
        return Math.Abs(delta) <= maxStep ? current + delta : current + Math.Sign(delta) * maxStep;
    }
}
