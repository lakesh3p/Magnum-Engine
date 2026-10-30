#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace magnum {

// The retained UI: a tree of nodes per canvas. Everything here is plain data (no GL, no Lua), so the layout, the
// interaction and the drawing can be tested without a window. Positions and sizes are in "UI units": a canvas is
// laid out in a design space (for example 1280 x 720) that is scaled to the real view size when it is drawn.

using UiId = uint32_t; // 0 = no node

enum class UiKind { Panel, Image, Text, Button, Toggle, Slider, Progress, Scroll };

enum class UiLayout { None, Row, Column, Grid };       // how a container places its children
enum class UiJustify { Start, Center, End, SpaceBetween, SpaceAround }; // along the layout direction
enum class UiItems { Start, Center, End, Stretch };    // across the layout direction
enum class UiTextAlign { Left, Center, Right };
enum class UiFit { Stretch, Contain, Cover };          // an image inside a larger rectangle
enum class UiToggleLook { Check, Switch };
enum class UiScaleMode { Constant, Height, Width, Fit, Fill };

enum class UiEase {
    Linear, InQuad, OutQuad, InOutQuad, InCubic, OutCubic, InOutCubic, InQuart, OutQuart, InOutQuart, InSine, OutSine,
    InOutSine, InExpo, OutExpo, InOutExpo, InBack, OutBack, InOutBack, OutElastic, OutBounce, Count
};
const char* UiEaseName(UiEase ease);
bool UiEaseFromName(const std::string& name, UiEase& out); // "out_cubic", "in_out_sine", ...
float UiApplyEase(UiEase ease, float t);                   // t in 0..1

struct UiRect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    float Right() const { return x + w; }
    float Bottom() const { return y + h; }
    glm::vec2 Center() const { return {x + w * 0.5f, y + h * 0.5f}; }
    bool Contains(glm::vec2 p) const { return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h; }
};
UiRect Intersect(const UiRect& a, const UiRect& b); // empty (w = h = 0) when they do not overlap

// A 2D affine transform (rotation, scale, translation), used for the visual transform of a node and its subtree.
struct UiAffine {
    glm::vec2 x{1.0f, 0.0f}; // image of the x axis
    glm::vec2 y{0.0f, 1.0f}; // image of the y axis
    glm::vec2 t{0.0f};       // translation
    glm::vec2 Apply(glm::vec2 p) const { return x * p.x + y * p.y + t; }
    UiAffine Then(const UiAffine& outer) const; // this first, then `outer`
    UiAffine Inverse() const;
    bool IsIdentity() const { return x == glm::vec2(1, 0) && y == glm::vec2(0, 1) && t == glm::vec2(0); }
    bool IsAxisAligned() const { return x.y == 0.0f && y.x == 0.0f && x.x > 0.0f && y.y > 0.0f; }
};

// ---- looks ---------------------------------------------------------------------------------------------------------

// What a node looks like in one state. Only the listed fields are set; a hover look lists just what changes on hover.
struct UiLook {
    std::optional<glm::vec4> bg;     // background (the track of sliders and progress bars)
    std::optional<glm::vec4> border;
    std::optional<glm::vec4> text;   // label color (the color of Text nodes)
    std::optional<glm::vec4> tint;   // multiplies the node's image
    std::optional<glm::vec4> fill;   // progress bar / slider fill, toggle "on" color
    std::optional<glm::vec4> handle; // slider knob, toggle knob / check mark
    std::optional<float> borderWidth;
    std::optional<float> radius;
    std::optional<std::string> image; // replaces the node's image in this state (sprite buttons)
    // Applies the set fields of `over` on top of this look.
    void Merge(const UiLook& over);
};

struct UiShadow {
    bool enabled = false;
    glm::vec2 offset{0.0f, 2.0f};
    float blur = 8.0f;
    float spread = 0.0f;
    glm::vec4 color{0.0f, 0.0f, 0.0f, 0.35f};
};

struct UiStyle {
    UiLook base;
    UiLook hover, pressed, disabled, active; // active: a toggle that is on
    UiShadow shadow;
    float transition = 0.1f; // seconds a change of state takes to show
};

// The values a node is drawn with right now (the style resolved for its state, eased over `transition`).
struct UiVisual {
    glm::vec4 bg{0.0f}, border{0.0f}, text{1.0f}, tint{1.0f}, fill{1.0f}, handle{1.0f};
    float borderWidth = 0.0f;
    float radius = 0.0f;
    std::string image;      // the image shown now (a state may swap it)
    bool initialized = false;
};

// The style a kind starts with.
UiStyle DefaultStyle(UiKind kind);

// ---- events --------------------------------------------------------------------------------------------------------

enum class UiEventType { Click, Press, Release, Enter, Leave, Change, TweenDone };

struct UiEvent {
    UiEventType type = UiEventType::Click;
    UiId node = 0;
    float value = 0.0f;  // Change: the new value; toggles send 1 or 0
    uint32_t token = 0;  // TweenDone: the token given when the tween was started
};

// ---- tweens --------------------------------------------------------------------------------------------------------

enum class UiProp { Opacity, Scale, Rotation, Pos, Size, Bg, TextColor, Value, Scroll };

