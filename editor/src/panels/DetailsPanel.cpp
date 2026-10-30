#include "panels/DetailsPanel.h"

#include "commands/EditorCommands.h"
#include "model/ModelImport.h"
#include "panels/PanelUtil.h"
#include "render/Texture.h"
#include "scene/GameComponents.h"

#include <imgui.h>

#include <cstring>
#include <string>
#include <vector>

namespace magnum::editor {

namespace {

// Draws the widget for one value. Returns true if the user changed it this frame.
bool EditValue(const PropertyInfo& p, PropertyValue& v) {
    switch (p.kind) {
    case PropKind::Bool: return ImGui::Checkbox("##v", &std::get<bool>(v));
    case PropKind::Int: return ImGui::DragInt("##v", &std::get<int>(v));
    case PropKind::Float: {
        float& f = std::get<float>(v);
        return p.hasRange ? ImGui::SliderFloat("##v", &f, p.min, p.max) : ImGui::DragFloat("##v", &f, p.speed);
    }
    case PropKind::Vec3: return ImGui::DragFloat3("##v", &std::get<glm::vec3>(v).x, p.speed);
    case PropKind::Color3: return ImGui::ColorEdit3("##v", &std::get<glm::vec3>(v).x);
    case PropKind::String:
    case PropKind::CString: {
        std::string& s = std::get<std::string>(v);
        char buffer[256];
        std::strncpy(buffer, s.c_str(), sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = '\0';
        if (ImGui::InputText("##v", buffer, sizeof(buffer))) {
            s = buffer;
            return true;
        }
        return false;
    }
    case PropKind::Enum: {
        int& index = std::get<int>(v);
        bool changed = false;
        const int count = static_cast<int>(p.options.size());
        if (ImGui::BeginCombo("##v", index >= 0 && index < count ? p.options[static_cast<size_t>(index)] : "?")) {
            for (int i = 0; i < count; ++i) {
                if (ImGui::Selectable(p.options[static_cast<size_t>(i)], i == index)) {
                    index = i;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }
    }
    return false;
}

bool IsDiscrete(PropKind kind) {
    return kind == PropKind::Bool || kind == PropKind::Enum;
}

} // namespace

void DetailsPanel::EditProperty(EditorContext& ctx, uint64_t id, const ComponentInfo& info, size_t propIndex,
                                void* component) {
    const PropertyInfo& p = info.props[propIndex];
    const PropertyValue current = GetProperty(p, component);
    PropertyValue edited = current;

    const bool changed = EditValue(p, edited);
    const bool activated = ImGui::IsItemActivated();
    const bool deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
    const bool deactivated = ImGui::IsItemDeactivated();

    // Material fields accept .lua files dragged from the Content Browser; a mesh's Model field accepts model files.
    const bool imageField = (std::strcmp(info.name, "Sprite") == 0 || std::strcmp(info.name, "Decal") == 0) && std::strcmp(p.name, "Texture") == 0;
    const bool documentField = std::strcmp(info.name, "Canvas") == 0 && std::strcmp(p.name, "Document") == 0;
    const bool modelField = std::strcmp(info.name, "Mesh") == 0 && std::strcmp(p.name, "Model") == 0;
    if (p.kind == PropKind::String && (std::strcmp(p.name, "Material") == 0 || std::strcmp(p.name, "Script") == 0 || imageField || documentField || modelField) &&
        ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MAGNUM_ASSET")) {
            const std::string path(static_cast<const char*>(payload->Data));
            const bool lua = path.size() > 4 && path.compare(path.size() - 4, 4, ".lua") == 0;
            const bool accepted = imageField ? TextureLibrary::IsImageFile(path) : modelField ? IsModelFile(path) : lua;
            if (accepted) SetPropertyCommand(ctx, id, info, propIndex, path);
        }
        ImGui::EndDragDropTarget();
    }

    if (activated && !IsDiscrete(p.kind)) {
        m_editing = true;
        m_before = current; // value before this frame's change
    }
    if (changed) {
        if (IsDiscrete(p.kind) || !m_editing) {
            SetPropertyCommand(ctx, id, info, propIndex, edited); // one-shot edit
        } else {
            SetProperty(p, component, edited); // live while dragging; recorded when released
        }
    }
    if (deactivated) {
        if (deactivatedAfterEdit && m_editing) {
            RecordPropertyEdit(ctx, id, info, propIndex, m_before, GetProperty(p, component));
        }
        m_editing = false;
    }
}

void DetailsPanel::DrawComponent(EditorContext& ctx, uint64_t id, const ComponentInfo& info, void* component) {
    const bool filtering = m_filter[0] != '\0';

    // Which properties pass the search, grouped by category in declaration order.
    std::vector<const char*> categories;
    std::vector<std::vector<size_t>> visible;
    for (size_t i = 0; i < info.props.size(); ++i) {
        const PropertyInfo& p = info.props[i];
        if (p.hidden) continue;
        if (filtering && !ContainsNoCase(p.name, m_filter)) continue;
        size_t c = 0;
        while (c < categories.size() && std::strcmp(categories[c], p.category) != 0) ++c;
        if (c == categories.size()) {
            categories.push_back(p.category);
            visible.emplace_back();
        }
        visible[c].push_back(i);
    }
    if (categories.empty()) return; // nothing matches the search

    if (filtering) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    const bool open = ImGui::CollapsingHeader(info.name, ImGuiTreeNodeFlags_DefaultOpen);
    if (info.removable && ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Remove Component")) m_removeRequest = &info;
        ImGui::EndPopup();
    }
    if (!open) return;

    for (size_t c = 0; c < categories.size(); ++c) {
        // A category with the same name as its component would only repeat the header.
        const bool showCategory = std::strcmp(categories[c], info.name) != 0;
        if (showCategory) {
            if (filtering) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            if (!ImGui::TreeNodeEx(categories[c], ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) continue;
        }

        if (ImGui::BeginTable("##props", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.38f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.62f);

            for (const size_t i : visible[c]) {
                const PropertyInfo& p = info.props[i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(p.name);

                ImGui::TableSetColumnIndex(1);
                ImGui::PushID(static_cast<int>(i));
                const bool modified = GetProperty(p, component) != DefaultValue(info, p);
                const float resetWidth = ImGui::GetFrameHeight();
                ImGui::SetNextItemWidth(-(resetWidth + ImGui::GetStyle().ItemSpacing.x));
                EditProperty(ctx, id, info, i, component);

                ImGui::SameLine();
                if (modified) {
                    if (ImGui::Button("x", ImVec2(resetWidth, 0))) {
                        SetPropertyCommand(ctx, id, info, i, DefaultValue(info, p));
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset to default");
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (showCategory) ImGui::TreePop();
    }

    // The mesh's material: its parameters, generated from the shader, right below the mesh properties.
    if (!filtering && std::strcmp(info.name, "Mesh") == 0) {
        const std::string material = static_cast<const MeshComponent*>(component)->material;
        if (ImGui::TreeNodeEx("Material Parameters", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
            ImGui::TextDisabled("Shared by every actor using this material.");
            m_materialInspector.Draw(ctx, material);
            ImGui::TreePop();
        }
    }
}

void DetailsPanel::Draw(EditorContext& ctx) {
    const bool visible = ImGui::Begin("Details");
    ctx.detailsDock = ImGui::GetWindowDockID();
    if (!visible) {
        ImGui::End();
        return;
    }

    const uint64_t id = ctx.selected;
    const entt::entity e = ctx.scene.Find(id);
    if (e == entt::null) {
        ImGui::TextDisabled("Select an actor to see its properties.");
        ImGui::End();
        return;
    }
    entt::registry& reg = ctx.scene.Registry();

    // Actor name as the title.
    if (const ComponentInfo* nameInfo = FindComponentInfo("Name")) {
        ImGui::SetNextItemWidth(-1);
        EditProperty(ctx, id, *nameInfo, 0, nameInfo->get(reg, e));
    }

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##search", "Search details", m_filter, sizeof(m_filter));
    ImGui::Separator();

    m_removeRequest = nullptr;
    for (const ComponentInfo* info : AllComponentInfos()) {
        if (info->hidden || !info->has(reg, e)) continue;
        ImGui::PushID(info->name);
        DrawComponent(ctx, id, *info, info->get(reg, e));
        ImGui::PopID();
    }

    // Game components saved in the scene whose type the loaded module does not provide.
    if (const auto* game = reg.try_get<GameComponentsComponent>(e)) {
        for (const GameInstance& item : game->items) {
            if (item.resolved) continue;
            ImGui::TextDisabled("%s  (not registered by the game module; values are kept)", item.type.c_str());
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("+ Add Component", ImVec2(-1, 0))) ImGui::OpenPopup("##addcomponent");
    const ComponentInfo* addRequest = nullptr;
    if (ImGui::BeginPopup("##addcomponent")) {
        bool any = false;
        const ComponentInfo* shownGameHeader = nullptr;
        for (const ComponentInfo* info : AllComponentInfos()) {
            if (info->hidden || !info->removable || info->has(reg, e)) continue;
            if (info->game && !shownGameHeader) {
                ImGui::SeparatorText("Game");
                shownGameHeader = info;
            }
            any = true;
            if (ImGui::MenuItem(info->name)) addRequest = info;
        }
        if (!any) ImGui::TextDisabled("All components added");
        ImGui::EndPopup();
    }

    // Structural changes after drawing, so no widget holds a stale component pointer.
    if (addRequest) AddComponentCommand(ctx, id, *addRequest);
    if (m_removeRequest) RemoveComponentCommand(ctx, id, *m_removeRequest);

    ImGui::End();
}

} // namespace magnum::editor
