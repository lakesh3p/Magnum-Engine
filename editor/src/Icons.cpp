#include "Icons.h"

#include "Theme.h"

#include <algorithm>
#include <cmath>

namespace magnum::editor {

namespace {

constexpr float kPi = 3.14159265f;

const char* kNames[] = {"Play", "Stop", "Save", "Undo", "Redo", "Move", "Rotate", "Scale", "World", "Local", "Snap",
                        "Folder", "Cube", "Sphere", "Plane", "Light", "Actor", "Search", "Add", "Trash", "Reload", "Grid",
                        "Material", "Graph", "Texture", "Code", "Scene", "ChevronRight", "ChevronDown", "Up", "Dot",
                        "Select", "Pause", "Cloud", "Person", "Camera", "Lock", "Maximize", "ChevronLeft",
                        "Cylinder", "Cone", "Capsule", "Hemisphere", "Torus", "Arch", "Pyramid", "Wedge", "Prism", "Diamond",
                        "Stairs", "Disc", "Tube", "Ring", "Shader", "Script", "Collider", "Rigidbody"};
static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<size_t>(Icon::Count), "name every icon");

} // namespace

// Every icon is drawn on a unit square (-0.5 .. 0.5) with one stroke width, round caps and round joins, the way the
// design draws them (stroke-linecap: round). `s` is the size in pixels.
void DrawIcon(ImDrawList* dl, Icon icon, ImVec2 c, float s, ImU32 col) {
    const float t = std::max(1.4f, s * 0.088f); // stroke width
    auto P = [&](float x, float y) { return ImVec2(c.x + x * s, c.y + y * s); };
    auto dot = [&](ImVec2 p) { dl->AddCircleFilled(p, t * 0.5f, col, 10); };

    auto line = [&](float x0, float y0, float x1, float y1) {
        dl->AddLine(P(x0, y0), P(x1, y1), col, t);
        dot(P(x0, y0));
        dot(P(x1, y1));
    };
    auto poly = [&](std::initializer_list<ImVec2> points, bool closed) {
        for (const ImVec2& p : points) dl->PathLineTo(P(p.x, p.y));
        dl->PathStroke(col, closed ? ImDrawFlags_Closed : 0, t);
        for (const ImVec2& p : points) dot(P(p.x, p.y)); // round joins
    };
    auto arc = [&](float cx, float cy, float r, float a0, float a1) {
        dl->PathArcTo(P(cx, cy), r * s, a0, a1, 28);
        dl->PathStroke(col, 0, t);
        dot(P(cx + r * std::cos(a0), cy + r * std::sin(a0)));
        dot(P(cx + r * std::cos(a1), cy + r * std::sin(a1)));
    };
    // An ellipse arc (a0..a1 in radians, 0 = right, positive = down on screen).
    auto earc = [&](float cx, float cy, float rx, float ry, float a0, float a1) {
        const int n = 24;
        for (int i = 0; i <= n; ++i) {
            const float a = a0 + (a1 - a0) * static_cast<float>(i) / static_cast<float>(n);
            dl->PathLineTo(P(cx + rx * std::cos(a), cy + ry * std::sin(a)));
        }
        dl->PathStroke(col, 0, t);
    };
    // An open arrowhead: two strokes from the tip, pointing along `angle`.
    auto head = [&](float x, float y, float angle, float size = 0.17f) {
        const float spread = 0.72f;
        for (const float side : {-1.0f, 1.0f}) {
            const float a = angle + kPi + side * spread;
            line(x, y, x + std::cos(a) * size, y + std::sin(a) * size);
        }
    };

    switch (icon) {
    case Icon::Play:
        dl->AddTriangleFilled(P(-0.22f, -0.36f), P(0.38f, 0.0f), P(-0.22f, 0.36f), col);
        poly({{-0.22f, -0.36f}, {0.38f, 0.0f}, {-0.22f, 0.36f}}, true); // rounds the corners
        break;
    case Icon::Stop:
        dl->AddRectFilled(P(-0.30f, -0.30f), P(0.30f, 0.30f), col, s * 0.12f);
        break;
    case Icon::Pause:
        dl->AddRectFilled(P(-0.28f, -0.32f), P(-0.06f, 0.32f), col, s * 0.06f);
        dl->AddRectFilled(P(0.06f, -0.32f), P(0.28f, 0.32f), col, s * 0.06f);
        break;
    case Icon::Save:
        dl->AddRect(P(-0.38f, -0.38f), P(0.38f, 0.38f), col, s * 0.10f, 0, t);
        dl->AddRectFilled(P(-0.20f, -0.38f), P(0.18f, -0.12f), col);
        dl->AddRect(P(-0.22f, 0.04f), P(0.22f, 0.38f), col, s * 0.04f, 0, t);
        break;
    case Icon::Undo:
    case Icon::Redo: {
        const float f = icon == Icon::Undo ? 1.0f : -1.0f; // redo is the mirror image
        dl->PathClear();
        for (int i = 0; i <= 20; ++i) { // from the arrow tip, over the top, down the right side
            const float a = 3.65f + (6.35f - 3.65f) * static_cast<float>(i) / 20.0f;
            dl->PathLineTo(P(f * (0.02f + 0.30f * std::cos(a)), 0.10f + 0.30f * std::sin(a)));
        }
        dl->PathStroke(col, 0, t);
        const float tx = f * (0.02f + 0.30f * std::cos(3.65f)), ty = 0.10f + 0.30f * std::sin(3.65f);
        dot(P(f * (0.02f + 0.30f * std::cos(6.35f)), 0.10f + 0.30f * std::sin(6.35f)));
        head(tx, ty, f > 0 ? 2.35f : kPi - 2.35f, 0.18f);
        break;
    }
    case Icon::Select:
        poly({{-0.28f, -0.40f}, {0.32f, -0.04f}, {0.04f, 0.05f}, {-0.05f, 0.34f}}, true);
        line(0.10f, 0.12f, 0.30f, 0.38f);
        break;
    case Icon::Move:
        line(-0.40f, 0.0f, 0.40f, 0.0f);
        line(0.0f, -0.40f, 0.0f, 0.40f);
        head(0.0f, -0.40f, -kPi / 2, 0.15f);
        head(0.0f, 0.40f, kPi / 2, 0.15f);
        head(-0.40f, 0.0f, kPi, 0.15f);
        head(0.40f, 0.0f, 0.0f, 0.15f);
        break;
    case Icon::Rotate:
        arc(0.0f, 0.0f, 0.34f, -0.3f, 4.55f);
        head(0.34f * std::cos(-0.3f), 0.34f * std::sin(-0.3f), -0.3f - kPi / 2 + 0.25f, 0.18f);
        break;
    case Icon::Scale:
        dl->AddRect(P(-0.38f, -0.04f), P(0.04f, 0.38f), col, s * 0.06f, 0, t);
        line(-0.06f, 0.06f, 0.38f, -0.38f);
        line(0.38f, -0.38f, 0.38f, -0.16f);
        line(0.38f, -0.38f, 0.16f, -0.38f);
        break;
    case Icon::World:
        dl->AddCircle(P(0, 0), s * 0.38f, col, 28, t);
        dl->PathEllipticalArcTo(P(0, 0), ImVec2(s * 0.16f, s * 0.38f), 0.0f, 0.0f, 2 * kPi, 24);
        dl->PathStroke(col, 0, t * 0.85f);
        line(-0.38f, 0.0f, 0.38f, 0.0f);
        break;
    case Icon::Local:
        line(-0.32f, 0.32f, -0.32f, -0.34f);
        line(-0.32f, 0.32f, 0.34f, 0.32f);
        line(-0.32f, 0.32f, 0.10f, -0.10f);
        head(-0.32f, -0.34f, -kPi / 2, 0.14f);
        head(0.34f, 0.32f, 0.0f, 0.14f);
        head(0.10f, -0.10f, -kPi / 4, 0.14f);
        break;
    case Icon::Snap:
        // a horseshoe magnet with its pole faces
        arc(0.0f, -0.02f, 0.28f, kPi, 2 * kPi);
        line(-0.28f, -0.02f, -0.28f, 0.26f);
        line(0.28f, -0.02f, 0.28f, 0.26f);
        dl->AddRectFilled(P(-0.38f, 0.26f), P(-0.18f, 0.38f), col, s * 0.03f);
        dl->AddRectFilled(P(0.18f, 0.26f), P(0.38f, 0.38f), col, s * 0.03f);
        break;
    case Icon::Folder:
        dl->AddRectFilled(P(-0.42f, -0.28f), P(-0.04f, -0.10f), col, s * 0.05f);
        dl->AddRectFilled(P(-0.42f, -0.16f), P(0.42f, 0.30f), col, s * 0.06f);
        break;
    case Icon::Cube:
        poly({{0.0f, -0.44f}, {0.38f, -0.22f}, {0.38f, 0.22f}, {0.0f, 0.44f}, {-0.38f, 0.22f}, {-0.38f, -0.22f}}, true);
        line(0.0f, 0.0f, 0.0f, 0.44f);
        line(0.0f, 0.0f, 0.38f, -0.22f);
        line(0.0f, 0.0f, -0.38f, -0.22f);
        break;
    case Icon::Sphere:
        dl->AddCircle(P(0, 0), s * 0.40f, col, 32, t);
        earc(0.0f, 0.0f, 0.40f, 0.14f, 0.0f, kPi);
        earc(0.0f, 0.0f, 0.16f, 0.40f, -kPi / 2, kPi / 2);
        break;
    case Icon::Cylinder:
        dl->PathEllipticalArcTo(P(0, -0.30f), ImVec2(s * 0.30f, s * 0.11f), 0.0f, 0.0f, 2 * kPi, 24);
        dl->PathStroke(col, 0, t);
        line(-0.30f, -0.30f, -0.30f, 0.30f);
        line(0.30f, -0.30f, 0.30f, 0.30f);
        earc(0.0f, 0.30f, 0.30f, 0.11f, 0.0f, kPi);
        break;
    case Icon::Cone:
        line(0.0f, -0.42f, -0.34f, 0.28f);
        line(0.0f, -0.42f, 0.34f, 0.28f);
        earc(0.0f, 0.28f, 0.34f, 0.12f, 0.0f, kPi);
        break;
    case Icon::Plane:
        poly({{-0.20f, -0.22f}, {0.46f, -0.22f}, {0.20f, 0.26f}, {-0.46f, 0.26f}}, true);
        line(0.04f, -0.22f, -0.13f, 0.26f);
        line(0.25f, -0.22f, 0.07f, 0.26f);
        break;
    case Icon::Light:
        dl->AddCircle(P(0, 0), s * 0.17f, col, 20, t);
        for (int i = 0; i < 8; ++i) {
            const float a = static_cast<float>(i) * kPi / 4.0f;
            line(std::cos(a) * 0.29f, std::sin(a) * 0.29f, std::cos(a) * 0.44f, std::sin(a) * 0.44f);
        }
        break;
    case Icon::Actor:
        dl->AddCircle(P(0, 0), s * 0.19f, col, 20, t);
        line(0.0f, -0.44f, 0.0f, -0.26f);
        line(0.0f, 0.26f, 0.0f, 0.44f);
        line(-0.44f, 0.0f, -0.26f, 0.0f);
        line(0.26f, 0.0f, 0.44f, 0.0f);
        break;
    case Icon::Person:
        dl->AddCircle(P(0.0f, -0.20f), s * 0.16f, col, 18, t);
        poly({{-0.34f, 0.40f}, {-0.30f, 0.12f}, {0.30f, 0.12f}, {0.34f, 0.40f}}, true);
        break;
    case Icon::Search:
        dl->AddCircle(P(-0.08f, -0.08f), s * 0.27f, col, 26, t);
        line(0.12f, 0.12f, 0.40f, 0.40f);
        break;
    case Icon::Add:
        line(-0.34f, 0.0f, 0.34f, 0.0f);
        line(0.0f, -0.34f, 0.0f, 0.34f);
        break;
    case Icon::Trash:
        line(-0.38f, -0.24f, 0.38f, -0.24f);
        poly({{-0.12f, -0.24f}, {-0.12f, -0.38f}, {0.12f, -0.38f}, {0.12f, -0.24f}}, false);
        poly({{-0.28f, -0.24f}, {-0.22f, 0.40f}, {0.22f, 0.40f}, {0.28f, -0.24f}}, false);
        line(-0.06f, -0.08f, -0.06f, 0.26f);
        line(0.06f, -0.08f, 0.06f, 0.26f);
        break;
    case Icon::Reload:
        arc(0.0f, 0.0f, 0.34f, 0.55f, 5.55f);
        head(0.34f * std::cos(5.55f), 0.34f * std::sin(5.55f), 5.55f + kPi / 2 - 0.2f, 0.18f);
        break;
    case Icon::Grid:
        dl->AddRect(P(-0.40f, -0.40f), P(0.40f, 0.40f), col, s * 0.07f, 0, t);
        line(-0.13f, -0.40f, -0.13f, 0.40f);
        line(0.13f, -0.40f, 0.13f, 0.40f);
        line(-0.40f, -0.13f, 0.40f, -0.13f);
        line(-0.40f, 0.13f, 0.40f, 0.13f);
        break;
    case Icon::Material:
        dl->AddCircleFilled(P(0, 0), s * 0.40f, col, 32);
        dl->AddCircleFilled(P(-0.13f, -0.14f), s * 0.11f, IM_COL32(255, 255, 255, 110), 14);
        break;
    case Icon::Graph: // three nodes joined by curved links
        dl->AddRect(P(-0.46f, -0.40f), P(-0.10f, -0.14f), col, s * 0.06f, 0, t);
        dl->AddRect(P(-0.46f, 0.10f), P(-0.10f, 0.36f), col, s * 0.06f, 0, t);
        dl->AddRect(P(0.12f, -0.14f), P(0.46f, 0.12f), col, s * 0.06f, 0, t);
        dl->PathLineTo(P(-0.10f, -0.27f));
        dl->PathBezierCubicCurveTo(P(0.0f, -0.27f), P(0.02f, -0.01f), P(0.12f, -0.01f));
        dl->PathStroke(col, 0, t);
        dl->PathLineTo(P(-0.10f, 0.23f));
        dl->PathBezierCubicCurveTo(P(0.0f, 0.23f), P(0.02f, -0.01f), P(0.12f, -0.01f));
        dl->PathStroke(col, 0, t);
        break;
    case Icon::Shader: // a prism: a beam goes in, light fans out
        poly({{-0.10f, -0.32f}, {-0.10f, 0.32f}, {0.18f, 0.0f}}, true);
        line(-0.46f, 0.0f, -0.10f, 0.0f);
        line(0.18f, 0.0f, 0.46f, -0.24f);
        line(0.18f, 0.0f, 0.46f, 0.0f);
        line(0.18f, 0.0f, 0.46f, 0.24f);
        break;
    case Icon::Script: // the Lua moon: a planet and its small moon
        dl->AddCircle(P(-0.04f, 0.04f), s * 0.30f, col, 28, t);
        dl->AddCircleFilled(P(0.30f, -0.30f), s * 0.10f, col, 14);
        dl->AddCircleFilled(P(-0.04f, 0.04f), s * 0.14f, col, 20);
        break;
    case Icon::Collider: // a box with corner brackets
        dl->AddRect(P(-0.22f, -0.22f), P(0.22f, 0.22f), col, s * 0.04f, 0, t * 0.8f);
        poly({{-0.46f, -0.24f}, {-0.46f, -0.46f}, {-0.24f, -0.46f}}, false);
        poly({{0.24f, -0.46f}, {0.46f, -0.46f}, {0.46f, -0.24f}}, false);
        poly({{-0.46f, 0.24f}, {-0.46f, 0.46f}, {-0.24f, 0.46f}}, false);
        poly({{0.24f, 0.46f}, {0.46f, 0.46f}, {0.46f, 0.24f}}, false);
        break;
    case Icon::Rigidbody: // a ball with a falling arrow
        dl->AddCircle(P(0.0f, -0.16f), s * 0.22f, col, 24, t);
        line(0.0f, 0.18f, 0.0f, 0.44f);
        head(0.0f, 0.44f, kPi / 2, 0.14f);
        break;
    case Icon::Texture:
        dl->AddRect(P(-0.42f, -0.34f), P(0.42f, 0.34f), col, s * 0.08f, 0, t);
        poly({{-0.42f, 0.22f}, {-0.14f, -0.04f}, {0.04f, 0.14f}, {0.16f, 0.02f}, {0.42f, 0.26f}}, false);
        dl->AddCircleFilled(P(0.18f, -0.14f), s * 0.07f, col, 12);
        break;
    case Icon::Code:
        poly({{-0.14f, -0.30f}, {-0.42f, 0.0f}, {-0.14f, 0.30f}}, false);
        poly({{0.14f, -0.30f}, {0.42f, 0.0f}, {0.14f, 0.30f}}, false);
        line(0.06f, -0.34f, -0.06f, 0.34f);
        break;
    case Icon::Scene:
        poly({{-0.44f, 0.30f}, {-0.14f, -0.16f}, {0.04f, 0.10f}, {0.16f, -0.04f}, {0.44f, 0.30f}}, false);
        line(-0.44f, 0.30f, 0.44f, 0.30f);
        dl->AddCircle(P(0.22f, -0.26f), s * 0.09f, col, 14, t);
        break;
    case Icon::Cloud:
        dl->PathArcTo(P(-0.20f, 0.04f), s * 0.18f, kPi * 0.5f, kPi * 1.5f, 12);
        dl->PathArcTo(P(0.00f, -0.12f), s * 0.24f, kPi * 1.05f, kPi * 1.95f, 12);
        dl->PathArcTo(P(0.24f, 0.04f), s * 0.18f, kPi * 1.5f, kPi * 2.5f, 12);
        dl->PathStroke(col, ImDrawFlags_Closed, t);
        break;
    case Icon::Camera:
        dl->AddRect(P(-0.42f, -0.20f), P(0.12f, 0.20f), col, s * 0.08f, 0, t);
        poly({{0.12f, -0.04f}, {0.42f, -0.22f}, {0.42f, 0.22f}, {0.12f, 0.04f}}, true);
        break;
    case Icon::Lock:
        dl->AddRect(P(-0.30f, -0.04f), P(0.30f, 0.40f), col, s * 0.09f, 0, t);
        arc(0.0f, -0.04f, 0.18f, kPi, 2 * kPi);
        break;
    case Icon::Maximize:
        poly({{0.10f, -0.38f}, {0.38f, -0.38f}, {0.38f, -0.10f}}, false);
        poly({{-0.10f, 0.38f}, {-0.38f, 0.38f}, {-0.38f, 0.10f}}, false);
        line(0.38f, -0.38f, 0.06f, -0.06f);
        line(-0.38f, 0.38f, -0.06f, 0.06f);
        break;
    case Icon::ChevronRight: poly({{-0.14f, -0.28f}, {0.16f, 0.0f}, {-0.14f, 0.28f}}, false); break;
    case Icon::ChevronLeft: poly({{0.14f, -0.28f}, {-0.16f, 0.0f}, {0.14f, 0.28f}}, false); break;
    case Icon::ChevronDown: poly({{-0.28f, -0.14f}, {0.0f, 0.16f}, {0.28f, -0.14f}}, false); break;
    case Icon::Up: poly({{-0.28f, 0.10f}, {0.0f, -0.18f}, {0.28f, 0.10f}}, false); break;
    case Icon::Dot:
        dl->AddCircleFilled(P(0, 0), s * 0.22f, col, 16);
        break;
    case Icon::Capsule:
        dl->AddRect(P(-0.22f, -0.44f), P(0.22f, 0.44f), col, s * 0.22f, 0, t);
        earc(0.0f, -0.20f, 0.22f, 0.07f, 0.0f, kPi);
        earc(0.0f, 0.20f, 0.22f, 0.07f, 0.0f, kPi);
        break;
    case Icon::Hemisphere:
        arc(0.0f, 0.12f, 0.40f, kPi, 2 * kPi);
        line(-0.40f, 0.12f, 0.40f, 0.12f);
        earc(0.0f, 0.12f, 0.40f, 0.12f, 0.0f, kPi);
        break;
    case Icon::Torus:
        dl->PathEllipticalArcTo(P(0, 0), ImVec2(s * 0.44f, s * 0.27f), 0.0f, 0.0f, 2 * kPi, 32);
        dl->PathStroke(col, 0, t);
        dl->PathEllipticalArcTo(P(0, 0), ImVec2(s * 0.19f, s * 0.09f), 0.0f, 0.0f, 2 * kPi, 24);
        dl->PathStroke(col, 0, t);
        break;
    case Icon::Arch:
        arc(0.0f, 0.06f, 0.38f, kPi, 2 * kPi);
        arc(0.0f, 0.06f, 0.18f, kPi, 2 * kPi);
        line(-0.38f, 0.06f, -0.38f, 0.40f);
        line(0.38f, 0.06f, 0.38f, 0.40f);
        line(-0.18f, 0.06f, -0.18f, 0.40f);
        line(0.18f, 0.06f, 0.18f, 0.40f);
        line(-0.38f, 0.40f, -0.18f, 0.40f);
        line(0.18f, 0.40f, 0.38f, 0.40f);
        break;
    case Icon::Pyramid:
        poly({{0.0f, -0.44f}, {-0.40f, 0.26f}, {0.10f, 0.40f}, {0.44f, 0.20f}}, true);
        line(0.0f, -0.44f, 0.10f, 0.40f);
        break;
    case Icon::Wedge:
        poly({{-0.42f, 0.30f}, {0.10f, 0.30f}, {0.10f, -0.20f}}, true);
        poly({{0.10f, -0.20f}, {0.40f, -0.36f}, {0.40f, 0.14f}, {0.10f, 0.30f}}, false);
        line(0.10f, -0.20f, 0.40f, -0.36f);
        break;
    case Icon::Prism:
        poly({{-0.42f, 0.32f}, {0.02f, 0.32f}, {-0.20f, -0.20f}}, true);
        poly({{-0.20f, -0.20f}, {0.20f, -0.40f}, {0.42f, 0.12f}, {0.02f, 0.32f}}, false);
        break;
    case Icon::Diamond:
        poly({{0.0f, -0.44f}, {0.36f, 0.0f}, {0.0f, 0.44f}, {-0.36f, 0.0f}}, true);
        line(-0.36f, 0.0f, 0.36f, 0.0f);
        line(0.0f, -0.44f, 0.0f, 0.0f);
        break;
    case Icon::Stairs:
        poly({{-0.42f, 0.38f}, {-0.42f, 0.16f}, {-0.14f, 0.16f}, {-0.14f, -0.08f}, {0.14f, -0.08f}, {0.14f, -0.32f}, {0.42f, -0.32f},
              {0.42f, 0.38f}}, true);
        break;
    case Icon::Disc:
        dl->PathEllipticalArcTo(P(0, -0.04f), ImVec2(s * 0.44f, s * 0.22f), 0.0f, 0.0f, 2 * kPi, 32);
        dl->PathStroke(col, 0, t);
        line(-0.44f, -0.04f, -0.44f, 0.08f);
        line(0.44f, -0.04f, 0.44f, 0.08f);
        earc(0.0f, 0.08f, 0.44f, 0.22f, 0.0f, kPi);
        break;
    case Icon::Tube:
        dl->PathEllipticalArcTo(P(0, -0.30f), ImVec2(s * 0.32f, s * 0.12f), 0.0f, 0.0f, 2 * kPi, 28);
        dl->PathStroke(col, 0, t);
        dl->PathEllipticalArcTo(P(0, -0.30f), ImVec2(s * 0.19f, s * 0.06f), 0.0f, 0.0f, 2 * kPi, 20);
        dl->PathStroke(col, 0, t * 0.8f);
        line(-0.32f, -0.30f, -0.32f, 0.30f);
        line(0.32f, -0.30f, 0.32f, 0.30f);
        earc(0.0f, 0.30f, 0.32f, 0.12f, 0.0f, kPi);
        break;
    case Icon::Ring:
        dl->PathEllipticalArcTo(P(0, 0), ImVec2(s * 0.44f, s * 0.24f), 0.0f, 0.0f, 2 * kPi, 32);
        dl->PathStroke(col, 0, t);
        dl->PathEllipticalArcTo(P(0, 0), ImVec2(s * 0.26f, s * 0.12f), 0.0f, 0.0f, 2 * kPi, 24);
        dl->PathStroke(col, 0, t);
        break;
    case Icon::Count: break;
    }
}

