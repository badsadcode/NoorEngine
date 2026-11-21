-- Pulled three times, half a second apart: each pull a "pulled" message with its strength. Then it asks the door, by
-- calling its script's function, whether it's open (the door may be Lua or C#: it doesn't matter to the lever).
local Lever = {}

function Lever:on_start()
    self.pulls = 0
    noor.every(0.5, function()
        self.pulls += 1
        self.entity:emit("pulled", { strength = self.pulls })
        return self.pulls < 3
    end)
end

function Lever:status() return "ok " .. self.pulls end

function Lever:on_update()
    local open = noor.time() > 2 and noor.find("Door"):call("is_open", "please") or false
    noor.ui.text("lever pulls " .. self.pulls .. ", door open " .. tostring(open), 20, 20)
end

return Lever
