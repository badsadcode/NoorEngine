-- The built-in `platformer` component already runs and jumps (A/D or arrows, Space; down + Space drops through a
-- plank). This script is where your game starts: here it only shows the keys. Add coins, enemies, a goal...
-- For a controller of your own instead, see "Your own controller" in the Lua docs ("motion": "script").
local Player = {}

function Player:on_update(dt)
    if noor.time() < 5 then
        noor.ui.text("A / D or arrows to run, Space to jump", 0, -24, { size = 36, anchor = "bottom" })
    end
end

return Player
