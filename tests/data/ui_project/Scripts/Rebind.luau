-- Rebinding that survives a restart (part 6e): the first run makes J jump instead of Space and saves it; the next run
-- (the same player's settings folder) finds J there. CsRebind.cs does the same in C#.
local Rebind = {}

function Rebind:on_start()
    local jump = noor.input.bindings("jump")
    if jump[1] == "J" then
        self.state = "remembered"
    else
        noor.input.rebind("jump", "Space", "J")
        noor.input.save_bindings()
        self.state = "saved"
    end
end

function Rebind:on_update()
    noor.ui.text(self.state .. ": " .. table.concat(noor.input.bindings("jump"), ","), 20, 20)
end

return Rebind
