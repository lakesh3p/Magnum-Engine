#include "ui/UiSystem.h"

#include "scene/SpriteSize.h"

#include <algorithm>
#include <cmath>

namespace magnum {

// ---- small helpers ---------------------------------------------------------------------------------------------------

namespace {

constexpr float kPi = 3.14159265358979f;

const char* const kEaseNames[] = {"linear",    "in_quad",    "out_quad",  "in_out_quad",  "in_cubic",  "out_cubic",   "in_out_cubic",
                                  "in_quart",  "out_quart",  "in_out_quart", "in_sine",   "out_sine",  "in_out_sine", "in_expo",
                                  "out_expo",  "in_out_expo", "in_back",   "out_back",    "in_out_back", "out_elastic", "out_bounce"};

float OutBounce(float t) {
    constexpr float n = 7.5625f, d = 2.75f;
    if (t < 1.0f / d) return n * t * t;
    if (t < 2.0f / d) return n * (t -= 1.5f / d) * t + 0.75f;
    if (t < 2.5f / d) return n * (t -= 2.25f / d) * t + 0.9375f;
    return n * (t -= 2.625f / d) * t + 0.984375f;
}

} // namespace

const char* UiEaseName(UiEase ease) {
    const int i = static_cast<int>(ease);
    return i >= 0 && i < static_cast<int>(UiEase::Count) ? kEaseNames[i] : "linear";
}

bool UiEaseFromName(const std::string& name, UiEase& out) {
    std::string n = name;
    std::replace(n.begin(), n.end(), '-', '_');
    std::replace(n.begin(), n.end(), ' ', '_');
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (n == "out" || n == "ease_out") n = "out_cubic";
    if (n == "in" || n == "ease_in") n = "in_cubic";
    if (n == "in_out" || n == "ease" || n == "ease_in_out") n = "in_out_cubic";
    for (int i = 0; i < static_cast<int>(UiEase::Count); ++i) {
        if (n == kEaseNames[i]) {
            out = static_cast<UiEase>(i);
            return true;
        }
    }
    return false;
}

float UiApplyEase(UiEase ease, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    constexpr float c1 = 1.70158f, c2 = c1 * 1.525f, c3 = c1 + 1.0f;
    switch (ease) {
    case UiEase::Linear: return t;
    case UiEase::InQuad: return t * t;
    case UiEase::OutQuad: return 1.0f - (1.0f - t) * (1.0f - t);
    case UiEase::InOutQuad: return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
    case UiEase::InCubic: return t * t * t;
    case UiEase::OutCubic: return 1.0f - std::pow(1.0f - t, 3.0f);
    case UiEase::InOutCubic: return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
    case UiEase::InQuart: return t * t * t * t;
    case UiEase::OutQuart: return 1.0f - std::pow(1.0f - t, 4.0f);
    case UiEase::InOutQuart: return t < 0.5f ? 8.0f * t * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 4.0f) / 2.0f;
    case UiEase::InSine: return 1.0f - std::cos(t * kPi / 2.0f);
    case UiEase::OutSine: return std::sin(t * kPi / 2.0f);
    case UiEase::InOutSine: return -(std::cos(kPi * t) - 1.0f) / 2.0f;
    case UiEase::InExpo: return t == 0.0f ? 0.0f : std::pow(2.0f, 10.0f * t - 10.0f);
    case UiEase::OutExpo: return t == 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
    case UiEase::InOutExpo:
        if (t == 0.0f || t == 1.0f) return t;
        return t < 0.5f ? std::pow(2.0f, 20.0f * t - 10.0f) / 2.0f : (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) / 2.0f;
    case UiEase::InBack: return c3 * t * t * t - c1 * t * t;
    case UiEase::OutBack: return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
    case UiEase::InOutBack:
        return t < 0.5f ? (std::pow(2.0f * t, 2.0f) * ((c2 + 1.0f) * 2.0f * t - c2)) / 2.0f
                        : (std::pow(2.0f * t - 2.0f, 2.0f) * ((c2 + 1.0f) * (t * 2.0f - 2.0f) + c2) + 2.0f) / 2.0f;
    case UiEase::OutElastic:
        if (t == 0.0f || t == 1.0f) return t;
        return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * (2.0f * kPi / 3.0f)) + 1.0f;
    case UiEase::OutBounce: return OutBounce(t);
    default: return t;
    }
}

