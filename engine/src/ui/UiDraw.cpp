#include "ui/UiSystem.h"

#include <algorithm>
#include <cmath>

namespace magnum {

struct UiSystem::DrawContext {
    UiDrawList& list;
    float scale = 1.0f;
    bool clipped = false;
    int clip[4] = {0, 0, 0, 0};

    explicit DrawContext(UiDrawList& l) : list(l) {}

    int ImageIndex(const std::string& path) {
        for (size_t i = 0; i < list.images.size(); ++i) {
            if (list.images[i] == path) return static_cast<int>(i);
        }
        list.images.push_back(path);
        return static_cast<int>(list.images.size()) - 1;
    }

    UiDrawCommand& Command(int image, int atlas, bool pixelated) {
        if (!list.commands.empty()) {
            UiDrawCommand& last = list.commands.back();
            if (last.image == image && last.atlas == atlas && last.pixelated == pixelated && last.clipped == clipped &&
                (!clipped || (last.clip[0] == clip[0] && last.clip[1] == clip[1] && last.clip[2] == clip[2] && last.clip[3] == clip[3]))) {
                return last;
            }
        }
        UiDrawCommand c;
        c.image = image;
        c.atlas = atlas;
        c.pixelated = pixelated;
        c.clipped = clipped;
        std::copy(std::begin(clip), std::end(clip), std::begin(c.clip));
        c.firstIndex = static_cast<uint32_t>(list.indices.size());
        list.commands.push_back(c);
        return list.commands.back();
    }

