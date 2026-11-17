using System.Numerics;
using System.Text.Json.Nodes;
using NoorEngine;

// Shooter: the game itself. Click to shoot along the crosshair; hit the gold targets before the
// round ends. Lives on the Camera (so its forward is where you look).
//
// Parts of the game ("state", shared with FpsPlayer through this entity's "game" component):
//   demo    - before the first click: the game plays itself; click to play
//   playing - a round is on; Esc frees the mouse and pauses
//   paused  - click to carry on
//   over    - time's up: the score; click to play again
// The Lua twin is Shooter.luau.
public class Shooter : NoorScript
{
    private const string ShotSound = "Assets/Audio/shot.wav";
    private const string HitSound = "Assets/Audio/hit.wav";
    private const string EndSound = "Assets/Audio/round_end.wav";
    private const string TargetColor = "#F2C14E";
    private const string FlashColor = "#FFFFFF";

    // Where targets appear after a hit (x, height, z), in front of the starting spot.
    private static readonly Vector3[] Spots =
    [
        new(-8, 1.8f, -8), new(7, 1.5f, -10), new(0, 2.2f, -16), new(12, 1.2f, -2), new(-14, 2.0f, -2),
        new(4, 1.4f, -4), new(-4, 2.6f, -18), new(15, 2.4f, -15), new(-16, 1.3f, -12), new(9, 2.8f, 9),
        new(-10, 1.6f, 14), new(16, 1.8f, 6), new(-3, 1.2f, 2), new(1, 3.0f, -9),
    ];

    private string _state = "demo";
    private float _timeLeft;
    private float _cooldown;
    private float _hitMarker;
    private int _hits;
    private int _shots;
    private bool _wasCaptured;
    private Entity? _player;
    private readonly List<Entity> _targets = new();
    private readonly Dictionary<Entity, float> _hitTargets = new();  // flashing white, then moved
    private readonly Random _random = new(7);

    public override void OnStart()
    {
        _player = Entity.Parent;
        for (var i = 1; Noor.Find($"Target {i}") is { } target; i++) _targets.Add(target);
        Publish();
    }

    public override void OnUpdate(float dt)
    {
        _cooldown -= dt;
        _hitMarker -= dt;
        var click = Input.Pressed("fire");

        switch (_state)
        {
            case "demo":
                if (click) StartRound();
                else if (_cooldown <= 0 && TargetUnderCrosshair()) Fire();
                break;
            case "playing":
                if (Input.MouseCaptured) _wasCaptured = true;
                if (_wasCaptured && !Input.MouseCaptured)
                {
                    _state = "paused";  // Esc gave the mouse back
                    break;
                }
                if (click && _cooldown <= 0) Fire();
                _timeLeft -= dt;
                if (_timeLeft <= 0)
                {
                    _timeLeft = 0;
                    _state = "over";
                    Noor.PlaySound(EndSound, null, 0.8f);
                }
                break;
            case "paused":
                if (click) Resume();
                break;
            case "over":
                if (click) StartRound();
                break;
        }

        UpdateHitTargets(dt);
        Publish();
        DrawHud();
    }

    private void StartRound()
    {
        _hits = 0;
        _shots = 0;
        _timeLeft = Param("round_seconds", 60);
        _cooldown = 0.2f;  // the click that starts the round doesn't shoot
        Resume();
    }

    private void Resume()
    {
        _state = "playing";
        _wasCaptured = false;
        Input.CaptureMouse(true);
    }

    private bool TargetUnderCrosshair() =>
        Noor.Raycast(Entity.WorldPosition, Entity.Forward, 200, _player) is { } hit && IsLiveTarget(hit.Entity);

    private bool IsLiveTarget(Entity e) => e.Get("target") != null && !_hitTargets.ContainsKey(e);

