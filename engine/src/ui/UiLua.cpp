// The `ui` global for Lua scripts and UI documents.
//
//   Nodes      ui.panel{...} ui.row{...} ui.column{...} ui.grid{columns = 3, ...} ui.scroll{...} ui.text{...}
//              ui.image{...} ui.button{...} ui.toggle{...} ui.slider{...} ui.progress{...} ui.spacer{...}
//              Entries without a key are children (a string is a text for text, button and toggle):
//                  ui.column{ gap = 8, ui.text{ "Title", font_size = 28 }, ui.button{ "Play", on_click = start } }
//   Placement  anchor = "center" | "topleft" | "top" | "topright" | "left" | "right" | "bottomleft" | "bottom" |
//              "bottomright" | "fill" | "top_wide" | "bottom_wide" | "center_wide" | "left_tall" | "right_tall" |
//              "center_tall" | {min_x, min_y, max_x, max_y}; pivot; pos = {x, y}; size = {w, h} ("auto" or a negative
//              number sizes to the content); margin (stretched anchors); min_size, max_size; grow; align_self
//   Layout     layout = "row" | "column" | "grid"; gap / spacing; padding; justify = "start" | "center" | "end" |
//              "between" | "around"; items = "start" | "center" | "end" | "stretch"; columns; cell_size
//   Look       color, bg, border (width or color), border_width, radius, image, tint, slice, uv, fit, pixelated,
//              opacity, scale, rotation, clip, block, shadow = {x, y, blur, spread, color}, style = "name",
//              hover / pressed / disabled / active = { color, border, text_color, tint, fill, handle, ... },
//              transition (seconds)
//   Text       text, font ("Sans", "SansBold", "Mono" or a project .ttf), font_size, text_color, text_align, wrap,
//              line_height, text_shadow = {x, y, color}
//   Values     value, min, max, step, vertical, look = "check" | "switch" (toggle), scroll
//   Events     on_click, on_press, on_release, on_enter, on_leave, on_change(node, value), on_update(node, dt)
//   Node       node.text = "x" reads and writes any property above; node.rect = {x, y, w, h}; node:add(child, ...),
//              :remove(child), :clear(), :destroy(), :find(name), :animate{ opacity = 0, pos = {x, y}, scale = 1.2,
//              rotation = 10, size = {w, h}, bg = color, text_color = color, value = 1, duration = 0.3, delay = 0,
//              ease = "out_cubic", loop = false, yoyo = false, remove = false, hide = false, on_done = fn },
//              :stop(), :show(), :hide(), :to_front(), :click()
//   Global     ui.show(node), ui.hide(node), ui.root(), ui.canvas(name, { order, scale = "height", reference = {w, h} }),
//              ui.load("Content/UI/file.lua"[, canvas]), ui.find(name), ui.find_all(prefix), ui.style(name, {...}),
//              ui.pointer_over(), ui.screen(), ui.toast(text[, {...}]), ui.clear()
#include "ui/UiLua.h"

#include "core/Log.h"
#include "scene/Scene.h"
#include "shader/ShaderTypes.h"

#include <sol/sol.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace magnum {

