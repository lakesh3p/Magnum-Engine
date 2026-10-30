-- Side-scrolling platformer controller for a Sprite with a Collider and a Rigidbody (Lock Z on).
-- A / D or the arrow keys run, Space / W / Up jump (hold for a higher jump), coins are triggers named "Coin ...",
-- slimes are named "Slime ..." (land on them to squash them), the actor named "Goal" ends the level.
-- The sprite sheet has 8 frames: 0-1 idle, 2-5 run, 6 jump, 7 fall. The actor named "Camera" follows the player.
-- If the scene has a UI with nodes named CoinsLabel, CoinsBar, Heart1..3, WinPanel and WinStats (see Content/UI/hud.lua)
-- they show the score; without them the score is drawn with hud.text.

local RUN_SPEED = 7.0
local GROUND_ACCEL = 60.0
local AIR_ACCEL = 35.0
local JUMP_SPEED = 12.5
local STOMP_BOUNCE = 9.0
local COYOTE_TIME = 0.1     -- a jump still works this long after leaving a ledge
local JUMP_BUFFER = 0.12    -- ... or when pressed this long before landing
local FALL_LIMIT = -10.0    -- below this the player respawns
local CAMERA_LAG = 6.0
local MAX_LIVES = 3

local function approach(value, target, step)
    if value < target then return math.min(value + step, target) end
    return math.max(value - step, target)
end

function on_start(self)
    local s = self.state
    s.start = self.position
    s.camera = world.find("Camera")
    s.coins = 0
    s.total = #world.find_all("Coin")
    s.coyote = 0
    s.buffer = 0
    s.won = false
    s.lives = MAX_LIVES
    s.hurt = 0
    s.shownCoins, s.shownLives = -1, -1
    s.hud = {
        coins = ui.find("CoinsLabel"), bar = ui.find("CoinsBar"), win = ui.find("WinPanel"), stats = ui.find("WinStats"),
        hearts = { ui.find("Heart1"), ui.find("Heart2"), ui.find("Heart3") },
    }
end

local function respawn(self)
    local s = self.state
    s.lives = s.lives - 1
    s.hurt = 1.0
    self.position = s.start
    self.velocity = vec3(0, 0, 0)
    if s.lives <= 0 then
        s.lives = MAX_LIVES
        s.coins = 0 -- the level goes on, the score starts over
    end
end

-- Shows the score in the UI when it changed (and pops the counter), or with hud.text when the scene has no UI.
local function show_status(s)
    local h = s.hud
    if not h.coins then
        hud.text(0.02, 0.03, string.format("Coins %d / %d", s.coins, s.total), 26, 1, 0.85, 0.3)
        hud.text(0.02, 0.09, string.rep("<3 ", s.lives), 20, 1, 0.4, 0.4)
        return
    end
    if s.coins ~= s.shownCoins then
        h.coins.text = string.format("%d / %d", s.coins, s.total)
        if h.bar then h.bar:animate{ value = s.total > 0 and s.coins / s.total or 0, duration = 0.25 } end
        if s.shownCoins >= 0 and s.coins > s.shownCoins then -- a coin was picked up: pop
            h.coins.scale = 1.35
            h.coins:animate{ scale = 1, duration = 0.3, ease = "out_back" }
        end
        s.shownCoins = s.coins
    end
    if s.lives ~= s.shownLives then
        for i, heart in ipairs(h.hearts) do
            if heart then
                local alive = i <= s.lives
                heart:animate{ opacity = alive and 1 or 0.2, duration = 0.2 }
                if not alive and s.shownLives >= 0 then -- this life was just lost
                    heart.scale = 1.5
                    heart:animate{ scale = 1, duration = 0.35, ease = "out_elastic" }
                end
            end
        end
        s.shownLives = s.lives
    end
end

local function show_win(s)
    local h = s.hud
    if h.win then
        if h.stats then h.stats.text = string.format("%d of %d coins collected", s.coins, s.total) end
        h.win.visible = true
        h.win.opacity = 0
        h.win.scale = 0.85
        h.win:animate{ opacity = 1, scale = 1, duration = 0.35, ease = "out_back" }
    else
        hud.text(0.34, 0.38, "Level complete!", 44, 0.6, 1, 0.6)
        hud.text(0.36, 0.48, string.format("%d / %d coins   -   Esc to stop", s.coins, s.total), 22, 1, 1, 1, 0.85)
    end
end

function on_update(self, dt)
    local s = self.state
    local v = self.velocity
    local sprite = self.sprite

    if s.won and game.vars.keep_playing then -- the "Keep playing" button of the win card
        game.vars.keep_playing = nil
        s.won = false
        if s.hud.win then s.hud.win:animate{ opacity = 0, duration = 0.2, hide = true } end
    end

    -- Run.
    local dir = 0
    if not s.won then
        if input.key("a") or input.key("left") then dir = dir - 1 end
        if input.key("d") or input.key("right") then dir = dir + 1 end
    end
    local accel = self.grounded and GROUND_ACCEL or AIR_ACCEL
    local vx = approach(v.x, dir * RUN_SPEED, accel * dt)
    if dir ~= 0 then sprite.flip_x = dir < 0 end

    -- Jump: coyote time and a small buffer make the controls forgiving.
    s.coyote = self.grounded and COYOTE_TIME or s.coyote - dt
    local jumpKey = input.key("space") or input.key("w") or input.key("up")
    if (input.pressed("space") or input.pressed("w") or input.pressed("up")) and not s.won then s.buffer = JUMP_BUFFER else s.buffer = s.buffer - dt end
    local vy = v.y
    if s.buffer > 0 and s.coyote > 0 then
        vy = JUMP_SPEED
        s.buffer, s.coyote = 0, 0
    end
    if not jumpKey and vy > 3 then vy = vy * (1 - math.min(1, dt * 12)) end -- letting go early cuts the jump short
    self.velocity = vec3(vx, vy, 0)

    -- Animation follows what the body is doing.
    if not self.grounded then
        if vy > 0 then sprite:play(6, 6, 1) else sprite:play(7, 7, 1) end
    elseif math.abs(vx) > 0.5 then
        sprite:play(2, 5, 4 + math.abs(vx) * 1.2)
    else
        sprite:play(0, 1, 2.5)
    end
    -- Blink while invulnerable after a respawn.
    s.hurt = math.max(0, s.hurt - dt)
    sprite.opacity = (s.hurt > 0 and math.floor(s.hurt * 12) % 2 == 0) and 0.35 or 1.0

    if self.position.y < FALL_LIMIT then respawn(self) end

    -- Camera: follow with a little lag and keep the player a bit below the middle of the view.
    local cam = s.camera
    if cam and cam.valid then
        local c = cam.position
        local k = math.min(1, dt * CAMERA_LAG)
        cam.position = vec3(c.x + (self.position.x - c.x) * k, c.y + (self.position.y + 1.5 - c.y) * k, c.z)
    end

    show_status(s)
end

function on_trigger(self, other)
    local s = self.state
    if other.name:find("^Coin") then
        other:destroy()
        s.coins = s.coins + 1
    elseif other.name == "Goal" and not s.won then
        s.won = true
        show_win(s)
    end
end

function on_hit(self, other)
    if not other.name:find("^Slime") or self.state.hurt > 0 then return end
    if self.position.y - other.position.y > 0.6 then -- landed on top of it
        other:destroy()
        local v = self.velocity
        self.velocity = vec3(v.x, STOMP_BOUNCE, 0)
    else
        respawn(self)
    end
end