UiRect Intersect(const UiRect& a, const UiRect& b) {
    const float x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y);
    const float x1 = std::min(a.Right(), b.Right()), y1 = std::min(a.Bottom(), b.Bottom());
    if (x1 <= x0 || y1 <= y0) return {x0, y0, 0.0f, 0.0f};
    return {x0, y0, x1 - x0, y1 - y0};
}

UiAffine UiAffine::Then(const UiAffine& outer) const {
    UiAffine r;
    r.x = outer.x * x.x + outer.y * x.y;
    r.y = outer.x * y.x + outer.y * y.y;
    r.t = outer.Apply(t);
    return r;
}

UiAffine UiAffine::Inverse() const {
    const float det = x.x * y.y - y.x * x.y;
    if (std::abs(det) < 1e-12f) return {};
    const float inv = 1.0f / det;
    UiAffine r;
    r.x = {y.y * inv, -x.y * inv};
    r.y = {-y.x * inv, x.x * inv};
    r.t = -(r.x * t.x + r.y * t.y);
    return r;
}

void UiLook::Merge(const UiLook& over) {
    if (over.bg) bg = over.bg;
    if (over.border) border = over.border;
    if (over.text) text = over.text;
    if (over.tint) tint = over.tint;
    if (over.fill) fill = over.fill;
    if (over.handle) handle = over.handle;
    if (over.borderWidth) borderWidth = over.borderWidth;
    if (over.radius) radius = over.radius;
    if (over.image) image = over.image;
}

UiStyle DefaultStyle(UiKind kind) {
    UiStyle s;
    UiLook& b = s.base;
    b.bg = glm::vec4(0.0f);
    b.border = glm::vec4(0.0f);
    b.text = glm::vec4(0.92f, 0.93f, 0.96f, 1.0f);
    b.tint = glm::vec4(1.0f);
    b.fill = glm::vec4(0.25f, 0.55f, 0.95f, 1.0f);
    b.handle = glm::vec4(0.96f, 0.97f, 1.0f, 1.0f);
    b.borderWidth = 0.0f;
    b.radius = 0.0f;
    switch (kind) {
    case UiKind::Button:
        b.bg = glm::vec4(0.21f, 0.22f, 0.27f, 1.0f);
        b.border = glm::vec4(1.0f, 1.0f, 1.0f, 0.08f);
        b.borderWidth = 1.0f;
        b.radius = 5.0f;
        break;
    case UiKind::Toggle:
        b.bg = glm::vec4(0.16f, 0.17f, 0.2f, 1.0f);
        b.border = glm::vec4(1.0f, 1.0f, 1.0f, 0.2f);
        b.borderWidth = 1.0f;
        b.radius = 4.0f;
        break;
    case UiKind::Slider:
        b.bg = glm::vec4(0.13f, 0.14f, 0.17f, 1.0f);
        b.radius = 3.0f;
        break;
    case UiKind::Progress:
        b.bg = glm::vec4(0.12f, 0.13f, 0.16f, 1.0f);
        b.border = glm::vec4(1.0f, 1.0f, 1.0f, 0.06f);
        b.borderWidth = 1.0f;
        b.radius = 3.0f;
        break;
    case UiKind::Scroll:
        b.handle = glm::vec4(1.0f, 1.0f, 1.0f, 0.22f);
        break;
    default: break;
    }
    return s;
}

bool UiNode::BlocksPointer() const {
    if (block >= 0) return block != 0;
    return kind == UiKind::Button || kind == UiKind::Toggle || kind == UiKind::Slider || kind == UiKind::Scroll;
}

// ---- nodes -----------------------------------------------------------------------------------------------------------

UiSystem::UiSystem() {
    imageSize = [](const std::string& path, glm::vec2& out) {
        int w = 0, h = 0;
        if (!ImageSize(path, w, h)) return false;
        out = {static_cast<float>(w), static_cast<float>(h)};
        return true;
    };
}

UiSystem::~UiSystem() = default;

UiId UiSystem::Create(UiKind kind, const std::string& name) {
    auto node = std::make_unique<UiNode>();
    node->id = m_nextId++;
    node->kind = kind;
    node->name = name;
    node->style = DefaultStyle(kind);
    switch (kind) {
    case UiKind::Button:
        node->padding = {14.0f, 7.0f, 14.0f, 7.0f};
        node->textAlign = UiTextAlign::Center;
        break;
    case UiKind::Scroll:
        node->clip = true;
        node->layout = UiLayout::Column;
        break;
    case UiKind::Slider:
        node->value = 0.0f;
        break;
    default: break;
    }
    const UiId id = node->id;
    m_nodes[id] = std::move(node);
    return id;
}