namespace {

namespace fs = std::filesystem;

struct UiRef {
    UiId id = 0;
};

[[noreturn]] void Fail(const std::string& message) {
    throw std::runtime_error("ui: " + message);
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// ---- reading Lua values ---------------------------------------------------------------------------------------------------

bool IsNumber(const sol::object& o) { return o.get_type() == sol::type::number; }
bool IsTable(const sol::object& o) { return o.get_type() == sol::type::table; }
bool IsString(const sol::object& o) { return o.get_type() == sol::type::string; }

float ToFloat(const sol::object& o, const char* what) {
    if (IsNumber(o)) return o.as<float>();
    if (o.get_type() == sol::type::boolean) return o.as<bool>() ? 1.0f : 0.0f;
    Fail(std::string(what) + " needs a number");
}

bool ToBool(const sol::object& o, const char* what) {
    if (o.get_type() == sol::type::boolean) return o.as<bool>();
    if (IsNumber(o)) return o.as<float>() != 0.0f;
    if (o.get_type() == sol::type::lua_nil) return false;
    Fail(std::string(what) + " needs true or false");
}

std::string ToString(const sol::object& o, const char* what) {
    if (IsString(o)) return o.as<std::string>();
    if (IsNumber(o)) return std::to_string(o.as<double>());
    Fail(std::string(what) + " needs a string");
}

// A table field by name, or by position when absent.
std::optional<float> Field(const sol::table& t, std::initializer_list<const char*> names, int index) {
    for (const char* name : names) {
        const sol::optional<float> v = t[name];
        if (v) return *v;
    }
    if (index > 0) {
        const sol::optional<float> v = t[index];
        if (v) return *v;
    }
    return std::nullopt;
}

float SizeComponent(const sol::object& o) { // a number, or "auto" = size to the content
    if (IsString(o)) {
        const std::string s = Lower(o.as<std::string>());
        if (s == "auto") return -1.0f;
        Fail("a size is a number or \"auto\", not '" + s + "'");
    }
    return ToFloat(o, "a size");
}

glm::vec2 ToVec2(const sol::object& o, const char* what, bool allowAuto = false) {
    if (IsNumber(o)) return glm::vec2(o.as<float>());
    if (allowAuto && IsString(o)) return glm::vec2(SizeComponent(o));
    if (o.is<glm::vec3>()) return glm::vec2(o.as<glm::vec3>());
    if (!IsTable(o)) Fail(std::string(what) + " needs a number or a table {x, y}");
    const sol::table t = o.as<sol::table>();
    glm::vec2 v(0.0f);
    const char* keysX[] = {"x", "w", "width"};
    const char* keysY[] = {"y", "h", "height"};
    bool gotX = false, gotY = false;
    for (const char* k : keysX) {
        const sol::object c = t[k];
        if (c.valid() && c.get_type() != sol::type::lua_nil && !gotX) {
            v.x = allowAuto ? SizeComponent(c) : ToFloat(c, what);
            gotX = true;
        }
    }
    for (const char* k : keysY) {
        const sol::object c = t[k];
        if (c.valid() && c.get_type() != sol::type::lua_nil && !gotY) {
            v.y = allowAuto ? SizeComponent(c) : ToFloat(c, what);
            gotY = true;
        }
    }
    if (!gotX) {
        const sol::object c = t[1];
        if (c.valid() && c.get_type() != sol::type::lua_nil) v.x = allowAuto ? SizeComponent(c) : ToFloat(c, what);
    }
    if (!gotY) {
        const sol::object c = t[2];
        if (c.valid() && c.get_type() != sol::type::lua_nil) v.y = allowAuto ? SizeComponent(c) : ToFloat(c, what);
    }
    return v;
}

// left, top, right, bottom: a number, {l, t, r, b}, {horizontal, vertical} or {h =, v =}.
glm::vec4 ToEdges(const sol::object& o, const char* what) {
    if (IsNumber(o)) return glm::vec4(o.as<float>());
    if (!IsTable(o)) Fail(std::string(what) + " needs a number or a table {left, top, right, bottom}");
    const sol::table t = o.as<sol::table>();
    glm::vec4 v(0.0f);
    if (const auto h = Field(t, {"h", "horizontal"}, 0)) v.x = v.z = *h;
    if (const auto vv = Field(t, {"v", "vertical"}, 0)) v.y = v.w = *vv;
    if (const auto l = Field(t, {"l", "left"}, 0)) v.x = *l;
    if (const auto tp = Field(t, {"t", "top"}, 0)) v.y = *tp;
    if (const auto r = Field(t, {"r", "right"}, 0)) v.z = *r;
    if (const auto b = Field(t, {"b", "bottom"}, 0)) v.w = *b;
    const bool named = t["h"].valid() || t["v"].valid() || t["horizontal"].valid() || t["vertical"].valid() || t["l"].valid() || t["t"].valid() ||
                       t["r"].valid() || t["b"].valid() || t["left"].valid() || t["top"].valid() || t["right"].valid() || t["bottom"].valid();
    if (!named) {
        const size_t n = t.size();
        const auto at = [&](int i) { return t[i].get_or(0.0f); };
        if (n == 2) v = {at(1), at(2), at(1), at(2)};
        else if (n >= 4) v = {at(1), at(2), at(3), at(4)};
        else if (n == 1) v = glm::vec4(at(1));
    }
    return v;
}

bool ParseHex(const std::string& text, glm::vec4& out) {
    std::string s = text;
    if (!s.empty() && s[0] == '#') s.erase(0, 1);
    if (s.size() != 3 && s.size() != 4 && s.size() != 6 && s.size() != 8) return false;
    for (const char c : s) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    auto hex = [&](size_t i, size_t n) { return static_cast<float>(std::stoi(s.substr(i, n), nullptr, 16)); };
    if (s.size() <= 4) {
        auto nib = [&](size_t i) { return hex(i, 1) * 17.0f / 255.0f; };
        out = {nib(0), nib(1), nib(2), s.size() == 4 ? nib(3) : 1.0f};
    } else {
        out = {hex(0, 2) / 255.0f, hex(2, 2) / 255.0f, hex(4, 2) / 255.0f, s.size() == 8 ? hex(6, 2) / 255.0f : 1.0f};
    }
    return true;
}

// "#rrggbb" / "#rrggbbaa" / "#rgb", {r, g, b[, a]} (0..1, or 0..255 when any value is above 1), or a vec3.
glm::vec4 ToColor(const sol::object& o, const char* what) {
    if (IsString(o)) {
        glm::vec4 c;
        if (!ParseHex(o.as<std::string>(), c)) Fail(std::string(what) + ": '" + o.as<std::string>() + "' is not a color like \"#3a7bd5\"");
        return c;
    }
    if (o.is<glm::vec3>()) return glm::vec4(o.as<glm::vec3>(), 1.0f);
    if (!IsTable(o)) Fail(std::string(what) + " needs a color: \"#rrggbb\" or {r, g, b[, a]}");
    const sol::table t = o.as<sol::table>();
    glm::vec4 c(0.0f, 0.0f, 0.0f, 1.0f);
    c.r = Field(t, {"r", "red"}, 1).value_or(0.0f);
    c.g = Field(t, {"g", "green"}, 2).value_or(0.0f);
    c.b = Field(t, {"b", "blue"}, 3).value_or(0.0f);
    c.a = Field(t, {"a", "alpha"}, 4).value_or(1.0f);
    if (std::max({c.r, c.g, c.b, c.a}) > 1.0f) c /= 255.0f;
    return c;
}

UiLayout ToLayout(const std::string& s) {
    const std::string k = Lower(s);
    if (k == "none") return UiLayout::None;
    if (k == "row" || k == "horizontal") return UiLayout::Row;
    if (k == "column" || k == "col" || k == "vertical") return UiLayout::Column;
    if (k == "grid") return UiLayout::Grid;
    Fail("layout is 'none', 'row', 'column' or 'grid', not '" + s + "'");
}

UiJustify ToJustify(const std::string& s) {
    const std::string k = Lower(s);
    if (k == "start" || k == "begin" || k == "left" || k == "top") return UiJustify::Start;
    if (k == "center" || k == "middle") return UiJustify::Center;
    if (k == "end" || k == "right" || k == "bottom") return UiJustify::End;
    if (k == "between" || k == "space_between") return UiJustify::SpaceBetween;
    if (k == "around" || k == "space_around") return UiJustify::SpaceAround;
    Fail("justify is 'start', 'center', 'end', 'between' or 'around', not '" + s + "'");
}

UiItems ToItems(const std::string& s) {
    const std::string k = Lower(s);
    if (k == "start" || k == "begin" || k == "left" || k == "top") return UiItems::Start;
    if (k == "center" || k == "middle") return UiItems::Center;
    if (k == "end" || k == "right" || k == "bottom") return UiItems::End;
    if (k == "stretch" || k == "fill") return UiItems::Stretch;
    Fail("items is 'start', 'center', 'end' or 'stretch', not '" + s + "'");
}

UiScaleMode ToScaleMode(const std::string& s) {
    const std::string k = Lower(s);
    if (k == "constant" || k == "none" || k == "pixels") return UiScaleMode::Constant;
    if (k == "height") return UiScaleMode::Height;
    if (k == "width") return UiScaleMode::Width;
    if (k == "fit") return UiScaleMode::Fit;
    if (k == "fill") return UiScaleMode::Fill;
    Fail("scale is 'constant', 'height', 'width', 'fit' or 'fill', not '" + s + "'");
}

std::string KindName(UiKind k) {
    static const char* const names[] = {"panel", "image", "text", "button", "toggle", "slider", "progress", "scroll"};
    return names[static_cast<int>(k)];
}

// A style given by name to ui.style: only the fields that were written count.
struct NamedStyle {
    UiStyle style;
    bool hasTransition = false;
    bool hasShadow = false;
};

} // namespace

// ---- the implementation -------------------------------------------------------------------------------------------------------

struct UiScripting::Impl {
    UiSystem& ui;
    sol::state_view lua;
    fs::path root;
    std::function<void()> budget;
    std::map<std::string, NamedStyle> styles;
    std::unordered_map<UiId, std::map<std::string, sol::protected_function>> callbacks;
    std::unordered_map<uint32_t, sol::protected_function> tweenDone;
    uint32_t nextToken = 1;
    sol::table methods;
    double sinceCheck = 0.0;
    bool bound = false;

    Impl(lua_State* L, UiSystem& system, fs::path projectRoot, std::function<void()> resetBudget)
        : ui(system), lua(L), root(std::move(projectRoot)), budget(std::move(resetBudget)) {}

    // ---- helpers ----------------------------------------------------------------------------------------------------------

    UiNode& Node(const UiRef& ref, const char* what = "node") {
        UiNode* n = ui.Get(ref.id);
        if (!n) Fail(std::string("this ") + what + " was destroyed");
        return *n;
    }

    UiId ActiveRoot() {
        UiCanvas* canvas = ui.FindCanvas(ui.ActiveCanvasName());
        if (!canvas) canvas = ui.AddCanvas(ui.ActiveCanvasName(), 0);
        return canvas->root;
    }

    void Call(const std::string& what, sol::protected_function& fn, UiId node, auto&&... args) {
        if (!fn.valid()) return;
        if (budget) budget();
        const sol::protected_function_result r = fn(UiRef{node}, std::forward<decltype(args)>(args)...);
        if (!r.valid()) {
            const sol::error e = r;
            Log::Error("[ui] {}: {}", what, e.what());
            fn = sol::protected_function(); // reported once; set the callback again to retry
        }
    }

    // ---- looks and styles -----------------------------------------------------------------------------------------------------

    // Which look field `color` means for a node of this kind.
    enum class ColorTarget { Bg, Text, Tint };
    static ColorTarget ColorMeaning(UiKind k) {
        if (k == UiKind::Text) return ColorTarget::Text;
        if (k == UiKind::Image) return ColorTarget::Tint;
        return ColorTarget::Bg;
    }

