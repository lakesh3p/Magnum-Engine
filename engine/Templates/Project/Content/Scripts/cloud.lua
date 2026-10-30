-- Drifts an actor slowly to the right and wraps it around, for background clouds in 2D scenes.

local SPEED = 0.35
local WRAP_FROM, WRAP_TO = 80, -20

function on_update(self, dt)
    local p = self.position
    local x = p.x + SPEED * dt * (0.5 + self.scale.x)
    if x > WRAP_FROM then x = WRAP_TO end
    self.position = vec3(x, p.y, p.z)
end
