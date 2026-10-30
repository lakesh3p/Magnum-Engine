#include "panels/ConsolePanel.h"

#include <imgui.h>

namespace magnum::editor {

void ConsolePanel::Draw(const char* title) {
    if (!ImGui::Begin(title)) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Clear")) {
        Log::Clear();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Info", &m_showInfo);
    ImGui::SameLine();
    ImGui::Checkbox("Warn", &m_showWarn);
    ImGui::SameLine();
    ImGui::Checkbox("Error", &m_showError);
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &m_autoScroll);

    const uint64_t revision = Log::Revision();
    if (revision != m_revision) {
        m_entries = Log::Snapshot();
        m_revision = revision;
    }

    const float footer = ImGui::GetFrameHeightWithSpacing() + 2.0f;
    if (ImGui::BeginChild("##log", ImVec2(0, -footer), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
        for (const LogEntry& e : m_entries) {
            ImVec4 color;
            switch (e.level) {
            case LogLevel::Info:
                if (!m_showInfo) continue;
                color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
                break;
            case LogLevel::Warn:
                if (!m_showWarn) continue;
                color = ImVec4(1.0f, 0.78f, 0.25f, 1.0f);
                break;
            case LogLevel::Error:
                if (!m_showError) continue;
                color = ImVec4(1.0f, 0.38f, 0.35f, 1.0f);
                break;
            }
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(e.text.c_str());
            ImGui::PopStyleColor();
        }
        if (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();

    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##cmd", "Command (type 'help')", m_input, sizeof(m_input),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (m_input[0] != '\0' && onCommand) {
            onCommand(m_input);
        }
        m_input[0] = '\0';
        ImGui::SetKeyboardFocusHere(-1);
    }

    ImGui::End();
}

} // namespace magnum::editor