    bool SetLookField(UiLook& look, UiKind kind, const std::string& key, const sol::object& v) {
        if (key == "color") {
            const glm::vec4 c = ToColor(v, "color");
            switch (ColorMeaning(kind)) {
            case ColorTarget::Text: look.text = c; break;
            case ColorTarget::Tint: look.tint = c; break;
            default: look.bg = c; break;
            }
        } else if (key == "bg" || key == "background") {
            look.bg = ToColor(v, "bg");
        } else if (key == "border_color") {
            look.border = ToColor(v, "border_color");
        } else if (key == "border") {
            if (IsNumber(v)) {
                look.borderWidth = v.as<float>();
            } else if (IsTable(v) && (v.as<sol::table>()["width"].valid() || v.as<sol::table>()["color"].valid())) {
                const sol::table t = v.as<sol::table>();
                if (t["width"].valid()) look.borderWidth = ToFloat(t["width"], "border width");
                if (t["color"].valid()) look.border = ToColor(t["color"], "border color");
            } else {
                look.border = ToColor(v, "border");
            }
        } else if (key == "border_width") {
            look.borderWidth = ToFloat(v, "border_width");
        } else if (key == "radius") {
            look.radius = ToFloat(v, "radius");
        } else if (key == "text_color") {
            look.text = ToColor(v, "text_color");
        } else if (key == "tint") {
            look.tint = ToColor(v, "tint");
        } else if (key == "fill") {
            look.fill = ToColor(v, "fill");
        } else if (key == "handle") {
            look.handle = ToColor(v, "handle");
        } else if (key == "image") {
            look.image = ToString(v, "image");
        } else {
            return false;
        }
        return true;
    }

    UiLook ParseLook(UiKind kind, const sol::object& o, const char* what) {
        if (!IsTable(o)) Fail(std::string(what) + " needs a table of look fields");
        UiLook look;
        for (const auto& kv : o.as<sol::table>()) {
            if (!IsString(kv.first)) continue;
            const std::string key = kv.first.as<std::string>();
            if (!SetLookField(look, kind, key, kv.second)) Fail(std::string(what) + " has no field '" + key + "'");
        }
        return look;
    }

    UiShadow ParseShadow(const sol::object& o, const char* what) {
        UiShadow s;
        if (o.get_type() == sol::type::boolean || o.get_type() == sol::type::lua_nil) {
            s.enabled = o.get_type() == sol::type::boolean && o.as<bool>();
            return s;
        }
        if (!IsTable(o)) Fail(std::string(what) + " needs a table {x, y, blur, spread, color} or false");
        const sol::table t = o.as<sol::table>();
        s.enabled = true;
        s.offset = {Field(t, {"x"}, 0).value_or(0.0f), Field(t, {"y"}, 0).value_or(2.0f)};
        s.blur = Field(t, {"blur"}, 0).value_or(8.0f);
        s.spread = Field(t, {"spread"}, 0).value_or(0.0f);
        if (t["color"].valid()) s.color = ToColor(t["color"], "shadow color");
        return s;
    }

    void SetAlignedStyle(UiNode& n, const std::string& name) {
        const auto it = styles.find(name);
        if (it == styles.end()) Fail("there is no style named '" + name + "' (define it with ui.style)");
        const NamedStyle& named = it->second;
        UiStyle merged = DefaultStyle(n.kind);
        merged.base.Merge(named.style.base);
        merged.hover.Merge(named.style.hover);
        merged.pressed.Merge(named.style.pressed);
        merged.disabled.Merge(named.style.disabled);
        merged.active.Merge(named.style.active);
        merged.transition = named.hasTransition ? named.style.transition : merged.transition;
        if (named.hasShadow) merged.shadow = named.style.shadow;
        n.style = merged;
    }

    // ---- properties --------------------------------------------------------------------------------------------------------------

    void SetCallback(UiId id, const std::string& key, const sol::object& v) {
        if (v.get_type() == sol::type::lua_nil) {
            callbacks[id].erase(key);
            return;
        }
        if (v.get_type() != sol::type::function) Fail(key + " needs a function");
        callbacks[id][key] = sol::protected_function(v.as<sol::function>());
    }

    void SetValue(UiNode& n, const sol::object& v) {
        if (n.kind == UiKind::Toggle) {
            ui.SetValue(n.id, ToBool(v, "value") ? 1.0f : 0.0f, true);
        } else {
            ui.SetValue(n.id, ToFloat(v, "value"), true);
        }
    }

