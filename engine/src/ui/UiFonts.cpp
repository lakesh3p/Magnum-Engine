#include "ui/UiFonts.h"

#include "core/Log.h"
#include "core/Paths.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <map>

namespace magnum {

uint32_t UiNextCodepoint(const std::string& s, size_t& i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    auto continuation = [&](size_t k) { return k < s.size() && (static_cast<unsigned char>(s[k]) & 0xC0) == 0x80; };
    if (c < 0x80) {
        ++i;
        return c;
    }
    if ((c & 0xE0) == 0xC0 && continuation(i + 1)) {
        const uint32_t cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
        i += 2;
        return cp;
    }
    if ((c & 0xF0) == 0xE0 && continuation(i + 1) && continuation(i + 2)) {
        const uint32_t cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6) | (static_cast<unsigned char>(s[i + 2]) & 0x3Fu);
        i += 3;
        return cp;
    }
    if ((c & 0xF8) == 0xF0 && continuation(i + 1) && continuation(i + 2) && continuation(i + 3)) {
        const uint32_t cp = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 12) |
                            ((static_cast<unsigned char>(s[i + 2]) & 0x3Fu) << 6) | (static_cast<unsigned char>(s[i + 3]) & 0x3Fu);
        i += 4;
        return cp;
    }
    ++i;
    return 0xFFFD;
}

namespace {

struct FontFile {
    std::string path;
    std::string failedWith; // the folders in effect when loading failed: a font is tried again once they change
    std::vector<unsigned char> data;
    stbtt_fontinfo info{};
    bool ok = false;
};

// A font at one pixel size: its glyph table and the atlas they were drawn into.
struct SizedFont {
    int atlas = -1;
    float ascent = 0.0f, descent = 0.0f, lineGap = 0.0f;
    std::unordered_map<uint32_t, stbtt_packedchar> chars;
    int atlasWidth = 0, atlasHeight = 0;
};

const char* const kAliases[][2] = {
    {"sans", "Engine/Fonts/IBMPlexSans-Regular.ttf"},
    {"sansbold", "Engine/Fonts/IBMPlexSans-SemiBold.ttf"},
    {"bold", "Engine/Fonts/IBMPlexSans-SemiBold.ttf"},
    {"mono", "Engine/Fonts/JetBrainsMono-Regular.ttf"},
};

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Code points baked into every atlas (those the font really has): ASCII, Latin-1, Latin Extended-A, Cyrillic, general
// punctuation, currency, arrows, a few math and geometric symbols.
std::vector<int> WantedCodepoints(const stbtt_fontinfo& info, int pixelSize) {
    struct Range {
        int first, last;
    };
    static const Range ranges[] = {{32, 126},      {160, 255},       {0x400, 0x4FF},   {0x2010, 0x2027}, {0x100, 0x17F},   {0x2030, 0x203A},
                                   {0x20A0, 0x20BF}, {0x2190, 0x2199}, {0x2212, 0x2212}, {0x2022, 0x2022}, {0x25A0, 0x25CF}, {0x2713, 0x2714},
                                   {0x2605, 0x2606}};
    std::vector<int> out;
    // Big sizes (titles) only need the everyday glyphs: the atlas grows with the square of the size.
    const size_t used = pixelSize >= 56 ? 4 : sizeof(ranges) / sizeof(ranges[0]);
    for (size_t index = 0; index < used; ++index) {
        const Range& r = ranges[index];
        for (int cp = r.first; cp <= r.last; ++cp) {
            if (stbtt_FindGlyphIndex(&info, cp) != 0) out.push_back(cp);
        }
    }
    return out;
}

} // namespace

struct UiFonts::Impl {
    std::vector<std::unique_ptr<FontFile>> files;
    std::map<std::string, int> byName;
    std::map<std::pair<int, int>, SizedFont> sized; // (font, pixel size)
    std::vector<UiAtlas> atlases;

    int Load(const std::string& relPath) {
        const std::string roots = EngineRoot().generic_string() + "|" + ProjectRoot().generic_string();
        FontFile* file = nullptr;
        int index = -1;
        for (size_t i = 0; i < files.size(); ++i) {
            if (files[i]->path != relPath) continue;
            if (files[i]->ok) return static_cast<int>(i);
            if (files[i]->failedWith == roots) return -1; // already tried from here
            file = files[i].get();
            index = static_cast<int>(i);
        }
        if (!file) {
            files.push_back(std::make_unique<FontFile>());
            file = files.back().get();
            file->path = relPath;
            index = static_cast<int>(files.size()) - 1;
        }
        std::ifstream in(ResolveAsset(ProjectRoot(), relPath), std::ios::binary);
        if (in) {
            file->data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            if (!file->data.empty() && stbtt_InitFont(&file->info, file->data.data(), stbtt_GetFontOffsetForIndex(file->data.data(), 0))) file->ok = true;
        }
        if (!file->ok) {
            file->failedWith = roots;
            Log::Warn("UI font '{}' could not be loaded", relPath);
            return -1;
        }
        return index;
    }

