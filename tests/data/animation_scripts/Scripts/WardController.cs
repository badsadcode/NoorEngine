using System;
using NoorEngine;

// Ward on his controller (Ward.nooranim, Milestone 8C part 3): ward_controller.luau in C#. Speeds him up from standing
// to a run, triggers a nod at frame 90, waves from frame 120, and leans into the run when the nod has ended.
public class WardController : NoorScript
{
    private int _frames;

    public override void OnUpdate(float dt)
    {
        _frames++;
        // The same float the Lua script computes (frames x 0.0167 in double, then stored as a number).
        Entity.SetAnimationParameter("speed", (float)Math.Min(_frames * 0.0167, 4.0));
        if (_frames == 90) Entity.SetAnimationTrigger("nod");
        if (_frames == 120) Entity.SetAnimationParameter("waving", true);
    }

    public override void OnAnimationEnd(string animation)
    {
        if (animation == "Nod") Entity.SetAnimationParameter("lean", 0.6f);
    }
}
