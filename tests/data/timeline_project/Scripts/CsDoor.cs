using System.Collections.Generic;
using System.Globalization;
using NoorEngine;

// The C# twin of Door.luau.
public class CsDoor : NoorScript
{
    private readonly List<string> _log = new();

    public override void OnStart()
    {
        After(0.5, () => Entity.PlayTimeline("open", 1.5f));
        On(Entity.Child("Hinge")!, "latched", (data, _) => _log.Add("latched " + (string?)data?["how"]));
        On(Entity, "timeline_end", (data, _) => _log.Add("end " + (string?)data?["name"]));
    }

    public void creak(int n) => _log.Add("creak x" + n);

    public override void OnUpdate(float dt)
    {
        var playing = Entity.Timeline ?? "nil";
        var light = (double?)Entity.Get("light")?["intensity"] ?? -1;
        Ui.Text(string.Create(CultureInfo.InvariantCulture,
            $"playing {playing}, hinge {Entity.Child("Hinge")!.Euler.Y:F2}, light {light:F1}, {string.Join(", ", _log)}"), 20, 20);
    }
}
