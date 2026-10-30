-- First-person character controller for an actor with a Collider and a Rigidbody.
-- WASD moves, Shift sprints, Space jumps, the mouse looks around. The scene's "Camera" actor follows the head.
-- Collect every Orb to win. Click the viewport first (it captures the mouse); Esc releases it, Esc again stops.
-- If the scene has a Canvas with nodes named OrbsLabel and Congrats (Content/UI/hud.lua) the score is shown there,
-- otherwise it is drawn with hud.text.

local WALK_SPEED = 5.0
local SPRINT_SPEED = 9.0
local JUMP_SPEED = 7.5
local MOUSE_SENSITIVITY = 0.12   -- degrees per pixel
local EYE_HEIGHT = 0.7           -- above the center of the body
local FALL_LIMIT = -20.0         -- below this the player respawns

function on_start(self)
    local s = self.state
    s.yaw = self.rotation.y
    s.pitch = 0
    s.score = 0
    s.start = self.position
    s.camera = world.find("Camera")
    s.total = #world.find_all("Orb")
    s.label, s.congrats = ui.find("OrbsLabel"), ui.find("Congrats")
    s.shown = -1
    if not s.camera then log("The scene needs a Camera actor for the player's view") end
end

function on_update(self, dt)
    local s = self.state

    -- Look: yaw turns the body, pitch only tilts the camera.
    local dx, dy = input.mouse_delta()
    s.yaw = s.yaw - dx * MOUSE_SENSITIVITY
    s.pitch = math.max(-85, math.min(85, s.pitch - dy * MOUSE_SENSITIVITY))
    self.rotation = vec3(0, s.yaw, 0)

    -- Move on the ground plane relative to where the body faces.
    local move = vec3(0, 0, 0)
    if input.key("w") then move = move + self.forward end
    if input.key("s") then move = move - self.forward end
    if input.key("d") then move = move + self.right end
    if input.key("a") then move = move - self.right end
    move.y = 0
    local speed = input.key("shift") and SPRINT_SPEED or WALK_SPEED
    move = move:normalized() * speed
    local v = self.velocity
    self.velocity = vec3(move.x, v.y, move.z)

    if input.pressed("space") and self.grounded then
        local v2 = self.velocity
        self.velocity = vec3(v2.x, JUMP_SPEED, v2.z)
    end

    if self.position.y < FALL_LIMIT then
        self.position = s.start
        self.velocity = vec3(0, 0, 0)
    end

    -- The camera sits at the head and looks where the mouse looks.
    if s.camera and s.camera.valid then
        s.camera.position = self.position + vec3(0, EYE_HEIGHT, 0)
        s.camera.rotation = vec3(s.pitch, s.yaw, 0)
    end

    -- A tiny HUD: in the UI when the scene has one, else drawn directly.
    local left = #world.find_all("Orb")
    local got = s.total - left
    if s.label then
        if got ~= s.shown then
            s.label.text = string.format("Orbs: %d / %d", got, s.total)
            if s.shown >= 0 then -- an orb was just picked up: pop the counter
                s.label.scale = 1.3
                s.label:animate{ scale = 1, duration = 0.3, ease = "out_back" }
            end
            s.shown = got
            if s.congrats and s.total > 0 and left == 0 then
                s.congrats.visible = true
                s.congrats.opacity = 0
                s.congrats:animate{ opacity = 1, duration = 0.5 }
            end
        end
    else
        hud.text(0.02, 0.03, string.format("Orbs: %d / %d", got, s.total), 26, 1, 0.9, 0.4)
        hud.rect(0.495, 0.49, 0.01, 0.02, 1, 1, 1, 0.8) -- crosshair
        if s.total > 0 and left == 0 then
            hud.text(0.38, 0.4, "You collected them all!", 40, 0.5, 1, 0.5)
            hud.text(0.40, 0.48, "Press Esc to stop", 22, 1, 1, 1, 0.8)
        end
    end
end
