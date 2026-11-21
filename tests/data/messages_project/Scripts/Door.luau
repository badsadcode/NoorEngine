-- Listens to the lever; opens on the third pull; answers "is_open"; asks the lever for its status.
local Door = {}

function Door:on_start()
    self.heard = 0
    self.strength = 0
    noor.find("Lever"):on("pulled", function(data, sender)
        self.heard += 1
        self.strength = data.strength
        self.from = sender:name()
        self.says = sender:call("status")
    end)
end

function Door:is_open(word) return word == "please" and self.strength >= 3 end

function Door:on_update()
    noor.ui.text(string.format("heard %d, strength %d, from %s, lever says %s", self.heard, self.strength,
        self.from or "-", self.says or "-"), 20, 60)
end

return Door
