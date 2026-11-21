using System.Collections.Generic;
using System.Globalization;
using System.Numerics;
using NoorEngine;

// The C# twin of Spawner.luau: the same bullets, tags and teleport (gameplay foundations, part 1).
public class CsSpawner : NoorScript
{
    private readonly List<Entity> _spawned = new();
    private bool _teleported;

    private static float Lane(int i) => (i - 5.5f) * 0.8f;

    public override void OnUpdate(float dt)
    {
        var t = Noor.Time;
        while (_spawned.Count < 10 && t >= _spawned.Count * 0.1)
        {
            var i = _spawned.Count + 1;
            var b = Noor.Spawn("Assets/Bullet.noorprefab", new Vector3(Lane(i), 3 + i * 0.1f, 0))!;
            if (i % 2 == 0) b.AddTag("even");
            _spawned.Add(b);
        }
        if (t >= 1.5 && !_teleported) _teleported = _spawned[0].Teleport(new Vector3(0, 5, 0));
        var trail = _spawned.Count > 0 && _spawned[0].Child("Trail") != null;
        Ui.Text($"spawned {_spawned.Count}, bullets {Noor.FindAll("bullet").Length}, even {Noor.FindAll("even").Length}, " +
                $"trail {(trail ? "true" : "false")}, teleported {(_teleported ? "true" : "false")}", 20, 20);
        if (_spawned.Count > 0)
        {
            var p = _spawned[0].WorldPosition;
            Ui.Text(string.Create(CultureInfo.InvariantCulture, $"first {p.X:F2} {p.Y:F2}"), 20, 60);  // dots, whatever the PC's language
        }
    }
}