    void SetProperty(UiNode& n, const std::string& key, const sol::object& v) {
        if (key.size() > 3 && key.compare(0, 3, "on_") == 0) {
            static const std::set<std::string> events = {"on_click", "on_press", "on_release", "on_enter", "on_leave", "on_change", "on_update"};
            if (!events.contains(key)) Fail("unknown event '" + key + "' (on_click, on_press, on_release, on_enter, on_leave, on_change, on_update)");
            SetCallback(n.id, key, v);
            return;
        }
        // placement
        if (key == "name") n.name = ToString(v, "name");
        else if (key == "visible") n.visible = ToBool(v, "visible");
        else if (key == "enabled") n.enabled = ToBool(v, "enabled");
        else if (key == "clip") n.clip = ToBool(v, "clip");
        else if (key == "block") n.block = ToBool(v, "block") ? 1 : 0;
        else if (key == "ignore_layout") n.ignoreLayout = ToBool(v, "ignore_layout");
        else if (key == "anchor") {
            if (IsString(v)) {
                if (!UiSystem::ApplyAnchorPreset(n, v.as<std::string>())) Fail("unknown anchor '" + v.as<std::string>() + "' (topleft, top, topright, left, center, right, bottomleft, bottom, bottomright, fill, top_wide, bottom_wide, center_wide, left_tall, right_tall, center_tall)");
            } else if (IsTable(v)) {
                const sol::table t = v.as<sol::table>();
                if (t.size() == 2) {
                    n.anchorMin = n.anchorMax = {t[1].get_or(0.0f), t[2].get_or(0.0f)};
                } else if (t.size() >= 4) {
                    n.anchorMin = {t[1].get_or(0.0f), t[2].get_or(0.0f)};
                    n.anchorMax = {t[3].get_or(0.0f), t[4].get_or(0.0f)};
                } else {
                    Fail("anchor needs a preset name or {min_x, min_y, max_x, max_y}");
                }
            } else {
                Fail("anchor needs a preset name or {min_x, min_y, max_x, max_y}");
            }
        } else if (key == "pivot") {
            if (IsString(v)) {
                UiNode probe;
                if (!UiSystem::ApplyAnchorPreset(probe, v.as<std::string>())) Fail("unknown pivot '" + v.as<std::string>() + "'");
                n.pivot = probe.pivot;
            } else {
                n.pivot = ToVec2(v, "pivot");
            }
        } else if (key == "pos" || key == "position" || key == "offset") n.pos = ToVec2(v, "pos");
        else if (key == "x") n.pos.x = ToFloat(v, "x");
        else if (key == "y") n.pos.y = ToFloat(v, "y");
        else if (key == "size") n.size = ToVec2(v, "size", true);
        else if (key == "width" || key == "w") n.size.x = SizeComponent(v);
        else if (key == "height" || key == "h") n.size.y = SizeComponent(v);
        else if (key == "min_size") n.minSize = ToVec2(v, "min_size");
        else if (key == "max_size") n.maxSize = ToVec2(v, "max_size");
        else if (key == "margin") {
            const glm::vec4 e = ToEdges(v, "margin");
            n.marginMin = {e.x, e.y};
            n.marginMax = {e.z, e.w};
        } else if (key == "grow") n.grow = ToFloat(v, "grow");
        else if (key == "align_self") {
            if (v.get_type() == sol::type::lua_nil) n.alignSelf.reset();
            else n.alignSelf = ToItems(ToString(v, "align_self"));
        }
        // layout
        else if (key == "layout") n.layout = ToLayout(ToString(v, "layout"));
        else if (key == "spacing" || key == "gap") n.spacing = ToVec2(v, "spacing");
        else if (key == "padding") n.padding = ToEdges(v, "padding");
        else if (key == "justify") n.justify = ToJustify(ToString(v, "justify"));
        else if (key == "items") n.items = ToItems(ToString(v, "items"));
        else if (key == "columns") n.columns = std::max(1, static_cast<int>(ToFloat(v, "columns")));
        else if (key == "cell_size") n.cellSize = ToVec2(v, "cell_size");
        // transform
        else if (key == "opacity") n.opacity = ToFloat(v, "opacity");
        else if (key == "scale") n.scale = ToFloat(v, "scale");
        else if (key == "rotation") n.rotation = ToFloat(v, "rotation");
        // text
        else if (key == "text") n.text = IsString(v) ? v.as<std::string>() : (IsNumber(v) ? [&] {
                                       const double d = v.as<double>();
                                       std::ostringstream os;
                                       if (d == static_cast<long long>(d)) os << static_cast<long long>(d);
                                       else os << d;
                                       return os.str();
                                   }() : ToString(v, "text"));
        else if (key == "font") n.font = ToString(v, "font");
        else if (key == "font_size" || key == "text_size") n.fontSize = std::max(ToFloat(v, "font_size"), 1.0f);
        else if (key == "text_align") {
            const std::string k = Lower(ToString(v, "text_align"));
            if (k == "left" || k == "start") n.textAlign = UiTextAlign::Left;
            else if (k == "center" || k == "middle") n.textAlign = UiTextAlign::Center;
            else if (k == "right" || k == "end") n.textAlign = UiTextAlign::Right;
            else Fail("text_align is 'left', 'center' or 'right'");
        } else if (key == "wrap") n.wrap = ToBool(v, "wrap");
        else if (key == "line_height") n.lineHeight = std::max(ToFloat(v, "line_height"), 0.1f);
        else if (key == "text_shadow") n.textShadow = ParseShadow(v, "text_shadow");
        // image
        else if (key == "image") n.image = v.get_type() == sol::type::lua_nil ? std::string() : ToString(v, "image");
        else if (key == "uv") {
            const glm::vec4 e = ToEdges(v, "uv");
            n.uv = e;
        } else if (key == "slice") n.slice = ToEdges(v, "slice");
        else if (key == "fit") {
            const std::string k = Lower(ToString(v, "fit"));
            if (k == "stretch") n.fit = UiFit::Stretch;
            else if (k == "contain") n.fit = UiFit::Contain;
            else if (k == "cover") n.fit = UiFit::Cover;
            else Fail("fit is 'stretch', 'contain' or 'cover'");
        } else if (key == "pixelated") n.pixelated = ToBool(v, "pixelated");
        // values
        else if (key == "value") SetValue(n, v);
        else if (key == "min") n.minValue = ToFloat(v, "min");
        else if (key == "max") n.maxValue = ToFloat(v, "max");
        else if (key == "step") n.step = std::max(ToFloat(v, "step"), 0.0f);
        else if (key == "vertical") n.vertical = ToBool(v, "vertical");
        else if (key == "look") {
            const std::string k = Lower(ToString(v, "look"));
            if (k == "check" || k == "checkbox") n.look = UiToggleLook::Check;
            else if (k == "switch") n.look = UiToggleLook::Switch;
            else Fail("look is 'check' or 'switch'");
        } else if (key == "scroll") {
            n.scroll = IsNumber(v) ? glm::vec2(0.0f, v.as<float>()) : ToVec2(v, "scroll");
        }
        // style
        else if (key == "style") SetAlignedStyle(n, ToString(v, "style"));
        else if (key == "hover") n.style.hover = ParseLook(n.kind, v, "hover");
        else if (key == "pressed") n.style.pressed = ParseLook(n.kind, v, "pressed");
        else if (key == "disabled") n.style.disabled = ParseLook(n.kind, v, "disabled");
        else if (key == "active") n.style.active = ParseLook(n.kind, v, "active");
        else if (key == "shadow") n.style.shadow = ParseShadow(v, "shadow");
        else if (key == "transition") n.style.transition = std::max(ToFloat(v, "transition"), 0.0f);
        else if (SetLookField(n.style.base, n.kind, key, v)) {
            if (key == "image") n.image = n.style.base.image.value_or("");
        } else {
            Fail("unknown property '" + key + "' for a " + KindName(n.kind));
        }
    }

    static sol::table Vec2Table(sol::state_view lua, glm::vec2 v, bool size) {
        sol::table t = lua.create_table();
        t["x"] = v.x;
        t["y"] = v.y;
        if (size) {
            t["w"] = v.x;
            t["h"] = v.y;
        }
        return t;
    }

    sol::object GetProperty(const UiRef& ref, const std::string& key, sol::this_state ts) {
        sol::state_view L(ts);
        UiNode* np = ui.Get(ref.id);
        if (key == "valid") return sol::make_object(L, np != nullptr);
        if (key == "id") return sol::make_object(L, static_cast<double>(ref.id));
        if (!np) return sol::nil;
        UiNode& n = *np;
        if (key == "name") return sol::make_object(L, n.name);
        if (key == "kind") return sol::make_object(L, KindName(n.kind));
        if (key == "visible") return sol::make_object(L, n.visible);
        if (key == "enabled") return sol::make_object(L, n.enabled);
        if (key == "text") return sol::make_object(L, n.text);
        if (key == "font") return sol::make_object(L, n.font);
        if (key == "font_size") return sol::make_object(L, n.fontSize);
        if (key == "image") return sol::make_object(L, n.image);
        if (key == "value") {
            if (n.kind == UiKind::Toggle) return sol::make_object(L, n.value > 0.5f);
            return sol::make_object(L, n.value);
        }
        if (key == "min") return sol::make_object(L, n.minValue);
        if (key == "max") return sol::make_object(L, n.maxValue);
        if (key == "step") return sol::make_object(L, n.step);
        if (key == "opacity") return sol::make_object(L, n.opacity);
        if (key == "scale") return sol::make_object(L, n.scale);
        if (key == "rotation") return sol::make_object(L, n.rotation);
        if (key == "grow") return sol::make_object(L, n.grow);
        if (key == "columns") return sol::make_object(L, n.columns);
        if (key == "wrap") return sol::make_object(L, n.wrap);
        if (key == "clip") return sol::make_object(L, n.clip);
        if (key == "hovered") return sol::make_object(L, n.hovered);
        if (key == "pressed") return sol::make_object(L, n.pressed);
        if (key == "pos" || key == "position") return sol::make_object(L, Vec2Table(L, n.pos, false));
        if (key == "x") return sol::make_object(L, n.pos.x);
        if (key == "y") return sol::make_object(L, n.pos.y);
        if (key == "scroll") return sol::make_object(L, Vec2Table(L, n.scroll, false));
        if (key == "size") return sol::make_object(L, Vec2Table(L, {n.size.x < 0.0f ? n.rect.w : n.size.x, n.size.y < 0.0f ? n.rect.h : n.size.y}, true));
        if (key == "width" || key == "w") return sol::make_object(L, n.size.x < 0.0f ? n.rect.w : n.size.x);
        if (key == "height" || key == "h") return sol::make_object(L, n.size.y < 0.0f ? n.rect.h : n.size.y);
        if (key == "content_size") return sol::make_object(L, Vec2Table(L, n.contentSize, true));
        if (key == "rect") {
            sol::table t = L.create_table();
            t["x"] = n.rect.x;
            t["y"] = n.rect.y;
            t["w"] = n.rect.w;
            t["h"] = n.rect.h;
            return sol::make_object(L, t);
        }
        if (key == "parent") {
            if (!n.parent) return sol::nil;
            return sol::make_object(L, UiRef{n.parent});
        }
        if (key == "children") {
            sol::table t = L.create_table();
            for (const UiId c : n.children) t.add(UiRef{c});
            return sol::make_object(L, t);
        }
        auto color = [&](const glm::vec4& c) {
            sol::table t = L.create_table();
            t["r"] = c.r;
            t["g"] = c.g;
            t["b"] = c.b;
            t["a"] = c.a;
            return sol::make_object(L, t);
        };
        if (key == "color") {
            switch (ColorMeaning(n.kind)) {
            case ColorTarget::Text: return color(n.style.base.text.value_or(glm::vec4(1.0f)));
            case ColorTarget::Tint: return color(n.style.base.tint.value_or(glm::vec4(1.0f)));
            default: return color(n.style.base.bg.value_or(glm::vec4(0.0f)));
            }
        }
        if (key == "bg") return color(n.style.base.bg.value_or(glm::vec4(0.0f)));
        if (key == "text_color") return color(n.style.base.text.value_or(glm::vec4(1.0f)));
        if (key == "tint") return color(n.style.base.tint.value_or(glm::vec4(1.0f)));
        if (key == "fill") return color(n.style.base.fill.value_or(glm::vec4(1.0f)));
        if (key.size() > 3 && key.compare(0, 3, "on_") == 0) {
            const auto it = callbacks.find(ref.id);
            if (it != callbacks.end()) {
                const auto cb = it->second.find(key);
                if (cb != it->second.end()) return sol::make_object(L, cb->second);
            }
            return sol::nil;
        }
        return sol::nil;
    }

