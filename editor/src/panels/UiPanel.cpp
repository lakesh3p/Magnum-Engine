#include "panels/UiPanel.h"

#include "Icons.h"
#include "Theme.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace magnum::editor {

namespace {

const char* KindName(UiKind k) {
    static const char* const names[] = {"Panel", "Image", "Text", "Button", "Toggle", "Slider", "Progress", "Scroll"};
    return names[static_cast<int>(k)];
}

ImU32 KindColor(UiKind k) {
    switch (k) {
    case UiKind::Button:
    case UiKind::Toggle:
    case UiKind::Slider: return ui::U32(ui::kGreen);
    case UiKind::Text: return ui::U32(ui::kYellow);
    case UiKind::Image: return ui::U32(ui::kPurple);
    case UiKind::Progress: return ui::U32(ui::kBlue);
    case UiKind::Scroll: return ui::U32(ui::kOrange);
    default: return ui::U32(ui::kTextDim);
    }
}

// A label on the left, the widget on the right.
bool Row(const char* label) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, ui::kTextDim);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
    return true;
}

bool EditString(const char* label, std::string& s) {
    Row(label);
    char buffer[512];
    std::strncpy(buffer, s.c_str(), sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    ImGui::PushID(label);
    const bool changed = ImGui::InputText("##v", buffer, sizeof(buffer));
    ImGui::PopID();
    if (changed) s = buffer;
    return changed;
}

bool EditColor(const char* label, std::optional<glm::vec4>& value) {
    Row(label);
    glm::vec4 c = value.value_or(glm::vec4(0.0f));
    ImGui::PushID(label);
    const bool changed = ImGui::ColorEdit4("##v", &c.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreviewHalf);
    ImGui::PopID();
    if (changed) value = c;
    return changed;
}

bool EditFloat(const char* label, float& v, float speed = 0.1f, float lo = -100000.0f, float hi = 100000.0f) {
    Row(label);
    ImGui::PushID(label);
    const bool changed = ImGui::DragFloat("##v", &v, speed, lo, hi, "%.2f");
    ImGui::PopID();
    return changed;
}

bool EditVec2(const char* label, glm::vec2& v, float speed = 0.5f) {
    Row(label);
    ImGui::PushID(label);
    const bool changed = ImGui::DragFloat2("##v", &v.x, speed, -100000.0f, 100000.0f, "%.1f");
    ImGui::PopID();
    return changed;
}

bool EditBool(const char* label, bool& v) {
    Row(label);
    ImGui::PushID(label);
    const bool changed = ImGui::Checkbox("##v", &v);
    ImGui::PopID();
    return changed;
}

template <class E>
bool EditEnum(const char* label, E& value, const char* const* names, int count) {
    Row(label);
    ImGui::PushID(label);
    bool changed = false;
    int index = static_cast<int>(value);
    if (ImGui::BeginCombo("##v", index >= 0 && index < count ? names[index] : "?")) {
        for (int i = 0; i < count; ++i) {
            if (ImGui::Selectable(names[i], i == index)) {
                value = static_cast<E>(i);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    return changed;
}

} // namespace

void UiPanel::DrawNodeTree(EditorContext& ctx, UiSystem& ui, UiId id) {
    UiNode* n = ui.Get(id);
    if (!n) return;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_FramePadding;
    if (n->children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (ctx.uiSelected == id) flags |= ImGuiTreeNodeFlags_Selected;
    ImGui::PushID(static_cast<int>(id));
    char label[160];
    std::snprintf(label, sizeof(label), "%s", n->name.empty() ? "(unnamed)" : n->name.c_str());
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const bool open = ImGui::TreeNodeEx("##node", flags, "%s", "");
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) ctx.uiSelected = id;
    // Kind badge and name drawn over the row.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float textY = start.y + ImGui::GetStyle().FramePadding.y;
    const float x = ImGui::GetItemRectMin().x + ImGui::GetTreeNodeToLabelSpacing();
    const ImU32 dim = ui::U32(ui::kTextFaint);
    dl->AddText(ImVec2(x, textY), KindColor(n->kind), KindName(n->kind));
    const float kindWidth = ImGui::CalcTextSize(KindName(n->kind)).x + 8.0f;
    dl->AddText(ImVec2(x + kindWidth, textY), n->visible ? ui::U32(ui::kText) : dim, label);
    if (!n->visible) dl->AddText(ImVec2(x + kindWidth + ImGui::CalcTextSize(label).x + 8.0f, textY), dim, "hidden");
    if (ImGui::BeginPopupContextItem()) {
        ctx.uiSelected = id;
        if (ImGui::MenuItem(n->visible ? "Hide" : "Show")) n->visible = !n->visible;
        if (ImGui::MenuItem(n->enabled ? "Disable" : "Enable")) n->enabled = !n->enabled;
        if (ImGui::MenuItem("Destroy")) {
            ui.Destroy(id);
            ctx.uiSelected = 0;
            ImGui::EndPopup();
            if (open) ImGui::TreePop();
            ImGui::PopID();
            return;
        }
        ImGui::EndPopup();
    }
    if (open) {
        const std::vector<UiId> kids = n->children;
        for (const UiId c : kids) DrawNodeTree(ctx, ui, c);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void UiPanel::DrawProperties(EditorContext& ctx, UiSystem& ui, UiNode& n) {
    (void)ctx;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6.0f, 2.0f));
    if (ImGui::BeginTable("props", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX)) {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 96.0f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

        EditString("Name", n.name);
        EditBool("Visible", n.visible);
        EditBool("Enabled", n.enabled);
        EditBool("Clip", n.clip);
        bool block = n.BlocksPointer();
        if (EditBool("Blocks pointer", block)) n.block = block ? 1 : 0;

        // Placement.
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::kTextFaint);
        ImGui::TextUnformatted("PLACEMENT");
        ImGui::PopStyleColor();
        EditVec2("Anchor min", n.anchorMin, 0.01f);
        EditVec2("Anchor max", n.anchorMax, 0.01f);
        EditVec2("Pivot", n.pivot, 0.01f);
        EditVec2("Position", n.pos);
        EditVec2("Size (-1 auto)", n.size);
        EditVec2("Margin min", n.marginMin);
        EditVec2("Margin max", n.marginMax);
        EditFloat("Grow", n.grow, 0.05f, 0.0f, 100.0f);
        Row("Result");
        ImGui::TextColored(ui::kTextDim, "%.0f, %.0f   %.0f x %.0f", n.rect.x, n.rect.y, n.rect.w, n.rect.h);

        // Layout.
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::kTextFaint);
        ImGui::TextUnformatted("LAYOUT");
        ImGui::PopStyleColor();
        static const char* const layouts[] = {"None", "Row", "Column", "Grid"};
        static const char* const justify[] = {"Start", "Center", "End", "Space between", "Space around"};
        static const char* const items[] = {"Start", "Center", "End", "Stretch"};
        EditEnum("Layout", n.layout, layouts, 4);
        EditVec2("Gap", n.spacing);
        glm::vec2 padH(n.padding.x, n.padding.z), padV(n.padding.y, n.padding.w);
        if (EditVec2("Padding L / R", padH)) {
            n.padding.x = padH.x;
            n.padding.z = padH.y;
        }
        if (EditVec2("Padding T / B", padV)) {
            n.padding.y = padV.x;
            n.padding.w = padV.y;
        }
        EditEnum("Justify", n.justify, justify, 5);
        EditEnum("Items", n.items, items, 4);
        if (n.layout == UiLayout::Grid) {
            Row("Columns");
            ImGui::DragInt("##columns", &n.columns, 0.1f, 1, 64);
        }

        // Look.
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::kTextFaint);
        ImGui::TextUnformatted("LOOK");
        ImGui::PopStyleColor();
        EditFloat("Opacity", n.opacity, 0.01f, 0.0f, 1.0f);
        EditFloat("Scale", n.scale, 0.01f, 0.0f, 20.0f);
        EditFloat("Rotation", n.rotation, 0.5f);
        EditColor("Background", n.style.base.bg);
        EditColor("Border", n.style.base.border);
        {
            float width = n.style.base.borderWidth.value_or(0.0f);
            if (EditFloat("Border width", width, 0.1f, 0.0f, 64.0f)) n.style.base.borderWidth = width;
            float radius = n.style.base.radius.value_or(0.0f);
            if (EditFloat("Radius", radius, 0.1f, 0.0f, 200.0f)) n.style.base.radius = radius;
        }
        if (n.kind == UiKind::Text || n.kind == UiKind::Button || n.kind == UiKind::Toggle) EditColor("Text color", n.style.base.text);
        if (n.kind == UiKind::Image || !n.image.empty()) EditColor("Tint", n.style.base.tint);
        if (n.kind == UiKind::Progress || n.kind == UiKind::Slider || n.kind == UiKind::Toggle) EditColor("Fill", n.style.base.fill);
        if (n.kind == UiKind::Slider || n.kind == UiKind::Toggle || n.kind == UiKind::Scroll) EditColor("Handle", n.style.base.handle);
        EditString("Image", n.image);
        if (!n.image.empty()) {
            Row("Slice L T R B");
            ImGui::DragFloat4("##slice", &n.slice.x, 0.5f, 0.0f, 4096.0f, "%.0f");
        }

        // Content.
        if (n.kind == UiKind::Text || n.kind == UiKind::Button || n.kind == UiKind::Toggle) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushStyleColor(ImGuiCol_Text, ui::kTextFaint);
            ImGui::TextUnformatted("TEXT");
            ImGui::PopStyleColor();
            EditString("Text", n.text);
            EditString("Font", n.font);
            EditFloat("Font size", n.fontSize, 0.25f, 1.0f, 512.0f);
            static const char* const aligns[] = {"Left", "Center", "Right"};
            EditEnum("Align", n.textAlign, aligns, 3);
            EditBool("Wrap", n.wrap);
        }
        if (n.kind == UiKind::Slider || n.kind == UiKind::Progress || n.kind == UiKind::Toggle) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushStyleColor(ImGuiCol_Text, ui::kTextFaint);
            ImGui::TextUnformatted("VALUE");
            ImGui::PopStyleColor();
            float value = n.value;
            if (EditFloat("Value", value, 0.01f)) ui.SetValue(n.id, value, false);
            if (n.kind != UiKind::Toggle) {
                EditFloat("Min", n.minValue, 0.05f);
                EditFloat("Max", n.maxValue, 0.05f);
                EditFloat("Step", n.step, 0.05f, 0.0f, 1000.0f);
                EditBool("Vertical", n.vertical);
            }
        }
        if (n.kind == UiKind::Scroll) {
            EditVec2("Scroll", n.scroll);
            Row("Content");
            ImGui::TextColored(ui::kTextDim, "%.0f x %.0f", n.contentSize.x, n.contentSize.y);
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
}