    // One quad: four corners in pixels (top left, top right, bottom right, bottom left).
    void Quad(const glm::vec2 corner[4], const glm::vec2 uv[4], const glm::vec4& color, const glm::vec4& shape, const glm::vec4& border,
              const glm::vec2 local[4], float mode, float soft, int image, int atlas, bool pixelated) {
        if (color.a <= 0.0f && border.a <= 0.0f) return;
        UiDrawCommand& cmd = Command(image, atlas, pixelated);
        const uint32_t base = static_cast<uint32_t>(list.vertices.size());
        for (int i = 0; i < 4; ++i) {
            UiVertex v;
            v.pos = corner[i];
            v.uv = uv[i];
            v.color = color;
            v.shape = shape;
            v.border = border;
            v.local = local[i];
            v.misc = {mode, soft};
            list.vertices.push_back(v);
        }
        for (const uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) list.indices.push_back(base + i);
        cmd.indexCount += 6;
    }
};

namespace {

constexpr float kThumbRadius = 3.0f;

glm::vec4 Faded(glm::vec4 c, float opacity) {
    c.a *= opacity;
    return c;
}

// The transform from a node's design units to view pixels.
struct Space {
    const UiAffine* world;
    float scale;
    glm::vec2 ToPixels(glm::vec2 p) const { return world->Apply(p) * scale; }
    float Zoom() const { return glm::length(world->x) * scale; } // how much a length grows (uniform scales)
    bool Snaps() const { return world->IsAxisAligned(); }
};

// A rounded rectangle (optionally textured), as one quad.
void RoundedRect(UiSystem::DrawContext& dc, const Space& sp, const UiRect& r, const glm::vec4& color, const glm::vec4& border,
                 float borderWidth, float radius, float expand = 0.0f, float soft = 0.0f, int image = -1, bool pixelated = false,
                 const glm::vec4& uvRect = glm::vec4(0, 0, 1, 1)) {
    if (r.w <= 0.0f || r.h <= 0.0f) return;
    glm::vec2 c[4] = {sp.ToPixels({r.x, r.y}), sp.ToPixels({r.Right(), r.y}), sp.ToPixels({r.Right(), r.Bottom()}), sp.ToPixels({r.x, r.Bottom()})};
    const float zoom = sp.Zoom();
    glm::vec2 half(r.w * 0.5f * zoom, r.h * 0.5f * zoom);
    if (sp.Snaps()) { // whole pixels keep edges and borders crisp
        const float x0 = std::round(c[0].x), y0 = std::round(c[0].y), x1 = std::max(std::round(c[2].x), x0 + 1.0f), y1 = std::max(std::round(c[2].y), y0 + 1.0f);
        c[0] = {x0, y0};
        c[1] = {x1, y0};
        c[2] = {x1, y1};
        c[3] = {x0, y1};
        half = {(x1 - x0) * 0.5f, (y1 - y0) * 0.5f};
    }
    const float grow = expand * zoom + soft;
    if (grow != 0.0f) {
        const glm::vec2 dir[4] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        // Corners move outward along the rectangle's own axes (so rotated rectangles grow correctly).
        const glm::vec2 ax = glm::normalize(c[1] - c[0]), ay = glm::normalize(c[3] - c[0]);
        for (int i = 0; i < 4; ++i) c[i] += ax * (dir[i].x * grow) + ay * (dir[i].y * grow);
    }
    const glm::vec2 shownHalf = half + glm::vec2(expand * zoom);
    const glm::vec2 localHalf = shownHalf + glm::vec2(soft);
    const glm::vec2 local[4] = {{-localHalf.x, -localHalf.y}, {localHalf.x, -localHalf.y}, {localHalf.x, localHalf.y}, {-localHalf.x, localHalf.y}};
    // uvRect is x, y, w, h in texture space with v up already.
    const glm::vec2 uv[4] = {{uvRect.x, uvRect.y + uvRect.w}, {uvRect.x + uvRect.z, uvRect.y + uvRect.w}, {uvRect.x + uvRect.z, uvRect.y}, {uvRect.x, uvRect.y}};
    const float rad = std::min(std::max((radius + expand) * zoom, 0.0f), std::min(shownHalf.x, shownHalf.y));
    dc.Quad(c, uv, color, glm::vec4(shownHalf.x, shownHalf.y, rad, borderWidth * zoom), border, local, kUiShape, soft, image, -1, pixelated);
}

// Converts a rectangle of an image given from the top left (x, y, w, h in 0..1) to texture space (v up).
glm::vec4 TextureRect(float x, float y, float w, float h) {
    return glm::vec4(x, 1.0f - (y + h), w, h);
}

} // namespace

void UiSystem::DrawNode(DrawContext& dc, UiNode& n) {
    if (!n.visible || n.worldOpacity <= 0.001f) return;
    const Space sp{&n.world, dc.scale};
    const float opacity = n.worldOpacity;
    const UiVisual& v = n.visual;
    const UiRect r = n.rect;

    // Clip: children (and the node itself) are cut at the rectangle's bounding box.
    const bool savedClipped = dc.clipped;
    int savedClip[4];
    std::copy(std::begin(dc.clip), std::end(dc.clip), std::begin(savedClip));
    if (n.clip || n.kind == UiKind::Scroll) {
        const glm::vec2 a = sp.ToPixels({r.x, r.y}), b = sp.ToPixels({r.Right(), r.y}), c = sp.ToPixels({r.Right(), r.Bottom()}), d = sp.ToPixels({r.x, r.Bottom()});
        const float x0 = std::min({a.x, b.x, c.x, d.x}), x1 = std::max({a.x, b.x, c.x, d.x});
        const float y0 = std::min({a.y, b.y, c.y, d.y}), y1 = std::max({a.y, b.y, c.y, d.y});
        int box[4] = {static_cast<int>(std::floor(x0)), static_cast<int>(std::floor(y0)), static_cast<int>(std::ceil(x1)), static_cast<int>(std::ceil(y1))};
        if (dc.clipped) {
            box[0] = std::max(box[0], dc.clip[0]);
            box[1] = std::max(box[1], dc.clip[1]);
            box[2] = std::min(box[2], dc.clip[0] + dc.clip[2]);
            box[3] = std::min(box[3], dc.clip[1] + dc.clip[3]);
        }
        if (box[2] <= box[0] || box[3] <= box[1]) return; // nothing of it is visible
        dc.clipped = true;
        dc.clip[0] = box[0];
        dc.clip[1] = box[1];
        dc.clip[2] = box[2] - box[0];
        dc.clip[3] = box[3] - box[1];
    }

    auto drawImage = [&](const UiRect& dest, const std::string& path, const glm::vec4& tint, float radius) {
        if (path.empty() || tint.a <= 0.0f) return;
        glm::vec2 size(0.0f);
        const bool known = imageSize && imageSize(path, size) && size.x > 0.0f && size.y > 0.0f;
        const int image = dc.ImageIndex(path);
        glm::vec4 uv = n.uv; // from the top left
        const bool sliced = known && (n.slice.x > 0.0f || n.slice.y > 0.0f || n.slice.z > 0.0f || n.slice.w > 0.0f);
        if (sliced) {
            // 9-slice: the corners keep their size, the edges and the middle stretch. Borders are in source pixels.
            const float sx = uv.x * size.x, sy = uv.y * size.y, sw = uv.z * size.x, sh = uv.w * size.y;
            float l = std::min(n.slice.x, sw * 0.5f), t = std::min(n.slice.y, sh * 0.5f), rr = std::min(n.slice.z, sw * 0.5f), b = std::min(n.slice.w, sh * 0.5f);
            // On a small rectangle the borders shrink together.
            const float k = std::min({1.0f, dest.w / std::max(l + rr, 1.0f), dest.h / std::max(t + b, 1.0f)});
            const float dl = l * k, dt = t * k, dr = rr * k, db = b * k;
            const float dx[4] = {dest.x, dest.x + dl, dest.Right() - dr, dest.Right()};
            const float dy[4] = {dest.y, dest.y + dt, dest.Bottom() - db, dest.Bottom()};
            const float su[4] = {sx, sx + l, sx + sw - rr, sx + sw};
            const float sv[4] = {sy, sy + t, sy + sh - b, sy + sh};
            for (int row = 0; row < 3; ++row) {
                for (int col = 0; col < 3; ++col) {
                    const UiRect cell{dx[col], dy[row], dx[col + 1] - dx[col], dy[row + 1] - dy[row]};
                    if (cell.w <= 0.0f || cell.h <= 0.0f) continue;
                    const float u0 = su[col] / size.x, u1 = su[col + 1] / size.x, v0 = sv[row] / size.y, v1 = sv[row + 1] / size.y;
                    RoundedRect(dc, sp, cell, tint, glm::vec4(0.0f), 0.0f, 0.0f, 0.0f, 0.0f, image, n.pixelated, TextureRect(u0, v0, u1 - u0, v1 - v0));
                }
            }
            return;
        }
        UiRect target = dest;
        if (known && n.fit != UiFit::Stretch) {
            const float imageAspect = (size.x * uv.z) / std::max(size.y * uv.w, 1.0f), boxAspect = dest.w / std::max(dest.h, 1.0f);
            if (n.fit == UiFit::Contain) {
                if (imageAspect > boxAspect) {
                    target.h = dest.w / imageAspect;
                    target.y = dest.y + (dest.h - target.h) * 0.5f;
                } else {
                    target.w = dest.h * imageAspect;
                    target.x = dest.x + (dest.w - target.w) * 0.5f;
                }
            } else { // Cover: keep the box, crop the picture
                if (imageAspect > boxAspect) {
                    const float keep = boxAspect / imageAspect; // fraction of the width that stays
                    uv.x += uv.z * (1.0f - keep) * 0.5f;
                    uv.z *= keep;
                } else {
                    const float keep = imageAspect / boxAspect;
                    uv.y += uv.w * (1.0f - keep) * 0.5f;
                    uv.w *= keep;
                }
            }
        }
        RoundedRect(dc, sp, target, tint, glm::vec4(0.0f), 0.0f, radius, 0.0f, 0.0f, image, n.pixelated, TextureRect(uv.x, uv.y, uv.z, uv.w));
    };

    auto drawText = [&](const UiRect& box, const glm::vec4& color, bool centerVertically) {
        if (n.text.empty() || color.a <= 0.0f) return;
        const bool wrapped = n.wrap && box.w > 0.0f;
        const UiTextLayout& layout = TextLayoutFor(n, wrapped ? box.w : 0.0f, dc.scale);
        if (layout.glyphs.empty() || layout.atlas < 0) return;
        const glm::vec2 sizeUnits = layout.size / dc.scale;
        float ox = box.x, oy = box.y;
        if (!wrapped) {
            if (n.textAlign == UiTextAlign::Center) ox = box.x + (box.w - sizeUnits.x) * 0.5f;
            else if (n.textAlign == UiTextAlign::Right) ox = box.Right() - sizeUnits.x;
        }
        if (centerVertically) oy = box.y + (box.h - sizeUnits.y) * 0.5f;

        auto emit = [&](glm::vec2 shift, const glm::vec4& col) {
            const bool snap = sp.Snaps();
            const glm::vec2 originPx = sp.ToPixels({ox + shift.x, oy + shift.y});
            const glm::vec2 originSnapped = snap ? glm::vec2(std::round(originPx.x), std::round(originPx.y)) : originPx;
            const float zoom = sp.Zoom() / dc.scale; // node scale on top of the canvas scale
            for (const UiGlyphQuad& g : layout.glyphs) {
                glm::vec2 corner[4];
                if (snap) {
                    // Glyph rectangles in pixels from the snapped origin, so every glyph stays on whole pixels.
                    const float x0 = std::round(originSnapped.x + g.x0 * zoom), y0 = std::round(originSnapped.y + g.y0 * zoom);
                    const float x1 = x0 + std::max(std::round((g.x1 - g.x0) * zoom), 1.0f), y1 = y0 + std::max(std::round((g.y1 - g.y0) * zoom), 1.0f);
                    corner[0] = {x0, y0};
                    corner[1] = {x1, y0};
                    corner[2] = {x1, y1};
                    corner[3] = {x0, y1};
                } else {
                    const glm::vec2 a(ox + shift.x + g.x0 / dc.scale, oy + shift.y + g.y0 / dc.scale);
                    const glm::vec2 b(ox + shift.x + g.x1 / dc.scale, oy + shift.y + g.y1 / dc.scale);
                    corner[0] = sp.ToPixels({a.x, a.y});
                    corner[1] = sp.ToPixels({b.x, a.y});
                    corner[2] = sp.ToPixels({b.x, b.y});
                    corner[3] = sp.ToPixels({a.x, b.y});
                }
                const glm::vec2 uv[4] = {{g.u0, g.v0}, {g.u1, g.v0}, {g.u1, g.v1}, {g.u0, g.v1}};
                const glm::vec2 local[4] = {};
                dc.Quad(corner, uv, col, glm::vec4(0.0f), glm::vec4(0.0f), local, kUiText, 0.0f, -1, layout.atlas, false);
            }
        };
        if (n.textShadow.enabled) emit(n.textShadow.offset, Faded(n.textShadow.color, opacity * color.a));
        emit(glm::vec2(0.0f), Faded(color, opacity));
    };

    auto drawBackground = [&] {
        if (n.style.shadow.enabled && v.bg.a > 0.0f) {
            const UiShadow& s = n.style.shadow;
            const UiRect shadowRect{r.x + s.offset.x, r.y + s.offset.y, r.w, r.h};
            RoundedRect(dc, sp, shadowRect, Faded(s.color, opacity * v.bg.a), glm::vec4(0.0f), 0.0f, v.radius, s.spread, std::max(s.blur, 0.5f) * sp.Zoom());
        }
        if (v.bg.a > 0.0f || (v.border.a > 0.0f && v.borderWidth > 0.0f)) {
            RoundedRect(dc, sp, r, Faded(v.bg, opacity), Faded(v.border, opacity), v.borderWidth, v.radius);
        }
    };

    const UiRect inner{r.x + n.padding.x, r.y + n.padding.y, std::max(r.w - n.padding.x - n.padding.z, 0.0f), std::max(r.h - n.padding.y - n.padding.w, 0.0f)};

    switch (n.kind) {
    case UiKind::Panel:
    case UiKind::Scroll:
        drawBackground();
        if (!v.image.empty()) drawImage(r, v.image, Faded(v.tint, opacity), v.radius);
        break;
    case UiKind::Image:
        drawBackground();
        drawImage(r, v.image, Faded(v.tint, opacity), v.radius);
        break;
    case UiKind::Text: drawText(inner, v.text, false); break;
    case UiKind::Button:
        drawBackground();
        if (!v.image.empty()) drawImage(r, v.image, Faded(v.tint, opacity), v.radius);
        drawText(inner, v.text, true);
        break;
    case UiKind::Toggle: {
        const bool on = n.value > 0.5f;
        const bool sw = n.look == UiToggleLook::Switch;
        const float bw = sw ? 34.0f : 18.0f, bh = 18.0f;
        const UiRect box{r.x, r.y + (r.h - bh) * 0.5f, bw, bh};
        if (sw) {
            const glm::vec4 track = on ? v.fill : v.bg;
            RoundedRect(dc, sp, box, Faded(track, opacity), Faded(v.border, opacity), v.borderWidth, bh * 0.5f);
            const float knob = bh - 6.0f;
            const float kx = on ? box.Right() - 3.0f - knob : box.x + 3.0f;
            RoundedRect(dc, sp, {kx, box.y + 3.0f, knob, knob}, Faded(v.handle, opacity), glm::vec4(0.0f), 0.0f, knob * 0.5f);
        } else {
            RoundedRect(dc, sp, box, Faded(on ? v.fill : v.bg, opacity), Faded(on ? glm::vec4(0.0f) : v.border, opacity), v.borderWidth, v.radius);
            if (on) RoundedRect(dc, sp, {box.x + 5.0f, box.y + 5.0f, bh - 10.0f, bh - 10.0f}, Faded(v.handle, opacity), glm::vec4(0.0f), 0.0f, 2.0f);
        }
        if (!n.text.empty()) drawText({r.x + bw + 8.0f, r.y, std::max(r.w - bw - 8.0f, 0.0f), r.h}, v.text, true);
        break;
    }
    case UiKind::Slider: {
        const float span = std::max(n.maxValue - n.minValue, 1e-6f);
        const float t = std::clamp((n.value - n.minValue) / span, 0.0f, 1.0f);
        const float pad = std::min((n.vertical ? r.w : r.h) * 0.5f, 10.0f);
        const float thick = 6.0f;
        UiRect track, fill;
        glm::vec2 knobCenter;
        if (n.vertical) {
            const float cx = r.x + r.w * 0.5f;
            track = {cx - thick * 0.5f, r.y + pad, thick, std::max(r.h - 2.0f * pad, 0.0f)};
            const float ky = track.Bottom() - t * track.h;
            fill = {track.x, ky, thick, track.Bottom() - ky};
            knobCenter = {cx, ky};
        } else {
            const float cy = r.y + r.h * 0.5f;
            track = {r.x + pad, cy - thick * 0.5f, std::max(r.w - 2.0f * pad, 0.0f), thick};
            const float kx = track.x + t * track.w;
            fill = {track.x, track.y, kx - track.x, thick};
            knobCenter = {kx, cy};
        }
        RoundedRect(dc, sp, track, Faded(v.bg, opacity), glm::vec4(0.0f), 0.0f, v.radius);
        if (fill.w > 0.0f && fill.h > 0.0f) RoundedRect(dc, sp, fill, Faded(v.fill, opacity), glm::vec4(0.0f), 0.0f, v.radius);
        const float knob = pad * 2.0f * 0.8f;
        RoundedRect(dc, sp, {knobCenter.x - knob * 0.5f, knobCenter.y - knob * 0.5f, knob, knob}, Faded(v.handle, opacity),
                    Faded(glm::vec4(0.0f, 0.0f, 0.0f, 0.35f), opacity), 1.0f, knob * 0.5f);
        break;
    }
    case UiKind::Progress: {
        const float span = std::max(n.maxValue - n.minValue, 1e-6f);
        const float t = std::clamp((n.value - n.minValue) / span, 0.0f, 1.0f);
        RoundedRect(dc, sp, r, Faded(v.bg, opacity), Faded(v.border, opacity), v.borderWidth, v.radius);
        const float bw = v.borderWidth;
        UiRect fill{r.x + bw, r.y + bw, std::max(r.w - 2.0f * bw, 0.0f), std::max(r.h - 2.0f * bw, 0.0f)};
        if (n.vertical) {
            const float h = fill.h * t;
            fill.y = fill.Bottom() - h;
            fill.h = h;
        } else {
            fill.w *= t;
        }
        if (fill.w > 0.0f && fill.h > 0.0f) RoundedRect(dc, sp, fill, Faded(v.fill, opacity), glm::vec4(0.0f), 0.0f, std::max(v.radius - bw, 0.0f));
        break;
    }
    }

    for (const UiId id : n.children) {
        if (UiNode* c = Get(id)) DrawNode(dc, *c);
    }

    if (n.kind == UiKind::Scroll) {
        const UiRect thumb = ScrollThumb(n);
        if (thumb.w > 0.0f) {
            glm::vec4 color = v.handle;
            if (n.hovered || m_scrollDrag == n.id) color.a = std::min(1.0f, color.a * 1.7f);
            RoundedRect(dc, sp, thumb, Faded(color, opacity), glm::vec4(0.0f), 0.0f, kThumbRadius);
        }
    }

    dc.clipped = savedClipped;
    std::copy(std::begin(savedClip), std::end(savedClip), std::begin(dc.clip));
}

void UiSystem::BuildDrawList(UiDrawList& out) {
    out.Clear();
    out.viewWidth = m_viewWidth;
    out.viewHeight = m_viewHeight;
    for (UiCanvas& canvas : m_canvases) {
        if (!canvas.visible) continue;
        UiNode* root = Get(canvas.root);
        if (!root) continue;
        DrawContext dc(out);
        dc.scale = canvas.scale;
        m_layoutScale = canvas.scale;
        DrawNode(dc, *root);
    }
}

} // namespace magnum