    SizedFont& Sized(int font, int pixelSize) {
        const auto key = std::make_pair(font, pixelSize);
        if (const auto it = sized.find(key); it != sized.end()) return it->second;
        SizedFont& s = sized[key];
        FontFile& f = *files[static_cast<size_t>(font)];
        const float scale = stbtt_ScaleForMappingEmToPixels(&f.info, static_cast<float>(pixelSize)); // sizes are em sizes, like CSS px
        int ascent = 0, descent = 0, gap = 0;
        stbtt_GetFontVMetrics(&f.info, &ascent, &descent, &gap);
        s.ascent = static_cast<float>(ascent) * scale;
        s.descent = static_cast<float>(descent) * scale;
        s.lineGap = static_cast<float>(gap) * scale;

        const std::vector<int> codepoints = WantedCodepoints(f.info, pixelSize);
        std::vector<stbtt_packedchar> packed(codepoints.size());
        std::vector<unsigned char> pixels;
        // The smallest atlas the glyphs fit into: 256 x 256, 512 x 256, 512 x 512 ... up to 2048 x 2048.
        int width = 256, height = 256;
        bool done = false;
        for (int attempt = 0; attempt < 7 && !done; ++attempt) {
            width = 256 << ((attempt + 1) / 2);
            height = 256 << (attempt / 2);
            pixels.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
            stbtt_pack_context ctx;
            if (!stbtt_PackBegin(&ctx, pixels.data(), width, height, 0, 1, nullptr)) break;
            stbtt_PackSetOversampling(&ctx, 1, 1);
            stbtt_pack_range range{};
            range.font_size = STBTT_POINT_SIZE(static_cast<float>(pixelSize)); // negative = the em square is this many pixels
            range.first_unicode_codepoint_in_range = 0;
            range.array_of_unicode_codepoints = const_cast<int*>(codepoints.data());
            range.num_chars = static_cast<int>(codepoints.size());
            range.chardata_for_range = packed.data();
            done = stbtt_PackFontRanges(&ctx, f.data.data(), 0, &range, 1) != 0;
            stbtt_PackEnd(&ctx);
        }
        if (pixels.size() != static_cast<size_t>(width) * static_cast<size_t>(height)) { // PackBegin failed: an empty atlas of the size we claim
            pixels.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
        }
        if (!done) Log::Warn("UI font atlas for '{}' at {} px does not fit, some glyphs are missing", f.path, pixelSize);
        s.atlasWidth = width;
        s.atlasHeight = height;
        for (size_t i = 0; i < codepoints.size(); ++i) s.chars[static_cast<uint32_t>(codepoints[i])] = packed[i];

        UiAtlas atlas;
        atlas.id = static_cast<int>(atlases.size());
        atlas.width = width;
        atlas.height = height;
        atlas.pixels = std::move(pixels);
        atlas.version = 1;
        s.atlas = atlas.id;
        atlases.push_back(std::move(atlas));
        return s;
    }
};

UiFonts::UiFonts() : m_impl(std::make_unique<Impl>()) {}
UiFonts::~UiFonts() = default;

UiFonts& UiFonts::Get() {
    static UiFonts instance;
    return instance;
}

void UiFonts::Clear() {
    m_impl = std::make_unique<Impl>();
}

size_t UiFonts::AtlasCount() const {
    return m_impl->atlases.size();
}

const UiAtlas* UiFonts::Atlas(int id) const {
    return id >= 0 && id < static_cast<int>(m_impl->atlases.size()) ? &m_impl->atlases[static_cast<size_t>(id)] : nullptr;
}

int UiFonts::FontId(const std::string& name) {
    const std::string key = Lower(name.empty() ? "sans" : name);
    if (const auto it = m_impl->byName.find(key); it != m_impl->byName.end()) return it->second;
    std::string path = name;
    for (const auto& alias : kAliases) {
        if (key == alias[0]) path = alias[1];
    }
    int id = m_impl->Load(path);
    if (id < 0 && key != "sans") id = m_impl->Load(kAliases[0][1]); // a missing font falls back to the default one
    if (id >= 0) m_impl->byName[key] = id; // a failure is not remembered: the folders may be set a moment later
    return id;
}

float UiFonts::LineHeight(int font, int pixelSize) {
    if (font < 0) return static_cast<float>(pixelSize) * 1.3f;
    const SizedFont& s = m_impl->Sized(font, std::max(pixelSize, 1));
    return s.ascent - s.descent + s.lineGap;
}

