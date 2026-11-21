-- A menu driven by a gamepad (part 6e): the pad moves focus (d-pad or stick) and presses (South); it's unplugged and
-- plugged back in; a second pad jumps as player 2. CsPadMenu.cs does the same in C#.
local PadMenu = {}

function PadMenu:on_start()
    self.log = {}
    self.menu = noor.ui.open("UI/pad.noorui")
    self.menu:focus("start")
    self.menu:on("click", function(e) table.insert(self.log, "click " .. e.element) end)
    noor.on("pad_connected", function(d) table.insert(self.log, "connected " .. d.player .. " " .. d.name) end)
    noor.on("pad_disconnected", function(d) table.insert(self.log, "gone " .. d.player) end)
end

function PadMenu:on_update()
    if noor.input.pressed("jump") then table.insert(self.log, "jump 1") end
    if noor.input.pressed("jump", 2) then table.insert(self.log, "jump 2") end
    noor.ui.text(table.concat(self.log, "; "), 20, 20)
end

return PadMenu
