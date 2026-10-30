#include "material/MaterialData.h"

#include <algorithm>
#include <cstdio>

namespace magnum {

const MaterialValue* MaterialData::Find(const std::string& name) const {
    for (const MaterialValue& v : params) {
        if (v.name == name) return &v;
    }
    return nullptr;
}

void MaterialData::SetNumber(const std::string& name, int components, const std::array<float, 4>& value) {
    Remove(name);
    MaterialValue v;
    v.name = name;
    v.components = components;
    v.number = value;
    params.push_back(std::move(v));
}

void MaterialData::SetTexture(const std::string& name, const std::string& path) {
    Remove(name);
    MaterialValue v;
    v.name = name;
    v.components = 0;
    v.texture = path;
    params.push_back(std::move(v));
}

void MaterialData::Remove(const std::string& name) {
    params.erase(std::remove_if(params.begin(), params.end(), [&](const MaterialValue& v) { return v.name == name; }),
                 params.end());
}

namespace {

std::string Number(float f) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.7g", static_cast<double>(f));
    return buffer;
}

std::string Quote(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

} // namespace

std::string FormatMaterial(const MaterialData& data) {
    std::vector<const MaterialValue*> sorted;
    for (const MaterialValue& v : data.params) sorted.push_back(&v);
    std::sort(sorted.begin(), sorted.end(), [](const MaterialValue* a, const MaterialValue* b) { return a->name < b->name; });

    std::string out =
        "-- Magnum material. Edit by hand or in the editor (the editor rewrites this file when parameters change).\n";
    out += "return material {\n";
    out += "    shader = " + Quote(data.shader) + ",\n";
    if (sorted.empty()) {
        out += "    params = {},\n";
    } else {
        out += "    params = {\n";
        for (const MaterialValue* v : sorted) {
            out += "        " + v->name + " = ";
            if (v->IsTexture()) {
                out += Quote(v->texture);
            } else if (v->components == 1) {
                out += Number(v->number[0]);
            } else {
                out += "{ ";
                for (int i = 0; i < v->components; ++i) {
                    out += (i ? ", " : "") + Number(v->number[static_cast<size_t>(i)]);
                }
                out += " }";
            }
            out += ",\n";
        }
        out += "    },\n";
    }
    out += "}\n";
    return out;
}

} // namespace magnum