UiNode* UiSystem::Get(UiId id) {
    const auto it = m_nodes.find(id);
    return it == m_nodes.end() ? nullptr : it->second.get();
}

const UiNode* UiSystem::Get(UiId id) const {
    const auto it = m_nodes.find(id);
    return it == m_nodes.end() ? nullptr : it->second.get();
}

void UiSystem::Detach(UiId child) {
    UiNode* c = Get(child);
    if (!c || !c->parent) return;
    if (UiNode* p = Get(c->parent)) {
        p->children.erase(std::remove(p->children.begin(), p->children.end(), child), p->children.end());
    }
    c->parent = 0;
}

bool UiSystem::Attach(UiId parent, UiId child, int index) {
    UiNode* p = Get(parent);
    UiNode* c = Get(child);
    if (!p || !c || parent == child) return false;
    for (const UiNode* a = p; a; a = Get(a->parent)) { // a node cannot go below itself
        if (a->id == child) return false;
    }
    for (const UiCanvas& canvas : m_canvases) {
        if (canvas.root == child) return false; // canvas roots stay roots
    }
    Detach(child);
    c->parent = parent;
    if (index < 0 || index >= static_cast<int>(p->children.size())) p->children.push_back(child);
    else p->children.insert(p->children.begin() + index, child);
    return true;
}

void UiSystem::Destroy(UiId id) {
    if (!Exists(id)) return;
    for (const UiCanvas& canvas : m_canvases) {
        if (canvas.root == id) return; // remove the canvas instead
    }
    Detach(id);
    std::vector<UiId> stack{id};
    while (!stack.empty()) {
        const UiId current = stack.back();
        stack.pop_back();
        const UiNode* n = Get(current);
        if (!n) continue;
        stack.insert(stack.end(), n->children.begin(), n->children.end());
        StopTweens(current);
        if (m_hit == current) m_hit = 0;
        if (m_captured == current) m_captured = 0;
        if (m_scrollDrag == current) m_scrollDrag = 0;
        m_nodes.erase(current);
    }
}

UiId UiSystem::Find(const std::string& name, UiId under) const {
    std::vector<UiId> stack;
    auto visit = [&](UiId root) {
        stack.clear();
        stack.push_back(root);
        while (!stack.empty()) {
            const UiId current = stack.back();
            stack.pop_back();
            const UiNode* n = Get(current);
            if (!n) continue;
            if (n->name == name) return current;
            for (auto it = n->children.rbegin(); it != n->children.rend(); ++it) stack.push_back(*it);
        }
        return UiId{0};
    };
    if (under) return visit(under);
    for (auto it = m_canvases.rbegin(); it != m_canvases.rend(); ++it) {
        if (const UiId found = visit(it->root)) return found;
    }
    return 0;
}

std::vector<UiId> UiSystem::FindAll(const std::string& prefix) const {
    std::vector<UiId> out, stack;
    for (auto canvas = m_canvases.rbegin(); canvas != m_canvases.rend(); ++canvas) {
        stack.assign(1, canvas->root);
        while (!stack.empty()) {
            const UiId current = stack.back();
            stack.pop_back();
            const UiNode* n = Get(current);
            if (!n) continue;
            if (!n->name.empty() && n->name.compare(0, prefix.size(), prefix) == 0) out.push_back(current);
            for (auto it = n->children.rbegin(); it != n->children.rend(); ++it) stack.push_back(*it);
        }
    }
    return out;
}

