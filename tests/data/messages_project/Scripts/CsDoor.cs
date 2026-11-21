using System.Text.Json.Nodes;
using NoorEngine;

// The C# twin of Door.luau, talking to the Lua lever.
public class CsDoor : NoorScript
{
    private int _heard, _strength;
    private string _from = "-", _says = "-";

    public override void OnStart()
    {
        On(Noor.Find("Lever")!, "pulled", (data, sender) =>
        {
            _heard++;
            _strength = (int)data!["strength"]!;
            _from = sender!.Name;
            _says = (string?)sender.Call("status") ?? "-";
        });
    }

    public bool is_open(string word) => word == "please" && _strength >= 3;

    public override void OnUpdate(float dt) =>
        Ui.Text($"heard {_heard}, strength {_strength}, from {_from}, lever says {_says}", 20, 60);
}
