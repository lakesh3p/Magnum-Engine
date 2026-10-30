-- Magnum shader DSL.
--
-- A shader script builds a shader object, gives it a vertex and a fragment function,
-- and returns it:
--
--     local S = shader("Name")
--     local position = S:attribute("position", "vec2")
--     local tint     = S:param("tint", "color", {1, 1, 1})
--     S:vertex(function(a)   return vec4(a.position, 0, 1), { uv = a.position } end)
--     S:fragment(function(v) return vec4(tint * v.uv.x, 1) end)
--     return S
--
-- The stage functions are ordinary Lua, but they run on symbolic values: every operation
-- (a * b, sin(x), v.xy ...) records one GLSL statement instead of computing a number.
-- Each recorded statement remembers the Lua line that produced it, so GLSL compile
-- errors are reported against your .lua file.
--
-- What a shader can be used for:
--   * surfaces and animation of any mesh: S:vertex may move vertices (use the `time` builtin) and
--     S:fragment colors them; a material picks the shader, an actor picks the material
--   * decals: a shader for a Decal draws the box of the actor and reads scene_depth to find the surface behind
--     each pixel (see Engine/Shaders/decal.lua)
--   * the sky, clouds, fog, post effects: S:fullscreen(function(v) ... end) with v.uv and v.ray, drawn by a
--     pass of the render pipeline (Content/Pipelines/default.lua)
--   * procedural patterns: hash(p), noise(p) and fbm(p, octaves) for a vec2 or vec3 p
--   * long loops (ray marching, many octaves): loop(count, state, function(i, ...) ... end) is a real GLSL loop;
--     a Lua `for` is unrolled, and a stage with more than 6000 operations is refused
--
-- Not available inside stage functions: comparisons and branching on shader values
-- (use mix / step / smoothstep / clamp), because they have no value at trace time.

local M = {}

local DSL_SOURCE = debug.getinfo(1, "S").source

local COMPONENTS = { float = 1, vec2 = 2, vec3 = 3, vec4 = 4 }
local VEC_OF = { "float", "vec2", "vec3", "vec4" }

-- Uniforms the engine sets for every draw. Declared on demand with S:builtin(name).
--   model, view_proj, normal_matrix : object -> world, world -> clip, and the matrix for normals
--   camera_pos                      : world-space eye position
--   light_dir, light_color          : direction TO the main directional light, and its color * intensity
--   view, projection, inv_view_proj : camera matrices (inv_view_proj maps clip space back to world space)
--   scene_color                     : the image drawn so far, as a texture (fullscreen post passes only)
--   scene_depth                     : the depth of everything drawn so far, 0..1 (read .r), set for decal passes
--   inv_model                       : world -> object, the inverse of model
--   frag_coord                      : the pixel being shaded in window coordinates (GLSL gl_FragCoord, fragment stage only)
local BUILTINS = {
    time = "float", aspect = "float", resolution = "vec2",
    model = "mat4", view_proj = "mat4", normal_matrix = "mat3",
    view = "mat4", projection = "mat4", inv_view_proj = "mat4",
    camera_pos = "vec3", light_dir = "vec3", light_color = "vec3",
    scene_color = "sampler2D", scene_depth = "sampler2D", inv_model = "mat4", frag_coord = "vec4",
}

-- GLSL helpers behind hash(), noise() and fbm(); added to a stage only when it uses one.
local NOISE_GLSL = {
    "float k_hash(vec3 p) { p = fract(p * 0.3183099 + vec3(0.1, 0.2, 0.3)); p *= 17.0; return fract(p.x * p.y * p.z * (p.x + p.y + p.z)); }",
    "float k_hash(vec2 p) { return k_hash(vec3(p, 0.37)); }",
    "float k_noise(vec3 x) {",
    "    vec3 i = floor(x); vec3 f = fract(x); f = f * f * (3.0 - 2.0 * f);",
    "    return mix(mix(mix(k_hash(i), k_hash(i + vec3(1, 0, 0)), f.x), mix(k_hash(i + vec3(0, 1, 0)), k_hash(i + vec3(1, 1, 0)), f.x), f.y),",
    "               mix(mix(k_hash(i + vec3(0, 0, 1)), k_hash(i + vec3(1, 0, 1)), f.x), mix(k_hash(i + vec3(0, 1, 1)), k_hash(i + vec3(1, 1, 1)), f.x), f.y), f.z);",
    "}",
    "float k_noise(vec2 x) { return k_noise(vec3(x, 0.0)); }",
    "float k_fbm(vec3 p, int octaves) {",
    "    float a = 0.5; float s = 0.0;",
    "    for (int i = 0; i < octaves; i++) { s += a * k_noise(p); p = p * 2.02 + vec3(17.1, 5.3, 9.7); a *= 0.5; }",
    "    return s;",
    "}",
    "float k_fbm(vec2 p, int octaves) { return k_fbm(vec3(p, 0.0), octaves); }",
}

