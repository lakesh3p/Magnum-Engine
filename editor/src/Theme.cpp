#include "Theme.h"

#include <imgui_freetype.h>

#include <filesystem>
#include <string>

namespace magnum::editor {

namespace {

ImFont* g_fonts[4] = {nullptr, nullptr, nullptr, nullptr};

ImFont* Load(const char* path, float size, const ImWchar* ranges) {
    if (!std::filesystem::exists(path)) return nullptr;
    ImFontConfig config;
    config.OversampleH = 1; // FreeType positions glyphs exactly; oversampling would only blur
    config.OversampleV = 1;
    config.PixelSnapH = false;
    config.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_LightHinting;
    return ImGui::GetIO().Fonts->AddFontFromFileTTF(path, size, &config, ranges);
}

} // namespace

void LoadFonts(float pixelSize) {
    ImGuiIO& io = ImGui::GetIO();
    const ImWchar* ranges = io.Fonts->GetGlyphRangesCyrillic(); // Latin + Cyrillic
    // IBM Plex Sans and JetBrains Mono from Engine/Fonts (OFL, licenses next to them). If they are missing the
    // Windows system fonts take over, then ImGui's built-in font.
    const std::string dir = std::string(MAGNUM_ENGINE_DIR) + "/Engine/Fonts/";
    auto pick = [](const std::string& preferred, const char* fallback) {
        return std::filesystem::exists(preferred) ? preferred : std::string(fallback);
    };
    g_fonts[0] = Load(pick(dir + "IBMPlexSans-Regular.ttf", "C:/Windows/Fonts/segoeui.ttf").c_str(), pixelSize, ranges);
    const std::string semibold = pick(dir + "IBMPlexSans-SemiBold.ttf", "C:/Windows/Fonts/seguisb.ttf");
    g_fonts[1] = Load(semibold.c_str(), pixelSize, ranges);
    g_fonts[2] = Load(semibold.c_str(), pixelSize * 1.3f, ranges);
    g_fonts[3] = Load(pick(dir + "JetBrainsMono-Regular.ttf", "C:/Windows/Fonts/CascadiaMono.ttf").c_str(), pixelSize - 0.5f, ranges);
    if (!g_fonts[0]) { // not on Windows (or a stripped install): ImGui's built-in font keeps everything working
        g_fonts[0] = io.Fonts->AddFontDefault();
    }
    for (ImFont*& f : g_fonts) {
        if (!f) f = g_fonts[0];
    }
    io.FontDefault = g_fonts[0];
}

ImFont* GetFont(UiFont font) {
    return g_fonts[static_cast<int>(font)];
}

void ApplyTheme() {
    using namespace ui;
    ImGuiStyle& s = ImGui::GetStyle();

    // Shapes: panels are square and flush, controls have small radii.
    s.WindowRounding = 0.0f;
    s.ChildRounding = 4.0f;
    s.PopupRounding = 6.0f;
    s.FrameRounding = 4.0f;
    s.ScrollbarRounding = 4.0f;
    s.GrabRounding = 7.0f;
    s.TabRounding = 5.0f;
    s.WindowBorderSize = 0.0f;
    s.ChildBorderSize = 1.0f;
    s.PopupBorderSize = 1.0f;
    s.FrameBorderSize = 1.0f;
    s.TabBorderSize = 0.0f;
    s.TabBarBorderSize = 1.0f;
    s.TabBarOverlineSize = 2.0f;
    s.DockingSeparatorSize = 1.0f; // the 1px line between panels
    s.SeparatorTextBorderSize = 1.0f;

    // Dense, like a tool: rows about 24px high.
    s.WindowPadding = ImVec2(10, 8);
    s.FramePadding = ImVec2(8, 3);
    s.ItemSpacing = ImVec2(8, 5);
    s.ItemInnerSpacing = ImVec2(6, 4);
    s.CellPadding = ImVec2(6, 3);
    s.IndentSpacing = 14.0f;
    s.ScrollbarSize = 10.0f;
    s.GrabMinSize = 12.0f;
    s.WindowMenuButtonPosition = ImGuiDir_None;
    s.SeparatorTextPadding = ImVec2(20, 4);

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = kText;
    c[ImGuiCol_TextDisabled] = kTextDim;
    c[ImGuiCol_WindowBg] = kPanel;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = kPanel;
    c[ImGuiCol_Border] = kBorder;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = kInput;
    c[ImGuiCol_FrameBgHovered] = Hex(0x141414);
    c[ImGuiCol_FrameBgActive] = Hex(0x181818);
    c[ImGuiCol_TitleBg] = kTab;
    c[ImGuiCol_TitleBgActive] = kTab;
    c[ImGuiCol_TitleBgCollapsed] = kTab;
    c[ImGuiCol_MenuBarBg] = kBase;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = Hex(0x3a3a3a);
    c[ImGuiCol_ScrollbarGrabHovered] = Hex(0x4a4a4a);
    c[ImGuiCol_ScrollbarGrabActive] = kAccent;
    c[ImGuiCol_CheckMark] = kAccent;
    c[ImGuiCol_SliderGrab] = Hex(0xf4f0e8);
    c[ImGuiCol_SliderGrabActive] = Hex(0xffffff);
    c[ImGuiCol_Button] = kHover;
    c[ImGuiCol_ButtonHovered] = kActive;
    c[ImGuiCol_ButtonActive] = kAccent;
    c[ImGuiCol_Header] = kRaised;
    c[ImGuiCol_HeaderHovered] = kHover;
    c[ImGuiCol_HeaderActive] = kHover;
    c[ImGuiCol_Separator] = kBorder;
    c[ImGuiCol_SeparatorHovered] = kAccent;
    c[ImGuiCol_SeparatorActive] = kAccent;
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.4f);
    c[ImGuiCol_ResizeGripActive] = kAccent;
    c[ImGuiCol_Tab] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabHovered] = kHover;
    c[ImGuiCol_TabSelected] = kPanel;
    c[ImGuiCol_TabSelectedOverline] = kAccent;
    c[ImGuiCol_TabDimmed] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabDimmedSelected] = kPanel;
    c[ImGuiCol_TabDimmedSelectedOverline] = kTextFaint;
    c[ImGuiCol_DockingPreview] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.30f);
    c[ImGuiCol_DockingEmptyBg] = kBase;
    c[ImGuiCol_TableHeaderBg] = kRaised;
    c[ImGuiCol_TableBorderStrong] = kBorder;
    c[ImGuiCol_TableBorderLight] = ImVec4(1, 1, 1, 0.05f);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.02f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.40f);
    c[ImGuiCol_DragDropTarget] = kAccentHover;
    c[ImGuiCol_NavCursor] = kAccentHover;
    c[ImGuiCol_NavWindowingHighlight] = ImVec4(1, 1, 1, 0.5f);
    c[ImGuiCol_NavWindowingDimBg] = ImVec4(0, 0, 0, 0.45f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);
}

} // namespace magnum::editor
