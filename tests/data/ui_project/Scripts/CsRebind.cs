using NoorEngine;

// The C# twin of Rebind.luau.
public class CsRebind : NoorScript
{
    private string _state = "?";

    public override void OnStart()
    {
        if (Input.Bindings("jump")[0] == "J")
        {
            _state = "remembered";
        }
        else
        {
            Input.Rebind("jump", "Space", "J");
            Input.SaveBindings();
            _state = "saved";
        }
    }

    public override void OnUpdate(float dt) => Ui.Text(_state + ": " + string.Join(",", Input.Bindings("jump")), 20, 20);
}
