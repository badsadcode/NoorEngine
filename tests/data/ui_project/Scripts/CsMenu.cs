using System.Collections.Generic;
using System.Globalization;
using NoorEngine;

// The C# twin of Menu.luau.
public class CsMenu : NoorScript
{
    private int _frame;
    private readonly List<string> _log = new();
    private Entity _box = null!;
    private UiDocument _hud = null!;
    private UiDocument? _menu;

    private static string Lua(object? v) => v switch
    {
        null => "nil",
        bool b => b ? "true" : "false",
        _ => System.Convert.ToString(v, CultureInfo.InvariantCulture)!,
    };

    public override void OnStart()
    {
        _box = Noor.Find("Box")!;
        _hud = Ui.Find("UI/hud.noorui")!;
    }

    public override void OnUpdate(float dt)
    {
        _frame++;
        if (_frame == 10)
        {
            Noor.Paused = true;
            var menu = _menu = Ui.Open("UI/pause.noorui", Entity);
            menu.SetText("title", string.Create(CultureInfo.InvariantCulture, $"Paused at {_box.Position.X:F3}"));
            On(menu, "click", "resume", e =>
            {
                _log.Add("click " + (string?)e?["element"]);
                menu.Hide();
                Noor.Paused = false;
            });
            On(menu, "change", null, e =>
            {
                var v = e?["value"];
                var text = v is null ? "nil" : v.GetValueKind() switch
                {
                    System.Text.Json.JsonValueKind.True => "true",
                    System.Text.Json.JsonValueKind.False => "false",
                    System.Text.Json.JsonValueKind.Number => Lua(v.GetValue<double>()),
                    _ => (string?)v ?? "nil",
                };
                _log.Add($"change {(string?)e?["element"]} {text}");
            });
            menu.SetValue("volume", 0.25);
        }
        else if (_frame == 20)
        {
            _menu!.SetValue("subtitles", true);
            _log.Add(string.Create(CultureInfo.InvariantCulture,
                $"volume {_menu.Value("volume")!.GetValue<double>():F2}, subtitles {Lua(_menu.Value("subtitles")!.GetValue<bool>())}, title {(string?)_menu.Value("title")}"));
            _menu.Click("resume");
        }
        _hud.SetText("score", _box.Position.X.ToString("F3", CultureInfo.InvariantCulture));
        var shown = _menu is null ? "nil" : Lua(_menu.Visible);
        Ui.Text(string.Create(CultureInfo.InvariantCulture,
            $"frame {_frame}, paused {Lua(Noor.Paused)}, menu {shown}, box {_box.Position.X:F3}, hud {(string?)_hud.Value("score")} | {string.Join("; ", _log)}"), 20, 20);
    }
}
