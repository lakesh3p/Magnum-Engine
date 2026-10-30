#include "panels/ViewportPanel.h"

#include "Icons.h"
#include "Picking.h"
#include "Theme.h"
#include "commands/EditorCommands.h"
#include "scene/TransformMath.h"

#include <GLFW/glfw3.h>
#include <ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace magnum::editor {

namespace {

} // namespace

void ViewportPanel::Shutdown() {
    m_fb.Release();
}

namespace {

// A small translucent button floating over the 3D view. `chevron` adds a drop-down arrow.
bool OverlayButton(const char* id, const char* label, bool chevron, bool mono = false, bool* hot = nullptr) {
    ImGui::PushID(id);
    if (mono) ImGui::PushFont(GetFont(UiFont::Mono));
    const ImVec2 text = ImGui::CalcTextSize(label);
    if (mono) ImGui::PopFont();
    const float height = 26.0f;
    const float width = text.x + 20.0f + (chevron ? 16.0f : 0.0f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##b", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    if (hot && hovered) *hot = true;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), ui::U32(hovered ? ui::kHover : ui::kPanel, 0.92f), 5.0f);
    dl->AddRect(pos, ImVec2(pos.x + width, pos.y + height), ui::U32(ui::kBorder), 5.0f);
    if (mono) ImGui::PushFont(GetFont(UiFont::Mono));
    dl->AddText(ImVec2(pos.x + 10.0f, pos.y + (height - text.y) * 0.5f), ui::U32(ui::kTextSoft), label);
    if (mono) ImGui::PopFont();
    if (chevron) DrawIcon(dl, Icon::ChevronDown, ImVec2(pos.x + width - 14.0f, pos.y + height * 0.5f), 10.0f, ui::U32(ui::kTextDim));
    ImGui::PopID();
    return clicked;
}

const char* ToolName(GizmoMode mode) {
    switch (mode) {
    case GizmoMode::Select: return "Select";
    case GizmoMode::Translate: return "Move";
    case GizmoMode::Rotate: return "Rotate";
    default: return "Scale";
    }
}

} // namespace

// Buttons and a readout drawn over the rendered image (the image is the whole panel).
void ViewportPanel::DrawOverlays(EditorContext& ctx, ImVec2 origin, ImVec2 size) {
    m_overlayHot = false;

    ImGui::SetCursorScreenPos(ImVec2(origin.x + 10.0f, origin.y + 10.0f));
    if (OverlayButton("lit", m_wireframe ? "Wireframe" : "Lit", true, false, &m_overlayHot)) ImGui::OpenPopup("##litmenu");
    if (ImGui::BeginPopup("##litmenu")) {
        if (ImGui::MenuItem("Lit", nullptr, !m_wireframe)) m_wireframe = false;
        if (ImGui::MenuItem("Wireframe", nullptr, m_wireframe)) m_wireframe = true;
        ImGui::EndPopup();
    }
    ImGui::SameLine(0, 6);
    if (OverlayButton("dim", m_view2D ? "2D" : "3D", false, false, &m_overlayHot)) {
        m_view2D = !m_view2D;
        if (m_view2D) { // start the 2D view where the editor camera is looking
            m_center2D = glm::vec2(ctx.camera.position.x, ctx.camera.position.y);
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Switch between the 3D view and the flat 2D view (right drag pans, wheel zooms)");
    ImGui::SameLine(0, 6);
    if (OverlayButton("show", "Show", true, false, &m_overlayHot)) ImGui::OpenPopup("##showmenu");
    if (ImGui::BeginPopup("##showmenu")) {
        ImGui::Checkbox("Grid", &m_showGrid);
        ImGui::Checkbox("Actor markers", &m_showMarkers);
        ImGui::Checkbox("Post processing", &m_postProcess);
        ImGui::Checkbox("UI", &ctx.showUi);
        ImGui::Separator();
        ImGui::Checkbox("Statistics", &m_showStats);
        ImGui::EndPopup();
    }

    char speed[32];
    std::snprintf(speed, sizeof(speed), "Speed %.1f", ctx.camera.moveSpeed);
    ImGui::PushFont(GetFont(UiFont::Mono));
    const float speedWidth = ImGui::CalcTextSize(speed).x + 20.0f;
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(origin.x + size.x - speedWidth - 10.0f, origin.y + 10.0f));
    OverlayButton("speed", speed, false, true, &m_overlayHot);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Camera speed (hold the right mouse button and use the wheel)");

    if (m_showStats) {
        // Frame time and the GPU time of each pass of the render pipeline.
        const ImGuiIO& io = ImGui::GetIO();
        char head[64];
        std::snprintf(head, sizeof(head), "%.1f ms   %.0f FPS", 1000.0f / std::max(io.Framerate, 1.0f), io.Framerate);
        std::vector<std::pair<std::string, std::string>> rows;
        float gpuTotal = 0.0f;
        for (const SceneRenderer::PassTiming& t : ctx.renderer.Timings()) {
            char ms[24];
            std::snprintf(ms, sizeof(ms), "%.2f ms", t.ms);
            rows.emplace_back(t.name, ms);
            gpuTotal += t.ms;
        }
        char total[24];
        std::snprintf(total, sizeof(total), "%.2f ms", gpuTotal);
        rows.emplace_back("GPU passes", total);

        ImGui::PushFont(GetFont(UiFont::Mono));
        const float rowHeight = ImGui::GetTextLineHeight() + 3.0f;
        const float boxWidth = 230.0f;
        const float boxHeight = 12.0f + rowHeight * static_cast<float>(rows.size() + 1);
        const ImVec2 bp(origin.x + size.x - boxWidth - 10.0f, origin.y + 44.0f);
        ImDrawList* sdl = ImGui::GetWindowDrawList();
        sdl->AddRectFilled(bp, ImVec2(bp.x + boxWidth, bp.y + boxHeight), ui::U32(ui::kPanel, 0.92f), 5.0f);
        sdl->AddRect(bp, ImVec2(bp.x + boxWidth, bp.y + boxHeight), ui::U32(ui::kBorder), 5.0f);
        float y = bp.y + 6.0f;
        sdl->AddText(ImVec2(bp.x + 10.0f, y), ui::U32(ui::kText), head);
        y += rowHeight;
        for (const auto& [name, value] : rows) {
            sdl->AddText(ImVec2(bp.x + 10.0f, y), ui::U32(ui::kTextDim), name.c_str());
            sdl->AddText(ImVec2(bp.x + boxWidth - 10.0f - ImGui::CalcTextSize(value.c_str()).x, y), ui::U32(ui::kTextSoft), value.c_str());
            y += rowHeight;
        }
        ImGui::PopFont();
    }

    // Bottom-left readout: selection, tool and space.
    const NameComponent* name = ctx.scene.TryGet<NameComponent>(ctx.selected);
    std::string readout = name ? name->name : std::string("No selection");
    readout += std::string(" \xC2\xB7 ") + ToolName(ctx.gizmoMode) + " \xC2\xB7 " + (ctx.gizmoLocal ? "Local" : "World");
    ImGui::PushFont(GetFont(UiFont::Mono));
    const ImVec2 rs = ImGui::CalcTextSize(readout.c_str());
    const ImVec2 rp(origin.x + 10.0f, origin.y + size.y - 10.0f - 26.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(rp, ImVec2(rp.x + rs.x + 20.0f, rp.y + 26.0f), ui::U32(ui::kPanel, 0.92f), 5.0f);
    dl->AddRect(rp, ImVec2(rp.x + rs.x + 20.0f, rp.y + 26.0f), ui::U32(ui::kBorder), 5.0f);
    dl->AddText(ImVec2(rp.x + 10.0f, rp.y + (26.0f - rs.y) * 0.5f), ui::U32(ui::kTextSoft), readout.c_str());
    ImGui::PopFont();

    if (ctx.playing) {
        const char* label = ctx.paused ? "PAUSED" : "PLAYING";
        ImGui::PushFont(GetFont(UiFont::Semibold));
        const ImVec2 ls = ImGui::CalcTextSize(label);
        dl->AddText(ImVec2(origin.x + (size.x - ls.x) * 0.5f, origin.y + 14.0f), ui::U32(ui::kOrange), label);
        ImGui::PopFont();
    }
}

void ViewportPanel::Draw(EditorContext& ctx) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool open = ImGui::Begin("Viewport");
    ImGui::PopStyleVar();

    if (open) {
        const ImVec2 size = ImGui::GetContentRegionAvail();
        const int w = static_cast<int>(size.x);
        const int h = static_cast<int>(size.y);
        if (w > 0 && h > 0) {
            // While playing with a camera in the scene the player's view replaces the editor camera.
            const bool gameView = ctx.playing && ctx.gameCamera.valid;
            const bool view2D = m_view2D && !gameView;
            ctx.view2D = view2D;
            ctx.view2DCenter = m_center2D;
            if (!gameView && !view2D) UpdateFlyCamera(ctx);

            m_fb.Resize(w, h);
            m_fb.Bind();
            RenderView view;
            const float aspect = static_cast<float>(w) / static_cast<float>(h);
            if (gameView) {
                const CameraView& cam = ctx.gameCamera;
                TransformComponent pose;
                pose.position = cam.position;
                pose.rotation = cam.rotation;
                view.view = glm::inverse(glm::translate(glm::mat4(1.0f), cam.position) * pose.RotationMatrix());
                view.projection = cam.orthographic
                                      ? glm::ortho(-cam.orthoSize * aspect, cam.orthoSize * aspect, -cam.orthoSize, cam.orthoSize, cam.nearPlane, cam.farPlane)
                                      : glm::perspective(glm::radians(cam.fov), aspect, cam.nearPlane, cam.farPlane);
                view.cameraPosition = cam.position;
            } else if (view2D) {
                const glm::vec3 eye(m_center2D.x, m_center2D.y, 50.0f);
                view.view = glm::translate(glm::mat4(1.0f), -eye);
                view.projection = glm::ortho(-m_half2D * aspect, m_half2D * aspect, -m_half2D, m_half2D, 0.1f, 200.0f);
                view.cameraPosition = eye;
            } else {
                view.view = ctx.camera.View();
                view.projection = ctx.camera.Projection(aspect);
                view.cameraPosition = ctx.camera.position;
            }
            view.width = w;
            view.height = h;
            view.time = static_cast<float>(ctx.time);
            EditorOverlay overlay;
            overlay.selected = gameView ? 0 : ctx.selected;
            overlay.drawGrid = m_showGrid && !gameView && !view2D; // the 2D view draws its own grid
            overlay.drawMarkers = m_showMarkers && !gameView; // the player sees the game, not the editor's helpers
            overlay.wireframe = m_wireframe;
            overlay.postProcess = m_postProcess;
            if (ctx.showUi && ctx.buildUiDraw) {
                ctx.buildUiDraw(w, h, ctx.uiDraw);
                if (!ctx.uiDraw.Empty()) view.ui = &ctx.uiDraw;
            }
            RenderResources resources = ctx.Resources();
            ctx.renderer.Render(ctx.scene, resources, view, overlay);
            Framebuffer::BindDefault();

            // GL textures are bottom-up, so flip V.
            ImGui::Image(static_cast<ImTextureID>(m_fb.ColorTexture()), size, ImVec2(0, 1), ImVec2(1, 0));
            const ImVec2 origin = ImGui::GetItemRectMin();

            // An actor dragged from Place Actors lands on the ground under the mouse.
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MAGNUM_SPAWN")) {
                    const SpawnKind kind = *static_cast<const SpawnKind*>(payload->Data);
                    const ImVec2 mouse = ImGui::GetMousePos();
                    const glm::vec2 ndc((mouse.x - origin.x) / size.x * 2.0f - 1.0f, 1.0f - (mouse.y - origin.y) / size.y * 2.0f);
                    glm::vec3 at;
                    if (view2D) {
                        const glm::vec2 point = ScreenToWorld2D(mouse, origin, size, aspect);
                        at = glm::vec3(point.x, point.y, 0.0f);
                    } else {
                        const Ray ray = ctx.camera.ScreenRay(ndc, aspect);
                        at = ray.origin + ray.dir * 8.0f;
                        if (ray.dir.y < -0.01f) at = ray.origin + ray.dir * std::min(-ray.origin.y / ray.dir.y, 200.0f);
                    }
                    SpawnActor(ctx, kind, &at);
                }
                ImGui::EndDragDropTarget();
            }

            // The node picked in the UI panel.
            if (ctx.showUi && ctx.activeUi && ctx.uiSelected) {
                if (const UiNode* node = ctx.activeUi->Get(ctx.uiSelected)) {
                    if (node->visible) {
                        const UiRect r = ctx.activeUi->PixelRect(ctx.uiSelected);
                        ImDrawList* odl = ImGui::GetWindowDrawList();
                        odl->AddRect(ImVec2(origin.x + r.x - 1.0f, origin.y + r.y - 1.0f), ImVec2(origin.x + r.x + r.w + 1.0f, origin.y + r.y + r.h + 1.0f),
                                     ui::U32(ui::kOrange), 0.0f, 0, 1.5f);
                    }
                }
            }

            ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
            ImGuizmo::SetRect(origin.x, origin.y, size.x, size.y);
            if (view2D && m_showGrid) DrawGrid2D(origin, size, aspect);
            if (!gameView) {
                DrawGizmo(ctx, view.view, view.projection, view2D);
                DrawOverlays(ctx, origin, size);
            } else {
                m_overlayHot = false;
                DrawHud(ctx, origin, size);
            }
            HandleInput(ctx, w, h, origin);

            // While playing: an orange frame, and keys go to the game only when this window is focused.
            if (ctx.playing) {
                ImGui::GetWindowDrawList()->AddRect(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                                                    ui::U32(ui::kOrange, 0.8f), 0.0f, 0, 2.0f);
            }
            ctx.gameInputEnabled = ctx.playing && ImGui::IsWindowFocused() && !m_flying && !ImGui::GetIO().WantTextInput;
            {   // where the mouse is over the picture, for scripts (input.mouse_position)
                const ImVec2 mouse = ImGui::GetIO().MousePos;
                ctx.gameMouseInView = mouse.x >= origin.x && mouse.x < origin.x + size.x && mouse.y >= origin.y && mouse.y < origin.y + size.y &&
                                      ImGui::IsWindowHovered();
                ctx.gameMousePosition = glm::vec2((mouse.x - origin.x) / size.x, (mouse.y - origin.y) / size.y);
                ctx.gameAspect = aspect;
                ctx.gameViewSize = glm::vec2(size.x, size.y);
                ctx.gameViewOrigin = glm::vec2(origin.x - ImGui::GetMainViewport()->Pos.x, origin.y - ImGui::GetMainViewport()->Pos.y);
                ctx.gameMouseWheel = ctx.playing && ctx.gameMouseInView ? ImGui::GetIO().MouseWheel : 0.0f;
            }
        }
    }
    ImGui::End();
}

