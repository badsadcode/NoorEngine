using System.Collections.Generic;
using NoorEngine;

// The C# twin of PadMenu.luau.
public class CsPadMenu : NoorScript
{
    private readonly List<string> _log = new();

    public override void OnStart()
    {
        var menu = Ui.Open("UI/pad.noorui", Entity);
        menu.Focus("start");
        On(menu, "click", null, e => _log.Add("click " + (string?)e?["element"]));
        On("pad_connected", (d, _) => _log.Add($"connected {(int?)d?["player"]} {(string?)d?["name"]}"));
        On("pad_disconnected", (d, _) => _log.Add($"gone {(int?)d?["player"]}"));
    }

    public override void OnUpdate(float dt)
    {
        if (Input.Pressed("jump")) _log.Add("jump 1");
        if (Input.Pressed("jump", 2)) _log.Add("jump 2");
        Ui.Text(string.Join("; ", _log), 20, 20);
    }
}
