#include "panels/OutlinerPanel.h"

#include "Icons.h"
#include "Theme.h"
#include "commands/EditorCommands.h"
#include "panels/PanelUtil.h"

#include <imgui.h>

#include <cstdio>
#include <cstring>

namespace magnum::editor {

namespace {

constexpr float kRowHeight = 22.0f;

const char* TypeLabel(const Scene& scene, uint64_t id) {
    if (const MeshComponent* m = scene.TryGet<MeshComponent>(id)) return m->model.empty() ? kPrimitiveNames[static_cast<int>(m->primitive)] : "Model";
    if (scene.TryGet<LightComponent>(id)) return "Light";
    if (scene.TryGet<CameraComponent>(id)) return "Camera";
    if (scene.TryGet<SpriteComponent>(id)) return "Sprite";
    if (scene.TryGet<DecalComponent>(id)) return "Decal";
    if (scene.TryGet<CanvasComponent>(id)) return "Canvas";
    if (const ColliderComponent* c = scene.TryGet<ColliderComponent>(id)) return c->trigger ? "Trigger" : "Collider";
    return "Empty";
}

} // namespace

void OutlinerPanel::Draw(EditorContext& ctx) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool visible = ImGui::Begin("Outliner");
    ImGui::PopStyleVar();
    if (!visible) {
        ImGui::End();
        return;
    }