void ViewportPanel::UpdateFlyCamera(EditorContext& ctx) {
    if (!m_flying) return;
    const ImGuiIO& io = ImGui::GetIO();
    EditorCamera& cam = ctx.camera;

    cam.yaw += io.MouseDelta.x * 0.12f;
    cam.pitch = std::clamp(cam.pitch - io.MouseDelta.y * 0.12f, -89.0f, 89.0f);
    if (io.MouseWheel != 0.0f) {
        cam.moveSpeed = std::clamp(cam.moveSpeed * std::pow(1.2f, io.MouseWheel), 0.5f, 200.0f);
    }

    glm::vec3 move(0.0f);
    if (ImGui::IsKeyDown(ImGuiKey_W)) move += cam.Forward();
    if (ImGui::IsKeyDown(ImGuiKey_S)) move -= cam.Forward();
    if (ImGui::IsKeyDown(ImGuiKey_D)) move += cam.Right();
    if (ImGui::IsKeyDown(ImGuiKey_A)) move -= cam.Right();
    if (ImGui::IsKeyDown(ImGuiKey_E)) move += glm::vec3(0, 1, 0);
    if (ImGui::IsKeyDown(ImGuiKey_Q)) move -= glm::vec3(0, 1, 0);
    if (glm::length(move) > 0.0f) {
        const float speed = cam.moveSpeed * (io.KeyShift ? 3.0f : 1.0f);
        cam.position += glm::normalize(move) * speed * ctx.deltaTime;
    }
}

