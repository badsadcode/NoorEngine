using System;
using System.Numerics;
using NoorEngine;

// Coth (The Uncounted) walks a circle for two seconds, attacks, and when the attack has ended, falls down
// (Milestone 8B, part 6): coth_walk.luau in C#. The built-game test checks both got there.
public class CothWalk : NoorScript
{
    private float _t;
    private bool _attacking;

    public override void OnStart() => Entity.PlayAnimation("walk");

    public override void OnUpdate(float dt)
    {
        if (_attacking) return;
        _t += dt;
        var r = Param("radius", 2);
        var a = _t / Param("lap", 8) * 2 * MathF.PI;
        Entity.Position = new Vector3(Param("centre_x", 0) + r * MathF.Sin(a), 0, Param("centre_z", 0) + r * MathF.Cos(a));
        // Facing along the way round: forward (-Z) along (cos a, 0, -sin a), a yaw of a - 90 degrees.
        Entity.Euler = new Vector3(0, a * 180 / MathF.PI - 90, 0);
        if (_t >= 2)
        {
            _attacking = true;
            Entity.PlayAnimation("attack");
        }
    }

    public override void OnAnimationEnd(string animation)
    {
        if (animation == "attack") Entity.PlayAnimation("die");
        else if (animation == "die") Entity.Set("story", new System.Text.Json.Nodes.JsonObject { ["done"] = true });
    }
}