    // Filter field with a magnifier, edge to edge.
    ImGui::SetCursorPos(ImVec2(8.0f, ImGui::GetCursorPosY() + 8.0f)); // below the tab bar
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(26.0f, 3.0f));
    ImGui::InputTextWithHint("##filter", "filter", m_filter, sizeof(m_filter));
    ImGui::PopStyleVar();
    {
        const ImVec2 min = ImGui::GetItemRectMin();
        DrawIcon(ImGui::GetWindowDrawList(), Icon::Search, ImVec2(min.x + 13.0f, min.y + ImGui::GetItemRectSize().y * 0.5f), 12.0f, ui::U32(ui::kTextDim));
    }
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);

    // Column titles: name on the left, kind on the right, like a table in a debugger.
    const float width = ImGui::GetContentRegionAvail().x;
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        char count[32];
        std::snprintf(count, sizeof(count), "%zu", ctx.scene.Order().size());
        ImGui::PushFont(GetFont(UiFont::Semibold));
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.86f, ImVec2(pos.x + 12.0f, pos.y + 3.0f), ui::U32(ui::kTextDim), "ACTORS");
        ImGui::PopFont();
        ImGui::PushFont(GetFont(UiFont::Mono));
        dl->AddText(ImVec2(pos.x + width - ImGui::CalcTextSize(count).x - 10.0f, pos.y + 3.0f), ui::U32(ui::kTextFaint), count);
        ImGui::PopFont();
        dl->AddLine(ImVec2(pos.x, pos.y + 22.0f), ImVec2(pos.x + width, pos.y + 22.0f), ui::U32(ui::kBorder));
        ImGui::Dummy(ImVec2(width, 23.0f));
    }

    uint64_t toDelete = 0, toDuplicate = 0;
    bool any = false;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    for (const uint64_t id : ctx.scene.Order()) {
        const std::string& name = ctx.scene.TryGet<NameComponent>(id)->name;
        const char* type = TypeLabel(ctx.scene, id);
        if (m_filter[0] && !ContainsNoCase(name, m_filter) && !ContainsNoCase(type, m_filter)) continue;
        any = true;

        ImGui::PushID(reinterpret_cast<const void*>(static_cast<uintptr_t>(id)));
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const bool selected = id == ctx.selected;
        const bool clicked = ImGui::InvisibleButton("##row", ImVec2(width, kRowHeight));
        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 end(pos.x + width, pos.y + kRowHeight);
        if (selected) {
            dl->AddRectFilled(pos, end, ui::U32(ui::kSelection));
            dl->AddRectFilled(pos, ImVec2(pos.x + 2.0f, end.y), ui::U32(ui::kAccent));
        } else if (hovered) {
            dl->AddRectFilled(pos, end, ui::U32(ui::kHover));
        }

        Icon icon = Icon::Actor;
        ImU32 iconColor = ui::U32(ui::kTextDim);
        if (const MeshComponent* m = ctx.scene.TryGet<MeshComponent>(id)) {
            icon = PrimitiveIcon(m->primitive);
            iconColor = selected ? ui::U32(ui::kAccent) : ui::U32(ui::kTextSoft);
        } else if (ctx.scene.TryGet<LightComponent>(id)) {
            icon = Icon::Light;
            iconColor = ui::U32(ui::kYellow);
        } else if (ctx.scene.TryGet<CameraComponent>(id)) {
            icon = Icon::Camera;
            iconColor = ui::U32(ui::kPurple);
        } else if (ctx.scene.TryGet<CanvasComponent>(id)) {
            icon = Icon::Maximize;
            iconColor = ui::U32(ui::kOrange);
        } else if (ctx.scene.TryGet<DecalComponent>(id)) {
            icon = Icon::Texture;
            iconColor = ui::U32(ui::kOrange);
        } else if (ctx.scene.TryGet<SpriteComponent>(id)) {
            icon = Icon::Texture;
            iconColor = selected ? ui::U32(ui::kAccent) : ui::U32(ui::kTextSoft);
        } else if (ctx.scene.TryGet<ColliderComponent>(id)) {
            icon = Icon::Collider;
            iconColor = ui::U32(ui::kGreen);
        }
        DrawIcon(dl, icon, ImVec2(pos.x + 20.0f, pos.y + kRowHeight * 0.5f), 14.0f, iconColor);
        dl->AddText(ImVec2(pos.x + 36.0f, pos.y + (kRowHeight - ImGui::GetTextLineHeight()) * 0.5f),
                    selected ? IM_COL32(255, 255, 255, 255) : ui::U32(ui::kTextSoft), name.c_str());
        // Small badges after the name: a script is attached, the actor is a physics body.
        {
            float x = pos.x + 36.0f + ImGui::CalcTextSize(name.c_str()).x + 10.0f;
            if (ctx.scene.TryGet<ScriptComponent>(id)) {
                DrawIcon(dl, Icon::Script, ImVec2(x + 5.0f, pos.y + kRowHeight * 0.5f), 11.0f, ui::U32(ui::kOrange));
                x += 16.0f;
            }
            if (ctx.scene.TryGet<RigidbodyComponent>(id)) DrawIcon(dl, Icon::Rigidbody, ImVec2(x + 5.0f, pos.y + kRowHeight * 0.5f), 11.0f, ui::U32(ui::kBlue));
        }
        ImGui::PushFont(GetFont(UiFont::Mono));
        const ImVec2 ts = ImGui::CalcTextSize(type);
        dl->AddText(ImVec2(end.x - ts.x - 10.0f, pos.y + (kRowHeight - ts.y) * 0.5f), selected ? ui::U32(ui::kAccentHover) : ui::U32(ui::kTextFaint), type);
        ImGui::PopFont();

        if (clicked) ctx.selected = id;
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            m_renameId = id;
            std::strncpy(m_renameBuffer, name.c_str(), sizeof(m_renameBuffer) - 1);
            m_openRename = true;
        }
        if (ImGui::BeginPopupContextItem()) {
            ctx.selected = id;
            if (ImGui::MenuItem("Rename", "F2")) {
                m_renameId = id;
                std::strncpy(m_renameBuffer, name.c_str(), sizeof(m_renameBuffer) - 1);
                m_openRename = true;
            }
            if (ImGui::MenuItem("Duplicate", "Ctrl+D")) toDuplicate = id;
            if (ImGui::MenuItem("Delete", "Del")) toDelete = id;
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::PopStyleVar();

    if (!any) {
        ImGui::SetCursorPos(ImVec2(12.0f, ImGui::GetCursorPosY() + 8.0f));
        ImGui::TextDisabled(m_filter[0] ? "no match" : "empty level: add actors from Place Actors");
    }

    // Structural changes happen after the list was drawn, so no row refers to a dead actor.
    if (toDuplicate) DuplicateActor(ctx, toDuplicate);
    if (toDelete) DeleteActor(ctx, toDelete);

    DrawRenamePopup(ctx);
    ImGui::End();
}

void OutlinerPanel::DrawRenamePopup(EditorContext& ctx) {
    if (m_openRename) {
        ImGui::OpenPopup("Rename Actor");
        m_openRename = false;
    }
    if (ImGui::BeginPopup("Rename Actor")) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##rename", m_renameBuffer, sizeof(m_renameBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (const ComponentInfo* info = FindComponentInfo("Name"); info && m_renameBuffer[0]) {
                SetPropertyCommand(ctx, m_renameId, *info, 0, std::string(m_renameBuffer));
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace magnum::editor
