using System.Globalization;
using NoorEngine;

// The C# twin of Clock.luau.
public class CsClock : NoorScript
{
    private int _ticks, _updates;
    private double _afterAt = -1, _w1 = -1, _w2 = -1;

    public override async void OnStart()
    {
        After(0.5, () => _afterAt = Noor.Time);
        Every(0.25, () => ++_ticks < 4);
        await Wait(0.3);
        _w1 = Noor.Time;
        await Wait(0.5);
        _w2 = Noor.Time;
    }

    public override void OnUpdate(float dt)
    {
        _updates++;
        Ui.Text(string.Create(CultureInfo.InvariantCulture,
            $"after {_afterAt:F3}, ticks {_ticks}, waits {_w1:F3} {_w2:F3}, updates {_updates}"), 20, 20);
    }
}