bool UiSystem::ApplyAnchorPreset(UiNode& n, const std::string& preset) {
    struct Preset {
        const char* name;
        glm::vec2 lo, hi, pivot;
    };
    static const Preset presets[] = {
        {"topleft", {0, 0}, {0, 0}, {0, 0}},       {"top", {0.5f, 0}, {0.5f, 0}, {0.5f, 0}},     {"topright", {1, 0}, {1, 0}, {1, 0}},
        {"left", {0, 0.5f}, {0, 0.5f}, {0, 0.5f}}, {"center", {0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f}}, {"right", {1, 0.5f}, {1, 0.5f}, {1, 0.5f}},
        {"bottomleft", {0, 1}, {0, 1}, {0, 1}},    {"bottom", {0.5f, 1}, {0.5f, 1}, {0.5f, 1}}, {"bottomright", {1, 1}, {1, 1}, {1, 1}},
        {"fill", {0, 0}, {1, 1}, {0.5f, 0.5f}},
        {"top_wide", {0, 0}, {1, 0}, {0.5f, 0}},   {"bottom_wide", {0, 1}, {1, 1}, {0.5f, 1}},   {"center_wide", {0, 0.5f}, {1, 0.5f}, {0.5f, 0.5f}},
        {"left_tall", {0, 0}, {0, 1}, {0, 0.5f}},  {"right_tall", {1, 0}, {1, 1}, {1, 0.5f}},   {"center_tall", {0.5f, 0}, {0.5f, 1}, {0.5f, 0.5f}},
    };
    std::string key = preset;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    key.erase(std::remove_if(key.begin(), key.end(), [](char c) { return c == '-' || c == ' '; }), key.end());
    if (key == "topcenter") key = "top";
    if (key == "bottomcenter") key = "bottom";
    if (key == "leftcenter" || key == "middleleft") key = "left";
    if (key == "rightcenter" || key == "middleright") key = "right";
    if (key == "middle") key = "center";
    for (const Preset& p : presets) {
        if (key == p.name) {
            n.anchorMin = p.lo;
            n.anchorMax = p.hi;
            n.pivot = p.pivot;
            return true;
        }
    }
    return false;
}

// ---- canvases --------------------------------------------------------------------------------------------------------

UiCanvas* UiSystem::AddCanvas(const std::string& name, int order) {
    if (UiCanvas* existing = FindCanvas(name)) {
        existing->order = order;
        std::stable_sort(m_canvases.begin(), m_canvases.end(), [](const UiCanvas& a, const UiCanvas& b) { return a.order < b.order; });
        return FindCanvas(name);
    }
    UiCanvas canvas;
    canvas.name = name;
    canvas.order = order;
    canvas.root = Create(UiKind::Panel, name);
    if (UiNode* root = Get(canvas.root)) root->ignoreLayout = true;
    m_canvases.push_back(canvas);
    std::stable_sort(m_canvases.begin(), m_canvases.end(), [](const UiCanvas& a, const UiCanvas& b) { return a.order < b.order; });
    return FindCanvas(name);
}

UiCanvas* UiSystem::FindCanvas(const std::string& name) {
    for (UiCanvas& c : m_canvases) {
        if (c.name == name) return &c;
    }
    return nullptr;
}

const UiCanvas* UiSystem::FindCanvas(const std::string& name) const {
    for (const UiCanvas& c : m_canvases) {
        if (c.name == name) return &c;
    }
    return nullptr;
}

UiCanvas* UiSystem::CanvasForRoot(UiId root) {
    for (UiCanvas& c : m_canvases) {
        if (c.root == root) return &c;
    }
    return nullptr;
}

UiCanvas* UiSystem::CanvasOf(UiId node) {
    const UiNode* n = Get(node);
    while (n && n->parent) n = Get(n->parent);
    return n ? CanvasForRoot(n->id) : nullptr;
}

void UiSystem::RemoveCanvas(const std::string& name) {
    const auto it = std::find_if(m_canvases.begin(), m_canvases.end(), [&](const UiCanvas& c) { return c.name == name; });
    if (it == m_canvases.end()) return;
    const UiId root = it->root;
    m_canvases.erase(it);
    Destroy(root);
}

void UiSystem::ClearCanvases() {
    while (!m_canvases.empty()) RemoveCanvas(m_canvases.back().name);
    m_nodes.clear();
    m_tweens.clear();
    m_events.clear();
    m_hit = m_captured = m_scrollDrag = 0;
    m_wasDown = false;
}

void UiSystem::SetViewSize(int width, int height) {
    m_viewWidth = std::max(width, 1);
    m_viewHeight = std::max(height, 1);
}

// ---- events, values, tweens ---------------------------------------------------------------------------------------------

void UiSystem::RaiseEvent(UiEventType type, UiId node, float value, uint32_t token) {
    UiEvent e;
    e.type = type;
    e.node = node;
    e.value = value;
    e.token = token;
    m_events.push_back(e);
}

