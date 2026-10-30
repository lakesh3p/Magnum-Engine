#include "panels/PlaceActorsPanel.h"

#include "Icons.h"
#include "Theme.h"
#include "commands/EditorCommands.h"
#include "panels/PanelUtil.h"

#include <imgui.h>

#include <cctype>
#include <cfloat>
#include <string>

namespace magnum::editor {

namespace {

constexpr float kRowHeight = 22.0f;

enum Group { Basic, Gameplay, TwoD, Interface, Shapes, Architecture, Lights, GroupCount };
const char* const kGroups[GroupCount] = {"Basic", "Gameplay", "2D", "UI", "Shapes", "Architecture", "Lights"};

struct Entry {
    SpawnKind kind;
    Icon icon;
    Group group;
};

const Entry kEntries[] = {
    {SpawnKind::Empty, Icon::Actor, Basic},
    {SpawnKind::Decal, Icon::Texture, Basic},
    {SpawnKind::Character, Icon::Person, Gameplay},
    {SpawnKind::Camera, Icon::Camera, Gameplay},
    {SpawnKind::PhysicsCube, Icon::Rigidbody, Gameplay},
    {SpawnKind::PhysicsSphere, Icon::Rigidbody, Gameplay},
    {SpawnKind::TriggerVolume, Icon::Collider, Gameplay},
    {SpawnKind::Wall, Icon::Cube, Gameplay},
    {SpawnKind::Ground, Icon::Plane, Gameplay},
    {SpawnKind::Sprite, Icon::Texture, TwoD},
    {SpawnKind::PhysicsSprite, Icon::Rigidbody, TwoD},
    {SpawnKind::Tile, Icon::Grid, TwoD},
    {SpawnKind::Player2D, Icon::Person, TwoD},
    {SpawnKind::Camera2D, Icon::Camera, TwoD},
    {SpawnKind::Quad, Icon::Plane, TwoD},
    {SpawnKind::Canvas, Icon::Maximize, Interface},
    {SpawnKind::Cube, Icon::Cube, Shapes},
    {SpawnKind::Sphere, Icon::Sphere, Shapes},
    {SpawnKind::Plane, Icon::Plane, Shapes},
    {SpawnKind::Cylinder, Icon::Cylinder, Shapes},
    {SpawnKind::Cone, Icon::Cone, Shapes},
    {SpawnKind::Capsule, Icon::Capsule, Shapes},
    {SpawnKind::Hemisphere, Icon::Hemisphere, Shapes},
    {SpawnKind::Torus, Icon::Torus, Shapes},
    {SpawnKind::Pyramid, Icon::Pyramid, Shapes},
    {SpawnKind::Prism, Icon::Prism, Shapes},
    {SpawnKind::Diamond, Icon::Diamond, Shapes},
    {SpawnKind::Wedge, Icon::Wedge, Architecture},
    {SpawnKind::Stairs, Icon::Stairs, Architecture},
    {SpawnKind::Arch, Icon::Arch, Architecture},
    {SpawnKind::Tube, Icon::Tube, Architecture},
    {SpawnKind::Ring, Icon::Ring, Architecture},
    {SpawnKind::Disc, Icon::Disc, Architecture},
    {SpawnKind::DirectionalLight, Icon::Light, Lights},
};

ImU32 IconColor(const Entry& e) {
    switch (e.group) {
    case Lights: return ui::U32(ui::kYellow);
    case Gameplay: return ui::U32(ui::kGreen);
    case TwoD: return ui::U32(ui::kPurple);
    case Interface: return ui::U32(ui::kOrange);
    case Shapes: return ui::U32(ui::kBlue);
    case Architecture: return ui::U32(ui::kOrange, 0.85f);
    default: return ui::U32(ui::kTextDim);
    }
}

bool Matches(const Entry& e, const char* filter) {
    const SpawnInfo& info = GetSpawnInfo(e.kind);
    return ContainsNoCase(info.name, filter) || ContainsNoCase(info.command, filter);
}

// A group title: chevron, UPPERCASE name, and how many rows it holds.
void GroupHeader(const char* title, int count, bool& open) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::PushID(title);
    const bool clicked = ImGui::InvisibleButton("##group", ImVec2(width, kRowHeight));
    ImGui::PopID();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (ImGui::IsItemHovered()) dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + kRowHeight), ui::U32(ui::kHover, 0.6f));
    DrawIcon(dl, open ? Icon::ChevronDown : Icon::ChevronRight, ImVec2(pos.x + 12.0f, pos.y + kRowHeight * 0.5f), 10.0f, ui::U32(ui::kTextDim));

    std::string upper = title;
    for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    ImGui::PushFont(GetFont(UiFont::Semibold));
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.86f, ImVec2(pos.x + 24.0f, pos.y + (kRowHeight - ImGui::GetTextLineHeight() * 0.86f) * 0.5f),
                ui::U32(ui::kTextDim), upper.c_str());
    ImGui::PopFont();

    char number[16];
    std::snprintf(number, sizeof(number), "%d", count);
    ImGui::PushFont(GetFont(UiFont::Mono));
    const ImVec2 ns = ImGui::CalcTextSize(number);
    dl->AddText(ImVec2(pos.x + width - ns.x - 10.0f, pos.y + (kRowHeight - ns.y) * 0.5f), ui::U32(ui::kTextFaint), number);
    ImGui::PopFont();

    if (clicked) open = !open;
}

} // namespace