void ViewportPanel::DrawGizmo(EditorContext& ctx, const glm::mat4& view, const glm::mat4& projection, bool orthographic) {
    TransformComponent* transform = ctx.scene.TryGet<TransformComponent>(ctx.selected);
    if (!transform || ctx.gizmoMode == GizmoMode::Select) {
        m_gizmoActive = false;
        return;
    }

    static const ImGuizmo::OPERATION operations[] = {ImGuizmo::TRANSLATE, ImGuizmo::ROTATE, ImGuizmo::SCALE};
    ImGuizmo::SetOrthographic(orthographic);
    ImGuizmo::Enable(!m_flying);

    const bool snapping = ctx.gizmoSnap != ImGui::GetIO().KeyCtrl;
    const float snapValues[3] = {ctx.gizmoMode == GizmoMode::Rotate ? 15.0f : (ctx.gizmoMode == GizmoMode::Scale ? 0.1f : 0.5f),
                                 ctx.gizmoMode == GizmoMode::Scale ? 0.1f : 0.5f, ctx.gizmoMode == GizmoMode::Scale ? 0.1f : 0.5f};

    const TransformComponent before = *transform;
    glm::mat4 matrix = transform->Matrix();
    ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), operations[static_cast<int>(ctx.gizmoMode) - 1],
                         ctx.gizmoLocal ? ImGuizmo::LOCAL : ImGuizmo::WORLD, glm::value_ptr(matrix), nullptr,
                         snapping ? snapValues : nullptr);

    if (ImGuizmo::IsUsing()) {
        if (!m_gizmoActive) {
            m_gizmoActive = true;
            m_gizmoActor = ctx.selected;
            m_gizmoBefore = before; // value from before this frame's change
        }
        *transform = DecomposeTransform(matrix, before);
    } else if (m_gizmoActive) {
        m_gizmoActive = false;
        if (const TransformComponent* now = ctx.scene.TryGet<TransformComponent>(m_gizmoActor)) {
            RecordTransformEdit(ctx, m_gizmoActor, m_gizmoBefore, *now);
        }
    }
}

