using NoorEngine;

// Ward walks for a second, then waves (a one-shot clip, at double speed); when the wave has ended he nods, and when the
// nod has ended he walks on (Milestone 8C, part 2): ward_wave.luau in C#. The built-game check runs both.
public class WardWave : NoorScript
{
    private int _frames;  // frames, not summed seconds: C# sums floats and Lua doubles, so "a second" could land a frame apart
    private bool _waved;

    public override void OnUpdate(float dt)
    {
        if (_waved) return;
        if (++_frames >= 60)
        {
            _waved = true;
            Entity.Set("animator", new System.Text.Json.Nodes.JsonObject { ["animation"] = "Walk", ["loop"] = false });  // one-shots from here on
            Entity.PlayAnimation("Wave", 2);
        }
    }

    public override void OnAnimationEnd(string animation)
    {
        if (animation == "Wave") Entity.PlayAnimation("Nod");
        else if (animation == "Nod")
        {
            Entity.Set("animator", new System.Text.Json.Nodes.JsonObject { ["animation"] = "Nod", ["loop"] = true });
            Entity.PlayAnimation("Walk");
        }
    }
}
