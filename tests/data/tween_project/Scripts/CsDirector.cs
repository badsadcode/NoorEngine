using System.Globalization;
using System.Numerics;
using NoorEngine;

// The C# twin of Director.luau.
public class CsDirector : NoorScript
{
    private Entity _door = null!, _platform = null!;
    private int _steps;

    public override void OnStart()
    {
        _door = Noor.Find("Door")!;
        _platform = Noor.Find("Platform")!;
        Tween(_door, "position", new Vector3(0, 3, 0), 0.5, "ease_out", onDone: () =>
        {
            _steps++;
            Tween(_door, "scale", new Vector3(1.5f, 1.5f, 1.5f), 1.0, "bounce_out", onDone: () =>
            {
                _steps++;
                Tween(_door, "primitive.color", "#FF0000", 0.5, onDone: () => _steps++);
            });
        });
        _platform.Tween("position", new Vector3(3, -1, 0), 1.0, "ease_in_out", loops: -1, yoyo: true);
    }

    public override void OnUpdate(float dt)
    {
        var d = _door.Position;
        var colour = (string?)_door.Get("primitive")?["color"] ?? "?";
        Ui.Text(string.Create(CultureInfo.InvariantCulture,
            $"steps {_steps}, door y {d.Y:F3}, scale {_door.Scale.X:F3}, colour {colour}, platform x {_platform.Position.X:F3}"), 20, 20);
    }
}
