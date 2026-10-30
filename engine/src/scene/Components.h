#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstdint>
#include <string>

namespace magnum {

// Stable identity of an actor. Commands and (later) saved scenes refer to actors by this id,
// never by entt::entity, which changes when an actor is deleted and restored.
struct IdComponent {
    uint64_t id = 0;
};

struct NameComponent {
    std::string name = "Actor";
};

struct TransformComponent {
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f}; // degrees: x = pitch, y = yaw, z = roll
    glm::vec3 scale{1.0f};

    glm::mat4 RotationMatrix() const {
        glm::mat4 r(1.0f);
        r = glm::rotate(r, glm::radians(rotation.y), glm::vec3(0, 1, 0));
        r = glm::rotate(r, glm::radians(rotation.x), glm::vec3(1, 0, 0));
        r = glm::rotate(r, glm::radians(rotation.z), glm::vec3(0, 0, 1));
        return r;
    }
    glm::mat4 Matrix() const {
        return glm::translate(glm::mat4(1.0f), position) * RotationMatrix() * glm::scale(glm::mat4(1.0f), scale);
    }
    // Direction the actor looks along (-Z in local space).
    glm::vec3 Forward() const { return glm::normalize(glm::vec3(RotationMatrix() * glm::vec4(0, 0, -1, 0))); }
};

enum class Primitive : int {
    Cube, Plane, Sphere, Cylinder, Cone,
    Capsule, Hemisphere, Torus, Arch, Pyramid, Wedge, Prism, Diamond, Stairs, Disc, Tube, Ring,
    Quad // a flat square facing +Z (sprites, decals, 2D ground)
};
// Saved scenes store the index, so new primitives are only ever added at the end.
inline constexpr const char* kPrimitiveNames[] = {
    "Cube", "Plane", "Sphere", "Cylinder", "Cone",
    "Capsule", "Hemisphere", "Torus", "Arch", "Pyramid", "Wedge", "Prism", "Diamond", "Stairs", "Disc", "Tube", "Ring", "Quad"};
inline constexpr int kPrimitiveCount = static_cast<int>(sizeof(kPrimitiveNames) / sizeof(kPrimitiveNames[0]));

// Half-size of each primitive's bounding box in its own space (before the actor's scale). Picking, the selection box and
// colliders all use it.
inline glm::vec3 PrimitiveHalfExtents(Primitive primitive) {
    switch (primitive) {
    case Primitive::Plane: return {0.5f, 0.005f, 0.5f};
    case Primitive::Capsule: return {0.25f, 0.5f, 0.25f};
    case Primitive::Hemisphere: return {0.5f, 0.25f, 0.5f};
    case Primitive::Torus: return {0.5f, 0.16f, 0.5f};
    case Primitive::Arch: return {0.5f, 0.5f, 0.16f};
    case Primitive::Disc: return {0.5f, 0.05f, 0.5f};
    case Primitive::Ring: return {0.5f, 0.06f, 0.5f};
    case Primitive::Quad: return {0.5f, 0.5f, 0.005f};
    default: return glm::vec3(0.5f);
    }
}

struct MeshComponent {
    Primitive primitive = Primitive::Cube;
    std::string material = "Content/Materials/Default.lua"; // material file (see material/MaterialData.h)
    std::string model;                                      // model file of any format the importer reads; empty = the primitive
};

// A directional light; its direction comes from the actor's rotation.
struct LightComponent {
    glm::vec3 color{1.0f, 0.96f, 0.88f};
    float intensity = 1.0f;
};

// ---- Gameplay components ------------------------------------------------------------------------------------------

// A point of view. While playing, the editor shows the world through the primary camera.
struct CameraComponent {
    float fov = 60.0f; // vertical, degrees
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    bool primary = true;
    bool orthographic = false; // no perspective: the 2D camera. It looks down -Z from where it stands.
    float orthoSize = 5.0f;    // orthographic: half the height of the view, in world units
};

