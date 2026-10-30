-- A slime that patrols back and forth around where it started. Give the actor a Sprite (2 frames), a Collider and a
-- Rigidbody with Lock Z. Set self.state.range in on_start of your own copy to change how far it walks.

local SPEED = 1.6
local RANGE = 3.0

function on_start(self)
    local s = self.state
    s.home = self.position.x
    s.dir = (math.floor(self.position.x) % 2 == 0) and 1 or -1 -- neighbours walk in opposite directions
    self.sprite:play(0, 1, 3)
end

function on_update(self, dt)
    local s = self.state
    local x = self.position.x
    if x > s.home + RANGE then s.dir = -1 elseif x < s.home - RANGE then s.dir = 1 end
    self.velocity = vec3(s.dir * SPEED, self.velocity.y, 0)
    self.sprite.flip_x = s.dir > 0
end

-- Turn around when something solid is in the way.
function on_hit(self, other)
    local dx = other.position.x - self.position.x
    if math.abs(dx) > 0.6 and (dx > 0) == (self.state.dir > 0) then self.state.dir = -self.state.dir end
end