    // ---- building nodes ----------------------------------------------------------------------------------------------------------

    UiRef Build(UiKind kind, const sol::object& spec, bool forceGridColumns = false) {
        const UiId id = ui.Create(kind);
        UiNode& n = *ui.Get(id);
        if (kind == UiKind::Toggle) n.value = 0.0f;
        if (spec.get_type() == sol::type::lua_nil) return {id};
        if (!IsTable(spec)) {
            if (IsString(spec)) {
                n.text = spec.as<std::string>();
                return {id};
            }
            Fail("a node is made from a table: ui." + KindName(kind) + "{ ... }");
        }
        const sol::table t = spec.as<sol::table>();
        (void)forceGridColumns;

        // Order matters in a few places: a named style first, a preset anchor before explicit pivot / pos / size.
        if (t["style"].valid()) SetProperty(n, "style", t["style"]);
        if (t["anchor"].valid()) SetProperty(n, "anchor", t["anchor"]);
        std::vector<UiId> children;
        sol::object initialValue; // applied last: it depends on min, max and step, and Lua visits keys in no fixed order
        for (const auto& kv : t) {
            if (kv.first.get_type() == sol::type::number) {
                if (IsString(kv.second)) {
                    n.text = kv.second.as<std::string>();
                } else if (kv.second.is<UiRef>()) {
                    children.push_back(kv.second.as<UiRef>().id);
                } else if (kv.second.get_type() == sol::type::lua_nil) {
                    continue;
                } else {
                    Fail("an entry of ui." + KindName(kind) + "{} is a child node or a text, not a " + sol::type_name(lua, kv.second.get_type()));
                }
                continue;
            }
            if (!IsString(kv.first)) continue;
            const std::string key = kv.first.as<std::string>();
            if (key == "style" || key == "anchor") continue;
            if (key == "value") {
                initialValue = kv.second;
                continue;
            }
            if (key == "children") {
                if (!IsTable(kv.second)) Fail("children needs a table of nodes");
                for (const auto& c : kv.second.as<sol::table>()) {
                    if (c.second.is<UiRef>()) children.push_back(c.second.as<UiRef>().id);
                    else if (c.second.get_type() != sol::type::lua_nil) Fail("children holds nodes only");
                }
                continue;
            }
            SetProperty(n, key, kv.second);
        }
        if (initialValue.valid() && initialValue.get_type() != sol::type::lua_nil) { // a starting value is not a change: no event
            ui.SetValue(id, n.kind == UiKind::Toggle ? (ToBool(initialValue, "value") ? 1.0f : 0.0f) : ToFloat(initialValue, "value"), false);
        }
        // Children keep the order of the array part (pairs() visits the array part in order, first).
        for (const UiId c : children) {
            if (!ui.Attach(id, c)) Fail("a node cannot be a child of itself or of its descendants");
        }
        return {id};
    }

    // ---- animation ------------------------------------------------------------------------------------------------------------------

    void Animate(const UiRef& ref, const sol::table& t) {
        UiNode& n = Node(ref);
        UiTween base;
        base.node = ref.id;
        base.duration = Field(t, {"duration", "time", "seconds"}, 0).value_or(0.3f);
        base.delay = Field(t, {"delay"}, 0).value_or(0.0f);
        base.loop = t["loop"].get_or(false);
        base.yoyo = t["yoyo"].get_or(false);
        base.removeNode = t["remove"].get_or(false);
        base.hideNode = t["hide"].get_or(false);
        if (t["ease"].valid()) {
            const std::string name = ToString(t["ease"], "ease");
            if (!UiEaseFromName(name, base.ease)) Fail("unknown ease '" + name + "' (linear, in_quad, out_quad, in_out_quad, ... out_back, out_elastic, out_bounce)");
        }
        if (t["on_done"].valid() && t["on_done"].get_type() == sol::type::function) {
            base.token = nextToken++;
            tweenDone[base.token] = sol::protected_function(t["on_done"].get<sol::function>());
        }
        const sol::object fromObj = t["from"];
        const sol::table from = IsTable(fromObj) ? fromObj.as<sol::table>() : lua.create_table();

        std::vector<UiTween> made;
        auto add = [&](const char* key, UiProp prop, auto&& parse) {
            const sol::object to = t[key];
            if (!to.valid() || to.get_type() == sol::type::lua_nil) return;
            UiTween tw = base;
            tw.prop = prop;
            tw.to = parse(to);
            const sol::object f = from[key];
            if (f.valid() && f.get_type() != sol::type::lua_nil) {
                tw.from = parse(f);
                tw.hasFrom = true;
            }
            made.push_back(tw);
        };
        add("opacity", UiProp::Opacity, [](const sol::object& o) { return glm::vec4(ToFloat(o, "opacity")); });
        add("scale", UiProp::Scale, [](const sol::object& o) { return glm::vec4(ToFloat(o, "scale")); });
        add("rotation", UiProp::Rotation, [](const sol::object& o) { return glm::vec4(ToFloat(o, "rotation")); });
        add("pos", UiProp::Pos, [](const sol::object& o) { return glm::vec4(ToVec2(o, "pos"), 0.0f, 0.0f); });
        add("size", UiProp::Size, [](const sol::object& o) { return glm::vec4(ToVec2(o, "size"), 0.0f, 0.0f); });
        add("bg", UiProp::Bg, [](const sol::object& o) { return ToColor(o, "bg"); });
        add("color", UiProp::Bg, [&](const sol::object& o) { return ToColor(o, "color"); });
        add("text_color", UiProp::TextColor, [](const sol::object& o) { return ToColor(o, "text_color"); });
        add("value", UiProp::Value, [](const sol::object& o) { return glm::vec4(ToFloat(o, "value")); });
        add("scroll", UiProp::Scroll, [](const sol::object& o) { return IsNumber(o) ? glm::vec4(0.0f, o.as<float>(), 0.0f, 0.0f) : glm::vec4(ToVec2(o, "scroll"), 0.0f, 0.0f); });
        if (n.kind == UiKind::Text) { // a text's `color` is its text color
            for (UiTween& tw : made) {
                if (tw.prop == UiProp::Bg && t["color"].valid() && !t["bg"].valid()) tw.prop = UiProp::TextColor;
            }
        }
        if (made.empty() && (base.removeNode || base.hideNode)) { // nothing to move: a timer that ends in removing or hiding
            UiTween wait = base;
            wait.prop = UiProp::Opacity;
            wait.hasFrom = true;
            wait.from = wait.to = glm::vec4(n.opacity);
            made.push_back(wait);
        }
        if (made.empty()) Fail("animate needs at least one property to move (opacity, pos, size, scale, rotation, bg, text_color, value, scroll)");
        // Only the first tween carries the done token and the end action; the others finish at the same moment.
        for (size_t i = 1; i < made.size(); ++i) {
            made[i].token = 0;
            made[i].removeNode = false;
            made[i].hideNode = false;
        }
        for (const UiTween& tw : made) ui.Animate(tw);
    }

