-- Runs while paused ("pause": "always"). On frame 10 it opens the pause menu and pauses, writes into it and listens to
-- it; on frame 20 it ticks the checkbox, reads both controls and clicks Resume; the click resumes the game. Every frame
-- it shows the box's position on the HUD page (opened by a ui_document component). CsMenu.cs does the same in C#.
local Menu = {}

function Menu:on_start()
    self.frame = 0
    self.log = {}
    self.box = noor.find("Box")
    self.hud = noor.ui.find("UI/hud.noorui")
end

function Menu:on_update()
    self.frame = self.frame + 1
    if self.frame == 10 then
        noor.set_paused(true)
        self.menu = noor.ui.open("UI/pause.noorui")
        self.menu:set_text("title", string.format("Paused at %.3f", self.box:position().x))
        self.menu:on("click", "resume", function(e)
            table.insert(self.log, "click " .. e.element)
            self.menu:hide()
            noor.set_paused(false)
        end)
        self.menu:on("change", function(e)
            table.insert(self.log, string.format("change %s %s", e.element, tostring(e.value)))
        end)
        self.menu:set_value("volume", 0.25)
    elseif self.frame == 20 then
        self.menu:set_value("subtitles", true)
        table.insert(self.log, string.format("volume %.2f, subtitles %s, title %s", self.menu:value("volume"),
            tostring(self.menu:value("subtitles")), self.menu:value("title")))
        self.menu:click("resume")
    end
    self.hud:set_text("score", string.format("%.3f", self.box:position().x))
    local shown = self.menu and tostring(self.menu:visible()) or "nil"
    noor.ui.text(string.format("frame %d, paused %s, menu %s, box %.3f, hud %s | %s", self.frame, tostring(noor.paused()),
        shown, self.box:position().x, self.hud:value("score"), table.concat(self.log, "; ")), 20, 20)
end

return Menu
