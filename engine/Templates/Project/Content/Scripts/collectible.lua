-- A pickup: spins and bobs, and disappears when the Player touches it.
-- Needs a Collider marked Trigger (a sphere works well) on the same actor.

function on_start(self)
    self.state.base = self.position
    self.state.t = math.random() * 6.28
end

function on_update(self, dt)
    local s = self.state
    s.t = s.t + dt
    self.position = s.base + vec3(0, math.sin(s.t * 2.0) * 0.15, 0)
    self.rotation = vec3(0, s.t * 90, 0)
end

function on_trigger(self, other)
    if other.name == "Player" then
        self:destroy()
    end
end