    // ---- documents ---------------------------------------------------------------------------------------------------------------------

    static int64_t Stamp(const fs::path& file) {
        std::error_code ec;
        const auto t = fs::last_write_time(file, ec);
        return ec ? 0 : static_cast<int64_t>(t.time_since_epoch().count());
    }

    bool RunDocument(const std::string& path, const std::string& tempCanvas, std::string& error) {
        std::ifstream in(root / path, std::ios::binary);
        if (!in) {
            error = path + ": cannot read the document";
            return false;
        }
        std::ostringstream text;
        text << in.rdbuf();
        std::string source = text.str();
        StripUtf8Bom(source);

        sol::environment env(lua, sol::create, lua.globals());
        sol::load_result chunk = lua.load(source, "@" + path, sol::load_mode::text);
        if (!chunk.valid()) {
            const sol::error e = chunk;
            error = e.what();
            return false;
        }
        sol::protected_function run = chunk;
        sol::set_environment(env, run);
        const std::string previousActive = ui.ActiveCanvasName();
        ui.SetActiveCanvas(tempCanvas);
        if (budget) budget();
        const sol::protected_function_result result = run();
        ui.SetActiveCanvas(previousActive);
        if (!result.valid()) {
            const sol::error e = result;
            error = e.what();
            return false;
        }
        // The document returns its root node (or a list of them); anything it made but did not attach is dropped.
        UiCanvas* temp = ui.FindCanvas(tempCanvas);
        if (!temp) {
            error = path + ": the canvas went away while the document ran";
            return false;
        }
        sol::object returned = result.get_type() == sol::type::none ? sol::object(sol::nil) : result.get<sol::object>();
        auto attach = [&](const sol::object& o) {
            if (o.is<UiRef>()) {
                const UiRef r = o.as<UiRef>();
                const UiNode* n = ui.Get(r.id);
                if (n && !n->parent) ui.Attach(temp->root, r.id);
            }
        };
        if (returned.is<UiRef>()) {
            attach(returned);
        } else if (IsTable(returned)) {
            for (const auto& kv : returned.as<sol::table>()) attach(kv.second);
        }
        return true;
    }

    bool Load(const std::string& path, const std::string& canvasName, int order, std::string* errorOut, bool failKeepsCanvas) {
        const std::string tempName = canvasName + "#loading";
        ui.RemoveCanvas(tempName);
        ui.AddCanvas(tempName, order);
        std::string error;
        const bool ok = RunDocument(path, tempName, error);
        UiCanvas* dest = ui.FindCanvas(canvasName);
        if (!ok) {
            ui.RemoveCanvas(tempName);
            Log::Error("[ui {}] {}", path, error);
            if (errorOut) *errorOut = error;
            if (failKeepsCanvas) {
                if (!dest) dest = ui.AddCanvas(canvasName, order);
                dest->document = path; // remembered, so a fix of the file is picked up
                dest->stamp = Stamp(root / path);
            }
            return false;
        }
        if (!dest) dest = ui.AddCanvas(canvasName, order);
        if (UiNode* destRoot = ui.Get(dest->root)) {
            const std::vector<UiId> old = destRoot->children;
            for (const UiId c : old) ui.Destroy(c);
        }
        UiCanvas* temp = ui.FindCanvas(tempName);
        if (temp) {
            if (UiNode* tempRoot = ui.Get(temp->root)) {
                const std::vector<UiId> fresh = tempRoot->children;
                for (const UiId c : fresh) ui.Attach(dest->root, c);
            }
        }
        dest = ui.FindCanvas(canvasName);
        ui.RemoveCanvas(tempName);
        dest = ui.FindCanvas(canvasName);
        if (dest) {
            dest->order = order;
            dest->document = path;
            dest->stamp = Stamp(root / path);
        }
        return true;
    }

    // ---- events -------------------------------------------------------------------------------------------------------------------------

    void Pump(float dt) {
        const std::vector<UiEvent> events = ui.TakeEvents();
        for (const UiEvent& e : events) {
            if (e.type == UiEventType::TweenDone) {
                const auto it = tweenDone.find(e.token);
                if (it != tweenDone.end()) {
                    sol::protected_function fn = it->second;
                    tweenDone.erase(it);
                    Call("on_done", fn, e.node);
                }
                continue;
            }
            const char* key = nullptr;
            switch (e.type) {
            case UiEventType::Click: key = "on_click"; break;
            case UiEventType::Press: key = "on_press"; break;
            case UiEventType::Release: key = "on_release"; break;
            case UiEventType::Enter: key = "on_enter"; break;
            case UiEventType::Leave: key = "on_leave"; break;
            case UiEventType::Change: key = "on_change"; break;
            default: break;
            }
            if (!key) continue;
            const auto node = callbacks.find(e.node);
            if (node == callbacks.end()) continue;
            const auto cb = node->second.find(key);
            if (cb == node->second.end()) continue;
            sol::protected_function fn = cb->second;
            const UiNode* n = ui.Get(e.node);
            const std::string what = std::string(key) + " of '" + (n ? n->name : std::string("?")) + "'";
            if (e.type == UiEventType::Change) {
                if (n && n->kind == UiKind::Toggle) Call(what, fn, e.node, e.value > 0.5f);
                else Call(what, fn, e.node, e.value);
            } else if (e.type == UiEventType::Click && n && n->kind == UiKind::Toggle) {
                Call(what, fn, e.node, e.value > 0.5f);
            } else {
                Call(what, fn, e.node);
            }
            if (!fn.valid()) callbacks[e.node].erase(key); // it failed: gone until the next assignment
        }

        // on_update of every node that has one.
        std::vector<UiId> updating;
        for (auto it = callbacks.begin(); it != callbacks.end();) {
            if (!ui.Exists(it->first)) {
                it = callbacks.erase(it); // the node is gone
                continue;
            }
            if (it->second.contains("on_update")) updating.push_back(it->first);
            ++it;
        }
        for (const UiId id : updating) {
            const UiNode* n = ui.Get(id);
            if (!n || !n->visible) continue;
            const auto it = callbacks.find(id);
            if (it == callbacks.end()) continue;
            const auto cb = it->second.find("on_update");
            if (cb == it->second.end()) continue;
            sol::protected_function fn = cb->second;
            Call("on_update of '" + n->name + "'", fn, id, dt);
            if (!fn.valid()) {
                const auto again = callbacks.find(id);
                if (again != callbacks.end()) again->second.erase("on_update");
            }
        }
    }