std::vector<UiEvent> UiSystem::TakeEvents() {
    std::vector<UiEvent> out;
    out.swap(m_events);
    return out;
}

void UiSystem::SetValue(UiId id, float value, bool notify) {
    UiNode* n = Get(id);
    if (!n) return;
    if (n->kind == UiKind::Toggle) {
        value = value > 0.5f ? 1.0f : 0.0f;
    } else {
        const float lo = std::min(n->minValue, n->maxValue), hi = std::max(n->minValue, n->maxValue);
        if (n->step > 0.0f && n->kind == UiKind::Slider) value = n->minValue + std::round((value - n->minValue) / n->step) * n->step;
        value = std::clamp(value, lo, hi);
    }
    if (value == n->value) return;
    n->value = value;
    if (notify) RaiseEvent(UiEventType::Change, id, value);
}

void UiSystem::RaiseClick(UiId id) {
    if (const UiNode* n = Get(id)) RaiseEvent(UiEventType::Click, id, n->value);
}

void UiSystem::SetScroll(UiId id, glm::vec2 scroll) {
    UiNode* n = Get(id);
    if (!n) return;
    n->scroll = glm::max(scroll, glm::vec2(0.0f)); // the upper limit is applied by the next layout
}

namespace {

glm::vec4 GetProp(const UiNode& n, UiProp prop) {
    switch (prop) {
    case UiProp::Opacity: return glm::vec4(n.opacity);
    case UiProp::Scale: return glm::vec4(n.scale);
    case UiProp::Rotation: return glm::vec4(n.rotation);
    case UiProp::Pos: return glm::vec4(n.pos, 0.0f, 0.0f);
    case UiProp::Size: return glm::vec4(n.size.x < 0.0f ? n.rect.w : n.size.x, n.size.y < 0.0f ? n.rect.h : n.size.y, 0.0f, 0.0f);
    case UiProp::Bg: return n.style.base.bg.value_or(glm::vec4(0.0f));
    case UiProp::TextColor: return n.style.base.text.value_or(glm::vec4(1.0f));
    case UiProp::Value: return glm::vec4(n.value);
    case UiProp::Scroll: return glm::vec4(n.scroll, 0.0f, 0.0f);
    }
    return glm::vec4(0.0f);
}

void SetProp(UiNode& n, UiProp prop, const glm::vec4& v) {
    switch (prop) {
    case UiProp::Opacity: n.opacity = v.x; break;
    case UiProp::Scale: n.scale = v.x; break;
    case UiProp::Rotation: n.rotation = v.x; break;
    case UiProp::Pos: n.pos = {v.x, v.y}; break;
    case UiProp::Size: n.size = {v.x, v.y}; break;
    case UiProp::Bg: n.style.base.bg = v; break;
    case UiProp::TextColor: n.style.base.text = v; break;
    case UiProp::Value:
        n.value = n.kind == UiKind::Toggle ? (v.x > 0.5f ? 1.0f : 0.0f) : std::clamp(v.x, std::min(n.minValue, n.maxValue), std::max(n.minValue, n.maxValue));
        break;
    case UiProp::Scroll: n.scroll = {std::max(v.x, 0.0f), std::max(v.y, 0.0f)}; break;
    }
}

} // namespace

bool UiSystem::Animate(const UiTween& tween) {
    if (!Exists(tween.node)) return false;
    // A new tween of the same property replaces the running one, so repeated triggers do not fight.
    m_tweens.erase(std::remove_if(m_tweens.begin(), m_tweens.end(),
                                  [&](const UiTween& t) { return t.node == tween.node && t.prop == tween.prop; }),
                   m_tweens.end());
    UiTween t = tween;
    t.duration = std::max(t.duration, 1e-4f);
    t.elapsed = 0.0f;
    t.started = false;
    m_tweens.push_back(t);
    return true;
}

void UiSystem::StopTweens(UiId node) {
    m_tweens.erase(std::remove_if(m_tweens.begin(), m_tweens.end(), [&](const UiTween& t) { return t.node == node; }), m_tweens.end());
}

bool UiSystem::HasTweens(UiId node) const {
    return std::any_of(m_tweens.begin(), m_tweens.end(), [&](const UiTween& t) { return t.node == node; });
}

