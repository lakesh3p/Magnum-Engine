#pragma once

#include "scene/Components.h"

#include <string>

namespace magnum::editor {

enum class SpawnKind {
    Empty, Cube, Plane, Sphere, DirectionalLight, Cylinder, Cone,
    Capsule, Hemisphere, Torus, Arch, Pyramid, Wedge, Prism, Diamond, Stairs, Disc, Tube, Ring,
    Camera, TriggerVolume, PhysicsCube, PhysicsSphere, Character, Wall, Ground,
    Sprite, PhysicsSprite, Tile, Camera2D, Player2D, Quad, Decal, Canvas,
    Count
};

// Components a spawned actor starts with besides its mesh.
enum SpawnFeature : unsigned {
    kFeatureCamera = 1,
    kFeatureCollider = 2,
    kFeatureRigidbody = 4,
    kFeatureTrigger = 8,       // the collider is a trigger
    kFeatureSphereCollider = 16,
    kFeatureSprite = 32,
    kFeatureOrtho = 64,        // the camera is orthographic
    kFeatureLockZ = 128,       // a 2D body
    kFeatureDecal = 256,
    kFeatureCanvas = 512,      // a UI canvas (starts with the project's hud document when there is one)
};

// What each kind is called, the console word that spawns it (`spawn cube`) and what it is made of.
struct SpawnInfo {
    SpawnKind kind;
    const char* name;
    const char* command;
    bool isMesh;
    Primitive primitive;
    float lift;      // height of the actor's origin above the ground it is dropped on
    unsigned features = 0;
    const char* script = ""; // a gameplay script it runs ("" = none)
    float scaleX = 1.0f, scaleY = 1.0f, scaleZ = 1.0f;
    float depth = 0.0f; // offset along Z from where it is dropped (a 2D camera stands back from the sprites)
};
const SpawnInfo& GetSpawnInfo(SpawnKind kind);
bool FindSpawnKind(const std::string& command, SpawnKind& kind); // `command` in lower case

} // namespace magnum::editor