    void Bind();
    void BindMethods();
};

// ---- binding --------------------------------------------------------------------------------------------------------------------------

void UiScripting::Impl::BindMethods() {
    methods = lua.create_table();
    auto children = [this](const UiRef& self, sol::variadic_args args) {
        UiNode& n = Node(self);
        auto addOne = [&](const sol::object& o) {
            if (!o.is<UiRef>()) Fail("add needs nodes");
            if (!ui.Attach(n.id, o.as<UiRef>().id)) Fail("a node cannot be added to itself or to its own descendants");
        };
        for (const sol::object o : args) {
            if (IsTable(o)) {
                for (const auto& kv : o.as<sol::table>()) addOne(kv.second);
            } else {
                addOne(o);
            }
        }
        return self;
    };
    methods["add"] = children;
    methods["remove"] = [this](const UiRef& self, const UiRef& child) {
        const UiNode& n = Node(self);
        if (ui.Get(child.id) && ui.Get(child.id)->parent == n.id) ui.Detach(child.id);
    };
    methods["clear"] = [this](const UiRef& self) {
        const std::vector<UiId> kids = Node(self).children;
        for (const UiId c : kids) ui.Destroy(c);
    };
    methods["destroy"] = [this](const UiRef& self) { ui.Destroy(self.id); };
    methods["find"] = [this](const UiRef& self, const std::string& name) -> sol::optional<UiRef> {
        const UiId found = ui.Find(name, Node(self).id);
        if (!found) return sol::nullopt;
        return UiRef{found};
    };
    methods["animate"] = [this](const UiRef& self, const sol::table& t) { Animate(self, t); };
    methods["stop"] = [this](const UiRef& self) { ui.StopTweens(self.id); };
    methods["show"] = [this](const UiRef& self) {
        UiNode& n = Node(self);
        n.visible = true;
        if (!n.parent) ui.Attach(ActiveRoot(), n.id);
    };
    methods["hide"] = [this](const UiRef& self) { Node(self).visible = false; };
    methods["to_front"] = [this](const UiRef& self) {
        UiNode& n = Node(self);
        if (n.parent) ui.Attach(n.parent, n.id);
    };
    methods["to_back"] = [this](const UiRef& self) {
        UiNode& n = Node(self);
        if (n.parent) ui.Attach(n.parent, n.id, 0);
    };
    methods["set_style"] = [this](const UiRef& self, const std::string& name) { SetAlignedStyle(Node(self), name); };
    methods["pixel_rect"] = [this](const UiRef& self) {
        Node(self);
        const UiRect r = ui.PixelRect(self.id);
        return std::make_tuple(r.x, r.y, r.w, r.h);
    };
    methods["scroll_to"] = [this](const UiRef& self, float y) { ui.SetScroll(self.id, {0.0f, y}); };
    methods["click"] = [this](const UiRef& self) { // what a click would do (tests, keyboard shortcuts)
        UiNode& n = Node(self);
        if (!n.worldEnabled) return;
        if (n.kind == UiKind::Toggle) ui.SetValue(n.id, n.value > 0.5f ? 0.0f : 1.0f, true);
        ui.RaiseClick(n.id);
    };
    methods["on"] = [this](const UiRef& self, const std::string& event, const sol::object& fn) {
        UiNode& n = Node(self);
        SetProperty(n, event.compare(0, 3, "on_") == 0 ? event : "on_" + event, fn);
    };
}

void UiScripting::Impl::Bind() {
    if (bound) return;
    bound = true;
    BindMethods();

    lua.new_usertype<UiRef>(
        "UiNode", sol::no_constructor,
        sol::meta_function::index,
        [this](const UiRef& ref, const sol::stack_object& key, sol::this_state ts) -> sol::object {
            if (key.get_type() != sol::type::string) return sol::nil;
            const std::string k = key.as<std::string>();
            const sol::object method = methods[k];
            if (method.valid() && method.get_type() != sol::type::lua_nil) return method;
            return GetProperty(ref, k, ts);
        },
        sol::meta_function::new_index,
        [this](const UiRef& ref, const sol::stack_object& key, const sol::stack_object& value) {
            if (key.get_type() != sol::type::string) Fail("node properties are named by strings");
            UiNode* n = ui.Get(ref.id);
            if (!n) return; // writes to a destroyed node do nothing
            SetProperty(*n, key.as<std::string>(), sol::object(value));
        },
        sol::meta_function::equal_to, [](const UiRef& a, const UiRef& b) { return a.id == b.id; },
        sol::meta_function::to_string, [this](const UiRef& r) {
            const UiNode* n = ui.Get(r.id);
            return n ? "ui." + KindName(n->kind) + "('" + n->name + "')" : std::string("ui.node(destroyed)");
        });

    sol::table t = lua.create_named_table("ui");
    auto ctor = [this, &t](const char* name, UiKind kind) {
        t[name] = [this, kind](sol::optional<sol::object> spec) { return Build(kind, spec.value_or(sol::object(sol::nil))); };
    };
    ctor("panel", UiKind::Panel);
    ctor("image", UiKind::Image);
    ctor("text", UiKind::Text);
    ctor("label", UiKind::Text);
    ctor("button", UiKind::Button);
    ctor("toggle", UiKind::Toggle);
    ctor("slider", UiKind::Slider);
    ctor("progress", UiKind::Progress);
    ctor("scroll", UiKind::Scroll);
    ctor("spacer", UiKind::Panel);
    auto layoutCtor = [this, &t](const char* name, UiLayout layout) {
        t[name] = [this, layout](sol::optional<sol::object> spec) {
            const UiRef r = Build(UiKind::Panel, spec.value_or(sol::object(sol::nil)));
            UiNode& n = *ui.Get(r.id);
            const bool given = IsTable(spec.value_or(sol::object(sol::nil))) && spec->as<sol::table>()["layout"].valid();
            if (!given) n.layout = layout;
            return r;
        };
    };
    layoutCtor("row", UiLayout::Row);
    layoutCtor("column", UiLayout::Column);
    layoutCtor("grid", UiLayout::Grid);
    t["spacer"] = [this](sol::optional<sol::object> spec) {
        const UiRef r = Build(UiKind::Panel, spec.value_or(sol::object(sol::nil)));
        UiNode& n = *ui.Get(r.id);
        if (n.grow == 0.0f && n.size.x < 0.0f && n.size.y < 0.0f) n.grow = 1.0f; // a spacer pushes its neighbours apart
        return r;
    };
    t["node"] = [this](const std::string& kind, sol::optional<sol::object> spec) {
        static const std::map<std::string, UiKind> kinds = {{"panel", UiKind::Panel},   {"image", UiKind::Image},   {"text", UiKind::Text},
                                                             {"button", UiKind::Button}, {"toggle", UiKind::Toggle}, {"slider", UiKind::Slider},
                                                             {"progress", UiKind::Progress}, {"scroll", UiKind::Scroll}};
        const auto it = kinds.find(Lower(kind));
        if (it == kinds.end()) Fail("unknown node kind '" + kind + "'");
        return Build(it->second, spec.value_or(sol::object(sol::nil)));
    };

    t["show"] = [this](const UiRef& node) {
        UiNode& n = Node(node);
        n.visible = true;
        if (!n.parent) ui.Attach(ActiveRoot(), n.id);
        return node;
    };
    t["hide"] = [this](const UiRef& node) { Node(node).visible = false; };
    t["destroy"] = [this](const UiRef& node) { ui.Destroy(node.id); };
    t["root"] = [this]() { return UiRef{ActiveRoot()}; };
    t["clear"] = [this]() {
        if (UiNode* root = ui.Get(ActiveRoot())) {
            const std::vector<UiId> kids = root->children;
            for (const UiId c : kids) ui.Destroy(c);
        }
    };
    t["canvas"] = [this](const std::string& name, sol::optional<sol::table> options) {
        UiCanvas* canvas = ui.FindCanvas(name);
        if (!canvas) canvas = ui.AddCanvas(name, 0);
        if (options) {
            const sol::table o = *options;
            if (o["order"].valid()) canvas->order = static_cast<int>(ToFloat(o["order"], "order"));
            if (o["scale"].valid()) canvas->scaleMode = ToScaleMode(ToString(o["scale"], "scale"));
            if (o["reference"].valid()) canvas->reference = ToVec2(o["reference"], "reference");
            if (o["visible"].valid()) canvas->visible = ToBool(o["visible"], "visible");
            ui.AddCanvas(name, canvas->order); // re-sort by order
            canvas = ui.FindCanvas(name);
        }
        ui.SetActiveCanvas(name);
        return UiRef{canvas->root};
    };
    t["set_canvas_visible"] = [this](const std::string& name, bool visible) {
        if (UiCanvas* c = ui.FindCanvas(name)) c->visible = visible;
    };
    t["screen"] = [this]() {
        UiCanvas* canvas = ui.FindCanvas(ui.ActiveCanvasName());
        if (!canvas) canvas = ui.AddCanvas(ui.ActiveCanvasName(), 0);
        return std::make_tuple(canvas->size.x, canvas->size.y);
    };
    t["pixel_size"] = [this]() { return std::make_tuple(ui.ViewSize().x, ui.ViewSize().y); };
    t["pointer_over"] = [this]() { return ui.PointerOverUi(); };
    t["hovered"] = [this]() -> sol::optional<UiRef> {
        if (!ui.HoveredNode()) return sol::nullopt;
        return UiRef{ui.HoveredNode()};
    };
    t["find"] = [this](const std::string& name) -> sol::optional<UiRef> {
        const UiId id = ui.Find(name);
        if (!id) return sol::nullopt;
        return UiRef{id};
    };
    t["find_all"] = [this](const std::string& prefix, sol::this_state ts) {
        sol::table out = sol::state_view(ts).create_table();
        for (const UiId id : ui.FindAll(prefix)) out.add(UiRef{id});
        return out;
    };
    t["style"] = [this](const std::string& name, const sol::table& def) {
        NamedStyle named;
        UiKind probe = UiKind::Button; // `color` in a named style means the background, like for a button
        for (const auto& kv : def) {
            if (!IsString(kv.first)) continue;
            const std::string key = kv.first.as<std::string>();
            if (key == "hover") named.style.hover = ParseLook(probe, kv.second, "hover");
            else if (key == "pressed") named.style.pressed = ParseLook(probe, kv.second, "pressed");
            else if (key == "disabled") named.style.disabled = ParseLook(probe, kv.second, "disabled");
            else if (key == "active") named.style.active = ParseLook(probe, kv.second, "active");
            else if (key == "shadow") {
                named.style.shadow = ParseShadow(kv.second, "shadow");
                named.hasShadow = true;
            } else if (key == "transition") {
                named.style.transition = std::max(ToFloat(kv.second, "transition"), 0.0f);
                named.hasTransition = true;
            } else if (!SetLookField(named.style.base, probe, key, kv.second)) {
                Fail("a style has no field '" + key + "'");
            }
        }
        styles[name] = named;
    };
    t["load"] = [this](const std::string& path, sol::optional<std::string> canvasName, sol::optional<int> order) -> sol::optional<UiRef> {
        std::string name = canvasName.value_or(fs::path(path).stem().string());
        const UiCanvas* existing = ui.FindCanvas(name);
        const int canvasOrder = order.value_or(existing ? existing->order : 0);
        if (!Load(path, name, canvasOrder, nullptr, false)) return sol::nullopt;
        ui.SetActiveCanvas(name);
        return UiRef{ui.FindCanvas(name)->root};
    };
    t["ease_names"] = [this](sol::this_state ts) {
        sol::table out = sol::state_view(ts).create_table();
        for (int i = 0; i < static_cast<int>(UiEase::Count); ++i) out.add(UiEaseName(static_cast<UiEase>(i)));
        return out;
    };

    // Conveniences written in Lua itself.
    lua.script(R"LUA(
        function ui.toast(text, opts)
            opts = opts or {}
            local box = ui.panel{ anchor = opts.anchor or "bottom", pos = { 0, opts.y or -56 }, padding = { 16, 9 },
                bg = opts.bg or "#101218e6", radius = 6, opacity = 0,
                ui.text{ text = text, font_size = opts.size or 18, color = opts.color or "#f2f4f8" } }
            ui.show(box)
            box:animate{ opacity = 1, duration = 0.18, on_done = function()
                box:animate{ opacity = 0, duration = 0.4, delay = opts.seconds or 2.0, remove = true }
            end }
            return box
        end
        function ui.fade_in(node, seconds)
            node.opacity = 0
            node:show()
            node:animate{ opacity = 1, duration = seconds or 0.25 }
        end
        function ui.fade_out(node, seconds)
            node:animate{ opacity = 0, duration = seconds or 0.25, hide = true }
        end
    )LUA");
}

// ---- public ----------------------------------------------------------------------------------------------------------------------------

UiScripting::UiScripting(lua_State* L, UiSystem& system, fs::path projectRoot, std::function<void()> resetBudget)
    : m_impl(std::make_unique<Impl>(L, system, std::move(projectRoot), std::move(resetBudget))) {}

UiScripting::~UiScripting() = default;

void UiScripting::Bind() { m_impl->Bind(); }

void UiScripting::Reset() {
    m_impl->callbacks.clear();
    m_impl->tweenDone.clear();
    m_impl->styles.clear();
}

bool UiScripting::LoadDocument(const std::string& path, const std::string& canvasName, int order, std::string* error) {
    return m_impl->Load(path, canvasName, order, error, true);
}

void UiScripting::Pump(float dt) { m_impl->Pump(dt); }

void UiScripting::ReloadChanged() {
    Impl& g = *m_impl;
    // Collect first: loading changes the canvas list.
    struct Item {
        std::string name, document;
        int order;
    };
    std::vector<Item> due;
    for (const UiCanvas& c : g.ui.Canvases()) {
        if (c.document.empty() || c.name.find('#') != std::string::npos) continue;
        if (Impl::Stamp(g.root / c.document) != c.stamp) due.push_back({c.name, c.document, c.order});
    }
    for (const Item& item : due) {
        if (g.Load(item.document, item.name, item.order, nullptr, true)) Log::Info("[ui {}] reloaded", item.document);
    }
}

void UiScripting::SyncSceneCanvases(Scene& scene, float dt) {
    Impl& g = *m_impl;
    std::set<std::string> alive;
    for (auto [e, id, canvas] : scene.Registry().view<IdComponent, CanvasComponent>().each()) {
        const std::string name = "scene:" + std::to_string(id.id);
        alive.insert(name);
        UiCanvas* c = g.ui.FindCanvas(name);
        if (!c || (c->document != canvas.document && !canvas.document.empty())) {
            if (canvas.document.empty()) {
                if (!c) c = g.ui.AddCanvas(name, canvas.order);
            } else {
                g.Load(canvas.document, name, canvas.order, nullptr, true);
            }
            c = g.ui.FindCanvas(name);
            if (c) {
                c->fromScene = true;
                c->actor = id.id;
            }
        }
        if (!c) continue;
        if (canvas.document.empty() && !c->document.empty()) { // the document was cleared: show nothing
            if (UiNode* r = g.ui.Get(c->root)) {
                const std::vector<UiId> kids = r->children;
                for (const UiId k : kids) g.ui.Destroy(k);
            }
            c->document.clear();
        }
        if (c->order != canvas.order) g.ui.AddCanvas(name, canvas.order);
        c = g.ui.FindCanvas(name);
        c->visible = canvas.visible;
        c->scaleMode = static_cast<UiScaleMode>(canvas.scaleMode);
        c->reference = {canvas.referenceWidth, canvas.referenceHeight};
    }
    std::vector<std::string> gone;
    for (const UiCanvas& c : g.ui.Canvases()) {
        if (c.fromScene && !alive.contains(c.name)) gone.push_back(c.name);
    }
    for (const std::string& name : gone) g.ui.RemoveCanvas(name);

    g.sinceCheck += dt;
    if (g.sinceCheck > 0.5) {
        g.sinceCheck = 0.0;
        ReloadChanged();
    }
}

} // namespace magnum