Icon PrimitiveIcon(Primitive primitive) {
    switch (primitive) {
    case Primitive::Sphere: return Icon::Sphere;
    case Primitive::Plane: return Icon::Plane;
    case Primitive::Cylinder: return Icon::Cylinder;
    case Primitive::Cone: return Icon::Cone;
    case Primitive::Capsule: return Icon::Capsule;
    case Primitive::Hemisphere: return Icon::Hemisphere;
    case Primitive::Torus: return Icon::Torus;
    case Primitive::Arch: return Icon::Arch;
    case Primitive::Pyramid: return Icon::Pyramid;
    case Primitive::Wedge: return Icon::Wedge;
    case Primitive::Prism: return Icon::Prism;
    case Primitive::Diamond: return Icon::Diamond;
    case Primitive::Stairs: return Icon::Stairs;
    case Primitive::Disc: return Icon::Disc;
    case Primitive::Tube: return Icon::Tube;
    case Primitive::Ring: return Icon::Ring;
    case Primitive::Quad: return Icon::Texture;
    default: return Icon::Cube;
    }
}

bool IconButton(const char* id, Icon icon, const char* tooltip, bool active, float size, ImU32 tint) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 end(pos.x + size, pos.y + size);
    if (active) dl->AddRectFilled(pos, end, ui::U32(held ? ui::kAccentHover : ui::kAccent), 5.0f);
    else if (held) dl->AddRectFilled(pos, end, ui::U32(ui::kActive), 5.0f);
    else if (hovered) dl->AddRectFilled(pos, end, ui::U32(ui::kHover), 5.0f);

    const ImU32 color = tint ? tint : (active ? ui::U32(ui::kOnAccent) : ui::U32(hovered ? ui::kText : ui::kTextSoft));
    DrawIcon(dl, icon, ImVec2(pos.x + size * 0.5f, pos.y + size * 0.5f), size * 0.56f, color);
    if (hovered && tooltip && *tooltip) ImGui::SetTooltip("%s", tooltip);
    return clicked;
}

