#pragma once

#include "shader/ShaderTypes.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace magnum {

// A linked GL program. GLSL compile errors are mapped back to lines of the .lua source.
class ShaderProgram {
public:
    ~ShaderProgram();
    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;

    // The same program with MAGNUM_INSTANCED defined: model and normal_matrix come from instance attributes.
    // Returns nullptr when the source cannot be changed that way (the caller then draws one actor at a time).
    static std::unique_ptr<ShaderProgram> CreateInstanced(const CompiledShaderSource& source,
                                                          std::vector<ShaderDiagnostic>& diagnostics);
    // Returns nullptr and fills diagnostics on failure.
    static std::unique_ptr<ShaderProgram> Create(const CompiledShaderSource& source,
                                                 std::vector<ShaderDiagnostic>& diagnostics);

    void Use() const;
    void SetFloat(const char* uniform, float v) const;
    void SetVec2(const char* uniform, float x, float y) const;
    void SetVec3(const char* uniform, float x, float y, float z) const;
    void SetMat3(const char* uniform, const float* columnMajor9) const;
    void SetMat4(const char* uniform, const float* columnMajor16) const;
    void SetInt(const char* uniform, int v) const;
    // Uploads a numeric parameter with the given value (components as declared by the parameter).
    void SetParamValue(const ShaderParam& param, const std::array<float, 4>& value) const;

private:
    ShaderProgram() = default;
    uint32_t m_program = 0;
};

} // namespace magnum
