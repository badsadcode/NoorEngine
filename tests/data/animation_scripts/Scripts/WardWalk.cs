using NoorEngine;

// Ward walks on root motion inside a character controller (Milestone 8C, part 4): ward_walk.luau in C#. He hears his
// footsteps, walks half as fast again after four of them, and a wall ahead stops him.
public class WardWalk : NoorScript
{
    private int _steps;

    public override void OnAnimationEvent(string name)
    {
        _steps++;
        if (_steps == 4) Entity.SetAnimationSpeed(1.5f);
        Entity.Set("steps", new System.Text.Json.Nodes.JsonObject { ["count"] = _steps, ["last"] = name });
    }
}
