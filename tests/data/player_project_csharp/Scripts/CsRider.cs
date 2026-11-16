using System.Numerics;
using NoorEngine;

// Rises at params.speed metres per second: the player test checks C# ran in the built game.
public class CsRider : NoorScript
{
    public override void OnUpdate(float dt)
    {
        Entity.Translate(new Vector3(0, Param("speed", 1) * dt, 0));
        Ui.Text("C#", 0, -20, 24, "#F2C14E", "bottom");  // the HUD from C#, too
    }
}
