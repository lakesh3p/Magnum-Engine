#pragma once

#include "game/Physics.h"
#include "scene/Scene.h"
#include "ui/UiSystem.h"

#include <glm/glm.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace magnum {

// What the player is doing, supplied by whoever runs the game (the editor viewport).
struct GameInput {
    std::function<bool(int)> keyDown;   // GLFW key code
    std::function<bool(int)> mouseDown; // 0 left, 1 right, 2 middle
    glm::vec2 mouseDelta{0.0f};         // pixels moved since the last frame (while the mouse is captured)
    glm::vec2 mousePosition{0.5f};      // where the mouse is over the game view, 0..1 from the top left
    bool mouseInView = false;           // ... and whether it is over the view at all
    float aspect = 16.0f / 9.0f;        // width / height of the game view
    glm::vec2 viewSize{1280.0f, 720.0f}; // the game view in pixels (the UI lays itself out against it)
    float mouseWheel = 0.0f;            // wheel notches since the last frame, positive = away from the user
};

// Something a script asked to be drawn over the game view this frame. Positions and sizes are fractions (0..1) of the
// view, measured from the top left.
struct HudItem {
    enum class Kind { Text, Rect };
    Kind kind = Kind::Text;
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    float size = 20.0f; // text height in pixels
    glm::vec4 color{1.0f};
    std::string text;
};

struct CameraView {
    bool valid = false;
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f}; // degrees, like a transform
    float fov = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    bool orthographic = false;
    float orthoSize = 5.0f; // orthographic: half the height of the view in world units
};

// A ray from the camera through a point of the view (ndc: -1..1, y up). Orthographic cameras give parallel rays.
struct ViewRay {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
};
ViewRay CameraRay(const CameraView& camera, float aspect, glm::vec2 ndc);

// Runs the game rules of a scene while it plays: Lua scripts on actors (Script component), physics for actors with
// a Collider, and the camera the player looks through. The scene itself is the state: stopping play restores it from
// the copy the editor made before starting, so nothing here needs to undo anything.
//
// Frame order: scripts' on_update, physics, then on_hit / on_trigger / on_trigger_exit for what physics reported.
//
// Script API (see Content/Scripts for working examples). A script defines any of
//     on_start(self)  on_update(self, dt)  on_hit(self, other)  on_trigger(self, other)  on_trigger_exit(self, other)
// `self` is the actor: self.name, .position, .rotation, .scale, .velocity (vec3), .grounded, .forward, .right, .up,
// .material, .state (a table that is yours), .sprite (see below), :destroy(), :move(v), :look_at(v), :add_collider(),
// :add_rigidbody(), :add_script(path), :add_sprite(image[, width, height]).
// Sprites: actor.sprite.texture / .frame / .tint / .opacity / .flip_x / .flip_y / .order, :play(first, last, fps[, loop]), :stop().
// Globals: vec3(x, y, z), input.key("w") / .pressed("space") / .mouse_delta() / .mouse(button) / .mouse_position() /
// .mouse_world([z]), world.find(name) / .find_all(prefix) / .spawn(kind, x, y, z[, name]) / .spawn_sprite(image, x, y[, w, h])
// / .spawn_decal(image, point, normal[, size, depth])
// / .destroy(actor) / .raycast(origin, dir[, max]) / .screen_ray(x, y),
// time.dt / time.elapsed, hud.text(x, y, text[, size, r, g, b, a]) / hud.rect(x, y, w, h, r, g, b, a),
// game.stop(), game.vars (a table shared by every script and UI document), log(...),
// game.pause() / .resume() / .toggle_pause() / .paused(): while paused scripts, physics and sprite animations stand still and
// only the UI runs (put the pause menu in a UI document), and `ui` for menus, HUDs and dialogs (see docs/UI.md):
// ui.panel{...}, ui.button{...}, ui.load(...).
// input.mouse(button) is false while the mouse is over a blocking UI element; input.ui_blocked() tells.
class Gameplay {
public:
    Gameplay();
    ~Gameplay();
    Gameplay(const Gameplay&) = delete;
    Gameplay& operator=(const Gameplay&) = delete;

    // Creates the physics bodies and the script instances, then calls on_start. Scripts that fail to load are
    // reported in the log and skipped; play still starts.
    bool Begin(Scene& scene, const std::filesystem::path& projectRoot);
    void Update(Scene& scene, float dt, const GameInput& input);
    void End();

    bool Running() const;
    bool StopRequested() const; // a script called game.stop()
    const std::vector<HudItem>& Hud() const;
    Physics& GetPhysics();
    // The UI of the running game (draw it with BuildDrawList after Update).
    UiSystem& Ui();
    // Runs a line of Lua in the running game's VM (the editor console: `lua ...`). False with a message when it fails.
    bool RunLua(const std::string& source, std::string& error);
    size_t ScriptCount() const; // running script instances

    // The GLFW key code of a script key name ("a", "space", "left" ...), or -1.
    static int KeyCodeFromName(const std::string& name);

    // The primary camera of the scene (the first one marked primary, else the first camera), if any.
    static CameraView FindCamera(const Scene& scene);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace magnum
