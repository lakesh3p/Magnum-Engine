#include "commands/SpawnKinds.h"

#include <cstddef>

namespace magnum::editor {

namespace {

const SpawnInfo kSpawnInfos[] = {
    {SpawnKind::Empty, "Empty Actor", "empty", false, Primitive::Cube, 0.0f},
    {SpawnKind::Cube, "Cube", "cube", true, Primitive::Cube, 0.5f},
    {SpawnKind::Plane, "Plane", "plane", true, Primitive::Plane, 0.0f},
    {SpawnKind::Sphere, "Sphere", "sphere", true, Primitive::Sphere, 0.5f},
    {SpawnKind::DirectionalLight, "Directional Light", "light", false, Primitive::Cube, 4.0f},
    {SpawnKind::Cylinder, "Cylinder", "cylinder", true, Primitive::Cylinder, 0.5f},
    {SpawnKind::Cone, "Cone", "cone", true, Primitive::Cone, 0.5f},
    {SpawnKind::Capsule, "Capsule", "capsule", true, Primitive::Capsule, 0.5f},
    {SpawnKind::Hemisphere, "Hemisphere", "hemisphere", true, Primitive::Hemisphere, 0.25f},
    {SpawnKind::Torus, "Torus", "torus", true, Primitive::Torus, 0.16f},
    {SpawnKind::Arch, "Arch", "arch", true, Primitive::Arch, 0.5f},
    {SpawnKind::Pyramid, "Pyramid", "pyramid", true, Primitive::Pyramid, 0.5f},
    {SpawnKind::Wedge, "Wedge", "wedge", true, Primitive::Wedge, 0.5f},
    {SpawnKind::Prism, "Prism", "prism", true, Primitive::Prism, 0.5f},
    {SpawnKind::Diamond, "Diamond", "diamond", true, Primitive::Diamond, 0.5f},
    {SpawnKind::Stairs, "Stairs", "stairs", true, Primitive::Stairs, 0.5f},
    {SpawnKind::Disc, "Disc", "disc", true, Primitive::Disc, 0.05f},
    {SpawnKind::Tube, "Tube", "tube", true, Primitive::Tube, 0.5f},
    {SpawnKind::Ring, "Ring", "ring", true, Primitive::Ring, 0.06f},
    // Gameplay
    {SpawnKind::Camera, "Camera", "camera", false, Primitive::Cube, 1.6f, kFeatureCamera},
    {SpawnKind::TriggerVolume, "Trigger Volume", "trigger", false, Primitive::Cube, 1.5f, kFeatureCollider | kFeatureTrigger, "", 3.0f, 3.0f, 3.0f},
    {SpawnKind::PhysicsCube, "Physics Cube", "physcube", true, Primitive::Cube, 1.0f, kFeatureCollider | kFeatureRigidbody},
    {SpawnKind::PhysicsSphere, "Physics Sphere", "physsphere", true, Primitive::Sphere, 1.0f, kFeatureCollider | kFeatureRigidbody | kFeatureSphereCollider},
    {SpawnKind::Character, "Player", "player", true, Primitive::Capsule, 0.9f, kFeatureCollider | kFeatureRigidbody,
     "Content/Scripts/character.lua", 1.6f, 1.8f, 1.6f},
    {SpawnKind::Wall, "Wall", "wall", true, Primitive::Cube, 1.5f, kFeatureCollider, "", 4.0f, 3.0f, 0.4f},
    {SpawnKind::Ground, "Ground", "ground", true, Primitive::Plane, 0.0f, kFeatureCollider, "", 20.0f, 1.0f, 20.0f},
    // 2D
    {SpawnKind::Sprite, "Sprite", "sprite", false, Primitive::Quad, 0.0f, kFeatureSprite},
    {SpawnKind::PhysicsSprite, "Physics Sprite", "physsprite", false, Primitive::Quad, 0.0f, kFeatureSprite | kFeatureCollider | kFeatureRigidbody | kFeatureLockZ},
    {SpawnKind::Tile, "Tile", "tile", false, Primitive::Quad, 0.0f, kFeatureSprite | kFeatureCollider},
    {SpawnKind::Camera2D, "Camera 2D", "camera2d", false, Primitive::Quad, 0.0f, kFeatureCamera | kFeatureOrtho, "", 1.0f, 1.0f, 1.0f, 10.0f},
    {SpawnKind::Player2D, "Player 2D", "player2d", false, Primitive::Quad, 0.0f, kFeatureSprite | kFeatureCollider | kFeatureRigidbody | kFeatureLockZ,
     "Content/Scripts/platformer.lua"},
    {SpawnKind::Quad, "Quad", "quad", true, Primitive::Quad, 0.5f},
    {SpawnKind::Decal, "Decal", "decal", false, Primitive::Quad, 0.5f, kFeatureDecal, "", 2.0f, 2.0f, 1.0f},
    {SpawnKind::Canvas, "Canvas", "canvas", false, Primitive::Quad, 0.0f, kFeatureCanvas},
};
static_assert(sizeof(kSpawnInfos) / sizeof(kSpawnInfos[0]) == static_cast<size_t>(SpawnKind::Count), "describe every kind");

} // namespace

const SpawnInfo& GetSpawnInfo(SpawnKind kind) {
    for (const SpawnInfo& info : kSpawnInfos) {
        if (info.kind == kind) return info;
    }
    return kSpawnInfos[0];
}

bool FindSpawnKind(const std::string& command, SpawnKind& kind) {
    for (const SpawnInfo& info : kSpawnInfos) {
        if (command == info.command) {
            kind = info.kind;
            return true;
        }
    }
    return false;
}

} // namespace magnum::editor