glm::vec2 ViewportPanel::ScreenToWorld2D(ImVec2 mouse, ImVec2 origin, ImVec2 size, float aspect) const {
    const float nx = (mouse.x - origin.x) / size.x * 2.0f - 1.0f;
    const float ny = 1.0f - (mouse.y - origin.y) / size.y * 2.0f;
    return {m_center2D.x + nx * m_half2D * aspect, m_center2D.y + ny * m_half2D};
}

// Grid lines of the 2D view: every unit, stronger every ten, colored axes.
void ViewportPanel::DrawGrid2D(ImVec2 origin, ImVec2 size, float aspect) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float pixelsPerUnit = size.y / (2.0f * m_half2D);
    auto toScreen = [&](float x, float y) {
        return ImVec2(origin.x + size.x * 0.5f + (x - m_center2D.x) * pixelsPerUnit, origin.y + size.y * 0.5f - (y - m_center2D.y) * pixelsPerUnit);
    };
    dl->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
    const float left = m_center2D.x - m_half2D * aspect, right = m_center2D.x + m_half2D * aspect;
    const float bottom = m_center2D.y - m_half2D, top = m_center2D.y + m_half2D;
    for (const float step : {1.0f, 10.0f}) {
        if (step * pixelsPerUnit < 7.0f) continue; // too dense to be useful
        const ImU32 color = IM_COL32(255, 255, 255, step > 1.0f ? 34 : 16);
        for (float x = std::floor(left / step) * step; x <= right; x += step) dl->AddLine(toScreen(x, bottom), toScreen(x, top), color);
        for (float y = std::floor(bottom / step) * step; y <= top; y += step) dl->AddLine(toScreen(left, y), toScreen(right, y), color);
    }
    dl->AddLine(toScreen(left, 0.0f), toScreen(right, 0.0f), IM_COL32(220, 70, 70, 150), 1.5f);  // the X axis
    dl->AddLine(toScreen(0.0f, bottom), toScreen(0.0f, top), IM_COL32(90, 200, 100, 150), 1.5f); // the Y axis
    dl->PopClipRect();
}