void UiPanel::Draw(EditorContext& ctx) {
    if (ctx.detailsDock) ImGui::SetNextWindowDockID(ctx.detailsDock, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("UI")) {
        ImGui::End();
        return;
    }
    UiSystem* ui = ctx.activeUi;
    if (!ui) {
        ImGui::TextDisabled("no UI yet");
        ImGui::End();
        return;
    }
    if (!ui->Get(ctx.uiSelected)) ctx.uiSelected = 0;

    const float height = ImGui::GetContentRegionAvail().y;
    ImGui::PushStyleColor(ImGuiCol_Text, ui::kTextFaint);
    ImGui::Text("%s   %zu node(s)", ctx.playing ? "RUNNING GAME" : "PREVIEW", ui->NodeCount());
    ImGui::PopStyleColor();
    if (ui->Canvases().empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("Add a Canvas actor (Place Actors > UI) and set its Document to a file in Content/UI, or build a UI from a script with ui.panel{...}.");
    }

    if (ImGui::BeginChild("tree", ImVec2(0, std::max(height * 0.38f, 80.0f)), ImGuiChildFlags_ResizeY)) {
        const std::vector<UiCanvas> canvases = ui->Canvases(); // a copy: drawing may destroy nodes
        for (auto it = canvases.rbegin(); it != canvases.rend(); ++it) {
            ImGui::PushID(it->name.c_str());
            const bool open = ImGui::TreeNodeEx("##canvas", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding, "%s", "");
            const ImVec2 min = ImGui::GetItemRectMin();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            char title[200];
            std::string shown = it->name;
            if (it->fromScene) { // a Canvas component: show the actor's name and the document
                if (const NameComponent* actorName = ctx.scene.TryGet<NameComponent>(it->actor)) shown = actorName->name;
            }
            std::snprintf(title, sizeof(title), "%s   order %d   x%.2f%s", shown.c_str(), it->order, it->scale, it->visible ? "" : "   hidden");
            dl->AddText(ImVec2(min.x + ImGui::GetTreeNodeToLabelSpacing(), min.y + ImGui::GetStyle().FramePadding.y), ui::U32(ui::kOrange), title);
            if (open) {
                if (const UiNode* root = ui->Get(it->root)) {
                    const std::vector<UiId> kids = root->children;
                    for (const UiId c : kids) DrawNodeTree(ctx, *ui, c);
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    if (UiNode* n = ui->Get(ctx.uiSelected)) {
        ImGui::Spacing();
        ImGui::TextColored(ui::kTextDim, "Edits are live and are not written to the document.");
        DrawProperties(ctx, *ui, *n);
    }
    ImGui::End();
}

} // namespace magnum::editor
