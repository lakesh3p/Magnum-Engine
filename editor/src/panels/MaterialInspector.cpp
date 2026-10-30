#include "panels/MaterialInspector.h"

#include "commands/MaterialCommands.h"

#include <imgui.h>

#include <algorithm>
#include <filesystem>

namespace magnum::editor {

namespace fs = std::filesystem;

void MaterialInspector::Commit(EditorContext& ctx, MaterialAsset& material, const MaterialData& after) {
    const MaterialData before = material.Data();
    material.SetData(after);
    RecordMaterialEdit(ctx, material.Path(), before, after);
}

void MaterialInspector::DrawTextureSlot(EditorContext& ctx, MaterialAsset& material, const ShaderParam& param) {
    const MaterialValue* ov = material.Data().Find(param.name);
    const bool overridden = ov && ov->IsTexture();
    const std::string current = overridden ? ov->texture : param.defaultTexture;

    const float size = 52.0f;
    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##slot", ImVec2(size, size));
    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const bool hovered = ImGui::IsItemHovered();

    // Slot picture: the texture itself (shown as stored, not sRGB-decoded) or an empty checkered frame.
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 max(min.x + size, min.y + size);
    draw->AddRectFilled(min, max, IM_COL32(24, 24, 28, 255));
    if (!current.empty()) {
        const Texture& t = ctx.textures.Get(current, false);
        if (t.id) {
            draw->AddImage(static_cast<ImTextureID>(t.id), min, max, ImVec2(0, 1), ImVec2(1, 0));
        } else {
            draw->AddText(ImVec2(min.x + 6, min.y + 18), IM_COL32(255, 110, 100, 255), "missing");
        }
    } else {
        draw->AddText(ImVec2(min.x + 12, min.y + 18), IM_COL32(120, 120, 128, 255), "none");
    }
    draw->AddRect(min, max, hovered ? IM_COL32(0, 120, 215, 255) : IM_COL32(70, 70, 78, 255));

    std::string dropped;
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MAGNUM_ASSET")) {
            const std::string path(static_cast<const char*>(payload->Data));
            if (TextureLibrary::IsImageFile(path)) dropped = path;
        }
        ImGui::EndDragDropTarget();
    }
    if (hovered) ImGui::SetTooltip("%s\nClick to choose, or drop a texture here.", current.empty() ? "(no texture)" : current.c_str());

    // Name + clear next to the picture.
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(current.empty() ? "None" : fs::path(current).filename().string().c_str());
    if (overridden) {
        if (ImGui::SmallButton("Reset")) {
            MaterialData after = material.Data();
            after.Remove(param.name);
            Commit(ctx, material, after);
        }
    }
    ImGui::EndGroup();

    if (clicked) {
        m_textureChoices.clear();
        std::error_code ec;
        for (const auto& item : fs::recursive_directory_iterator(ctx.projectRoot / "Content", ec)) {
            const std::string rel = fs::relative(item.path(), ctx.projectRoot, ec).generic_string();
            if (item.is_regular_file(ec) && TextureLibrary::IsImageFile(rel)) m_textureChoices.push_back(rel);
        }
        std::sort(m_textureChoices.begin(), m_textureChoices.end());
        ImGui::OpenPopup("##texturepicker");
    }
    if (ImGui::BeginPopup("##texturepicker")) {
        if (ImGui::Selectable("None")) dropped = "\x01none";
        for (const std::string& choice : m_textureChoices) {
            if (ImGui::Selectable(choice.c_str(), choice == current)) dropped = choice;
        }
        if (m_textureChoices.empty()) ImGui::TextDisabled("No images under Content/");
        ImGui::EndPopup();
    }

    if (!dropped.empty()) {
        MaterialData after = material.Data();
        after.SetTexture(param.name, dropped == "\x01none" ? std::string() : dropped);
        Commit(ctx, material, after);
    }
}

void MaterialInspector::Draw(EditorContext& ctx, const std::string& materialPath) {
    MaterialAsset* material = ctx.materials.Get(materialPath);
    if (!material) {
        ImGui::TextDisabled("Material not found: %s", materialPath.c_str());
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.38f, 0.35f, 1.0f));
    for (const ShaderDiagnostic& d : material->Diagnostics()) ImGui::TextWrapped("%s", d.ToString().c_str());
    ImGui::PopStyleColor();

    ShaderAsset* shader = ctx.shaders.Get(material->Data().shader);
    if (!shader) {
        ImGui::TextColored(ImVec4(1.0f, 0.38f, 0.35f, 1.0f), "Shader not found: %s", material->Data().shader.c_str());
        return;
    }
    if (!shader->Diagnostics().empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.38f, 0.35f, 1.0f));
        for (const ShaderDiagnostic& d : shader->Diagnostics()) ImGui::TextWrapped("%s", d.ToString().c_str());
        ImGui::PopStyleColor();
        if (shader->Program()) ImGui::TextDisabled("Showing the last version of the shader that compiled.");
    }
    if (shader->Params().empty()) {
        ImGui::TextDisabled("This shader declares no parameters.");
        return;
    }

    if (!ImGui::BeginTable("##matparams", 2, ImGuiTableFlags_SizingStretchProp)) return;
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.34f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.66f);

    for (const ShaderParam& p : shader->Params()) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(p.name.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::PushID(p.name.c_str());

        if (p.isTexture) {
            DrawTextureSlot(ctx, *material, p);
            ImGui::PopID();
            continue;
        }

        const MaterialValue* ov = material->Data().Find(p.name);
        const bool overridden = ov && !ov->IsTexture() && ov->components == p.components;
        std::array<float, 4> value = overridden ? ov->number : p.defaultValue;

        const float resetWidth = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(-(resetWidth + ImGui::GetStyle().ItemSpacing.x));
        float* v = value.data();
        bool changed = false;
        switch (p.components) {
        case 1:
            changed = p.hasRange ? ImGui::SliderFloat("##v", v, p.min, p.max) : ImGui::DragFloat("##v", v, 0.01f);
            break;
        case 2: changed = ImGui::DragFloat2("##v", v, 0.01f); break;
        case 3: changed = p.isColor ? ImGui::ColorEdit3("##v", v) : ImGui::DragFloat3("##v", v, 0.01f); break;
        default: changed = p.isColor ? ImGui::ColorEdit4("##v", v) : ImGui::DragFloat4("##v", v, 0.01f); break;
        }
        const bool activated = ImGui::IsItemActivated();
        const bool deactivated = ImGui::IsItemDeactivated();
        const bool deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();

        if (activated) {
            m_editing = true;
            m_before = material->Data(); // before this frame's change
        }
        if (changed) {
            MaterialData after = material->Data();
            after.SetNumber(p.name, p.components, value);
            if (m_editing) material->SetData(after); // live while dragging
            else Commit(ctx, *material, after);       // one-shot (e.g. picker popup)
        }
        if (deactivated) {
            if (deactivatedAfterEdit && m_editing) RecordMaterialEdit(ctx, material->Path(), m_before, material->Data());
            m_editing = false;
        }

        ImGui::SameLine();
        if (overridden) {
            if (ImGui::Button("x", ImVec2(resetWidth, 0))) {
                MaterialData after = material->Data();
                after.Remove(p.name);
                Commit(ctx, *material, after);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset to the shader's default");
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

} // namespace magnum::editor