// What scripts draw over the game view (hud.text / hud.rect), plus a hint while the mouse is not captured yet.
void ViewportPanel::DrawHud(EditorContext& ctx, ImVec2 origin, ImVec2 size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 end(origin.x + size.x, origin.y + size.y);
    dl->PushClipRect(origin, end, true);
    for (const HudItem& item : ctx.hud) {
        const ImU32 color = ImGui::ColorConvertFloat4ToU32(ImVec4(item.color.r, item.color.g, item.color.b, item.color.a));
        const ImVec2 p(origin.x + item.x * size.x, origin.y + item.y * size.y);
        if (item.kind == HudItem::Kind::Rect) {
            dl->AddRectFilled(p, ImVec2(p.x + item.w * size.x, p.y + item.h * size.y), color);
        } else {
            dl->AddText(ImGui::GetFont(), item.size, ImVec2(p.x + 1.5f, p.y + 1.5f), IM_COL32(0, 0, 0, 160), item.text.c_str()); // shadow
            dl->AddText(ImGui::GetFont(), item.size, p, color, item.text.c_str());
        }
    }
    if (!ctx.mouseCaptured) {
        const char* hint = "Click to capture the mouse. Esc releases it, Esc again stops.";
        const ImVec2 ts = ImGui::CalcTextSize(hint);
        dl->AddRectFilled(ImVec2(origin.x + (size.x - ts.x) * 0.5f - 10.0f, end.y - 40.0f), ImVec2(origin.x + (size.x + ts.x) * 0.5f + 10.0f, end.y - 14.0f),
                          IM_COL32(0, 0, 0, 150), 5.0f);
        dl->AddText(ImVec2(origin.x + (size.x - ts.x) * 0.5f, end.y - 36.0f), IM_COL32(230, 230, 230, 255), hint);
    }
    dl->PopClipRect();
}