UiTextLayout UiFonts::Layout(int font, int pixelSize, const std::string& text, float maxWidth, UiTextAlign align, float lineHeightMul) {
    UiTextLayout out;
    if (font < 0 || text.empty()) {
        out.lines = text.empty() ? 0 : 1;
        return out;
    }
    pixelSize = std::clamp(pixelSize, 1, 512);
    SizedFont& sized = m_impl->Sized(font, pixelSize);
    out.atlas = sized.atlas;
    const float lineStep = (sized.ascent - sized.descent + sized.lineGap) * std::max(lineHeightMul, 0.1f);

    // Split into lines (explicit breaks, then greedy word wrap) remembering each line's code points.
    struct Glyph {
        const stbtt_packedchar* ch = nullptr;
        uint32_t cp = 0;
        float advance = 0.0f;
    };
    auto lookup = [&](uint32_t cp) -> const stbtt_packedchar* {
        auto it = sized.chars.find(cp);
        if (it == sized.chars.end()) it = sized.chars.find('?');
        return it == sized.chars.end() ? nullptr : &it->second;
    };
    std::vector<std::vector<Glyph>> lines(1);
    float lineWidth = 0.0f;
    std::vector<float> widths(1, 0.0f);
    size_t i = 0;
    auto breakLine = [&] {
        widths.back() = lineWidth;
        lines.emplace_back();
        widths.push_back(0.0f);
        lineWidth = 0.0f;
    };
    while (i < text.size()) {
        const uint32_t cp = UiNextCodepoint(text, i);
        if (cp == '\r') continue;
        if (cp == '\n') {
            breakLine();
            continue;
        }
        Glyph g;
        g.cp = cp == '\t' ? ' ' : cp;
        g.ch = lookup(g.cp);
        g.advance = g.ch ? g.ch->xadvance : static_cast<float>(pixelSize) * 0.5f;
        if (cp == '\t') g.advance *= 4.0f;
        if (maxWidth > 0.0f && lineWidth + g.advance > maxWidth && g.cp != ' ' && !lines.back().empty()) {
            // Wrap at the last space of this line when there is one, else in the middle of the word.
            size_t split = lines.back().size();
            for (size_t k = lines.back().size(); k > 0; --k) {
                if (lines.back()[k - 1].cp == ' ') {
                    split = k;
                    break;
                }
            }
            std::vector<Glyph> carry;
            if (split < lines.back().size()) {
                carry.assign(lines.back().begin() + static_cast<std::ptrdiff_t>(split), lines.back().end());
                lines.back().resize(split);
            }
            // Trailing spaces do not count towards the line's width.
            while (!lines.back().empty() && lines.back().back().cp == ' ') lines.back().pop_back();
            float w = 0.0f;
            for (const Glyph& l : lines.back()) w += l.advance;
            widths.back() = w;
            lines.emplace_back(carry);
            float carried = 0.0f;
            for (const Glyph& c : carry) carried += c.advance;
            widths.push_back(0.0f);
            lineWidth = carried;
        }
        lines.back().push_back(g);
        lineWidth += g.advance;
    }
    widths.back() = lineWidth;

    float widest = 0.0f;
    for (size_t l = 0; l < lines.size(); ++l) {
        // A line's trailing spaces are not visible and must not shift centered text.
        float w = 0.0f;
        size_t last = lines[l].size();
        while (last > 0 && lines[l][last - 1].cp == ' ') --last;
        for (size_t k = 0; k < last; ++k) w += lines[l][k].advance;
        widths[l] = w;
        widest = std::max(widest, w);
    }
    const float boxWidth = maxWidth > 0.0f ? std::max(maxWidth, widest) : widest;

    const float invW = 1.0f / static_cast<float>(sized.atlasWidth);
    const float invH = 1.0f / static_cast<float>(sized.atlasHeight);
    for (size_t l = 0; l < lines.size(); ++l) {
        float pen = 0.0f;
        if (align == UiTextAlign::Center) pen = (boxWidth - widths[l]) * 0.5f;
        else if (align == UiTextAlign::Right) pen = boxWidth - widths[l];
        const float baseline = static_cast<float>(l) * lineStep + sized.ascent;
        for (const Glyph& g : lines[l]) {
            if (g.ch && g.cp != ' ' && g.ch->x1 > g.ch->x0) {
                UiGlyphQuad q;
                q.x0 = pen + g.ch->xoff;
                q.y0 = baseline + g.ch->yoff;
                q.x1 = pen + g.ch->xoff2;
                q.y1 = baseline + g.ch->yoff2;
                q.u0 = static_cast<float>(g.ch->x0) * invW;
                q.v0 = static_cast<float>(g.ch->y0) * invH;
                q.u1 = static_cast<float>(g.ch->x1) * invW;
                q.v1 = static_cast<float>(g.ch->y1) * invH;
                out.glyphs.push_back(q);
            }
            pen += g.advance;
        }
    }
    out.lines = static_cast<int>(lines.size());
    out.size = {boxWidth, static_cast<float>(lines.size()) * lineStep};
    return out;
}

} // namespace magnum