-- Vertex attributes with a fixed location, so every mesh can feed every shader.
-- Other names get locations from 4 up (and need a mesh that provides them).
local STANDARD_LOCATIONS = { position = 0, normal = 1, uv = 2, color = 3 }

-- Matrix types and the vector type they multiply.
local MAT_VEC = { mat4 = "vec4", mat3 = "vec3" }

-- A stage that records more operations than this is almost certainly a long Lua `for` loop unrolled into
-- straight-line code, which freezes the GPU driver compiler for minutes. Better to stop with a clear error.
local MAX_STATEMENTS = 6000

local ctx = nil -- emission context of the stage being traced: { lines, map, tmp }

---------------------------------------------------------------------------------------
-- Errors and source lines
---------------------------------------------------------------------------------------

-- The last line of the user's script that started executing while a stage function is traced.
-- Needed for tail calls: `return vec4(...)` replaces the user's stack frame, so the stack alone
-- can no longer tell which line the call came from.
local last_user_line = nil

local function track_line(_, line)
    local info = debug.getinfo(2, "S")
    if info.what ~= "C" and info.source ~= DSL_SOURCE then
        last_user_line = { short_src = info.short_src, currentline = line }
    end
end

-- First stack frame that belongs to the user's script rather than to this file
-- (falling back to the last executed user line).
local function user_frame()
    local level = 2
    while true do
        local info = debug.getinfo(level, "Sl")
        if not info then return last_user_line end
        if info.currentline > 0 and info.source ~= DSL_SOURCE then return info end
        level = level + 1
    end
end

local function user_line()
    local info = user_frame()
    return info and info.currentline or 0
end

local function fail(msg)
    local info = user_frame()
    if info then msg = info.short_src .. ":" .. info.currentline .. ": " .. msg end
    error(msg, 0)
end

-- Error attributed to the end of a user function (used for stage return values).
local function fail_in(fn, msg)
    local info = debug.getinfo(fn, "S")
    error(info.short_src .. ":" .. info.lastlinedefined .. ": " .. msg, 0)
end

---------------------------------------------------------------------------------------
-- Symbolic values
---------------------------------------------------------------------------------------

local Value = {}

local function new_value(t, expr)
    return setmetatable({ type = t, expr = expr }, Value)
end

local function is_value(x)
    return getmetatable(x) == Value
end

local function literal(n)
    if n ~= n or n == math.huge or n == -math.huge then fail("number is not finite") end
    local s = string.format("%.9g", n)
    if not s:find("[%.eE]") then s = s .. ".0" end
    if n < 0 then s = "(" .. s .. ")" end
    return s
end

local function val(x)
    if is_value(x) then return x end
    if type(x) == "number" then return new_value("float", literal(x)) end
    fail("expected a number or a shader value, got " .. type(x))
end