void ViewportPanel::HandleInput(EditorContext& ctx, int width, int height, ImVec2 origin) {
    // The gizmo draws after the image, so test the image's rect by position instead of IsItemHovered.
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 end(origin.x + static_cast<float>(width), origin.y + static_cast<float>(height));
    const bool inside = io.MousePos.x >= origin.x && io.MousePos.x < end.x && io.MousePos.y >= origin.y &&
                        io.MousePos.y < end.y;
    const bool hovered = inside && ImGui::IsWindowHovered(ImGuiHoveredFlags_None) && !ImGui::IsAnyItemActive();

    // Playing through a camera: a click captures the mouse for the game (not for a 2D camera, which needs the pointer);
    // the editor's own controls are off.
    if (ctx.playing && ctx.gameCamera.valid) {
        if (!ctx.gameCamera.orthographic && !ctx.mouseCaptured && hovered && !ctx.uiPointerOver && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            ctx.mouseCaptured = true;
            glfwSetInputMode(ctx.window->Handle(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
        ctx.gameMouseDelta = ctx.mouseCaptured ? glm::vec2(io.MouseDelta.x, io.MouseDelta.y) : glm::vec2(0.0f);
        return;
    }
    if (ctx.mouseCaptured) { // the game view went away (play stopped, camera deleted): give the mouse back
        ctx.mouseCaptured = false;
        ctx.gameMouseDelta = glm::vec2(0.0f);
        glfwSetInputMode(ctx.window->Handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }

    if (m_view2D) { // the 2D view: right or middle drag pans, the wheel zooms around the mouse, a click picks
        const float aspect = static_cast<float>(width) / static_cast<float>(height);
        const ImVec2 sz(static_cast<float>(width), static_cast<float>(height));
        if (hovered && !m_overlayHot) {
            if (io.MouseWheel != 0.0f) {
                const glm::vec2 before = ScreenToWorld2D(io.MousePos, origin, sz, aspect);
                m_half2D = std::clamp(m_half2D * std::pow(0.88f, io.MouseWheel), 0.05f, 2000.0f);
                const glm::vec2 after = ScreenToWorld2D(io.MousePos, origin, sz, aspect);
                m_center2D += before - after; // keep the point under the mouse where it was
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
                const glm::vec2 point = ScreenToWorld2D(io.MousePos, origin, sz, aspect);
                Ray ray;
                ray.origin = glm::vec3(point.x, point.y, 50.0f);
                ray.dir = glm::vec3(0.0f, 0.0f, -1.0f);
                ctx.selected = PickActor(ctx.scene, ray);
            }
            if (!ctx.playing) {
                if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) ctx.gizmoMode = GizmoMode::Select;
                if (ImGui::IsKeyPressed(ImGuiKey_W, false)) ctx.gizmoMode = GizmoMode::Translate;
                if (ImGui::IsKeyPressed(ImGuiKey_E, false)) ctx.gizmoMode = GizmoMode::Rotate;
                if (ImGui::IsKeyPressed(ImGuiKey_R, false)) ctx.gizmoMode = GizmoMode::Scale;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F, false)) FocusSelection(ctx);
        }
        if ((ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle)) && (hovered || m_panning2D)) {
            m_panning2D = true;
            const float perPixel = 2.0f * m_half2D / sz.y;
            m_center2D.x -= io.MouseDelta.x * perPixel;
            m_center2D.y += io.MouseDelta.y * perPixel;
        } else {
            m_panning2D = false;
        }
        return;
    }

    if (!m_flying && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        m_flying = true;
        glfwSetInputMode(ctx.window->Handle(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }
    if (m_flying && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        m_flying = false;
        glfwSetInputMode(ctx.window->Handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
    if (m_flying || !hovered || m_overlayHot) return;

    if (!ctx.playing) { // while playing, W/E/R belong to the game
        if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) ctx.gizmoMode = GizmoMode::Select;
        if (ImGui::IsKeyPressed(ImGuiKey_W, false)) ctx.gizmoMode = GizmoMode::Translate;
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) ctx.gizmoMode = GizmoMode::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) ctx.gizmoMode = GizmoMode::Scale;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F, false)) FocusSelection(ctx);

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
        const glm::vec2 ndc((io.MousePos.x - origin.x) / static_cast<float>(width) * 2.0f - 1.0f,
                            1.0f - (io.MousePos.y - origin.y) / static_cast<float>(height) * 2.0f);
        const Ray ray = ctx.camera.ScreenRay(ndc, static_cast<float>(width) / static_cast<float>(height));
        ctx.selected = PickActor(ctx.scene, ray);
    }
    if (io.MouseWheel != 0.0f) {
        ctx.camera.position += ctx.camera.Forward() * io.MouseWheel * std::max(ctx.camera.moveSpeed * 0.2f, 0.5f);
    }
}

void ViewportPanel::FocusSelection(EditorContext& ctx) {
    const TransformComponent* t = ctx.scene.TryGet<TransformComponent>(ctx.selected);
    if (!t) return;
    const glm::vec3 half = ctx.scene.LocalHalfExtents(ctx.selected) * glm::abs(t->scale);
    if (m_view2D) {
        m_center2D = glm::vec2(t->position.x, t->position.y);
        m_half2D = std::max(std::max(half.x, half.y) * 3.0f, 1.0f);
        return;
    }
    ctx.camera.Focus(t->position, glm::length(half) * 1.6f);
}

} // namespace magnum::editor
