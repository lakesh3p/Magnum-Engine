#pragma once

#include "scene/Components.h"

#include <glm/glm.hpp>

#include <string>

namespace magnum {

// Size of an image file in pixels, read from its header and cached until the file changes. `relPath` is relative to the
// open project (see core/Paths.h). False if the file is missing or not an image.
bool ImageSize(const std::string& relPath, int& width, int& height);

// The world size of a sprite: its own width and height when both are set, otherwise one atlas cell of its image
// divided by pixelsPerUnit (a sprite without an image is 1 x 1).
glm::vec2 SpriteWorldSize(const SpriteComponent& sprite);

// The UV rectangle (x, y = bottom left, z, w = size; sizes are negative for flipped sprites) of the sprite's current frame.
glm::vec4 SpriteUvRect(const SpriteComponent& sprite);

// Moves the animation of a sprite on by `dt` seconds (does nothing unless `animate` is set).
void AdvanceSpriteAnimation(SpriteComponent& sprite, float dt);

} // namespace magnum