void PlaceActorsPanel::Draw(EditorContext& ctx) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool visible = ImGui::Begin("Place Actors");
    ImGui::PopStyleVar();
    if (!visible) {
        ImGui::End();
        return;
    }

    // Filter field with a leading magnifier, edge to edge.
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

    const bool filtering = m_filter[0] != '\0';
    const float width = ImGui::GetContentRegionAvail().x;
    bool any = false;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    for (int g = 0; g < GroupCount; ++g) {
        int count = 0;
        for (const Entry& e : kEntries) {
            if (e.group == g && (!filtering || Matches(e, m_filter))) ++count;
        }
        if (count == 0) continue;
        any = true;
        GroupHeader(kGroups[g], count, m_open[g]);
        if (!filtering && !m_open[g]) continue; // a filter shows every match, folded or not

        for (const Entry& e : kEntries) {
            if (e.group != g || (filtering && !Matches(e, m_filter))) continue;
            const SpawnInfo& info = GetSpawnInfo(e.kind);
            const std::string command = std::string("spawn ") + info.command;

            ImGui::PushID(info.name);
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const bool clicked = ImGui::InvisibleButton("##row", ImVec2(width, kRowHeight));
            const bool hovered = ImGui::IsItemHovered();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (ImGui::IsItemActive()) dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + kRowHeight), ui::U32(ui::kActive));
            else if (hovered) dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + kRowHeight), ui::U32(ui::kHover));

            DrawIcon(dl, e.icon, ImVec2(pos.x + 26.0f, pos.y + kRowHeight * 0.5f), 14.0f, IconColor(e));
            dl->AddText(ImVec2(pos.x + 42.0f, pos.y + (kRowHeight - ImGui::GetTextLineHeight()) * 0.5f), ui::U32(hovered ? ui::kText : ui::kTextSoft), info.name);

            const float nameEnd = 42.0f + ImGui::CalcTextSize(info.name).x;
            ImGui::PushFont(GetFont(UiFont::Mono));
            const ImVec2 cs = ImGui::CalcTextSize(command.c_str());
            if (width - cs.x - 10.0f > nameEnd + 24.0f) { // only when it fits without crowding the name
                dl->AddText(ImVec2(pos.x + width - cs.x - 10.0f, pos.y + (kRowHeight - cs.y) * 0.5f), ui::U32(ui::kTextFaint), command.c_str());
            }
            ImGui::PopFont();

            if (clicked) SpawnActor(ctx, e.kind);
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                ImGui::SetDragDropPayload("MAGNUM_SPAWN", &e.kind, sizeof(e.kind));
                ImGui::TextUnformatted(info.name);
                ImGui::EndDragDropSource();
            } else if (hovered) {
                ImGui::SetTooltip("Click: add in front of the camera\nDrag into the viewport: drop on the ground");
            }
            ImGui::PopID();
        }
    }
    ImGui::PopStyleVar();

    if (!any) {
        ImGui::SetCursorPos(ImVec2(12.0f, ImGui::GetCursorPosY() + 8.0f));
        ImGui::TextDisabled("no match");
    }
    ImGui::End();
}

} // namespace magnum::editor
