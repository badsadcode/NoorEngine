-- Rolls a storm in (Milestone 8B, part 8: the 8B sample): this entity's weather zone goes from nothing to fully on
-- over params.seconds, smoothly (rain, wind, low cloud and fog all blend with it).
local Storm = {}

function Storm:on_update(dt)
    self.t = (self.t or 0) + dt
    local x = math.min(1, self.t / (self.params.seconds or 4))
    local zone = self.entity:get("weather_zone")
    if zone then
        zone.weight = x * x * (3 - 2 * x)  -- smoothstep: eases in and out
        self.entity:set("weather_zone", zone)
    end
end

return Storm
