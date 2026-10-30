#include "panels/MaterialPanel.h"

#include "ThumbnailCache.h"
#include "platform/FileOps.h"
#include "shader/MaterialGraph.h"

#include <imgui.h>

#include <algorithm>
#include <fstream>

namespace magnum::editor {

void MaterialPanel::Draw(EditorContext& ctx) {
    if (!ImGui::Begin("Material")) {
        ImGui::End();
        return;
    }

    std::string path = ctx.inspectedMaterial;
    if (path.empty()) {
        if (const MeshComponent* mesh = ctx.scene.TryGet<MeshComponent>(ctx.selected)) path = mesh->material;
    }
    if (path.empty()) {
        ImGui::TextDisabled("Select an actor with a mesh, or a material in the Content Browser.");
        ImGui::End();
        return;
    }

    MaterialAsset* material = ctx.materials.Get(path);
    if (!material) {
        ImGui::TextDisabled("Material not found: %s", path.c_str());
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted(path.substr(path.rfind('/') + 1).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", path.c_str());

    // Preview sphere: centered, no background, turned by dragging (double-click puts it back). Redrawn when the
    // material, its shader or a texture changed, or when it was turned.
    {
        RenderResources resources = ctx.Resources();
        const float avail = ImGui::GetContentRegionAvail().x;
        const float side = std::max(64.0f, std::min(avail, 260.0f));
        const int pixels = static_cast<int>(side);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - side) * 0.5f);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##preview", ImVec2(side, side));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
            m_yaw += ImGui::GetIO().MouseDelta.x * 0.6f;
            m_pitch = std::clamp(m_pitch + ImGui::GetIO().MouseDelta.y * 0.6f, -89.0f, 89.0f);
        }
        if (ImGui::IsItemHovered()) {
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) m_yaw = m_pitch = 0.0f;
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            ImGui::SetTooltip("Drag to rotate, double-click to reset");
        }

        const uint64_t revision = AssetRevision(resources);
        if (revision != m_drawnRevision || m_yaw != m_drawnYaw || m_pitch != m_drawnPitch || path != m_drawnPath || pixels != m_drawnSize) {
            m_previewFb.Resize(pixels, pixels);
            m_previewFb.Bind();
            ctx.renderer.RenderMaterialPreview(resources, path, pixels, pixels, m_yaw, m_pitch);
            Framebuffer::BindDefault();
            m_drawnRevision = revision;
            m_drawnYaw = m_yaw;
            m_drawnPitch = m_pitch;
            m_drawnPath = path;
            m_drawnSize = pixels;
        }
        ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(m_previewFb.ColorTexture()), pos, ImVec2(pos.x + side, pos.y + side),
                                             ImVec2(0, 1), ImVec2(1, 0));
    }

    ImGui::TextDisabled("Shader: %s", material->Data().shader.c_str());
    if (ImGui::SmallButton("Open material file")) OpenWithDefaultApp(ctx.projectRoot / path);
    ImGui::SameLine();
    if (ImGui::SmallButton("Open shader file")) OpenWithDefaultApp(ctx.projectRoot / material->Data().shader);
    // Graph shaders open in the node editor.
    {
        std::ifstream shaderFile(ctx.projectRoot / material->Data().shader, std::ios::binary);
        char head[32] = {};
        shaderFile.read(head, sizeof(head) - 1);
        if (IsGraphLua(head)) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Edit shader graph")) ctx.graphRequest = material->Data().shader;
        }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reload")) {
        material->Reload();
        if (ShaderAsset* shader = ctx.shaders.Get(material->Data().shader)) shader->Reload();
    }
    ImGui::Separator();

    m_inspector.Draw(ctx, path);
    ImGui::End();
}

} // namespace magnum::editor
