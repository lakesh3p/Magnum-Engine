#pragma once

#include "EditorContext.h"
#include "render/Framebuffer.h"
#include "render/SceneRenderer.h"
#include "scene/Components.h"

#include <imgui.h>

namespace magnum::editor {

// The 3D view: renders the scene into a framebuffer and handles the camera, picking and gizmos.
//   RMB + WASD/QE  fly (Shift = faster, wheel = speed)     LMB  select     F  focus selection
//   Q / W / E / R  select / move / rotate / scale                  Ctrl  toggles snapping while dragging
class ViewportPanel {
public:
    void Shutdown();
    void Draw(EditorContext& ctx);

    Framebuffer& Fb() { return m_fb; }
    bool IsFlying() const { return m_flying; }
    void FocusSelection(EditorContext& ctx);
    void ToggleStats() { m_showStats = !m_showStats; }
    void SetView2D(bool on) { m_view2D = on; }
    bool View2D() const { return m_view2D; }
    void SetCenter2D(glm::vec2 center, float halfHeight) { m_center2D = center; m_half2D = halfHeight; }
    glm::vec2 Center2D() const { return m_center2D; }
    float Half2D() const { return m_half2D; }

private:
    void DrawOverlays(EditorContext& ctx, ImVec2 origin, ImVec2 size);
    void DrawHud(EditorContext& ctx, ImVec2 origin, ImVec2 size);
    void DrawGrid2D(ImVec2 origin, ImVec2 size, float aspect);
    glm::vec2 ScreenToWorld2D(ImVec2 mouse, ImVec2 origin, ImVec2 size, float aspect) const;
    void UpdateFlyCamera(EditorContext& ctx);
    void DrawGizmo(EditorContext& ctx, const glm::mat4& view, const glm::mat4& projection, bool orthographic);
    void HandleInput(EditorContext& ctx, int width, int height, ImVec2 imageOrigin);

    Framebuffer m_fb;
    bool m_flying = false;

    // View options (the Lit and Show menus over the viewport).
    bool m_wireframe = false;
    bool m_showGrid = true;
    bool m_showMarkers = true;
    bool m_postProcess = true;
    bool m_showStats = false;
    // The 2D view of the editor: orthographic, looking down -Z at the XY plane (pan with the right mouse button, wheel zooms).
    bool m_view2D = false;
    glm::vec2 m_center2D{0.0f};
    float m_half2D = 6.0f; // half the height of the view in world units
    bool m_panning2D = false;
    bool m_overlayHot = false; // the mouse is over one of the overlay buttons: clicks are not for the scene

    // A gizmo drag in progress: the transform before it started, recorded as one undo step when released.
    bool m_gizmoActive = false;
    uint64_t m_gizmoActor = 0;
    TransformComponent m_gizmoBefore;
};

} // namespace magnum::editor
