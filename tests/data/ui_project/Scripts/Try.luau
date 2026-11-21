-- Scenes/Try: a pause menu to play with by hand (not run by the tests). The game starts paused with the menu open;
-- Resume (a click, or Enter on it) plays on, P pauses again. The name, volume and subtitles show on the HUD page.
-- Runs while paused ("pause": "always").
local Try = {}

function Try:on_start()
    self.hud = noor.ui.find("UI/hud.noorui")
    self.menu = noor.ui.open("UI/try.noorui")
    noor.set_paused(true)
    self.menu:on("click", "resume", function() self:resume() end)
    self.menu:on("click", "reset", function()
        self.menu:set_value("name", "Alden")
        self.menu:set_value("volume", 0.5)
        self.menu:set_value("subtitles", false)
    end)
    self.menu:on("change", "volume", function(e)
        self.menu:set_text("volume-text", string.format("%d%%", math.floor(e.value * 100 + 0.5)))
    end)
    self.menu:focus("resume")
end

function Try:resume()
    self.menu:hide()
    noor.set_paused(false)
end

function Try:on_update()
    if noor.input.pressed("P") and not noor.paused() then
        noor.set_paused(true)
        self.menu:show()
        self.menu:focus("resume")
    end
    local name = self.menu:value("name")
    if name == "" then name = "stranger" end
    self.hud:set_text("score", string.format("Hello, %s | volume %d%% | subtitles %s", name,
        math.floor(self.menu:value("volume") * 100 + 0.5), self.menu:value("subtitles") and "on" or "off"))
end

return Try
