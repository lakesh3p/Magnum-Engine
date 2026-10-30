#pragma once

#include "ui/UiTypes.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace magnum {

// One glyph to draw: a rectangle in pixels, relative to the top left of the text box, and the part of the font atlas
// that holds its picture (0..1).
struct UiGlyphQuad {
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
};

struct UiTextLayout {
    std::vector<UiGlyphQuad> glyphs;
    glm::vec2 size{0.0f}; // pixels
    int lines = 0;
    int atlas = -1;       // which font atlas the glyphs come from
};

// A single-channel picture of the glyphs of one font at one pixel size. The renderer uploads it when `version` changes.
struct UiAtlas {
    int id = -1;
    int width = 0, height = 0;
    std::vector<uint8_t> pixels;
    uint32_t version = 0;
};

// Fonts for the UI, rasterized with stb_truetype: Latin, Cyrillic, punctuation and a few symbols. Fonts are looked up by
// name ("Sans", "SansBold", "Mono") or by a project path to a .ttf file, and baked into an atlas per pixel size the first
// time they are used. Text layout needs no graphics context, so it can be measured anywhere.
class UiFonts {
public:
    static UiFonts& Get();

    // The id of a font ("" = Sans). Falls back to Sans when the file cannot be read; -1 only when no font file exists
    // at all (a broken installation).
    int FontId(const std::string& name);

    // Lays text out at `pixelSize` (the height of the em square in pixels). `maxWidth` > 0 wraps words to it and aligns
    // lines inside it; 0 keeps every line as long as it is. `lineHeight` multiplies the font's natural line spacing.
    UiTextLayout Layout(int font, int pixelSize, const std::string& text, float maxWidth, UiTextAlign align, float lineHeight);

    const UiAtlas* Atlas(int id) const;
    size_t AtlasCount() const;

    // Line height and ascent in pixels of a font at a size (for placing icons next to text).
    float LineHeight(int font, int pixelSize);

    // Forgets every loaded font and atlas (tests; a changed project).
    void Clear();

private:
    UiFonts();
    ~UiFonts();
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// Decodes the next UTF-8 code point and advances `i`; invalid bytes give U+FFFD.
uint32_t UiNextCodepoint(const std::string& s, size_t& i);

} // namespace magnum
