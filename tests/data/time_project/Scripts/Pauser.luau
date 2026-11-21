-- Runs while paused ("pause": "always"): pauses the game at 1 s of game time for half a real second (a real-time
-- timer), checks the falling box didn't move meanwhile, then plays on in slow motion.
local Pauser = {}

function Pauser:on_start()
    self.box = noor.find("Box")
end

function Pauser:on_update(dt)
    if not self.paused_at and noor.time() >= 1.0 then
        noor.set_paused(true)
        self.paused_at = noor.real_time()
        self.box_y = self.box:world_position().y
        noor.after(0.5, function()
            self.frozen = math.abs(self.box:world_position().y - self.box_y) < 1e-6
            noor.set_paused(false)
            noor.set_time_scale(0.5)
            self.resumed = noor.real_time()
        end, { real = true })
    end
    noor.ui.text(string.format("paused %.3f real s, frozen %s, scale %.2f, game %.3f, real %.3f",
        self.resumed and (self.resumed - self.paused_at) or -1, tostring(self.frozen), noor.time_scale(), noor.time(),
        noor.real_time()), 20, 60)
end

return Pauser
