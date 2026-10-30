#pragma once

#include <imgui.h>

namespace magnum::editor {

// Palette shared by the style and by custom-drawn widgets (icons, thumbnails, toolbars).
// Neutral greys with one blue accent; surfaces are told apart by 1px borders, not by gutters or shadows.
namespace ui {
constexpr ImVec4 Hex(unsigned rgb, float a = 1.0f) {
    return ImVec4(static_cast<float>((rgb >> 16) & 0xFF) / 255.0f, static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                  static_cast<float>(rgb & 0xFF) / 255.0f, a);
}

inline constexpr ImVec4 kBase = Hex(0x0e0e0e);      // menu bar, status bar, wells
inline constexpr ImVec4 kBg = Hex(0x151515);        // viewport and asset areas
inline constexpr ImVec4 kTab = Hex(0x171717);       // tab strips, side trees
inline constexpr ImVec4 kPanel = Hex(0x1c1c1c);     // panel background, toolbars
inline constexpr ImVec4 kRaised = Hex(0x232323);    // section headers
inline constexpr ImVec4 kInput = Hex(0x101010);     // text fields
inline constexpr ImVec4 kHover = Hex(0x2a2a2a);
inline constexpr ImVec4 kActive = Hex(0x303030);
inline constexpr ImVec4 kBorder = Hex(0x333333);
inline constexpr ImVec4 kInputBorder = Hex(0x3a3a3a);
inline constexpr ImVec4 kText = Hex(0xe4e4e4);
inline constexpr ImVec4 kTextSoft = Hex(0xd0d0d0);
inline constexpr ImVec4 kTextDim = Hex(0x9e9e9e);
inline constexpr ImVec4 kTextFaint = Hex(0x6a6a6a);
inline constexpr ImVec4 kAccent = Hex(0x2b9df7);
inline constexpr ImVec4 kAccentHover = Hex(0x4aaefa);
inline constexpr ImVec4 kOnAccent = Hex(0x04121f);  // text and icons drawn on the accent color
inline constexpr ImVec4 kSelection = Hex(0x12324f); // selected row / tile background
inline constexpr ImVec4 kGreen = Hex(0x7bc47f);
inline constexpr ImVec4 kRed = Hex(0xef6b6b);
inline constexpr ImVec4 kOrange = Hex(0xf08a3c);
inline constexpr ImVec4 kYellow = Hex(0xe9b44c);
inline constexpr ImVec4 kFolder = Hex(0xd9a441);
inline constexpr ImVec4 kBlue = Hex(0x9ec1f0);
inline constexpr ImVec4 kPurple = Hex(0xb78fdc);

inline ImU32 U32(const ImVec4& c, float alpha = 1.0f) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * alpha)); }
} // namespace ui

enum class UiFont { Regular, Semibold, Title, Mono };

// Loads the UI fonts rasterized by FreeType, with Latin and Cyrillic glyphs: Segoe UI Variable (Windows 11) or
// Segoe UI, and Cascadia Mono for numbers and code. Call once after ImGui::CreateContext() and before the renderer
// backend is initialized. Falls back to ImGui's font if the files are missing.
void LoadFonts(float pixelSize = 14.0f);
ImFont* GetFont(UiFont font);

// Flat dark style: sharp panels separated by 1px borders, small radii, one blue accent.
void ApplyTheme();

} // namespace magnum::editor
