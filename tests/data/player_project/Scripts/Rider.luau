-- Rises at params.speed metres per second: the player test checks a script ran.
local Rider = {}

function Rider:on_update(dt)
    self.entity:translate(vector.create(0, self.params.speed * dt, 0))
    -- The HUD (the player test checks the green square's pixel and the vertex count):
    noor.ui.rect(10, 10, 60, 60, { color = "#00FF00" })
    noor.ui.text("Score: 3", 0, 20, { size = 32, anchor = "top" })
end

return Rider
