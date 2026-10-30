#include "ui/UiSystem.h"

#include <algorithm>
#include <cmath>

namespace magnum {

// The glyphs of a node's text laid out at one pixel size and width. Layouts are cached on the node and rebuilt only
// when the text, the font, the size or the width changes.
struct UiTextCache {
    bool valid = false;
    std::string text, font;
    int pixel = 0;
    float maxWidth = 0.0f;
    UiTextAlign align = UiTextAlign::Left;
    float lineHeight = 1.0f;
    UiTextLayout layout;
};

namespace {

float Clamp(float v, float lo, float hi) { return std::min(std::max(v, lo), hi); }

} // namespace

const UiTextLayout& UiSystem::TextLayoutFor(UiNode& n, float maxWidthUnits, float scale) {
    const int pixel = std::max(1, static_cast<int>(std::lround(n.fontSize * scale)));
    const float maxPx = maxWidthUnits > 0.0f ? maxWidthUnits * scale : 0.0f;
    if (!n.textCache) n.textCache = std::make_shared<UiTextCache>();
    UiTextCache& c = *n.textCache;
    if (c.valid && c.text == n.text && c.font == n.font && c.pixel == pixel && c.maxWidth == maxPx && c.align == n.textAlign &&
        c.lineHeight == n.lineHeight) {
        return c.layout;
    }
    UiFonts& fonts = UiFonts::Get();
    c.layout = fonts.Layout(fonts.FontId(n.font), pixel, n.text, maxPx, n.textAlign, n.lineHeight);
    c.text = n.text;
    c.font = n.font;
    c.pixel = pixel;
    c.maxWidth = maxPx;
    c.align = n.textAlign;
    c.lineHeight = n.lineHeight;
    c.valid = true;
    return c.layout;
}

glm::vec2 UiSystem::MeasureText(UiNode& n, float maxWidth, float scale) {
    if (n.text.empty()) return glm::vec2(0.0f);
    const UiTextLayout& layout = TextLayoutFor(n, maxWidth, scale);
    return layout.size / scale;
}

// ---- preferred sizes ---------------------------------------------------------------------------------------------------

namespace {

constexpr float kDefaultSliderLength = 160.0f, kSliderThickness = 20.0f;
constexpr float kProgressLength = 200.0f, kProgressThickness = 10.0f;
constexpr float kToggleBox = 18.0f, kToggleGap = 8.0f;

} // namespace

// The size a node's content needs (children, label, image) without padding or explicit sizes.
glm::vec2 UiSystem::ContentSize(UiNode& n, glm::vec2 available) {
    const float scale = m_layoutScale;
    const glm::vec2 inner(std::max(available.x - n.padding.x - n.padding.z, 0.0f), std::max(available.y - n.padding.y - n.padding.w, 0.0f));
    glm::vec2 content(0.0f);

    switch (n.kind) {
    case UiKind::Text: {
        const float wrapWidth = n.wrap ? (n.size.x >= 0.0f ? n.size.x - n.padding.x - n.padding.z : inner.x) : 0.0f;
        content = MeasureText(n, wrapWidth, scale);
        break;
    }
    case UiKind::Image: {
        glm::vec2 natural(32.0f);
        if (!n.image.empty() && imageSize) {
            glm::vec2 px;
            if (imageSize(n.image, px)) natural = px * glm::vec2(n.uv.z, n.uv.w);
        }
        content = natural;
        break;
    }
    case UiKind::Slider:
        content = n.vertical ? glm::vec2(kSliderThickness, kDefaultSliderLength) : glm::vec2(kDefaultSliderLength, kSliderThickness);
        break;
    case UiKind::Progress:
        content = n.vertical ? glm::vec2(kProgressThickness, kProgressLength) : glm::vec2(kProgressLength, kProgressThickness);
        break;
    case UiKind::Toggle: {
        const glm::vec2 box = n.look == UiToggleLook::Switch ? glm::vec2(34.0f, kToggleBox) : glm::vec2(kToggleBox);
        const glm::vec2 label = MeasureText(n, 0.0f, scale);
        content = {box.x + (n.text.empty() ? 0.0f : kToggleGap + label.x), std::max(box.y, label.y)};
        break;
    }
    case UiKind::Button: content = MeasureText(n, 0.0f, scale); break;
    default: break;
    }

    // Children.
    std::vector<UiNode*> flow, other;
    for (const UiId id : n.children) {
        UiNode* c = Get(id);
        if (!c || !c->visible) continue;
        ((n.layout == UiLayout::None || c->ignoreLayout) ? other : flow).push_back(c);
    }
    glm::vec2 childrenSize(0.0f);
    if (!flow.empty()) {
        std::vector<glm::vec2> pref;
        pref.reserve(flow.size());
        for (UiNode* c : flow) pref.push_back(Preferred(*c, inner));
        if (n.layout == UiLayout::Row || n.layout == UiLayout::Column) {
            const int m = n.layout == UiLayout::Row ? 0 : 1, x = 1 - m;
            float main = n.spacing[m] * static_cast<float>(flow.size() - 1), cross = 0.0f;
            for (const glm::vec2& p : pref) {
                main += p[m];
                cross = std::max(cross, p[x]);
            }
            childrenSize[m] = main;
            childrenSize[x] = cross;
        } else { // Grid
            const int cols = std::max(n.columns, 1);
            const int rows = (static_cast<int>(flow.size()) + cols - 1) / cols;
            float cellW = n.cellSize.x;
            if (cellW <= 0.0f) {
                for (const glm::vec2& p : pref) cellW = std::max(cellW, p.x);
            }
            float height = n.spacing.y * static_cast<float>(rows - 1);
            for (int r = 0; r < rows; ++r) {
                float rowH = n.cellSize.y;
                if (rowH <= 0.0f) {
                    for (int k = r * cols; k < std::min(static_cast<int>(pref.size()), (r + 1) * cols); ++k) rowH = std::max(rowH, pref[static_cast<size_t>(k)].y);
                }
                height += rowH;
            }
            childrenSize = {cellW * static_cast<float>(cols) + n.spacing.x * static_cast<float>(cols - 1), height};
        }
    }
    for (UiNode* c : other) { // placed by anchors: what reaches furthest right / down counts
        if (c->anchorMin != glm::vec2(0.0f) || c->anchorMax != glm::vec2(0.0f)) continue; // stretched or anchored away: no say
        const glm::vec2 p = Preferred(*c, inner);
        childrenSize = glm::max(childrenSize, glm::max(c->pos, glm::vec2(0.0f)) + p);
    }
    return glm::max(content, childrenSize);
}

glm::vec2 UiSystem::Preferred(UiNode& n, glm::vec2 available) {
    glm::vec2 size = ContentSize(n, available) + glm::vec2(n.padding.x + n.padding.z, n.padding.y + n.padding.w);
    if (n.kind == UiKind::Scroll) { // a scroll view takes what it is given instead of growing with its content
        size = glm::min(size, available);
    }
    for (int a = 0; a < 2; ++a) {
        if (n.size[a] >= 0.0f) size[a] = n.size[a];
        size[a] = Clamp(size[a], n.minSize[a], n.maxSize[a]);
    }
    return size;
}

UiRect UiSystem::AnchoredRect(UiNode& n, const UiRect& parent) {
    const float pmin[2] = {parent.x, parent.y}, psize[2] = {parent.w, parent.h};
    float lo[2], size[2];
    bool needPreferred = false;
    for (int a = 0; a < 2; ++a) needPreferred = needPreferred || n.anchorMin[a] == n.anchorMax[a];
    const glm::vec2 pref = needPreferred ? Preferred(n, {parent.w, parent.h}) : glm::vec2(0.0f);
    for (int a = 0; a < 2; ++a) {
        if (n.anchorMin[a] == n.anchorMax[a]) {
            size[a] = pref[a];
            const float anchorPoint = pmin[a] + n.anchorMin[a] * psize[a];
            lo[a] = anchorPoint + n.pos[a] - n.pivot[a] * size[a];
        } else {
            const float from = pmin[a] + n.anchorMin[a] * psize[a] + n.marginMin[a];
            const float to = pmin[a] + n.anchorMax[a] * psize[a] - n.marginMax[a];
            size[a] = Clamp(std::max(to - from, 0.0f), n.minSize[a], n.maxSize[a]);
            lo[a] = from + n.pos[a];
        }
    }
    return {lo[0], lo[1], size[0], size[1]};
}

// ---- placing nodes -----------------------------------------------------------------------------------------------------

void UiSystem::LayoutNode(UiNode& n, const UiRect& rect, const UiAffine& parentWorld, float parentOpacity, bool parentEnabled) {
    n.rect = rect;
    UiAffine local;
    if (n.scale != 1.0f || n.rotation != 0.0f) {
        const glm::vec2 pivotPoint(rect.x + n.pivot.x * rect.w, rect.y + n.pivot.y * rect.h);
        const float radians = glm::radians(n.rotation);
        const float c = std::cos(radians) * n.scale, s = std::sin(radians) * n.scale;
        local.x = {c, s};
        local.y = {-s, c};
        local.t = pivotPoint - (local.x * pivotPoint.x + local.y * pivotPoint.y);
    }
    n.world = local.Then(parentWorld);
    n.worldOpacity = parentOpacity * std::clamp(n.opacity, 0.0f, 1.0f);
    n.worldEnabled = parentEnabled && n.enabled;
    if (n.visible) LayoutChildren(n);
}

void UiSystem::LayoutChildren(UiNode& p) {
    std::vector<UiNode*> flow, placed;
    for (const UiId id : p.children) {
        UiNode* c = Get(id);
        if (!c || !c->visible) continue;
        ((p.layout == UiLayout::None || c->ignoreLayout) ? placed : flow).push_back(c);
    }
    const bool scrolls = p.kind == UiKind::Scroll;
    const UiRect content{p.rect.x + p.padding.x, p.rect.y + p.padding.y, std::max(p.rect.w - p.padding.x - p.padding.z, 0.0f),
                         std::max(p.rect.h - p.padding.y - p.padding.w, 0.0f)};
    float needed[2] = {0.0f, 0.0f}; // what the flowing children occupy (scroll views)

    if (!flow.empty() && (p.layout == UiLayout::Row || p.layout == UiLayout::Column)) {
        const int m = p.layout == UiLayout::Row ? 0 : 1, x = 1 - m;
        const float origin[2] = {content.x, content.y}, extent[2] = {content.w, content.h};
        std::vector<glm::vec2> pref;
        pref.reserve(flow.size());
        float growSum = 0.0f;
        float total = p.spacing[m] * static_cast<float>(flow.size() - 1);
        for (UiNode* c : flow) {
            glm::vec2 s = Preferred(*c, {content.w, content.h});
            const UiItems align = c->alignSelf.value_or(p.items);
            if (align == UiItems::Stretch && c->size[x] < 0.0f) s[x] = Clamp(extent[x], c->minSize[x], c->maxSize[x]);
            pref.push_back(s);
            total += s[m];
            growSum += std::max(c->grow, 0.0f);
        }
        needed[m] = total;
        needed[x] = extent[x];
        float virtualMain = extent[m];
        float scrollOffset = 0.0f;
        if (scrolls) {
            virtualMain = std::max(extent[m], total);
            const float maxScroll = std::max(0.0f, total + p.padding[m] + p.padding[m + 2] - (m == 0 ? p.rect.w : p.rect.h));
            p.scroll[m] = Clamp(p.scroll[m], 0.0f, maxScroll);
            scrollOffset = p.scroll[m];
            p.scroll[x] = 0.0f;
        }
        float freeSpace = virtualMain - total;
        float lead = 0.0f, gap = p.spacing[m];
        if (freeSpace > 0.0f && growSum > 0.0f) {
            for (size_t i = 0; i < flow.size(); ++i) {
                pref[i][m] = Clamp(pref[i][m] + freeSpace * std::max(flow[i]->grow, 0.0f) / growSum, flow[i]->minSize[m], flow[i]->maxSize[m]);
            }
        } else if (freeSpace > 0.0f) {
            const float n = static_cast<float>(flow.size());
            switch (p.justify) {
            case UiJustify::Start: break;
            case UiJustify::Center: lead = freeSpace * 0.5f; break;
            case UiJustify::End: lead = freeSpace; break;
            case UiJustify::SpaceBetween:
                if (flow.size() > 1) gap += freeSpace / (n - 1.0f);
                break;
            case UiJustify::SpaceAround:
                gap += freeSpace / n;
                lead = freeSpace / (2.0f * n);
                break;
            }
        }
        float cursor = origin[m] + lead - scrollOffset;
        for (size_t i = 0; i < flow.size(); ++i) {
            UiNode* c = flow[i];
            const UiItems align = c->alignSelf.value_or(p.items);
            float crossPos = origin[x];
            if (align == UiItems::Center) crossPos += (extent[x] - pref[i][x]) * 0.5f;
            else if (align == UiItems::End) crossPos += extent[x] - pref[i][x];
            float lo[2], size[2];
            lo[m] = cursor;
            lo[x] = crossPos;
            size[m] = pref[i][m];
            size[x] = pref[i][x];
            UiRect r{lo[0] + c->pos.x, lo[1] + c->pos.y, size[0], size[1]};
            LayoutNode(*c, r, p.world, p.worldOpacity, p.worldEnabled);
            cursor += pref[i][m] + gap;
        }
    } else if (!flow.empty() && p.layout == UiLayout::Grid) {
        const int cols = std::max(p.columns, 1);
        const int count = static_cast<int>(flow.size());
        const int rows = (count + cols - 1) / cols;
        const float cellW = p.cellSize.x > 0.0f ? p.cellSize.x : std::max((content.w - p.spacing.x * static_cast<float>(cols - 1)) / static_cast<float>(cols), 0.0f);
        std::vector<glm::vec2> pref;
        pref.reserve(flow.size());
        for (UiNode* c : flow) pref.push_back(Preferred(*c, {cellW, content.h}));
        std::vector<float> rowH(static_cast<size_t>(rows), p.cellSize.y);
        if (p.cellSize.y <= 0.0f) {
            for (int i = 0; i < count; ++i) rowH[static_cast<size_t>(i / cols)] = std::max(rowH[static_cast<size_t>(i / cols)], pref[static_cast<size_t>(i)].y);
        }
        float total = p.spacing.y * static_cast<float>(rows - 1);
        for (const float h : rowH) total += h;
        needed[0] = cellW * static_cast<float>(cols) + p.spacing.x * static_cast<float>(cols - 1);
        needed[1] = total;
        float scrollY = 0.0f;
        if (scrolls) {
            const float maxScroll = std::max(0.0f, total + p.padding.y + p.padding.w - p.rect.h);
            p.scroll.y = Clamp(p.scroll.y, 0.0f, maxScroll);
            p.scroll.x = 0.0f;
            scrollY = p.scroll.y;
        }
        float y = content.y - scrollY;
        for (int r = 0; r < rows; ++r) {
            for (int col = 0; col < cols; ++col) {
                const int i = r * cols + col;
                if (i >= count) break;
                UiNode* c = flow[static_cast<size_t>(i)];
                const float cx = content.x + static_cast<float>(col) * (cellW + p.spacing.x);
                const UiItems align = c->alignSelf.value_or(p.items);
                float w = std::min(pref[static_cast<size_t>(i)].x, cellW), h = std::min(pref[static_cast<size_t>(i)].y, rowH[static_cast<size_t>(r)]);
                if (align == UiItems::Stretch) {
                    if (c->size.x < 0.0f) w = cellW;
                    if (c->size.y < 0.0f) h = rowH[static_cast<size_t>(r)];
                }
                float px = cx, py = y;
                if (align == UiItems::Center) {
                    px += (cellW - w) * 0.5f;
                    py += (rowH[static_cast<size_t>(r)] - h) * 0.5f;
                } else if (align == UiItems::End) {
                    px += cellW - w;
                    py += rowH[static_cast<size_t>(r)] - h;
                }
                LayoutNode(*c, {px + c->pos.x, py + c->pos.y, w, h}, p.world, p.worldOpacity, p.worldEnabled);
            }
            y += rowH[static_cast<size_t>(r)] + p.spacing.y;
        }
    }

    // Children placed by their anchors. In a scroll view they move with the content.
    if (!placed.empty()) {
        UiRect anchorParent = content; // anchors measure against the padded area
        if (scrolls && p.layout == UiLayout::None) {
            p.scroll = glm::max(p.scroll, glm::vec2(0.0f));
            anchorParent.x -= p.scroll.x;
            anchorParent.y -= p.scroll.y;
        }
        for (UiNode* c : placed) {
            const UiRect r = AnchoredRect(*c, anchorParent);
            LayoutNode(*c, r, p.world, p.worldOpacity, p.worldEnabled);
            if (scrolls && p.layout == UiLayout::None) {
                needed[0] = std::max(needed[0], r.Right() - p.rect.x + p.scroll.x + p.padding.z);
                needed[1] = std::max(needed[1], r.Bottom() - p.rect.y + p.scroll.y + p.padding.w);
            }
        }
    }

    if (scrolls) {
        if (p.layout == UiLayout::None) {
            p.contentSize = {needed[0], needed[1]};
            p.scroll.x = Clamp(p.scroll.x, 0.0f, std::max(0.0f, p.contentSize.x - p.rect.w));
            p.scroll.y = Clamp(p.scroll.y, 0.0f, std::max(0.0f, p.contentSize.y - p.rect.h));
        } else {
            p.contentSize = {needed[0] + p.padding.x + p.padding.z, needed[1] + p.padding.y + p.padding.w};
        }
    }
}

// ---- canvases -----------------------------------------------------------------------------------------------------------

void UiSystem::LayoutCanvas(UiCanvas& c) {
    const float vw = static_cast<float>(m_viewWidth), vh = static_cast<float>(m_viewHeight);
    float scale = 1.0f;
    const glm::vec2 ref = glm::max(c.reference, glm::vec2(1.0f));
    switch (c.scaleMode) {
    case UiScaleMode::Constant: scale = 1.0f; break;
    case UiScaleMode::Height: scale = vh / ref.y; break;
    case UiScaleMode::Width: scale = vw / ref.x; break;
    case UiScaleMode::Fit: scale = std::min(vw / ref.x, vh / ref.y); break;
    case UiScaleMode::Fill: scale = std::max(vw / ref.x, vh / ref.y); break;
    }
    c.scale = std::max(scale, 0.05f);
    c.size = {vw / c.scale, vh / c.scale};
    m_layoutScale = c.scale;
    UiNode* root = Get(c.root);
    if (!root) return;
    root->anchorMin = root->anchorMax = glm::vec2(0.0f);
    LayoutNode(*root, {0.0f, 0.0f, c.size.x, c.size.y}, UiAffine{}, 1.0f, true);
}

void UiSystem::Layout() {
    for (UiCanvas& c : m_canvases) {
        if (c.visible) LayoutCanvas(c);
    }
}

} // namespace magnum
