using System;
using System.Globalization;
using NoorEngine;

// The C# twin of Pauser.luau (runs while paused: "pause": "always").
public class CsPauser : NoorScript
{
    private Entity _box = null!;
    private double _pausedAt = -1, _resumed = -1, _boxY;
    private bool? _frozen;

    public override void OnStart() => _box = Noor.Find("Box")!;

    public override void OnUpdate(float dt)
    {
        if (_pausedAt < 0 && Noor.Time >= 1.0)
        {
            Noor.Paused = true;
            _pausedAt = Noor.RealTime;
            _boxY = _box.WorldPosition.Y;
            After(0.5, () =>
            {
                _frozen = Math.Abs(_box.WorldPosition.Y - _boxY) < 1e-6;
                Noor.Paused = false;
                Noor.TimeScale = 0.5;
                _resumed = Noor.RealTime;
            }, real: true);
        }
        var frozen = _frozen is null ? "nil" : _frozen.Value ? "true" : "false";
        Ui.Text(string.Create(CultureInfo.InvariantCulture,
            $"paused {(_resumed >= 0 ? _resumed - _pausedAt : -1):F3} real s, frozen {frozen}, scale {Noor.TimeScale:F2}, game {Noor.Time:F3}, real {Noor.RealTime:F3}"), 20, 60);
    }
}
