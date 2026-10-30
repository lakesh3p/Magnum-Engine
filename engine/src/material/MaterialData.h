#pragma once

#include "shader/ShaderTypes.h"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace magnum {

// One parameter override inside a material: a number/vector (1-4 components) or a texture path.
struct MaterialValue {
    std::string name;
    int components = 0; // 0 = texture
    std::array<float, 4> number{};
    std::string texture;

    bool IsTexture() const { return components == 0; }
    bool operator==(const MaterialValue&) const = default;
};

// The contents of a material file: which shader to use and the parameters that differ from the shader's defaults.
struct MaterialData {
    std::string shader;
    std::vector<MaterialValue> params;

    const MaterialValue* Find(const std::string& name) const;
    void SetNumber(const std::string& name, int components, const std::array<float, 4>& value);
    void SetTexture(const std::string& name, const std::string& path);
    void Remove(const std::string& name);
    bool operator==(const MaterialData&) const = default;
};

// Text of the material file for `data`. Parameters are written sorted by name so saves are stable.
std::string FormatMaterial(const MaterialData& data);

// Reads a material script:  return material { shader = "...", params = { name = 1.0, tint = {1, 0, 0}, map = "path" } }
// Runs in a sandbox without file or OS access.
bool LoadMaterialFile(const std::filesystem::path& file, const std::string& displayName, MaterialData& out,
                      std::vector<ShaderDiagnostic>& diagnostics);

} // namespace magnum
