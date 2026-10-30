#include "ui/UiSystem.h"

#include <algorithm>
#include <cmath>

namespace magnum {

namespace {

constexpr float kScrollStep = 48.0f;      // UI units per wheel notch
constexpr float kScrollbarWidth = 6.0f;
constexpr float kScrollbarMargin = 3.0f;
constexpr float kMinThumb = 24.0f;

float SliderPad(const UiNode& n) { return std::min((n.vertical ? n.rect.w : n.rect.h) * 0.5f, 10.0f); }

glm::vec4 Lighten(glm::vec4 c, float amount) {
    return glm::vec4(glm::mix(glm::vec3(c), glm::vec3(1.0f), amount), c.a);
}

glm::vec4 Darken(glm::vec4 c, float amount) {
    return glm::vec4(glm::mix(glm::vec3(c), glm::vec3(0.0f), amount), c.a);
}

} // namespace

// ---- hit testing -------------------------------------------------------------------------------------------------------

bool UiSystem::HitTest(UiNode& n, glm::vec2 point, UiId& out) {
    if (!n.visible) return false;
    const glm::vec2 local = n.world.IsIdentity() ? point : n.world.Inverse().Apply(point);
    const bool inside = n.rect.Contains(local);
    const bool clips = n.clip || n.kind == UiKind::Scroll;
    if (clips && !inside) return false; // children are cut at this rectangle, so nothing below can be hit either
    for (auto it = n.children.rbegin(); it != n.children.rend(); ++it) {
        UiNode* c = Get(*it);
        if (c && HitTest(*c, point, out)) return true;
    }
    if (inside && n.BlocksPointer() && n.opacity > 0.0f) {
        out = n.id;
        return true;
    }
    return false;
}

UiRect UiSystem::ScrollThumb(const UiNode& n) const {
    if (n.kind != UiKind::Scroll) return {};
    const bool horizontal = n.layout == UiLayout::Row;
    const float view = horizontal ? n.rect.w : n.rect.h;
    const float content = horizontal ? n.contentSize.x : n.contentSize.y;
    if (content <= view + 0.5f || view <= 0.0f) return {};
    const float thumb = std::max(kMinThumb, view * view / content);
    const float travel = view - thumb;
    const float offset = horizontal ? n.scroll.x : n.scroll.y;
    const float t = std::clamp(offset / std::max(content - view, 1.0f), 0.0f, 1.0f);
    if (horizontal) return {n.rect.x + t * travel, n.rect.Bottom() - kScrollbarWidth - kScrollbarMargin, thumb, kScrollbarWidth};
    return {n.rect.Right() - kScrollbarWidth - kScrollbarMargin, n.rect.y + t * travel, kScrollbarWidth, thumb};
}

void UiSystem::SliderFromPointer(UiNode& n, glm::vec2 point) {
    const glm::vec2 local = n.world.IsIdentity() ? point : n.world.Inverse().Apply(point);
    const float pad = SliderPad(n);
    float t;
    if (n.vertical) {
        const float length = std::max(n.rect.h - 2.0f * pad, 1.0f);
        t = 1.0f - (local.y - (n.rect.y + pad)) / length; // the minimum is at the bottom
    } else {
        const float length = std::max(n.rect.w - 2.0f * pad, 1.0f);
        t = (local.x - (n.rect.x + pad)) / length;
    }
    t = std::clamp(t, 0.0f, 1.0f);
    SetValue(n.id, n.minValue + t * (n.maxValue - n.minValue), true);
}

// ---- the pointer ---------------------------------------------------------------------------------------------------------

void UiSystem::Interact(const UiPointer& pointer, float /*dt*/) {
    // What is under the pointer: the topmost canvas that has something blocking there gets it.
    UiId hit = 0;
    if (pointer.inView) {
        for (auto it = m_canvases.rbegin(); it != m_canvases.rend(); ++it) {
            if (!it->visible) continue;
            UiNode* root = Get(it->root);
            if (!root) continue;
            const glm::vec2 point = pointer.position / it->scale;
            UiId found = 0;
            if (HitTest(*root, point, found)) {
                hit = found;
                m_pointerDesign = point;
                break;
            }
        }
    }

    // Hover changes.
    if (hit != m_hit) {
        if (UiNode* old = Get(m_hit)) {
            old->hovered = false;
            RaiseEvent(UiEventType::Leave, old->id);
        }
        if (UiNode* now = Get(hit)) {
            now->hovered = true;
            RaiseEvent(UiEventType::Enter, now->id);
        }
        m_hit = hit;
    }

    const bool down = pointer.down[0];
    UiNode* captured = Get(m_captured);
    glm::vec2 capturedPoint = pointer.position;
    if (captured) {
        if (const UiCanvas* c = CanvasOf(m_captured)) capturedPoint = pointer.position / c->scale;
    }

    if (down && !m_wasDown) { // press
        UiNode* target = Get(hit);
        if (target && target->worldEnabled && target->IsInteractive()) {
            m_captured = hit;
            target->pressed = true;
            RaiseEvent(UiEventType::Press, hit);
            if (target->kind == UiKind::Slider) SliderFromPointer(*target, m_pointerDesign);
        } else if (target && target->kind == UiKind::Scroll && target->worldEnabled) {
            const glm::vec2 local = target->world.IsIdentity() ? m_pointerDesign : target->world.Inverse().Apply(m_pointerDesign);
            const UiRect thumb = ScrollThumb(*target);
            if (thumb.w > 0.0f && thumb.Contains(local)) {
                m_scrollDrag = hit;
                m_scrollGrab = target->layout == UiLayout::Row ? local.x - thumb.x : local.y - thumb.y;
            }
        }
    } else if (down && m_wasDown) { // held
        if (captured && captured->kind == UiKind::Slider && captured->worldEnabled) SliderFromPointer(*captured, capturedPoint);
        if (UiNode* s = Get(m_scrollDrag)) {
            const glm::vec2 local = s->world.IsIdentity() ? capturedPoint : s->world.Inverse().Apply(capturedPoint);
            const bool horizontal = s->layout == UiLayout::Row;
            const UiRect thumb = ScrollThumb(*s);
            const float view = horizontal ? s->rect.w : s->rect.h;
            const float content = horizontal ? s->contentSize.x : s->contentSize.y;
            const float travel = std::max(view - (horizontal ? thumb.w : thumb.h), 1.0f);
            const float along = (horizontal ? local.x - s->rect.x : local.y - s->rect.y) - m_scrollGrab;
            const float t = std::clamp(along / travel, 0.0f, 1.0f);
            (horizontal ? s->scroll.x : s->scroll.y) = t * std::max(content - view, 0.0f);
        }
    } else if (!down && m_wasDown) { // release
        if (captured) {
            captured->pressed = false;
            RaiseEvent(UiEventType::Release, captured->id);
            if (hit == m_captured && captured->worldEnabled) {
                if (captured->kind == UiKind::Toggle) SetValue(captured->id, captured->value > 0.5f ? 0.0f : 1.0f, true);
                RaiseEvent(UiEventType::Click, captured->id, captured->value);
            }
        }
        m_captured = 0;
        m_scrollDrag = 0;
    }
    m_wasDown = down;
    if (UiNode* c = Get(m_captured)) c->pressed = down;

    // The wheel scrolls the nearest scroll view under the pointer.
    if (pointer.wheel != 0.0f && hit) {
        for (UiNode* n = Get(hit); n; n = Get(n->parent)) {
            if (n->kind != UiKind::Scroll || !n->worldEnabled) continue;
            const bool horizontal = n->layout == UiLayout::Row;
            const float content = horizontal ? n->contentSize.x : n->contentSize.y;
            const float view = horizontal ? n->rect.w : n->rect.h;
            float& offset = horizontal ? n->scroll.x : n->scroll.y;
            offset = std::clamp(offset - pointer.wheel * kScrollStep, 0.0f, std::max(content - view, 0.0f));
            break;
        }
    }
}

// ---- looks ------------------------------------------------------------------------------------------------------------------

void UiSystem::UpdateVisuals(float dt) {
    for (auto& [id, owned] : m_nodes) {
        UiNode& n = *owned;
        UiLook look = n.style.base;
        const bool interactive = n.IsInteractive();
        const bool enabled = n.worldEnabled;
        if (n.kind == UiKind::Toggle && n.value > 0.5f) look.Merge(n.style.active);
        const bool hover = n.hovered && enabled && (interactive || n.block == 1);
        const bool press = n.pressed && n.hovered && enabled;
        if (hover) {
            look.Merge(n.style.hover);
            if (!n.style.hover.bg && look.bg && look.bg->a > 0.0f && n.kind == UiKind::Button) look.bg = Lighten(*look.bg, 0.07f);
            if (!n.style.hover.border && look.border && n.kind == UiKind::Button) look.border = Lighten(*look.border, 0.25f);
            if (!n.style.hover.handle && look.handle && (n.kind == UiKind::Slider || n.kind == UiKind::Toggle)) look.handle = Lighten(*look.handle, 0.0f);
            if (!n.style.hover.bg && n.kind == UiKind::Toggle && look.bg) look.bg = Lighten(*look.bg, 0.06f);
        }
        if (press) {
            look.Merge(n.style.pressed);
            if (!n.style.pressed.bg && look.bg && look.bg->a > 0.0f && n.kind == UiKind::Button) look.bg = Darken(*look.bg, 0.14f);
        }
        if (!enabled) {
            look.Merge(n.style.disabled);
            if (!n.style.disabled.bg && look.bg) look.bg->a *= 0.5f;
            if (!n.style.disabled.text && look.text) look.text->a *= 0.45f;
            if (!n.style.disabled.fill && look.fill) look.fill->a *= 0.45f;
            if (!n.style.disabled.handle && look.handle) look.handle->a *= 0.5f;
            if (!n.style.disabled.border && look.border) look.border->a *= 0.5f;
        }

        UiVisual target;
        target.bg = look.bg.value_or(glm::vec4(0.0f));
        target.border = look.border.value_or(glm::vec4(0.0f));
        target.text = look.text.value_or(glm::vec4(1.0f));
        target.tint = look.tint.value_or(glm::vec4(1.0f));
        target.fill = look.fill.value_or(glm::vec4(1.0f));
        target.handle = look.handle.value_or(glm::vec4(1.0f));
        target.borderWidth = look.borderWidth.value_or(0.0f);
        target.radius = look.radius.value_or(0.0f);
        target.image = look.image.value_or(n.image);

        UiVisual& v = n.visual;
        v.image = target.image;
        if (!v.initialized || n.style.transition <= 0.0f) {
            v = target;
            v.initialized = true;
            continue;
        }
        const float k = std::clamp(1.0f - std::exp(-dt * 3.0f / n.style.transition), 0.0f, 1.0f);
        v.bg += (target.bg - v.bg) * k;
        v.border += (target.border - v.border) * k;
        v.text += (target.text - v.text) * k;
        v.tint += (target.tint - v.tint) * k;
        v.fill += (target.fill - v.fill) * k;
        v.handle += (target.handle - v.handle) * k;
        v.borderWidth += (target.borderWidth - v.borderWidth) * k;
        v.radius += (target.radius - v.radius) * k;
    }
}

} // namespace magnum