struct UiTween {
    UiId node = 0;
    UiProp prop = UiProp::Opacity;
    glm::vec4 from{0.0f}, to{0.0f};
    float duration = 0.3f;
    float delay = 0.0f;
    float elapsed = 0.0f;
    UiEase ease = UiEase::OutCubic;
    bool loop = false;
    bool yoyo = false;
    bool started = false;       // `from` is captured when the delay is over
    bool hasFrom = false;       // `from` was given by the caller
    bool removeNode = false;    // destroy the node when the tween ends (floating numbers, toasts)
    bool hideNode = false;      // hide it instead
    uint32_t token = 0;         // reported in the TweenDone event; 0 = no event
};

// ---- the node ------------------------------------------------------------------------------------------------------

struct UiTextCache; // the laid-out glyphs of a node's text (see UiFonts.h)

struct UiNode {
    UiId id = 0;
    UiKind kind = UiKind::Panel;
    std::string name;
    UiId parent = 0;
    std::vector<UiId> children;

    bool visible = true;
    bool enabled = true;
    bool clip = false;          // children are cut at this node's rectangle (always on for Scroll)
    int block = -1;             // does the pointer stop here? -1 = by kind (buttons, toggles, sliders, scrolls do), 0 no, 1 yes
    bool ignoreLayout = false;  // placed by its anchors even inside a Row / Column / Grid

    // Placement. Anchors are fractions of the parent's rectangle. With the same anchor on an axis the node has a fixed
    // size there (pos moves its pivot away from the anchor point); with different anchors it stretches, leaving margins.
    glm::vec2 anchorMin{0.0f}, anchorMax{0.0f};
    glm::vec2 pivot{0.0f};
    glm::vec2 pos{0.0f};
    glm::vec2 size{-1.0f};      // a negative component sizes the node to its content
    glm::vec2 marginMin{0.0f}, marginMax{0.0f}; // left/top and right/bottom, used on stretched axes
    glm::vec2 minSize{0.0f}, maxSize{100000.0f};
    float grow = 0.0f;          // share of the free space in a Row / Column
    std::optional<UiItems> alignSelf;

    // Layout of the children.
    UiLayout layout = UiLayout::None;
    glm::vec4 padding{0.0f};    // left, top, right, bottom
    glm::vec2 spacing{0.0f};
    UiJustify justify = UiJustify::Start;
    UiItems items = UiItems::Start;
    int columns = 2;            // Grid
    glm::vec2 cellSize{0.0f};   // Grid: fixed cell size, 0 = equal columns, rows as tall as their tallest child

    // Visual transform of the node and its subtree (layout and hit testing follow it).
    float opacity = 1.0f;
    float scale = 1.0f;
    float rotation = 0.0f;      // degrees, clockwise on screen

    // Content.
    std::string text;
    std::string font;           // "" = Sans; Sans, SansBold, Mono or a project path to a .ttf
    float fontSize = 16.0f;
    UiTextAlign textAlign = UiTextAlign::Left;
    bool wrap = false;
    float lineHeight = 1.0f;    // multiplier
    UiShadow textShadow;
    std::string image;
    glm::vec4 uv{0.0f, 0.0f, 1.0f, 1.0f}; // x, y, w, h of the used part of the image (0..1, from the top left)
    glm::vec4 slice{0.0f};      // 9-slice borders in image pixels: left, top, right, bottom
    UiFit fit = UiFit::Stretch;
    bool pixelated = false;
    float value = 0.0f;         // Slider, Progress: the value; Toggle: 1 = on
    float minValue = 0.0f, maxValue = 1.0f;
    float step = 0.0f;
    bool vertical = false;      // Slider, Progress
    UiToggleLook look = UiToggleLook::Check;
    glm::vec2 scroll{0.0f};     // Scroll: how far the content is moved

    UiStyle style;
    UiVisual visual;

    // Runtime state.
    UiRect rect;                // in canvas (design) units, before the node's own visual transform
    UiAffine world;             // visual transform of this node including its ancestors
    float worldOpacity = 1.0f;
    glm::vec2 contentSize{0.0f}; // Scroll: size of what the children occupy
    bool hovered = false;
    bool pressed = false;
    bool worldEnabled = true;   // enabled, and so are all its ancestors
    std::shared_ptr<UiTextCache> textCache;

    bool IsInteractive() const { return kind == UiKind::Button || kind == UiKind::Toggle || kind == UiKind::Slider; }
    bool BlocksPointer() const;
};

// ---- canvases ------------------------------------------------------------------------------------------------------

struct UiCanvas {
    std::string name;
    int order = 0;              // higher is drawn on top and gets the pointer first
    bool visible = true;
    UiScaleMode scaleMode = UiScaleMode::Height;
    glm::vec2 reference{1280.0f, 720.0f}; // the design size the scale mode measures against
    float scale = 1.0f;         // computed: pixels per UI unit
    glm::vec2 size{1280.0f, 720.0f}; // computed: the canvas in UI units
    UiId root = 0;
    std::string document;       // file it was loaded from ("" = built by scripts)
    int64_t stamp = 0;          // modification time of the document, for hot reload
    bool fromScene = false;     // created for a Canvas component
    uint64_t actor = 0;
};

// What the pointer is doing, in view pixels (origin at the top left).
struct UiPointer {
    glm::vec2 position{0.0f};
    bool inView = false;
    bool down[3] = {false, false, false};
    float wheel = 0.0f; // notches since the last update, positive = up
};

} // namespace magnum
