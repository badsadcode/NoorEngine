-- A first script: it turns the sprite it's on and writes on the screen. Change it, press Play, and see.
local Main = {}

function Main:on_update(dt)
    self.entity:rotate(vector.create(0, 0, 1), self.params.speed * dt)  -- degrees a second, from the script's params
    noor.ui.text("Hello, 2D!", 0, 40, { size = 48, anchor = "top" })
end

return Main