local function add_line(c, text, src_line)
    c.lines[#c.lines + 1] = text
    c.map[#c.map + 1] = src_line or 0
end

-- Records `type tN = expr;` and returns a value referring to tN.
local function emit(t, expr)
    if not ctx then fail("shader values can only be used inside vertex/fragment functions (outside them, only vec2/vec3/vec4 of plain numbers can be made)") end
    ctx.tmp = ctx.tmp + 1
    if ctx.tmp > MAX_STATEMENTS then
        fail("this stage records more than " .. MAX_STATEMENTS .. " operations. A Lua `for` loop is unrolled into straight-line "
             .. "code; use loop(count, state, function(i, ...) ... end) for long loops, which becomes a real GLSL loop")
    end
    local name = "t" .. ctx.tmp
    add_line(ctx, "    " .. t .. " " .. name .. " = " .. expr .. ";", user_line())
    return new_value(t, name)
end

-- Result type of a component-wise binary operation (with float broadcasting).
local function binary_type(op, a, b)
    local a_vec, b_vec = MAT_VEC[a.type], MAT_VEC[b.type]
    if a_vec or b_vec then
        if op == "*" and a_vec and (b.type == a_vec or b.type == a.type) then
            return b.type
        end
    elseif a.type == b.type then
        return a.type
    elseif a.type == "float" then
        return b.type
    elseif b.type == "float" then
        return a.type
    end
    fail(string.format("cannot apply '%s' to %s and %s", op, a.type, b.type))
end

local function arith(op)
    return function(a, b)
        a, b = val(a), val(b)
        return emit(binary_type(op, a, b), a.expr .. " " .. op .. " " .. b.expr)
    end
end

Value.__add = arith("+")
Value.__sub = arith("-")
Value.__mul = arith("*")
Value.__div = arith("/")
Value.__unm = function(a) return emit(a.type, "-" .. a.expr) end
Value.__tostring = function(a) return "<" .. a.type .. " " .. a.expr .. ">" end

-- Swizzles: v.xy, v.rgb, v.x ...
Value.__index = function(self, key)
    if type(key) == "string" and #key >= 1 and #key <= 4 then
        local set = (key:match("^[xyzw]+$") and "xyzw") or (key:match("^[rgba]+$") and "rgba")
        local n = COMPONENTS[self.type]
        if set and n then
            for ch in key:gmatch(".") do
                if set:find(ch, 1, true) > n then
                    fail(string.format("'%s' is not a component of %s", ch, self.type))
                end
            end
            return emit(VEC_OF[#key], self.expr .. "." .. key)
        end
    end
    fail("unknown field '" .. tostring(key) .. "' on a " .. self.type .. " value")
end

Value.__newindex = function()
    fail("shader values are immutable; build a new value instead")
end

---------------------------------------------------------------------------------------
-- Constructors and functions
---------------------------------------------------------------------------------------

local env = {} -- globals visible to shader scripts

-- A value whose GLSL text is its own constant expression (no statement is recorded for it).
local function const_value(t, expr)
    local v = new_value(t, expr)
    rawset(v, "const", true)
    return v
end

local function constructor(tname)
    local n = COMPONENTS[tname]
    return function(...)
        local count = select("#", ...)
        if count == 0 then fail(tname .. "() needs arguments") end
        local parts, total = {}, 0
        local args = { ... }
        -- Outside a stage function only constants can be built (vec3(0.1, 0.2, 0.3)): they are written
        -- into the GLSL as they are, so a script can keep them in top-level variables.
        local constant = ctx == nil
        for i = 1, count do
            local v = val(args[i])
            if constant and not (type(args[i]) == "number" or rawget(v, "const")) then
                fail(tname .. "() outside vertex/fragment functions can only use plain numbers")
            end
            if not COMPONENTS[v.type] then fail("cannot put a " .. v.type .. " into " .. tname .. "()") end
            parts[i] = v.expr
            total = total + COMPONENTS[v.type]
        end
        if n == 1 then
            if count ~= 1 or total ~= 1 then fail("float() takes exactly one scalar") end
            if constant then return const_value("float", parts[1]) end
            return emit("float", parts[1])
        end
        if not (count == 1 and total == 1) and total ~= n then
            fail(string.format("%s() needs %d components, got %d", tname, n, total))
        end
        if constant then return const_value(tname, tname .. "(" .. table.concat(parts, ", ") .. ")") end
        return emit(tname, tname .. "(" .. table.concat(parts, ", ") .. ")")
    end
end

for _, t in ipairs({ "float", "vec2", "vec3", "vec4" }) do env[t] = constructor(t) end

-- f(x) -> same type as x
for _, name in ipairs({ "sin", "cos", "tan", "asin", "acos", "atan", "abs", "sign", "floor", "ceil",
                        "fract", "fwidth", "dFdx", "dFdy", "sqrt", "inversesqrt", "exp", "exp2", "log", "log2", "radians",
                        "degrees", "normalize" }) do
    env[name] = function(a)
        a = val(a)
        return emit(a.type, name .. "(" .. a.expr .. ")")
    end
end

-- f(a, b) -> broadcast type of a and b
for _, name in ipairs({ "min", "max", "pow", "mod" }) do
    env[name] = function(a, b)
        a, b = val(a), val(b)
        return emit(binary_type(name, a, b), name .. "(" .. a.expr .. ", " .. b.expr .. ")")
    end
end

env.atan2 = function(y, x)
    y, x = val(y), val(x)
    return emit(binary_type("atan2", y, x), "atan(" .. y.expr .. ", " .. x.expr .. ")")
end

-- f(a, b) -> float
for _, name in ipairs({ "dot", "distance" }) do
    env[name] = function(a, b)
        a, b = val(a), val(b)
        if a.type ~= b.type then fail(string.format("%s() needs matching types, got %s and %s", name, a.type, b.type)) end
        return emit("float", name .. "(" .. a.expr .. ", " .. b.expr .. ")")
    end
end

env.length = function(a)
    a = val(a)
    return emit("float", "length(" .. a.expr .. ")")
end

env.cross = function(a, b)
    a, b = val(a), val(b)
    if a.type ~= "vec3" or b.type ~= "vec3" then fail("cross() needs two vec3") end
    return emit("vec3", "cross(" .. a.expr .. ", " .. b.expr .. ")")
end

env.reflect = function(i, n)
    i, n = val(i), val(n)
    return emit(i.type, "reflect(" .. i.expr .. ", " .. n.expr .. ")")
end

env.clamp = function(x, lo, hi)
    x, lo, hi = val(x), val(lo), val(hi)
    return emit(x.type, "clamp(" .. x.expr .. ", " .. lo.expr .. ", " .. hi.expr .. ")")
end

env.mix = function(a, b, t)
    a, b, t = val(a), val(b), val(t)
    return emit(binary_type("mix", a, b), "mix(" .. a.expr .. ", " .. b.expr .. ", " .. t.expr .. ")")
end

-- texture(sampler, uv) -> vec4. The sampler is a "texture" parameter.
env.texture = function(sampler, uv)
    sampler, uv = val(sampler), val(uv)
    if sampler.type ~= "sampler2D" then fail("texture() needs a texture parameter as its first argument") end
    if uv.type ~= "vec2" then fail("texture() needs a vec2 uv, got " .. uv.type) end
    return emit("vec4", "texture(" .. sampler.expr .. ", " .. uv.expr .. ")")
end

-- Procedural noise, 0..1. hash/noise take a vec2 or vec3; fbm sums `octaves` (1..8, default 4) layers of noise.
local function noise_fn(name, glsl)
    return function(p, octaves)
        p = val(p)
        if p.type ~= "vec2" and p.type ~= "vec3" then fail(name .. "() needs a vec2 or vec3, got " .. p.type) end
        ctx.noise = true
        if name == "fbm" then
            octaves = octaves or 4
            if type(octaves) ~= "number" or octaves < 1 or octaves > 8 or octaves % 1 ~= 0 then
                fail("fbm() octaves must be a whole number from 1 to 8")
            end
            return emit("float", "k_fbm(" .. p.expr .. ", " .. octaves .. ")")
        end
        return emit("float", glsl .. "(" .. p.expr .. ")")
    end
end
env.hash = noise_fn("hash", "k_hash")
env.noise = noise_fn("noise", "k_noise")
env.fbm = noise_fn("fbm")

env.step = function(edge, x)
    edge, x = val(edge), val(x)
    return emit(x.type, "step(" .. edge.expr .. ", " .. x.expr .. ")")
end

env.smoothstep = function(e0, e1, x)
    e0, e1, x = val(e0), val(e1), val(x)
    return emit(x.type, "smoothstep(" .. e0.expr .. ", " .. e1.expr .. ", " .. x.expr .. ")")
end

-- loop(count, state, body) -> final state values
-- A real GLSL `for` loop (a Lua `for` unrolls its body `count` times, which makes huge shaders).
--   state : list of starting values (numbers or shader values) that the loop carries from one pass to the next
--   body  : function(i, a, b, ...) receiving the pass number i (0-based, a float) and the current state,
--           returning the new state in the same order and with the same types
-- Returns the state after the last pass:
--     local sum, weight = loop(8, { 0.0, 1.0 }, function(i, sum, weight)
--         return sum + noise(p * (i + 1.0)) * weight, weight * 0.5
--     end)
env.loop = function(count, state, body)
    if type(count) ~= "number" or count < 1 or count > 512 or count % 1 ~= 0 then
        fail("loop() needs a whole number of passes from 1 to 512")
    end
    if type(state) ~= "table" or is_value(state) or #state == 0 then fail("loop() needs a list of starting values: loop(count, { a, b }, function(i, a, b) ... end)") end
    if type(body) ~= "function" then fail("loop() needs a function as its third argument") end
    if not ctx then fail("loop() can only be used inside vertex/fragment functions") end

    -- Mutable variables outside the loop hold the state.
    local vars = {}
    for k = 1, #state do
        local v = val(state[k])
        if not COMPONENTS[v.type] then fail("loop state must be float/vec2/vec3/vec4, got " .. v.type) end
        ctx.tmp = ctx.tmp + 1
        local name = "s" .. ctx.tmp
        add_line(ctx, "    " .. v.type .. " " .. name .. " = " .. v.expr .. ";", user_line())
        vars[k] = new_value(v.type, name)
    end
    ctx.tmp = ctx.tmp + 1
    local counter = "i" .. ctx.tmp
    add_line(ctx, string.format("    for (int %s = 0; %s < %d; %s++) {", counter, counter, count, counter), user_line())
    local inputs = { new_value("float", "float(" .. counter .. ")") }
    for k = 1, #vars do inputs[k + 1] = vars[k] end
    local results = { body(table.unpack(inputs)) }
    if #results ~= #vars then
        fail(string.format("the loop body must return %d value(s) (the state), got %d", #vars, #results))
    end
    -- Copy every result to a temporary first, so a result that reads another state variable sees the old value.
    local copies = {}
    for k = 1, #vars do
        local r = val(results[k])
        if r.type ~= vars[k].type then
            fail(string.format("loop state #%d is a %s but the body returned a %s", k, vars[k].type, r.type))
        end
        copies[k] = emit(r.type, r.expr)
    end
    for k = 1, #vars do add_line(ctx, "    " .. vars[k].expr .. " = " .. copies[k].expr .. ";", user_line()) end
    add_line(ctx, "    }", user_line())
    return table.unpack(vars)
end

---------------------------------------------------------------------------------------
-- Shader object
---------------------------------------------------------------------------------------

local Shader = {}
Shader.__index = Shader

local function check_name(what, name)
    if type(name) ~= "string" or not name:match("^[%a_][%w_]*$") then
        fail(what .. " name must be an identifier, got '" .. tostring(name) .. "'")
    end
end

function env.shader(name)
    local info = user_frame()
    return setmetatable({
        name = name or "Shader",
        src = info and info.short_src or "?",
        line = info and info.currentline or 0,
        attributes = {}, -- { name, type }
        attr_decls = {}, -- { text, line }, vertex stage only
        uniform_decls = {},
        params = {},
        builtins = {},
        names = {},
    }, Shader)
end

function Shader:attribute(name, t)
    check_name("attribute", name)
    if not COMPONENTS[t] then fail("attribute type must be float, vec2, vec3 or vec4, got '" .. tostring(t) .. "'") end
    if self.names["a:" .. name] then fail("attribute '" .. name .. "' is declared twice") end
    self.names["a:" .. name] = true
    local location = STANDARD_LOCATIONS[name]
    if not location then
        self.custom_attrs = (self.custom_attrs or 0) + 1
        location = 3 + self.custom_attrs
    end
    self.attributes[#self.attributes + 1] = { name = name, type = t }
    self.attr_decls[#self.attr_decls + 1] = {
        text = string.format("layout(location = %d) in %s a_%s;", location, t, name), line = user_line() }
    return new_value(t, "a_" .. name)
end

function Shader:builtin(name)
    local t = BUILTINS[name]
    if not t then
        local known = {}
        for k in pairs(BUILTINS) do known[#known + 1] = k end
        table.sort(known)
        fail("unknown builtin '" .. tostring(name) .. "', available: " .. table.concat(known, ", "))
    end
    if name == "frag_coord" then return new_value(t, "gl_FragCoord") end -- not a uniform
    if not self.builtins[name] then
        self.builtins[name] = true
        local instance = name == "model" or name == "normal_matrix" -- see the per-instance block in generate()
    self.uniform_decls[#self.uniform_decls + 1] = { text = "uniform " .. t .. " u_" .. name .. ";", line = user_line(), instance = instance }
    end
    return new_value(t, "u_" .. name)
end

-- S:param(name, kind, default, { min = , max = })
-- kind: "float", "vec2", "vec3", "vec4", "color" (vec3 or vec4, by the length of default) or "texture".
-- Parameters become uniforms and show up in the editor with a widget matching their kind.
function Shader:param(name, kind, default, opts)
    check_name("parameter", name)
    if self.names["p:" .. name] then fail("parameter '" .. name .. "' is declared twice") end
    opts = opts or {}

    -- Texture slot: S:param("albedo", "texture", "Content/Textures/x.png" or nil, { srgb = true })
    -- A material fills the slot with a path; without one the default (or plain white) is used.
    if kind == "texture" then
        if default ~= nil and type(default) ~= "string" then
            fail("the default of texture parameter '" .. name .. "' must be a path string or nil")
        end
        self.names["p:" .. name] = true
        self.params[#self.params + 1] = {
            name = name, kind = "texture", type = "sampler2D", uniform = "p_" .. name,
            default = {}, texture = default or "", srgb = opts.srgb ~= false,
        }
        self.uniform_decls[#self.uniform_decls + 1] = { text = "uniform sampler2D p_" .. name .. ";", line = user_line() }
        return new_value("sampler2D", "p_" .. name)
    end

    local gtype
    if kind == "color" then
        default = default or { 1, 1, 1 }
        gtype = (type(default) == "table" and #default == 4) and "vec4" or "vec3"
    elseif COMPONENTS[kind] then
        gtype = kind
        if default == nil then
            default = {}
            for i = 1, COMPONENTS[kind] do default[i] = 0 end
        end
    else
        fail("parameter kind must be float, vec2, vec3, vec4 or color, got '" .. tostring(kind) .. "'")
    end
    if type(default) == "number" then default = { default } end
    if type(default) ~= "table" or #default ~= COMPONENTS[gtype] then
        fail(string.format("default of '%s' must have %d number(s)", name, COMPONENTS[gtype]))
    end
    for i = 1, #default do
        if type(default[i]) ~= "number" then fail("default of '" .. name .. "' must contain only numbers") end
    end

    self.names["p:" .. name] = true
    self.params[#self.params + 1] = {
        name = name, kind = kind, type = gtype, uniform = "p_" .. name,
        default = { table.unpack(default) }, min = opts.min, max = opts.max,
    }
    self.uniform_decls[#self.uniform_decls + 1] = { text = "uniform " .. gtype .. " p_" .. name .. ";", line = user_line() }
    return new_value(gtype, "p_" .. name)
end

function Shader:vertex(fn)
    if type(fn) ~= "function" then fail("S:vertex expects a function") end
    self._vertex = fn
end

function Shader:fragment(fn)
    if type(fn) ~= "function" then fail("S:fragment expects a function") end
    self._fragment = fn
end

-- A shader that covers the whole screen instead of a mesh: sky, clouds, fog, post effects.
-- The function receives v.uv (vec2, 0..1 over the screen) and v.ray (vec3, unit world-space
-- direction from the camera through this pixel) and returns the color, like S:fragment.
-- It is drawn by a pipeline pass (see Content/Pipelines), not by an actor.
function Shader:fullscreen(fn)
    if type(fn) ~= "function" then fail("S:fullscreen expects a function") end
    self._fragment = fn
    self._fullscreen = true
end

-- S:cutout(threshold): pixels whose alpha is below the threshold are dropped (discard), the rest are drawn opaque.
-- For cut-out cards such as grass or leaves: no sorting and no blending, the alpha only decides what is there.
function Shader:cutout(threshold)
    if type(threshold) ~= "number" then fail("S:cutout expects a number from 0 to 1") end
    self._cutout = threshold
end

local function shader_error(self, msg)
    error(self.src .. ":" .. self.line .. ": " .. msg, 0)
end

-- Runs one stage function with a fresh emission context.
local function trace(fn, arg)
    ctx = { lines = {}, map = {}, tmp = 0, noise = false }
    last_user_line = nil
    debug.sethook(track_line, "l")
    local ok, a, b = pcall(fn, arg)
    debug.sethook()
    local c = ctx
    ctx = nil
    if not ok then error(a, 0) end
    return c, a, b
end

local function sorted_keys(t)
    local keys = {}
    for k in pairs(t) do keys[#keys + 1] = k end
    table.sort(keys)
    return keys
end

-- Joins header declarations, the traced body and closing lines into GLSL text + line map.
local function assemble(self, header, c, closing, closing_line)
    local out, map = {}, {}
    local function push(text, line)
        out[#out + 1] = text
        map[#map + 1] = line or 0
    end
    push("#version 450 core")
    push("// Generated by Magnum from shader '" .. self.name .. "'. Edit the .lua file, not this text.")
    for _, d in ipairs(header) do push(d.text, d.line) end
    if c.noise then
        for _, text in ipairs(NOISE_GLSL) do push(text) end
    end
    push("void main() {")
    for i, text in ipairs(c.lines) do push(text, c.map[i]) end
    for _, text in ipairs(closing) do push("    " .. text, closing_line) end
    push("}")
    return table.concat(out, "\n") .. "\n", map
end

-- Fullscreen shaders: the engine draws one oversized triangle with no vertex buffer, so the
-- vertex stage is fixed and only the fragment function is traced.
function Shader:generate_fullscreen()
    if self._vertex then shader_error(self, "shader '" .. self.name .. "' uses S:fullscreen, it cannot also have S:vertex") end
    local vertex = table.concat({
        "#version 450 core",
        "// Generated by Magnum from shader '" .. self.name .. "' (fullscreen). Edit the .lua file, not this text.",
        "uniform mat4 u_inv_view_proj;",
        "uniform vec3 u_camera_pos;",
        "out vec2 v_uv;",
        "out vec3 v_ray;",
        "void main() {",
        "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));",
        "    vec4 clip = vec4(p * 2.0 - 1.0, 1.0, 1.0);",
        "    vec4 world = u_inv_view_proj * clip;",
        "    v_uv = p;",
        "    v_ray = world.xyz / world.w - u_camera_pos; // linear across the screen; normalized per pixel",
        "    gl_Position = clip;",
        "}", "" }, "\n")

    local fheader = {}
    for _, d in ipairs(self.uniform_decls) do fheader[#fheader + 1] = d end
    local fline = debug.getinfo(self._fragment, "S").lastlinedefined
    fheader[#fheader + 1] = { text = "in vec2 v_uv;", line = fline }
    fheader[#fheader + 1] = { text = "in vec3 v_ray;", line = fline }
    fheader[#fheader + 1] = { text = "out vec4 o_color;", line = fline }

    local user_fn = self._fragment
    local function with_ray(v)
        v.ray = env.normalize(v.ray)
        return user_fn(v)
    end
    local fc, color = trace(with_ray, { uv = new_value("vec2", "v_uv"), ray = new_value("vec3", "v_ray") })
    color = val(color)
    local fclosing
    if color.type == "vec4" then
        fclosing = { "o_color = " .. color.expr .. ";" }
    elseif color.type == "vec3" then
        fclosing = { "o_color = vec4(" .. color.expr .. ", 1.0);" }
    else
        fail_in(self._fragment, "fullscreen function must return a vec4 (or vec3) color, got " .. color.type)
    end
    local fragment_glsl, fragment_map = assemble(self, fheader, fc, fclosing, fline)

    local vertex_map = {}
    for _ in vertex:gmatch("\n") do vertex_map[#vertex_map + 1] = 0 end
    return {
        name = self.name,
        vertex = vertex, vertex_map = vertex_map,
        fragment = fragment_glsl, fragment_map = fragment_map,
        params = self.params,
    }
end

function Shader:generate()
    if self._fullscreen then return self:generate_fullscreen() end
    if not self._vertex then shader_error(self, "shader '" .. self.name .. "' has no vertex function (S:vertex)") end
    if not self._fragment then shader_error(self, "shader '" .. self.name .. "' has no fragment function (S:fragment)") end

    -- Vertex stage
    local attrs = {}
    for _, a in ipairs(self.attributes) do attrs[a.name] = new_value(a.type, "a_" .. a.name) end
    local vc, pos, varyings = trace(self._vertex, attrs)
    pos = val(pos)
    if pos.type ~= "vec4" then
        fail_in(self._vertex, "vertex function must return a vec4 clip-space position, got " .. pos.type)
    end
    varyings = varyings or {}
    if type(varyings) ~= "table" or is_value(varyings) then
        fail_in(self._vertex, "the second return value of the vertex function must be a table of varyings")
    end

    local vheader, fheader = {}, {}
    for _, d in ipairs(self.attr_decls) do vheader[#vheader + 1] = d end
    -- model and normal_matrix come from per-instance attributes when the engine draws instanced
    -- (MAGNUM_INSTANCED is defined for that variant only); everything else stays a uniform.
    local per_instance = {}
    for _, d in ipairs(self.uniform_decls) do
        if d.instance then
            per_instance[#per_instance + 1] = d
        else
            vheader[#vheader + 1] = d
        end
        fheader[#fheader + 1] = d
    end
    if #per_instance > 0 then
        local vline = debug.getinfo(self._vertex, "S").lastlinedefined
        local function vpush(text) vheader[#vheader + 1] = { text = text, line = vline } end
        vpush("#ifdef MAGNUM_INSTANCED")
        vpush("layout(location = 9) in mat4 a_instance_model;")
        vpush("layout(location = 13) in mat3 a_instance_normal;")
        vpush("#define u_model a_instance_model")
        vpush("#define u_normal_matrix a_instance_normal")
        vpush("#else")
        for _, d in ipairs(per_instance) do vheader[#vheader + 1] = d end
        vpush("#endif")
    end

    local vclosing = { "gl_Position = " .. pos.expr .. ";" }
    local frag_in = {}
    local vline = debug.getinfo(self._vertex, "S").lastlinedefined
    for _, name in ipairs(sorted_keys(varyings)) do
        check_name("varying", name)
        local v = val(varyings[name])
        if not COMPONENTS[v.type] then fail_in(self._vertex, "varying '" .. name .. "' has unsupported type " .. v.type) end
        vheader[#vheader + 1] = { text = "out " .. v.type .. " v_" .. name .. ";", line = vline }
        fheader[#fheader + 1] = { text = "in " .. v.type .. " v_" .. name .. ";", line = vline }
        vclosing[#vclosing + 1] = "v_" .. name .. " = " .. v.expr .. ";"
        frag_in[name] = new_value(v.type, "v_" .. name)
    end
    local vertex_glsl, vertex_map = assemble(self, vheader, vc, vclosing, vline)

    -- Fragment stage
    local fc, color = trace(self._fragment, frag_in)
    color = val(color)
    local fline = debug.getinfo(self._fragment, "S").lastlinedefined
    fheader[#fheader + 1] = { text = "out vec4 o_color;", line = fline }
    local fclosing
    if color.type == "vec4" and self._cutout then
        fclosing = {
            "vec4 c_out = " .. color.expr .. ";",
            string.format("if (c_out.a < %.6f) discard;", self._cutout),
            "o_color = c_out;",
        }
    elseif color.type == "vec4" then
        fclosing = { "o_color = " .. color.expr .. ";" }
    elseif color.type == "vec3" then
        fclosing = { "o_color = vec4(" .. color.expr .. ", 1.0);" }
    else
        fail_in(self._fragment, "fragment function must return a vec4 (or vec3) color, got " .. color.type)
    end
    local fragment_glsl, fragment_map = assemble(self, fheader, fc, fclosing, fline)

    return {
        name = self.name,
        vertex = vertex_glsl, vertex_map = vertex_map,
        fragment = fragment_glsl, fragment_map = fragment_map,
        params = self.params,
    }
end

---------------------------------------------------------------------------------------
-- Entry point used by the engine
---------------------------------------------------------------------------------------

-- Plain Lua helpers a shader script may use for constant math and tables.
for _, name in ipairs({ "math", "string", "table", "ipairs", "pairs", "select", "type", "tostring",
                        "tonumber", "error", "assert", "next" }) do
    env[name] = _G[name]
end
env.unpack = table.unpack
env.pi = math.pi

M.log = nil -- set by the engine; print() inside shader scripts goes to the editor console
env.print = function(...)
    local parts = {}
    for i = 1, select("#", ...) do parts[i] = tostring((select(i, ...))) end
    if M.log then M.log(table.concat(parts, "\t")) end
end

-- Runs a shader script and returns the generated GLSL, line maps and parameters.
-- Errors are raised as strings of the form "chunk:line: message".
function M.compile(source, chunkname)
    local f, err = load(source, chunkname, "t", env)
    if not f then error(err, 0) end
    local S = f()
    if getmetatable(S) ~= Shader then
        error(chunkname:gsub("^@", "") .. ":1: the script must end with 'return S' where S = shader(\"Name\")", 0)
    end
    return S:generate()
end

return M
