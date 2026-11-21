-- A timer, a repeating timer that stops itself, and a sequence that waits twice; it stops while the game is paused.
local Clock = {}

function Clock:on_start()
    self.ticks = 0
    self.updates = 0
    noor.after(0.5, function() self.after_at = noor.time() end)
    noor.every(0.25, function()
        self.ticks += 1
        return self.ticks < 4  -- false: stop
    end)
    noor.start(function()
        noor.wait(0.3)
        self.w1 = noor.time()
        noor.wait(0.5)
        self.w2 = noor.time()
    end)
end

function Clock:on_update(dt)
    self.updates += 1
    noor.ui.text(string.format("after %.3f, ticks %d, waits %.3f %.3f, updates %d", self.after_at or -1, self.ticks,
        self.w1 or -1, self.w2 or -1, self.updates), 20, 20)
end

return Clock