    private void Fire()
    {
        _shots++;
        _cooldown = _state == "demo" ? 0.6f : 0.15f;
        Noor.PlaySound(ShotSound, null, 0.6f);
        if (Noor.Raycast(Entity.WorldPosition, Entity.Forward, 200, _player) is not { } hit) return;

        if (IsLiveTarget(hit.Entity))
        {
            _hits++;
            _hitMarker = 0.15f;
            Noor.PlaySound(HitSound, hit.Point);
            SetColor(hit.Entity, FlashColor);
            _hitTargets[hit.Entity] = 0.15f;
        }
        else if (hit.Entity.Get("rigidbody")?["type"]?.GetValue<string>() == "dynamic")
        {
            hit.Entity.AddImpulse(Entity.Forward * 40);  // crates get knocked about
        }
    }

    // A hit target flashes white for a moment, then turns up somewhere else.
    private void UpdateHitTargets(float dt)
    {
        foreach (var target in _hitTargets.Keys.ToList())
        {
            _hitTargets[target] -= dt;
            if (_hitTargets[target] > 0) continue;
            _hitTargets.Remove(target);
            target.Position = FreeSpot();
            SetColor(target, TargetColor);
        }
    }

    private Vector3 FreeSpot()
    {
        var player = _player?.WorldPosition ?? Vector3.Zero;
        for (var attempt = 0; attempt < 20; attempt++)
        {
            var spot = Spots[_random.Next(Spots.Length)];
            var flat = new Vector2(spot.X - player.X, spot.Z - player.Z);
            if (flat.Length() < 6) continue;  // not right in the player's face
            if (_targets.Any(t => Vector3.Distance(t.WorldPosition, spot) < 1.5f)) continue;  // taken
            return spot;
        }
        return Spots[_random.Next(Spots.Length)];
    }

    private static void SetColor(Entity e, string color)
    {
        if (e.Get("primitive") is not JsonObject primitive) return;
        primitive["color"] = color;
        e.Set("primitive", primitive);
    }

    private void Publish() => Entity.Set("game", new JsonObject { ["state"] = _state });

    // ---- HUD (UI units: the screen is 720 tall) ----

    private void DrawHud()
    {
        var accuracy = _shots == 0 ? 0 : (int)Math.Round(100.0 * _hits / _shots);
        if (_state is "playing" or "demo")
        {
            if (_hitMarker > 0) Ui.Rect(0, 0, 10, 10, "#FF5050", "center");
            else Ui.Rect(0, 0, 4, 4, "#FFFFFFC0", "center");
        }

        switch (_state)
        {
            case "demo":
                Ui.Text($"DEMO · Hits: {_hits}", 24, 20, 22, "#FFFFFFA0");
                // Low on the screen, so the demo's shooting stays in view.
                Ui.Rect(0, -30, 760, 190, "#100C1CC0", "bottom");
                Ui.Text("NoorFPS-lite", 0, -140, 56, "#F2C14E", "bottom");
                if ((int)(Noor.Time * 2) % 2 == 0) Ui.Text("Click to play", 0, -90, 28, "#FFFFFF", "bottom");
                Ui.Text("WASD walk · Shift sprint · Space jump · Click shoot · Esc frees the mouse", 0, -50, 18, "#FFFFFFB0", "bottom");
                break;
            case "playing":
                Ui.Text($"Hits: {_hits}", 24, 20, 36);
                Ui.Text($"Shots: {_shots} · {accuracy}%", 24, 62, 20, "#FFFFFFB0");
                var seconds = (int)Math.Ceiling(_timeLeft);
                Ui.Text($"{seconds / 60}:{seconds % 60:00}", 0, 20, 40, seconds <= 10 ? "#FF5050" : "#FFFFFF", "top");
                break;
            case "paused":
                Panel(160);
                Ui.Text("Paused", 0, -25, 56, "#FFFFFF", "center");
                Ui.Text("Click to carry on", 0, 35, 26, "#FFFFFFC0", "center");
                break;
            case "over":
                Panel(240);
                Ui.Text("Time!", 0, -65, 64, "#F2C14E", "center");
                Ui.Text($"Hits: {_hits} · Accuracy: {accuracy}%", 0, 10, 32, "#FFFFFF", "center");
                Ui.Text("Click to play again", 0, 70, 24, "#FFFFFFC0", "center");
                break;
        }
    }

    private static void Panel(float height) => Ui.Rect(0, 0, 760, height, "#100C1CC0", "center");
}
