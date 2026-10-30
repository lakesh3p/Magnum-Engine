#include "shader/ShaderProgram.h"

#include "core/Log.h"

#include <glad/gl.h>

#include <regex>
#include <sstream>

namespace magnum {

namespace {

std::string Trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
}

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) lines.push_back(line);
    return lines;
}

// Turns a driver info log into diagnostics on the .lua file.
// Handles the NVIDIA ("0(12) : error ...") and Mesa/Intel/AMD ("0:12(3): error: ...", "ERROR: 0:12: ...") formats.
std::vector<ShaderDiagnostic> ParseGlslLog(const std::string& log, const char* stage, const std::string& glsl,
                                           const std::vector<int>& lineMap, const std::string& luaFile) {
    static const std::regex nvidia(R"(^\d+\((\d+)\)\s*:\s*(.*)$)");
    static const std::regex mesa(R"(^\d+:(\d+)\(\d+\):\s*(.*)$)");
    static const std::regex generic(R"(^(?:ERROR|WARNING):\s*\d+:(\d+):\s*(.*)$)");

    const std::vector<std::string> glslLines = SplitLines(glsl);
    std::vector<ShaderDiagnostic> out;
    for (const std::string& raw : SplitLines(log)) {
        const std::string line = Trim(raw);
        if (line.empty()) continue;

        std::smatch m;
        ShaderDiagnostic d;
        d.file = luaFile;
        if (std::regex_match(line, m, nvidia) || std::regex_match(line, m, mesa) ||
            std::regex_match(line, m, generic)) {
            const int glslLine = std::stoi(m[1]);
            const size_t idx = static_cast<size_t>(glslLine - 1);
            d.line = idx < lineMap.size() ? lineMap[idx] : 0;
            d.message = std::string("GLSL ") + stage + ": " + m[2].str();
            if (idx < glslLines.size()) {
                d.message += "  [generated line " + std::to_string(glslLine) + ": " + Trim(glslLines[idx]) + "]";
            }
        } else {
            d.message = std::string("GLSL ") + stage + ": " + line;
        }
        out.push_back(std::move(d));
    }
    return out;
}

std::string InfoLog(GLuint object, bool isProgram) {
    GLint length = 0;
    if (isProgram) glGetProgramiv(object, GL_INFO_LOG_LENGTH, &length);
    else glGetShaderiv(object, GL_INFO_LOG_LENGTH, &length);
    if (length <= 1) return {};
    std::string log(static_cast<size_t>(length), '\0');
    if (isProgram) glGetProgramInfoLog(object, length, nullptr, log.data());
    else glGetShaderInfoLog(object, length, nullptr, log.data());
    log.resize(static_cast<size_t>(length - 1));
    return log;
}

GLuint CompileStage(GLenum type, const char* stageName, const std::string& glsl, const std::vector<int>& lineMap,
                    const std::string& luaFile, std::vector<ShaderDiagnostic>& diagnostics) {
    const GLuint shader = glCreateShader(type);
    const char* text = glsl.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    const std::string log = InfoLog(shader, false);
    if (!ok) {
        auto diags = ParseGlslLog(log, stageName, glsl, lineMap, luaFile);
        if (diags.empty()) diags.push_back({luaFile, 0, std::string("GLSL ") + stageName + ": compile failed"});
        diagnostics.insert(diagnostics.end(), diags.begin(), diags.end());
        glDeleteShader(shader);
        return 0;
    }
    for (const auto& w : ParseGlslLog(log, stageName, glsl, lineMap, luaFile)) {
        Log::Warn("{}", w.ToString());
    }
    return shader;
}

} // namespace

ShaderProgram::~ShaderProgram() {
    if (m_program) glDeleteProgram(m_program);
}

std::unique_ptr<ShaderProgram> ShaderProgram::CreateInstanced(const CompiledShaderSource& source,
                                                              std::vector<ShaderDiagnostic>& diagnostics) {
    // The generated vertex source starts with its #version line; MAGNUM_INSTANCED goes right after it. The line map
    // gets one extra entry so the lines after it still point at the same .lua lines.
    const std::string version = "#version 450 core\n";
    if (source.vertexGlsl.compare(0, version.size(), version) != 0 || source.vertexLineMap.empty()) return nullptr;
    CompiledShaderSource instanced = source;
    instanced.vertexGlsl.insert(version.size(), "#define MAGNUM_INSTANCED 1\n");
    instanced.vertexLineMap.insert(instanced.vertexLineMap.begin() + 1, instanced.vertexLineMap.front());
    return Create(instanced, diagnostics);
}

std::unique_ptr<ShaderProgram> ShaderProgram::Create(const CompiledShaderSource& source,
                                                     std::vector<ShaderDiagnostic>& diagnostics) {
    const GLuint vs = CompileStage(GL_VERTEX_SHADER, "vertex", source.vertexGlsl, source.vertexLineMap,
                                   source.luaFile, diagnostics);
    const GLuint fs = CompileStage(GL_FRAGMENT_SHADER, "fragment", source.fragmentGlsl, source.fragmentLineMap,
                                   source.luaFile, diagnostics);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return nullptr;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        diagnostics.push_back({source.luaFile, 0, "GLSL link: " + Trim(InfoLog(program, true))});
        glDeleteProgram(program);
        return nullptr;
    }

    std::unique_ptr<ShaderProgram> result(new ShaderProgram());
    result->m_program = program;
    return result;
}

void ShaderProgram::Use() const {
    glUseProgram(m_program);
}

void ShaderProgram::SetFloat(const char* uniform, float v) const {
    glProgramUniform1f(m_program, glGetUniformLocation(m_program, uniform), v);
}

void ShaderProgram::SetVec2(const char* uniform, float x, float y) const {
    glProgramUniform2f(m_program, glGetUniformLocation(m_program, uniform), x, y);
}

void ShaderProgram::SetVec3(const char* uniform, float x, float y, float z) const {
    glProgramUniform3f(m_program, glGetUniformLocation(m_program, uniform), x, y, z);
}

void ShaderProgram::SetMat3(const char* uniform, const float* m) const {
    glProgramUniformMatrix3fv(m_program, glGetUniformLocation(m_program, uniform), 1, GL_FALSE, m);
}

void ShaderProgram::SetMat4(const char* uniform, const float* m) const {
    glProgramUniformMatrix4fv(m_program, glGetUniformLocation(m_program, uniform), 1, GL_FALSE, m);
}

void ShaderProgram::SetInt(const char* uniform, int v) const {
    glProgramUniform1i(m_program, glGetUniformLocation(m_program, uniform), v);
}

void ShaderProgram::SetParamValue(const ShaderParam& p, const std::array<float, 4>& value) const {
    const GLint loc = glGetUniformLocation(m_program, p.uniform.c_str());
    if (loc < 0) return; // unused by the shader, optimized away
    const float* v = value.data();
    switch (p.components) {
    case 1: glProgramUniform1f(m_program, loc, v[0]); break;
    case 2: glProgramUniform2f(m_program, loc, v[0], v[1]); break;
    case 3: glProgramUniform3f(m_program, loc, v[0], v[1], v[2]); break;
    default: glProgramUniform4f(m_program, loc, v[0], v[1], v[2], v[3]); break;
    }
}

} // namespace magnum
