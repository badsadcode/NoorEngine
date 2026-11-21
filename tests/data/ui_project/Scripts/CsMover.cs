using System.Numerics;
using NoorEngine;

// The C# twin of Mover.luau.
public class CsMover : NoorScript
{
    public override void OnUpdate(float dt) => Entity.Position += new Vector3(dt, 0, 0);
}