void IconText(Icon icon, const char* text, ImU32 iconColor) {
    const float h = ImGui::GetTextLineHeight();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    DrawIcon(ImGui::GetWindowDrawList(), icon, ImVec2(pos.x + h * 0.55f, pos.y + h * 0.5f), h * 0.95f,
             iconColor ? iconColor : ui::U32(ui::kTextDim));
    ImGui::Dummy(ImVec2(h * 1.1f, h));
    ImGui::SameLine(0.0f, 5.0f);
    ImGui::TextUnformatted(text);
}

void ShowIconSheet(bool* open) {
    if (!*open) return;
    ImGui::SetNextWindowSize(ImVec2(760, 420), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Icon sheet", open)) {
        const float cell = 92.0f;
        const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cell));
        for (int i = 0; i < static_cast<int>(Icon::Count); ++i) {
            if (i % columns) ImGui::SameLine();
            ImGui::BeginGroup();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(cell - 8, 56));
            DrawIcon(ImGui::GetWindowDrawList(), static_cast<Icon>(i), ImVec2(p.x + (cell - 8) * 0.5f, p.y + 28), 48.0f, ui::U32(ui::kText));
            ImGui::TextDisabled("%s", kNames[i]);
            ImGui::EndGroup();
        }
    }
    ImGui::End();
}

} // namespace magnum::editor