void UiSystem::UpdateTweens(float dt) {
    std::vector<UiId> toRemove, toHide;
    for (size_t i = 0; i < m_tweens.size();) {
        UiTween& t = m_tweens[i];
        UiNode* n = Get(t.node);
        if (!n) {
            m_tweens.erase(m_tweens.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        t.elapsed += dt;
        if (t.elapsed < t.delay) {
            ++i;
            continue;
        }
        if (!t.started) {
            if (!t.hasFrom) t.from = GetProp(*n, t.prop);
            t.started = true;
        }
        float local = t.elapsed - t.delay;
        bool finished = false;
        float phase;
        if (t.loop) {
            const float cycle = t.duration * (t.yoyo ? 2.0f : 1.0f);
            local = std::fmod(local, cycle);
            phase = t.yoyo && local > t.duration ? 2.0f - local / t.duration : local / t.duration;
        } else if (local >= t.duration) {
            phase = 1.0f;
            finished = true;
        } else {
            phase = local / t.duration;
        }
        const float k = UiApplyEase(t.ease, phase);
        SetProp(*n, t.prop, t.from + (t.to - t.from) * k);
        if (finished) {
            const UiTween done = t;
            m_tweens.erase(m_tweens.begin() + static_cast<std::ptrdiff_t>(i));
            if (done.token) RaiseEvent(UiEventType::TweenDone, done.node, 0.0f, done.token);
            if (done.removeNode) toRemove.push_back(done.node);
            else if (done.hideNode) toHide.push_back(done.node);
            continue;
        }
        ++i;
    }
    for (const UiId id : toHide) {
        if (UiNode* n = Get(id)) n->visible = false;
    }
    for (const UiId id : toRemove) Destroy(id);
}

void UiSystem::Update(float dt, const UiPointer& pointer) {
    UpdateTweens(dt);
    Layout();
    Interact(pointer, dt);
    UpdateVisuals(dt);
}

std::string UiSystem::Dump() const {
    static const char* const kinds[] = {"panel", "image", "text", "button", "toggle", "slider", "progress", "scroll"};
    std::string out;
    char line[256];
    for (auto canvas = m_canvases.rbegin(); canvas != m_canvases.rend(); ++canvas) {
        std::snprintf(line, sizeof(line), "canvas '%s'  order %d  scale %.2f  size %.0f x %.0f%s%s\n", canvas->name.c_str(), canvas->order, canvas->scale,
                      canvas->size.x, canvas->size.y, canvas->visible ? "" : "  (hidden)", canvas->document.empty() ? "" : ("  " + canvas->document).c_str());
        out += line;
        struct Item {
            UiId id;
            int depth;
        };
        std::vector<Item> stack;
        if (const UiNode* root = Get(canvas->root)) {
            for (auto it = root->children.rbegin(); it != root->children.rend(); ++it) stack.push_back({*it, 1});
        }
        while (!stack.empty()) {
            const Item item = stack.back();
            stack.pop_back();
            const UiNode* n = Get(item.id);
            if (!n) continue;
            std::string text = n->text.size() > 24 ? n->text.substr(0, 24) + "..." : n->text;
            std::snprintf(line, sizeof(line), "%*s%s '%s'  %.0f,%.0f  %.0f x %.0f%s%s%s%s%s\n", item.depth * 2, "", kinds[static_cast<int>(n->kind)], n->name.c_str(),
                          n->rect.x, n->rect.y, n->rect.w, n->rect.h, n->visible ? "" : "  hidden", n->worldEnabled ? "" : "  disabled",
                          n->hovered ? "  hovered" : "", n->pressed ? "  pressed" : "", text.empty() ? "" : ("  \"" + text + "\"").c_str());
            out += line;
            for (auto it = n->children.rbegin(); it != n->children.rend(); ++it) stack.push_back({*it, item.depth + 1});
        }
    }
    return out;
}

UiRect UiSystem::PixelRect(UiId id) const {
    const UiNode* n = Get(id);
    if (!n) return {};
    const UiNode* top = n;
    while (top->parent) top = Get(top->parent);
    const UiCanvas* canvas = nullptr;
    for (const UiCanvas& c : m_canvases) {
        if (c.root == top->id) canvas = &c;
    }
    const float s = canvas ? canvas->scale : 1.0f;
    return {n->rect.x * s, n->rect.y * s, n->rect.w * s, n->rect.h * s};
}

} // namespace magnum