// A collision volume that fits the actor's mesh (or a small box for actors without one), scaled by `size`.
// Boxes follow the actor's rotation as a bounding box. A trigger reports overlaps and blocks nothing.
enum class ColliderShape : int { Box, Sphere };
inline constexpr const char* kColliderShapeNames[] = {"Box", "Sphere"};

struct ColliderComponent {
    ColliderShape shape = ColliderShape::Box;
    glm::vec3 size{1.0f};
    bool trigger = false;
};

// Gives a collider physics: gravity, velocity, being stopped by other colliders. A kinematic body is moved only by
// scripts but still blocks the others.
struct RigidbodyComponent {
    bool useGravity = true;
    float gravityScale = 1.0f;
    float drag = 0.0f;
    bool kinematic = false;
    bool lockZ = false; // never moves along Z: a 2D body (sprites, side-scrollers)
};

// A flat image drawn in the world: a sprite. Part of an image (an atlas cell) can be shown, and the cells can play as
// an animation. Sprites are sorted back to front by `order`, then by depth, and blend with what is behind them.
struct SpriteComponent {
    std::string texture;           // project-relative image path; "" draws a plain rectangle
    glm::vec3 tint{1.0f};
    float opacity = 1.0f;
    float width = 0.0f;            // world size; 0 = taken from the image (pixels / pixelsPerUnit, per atlas cell)
    float height = 0.0f;
    float pixelsPerUnit = 100.0f;
    int columns = 1;               // the image is a grid of columns x rows cells
    int rows = 1;
    int frame = 0;                 // shown cell, counted row by row from the top left
    int firstFrame = 0;            // the animation starts at this cell ...
    int frameCount = 0;            // ... and uses this many cells; 0 = up to the last one
    float fps = 8.0f;
    bool animate = false;
    bool loop = true;
    float animationTime = 0.0f;    // seconds into the animation (runtime state, saved so a play session restarts clean)
    bool flipX = false;
    bool flipY = false;
    bool billboard = false;        // always faces the camera
    bool pixelated = false;        // nearest-neighbour filtering: crisp pixel art
    int order = 0;                 // draw order: higher is in front
};

// A picture projected onto whatever solid surfaces lie inside this actor's box (bullet holes, dirt, blood, signs on a
// wall, a glowing rune on the floor). The box is the actor's transform: the image is projected along the actor's local Z
// axis and fills its local X / Y, so scale sets the size and depth of the projection. A "material" can replace the
// built-in decal shader with your own (see Engine/Shaders/decal.lua).
struct DecalComponent {
    std::string texture;   // project-relative image path
    glm::vec3 tint{1.0f};
    float opacity = 1.0f;
    float angleFade = 0.5f; // 0 = none; fades the decal out on surfaces that are almost parallel to the projection
    std::string material;  // optional: a material whose shader was written for decals
    int order = 0;         // higher draws later (on top of other decals)
};

// A screen-space UI document (Content/UI/*.lua) drawn over the game view while playing, and over the editor viewport as
// a live preview. Several canvases can be on screen at once; a higher order is drawn on top and gets the mouse first.
enum class CanvasScaleMode : int { Constant, Height, Width, Fit, Fill };
inline constexpr const char* kCanvasScaleNames[] = {"Constant", "Height", "Width", "Fit", "Fill"};

struct CanvasComponent {
    std::string document;          // project-relative path of the UI document
    int order = 0;
    bool visible = true;
    CanvasScaleMode scaleMode = CanvasScaleMode::Height; // how the design size is fitted to the view
    float referenceWidth = 1280.0f;  // the design size (UI units) the scale mode measures against
    float referenceHeight = 720.0f;
};

// A Lua gameplay script (Content/Scripts/*.lua) that runs on this actor while playing.
struct ScriptComponent {
    std::string script;
};

} // namespace magnum
