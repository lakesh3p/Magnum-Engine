#pragma once

#include "ui/UiDraw.h"
#include "ui/UiFonts.h"
#include "ui/UiTypes.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace magnum {

// The retained UI of a play session or of the editor preview: canvases, each a tree of nodes. `Update` lays the
// canvases out, runs the pointer interaction (hover, press, click, drag, scroll), tweens and eases the visuals;
// `BuildDrawList` turns the result into triangles. Nothing here touches the graphics API or Lua; scripts reach it
// through the bindings in UiLua.h and read what happened as events.
class UiSystem {
public:
    UiSystem();
    ~UiSystem();
    UiSystem(const UiSystem&) = delete;
    UiSystem& operator=(const UiSystem&) = delete;

    // ---- nodes --------------------------------------------------------------------------------------------------------

    // A new node that belongs to no canvas yet; attach it to one with Attach.
    UiId Create(UiKind kind, const std::string& name = "");
    UiNode* Get(UiId id);
    const UiNode* Get(UiId id) const;
    bool Exists(UiId id) const { return m_nodes.contains(id); }
    // Detaches the node and deletes it with everything below it.
    void Destroy(UiId id);
    // Makes `child` the last child of `parent` (or puts it at `index`). False when that would make a node its own ancestor.
    bool Attach(UiId parent, UiId child, int index = -1);
    void Detach(UiId child);
    // First node called `name`, searching below `under` (0 = every canvas, top canvas first).
    UiId Find(const std::string& name, UiId under = 0) const;
    std::vector<UiId> FindAll(const std::string& prefix) const;
    size_t NodeCount() const { return m_nodes.size(); }
    // Sets anchors, pivot and offsets from a preset name: topleft, top, topright, left, center, right, bottomleft, bottom,
    // bottomright, fill, top_wide, bottom_wide, center_wide, left_tall, right_tall, center_tall. False for an unknown name.
    static bool ApplyAnchorPreset(UiNode& node, const std::string& preset);

    // ---- canvases -----------------------------------------------------------------------------------------------------

    UiCanvas* AddCanvas(const std::string& name, int order);
    UiCanvas* FindCanvas(const std::string& name);
    const UiCanvas* FindCanvas(const std::string& name) const;
    void RemoveCanvas(const std::string& name);
    void ClearCanvases();
    std::vector<UiCanvas>& Canvases() { return m_canvases; }
    const std::vector<UiCanvas>& Canvases() const { return m_canvases; }
    UiCanvas* CanvasOf(UiId node);
    // New nodes made by scripts go to this canvas.
    void SetActiveCanvas(const std::string& name) { m_active = name; }
    const std::string& ActiveCanvasName() const { return m_active; }

    // ---- frame --------------------------------------------------------------------------------------------------------

    // Size of the view the UI is drawn into, in pixels.
    void SetViewSize(int width, int height);
    glm::ivec2 ViewSize() const { return {m_viewWidth, m_viewHeight}; }
    void Update(float dt, const UiPointer& pointer);
    // Lays every canvas out (Update does this; exposed for tests and for a preview without a pointer).
    void Layout();
    // True while the pointer is over a node that stops it (buttons, sliders, panels with `block`): the game should not
    // treat clicks as its own then.
    bool PointerOverUi() const { return m_hit != 0; }
    UiId HoveredNode() const { return m_hit; }
    UiId PressedNode() const { return m_captured; }
    std::vector<UiEvent> TakeEvents();

    // ---- changes with consequences --------------------------------------------------------------------------------------

    // Sets a value (slider, progress, toggle), clamps and snaps it, and reports a Change event when `notify` is set and it moved.
    void SetValue(UiId id, float value, bool notify);
    void SetScroll(UiId id, glm::vec2 scroll);
    // Reports a click on a node as if the pointer had pressed and released it there (scripts, tests, shortcuts).
    void RaiseClick(UiId id);
    // Starts a tween; returns false if the node does not exist.
    bool Animate(const UiTween& tween);
    void StopTweens(UiId node);
    bool HasTweens(UiId node) const;

    // ---- drawing ------------------------------------------------------------------------------------------------------

    void BuildDrawList(UiDrawList& out);
    struct DrawContext; // (private to UiDraw.cpp)

    // ---- queries ------------------------------------------------------------------------------------------------------

    // The canvases and their node trees as indented text (the console command `ui tree`).
    std::string Dump() const;

    // Rectangle of a node in view pixels (after the canvas scale; ignores the node's own rotation).
    UiRect PixelRect(UiId id) const;
    // Size of an image in pixels (for sizing nodes to their picture); defaults to the sprite helper.
    std::function<bool(const std::string&, glm::vec2&)> imageSize;
    // Text size (UI units) of what a node would show; used for auto sizes.
    glm::vec2 MeasureText(UiNode& node, float maxWidth, float scale);

private:
    struct CanvasPass;
    // layout (UiLayout.cpp)
    void LayoutCanvas(UiCanvas& canvas);
    void LayoutNode(UiNode& node, const UiRect& rect, const UiAffine& parentWorld, float parentOpacity, bool parentEnabled);
    void LayoutChildren(UiNode& node);
    glm::vec2 Preferred(UiNode& node, glm::vec2 available);
    UiRect AnchoredRect(UiNode& node, const UiRect& parent);
    const UiTextLayout& TextLayoutFor(UiNode& node, float maxWidthUnits, float scale);
    glm::vec2 ContentSize(UiNode& node, glm::vec2 available);
    // interaction (UiInput.cpp)
    void Interact(const UiPointer& pointer, float dt);
    bool HitTest(UiNode& node, glm::vec2 point, UiId& out);
    void RaiseEvent(UiEventType type, UiId node, float value = 0.0f, uint32_t token = 0);
    void UpdateTweens(float dt);
    void UpdateVisuals(float dt);
    void SliderFromPointer(UiNode& node, glm::vec2 pointInDesign);
    UiRect ScrollThumb(const UiNode& node) const;
    UiCanvas* CanvasForRoot(UiId root);
    // drawing (UiDraw.cpp)
    void DrawNode(DrawContext& dc, UiNode& node);

    std::unordered_map<UiId, std::unique_ptr<UiNode>> m_nodes;
    UiId m_nextId = 1;
    std::vector<UiCanvas> m_canvases; // sorted by order, lowest first
    std::string m_active = "main";
    std::vector<UiEvent> m_events;
    std::vector<UiTween> m_tweens;
    int m_viewWidth = 1280, m_viewHeight = 720;
    float m_layoutScale = 1.0f; // pixels per UI unit of the canvas being laid out

    UiId m_hit = 0;       // node under the pointer that blocks it
    UiId m_captured = 0;  // node that got the press and keeps the pointer until release
    bool m_wasDown = false;
    UiId m_scrollDrag = 0;
    float m_scrollGrab = 0.0f;
    glm::vec2 m_pointerDesign{0.0f}; // pointer in the design units of the canvas that owns it
    uint32_t m_nextToken = 1;
};

} // namespace magnum
