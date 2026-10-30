-- Slides the actor back and forth. Put it on a Rigidbody marked Kinematic so other bodies stand on it.
local DISTANCE = 4.0
local SPEED = 1.0

function on_start(self)
    self.state.origin = self.position
    self.state.t = 0
end

function on_update(self, dt)
    local s = self.state
    s.t = s.t + dt * SPEED
    self.position = s.origin + vec3(math.sin(s.t) * DISTANCE, 0, 0)
end
